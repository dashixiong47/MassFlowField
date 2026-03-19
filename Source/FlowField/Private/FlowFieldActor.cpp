#include "FlowFieldActor.h"
#include "FlowFieldSubsystem.h"
#include "FlowFieldObstacleActor.h"
#include "DrawDebugHelpers.h"
#include "Engine/LevelBounds.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#endif

AFlowFieldActor::AFlowFieldActor()
{
    PrimaryActorTick.bCanEverTick = false;
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

    bReady = false;
    Grid = MoveTemp(NewGrid);
    bReady = true;
    Grid.ResetOccupancy();

    ClearDebug();
    DrawDebug();

    UE_LOG(LogTemp, Log, TEXT("[FlowField] GenerateInternal complete ✓  NetMode=%d"),
        (int32)GetWorld()->GetNetMode());
}

void AFlowFieldActor::ApplyObstaclesToGrid(FFlowFieldGrid& TargetGrid)
{
    if (ObstacleTag.IsNone()) return;

    int32 ObstacleCount = 0;
    int32 CellsMarked   = 0;
    float Half          = CellSize * 0.5f;

    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor->ActorHasTag(ObstacleTag)) continue;

        ObstacleCount++;

        FVector Origin, Extent;
        Actor->GetActorBounds(true, Origin, Extent);

        float ExpandX = Extent.X + ObstacleRadius;
        float ExpandY = Extent.Y + ObstacleRadius;

        FIntPoint CellMin = TargetGrid.WorldToCell(FVector(Origin.X - ExpandX, Origin.Y - ExpandY, 0.f));
        FIntPoint CellMax = TargetGrid.WorldToCell(FVector(Origin.X + ExpandX, Origin.Y + ExpandY, 0.f));

        CellMin.X = FMath::Clamp(CellMin.X, 0, TargetGrid.Width  - 1);
        CellMin.Y = FMath::Clamp(CellMin.Y, 0, TargetGrid.Height - 1);
        CellMax.X = FMath::Clamp(CellMax.X, 0, TargetGrid.Width  - 1);
        CellMax.Y = FMath::Clamp(CellMax.Y, 0, TargetGrid.Height - 1);

        for (int32 Y = CellMin.Y; Y <= CellMax.Y; ++Y)
        {
            for (int32 X = CellMin.X; X <= CellMax.X; ++X)
            {
                FVector CellCenter(
                    TargetGrid.Origin.X + X * CellSize + Half,
                    TargetGrid.Origin.Y + Y * CellSize + Half,
                    Origin.Z
                );

                float OverlapX = FMath::Max(0.f,
                    FMath::Min(CellCenter.X + Half, Origin.X + ExpandX) -
                    FMath::Max(CellCenter.X - Half, Origin.X - ExpandX));
                float OverlapY = FMath::Max(0.f,
                    FMath::Min(CellCenter.Y + Half, Origin.Y + ExpandY) -
                    FMath::Max(CellCenter.Y - Half, Origin.Y - ExpandY));

                float OverlapRatio = (OverlapX * OverlapY) / (CellSize * CellSize);
                if (OverlapRatio < ObstacleOverlapThreshold) continue;

                FFlowFieldCell& Cell = TargetGrid.GetCell(X, Y);
                if (!Cell.IsBlocked())
                {
                    Cell.Cost = 255;
                    CellsMarked++;
                }
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[FlowField] ApplyObstacles: %d obstacles, %d cells marked"),
        ObstacleCount, CellsMarked);
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
    UWorld* World = GetWorld();
    if (!World) return;
    FlushPersistentDebugLines(World);
    FlushDebugStrings(World);
}

void AFlowFieldActor::DrawDebug() const
{
    if (!Grid.IsValid()) return;

    UWorld* World = GetWorld();
    if (!World) return;

    float MaxInteg = Grid.MaxIntegration();
    float Half     = Grid.CellSize * 0.5f;

    FVector CamPos = FVector::ZeroVector;
    bool bHasCam   = false;

#if WITH_EDITOR
    if (GEditor)
    {
        for (FEditorViewportClient* VC : GEditor->GetAllViewportClients())
        {
            if (VC && VC->IsPerspective())
            {
                CamPos  = VC->GetViewLocation();
                bHasCam = true;
                break;
            }
        }
    }
#endif

    if (!bHasCam)
    {
        if (APlayerController* PC = World->GetFirstPlayerController())
        {
            if (PC->PlayerCameraManager)
            {
                CamPos  = PC->PlayerCameraManager->GetCameraLocation();
                bHasCam = true;
            }
        }
    }

    const float MaxDrawDistSq = DebugDrawDistance * DebugDrawDistance;

    for (int32 Y = 0; Y < Grid.Height; ++Y)
    {
        for (int32 X = 0; X < Grid.Width; ++X)
        {
            const FFlowFieldCell& Cell = Grid.GetCell(X, Y);

            FVector Center(
                Grid.Origin.X + X * Grid.CellSize + Half,
                Grid.Origin.Y + Y * Grid.CellSize + Half,
                Cell.SurfaceZ + 8.f
            );

            if (bHasCam && FVector::DistSquared2D(Center, CamPos) > MaxDrawDistSq) continue;

            if (bDrawGrid)
            {
                FColor Col   = Cell.IsBlocked() ? FColor(180, 0, 0) : FColor(0, 120, 0);
                float  Thick = Cell.IsBlocked() ? 3.f : 1.f;
                DrawDebugCircle(World, Center, Half, 8, Col, true, -1.f, 0, Thick,
                    FVector(1,0,0), FVector(0,1,0), false);
            }

            if (bDrawHeatmap && Cell.IsReachable())
            {
                float T = (MaxInteg > 0.f) ? (Cell.Integration / MaxInteg) : 0.f;
                FLinearColor Heat = FLinearColor::LerpUsingHSV(
                    FLinearColor(0.33f, 1.f, 0.8f), FLinearColor(0.f, 1.f, 0.8f), T);
                DrawDebugCircle(World, Center, Half * 0.5f, 6, Heat.ToFColor(true),
                    true, -1.f, 0, 1.f, FVector(1,0,0), FVector(0,1,0), false);
            }

            if (bDrawFlow && !Cell.FlowDir.IsZero() && !Cell.IsBlocked())
            {
                FVector Dir = FVector::VectorPlaneProject(
                    FVector(Cell.FlowDir.X, Cell.FlowDir.Y, 0.f), Cell.Normal).GetSafeNormal();
                float ArrowLen = Grid.CellSize * ArrowScale;
                DrawDebugDirectionalArrow(World, Center, Center + Dir * ArrowLen,
                    ArrowLen * 0.35f, FColor::White, true, -1.f, 0, 1.5f);
            }

            if (bDrawScores)
            {
                FString Line1 = Cell.IsBlocked() ? TEXT("X") :
                    (Cell.IsReachable() ? FString::Printf(TEXT("%.0f"), Cell.Integration) : TEXT("?"));
                DrawDebugString(World, Center + FVector(0,0,15.f), Line1, nullptr,
                    Cell.IsBlocked() ? FColor::Red : FColor::Yellow, -1.f, true, 0.7f);
            }
        }
    }

    if (bReady)
    {
        DrawDebugSphere(World, CurrentGoal + FVector(0,0,60),
            Grid.CellSize * 0.3f, 12, FColor::Red, true, -1.f, 0, 4.f);
        DrawDebugString(World, CurrentGoal + FVector(0,0,120),
            TEXT("GOAL"), nullptr, FColor::Red, -1.f, true, 1.2f);
    }

    if (bDrawGrid && !ObstacleTag.IsNone())
    {
        for (TActorIterator<AActor> It(GetWorld()); It; ++It)
        {
            AActor* Actor = *It;
            if (!Actor->ActorHasTag(ObstacleTag)) continue;

            FVector Origin, Extent;
            Actor->GetActorBounds(true, Origin, Extent);
            float ExpandX = Extent.X + ObstacleRadius;
            float ExpandY = Extent.Y + ObstacleRadius;
            FVector BoxCenter(Origin.X, Origin.Y, Origin.Z + 8.f);

            DrawDebugBox(World, BoxCenter, FVector(ExpandX, ExpandY, 5.f),
                FColor(255,100,0), true, -1.f, 0, 2.f);

            float CrossSize = FMath::Min(FMath::Min(ExpandX, ExpandY) * 0.3f, 60.f);
            DrawDebugDirectionalArrow(World,
                BoxCenter - FVector(CrossSize,0,0), BoxCenter + FVector(CrossSize,0,0),
                CrossSize * 0.4f, FColor(255,100,0), true, -1.f, 0, 2.f);
            DrawDebugDirectionalArrow(World,
                BoxCenter - FVector(0,CrossSize,0), BoxCenter + FVector(0,CrossSize,0),
                CrossSize * 0.4f, FColor(255,100,0), true, -1.f, 0, 2.f);

            if (bDrawScores)
                DrawDebugString(World, BoxCenter + FVector(0,0,30),
                    FString::Printf(TEXT("%.0fx%.0f"), ExpandX*2.f, ExpandY*2.f),
                    nullptr, FColor(255,160,50), -1.f, true, 0.8f);
        }
    }
}

#if WITH_EDITOR
void AFlowFieldActor::PostEditMove(bool bFinished)
{
    Super::PostEditMove(bFinished);
    if (bFinished && bReady)
        Generate(CurrentGoal);
}
#endif