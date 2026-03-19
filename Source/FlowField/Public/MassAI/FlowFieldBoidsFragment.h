#pragma once
#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "FlowFieldBoidsFragment.generated.h"

USTRUCT()
struct FLOWFIELD_API FFlowFieldBoidsFragment : public FMassFragment
{
	GENERATED_BODY()

	// 本帧计算出的分离力（由 BoidsProcessor 写入，MovementProcessor 读取）
	UPROPERTY() FVector SeparationForce = FVector::ZeroVector;

	// 感知半径（cm），建议设为 AI 模型宽度，防止实体重叠
	UPROPERTY() float SeparationRadius = 120.f;

	// 分离力混入移动方向的权重
	UPROPERTY() float SeparationWeight = 1.5f;

	// 分离力平滑速度，越小越平滑
	UPROPERTY() float SeparationSmoothSpeed = 5.f;

	// 停止实体的排斥半径倍数（相对于 SeparationRadius）
	UPROPERTY() float StoppedRadiusMultiplier = 2.f;
};