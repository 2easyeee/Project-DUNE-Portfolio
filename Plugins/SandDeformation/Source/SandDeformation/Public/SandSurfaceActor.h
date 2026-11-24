// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SandSurfaceActor.generated.h"

class USandSurfaceComponent;

/** Convenience actor: an empty actor with a USandSurfaceComponent. Place it in a level, set the component's patch and material. */
UCLASS()
class SANDDEFORMATION_API ASandSurfaceActor : public AActor
{
	GENERATED_BODY()

public:
	ASandSurfaceActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sand")
	TObjectPtr<USandSurfaceComponent> Surface;
};
