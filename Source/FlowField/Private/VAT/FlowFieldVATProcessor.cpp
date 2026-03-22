#include "VAT/FlowFieldVATProcessor.h"
#include "VAT/FlowFieldVATFragment.h"
#include "MassExecutionContext.h"
#include "MassEntityManager.h"
#include "MassCommonFragments.h"
#include "MassRepresentationFragments.h"
#include "MassRepresentationTypes.h"
#include "MassRepresentationSubsystem.h"
#include "MassLODSubsystem.h"
#include "Engine/World.h"

UFlowFieldVATProcessor::UFlowFieldVATProcessor()
{
    ProcessingPhase = EMassProcessingPhase::PostPhysics;
    bRequiresGameThreadExecution = true; // RepresentationSubsystem SharedFragment 要求游戏线程
    ExecutionFlags = (int32)(EProcessorExecutionFlags::Client
                           | EProcessorExecutionFlags::Standalone);
    bAutoRegisterWithProcessingPhases = true;

    // 在官方 Representation 组之后运行（与 UMassUpdateISMProcessor 同级）
    ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Representation);
}

void UFlowFieldVATProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
    EntityQuery.Initialize(EntityManager);
    EntityQuery.AddTagRequirement<FFlowFieldVATTag>(EMassFragmentPresence::All);
    EntityQuery.AddRequirement<FFlowFieldVATFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
    EntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddRequirement<FMassRepresentationLODFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddConstSharedRequirement<FFlowFieldVATSharedData>();
    EntityQuery.AddSharedRequirement<FMassRepresentationSubsystemSharedFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.RegisterWithProcessor(*this);
    RegisterQuery(EntityQuery);
}

void UFlowFieldVATProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
    UWorld* World = GetWorld();
    if (!World) return;

    // ── 收集观察者位置（用于 LOD 距离计算）──────────────────────
    TArray<FVector> ViewerLocations;
    if (const UMassLODSubsystem* LODSub = World->GetSubsystem<UMassLODSubsystem>())
    {
        for (const FViewerInfo& Viewer : LODSub->GetViewers())
        {
            if (Viewer.bEnabled)
            {
                ViewerLocations.Add(Viewer.Location);
            }
        }
    }
    // Fallback：无观察者时使用原点（所有实体均会被渲染）
    if (ViewerLocations.IsEmpty())
    {
        ViewerLocations.Add(FVector::ZeroVector);
    }

    const float DeltaTime = Context.GetDeltaTimeSeconds();

    EntityQuery.ForEachEntityChunk(Context,
        [&](FMassExecutionContext& ChunkContext)
        {
            UMassRepresentationSubsystem* RepSub =
                ChunkContext.GetMutableSharedFragment<FMassRepresentationSubsystemSharedFragment>().RepresentationSubsystem;
            if (!RepSub) return;

            const FFlowFieldVATSharedData& VATShared =
                ChunkContext.GetConstSharedFragment<FFlowFieldVATSharedData>();
            const float CullDistSq      = VATShared.CullDistance * VATShared.CullDistance;
            const float LODSwitchDistSq = (VATShared.LODSwitchDistance > 0.f)
                ? VATShared.LODSwitchDistance * VATShared.LODSwitchDistance
                : -1.f;

            // 获取本帧 ISMInfo 数组（RAII guard，持有读写锁直到析构）
            FMassInstancedStaticMeshInfoArrayView ISMInfosView =
                RepSub->GetMutableInstancedStaticMeshInfos();

            auto VATFrags        = ChunkContext.GetMutableFragmentView<FFlowFieldVATFragment>();
            auto Transforms      = ChunkContext.GetFragmentView<FTransformFragment>();
            auto RepFrags        = ChunkContext.GetMutableFragmentView<FMassRepresentationFragment>();
            auto RepLODFrags     = ChunkContext.GetMutableFragmentView<FMassRepresentationLODFragment>();
            const int32 Num      = ChunkContext.GetNumEntities();

            for (int32 i = 0; i < Num; ++i)
            {
                FFlowFieldVATFragment&        VATFrag  = VATFrags[i];
                FMassRepresentationFragment&  RepFrag  = RepFrags[i];
                FMassRepresentationLODFragment& LODFrag = RepLODFrags[i];
                const FTransform& WorldTransform = Transforms[i].GetTransform();
                const FVector& Pos = WorldTransform.GetLocation();

                // ── 计算到最近观察者的距离²──────────────────────────
                float MinDistSq = FLT_MAX;
                for (const FVector& VL : ViewerLocations)
                {
                    MinDistSq = FMath::Min(MinDistSq, FVector::DistSquared(Pos, VL));
                }

                // ── 映射距离到 LODSignificance ────────────────────────
                // High(0)~Low(2)：VAT 网格
                // Low(2)~Off(3)：低精度网格（若配置）
                // Off(3)：剔除
                float LODSig;
                if (MinDistSq >= CullDistSq)
                {
                    LODSig = float(EMassLOD::Off); // = 3.f，超出剔除距离
                }
                else if (LODSwitchDistSq > 0.f && MinDistSq >= LODSwitchDistSq)
                {
                    LODSig = 2.5f; // Low 范围 → 低精度网格
                }
                else
                {
                    LODSig = 0.5f; // High 范围 → VAT 网格
                }

                const float PrevLODSig = (RepFrag.PrevLODSignificance >= 0.f
                    && RepFrag.PrevLODSignificance < float(EMassLOD::Off))
                    ? RepFrag.PrevLODSignificance
                    : -1.f; // -1 表示无有效的上一帧范围（初始/从剔除恢复）

                // ── 更新 LOD Fragment ────────────────────────────────
                LODFrag.LODSignificance = LODSig;

                if (LODSig >= float(EMassLOD::Off))
                {
                    // 超出剔除距离：不提交到批次，EndVisualChanges 会自动移除 ISM 实例
                    RepFrag.CurrentRepresentation = EMassRepresentationType::None;
                    RepFrag.PrevTransform         = WorldTransform;
                    RepFrag.PrevLODSignificance   = LODSig;
                    continue;
                }

                // ── 可见实体：提交 Transform 到 ISM 批次 ─────────────
                RepFrag.CurrentRepresentation = EMassRepresentationType::StaticMeshInstance;

                const int32 ISMInfoIndex = RepFrag.StaticMeshDescHandle.ToIndex();
                if (!ISMInfosView.IsValidIndex(ISMInfoIndex))
                {
                    continue;
                }
                FMassInstancedStaticMeshInfo& ISMInfo = ISMInfosView[ISMInfoIndex];

                // 应用网格朝向修正（本地旋转偏移，修正模型前向与 +X 不一致的问题）
                FTransform SubmitTransform = WorldTransform;
                if (VATFrag.DataAsset
                    && !VATFrag.DataAsset->MeshRotationOffset.IsNearlyZero())
                {
                    SubmitTransform.SetRotation(
                        WorldTransform.GetRotation() *
                        VATFrag.DataAsset->MeshRotationOffset.Quaternion());
                }

                ISMInfo.AddBatchedTransform(
                    ChunkContext.GetEntity(i),
                    SubmitTransform,
                    RepFrag.PrevTransform,
                    LODSig,
                    PrevLODSig);

                // ── 推进动画时间并提交 CustomData ────────────────────
                float AbsFrame = 0.f;
                if (VATFrag.DataAsset)
                {
                    const FFlowFieldVATAnimation* Anim =
                        VATFrag.DataAsset->GetAnimation(VATFrag.AnimationID);
                    if (Anim && Anim->FPS > 0.f && Anim->FrameCount > 0)
                    {
                        const float AnimDuration = Anim->FrameCount / Anim->FPS;
                        VATFrag.AnimTime += DeltaTime * VATFrag.PlayRate;
                        if (Anim->bLoop)
                        {
                            VATFrag.AnimTime = FMath::Fmod(VATFrag.AnimTime, AnimDuration);
                            if (VATFrag.AnimTime < 0.f) VATFrag.AnimTime += AnimDuration;
                        }
                        else
                        {
                            VATFrag.AnimTime = FMath::Clamp(VATFrag.AnimTime, 0.f, AnimDuration);
                        }
                        const int32 LocalFrame = FMath::FloorToInt(VATFrag.AnimTime * Anim->FPS);
                        AbsFrame = float(Anim->StartFrame + LocalFrame);
                    }
                }

                // 每帧都必须为每个 AddBatchedTransform 的实体提供 CustomData
                ISMInfo.AddBatchedCustomData<float>(AbsFrame, LODSig, PrevLODSig);

                // ── 更新 PrevTransform / PrevLODSignificance ─────────
                RepFrag.PrevTransform       = WorldTransform;
                RepFrag.PrevLODSignificance = LODSig;
            }
        });
}
