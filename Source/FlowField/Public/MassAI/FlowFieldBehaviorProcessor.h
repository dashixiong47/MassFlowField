#pragma once
#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "FlowFieldBehaviorProcessor.generated.h"

/**
 * 游戏线程 Processor，负责侦测 AI 状态变化并派发事件到
 * UFlowFieldTargetComponent 和 UFlowFieldObstacleComponent。
 *
 * 执行顺序：PostPhysics，MovementProcessor 之后。
 * 调用链：bChasingTarget/bInAttackRange/bAtWall 变化 → OnAIEnterRange / OnAIAttack 等 BlueprintNativeEvent。
 */
UCLASS()
class FLOWFIELD_API UFlowFieldBehaviorProcessor : public UMassProcessor
{
    GENERATED_BODY()

public:
    UFlowFieldBehaviorProcessor();

protected:
    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
    virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
    FMassEntityQuery EntityQuery;
};
