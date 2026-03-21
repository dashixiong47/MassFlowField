// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlowField.h"

#include "FlowFieldStyle.h"

#if WITH_EDITOR
#include "Flowfieldeditortoolbar.h"
#endif

#define LOCTEXT_NAMESPACE "FFlowFieldModule"

void FFlowFieldModule::StartupModule()
{
#if WITH_EDITOR
	FFlowFieldStyle::Initialize();
	EditorToolbar = MakeShared<FFlowFieldEditorToolbar>();
	EditorToolbar->Register();
#endif
	
}

void FFlowFieldModule::ShutdownModule()
{
#if WITH_EDITOR
	FFlowFieldStyle::Shutdown();
	if (EditorToolbar.IsValid())
		EditorToolbar->Unregister();
#endif
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFlowFieldModule, FlowField)