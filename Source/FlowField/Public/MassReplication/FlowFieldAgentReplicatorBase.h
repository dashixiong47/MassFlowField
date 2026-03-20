#pragma once
#include "CoreMinimal.h"
#include "MassReplicationProcessor.h"
#include "MassEntityQuery.h"
#include "MassReplication/Public/MassReplicationTransformHandlers.h"
#include "FlowFieldAgentReplicatorBase.generated.h"

/**
 * Replicator 抽象基类。
 * 项目层继承并 override：
 *   AddCustomRequirements         — 追加 Fragment 查询
 *   ProcessClientReplicationInternal — 实现 Add/Modify/Remove
 */
UCLASS(Abstract)
class FLOWFIELD_API UFlowFieldAgentReplicatorBase : public UMassReplicatorBase
{
	GENERATED_BODY()
public:
	virtual void AddRequirements(FMassEntityQuery& EntityQuery) override
	{
		FMassReplicationProcessorPositionYawHandler::AddRequirements(EntityQuery);
		AddCustomRequirements(EntityQuery);
	}

	virtual void ProcessClientReplication(
		FMassExecutionContext& Context,
		FMassReplicationContext& RepContext) override final
	{
		PositionYawHandler.CacheFragmentViews(Context);
		ProcessClientReplicationInternal(Context, RepContext);
	}

protected:
	FMassReplicationProcessorPositionYawHandler PositionYawHandler;

	virtual void AddCustomRequirements(FMassEntityQuery& EntityQuery) {}

	virtual void ProcessClientReplicationInternal(
		FMassExecutionContext& Context,
		FMassReplicationContext& RepContext)
		PURE_VIRTUAL(UFlowFieldAgentReplicatorBase::ProcessClientReplicationInternal, );
};