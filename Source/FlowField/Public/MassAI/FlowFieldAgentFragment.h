#pragma once
#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "FlowFieldAgentFragment.generated.h"

// 标识为 FlowField 管理的 AI 实体
USTRUCT()
struct FLOWFIELD_API FFlowFieldAgentTag : public FMassTag
{
    GENERATED_BODY()
};

// 标识实体当前正在移动（停止时可移除以节省 Tick 开销）
USTRUCT()
struct FLOWFIELD_API FFlowFieldMovingTag : public FMassTag
{
    GENERATED_BODY()
};

USTRUCT()
struct FLOWFIELD_API FFlowFieldAgentFragment : public FMassFragment
{
    GENERATED_BODY()

    // ── 移动 ──────────────────────────────────────────────────────
    UPROPERTY() float   MoveSpeed    = 300.f;
    UPROPERTY() FVector CurrentDir   = FVector::ZeroVector;
    UPROPERTY() float   DirSmoothing = 10.f;

    // 上一帧已知目标位置，用于检测目标切换
    // 注意：不能用 FLT_MAX，WorldToCell 会整数溢出导致切换检测永远失效
    UPROPERTY() FVector LastKnownGoal = FVector(-99999999.f, -99999999.f, 0.f);

    // ── 地面贴合 ──────────────────────────────────────────────────
    UPROPERTY() float   SmoothedSurfaceZ         = 0.f;
    UPROPERTY() FVector SmoothedNormal           = FVector::UpVector;
    UPROPERTY() bool    bSurfaceInitialized      = false;
    UPROPERTY() float   SurfaceZSmoothSpeed      = 10.f;
    UPROPERTY() float   SurfaceNormalSmoothSpeed = 8.f;

    // ── 服务端位置校正 ────────────────────────────────────────────
    UPROPERTY() FVector CorrectionTargetLocation = FVector::ZeroVector;
    UPROPERTY() float   CorrectionTargetYaw      = 0.f;
    UPROPERTY() bool    bHasCorrection           = false;

    // ── 击退 ──────────────────────────────────────────────────────
    UPROPERTY() FVector KnockbackVelocity  = FVector::ZeroVector; // 当前击退速度（cm/s）
    UPROPERTY() float   KnockbackDecay     = 5.f;                 // 衰减系数，越大停得越快
    UPROPERTY() bool    bIsKnockedBack     = false;               // 是否正在被击退
};