#pragma once
#include "CoreMinimal.h"
#include "FlowFieldAttackTypes.generated.h"

// ── 枚举 ──────────────────────────────────────────────────────────

UENUM(BlueprintType)
enum class EFlowFieldAttackType : uint8
{
    Projectile  UMETA(DisplayName="飞行体"),
    Laser       UMETA(DisplayName="激光"),
    Explosion   UMETA(DisplayName="爆炸"),
};

UENUM(BlueprintType)
enum class EFlowFieldLaserMode : uint8
{
    Straight    UMETA(DisplayName="直线"),
    Fan         UMETA(DisplayName="扇形"),
    Chain       UMETA(DisplayName="连锁"),
};

// ── 攻击配置（Blueprint 可读写）──────────────────────────────────

USTRUCT(BlueprintType)
struct FLOWFIELD_API FFlowFieldAttackConfig
{
    GENERATED_BODY()

    // ── 基础 ──────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="攻击")
    EFlowFieldAttackType Type = EFlowFieldAttackType::Projectile;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="攻击|激光",
        meta=(EditCondition="Type==EFlowFieldAttackType::Laser"))
    EFlowFieldLaserMode LaserMode = EFlowFieldLaserMode::Straight;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="攻击")
    FVector Origin = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="攻击")
    FVector Target = FVector::ZeroVector;

    /** 最大射程（cm）。激光线长 / 飞行体超出此距离自动销毁。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="攻击",
        meta=(ClampMin="1"))
    float MaxRange = 3000.f;

    /** 命中碰撞半径（cm）。飞行体球体半径 / 激光宽度 / 爆炸范围。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="攻击",
        meta=(ClampMin="1"))
    float HitRadius = 60.f;

    // ── 飞行体 ────────────────────────────────────────────────────
    /** 飞行时间（s）。0 = 用 ProjectileSpeed 自动计算。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="攻击|飞行体",
        meta=(ClampMin="0"))
    float TravelTime = 0.f;

    /** 飞行速度（cm/s）。TravelTime > 0 时忽略。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="攻击|飞行体",
        meta=(ClampMin="1"))
    float ProjectileSpeed = 1000.f;

    // ── 激光 ──────────────────────────────────────────────────────
    /** 视觉持续时间（s）。激光/爆炸伤害即时，但调试线 + OnAttackEnd 在此时间后触发。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="攻击|激光",
        meta=(ClampMin="0"))
    float VisualDuration = 0.15f;

    /** 扇形总角度（°）。LaserMode=Fan 时生效。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="攻击|激光|扇形",
        meta=(ClampMin="1", ClampMax="360"))
    float FanAngle = 60.f;

    /** 扇形射线数量。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="攻击|激光|扇形",
        meta=(ClampMin="1", ClampMax="32"))
    int32 FanRayCount = 5;

    /** 连锁跳跃搜索半径（cm）。LaserMode=Chain 时，每次命中后在此半径内找下一目标。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="攻击|激光|连锁",
        meta=(ClampMin="1"))
    float ChainRadius = 400.f;

    /** 最大连锁跳跃次数（含第一次命中）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="攻击|激光|连锁",
        meta=(ClampMin="1", ClampMax="20"))
    int32 MaxChainCount = 3;

    // ── 伤害 ──────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="攻击|伤害",
        meta=(ClampMin="0"))
    float DirectDamage = 10.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="攻击|持续伤害",
        meta=(ClampMin="0"))
    float DotDamage = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="攻击|持续伤害",
        meta=(ClampMin="0"))
    float DotDuration = 0.f;

    /** DoT 触发间隔（s）。每隔此时间派发一次 OnDoTTick 事件。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="攻击|持续伤害",
        meta=(ClampMin="0.05"))
    float DotInterval = 0.5f;

    // ── 击退 ──────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="攻击|击退",
        meta=(ClampMin="0"))
    float KnockbackStrength = 0.f;

    /** 击退生效半径（cm）。0 = 与 HitRadius 相同。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="攻击|击退",
        meta=(ClampMin="0"))
    float KnockbackRadius = 0.f;

    // ── 行为 ──────────────────────────────────────────────────────
    /** 穿透：命中后不停止，继续命中其他实体（不重复命中同一实体）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="攻击")
    bool bPiercing = false;
};

// ── 运行时内部状态（非 USTRUCT，不需要 UHT）────────────────────

/** 空间哈希缓存 — 每帧在 AttackProcessor 中重建，O(1) 插入 + O(局部范围) 查询 */
struct FFlowFieldSpatialCache
{
    struct FEntry
    {
        int32     EntityId;
        FVector2D Pos2D;
        AActor*   Actor; // VAT 实体为 nullptr
    };

    static constexpr float kCellSize    = 300.f;
    static constexpr float kInvCellSize = 1.f / kCellSize;

    TMap<uint64, TArray<int32>> Grid;   // cell → Entry 索引列表
    TArray<FEntry>               All;   // 所有实体扁平数组

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

    /** 球形查询（2D） */
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

    /** 线段胶囊查询（2D） */
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

/** 活跃攻击运行时状态 */
struct FFlowFieldActiveAttack
{
    int32                  AttackId     = -1;
    FFlowFieldAttackConfig Config;
    bool                   bActive      = true;
    float                  ElapsedTime  = 0.f;
    float                  TotalTime    = 0.f;  // 飞行体：计算后的总飞行时间

    // 飞行体：当前位置
    FVector                CurrentPos   = FVector::ZeroVector;
    // 穿透：已命中实体集合（O(1) 查询）
    TSet<int32>            HitEntityIds;

    // 激光直线：实际终点（命中非穿透时截断，调试绘制用）
    FVector                LaserEnd     = FVector::ZeroVector;
    // 激光连锁 / 扇形：路径点（调试绘制用）
    TArray<FVector>        DebugPath;

    // 激光/爆炸：是否已完成命中检测（只处理一次）
    bool                   bHitProcessed = false;
};

/** DoT（持续伤害）条目，按实体 ID 聚合到 Map 中 */
struct FFlowFieldDoTEntry
{
    int32 AttackId       = -1;
    float DamagePerSec   = 0.f;
    float TimeRemaining  = 0.f;
    float DotInterval    = 0.5f;
    float IntervalTimer  = 0.f;  // 从 DotInterval 开始，第一帧即触发
};
