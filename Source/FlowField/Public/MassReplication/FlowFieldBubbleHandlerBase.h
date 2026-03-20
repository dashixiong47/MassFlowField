#pragma once
#include "CoreMinimal.h"
#include "MassReplication/Public/MassClientBubbleHandler.h"
#include "MassReplication/Public/MassReplicationTransformHandlers.h"
#include "MassEntityView.h"
#include "MassCommonFragments.h"
#include "MassAI/FlowFieldAgentFragment.h"

// 前向声明，不 include 任何带 generated.h 的插件头
struct FFlowFieldAgentFragment;
class AFlowFieldActor;
class UFlowFieldSubsystem;

/**
 * BubbleHandler 模板基类。
 * TItemType  : 项目层 FMassFastArrayItemBase 子结构体
 * TAgentType : 项目层同步数据结构体（继承 FFlowFieldReplicatedAgentBase）
 *
 * 项目层 override：
 *   ApplyCustomDataOnSpawn  — 首次同步写入自定义字段
 *   ApplyCustomDataOnChange — 变更时更新自定义字段
 */
template<typename TItemType, typename TAgentType>
class TFlowFieldBubbleHandlerBase : public TClientBubbleHandlerBase<TItemType>
{
public:
    TMassClientBubbleTransformHandler<TItemType> TransformHandler;

    TFlowFieldBubbleHandlerBase() : TransformHandler(*this) {}

#if UE_REPLICATION_COMPILE_SERVER_CODE
    TItemType* GetMutableItem(FMassReplicatedAgentHandle Handle)
    {
        if (this->AgentHandleManager.IsValidHandle(Handle))
        {
            const FMassAgentLookupData& D = this->AgentLookupArray[Handle.GetIndex()];
            return &(*this->Agents)[D.AgentsIdx];
        }
        return nullptr;
    }
    void MarkItemDirty(TItemType& Item) const { this->Serializer->MarkItemDirty(Item); }
#endif

#if UE_REPLICATION_COMPILE_CLIENT_CODE
    virtual void PostReplicatedAdd(
        const TArrayView<int32> AddedIndices, int32 FinalSize) override
    {
        auto AddReqs = [](FMassEntityQuery& Q)
        {
            TMassClientBubbleTransformHandler<TItemType>::AddRequirementsForSpawnQuery(Q);
        };
        auto CacheViews = [this](FMassExecutionContext& Ctx)
        {
            TransformHandler.CacheFragmentViewsForSpawnQuery(Ctx);
        };
        auto SetSpawned = [this](const FMassEntityView& View, const TAgentType& Item, int32 Idx)
        {
            TransformHandler.SetSpawnedEntityData(Idx, Item.GetReplicatedPositionYawData());
            ApplyCustomDataOnSpawn(View, Item);
        };
        auto SetModified = [](const FMassEntityView& View, const TAgentType& Item)
        {
            TMassClientBubbleTransformHandler<TItemType>::SetModifiedEntityData(
                View, Item.GetReplicatedPositionYawData());
        };
        this->PostReplicatedAddHelper(AddedIndices, AddReqs, CacheViews, SetSpawned, SetModified);
        TransformHandler.ClearFragmentViewsForSpawnQuery();
    }

    virtual void PostReplicatedChange(
        const TArrayView<int32> ChangedIndices, int32 FinalSize) override
    {
        auto Update = [this](const FMassEntityView& View, const TAgentType& Item)
        {
            const FReplicatedAgentPositionYawData& P = Item.GetReplicatedPositionYawData();

            // 直接写入 FTransformFragment，确保客户端位置立即生效。
            // 不依赖 FFlowFieldAgentFragment 或 ClientInterpProcessor，
            // 对任何 Archetype 都有效。
            if (FTransformFragment* TF = View.GetFragmentDataPtr<FTransformFragment>())
            {
                FTransform& T = TF->GetMutableTransform();
                T.SetLocation(FVector(P.GetPosition()));
                T.SetRotation(FQuat(FRotator(0.f, P.GetYaw(), 0.f)));
            }

            // 如果实体有 FFlowFieldAgentFragment，额外写入校正目标供插值 Processor 平滑。
            if (FFlowFieldAgentFragment* Agent =
                    View.GetFragmentDataPtr<FFlowFieldAgentFragment>())
            {
                Agent->CorrectionTargetLocation = FVector(P.GetPosition());
                Agent->CorrectionTargetYaw      = P.GetYaw();
                Agent->bHasCorrection           = true;
            }

            ApplyCustomDataOnChange(View, Item);
        };
        this->PostReplicatedChangeHelper(ChangedIndices, Update);
    }

protected:
    virtual void ApplyCustomDataOnSpawn(const FMassEntityView&, const TAgentType&) {}
    virtual void ApplyCustomDataOnChange(const FMassEntityView&, const TAgentType&) {}
#endif
};