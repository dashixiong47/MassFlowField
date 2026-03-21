#include "MassAI/FlowFieldAgentTrait.h"

void UFlowFieldAgentTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
    FFlowFieldAgentFragment& AgentFrag = BuildContext.AddFragment_GetRef<FFlowFieldAgentFragment>();
    AgentFrag.MoveSpeed          = MoveSpeed;
    AgentFrag.AgentRadius        = AgentRadius;
    AgentFrag.DirSmoothing       = DirSmoothing;
    AgentFrag.RVOTimeHorizon     = RVOTimeHorizon;
    AgentFrag.RVONeighborDist    = (RVONeighborDist > 0.f) ? RVONeighborDist : AgentRadius * 5.f;
    AgentFrag.RVOMaxNeighbors    = RVOMaxNeighbors;
    AgentFrag.SurfaceZSmoothSpeed = SurfaceZSmoothSpeed;

    BuildContext.AddTag<FFlowFieldAgentTag>();
    BuildContext.AddTag<FFlowFieldMovingTag>();
}
