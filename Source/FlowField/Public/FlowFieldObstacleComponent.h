#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FlowFieldObstacleComponent.generated.h"

/**
 * 挂在障碍物 Actor 上。
 * BeginPlay 时自动向 AFlowFieldActor 注册，EndPlay 时注销并精确撤销对应格子。
 * 编辑器模式下：移动/属性变更时触发 FlowFieldActor OnConstruction 重建编辑器调试视图。
 */
UCLASS(ClassGroup="FlowField", meta=(BlueprintSpawnableComponent),
    meta=(DisplayName="FlowField 障碍物"))
class FLOWFIELD_API UFlowFieldObstacleComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    /** 在 Actor 包围盒基础上额外扩展的障碍半径（cm）*/
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="FlowField",
        meta=(ClampMin="0", DisplayName="扩展半径（cm）"))
    float Radius = 0.f;

    // FlowFieldActor 内部写入：当前被本组件标记为阻断的格子（用于销毁时精确回滚）
    TArray<FIntPoint> BlockedCells;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void OnRegister() override;
    virtual void OnUnregister() override;

private:
    void OnOwnerMoved(AActor* Actor);
    void NotifyFlowFieldActor() const;
    FDelegateHandle ActorMovedHandle;
#endif
};
