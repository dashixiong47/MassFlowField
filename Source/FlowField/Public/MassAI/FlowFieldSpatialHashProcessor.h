#pragma once
#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "MassEntityQuery.h"
#include "FlowFieldSpatialHashProcessor.generated.h"

// 每帧更新 MonsterManager 的空间哈希
// 服务端和客户端都跑，保证查询准确
UCLASS()
class FLOWFIELD_API UFlowFieldSpatialHashProcessor : public UMassProcessor
{
	GENERATED_BODY()
public:
	UFlowFieldSpatialHashProcessor();
protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
	FMassEntityQuery EntityQuery;
};