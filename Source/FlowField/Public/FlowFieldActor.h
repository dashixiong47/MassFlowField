#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"
#include "FlowFieldTypes.h"
#include "FlowFieldGrid.h"
#include "FlowFieldDebugComponent.h"
#include "FlowFieldTargetComponent.h"
#include "FlowFieldAttackComponent.h"
#include "FlowFieldActor.generated.h"

class UFlowFieldObstacleComponent;
class UFlowFieldVATDataAsset;
class UInstancedStaticMeshComponent;

// 被追踪目标信息（由 UpdateTarget 定时缓存，供各 Processor 读取）
struct FFlowFieldTrackedTarget
{
    FVector Position     = FVector::ZeroVector;
    float   PushRadius   = 100.f; // 玩家推挤 AI + 计入减速的接触半径（cm）
    float   PushStrength = 600.f; // 最大推挤速度（cm/s），近处最强
    TWeakObjectPtr<UFlowFieldTargetComponent> OwnerComp; // 回写减速乘数用
    // 注：AI 感知范围由各实体 Fragment.DetectRadius 决定，不再存于此结构体
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

    /**
     * 扫描模式：
     * - TopDown：原始行为，单次射线取第一个命中，速度快但遇到屋顶/顶棚会遮挡下方楼梯。
     * - GroundLevel：穿透整列取所有法线朝上的命中面，选取 Z 最低的可走面，
     *   适用于楼梯、走廊等上方有遮挡物的场景。
     */
    UPROPERTY(EditAnywhere, Category="FlowField",
        meta=(DisplayName="扫描模式"))
    EFlowFieldScanMode ScanMode = EFlowFieldScanMode::TopDown;

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

    // 扫描烘焙的静态障碍 mask（0=可走, 1=障碍）
    // 序列化到 .umap，引擎重启后无需重新扫描
    UPROPERTY()
    TArray<uint8> BakedObstacleMask;

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

    // 由 FlowFieldSubsystem::FinalizeScan 调用，将扫描障碍直接烘焙进网格
    void BakeObstaclesFromScan(int32 W, int32 H, const TArray<FIntPoint>& ObstacleCells);

    // 清除烘焙数据，并重建当前网格（编辑器 Clear 调用）
    void ClearBakedObstacles();
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
    const TArray<TWeakObjectPtr<UFlowFieldObstacleComponent>>& GetRegisteredObstacles() const { return RegisteredObstacles; }

    // 人群计数（由 RVO 处理器每帧写入，Tick 读取后分发到各目标组件）
    // 与 TrackedTargets 等长，索引对应
    TArray<int32> CrowdCounts;

    // 攻击管理组件（由 AttackProcessor 每帧驱动）
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="FlowField|攻击")
    UFlowFieldAttackComponent* AttackComp = nullptr;

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

    // ── VAT 渲染 ISM 管理 ─────────────────────────────────────────
public:
    /**
     * 根据 DataAsset 获取（或创建）对应的 ISM 组件。
     * 使用 ISM（非 HISM）— 每帧更新 transform 时无 BVH 重建开销。
     * 由 UFlowFieldVATProcessor 在游戏线程调用。
     */
    UInstancedStaticMeshComponent* GetOrCreateVATRenderer(
        UFlowFieldVATDataAsset* DataAsset);

private:
    /** DataAsset → ISM 映射，每种怪物类型对应一个 ISM */
    UPROPERTY()
    TMap<TObjectPtr<UFlowFieldVATDataAsset>,
         TObjectPtr<UInstancedStaticMeshComponent>> VATRenderers;
};