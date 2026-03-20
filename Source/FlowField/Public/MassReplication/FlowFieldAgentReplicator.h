#pragma once
#include "CoreMinimal.h"
#include "MassReplication/Public/MassReplicationTypes.h"
#include "MassReplication/Public/MassClientBubbleHandler.h"
#include "MassReplication/Public/MassClientBubbleInfoBase.h"
#include "MassReplication/Public/MassClientBubbleSerializerBase.h"
#include "MassReplication/Public/MassReplicationProcessor.h"
#include "MassReplication/Public/MassReplicationFragments.h"
#include "MassReplication/Public/MassReplicationTransformHandlers.h"
#include "MassCommon/Public/MassCommonFragments.h"
#include "Net/UnrealNetwork.h"
#include "MassAI/FlowFieldAgentFragment.h"
#include "MassReplication/FlowFieldReplicatedAgentBase.h"
#include "MassReplication/FlowFieldAgentReplicatorBase.h"
#include "MassReplication/FlowFieldBubbleHandlerBase.h"
#include "FlowFieldAgentReplicator.generated.h"

// ── FastArray Item ─────────────────────────────────────────────────
USTRUCT()
struct FLOWFIELD_API FFlowFieldFastArrayItem : public FMassFastArrayItemBase
{
    GENERATED_BODY()
    typedef FFlowFieldReplicatedAgent FReplicatedAgentType;
    FFlowFieldFastArrayItem() = default;
    FFlowFieldFastArrayItem(const FFlowFieldReplicatedAgent& InAgent,
                             const FMassReplicatedAgentHandle InHandle)
        : FMassFastArrayItemBase(InHandle), Agent(InAgent) {}
    UPROPERTY()
    FFlowFieldReplicatedAgent Agent;
};

// ── BubbleHandler（插件默认，Apply 在 cpp 里实现）─────────────────
class FLOWFIELD_API FFlowFieldClientBubbleHandler
    : public TFlowFieldBubbleHandlerBase<FFlowFieldFastArrayItem, FFlowFieldReplicatedAgent>
{
#if UE_REPLICATION_COMPILE_CLIENT_CODE
protected:
    virtual void ApplyCustomDataOnSpawn(
        const FMassEntityView& EntityView,
        const FFlowFieldReplicatedAgent& Item) override;
    virtual void ApplyCustomDataOnChange(
        const FMassEntityView& EntityView,
        const FFlowFieldReplicatedAgent& Item) override;
#endif
};

// ── Serializer ─────────────────────────────────────────────────────
USTRUCT()
struct FLOWFIELD_API FFlowFieldClientBubbleSerializer : public FMassClientBubbleSerializerBase
{
    GENERATED_BODY()
    FFlowFieldClientBubbleSerializer() { Bubble.Initialize(Entities, *this); }
    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<
            FFlowFieldFastArrayItem, FFlowFieldClientBubbleSerializer>(Entities, DeltaParams, *this);
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
    enum { WithNetDeltaSerializer = true, WithCopy = false };
};

// ── Replicator（插件默认，仅同步位置+Yaw）─────────────────────────
UCLASS()
class FLOWFIELD_API UFlowFieldAgentReplicator : public UFlowFieldAgentReplicatorBase
{
    GENERATED_BODY()
protected:
    virtual void ProcessClientReplicationInternal(
        FMassExecutionContext& Context,
        FMassReplicationContext& RepContext) override;
};

// ── BubbleInfo Actor ───────────────────────────────────────────────
UCLASS()
class FLOWFIELD_API AFlowFieldClientBubbleInfo : public AMassClientBubbleInfoBase
{
    GENERATED_BODY()
public:
    AFlowFieldClientBubbleInfo(const FObjectInitializer& ObjectInitializer);
    virtual void GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    FFlowFieldClientBubbleSerializer& GetBubbleSerializer() { return BubbleSerializer; }
protected:
    UPROPERTY(Replicated, Transient)
    FFlowFieldClientBubbleSerializer BubbleSerializer;
};