#pragma once
#include "CoreMinimal.h"
#include "FlowFieldAttackTypes.generated.h"

// ── 自定义效果条目 ────────────────────────────────────────────

/** 一条自定义效果，TypeId 由项目代码识别，Value/Duration 含义由项目自定义 */
USTRUCT(BlueprintType)
struct FLOWFIELD_API FFlowFieldCustomEffectEntry
{
    GENERATED_BODY()

    /** 效果标识符，例如 "Freeze"、"Poison" */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="自定义效果",
        meta=(DisplayName="效果标识符（TypeId）"))
    FName TypeId;

    /** 效果强度（由项目代码解释，例如减速倍率、毒伤量等） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="自定义效果",
        meta=(DisplayName="效果强度（Value）"))
    float Value = 0.f;

    /** 效果持续时间（s），0 = 瞬发 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="自定义效果",
        meta=(ClampMin="0", DisplayName="持续时间（s）"))
    float Duration = 0.f;
};

// ── 攻击效果参数（伤害 + 内置状态效果 + 自定义扩展）────────────

USTRUCT(BlueprintType)
struct FLOWFIELD_API FFlowFieldEffectParams
{
    GENERATED_BODY()

    // ── 直接伤害 ──────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="伤害",
        meta=(ClampMin="0", DisplayName="直接伤害"))
    float DirectDamage = 10.f;

    // ── 持续伤害（DoT） ───────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="持续伤害",
        meta=(ClampMin="0", DisplayName="DoT 每秒伤害"))
    float DotDamage = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="持续伤害",
        meta=(ClampMin="0", DisplayName="DoT 持续时间（s）"))
    float DotDuration = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="持续伤害",
        meta=(ClampMin="0.05", DisplayName="DoT 触发间隔（s）"))
    float DotInterval = 0.5f;

    // ── 击退 ──────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="击退",
        meta=(DisplayName="启用击退"))
    bool bKnockback = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="击退",
        meta=(ClampMin="0", EditCondition="bKnockback", DisplayName="击退力度（cm/s）"))
    float KnockbackStrength = 500.f;

    /** 0 = 与攻击的命中半径相同 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="击退",
        meta=(ClampMin="0", EditCondition="bKnockback", DisplayName="击退范围（cm，0=同命中半径）"))
    float KnockbackRadius = 0.f;

    /** 击退结束后的停顿时间（s）；0 = 击退结束立即恢复移动 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="击退",
        meta=(ClampMin="0", EditCondition="bKnockback", DisplayName="击退后停顿时间（s，0=立即恢复）"))
    float KnockbackStaggerDuration = 0.5f;

    // ── 减速 ──────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="减速",
        meta=(DisplayName="启用减速"))
    bool bSlow = false;

    /** 速度乘数：0=完全停止，0.5=半速（bSlow=true 时有效） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="减速",
        meta=(ClampMin="0", ClampMax="0.99", EditCondition="bSlow", DisplayName="速度乘数（0=全停 0.5=半速）"))
    float SlowFactor = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="减速",
        meta=(ClampMin="0.1", EditCondition="bSlow", DisplayName="减速持续时间（s）"))
    float SlowDuration = 2.f;

    // ── 眩晕 ──────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="眩晕",
        meta=(DisplayName="启用眩晕"))
    bool bStun = false;

    /** 眩晕持续时间（s），期间实体停止主动移动 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="眩晕",
        meta=(ClampMin="0.1", EditCondition="bStun", DisplayName="眩晕持续时间（s）"))
    float StunDuration = 1.f;

    // ── 自定义效果 ────────────────────────────────────────────
    /** 项目自定义效果列表，命中时通过 OnCustomEffect 委托广播给 BP */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="自定义效果",
        meta=(DisplayName="自定义效果列表"))
    TArray<FFlowFieldCustomEffectEntry> CustomEffects;
};

// ── 飞行体 ────────────────────────────────────────────────────

USTRUCT(BlueprintType)
struct FLOWFIELD_API FFlowFieldProjectileConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="飞行体",
        meta=(DisplayName="发射起点"))
    FVector Origin = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="飞行体",
        meta=(DisplayName="目标位置"))
    FVector Target = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="飞行体",
        meta=(ClampMin="1", DisplayName="命中半径（cm）"))
    float HitRadius = 60.f;

    /** 0 = 由飞行速度自动计算 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="飞行体",
        meta=(ClampMin="0", DisplayName="飞行时间（s，0=自动）"))
    float TravelTime = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="飞行体",
        meta=(ClampMin="1", DisplayName="飞行速度（cm/s）"))
    float ProjectileSpeed = 1000.f;

    /** 1=单发，>1=扇形散射（霰弹枪） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="飞行体",
        meta=(ClampMin="1", ClampMax="32", DisplayName="弹数（1=单发 >1=散弹）"))
    int32 RayCount = 1;

    /** 扇形总角度（°），RayCount > 1 时生效 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="飞行体",
        meta=(ClampMin="1", ClampMax="180", EditCondition="RayCount>1", DisplayName="散射角度（°）"))
    float FanAngle = 30.f;

    /** 穿透：命中后继续飞行，不重复命中同一实体 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="飞行体",
        meta=(DisplayName="穿透"))
    bool bPiercing = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="飞行体",
        meta=(DisplayName="效果"))
    FFlowFieldEffectParams Effects;
};

// ── 激光（直线 / 扇形） ──────────────────────────────────────

USTRUCT(BlueprintType)
struct FLOWFIELD_API FFlowFieldLaserConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="激光",
        meta=(DisplayName="发射起点"))
    FVector Origin = FVector::ZeroVector;

    /** 方向目标点，射线沿此方向延伸到最大射程 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="激光",
        meta=(DisplayName="方向目标点"))
    FVector Target = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="激光",
        meta=(ClampMin="1", DisplayName="最大射程（cm）"))
    float MaxRange = 3000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="激光",
        meta=(ClampMin="1", DisplayName="命中半径（cm）"))
    float HitRadius = 60.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="激光",
        meta=(ClampMin="0", DisplayName="视觉持续时间（s）"))
    float VisualDuration = 0.15f;

    /** 1=直线，>1=扇形展开 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="激光",
        meta=(ClampMin="1", ClampMax="32", DisplayName="射线数量（1=直线 >1=扇形）"))
    int32 RayCount = 1;

    /** 扇形总角度（°），RayCount > 1 时生效 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="激光",
        meta=(ClampMin="1", ClampMax="360", EditCondition="RayCount>1", DisplayName="扇形角度（°）"))
    float FanAngle = 60.f;

    /** 穿透：每条射线命中多个目标 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="激光",
        meta=(DisplayName="穿透"))
    bool bPiercing = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="激光",
        meta=(DisplayName="效果"))
    FFlowFieldEffectParams Effects;
};

// ── 连锁激光 ──────────────────────────────────────────────────

USTRUCT(BlueprintType)
struct FLOWFIELD_API FFlowFieldChainConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="连锁",
        meta=(DisplayName="发射起点"))
    FVector Origin = FVector::ZeroVector;

    /** 第一跳方向目标点 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="连锁",
        meta=(DisplayName="第一跳目标点"))
    FVector Target = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="连锁",
        meta=(ClampMin="1", DisplayName="第一跳最大射程（cm）"))
    float MaxRange = 3000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="连锁",
        meta=(ClampMin="1", DisplayName="命中半径（cm）"))
    float HitRadius = 60.f;

    /** 每次跳跃时搜索下一目标的半径 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="连锁",
        meta=(ClampMin="1", DisplayName="跳跃搜索半径（cm）"))
    float ChainRadius = 400.f;

    /** 最大连锁跳数（含第一次命中） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="连锁",
        meta=(ClampMin="1", ClampMax="20", DisplayName="最大跳数"))
    int32 MaxChainCount = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="连锁",
        meta=(ClampMin="0", DisplayName="视觉持续时间（s）"))
    float VisualDuration = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="连锁",
        meta=(DisplayName="效果"))
    FFlowFieldEffectParams Effects;
};

// ── 爆炸 ──────────────────────────────────────────────────────

USTRUCT(BlueprintType)
struct FLOWFIELD_API FFlowFieldExplosionConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="爆炸",
        meta=(DisplayName="爆炸中心"))
    FVector Center = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="爆炸",
        meta=(ClampMin="1", DisplayName="爆炸半径（cm）"))
    float Radius = 300.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="爆炸",
        meta=(ClampMin="0", DisplayName="视觉持续时间（s）"))
    float VisualDuration = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="爆炸",
        meta=(DisplayName="效果"))
    FFlowFieldEffectParams Effects;
};

// ── 运行时内部状态（非 USTRUCT，不需要 UHT）────────────────────

enum class EFlowFieldAttackTypeTag : uint8
{
    Projectile,
    Laser,
    Chain,
    Explosion,
};

/** 空间哈希缓存 — 每帧在 AttackProcessor 中重建 */
struct FFlowFieldSpatialCache
{
    struct FEntry
    {
        int32     EntityId;
        FVector2D Pos2D;
        AActor*   Actor;
    };

    static constexpr float kCellSize    = 300.f;
    static constexpr float kInvCellSize = 1.f / kCellSize;

    TMap<uint64, TArray<int32>> Grid;
    TArray<FEntry>               All;

    void Reset(int32 Reserve = 0)
    {
        Grid.Reset();
        All.Reset();
        if (Reserve > 0) All.Reserve(Reserve);
    }

    void Add(int32 EntityId, FVector Pos, AActor* Actor)
    {
        const int32 Idx = All.Num();
        All.Add({ EntityId, FVector2D(Pos.X, Pos.Y), Actor });
        const int32 CX = FMath::FloorToInt(Pos.X * kInvCellSize);
        const int32 CY = FMath::FloorToInt(Pos.Y * kInvCellSize);
        Grid.FindOrAdd(Key(CX, CY)).Add(Idx);
    }

    void QueryRadius(FVector Center, float Radius, TArray<const FEntry*>& Out) const
    {
        const int32 MinCX = FMath::FloorToInt((Center.X - Radius) * kInvCellSize);
        const int32 MaxCX = FMath::FloorToInt((Center.X + Radius) * kInvCellSize);
        const int32 MinCY = FMath::FloorToInt((Center.Y - Radius) * kInvCellSize);
        const int32 MaxCY = FMath::FloorToInt((Center.Y + Radius) * kInvCellSize);
        const float RadSq = Radius * Radius;

        for (int32 CX = MinCX; CX <= MaxCX; ++CX)
        for (int32 CY = MinCY; CY <= MaxCY; ++CY)
        {
            const TArray<int32>* Bucket = Grid.Find(Key(CX, CY));
            if (!Bucket) continue;
            for (int32 Idx : *Bucket)
            {
                const FEntry& E = All[Idx];
                const float Dx = E.Pos2D.X - Center.X;
                const float Dy = E.Pos2D.Y - Center.Y;
                if (Dx * Dx + Dy * Dy <= RadSq)
                    Out.Add(&E);
            }
        }
    }

    void QueryLine(FVector A, FVector B, float Radius, TArray<const FEntry*>& Out) const
    {
        const float MinX = FMath::Min(A.X, B.X) - Radius;
        const float MaxX = FMath::Max(A.X, B.X) + Radius;
        const float MinY = FMath::Min(A.Y, B.Y) - Radius;
        const float MaxY = FMath::Max(A.Y, B.Y) + Radius;

        const int32 MinCX = FMath::FloorToInt(MinX * kInvCellSize);
        const int32 MaxCX = FMath::FloorToInt(MaxX * kInvCellSize);
        const int32 MinCY = FMath::FloorToInt(MinY * kInvCellSize);
        const int32 MaxCY = FMath::FloorToInt(MaxY * kInvCellSize);

        const float RadSq = Radius * Radius;
        const FVector2D S(A.X, A.Y), E(B.X, B.Y);
        const FVector2D LineDir = E - S;
        const float     LineLenSq = LineDir.SizeSquared();

        for (int32 CX = MinCX; CX <= MaxCX; ++CX)
        for (int32 CY = MinCY; CY <= MaxCY; ++CY)
        {
            const TArray<int32>* Bucket = Grid.Find(Key(CX, CY));
            if (!Bucket) continue;
            for (int32 Idx : *Bucket)
            {
                const FEntry& En = All[Idx];
                float DistSq;
                if (LineLenSq < 1.f)
                {
                    const FVector2D D = En.Pos2D - S;
                    DistSq = D.SizeSquared();
                }
                else
                {
                    const float T = FMath::Clamp(
                        FVector2D::DotProduct(En.Pos2D - S, LineDir) / LineLenSq, 0.f, 1.f);
                    const FVector2D Closest = S + LineDir * T;
                    const FVector2D D = En.Pos2D - Closest;
                    DistSq = D.SizeSquared();
                }
                if (DistSq <= RadSq)
                    Out.Add(&En);
            }
        }
    }

private:
    static uint64 Key(int32 X, int32 Y)
    {
        return (uint64((uint32)X) << 32) | uint64((uint32)Y);
    }
};

/** 活跃攻击运行时状态（所有类型统一） */
struct FFlowFieldActiveAttack
{
    int32                  AttackId  = -1;
    EFlowFieldAttackTypeTag Type     = EFlowFieldAttackTypeTag::Projectile;
    bool                   bActive  = true;
    bool                   bPiercing = false;
    float                  ElapsedTime  = 0.f;
    float                  VisualDuration = 0.15f;

    // 伤害参数（从 Config 展平，避免持有 Config 引用）
    float DirectDamage    = 0.f;
    float DotDamage       = 0.f;
    float DotDuration     = 0.f;
    float DotInterval     = 0.5f;
    bool  bKnockback      = false;
    float KnockbackStrength = 0.f;
    float KnockbackRadius   = 0.f;
    float HitRadius         = 60.f;

    // 飞行体
    FVector Origin       = FVector::ZeroVector;
    FVector Target       = FVector::ZeroVector; // 飞行体目标 / 爆炸中心 / 激光方向
    float   TotalTime    = 1.f;
    FVector CurrentPos   = FVector::ZeroVector;

    // 连锁
    float   MaxRange     = 3000.f;
    float   ChainRadius  = 400.f;
    int32   MaxChainCount = 3;

    // 减速
    bool  bSlow         = false;
    float SlowFactor    = 0.5f;
    float SlowDuration  = 2.f;

    // 眩晕
    bool  bStun         = false;
    float StunDuration  = 1.f;

    // 自定义效果
    TArray<FFlowFieldCustomEffectEntry> CustomEffects;

    // 穿透：已命中实体集合
    TSet<int32>     HitEntityIds;

    // 激光/连锁路径（[0]=Origin，[1..N]=各射线/跳跃终点，调试+命中截断用）
    TArray<FVector> DebugPath;
};

/** DoT 条目，按实体 ID 聚合 */
struct FFlowFieldDoTEntry
{
    int32 AttackId      = -1;
    float DamagePerSec  = 0.f;
    float TimeRemaining = 0.f;
    float DotInterval   = 0.5f;
    float IntervalTimer = 0.f;
};
