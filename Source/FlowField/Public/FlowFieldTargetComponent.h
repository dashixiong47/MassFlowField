#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FlowFieldTargetComponent.generated.h"

/**
 * 挂在目标 Actor（如 Player）上。
 * FlowFieldActor 会自动寻找带 TargetTag 的 Actor，
 * 并读取此组件的 ChaseRadius 决定追踪范围；无组件则用 FlowFieldActor 上的默认值。
 */
UCLASS(ClassGroup="FlowField", meta=(BlueprintSpawnableComponent),
    meta=(DisplayName="FlowField 追踪目标"))
class FLOWFIELD_API UFlowFieldTargetComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    /** AI 追踪此目标的最大距离（cm）。目标在此范围内时 FlowField 才会向其生成。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="FlowField",
        meta=(ClampMin="0", DisplayName="追踪半径（cm）"))
    float ChaseRadius = 3000.f;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
