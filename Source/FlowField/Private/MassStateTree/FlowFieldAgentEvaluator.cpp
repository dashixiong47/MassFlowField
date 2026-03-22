#include "MassStateTree/FlowFieldAgentEvaluator.h"
#include "MassStateTreeExecutionContext.h"
#include "StateTreeLinker.h"

bool FFlowFieldAgentEvaluator::Link(FStateTreeLinker& Linker)
{
    Linker.LinkExternalData(TransformHandle);
    Linker.LinkExternalData(AgentHandle);
    return true;
}

void FFlowFieldAgentEvaluator::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
    const FFlowFieldAgentFragment& Agent = Context.GetExternalData(AgentHandle);
    const FTransformFragment&      Trans = Context.GetExternalData(TransformHandle);

    FFlowFieldAgentEvaluatorInstanceData& InstanceData =
        Context.GetInstanceData<FFlowFieldAgentEvaluatorInstanceData>(*this);

    // ── 变换 ────────────────────────────────────────────────────
    InstanceData.WorldPosition = Trans.GetTransform().GetLocation();
    InstanceData.WorldRotation = Trans.GetTransform().GetRotation().Rotator();

    // ── 移动 ────────────────────────────────────────────────────
    InstanceData.MoveSpeed            = Agent.MoveSpeed;
    InstanceData.CurrentDir           = Agent.CurrentDir;
    InstanceData.SmoothedMoveVelocity = Agent.SmoothedMoveVelocity;
    InstanceData.RVOComputedVelocity  = Agent.RVOComputedVelocity;

    // ── 状态标志 ─────────────────────────────────────────────────
    InstanceData.bIsKnockedBack   = Agent.bIsKnockedBack;
    InstanceData.KnockbackVelocity = Agent.KnockbackVelocity;
    InstanceData.bInStopZone      = Agent.bInStopZone;
    InstanceData.bChasingTarget   = Agent.bChasingTarget;
    InstanceData.ChaseTargetPos   = Agent.ChaseTargetPos;
    InstanceData.bInAttackRange   = Agent.bInAttackRange;

    // ── 地面 ────────────────────────────────────────────────────
    InstanceData.SmoothedSurfaceZ = Agent.SmoothedSurfaceZ;
}
