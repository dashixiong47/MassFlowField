#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FlowFieldTargetComponent.generated.h"

// ── 委托声明 ─────────────────────────────────────────────────────
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAIEnterRange,       AActor*, AIActor, int32, EntityId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAIExitRange,        AActor*, AIActor, int32, EntityId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAIForgotTarget,     AActor*, AIActor, int32, EntityId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAIEnterAttackRange, AActor*, AIActor, int32, EntityId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAIExitAttackRange,  AActor*, AIActor, int32, EntityId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnAIAttack,         AActor*, AIActor, int32, EntityId, float, Damage);

/**
 * 挂在目标 Actor（如 Player）上。
 * FlowFieldActor 会自动寻找带 TargetTag 的 Actor 并追踪；
 * AI 感知范围由各实体 Fragment.DetectRadius 决定（非此组件）。
 *
 * 事件使用方式（二选一，可同时用）：
 *   A. 蓝图继承此组件 → override On* 虚函数（子类自定义逻辑）
 *   B. 在 Actor 蓝图中直接绑定 On* Delegate（无需继承）
 */
UCLASS(Blueprintable, ClassGroup="FlowField", meta=(BlueprintSpawnableComponent),
    meta=(DisplayName="FlowField 追踪目标"))
class FLOWFIELD_API UFlowFieldTargetComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UFlowFieldTargetComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;


    // ── 玩家推挤 ──────────────────────────────────────────────────

    /** 玩家推挤 AI 的有效半径（cm）。AI 进入此范围才被推开，同时计入减速统计。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="FlowField|推挤",
        meta=(ClampMin="0", DisplayName="推挤/接触范围（cm）"))
    float PushRadius = 100.f;

    /** 推挤最大速度（cm/s）。越靠近中心推力越大，建议 400~800。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="FlowField|推挤",
        meta=(ClampMin="0", DisplayName="推挤力度（cm/s）"))
    float PushStrength = 600.f;

    /** 减速下限（0~1）。AI 足够多时玩家最慢只能达到此比例。1 = 永不减速。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="FlowField|推挤",
        meta=(ClampMin="0.05", ClampMax="1.0", DisplayName="最大减速比例"))
    float MaxSlowdownFactor = 0.3f;

    /** 达到最大减速所需的 AI 数量。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="FlowField|推挤",
        meta=(ClampMin="1", DisplayName="满减速 AI 数量"))
    int32 AgentsForMaxSlow = 8;

    /** 当前速度乘数（0~1），由 Mass 系统每帧写入。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="FlowField|推挤",
        meta=(DisplayName="当前速度乘数"))
    float CurrentSpeedMultiplier = 1.f;

    // ── 广播委托（无需继承，直接在 Actor 蓝图中 Bind）────────────

    UPROPERTY(BlueprintAssignable, Category="FlowField|Events")
    FOnAIEnterRange       OnAIEnterRangeDelegate;

    UPROPERTY(BlueprintAssignable, Category="FlowField|Events")
    FOnAIExitRange        OnAIExitRangeDelegate;

    /** AI 彻底遗忘目标（出感知范围 + ForgetTime 耗尽，真正停止追踪） */
    UPROPERTY(BlueprintAssignable, Category="FlowField|Events")
    FOnAIForgotTarget     OnAIForgotTargetDelegate;

    UPROPERTY(BlueprintAssignable, Category="FlowField|Events")
    FOnAIEnterAttackRange OnAIEnterAttackRangeDelegate;

    UPROPERTY(BlueprintAssignable, Category="FlowField|Events")
    FOnAIExitAttackRange  OnAIExitAttackRangeDelegate;

    UPROPERTY(BlueprintAssignable, Category="FlowField|Events")
    FOnAIAttack           OnAIAttackDelegate;

    // ── BlueprintNativeEvent（子类 override 实现自定义逻辑）──────

    /** AI 进入追踪范围（bChasingTarget 由 false→true） */
    UFUNCTION(BlueprintNativeEvent, Category="FlowField|Events")
    void OnAIEnterRange(AActor* AIActor, int32 EntityId);
    virtual void OnAIEnterRange_Implementation(AActor* AIActor, int32 EntityId) {}

    /** AI 物理上离开感知范围（bInChaseRange 由 true→false，ForgetTime 内仍在追踪） */
    UFUNCTION(BlueprintNativeEvent, Category="FlowField|Events")
    void OnAIExitRange(AActor* AIActor, int32 EntityId);
    virtual void OnAIExitRange_Implementation(AActor* AIActor, int32 EntityId) {}

    /** AI 彻底遗忘目标（出范围 + ForgetTime 耗尽，真正放弃追踪） */
    UFUNCTION(BlueprintNativeEvent, Category="FlowField|Events")
    void OnAIForgotTarget(AActor* AIActor, int32 EntityId);
    virtual void OnAIForgotTarget_Implementation(AActor* AIActor, int32 EntityId) {}

    /** AI 进入攻击距离（bInAttackRange 由 false→true） */
    UFUNCTION(BlueprintNativeEvent, Category="FlowField|Events")
    void OnAIEnterAttackRange(AActor* AIActor, int32 EntityId);
    virtual void OnAIEnterAttackRange_Implementation(AActor* AIActor, int32 EntityId) {}

    /** AI 离开攻击距离（bInAttackRange 由 true→false） */
    UFUNCTION(BlueprintNativeEvent, Category="FlowField|Events")
    void OnAIExitAttackRange(AActor* AIActor, int32 EntityId);
    virtual void OnAIExitAttackRange_Implementation(AActor* AIActor, int32 EntityId) {}

    /** AI 攻击（每 AttackInterval 秒触发一次） */
    UFUNCTION(BlueprintNativeEvent, Category="FlowField|Events")
    void OnAIAttack(AActor* AIActor, int32 EntityId, float Damage);
    virtual void OnAIAttack_Implementation(AActor* AIActor, int32 EntityId, float Damage) {}

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    float BaseWalkSpeed = 0.f;
};
