#pragma once
#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "VAT/FlowFieldVATDataAsset.h"
#include "FlowFieldVATFragment.generated.h"

/** 标识实体使用 VAT 渲染（自管理 ISM，不走官方 Actor/LOD 处理器） */
USTRUCT()
struct FLOWFIELD_API FFlowFieldVATTag : public FMassTag
{
    GENERATED_BODY()
};

/**
 * VAT 渲染 Fragment — 存储动画播放状态
 * ISM 实例由 UMassRepresentationSubsystem 的批量系统管理
 */
USTRUCT()
struct FLOWFIELD_API FFlowFieldVATFragment : public FMassFragment
{
    GENERATED_BODY()

    /**
     * VAT 数据资产（由 FlowFieldAgentTrait 写入，运行时只读）
     * 裸指针保持 trivially copyable；生命周期由 Trait UPROPERTY 持有
     */
    UPROPERTY()
    UFlowFieldVATDataAsset* DataAsset = nullptr;

    /** 当前播放的动画 ID（DataAsset.Animations 的下标） */
    UPROPERTY()
    int32 AnimationID = 0;

    /** 当前动画时间（秒，从 0 开始） */
    UPROPERTY()
    float AnimTime = 0.f;

    /** 播放速率倍率（1.0 = 正常速度） */
    UPROPERTY()
    float PlayRate = 1.f;
};

/**
 * VAT LOD 距离配置（FMassConstSharedFragment，同 Trait 配置的所有实体共享）
 */
USTRUCT()
struct FLOWFIELD_API FFlowFieldVATSharedData : public FMassConstSharedFragment
{
    GENERATED_BODY()

    /** VAT → 低精度网格切换距离（cm）。0 表示全程使用 VAT */
    UPROPERTY()
    float LODSwitchDistance = 0.f;

    /** 剔除距离（cm）。超出此距离的实体从 ISM 中移除，不渲染 */
    UPROPERTY()
    float CullDistance = 10000.f;

    bool Identical(const FFlowFieldVATSharedData* Other, uint32) const
    {
        return LODSwitchDistance == Other->LODSwitchDistance
            && CullDistance == Other->CullDistance;
    }
};

template<>
struct TStructOpsTypeTraits<FFlowFieldVATSharedData>
    : public TStructOpsTypeTraitsBase2<FFlowFieldVATSharedData>
{
    enum { WithIdentical = true };
};
