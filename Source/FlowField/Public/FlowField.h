// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FFlowFieldModule : public IModuleInterface
{
public:
#if WITH_EDITOR
	// Editor 专用工具栏，Shipping 不包含
	TSharedPtr<class FFlowFieldEditorToolbar> EditorToolbar;
#endif

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};