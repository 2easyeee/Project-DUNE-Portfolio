// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "DuneHUD.generated.h"

/** Project DUNE: a crosshair at the screen centre while the gun is in hand, and the name of the weapon in hand. */
UCLASS()
class ADuneHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
