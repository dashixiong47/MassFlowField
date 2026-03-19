#pragma once
#include "CoreMinimal.h"
#include "MassStateTreeTypes.h"
#include "MassStateTreeTypes.h"
#include "FlowFieldMoveTask.generated.h"

// StateTree Task：进入状态时开启 FlowField 移动，退出时停止
// 在 StateTree 资产里添加此 Task 到对应状态节点即可

USTRUCT(meta = (DisplayName = "FlowField 移动控制"))
struct FLOWFIELD_API FFlowFieldMoveTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FMassStateTreeTaskBase::FInstanceDataType;

	// 进入状态：加 FFlowFieldMovingTag，开始移动
	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	// 退出状态：移除 FFlowFieldMovingTag，停止移动
	virtual void ExitState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	// 每帧返回 Running，保持状态
	virtual EStateTreeRunStatus Tick(
		FStateTreeExecutionContext& Context,
		const float DeltaTime) const override;
};