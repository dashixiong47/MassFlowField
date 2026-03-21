 #include "MassAI/FlowFieldClientInterpProcessor.h"
#include "MassExecutionContext.h"
#include "MassEntityManager.h"
#include "MassCommonTypes.h"
#include "FlowFieldSubsystem.h"
#include "FlowFieldActor.h"

UFlowFieldClientInterpProcessor::UFlowFieldClientInterpProcessor()
{
    ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Movement;
    // 必须在 MovementProcessor 之后，等移动位置确定后再做地面贴合和校正插值
    ExecutionOrder.ExecuteAfter.Add(TEXT("FlowFieldMovementProcessor"));
    ExecutionFlags = (int32)(EProcessorExecutionFlags::Client);
    bAutoRegisterWithProcessingPhases = true;
}

void UFlowFieldClientInterpProcessor::ConfigureQueries(
    const TSharedRef<FMassEntityManager>& EntityManager)
{
    EntityQuery.Initialize(EntityManager);
    EntityQuery.AddTagRequirement<FFlowFieldAgentTag>(EMassFragmentPresence::All);
    EntityQuery.AddTagRequirement<FFlowFieldMovingTag>(EMassFragmentPresence::All);
    EntityQuery.AddRequirement<FFlowFieldAgentFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.RegisterWithProcessor(*this);
    RegisterQuery(EntityQuery);
}

void UFlowFieldClientInterpProcessor::Execute(
    FMassEntityManager& EntityManager,
    FMassExecutionContext& Context)
{
    const float DeltaTime = Context.GetDeltaTimeSeconds();

    UWorld*          World     = GetWorld();
    AFlowFieldActor* FlowActor = nullptr;
    if (UFlowFieldSubsystem* FlowSub = World ? World->GetSubsystem<UFlowFieldSubsystem>() : nullptr)
        FlowActor = FlowSub->GetActor();

    EntityQuery.ForEachEntityChunk(Context,
        [&](FMassExecutionContext& ChunkContext)
        {
            auto Agents     = ChunkContext.GetMutableFragmentView<FFlowFieldAgentFragment>();
            auto Transforms = ChunkContext.GetMutableFragmentView<FTransformFragment>();
            const int32 Num = ChunkContext.GetNumEntities();

            for (int32 i = 0; i < Num; i++)
            {
                FFlowFieldAgentFragment& Agent     = Agents[i];
                FTransform&             Transform  = Transforms[i].GetMutableTransform();

                // ── 服务端位置校正 ────────────────────────────────────────
                // 收到服务端位置后慢慢拉回，不影响本地流畅感
                if (Agent.bHasCorrection)
                {
                    const FVector CurPos    = Transform.GetLocation();
                    const FVector TargetPos = Agent.CorrectionTargetLocation;
                    const float   Dist      = FVector::Dist2D(CurPos, TargetPos);

                    if (Dist < 10.f)
                    {
                        // 偏差极小，直接清除
                        Agent.bHasCorrection = false;
                    }
                    else if (Dist > 200.f)
                    {
                        // 偏差过大（如刚加入游戏），直接传送
                        FVector SnapPos    = TargetPos;
                        float   SnapZ      = CurPos.Z;
                        FVector SnapNormal = FVector::UpVector;
                        if (FlowActor)
                            FlowActor->GetSavedSurfaceData(SnapPos, SnapZ, SnapNormal);
                        SnapPos.Z = SnapZ;
                        Transform.SetLocation(SnapPos);
                        Agent.bHasCorrection = false;
                    }
                    else
                    {
                        // 中等偏差：每帧校正 5%，保持本地预测流畅
                        FVector NewPos     = FMath::Lerp(CurPos, TargetPos, 0.05f);
                        float   LocalZ     = CurPos.Z;
                        FVector LocalNormal = FVector::UpVector;
                        if (FlowActor)
                            FlowActor->GetSavedSurfaceData(NewPos, LocalZ, LocalNormal);
                        NewPos.Z = LocalZ;
                        Transform.SetLocation(NewPos);
                    }
                }

                // ── 地面高度贴合（客户端本地）──────────────────────────
                if (Agent.bSurfaceInitialized && FlowActor)
                {
                    const FVector Pos = Transform.GetLocation();
                    float   LocalZ    = Pos.Z;
                    FVector LocalNormal = FVector::UpVector;
                    if (FlowActor->GetSavedSurfaceData(Pos, LocalZ, LocalNormal)
                        && LocalZ != 0.f && FMath::Abs(LocalZ - Pos.Z) <= 500.f)
                    {
                        Agent.SmoothedSurfaceZ = FMath::FInterpTo(
                            Agent.SmoothedSurfaceZ, LocalZ, DeltaTime, Agent.SurfaceZSmoothSpeed);

                        FVector FixPos = Pos;
                        FixPos.Z = Agent.SmoothedSurfaceZ;
                        Transform.SetLocation(FixPos);

                        // 纯 Yaw 旋转，不跟随地面法线
                        if (!Agent.CurrentDir.IsNearlyZero())
                        {
                            FRotator NewRot = FMath::RInterpTo(
                                Transform.GetRotation().Rotator(),
                                Agent.CurrentDir.ToOrientationRotator(),
                                DeltaTime, 10.f);
                            Transform.SetRotation(NewRot.Quaternion());
                        }
                    }
                }
            }
        });
}