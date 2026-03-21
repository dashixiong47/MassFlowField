#pragma once
#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "MassEntityQuery.h"
#include "MassAI/FlowFieldAgentFragment.h"
#include "FlowFieldRVOProcessor.generated.h"

// 前向声明，避免在公共头文件中暴露 RVO2 类型
namespace RVO { class RVOSimulator; }

/**
 * RVO2 速度计算 Processor
 * 运行于 MovementProcessor 之前：
 *   1. 将 Mass 实体位置同步到 RVO 模拟器
 *   2. 以流场方向 * MoveSpeed 作为期望速度
 *   3. RVO doStep()，输出无碰撞速度
 *   4. 将结果写入 Agent.RVOComputedVelocity
 */
UCLASS()
class FLOWFIELD_API UFlowFieldRVOProcessor : public UMassProcessor
{
    GENERATED_BODY()

public:
    UFlowFieldRVOProcessor();
    virtual ~UFlowFieldRVOProcessor() override;

protected:
    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
    virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

public:
    /** 目标可走时的停止距离 = CellSize × 此值 */
    UPROPERTY(EditAnywhere, Category="FlowField|RVO", meta=(ClampMin="0.1", ClampMax="2.0",
        DisplayName="普通目标停止距离倍率"))
    float NormalGoalStopRadiusMult = 0.6f;

private:
    FMassEntityQuery EntityQuery;
    RVO::RVOSimulator* RVOSim = nullptr;

    // 目标在障碍内时，缓存"目标最近可走点"，避免每帧每实体重复搜索
    FVector CachedGoalForEffective  = FVector(-99999999.f, -99999999.f, 0.f);
    FVector CachedEffectiveGoal     = FVector::ZeroVector;
};
