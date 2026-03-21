#pragma once
#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "FlowFieldDebugComponent.generated.h"

struct FFlowFieldDebugLine
{
    FVector      Start;
    FVector      End;
    FLinearColor Color;
    float        Thickness;
};

// 实心四边形（用 2 个三角形渲染，支持顶点色）
struct FFlowFieldDebugQuad
{
    FVector V[4]; // TL TR BR BL（顺时针）
    FColor  Color;
};

UCLASS()
class FLOWFIELD_API UFlowFieldDebugComponent : public UPrimitiveComponent
{
    GENERATED_BODY()

public:
    void Update(TArray<FFlowFieldDebugLine>&& InLines, TArray<FFlowFieldDebugQuad>&& InQuads);
    void ClearAll();

    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
    virtual bool IsZeroExtent() const override { return true; }

private:
    TArray<FFlowFieldDebugLine> Lines;
    TArray<FFlowFieldDebugQuad> Quads;
};
