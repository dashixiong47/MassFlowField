#pragma once
#include "Engine/DeveloperSettings.h"
#include "Engine/DataTable.h"
#include "MassReplication/Public/MassClientBubbleInfoBase.h"
#include "FlowFieldSettings.generated.h"

class UFlowFieldAgentReplicatorBase;
class AFlowFieldClientBubbleInfo;

/**
 * ProjectSettings → Game → FlowField AI
 * 项目层在此填入自定义子类，插件运行时读取，无需修改插件代码。
 * 配置存储在 Config/DefaultFlowField.ini。
 */
UCLASS(Config = FlowField, DefaultConfig,
	   meta = (DisplayName = "FlowField AI Settings"))
class FLOWFIELD_API UFlowFieldSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	UFlowFieldSettings()
	{
		CategoryName = TEXT("Game");
		SectionName  = TEXT("FlowField AI");
	}

	/** 项目层 Replicator 子类（继承 UFlowFieldAgentReplicatorBase），留空用插件默认 */
	UPROPERTY(Config, EditAnywhere, Category = "Replication",
			  meta = (DisplayName = "Agent Replicator Class"))
	TSoftClassPtr<UObject> AgentReplicatorClass;

	/** 项目层 BubbleInfo 子类（继承 AMassClientBubbleInfoBase），留空用插件默认 */
	UPROPERTY(Config, EditAnywhere, Category = "Replication",
			  meta = (DisplayName = "Client Bubble Info Class"))
	TSoftClassPtr<AMassClientBubbleInfoBase> ClientBubbleInfoClass;

	UClass* GetResolvedReplicatorClass() const;
	TSubclassOf<AMassClientBubbleInfoBase> GetResolvedBubbleInfoClass() const;

	static const UFlowFieldSettings* Get() { return GetDefault<UFlowFieldSettings>(); }

	// ── 调试绘制 ───────────────────────────────────────────────────────

	UPROPERTY(Config, EditAnywhere, Category="调试",
		meta=(DisplayName="绘制网格"))
	bool bDrawGrid = false;

	UPROPERTY(Config, EditAnywhere, Category="调试",
		meta=(DisplayName="绘制流场方向"))
	bool bDrawFlow = true;

	UPROPERTY(Config, EditAnywhere, Category="调试",
		meta=(DisplayName="绘制热力图"))
	bool bDrawHeatmap = true;

	UPROPERTY(Config, EditAnywhere, Category="调试",
		meta=(DisplayName="箭头缩放"))
	float ArrowScale = 0.4f;

	UPROPERTY(Config, EditAnywhere, Category="调试",
		meta=(ClampMin="500", ClampMax="50000", DisplayName="调试绘制距离（cm）"))
	float DebugDrawDistance = 5000.f;

	UPROPERTY(Config, EditAnywhere, Category="调试",
		meta=(DisplayName="绘制目标推挤范围"))
	bool bDrawTargetRanges = false;

	UPROPERTY(Config, EditAnywhere, Category="调试",
		meta=(DisplayName="绘制 AI 感知范围"))
	bool bDrawAgentRanges = false;

	// ── 调试颜色 ─────────────────────────────────────────────────────

	UPROPERTY(Config, EditAnywhere, Category="调试|颜色",
		meta=(DisplayName="热力图近端色"))
	FLinearColor DebugHeatLow = FLinearColor(0.f, 1.f, 0.f);

	UPROPERTY(Config, EditAnywhere, Category="调试|颜色",
		meta=(DisplayName="热力图远端色"))
	FLinearColor DebugHeatHigh = FLinearColor(1.f, 0.f, 0.f);

	UPROPERTY(Config, EditAnywhere, Category="调试|颜色",
		meta=(DisplayName="无流场时填充色"))
	FLinearColor DebugColorNoFlow = FLinearColor(0.47f, 0.47f, 0.47f);

	UPROPERTY(Config, EditAnywhere, Category="调试|颜色",
		meta=(DisplayName="障碍格轮廓色"))
	FLinearColor DebugColorObstacle = FLinearColor(0.9f, 0.15f, 0.1f);

	UPROPERTY(Config, EditAnywhere, Category="调试|颜色",
		meta=(DisplayName="可走格轮廓色"))
	FLinearColor DebugColorWalkable = FLinearColor(0.1f, 0.7f, 0.15f);

	UPROPERTY(Config, EditAnywhere, Category="调试|颜色",
		meta=(DisplayName="流场箭头色"))
	FLinearColor DebugColorArrow = FLinearColor::White;

	UPROPERTY(Config, EditAnywhere, Category="调试|颜色",
		meta=(DisplayName="目标标记色"))
	FLinearColor DebugColorGoal = FLinearColor::Red;

	// ── 攻击 ──────────────────────────────────────────────────────────

	/** 攻击配置 DataTable（行类型：FFlowFieldAttackRow），运行时由 Subsystem 读入缓存 */
	UPROPERTY(Config, EditAnywhere, Category="攻击",
		meta=(DisplayName="攻击配置表", RequiredAssetDataTags="RowStructure=/Script/FlowField.FlowFieldAttackRow"))
	TSoftObjectPtr<UDataTable> DefaultAttackTable;
};