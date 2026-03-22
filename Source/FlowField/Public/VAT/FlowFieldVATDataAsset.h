#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "FlowFieldVATDataAsset.generated.h"

/** 单条顶点动画定义 */
USTRUCT(BlueprintType)
struct FLOWFIELD_API FFlowFieldVATAnimation
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VAT",
        meta=(DisplayName="动画名称"))
    FName AnimationName = NAME_None;

    /** 该动画在纹理中的起始帧 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VAT",
        meta=(ClampMin="0", DisplayName="起始帧"))
    int32 StartFrame = 0;

    /** 帧数量 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VAT",
        meta=(ClampMin="1", DisplayName="帧数量"))
    int32 FrameCount = 30;

    /** 帧率（每秒帧数） */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VAT",
        meta=(ClampMin="1.0", DisplayName="帧率 (FPS)"))
    float FPS = 30.f;

    /** 是否循环 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VAT",
        meta=(DisplayName="循环播放"))
    bool bLoop = true;
};

/**
 * VAT（顶点动画纹理）数据资产
 * 每种怪物类型配置一个，FlowFieldAgentTrait 引用它。
 * 挂在 FlowFieldActor 上的 HISM 组件数量 = 场景中使用的 DataAsset 数量。
 */
UCLASS(BlueprintType)
class FLOWFIELD_API UFlowFieldVATDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    /** 渲染用静态网格体 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VAT",
        meta=(DisplayName="静态网格体"))
    TObjectPtr<UStaticMesh> Mesh = nullptr;

    /** VAT 材质（需在材质中读取自定义数据）*/
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VAT",
        meta=(DisplayName="材质"))
    TObjectPtr<UMaterialInterface> Material = nullptr;

    /**
     * HISM 实例自定义数据布局（NumCustomDataFloats）：
     *   [0] = 绝对帧索引（float，着色器取整使用）
     *   [1…N] = 可扩展自定义字段，由材质定义含义
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VAT",
        meta=(ClampMin="1", ClampMax="8", DisplayName="自定义数据浮点数数量"))
    int32 NumCustomDataFloats = 1;

    /** 动画列表，AnimationID 为此数组下标 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VAT",
        meta=(DisplayName="动画列表"))
    TArray<FFlowFieldVATAnimation> Animations;

    /**
     * 网格朝向修正（本地旋转偏移）。
     * 若 Mesh 的模型前向与 +X 不一致（如朝 +Y 或 -X），在此填入修正旋转。
     * 例如：模型朝 +Y → 填 (0, 0, -90)；模型朝 -X → 填 (0, 0, 180)。
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VAT",
        meta=(DisplayName="网格旋转偏移"))
    FRotator MeshRotationOffset = FRotator::ZeroRotator;

    // ── LOD / 剔除 ────────────────────────────────────────────────

    /** 启用远景低精度网格体。超过切换距离后替代 VAT 网格，节省着色器开销。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VAT|LOD",
        meta=(DisplayName="启用远景 LOD 网格"))
    bool bUseLODMesh = false;

    /** 远景低精度网格体（仅 bUseLODMesh=true 时生效）。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VAT|LOD",
        meta=(EditCondition="bUseLODMesh", DisplayName="远景 LOD 网格体"))
    TObjectPtr<UStaticMesh> LODMesh = nullptr;

    /** 远景 LOD 网格体材质（不填则使用网格体默认材质）。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VAT|LOD",
        meta=(EditCondition="bUseLODMesh", DisplayName="远景 LOD 材质"))
    TObjectPtr<UMaterialInterface> LODMeshMaterial = nullptr;

    /** VAT → LOD 网格切换距离（cm）。超过此距离切换到低精度网格。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VAT|LOD",
        meta=(ClampMin="0", EditCondition="bUseLODMesh", DisplayName="LOD 切换距离（cm）"))
    float LODSwitchDistance = 5000.f;

    /** 启用距离剔除。超出剔除距离的实体不占 GPU 资源。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VAT|LOD",
        meta=(DisplayName="启用距离剔除"))
    bool bEnableDistanceCulling = true;

    /** 剔除距离（cm）。超出此距离的实体从 ISM 中移除，不渲染。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VAT|LOD",
        meta=(ClampMin="100", EditCondition="bEnableDistanceCulling", DisplayName="剔除距离（cm）"))
    float CullDistance = 10000.f;

    /** 根据名称查找动画 ID（-1 = 未找到） */
    UFUNCTION(BlueprintCallable, Category="VAT")
    int32 FindAnimationByName(FName Name) const;

    /** 获取指定 AnimationID 的帧数据，bounds-checked */
    const FFlowFieldVATAnimation* GetAnimation(int32 AnimationID) const;
};
