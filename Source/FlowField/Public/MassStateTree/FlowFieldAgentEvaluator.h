#pragma once
#include "CoreMinimal.h"
#include "MassStateTreeTypes.h"
#include "MassCommonFragments.h"
#include "MassAI/FlowFieldAgentFragment.h"
#include "MassAI/FlowFieldAgentConfig.h"
#include "FlowFieldAgentEvaluator.generated.h"

/**
 * 求值器输出数据（per-entity 实例数据）
 * StateTree 中的 Task 和 Condition 可绑定到这些 Output 属性。
 * 可扩展：项目层若需要额外字段，继承本结构并配合子类求值器使用。
 */
USTRUCT(BlueprintType)
struct FLOWFIELD_API FFlowFieldAgentEvaluatorInstanceData
{
    GENERATED_BODY()

    // ── 变换 ──────────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, Category="FlowField|输出",
        meta=(Output, DisplayName="世界位置"))
    FVector WorldPosition = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, Category="FlowField|输出",
        meta=(Output, DisplayName="世界旋转"))
    FRotator WorldRotation = FRotator::ZeroRotator;

    // ── 移动 ──────────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, Category="FlowField|输出",
        meta=(Output, DisplayName="移动速度 (cm/s)"))
    float MoveSpeed = 0.f;

    UPROPERTY(EditAnywhere, Category="FlowField|输出",
        meta=(Output, DisplayName="当前移动方向"))
    FVector CurrentDir = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, Category="FlowField|输出",
        meta=(Output, DisplayName="平滑移动速度向量"))
    FVector SmoothedMoveVelocity = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, Category="FlowField|输出",
        meta=(Output, DisplayName="RVO 计算速度"))
    FVector RVOComputedVelocity = FVector::ZeroVector;

    // ── 状态标志 ──────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, Category="FlowField|输出",
        meta=(Output, DisplayName="正在被击退"))
    bool bIsKnockedBack = false;

    UPROPERTY(EditAnywhere, Category="FlowField|输出",
        meta=(Output, DisplayName="击退速度向量"))
    FVector KnockbackVelocity = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, Category="FlowField|输出",
        meta=(Output, DisplayName="在停止区域（贴墙或到达目标）"))
    bool bInStopZone = false;

    UPROPERTY(EditAnywhere, Category="FlowField|输出",
        meta=(Output, DisplayName="正在追踪动态目标"))
    bool bChasingTarget = false;

    UPROPERTY(EditAnywhere, Category="FlowField|输出",
        meta=(Output, DisplayName="追踪目标世界位置"))
    FVector ChaseTargetPos = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, Category="FlowField|输出",
        meta=(Output, DisplayName="在攻击距离内"))
    bool bInAttackRange = false;

    // ── 地面 ──────────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, Category="FlowField|输出",
        meta=(Output, DisplayName="平滑后地面 Z"))
    float SmoothedSurfaceZ = 0.f;
};

/**
 * FlowField 智能体求值器（Mass StateTree Evaluator）
 *
 * 每帧从 FFlowFieldAgentFragment + FTransformFragment 读取数据，
 * 填写 FFlowFieldAgentEvaluatorInstanceData，供同 StateTree 中的 Task/Condition 绑定。
 *
 * 可扩展：
 *   1. 在项目层继承 FFlowFieldAgentEvaluatorInstanceData，追加自定义 Output 字段
 *   2. 继承 FFlowFieldAgentEvaluator，在 Link() 中补充 LinkExternalData()，
 *      在 Tick() 中先调用 Super，再填写额外字段
 */
USTRUCT(meta = (DisplayName = "FlowField 智能体求值器"))
struct FLOWFIELD_API FFlowFieldAgentEvaluator : public FMassStateTreeEvaluatorBase
{
    GENERATED_BODY()

    using FInstanceDataType = FFlowFieldAgentEvaluatorInstanceData;

    virtual bool Link(FStateTreeLinker& Linker) override;
    virtual void Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
    virtual const UStruct* GetInstanceDataType() const override
    {
        return FInstanceDataType::StaticStruct();
    }

protected:
    /** 子类可在 Link() 中继续使用这些 Handle */
    TStateTreeExternalDataHandle<FTransformFragment>      TransformHandle;
    TStateTreeExternalDataHandle<FFlowFieldAgentFragment> AgentHandle;
};
