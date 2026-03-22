#pragma once
#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "MassEntityQuery.h"
#include "MassEntityTypes.h"
#include "MassCommonFragments.h"
#include "MassAI/FlowFieldAgentFragment.h"
#include "FlowFieldAttackProcessor.generated.h"

UCLASS()
class FLOWFIELD_API UFlowFieldAttackProcessor : public UMassProcessor
{
    GENERATED_BODY()

public:
    UFlowFieldAttackProcessor();

protected:
    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
    virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
    // 存活实体查询（用于构建空间缓存 + 处理待击杀）
    FMassEntityQuery EntityQuery;
    // 死亡实体查询（用于死亡倒计时 + 处理待销毁）
    FMassEntityQuery DeathLingerQuery;

    // 每帧重建：EntityId → Handle 映射（用于 Kill/Destroy 请求）
    TMap<int32, FMassEntityHandle> AliveHandleCache;
    TMap<int32, FMassEntityHandle> DeadHandleCache;
};
