#pragma once
#include "CoreMinimal.h"
#include "MassStateTreeTypes.h"
#include "FlowFieldAttackTask.generated.h"

/**
 * StateTree Task：进入攻击状态。
 *
 * EnterState：停止移动（移除 FFlowFieldMovingTag）
 * Tick      ：持续返回 Running，由 StateTree 转换条件控制退出
 * ExitState ：无需操作（回到移动状态时 FFlowFieldMoveTask 会重新加 Tag）
 *
 * 推荐 StateTree 配置：
 *   State "移动"  → FFlowFieldMoveTask
 *                   Transition: bInAttackRange == true  → State "攻击"
 *   State "攻击"  → FFlowFieldAttackTask
 *                   Transition: bInAttackRange == false → State "移动"
 */
USTRUCT(meta = (DisplayName = "FlowField 攻击控制"))
struct FLOWFIELD_API FFlowFieldAttackTask : public FMassStateTreeTaskBase
{
    GENERATED_BODY()

    using FInstanceDataType = FMassStateTreeTaskBase::FInstanceDataType;

    virtual EStateTreeRunStatus EnterState(
        FStateTreeExecutionContext& Context,
        const FStateTreeTransitionResult& Transition) const override;

    virtual void ExitState(
        FStateTreeExecutionContext& Context,
        const FStateTreeTransitionResult& Transition) const override;

    virtual EStateTreeRunStatus Tick(
        FStateTreeExecutionContext& Context,
        const float DeltaTime) const override;
};
