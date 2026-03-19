#include "MassAI/FlowFieldMovementProcessor.h"
#include "MassAI/FlowFieldBoidsFragment.h"
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
    bRequiresGameThreadExecution = true;
}

void UFlowFieldMovementProcessor::ConfigureQueries(
    const TSharedRef<FMassEntityManager>& EntityManager)
{
    EntityQuery.Initialize(EntityManager);
    EntityQuery.AddTagRequirement<FFlowFieldAgentTag>(EMassFragmentPresence::All);
    EntityQuery.AddTagRequirement<FFlowFieldMovingTag>(EMassFragmentPresence::Optional);
    EntityQuery.AddRequirement<FFlowFieldAgentFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddRequirement<FFlowFieldBoidsFragment>(EMassFragmentAccess::ReadOnly);
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

    const bool    bFlowReady        = FlowSub->IsReady();
    const FVector CurrentGoal       = FlowActor->CurrentGoal;
    const float   DeltaTime         = Context.GetDeltaTimeSeconds();
    const float   GoalChangedDistSq = FMath::Square(FlowActor->CellSize * 0.5f);

    EntityQuery.ForEachEntityChunk(Context,
        [&](FMassExecutionContext& ChunkContext)
        {
            auto Agents     = ChunkContext.GetMutableFragmentView<FFlowFieldAgentFragment>();
            auto Boids      = ChunkContext.GetFragmentView<FFlowFieldBoidsFragment>();
            auto Transforms = ChunkContext.GetMutableFragmentView<FTransformFragment>();
            const int32 Num = ChunkContext.GetNumEntities();

            for (int32 i = 0; i < Num; i++)
            {
                FFlowFieldAgentFragment&       Agent     = Agents[i];
                const FFlowFieldBoidsFragment& Boid      = Boids[i];
                FTransform&                    Transform = Transforms[i].GetMutableTransform();
                const FVector                  Pos       = Transform.GetLocation();

                // ── 击退处理 ─────────────────────────────────────────
                if (Agent.bIsKnockedBack)
                {
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
                        FRotator NewRot = FMath::RInterpTo(
                            Transform.GetRotation().Rotator(),
                            KBDir.ToOrientationRotator(), DeltaTime, 15.f);
                        Transform.SetRotation(NewRot.Quaternion());
                        Agent.KnockbackVelocity = FMath::VInterpTo(
                            Agent.KnockbackVelocity, FVector::ZeroVector,
                            DeltaTime, Agent.KnockbackDecay);
                    }
                    else
                    {
                        Agent.KnockbackVelocity = FVector::ZeroVector;
                        Agent.bIsKnockedBack    = false;
                        Agent.CurrentDir        = FVector::ZeroVector;
                    }
                    continue;
                }

                // ── 地面贴合 ─────────────────────────────────────────
                {
                    float   RawZ      = Pos.Z;
                    FVector RawNormal = FVector::UpVector;
                    if (FlowActor->GetSavedSurfaceData(Pos, RawZ, RawNormal))
                    {
                        if (!Agent.bSurfaceInitialized)
                        {
                            Agent.SmoothedSurfaceZ    = RawZ;
                            Agent.SmoothedNormal      = RawNormal;
                            Agent.bSurfaceInitialized = true;
                            FVector FixedPos          = Pos;
                            FixedPos.Z                = RawZ;
                            Transform.SetLocation(FixedPos);
                        }
                        else if (bFlowReady)
                        {
                            Agent.SmoothedSurfaceZ = FMath::FInterpTo(
                                Agent.SmoothedSurfaceZ, RawZ, DeltaTime, Agent.SurfaceZSmoothSpeed);
                            Agent.SmoothedNormal = FMath::VInterpTo(
                                Agent.SmoothedNormal, RawNormal, DeltaTime, Agent.SurfaceNormalSmoothSpeed);
                            Agent.SmoothedNormal.Normalize();
                        }
                    }
                }

                if (!bFlowReady) continue;

                // ── 目标切换检测 ─────────────────────────────────────
                if (FVector::DistSquared2D(CurrentGoal, Agent.LastKnownGoal) > GoalChangedDistSq)
                {
                    Agent.CurrentDir    = FVector::ZeroVector;
                    Agent.LastKnownGoal = CurrentGoal;
                }

                // ── 在障碍内：强制推到最近可走格子 ──────────────────
                if (FlowActor->GetIntegration(Pos) < 0.f)
                {
                    FIntPoint NearCell = FlowActor->FindNearestWalkable(Pos);
                    if (NearCell.X >= 0)
                    {
                        FVector SafePos = FlowActor->GetCellCenter(NearCell);
                        SafePos.Z       = Agent.bSurfaceInitialized ? Agent.SmoothedSurfaceZ : Pos.Z;
                        Transform.SetLocation(SafePos);
                    }
                    continue;
                }

                // ── 流场方向 ─────────────────────────────────────────
                FVector DesiredDir = FlowActor->GetFlowDirectionSmooth(Pos);
                if (DesiredDir.IsNearlyZero())
                {
                    FVector ToGoal = (CurrentGoal - Pos).GetSafeNormal2D();
                    if (ToGoal.IsNearlyZero()) continue;
                    DesiredDir = ToGoal;
                }

                // ── 叠加 Boids 分离力 ─────────────────────────────────
                FVector BlendedDir = DesiredDir;
                if (!Boid.SeparationForce.IsNearlyZero())
                    BlendedDir += Boid.SeparationForce * Boid.SeparationWeight;

                BlendedDir.Z = 0.f;
                if (BlendedDir.IsNearlyZero()) continue;
                BlendedDir.Normalize();

                // ── 方向平滑 ─────────────────────────────────────────
                if (Agent.CurrentDir.IsNearlyZero())
                    Agent.CurrentDir = BlendedDir;
                else
                {
                    Agent.CurrentDir = FMath::VInterpTo(
                        Agent.CurrentDir, BlendedDir, DeltaTime, Agent.DirSmoothing);
                    if (!Agent.CurrentDir.IsNearlyZero())
                        Agent.CurrentDir.Normalize();
                }

                // ── 服务端位置校正 ────────────────────────────────────
                if (Agent.bHasCorrection)
                {
                    if (FVector::Dist2D(Pos, Agent.CorrectionTargetLocation) < 10.f)
                        Agent.bHasCorrection = false;
                    else
                    {
                        FVector CorrDir = (Agent.CorrectionTargetLocation - Pos).GetSafeNormal2D();
                        Agent.CurrentDir = FMath::VInterpTo(Agent.CurrentDir, CorrDir, DeltaTime, 8.f);
                    }
                }

                if (Agent.CurrentDir.IsNearlyZero()) continue;

                // ── 移动 ─────────────────────────────────────────────
                FVector NewPos = FVector(
                    Pos.X + Agent.CurrentDir.X * Agent.MoveSpeed * DeltaTime,
                    Pos.Y + Agent.CurrentDir.Y * Agent.MoveSpeed * DeltaTime,
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
                FRotator NewRot = FMath::RInterpTo(
                    Transform.GetRotation().Rotator(),
                    Agent.CurrentDir.ToOrientationRotator(),
                    DeltaTime, 10.f);
                Transform.SetRotation(NewRot.Quaternion());
            }
        });
}