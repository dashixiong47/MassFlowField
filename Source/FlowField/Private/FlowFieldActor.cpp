#include "FlowFieldActor.h"
#include "FlowFieldSubsystem.h"
#include "FlowFieldSettings.h"
#include "FlowFieldObstacleActor.h"
#include "FlowFieldObstacleComponent.h"
#include "FlowFieldTargetComponent.h"
#include "DrawDebugHelpers.h"
#include "FlowFieldDebugComponent.h"
#include "Engine/LevelBounds.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "VAT/FlowFieldVATDataAsset.h"
#include "Components/InstancedStaticMeshComponent.h"
#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#endif

AFlowFieldActor::AFlowFieldActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.05f; // 每 50ms 检查一次相机，不必每帧
    bReplicates = true;

    USceneComponent* Root = CreateDefaultSubobject<USceneComponent>("Root");
    SetRootComponent(Root);

    BoundsBox = CreateDefaultSubobject<UBoxComponent>("BoundsBox");
    BoundsBox->SetupAttachment(Root);
    BoundsBox->SetBoxExtent(FVector(2500.f, 2500.f, 300.f));
    BoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BoundsBox->SetHiddenInGame(true);
#if WITH_EDITOR
    BoundsBox->bVisualizeComponent = true;
#endif

    // 调试可视化组件：SceneProxy + PDI，与引擎碰撞体积可视化相同机制
    DebugComp = CreateDefaultSubobject<UFlowFieldDebugComponent>("FlowFieldDebugComp");
    DebugComp->SetupAttachment(Root);
    DebugComp->SetAbsolute(true, true, true); // 世界坐标，不随 Actor 偏移
    DebugComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DebugComp->SetCastShadow(false);

    // 攻击管理组件
    AttackComp = CreateDefaultSubobject<UFlowFieldAttackComponent>("FlowFieldAttackComp");
}

void AFlowFieldActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AFlowFieldActor, CurrentGoal);
}

void AFlowFieldActor::BeginPlay()
{
    Super::BeginPlay();
    if (auto* Sub = GetWorld()->GetSubsystem<UFlowFieldSubsystem>())
    {
        Sub->RegisterActor(this);
        UE_LOG(LogTemp, Log, TEXT("[FlowField] Actor registered, NetMode=%d"), (int32)GetWorld()->GetNetMode());
    }

    // 客户端初次加入时，CurrentGoal 可能已经有值但不触发 OnRep
    BeginPlay_Client();

    // 服务端：启动定时追踪（目标由 UFlowFieldTargetComponent 注册）
    if (HasAuthority())
    {
        GetWorldTimerManager().SetTimer(TargetUpdateTimer, this,
            &AFlowFieldActor::UpdateTarget,
            TargetUpdateInterval, /*bLoop=*/true, /*FirstDelay=*/0.f);
    }
}

void AFlowFieldActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    // 编辑器放置/移动时：重建 obstacle 格子（无需目标/流场），让调试可见
    if (!GetWorld() || GetWorld()->IsGameWorld()) return;

    FVector Min, Max;
    if (!ResolveBounds(Min, Max)) return;

    int32 W = FMath::CeilToInt((Max.X - Min.X) / CellSize);
    int32 H = FMath::CeilToInt((Max.Y - Min.Y) / CellSize);
    if (W <= 0 || H <= 0) return;

    FFlowFieldGrid EditorGrid;
    EditorGrid.Init(Min, W, H, CellSize);

    // 应用烘焙障碍（编辑器可见）
    if (BakedObstacleMask.Num() == W * H)
    {
        for (int32 Y = 0; Y < H; ++Y)
            for (int32 X = 0; X < W; ++X)
                if (BakedObstacleMask[Y * W + X])
                    EditorGrid.GetCell(X, Y).Cost = 255;
    }

    ApplyObstaclesToGrid(EditorGrid);

    bReady = false;
    Grid   = MoveTemp(EditorGrid);
    // 不生成 integration/flow，仅障碍布局可视
    CachedDebugZ.Reset();
    CachedDebugW   = 0;
    bDebugDirty    = true;
    LastDebugCamPos = FVector(FLT_MAX, FLT_MAX, FLT_MAX);
}

void AFlowFieldActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // ── 每帧刷新目标位置（减少推挤延迟，避免依赖 0.5s 定时器）──
    {
        FRWScopeLock WriteLock(GridRWLock, SLT_Write);
        for (FFlowFieldTrackedTarget& T : TrackedTargets)
        {
            UFlowFieldTargetComponent* Comp = T.OwnerComp.Get();
            if (Comp && Comp->GetOwner())
                T.Position = Comp->GetOwner()->GetActorLocation();
        }
    }

    // ── 分发人群计数 → 更新各目标组件的速度乘数 ─────────────────
    for (int32 i = 0; i < TrackedTargets.Num() && i < CrowdCounts.Num(); ++i)
    {
        UFlowFieldTargetComponent* Comp = TrackedTargets[i].OwnerComp.Get();
        if (!Comp) continue;
        const float Frac = FMath::Clamp(
            (float)CrowdCounts[i] / (float)FMath::Max(1, Comp->AgentsForMaxSlow),
            0.f, 1.f);
        Comp->CurrentSpeedMultiplier = FMath::Lerp(1.f, Comp->MaxSlowdownFactor, Frac);
    }

    const UFlowFieldSettings* S = UFlowFieldSettings::Get();

    // ── 调试：追踪范围平面圆（地面投影，黄色）────────────────────
    if (S->bDrawTargetRanges)
    {
        UWorld* W = GetWorld();
        for (const FFlowFieldTrackedTarget& T : TrackedTargets)
        {
            DrawDebugCircle(W, T.Position, T.PushRadius, 32, FColor(255, 220, 0),
                false, 0.f, 0, 2.f, FVector(1,0,0), FVector(0,1,0));
        }
    }

    // 没有任何调试开关打开时跳过网格/流场调试
    if (!S->bDrawGrid && !S->bDrawFlow && !S->bDrawHeatmap) return;
    if (!Grid.IsValid()) return;

    // 取相机位置
    FVector CamPos = FVector::ZeroVector;
    bool bHasCam   = false;
#if WITH_EDITOR
    if (GEditor && !GetWorld()->IsGameWorld())
    {
        for (FEditorViewportClient* VC : GEditor->GetAllViewportClients())
        {
            if (VC && VC->IsPerspective()) { CamPos = VC->GetViewLocation(); bHasCam = true; break; }
        }
    }
#endif
    if (!bHasCam)
    {
        if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
            if (PC->PlayerCameraManager) { CamPos = PC->PlayerCameraManager->GetCameraLocation(); bHasCam = true; }
    }
    if (!bHasCam) return;

    // 相机移动超过半格才重建线段/面片（避免每帧重建）
    const float MovedSq = FVector::DistSquared2D(CamPos, LastDebugCamPos);
    if (bDebugDirty || MovedSq >= FMath::Square(CellSize * 0.5f))
    {
        LastDebugCamPos = CamPos;
        bDebugDirty     = false;
        RebuildDebugLines(CamPos);
    }

    // 追踪目标范围球（黄色，每帧绘制 0 duration = 单帧持续）
    if (S->bDrawTargetRanges)
    {
        UWorld* W = GetWorld();
        for (const FFlowFieldTrackedTarget& T : TrackedTargets)
        {
            DrawDebugSphere(W, T.Position, T.PushRadius,
                32, FColor(255, 220, 0), false, 0.f, 0, 2.f);
        }
    }
}

void AFlowFieldActor::OnRep_CurrentGoal()
{
    UE_LOG(LogTemp, Log, TEXT("[FlowField][Client] OnRep_CurrentGoal -> %s"), *CurrentGoal.ToString());
    GenerateInternal(CurrentGoal);
}

void AFlowFieldActor::BeginPlay_Client()
{
    // 客户端 BeginPlay 时如果 CurrentGoal 已有值（服务端之前已设过目标）
    // OnRep 不会触发，需要手动生成一次流场
    if (!HasAuthority() && !CurrentGoal.IsZero())
    {
        UE_LOG(LogTemp, Log, TEXT("[FlowField][Client] BeginPlay 已有目标，直接生成流场 -> %s"), *CurrentGoal.ToString());
        GenerateInternal(CurrentGoal);
    }
}

void AFlowFieldActor::Generate(FVector GoalPos)
{
    if (!HasAuthority())
    {
        UE_LOG(LogTemp, Warning, TEXT("[FlowField] Generate 只能在服务端调用"));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[FlowField][Server] Generate -> Goal=%s"), *GoalPos.ToString());
    GenerateInternal(GoalPos);
    CurrentGoal = GoalPos;
}

void AFlowFieldActor::GenerateInternal(FVector GoalPos)
{
    FVector Min, Max;
    if (!ResolveBounds(Min, Max))
    {
        UE_LOG(LogTemp, Error, TEXT("[FlowField] ResolveBounds failed"));
        return;
    }

    int32 W = FMath::CeilToInt((Max.X - Min.X) / CellSize);
    int32 H = FMath::CeilToInt((Max.Y - Min.Y) / CellSize);

    if (W <= 0 || H <= 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[FlowField] Grid size invalid (%d x %d)"), W, H);
        return;
    }

    FFlowFieldGrid NewGrid;
    NewGrid.Init(Min, W, H, CellSize);

    if (SavedSurfaceZ.Num() == W * H && SavedGridWidth == W && SavedGridHeight == H)
    {
        for (int32 Y = 0; Y < H; ++Y)
            for (int32 X = 0; X < W; ++X)
            {
                FFlowFieldCell& Cell = NewGrid.GetCell(X, Y);
                Cell.SurfaceZ = SavedSurfaceZ[Y * W + X];
                Cell.Normal   = SavedNormals[Y * W + X];
            }
    }

    // 应用烘焙的静态障碍（扫描结果，序列化保存，无需重新扫描）
    if (BakedObstacleMask.Num() == W * H)
    {
        for (int32 Y = 0; Y < H; ++Y)
            for (int32 X = 0; X < W; ++X)
                if (BakedObstacleMask[Y * W + X])
                    NewGrid.GetCell(X, Y).Cost = 255;
    }

    ApplyObstaclesToGrid(NewGrid);
    NewGrid.SealEnclosedRegions();

    FIntPoint GoalCell = NewGrid.WorldToCell(GoalPos);

    if (!NewGrid.IsInBounds(GoalCell))
    {
        UE_LOG(LogTemp, Error, TEXT("[FlowField] Goal outside grid (%d,%d)"), GoalCell.X, GoalCell.Y);
        return;
    }

    if (NewGrid.GetCell(GoalCell.X, GoalCell.Y).IsBlocked())
    {
        TArray<FIntPoint> Ring = NewGrid.FindWalkableRingAroundBlocked(GoalCell);

        if (Ring.Num() == 0)
        {
            FIntPoint Fixed = NewGrid.FindNearestWalkable(GoalCell);
            if (Fixed.X < 0)
            {
                UE_LOG(LogTemp, Error, TEXT("[FlowField] No walkable cell near goal"));
                return;
            }
            NewGrid.BuildIntegrationField(Fixed);
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("[FlowField] 目标在障碍内，包围圈 %d 个格子"), Ring.Num());
            NewGrid.BuildIntegrationFieldMulti(Ring);
        }
    }
    else
    {
        NewGrid.BuildIntegrationField(GoalCell);
    }

    NewGrid.BuildFlowField();
    NewGrid.ComputeBorderCells(); // 预计算边界格，AI 贴墙检测直接查表
    {
        int32 BorderCount = 0;
        for (int32 Y = 0; Y < NewGrid.Height; Y++)
            for (int32 X = 0; X < NewGrid.Width; X++)
                if (NewGrid.IsBorderCell(FIntPoint(X, Y))) BorderCount++;
        UE_LOG(LogTemp, Log, TEXT("[FlowField] BorderCells computed: %d / %d"), BorderCount, NewGrid.Width * NewGrid.Height);
    }

    bReady = false;
    {
        FRWScopeLock WriteLock(GridRWLock, SLT_Write);
        Grid = MoveTemp(NewGrid);
    }
    bReady = true;
    Grid.ResetOccupancy();

    // 流场更新：清 Z 缓存，下一个 Tick 重建调试绘制
    CachedDebugZ.Reset();
    CachedDebugW = 0;
    bDebugDirty  = true;
    LastDebugCamPos = FVector(FLT_MAX, FLT_MAX, FLT_MAX); // 强制重建

    UE_LOG(LogTemp, Log, TEXT("[FlowField] GenerateInternal complete ✓  NetMode=%d"),
        (int32)GetWorld()->GetNetMode());
}

void AFlowFieldActor::ApplySingleObstacleToGrid(FFlowFieldGrid& TargetGrid, UFlowFieldObstacleComponent* Comp)
{
    if (!Comp || !Comp->GetOwner()) return;

    FVector Origin, Extent;
    Comp->GetOwner()->GetActorBounds(true, Origin, Extent);

    const float ExpandX = Extent.X + Comp->Radius;
    const float ExpandY = Extent.Y + Comp->Radius;

    FIntPoint CellMin = TargetGrid.WorldToCell(FVector(Origin.X - ExpandX, Origin.Y - ExpandY, 0.f));
    FIntPoint CellMax = TargetGrid.WorldToCell(FVector(Origin.X + ExpandX, Origin.Y + ExpandY, 0.f));

    CellMin.X = FMath::Clamp(CellMin.X, 0, TargetGrid.Width  - 1);
    CellMin.Y = FMath::Clamp(CellMin.Y, 0, TargetGrid.Height - 1);
    CellMax.X = FMath::Clamp(CellMax.X, 0, TargetGrid.Width  - 1);
    CellMax.Y = FMath::Clamp(CellMax.Y, 0, TargetGrid.Height - 1);

    // 障碍物顶部 Z：低于地表说明是地面/地板平面，整体跳过
    const float ObstacleTopZ = Origin.Z + Extent.Z;

    Comp->BlockedCells.Reset();
    int32 CellsMarked = 0;
    for (int32 Y = CellMin.Y; Y <= CellMax.Y; ++Y)
    for (int32 X = CellMin.X; X <= CellMax.X; ++X)
    {
        FFlowFieldCell& Cell = TargetGrid.GetCell(X, Y);
        if (Cell.IsBlocked()) continue;

        // 地表 Z 已知时：障碍物顶部需高于地表至少 MaxStepHeight，否则视为地面不阻断
        if (Cell.SurfaceZ != 0.f && ObstacleTopZ < Cell.SurfaceZ + MaxStepHeight) continue;

        Cell.Cost = 255;
        Comp->BlockedCells.Add(FIntPoint(X, Y));
        CellsMarked++;
    }
    UE_LOG(LogTemp, Log, TEXT("[FlowField] Obstacle '%s': ObstacleTopZ=%.0f, cells [%d,%d]-[%d,%d], marked=%d"),
        *Comp->GetOwner()->GetName(), ObstacleTopZ,
        CellMin.X, CellMin.Y, CellMax.X, CellMax.Y, CellsMarked);
}

void AFlowFieldActor::ApplyObstaclesToGrid(FFlowFieldGrid& TargetGrid)
{
    UWorld* World = GetWorld();
    if (!World) return;

    // 始终扫描场景（运行时 + 编辑器统一路径）：
    // 避免 Generate() 在障碍物 BeginPlay() 之前调用导致遗漏，
    // 同时正确跳过正在销毁的 Actor（UnregisterObstacle 触发的重建场景）。
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* A = *It;
        if (!IsValid(A) || A->IsActorBeingDestroyed()) continue;
        if (UFlowFieldObstacleComponent* Comp = A->FindComponentByClass<UFlowFieldObstacleComponent>())
            ApplySingleObstacleToGrid(TargetGrid, Comp);
    }
}

void AFlowFieldActor::RebuildFlowAfterObstacleChange()
{
    if (!bReady || CurrentGoal.IsZero()) return;
    GenerateInternal(CurrentGoal);
}

FVector AFlowFieldActor::GetFlowDirection(FVector WorldPos) const
{
    if (!bReady) return FVector::ZeroVector;
    return Grid.GetFlowDirection(WorldPos);
}

FVector AFlowFieldActor::GetFlowDirectionSmooth(FVector WorldPos) const
{
    if (!bReady) return FVector::ZeroVector;
    return Grid.GetFlowDirectionInterpolated(WorldPos);
}

bool AFlowFieldActor::CanReach(FVector WorldPos) const
{
    if (!bReady) return false;
    return Grid.GetIntegrationFast(WorldPos) >= 0.f;
}

float AFlowFieldActor::GetIntegration(FVector WorldPos) const
{
    if (!bReady) return -1.f;
    return Grid.GetIntegrationFast(WorldPos);
}

bool AFlowFieldActor::GetSurfaceData(FVector WorldPos, float& OutSurfaceZ, FVector& OutNormal) const
{
    if (!bReady) return false;
    FIntPoint P = Grid.WorldToCell(WorldPos);
    const FFlowFieldCell* C = Grid.TryGetCell(P.X, P.Y);
    if (!C) return false;
    OutSurfaceZ = C->SurfaceZ;
    OutNormal   = C->Normal;
    return true;
}

bool AFlowFieldActor::GetSavedSurfaceData(FVector WorldPos, float& OutSurfaceZ, FVector& OutNormal) const
{
    if (SavedSurfaceZ.Num() == 0 || SavedGridWidth <= 0 || SavedGridHeight <= 0)
        return false;

    FVector Min, Max;
    if (!ResolveBounds(Min, Max)) return false;

    int32 X = FMath::FloorToInt((WorldPos.X - Min.X) / CellSize);
    int32 Y = FMath::FloorToInt((WorldPos.Y - Min.Y) / CellSize);

    X = FMath::Clamp(X, 0, SavedGridWidth  - 1);
    Y = FMath::Clamp(Y, 0, SavedGridHeight - 1);

    int32 Idx = Y * SavedGridWidth + X;
    if (!SavedSurfaceZ.IsValidIndex(Idx)) return false;

    OutSurfaceZ = SavedSurfaceZ[Idx];
    OutNormal   = SavedNormals[Idx];
    return true;
}

void AFlowFieldActor::ReleaseCell(FIntPoint Cell)
{
    if (!bReady) return;
    Grid.ReleaseCell(Cell.X, Cell.Y);
}

int32 AFlowFieldActor::GetOccupancy(FIntPoint Cell) const
{
    if (!bReady) return 0;
    return Grid.GetOccupancy(Cell.X, Cell.Y);
}

void AFlowFieldActor::ResetOccupancy()
{
    if (!bReady) return;
    Grid.ResetOccupancy();
}

FVector AFlowFieldActor::GetCellCenter(FIntPoint Cell) const
{
    if (!bReady) return FVector::ZeroVector;
    return Grid.CellToWorld(Cell.X, Cell.Y);
}

bool AFlowFieldActor::ResolveBounds(FVector& OutMin, FVector& OutMax) const
{
    if (BoundsMode == EFlowFieldBoundsMode::VolumeBox)
    {
        FVector Center = BoundsBox->GetComponentLocation();
        FVector Extent = BoundsBox->GetScaledBoxExtent();
        OutMin = Center - Extent;
        OutMax = Center + Extent;
        return true;
    }

    ULevel* Level = GetWorld()->PersistentLevel;
    if (!Level) return false;

    FBox Box = ALevelBounds::CalculateLevelBounds(Level);
    if (!Box.IsValid) return false;

    OutMin = Box.Min;
    OutMax = Box.Max;
    return true;
}

void AFlowFieldActor::ClearDebug()
{
    if (DebugComp) DebugComp->ClearAll();
    if (UWorld* World = GetWorld()) FlushDebugStrings(World);
}

void AFlowFieldActor::RefreshDebugNow()
{
    if (!Grid.IsValid() || !DebugComp) return;

    UWorld* World = GetWorld();
    if (!World) return;

    FVector CamPos = FVector::ZeroVector;
    bool bFound = false;
#if WITH_EDITOR
    if (GEditor)
        for (FEditorViewportClient* VC : GEditor->GetAllViewportClients())
            if (VC && VC->IsPerspective()) { CamPos = VC->GetViewLocation(); bFound = true; break; }
#endif
    if (!bFound)
        if (APlayerController* PC = World->GetFirstPlayerController())
            if (PC->PlayerCameraManager) { CamPos = PC->PlayerCameraManager->GetCameraLocation(); bFound = true; }

    if (!bFound) return;
    LastDebugCamPos = CamPos;
    bDebugDirty     = false;
    RebuildDebugLines(CamPos);
}

void AFlowFieldActor::RebuildDebugLines(FVector CamPos)
{
    if (!Grid.IsValid() || !DebugComp) return;
    UWorld* World = GetWorld();
    if (!World) return;

    const UFlowFieldSettings* S = UFlowFieldSettings::Get();

    const float MaxInteg      = Grid.MaxIntegration();
    const float Half          = Grid.CellSize * 0.5f;
    const float ZOff          = 8.f;
    const float MaxDrawDistSq = S->DebugDrawDistance * S->DebugDrawDistance;
    const bool  bAnyDebug     = S->bDrawGrid || S->bDrawHeatmap || S->bDrawFlow;

    // ── 顶点网格 Z 缓存 ──────────────────────────────────────────────────────
    // 用 (W+1)×(H+1) 顶点网格替代 per-cell 单点线迹 + 法线平面投影。
    // 每个角单独打线迹 → 斜面/起伏地形完全贴合；相邻格子共用顶点 → 无冗余线迹。
    const int32 VW = Grid.Width  + 1;
    const int32 VH = Grid.Height + 1;

    if (CachedDebugW != Grid.Width || CachedDebugH != Grid.Height)
    {
        CachedDebugZ.Init(-BIG_NUMBER, VW * VH);
        CachedDebugW = Grid.Width;
        CachedDebugH = Grid.Height;
    }

    // 惰性取顶点 Z：命中则缓存，未命中留 -BIG_NUMBER 等下帧重试
    auto GetVertexZ = [&](int32 VX, int32 VY) -> float
    {
        const int32 VI = VY * VW + VX;
        if (CachedDebugZ[VI] > -BIG_NUMBER) return CachedDebugZ[VI];

        const float WX = Grid.Origin.X + VX * Grid.CellSize;
        const float WY = Grid.Origin.Y + VY * Grid.CellSize;
        FCollisionObjectQueryParams ObjQuery;
        ObjQuery.AddObjectTypesToQuery(ECC_WorldStatic);
        ObjQuery.AddObjectTypesToQuery(ECC_GameTraceChannel1); // Terrain
        FHitResult Hit;
        if (World->LineTraceSingleByObjectType(Hit,
                FVector(WX, WY, 100000.f),
                FVector(WX, WY, -100000.f),
                ObjQuery))
        {
            CachedDebugZ[VI] = Hit.ImpactPoint.Z;
        }
        return CachedDebugZ[VI] > -BIG_NUMBER ? CachedDebugZ[VI] : 0.f;
    };

    TArray<FFlowFieldDebugLine> Lines;
    TArray<FFlowFieldDebugQuad> Quads;
    Lines.Reserve(Grid.Width * Grid.Height * 6);
    if (S->bDrawHeatmap) Quads.Reserve(Grid.Width * Grid.Height);

    if (bAnyDebug)
    {
        for (int32 Y = 0; Y < Grid.Height; ++Y)
        for (int32 X = 0; X < Grid.Width;  ++X)
        {
            const FFlowFieldCell& Cell = Grid.GetCell(X, Y);
            const float CX = Grid.Origin.X + X * Grid.CellSize + Half;
            const float CY = Grid.Origin.Y + Y * Grid.CellSize + Half;

            const float Dx = CX - CamPos.X, Dy = CY - CamPos.Y;
            if (Dx*Dx + Dy*Dy > MaxDrawDistSq) continue;

            const bool bBlocked = Cell.IsBlocked();

            // ── 四角地面 Z（每角独立，完全贴合地形起伏）────────────────
            const float ZTL = GetVertexZ(X,   Y  ) + ZOff;
            const float ZTR = GetVertexZ(X+1, Y  ) + ZOff;
            const float ZBR = GetVertexZ(X+1, Y+1) + ZOff;
            const float ZBL = GetVertexZ(X,   Y+1) + ZOff;
            const float ZC  = (ZTL + ZTR + ZBR + ZBL) * 0.25f; // 格心 Z（箭头用）

            const FVector TL(CX-Half, CY-Half, ZTL);
            const FVector TR(CX+Half, CY-Half, ZTR);
            const FVector BR(CX+Half, CY+Half, ZBR);
            const FVector BL(CX-Half, CY+Half, ZBL);
            const FVector Center(CX, CY, ZC);

            // ── 热力图：实心四边形 ────────────────────────────────────
            if (S->bDrawHeatmap && !bBlocked)
            {
                FLinearColor Base;
                if (Cell.IsReachable() && MaxInteg > 0.f)
                {
                    // 16 步量化 → SceneProxy 按颜色分组，DrawCall 数 ≤ 16
                    const float T = FMath::RoundToFloat(
                        FMath::Clamp(Cell.Integration / MaxInteg, 0.f, 1.f) * 15.f) / 15.f;
                    Base = FLinearColor::LerpUsingHSV(S->DebugHeatLow, S->DebugHeatHigh, T);
                }
                else
                {
                    Base = S->DebugColorNoFlow;
                }
                Quads.Add({ {TL, TR, BR, BL}, Base.ToFColor(false) });
            }

            // ── 网格轮廓 ──────────────────────────────────────────────
            if (S->bDrawGrid)
            {
                const FLinearColor& Col  = bBlocked ? S->DebugColorObstacle : S->DebugColorWalkable;
                const float        Thick = bBlocked ? 1.5f : 0.5f;
                Lines.Add({TL, TR, Col, Thick});
                Lines.Add({TR, BR, Col, Thick});
                Lines.Add({BR, BL, Col, Thick});
                Lines.Add({BL, TL, Col, Thick});
            }

            // ── 流场箭头 / 无流场圆圈 ────────────────────────────────
            if (S->bDrawFlow && !bBlocked)
            {
                if (!Cell.FlowDir.IsZero())
                {
                    // ── 有方向：画箭头 ──────────────────────────────────
                    FVector Dir = FVector(Cell.FlowDir.X, Cell.FlowDir.Y, 0.f).GetSafeNormal();
                    const float Len  = Grid.CellSize * S->ArrowScale;
                    const float TipX = CX + Dir.X * Len;
                    const float TipY = CY + Dir.Y * Len;

                    // 箭头终点 Z：取目标格子四个角的均值
                    float TipZ = ZC;
                    FIntPoint TC = Grid.WorldToCell(FVector(TipX, TipY, 0.f));
                    if (Grid.IsInBounds(TC))
                    {
                        TipZ = (GetVertexZ(TC.X,   TC.Y  ) + GetVertexZ(TC.X+1, TC.Y  ) +
                                GetVertexZ(TC.X,   TC.Y+1) + GetVertexZ(TC.X+1, TC.Y+1))
                               * 0.25f + ZOff;
                    }

                    const FVector Tip(TipX, TipY, TipZ);

                    // 翼片：沿地形法线方向旋转，贴合斜面
                    const FVector SlopeNormal = FVector(
                        (ZTL + ZBL - ZTR - ZBR) / (2.f * Grid.CellSize),
                        (ZTL + ZTR - ZBL - ZBR) / (2.f * Grid.CellSize),
                        1.f).GetSafeNormal();
                    Lines.Add({Center, Tip, S->DebugColorArrow, 1.5f});
                    Lines.Add({Tip, Tip + Dir.RotateAngleAxis( 145.f, SlopeNormal) * Len * 0.3f, S->DebugColorArrow, 1.f});
                    Lines.Add({Tip, Tip + Dir.RotateAngleAxis(-145.f, SlopeNormal) * Len * 0.3f, S->DebugColorArrow, 1.f});
                }
                else
                {
                    // ── 无方向：画小圆圈（贴合地形）──────────────────────
                    const float R    = Grid.CellSize * S->ArrowScale * 0.35f;
                    const int32 Segs = 8;
                    for (int32 i = 0; i < Segs; ++i)
                    {
                        const float A0 = (float)i       / Segs * 2.f * PI;
                        const float A1 = (float)(i + 1) / Segs * 2.f * PI;
                        // 每段端点在格心 Z 处（已由 GetVertexZ 贴地）
                        Lines.Add({
                            FVector(CX + FMath::Cos(A0) * R, CY + FMath::Sin(A0) * R, ZC),
                            FVector(CX + FMath::Cos(A1) * R, CY + FMath::Sin(A1) * R, ZC),
                            S->DebugColorNoFlow, 0.8f
                        });
                    }
                }
            }
        }
    }

    // ── 目标标记（菱形 + 竖线）────────────────────────────────────────
    if (bReady && !CurrentGoal.IsZero())
    {
        const FVector Above = CurrentGoal + FVector(0, 0, 50.f);
        const float   R     = Grid.CellSize * 0.35f;
        Lines.Add({Above + FVector( R, 0, 0), Above + FVector(0,  R, 0), S->DebugColorGoal, 2.f});
        Lines.Add({Above + FVector(0,  R, 0), Above + FVector(-R, 0, 0), S->DebugColorGoal, 2.f});
        Lines.Add({Above + FVector(-R, 0, 0), Above + FVector(0, -R, 0), S->DebugColorGoal, 2.f});
        Lines.Add({Above + FVector(0, -R, 0), Above + FVector( R, 0, 0), S->DebugColorGoal, 2.f});
        Lines.Add({CurrentGoal, Above, S->DebugColorGoal, 2.f});
    }

    DebugComp->Update(MoveTemp(Lines), MoveTemp(Quads));

}

#if WITH_EDITOR
void AFlowFieldActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    // 网格/障碍参数变化：重新生成 obstacle 布局
    static const TArray<FName> GridProps = {
        GET_MEMBER_NAME_CHECKED(AFlowFieldActor, CellSize),
    };
    const FName Changed = PropertyChangedEvent.GetPropertyName();
    if (GridProps.Contains(Changed))
    {
        CachedDebugZ.Reset(); CachedDebugW = 0;
        OnConstruction(GetActorTransform()); // 重建 obstacle 格子
        return; // OnConstruction 内部会设 bDebugDirty，Tick 会重画
    }

    // 调试开关变化：立即重建线段
    bDebugDirty = true;
    RefreshDebugNow();
}
#endif

#if WITH_EDITOR
void AFlowFieldActor::PostEditMove(bool bFinished)
{
    Super::PostEditMove(bFinished);
    if (bFinished && bReady)
        Generate(CurrentGoal);
}
#endif

TArray<FVector> AFlowFieldActor::FindPath(FVector Start, FVector Goal) const
{
    TArray<FVector> Result;
    if (!Grid.IsValid()) return Result;

    const FIntPoint StartCell = Grid.WorldToCell(Start);
    const FIntPoint GoalCell  = Grid.WorldToCell(Goal);

    TArray<FIntPoint> CellPath;
    if (!Grid.FindPathAStar(StartCell, GoalCell, CellPath)) return Result;

    Result.Reserve(CellPath.Num());
    for (const FIntPoint& P : CellPath)
        Result.Add(Grid.CellToWorld(P.X, P.Y));

    return Result;
}

void AFlowFieldActor::RegisterTarget(UFlowFieldTargetComponent* Comp)
{
    if (!Comp) return;
    RegisteredTargets.AddUnique(Comp);
}

void AFlowFieldActor::UnregisterTarget(UFlowFieldTargetComponent* Comp)
{
    RegisteredTargets.RemoveSwap(Comp);
}

void AFlowFieldActor::UpdateTarget()
{
    TrackedTargets.Reset();

    for (int32 i = RegisteredTargets.Num() - 1; i >= 0; --i)
    {
        UFlowFieldTargetComponent* Comp = RegisteredTargets[i].Get();
        if (!Comp || !Comp->GetOwner())
        {
            RegisteredTargets.RemoveAtSwap(i); // 清理已销毁的目标
            continue;
        }
        FFlowFieldTrackedTarget T;
        T.Position     = Comp->GetOwner()->GetActorLocation();
        T.PushRadius   = Comp->PushRadius;
        T.PushStrength = Comp->PushStrength;
        T.OwnerComp    = Comp;
        TrackedTargets.Add(T);
    }

    CrowdCounts.SetNumZeroed(TrackedTargets.Num());
}

void AFlowFieldActor::RegisterObstacle(UFlowFieldObstacleComponent* Comp)
{
    if (!Comp) return;
    RegisteredObstacles.AddUnique(Comp);
}

void AFlowFieldActor::UnregisterObstacle(UFlowFieldObstacleComponent* Comp)
{
    if (!Comp) return;
    RegisteredObstacles.RemoveSwap(Comp);
    // 障碍销毁时只重建流场（地表数据保留），触发精确格子更新
    if (bReady) RebuildFlowAfterObstacleChange();
}

// ── VAT 渲染 ──────────────────────────────────────────────────────

UInstancedStaticMeshComponent* AFlowFieldActor::GetOrCreateVATRenderer(
    UFlowFieldVATDataAsset* DataAsset)
{
    if (!DataAsset || !DataAsset->Mesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VAT] GetOrCreateVATRenderer failed: DataAsset=%s Mesh=%s"),
            DataAsset ? *DataAsset->GetName() : TEXT("NULL"),
            (DataAsset && !DataAsset->Mesh) ? TEXT("NULL") : TEXT("OK"));
        return nullptr;
    }

    if (TObjectPtr<UInstancedStaticMeshComponent>* Found = VATRenderers.Find(DataAsset))
    {
        return Found->Get();
    }

    // 使用 ISM（非 HISM）：每帧更新 transform 不触发 BVH 重建
    UInstancedStaticMeshComponent* ISM =
        NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
    ISM->SetStaticMesh(DataAsset->Mesh);
    if (DataAsset->Material)
    {
        ISM->SetMaterial(0, DataAsset->Material);
    }
    ISM->NumCustomDataFloats = DataAsset->NumCustomDataFloats;
    // 动态实例必须 Movable，否则运行时更新 transform 无效
    ISM->SetMobility(EComponentMobility::Movable);
    ISM->SetHiddenInGame(false);
    ISM->SetVisibility(true);
    // RF_Transient：让 UpdateInstanceTransform 内部的 Modify() 在 PIE 中变成 no-op，
    //              否则每帧 N 次写 undo history，是最大性能杀手
    // SetCanEverAffectNavigation(false)：禁止导航更新，否则每实体每帧触发 PartialNavigationUpdates
    ISM->SetCanEverAffectNavigation(false);
    ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ISM->SetupAttachment(GetRootComponent());
    ISM->RegisterComponent();

    VATRenderers.Add(DataAsset, ISM);
    return ISM;
}

// ── 扫描烘焙 ──────────────────────────────────────────────────────

void AFlowFieldActor::BakeObstaclesFromScan(int32 W, int32 H, const TArray<FIntPoint>& ObstacleCells)
{
    BakedObstacleMask.Init(0, W * H);
    for (const FIntPoint& P : ObstacleCells)
    {
        if (P.X >= 0 && P.X < W && P.Y >= 0 && P.Y < H)
            BakedObstacleMask[P.Y * W + P.X] = 1;
    }

    // 立即写入当前运行时 Grid（不等下次 Generate）
    for (const FIntPoint& P : ObstacleCells)
    {
        if (FFlowFieldCell* C = Grid.TryGetCell(P.X, P.Y))
            C->Cost = 255;
    }

    Grid.ComputeBorderCells();
    bDebugDirty = true;

    // 如果流场已生成，障碍变化后重算积分场+流向（否则下次 Generate 时自动包含）
    RebuildFlowAfterObstacleChange();
}

void AFlowFieldActor::ClearBakedObstacles()
{
    BakedObstacleMask.Empty();
    // 重建编辑器网格（清除烘焙格子的 Cost=255）
    RefreshEditorObstacleLayout();
}
