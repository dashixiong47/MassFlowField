#pragma once
#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "MassEntityQuery.h"
#include "FlowFieldVATProcessor.generated.h"

/**
 * VAT 渲染 Processor
 * 运行于 PostPhysics（在 EndVisualChanges 之前）：
 *   1. 计算每实体到观察者的距离，得到 LODSignificance
 *   2. 可见实体：AddBatchedTransform + 推进动画 + AddBatchedCustomData
 *   3. 不可见/超距实体：跳过（EndVisualChanges 自动从 ISM 移除）
 *
 * 不依赖 UMassRepresentationProcessor / FMassVisualizationLODProcessor，
 * 自包含 LOD 逻辑，与官方 MassRepresentationSubsystem 的批量 GPU 上传机制集成。
 */
UCLASS()
class FLOWFIELD_API UFlowFieldVATProcessor : public UMassProcessor
{
    GENERATED_BODY()

public:
    UFlowFieldVATProcessor();

protected:
    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
    virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
    FMassEntityQuery EntityQuery;
};
