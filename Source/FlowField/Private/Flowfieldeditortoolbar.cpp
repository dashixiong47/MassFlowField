#include "FlowFieldEditorToolbar.h"

#if WITH_EDITOR
#include "FlowFieldSubsystem.h"
#include "FlowFieldActor.h"
#include "ToolMenus.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

// ─────────────────────────────────────────────────────────────────────────────

#define LOCTEXT_NAMESPACE "FlowField"

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

// ── Register ──────────────────────────────────────────────────────────────────

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

	FToolMenuEntry ScanEntry = FToolMenuEntry::InitToolBarButton(
		FFlowFieldCommands::Get().ScanObstacles,
		INVTEXT("FF Scan"),
		INVTEXT("Scan obstacles and place FlowField marker Actors"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Layers")
	);
	ScanEntry.SetCommandList(CommandList);
	Section.AddEntry(ScanEntry);

	FToolMenuEntry ClearEntry = FToolMenuEntry::InitToolBarButton(
		FFlowFieldCommands::Get().ClearObstacles,
		INVTEXT("FF Clear"),
		INVTEXT("Remove all FlowField obstacle marker Actors"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Layers")
	);
	ClearEntry.SetCommandList(CommandList);
	Section.AddEntry(ClearEntry);
}

void FFlowFieldEditorToolbar::Unregister()
{
	CancelScanNotification();
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::Get()->UnregisterOwner(this);
	FFlowFieldCommands::Unregister();
}

// ── Handlers ──────────────────────────────────────────────────────────────────

void FFlowFieldEditorToolbar::OnScanObstacles()
{
	UE_LOG(LogTemp, Warning, TEXT("[FF Toolbar] OnScanObstacles called"));

	if (!GEditor)
	{
		UE_LOG(LogTemp, Error, TEXT("[FF Toolbar] GEditor is null"));
		return;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("[FF Toolbar] World is null"));
		return;
	}

	UFlowFieldSubsystem* Sub = World->GetSubsystem<UFlowFieldSubsystem>();
	if (!Sub)
	{
		UE_LOG(LogTemp, Error, TEXT("[FF Toolbar] FlowFieldSubsystem not found"));
		return;
	}

	if (Sub->IsScanning())
	{
		UE_LOG(LogTemp, Warning, TEXT("[FF Toolbar] Scan already in progress"));
		return;
	}

	// 计算总格子数用于进度显示
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
	else
	{
		// FlowFieldActor 还没注册，从场景找
		for (TActorIterator<AFlowFieldActor> It(World); It; ++It)
		{
			FVector Min, Max;
			if ((*It)->ResolveBounds(Min, Max))
			{
				int32 W = FMath::CeilToInt((Max.X - Min.X) / (*It)->CellSize);
				int32 H = FMath::CeilToInt((Max.Y - Min.Y) / (*It)->CellSize);
				Total = W * H;
			}
			break;
		}
	}

	ShowScanNotification(Total);
	Sub->ScanAndPlaceObstacles();

	// 启动进度更新 Timer（每0.1秒更新一次进度条）
	if (GEditor)
	{
		TWeakObjectPtr<UFlowFieldSubsystem> WeakSub(Sub);
		TWeakPtr<FFlowFieldEditorToolbar> WeakThis(
			TSharedPtr<FFlowFieldEditorToolbar>(this, [](FFlowFieldEditorToolbar*){})
		);

		// 用 UE 的 Tick 代替 Timer，编辑器里 Timer 可能不稳定
		// 直接绑定到 Sub 的进度回调
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
	}
}

void FFlowFieldEditorToolbar::OnClearObstacles()
{
	UE_LOG(LogTemp, Warning, TEXT("[FF Toolbar] OnClearObstacles called"));

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return;

	UFlowFieldSubsystem* Sub = World->GetSubsystem<UFlowFieldSubsystem>();
	if (!Sub) return;

	CancelScanNotification();
	Sub->ClearObstacleActors();

	// 完成通知
	FNotificationInfo Info(INVTEXT("FlowField obstacles cleared"));
	Info.bFireAndForget = true;
	Info.ExpireDuration = 2.f;
	Info.Image = FAppStyle::GetBrush("Icons.SuccessWithColor");
	FSlateNotificationManager::Get().AddNotification(Info);
}

// ── Notification helpers ───────────────────────────────────────────────────────

void FFlowFieldEditorToolbar::ShowScanNotification(int32 Total)
{
	CancelScanNotification();

	FNotificationInfo Info(FText::Format(
		INVTEXT("FlowField Scanning... 0 / {0}"),
		FText::AsNumber(Total)
	));
	Info.bFireAndForget   = false;  // 手动控制消失
	Info.bUseSuccessFailIcons = true;
	Info.bUseLargeFont    = false;
	Info.ExpireDuration   = 0.f;
	Info.Image = FAppStyle::GetBrush("Icons.Loading");

	// 取消按钮
	Info.ButtonDetails.Add(FNotificationButtonInfo(
		INVTEXT("Cancel"),
		FText(),
		FSimpleDelegate::CreateRaw(this, &FFlowFieldEditorToolbar::CancelScanNotification)
	));

	ScanNotification = FSlateNotificationManager::Get().AddNotification(Info);

	if (TSharedPtr<SNotificationItem> Pin = ScanNotification.Pin())
		Pin->SetCompletionState(SNotificationItem::CS_Pending);
}

void FFlowFieldEditorToolbar::UpdateScanNotification(int32 Submitted, int32 Total)
{
	TSharedPtr<SNotificationItem> Pin = ScanNotification.Pin();
	if (!Pin) return;

	float Pct = Total > 0 ? (float)Submitted / (float)Total * 100.f : 0.f;
	Pin->SetText(FText::Format(
		INVTEXT("FlowField Scanning... {0} / {1} ({2}%)"),
		FText::AsNumber(Submitted),
		FText::AsNumber(Total),
		FText::AsNumber(FMath::RoundToInt(Pct))
	));
}

void FFlowFieldEditorToolbar::FinishScanNotification(int32 Placed)
{
	TSharedPtr<SNotificationItem> Pin = ScanNotification.Pin();
	if (!Pin) return;

	Pin->SetText(FText::Format(
		INVTEXT("FlowField Scan Complete — {0} obstacles placed"),
		FText::AsNumber(Placed)
	));
	Pin->SetCompletionState(SNotificationItem::CS_Success);
	Pin->ExpireAndFadeout();

	ScanNotification.Reset();
}

void FFlowFieldEditorToolbar::CancelScanNotification()
{
	TSharedPtr<SNotificationItem> Pin = ScanNotification.Pin();
	if (!Pin) return;

	Pin->SetCompletionState(SNotificationItem::CS_Fail);
	Pin->ExpireAndFadeout();
	ScanNotification.Reset();

	// 停止正在进行的扫描
	if (GEditor)
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (World)
		{
			if (UFlowFieldSubsystem* Sub = World->GetSubsystem<UFlowFieldSubsystem>())
				Sub->ClearObstacleActors();
		}
	}
}

#endif // WITH_EDITOR