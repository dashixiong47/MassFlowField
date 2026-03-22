#include "MassStateTree/FlowFieldAttackTask.h"
#include "MassAI/FlowFieldAgentFragment.h"
#include "MassStateTreeExecutionContext.h"
#include "MassEntityManager.h"
#include "MassEntityView.h"
#include "MassCommandBuffer.h"

EStateTreeRunStatus FFlowFieldAttackTask::EnterState(
    FStateTreeExecutionContext& Context,
    const FStateTreeTransitionResult& Transition) const
{
    FMassStateTreeExecutionContext& MassContext =
        static_cast<FMassStateTreeExecutionContext&>(Context);

    FMassEntityHandle Entity = MassContext.GetEntity();
    if (!Entity.IsValid()) return EStateTreeRunStatus::Running;

    FMassEntityView EntityView(MassContext.GetEntityManager(), Entity);

    // 停止移动：移除 MovingTag，MovementProcessor 将不再驱动此实体
    if (EntityView.HasTag<FFlowFieldMovingTag>())
    {
        MassContext.GetEntityManager().Defer().RemoveTag<FFlowFieldMovingTag>(Entity);
    }

    return EStateTreeRunStatus::Running;
}

void FFlowFieldAttackTask::ExitState(
    FStateTreeExecutionContext& Context,
    const FStateTreeTransitionResult& Transition) const
{
    // 无需操作：回到移动状态时 FFlowFieldMoveTask::EnterState 会重新添加 MovingTag
}

EStateTreeRunStatus FFlowFieldAttackTask::Tick(
    FStateTreeExecutionContext& Context,
    const float DeltaTime) const
{
    return EStateTreeRunStatus::Running;
}
