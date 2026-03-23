#include "MassAI/FlowFieldAgentTrait.h"
#include "MassAI/FlowFieldAgentConfig.h"
#include "VAT/FlowFieldVATFragment.h"
#include "MassCommonFragments.h"
#include "MassActorSubsystem.h"
#include "MassEntityUtils.h"
#include "MassRepresentationSubsystem.h"
#include "MassRepresentationTypes.h"
#include "Engine/World.h"

void UFlowFieldAgentTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
    // ── ConstShared Config（同配置实体共享一份）──────────────────
    FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(World);

    FFlowFieldAgentConfig Config;
    Config.MoveSpeed             = MoveSpeed;
    Config.AgentRadius           = AgentRadius;
    Config.DirSmoothing          = DirSmoothing;
    Config.RVOTimeHorizon        = RVOTimeHorizon;
    Config.RVONeighborDist       = (RVONeighborDist > 0.f) ? RVONeighborDist : AgentRadius * 5.f;
    Config.RVOMaxNeighbors       = RVOMaxNeighbors;
    Config.SurfaceZSmoothSpeed   = SurfaceZSmoothSpeed;
    Config.DetectRadius          = DetectRadius;
    Config.ForgetTime            = ForgetTime;
    Config.AttackRange           = AttackRange;
    Config.AttackInterval        = AttackInterval;
    Config.AttackDamage          = AttackDamage;
    Config.CrowdSpeedMin         = CrowdSpeedMin;
    Config.CrowdDensityFullAt    = CrowdDensityFullAt;
    Config.CrowdInertiaSmoothing = CrowdInertiaSmoothing;
    Config.bAutoDestroy          = bAutoDestroy;
    Config.DeathLingerTime       = DeathLingerTime;
    // MaxHP、KnockbackDecay、KnockbackStaggerDuration 使用默认值（Trait 暂不暴露这些）

    FConstSharedStruct ConfigStruct = EntityManager.GetOrCreateConstSharedFragment(Config);
    BuildContext.AddConstSharedFragment(ConfigStruct);

    // ── 运行时 Fragment（初始值，Processor 运行时写入）──────────
    BuildContext.AddFragment<FFlowFieldAgentFragment>();

    BuildContext.AddTag<FFlowFieldAgentTag>();
    BuildContext.AddTag<FFlowFieldMovingTag>();
    BuildContext.AddFragment<FTransformFragment>();

    // ── VAT 渲染 ──────────────────────────────────────────────────
    if (bUseVATRendering && !VATDataAsset.IsNull()
        && !World.IsNetMode(NM_DedicatedServer)
        && !BuildContext.IsInspectingData())
    {
        UFlowFieldVATDataAsset* Asset = VATDataAsset.LoadSynchronous();
        if (!Asset || !Asset->Mesh)
        {
            UE_LOG(LogTemp, Warning, TEXT("FlowFieldAgentTrait: VATDataAsset or its Mesh is null, falling back to actor mode."));
            BuildContext.AddFragment<FMassActorFragment>();
            return;
        }

        // ── 向 MassRepresentationSubsystem 注册 ISM 描述 ──────────
        UMassRepresentationSubsystem* RepSub =
            World.GetSubsystem<UMassRepresentationSubsystem>();
        if (!RepSub)
        {
            UE_LOG(LogTemp, Warning, TEXT("FlowFieldAgentTrait: UMassRepresentationSubsystem not found, falling back to actor mode."));
            BuildContext.AddFragment<FMassActorFragment>();
            return;
        }

        // 构建 FStaticMeshInstanceVisualizationDesc
        FStaticMeshInstanceVisualizationDesc VisDesc;

        // 主 Mesh（VAT，近距离）
        FMassStaticMeshInstanceVisualizationMeshDesc VATMeshDesc;
        VATMeshDesc.Mesh = Asset->Mesh;
        if (Asset->Material)
        {
            VATMeshDesc.MaterialOverrides.Add(Asset->Material);
        }
        VATMeshDesc.bCastShadows  = true;
        VATMeshDesc.Mobility      = EComponentMobility::Movable;
        // LOD 参数全部从 DataAsset 读取
        const bool bHasLODMesh = Asset->bUseLODMesh
            && (Asset->LODMesh != nullptr)
            && (Asset->LODSwitchDistance > 0.f);
        VATMeshDesc.MinLODSignificance = float(EMassLOD::High);   // 0
        VATMeshDesc.MaxLODSignificance = bHasLODMesh
            ? float(EMassLOD::Low)  // 2（切换后由 LOD 网格接管）
            : float(EMassLOD::Off); // 3（没有 LOD 网格时，直到剔除距离都显示 VAT）
        VisDesc.Meshes.Add(VATMeshDesc);

        // 可选：远景低精度网格体（Low LOD 范围）
        if (bHasLODMesh)
        {
            FMassStaticMeshInstanceVisualizationMeshDesc LODMeshDesc;
            LODMeshDesc.Mesh = Asset->LODMesh;
            if (Asset->LODMeshMaterial)
            {
                LODMeshDesc.MaterialOverrides.Add(Asset->LODMeshMaterial);
            }
            LODMeshDesc.bCastShadows  = false;
            LODMeshDesc.Mobility      = EComponentMobility::Movable;
            LODMeshDesc.MinLODSignificance = float(EMassLOD::Low); // 2
            LODMeshDesc.MaxLODSignificance = float(EMassLOD::Off); // 3
            VisDesc.Meshes.Add(LODMeshDesc);
        }

        // 初始 CustomData：1 个 float（动画帧索引）
        VisDesc.CustomDataFloats.Add(0.f);

        // 注册，获得 Handle
        FMassRepresentationFragment& RepFrag = BuildContext.AddFragment_GetRef<FMassRepresentationFragment>();
        RepFrag.StaticMeshDescHandle = RepSub->FindOrAddStaticMeshDesc(VisDesc);

        // FMassRepresentationSubsystemSharedFragment（Shared，游戏线程）
        FMassRepresentationSubsystemSharedFragment SubShared;
        SubShared.RepresentationSubsystem = RepSub;
        FSharedStruct SubSharedStruct = EntityManager.GetOrCreateSharedFragment(SubShared);
        BuildContext.AddSharedFragment(SubSharedStruct);

        // FMassRepresentationLODFragment — 存储每帧 LODSignificance
        BuildContext.AddFragment<FMassRepresentationLODFragment>();

        // VAT 动画状态 Fragment
        FFlowFieldVATFragment& VATFrag = BuildContext.AddFragment_GetRef<FFlowFieldVATFragment>();
        VATFrag.DataAsset   = Asset;
        VATFrag.AnimationID = 0;
        VATFrag.AnimTime    = 0.f;
        VATFrag.PlayRate    = 1.f;

        // LOD 距离配置（ConstShared，同 Trait 配置的实体共享）
        FFlowFieldVATSharedData VATShared;
        VATShared.LODSwitchDistance = bHasLODMesh ? Asset->LODSwitchDistance : 0.f;
        // 未启用距离剔除时设为极大值，处理器中 MinDistSq 永远 < CullDistSq
        VATShared.CullDistance      = Asset->bEnableDistanceCulling
            ? FMath::Max(Asset->CullDistance, 100.f)
            : 1e9f;
        FConstSharedStruct VATSharedStruct = EntityManager.GetOrCreateConstSharedFragment(VATShared);
        BuildContext.AddConstSharedFragment(VATSharedStruct);

        // Tag：告知 UMassUpdateISMProcessor 等不要处理此实体（它们不需要 FMassVisualizationChunkFragment，
        // 实际上已因缺少该 ChunkFragment 而不匹配，但加 FMassStaticRepresentationTag 更明确）
        BuildContext.AddTag<FFlowFieldVATTag>();
    }
    else
    {
        // 非 VAT 路径：使用 Mass 内置 Actor 代理
        BuildContext.AddFragment<FMassActorFragment>();
    }
}
