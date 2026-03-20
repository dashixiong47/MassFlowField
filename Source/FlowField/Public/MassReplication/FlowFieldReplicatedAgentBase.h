#pragma once
#include "CoreMinimal.h"
#include "MassReplication/Public/MassReplicationTransformHandlers.h"
#include "MassReplication/Public/MassReplicationTypes.h"
#include "FlowFieldReplicatedAgentBase.generated.h"

/**
 * 复制数据基类。内置位置+Yaw，项目层继承添加自定义字段。
 * 此文件不得 include 任何其他插件头，否则 UHT 会检测到循环依赖。
 */
USTRUCT()
struct FLOWFIELD_API FFlowFieldReplicatedAgentBase : public FReplicatedAgentBase
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

private:
	UPROPERTY()
	FReplicatedAgentPositionYawData PositionYawData;
};

/**
 * 插件默认同步数据（不携带额外字段）。
 * 项目层继承 FFlowFieldReplicatedAgentBase 另建子结构体。
 */
USTRUCT()
struct FLOWFIELD_API FFlowFieldReplicatedAgent : public FFlowFieldReplicatedAgentBase
{
	GENERATED_BODY()
};