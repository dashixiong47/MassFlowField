// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlowField.h"

#if WITH_EDITOR
#include "Flowfieldeditortoolbar.h"
#endif

#define LOCTEXT_NAMESPACE "FFlowFieldModule"

void FFlowFieldModule::StartupModule()
{
#if WITH_EDITOR
	EditorToolbar = MakeShared<FFlowFieldEditorToolbar>();
	EditorToolbar->Register();
#endif
	
}

void FFlowFieldModule::ShutdownModule()
{
#if WITH_EDITOR
	if (EditorToolbar.IsValid())
		EditorToolbar->Unregister();
#endif
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFlowFieldModule, FlowField)