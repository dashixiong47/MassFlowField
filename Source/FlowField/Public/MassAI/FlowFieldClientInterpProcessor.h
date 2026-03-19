#pragma once
#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "MassEntityQuery.h"
#include "MassCommonFragments.h"
#include "MassAI/FlowFieldAgentFragment.h"
#include "FlowFieldClientInterpProcessor.generated.h"

// 客户端专用：收到服务端位置校正后平滑插值
// 服务端不跑此 Processor
UCLASS()
class FLOWFIELD_API UFlowFieldClientInterpProcessor : public UMassProcessor
{
	GENERATED_BODY()
public:
	UFlowFieldClientInterpProcessor();
protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
	FMassEntityQuery EntityQuery;
};