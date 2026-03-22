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
    UPROPERTY() float   AgentRadius  = 60.f;
    UPROPERTY() FVector CurrentDir   = FVector::ZeroVector;
    UPROPERTY() float   DirSmoothing = 10.f;

    // ── RVO2 ──────────────────────────────────────────────────────
    UPROPERTY() int32   RVOAgentId           = -1;              // RVO sim 内的 agent 索引
    UPROPERTY() FVector RVOComputedVelocity  = FVector::ZeroVector; // RVO 输出速度（cm/s）
    UPROPERTY() FVector SmoothedMoveVelocity = FVector::ZeroVector; // 平滑后的移动速度，消抖用
    UPROPERTY() bool    bInStopZone          = false;               // 已贴墙停止，朝向障碍

    // RVO 参数（由 Trait 写入，运行时只读）
    UPROPERTY() float   RVOTimeHorizon       = 1.5f;  // 预测碰撞的时间窗（s），越大越平滑但分散越慢
    UPROPERTY() float   RVONeighborDist      = 0.f;   // 感知范围（cm），0 = 运行时自动算 AgentRadius*5
    UPROPERTY() int32   RVOMaxNeighbors      = 10;    // 最多感知几个邻居

    // 上一帧已知目标位置，用于检测目标切换
    // 注意：不能用 FLT_MAX，WorldToCell 会整数溢出导致切换检测永远失效
    UPROPERTY() FVector LastKnownGoal = FVector(-99999999.f, -99999999.f, 0.f);

    // ── 地面贴合 ──────────────────────────────────────────────────
    UPROPERTY() float   SmoothedSurfaceZ    = 0.f;
    UPROPERTY() bool    bSurfaceInitialized = false;
    UPROPERTY() float   SurfaceZSmoothSpeed = 10.f;

    // ── 服务端位置校正 ────────────────────────────────────────────
    UPROPERTY() FVector CorrectionTargetLocation = FVector::ZeroVector;
    UPROPERTY() float   CorrectionTargetYaw      = 0.f;
    UPROPERTY() bool    bHasCorrection           = false;

    // ── 击退 ──────────────────────────────────────────────────────
    UPROPERTY() FVector KnockbackVelocity  = FVector::ZeroVector; // 当前击退速度（cm/s）
    UPROPERTY() float   KnockbackDecay     = 5.f;                 // 衰减系数，越大停得越快
    UPROPERTY() bool    bIsKnockedBack     = false;               // 是否正在被击退

    // ── 追踪目标 ──────────────────────────────────────────────────
    UPROPERTY() bool    bAtWall         = false;               // 是否已贴附目标障碍物包围圈（攻城模式）
    UPROPERTY() bool    bChasingTarget  = false;               // 是否正在追踪动态目标
    UPROPERTY() FVector ChaseTargetPos  = FVector::ZeroVector; // 当前追踪的目标世界位置
    UPROPERTY() bool    bInAttackRange  = false;               // 是否进入攻击距离
    UPROPERTY() float   AttackRange     = 120.f;               // 攻击距离（cm），由 Trait 写入

    // ── 感知配置（由 Trait 写入）─────────────────────────────────
    UPROPERTY() float   DetectRadius    = 500.f; // AI 感知/追踪范围（cm），替代目标侧 ChaseRadius
    UPROPERTY() float   ForgetTime      = 2.0f;  // 离开感知范围后继续追踪的时间（s），0 = 立即遗忘

    // ── 攻击配置（由 Trait 写入）─────────────────────────────────
    UPROPERTY() float   AttackInterval  = 1.0f;  // 攻击间隔（s）
    UPROPERTY() float   AttackDamage    = 10.f;  // 每次伤害

    // ── 事件系统运行时状态（由各 Processor 写入）─────────────────
    UPROPERTY() float   AttackTimer      = 0.f;   // 攻击/障碍攻击累计计时（s）
    UPROPERTY() float   ForgetTimer      = 0.f;   // 遗忘倒计时（s）
    UPROPERTY() int32   TargetIndex      = -1;    // 当前/上次追踪的 TrackedTarget 索引
    UPROPERTY() int32   ObstacleIndex    = -1;    // 当前/上次贴附的障碍索引
    UPROPERTY() bool    bInChaseRange    = false; // 当帧物理上是否在 DetectRadius 内（RVO 写）

    // 上帧状态快照，用于检测 Enter/Exit 事件
    UPROPERTY() bool    bWasChasingTarget  = false;
    UPROPERTY() bool    bWasInAttackRange  = false;
    UPROPERTY() bool    bWasAtWall         = false;
    UPROPERTY() bool    bWasInChaseRange   = false; // 用于物理进出范围事件
};