#include "MassAI/FlowFieldRVOProcessor.h"
#include "MassExecutionContext.h"
#include "MassEntityManager.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "FlowFieldSubsystem.h"
#include "FlowFieldActor.h"

// RVO2 — 编译为静态库，RVO_STATIC_DEFINE 已在 Build.cs 中定义
THIRD_PARTY_INCLUDES_START
#include "Vector2.h"
#include "RVOSimulator.h"
THIRD_PARTY_INCLUDES_END

UFlowFieldRVOProcessor::UFlowFieldRVOProcessor()
{
    ProcessingPhase = EMassProcessingPhase::PostPhysics;
    ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Movement;
    ExecutionOrder.ExecuteBefore.Add(TEXT("FlowFieldMovementProcessor"));
    ExecutionFlags = (int32)(EProcessorExecutionFlags::Server | EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
    bAutoRegisterWithProcessingPhases = true;
    bRequiresGameThreadExecution = true;
}

UFlowFieldRVOProcessor::~UFlowFieldRVOProcessor()
{
    delete RVOSim;
    RVOSim = nullptr;
}

void UFlowFieldRVOProcessor::ConfigureQueries(
    const TSharedRef<FMassEntityManager>& EntityManager)
{
    EntityQuery.Initialize(EntityManager);
    EntityQuery.AddTagRequirement<FFlowFieldAgentTag>(EMassFragmentPresence::All);
    EntityQuery.AddTagRequirement<FFlowFieldMovingTag>(EMassFragmentPresence::Optional);
    EntityQuery.AddRequirement<FFlowFieldAgentFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
    EntityQuery.RegisterWithProcessor(*this);
    RegisterQuery(EntityQuery);
}

void UFlowFieldRVOProcessor::Execute(
    FMassEntityManager& EntityManager,
    FMassExecutionContext& Context)
{
    UWorld* World = GetWorld();
    if (!World) return;

    UFlowFieldSubsystem* FlowSub = World->GetSubsystem<UFlowFieldSubsystem>();
    if (!FlowSub) return;

    AFlowFieldActor* FlowActor = FlowSub->GetActor();
    if (!FlowActor) return;

    const bool  bFlowReady  = FlowSub->IsReady();
    const float DeltaTime   = Context.GetDeltaTimeSeconds();
    if (DeltaTime <= 0.f) return;

    // ── 初始化模拟器 ─────────────────────────────────────────────
    if (!RVOSim)
    {
        RVOSim = new RVO::RVOSimulator();
        // 默认参数（实体注册时可按半径覆盖）
        // neighborDist=300cm  maxNeighbors=10  timeHorizon=0.5s
        // timeHorizonObst=0.5s  radius=60cm  maxSpeed=400cm/s
        RVOSim->setAgentDefaults(300.f, 10, 0.5f, 0.5f, 60.f, 400.f);
    }
    RVOSim->setTimeStep(DeltaTime);

    const FVector CurrentGoal    = FlowActor->CurrentGoal;
    const float   CellSize       = FlowActor->CellSize;

    // ── 计算有效目标（普通模式用，障碍目标模式不走 bNearGoal）────
    const bool bGoalInObstacle = (FlowActor->GetIntegration(CurrentGoal) < 0.f);
    if (bGoalInObstacle)
    {
        if (FVector::Dist2D(CachedGoalForEffective, CurrentGoal) > 1.f)
        {
            FIntPoint NC = FlowActor->FindNearestWalkable(CurrentGoal);
            CachedEffectiveGoal    = (NC.X >= 0) ? FlowActor->GetCellCenter(NC) : CurrentGoal;
            CachedGoalForEffective = CurrentGoal;
        }
    }
    // 攻城模式（目标在障碍内）不用距离停止，只用 bAtWall；普通模式用距离
    const FVector EffectiveGoal = bGoalInObstacle ? CachedEffectiveGoal : CurrentGoal;
    const float   StopDistSq   = bGoalInObstacle ? 0.f : FMath::Square(CellSize * NormalGoalStopRadiusMult);

    // ── Pass 1：同步位置 + 设置期望速度 ─────────────────────────
    EntityQuery.ForEachEntityChunk(Context,
        [&](FMassExecutionContext& ChunkContext)
        {
            auto Agents     = ChunkContext.GetMutableFragmentView<FFlowFieldAgentFragment>();
            auto Transforms = ChunkContext.GetFragmentView<FTransformFragment>();
            const int32 Num = ChunkContext.GetNumEntities();

            for (int32 i = 0; i < Num; i++)
            {
                FFlowFieldAgentFragment& Agent = Agents[i];
                const FVector Pos = Transforms[i].GetTransform().GetLocation();
                const RVO::Vector2 RVOPos(Pos.X, Pos.Y);

                // 首次出现：注册到 RVO 模拟器
                if (Agent.RVOAgentId < 0)
                {
                    const std::size_t Id = RVOSim->addAgent(
                        RVOPos,
                        Agent.RVONeighborDist,     // neighborDist：感知范围
                        (std::size_t)Agent.RVOMaxNeighbors,
                        Agent.RVOTimeHorizon,      // timeHorizon
                        Agent.RVOTimeHorizon,      // timeHorizonObst（与 timeHorizon 保持一致）
                        Agent.AgentRadius,         // 实体半径
                        Agent.MoveSpeed * 1.2f     // maxSpeed 略高于 MoveSpeed，让 RVO 有调整空间
                    );
                    Agent.RVOAgentId = (int32)Id;
                }
                else
                {
                    RVOSim->setAgentPosition((std::size_t)Agent.RVOAgentId, RVOPos);
                }

                // ── 计算期望速度 ──────────────────────────────────
                RVO::Vector2 PrefVel(0.f, 0.f);

                if (!Agent.bIsKnockedBack && bFlowReady
                    && FlowActor->GetIntegration(Pos) >= 0.f)
                {
                    // 目标在障碍内（攻城模式）：只用 bAtWall，让 AI 铺开包围整圈
                    // 目标在可走格：只用 bNearGoal
                    // 目标在可走格：只用 bNearGoal，靠近目标就停
                    bool bAtWall = false;
                    if (bGoalInObstacle && FlowActor->IsBorderCell(Pos))
                    {
                        const FVector GoalDir2D = (FlowActor->CurrentGoal - Pos).GetSafeNormal2D();
                        if (!GoalDir2D.IsNearlyZero())
                        {
                            const bool bHalfBlocked = FlowActor->GetIntegration(Pos + GoalDir2D * (CellSize * 0.5f)) < 0.f;
                            const bool bFullBlocked  = FlowActor->GetIntegration(Pos + GoalDir2D * CellSize)          < 0.f;
                            bAtWall = bHalfBlocked || bFullBlocked;
                        }
                    }
                    const bool bNearGoal  = !bGoalInObstacle && (FVector::DistSquared2D(Pos, EffectiveGoal) < StopDistSq);
                    const bool bStopped   = bNearGoal || bAtWall;
                    Agent.bInStopZone = bStopped;

                    if (!bStopped)
                    {
                        // ── 检查是否进入追踪目标范围 ────────────────
                        const TArray<FFlowFieldTrackedTarget>& Targets =
                            FlowActor->GetTrackedTargets();
                        bool    bInRange      = false;
                        FVector ChaseTargetPos = FVector::ZeroVector;
                        for (const FFlowFieldTrackedTarget& T : Targets)
                        {
                            if (FVector::DistSquared2D(Pos, T.Position) <= FMath::Square(T.Radius))
                            {
                                bInRange      = true;
                                ChaseTargetPos = T.Position;
                                break;
                            }
                        }

                        if (bInRange)
                        {
                            Agent.bChasingTarget = true;
                            Agent.ChaseTargetPos = ChaseTargetPos;

                            // 已经紧贴目标（两个碰撞半径内）：停下来，不再冲过头
                            const float StopNearTargetSq = FMath::Square(Agent.AgentRadius * 2.f);
                            if (FVector::DistSquared2D(Pos, ChaseTargetPos) >= StopNearTargetSq)
                            {
                                // 直接追踪：朝目标方向移动，遇障碍偏转 ±90°
                                FVector ChaseDir = (ChaseTargetPos - Pos).GetSafeNormal2D();
                                const FVector LookAhead = Pos + ChaseDir * Agent.AgentRadius * 2.f;
                                if (FlowActor->GetIntegration(LookAhead) < 0.f)
                                {
                                    const FVector Left  = FVector(-ChaseDir.Y,  ChaseDir.X, 0.f);
                                    const FVector Right = FVector( ChaseDir.Y, -ChaseDir.X, 0.f);
                                    const bool bLeftOk  = FlowActor->GetIntegration(Pos + Left  * Agent.AgentRadius * 2.f) >= 0.f;
                                    const bool bRightOk = FlowActor->GetIntegration(Pos + Right * Agent.AgentRadius * 2.f) >= 0.f;
                                    if      ( bLeftOk && !bRightOk) ChaseDir = (ChaseDir + Left ).GetSafeNormal2D();
                                    else if (!bLeftOk &&  bRightOk) ChaseDir = (ChaseDir + Right).GetSafeNormal2D();
                                }
                                if (!ChaseDir.IsNearlyZero())
                                    PrefVel = RVO::Vector2(ChaseDir.X * Agent.MoveSpeed,
                                                           ChaseDir.Y * Agent.MoveSpeed);
                            }
                        }
                        else
                        {
                            // 不在范围内：正常 FlowField
                            Agent.bChasingTarget = false;
                            Agent.ChaseTargetPos = FVector::ZeroVector;

                            FVector FlowDir = FlowActor->GetFlowDirectionSmooth(Pos);
                            if (!FlowDir.IsNearlyZero())
                                PrefVel = RVO::Vector2(FlowDir.X * Agent.MoveSpeed,
                                                       FlowDir.Y * Agent.MoveSpeed);
                        }
                    }
                }
                else
                {
                    Agent.bInStopZone    = false;
                    Agent.bChasingTarget = false;
                }

                RVOSim->setAgentPrefVelocity((std::size_t)Agent.RVOAgentId, PrefVel);
            }
        });

    // ── RVO 步进 ─────────────────────────────────────────────────
    RVOSim->doStep();

    // ── Pass 2：读回计算速度 ─────────────────────────────────────
    EntityQuery.ForEachEntityChunk(Context,
        [&](FMassExecutionContext& ChunkContext)
        {
            auto Agents = ChunkContext.GetMutableFragmentView<FFlowFieldAgentFragment>();
            const int32 Num = ChunkContext.GetNumEntities();

            for (int32 i = 0; i < Num; i++)
            {
                FFlowFieldAgentFragment& Agent = Agents[i];
                if (Agent.RVOAgentId < 0) continue;

                const RVO::Vector2& V = RVOSim->getAgentVelocity((std::size_t)Agent.RVOAgentId);
                Agent.RVOComputedVelocity = FVector(V.x(), V.y(), 0.f);
            }
        });
}
