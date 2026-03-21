#include "MassAI/FlowFieldMovementProcessor.h"
#include "FlowFieldSubsystem.h"
#include "FlowFieldActor.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "MassEntityManager.h"
#include "MassRepresentationTypes.h"

UFlowFieldMovementProcessor::UFlowFieldMovementProcessor(): EntityQuery()
{
    ProcessingPhase = EMassProcessingPhase::PostPhysics;
    ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Movement;
    ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Representation);
    ExecutionFlags = (int32)(EProcessorExecutionFlags::Server | EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
    bAutoRegisterWithProcessingPhases = true;
}

void UFlowFieldMovementProcessor::ConfigureQueries(
    const TSharedRef<FMassEntityManager>& EntityManager)
{
    EntityQuery.Initialize(EntityManager);
    EntityQuery.AddTagRequirement<FFlowFieldAgentTag>(EMassFragmentPresence::All);
    EntityQuery.AddTagRequirement<FFlowFieldMovingTag>(EMassFragmentPresence::Optional);
    EntityQuery.AddRequirement<FFlowFieldAgentFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.RegisterWithProcessor(*this);
    RegisterQuery(EntityQuery);
}

void UFlowFieldMovementProcessor::Execute(
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

    FRWScopeLock ReadLock(FlowActor->GridRWLock, SLT_ReadOnly);

    EntityQuery.ForEachEntityChunk(Context,
        [&](FMassExecutionContext& ChunkContext)
        {
            auto Agents     = ChunkContext.GetMutableFragmentView<FFlowFieldAgentFragment>();
            auto Transforms = ChunkContext.GetMutableFragmentView<FTransformFragment>();
            const int32 Num = ChunkContext.GetNumEntities();

            for (int32 i = 0; i < Num; i++)
            {
                FFlowFieldAgentFragment& Agent     = Agents[i];
                FTransform&              Transform = Transforms[i].GetMutableTransform();
                const FVector            Pos       = Transform.GetLocation();
                FRotator                 CurRotator = Transform.GetRotation().Rotator();

                // ── 击退 ─────────────────────────────────────────────
                if (Agent.bIsKnockedBack)
                {
                    // 爆炸可能直接把 AI 推入障碍，先脱困再移动
                    if (bFlowReady && FlowActor->GetIntegration(Pos) < 0.f)
                    {
                        FIntPoint NC = FlowActor->FindNearestWalkable(Pos);
                        if (NC.X >= 0)
                        {
                            FVector SafePos = FlowActor->GetCellCenter(NC);
                            SafePos.Z = Agent.bSurfaceInitialized ? Agent.SmoothedSurfaceZ : Pos.Z;
                            Transform.SetLocation(SafePos);
                        }
                        Agent.KnockbackVelocity    = FVector::ZeroVector;
                        Agent.bIsKnockedBack       = false;
                        Agent.RVOComputedVelocity  = FVector::ZeroVector;
                        Agent.SmoothedMoveVelocity = FVector::ZeroVector;
                        continue;
                    }

                    if (Agent.KnockbackVelocity.SizeSquared2D() > 1.f)
                    {
                        FVector KBDir  = Agent.KnockbackVelocity.GetSafeNormal2D();
                        FVector NewPos = FVector(
                            Pos.X + Agent.KnockbackVelocity.X * DeltaTime,
                            Pos.Y + Agent.KnockbackVelocity.Y * DeltaTime,
                            Agent.bSurfaceInitialized ? Agent.SmoothedSurfaceZ : Pos.Z);

                        if (FlowActor->GetIntegration(NewPos) < 0.f)
                        {
                            FVector TryX = FVector(NewPos.X, Pos.Y, NewPos.Z);
                            FVector TryY = FVector(Pos.X, NewPos.Y, NewPos.Z);
                            if      (FlowActor->GetIntegration(TryX) >= 0.f) NewPos = TryX;
                            else if (FlowActor->GetIntegration(TryY) >= 0.f) NewPos = TryY;
                            else
                            {
                                Agent.KnockbackVelocity = FVector::ZeroVector;
                                Agent.bIsKnockedBack    = false;
                                NewPos = FVector(Pos.X, Pos.Y, NewPos.Z);
                            }
                        }
                        Transform.SetLocation(NewPos);
                        Transform.SetRotation(FMath::RInterpTo(
                            CurRotator,
                            KBDir.ToOrientationRotator(), DeltaTime, 15.f).Quaternion());
                        Agent.KnockbackVelocity = FMath::VInterpTo(
                            Agent.KnockbackVelocity, FVector::ZeroVector,
                            DeltaTime, Agent.KnockbackDecay);
                    }
                    else
                    {
                        Agent.KnockbackVelocity = FVector::ZeroVector;
                        Agent.bIsKnockedBack    = false;
                    }
                    continue;
                }

                // ── 地面贴合 ─────────────────────────────────────────
                {
                    float   RawZ      = Pos.Z;
                    FVector RawNormal = FVector::UpVector;
                    bool bGotSurface = FlowActor->GetSavedSurfaceData(Pos, RawZ, RawNormal);

                    // 扫描 Z 为 0 或与当前位置偏差超过 500cm，视为无效，改用射线
                    if (bGotSurface && (RawZ == 0.f || FMath::Abs(RawZ - Pos.Z) > 500.f))
                        bGotSurface = false;

                    // 线迹只能在游戏线程执行；多线程模式下此 fallback 被跳过，
                    // 依赖 ScanAndPlaceObstacles 预存的 SavedSurfaceZ 完成初始化。
                    if (!bGotSurface && !Agent.bSurfaceInitialized && IsInGameThread())
                    {
                        FHitResult Hit;
                        if (World->LineTraceSingleByObjectType(
                                Hit,
                                FVector(Pos.X, Pos.Y, Pos.Z + 200.f),
                                FVector(Pos.X, Pos.Y, Pos.Z - 1000.f),
                                FCollisionObjectQueryParams(ECC_WorldStatic)))
                        {
                            RawZ = Hit.Location.Z;
                            bGotSurface = true;
                        }
                    }

                    if (bGotSurface)
                    {
                        if (!Agent.bSurfaceInitialized)
                        {
                            Agent.SmoothedSurfaceZ    = RawZ;
                            Agent.bSurfaceInitialized = true;
                            FVector P = Pos; P.Z = RawZ;
                            Transform.SetLocation(P);
                        }
                        else if (bFlowReady)
                        {
                            Agent.SmoothedSurfaceZ = FMath::FInterpTo(
                                Agent.SmoothedSurfaceZ, RawZ, DeltaTime, Agent.SurfaceZSmoothSpeed);
                        }
                    }
                }

                if (!bFlowReady) continue;

                // ── 在障碍内：推到最近可走格子 ───────────────────────
                if (FlowActor->GetIntegration(Pos) < 0.f)
                {
                    FVector UsePos = Pos;
                    FIntPoint NC = FlowActor->FindNearestWalkable(Pos);
                    if (NC.X >= 0)
                    {
                        FVector SafePos = FlowActor->GetCellCenter(NC);
                        SafePos.Z = Agent.bSurfaceInitialized ? Agent.SmoothedSurfaceZ : Pos.Z;
                        Transform.SetLocation(SafePos);
                        UsePos = SafePos;
                    }
                    // 清空 RVO 残留速度，防止下一帧被推回障碍
                    Agent.RVOComputedVelocity  = FVector::ZeroVector;
                    Agent.SmoothedMoveVelocity = FVector::ZeroVector;
                    // 用恢复后坐标朝向目标，避免旋转永远被 continue 跳过
                    const FVector RecoveryDir = (FlowActor->CurrentGoal - UsePos).GetSafeNormal2D();
                    const FVector TargetDir   = RecoveryDir.IsNearlyZero() ? Agent.CurrentDir : RecoveryDir;
                    if (!TargetDir.IsNearlyZero())
                    {
                        Agent.CurrentDir = Agent.CurrentDir.IsNearlyZero()
                            ? TargetDir
                            : FMath::VInterpTo(Agent.CurrentDir, TargetDir, DeltaTime, Agent.DirSmoothing).GetSafeNormal2D();
                        Transform.SetRotation(FMath::RInterpTo(
                            CurRotator,
                            Agent.CurrentDir.ToOrientationRotator(),
                            DeltaTime, RotationInterpSpeed).Quaternion());
                    }
                    continue;
                }

                // 贴墙停止：清零速度，只更新 Yaw 朝向障碍方向，保持地形 Pitch/Roll
                if (Agent.bInStopZone)
                {
                    Agent.SmoothedMoveVelocity = FVector::ZeroVector;

                    // 取预计算的面向方向（指向相邻障碍的平均方向）
                    const FVector2D FaceDir2D = FlowActor->GetBorderFaceDir(Pos);
                    FVector FaceDir3D = FaceDir2D.IsNearlyZero()
                        ? (FlowActor->CurrentGoal - Pos).GetSafeNormal2D()
                        : FVector(FaceDir2D.X, FaceDir2D.Y, 0.f);
                    if (FaceDir3D.IsNearlyZero()) FaceDir3D = Agent.CurrentDir;

                    if (!FaceDir3D.IsNearlyZero())
                    {
                        Agent.CurrentDir = Agent.CurrentDir.IsNearlyZero()
                            ? FaceDir3D
                            : FMath::VInterpTo(Agent.CurrentDir, FaceDir3D, DeltaTime, Agent.DirSmoothing).GetSafeNormal2D();
                        Transform.SetRotation(FMath::RInterpTo(
                            CurRotator,
                            Agent.CurrentDir.ToOrientationRotator(),
                            DeltaTime, RotationInterpSpeed).Quaternion());
                    }
                    continue;
                }

                // ── 旋转方向 ─────────────────────────────────────────
                // 行进中：流场方向为主；当流场方向背向目标（绕障碍物）时，
                //         按背向程度混入目标方向，避免 AI 贴墙时面向后方
                // 缓存一次，旋转计算和速度压缩共用，避免重复双线性插值
                const FVector CachedFlowDir = FlowActor->GetFlowDirectionSmooth(Pos);
                const FVector GoalDir2D = (FlowActor->CurrentGoal - Pos).GetSafeNormal2D();
                FVector RotDir;

                if (Agent.bChasingTarget && !Agent.ChaseTargetPos.IsZero())
                {
                    RotDir = (Agent.ChaseTargetPos - Pos).GetSafeNormal2D();
                    if (RotDir.IsNearlyZero()) RotDir = Agent.CurrentDir;
                }
                else
                {
                    FVector FlowDir = CachedFlowDir;
                    if (FlowDir.IsNearlyZero()) FlowDir = GoalDir2D;
                    if (FlowDir.IsNearlyZero()) FlowDir = Agent.CurrentDir;

                    const float Alignment = FVector::DotProduct(FlowDir, GoalDir2D);
                    const float GoalWeight = FMath::Max(0.f, -Alignment) * 0.85f;
                    RotDir = (FlowDir + GoalDir2D * GoalWeight).GetSafeNormal2D();
                    if (RotDir.IsNearlyZero()) RotDir = FlowDir;
                }

                if (!RotDir.IsNearlyZero())
                {
                    Agent.CurrentDir = Agent.CurrentDir.IsNearlyZero()
                        ? RotDir
                        : FMath::VInterpTo(Agent.CurrentDir, RotDir, DeltaTime, Agent.DirSmoothing).GetSafeNormal2D();
                }

                if (!Agent.CurrentDir.IsNearlyZero())
                    Transform.SetRotation(FMath::RInterpTo(
                        CurRotator,
                        Agent.CurrentDir.ToOrientationRotator(),
                        DeltaTime, RotationInterpSpeed).Quaternion());

                // ── RVO 计算速度 → 平滑 → 被挤减速 ──────────────────
                // 速度平滑：对 RVO 原始输出做低通滤波，消除逐帧高频跳变
                Agent.SmoothedMoveVelocity = FMath::VInterpTo(
                    Agent.SmoothedMoveVelocity, Agent.RVOComputedVelocity,
                    DeltaTime, VelocitySmoothSpeed);

                FVector MoveVel = Agent.SmoothedMoveVelocity;

                // 速度死区：低于 MoveSpeed × VelocityDeadZonePct 视为静止
                if (MoveVel.SizeSquared2D() < FMath::Square(Agent.MoveSpeed * VelocityDeadZonePct)) continue;

                // 被挤减速：实际速度方向与流场期望方向越不一致，速度越慢
                // 追踪目标时跳过此检测（方向本就和流场相反，不应被压速）
                if (!Agent.bInStopZone && !Agent.bChasingTarget) // 贴墙中不做被挤减速
                {
                    if (!CachedFlowDir.IsNearlyZero())
                    {
                        const float Alignment = FVector::DotProduct(MoveVel.GetSafeNormal2D(), CachedFlowDir);
                        const float SpeedScale = FMath::GetMappedRangeValueClamped(
                            FVector2D(-1.f, 1.f), FVector2D(PushedMinSpeedScale, 1.f), Alignment);
                        MoveVel *= SpeedScale;
                    }
                }

                // ── 服务端校正 ───────────────────────────────────────
                if (Agent.bHasCorrection)
                {
                    if (FVector::Dist2D(Pos, Agent.CorrectionTargetLocation) < 10.f)
                        Agent.bHasCorrection = false;
                    else
                    {
                        FVector CorrDir = (Agent.CorrectionTargetLocation - Pos).GetSafeNormal2D();
                        MoveVel = CorrDir * MoveVel.Size2D();
                    }
                }

                // ── 移动 ─────────────────────────────────────────────
                FVector NewPos = FVector(
                    Pos.X + MoveVel.X * DeltaTime,
                    Pos.Y + MoveVel.Y * DeltaTime,
                    Agent.bSurfaceInitialized ? Agent.SmoothedSurfaceZ : Pos.Z);

                if (FlowActor->GetIntegration(NewPos) < 0.f)
                {
                    FVector TryX = FVector(NewPos.X, Pos.Y, NewPos.Z);
                    FVector TryY = FVector(Pos.X, NewPos.Y, NewPos.Z);
                    if      (FlowActor->GetIntegration(TryX) >= 0.f) NewPos = TryX;
                    else if (FlowActor->GetIntegration(TryY) >= 0.f) NewPos = TryY;
                    else    NewPos = FVector(Pos.X, Pos.Y, NewPos.Z);
                }

                Transform.SetLocation(NewPos);
            }
        });
}
