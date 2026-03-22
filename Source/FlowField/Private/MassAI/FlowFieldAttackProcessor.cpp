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
    bRequiresGameThreadExecution = true;
}

void UFlowFieldAttackProcessor::ConfigureQueries(
    const TSharedRef<FMassEntityManager>& EntityManager)
{
    EntityQuery.Initialize(EntityManager);
    EntityQuery.AddTagRequirement<FFlowFieldAgentTag>(EMassFragmentPresence::All);
    EntityQuery.AddTagRequirement<FFlowFieldDeadTag>(EMassFragmentPresence::None);
    EntityQuery.AddRequirement<FFlowFieldAgentFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
    EntityQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadOnly,
        EMassFragmentPresence::Optional);
    EntityQuery.RegisterWithProcessor(*this);
    RegisterQuery(EntityQuery);

    DeathLingerQuery.Initialize(EntityManager);
    DeathLingerQuery.AddTagRequirement<FFlowFieldAgentTag>(EMassFragmentPresence::All);
    DeathLingerQuery.AddTagRequirement<FFlowFieldDeadTag>(EMassFragmentPresence::All);
    DeathLingerQuery.AddRequirement<FFlowFieldAgentFragment>(EMassFragmentAccess::ReadWrite);
    DeathLingerQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadOnly,
        EMassFragmentPresence::Optional);
    DeathLingerQuery.RegisterWithProcessor(*this);
    RegisterQuery(DeathLingerQuery);
}

// ── 辅助：应用一次命中 ──────────────────────────────────────────

static void ApplyHit(
    UFlowFieldAttackComponent*            Comp,
    UFlowFieldSubsystem*                  FlowSub,
    FFlowFieldActiveAttack&               Attack,
    const FFlowFieldSpatialCache::FEntry& Entry,
    FVector                               HitPos)
{
    // 直接伤害
    if (Attack.DirectDamage > 0.f)
        Comp->BroadcastEntityHit(Attack.AttackId, Entry.EntityId,
            Entry.Actor, Attack.DirectDamage, HitPos);

    // DoT
    if (Attack.DotDamage > 0.f && Attack.DotDuration > 0.f)
    {
        FFlowFieldDoTEntry DoT;
        DoT.AttackId      = Attack.AttackId;
        DoT.DamagePerSec  = Attack.DotDamage;
        DoT.TimeRemaining = Attack.DotDuration;
        DoT.DotInterval   = Attack.DotInterval;
        DoT.IntervalTimer = Attack.DotInterval; // 第一帧立即触发
        Comp->GetDoTMap().FindOrAdd(Entry.EntityId).Add(DoT);
    }

    // 击退
    if (Attack.bKnockback && FlowSub)
        FlowSub->ApplyExplosionKnockback(HitPos,
            Attack.KnockbackRadius, Attack.KnockbackStrength,
            5.f, Attack.KnockbackStaggerDuration);

    // 减速（取最强一次：Factor 越小越强）
    if (Attack.bSlow && Attack.SlowDuration > 0.f)
    {
        auto& Slot = Comp->GetPendingSlows().FindOrAdd(Entry.EntityId);
        if (Slot.Duration <= 0.f || Attack.SlowFactor < Slot.Factor)
            Slot.Factor = Attack.SlowFactor;
        Slot.Duration = FMath::Max(Slot.Duration, Attack.SlowDuration);
    }

    // 眩晕（取最长）
    if (Attack.bStun && Attack.StunDuration > 0.f)
    {
        float& Dur = Comp->GetPendingStuns().FindOrAdd(Entry.EntityId);
        Dur = FMath::Max(Dur, Attack.StunDuration);
    }

    // 自定义效果
    for (const FFlowFieldCustomEffectEntry& CE : Attack.CustomEffects)
        Comp->BroadcastCustomEffect(Attack.AttackId, Entry.EntityId,
            Entry.Actor, HitPos, CE.TypeId, CE.Value, CE.Duration);
}

// ── 激光（直线 RayCount=1 / 扇形 RayCount>1，统一处理）──────────

static void ProcessLaser(
    FFlowFieldActiveAttack&               Attack,
    const FFlowFieldSpatialCache&         Cache,
    UFlowFieldAttackComponent*            Comp,
    UFlowFieldSubsystem*                  FlowSub,
    UWorld*                               World,
    bool bDrawDebug, float DrawDistSq, FVector CamPos,
    TArray<const FFlowFieldSpatialCache::FEntry*>& Hits)
{
    // DebugPath[0]=Origin，[1..N]=各射线终点（FireLaser 时预计算）
    const FVector Origin = Attack.Origin;
    TSet<int32> HitThisAttack; // 跨射线去重

    for (int32 k = 1; k < Attack.DebugPath.Num(); ++k)
    {
        const FVector RayEnd = Attack.DebugPath[k];

        Hits.Reset();
        Cache.QueryLine(Origin, RayEnd, Attack.HitRadius, Hits);

        if (!Attack.bPiercing)
        {
            // 取最近目标（且本攻击未命中过）
            const FFlowFieldSpatialCache::FEntry* Nearest = nullptr;
            float MinD = FLT_MAX;
            const FVector2D O2D(Origin.X, Origin.Y);
            for (const auto* E : Hits)
            {
                if (HitThisAttack.Contains(E->EntityId)) continue;
                const float D = FVector2D::DistSquared(E->Pos2D, O2D);
                if (D < MinD) { MinD = D; Nearest = E; }
            }
            if (Nearest)
            {
                HitThisAttack.Add(Nearest->EntityId);
                const FVector HP(Nearest->Pos2D.X, Nearest->Pos2D.Y, Origin.Z);
                ApplyHit(Comp, FlowSub, Attack, *Nearest, HP);
                Attack.DebugPath[k] = HP; // 射线截断到命中点

                if (bDrawDebug && Comp->bDrawHitPoints &&
                    FVector::DistSquared(HP, CamPos) <= DrawDistSq)
                    DrawDebugSphere(World, HP, 20.f, 10, FColor::Green, false, 0.5f);
            }
        }
        else
        {
            for (const auto* E : Hits)
            {
                if (HitThisAttack.Contains(E->EntityId)) continue;
                HitThisAttack.Add(E->EntityId);
                const FVector HP(E->Pos2D.X, E->Pos2D.Y, Origin.Z);
                ApplyHit(Comp, FlowSub, Attack, *E, HP);

                if (bDrawDebug && Comp->bDrawHitPoints &&
                    FVector::DistSquared(HP, CamPos) <= DrawDistSq)
                    DrawDebugSphere(World, HP, 20.f, 10, FColor::Green, false, 0.5f);
            }
        }

        // 调试：射线（青色）
        if (bDrawDebug && Comp->bDrawLaser &&
            FVector::DistSquared(Origin, CamPos) <= DrawDistSq)
            DrawDebugLine(World, Origin, Attack.DebugPath[k],
                FColor::Cyan, false, Attack.VisualDuration, 0, 3.f);
    }
}

// ── 连锁激光 ────────────────────────────────────────────────────

static void ProcessChain(
    FFlowFieldActiveAttack&               Attack,
    const FFlowFieldSpatialCache&         Cache,
    UFlowFieldAttackComponent*            Comp,
    UFlowFieldSubsystem*                  FlowSub,
    UWorld*                               World,
    bool bDrawDebug, float DrawDistSq, FVector CamPos,
    TArray<const FFlowFieldSpatialCache::FEntry*>& Hits)
{
    Attack.DebugPath.Reset();
    Attack.DebugPath.Add(Attack.Origin);

    FVector ChainPos = Attack.Origin;
    TSet<int32> Visited;
    int32 ChainCount = 0;

    // 第一段：沿 Origin→Target 方向找最近目标
    {
        const FVector Dir = (Attack.Target - Attack.Origin).GetSafeNormal2D();
        const FVector End = Attack.Origin + Dir * Attack.MaxRange;
        Hits.Reset();
        Cache.QueryLine(Attack.Origin, End, Attack.HitRadius, Hits);

        const FFlowFieldSpatialCache::FEntry* First = nullptr;
        float MinD = FLT_MAX;
        const FVector2D O2D(Attack.Origin.X, Attack.Origin.Y);
        for (const auto* E : Hits)
        {
            float D = FVector2D::DistSquared(E->Pos2D, O2D);
            if (D < MinD) { MinD = D; First = E; }
        }
        if (!First) return;

        Visited.Add(First->EntityId);
        const FVector HP(First->Pos2D.X, First->Pos2D.Y, Attack.Origin.Z);
        ApplyHit(Comp, FlowSub, Attack, *First, HP);
        Attack.DebugPath.Add(HP);
        ChainPos = HP;
        ++ChainCount;
    }

    // 连锁跳跃
    while (ChainCount < Attack.MaxChainCount)
    {
        Hits.Reset();
        Cache.QueryRadius(ChainPos, Attack.ChainRadius, Hits);

        const FFlowFieldSpatialCache::FEntry* Next = nullptr;
        float MinD = FLT_MAX;
        const FVector2D C2D(ChainPos.X, ChainPos.Y);
        for (const auto* E : Hits)
        {
            if (Visited.Contains(E->EntityId)) continue;
            float D = FVector2D::DistSquared(E->Pos2D, C2D);
            if (D < MinD) { MinD = D; Next = E; }
        }
        if (!Next) break;

        Visited.Add(Next->EntityId);
        const FVector HP(Next->Pos2D.X, Next->Pos2D.Y, ChainPos.Z);
        ApplyHit(Comp, FlowSub, Attack, *Next, HP);

        if (bDrawDebug && Comp->bDrawLaser &&
            FVector::DistSquared(ChainPos, CamPos) <= DrawDistSq)
        {
            DrawDebugLine(World, ChainPos, HP,
                FColor::Magenta, false, Attack.VisualDuration, 0, 3.f);
            DrawDebugSphere(World, HP, 25.f, 10,
                FColor::Magenta, false, Attack.VisualDuration);
        }

        Attack.DebugPath.Add(HP);
        ChainPos = HP;
        ++ChainCount;
    }

    // 第一段调试线
    if (bDrawDebug && Comp->bDrawLaser && Attack.DebugPath.Num() >= 2 &&
        FVector::DistSquared(Attack.Origin, CamPos) <= DrawDistSq)
        DrawDebugLine(World, Attack.DebugPath[0], Attack.DebugPath[1],
            FColor::Magenta, false, Attack.VisualDuration, 0, 3.f);

    if (bDrawDebug && Comp->bDrawHitPoints)
        for (int32 k = 1; k < Attack.DebugPath.Num(); ++k)
            if (FVector::DistSquared(Attack.DebugPath[k], CamPos) <= DrawDistSq)
                DrawDebugSphere(World, Attack.DebugPath[k],
                    20.f, 10, FColor::Green, false, 0.5f);
}

// ── 爆炸 ────────────────────────────────────────────────────────

static void ProcessExplosion(
    FFlowFieldActiveAttack&               Attack,
    const FFlowFieldSpatialCache&         Cache,
    UFlowFieldAttackComponent*            Comp,
    UFlowFieldSubsystem*                  FlowSub,
    UWorld*                               World,
    bool bDrawDebug, float DrawDistSq, FVector CamPos,
    TArray<const FFlowFieldSpatialCache::FEntry*>& Hits)
{
    Hits.Reset();
    Cache.QueryRadius(Attack.Target, Attack.HitRadius, Hits);

    for (const auto* E : Hits)
    {
        const FVector HP(E->Pos2D.X, E->Pos2D.Y, Attack.Target.Z);
        ApplyHit(Comp, FlowSub, Attack, *E, HP);

        if (bDrawDebug && Comp->bDrawHitPoints &&
            FVector::DistSquared(HP, CamPos) <= DrawDistSq)
            DrawDebugSphere(World, HP, 20.f, 10, FColor::Green, false, 0.5f);
    }

    if (bDrawDebug && Comp->bDrawExplosion &&
        FVector::DistSquared(Attack.Target, CamPos) <= DrawDistSq)
    {
        DrawDebugSphere(World, Attack.Target, Attack.HitRadius,
            24, FColor::Red, false, Attack.VisualDuration, 0, 2.f);
        DrawDebugCircle(World, Attack.Target, Attack.HitRadius,
            32, FColor::Red, false, Attack.VisualDuration, 0, 2.f,
            FVector(1,0,0), FVector(0,1,0));
    }
}

// ── 死亡倒计时（内联辅助，空闲快速退出路径和主路径共用）─────────

static void TickDeathLinger(
    FMassEntityQuery& Query,
    FMassExecutionContext& Context,
    UFlowFieldAttackComponent* Comp,
    float DeltaTime)
{
    Query.ForEachEntityChunk(Context,
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
                    Comp->GetPendingDestroys().AddUnique(Handles[i].Index);
                else
                {
                    Agent.DeathTimer += DeltaTime;
                    if (Agent.DeathTimer >= Agent.DeathLingerTime)
                        Comp->GetPendingDestroys().AddUnique(Handles[i].Index);
                }
            }
        });
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

    // ── 空闲快速退出 ─────────────────────────────────────────────
    const bool bHasWork =
        Comp->GetActiveAttacks().Num()   > 0 ||
        Comp->GetPendingFires().Num()    > 0 ||
        Comp->GetPendingKills().Num()    > 0 ||
        Comp->GetPendingDestroys().Num() > 0 ||
        !Comp->GetDoTMap().IsEmpty()     ||
        !Comp->GetPendingSlows().IsEmpty() ||
        !Comp->GetPendingStuns().IsEmpty();
    if (!bHasWork)
    {
        TickDeathLinger(DeathLingerQuery, Context, Comp, DeltaTime);
        if (Comp->GetPendingDestroys().Num() == 0) return;
    }

    // ── 0. 调试相机位置 ──────────────────────────────────────────
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

    // ── 1. 构建空间缓存 + 更新存活实体 Handle 映射 ───────────────
    FFlowFieldSpatialCache Cache;
    Cache.Reset();
    AliveHandleCache.Reset();

    EntityQuery.ForEachEntityChunk(Context,
        [&](FMassExecutionContext& ChunkCtx)
        {
            const auto Transforms = ChunkCtx.GetFragmentView<FTransformFragment>();
            auto Agents = ChunkCtx.GetMutableFragmentView<FFlowFieldAgentFragment>();
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

                // 减速/眩晕倒计时
                FFlowFieldAgentFragment& Ag = Agents[i];
                if (Ag.SlowTimeRemaining > 0.f)
                {
                    Ag.SlowTimeRemaining -= DeltaTime;
                    if (Ag.SlowTimeRemaining <= 0.f)
                        { Ag.SlowTimeRemaining = 0.f; Ag.SlowFactor = 1.f; }
                }
                if (Ag.StunTimeRemaining > 0.f)
                {
                    Ag.StunTimeRemaining -= DeltaTime;
                    if (Ag.StunTimeRemaining < 0.f) Ag.StunTimeRemaining = 0.f;
                }
            }
        });

    // ── 2. 更新死亡实体 Handle 映射 ──────────────────────────────
    DeadHandleCache.Reset();
    DeathLingerQuery.ForEachEntityChunk(Context,
        [&](FMassExecutionContext& ChunkCtx)
        {
            const TConstArrayView<FMassEntityHandle> Handles = ChunkCtx.GetEntities();
            for (int32 i = 0; i < ChunkCtx.GetNumEntities(); ++i)
                DeadHandleCache.Add(Handles[i].Index, Handles[i]);
        });

    // ── 3. 待击杀实体 ────────────────────────────────────────────
    {
        auto& Kills = Comp->GetPendingKills();
        for (int32 EntityId : Kills)
        {
            if (FMassEntityHandle* H = AliveHandleCache.Find(EntityId))
            {
                if (EntityManager.IsEntityValid(*H))
                {
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

    // ── 4. 即时命中（Laser / Chain / Explosion PendingFires）─────
    TArray<const FFlowFieldSpatialCache::FEntry*> SharedHits;
    auto& PendingFires = Comp->GetPendingFires();
    for (FFlowFieldActiveAttack& Attack : PendingFires)
    {
        if (!Attack.bActive) continue;
        switch (Attack.Type)
        {
        case EFlowFieldAttackTypeTag::Laser:
            ProcessLaser(Attack, Cache, Comp, FlowSub, World,
                bAnyDebug, DrawDistSq, CamPos, SharedHits);
            break;
        case EFlowFieldAttackTypeTag::Chain:
            ProcessChain(Attack, Cache, Comp, FlowSub, World,
                bAnyDebug, DrawDistSq, CamPos, SharedHits);
            break;
        case EFlowFieldAttackTypeTag::Explosion:
            ProcessExplosion(Attack, Cache, Comp, FlowSub, World,
                bAnyDebug, DrawDistSq, CamPos, SharedHits);
            break;
        default: break;
        }
        Attack.bActive = Attack.VisualDuration > 0.f; // 有视觉时长才保留
    }
    // 有视觉时长的移入 ActiveAttacks 继续倒计时
    for (FFlowFieldActiveAttack& A : PendingFires)
        if (A.bActive)
            Comp->GetActiveAttacks().Add(MoveTemp(A));
    PendingFires.Reset();

    // ── 5. 飞行体推进 + 命中检测 ─────────────────────────────────
    auto& ActiveAttacks = Comp->GetActiveAttacks();
    TArray<const FFlowFieldSpatialCache::FEntry*> Hits;
    for (FFlowFieldActiveAttack& Attack : ActiveAttacks)
    {
        if (!Attack.bActive) continue;

        if (Attack.Type == EFlowFieldAttackTypeTag::Projectile)
        {
            Attack.ElapsedTime += DeltaTime;
            const float T = FMath::Clamp(
                Attack.ElapsedTime / FMath::Max(Attack.TotalTime, 0.001f), 0.f, 1.f);
            Attack.CurrentPos = FMath::Lerp(Attack.Origin, Attack.Target, T);

            Hits.Reset();
            Cache.QueryRadius(Attack.CurrentPos, Attack.HitRadius, Hits);
            for (const auto* E : Hits)
            {
                if (Attack.bPiercing)
                {
                    if (Attack.HitEntityIds.Contains(E->EntityId)) continue;
                    Attack.HitEntityIds.Add(E->EntityId);
                }
                ApplyHit(Comp, FlowSub, Attack, *E, Attack.CurrentPos);

                if (bAnyDebug && Comp->bDrawHitPoints &&
                    FVector::DistSquared(Attack.CurrentPos, CamPos) <= DrawDistSq)
                    DrawDebugSphere(World, Attack.CurrentPos,
                        20.f, 10, FColor::Green, false, 0.5f);

                if (!Attack.bPiercing) { Attack.bActive = false; break; }
            }

            if (Attack.bActive && T >= 1.f) Attack.bActive = false;

            // 调试：轨迹线（黄色）
            if (bAnyDebug && Comp->bDrawProjectileTrajectory &&
                FVector::DistSquared(Attack.Origin, CamPos) <= DrawDistSq)
                DrawDebugLine(World, Attack.Origin, Attack.Target,
                    FColor::Yellow, false, 0.f, 0, 1.5f);

            // 调试：箭头（橙色）
            if (bAnyDebug && Comp->bDrawProjectileArrow &&
                FVector::DistSquared(Attack.CurrentPos, CamPos) <= DrawDistSq)
            {
                const FVector Dir = (Attack.Target - Attack.Origin).GetSafeNormal();
                DrawDebugDirectionalArrow(World,
                    Attack.CurrentPos - Dir * 40.f,
                    Attack.CurrentPos + Dir * 40.f,
                    40.f, FColor::Orange, false, 0.f, 0, 3.f);
            }
        }
        else
        {
            // 激光/连锁/爆炸：仅维持视觉时长
            Attack.ElapsedTime += DeltaTime;
            if (Attack.ElapsedTime >= Attack.VisualDuration)
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

    // ── 6. DoT tick ──────────────────────────────────────────────
    for (auto& [EntityId, DoTs] : Comp->GetDoTMap())
    {
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
                Comp->BroadcastDoTTick(DoT.AttackId, EntityId, Actor,
                    DoT.DamagePerSec * DoT.DotInterval);
            }
        }
        DoTs.RemoveAll([](const FFlowFieldDoTEntry& E) { return E.TimeRemaining <= 0.f; });
    }
    for (auto It = Comp->GetDoTMap().CreateIterator(); It; ++It)
        if (It.Value().IsEmpty()) It.RemoveCurrent();

    // ── 6.5. 将本帧累积的减速/眩晕写入实体 Fragment ─────────────
    if (!Comp->GetPendingSlows().IsEmpty() || !Comp->GetPendingStuns().IsEmpty())
    {
        EntityQuery.ForEachEntityChunk(Context,
            [&](FMassExecutionContext& ChunkCtx)
            {
                auto Agents = ChunkCtx.GetMutableFragmentView<FFlowFieldAgentFragment>();
                const TConstArrayView<FMassEntityHandle> Handles = ChunkCtx.GetEntities();
                const int32 Num = ChunkCtx.GetNumEntities();
                for (int32 i = 0; i < Num; ++i)
                {
                    const int32 Id = Handles[i].Index;
                    FFlowFieldAgentFragment& Ag = Agents[i];

                    if (const auto* Slow = Comp->GetPendingSlows().Find(Id))
                    {
                        Ag.SlowFactor        = FMath::Min(Ag.SlowFactor, Slow->Factor);
                        Ag.SlowTimeRemaining = FMath::Max(Ag.SlowTimeRemaining, Slow->Duration);
                    }
                    if (const float* StunDur = Comp->GetPendingStuns().Find(Id))
                        Ag.StunTimeRemaining = FMath::Max(Ag.StunTimeRemaining, *StunDur);
                }
            });
        Comp->GetPendingSlows().Reset();
        Comp->GetPendingStuns().Reset();
    }

    // ── 7. 死亡倒计时 ────────────────────────────────────────────
    TickDeathLinger(DeathLingerQuery, Context, Comp, DeltaTime);

    // ── 8. 待销毁实体 ────────────────────────────────────────────
    {
        auto& Destroys = Comp->GetPendingDestroys();
        for (int32 EntityId : Destroys)
        {
            FMassEntityHandle Handle;
            bool bFound = false;
            if (FMassEntityHandle* HA = AliveHandleCache.Find(EntityId))
                { Handle = *HA; bFound = true; }
            else if (FMassEntityHandle* HD = DeadHandleCache.Find(EntityId))
                { Handle = *HD; bFound = true; }

            if (bFound && EntityManager.IsEntityValid(Handle))
            {
                Comp->GetDoTMap().Remove(EntityId);
                Comp->BroadcastEntityDestroyed(EntityId);
                Context.Defer().DestroyEntity(Handle);
            }
        }
        Destroys.Reset();
    }
}
