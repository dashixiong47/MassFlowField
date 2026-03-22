#include "MassAI/FlowFieldAttackProcessor.h"
#include "FlowFieldAttackComponent.h"
#include "FlowFieldAttackTypes.h"
#include "FlowFieldSubsystem.h"
#include "FlowFieldActor.h"
#include "MassExecutionContext.h"
#include "MassEntityManager.h"
#include "MassActorSubsystem.h"
#include "DrawDebugHelpers.h"

UFlowFieldAttackProcessor::UFlowFieldAttackProcessor()
{
    ProcessingPhase = EMassProcessingPhase::PostPhysics;
    ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Movement;
    ExecutionOrder.ExecuteAfter.Add(TEXT("FlowFieldBehaviorProcessor"));
    ExecutionFlags = (int32)(EProcessorExecutionFlags::Server
                           | EProcessorExecutionFlags::Client
                           | EProcessorExecutionFlags::Standalone);
    bAutoRegisterWithProcessingPhases = true;
    // 调用 Blueprint 委托必须在游戏线程
    bRequiresGameThreadExecution = true;
}

void UFlowFieldAttackProcessor::ConfigureQueries(
    const TSharedRef<FMassEntityManager>& EntityManager)
{
    // 存活实体查询：排除死亡实体
    EntityQuery.Initialize(EntityManager);
    EntityQuery.AddTagRequirement<FFlowFieldAgentTag>(EMassFragmentPresence::All);
    EntityQuery.AddTagRequirement<FFlowFieldDeadTag>(EMassFragmentPresence::None);
    EntityQuery.AddRequirement<FFlowFieldAgentFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
    EntityQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadOnly,
        EMassFragmentPresence::Optional);
    EntityQuery.RegisterWithProcessor(*this);
    RegisterQuery(EntityQuery);

    // 死亡倒计时查询：仅死亡实体
    DeathLingerQuery.Initialize(EntityManager);
    DeathLingerQuery.AddTagRequirement<FFlowFieldAgentTag>(EMassFragmentPresence::All);
    DeathLingerQuery.AddTagRequirement<FFlowFieldDeadTag>(EMassFragmentPresence::All);
    DeathLingerQuery.AddRequirement<FFlowFieldAgentFragment>(EMassFragmentAccess::ReadWrite);
    DeathLingerQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadOnly,
        EMassFragmentPresence::Optional);
    DeathLingerQuery.RegisterWithProcessor(*this);
    RegisterQuery(DeathLingerQuery);
}

// ── 辅助函数 ────────────────────────────────────────────────────

// 应用一次命中（直接伤害 + DoT + 击退）
static void ApplyHit(
    UFlowFieldAttackComponent* Comp,
    UFlowFieldSubsystem*       FlowSub,
    FFlowFieldActiveAttack&    Attack,
    const FFlowFieldSpatialCache::FEntry& Entry,
    FVector HitPos)
{
    const FFlowFieldAttackConfig& Cfg = Attack.Config;

    // 直接伤害
    if (Cfg.DirectDamage > 0.f)
        Comp->BroadcastEntityHit(Attack.AttackId, Entry.EntityId,
            Entry.Actor, Cfg.DirectDamage, HitPos);

    // DoT
    if (Cfg.DotDamage > 0.f && Cfg.DotDuration > 0.f)
    {
        FFlowFieldDoTEntry DoT;
        DoT.AttackId      = Attack.AttackId;
        DoT.DamagePerSec  = Cfg.DotDamage;
        DoT.TimeRemaining = Cfg.DotDuration;
        DoT.DotInterval   = FMath::Max(Cfg.DotInterval, 0.05f);
        DoT.IntervalTimer = DoT.DotInterval; // 第一帧立即触发
        Comp->GetDoTMap().FindOrAdd(Entry.EntityId).Add(DoT);
    }

    // 击退
    const float KBRadius = Cfg.KnockbackRadius > 0.f ? Cfg.KnockbackRadius : Cfg.HitRadius;
    if (Cfg.KnockbackStrength > 0.f && FlowSub)
        FlowSub->ApplyExplosionKnockback(HitPos, KBRadius,
            Cfg.KnockbackStrength);
}

// 处理直线激光命中
static void ProcessLaserStraight(
    FFlowFieldActiveAttack&    Attack,
    const FFlowFieldSpatialCache& Cache,
    UFlowFieldAttackComponent* Comp,
    UFlowFieldSubsystem*       FlowSub,
    UWorld*                    World,
    bool bDrawDebug, float DrawDistSq, FVector CamPos)
{
    const FFlowFieldAttackConfig& Cfg = Attack.Config;
    const FVector Dir = (Cfg.Target - Cfg.Origin).GetSafeNormal2D();
    const FVector End = Cfg.Origin + Dir * Cfg.MaxRange;
    Attack.LaserEnd = End;

    TArray<const FFlowFieldSpatialCache::FEntry*> Hits;
    Cache.QueryLine(Cfg.Origin, End, Cfg.HitRadius, Hits);

    if (!Cfg.bPiercing && Hits.Num() > 0)
    {
        // 取最近目标
        const FFlowFieldSpatialCache::FEntry* Nearest = nullptr;
        float MinDistSq = FLT_MAX;
        const FVector2D O2D(Cfg.Origin.X, Cfg.Origin.Y);
        for (const auto* E : Hits)
        {
            float D = FVector2D::DistSquared(E->Pos2D, O2D);
            if (D < MinDistSq) { MinDistSq = D; Nearest = E; }
        }
        if (Nearest)
        {
            const FVector HP(Nearest->Pos2D.X, Nearest->Pos2D.Y, Cfg.Origin.Z);
            ApplyHit(Comp, FlowSub, Attack, *Nearest, HP);
            Attack.LaserEnd = HP; // 截断到命中点

            if (bDrawDebug && Comp->bDrawHitPoints &&
                FVector::DistSquared(HP, CamPos) <= DrawDistSq)
                DrawDebugSphere(World, HP, 20.f, 10, FColor::Green, false, 0.5f);
        }
    }
    else
    {
        for (const auto* E : Hits)
        {
            if (Attack.HitEntityIds.Contains(E->EntityId)) continue;
            Attack.HitEntityIds.Add(E->EntityId);
            const FVector HP(E->Pos2D.X, E->Pos2D.Y, Cfg.Origin.Z);
            ApplyHit(Comp, FlowSub, Attack, *E, HP);

            if (bDrawDebug && Comp->bDrawHitPoints &&
                FVector::DistSquared(HP, CamPos) <= DrawDistSq)
                DrawDebugSphere(World, HP, 20.f, 10, FColor::Green, false, 0.5f);
        }
    }

    // 调试：激光线（青色）
    if (bDrawDebug && Comp->bDrawLaser &&
        FVector::DistSquared(Cfg.Origin, CamPos) <= DrawDistSq)
    {
        DrawDebugLine(World, Cfg.Origin, Attack.LaserEnd,
            FColor::Cyan, false, Cfg.VisualDuration, 0, 3.f);
    }
}

// 处理扇形激光命中
static void ProcessLaserFan(
    FFlowFieldActiveAttack&    Attack,
    const FFlowFieldSpatialCache& Cache,
    UFlowFieldAttackComponent* Comp,
    UFlowFieldSubsystem*       FlowSub,
    UWorld*                    World,
    bool bDrawDebug, float DrawDistSq, FVector CamPos)
{
    const FFlowFieldAttackConfig& Cfg = Attack.Config;
    // DebugPath[0] = Origin，[1..N] = 射线终点（在 FireAttack 时预算）
    const FVector Origin = Cfg.Origin;
    TSet<int32> HitThisAttack;

    for (int32 k = 1; k < Attack.DebugPath.Num(); ++k)
    {
        const FVector RayEnd = Attack.DebugPath[k];

        TArray<const FFlowFieldSpatialCache::FEntry*> Hits;
        Cache.QueryLine(Origin, RayEnd, Cfg.HitRadius, Hits);

        for (const auto* E : Hits)
        {
            if (HitThisAttack.Contains(E->EntityId)) continue;
            if (!Cfg.bPiercing && HitThisAttack.Num() > 0) break;
            HitThisAttack.Add(E->EntityId);
            const FVector HP(E->Pos2D.X, E->Pos2D.Y, Origin.Z);
            ApplyHit(Comp, FlowSub, Attack, *E, HP);

            if (bDrawDebug && Comp->bDrawHitPoints &&
                FVector::DistSquared(HP, CamPos) <= DrawDistSq)
                DrawDebugSphere(World, HP, 20.f, 10, FColor::Green, false, 0.5f);
        }

        // 调试：扇形射线（青色）
        if (bDrawDebug && Comp->bDrawLaser &&
            FVector::DistSquared(Origin, CamPos) <= DrawDistSq)
            DrawDebugLine(World, Origin, RayEnd,
                FColor::Cyan, false, Cfg.VisualDuration, 0, 2.f);
    }
}

// 处理连锁激光命中
static void ProcessLaserChain(
    FFlowFieldActiveAttack&    Attack,
    const FFlowFieldSpatialCache& Cache,
    UFlowFieldAttackComponent* Comp,
    UFlowFieldSubsystem*       FlowSub,
    UWorld*                    World,
    bool bDrawDebug, float DrawDistSq, FVector CamPos)
{
    const FFlowFieldAttackConfig& Cfg = Attack.Config;
    Attack.DebugPath.Reset();
    Attack.DebugPath.Add(Cfg.Origin);

    FVector ChainPos = Cfg.Origin;
    TSet<int32> Visited;
    int32 ChainCount = 0;

    // 第一段：从 Origin 到 Target 方向的第一个目标
    {
        const FVector Dir = (Cfg.Target - Cfg.Origin).GetSafeNormal2D();
        const FVector End = Cfg.Origin + Dir * Cfg.MaxRange;
        TArray<const FFlowFieldSpatialCache::FEntry*> Hits;
        Cache.QueryLine(Cfg.Origin, End, Cfg.HitRadius, Hits);

        // 最近目标
        const FFlowFieldSpatialCache::FEntry* First = nullptr;
        float MinD = FLT_MAX;
        const FVector2D O2D(Cfg.Origin.X, Cfg.Origin.Y);
        for (const auto* E : Hits)
        {
            float D = FVector2D::DistSquared(E->Pos2D, O2D);
            if (D < MinD) { MinD = D; First = E; }
        }
        if (!First) return;

        Visited.Add(First->EntityId);
        const FVector HP(First->Pos2D.X, First->Pos2D.Y, Cfg.Origin.Z);
        ApplyHit(Comp, FlowSub, Attack, *First, HP);
        Attack.DebugPath.Add(HP);
        ChainPos = HP;
        ++ChainCount;
    }

    // 连锁跳跃
    while (ChainCount < Cfg.MaxChainCount)
    {
        TArray<const FFlowFieldSpatialCache::FEntry*> Nearby;
        Cache.QueryRadius(ChainPos, Cfg.ChainRadius, Nearby);

        const FFlowFieldSpatialCache::FEntry* Next = nullptr;
        float MinD = FLT_MAX;
        const FVector2D C2D(ChainPos.X, ChainPos.Y);
        for (const auto* E : Nearby)
        {
            if (Visited.Contains(E->EntityId)) continue;
            float D = FVector2D::DistSquared(E->Pos2D, C2D);
            if (D < MinD) { MinD = D; Next = E; }
        }
        if (!Next) break;

        Visited.Add(Next->EntityId);
        const FVector HP(Next->Pos2D.X, Next->Pos2D.Y, ChainPos.Z);
        ApplyHit(Comp, FlowSub, Attack, *Next, HP);

        // 调试：连锁段（品红色）
        if (bDrawDebug && Comp->bDrawLaser &&
            FVector::DistSquared(ChainPos, CamPos) <= DrawDistSq)
        {
            DrawDebugLine(World, ChainPos, HP,
                FColor::Magenta, false, Cfg.VisualDuration, 0, 3.f);
            DrawDebugSphere(World, HP, 25.f, 10,
                FColor::Magenta, false, Cfg.VisualDuration);
        }

        Attack.DebugPath.Add(HP);
        ChainPos = HP;
        ++ChainCount;
    }

    // 调试：第一段（品红色）
    if (bDrawDebug && Comp->bDrawLaser && Attack.DebugPath.Num() >= 2 &&
        FVector::DistSquared(Cfg.Origin, CamPos) <= DrawDistSq)
        DrawDebugLine(World, Attack.DebugPath[0], Attack.DebugPath[1],
            FColor::Magenta, false, Cfg.VisualDuration, 0, 3.f);

    // 命中点
    if (bDrawDebug && Comp->bDrawHitPoints)
        for (int32 k = 1; k < Attack.DebugPath.Num(); ++k)
            if (FVector::DistSquared(Attack.DebugPath[k], CamPos) <= DrawDistSq)
                DrawDebugSphere(World, Attack.DebugPath[k],
                    20.f, 10, FColor::Green, false, 0.5f);
}

// 处理爆炸命中
static void ProcessExplosion(
    FFlowFieldActiveAttack&    Attack,
    const FFlowFieldSpatialCache& Cache,
    UFlowFieldAttackComponent* Comp,
    UFlowFieldSubsystem*       FlowSub,
    UWorld*                    World,
    bool bDrawDebug, float DrawDistSq, FVector CamPos)
{
    const FFlowFieldAttackConfig& Cfg = Attack.Config;
    TArray<const FFlowFieldSpatialCache::FEntry*> Hits;
    Cache.QueryRadius(Cfg.Target, Cfg.HitRadius, Hits);

    for (const auto* E : Hits)
    {
        const FVector HP(E->Pos2D.X, E->Pos2D.Y, Cfg.Target.Z);
        ApplyHit(Comp, FlowSub, Attack, *E, HP);

        if (bDrawDebug && Comp->bDrawHitPoints &&
            FVector::DistSquared(HP, CamPos) <= DrawDistSq)
            DrawDebugSphere(World, HP, 20.f, 10, FColor::Green, false, 0.5f);
    }

    // 调试：爆炸球体（红色）+ 平面圆（俯视友好）
    if (bDrawDebug && Comp->bDrawExplosion &&
        FVector::DistSquared(Cfg.Target, CamPos) <= DrawDistSq)
    {
        DrawDebugSphere(World, Cfg.Target, Cfg.HitRadius,
            24, FColor::Red, false, Cfg.VisualDuration, 0, 2.f);
        DrawDebugCircle(World, Cfg.Target, Cfg.HitRadius,
            32, FColor::Red, false, Cfg.VisualDuration, 0, 2.f,
            FVector(1,0,0), FVector(0,1,0));
    }
}

// ── Execute ─────────────────────────────────────────────────────

void UFlowFieldAttackProcessor::Execute(
    FMassEntityManager& EntityManager,
    FMassExecutionContext& Context)
{
    UWorld* World = GetWorld();
    if (!World) return;

    UFlowFieldSubsystem* FlowSub = World->GetSubsystem<UFlowFieldSubsystem>();
    if (!FlowSub) return;

    AFlowFieldActor* FlowActor = FlowSub->GetActor();
    if (!FlowActor) return;

    UFlowFieldAttackComponent* Comp = FlowActor->AttackComp;
    if (!Comp) return;

    const float DeltaTime = Context.GetDeltaTimeSeconds();
    if (DeltaTime <= 0.f) return;

    // ── 0. 相机位置（调试距离剔除）────────────────────────────
    FVector CamPos = FVector::ZeroVector;
    const float DrawDistSq = FMath::Square(Comp->AttackDebugDrawDistance);
    const bool bAnyDebug = Comp->bDrawProjectileTrajectory
        || Comp->bDrawProjectileArrow || Comp->bDrawLaser
        || Comp->bDrawExplosion       || Comp->bDrawHitPoints;
    if (bAnyDebug)
    {
        if (APlayerController* PC = World->GetFirstPlayerController())
            if (PC->PlayerCameraManager)
                CamPos = PC->PlayerCameraManager->GetCameraLocation();
    }

    // ── 1. 构建空间缓存 + 更新存活实体 Handle 映射 ───────────
    FFlowFieldSpatialCache Cache;
    Cache.Reset();
    AliveHandleCache.Reset();

    EntityQuery.ForEachEntityChunk(Context,
        [&](FMassExecutionContext& ChunkCtx)
        {
            const auto Agents     = ChunkCtx.GetFragmentView<FFlowFieldAgentFragment>();
            const auto Transforms = ChunkCtx.GetFragmentView<FTransformFragment>();
            const TConstArrayView<FMassEntityHandle> Handles = ChunkCtx.GetEntities();
            const int32 Num = ChunkCtx.GetNumEntities();

            const bool bHasActor = ChunkCtx.DoesArchetypeHaveFragment<FMassActorFragment>();
            TArrayView<FMassActorFragment> ActorView;
            if (bHasActor)
                ActorView = ChunkCtx.GetMutableFragmentView<FMassActorFragment>();

            for (int32 i = 0; i < Num; ++i)
            {
                const FVector Pos = Transforms[i].GetTransform().GetLocation();
                AActor* Actor = (bHasActor && ActorView.IsValidIndex(i))
                    ? ActorView[i].GetMutable() : nullptr;

                Cache.Add(Handles[i].Index, Pos, Actor);
                AliveHandleCache.Add(Handles[i].Index, Handles[i]);
            }
        });

    // ── 2. 更新死亡实体 Handle 映射 ──────────────────────────
    DeadHandleCache.Reset();
    DeathLingerQuery.ForEachEntityChunk(Context,
        [&](FMassExecutionContext& ChunkCtx)
        {
            const TConstArrayView<FMassEntityHandle> Handles = ChunkCtx.GetEntities();
            const int32 Num = ChunkCtx.GetNumEntities();
            for (int32 i = 0; i < Num; ++i)
                DeadHandleCache.Add(Handles[i].Index, Handles[i]);
        });

    // ── 3. 处理待击杀实体（上帧 BP 调用 KillAgent）──────────
    {
        auto& Kills = Comp->GetPendingKills();
        for (int32 EntityId : Kills)
        {
            if (FMassEntityHandle* H = AliveHandleCache.Find(EntityId))
            {
                if (EntityManager.IsEntityValid(*H))
                {
                    // 取 Actor 引用，广播死亡事件
                    AActor* Actor = nullptr;
                    for (const auto& E : Cache.All)
                        if (E.EntityId == EntityId) { Actor = E.Actor; break; }

                    EntityManager.AddTagToEntity(*H, FFlowFieldDeadTag::StaticStruct());
                    Comp->BroadcastEntityDied(EntityId, Actor);
                }
            }
        }
        Kills.Reset();
    }

    // ── 4. 即时命中检测（Laser / Explosion PendingFires）─────
    auto& PendingFires = Comp->GetPendingFires();
    for (FFlowFieldActiveAttack& Attack : PendingFires)
    {
        if (!Attack.bActive) continue;

        switch (Attack.Config.Type)
        {
        case EFlowFieldAttackType::Laser:
            switch (Attack.Config.LaserMode)
            {
            case EFlowFieldLaserMode::Straight:
                ProcessLaserStraight(Attack, Cache, Comp, FlowSub, World,
                    bAnyDebug, DrawDistSq, CamPos);
                break;
            case EFlowFieldLaserMode::Fan:
                ProcessLaserFan(Attack, Cache, Comp, FlowSub, World,
                    bAnyDebug, DrawDistSq, CamPos);
                break;
            case EFlowFieldLaserMode::Chain:
                ProcessLaserChain(Attack, Cache, Comp, FlowSub, World,
                    bAnyDebug, DrawDistSq, CamPos);
                break;
            }
            break;

        case EFlowFieldAttackType::Explosion:
            ProcessExplosion(Attack, Cache, Comp, FlowSub, World,
                bAnyDebug, DrawDistSq, CamPos);
            break;

        default: break;
        }

        Attack.bHitProcessed = true;
    }
    // 移入 ActiveAttacks（视觉持续）
    for (FFlowFieldActiveAttack& A : PendingFires)
        if (A.bActive && A.Config.VisualDuration > 0.f)
            Comp->GetActiveAttacks().Add(MoveTemp(A));
    PendingFires.Reset();

    // ── 5. 飞行体推进 + 命中检测 + 所有攻击调试绘制 ─────────
    auto& ActiveAttacks = Comp->GetActiveAttacks();
    for (FFlowFieldActiveAttack& Attack : ActiveAttacks)
    {
        if (!Attack.bActive) continue;
        const FFlowFieldAttackConfig& Cfg = Attack.Config;

        if (Cfg.Type == EFlowFieldAttackType::Projectile)
        {
            // 推进位置
            Attack.ElapsedTime += DeltaTime;
            const float T = FMath::Clamp(Attack.ElapsedTime / FMath::Max(Attack.TotalTime, 0.001f), 0.f, 1.f);
            Attack.CurrentPos = FMath::Lerp(Cfg.Origin, Cfg.Target, T);

            // 命中检测
            TArray<const FFlowFieldSpatialCache::FEntry*> Hits;
            Cache.QueryRadius(Attack.CurrentPos, Cfg.HitRadius, Hits);

            for (const auto* E : Hits)
            {
                if (Cfg.bPiercing)
                {
                    if (Attack.HitEntityIds.Contains(E->EntityId)) continue;
                    Attack.HitEntityIds.Add(E->EntityId);
                }
                ApplyHit(Comp, FlowSub, Attack, *E, Attack.CurrentPos);

                if (bAnyDebug && Comp->bDrawHitPoints &&
                    FVector::DistSquared(Attack.CurrentPos, CamPos) <= DrawDistSq)
                    DrawDebugSphere(World, Attack.CurrentPos,
                        20.f, 10, FColor::Green, false, 0.5f);

                if (!Cfg.bPiercing) { Attack.bActive = false; break; }
            }

            // 到达终点或超过 MaxRange
            if (Attack.bActive && T >= 1.f)
                Attack.bActive = false;

            // 调试：轨迹线（黄色，每帧重绘）
            if (bAnyDebug && Comp->bDrawProjectileTrajectory &&
                FVector::DistSquared(Cfg.Origin, CamPos) <= DrawDistSq)
                DrawDebugLine(World, Cfg.Origin, Cfg.Target,
                    FColor::Yellow, false, 0.f, 0, 1.5f);

            // 调试：飞行体箭头（橙色，跟随当前位置）
            if (bAnyDebug && Comp->bDrawProjectileArrow &&
                FVector::DistSquared(Attack.CurrentPos, CamPos) <= DrawDistSq)
            {
                const FVector Dir = (Cfg.Target - Cfg.Origin).GetSafeNormal();
                DrawDebugDirectionalArrow(World,
                    Attack.CurrentPos - Dir * 40.f,
                    Attack.CurrentPos + Dir * 40.f,
                    40.f, FColor::Orange, false, 0.f, 0, 3.f);
            }
        }
        else
        {
            // 激光 / 爆炸：仅维持视觉时长，调试线已在 PendingFires 阶段绘制
            Attack.ElapsedTime += DeltaTime;
            if (Attack.ElapsedTime >= Cfg.VisualDuration)
                Attack.bActive = false;
        }
    }

    // 清理非活跃攻击，派发 OnAttackEnd
    for (int32 i = ActiveAttacks.Num() - 1; i >= 0; --i)
    {
        FFlowFieldActiveAttack& A = ActiveAttacks[i];
        if (!A.bActive)
        {
            Comp->BroadcastAttackEnd(A.AttackId, A.CurrentPos);
            ActiveAttacks.RemoveAtSwap(i);
        }
    }

    // ── 6. DoT tick ──────────────────────────────────────────
    for (auto& [EntityId, DoTs] : Comp->GetDoTMap())
    {
        // 找 Actor（可能已死亡，死亡实体也继续 DoT）
        AActor* Actor = nullptr;
        for (const auto& E : Cache.All)
            if (E.EntityId == EntityId) { Actor = E.Actor; break; }

        for (FFlowFieldDoTEntry& DoT : DoTs)
        {
            DoT.TimeRemaining -= DeltaTime;
            DoT.IntervalTimer += DeltaTime;
            if (DoT.IntervalTimer >= DoT.DotInterval)
            {
                DoT.IntervalTimer -= DoT.DotInterval;
                const float Dmg = DoT.DamagePerSec * DoT.DotInterval;
                Comp->BroadcastDoTTick(DoT.AttackId, EntityId, Actor, Dmg);
            }
        }
        DoTs.RemoveAll([](const FFlowFieldDoTEntry& E) { return E.TimeRemaining <= 0.f; });
    }
    // 清理空条目
    for (auto It = Comp->GetDoTMap().CreateIterator(); It; ++It)
        if (It.Value().IsEmpty()) It.RemoveCurrent();

    // ── 7. 死亡倒计时（死亡实体，等待 DeathLingerTime）───────
    DeathLingerQuery.ForEachEntityChunk(Context,
        [&](FMassExecutionContext& ChunkCtx)
        {
            auto Agents = ChunkCtx.GetMutableFragmentView<FFlowFieldAgentFragment>();
            const TConstArrayView<FMassEntityHandle> Handles = ChunkCtx.GetEntities();
            const int32 Num = ChunkCtx.GetNumEntities();

            for (int32 i = 0; i < Num; ++i)
            {
                FFlowFieldAgentFragment& Agent = Agents[i];
                if (!Agent.bAutoDestroy) continue;
                if (Agent.DeathLingerTime <= 0.f)
                {
                    // 立即销毁
                    Comp->GetPendingDestroys().AddUnique(Handles[i].Index);
                }
                else
                {
                    Agent.DeathTimer += DeltaTime;
                    if (Agent.DeathTimer >= Agent.DeathLingerTime)
                        Comp->GetPendingDestroys().AddUnique(Handles[i].Index);
                }
            }
        });

    // ── 8. 处理待销毁（包含 BP 手动调用的 DestroyAgent）───────
    {
        auto& Destroys = Comp->GetPendingDestroys();
        for (int32 EntityId : Destroys)
        {
            // 先从 Alive，再从 Dead handle 里找
            FMassEntityHandle Handle;
            bool bFound = false;
            if (FMassEntityHandle* HA = AliveHandleCache.Find(EntityId))
                { Handle = *HA; bFound = true; }
            else if (FMassEntityHandle* HD = DeadHandleCache.Find(EntityId))
                { Handle = *HD; bFound = true; }

            if (bFound && EntityManager.IsEntityValid(Handle))
            {
                // 清除该实体的 DoT
                Comp->GetDoTMap().Remove(EntityId);
                Comp->BroadcastEntityDestroyed(EntityId);
                Context.Defer().DestroyEntity(Handle);
            }
        }
        Destroys.Reset();
    }
}
