#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FlowFieldObstacleComponent.generated.h"

// ── 委托声明 ─────────────────────────────────────────────────────
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAIReach,          AActor*, AIActor, int32, EntityId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAILeave,          AActor*, AIActor, int32, EntityId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnAIAttackObstacle, AActor*, AIActor, int32, EntityId, float, Damage);

/**
 * 挂在障碍物 Actor 上。
 * BeginPlay 时自动向 AFlowFieldActor 注册，EndPlay 时注销并精确撤销对应格子。
 *
 * 事件使用方式（二选一，可同时用）：
 *   A. 蓝图继承此组件 → override On* 虚函数
 *   B. 在 Actor 蓝图中直接绑定 On* Delegate（无需继承）
 */
UCLASS(Blueprintable, ClassGroup="FlowField", meta=(BlueprintSpawnableComponent),
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

    // ── 广播委托（无需继承，直接在 Actor 蓝图中 Bind）────────────

    UPROPERTY(BlueprintAssignable, Category="FlowField|Events")
    FOnAIReach           OnAIReachDelegate;

    UPROPERTY(BlueprintAssignable, Category="FlowField|Events")
    FOnAILeave           OnAILeaveDelegate;

    UPROPERTY(BlueprintAssignable, Category="FlowField|Events")
    FOnAIAttackObstacle  OnAIAttackObstacleDelegate;

    // ── BlueprintNativeEvent（子类 override 实现自定义逻辑）──────

    /** AI 首次到达此障碍物包围圈（bAtWall 由 false→true） */
    UFUNCTION(BlueprintNativeEvent, Category="FlowField|Events")
    void OnAIReach(AActor* AIActor, int32 EntityId);
    virtual void OnAIReach_Implementation(AActor* AIActor, int32 EntityId) {}

    /** AI 离开此障碍物包围圈（bAtWall 由 true→false） */
    UFUNCTION(BlueprintNativeEvent, Category="FlowField|Events")
    void OnAILeave(AActor* AIActor, int32 EntityId);
    virtual void OnAILeave_Implementation(AActor* AIActor, int32 EntityId) {}

    /** AI 攻击此障碍物（每 AttackInterval 秒触发一次） */
    UFUNCTION(BlueprintNativeEvent, Category="FlowField|Events")
    void OnAIAttackObstacle(AActor* AIActor, int32 EntityId, float Damage);
    virtual void OnAIAttackObstacle_Implementation(AActor* AIActor, int32 EntityId, float Damage) {}

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
