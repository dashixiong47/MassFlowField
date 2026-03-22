#include "FlowFieldAttackComponent.h"

UFlowFieldAttackComponent::UFlowFieldAttackComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

// ── 内部辅助：填充效果参数 ────────────────────────────────────

void UFlowFieldAttackComponent::FillEffects(FFlowFieldActiveAttack& A,
                                            const FFlowFieldEffectParams& E,
                                            float HitRadius)
{
    // 伤害
    A.DirectDamage      = E.DirectDamage;
    A.DotDamage         = E.DotDamage;
    A.DotDuration       = E.DotDuration;
    A.DotInterval       = FMath::Max(E.DotInterval, 0.05f);
    // 击退
    A.bKnockback               = E.bKnockback;
    A.KnockbackStrength        = E.KnockbackStrength;
    A.KnockbackRadius          = E.KnockbackRadius > 0.f ? E.KnockbackRadius : HitRadius;
    A.KnockbackStaggerDuration = E.KnockbackStaggerDuration;
    // 减速
    A.bSlow             = E.bSlow;
    A.SlowFactor        = FMath::Clamp(E.SlowFactor, 0.f, 0.99f);
    A.SlowDuration      = E.SlowDuration;
    // 眩晕
    A.bStun             = E.bStun;
    A.StunDuration      = E.StunDuration;
    // 自定义效果
    A.CustomEffects     = E.CustomEffects;
}

// ── 飞行体 ────────────────────────────────────────────────────

int32 UFlowFieldAttackComponent::FireProjectile(const FFlowFieldProjectileConfig& Config)
{
    const int32 N = FMath::Max(Config.RayCount, 1);
    const float BaseDist = FVector::Dist2D(Config.Origin, Config.Target);
    const FVector BaseDir = (Config.Target - Config.Origin).GetSafeNormal2D();

    int32 FirstId = -1;
    for (int32 k = 0; k < N; ++k)
    {
        // 计算本发方向（单发时无偏转）
        FVector Dir = BaseDir;
        if (N > 1)
        {
            const float HalfAngle = Config.FanAngle * 0.5f;
            const float Deg = FMath::Lerp(-HalfAngle, HalfAngle, float(k) / float(N - 1));
            Dir = FQuat(FVector::UpVector, FMath::DegreesToRadians(Deg)).RotateVector(BaseDir);
        }

        FFlowFieldActiveAttack A;
        A.AttackId   = AllocAttackId();
        A.Type       = EFlowFieldAttackTypeTag::Projectile;
        A.HitRadius  = Config.HitRadius;
        A.bPiercing  = Config.bPiercing;
        A.Origin     = Config.Origin;
        A.Target     = Config.Origin + Dir * BaseDist;
        A.CurrentPos = Config.Origin;
        FillEffects(A, Config.Effects, Config.HitRadius);

        if (Config.TravelTime > 0.f)
            A.TotalTime = Config.TravelTime;
        else
            A.TotalTime = Config.ProjectileSpeed > 0.f ? BaseDist / Config.ProjectileSpeed : 1.f;

        if (FirstId < 0) FirstId = A.AttackId;
        ActiveAttacks.Add(MoveTemp(A));
    }
    return FirstId;
}

// ── 激光（直线 / 扇形） ──────────────────────────────────────

int32 UFlowFieldAttackComponent::FireLaser(const FFlowFieldLaserConfig& Config)
{
    FFlowFieldActiveAttack A;
    A.AttackId       = AllocAttackId();
    A.Type           = EFlowFieldAttackTypeTag::Laser;
    A.HitRadius      = Config.HitRadius;
    A.bPiercing      = Config.bPiercing;
    A.VisualDuration = Config.VisualDuration;
    A.Origin         = Config.Origin;
    A.Target         = Config.Target;
    A.MaxRange       = Config.MaxRange;
    FillEffects(A, Config.Effects, Config.HitRadius);

    // 预计算射线终点（Fire 时算一次，避免每帧重算）
    const FVector BaseDir = (Config.Target - Config.Origin).GetSafeNormal2D();
    const int32 N = FMath::Max(Config.RayCount, 1);
    A.DebugPath.Reserve(N + 1);
    A.DebugPath.Add(Config.Origin); // [0] = Origin

    if (N == 1)
    {
        A.DebugPath.Add(Config.Origin + BaseDir * Config.MaxRange);
    }
    else
    {
        const float HalfAngle = Config.FanAngle * 0.5f;
        for (int32 k = 0; k < N; ++k)
        {
            const float Deg = FMath::Lerp(-HalfAngle, HalfAngle,
                                          float(k) / float(N - 1));
            const FQuat Rot(FVector::UpVector, FMath::DegreesToRadians(Deg));
            A.DebugPath.Add(Config.Origin + Rot.RotateVector(BaseDir) * Config.MaxRange);
        }
    }

    const int32 Id = A.AttackId;
    PendingFires.Add(MoveTemp(A));
    return Id;
}

// ── 连锁激光 ──────────────────────────────────────────────────

int32 UFlowFieldAttackComponent::FireChain(const FFlowFieldChainConfig& Config)
{
    FFlowFieldActiveAttack A;
    A.AttackId       = AllocAttackId();
    A.Type           = EFlowFieldAttackTypeTag::Chain;
    A.HitRadius      = Config.HitRadius;
    A.VisualDuration = Config.VisualDuration;
    A.Origin         = Config.Origin;
    A.Target         = Config.Target;
    A.MaxRange       = Config.MaxRange;
    A.ChainRadius    = Config.ChainRadius;
    A.MaxChainCount  = Config.MaxChainCount;
    FillEffects(A, Config.Effects, Config.HitRadius);

    const int32 Id = A.AttackId;
    PendingFires.Add(MoveTemp(A));
    return Id;
}

// ── 爆炸 ──────────────────────────────────────────────────────

int32 UFlowFieldAttackComponent::FireExplosion(const FFlowFieldExplosionConfig& Config)
{
    FFlowFieldActiveAttack A;
    A.AttackId       = AllocAttackId();
    A.Type           = EFlowFieldAttackTypeTag::Explosion;
    A.HitRadius      = Config.Radius;
    A.VisualDuration = Config.VisualDuration;
    A.Target         = Config.Center; // Target 存爆炸中心
    FillEffects(A, Config.Effects, Config.Radius);

    const int32 Id = A.AttackId;
    PendingFires.Add(MoveTemp(A));
    return Id;
}

// ── 取消 / 死亡管理 ──────────────────────────────────────────

void UFlowFieldAttackComponent::CancelAttack(int32 AttackId)
{
    for (FFlowFieldActiveAttack& A : ActiveAttacks)
        if (A.AttackId == AttackId) { A.bActive = false; break; }
    for (FFlowFieldActiveAttack& A : PendingFires)
        if (A.AttackId == AttackId) { A.bActive = false; break; }
}

void UFlowFieldAttackComponent::KillAgent(int32 EntityId)
{
    PendingKillIds.AddUnique(EntityId);
}

void UFlowFieldAttackComponent::DestroyAgent(int32 EntityId)
{
    PendingDestroyIds.AddUnique(EntityId);
}

// ── 广播 ──────────────────────────────────────────────────────

void UFlowFieldAttackComponent::BroadcastEntityHit(int32 AttackId, int32 EntityId,
    AActor* Actor, float Damage, FVector HitPos)
{
    OnEntityHitDelegate.Broadcast(AttackId, EntityId, Actor, Damage, HitPos);
}

void UFlowFieldAttackComponent::BroadcastDoTTick(int32 AttackId, int32 EntityId,
    AActor* Actor, float Damage)
{
    OnDoTTickDelegate.Broadcast(AttackId, EntityId, Actor, Damage);
}

void UFlowFieldAttackComponent::BroadcastAttackEnd(int32 AttackId, FVector FinalPos)
{
    OnAttackEndDelegate.Broadcast(AttackId, FinalPos);
}

void UFlowFieldAttackComponent::BroadcastEntityDied(int32 EntityId, AActor* Actor)
{
    OnEntityDiedDelegate.Broadcast(EntityId, Actor);
}

void UFlowFieldAttackComponent::BroadcastEntityDestroyed(int32 EntityId)
{
    OnEntityDestroyedDelegate.Broadcast(EntityId);
}

void UFlowFieldAttackComponent::BroadcastCustomEffect(int32 AttackId, int32 EntityId,
    AActor* Actor, FVector HitPos, FName TypeId, float Value, float Duration)
{
    OnCustomEffectDelegate.Broadcast(AttackId, EntityId, Actor, HitPos, TypeId, Value, Duration);
}
