#pragma once
#include "CoreMinimal.h"
#include "MassEntityHandle.h"
// 简单的 2D 空间哈希，专门为 FlowField 做范围查询
// 每帧由 FlowFieldSpatialHashProcessor 更新
// 查询时间复杂度 O(结果数量) 而不是 O(总实体数)
struct FFlowFieldSpatialHash
{
    float CellSize = 200.f;

    struct FEntry
    {
        FMassEntityHandle Handle;
        FVector           Position;
    };

    TMap<uint64, TArray<FEntry>> Grid;

    void Clear() { Grid.Empty(); }

    void Insert(FMassEntityHandle Handle, FVector Pos)
    {
        uint64 Key = MakeKey(Pos);
        Grid.FindOrAdd(Key).Add({Handle, Pos});
    }

    // 查询半径内的所有实体
    void Query(FVector Center, float Radius, TArray<FEntry>& OutResults) const
    {
        int32 MinX = FMath::FloorToInt((Center.X - Radius) / CellSize);
        int32 MaxX = FMath::FloorToInt((Center.X + Radius) / CellSize);
        int32 MinY = FMath::FloorToInt((Center.Y - Radius) / CellSize);
        int32 MaxY = FMath::FloorToInt((Center.Y + Radius) / CellSize);
        float RadSq = Radius * Radius;

        for (int32 X = MinX; X <= MaxX; X++)
        for (int32 Y = MinY; Y <= MaxY; Y++)
        {
            uint64 Key = MakeKeyXY(X, Y);
            const TArray<FEntry>* Entries = Grid.Find(Key);
            if (!Entries) continue;
            for (const FEntry& E : *Entries)
            {
                if (FVector::DistSquared2D(E.Position, Center) <= RadSq)
                    OutResults.Add(E);
            }
        }
    }

    // 查询最近的单个实体
    bool QueryNearest(FVector Center, float MaxRadius, FEntry& OutResult) const
    {
        TArray<FEntry> Results;
        Query(Center, MaxRadius, Results);
        if (Results.IsEmpty()) return false;

        float BestDist = FLT_MAX;
        for (const FEntry& E : Results)
        {
            float D = FVector::DistSquared2D(E.Position, Center);
            if (D < BestDist) { BestDist = D; OutResult = E; }
        }
        return true;
    }

private:
    FORCEINLINE uint64 MakeKey(FVector Pos) const
    {
        return MakeKeyXY(FMath::FloorToInt(Pos.X / CellSize),
                         FMath::FloorToInt(Pos.Y / CellSize));
    }
    FORCEINLINE static uint64 MakeKeyXY(int32 X, int32 Y)
    {
        return ((uint64)(uint32)X << 32) | (uint32)Y;
    }
};