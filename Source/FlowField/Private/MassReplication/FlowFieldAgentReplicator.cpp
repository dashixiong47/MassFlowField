#include "MassReplication/FlowFieldAgentReplicator.h"
#include "MassAI/FlowFieldAgentFragment.h"
#include "MassReplication/Public/MassReplicationFragments.h"
#include "MassReplication/Public/MassReplicationTransformHandlers.h"
#include "MassExecutionContext.h"
#include "FlowFieldSubsystem.h"
#include "FlowFieldActor.h"
#include "Net/UnrealNetwork.h"

// ── BubbleInfo ────────────────────────────────────────────────────
AFlowFieldClientBubbleInfo::AFlowFieldClientBubbleInfo(
    const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    bReplicates = true;
    Serializers.Add(&BubbleSerializer);
}

void AFlowFieldClientBubbleInfo::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    FDoRepLifetimeParams Params;
    Params.bIsPushBased = true;
    DOREPLIFETIME_WITH_PARAMS_FAST(AFlowFieldClientBubbleInfo, BubbleSerializer, Params);
}

// ── BubbleHandler Apply（地面修正 + 位置校正）────────────────────
#if UE_REPLICATION_COMPILE_CLIENT_CODE

void FFlowFieldClientBubbleHandler::ApplyCustomDataOnSpawn(
    const FMassEntityView& EntityView,
    const FFlowFieldReplicatedAgent& Item)
{
    if (!Serializer) return;
    UWorld* World = Serializer->GetWorld();
    if (!World) return;

    UFlowFieldSubsystem* FlowSub = World->GetSubsystem<UFlowFieldSubsystem>();
    AFlowFieldActor* FlowActor   = FlowSub ? FlowSub->GetActor() : nullptr;
    if (!FlowActor || !FlowActor->bReady) return;

    const FVector Loc    = FVector(Item.GetReplicatedPositionYawData().GetPosition());
    float         SurfZ  = Loc.Z;
    FVector       Normal = FVector::UpVector;
    if (!FlowActor->GetSurfaceData(Loc, SurfZ, Normal)) return;

    if (FTransformFragment* TF = EntityView.GetFragmentDataPtr<FTransformFragment>())
    {
        FVector Fixed = Loc; Fixed.Z = SurfZ;
        TF->GetMutableTransform().SetLocation(Fixed);
    }
    if (FFlowFieldAgentFragment* Agent = EntityView.GetFragmentDataPtr<FFlowFieldAgentFragment>())
    {
        Agent->SmoothedSurfaceZ    = SurfZ;
        Agent->SmoothedNormal      = Normal;
        Agent->bSurfaceInitialized = true;
    }
}

void FFlowFieldClientBubbleHandler::ApplyCustomDataOnChange(
    const FMassEntityView& EntityView,
    const FFlowFieldReplicatedAgent& Item)
{
    // FTransformFragment 写入和 FFlowFieldAgentFragment::bHasCorrection 写入
    // 均已在基类 PostReplicatedChange 的 Update lambda 里完成，此处无需重复。
}

#endif // UE_REPLICATION_COMPILE_CLIENT_CODE

// ── Replicator ────────────────────────────────────────────────────
void UFlowFieldAgentReplicator::ProcessClientReplicationInternal(
    FMassExecutionContext&    Context,
    FMassReplicationContext& RepContext)
{
    FMassReplicationSharedFragment*     RepSharedFrag      = nullptr;
    TConstArrayView<FTransformFragment> TransformFragments;

    auto Cache = [&](FMassExecutionContext& Ctx)
    {
        RepSharedFrag      = &Ctx.GetMutableSharedFragment<FMassReplicationSharedFragment>();
        TransformFragments =  Ctx.GetFragmentView<FTransformFragment>();
    };

    auto Add = [&](FMassExecutionContext& Ctx, int32 Idx,
                   FFlowFieldReplicatedAgent& Out, FMassClientHandle Client)
    {
        AFlowFieldClientBubbleInfo& BubbleInfo =
            static_cast<AFlowFieldClientBubbleInfo&>(
                RepSharedFrag->GetTypedClientBubbleInfoChecked<AFlowFieldClientBubbleInfo>(Client));
        PositionYawHandler.AddEntity(Idx, Out.GetReplicatedPositionYawDataMutable());
        return BubbleInfo.GetBubbleSerializer().Bubble.AddAgent(Ctx.GetEntity(Idx), Out);
    };

    auto Modify = [&](FMassExecutionContext& Ctx, int32 Idx, EMassLOD::Type LOD, double Time,
                      FMassReplicatedAgentHandle Handle, FMassClientHandle Client)
    {
        AFlowFieldClientBubbleInfo& BubbleInfo =
            static_cast<AFlowFieldClientBubbleInfo&>(
                RepSharedFrag->GetTypedClientBubbleInfoChecked<AFlowFieldClientBubbleInfo>(Client));
        auto& Bubble = BubbleInfo.GetBubbleSerializer().Bubble;

        FFlowFieldFastArrayItem* Item = Bubble.GetMutableItem(Handle);
        if (!Item) return;

        const FVector& CurPos = TransformFragments[Idx].GetTransform().GetLocation();
        const FVector  OldPos = FVector(Item->Agent.GetReplicatedPositionYawData().GetPosition());

        constexpr float LocationTolerance = 5.f;
        if (!FVector::PointsAreNear(CurPos, OldPos, LocationTolerance))
        {
            PositionYawHandler.ModifyEntity(Handle, Idx, Bubble.TransformHandler);
            Bubble.MarkItemDirty(*Item);
        }
    };

    auto Remove = [&](FMassExecutionContext& Ctx,
                      FMassReplicatedAgentHandle Handle, FMassClientHandle Client)
    {
        AFlowFieldClientBubbleInfo& BubbleInfo =
            static_cast<AFlowFieldClientBubbleInfo&>(
                RepSharedFrag->GetTypedClientBubbleInfoChecked<AFlowFieldClientBubbleInfo>(Client));
        BubbleInfo.GetBubbleSerializer().Bubble.RemoveAgent(Handle);
    };

    CalculateClientReplication<FFlowFieldFastArrayItem>(
        Context, RepContext, Cache, Add, Modify, Remove);
}