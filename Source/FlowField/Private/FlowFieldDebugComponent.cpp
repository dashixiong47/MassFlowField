#include "FlowFieldDebugComponent.h"
#include "PrimitiveSceneProxy.h"
#include "SceneManagement.h"
#include "DynamicMeshBuilder.h"
#include "Engine/Engine.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"

// ─────────────────────────────────────────────────────────────────────────────
// SceneProxy
// ─────────────────────────────────────────────────────────────────────────────
class FFlowFieldDebugSceneProxy final : public FPrimitiveSceneProxy
{
public:
    FFlowFieldDebugSceneProxy(const UPrimitiveComponent* InComponent,
                               TArray<FFlowFieldDebugLine> InLines,
                               TArray<FFlowFieldDebugQuad> InQuads)
        : FPrimitiveSceneProxy(InComponent)
        , Lines(MoveTemp(InLines))
        , Quads(MoveTemp(InQuads))
    {
        bWillEverBeLit = false;
    }

    virtual void GetDynamicMeshElements(
        const TArray<const FSceneView*>& Views,
        const FSceneViewFamily& ViewFamily,
        uint32 VisibilityMap,
        FMeshElementCollector& Collector) const override
    {
        if (!GEngine) return;

        UMaterialInterface* OpaqueMat = GEngine->LevelColorationUnlitMaterial
            ? static_cast<UMaterialInterface*>(GEngine->LevelColorationUnlitMaterial)
            : static_cast<UMaterialInterface*>(GEngine->GeomMaterial);

        for (int32 ViewIdx = 0; ViewIdx < Views.Num(); ++ViewIdx)
        {
            if (!(VisibilityMap & (1 << ViewIdx))) continue;

            // ── 线段 ──────────────────────────────────────────────────
            FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIdx);
            for (const FFlowFieldDebugLine& L : Lines)
                PDI->DrawLine(L.Start, L.End, L.Color, SDPG_World, L.Thickness);

            if (Quads.Num() == 0) continue;

            // 按颜色分组（量化 16 步 → ≤16 DrawCall）
            TMap<uint32, TArray<int32>> ColorGroups;
            ColorGroups.Reserve(32);
            for (int32 i = 0; i < Quads.Num(); i++)
                ColorGroups.FindOrAdd(Quads[i].Color.DWColor()).Add(i);

            const ERHIFeatureLevel::Type FeatureLevel = Views[ViewIdx]->GetFeatureLevel();

            for (auto& Pair : ColorGroups)
            {
                UMaterialInterface* BaseMat = OpaqueMat;
                if (!BaseMat) continue;
                const FColor QuadColor = Quads[Pair.Value[0]].Color;

                const FLinearColor LC = FLinearColor::FromSRGBColor(QuadColor);

                FDynamicMeshBuilder MeshBuilder(FeatureLevel);

                for (int32 QuadIdx : Pair.Value)
                {
                    const FFlowFieldDebugQuad& Q = Quads[QuadIdx];
                    FDynamicMeshVertex V[4];
                    for (int32 k = 0; k < 4; k++)
                    {
                        V[k].Position             = FVector3f(Q.V[k]);
                        V[k].Color                = QuadColor;
                        V[k].TextureCoordinate[0] = FVector2f(k & 1, k >> 1);
                        V[k].TangentX             = FVector3f(1.f, 0.f, 0.f);
                        V[k].TangentZ             = FVector3f(0.f, 0.f, 1.f);
                        V[k].TangentZ.Vector.W    = 127;
                    }
                    const int32 I0 = MeshBuilder.AddVertex(V[0]);
                    const int32 I1 = MeshBuilder.AddVertex(V[1]);
                    const int32 I2 = MeshBuilder.AddVertex(V[2]);
                    const int32 I3 = MeshBuilder.AddVertex(V[3]);
                    MeshBuilder.AddTriangle(I0, I1, I2); // TL-TR-BR
                    MeshBuilder.AddTriangle(I0, I2, I3); // TL-BR-BL
                }

                auto* MatProxy = new FColoredMaterialRenderProxy(BaseMat->GetRenderProxy(), LC);
                Collector.RegisterOneFrameMaterialProxy(MatProxy);

                MeshBuilder.GetMesh(FMatrix::Identity, MatProxy,
                    SDPG_World,
                    /*bDisableBackfaceCulling=*/true,
                    /*bReceivesDecals=*/false,
                    ViewIdx, Collector);
            }
        }
    }

    virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
    {
        FPrimitiveViewRelevance R;
        R.bDrawRelevance    = IsShown(View);
        R.bDynamicRelevance = true;
        R.bRenderInMainPass = true;
        R.bShadowRelevance  = false;
        // bRenderInMainPass=true 时，引擎会对每条 FMeshBatch 按其材质的 BlendMode
        // 分派到正确的渲染队列（不透明/半透明），无需手动设置 bOpaque/bTranslucency
        R.bOpaque = true; // 保证不透明格子正确入队；半透明由材质 BlendMode 自动处理
        return R;
    }

    virtual uint32 GetMemoryFootprint() const override
    {
        return sizeof(*this) + Lines.GetAllocatedSize() + Quads.GetAllocatedSize();
    }

    SIZE_T GetTypeHash() const override
    {
        static size_t Unique;
        return reinterpret_cast<size_t>(&Unique);
    }

private:
    TArray<FFlowFieldDebugLine> Lines;
    TArray<FFlowFieldDebugQuad> Quads;
};

// ─────────────────────────────────────────────────────────────────────────────
// UFlowFieldDebugComponent
// ─────────────────────────────────────────────────────────────────────────────
void UFlowFieldDebugComponent::Update(TArray<FFlowFieldDebugLine>&& InLines,
                                       TArray<FFlowFieldDebugQuad>&& InQuads)
{
    Lines = MoveTemp(InLines);
    Quads = MoveTemp(InQuads);
    MarkRenderStateDirty();
}

void UFlowFieldDebugComponent::ClearAll()
{
    Lines.Reset();
    Quads.Reset();
    MarkRenderStateDirty();
}

FPrimitiveSceneProxy* UFlowFieldDebugComponent::CreateSceneProxy()
{
    if (Lines.Num() == 0 && Quads.Num() == 0) return nullptr;
    return new FFlowFieldDebugSceneProxy(this, Lines, Quads);
}

FBoxSphereBounds UFlowFieldDebugComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    return FBoxSphereBounds(FVector::ZeroVector, FVector(WORLD_MAX * 0.5f), WORLD_MAX * 0.5f);
}
