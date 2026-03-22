#include "FlowFieldAttackComponent.h"

UFlowFieldAttackComponent::UFlowFieldAttackComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

int32 UFlowFieldAttackComponent::FireAttack(const FFlowFieldAttackConfig& Config)
{
    FFlowFieldActiveAttack Attack;
    Attack.AttackId    = AllocAttackId();
    Attack.Config      = Config;
    Attack.CurrentPos  = Config.Origin;
    Attack.ElapsedTime = 0.f;

    switch (Config.Type)
    {
    case EFlowFieldAttackType::Projectile:
    {
        // 计算总飞行时间
        if (Config.TravelTime > 0.f)
            Attack.TotalTime = Config.TravelTime;
        else
        {
            const float Dist = FVector::Dist2D(Config.Origin, Config.Target);
            Attack.TotalTime = (Config.ProjectileSpeed > 0.f)
                ? Dist / Config.ProjectileSpeed : 1.f;
        }
        ActiveAttacks.Add(MoveTemp(Attack));
        break;
    }

    case EFlowFieldAttackType::Laser:
    case EFlowFieldAttackType::Explosion:
    {
        // 预计算扇形射线方向（在 Fire 时只算一次）
        if (Config.Type == EFlowFieldAttackType::Laser
            && Config.LaserMode == EFlowFieldLaserMode::Fan
            && Config.FanRayCount > 0)
        {
            const FVector BaseDir = (Config.Target - Config.Origin).GetSafeNormal2D();
            const float HalfAngle = Config.FanAngle * 0.5f;
            const int32 N = Config.FanRayCount;
            Attack.DebugPath.Reserve(N + 1);
            Attack.DebugPath.Add(Config.Origin); // index 0 = 起点
            for (int32 k = 0; k < N; ++k)
            {
                const float Deg = (N > 1)
                    ? FMath::Lerp(-HalfAngle, HalfAngle, float(k) / float(N - 1))
                    : 0.f;
                const FQuat Rot(FVector::UpVector, FMath::DegreesToRadians(Deg));
                const FVector RayDir = Rot.RotateVector(BaseDir);
                Attack.DebugPath.Add(Config.Origin + RayDir * Config.MaxRange);
            }
        }
        const int32 Id = Attack.AttackId;
        PendingFires.Add(MoveTemp(Attack));
        return Id;
    }
    }

    // Projectile
    return ActiveAttacks.Last().AttackId;
}

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
