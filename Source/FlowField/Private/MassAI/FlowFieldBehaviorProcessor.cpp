#include "MassAI/FlowFieldBehaviorProcessor.h"
#include "MassAI/FlowFieldAgentFragment.h"
#include "FlowFieldTargetComponent.h"
#include "FlowFieldObstacleComponent.h"
#include "FlowFieldSubsystem.h"
#include "FlowFieldActor.h"
#include "MassExecutionContext.h"
#include "MassEntityManager.h"
#include "MassCommonFragments.h"
#include "MassActorSubsystem.h"
#include "DrawDebugHelpers.h"

UFlowFieldBehaviorProcessor::UFlowFieldBehaviorProcessor()
{
    ProcessingPhase = EMassProcessingPhase::PostPhysics;
    // MovementProcessor 执行在前（同 Group），本处理器在其后
    ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Movement;
    ExecutionOrder.ExecuteAfter.Add(TEXT("FlowFieldMovementProcessor"));
    ExecutionFlags = (int32)(EProcessorExecutionFlags::Server | EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
    bAutoRegisterWithProcessingPhases = true;
    // 必须在游戏线程执行，因为要调用 UObject 上的 BlueprintNativeEvent
    bRequiresGameThreadExecution = true;
}

void UFlowFieldBehaviorProcessor::ConfigureQueries(
    const TSharedRef<FMassEntityManager>& EntityManager)
{
    EntityQuery.Initialize(EntityManager);
    EntityQuery.AddTagRequirement<FFlowFieldAgentTag>(EMassFragmentPresence::All);
    EntityQuery.AddTagRequirement<FFlowFieldDeadTag>(EMassFragmentPresence::None);
    EntityQuery.AddRequirement<FFlowFieldAgentFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
    // FMassActorFragment 仅非 VAT 路径有，Optional 兼容两种路径
    // ReadWrite 以便调用 GetMutableFragmentView（实际不修改 Fragment 内容）
    EntityQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
    EntityQuery.RegisterWithProcessor(*this);
    RegisterQuery(EntityQuery);
}

void UFlowFieldBehaviorProcessor::Execute(
    FMassEntityManager& EntityManager,
    FMassExecutionContext& Context)
{
    UWorld* World = GetWorld();
    if (!World) return;

    UFlowFieldSubsystem* FlowSub = World->GetSubsystem<UFlowFieldSubsystem>();
    if (!FlowSub) return;

    AFlowFieldActor* FlowActor = FlowSub->GetActor();
    if (!FlowActor) return;

    const float DeltaTime = Context.GetDeltaTimeSeconds();
    if (DeltaTime <= 0.f) return;

    // 快照：目标组件列表（只读，无需锁，仅游戏线程访问）
    const TArray<FFlowFieldTrackedTarget>& TrackedTargets = FlowActor->GetTrackedTargets();
    const TArray<TWeakObjectPtr<UFlowFieldObstacleComponent>>& RegObstacles = FlowActor->GetRegisteredObstacles();

    // 调试绘制：预取相机位置（距离剔除用）
    const bool bDrawAgents = FlowActor->bDrawAgentRanges;
    const float DrawDistSq = FMath::Square(FlowActor->DebugDrawDistance);
    FVector CamPos = FVector::ZeroVector;
    if (bDrawAgents)
    {
        if (APlayerController* PC = World->GetFirstPlayerController())
            if (PC->PlayerCameraManager)
                CamPos = PC->PlayerCameraManager->GetCameraLocation();
    }

    EntityQuery.ForEachEntityChunk(Context,
        [&](FMassExecutionContext& ChunkContext)
        {
            auto Agents     = ChunkContext.GetMutableFragmentView<FFlowFieldAgentFragment>();
            auto Transforms = ChunkContext.GetFragmentView<FTransformFragment>();
            const int32 Num = ChunkContext.GetNumEntities();
            const TConstArrayView<FMassEntityHandle> Entities = ChunkContext.GetEntities();

            // Optional FMassActorFragment（VAT 路径无此 Fragment）
            const bool bHasActorFrag = ChunkContext.DoesArchetypeHaveFragment<FMassActorFragment>();
            TArrayView<FMassActorFragment> ActorView;
            if (bHasActorFrag)
                ActorView = ChunkContext.GetMutableFragmentView<FMassActorFragment>();

            for (int32 i = 0; i < Num; i++)
            {
                FFlowFieldAgentFragment& Agent = Agents[i];

                // AI Actor 引用（VAT 路径无 Actor，传 nullptr）
                AActor* AIActor = (bHasActorFrag && ActorView.IsValidIndex(i))
                    ? ActorView[i].GetMutable() : nullptr;
                const int32 EntityId = Entities[i].Index;

                // ── 目标侧事件 ────────────────────────────────────
                UFlowFieldTargetComponent* TargetComp = nullptr;
                if (Agent.TargetIndex >= 0 && Agent.TargetIndex < TrackedTargets.Num())
                    TargetComp = TrackedTargets[Agent.TargetIndex].OwnerComp.Get();

                // 物理进入感知范围（bInChaseRange: false→true）
                if (Agent.bInChaseRange && !Agent.bWasInChaseRange && TargetComp)
                {
                    TargetComp->OnAIEnterRange(AIActor, EntityId);
                    TargetComp->OnAIEnterRangeDelegate.Broadcast(AIActor, EntityId);
                }
                // 物理离开感知范围（bInChaseRange: true→false），遗忘期仍在追踪
                else if (!Agent.bInChaseRange && Agent.bWasInChaseRange && TargetComp)
                {
                    TargetComp->OnAIExitRange(AIActor, EntityId);
                    TargetComp->OnAIExitRangeDelegate.Broadcast(AIActor, EntityId);
                }

                // ── 遗忘计时 ─────────────────────────────────────
                if (Agent.bChasingTarget && !Agent.bInChaseRange)
                {
                    // 出范围但仍在追踪：ForgetTime=0 立即遗忘，否则计时
                    if (Agent.ForgetTime <= 0.f)
                    {
                        Agent.bChasingTarget = false;
                        Agent.ForgetTimer    = 0.f;
                        if (TargetComp)
                        {
                            TargetComp->OnAIForgotTarget(AIActor, EntityId);
                            TargetComp->OnAIForgotTargetDelegate.Broadcast(AIActor, EntityId);
                        }
                    }
                    else
                    {
                        Agent.ForgetTimer += DeltaTime;
                        if (Agent.ForgetTimer >= Agent.ForgetTime)
                        {
                            Agent.ForgetTimer    = 0.f;
                            Agent.bChasingTarget = false;
                            if (TargetComp)
                            {
                                TargetComp->OnAIForgotTarget(AIActor, EntityId);
                                TargetComp->OnAIForgotTargetDelegate.Broadcast(AIActor, EntityId);
                            }
                        }
                    }
                }

                // Enter / Exit AttackRange + 攻击计时（仅在感知范围内时）
                if (Agent.bChasingTarget)
                {
                    if (Agent.bInAttackRange && !Agent.bWasInAttackRange && TargetComp)
                    {
                        TargetComp->OnAIEnterAttackRange(AIActor, EntityId);
                        TargetComp->OnAIEnterAttackRangeDelegate.Broadcast(AIActor, EntityId);
                        // 进入即打：预填满计时器，下次计时循环立即触发第一次攻击
                        Agent.AttackTimer = Agent.AttackInterval;
                    }
                    else if (!Agent.bInAttackRange && Agent.bWasInAttackRange && TargetComp)
                    {
                        TargetComp->OnAIExitAttackRange(AIActor, EntityId);
                        TargetComp->OnAIExitAttackRangeDelegate.Broadcast(AIActor, EntityId);
                    }

                    if (Agent.bInAttackRange && TargetComp)
                    {
                        Agent.AttackTimer += DeltaTime;
                        if (Agent.AttackTimer >= Agent.AttackInterval)
                        {
                            Agent.AttackTimer = 0.f;
                            TargetComp->OnAIAttack(AIActor, EntityId, Agent.AttackDamage);
                            TargetComp->OnAIAttackDelegate.Broadcast(AIActor, EntityId, Agent.AttackDamage);
                        }
                    }
                    else
                    {
                        Agent.AttackTimer = 0.f;
                    }
                }

                // ── 障碍物侧事件 ──────────────────────────────────
                const bool bAtWall = Agent.bAtWall;

                if (bAtWall && !Agent.bWasAtWall)
                {
                    // 首次到达：找最近的障碍物组件
                    // 用 ObstacleIndex 缓存，避免每帧扫描
                    if (Agent.ObstacleIndex < 0 || Agent.ObstacleIndex >= RegObstacles.Num()
                        || !RegObstacles[Agent.ObstacleIndex].IsValid())
                    {
                        // 找距 CurrentGoal 最近的已注册障碍（O(n) 仅在 Enter 时执行一次）
                        const FVector& Goal = FlowActor->CurrentGoal;
                        float BestDistSq = FLT_MAX;
                        int32 BestIdx = -1;
                        for (int32 OIdx = 0; OIdx < RegObstacles.Num(); ++OIdx)
                        {
                            UFlowFieldObstacleComponent* OComp = RegObstacles[OIdx].Get();
                            if (!OComp || !OComp->GetOwner()) continue;
                            const float DistSq = FVector::DistSquared2D(
                                OComp->GetOwner()->GetActorLocation(), Goal);
                            if (DistSq < BestDistSq)
                            {
                                BestDistSq = DistSq;
                                BestIdx    = OIdx;
                            }
                        }
                        Agent.ObstacleIndex = BestIdx;
                    }

                    UFlowFieldObstacleComponent* OComp = (Agent.ObstacleIndex >= 0)
                        ? RegObstacles[Agent.ObstacleIndex].Get() : nullptr;
                    if (OComp)
                    {
                        OComp->OnAIReach(AIActor, EntityId);
                        OComp->OnAIReachDelegate.Broadcast(AIActor, EntityId);
                        Agent.AttackTimer = Agent.AttackInterval; // 到达即打
                    }
                }
                else if (!bAtWall && Agent.bWasAtWall)
                {
                    UFlowFieldObstacleComponent* OComp = (Agent.ObstacleIndex >= 0
                        && Agent.ObstacleIndex < RegObstacles.Num())
                        ? RegObstacles[Agent.ObstacleIndex].Get() : nullptr;
                    if (OComp)
                    {
                        OComp->OnAILeave(AIActor, EntityId);
                        OComp->OnAILeaveDelegate.Broadcast(AIActor, EntityId);
                    }
                    // 离开后清除索引
                    Agent.ObstacleIndex = -1;
                }

                // 障碍物攻击计时（bAtWall 时计时，与目标追踪互斥）
                if (bAtWall)
                {
                    UFlowFieldObstacleComponent* OComp = (Agent.ObstacleIndex >= 0
                        && Agent.ObstacleIndex < RegObstacles.Num())
                        ? RegObstacles[Agent.ObstacleIndex].Get() : nullptr;
                    if (OComp)
                    {
                        Agent.AttackTimer += DeltaTime;
                        if (Agent.AttackTimer >= Agent.AttackInterval)
                        {
                            Agent.AttackTimer = 0.f;
                            OComp->OnAIAttackObstacle(AIActor, EntityId, Agent.AttackDamage);
                            OComp->OnAIAttackObstacleDelegate.Broadcast(AIActor, EntityId, Agent.AttackDamage);
                        }
                    }
                }
                else if (!bAtWall)
                {
                    // 不攻击障碍物时保持计时器归零（目标攻击有自己的分支）
                    if (!Agent.bInAttackRange)
                        Agent.AttackTimer = 0.f;
                }

                // ── 调试绘制：AI 感知范围 ─────────────────────────
                if (bDrawAgents)
                {
                    const FVector Pos = Transforms[i].GetTransform().GetLocation();
                    if (FVector::DistSquared(Pos, CamPos) <= DrawDistSq)
                    {
                        DrawDebugCircle(World,
                            FVector(Pos.X, Pos.Y, Pos.Z + 5.f),
                            Agent.DetectRadius,
                            32, FColor(0, 200, 255),
                            false, 0.f, 0, 1.f,
                            FVector(1,0,0), FVector(0,1,0));
                    }
                }

                // ── 更新快照 ──────────────────────────────────────
                Agent.bWasChasingTarget  = Agent.bChasingTarget;
                Agent.bWasInAttackRange  = Agent.bInAttackRange;
                Agent.bWasAtWall         = bAtWall;
                Agent.bWasInChaseRange   = Agent.bInChaseRange;
            }
        });
}
