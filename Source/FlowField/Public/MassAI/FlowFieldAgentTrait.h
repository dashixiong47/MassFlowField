#pragma once
#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "MassEntityTemplateRegistry.h"
#include "MassReplicationFragments.h"
#include "MassRepresentationFragments.h"
#include "MassAI/FlowFieldAgentFragment.h"
#include "MassReplicationSubsystem.h"
#include "MassReplication/FlowFieldAgentReplicator.h"
#include "VAT/FlowFieldVATDataAsset.h"
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

    // ── VAT 渲染 ──────────────────────────────────────────────────

    /**
     * 启用 VAT（顶点动画纹理）渲染模式。
     * 开启后实体使用 UMassRepresentationSubsystem 管理 HISM 实例，
     * 支持 LOD 剔除与多网格切换。请从 Mass 配置中移除 MassVisualizationTrait。
     */
    UPROPERTY(EditAnywhere, Category="FlowField|VAT渲染",
        meta=(DisplayName="启用 VAT 渲染"))
    bool bUseVATRendering = false;

    /** VAT 数据资产（仅 bUseVATRendering=true 时生效） */
    UPROPERTY(EditAnywhere, Category="FlowField|VAT渲染",
        meta=(EditCondition="bUseVATRendering", DisplayName="VAT 数据资产"))
    TSoftObjectPtr<UFlowFieldVATDataAsset> VATDataAsset;

    /**
     * 启用远景低精度网格体。
     * 开启后超过切换距离的实体改用简单网格体渲染，节省 VAT 着色器开销。
     */
    UPROPERTY(EditAnywhere, Category="FlowField|VAT渲染",
        meta=(EditCondition="bUseVATRendering", DisplayName="启用远景 LOD 网格"))
    bool bUseLODMesh = false;

    /**
     * 远景低精度网格体。
     * 超过切换距离后替代 VAT 网格，材质通常比 VAT 材质便宜得多。
     */
    UPROPERTY(EditAnywhere, Category="FlowField|VAT渲染",
        meta=(EditCondition="bUseVATRendering && bUseLODMesh", DisplayName="远景 LOD 网格体"))
    TObjectPtr<UStaticMesh> LODMesh;

    /** 远景 LOD 网格体材质（不填使用网格体默认材质） */
    UPROPERTY(EditAnywhere, Category="FlowField|VAT渲染",
        meta=(EditCondition="bUseVATRendering && bUseLODMesh", DisplayName="远景 LOD 材质"))
    TObjectPtr<UMaterialInterface> LODMeshMaterial;

    /**
     * VAT → LOD 网格切换距离（cm）。
     * 超过此距离后切换到低精度网格。
     */
    UPROPERTY(EditAnywhere, Category="FlowField|VAT渲染",
        meta=(ClampMin="0", EditCondition="bUseVATRendering && bUseLODMesh", DisplayName="LOD 切换距离（cm）"))
    float LODSwitchDistance = 5000.f;

    /**
     * 启用距离剔除。
     * 开启后超过剔除距离的实体会从 ISM 中移除，不占 GPU 资源。
     * 关闭时所有实体始终渲染（适合小规模调试）。
     */
    UPROPERTY(EditAnywhere, Category="FlowField|VAT渲染",
        meta=(EditCondition="bUseVATRendering", DisplayName="启用距离剔除"))
    bool bEnableDistanceCulling = true;

    /** 剔除距离（cm）。超出此距离的实体不渲染，从 ISM 中移除。 */
    UPROPERTY(EditAnywhere, Category="FlowField|VAT渲染",
        meta=(ClampMin="100", EditCondition="bUseVATRendering && bEnableDistanceCulling", DisplayName="剔除距离（cm）"))
    float CullDistance = 10000.f;

    virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
