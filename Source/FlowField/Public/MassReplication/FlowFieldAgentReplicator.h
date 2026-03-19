#pragma once
#include "CoreMinimal.h"
#include "MassReplicationTypes.h"
#include "MassClientBubbleHandler.h"
#include "MassClientBubbleInfoBase.h"
#include "MassClientBubbleSerializerBase.h"
#include "MassCommonFragments.h"
#include "MassReplicationProcessor.h"
#include "MassReplicationFragments.h"
#include "MassReplicationTransformHandlers.h"
#include "Net/UnrealNetwork.h"
#include "MassAI/FlowFieldAgentFragment.h"
#include "FlowFieldAgentReplicator.generated.h"

// ── 1. 同步数据结构 ─────────────────────────────────────────────
USTRUCT()
struct FLOWFIELD_API FFlowFieldReplicatedAgent : public FReplicatedAgentBase
{
    GENERATED_BODY()

    FReplicatedAgentPositionYawData& GetReplicatedPositionYawDataMutable()
    {
        return PositionYawData;
    }

    const FReplicatedAgentPositionYawData& GetReplicatedPositionYawData() const
    {
        return PositionYawData;
    }

    uint8 GetAnimFlags() const { return AnimFlags; }
    void  SetAnimFlags(uint8 InFlags) { AnimFlags = InFlags; }

private:
    UPROPERTY()
    FReplicatedAgentPositionYawData PositionYawData;

    UPROPERTY()
    uint8 AnimFlags = 0;
};

// ── 2. FastArray Item ─────────────────────────────────────────────
USTRUCT()
struct FLOWFIELD_API FFlowFieldFastArrayItem : public FMassFastArrayItemBase
{
    GENERATED_BODY()

    typedef FFlowFieldReplicatedAgent FReplicatedAgentType;

    FFlowFieldFastArrayItem() = default;

    FFlowFieldFastArrayItem(const FFlowFieldReplicatedAgent& InAgent, const FMassReplicatedAgentHandle InHandle)
        : FMassFastArrayItemBase(InHandle)
        , Agent(InAgent)
    {}

    UPROPERTY()
    FFlowFieldReplicatedAgent Agent;
};

// ── 3. BubbleHandler ─────────────────────────────────────────────
class FLOWFIELD_API FFlowFieldClientBubbleHandler
    : public TClientBubbleHandlerBase<FFlowFieldFastArrayItem>
{
public:
    TMassClientBubbleTransformHandler<FFlowFieldFastArrayItem> TransformHandler;

    FFlowFieldClientBubbleHandler()
        : TransformHandler(*this)
    {}

#if UE_REPLICATION_COMPILE_SERVER_CODE
    FFlowFieldFastArrayItem* GetMutableItem(FMassReplicatedAgentHandle Handle)
    {
        if (AgentHandleManager.IsValidHandle(Handle))
        {
            const FMassAgentLookupData& LookUpData = AgentLookupArray[Handle.GetIndex()];
            return &(*Agents)[LookUpData.AgentsIdx];
        }
        return nullptr;
    }

    void MarkItemDirty(FFlowFieldFastArrayItem& Item) const
    {
        Serializer->MarkItemDirty(Item);
    }
#endif

#if UE_REPLICATION_COMPILE_CLIENT_CODE
    virtual void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize) override;
    virtual void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize) override;
#endif
};

// ── 4. Serializer ─────────────────────────────────────────────
USTRUCT()
struct FLOWFIELD_API FFlowFieldClientBubbleSerializer : public FMassClientBubbleSerializerBase
{
    GENERATED_BODY()

    FFlowFieldClientBubbleSerializer()
    {
        Bubble.Initialize(Entities, *this);
    }

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FFlowFieldFastArrayItem, FFlowFieldClientBubbleSerializer>(Entities, DeltaParams, *this);
    }

    FFlowFieldClientBubbleHandler Bubble;

protected:
    UPROPERTY(Transient)
    TArray<FFlowFieldFastArrayItem> Entities;
};

template<>
struct TStructOpsTypeTraits<FFlowFieldClientBubbleSerializer>
    : public TStructOpsTypeTraitsBase2<FFlowFieldClientBubbleSerializer>
{
    enum
    {
        WithNetDeltaSerializer = true,
        WithCopy = false,
    };
};

// ── 5. Replicator ─────────────────────────────────────────────
UCLASS()
class FLOWFIELD_API UFlowFieldAgentReplicator : public UMassReplicatorBase
{
    GENERATED_BODY()
public:

    virtual void AddRequirements(FMassEntityQuery& EntityQuery) override;
    virtual void ProcessClientReplication(FMassExecutionContext& Context, FMassReplicationContext& ReplicationContext) override;

private:
    FMassReplicationProcessorPositionYawHandler PositionYawHandler;

};

// ── 6. BubbleInfo ─────────────────────────────────────────────
UCLASS()
class FLOWFIELD_API AFlowFieldClientBubbleInfo : public AMassClientBubbleInfoBase
{
    GENERATED_BODY()
public:
    AFlowFieldClientBubbleInfo(const FObjectInitializer& ObjectInitializer);
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    FFlowFieldClientBubbleSerializer& GetBubbleSerializer() { return BubbleSerializer; }

protected:
    UPROPERTY(Replicated, Transient)
    FFlowFieldClientBubbleSerializer BubbleSerializer;
};