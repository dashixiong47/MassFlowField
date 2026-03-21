#pragma once
#include "CoreMinimal.h"
#include "Algo/Reverse.h"
#include "FlowFieldTypes.h"

struct FFlowFieldGrid
{
public:
    FFlowFieldGrid() = default;

    // 初始化网格，分配所有数组
    void Init(FVector InOrigin, int32 InWidth, int32 InHeight, float InCellSize)
    {
        Origin   = InOrigin;
        Width    = InWidth;
        Height   = InHeight;
        CellSize = InCellSize;
        Cells.Init(FFlowFieldCell(), Width * Height);
        HotFlowDirs.Init(FVector2D::ZeroVector, Width * Height);
        HotIntegrations.Init(FLT_MAX, Width * Height);
        OccupancyMap.Init(0, Width * Height);
        BorderCells.Init(0, Width * Height);
        BorderFaceDirs.Init(FVector2D::ZeroVector, Width * Height);
    }

    // 清空所有数据
    void Reset()
    {
        Cells.Empty();
        HotFlowDirs.Empty();
        HotIntegrations.Empty();
        OccupancyMap.Empty();
        BorderCells.Empty();
        BorderFaceDirs.Empty();
        Width = Height = 0;
    }

    // 预计算边界格：walkable 且8邻域内至少有一个 blocked 格
    // 在 BuildFlowField 之后调用一次；障碍变化时手动重触发
    // 预计算边界格及其面向方向（障碍邻格的平均方向），GenerateInternal 调用一次
    void ComputeBorderCells()
    {
        BorderCells.Init(0, Width * Height);
        BorderFaceDirs.Init(FVector2D::ZeroVector, Width * Height);

        for (int32 Y = 0; Y < Height; Y++)
        {
            for (int32 X = 0; X < Width; X++)
            {
                if (GetCell(X, Y).IsBlocked()) continue;

                FVector2D FaceSum = FVector2D::ZeroVector;
                for (int32 DY = -1; DY <= 1; DY++)
                {
                    for (int32 DX = -1; DX <= 1; DX++)
                    {
                        if (DX == 0 && DY == 0) continue;
                        if (!IsInBounds(X+DX, Y+DY)) continue;
                        if (GetCell(X+DX, Y+DY).IsBlocked())
                            FaceSum += FVector2D((float)DX, (float)DY).GetSafeNormal();
                    }
                }

                if (!FaceSum.IsNearlyZero())
                {
                    BorderCells[Y * Width + X] = 1;
                    BorderFaceDirs[Y * Width + X] = FaceSum.GetSafeNormal();
                }
            }
        }
    }

    bool IsBorderCell(FIntPoint P) const
    {
        if (!IsInBounds(P)) return false;
        return BorderCells[P.Y * Width + P.X] != 0;
    }

    // 获取边界格预计算的面向方向（指向相邻障碍格的平均方向）
    FVector2D GetBorderFaceDir(FIntPoint P) const
    {
        if (!IsInBounds(P) || BorderCells[P.Y * Width + P.X] == 0) return FVector2D::ZeroVector;
        return BorderFaceDirs[P.Y * Width + P.X];
    }

    // 网格是否已初始化且有效
    bool IsValid() const { return Cells.Num() > 0 && Width > 0 && Height > 0; }

    // ── 坐标转换 ──────────────────────────────────────────────────

    // 世界坐标 → 格子坐标（不做范围检查）
    FIntPoint WorldToCell(FVector WorldPos) const
    {
        return FIntPoint(
            FMath::FloorToInt((WorldPos.X - Origin.X) / CellSize),
            FMath::FloorToInt((WorldPos.Y - Origin.Y) / CellSize)
        );
    }

    // 格子坐标 → 格子中心世界坐标（含地面 Z）
    FVector CellToWorld(int32 X, int32 Y) const
    {
        const FFlowFieldCell& C = GetCell(X, Y);
        return FVector(
            Origin.X + X * CellSize + CellSize * 0.5f,
            Origin.Y + Y * CellSize + CellSize * 0.5f,
            C.SurfaceZ
        );
    }

    bool IsInBounds(int32 X, int32 Y) const
    {
        return X >= 0 && Y >= 0 && X < Width && Y < Height;
    }

    bool IsInBounds(FIntPoint P) const { return IsInBounds(P.X, P.Y); }

    // 世界坐标是否在网格范围内
    bool ContainsWorldPos(FVector WorldPos) const
    {
        return IsInBounds(WorldToCell(WorldPos));
    }

    // ── 格子访问 ──────────────────────────────────────────────────

    FFlowFieldCell& GetCell(int32 X, int32 Y)             { return Cells[Y * Width + X]; }
    const FFlowFieldCell& GetCell(int32 X, int32 Y) const { return Cells[Y * Width + X]; }

    FFlowFieldCell* TryGetCell(int32 X, int32 Y)
    {
        return IsInBounds(X, Y) ? &Cells[Y * Width + X] : nullptr;
    }

    const FFlowFieldCell* TryGetCell(int32 X, int32 Y) const
    {
        return IsInBounds(X, Y) ? &Cells[Y * Width + X] : nullptr;
    }

    // ── 热路径查询（运行时高频调用）────────────────────────────────

    // 获取指定世界坐标的流场方向（非插值版，直接读格子）
    FVector GetFlowDirection(FVector WorldPos) const
    {
        FIntPoint P = WorldToCell(WorldPos);
        const FFlowFieldCell* C = TryGetCell(P.X, P.Y);
        if (!C || C->IsBlocked()) return FVector::ZeroVector;
        if (!C->FlowDir.IsZero())
            return FVector(C->FlowDir.X, C->FlowDir.Y, 0.f);
        return FVector::ZeroVector;
    }

    // 获取指定世界坐标的流场方向（双线性插值版，移动更平滑）
    FVector GetFlowDirectionInterpolated(FVector WorldPos) const
    {
        if (!IsValid()) return FVector::ZeroVector;

        float LocalX = (WorldPos.X - Origin.X) / CellSize;
        float LocalY = (WorldPos.Y - Origin.Y) / CellSize;

        int32 X = FMath::Clamp(FMath::FloorToInt(LocalX), 0, Width  - 2);
        int32 Y = FMath::Clamp(FMath::FloorToInt(LocalY), 0, Height - 2);

        float FracX = FMath::Clamp(LocalX - X, 0.f, 1.f);
        float FracY = FMath::Clamp(LocalY - Y, 0.f, 1.f);

        auto GetHotDir = [&](int32 CX, int32 CY) -> FVector2D
        {
            const FFlowFieldCell* C = TryGetCell(CX, CY);
            if (!C || C->IsBlocked() || !C->IsReachable()) return FVector2D::ZeroVector;

            const FVector2D& Dir = HotFlowDirs[CY * Width + CX];
            if (!Dir.IsNearlyZero()) return Dir;

            if (C->IsGoal())
            {
                // 目标格子自身 Integration=0，FlowDir 为零
                // 找周围 Integration 最大的邻居，方向反向指向它
                // 这样插值时目标格子附近的实体不会因为方向为零而停止
                static const FIntPoint Dirs4[] = {{1,0},{-1,0},{0,1},{0,-1}};
                float     BestInteg = 0.f; // 从 0 开始，只取比目标大的邻居
                FVector2D BestDir   = FVector2D::ZeroVector;
                for (const FIntPoint& D : Dirs4)
                {
                    if (!IsInBounds(CX + D.X, CY + D.Y)) continue;
                    const FFlowFieldCell* N = TryGetCell(CX + D.X, CY + D.Y);
                    if (!N || N->IsBlocked() || !N->IsReachable()) continue;
                    float NInteg = HotIntegrations[(CY + D.Y) * Width + (CX + D.X)];
                    // 修复：原来用 >，找的是最大邻居但初始值也是 FLT_MAX 导致永远不触发
                    // 现在：找 Integration 最大（最远）的邻居，让目标格子附近仍有引导方向
                    if (NInteg > BestInteg)
                    {
                        BestInteg = NInteg;
                        BestDir   = FVector2D((float)D.X, (float)D.Y); // 朝向该邻居（来的方向）
                    }
                }
                return BestDir;
            }
            return FVector2D::ZeroVector;
        };

        // 双线性插值：障碍格方向为零，不参与加权，避免在墙角被拉偏
        struct FSample { FVector2D Dir; float W; };
        FSample Samples[4] = {
            { GetHotDir(X,     Y    ), (1.f - FracX) * (1.f - FracY) },
            { GetHotDir(X + 1, Y    ), FracX         * (1.f - FracY) },
            { GetHotDir(X,     Y + 1), (1.f - FracX) * FracY         },
            { GetHotDir(X + 1, Y + 1), FracX         * FracY         },
        };

        FVector2D Result = FVector2D::ZeroVector;
        float     TotalW = 0.f;
        for (const FSample& S : Samples)
        {
            if (S.Dir.IsNearlyZero()) continue; // 障碍或不可达，跳过
            Result += S.Dir * S.W;
            TotalW += S.W;
        }

        if (TotalW < KINDA_SMALL_NUMBER) return FVector::ZeroVector;
        Result /= TotalW; // 重新归一化权重，消除障碍格的偏移影响
        if (Result.IsNearlyZero()) return FVector::ZeroVector;
        Result.Normalize();
        return FVector(Result.X, Result.Y, 0.f);
    }

    // 获取格子的 Integration 值；< 0 表示障碍或越界，FLT_MAX 表示不可达
    FORCEINLINE float GetIntegrationFast(FVector WorldPos) const
    {
        FIntPoint P = WorldToCell(WorldPos);
        if (!IsInBounds(P)) return -1.f;
        const FFlowFieldCell* C = TryGetCell(P.X, P.Y);
        if (!C) return -1.f;
        float V = HotIntegrations[P.Y * Width + P.X];
        return (V >= FLT_MAX) ? -1.f : V;
    }

    // ── 格子占位计数（Continuum Crowds 密度统计）────────────────────

    bool OccupyCell(int32 X, int32 Y, int32 MaxPerCell)
    {
        if (!IsInBounds(X, Y)) return false;
        if (GetCell(X, Y).IsBlocked()) return false;
        int32 Idx = Y * Width + X;
        if (OccupancyMap[Idx] >= MaxPerCell) return false;
        OccupancyMap[Idx]++;
        return true;
    }

    void ReleaseCell(int32 X, int32 Y)
    {
        if (!IsInBounds(X, Y)) return;
        int32 Idx = Y * Width + X;
        OccupancyMap[Idx] = FMath::Max(0, OccupancyMap[Idx] - 1);
    }

    bool IsCellFull(int32 X, int32 Y, int32 MaxPerCell) const
    {
        if (!IsInBounds(X, Y)) return true;
        if (GetCell(X, Y).IsBlocked()) return true;
        return OccupancyMap[Y * Width + X] >= MaxPerCell;
    }

    int32 GetOccupancy(int32 X, int32 Y) const
    {
        if (!IsInBounds(X, Y)) return 0;
        return OccupancyMap[Y * Width + X];
    }

    void ResetOccupancy()
    {
        for (int32& V : OccupancyMap) V = 0;
    }

    // ── 流场算法 ──────────────────────────────────────────────────

    // 单目标版本（保留兼容接口，内部调用多目标版本）
    void BuildIntegrationField(FIntPoint Goal)
    {
        TArray<FIntPoint> Goals;
        Goals.Add(Goal);
        BuildIntegrationFieldMulti(Goals);
    }

    // 多目标版本：所有目标格子 Integration=0
    // 用于目标在障碍内时，周围一圈可达格子都作为目标，让实体自然散开包围
    void BuildIntegrationFieldMulti(const TArray<FIntPoint>& Goals)
    {
        for (FFlowFieldCell& C : Cells)
            C.Integration = FLT_MAX;

        TArray<bool> InQueue;
        InQueue.Init(false, Cells.Num());

        TQueue<FIntPoint> Open;

        for (const FIntPoint& Goal : Goals)
        {
            if (!IsInBounds(Goal)) continue;
            FFlowFieldCell& GoalCell = GetCell(Goal.X, Goal.Y);
            if (GoalCell.IsBlocked()) continue;

            GoalCell.Integration = 0.f;
            if (!InQueue[Goal.Y * Width + Goal.X])
            {
                InQueue[Goal.Y * Width + Goal.X] = true;
                Open.Enqueue(Goal);
            }
        }

        static const FIntPoint Dirs8[] = {
            { 1, 0}, {-1, 0}, { 0, 1}, { 0,-1},
            { 1, 1}, { 1,-1}, {-1, 1}, {-1,-1}
        };

        while (!Open.IsEmpty())
        {
            FIntPoint Cur;
            Open.Dequeue(Cur);
            InQueue[Cur.Y * Width + Cur.X] = false;

            float CurCost = GetCell(Cur.X, Cur.Y).Integration;

            for (const FIntPoint& D : Dirs8)
            {
                int32 NX = Cur.X + D.X;
                int32 NY = Cur.Y + D.Y;
                if (!IsInBounds(NX, NY)) continue;

                FFlowFieldCell& N = GetCell(NX, NY);
                if (N.IsBlocked()) continue;

                if (D.X != 0 && D.Y != 0)
                {
                    const FFlowFieldCell* NX0 = TryGetCell(Cur.X + D.X, Cur.Y);
                    const FFlowFieldCell* NY0 = TryGetCell(Cur.X, Cur.Y + D.Y);
                    if (!NX0 || NX0->IsBlocked()) continue;
                    if (!NY0 || NY0->IsBlocked()) continue;
                }

                float MoveCost = (D.X != 0 && D.Y != 0) ? 1.414f : 1.f;
                float NewCost  = CurCost + MoveCost;

                if (NewCost < N.Integration)
                {
                    N.Integration = NewCost;
                    if (!InQueue[NY * Width + NX])
                    {
                        InQueue[NY * Width + NX] = true;
                        Open.Enqueue({NX, NY});
                    }
                }
            }
        }

        SyncHotIntegrations();
    }

    void BuildFlowField()
    {
        static const FIntPoint Dirs8[] = {
            { 1, 0}, {-1, 0}, { 0, 1}, { 0,-1},
            { 1, 1}, { 1,-1}, {-1, 1}, {-1,-1}
        };

        for (int32 Y = 0; Y < Height; ++Y)
        {
            for (int32 X = 0; X < Width; ++X)
            {
                FFlowFieldCell& Cell = GetCell(X, Y);
                Cell.FlowDir = FVector2D::ZeroVector;

                if (Cell.IsBlocked() || Cell.IsGoal() || !Cell.IsReachable())
                    continue;

                float     Best    = Cell.Integration;
                FVector2D BestDir = FVector2D::ZeroVector;

                for (const FIntPoint& D : Dirs8)
                {
                    int32 NX = X + D.X;
                    int32 NY = Y + D.Y;
                    const FFlowFieldCell* N = TryGetCell(NX, NY);
                    if (!N || N->IsBlocked()) continue;

                    if (D.X != 0 && D.Y != 0)
                    {
                        const FFlowFieldCell* NX0 = TryGetCell(X + D.X, Y);
                        const FFlowFieldCell* NY0 = TryGetCell(X, Y + D.Y);
                        if (!NX0 || NX0->IsBlocked()) continue;
                        if (!NY0 || NY0->IsBlocked()) continue;
                    }

                    if (N->Integration < Best)
                    {
                        Best    = N->Integration;
                        BestDir = FVector2D((float)D.X, (float)D.Y);
                    }
                }

                if (!BestDir.IsNearlyZero())
                    BestDir.Normalize();

                Cell.FlowDir = BestDir;
            }
        }

        SyncHotFlowDirs();
    }

    FIntPoint FindNearestWalkable(FIntPoint From) const
    {
        if (!IsInBounds(From)) return {-1, -1};
        if (!GetCell(From.X, From.Y).IsBlocked()) return From;

        TArray<bool> Visited;
        Visited.Init(false, Width * Height);

        TQueue<FIntPoint> Q;
        Q.Enqueue(From);
        Visited[From.Y * Width + From.X] = true;

        static const FIntPoint Dirs4[] = {{1,0},{-1,0},{0,1},{0,-1}};

        while (!Q.IsEmpty())
        {
            FIntPoint Cur;
            Q.Dequeue(Cur);
            for (const FIntPoint& D : Dirs4)
            {
                FIntPoint N = {Cur.X + D.X, Cur.Y + D.Y};
                if (!IsInBounds(N)) continue;
                if (Visited[N.Y * Width + N.X]) continue;
                Visited[N.Y * Width + N.X] = true;
                if (!GetCell(N.X, N.Y).IsBlocked()) return N;
                Q.Enqueue(N);
            }
        }
        return {-1, -1};
    }

    // 检查两个格子是否连通（BFS）
    bool IsConnected(FIntPoint A, FIntPoint B) const
    {
        if (!IsInBounds(A) || !IsInBounds(B)) return false;
        if (GetCell(A.X, A.Y).IsBlocked() || GetCell(B.X, B.Y).IsBlocked()) return false;
        if (A == B) return true;

        TArray<bool> Visited;
        Visited.Init(false, Width * Height);
        TQueue<FIntPoint> Q;
        Q.Enqueue(A);
        Visited[A.Y * Width + A.X] = true;

        static const FIntPoint Dirs4[] = {{1,0},{-1,0},{0,1},{0,-1}};
        while (!Q.IsEmpty())
        {
            FIntPoint Cur;
            Q.Dequeue(Cur);
            for (const FIntPoint& D : Dirs4)
            {
                FIntPoint N = {Cur.X + D.X, Cur.Y + D.Y};
                if (!IsInBounds(N) || Visited[N.Y * Width + N.X]) continue;
                if (GetCell(N.X, N.Y).IsBlocked()) continue;
                if (N == B) return true;
                Visited[N.Y * Width + N.X] = true;
                Q.Enqueue(N);
            }
        }
        return false;
    }

    // 目标可达但被障碍包围（孤岛空地场景）：
    // 思路：先找目标所在的小连通区域，再找包围该区域的障碍物，最后找障碍物外侧可达格子
    // 关键：用格子数量上限防止目标区域扩展到整个外部大区域
    TArray<FIntPoint> FindWalkableRingAroundIsolatedGoal(FIntPoint Goal) const
    {
        if (!IsInBounds(Goal)) return {};
        if (GetCell(Goal.X, Goal.Y).IsBlocked()) return {};

        static const FIntPoint Dirs4[] = {{1,0},{-1,0},{0,1},{0,-1}};

        // 第一步：BFS 找目标所在连通区域，限制最大格子数
        // 如果区域太大（超过阈值），说明目标在开阔地带，不是孤岛，返回空
        const int32 MaxIsolatedSize = (Width * Height) / 4; // 最多占网格 1/4

        TArray<bool> GoalRegion;
        GoalRegion.Init(false, Width * Height);
        TArray<FIntPoint> RegionCells;

        TQueue<FIntPoint> Q;
        Q.Enqueue(Goal);
        GoalRegion[Goal.Y * Width + Goal.X] = true;
        RegionCells.Add(Goal);

        bool bTooLarge = false;
        while (!Q.IsEmpty())
        {
            if (RegionCells.Num() > MaxIsolatedSize)
            {
                bTooLarge = true;
                break;
            }
            FIntPoint Cur;
            Q.Dequeue(Cur);
            for (const FIntPoint& D : Dirs4)
            {
                FIntPoint N = {Cur.X + D.X, Cur.Y + D.Y};
                if (!IsInBounds(N) || GoalRegion[N.Y * Width + N.X]) continue;
                if (GetCell(N.X, N.Y).IsBlocked()) continue;
                GoalRegion[N.Y * Width + N.X] = true;
                RegionCells.Add(N);
                Q.Enqueue(N);
            }
        }

        // 目标区域太大，说明不是孤岛，返回空（走正常单点流场）
        if (bTooLarge) return {};

        // 第二步：找所有紧贴目标区域的障碍物格子
        TSet<int32> BorderObstacles;
        for (const FIntPoint& RC : RegionCells)
        {
            for (const FIntPoint& D : Dirs4)
            {
                FIntPoint N = {RC.X + D.X, RC.Y + D.Y};
                if (!IsInBounds(N)) continue;
                if (GetCell(N.X, N.Y).IsBlocked())
                    BorderObstacles.Add(N.Y * Width + N.X);
            }
        }

        if (BorderObstacles.Num() == 0) return {};

        // 第三步：找障碍物外侧（不在目标区域内）的可达格子
        TArray<bool> RingSet;
        RingSet.Init(false, Width * Height);
        TArray<FIntPoint> Ring;
        for (int32 Key : BorderObstacles)
        {
            int32 OX = Key % Width;
            int32 OY = Key / Width;
            for (const FIntPoint& D : Dirs4)
            {
                FIntPoint N = {OX + D.X, OY + D.Y};
                if (!IsInBounds(N)) continue;
                int32 NK = N.Y * Width + N.X;
                if (GoalRegion[NK]) continue;
                if (GetCell(N.X, N.Y).IsBlocked()) continue;
                if (RingSet[NK]) continue;
                RingSet[NK] = true;
                Ring.Add(N);
            }
        }

        return Ring;
    }

    // 目标可达但被障碍隔开（障碍围住空地，目标在空地里）：
    // 找目标周围一定范围内的障碍物，返回这些障碍物外侧的可达格子
    TArray<FIntPoint> FindWalkableRingAroundBlockedNearGoal(FIntPoint Goal) const
    {
        if (!IsInBounds(Goal)) return {};

        static const FIntPoint Dirs4[] = {{1,0},{-1,0},{0,1},{0,-1}};

        // BFS 从目标出发，找周围的障碍物格子（遇到障碍就记录，不穿越）
        TArray<bool> Visited;
        Visited.Init(false, Width * Height);
        TQueue<FIntPoint> Q;
        Q.Enqueue(Goal);
        Visited[Goal.Y * Width + Goal.X] = true;

        TSet<int32> NearObstacles; // 目标周围的障碍物
        TArray<bool> GoalRegion;
        GoalRegion.Init(false, Width * Height);
        GoalRegion[Goal.Y * Width + Goal.X] = true;

        while (!Q.IsEmpty())
        {
            FIntPoint Cur;
            Q.Dequeue(Cur);
            for (const FIntPoint& D : Dirs4)
            {
                FIntPoint N = {Cur.X + D.X, Cur.Y + D.Y};
                if (!IsInBounds(N) || Visited[N.Y * Width + N.X]) continue;
                Visited[N.Y * Width + N.X] = true;

                if (GetCell(N.X, N.Y).IsBlocked())
                {
                    NearObstacles.Add(N.Y * Width + N.X);
                    // 障碍物也继续 BFS，找连通的障碍物区域
                    Q.Enqueue(N);
                }
                else
                {
                    GoalRegion[N.Y * Width + N.X] = true;
                    Q.Enqueue(N);
                }
            }
        }

        // 找障碍物外侧（不在目标区域）的可达格子
        TArray<bool> RingSet;
        RingSet.Init(false, Width * Height);
        TArray<FIntPoint> Ring;
        for (int32 Key : NearObstacles)
        {
            int32 OX = Key % Width;
            int32 OY = Key / Width;
            for (const FIntPoint& D : Dirs4)
            {
                FIntPoint N = {OX + D.X, OY + D.Y};
                if (!IsInBounds(N)) continue;
                int32 NK = N.Y * Width + N.X;
                if (GoalRegion[NK]) continue;
                if (GetCell(N.X, N.Y).IsBlocked()) continue;
                if (RingSet[NK]) continue;
                RingSet[NK] = true;
                Ring.Add(N);
            }
        }

        return Ring;
    }

    // 找目标周围所有紧贴障碍物边缘的可达格子（用于攻城包围效果）
    // 返回所有与障碍物相邻的可达格子，作为多目标流场的目标集合
    TArray<FIntPoint> FindWalkableRingAroundBlocked(FIntPoint BlockedCenter) const
    {
        TArray<FIntPoint> Ring;

        // BFS 找到所有连通的障碍物格子
        TArray<bool> Visited;
        Visited.Init(false, Width * Height);

        TQueue<FIntPoint> Q;

        // 从给定点出发，找连通的障碍物区域
        if (!IsInBounds(BlockedCenter)) return Ring;

        Q.Enqueue(BlockedCenter);
        Visited[BlockedCenter.Y * Width + BlockedCenter.X] = true;

        static const FIntPoint Dirs4[] = {{1,0},{-1,0},{0,1},{0,-1}};

        TArray<bool> RingSet; // 避免重复添加
        RingSet.Init(false, Width * Height);

        while (!Q.IsEmpty())
        {
            FIntPoint Cur;
            Q.Dequeue(Cur);

            for (const FIntPoint& D : Dirs4)
            {
                FIntPoint N = {Cur.X + D.X, Cur.Y + D.Y};
                if (!IsInBounds(N)) continue;

                const FFlowFieldCell& NC = GetCell(N.X, N.Y);

                if (!NC.IsBlocked())
                {
                    // 可达格子且紧贴障碍物，加入包围圈
                    int32 Key = N.Y * Width + N.X;
                    if (!RingSet[Key])
                    {
                        RingSet[Key] = true;
                        Ring.Add(N);
                    }
                }
                else if (!Visited[N.Y * Width + N.X])
                {
                    // 继续扩展障碍物区域
                    Visited[N.Y * Width + N.X] = true;
                    Q.Enqueue(N);
                }
            }
        }

        return Ring;
    }

    // 从网格边界洪水填充，把被障碍完全包围的封闭区域也标记为障碍
    // 调用时机：ApplyObstacles 之后、BuildIntegrationField 之前
    // 这样封闭空地内的目标会被当成障碍，FindNearestWalkable 自然找到城墙外边缘
    void SealEnclosedRegions()
    {
        // 从边界所有可走格子出发 BFS，标记所有"外部可达"格子
        TArray<bool> Reachable;
        Reachable.Init(false, Width * Height);
        TQueue<FIntPoint> Q;

        static const FIntPoint Dirs4[] = {{1,0},{-1,0},{0,1},{0,-1}};

        // 把四条边上的可走格子全部入队
        for (int32 X = 0; X < Width; ++X)
        {
            if (!GetCell(X, 0).IsBlocked())        { Reachable[0 * Width + X] = true;            Q.Enqueue({X, 0}); }
            if (!GetCell(X, Height-1).IsBlocked())  { Reachable[(Height-1)*Width+X] = true;       Q.Enqueue({X, Height-1}); }
        }
        for (int32 Y = 1; Y < Height - 1; ++Y)
        {
            if (!GetCell(0, Y).IsBlocked())         { Reachable[Y * Width + 0] = true;            Q.Enqueue({0, Y}); }
            if (!GetCell(Width-1, Y).IsBlocked())   { Reachable[Y * Width + Width-1] = true;      Q.Enqueue({Width-1, Y}); }
        }

        // BFS 扩展所有从边界可达的格子
        while (!Q.IsEmpty())
        {
            FIntPoint Cur;
            Q.Dequeue(Cur);
            for (const FIntPoint& D : Dirs4)
            {
                FIntPoint N = {Cur.X + D.X, Cur.Y + D.Y};
                if (!IsInBounds(N)) continue;
                if (Reachable[N.Y * Width + N.X]) continue;
                if (GetCell(N.X, N.Y).IsBlocked()) continue;
                Reachable[N.Y * Width + N.X] = true;
                Q.Enqueue(N);
            }
        }

        // 从边界不可达的可走格子 → 封闭区域 → 标记为障碍
        int32 SealedCount = 0;
        for (int32 Y = 0; Y < Height; ++Y)
        {
            for (int32 X = 0; X < Width; ++X)
            {
                if (!Reachable[Y * Width + X] && !GetCell(X, Y).IsBlocked())
                {
                    GetCell(X, Y).Cost = 255;
                    SealedCount++;
                }
            }
        }

        if (SealedCount > 0)
            UE_LOG(LogTemp, Log, TEXT("[FlowField] SealEnclosedRegions: 封闭 %d 个格子"), SealedCount);
    }

    // ── A* 寻路 ────────────────────────────────────────────────────
    // 在当前格子（含障碍）上做 A* 寻路，返回格子坐标路径。
    // 路径已精简（去掉方向不变的中间点），空数组 = 无路径。
    bool FindPathAStar(FIntPoint Start, FIntPoint Goal, TArray<FIntPoint>& OutPath) const
    {
        OutPath.Reset();
        if (!IsInBounds(Start) || !IsInBounds(Goal)) return false;
        if (GetCell(Start.X, Start.Y).IsBlocked() || GetCell(Goal.X, Goal.Y).IsBlocked()) return false;
        if (Start == Goal) { OutPath.Add(Start); return true; }

        const int32 Total = Width * Height;

        TArray<float>    GCost;  GCost.Init(FLT_MAX, Total);
        TArray<FIntPoint> Parent; Parent.Init({-1,-1}, Total);
        TArray<bool>      Closed; Closed.Init(false,   Total);

        struct FNode
        {
            float    F;
            FIntPoint P;
            // 最小堆：F 小的优先（UE TArray heap 是最大堆，用大于使最小的排最前）
            bool operator>(const FNode& O) const { return F > O.F; }
        };
        auto Pred = [](const FNode& A, const FNode& B){ return A.F > B.F; };

        auto Heuristic = [](FIntPoint A, FIntPoint B) -> float
        {
            // Chebyshev（适合 8 方向移动）
            const int32 DX = FMath::Abs(A.X - B.X);
            const int32 DY = FMath::Abs(A.Y - B.Y);
            return (float)FMath::Max(DX, DY) + 0.414f * (float)FMath::Min(DX, DY);
        };

        static const FIntPoint Dirs8[] = {
            { 1, 0},{-1, 0},{ 0, 1},{ 0,-1},
            { 1, 1},{ 1,-1},{-1, 1},{-1,-1}
        };

        TArray<FNode> Heap;
        const int32 SI      = Start.Y * Width + Start.X;
        GCost[SI]           = 0.f;
        Heap.HeapPush({Heuristic(Start, Goal), Start}, Pred);

        while (Heap.Num() > 0)
        {
            FNode Cur;
            Heap.HeapPop(Cur, Pred);

            const int32 CI = Cur.P.Y * Width + Cur.P.X;
            if (Closed[CI]) continue;
            Closed[CI] = true;

            if (Cur.P == Goal)
            {
                // 回溯路径
                FIntPoint P = Goal;
                while (P.X >= 0)
                {
                    OutPath.Add(P);
                    P = Parent[P.Y * Width + P.X];
                }
                Algo::Reverse(OutPath);
                // 精简路径：去掉方向不变的中间点
                if (OutPath.Num() > 2)
                {
                    TArray<FIntPoint> Slim;
                    Slim.Add(OutPath[0]);
                    for (int32 i = 1; i < OutPath.Num() - 1; i++)
                    {
                        FIntPoint D1 = OutPath[i]   - OutPath[i-1];
                        FIntPoint D2 = OutPath[i+1] - OutPath[i];
                        if (D1 != D2) Slim.Add(OutPath[i]);
                    }
                    Slim.Add(OutPath.Last());
                    OutPath = MoveTemp(Slim);
                }
                return true;
            }

            for (const FIntPoint& D : Dirs8)
            {
                const int32 NX = Cur.P.X + D.X;
                const int32 NY = Cur.P.Y + D.Y;
                if (!IsInBounds(NX, NY)) continue;

                const int32 NI = NY * Width + NX;
                if (Closed[NI]) continue;
                if (GetCell(NX, NY).IsBlocked()) continue;

                // 对角线：两侧格子必须可走
                if (D.X != 0 && D.Y != 0)
                {
                    const FFlowFieldCell* Ax = TryGetCell(Cur.P.X + D.X, Cur.P.Y);
                    const FFlowFieldCell* Ay = TryGetCell(Cur.P.X, Cur.P.Y + D.Y);
                    if (!Ax || Ax->IsBlocked()) continue;
                    if (!Ay || Ay->IsBlocked()) continue;
                }

                const float MoveG = GCost[CI] + (D.X != 0 && D.Y != 0 ? 1.414f : 1.f);
                if (MoveG < GCost[NI])
                {
                    GCost[NI]  = MoveG;
                    Parent[NI] = Cur.P;
                    Heap.HeapPush({MoveG + Heuristic({NX,NY}, Goal), {NX,NY}}, Pred);
                }
            }
        }
        return false; // 无路径
    }

    int32 CountWalkable()  const { int32 N=0; for (auto& C : Cells) if (!C.IsBlocked())      N++; return N; }
    int32 CountReachable() const { int32 N=0; for (auto& C : Cells) if (C.IsReachable())     N++; return N; }
    int32 CountWithFlow()  const { int32 N=0; for (auto& C : Cells) if (!C.FlowDir.IsZero()) N++; return N; }

    float MaxIntegration() const
    {
        float M = 0.f;
        for (auto& C : Cells)
            if (C.IsReachable()) M = FMath::Max(M, C.Integration);
        return M;
    }

    FVector Origin   = FVector::ZeroVector; // 网格世界空间左下角原点
    int32   Width    = 0;                   // X 方向格子数
    int32   Height   = 0;                   // Y 方向格子数
    float   CellSize = 200.f;              // 每格边长（cm）

private:
    TArray<FFlowFieldCell> Cells;           // 所有格子数据
    TArray<FVector2D>      HotFlowDirs;     // 热路径：缓存流场方向，避免逐帧访问 Cells
    TArray<float>          HotIntegrations; // 热路径：缓存 Integration，加速碰撞检测
    TArray<int32>          OccupancyMap;    // 每格当前 AI 占位计数
    TArray<uint8>          BorderCells;     // 预计算边界格：walkable 且紧邻 blocked 格，值 1 表示边界
    TArray<FVector2D>      BorderFaceDirs;  // 预计算边界格面向方向：紧邻障碍格方向的平均值

    // 将 Cells 中的 FlowDir 同步到热路径缓存
    void SyncHotFlowDirs()
    {
        const int32 Total = Width * Height;
        HotFlowDirs.SetNumUninitialized(Total);
        for (int32 i = 0; i < Total; i++)
            HotFlowDirs[i] = Cells[i].FlowDir;
    }

    // 将 Cells 中的 Integration 同步到热路径缓存
    void SyncHotIntegrations()
    {
        const int32 Total = Width * Height;
        HotIntegrations.SetNumUninitialized(Total);
        for (int32 i = 0; i < Total; i++)
            HotIntegrations[i] = Cells[i].Integration;
    }
};