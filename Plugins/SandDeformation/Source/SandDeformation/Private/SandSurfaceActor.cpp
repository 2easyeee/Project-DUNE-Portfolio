// Copyright Epic Games, Inc. All Rights Reserved.

#include "SandSurfaceActor.h"
#include "SandSurfaceComponent.h"

ASandSurfaceActor::ASandSurfaceActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Surface = CreateDefaultSubobject<USandSurfaceComponent>(TEXT("SandSurface"));
}
