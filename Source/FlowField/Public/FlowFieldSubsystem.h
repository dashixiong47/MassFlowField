#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "MassEntityTypes.h"
#include "MassEntityQuery.h"
#include "MassClientBubbleInfoBase.h"
#include "FlowFieldAttackTypes.h"
#include "FlowFieldSubsystem.generated.h"

class AFlowFieldActor;

struct FCellScanInfo
{
    bool    bHit     = false;
    float   SurfaceZ = 0.f;
    FVector Normal   = FVector::UpVector;
};

struct FScanState
{
    TSharedPtr<TArray<FCellScanInfo>>  CellInfosPtr;
    TSharedPtr<TArray<FTraceDelegate>> DelegatesPtr;
    TSharedPtr<FThreadSafeCounter>     PendingCount;

    FVector Min                 = FVector::ZeroVector;
    float   MaxZ                = 0.f;   // BoundsBox 顶面 Z，GroundLevel 模式用于裁掉超出范围的命中
    int32   W                   = 0;
    int32   H                   = 0;
    float   CellSize            = 0.f;
    float   Half                = 0.f;
    float   MaxStepH            = 0.f;
    int32   SubmitIdx           = 0;
    int32   Total               = 0;

    // GroundLevel 模式
    bool    bGroundLevelScan    = false;
    float   WalkableThreshold   = 0.707f;  // cos(45°)

    bool  bSubmitDone = false;
};

UCLASS()
class FLOWFIELD_API UFlowFieldSubsystem : public UWorldSubsystem, public FTickableGameObject
{
    GENERATED_BODY()

public:
    virtual void PostInitialize() override;

    // 子类重写此函数返回项目使用的 BubbleInfo 类
    // 默认返回插件自带的 AFlowFieldClientBubbleInfo
    virtual TSubclassOf<AMassClientBubbleInfoBase> GetBubbleInfoClass() const;

    void RegisterActor(AFlowFieldActor* Actor);

    // ── FTickableGameObject ───────────────────────────────────────
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override { return ScanState.IsValid(); }
    virtual bool IsTickableInEditor() const override { return true; }
    virtual TStatId GetStatId() const override
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(UFlowFieldSubsystem, STATGROUP_Tickables);
    }

    // ── Query ─────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category="FlowField")
    FVector GetFlowDirection(FVector WorldPos) const;

    UFUNCTION(BlueprintCallable, Category="FlowField")
    FVector GetFlowDirectionSmooth(FVector WorldPos) const;

    UFUNCTION(BlueprintCallable, Category="FlowField")
    bool CanReach(FVector WorldPos) const;

    UFUNCTION(BlueprintCallable, Category="FlowField")
    float GetIntegration(FVector WorldPos) const;

    // ── Control ───────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category="FlowField")
    void Generate(FVector GoalPos);

    UFUNCTION(BlueprintCallable, Category="FlowField")
    bool IsReady() const;

    UFUNCTION(BlueprintCallable, Category="FlowField")
    AFlowFieldActor* GetActor() const { return FlowFieldActor; }

    // ── 攻击系统 ──────────────────────────────────────────────────

    /** 发射飞行子弹，沿直线飞向目标，返回 AttackId */
    UFUNCTION(BlueprintCallable, Category="FlowField|攻击",
        meta=(DisplayName="发射飞行体（子弹）"))
    int32 FireProjectile(const FFlowFieldProjectileConfig& Config);

    /** 发射激光：RayCount=1 直线，RayCount>1 扇形，返回 AttackId */
    UFUNCTION(BlueprintCallable, Category="FlowField|攻击",
        meta=(DisplayName="发射激光（直线/扇形）"))
    int32 FireLaser(const FFlowFieldLaserConfig& Config);

    /** 发射连锁闪电，依次跳跃命中最近目标，返回 AttackId */
    UFUNCTION(BlueprintCallable, Category="FlowField|攻击",
        meta=(DisplayName="发射连锁激光"))
    int32 FireChain(const FFlowFieldChainConfig& Config);

    /** 触发范围爆炸，立即对半径内所有目标造成伤害，返回 AttackId */
    UFUNCTION(BlueprintCallable, Category="FlowField|攻击",
        meta=(DisplayName="触发爆炸"))
    int32 FireExplosion(const FFlowFieldExplosionConfig& Config);

    /** 提前取消攻击（会触发 OnAttackEnd 事件） */
    UFUNCTION(BlueprintCallable, Category="FlowField|攻击",
        meta=(DisplayName="取消攻击"))
    void CancelAttack(int32 AttackId);

    UFUNCTION(BlueprintCallable, Category="FlowField|死亡")
    void KillAgent(int32 EntityId);

    UFUNCTION(BlueprintCallable, Category="FlowField|死亡")
    void DestroyAgent(int32 EntityId);

    // ── 击退 ──────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category="FlowField|击退")
    void ApplyKnockback(FVector WorldPos, float Radius,
                        FVector Direction, float Force,
                        float DecaySpeed = 5.f, float StaggerDuration = 0.f);

    UFUNCTION(BlueprintCallable, Category="FlowField|击退")
    void ApplyExplosionKnockback(FVector WorldPos, float Radius,
                                  float Force, float DecaySpeed = 5.f,
                                  float StaggerDuration = 0.f);

    // ── Obstacle scanning ─────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category="FlowField")
    void ScanAndPlaceObstacles(int32 BatchSize = 200);

    UFUNCTION(BlueprintCallable, Category="FlowField")
    bool IsScanning() const { return ScanState.IsValid(); }

    UFUNCTION(BlueprintCallable, Category="FlowField")
    void ClearObstacleActors();

    TDelegate<void(int32)> OnScanProgressUpdated;
    TDelegate<void(int32)> OnScanCompleted;

private:
    UPROPERTY()
    AFlowFieldActor* FlowFieldActor = nullptr;

    TSharedPtr<FScanState> ScanState;
    int32 ScanBatchSize = 200;

    // 击退用 Query，懒初始化
    FMassEntityQuery KnockbackQuery;

    void SubmitNextBatch();
    void FinalizeScan();
};