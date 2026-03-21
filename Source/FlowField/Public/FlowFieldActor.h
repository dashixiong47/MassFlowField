#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"
#include "FlowFieldTypes.h"
#include "FlowFieldGrid.h"
#include "FlowFieldDebugComponent.h"
#include "FlowFieldTargetComponent.h"
#include "FlowFieldActor.generated.h"

class UFlowFieldObstacleComponent;

// 被追踪目标信息（由 UpdateTarget 定时缓存，供各 Processor 读取）
struct FFlowFieldTrackedTarget
{
    FVector Position = FVector::ZeroVector;
    float   Radius   = 0.f;
};

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

    // ── 目标追踪 ──────────────────────────────────────────────────

    /** 追踪目标更新间隔（s）。0.5 = 每秒更新两次流场目标。 */
    UPROPERTY(EditAnywhere, Category="FlowField|目标追踪",
        meta=(ClampMin="0.1", ClampMax="5.0", DisplayName="目标更新间隔（s）"))
    float TargetUpdateInterval = 0.5f;

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

    // ── 调试绘制 ──────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, Category="FlowField|调试",
        meta=(DisplayName="绘制网格"))
    bool bDrawGrid = false;

    UPROPERTY(EditAnywhere, Category="FlowField|调试",
        meta=(DisplayName="绘制流场方向"))
    bool bDrawFlow = false;

    UPROPERTY(EditAnywhere, Category="FlowField|调试",
        meta=(DisplayName="绘制热力图"))
    bool bDrawHeatmap = true;

    UPROPERTY(EditAnywhere, Category="FlowField|调试",
        meta=(DisplayName="箭头缩放"))
    float ArrowScale = 0.4f;

    UPROPERTY(EditAnywhere, Category="FlowField|调试",
        meta=(ClampMin="500", ClampMax="50000", DisplayName="调试绘制距离（cm）"))
    float DebugDrawDistance = 5000.f;

    // ── 调试颜色 ──────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, Category="FlowField|调试|颜色",
        meta=(DisplayName="热力图近端色（Integration=0）"))
    FLinearColor DebugHeatLow  = FLinearColor(0.f, 1.f, 0.f);  // 绿

    UPROPERTY(EditAnywhere, Category="FlowField|调试|颜色",
        meta=(DisplayName="热力图远端色（Integration=Max）"))
    FLinearColor DebugHeatHigh = FLinearColor(1.f, 0.f, 0.f);  // 红

    UPROPERTY(EditAnywhere, Category="FlowField|调试|颜色",
        meta=(DisplayName="无流场时填充色"))
    FLinearColor DebugColorNoFlow = FLinearColor(0.47f, 0.47f, 0.47f); // 灰

    UPROPERTY(EditAnywhere, Category="FlowField|调试|颜色",
        meta=(DisplayName="障碍格轮廓色"))
    FLinearColor DebugColorObstacle = FLinearColor(0.9f, 0.15f, 0.1f); // 红

    UPROPERTY(EditAnywhere, Category="FlowField|调试|颜色",
        meta=(DisplayName="可走格轮廓色"))
    FLinearColor DebugColorWalkable = FLinearColor(0.1f, 0.7f, 0.15f); // 绿

    UPROPERTY(EditAnywhere, Category="FlowField|调试|颜色",
        meta=(DisplayName="流场箭头色"))
    FLinearColor DebugColorArrow = FLinearColor::White;

    UPROPERTY(EditAnywhere, Category="FlowField|调试|颜色",
        meta=(DisplayName="目标标记色"))
    FLinearColor DebugColorGoal = FLinearColor::Red;

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

    const TArray<FFlowFieldTrackedTarget>& GetTrackedTargets() const { return TrackedTargets; }

    // 目标注册接口（由 UFlowFieldTargetComponent 在 BeginPlay/EndPlay 调用）
    void RegisterTarget(UFlowFieldTargetComponent* Comp);
    void UnregisterTarget(UFlowFieldTargetComponent* Comp);

    // 障碍物注册接口（由 UFlowFieldObstacleComponent 在 BeginPlay/EndPlay 调用）
    void RegisterObstacle(UFlowFieldObstacleComponent* Comp);
    void UnregisterObstacle(UFlowFieldObstacleComponent* Comp);

    // 多线程 Processor 访问网格时使用此锁（读锁）
    mutable FRWLock GridRWLock;

    /**
     * A* 寻路：在当前流场格子上找从 Start 到 Goal 的路径，返回世界坐标路径点数组。
     * 空数组 = 无路径（障碍隔断或越界）。不依赖 bReady，只需格子已初始化。
     * 开销远小于全图 BFS，适合动态目标（如追踪玩家）按需调用。
     */
    UFUNCTION(BlueprintCallable, Category="FlowField")
    TArray<FVector> FindPath(FVector Start, FVector Goal) const;

    // 查询某世界坐标是否是边界格（walkable 且紧邻障碍）
    bool IsBorderCell(FVector WorldPos) const { return Grid.IsBorderCell(Grid.WorldToCell(WorldPos)); }

    // 获取边界格预计算的面向方向（指向相邻障碍的平均方向，非边界格返回零向量）
    FVector2D GetBorderFaceDir(FVector WorldPos) const { return Grid.GetBorderFaceDir(Grid.WorldToCell(WorldPos)); }

    // 手动重新计算边界格（障碍销毁/添加后调用）
    UFUNCTION(BlueprintCallable, CallInEditor, Category="FlowField")
    void RebuildBorderCells() { Grid.ComputeBorderCells(); }

    // 编辑器中重建障碍布局（由 UFlowFieldObstacleComponent 在属性变更/移动时调用）
    void RefreshEditorObstacleLayout() { OnConstruction(GetActorTransform()); }

    // 找最近的可走格子（击退后脱困用）
    FIntPoint FindNearestWalkable(FVector WorldPos) const
    {
        if (!bReady) return {-1, -1};
        return Grid.FindNearestWalkable(Grid.WorldToCell(WorldPos));
    }

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    // 编辑器 Viewport 也 Tick（不需要 Play），用于相机跟踪调试绘制
    virtual bool ShouldTickIfViewportsOnly() const override { return true; }
    virtual void OnConstruction(const FTransform& Transform) override;

    UFUNCTION()
    void OnRep_CurrentGoal();

    void BeginPlay_Client();
    void GenerateInternal(FVector GoalPos);

#if WITH_EDITOR
    virtual void PostEditMove(bool bFinished) override;
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
    FFlowFieldGrid Grid;

    UPROPERTY()
    UFlowFieldDebugComponent* DebugComp = nullptr;

    // 相机跟踪：移动超过半格才重建
    FVector LastDebugCamPos = FVector(FLT_MAX, FLT_MAX, FLT_MAX);
    bool    bDebugDirty     = false;

    // Debug 地面 Z 缓存（线迹只做一次，之后复用）
    TArray<float> CachedDebugZ;
    int32         CachedDebugW = 0;
    int32         CachedDebugH = 0;

    void ApplyObstaclesToGrid(FFlowFieldGrid& TargetGrid);
    void ApplySingleObstacleToGrid(FFlowFieldGrid& TargetGrid, UFlowFieldObstacleComponent* Comp);
    void RebuildFlowAfterObstacleChange(); // 障碍变化后重建积分场+流场（保留地表数据）
    void RebuildDebugLines(FVector CamPos); // 构建线段数组 → UpdateLines
    void RefreshDebugNow();                 // 立即取相机位置重建（编辑器用）

    // 目标追踪
    FTimerHandle TargetUpdateTimer;
    TArray<FFlowFieldTrackedTarget> TrackedTargets;
    // 注册制：组件主动注册，避免每次 TActorIterator 全场扫描
    TArray<TWeakObjectPtr<UFlowFieldTargetComponent>> RegisteredTargets;
    void UpdateTarget(); // 刷新已注册目标的位置 → 更新 TrackedTargets

    // 障碍物注册列表
    TArray<TWeakObjectPtr<UFlowFieldObstacleComponent>> RegisteredObstacles;
};