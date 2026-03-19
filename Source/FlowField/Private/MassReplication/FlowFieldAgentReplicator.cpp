#include "MassReplication/FlowFieldAgentReplicator.h"
#include "MassAI/FlowFieldAgentFragment.h"
#include "MassReplicationFragments.h"
#include "MassSimulationLOD.h"
#include "MassReplicationSubsystem.h"
#include "MassReplicationFragments.h"
#include "MassReplicationTransformHandlers.h"
#include "MassExecutionContext.h"
#include "FlowFieldSubsystem.h"
#include "FlowFieldActor.h"

// ── BubbleInfo ─────────────────────────────────────────────

AFlowFieldClientBubbleInfo::AFlowFieldClientBubbleInfo(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    bReplicates = true;
    Serializers.Add(&BubbleSerializer);
}

void AFlowFieldClientBubbleInfo::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    FDoRepLifetimeParams SharedParams;
    SharedParams.bIsPushBased = true;
    DOREPLIFETIME_WITH_PARAMS_FAST(AFlowFieldClientBubbleInfo, BubbleSerializer, SharedParams);
}

// ── Client Handler ─────────────────────────────────────────────

#if UE_REPLICATION_COMPILE_CLIENT_CODE

void FFlowFieldClientBubbleHandler::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{

    // 通过 Serializer 获取 World，BubbleHandler 自身不继承 UObject 没有 GetWorld
    AFlowFieldActor* FlowActor = nullptr;
    if (Serializer)
    {
        if (UWorld* World = Serializer->GetWorld())
        {
            if (UFlowFieldSubsystem* FlowSub = World->GetSubsystem<UFlowFieldSubsystem>())
            {
                FlowActor = FlowSub->GetActor();
            }
        }
    }

    auto AddReqs = [](FMassEntityQuery& Query)
    {
        TMassClientBubbleTransformHandler<FFlowFieldFastArrayItem>::AddRequirementsForSpawnQuery(Query);
    };

    auto CacheViews = [this](FMassExecutionContext& Context)
    {
        TransformHandler.CacheFragmentViewsForSpawnQuery(Context);
    };

    auto SetSpawned = [this, FlowActor](const FMassEntityView& EntityView,
                                        const FFlowFieldReplicatedAgent& Item, int32 Idx)
    {
        // 设置初始位置
        TransformHandler.SetSpawnedEntityData(Idx, Item.GetReplicatedPositionYawData());

        // 用 FlowField 地面高度修正 Z，避免浮空
        if (FlowActor && FlowActor->bReady)
        {
            const FVector Loc = Item.GetReplicatedPositionYawData().GetPosition();
            float SurfaceZ    = Loc.Z;
            FVector Normal    = FVector::UpVector;

            if (FlowActor->GetSurfaceData(Loc, SurfaceZ, Normal))
            {
                if (FTransformFragment* TF = EntityView.GetFragmentDataPtr<FTransformFragment>())
                {
                    FVector FixedLoc = Loc;
                    FixedLoc.Z = SurfaceZ;
                    TF->GetMutableTransform().SetLocation(FixedLoc);
                }

                // 同时初始化 AgentFragment 地面数据，MovementProcessor 第一帧直接贴地不插值
                if (FFlowFieldAgentFragment* Agent = EntityView.GetFragmentDataPtr<FFlowFieldAgentFragment>())
                {
                    Agent->SmoothedSurfaceZ    = SurfaceZ;
                    Agent->SmoothedNormal      = Normal;
                    Agent->bSurfaceInitialized = true;
                }
            }
        }
    };

    auto SetModified = [](const FMassEntityView& EntityView, const FFlowFieldReplicatedAgent& Item)
    {
        TMassClientBubbleTransformHandler<FFlowFieldFastArrayItem>::SetModifiedEntityData(
            EntityView, Item.GetReplicatedPositionYawData());
    };

    PostReplicatedAddHelper(AddedIndices, AddReqs, CacheViews, SetSpawned, SetModified);
    TransformHandler.ClearFragmentViewsForSpawnQuery();
}

void FFlowFieldClientBubbleHandler::PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize)
{
    // 收到服务端位置校正，写入 bHasCorrection，由 ClientInterpProcessor 平滑插值
    UE_LOG(LogTemp, Warning, TEXT("[FlowField][Client] PostReplicatedChange 校正 %d 个实体"), ChangedIndices.Num());

    auto Update = [](const FMassEntityView& EntityView, const FFlowFieldReplicatedAgent& Item)
    {
        // 写入校正目标，不直接跳位，由 ClientInterpProcessor 平滑过去
        FFlowFieldAgentFragment& Agent = EntityView.GetFragmentData<FFlowFieldAgentFragment>();
        const FReplicatedAgentPositionYawData& PosData = Item.GetReplicatedPositionYawData();
        Agent.CorrectionTargetLocation = FVector(PosData.GetPosition());
        Agent.CorrectionTargetYaw      = PosData.GetYaw();
        Agent.bHasCorrection           = true;
    };

    PostReplicatedChangeHelper(ChangedIndices, Update);
}

#endif // UE_REPLICATION_COMPILE_CLIENT_CODE

// ── Replicator ─────────────────────────────────────────────

void UFlowFieldAgentReplicator::AddRequirements(FMassEntityQuery& Query)
{
    // 只加业务需要的 Fragment，其余 Replication Fragment 由框架内部处理
    FMassReplicationProcessorPositionYawHandler::AddRequirements(Query);
}

void UFlowFieldAgentReplicator::ProcessClientReplication(FMassExecutionContext& Context,
                                                         FMassReplicationContext& RepContext)
{
    PositionYawHandler.CacheFragmentViews(Context);

    FMassReplicationSharedFragment* RepSharedFrag = nullptr;

    TConstArrayView<FTransformFragment> TransformFragments;

    auto Cache = [&](FMassExecutionContext& Ctx)
    {
        RepSharedFrag = &Ctx.GetMutableSharedFragment<FMassReplicationSharedFragment>();
        TransformFragments = Ctx.GetFragmentView<FTransformFragment>();
    };

    // Add：首次同步初始位置
    auto Add = [&](FMassExecutionContext& Ctx, int32 Idx,
                   FFlowFieldReplicatedAgent& Out, FMassClientHandle Client)
    {
        AFlowFieldClientBubbleInfo& BubbleInfo =
            static_cast<AFlowFieldClientBubbleInfo&>(
                RepSharedFrag->GetTypedClientBubbleInfoChecked<AFlowFieldClientBubbleInfo>(Client));

        PositionYawHandler.AddEntity(Idx, Out.GetReplicatedPositionYawDataMutable());

        return BubbleInfo.GetBubbleSerializer().Bubble.AddAgent(Ctx.GetEntity(Idx), Out);
    };

    // 服务端权威，每帧位置变化由 Mass Replication 自动同步给客户端
    auto Modify = [&](FMassExecutionContext& Ctx, int32 Idx, EMassLOD::Type LOD, double Time,
                      FMassReplicatedAgentHandle Handle, FMassClientHandle Client)
    {
        AFlowFieldClientBubbleInfo& BubbleInfo =
            static_cast<AFlowFieldClientBubbleInfo&>(
                RepSharedFrag->GetTypedClientBubbleInfoChecked<AFlowFieldClientBubbleInfo>(Client));
        auto& Bubble = BubbleInfo.GetBubbleSerializer().Bubble;

        // 只有位置变化超过阈值才同步，减少带宽
        // LOD 越低同步频率越低（由 Mass Replication LOD 系统控制）
        FFlowFieldFastArrayItem* Item = Bubble.GetMutableItem(Handle);
        if (!Item) return;

        const FVector& CurPos = TransformFragments[Idx].GetTransform().GetLocation();
        const FVector  OldPos = FVector(Item->Agent.GetReplicatedPositionYawData().GetPosition());

        // 位置变化超过 5cm 才标脏同步，静止的实体不产生带宽
        constexpr float LocationTolerance = 5.f;
        if (!FVector::PointsAreNear(CurPos, OldPos, LocationTolerance))
        {
            PositionYawHandler.ModifyEntity(Handle, Idx, Bubble.TransformHandler);
            Bubble.MarkItemDirty(*Item);
        }
    };

    auto Remove = [&](FMassExecutionContext& Ctx, FMassReplicatedAgentHandle Handle, FMassClientHandle Client)
    {
        AFlowFieldClientBubbleInfo& BubbleInfo =
            static_cast<AFlowFieldClientBubbleInfo&>(
                RepSharedFrag->GetTypedClientBubbleInfoChecked<AFlowFieldClientBubbleInfo>(Client));
        BubbleInfo.GetBubbleSerializer().Bubble.RemoveAgent(Handle);
    };

    CalculateClientReplication<FFlowFieldFastArrayItem>(Context, RepContext, Cache, Add, Modify, Remove);
}