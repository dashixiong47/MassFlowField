#include "FlowFieldTargetComponent.h"
#include "FlowFieldSubsystem.h"
#include "FlowFieldActor.h"

void UFlowFieldTargetComponent::BeginPlay()
{
    Super::BeginPlay();

    UWorld* World = GetWorld();
    if (!World) return;

    UFlowFieldSubsystem* Sub = World->GetSubsystem<UFlowFieldSubsystem>();
    AFlowFieldActor* Actor = Sub ? Sub->GetActor() : nullptr;
    if (Actor)
        Actor->RegisterTarget(this);
}

void UFlowFieldTargetComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UWorld* World = GetWorld();
    if (World)
    {
        UFlowFieldSubsystem* Sub = World->GetSubsystem<UFlowFieldSubsystem>();
        AFlowFieldActor* Actor = Sub ? Sub->GetActor() : nullptr;
        if (Actor)
            Actor->UnregisterTarget(this);
    }

    Super::EndPlay(EndPlayReason);
}
