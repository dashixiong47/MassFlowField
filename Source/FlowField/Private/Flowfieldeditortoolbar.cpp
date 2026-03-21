#include "FlowFieldEditorToolbar.h"

#if WITH_EDITOR

#include "FlowFieldSubsystem.h"
#include "FlowFieldActor.h"
#include "ToolMenus.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "FlowFieldStyle.h"

#define LOCTEXT_NAMESPACE "FlowField"

// ─────────────────────────────────────────────────────────────────────────────
// Commands
// ─────────────────────────────────────────────────────────────────────────────

void FFlowFieldCommands::RegisterCommands()
{
	UI_COMMAND(ScanObstacles,
		"FF Scan",
		"Scan scene for obstacles and place FlowField marker Actors",
		EUserInterfaceActionType::Button,
		FInputChord()
	);

	UI_COMMAND(ClearObstacles,
		"FF Clear",
		"Remove all FlowField obstacle marker Actors from the scene",
		EUserInterfaceActionType::Button,
		FInputChord()
	);
}

#undef LOCTEXT_NAMESPACE

// ─────────────────────────────────────────────────────────────────────────────
// Register
// ─────────────────────────────────────────────────────────────────────────────

void FFlowFieldEditorToolbar::Register()
{
	FFlowFieldCommands::Register();

	CommandList = MakeShared<FUICommandList>();

	CommandList->MapAction(
		FFlowFieldCommands::Get().ScanObstacles,
		FExecuteAction::CreateRaw(this, &FFlowFieldEditorToolbar::OnScanObstacles)
	);

	CommandList->MapAction(
		FFlowFieldCommands::Get().ClearObstacles,
		FExecuteAction::CreateRaw(this, &FFlowFieldEditorToolbar::OnClearObstacles)
	);

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(
			this, &FFlowFieldEditorToolbar::RegisterMenuExtensions
		)
	);
}

void FFlowFieldEditorToolbar::RegisterMenuExtensions()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(
		"LevelEditor.LevelEditorToolBar.PlayToolBar"
	);

	FToolMenuSection& Section = Menu->FindOrAddSection("FlowField", INVTEXT("FlowField"));

	// ✅ 单按钮下拉菜单
	FToolMenuEntry ComboEntry = FToolMenuEntry::InitComboButton(
		"FlowFieldCombo",
		FUIAction(),
		FOnGetContent::CreateRaw(this, &FFlowFieldEditorToolbar::GenerateMenuContent),
		INVTEXT("FlowField"),
		INVTEXT("FlowField Tools"),
		FSlateIcon(FFlowFieldStyle::GetStyleSetName(), "FlowField.Icon")
	);

	Section.AddEntry(ComboEntry);
}

void FFlowFieldEditorToolbar::Unregister()
{
	CancelScanNotification();

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::Get()->UnregisterOwner(this);

	FFlowFieldCommands::Unregister();
}

// ─────────────────────────────────────────────────────────────────────────────
// Menu
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> FFlowFieldEditorToolbar::GenerateMenuContent()
{
	FMenuBuilder MenuBuilder(true, CommandList);

	// 原有功能
	// ✅ Scan
	MenuBuilder.AddMenuEntry(
		INVTEXT("FF Scan"),
		INVTEXT("Scan obstacles"),
		FSlateIcon(FFlowFieldStyle::GetStyleSetName(), "FlowField.Scan"),
		FUIAction(FExecuteAction::CreateRaw(this, &FFlowFieldEditorToolbar::OnScanObstacles))
	);

	// ✅ Clear
	MenuBuilder.AddMenuEntry(
		INVTEXT("FF Clear"),
		INVTEXT("Clear obstacles"),
		FSlateIcon(FFlowFieldStyle::GetStyleSetName(), "FlowField.Clear"),
		FUIAction(FExecuteAction::CreateRaw(this, &FFlowFieldEditorToolbar::OnClearObstacles))
	);
	// ✅ 以后扩展直接加这里
	/*
	MenuBuilder.AddMenuSeparator();

	MenuBuilder.AddMenuEntry(
		INVTEXT("Debug Draw"),
		INVTEXT("Toggle debug draw"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FFlowFieldEditorToolbar::OnDebugDraw))
	);
	*/

	return MenuBuilder.MakeWidget();
}

// ─────────────────────────────────────────────────────────────────────────────
// Handlers
// ─────────────────────────────────────────────────────────────────────────────

void FFlowFieldEditorToolbar::OnScanObstacles()
{
	if (!GEditor) return;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) return;

	UFlowFieldSubsystem* Sub = World->GetSubsystem<UFlowFieldSubsystem>();
	if (!Sub || Sub->IsScanning()) return;

	int32 Total = 0;

	if (AFlowFieldActor* Actor = Sub->GetActor())
	{
		FVector Min, Max;
		if (Actor->ResolveBounds(Min, Max))
		{
			int32 W = FMath::CeilToInt((Max.X - Min.X) / Actor->CellSize);
			int32 H = FMath::CeilToInt((Max.Y - Min.Y) / Actor->CellSize);
			Total = W * H;
		}
	}

	ShowScanNotification(Total);

	// 绑定回调（安全写法）
	Sub->OnScanProgressUpdated.BindLambda(
		[this, Total](int32 Submitted)
		{
			UpdateScanNotification(Submitted, Total);
		}
	);

	Sub->OnScanCompleted.BindLambda(
		[this](int32 Placed)
		{
			FinishScanNotification(Placed);
		}
	);

	Sub->ScanAndPlaceObstacles();
}

void FFlowFieldEditorToolbar::OnClearObstacles()
{
	if (!GEditor) return;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) return;

	if (UFlowFieldSubsystem* Sub = World->GetSubsystem<UFlowFieldSubsystem>())
	{
		CancelScanNotification();
		Sub->ClearObstacleActors();

		FNotificationInfo Info(INVTEXT("FlowField obstacles cleared"));
		Info.bFireAndForget = true;
		Info.ExpireDuration = 2.f;
		Info.Image = FAppStyle::GetBrush("Icons.SuccessWithColor");

		FSlateNotificationManager::Get().AddNotification(Info);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Notification
// ─────────────────────────────────────────────────────────────────────────────

void FFlowFieldEditorToolbar::ShowScanNotification(int32 Total)
{
	CancelScanNotification();

	FNotificationInfo Info(FText::Format(
		INVTEXT("FlowField Scanning... 0 / {0}"),
		FText::AsNumber(Total)
	));

	Info.bFireAndForget = false;
	Info.ExpireDuration = 0.f;
	Info.Image = FAppStyle::GetBrush("Icons.Loading");

	Info.ButtonDetails.Add(FNotificationButtonInfo(
		INVTEXT("Cancel"),
		FText(),
		FSimpleDelegate::CreateRaw(this, &FFlowFieldEditorToolbar::CancelScanNotification)
	));

	ScanNotification = FSlateNotificationManager::Get().AddNotification(Info);

	if (auto Pin = ScanNotification.Pin())
		Pin->SetCompletionState(SNotificationItem::CS_Pending);
}

void FFlowFieldEditorToolbar::UpdateScanNotification(int32 Submitted, int32 Total)
{
	if (auto Pin = ScanNotification.Pin())
	{
		float Pct = Total > 0 ? (float)Submitted / (float)Total * 100.f : 0.f;

		Pin->SetText(FText::Format(
			INVTEXT("FlowField Scanning... {0}/{1} ({2}%)"),
			FText::AsNumber(Submitted),
			FText::AsNumber(Total),
			FText::AsNumber(FMath::RoundToInt(Pct))
		));
	}
}

void FFlowFieldEditorToolbar::FinishScanNotification(int32 Placed)
{
	if (auto Pin = ScanNotification.Pin())
	{
		Pin->SetText(FText::Format(
			INVTEXT("FlowField Scan Complete — {0} placed"),
			FText::AsNumber(Placed)
		));

		Pin->SetCompletionState(SNotificationItem::CS_Success);
		Pin->ExpireAndFadeout();
	}

	ScanNotification.Reset();
}

void FFlowFieldEditorToolbar::CancelScanNotification()
{
	if (auto Pin = ScanNotification.Pin())
	{
		Pin->SetCompletionState(SNotificationItem::CS_Fail);
		Pin->ExpireAndFadeout();
	}

	ScanNotification.Reset();

	if (GEditor)
	{
		if (UWorld* World = GEditor->GetEditorWorldContext().World())
		{
			if (UFlowFieldSubsystem* Sub = World->GetSubsystem<UFlowFieldSubsystem>())
			{
				Sub->ClearObstacleActors();
			}
		}
	}
}

#endif // WITH_EDITOR
