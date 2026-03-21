#include "FlowFieldStyle.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"

TSharedPtr<FSlateStyleSet> FFlowFieldStyle::StyleInstance = nullptr;

#define IMAGE_BRUSH(RelativePath, Size) \
FSlateImageBrush(Style->RootToContentDir(RelativePath, TEXT(".png")), Size)

void FFlowFieldStyle::Initialize()
{
	if (StyleInstance.IsValid()) return;

	StyleInstance = MakeShared<FSlateStyleSet>("FlowFieldStyle");
	auto Style = StyleInstance.Get();

	// 找到插件资源目录
	FString ContentDir = IPluginManager::Get().FindPlugin("FlowField")->GetBaseDir() / TEXT("Resources");
	Style->SetContentRoot(ContentDir);

	// 注册图标
	Style->Set("FlowField.Icon", new IMAGE_BRUSH("Icon128", FVector2D(20.f, 20.f)));
	Style->Set("FlowField.Icon.Small", new IMAGE_BRUSH("Icon128", FVector2D(20.f, 20.f)));
	Style->Set("FlowField.Scan", new IMAGE_BRUSH("Scan128", FVector2D(20.f, 20.f)));
	Style->Set("FlowField.Scan.Small", new IMAGE_BRUSH("Scan128", FVector2D(20.f, 20.f)));
	Style->Set("FlowField.Clear", new IMAGE_BRUSH("Clear128", FVector2D(20.f, 20.f)));
	Style->Set("FlowField.Clear.Small", new IMAGE_BRUSH("Clear128", FVector2D(20.f, 20.f)));
	FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
}

void FFlowFieldStyle::Shutdown()
{
	if (!StyleInstance.IsValid()) return;

	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

TSharedPtr<ISlateStyle> FFlowFieldStyle::Get()
{
	return StyleInstance;
}

FName FFlowFieldStyle::GetStyleSetName()
{
	static FName Name(TEXT("FlowFieldStyle"));
	return Name;
}
