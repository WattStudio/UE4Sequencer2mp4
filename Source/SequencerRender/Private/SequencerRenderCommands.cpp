// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SequencerRenderCommands.h"

#define LOCTEXT_NAMESPACE "FSequencerRenderModule"

void FSequencerRenderCommands::RegisterCommands()
{
	UI_COMMAND(RenderActiveSequencer, "SequencerRender", "Execute SequencerRender action", EUserInterfaceActionType::Button, FInputGesture());
}

#undef LOCTEXT_NAMESPACE
