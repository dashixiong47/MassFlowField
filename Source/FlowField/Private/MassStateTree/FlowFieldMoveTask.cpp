#include "MassStateTree/FlowFieldMoveTask.h"
#include "MassAI/FlowFieldAgentFragment.h"
#include "MassStateTreeExecutionContext.h"
#include "MassEntityManager.h"
#include "MassEntityView.h"
#include "MassCommandBuffer.h"

EStateTreeRunStatus FFlowFieldMoveTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FMassStateTreeExecutionContext& MassContext =
		static_cast<FMassStateTreeExecutionContext&>(Context);

	FMassEntityManager& EntityManager = MassContext.GetEntityManager();
	FMassEntityHandle  Entity         = MassContext.GetEntity();

	if (!Entity.IsValid()) return EStateTreeRunStatus::Running;

	// FMassEntityView 是 5.4+ 检查 Tag 的正确方式
	FMassEntityView EntityView(EntityManager, Entity);
	if (!EntityView.HasTag<FFlowFieldMovingTag>())
	{
		// Tag 的增删必须通过 Defer，不能直接在 StateTree 里同步操作
		MassContext.GetEntityManager().Defer().AddTag<FFlowFieldMovingTag>(Entity);
	}

	return EStateTreeRunStatus::Running;
}

void FFlowFieldMoveTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FMassStateTreeExecutionContext& MassContext =
		static_cast<FMassStateTreeExecutionContext&>(Context);

	FMassEntityHandle Entity = MassContext.GetEntity();
	if (!Entity.IsValid()) return;

	FMassEntityView EntityView(MassContext.GetEntityManager(), Entity);
	if (EntityView.HasTag<FFlowFieldMovingTag>())
	{
		MassContext.GetEntityManager().Defer().RemoveTag<FFlowFieldMovingTag>(Entity);
	}
}

EStateTreeRunStatus FFlowFieldMoveTask::Tick(
	FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	return EStateTreeRunStatus::Running;
}