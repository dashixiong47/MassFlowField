#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"
#include "FlowFieldTypes.h"
#include "FlowFieldGrid.h"
#include "FlowFieldActor.generated.h"

UCLASS()
class FLOWFIELD_API AFlowFieldActor : public AActor
{
    GENERATED_BODY()

public:
    AFlowFieldActor();

    // ── 基础配置 ──────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, Category="FlowField",
        meta=(DisplayName="边界模式"))
    EFlowFieldBoundsMode BoundsMode = EFlowFieldBoundsMode::VolumeBox;

    UPROPERTY(EditAnywhere, Category="FlowField",
        meta=(DisplayName="边界盒"))
    UBoxComponent* BoundsBox;

    UPROPERTY(EditAnywhere, Category="FlowField",
        meta=(ClampMin="50", ClampMax="1000", DisplayName="格子尺寸（cm）"))
    float CellSize = 200.f;

    UPROPERTY(EditAnywhere, Category="FlowField",
        meta=(ClampMin="0", ClampMax="89", DisplayName="最大可走坡度（°）"))
    float MaxWalkSlope = 45.f;

    UPROPERTY(EditAnywhere, Category="FlowField",
        meta=(ClampMin="0", DisplayName="最大台阶高度（cm）"))
    float MaxStepHeight = 45.f;

    UPROPERTY(EditAnywhere, Category="FlowField",
        meta=(DisplayName="障碍物 Tag"))
    FName ObstacleTag = FName("FlowFieldObstacle");

    UPROPERTY(EditAnywhere, Category="FlowField",
        meta=(ClampMin="-500", ClampMax="500", DisplayName="障碍物扩展半径（cm）"))
    float ObstacleRadius = 0.f;

    UPROPERTY(EditAnywhere, Category="FlowField",
        meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="障碍物重叠阈值"))
    float ObstacleOverlapThreshold = 0.5f;

    // ── 调试绘制 ──────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, Category="FlowField|调试",
        meta=(DisplayName="绘制网格"))
    bool bDrawGrid = true;

    UPROPERTY(EditAnywhere, Category="FlowField|调试",
        meta=(DisplayName="绘制流场方向"))
    bool bDrawFlow = true;

    UPROPERTY(EditAnywhere, Category="FlowField|调试",
        meta=(DisplayName="绘制热力图"))
    bool bDrawHeatmap = true;

    UPROPERTY(EditAnywhere, Category="FlowField|调试",
        meta=(DisplayName="绘制 Integration 数值"))
    bool bDrawScores = true;

    UPROPERTY(EditAnywhere, Category="FlowField|调试",
        meta=(DisplayName="箭头缩放"))
    float ArrowScale = 0.4f;

    UPROPERTY(EditAnywhere, Category="FlowField|调试",
        meta=(ClampMin="500", ClampMax="50000", DisplayName="调试绘制距离（cm）"))
    float DebugDrawDistance = 5000.f;

    // ── 运行时状态（只读）────────────────────────────────────────

    // 当前目标位置，服务端设置后自动复制到客户端触发重新生成
    UPROPERTY(ReplicatedUsing=OnRep_CurrentGoal, VisibleAnywhere, BlueprintReadOnly, Category="FlowField|状态")
    FVector CurrentGoal = FVector::ZeroVector;

    // 流场是否已经生成完毕可以查询
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="FlowField|状态")
    bool bReady = false;

    // 扫描保存的地表高度（ScanAndPlaceObstacles 写入）
    UPROPERTY()
    TArray<float> SavedSurfaceZ;

    UPROPERTY()
    TArray<FVector> SavedNormals;

    UPROPERTY(VisibleAnywhere, Category="FlowField|状态")
    int32 SavedGridWidth = 0;

    UPROPERTY(VisibleAnywhere, Category="FlowField|状态")
    int32 SavedGridHeight = 0;

    // ── 公开 API ──────────────────────────────────────────────────

    // 在服务端生成流场（自动复制 CurrentGoal 触发客户端同步生成）
    UFUNCTION(BlueprintCallable, Category="FlowField")
    void Generate(FVector GoalPos);

    UFUNCTION(BlueprintCallable, Category="FlowField")
    FVector GetFlowDirection(FVector WorldPos) const;

    UFUNCTION(BlueprintCallable, Category="FlowField")
    FVector GetFlowDirectionSmooth(FVector WorldPos) const;

    UFUNCTION(BlueprintCallable, Category="FlowField")
    bool CanReach(FVector WorldPos) const;

    // 返回 Integration 值；< 0 表示障碍或越界
    UFUNCTION(BlueprintCallable, Category="FlowField")
    float GetIntegration(FVector WorldPos) const;

    UFUNCTION(CallInEditor, Category="FlowField")
    void ClearDebug();

    bool ResolveBounds(FVector& OutMin, FVector& OutMax) const;
    FIntPoint WorldToCell(FVector WorldPos) const { return Grid.WorldToCell(WorldPos); }

    // 依赖 bReady，流场算完才能用
    bool GetSurfaceData(FVector WorldPos, float& OutSurfaceZ, FVector& OutNormal) const;

    // 直接查 SavedSurfaceZ，不依赖 bReady（实体生成时可立即贴地）
    bool GetSavedSurfaceData(FVector WorldPos, float& OutSurfaceZ, FVector& OutNormal) const;

    void  ReleaseCell(FIntPoint Cell);
    int32 GetOccupancy(FIntPoint Cell) const;
    void  ResetOccupancy();

    // 获取格子中心世界坐标
    FVector GetCellCenter(FIntPoint Cell) const;

    // 找最近的可走格子（击退后脱困用）
    FIntPoint FindNearestWalkable(FVector WorldPos) const
    {
        if (!bReady) return {-1, -1};
        return Grid.FindNearestWalkable(Grid.WorldToCell(WorldPos));
    }

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
    virtual void BeginPlay() override;

    UFUNCTION()
    void OnRep_CurrentGoal();

    void BeginPlay_Client();
    void GenerateInternal(FVector GoalPos);

#if WITH_EDITOR
    virtual void PostEditMove(bool bFinished) override;
#endif

private:
    FFlowFieldGrid Grid;

    void ApplyObstaclesToGrid(FFlowFieldGrid& TargetGrid);
    void DrawDebug() const;
};