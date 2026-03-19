#pragma once
#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "MassEntityQuery.h"
#include "MassCommonFragments.h"
#include "MassAI/FlowFieldAgentFragment.h"
#include "MassAI/FlowFieldBoidsFragment.h"
#include "FlowFieldBoidsProcessor.generated.h"

UCLASS()
class FLOWFIELD_API UFlowFieldBoidsProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UFlowFieldBoidsProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};