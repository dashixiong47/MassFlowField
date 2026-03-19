#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SphereComponent.h"
#include "FlowFieldObstacleActor.generated.h"

/**
 * 占位 Actor，由 ScanAndPlaceObstacles 自动生成。
 * SphereComponent 的半径对应障碍物影响范围。
 * 自带 FlowFieldObstacle Tag，Generate 时被识别为障碍。
 * 不参与任何碰撞，仅作为 FlowField 的逻辑标记。
 */
UCLASS(NotBlueprintable)
class FLOWFIELD_API AFlowFieldObstacleActor : public AActor
{
	GENERATED_BODY()

public:
	AFlowFieldObstacleActor();

	UPROPERTY(VisibleAnywhere, Category="FlowField")
	USphereComponent* Sphere;

	// 来源格子信息（调试用）
	UPROPERTY(VisibleAnywhere, Category="FlowField")
	FString SourceActorName;
};