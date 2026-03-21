#pragma once
#include "CoreMinimal.h"
#include "FlowFieldTypes.generated.h"

UENUM(BlueprintType)
enum class EFlowFieldBoundsMode : uint8
{
	VolumeBox   UMETA(DisplayName = "Volume Box"),
	LevelBounds UMETA(DisplayName = "Level Bounds")
};

/**
 * 扫描模式：
 * TopDown     — 原始行为，从上往下单次射线，取第一个命中（遇屋顶即停）。
 * GroundLevel — 穿透模式，取整列所有法线朝上的命中面中 Z 最低的一个，
 *               可正确处理"楼梯上方有屋顶"等遮挡情形。
 */
UENUM(BlueprintType)
enum class EFlowFieldScanMode : uint8
{
	TopDown     UMETA(DisplayName = "TopDown（单次命中，快速）"),
	GroundLevel UMETA(DisplayName = "GroundLevel（穿透过滤，取最低可走面）"),
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