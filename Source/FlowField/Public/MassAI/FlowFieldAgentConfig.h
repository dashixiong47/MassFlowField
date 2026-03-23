#pragma once
#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "FlowFieldAgentConfig.generated.h"

/**
 * FFlowFieldAgentConfig — 由 Trait 写入、运行时只读的配置数据。
 * 同配置（相同 Trait 参数）的实体共享一份，节省内存和缓存压力。
 */
USTRUCT()
struct FLOWFIELD_API FFlowFieldAgentConfig : public FMassConstSharedFragment
{
    GENERATED_BODY()

    // ── 移动 ──────────────────────────────────────────────────────
    UPROPERTY() float MoveSpeed    = 300.f;
    UPROPERTY() float AgentRadius  = 60.f;
    UPROPERTY() float DirSmoothing = 10.f;

    // ── RVO2 ──────────────────────────────────────────────────────
    UPROPERTY() float RVOTimeHorizon  = 1.5f;
    UPROPERTY() float RVONeighborDist = 0.f;
    UPROPERTY() int32 RVOMaxNeighbors = 10;

    // ── 人群压力 ──────────────────────────────────────────────────
    UPROPERTY() float CrowdSpeedMin         = 0.25f;
    UPROPERTY() int32 CrowdDensityFullAt    = 8;
    UPROPERTY() float CrowdInertiaSmoothing = 2.f;

    // ── 生命值 ────────────────────────────────────────────────────
    UPROPERTY() float MaxHP = 100.f;

    // ── 攻击配置 ──────────────────────────────────────────────────
    UPROPERTY() float AttackRange    = 120.f;
    UPROPERTY() float AttackInterval = 1.0f;
    UPROPERTY() float AttackDamage   = 10.f;

    // ── 感知配置 ──────────────────────────────────────────────────
    UPROPERTY() float DetectRadius = 500.f;
    UPROPERTY() float ForgetTime   = 2.0f;

    // ── 死亡 ──────────────────────────────────────────────────────
    UPROPERTY() bool  bAutoDestroy    = true;
    UPROPERTY() float DeathLingerTime = 0.f;

    // ── 地面贴合 ──────────────────────────────────────────────────
    UPROPERTY() float SurfaceZSmoothSpeed = 10.f;
};
