#include "MassAI/FlowFieldSpatialHashProcessor.h"

#include "FlowFieldSubsystem.h"
#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassEntityManager.h"
#include "MassCommonTypes.h"
#include "MassAI/FlowFieldAgentFragment.h"

UFlowFieldSpatialHashProcessor::UFlowFieldSpatialHashProcessor()
{
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::PrePhysics;
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Server | EProcessorExecutionFlags::Client);
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
}

void UFlowFieldSpatialHashProcessor::ConfigureQueries(
	const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.Initialize(EntityManager);
	EntityQuery.AddTagRequirement<FFlowFieldAgentTag>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.RegisterWithProcessor(*this);
	RegisterQuery(EntityQuery);
}

void UFlowFieldSpatialHashProcessor::Execute(
	FMassEntityManager& EntityManager,
	FMassExecutionContext& Context)
{
	UWorld* World = GetWorld();
	if (!World) return;

	UFlowFieldSubsystem* FlowSub = World->GetSubsystem<UFlowFieldSubsystem>();
	if (!FlowSub) return;

	FlowSub->SpatialHash.Clear();

	EntityQuery.ForEachEntityChunk(Context,
		[&](FMassExecutionContext& ChunkContext)
		{
			auto Transforms = ChunkContext.GetFragmentView<FTransformFragment>();
			const int32 Num = ChunkContext.GetNumEntities();

			for (int32 i = 0; i < Num; i++)
			{
				FlowSub->SpatialHash.Insert(
					ChunkContext.GetEntity(i),
					Transforms[i].GetTransform().GetLocation());
			}
		});
}