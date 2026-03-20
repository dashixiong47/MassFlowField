#include "MassAI/FlowFieldBoidsProcessor.h"
#include "MassAI/FlowFieldAgentFragment.h"
#include "MassExecutionContext.h"
#include "MassEntityManager.h"
#include "MassCommonFragments.h"
#include "FlowFieldSubsystem.h"
#include "FlowFieldActor.h"
#include "MassCommonTypes.h"

// ── 局部空间哈希（仅供 BoidsProcessor 内部使用）───────────────────
// 与 FFlowFieldSpatialHash 不同：这个以 TArray 下标为 Key，
// 用于同一帧内快速查询邻居索引，不需要 FMassEntityHandle
struct FBoidsLocalGrid
{
    float CellSize = 150.f;
    int32 BucketNum = 0;
    TArray<int32> Buckets;
    TArray<int32> Next;

    // 根据位置数组构建哈希表，Count 为实体总数
    void Build(const TArray<FVector>& Positions, float InCellSize, int32 Count)
    {
        CellSize  = InCellSize;
        BucketNum = FMath::RoundUpToPowerOfTwo(Count * 2);
        Buckets.Init(-1, BucketNum);
        Next.SetNumUninitialized(Count);
        for (int32 i = 0; i < Count; i++)
        {
            int32 H    = HashPos(Positions[i]) & (BucketNum - 1);
            Next[i]    = Buckets[H];
            Buckets[H] = i;
        }
    }

    // 查询 Pos 附近 Radius 内的所有实体下标（排除自身 SelfIdx）
    void Query(const TArray<FVector>& Positions, FVector Pos, float Radius,
               int32 SelfIdx, TArray<int32>& Out) const
    {
        int32 CX    = FMath::FloorToInt(Pos.X / CellSize);
        int32 CY    = FMath::FloorToInt(Pos.Y / CellSize);
        int32 Range = FMath::CeilToInt(Radius / CellSize);
        float RadSq = Radius * Radius;
        for (int32 DX = -Range; DX <= Range; DX++)
            for (int32 DY = -Range; DY <= Range; DY++)
            {
                int32 H = HashXY(CX + DX, CY + DY) & (BucketNum - 1);
                for (int32 Idx = Buckets[H]; Idx != -1; Idx = Next[Idx])
                {
                    if (Idx == SelfIdx) continue;
                    FVector D = Pos - Positions[Idx];
                    D.Z = 0.f;
                    if (D.SizeSquared2D() < RadSq)
                        Out.Add(Idx);
                }
            }
    }

private:
    FORCEINLINE int32 HashPos(FVector P) const
    {
        return HashXY(FMath::FloorToInt(P.X / CellSize), FMath::FloorToInt(P.Y / CellSize));
    }

    FORCEINLINE static int32 HashXY(int32 X, int32 Y)
    {
        return (X * 73856093) ^ (Y * 19349663);
    }
};

UFlowFieldBoidsProcessor::UFlowFieldBoidsProcessor()
{
    
    ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Movement;
    // 必须在 MovementProcessor 之前执行，分离力写入后再由 Movement 读取
    ExecutionOrder.ExecuteBefore.Add(TEXT("FlowFieldMovementProcessor"));
    ExecutionFlags = (int32)(EProcessorExecutionFlags::Server | EProcessorExecutionFlags::Client);
    bAutoRegisterWithProcessingPhases = true;
}

void UFlowFieldBoidsProcessor::ConfigureQueries(
    const TSharedRef<FMassEntityManager>& EntityManager)
{
    EntityQuery.Initialize(EntityManager);
    EntityQuery.AddTagRequirement<FFlowFieldAgentTag>(EMassFragmentPresence::All);
    EntityQuery.AddTagRequirement<FFlowFieldMovingTag>(EMassFragmentPresence::All);
    EntityQuery.AddRequirement<FFlowFieldBoidsFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddRequirement<FFlowFieldAgentFragment>(EMassFragmentAccess::ReadOnly);
    EntityQuery.RegisterWithProcessor(*this);
    RegisterQuery(EntityQuery);
}

void UFlowFieldBoidsProcessor::Execute(
    FMassEntityManager& EntityManager,
    FMassExecutionContext& Context)
{
    UWorld* World = GetWorld();
    if (!World) return;

    UFlowFieldSubsystem* FlowSub  = World->GetSubsystem<UFlowFieldSubsystem>();
    AFlowFieldActor*     FlowActor = (FlowSub && FlowSub->IsReady()) ? FlowSub->GetActor() : nullptr;

    // ── 第一步：收集所有实体的位置和半径 ────────────────────────
    TArray<FVector> AllPositions;
    TArray<float>   AllRadii;
    AllPositions.Reserve(2000);
    AllRadii.Reserve(2000);

    EntityQuery.ForEachEntityChunk(Context,
        [&](FMassExecutionContext& ChunkContext)
        {
            auto Transforms = ChunkContext.GetFragmentView<FTransformFragment>();
            auto Boids      = ChunkContext.GetFragmentView<FFlowFieldBoidsFragment>();
            const int32 Num = ChunkContext.GetNumEntities();
            for (int32 i = 0; i < Num; i++)
            {
                AllPositions.Add(Transforms[i].GetTransform().GetLocation());
                AllRadii.Add(Boids[i].SeparationRadius);
            }
        });

    if (AllPositions.IsEmpty()) return;
    const int32 Total = AllPositions.Num();

    // ── 第二步：用最大感知半径建空间哈希 ────────────────────────
    float MaxRadius = 0.f;
    for (float R : AllRadii) MaxRadius = FMath::Max(MaxRadius, R);

    FBoidsLocalGrid HashGrid;
    HashGrid.Build(AllPositions, MaxRadius > 0.f ? MaxRadius : 150.f, Total);

    // ── 第三步：计算每个实体需要被推开的位移 ────────────────────
    TArray<FVector> PushVectors;
    PushVectors.SetNumZeroed(Total);

    for (int32 i = 0; i < Total; i++)
    {
        TArray<int32> Neighbors;
        Neighbors.Reserve(16);
        HashGrid.Query(AllPositions, AllPositions[i], AllRadii[i], i, Neighbors);

        for (int32 NIdx : Neighbors)
        {
            FVector Diff = AllPositions[i] - AllPositions[NIdx];
            Diff.Z = 0.f;
            float Dist = Diff.Size2D();
            if (Dist < KINDA_SMALL_NUMBER) continue;

            float MinDist = AllRadii[i] + AllRadii[NIdx];
            if (Dist >= MinDist) continue;

            // 重叠量平摊到双方，各推一半
            float Overlap = (MinDist - Dist) * 0.5f;
            PushVectors[i] += (Diff / Dist) * Overlap;
        }
    }

    // ── 第四步：应用分离力 ───────────────────────────────────────
    // SeparationForce 供 MovementProcessor 叠加到流场方向
    // 同时直接修正位置（硬推开），确保视觉上不重叠
    int32 GlobalIdx = 0;
    EntityQuery.ForEachEntityChunk(Context,
        [&](FMassExecutionContext& ChunkContext)
        {
            auto Boids      = ChunkContext.GetMutableFragmentView<FFlowFieldBoidsFragment>();
            auto Transforms = ChunkContext.GetMutableFragmentView<FTransformFragment>();
            auto Agents     = ChunkContext.GetFragmentView<FFlowFieldAgentFragment>();
            const int32 Num = ChunkContext.GetNumEntities();

            for (int32 i = 0; i < Num; i++, GlobalIdx++)
            {
                Boids[i].SeparationForce = PushVectors[GlobalIdx];

                if (PushVectors[GlobalIdx].IsNearlyZero()) continue;

                // 被击退中的实体不受 Boids 推开影响，保证击退效果完整表现
                if (Agents[i].bIsKnockedBack) continue;

                FTransform& T      = Transforms[i].GetMutableTransform();
                FVector     NewPos = T.GetLocation() + PushVectors[GlobalIdx];
                NewPos.Z           = T.GetLocation().Z; // 保持当前高度，Z 由 MovementProcessor 管理

                // 推开后的位置不能进障碍
                if (!FlowActor || FlowActor->GetIntegration(NewPos) >= 0.f)
                    T.SetLocation(NewPos);
            }
        });
}