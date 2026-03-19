#pragma once
#include "CoreMinimal.h"
#include "MassTranslator.h"
#include "MassEntityTraitBase.h"
#include "MassCommonFragments.h"
#include "MassEntityTemplateRegistry.h"
#include "MassAI/FlowFieldAgentFragment.h"
#include "FlowFieldTransformToActorTranslator.generated.h"

UCLASS()
class FLOWFIELD_API UFlowFieldTransformToActorTranslator : public UMassTranslator
{
	GENERATED_BODY()

public:
	UFlowFieldTransformToActorTranslator();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};

UCLASS(meta = (DisplayName = "FlowField Transform Sync Trait"))
class FLOWFIELD_API UMassFlowFieldTransformSyncTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	virtual void BuildTemplate(
		FMassEntityTemplateBuildContext& BuildContext,
		const UWorld& World) const override;
};