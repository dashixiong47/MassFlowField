#pragma once
#include "CoreMinimal.h"
#include "FlowFieldTypes.generated.h"

UENUM(BlueprintType)
enum class EFlowFieldBoundsMode : uint8
{
	VolumeBox   UMETA(DisplayName = "Volume Box"),
	LevelBounds UMETA(DisplayName = "Level Bounds")
};

USTRUCT(BlueprintType)
struct FFlowFieldCell
{
	GENERATED_BODY()

	// 1 = walkable, 255 = blocked
	UPROPERTY(BlueprintReadOnly)
	uint8 Cost = 1;

	// Dijkstra accumulated cost from goal; FLT_MAX = unreachable
	UPROPERTY(BlueprintReadOnly)
	float Integration = FLT_MAX;

	// Normalized 2D direction toward goal
	UPROPERTY(BlueprintReadOnly)
	FVector2D FlowDir = FVector2D::ZeroVector;

	// World Z of terrain surface
	UPROPERTY(BlueprintReadOnly)
	float SurfaceZ = 0.f;

	// Terrain surface normal
	UPROPERTY(BlueprintReadOnly)
	FVector Normal = FVector::UpVector;

	bool IsBlocked()   const { return Cost >= 255; }
	bool IsReachable() const { return Integration < FLT_MAX; }
	bool IsGoal()      const { return Integration == 0.f; }
};