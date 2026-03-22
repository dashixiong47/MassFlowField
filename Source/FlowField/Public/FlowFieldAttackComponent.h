#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FlowFieldAttackTypes.h"
#include "FlowFieldAttackComponent.generated.h"

// ── 委托声明 ─────────────────────────────────────────────────────
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FOnEntityHit,
    int32, AttackId, int32, EntityId, AActor*, EntityActor, float, Damage, FVector, HitPos);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnDoTTick,
    int32, AttackId, int32, EntityId, AActor*, EntityActor, float, Damage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAttackEnd,
    int32, AttackId, FVector, FinalPos);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEntityDied,
    int32, EntityId, AActor*, EntityActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEntityDestroyed,
    int32, EntityId);

/**
 * 挂在 FlowFieldActor 上，管理全部活跃攻击体、DoT 效果和死亡队列。
 * 攻击发起方（塔）调用 FireAttack() 后通过委托接收命中/DoT 通知。
 * 命中检测由 UFlowFieldAttackProcessor（Mass Processor）每帧执行。
 */
UCLASS(ClassGroup="FlowField", meta=(BlueprintSpawnableComponent),
    meta=(DisplayName="FlowField 攻击管理"))
class FLOWFIELD_API UFlowFieldAttackComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UFlowFieldAttackComponent();

    // ── 调试绘制 ──────────────────────────────────────────────────

    /** 绘制飞行体轨迹线（黄色，从起点到终点） */
    UPROPERTY(EditAnywhere, Category="FlowField|调试|攻击",
        meta=(DisplayName="绘制飞行体轨迹"))
    bool bDrawProjectileTrajectory = false;

    /** 绘制飞行体当前位置箭头（橙色，跟随移动） */
    UPROPERTY(EditAnywhere, Category="FlowField|调试|攻击",
        meta=(DisplayName="绘制飞行体箭头"))
    bool bDrawProjectileArrow = false;

    /** 绘制激光线段（直线=青色，扇形=青色多线，连锁=品红色） */
    UPROPERTY(EditAnywhere, Category="FlowField|调试|攻击",
        meta=(DisplayName="绘制激光"))
    bool bDrawLaser = false;

    /** 绘制爆炸范围球体（红色） */
    UPROPERTY(EditAnywhere, Category="FlowField|调试|攻击",
        meta=(DisplayName="绘制爆炸范围"))
    bool bDrawExplosion = false;

    /** 绘制命中点（绿色球体，持续 0.5s） */
    UPROPERTY(EditAnywhere, Category="FlowField|调试|攻击",
        meta=(DisplayName="绘制命中点"))
    bool bDrawHitPoints = false;

    /** 调试绘制距离剔除（cm），超出此距离不绘制，防卡顿 */
    UPROPERTY(EditAnywhere, Category="FlowField|调试|攻击",
        meta=(ClampMin="500", ClampMax="100000", DisplayName="调试绘制距离（cm）"))
    float AttackDebugDrawDistance = 10000.f;

    // ── 事件委托 ──────────────────────────────────────────────────

    /** AI 被直接命中（DirectDamage > 0 时触发） */
    UPROPERTY(BlueprintAssignable, Category="FlowField|攻击事件")
    FOnEntityHit OnEntityHitDelegate;

    /** DoT 每次触发（DotDamage > 0 时按 DotInterval 周期触发） */
    UPROPERTY(BlueprintAssignable, Category="FlowField|攻击事件")
    FOnDoTTick OnDoTTickDelegate;

    /** 攻击体结束（飞行体落地 / 激光+爆炸视觉时间结束） */
    UPROPERTY(BlueprintAssignable, Category="FlowField|攻击事件")
    FOnAttackEnd OnAttackEndDelegate;

    /** AI 被标记为死亡（KillAgent 调用时广播，可在此播放死亡反应） */
    UPROPERTY(BlueprintAssignable, Category="FlowField|攻击事件")
    FOnEntityDied OnEntityDiedDelegate;

    /** AI 实体即将从 Mass 中销毁（DestroyAgent 调用后、销毁前广播） */
    UPROPERTY(BlueprintAssignable, Category="FlowField|攻击事件")
    FOnEntityDestroyed OnEntityDestroyedDelegate;

    // ── Blueprint 调用 API ────────────────────────────────────────

    /**
     * 发起一次攻击。返回 AttackId，可用于 CancelAttack。
     * Projectile：立即加入飞行队列；
     * Laser / Explosion：加入 PendingFires，下次 Processor 执行时即时命中检测。
     */
    UFUNCTION(BlueprintCallable, Category="FlowField|攻击")
    int32 FireAttack(const FFlowFieldAttackConfig& Config);

    /** 提前取消一个攻击（触发 OnAttackEnd） */
    UFUNCTION(BlueprintCallable, Category="FlowField|攻击")
    void CancelAttack(int32 AttackId);

    /**
     * 将实体标记为死亡（添加 FFlowFieldDeadTag）。
     * 实体立即停止移动/RVO，等待 DeathLingerTime 后自动销毁（bAutoDestroy=true）。
     * 如果 bAutoDestroy=false，需要手动调用 DestroyAgent。
     */
    UFUNCTION(BlueprintCallable, Category="FlowField|死亡")
    void KillAgent(int32 EntityId);

    /**
     * 立即销毁一个 Mass 实体（广播 OnEntityDestroyed 后销毁）。
     * 通常在 OnEntityDied 收到、动画播放完成后调用。
     */
    UFUNCTION(BlueprintCallable, Category="FlowField|死亡")
    void DestroyAgent(int32 EntityId);

    // ── 内部接口（供 AttackProcessor 调用）───────────────────────

    TArray<FFlowFieldActiveAttack>& GetActiveAttacks()  { return ActiveAttacks; }
    TArray<FFlowFieldActiveAttack>& GetPendingFires()   { return PendingFires; }
    TMap<int32, TArray<FFlowFieldDoTEntry>>& GetDoTMap(){ return DoTMap; }
    TArray<int32>& GetPendingKills()                    { return PendingKillIds; }
    TArray<int32>& GetPendingDestroys()                 { return PendingDestroyIds; }

    void BroadcastEntityHit(int32 AttackId, int32 EntityId, AActor* Actor,
                            float Damage, FVector HitPos);
    void BroadcastDoTTick(int32 AttackId, int32 EntityId, AActor* Actor, float Damage);
    void BroadcastAttackEnd(int32 AttackId, FVector FinalPos);
    void BroadcastEntityDied(int32 EntityId, AActor* Actor);
    void BroadcastEntityDestroyed(int32 EntityId);

private:
    TArray<FFlowFieldActiveAttack>         ActiveAttacks;  // 飞行中的飞行体 + 视觉中的激光/爆炸
    TArray<FFlowFieldActiveAttack>         PendingFires;   // 等待本帧命中检测的激光/爆炸
    TMap<int32, TArray<FFlowFieldDoTEntry>> DoTMap;        // EntityId → DoT 条目列表
    TArray<int32>                          PendingKillIds;   // 待标记死亡
    TArray<int32>                          PendingDestroyIds;// 待销毁

    int32 NextAttackId = 1;
    int32 AllocAttackId() { return NextAttackId++; }
};
