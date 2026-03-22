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
    EntityQuery.AddTagRequirement<FFlowFieldDeadTag>(EMassFragmentPresence::None);
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

    // ── RVO 跳帧：非更新帧直接返回，MovementProcessor 复用上次速度 ──
    FrameCounter++;
    if (FrameCounter % FMath::Max(1, RVOUpdateInterval) != 0) return;
    const float RVODeltaTime = DeltaTime * RVOUpdateInterval; // 补偿累计时间

    // ── 初始化模拟器 ─────────────────────────────────────────────
    if (!RVOSim)
    {
        RVOSim = new RVO::RVOSimulator();
        RVOSim->setAgentDefaults(200.f, 5, 0.5f, 0.5f, 60.f, 400.f);
    }
    RVOSim->setTimeStep(RVODeltaTime);

    FRWScopeLock ReadLock(FlowActor->GridRWLock, SLT_ReadOnly);

    // 人群计数缓冲（与 TrackedTargets 同长，ForEachEntityChunk 并行时用原子增加）
    TArray<int32> LocalCrowdCounts;
    LocalCrowdCounts.SetNumZeroed(FlowActor->GetTrackedTargets().Num());

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
                    // 攻城停止判断：仅当积分值接近0（在目标障碍包围圈上）才停止。
                    // 旧方案用"边界格 + 目标方向被遮挡"判断，会错误地把经过的中间障碍
                    // 旁边的格子也判为停止位（中间障碍同样是边界格，同样遮挡目标方向）。
                    // 积分=0 的格子是 BuildIntegrationFieldMulti 的 BFS 源点（Ring），
                    // 即目标障碍的直接包围圈；被中间障碍挡住时积分仍然很高，不会误判。
                    const float EntityIntegration = FlowActor->GetIntegration(Pos);
                    const bool bAtWall = bGoalInObstacle && EntityIntegration >= 0.f && EntityIntegration < 2.f;
                    const bool bNearGoal  = !bGoalInObstacle && (FVector::DistSquared2D(Pos, EffectiveGoal) < StopDistSq);
                    const bool bStopped   = bNearGoal || bAtWall;
                    Agent.bInStopZone   = bStopped;
                    Agent.bAtWall       = bAtWall;
                    if (bStopped) Agent.bInChaseRange = false;

                    if (!bStopped)
                    {
                        // ── 检查是否进入追踪目标范围 ────────────────
                        const TArray<FFlowFieldTrackedTarget>& Targets =
                            FlowActor->GetTrackedTargets();
                        int32   FoundTargetIdx = -1;
                        FVector ChaseTargetPos = FVector::ZeroVector;
                        for (int32 TIdx = 0; TIdx < Targets.Num(); ++TIdx)
                        {
                            // 用 AI 自身的 DetectRadius，不再依赖目标侧配置
                            if (FVector::DistSquared2D(Pos, Targets[TIdx].Position) <= FMath::Square(Agent.DetectRadius))
                            {
                                FoundTargetIdx = TIdx;
                                ChaseTargetPos = Targets[TIdx].Position;
                                break;
                            }
                        }

                        // 辅助：计算朝向并做障碍偏转（in-range 和遗忘期共用）
                        auto CalcChaseDir = [&](const FVector& TargetPos) -> FVector
                        {
                            FVector Dir = (TargetPos - Pos).GetSafeNormal2D();
                            if (FlowActor->GetIntegration(Pos + Dir * Agent.AgentRadius * 2.f) < 0.f)
                            {
                                const FVector Left  = FVector(-Dir.Y,  Dir.X, 0.f);
                                const FVector Right = FVector( Dir.Y, -Dir.X, 0.f);
                                const bool bL = FlowActor->GetIntegration(Pos + Left  * Agent.AgentRadius * 2.f) >= 0.f;
                                const bool bR = FlowActor->GetIntegration(Pos + Right * Agent.AgentRadius * 2.f) >= 0.f;
                                if      ( bL && !bR) Dir = (Dir + Left ).GetSafeNormal2D();
                                else if (!bL &&  bR) Dir = (Dir + Right).GetSafeNormal2D();
                            }
                            return Dir;
                        };

                        if (FoundTargetIdx >= 0)
                        {
                            // ── 在感知范围内：正常追踪 ──────────────
                            Agent.bInChaseRange  = true;
                            Agent.bChasingTarget = true;
                            Agent.TargetIndex    = FoundTargetIdx;
                            Agent.ChaseTargetPos = ChaseTargetPos;
                            Agent.ForgetTimer    = 0.f; // 在范围内时重置遗忘计时

                            const float AttackRangeSq = FMath::Square(
                                Agent.AttackRange > 0.f ? Agent.AttackRange : Agent.AgentRadius * 2.f);
                            Agent.bInAttackRange =
                                FVector::DistSquared2D(Pos, ChaseTargetPos) < AttackRangeSq;

                            if (!Agent.bInAttackRange)
                            {
                                const FVector Dir = CalcChaseDir(ChaseTargetPos);
                                if (!Dir.IsNearlyZero())
                                    PrefVel = RVO::Vector2(Dir.X * Agent.MoveSpeed, Dir.Y * Agent.MoveSpeed);
                            }
                        }
                        else
                        {
                            // ── 不在感知范围内 ───────────────────────
                            Agent.bInChaseRange  = false;
                            Agent.bInAttackRange = false;
                            // bChasingTarget 不在此处清除——由 BehaviorProcessor 的遗忘计时决定

                            if (Agent.bChasingTarget && Agent.ForgetTime > 0.f)
                            {
                                // 遗忘期：继续朝上次已知位置移动
                                const FVector Dir = CalcChaseDir(Agent.ChaseTargetPos);
                                if (!Dir.IsNearlyZero())
                                    PrefVel = RVO::Vector2(Dir.X * Agent.MoveSpeed, Dir.Y * Agent.MoveSpeed);
                            }
                            else
                            {
                                // 兜底：初始状态 / ForgetTime=0 / 遗忘完成 → 回归流场
                                FVector FlowDir = FlowActor->GetFlowDirectionSmooth(Pos);
                                if (!FlowDir.IsNearlyZero())
                                    PrefVel = RVO::Vector2(FlowDir.X * Agent.MoveSpeed, FlowDir.Y * Agent.MoveSpeed);
                            }
                        }
                    }
                }
                else if (!Agent.bIsKnockedBack && !bFlowReady)
                {
                    // 流场未就绪：仅做范围内直接追踪，RVO 仍负责 AI 相互避让
                    Agent.bInStopZone = false;
                    Agent.bAtWall     = false;

                    const TArray<FFlowFieldTrackedTarget>& NoFlowTargets =
                        FlowActor->GetTrackedTargets();
                    bool bFoundTarget = false;
                    for (int32 TIdx = 0; TIdx < NoFlowTargets.Num(); ++TIdx)
                    {
                        const FFlowFieldTrackedTarget& CT = NoFlowTargets[TIdx];
                        if (FVector::DistSquared2D(Pos, CT.Position) <=
                            FMath::Square(Agent.DetectRadius))
                        {
                            Agent.bInChaseRange  = true;
                            Agent.bChasingTarget = true;
                            Agent.TargetIndex    = TIdx;
                            Agent.ChaseTargetPos = CT.Position;
                            Agent.ForgetTimer    = 0.f;
                            bFoundTarget = true;

                            const float AttackRangeSq2 =
                                FMath::Square(Agent.AttackRange > 0.f
                                    ? Agent.AttackRange
                                    : Agent.AgentRadius * 2.f);
                            Agent.bInAttackRange =
                                FVector::DistSquared2D(Pos, CT.Position) < AttackRangeSq2;

                            if (!Agent.bInAttackRange)
                            {
                                FVector ChaseDir =
                                    (CT.Position - Pos).GetSafeNormal2D();
                                if (!ChaseDir.IsNearlyZero())
                                    PrefVel = RVO::Vector2(
                                        ChaseDir.X * Agent.MoveSpeed,
                                        ChaseDir.Y * Agent.MoveSpeed);
                            }
                            break;
                        }
                    }
                    if (!bFoundTarget)
                    {
                        Agent.bInChaseRange  = false;
                        Agent.bInAttackRange = false;
                        // bChasingTarget 由 BehaviorProcessor 的遗忘计时决定是否清除
                        // 流场未就绪 + 无目标 + 遗忘期 → 朝最后位置移动；否则静止等待
                        if (Agent.bChasingTarget && Agent.ForgetTime > 0.f
                            && !Agent.ChaseTargetPos.IsNearlyZero())
                        {
                            FVector ChaseDir = (Agent.ChaseTargetPos - Pos).GetSafeNormal2D();
                            if (!ChaseDir.IsNearlyZero())
                                PrefVel = RVO::Vector2(
                                    ChaseDir.X * Agent.MoveSpeed,
                                    ChaseDir.Y * Agent.MoveSpeed);
                        }
                    }
                }
                else
                {
                    Agent.bInStopZone    = false;
                    Agent.bAtWall        = false;
                    Agent.bChasingTarget = false;
                    Agent.bInAttackRange = false;
                }

                // ── 人群计数（使用 PushRadius 作为接触范围）────────────
                {
                    const TArray<FFlowFieldTrackedTarget>& AllTargets =
                        FlowActor->GetTrackedTargets();
                    for (int32 TIdx = 0; TIdx < AllTargets.Num(); ++TIdx)
                    {
                        const FFlowFieldTrackedTarget& TT = AllTargets[TIdx];
                        const float DX    = Pos.X - TT.Position.X;
                        const float DY    = Pos.Y - TT.Position.Y;
                        const float DistSq = DX * DX + DY * DY;

                        if (TT.PushRadius > 0.f
                            && DistSq < FMath::Square(TT.PushRadius)
                            && TIdx < LocalCrowdCounts.Num())
                        {
                            FPlatformAtomics::InterlockedAdd(
                                reinterpret_cast<volatile int32*>(
                                    &LocalCrowdCounts[TIdx]), 1);
                        }
                    }
                }

                // ── 人群密度限速 + 动态时间窗（Layer 2 & 3）─────────
                if (Agent.CrowdDensityFullAt > 0)
                {
                    const float DensityT = FMath::Clamp(
                        float(Agent.LocalNeighborCount) / float(Agent.CrowdDensityFullAt),
                        0.f, 1.f);

                    // Layer 2：压缩期望速度幅值，使密集 AI 整体变慢
                    const float SpeedMult = FMath::Lerp(1.f, Agent.CrowdSpeedMin, DensityT);
                    PrefVel = PrefVel * SpeedMult;

                    // Layer 3：动态时间窗——密集时缩短，AI 不再提前大幅侧移，接受更近间距
                    const float DynHorizon = FMath::Lerp(
                        Agent.RVOTimeHorizon, Agent.RVOTimeHorizon * 0.2f, DensityT);
                    RVOSim->setAgentTimeHorizon((std::size_t)Agent.RVOAgentId, DynHorizon);

                    // 同步限制 maxSpeed，防止 RVO 在绕行时突然加速超出期望幅值
                    const float DynMaxSpeed = Agent.MoveSpeed * 1.2f * SpeedMult;
                    RVOSim->setAgentMaxSpeed((std::size_t)Agent.RVOAgentId, DynMaxSpeed);
                }

                RVOSim->setAgentPrefVelocity((std::size_t)Agent.RVOAgentId, PrefVel);
            }
        });

    // ── RVO 步进 ─────────────────────────────────────────────────
    RVOSim->doStep();

    // ── Pass 2：读回 RVO 计算速度（推挤在 MovementProcessor 每帧独立处理）
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

                // 更新邻居计数（一帧延迟，供下帧密度计算）
                Agent.LocalNeighborCount = (int32)RVOSim->getAgentNumAgentNeighbors(
                    (std::size_t)Agent.RVOAgentId);
            }
        });

    // ── 写回人群计数（供 FlowFieldActor Tick 分发给角色组件）────
    if (FlowActor->CrowdCounts.Num() == LocalCrowdCounts.Num())
    {
        for (int32 i = 0; i < LocalCrowdCounts.Num(); ++i)
            FlowActor->CrowdCounts[i] = LocalCrowdCounts[i];
    }
}
