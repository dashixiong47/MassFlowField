#pragma once
#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "MassEntityTemplateRegistry.h"
#include "MassReplicationFragments.h"
#include "MassRepresentationFragments.h"
#include "MassAI/FlowFieldAgentFragment.h"
#include "MassAI/FlowFieldBoidsFragment.h"
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

    // ── 分离力（防重叠）──────────────────────────────────────────

    UPROPERTY(EditAnywhere, Category="FlowField|分离力",
        meta=(ClampMin="0", DisplayName="感知半径（cm）"))
    float SeparationRadius = 120.f;

    UPROPERTY(EditAnywhere, Category="FlowField|分离力",
        meta=(ClampMin="0", DisplayName="分离力权重"))
    float SeparationWeight = 1.5f;

    UPROPERTY(EditAnywhere, Category="FlowField|分离力",
        meta=(ClampMin="0.1", ClampMax="30", DisplayName="分离力平滑速度"))
    float SeparationSmoothSpeed = 5.f;

    UPROPERTY(EditAnywhere, Category="FlowField|分离力",
        meta=(ClampMin="1", ClampMax="5", DisplayName="停止时排斥半径倍数"))
    float StoppedRadiusMultiplier = 2.f;

    // ── 地面贴合 ──────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, Category="FlowField|地面贴合",
        meta=(ClampMin="0.1", DisplayName="高度平滑速度"))
    float SurfaceZSmoothSpeed = 10.f;

    UPROPERTY(EditAnywhere, Category="FlowField|地面贴合",
        meta=(ClampMin="0.1", DisplayName="法线平滑速度"))
    float SurfaceNormalSmoothSpeed = 8.f;

    virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override
    {
        FFlowFieldAgentFragment& AgentFrag         = BuildContext.AddFragment_GetRef<FFlowFieldAgentFragment>();
        AgentFrag.MoveSpeed                        = MoveSpeed;
        AgentFrag.DirSmoothing                     = DirSmoothing;
        AgentFrag.SurfaceZSmoothSpeed              = SurfaceZSmoothSpeed;
        AgentFrag.SurfaceNormalSmoothSpeed         = SurfaceNormalSmoothSpeed;

        FFlowFieldBoidsFragment& BoidsFrag         = BuildContext.AddFragment_GetRef<FFlowFieldBoidsFragment>();
        BoidsFrag.SeparationRadius                 = SeparationRadius;
        BoidsFrag.SeparationWeight                 = SeparationWeight;
        BoidsFrag.SeparationSmoothSpeed            = SeparationSmoothSpeed;
        BoidsFrag.StoppedRadiusMultiplier          = StoppedRadiusMultiplier;

        BuildContext.AddTag<FFlowFieldAgentTag>();
        BuildContext.AddTag<FFlowFieldMovingTag>();
    }
};