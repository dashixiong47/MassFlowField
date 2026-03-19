#pragma once

#if WITH_EDITOR
#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "FlowField"

class FFlowFieldCommands : public TCommands<FFlowFieldCommands>
{
public:
	FFlowFieldCommands()
		: TCommands<FFlowFieldCommands>(
			TEXT("FlowField"),
			NSLOCTEXT("FlowField", "FlowFieldCommands", "FlowField"),
			NAME_None,
			FAppStyle::GetAppStyleSetName()
		)
	{}

	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> ScanObstacles;
	TSharedPtr<FUICommandInfo> ClearObstacles;
};

#undef LOCTEXT_NAMESPACE

class FFlowFieldEditorToolbar
{
public:
	void Register();
	void Unregister();

private:
	void RegisterMenuExtensions();
	void OnScanObstacles();
	void OnClearObstacles();

	// 进度通知
	void ShowScanNotification(int32 Total);
	void UpdateScanNotification(int32 Submitted, int32 Total);
	void FinishScanNotification(int32 Placed);
	void CancelScanNotification();

	TSharedPtr<FUICommandList>    CommandList;
	TWeakPtr<SNotificationItem>   ScanNotification;
	FTimerHandle                  ProgressTimerHandle;
};

#endif // WITH_EDITOR