// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "SequencerRenderStyle.h"

class FSequencerRenderCommands : public TCommands<FSequencerRenderCommands>
{
public:

	FSequencerRenderCommands()
		: TCommands<FSequencerRenderCommands>(TEXT("SequencerRender"), NSLOCTEXT("Contexts", "SequencerRender", "SequencerRender Plugin"), NAME_None, FSequencerRenderStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > RenderActiveSequencer;
};
