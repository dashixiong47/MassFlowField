#include "FlowFieldObstacleComponent.h"
#include "FlowFieldSubsystem.h"
#include "FlowFieldActor.h"
#include "EngineUtils.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

void UFlowFieldObstacleComponent::BeginPlay()
{
    Super::BeginPlay();
    UWorld* World = GetWorld();
    if (!World) return;
    UFlowFieldSubsystem* Sub = World->GetSubsystem<UFlowFieldSubsystem>();
    AFlowFieldActor* Actor = Sub ? Sub->GetActor() : nullptr;
    if (Actor) Actor->RegisterObstacle(this);
}

void UFlowFieldObstacleComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UWorld* World = GetWorld();
    if (World)
    {
        UFlowFieldSubsystem* Sub = World->GetSubsystem<UFlowFieldSubsystem>();
        AFlowFieldActor* Actor = Sub ? Sub->GetActor() : nullptr;
        if (Actor) Actor->UnregisterObstacle(this);
    }
    Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR

void UFlowFieldObstacleComponent::OnRegister()
{
    Super::OnRegister();
    if (GEditor && GetWorld() && !GetWorld()->IsGameWorld())
    {
        ActorMovedHandle = GEditor->OnActorMoved().AddUObject(
            this, &UFlowFieldObstacleComponent::OnOwnerMoved);
    }
}

void UFlowFieldObstacleComponent::OnUnregister()
{
    if (GEditor && ActorMovedHandle.IsValid())
    {
        GEditor->OnActorMoved().Remove(ActorMovedHandle);
        ActorMovedHandle.Reset();
    }
    Super::OnUnregister();
}

void UFlowFieldObstacleComponent::OnOwnerMoved(AActor* Actor)
{
    if (Actor == GetOwner()) NotifyFlowFieldActor();
}

void UFlowFieldObstacleComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    NotifyFlowFieldActor();
}

void UFlowFieldObstacleComponent::NotifyFlowFieldActor() const
{
    UWorld* World = GetWorld();
    if (!World) return;
    for (TActorIterator<AFlowFieldActor> It(World); It; ++It)
    {
        (*It)->RefreshEditorObstacleLayout();
        break; // 场景中通常只有一个 FlowFieldActor
    }
}

#endif
