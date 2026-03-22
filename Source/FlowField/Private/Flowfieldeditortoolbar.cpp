#include "FlowFieldEditorToolbar.h"

#if WITH_EDITOR

#include "FlowFieldSubsystem.h"
#include "FlowFieldActor.h"
#include "FlowFieldSettings.h"
#include "VAT/FlowFieldVATDataAsset.h"
#include "ToolMenus.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "FlowFieldStyle.h"
#include "AssetToolsModule.h"
#include "Factories/DataAssetFactory.h"
#include "Factories/DataTableFactory.h"
#include "FlowFieldAttackTypes.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"

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

	// 地图打开后检测碰撞配置（此时主窗口已完全渲染，通知一定可见）
	FEditorDelegates::OnMapOpened.AddRaw(
		this, &FFlowFieldEditorToolbar::OnMapOpenedForCollisionCheck
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

	FEditorDelegates::OnMapOpened.RemoveAll(this);

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::Get()->UnregisterOwner(this);

	FFlowFieldCommands::Unregister();
}

// ─────────────────────────────────────────────────────────────────────────────
// 辅助：刷新当前世界的攻击表缓存（PIE 内也有效）
// ─────────────────────────────────────────────────────────────────────────────

static void ReloadAttackTableInCurrentWorld()
{
	if (!GEditor) return;
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) return;
	if (UFlowFieldSubsystem* Sub = World->GetSubsystem<UFlowFieldSubsystem>())
		Sub->LoadAttackTable();
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

	MenuBuilder.AddMenuSeparator();

	// ✅ 创建攻击配置 DataTable
	MenuBuilder.AddMenuEntry(
		INVTEXT("Create Attack DataTable"),
		INVTEXT("在内容浏览器中创建攻击配置 DataTable（行类型：FFlowFieldAttackRow）"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.DataTable"),
		FUIAction(FExecuteAction::CreateRaw(this, &FFlowFieldEditorToolbar::OnCreateAttackDataTable))
	);

	// ✅ 重载攻击表缓存（DataTable 行修改后无需重启）
	MenuBuilder.AddMenuEntry(
		INVTEXT("Reload Attack Table"),
		INVTEXT("重新从 DataTable 加载攻击配置到运行时缓存，修改行数据后点此生效"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh"),
		FUIAction(FExecuteAction::CreateStatic(&ReloadAttackTableInCurrentWorld))
	);

	// ✅ 创建 VAT DataAsset
	MenuBuilder.AddMenuEntry(
		INVTEXT("Create VAT DataAsset"),
		INVTEXT("在内容浏览器中创建 FlowFieldVATDataAsset"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.DataAsset"),
		FUIAction(FExecuteAction::CreateRaw(this, &FFlowFieldEditorToolbar::OnCreateVATDataAsset))
	);

	MenuBuilder.AddMenuSeparator();

	// 懒创建 DetailsView（只创建一次，重复打开下拉菜单时复用）
	if (!SettingsDetailsView.IsValid())
	{
		FPropertyEditorModule& PropEditor =
			FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		FDetailsViewArgs Args;
		Args.bAllowSearch              = false;
		Args.bHideSelectionTip         = true;
		Args.bShowPropertyMatrixButton = false;
		Args.NameAreaSettings          = FDetailsViewArgs::HideNameArea;

		SettingsDetailsView = PropEditor.CreateDetailView(Args);
		SettingsDetailsView->SetObject(GetMutableDefault<UFlowFieldSettings>());

		// 属性改变后：写入 ini + 刷新运行时攻击表缓存（无需重启 PIE）
		SettingsDetailsView->OnFinishedChangingProperties().AddLambda(
			[](const FPropertyChangedEvent&)
			{
				GetMutableDefault<UFlowFieldSettings>()->SaveConfig();
				ReloadAttackTableInCurrentWorld();
			});
	}

	MenuBuilder.AddWidget(
		SNew(SBox)
		.MinDesiredWidth(320.f)
		.MaxDesiredHeight(500.f)
		[
			SettingsDetailsView.ToSharedRef()
		],
		FText::GetEmpty(),
		true   // bNoIndent
	);

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

// ─────────────────────────────────────────────────────────────────────────────
// 碰撞配置检测 & 修复
// ─────────────────────────────────────────────────────────────────────────────

/** 地图打开后触发一次检测，之后自动解绑，避免每次换图都弹 */
void FFlowFieldEditorToolbar::OnMapOpenedForCollisionCheck(const FString& /*Filename*/, bool /*bAsTemplate*/)
{
	FEditorDelegates::OnMapOpened.RemoveAll(this);
	CheckAndNotifyCollisionConfig();
}

/** 读项目 DefaultEngine.ini，检查是否已配置 Terrain 碰撞通道 + FlowFieldTerrain 预设 */
bool FFlowFieldEditorToolbar::HasTerrainChannel() const
{
	FString Content;
	const FString IniPath = FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini");
	if (!FFileHelper::LoadFileToString(Content, *IniPath))
	{
		return false;
	}
	// 需要通道和预设都存在
	const bool bHasChannel = Content.Contains(TEXT("Name=\"Terrain\"")) || Content.Contains(TEXT("Name=Terrain"));
	const bool bHasPreset  = Content.Contains(TEXT("FlowFieldTerrain"));
	return bHasChannel && bHasPreset;
}

void FFlowFieldEditorToolbar::CheckAndNotifyCollisionConfig()
{
	if (HasTerrainChannel()) return;

	// 隐藏旧通知（如果有）
	if (auto Pin = CollisionConfigNotification.Pin())
	{
		Pin->SetCompletionState(SNotificationItem::CS_None);
		Pin->ExpireAndFadeout();
	}

	FNotificationInfo Info(INVTEXT("FlowField：未检测到碰撞配置（Terrain 通道 + FlowFieldTerrain 预设）"));
	Info.bFireAndForget  = false;
	Info.ExpireDuration  = 0.f;
	Info.bUseLargeFont   = false;
	Info.Image           = FAppStyle::GetBrush("Icons.WarningWithColor");

	Info.ButtonDetails.Add(FNotificationButtonInfo(
		INVTEXT("一键修复碰撞配置"),
		INVTEXT("写入 Terrain Object 通道 + FlowFieldTerrain 预设，需重启编辑器生效"),
		FSimpleDelegate::CreateRaw(this, &FFlowFieldEditorToolbar::OnFixCollisionConfig)
	));

	Info.ButtonDetails.Add(FNotificationButtonInfo(
		INVTEXT("忽略"),
		FText(),
		FSimpleDelegate::CreateLambda([this]()
		{
			if (auto Pin = CollisionConfigNotification.Pin())
			{
				Pin->SetCompletionState(SNotificationItem::CS_None);
				Pin->ExpireAndFadeout();
			}
		})
	));

	CollisionConfigNotification = FSlateNotificationManager::Get().AddNotification(Info);
	if (auto Pin = CollisionConfigNotification.Pin())
	{
		Pin->SetCompletionState(SNotificationItem::CS_Pending);
	}
}

void FFlowFieldEditorToolbar::OnFixCollisionConfig()
{
	const FString IniPath = FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini");

	FString Content;
	FFileHelper::LoadFileToString(Content, *IniPath);

	// 幂等检查：通道和预设都已存在则跳过
	const bool bHasChannel = Content.Contains(TEXT("Name=\"Terrain\"")) || Content.Contains(TEXT("Name=Terrain"));
	const bool bHasPreset  = Content.Contains(TEXT("FlowFieldTerrain"));
	if (bHasChannel && bHasPreset)
	{
		if (auto Pin = CollisionConfigNotification.Pin()) Pin->ExpireAndFadeout();
		return;
	}

	// 要写入的行（只写缺失的部分）
	static const FString ChannelLine =
		TEXT("+DefaultChannelResponses=(Channel=ECC_GameTraceChannel1,DefaultResponse=ECR_Block,bTraceType=False,bStaticObject=True,Name=\"Terrain\")\n");

	static const FString PresetLine =
		TEXT("+Profiles=(Name=\"FlowFieldTerrain\",CollisionEnabled=QueryAndPhysics,bCanModify=True,ObjectTypeName=\"Terrain\",")
		TEXT("CustomResponses=((Channel=\"Visibility\",Response=ECR_Block),(Channel=\"Camera\",Response=ECR_Block),")
		TEXT("(Channel=\"WorldStatic\",Response=ECR_Block),(Channel=\"WorldDynamic\",Response=ECR_Block),")
		TEXT("(Channel=\"Pawn\",Response=ECR_Block),(Channel=\"PhysicsBody\",Response=ECR_Block),")
		TEXT("(Channel=\"Vehicle\",Response=ECR_Block),(Channel=\"Destructible\",Response=ECR_Block)),")
		TEXT("HelpMessage=\"FlowField 地形专用 - 阻挡所有通道\")\n");

	FString NewLines;
	if (!bHasChannel) NewLines += ChannelLine;
	if (!bHasPreset)  NewLines += PresetLine;

	const FString Section = TEXT("[/Script/Engine.CollisionProfile]");
	int32 SectionIdx = Content.Find(Section, ESearchCase::CaseSensitive);
	if (SectionIdx != INDEX_NONE)
	{
		// 在 section header 后换行处插入
		int32 InsertIdx = SectionIdx + Section.Len();
		// 跳过换行符
		while (InsertIdx < Content.Len() && (Content[InsertIdx] == TEXT('\r') || Content[InsertIdx] == TEXT('\n')))
		{
			++InsertIdx;
		}
		Content.InsertAt(InsertIdx, NewLines);
	}
	else
	{
		// 没有该 section，追加
		Content += TEXT("\n") + Section + TEXT("\n") + NewLines;
	}

	if (FFileHelper::SaveStringToFile(Content, *IniPath))
	{
		if (auto Pin = CollisionConfigNotification.Pin())
		{
			Pin->SetText(INVTEXT("Terrain 通道 + FlowFieldTerrain 预设已写入，请重启编辑器生效"));
			Pin->SetCompletionState(SNotificationItem::CS_Success);
			Pin->ExpireAndFadeout();
		}
	}
	else
	{
		if (auto Pin = CollisionConfigNotification.Pin())
		{
			Pin->SetText(INVTEXT("写入失败，请手动编辑 DefaultEngine.ini"));
			Pin->SetCompletionState(SNotificationItem::CS_Fail);
			Pin->ExpireAndFadeout();
		}
	}
}

void FFlowFieldEditorToolbar::OnCreateVATDataAsset()
{
	// ── 获取内容浏览器当前选中目录，未选则放 /Game ─────────────
	FContentBrowserModule& CBModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	TArray<FString> SelectedPaths;
	CBModule.Get().GetSelectedPathViewFolders(SelectedPaths);
	const FString PackagePath = SelectedPaths.Num() > 0 ? SelectedPaths[0] : TEXT("/Game");

	// ── 生成唯一资产名，避免同名冲突 ─────────────────────────
	IAssetTools& AssetTools =
		FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	FString AssetName, PackageName;
	AssetTools.CreateUniqueAssetName(
		PackagePath / TEXT("VATDataAsset"), TEXT(""), PackageName, AssetName
	);

	// ── 直接创建，不弹 class 选择面板 ────────────────────────
	UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
	Factory->DataAssetClass = UFlowFieldVATDataAsset::StaticClass();

	UObject* NewAsset = AssetTools.CreateAsset(
		AssetName, PackagePath,
		UFlowFieldVATDataAsset::StaticClass(), Factory
	);

	if (NewAsset)
	{
		// 内容浏览器定位到新资产
		TArray<UObject*> Assets = { NewAsset };
		GEditor->SyncBrowserToObjects(Assets);

		// 打开详情编辑器
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewAsset);
	}
}

void FFlowFieldEditorToolbar::OnCreateAttackDataTable()
{
	FContentBrowserModule& CBModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	TArray<FString> SelectedPaths;
	CBModule.Get().GetSelectedPathViewFolders(SelectedPaths);
	const FString PackagePath = SelectedPaths.Num() > 0 ? SelectedPaths[0] : TEXT("/Game");

	IAssetTools& AssetTools =
		FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	FString AssetName, PackageName;
	AssetTools.CreateUniqueAssetName(
		PackagePath / TEXT("DT_AttackConfig"), TEXT(""), PackageName, AssetName
	);

	UDataTableFactory* Factory = NewObject<UDataTableFactory>();
	Factory->Struct = FFlowFieldAttackRow::StaticStruct();

	UObject* NewAsset = AssetTools.CreateAsset(
		AssetName, PackagePath,
		UDataTable::StaticClass(), Factory
	);

	if (NewAsset)
	{
		TArray<UObject*> Assets = { NewAsset };
		GEditor->SyncBrowserToObjects(Assets);
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewAsset);
	}
}

#endif // WITH_EDITOR
