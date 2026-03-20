#include "FlowFieldSettings.h"
#include "MassReplication/FlowFieldAgentReplicatorBase.h"
#include "MassReplication/FlowFieldAgentReplicator.h"

UClass* UFlowFieldSettings::GetResolvedReplicatorClass() const
{
	if (!AgentReplicatorClass.IsNull())
		if (UClass* Loaded = AgentReplicatorClass.LoadSynchronous())
			return Loaded;
	return UFlowFieldAgentReplicator::StaticClass();
}

TSubclassOf<AMassClientBubbleInfoBase> UFlowFieldSettings::GetResolvedBubbleInfoClass() const
{
	if (!ClientBubbleInfoClass.IsNull())
		if (auto Loaded = ClientBubbleInfoClass.LoadSynchronous())
			return Loaded;
	return AFlowFieldClientBubbleInfo::StaticClass();
}