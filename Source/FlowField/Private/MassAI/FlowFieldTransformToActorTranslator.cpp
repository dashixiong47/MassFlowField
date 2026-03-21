#include "MassAI/FlowFieldTransformToActorTranslator.h"
#include "MassExecutionContext.h"
#include "MassEntityManager.h"
#include "MassCommonFragments.h"
#include "MassActorSubsystem.h"
#include "MassCommonTypes.h"
#include "MassAI/FlowFieldAgentFragment.h"
#include "FlowFieldSubsystem.h"
#include "FlowFieldActor.h"

void UMassFlowFieldTransformSyncTrait::BuildTemplate(
    FMassEntityTemplateBuildContext& BuildContext,
    const UWorld& World) const
{
    BuildContext.AddTranslator<UFlowFieldTransformToActorTranslator>();
}

UFlowFieldTransformToActorTranslator::UFlowFieldTransformToActorTranslator()
{
    ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::SyncWorldToMass;
    ExecutionFlags = (int32)EProcessorExecutionFlags::All;
    bAutoRegisterWithProcessingPhases = true;
    bRequiresGameThreadExecution = true;
}

void UFlowFieldTransformToActorTranslator::ConfigureQueries(
    const TSharedRef<FMassEntityManager>& EntityManager)
{
    EntityQuery.Initialize(EntityManager);
    EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
    EntityQuery.AddRequirement<FFlowFieldAgentFragment>(EMassFragmentAccess::ReadOnly); // 只读，平滑已在工作线程做完
    EntityQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddTagRequirement<FFlowFieldAgentTag>(EMassFragmentPresence::All);
    EntityQuery.RegisterWithProcessor(*this);
}

void UFlowFieldTransformToActorTranslator::Execute(
    FMassEntityManager& EntityManager,
    FMassExecutionContext& Context)
{
    EntityQuery.ForEachEntityChunk(Context,
    [&](FMassExecutionContext& ChunkContext)
    {
        auto Transforms = ChunkContext.GetFragmentView<FTransformFragment>();
        auto Agents     = ChunkContext.GetFragmentView<FFlowFieldAgentFragment>();
        auto ActorFrags = ChunkContext.GetMutableFragmentView<FMassActorFragment>();
        const int32 Num = ChunkContext.GetNumEntities();

        for (int32 i = 0; i < Num; i++)
        {
            AActor* Actor = ActorFrags[i].GetMutable();
            if (!Actor) continue;

            const FFlowFieldAgentFragment& Agent = Agents[i];
            const FTransform& Transform = Transforms[i].GetTransform();

            // Transform 里的位置已经是平滑后的地面高度（MovementProcessor 写入）
            FVector  Location = Transform.GetLocation();
            FQuat    Rotation = Transform.GetRotation();

            // 纯 Yaw，不做法线倾斜
            FVector Loc = Transform.GetLocation();
            UE_LOG(LogTemp, Verbose, TEXT("Setting actor %s to location %s"), *Actor->GetName(), *Loc.ToString());
            Actor->SetActorLocationAndRotation(
                Location, Rotation,
                false, nullptr,
                ETeleportType::TeleportPhysics);
        }
    });
}