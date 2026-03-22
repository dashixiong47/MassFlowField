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

/** 自定义效果命中委托：TypeId/Value/Duration 由攻击配置中的 CustomEffects 数组定义，
 *  项目代码在此委托中实现具体的效果逻辑（减速、冰冻、中毒等自定义行为）*/
DECLARE_DYNAMIC_MULTICAST_DELEGATE_SevenParams(FOnCustomEffect,
    int32,   AttackId,
    int32,   EntityId,
    AActor*, EntityActor,
    FVector, HitPos,
    FName,   TypeId,
    float,   Value,
    float,   Duration);

/**
 * 挂在 FlowFieldActor 上，管理全部活跃攻击、DoT 效果和死亡队列。
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

    UPROPERTY(EditAnywhere, Category="FlowField|调试|攻击",
        meta=(DisplayName="绘制飞行体轨迹"))
    bool bDrawProjectileTrajectory = false;

    UPROPERTY(EditAnywhere, Category="FlowField|调试|攻击",
        meta=(DisplayName="绘制飞行体箭头"))
    bool bDrawProjectileArrow = false;

    UPROPERTY(EditAnywhere, Category="FlowField|调试|攻击",
        meta=(DisplayName="绘制激光"))
    bool bDrawLaser = false;

    UPROPERTY(EditAnywhere, Category="FlowField|调试|攻击",
        meta=(DisplayName="绘制爆炸范围"))
    bool bDrawExplosion = false;

    UPROPERTY(EditAnywhere, Category="FlowField|调试|攻击",
        meta=(DisplayName="绘制命中点"))
    bool bDrawHitPoints = false;

    UPROPERTY(EditAnywhere, Category="FlowField|调试|攻击",
        meta=(ClampMin="500", ClampMax="100000", DisplayName="调试绘制距离（cm）"))
    float AttackDebugDrawDistance = 10000.f;

    // ── 事件委托 ──────────────────────────────────────────────────

    UPROPERTY(BlueprintAssignable, Category="FlowField|攻击事件")
    FOnEntityHit OnEntityHitDelegate;

    UPROPERTY(BlueprintAssignable, Category="FlowField|攻击事件")
    FOnDoTTick OnDoTTickDelegate;

    UPROPERTY(BlueprintAssignable, Category="FlowField|攻击事件")
    FOnAttackEnd OnAttackEndDelegate;

    UPROPERTY(BlueprintAssignable, Category="FlowField|攻击事件")
    FOnEntityDied OnEntityDiedDelegate;

    UPROPERTY(BlueprintAssignable, Category="FlowField|攻击事件")
    FOnEntityDestroyed OnEntityDestroyedDelegate;

    /** 命中时存在自定义效果条目则广播，项目代码绑定此委托实现自定义逻辑 */
    UPROPERTY(BlueprintAssignable, Category="FlowField|攻击事件")
    FOnCustomEffect OnCustomEffectDelegate;

    // ── 攻击发起 API ──────────────────────────────────────────────

    /** 发射飞行体，返回 AttackId */
    UFUNCTION(BlueprintCallable, Category="FlowField|攻击")
    int32 FireProjectile(const FFlowFieldProjectileConfig& Config);

    /** 发射激光（直线 RayCount=1，扇形 RayCount>1），返回 AttackId */
    UFUNCTION(BlueprintCallable, Category="FlowField|攻击")
    int32 FireLaser(const FFlowFieldLaserConfig& Config);

    /** 发射连锁激光，返回 AttackId */
    UFUNCTION(BlueprintCallable, Category="FlowField|攻击")
    int32 FireChain(const FFlowFieldChainConfig& Config);

    /** 触发爆炸，返回 AttackId */
    UFUNCTION(BlueprintCallable, Category="FlowField|攻击")
    int32 FireExplosion(const FFlowFieldExplosionConfig& Config);

    /** 提前取消一个攻击（触发 OnAttackEnd） */
    UFUNCTION(BlueprintCallable, Category="FlowField|攻击")
    void CancelAttack(int32 AttackId);

    // ── 死亡管理 ──────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category="FlowField|死亡")
    void KillAgent(int32 EntityId);

    UFUNCTION(BlueprintCallable, Category="FlowField|死亡")
    void DestroyAgent(int32 EntityId);

    // ── 内部接口（供 AttackProcessor 调用）───────────────────────

    // 减速/眩晕待应用条目（由 AttackProcessor 消费并写入实体 Fragment）
    struct FPendingSlowEntry { float Factor; float Duration; };

    TArray<FFlowFieldActiveAttack>& GetActiveAttacks()          { return ActiveAttacks; }
    TArray<FFlowFieldActiveAttack>& GetPendingFires()           { return PendingFires; }
    TMap<int32, TArray<FFlowFieldDoTEntry>>& GetDoTMap()        { return DoTMap; }
    TArray<int32>& GetPendingKills()                            { return PendingKillIds; }
    TArray<int32>& GetPendingDestroys()                         { return PendingDestroyIds; }
    TMap<int32, FPendingSlowEntry>& GetPendingSlows()           { return PendingSlows; }
    TMap<int32, float>& GetPendingStuns()                       { return PendingStuns; }

    void BroadcastEntityHit(int32 AttackId, int32 EntityId, AActor* Actor,
                            float Damage, FVector HitPos);
    void BroadcastDoTTick(int32 AttackId, int32 EntityId, AActor* Actor, float Damage);
    void BroadcastAttackEnd(int32 AttackId, FVector FinalPos);
    void BroadcastEntityDied(int32 EntityId, AActor* Actor);
    void BroadcastEntityDestroyed(int32 EntityId);
    void BroadcastCustomEffect(int32 AttackId, int32 EntityId, AActor* Actor,
                               FVector HitPos, FName TypeId, float Value, float Duration);

private:
    TArray<FFlowFieldActiveAttack>          ActiveAttacks;
    TArray<FFlowFieldActiveAttack>          PendingFires;
    TMap<int32, TArray<FFlowFieldDoTEntry>> DoTMap;
    TArray<int32>                           PendingKillIds;
    TArray<int32>                           PendingDestroyIds;
    TMap<int32, FPendingSlowEntry>          PendingSlows;
    TMap<int32, float>                      PendingStuns;

    int32 NextAttackId = 1;
    int32 AllocAttackId() { return NextAttackId++; }

    // 从 EffectParams 填充 ActiveAttack 效果字段
    static void FillEffects(FFlowFieldActiveAttack& A,
                            const FFlowFieldEffectParams& E, float HitRadius);
};
