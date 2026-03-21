#pragma once
#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "MassEntityQuery.h"
#include "MassCommonFragments.h"
#include "MassAI/FlowFieldAgentFragment.h"
#include "FlowFieldMovementProcessor.generated.h"

UCLASS()
class FLOWFIELD_API UFlowFieldMovementProcessor : public UMassProcessor
{
    GENERATED_BODY()

public:
    UFlowFieldMovementProcessor();

    /** RVO 速度死区：低于 MoveSpeed × 此值时不移动，消除靠墙微抖动 */
    UPROPERTY(EditAnywhere, Category="FlowField|移动", meta=(ClampMin="0", ClampMax="0.5",
        DisplayName="速度死区比例"))
    float VelocityDeadZonePct = 0.08f;

    /** 速度平滑插值速度。越小越丝滑但响应慢；越大越跟手但抖动多。推荐 6~12 */
    UPROPERTY(EditAnywhere, Category="FlowField|移动", meta=(ClampMin="1", ClampMax="30",
        DisplayName="速度平滑速度"))
    float VelocitySmoothSpeed = 8.f;

    /** 被挤时最低速度倍率（0=完全停，1=不减速）。被推方向与流场方向夹角越大，速度越接近此值 */
    UPROPERTY(EditAnywhere, Category="FlowField|移动", meta=(ClampMin="0", ClampMax="1",
        DisplayName="被挤最低速度倍率"))
    float PushedMinSpeedScale = 0.4f;

    /** 旋转插值速度（越小转向越慢，抖动越少）*/
    UPROPERTY(EditAnywhere, Category="FlowField|移动", meta=(ClampMin="1", ClampMax="30",
        DisplayName="旋转插值速度"))
    float RotationInterpSpeed = 7.f;

protected:
    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
    virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
    FMassEntityQuery EntityQuery;
};