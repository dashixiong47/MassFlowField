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

public:
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

	// 下拉菜单
	TSharedRef<SWidget> GenerateMenuContent();

	// 功能
	void OnScanObstacles();
	void OnClearObstacles();

	// 通知
	void ShowScanNotification(int32 Total);
	void UpdateScanNotification(int32 Submitted, int32 Total);
	void FinishScanNotification(int32 Placed);
	void CancelScanNotification();

private:
	TSharedPtr<FUICommandList>  CommandList;
	TWeakPtr<SNotificationItem> ScanNotification;
};

#endif // WITH_EDITOR
