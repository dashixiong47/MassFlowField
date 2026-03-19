#include "FlowFieldObstacleActor.h"

AFlowFieldObstacleActor::AFlowFieldObstacleActor()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>("Root");
	SetRootComponent(Root);

	Sphere = CreateDefaultSubobject<USphereComponent>("Sphere");
	Sphere->SetupAttachment(Root);
	Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Sphere->SetHiddenInGame(true);
	Sphere->ShapeColor = FColor(255, 100, 0);

#if WITH_EDITOR
	Sphere->bVisualizeComponent = true;
#endif

	Tags.Add(FName("FlowFieldObstacle"));
}