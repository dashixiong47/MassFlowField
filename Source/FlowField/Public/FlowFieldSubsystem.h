#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "MassEntityTypes.h"
#include "MassEntityQuery.h"
#include "MassClientBubbleInfoBase.h"
#include "MassAI/FlowFieldSpatialHash.h"
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

    FVector Min       = FVector::ZeroVector;
    int32   W         = 0;
    int32   H         = 0;
    float   CellSize  = 0.f;
    float   Half      = 0.f;
    float   MaxStepH  = 0.f;
    int32   SubmitIdx = 0;
    int32   Total     = 0;

    TSharedPtr<TArray<TPair<FIntPoint, FVector>>> SpawnList;
    int32 SpawnIdx    = 0;
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

    // ── 空间哈希（由 FlowFieldSpatialHashProcessor 每帧更新）────────
    FFlowFieldSpatialHash SpatialHash;

    // ── 击退 ──────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category="FlowField|击退")
    void ApplyKnockback(FVector WorldPos, float Radius,
                        FVector Direction, float Force,
                        float DecaySpeed = 5.f);

    UFUNCTION(BlueprintCallable, Category="FlowField|击退")
    void ApplyExplosionKnockback(FVector WorldPos, float Radius,
                                  float Force, float DecaySpeed = 5.f);

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
    void SpawnNextBatch();
};