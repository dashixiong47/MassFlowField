#pragma once
#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "MassEntityTemplateRegistry.h"
#include "MassReplicationFragments.h"
#include "MassRepresentationFragments.h"
#include "MassAI/FlowFieldAgentFragment.h"
#include "MassReplicationSubsystem.h"
#include "MassReplication/FlowFieldAgentReplicator.h"
#include "FlowFieldAgentTrait.generated.h"

UCLASS(meta = (DisplayName = "FlowField 智能体"))
class FLOWFIELD_API UFlowFieldAgentTrait : public UMassEntityTraitBase
{
    GENERATED_BODY()

public:
    // ── 移动 ──────────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, Category="FlowField|移动",
        meta=(ClampMin="0", DisplayName="移动速度（cm/s）"))
    float MoveSpeed = 300.f;

    UPROPERTY(EditAnywhere, Category="FlowField|移动",
        meta=(ClampMin="0", ClampMax="30", DisplayName="方向平滑速度"))
    float DirSmoothing = 10.f;

    UPROPERTY(EditAnywhere, Category="FlowField|移动",
        meta=(ClampMin="1", DisplayName="碰撞半径（cm）"))
    float AgentRadius = 60.f;

    // ── RVO2 避障 ─────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, Category="FlowField|RVO避障",
        meta=(ClampMin="0.1", ClampMax="5.0", DisplayName="预测时间窗（s）",
              ToolTip="越大越平滑，但群体聚集/分散速度略慢。推荐 1.0~2.0"))
    float RVOTimeHorizon = 1.5f;

    UPROPERTY(EditAnywhere, Category="FlowField|RVO避障",
        meta=(ClampMin="0", DisplayName="感知范围（cm，0=自动 AgentRadius×5）"))
    float RVONeighborDist = 0.f;

    UPROPERTY(EditAnywhere, Category="FlowField|RVO避障",
        meta=(ClampMin="1", ClampMax="30", DisplayName="最大感知邻居数"))
    int32 RVOMaxNeighbors = 10;

    // ── 地面贴合 ──────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, Category="FlowField|地面贴合",
        meta=(ClampMin="0.1", DisplayName="高度平滑速度"))
    float SurfaceZSmoothSpeed = 10.f;

    virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
