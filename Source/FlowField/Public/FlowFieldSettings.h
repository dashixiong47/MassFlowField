#pragma once
#include "Engine/DeveloperSettings.h"
#include "MassReplication/Public/MassClientBubbleInfoBase.h"
#include "FlowFieldSettings.generated.h"

class UFlowFieldAgentReplicatorBase;
class AFlowFieldClientBubbleInfo;

/**
 * ProjectSettings → Game → FlowField AI
 * 项目层在此填入自定义子类，插件运行时读取，无需修改插件代码。
 */
UCLASS(Config = Game, DefaultConfig,
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
};