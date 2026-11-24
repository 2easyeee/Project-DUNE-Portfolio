// Copyright Epic Games, Inc. All Rights Reserved.

#include "SandSimulationSubsystem.h"
#include "SandSelfTest.h"
#include "SandSurfaceComponent.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

bool USandSimulationSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void USandSimulationSubsystem::RegisterSurface(USandSurfaceComponent* Surface)
{
	Surfaces.AddUnique(Surface);
}

void USandSimulationSubsystem::UnregisterSurface(USandSurfaceComponent* Surface)
{
	Surfaces.Remove(Surface);
}

USandSurfaceComponent* USandSimulationSubsystem::FindSurfaceAt(const FVector& World, float MarginCm) const
{
	for (const TWeakObjectPtr<USandSurfaceComponent>& Surface : Surfaces)
	{
		if (Surface.IsValid() && Surface->IsInsidePatch(World, MarginCm))
		{
			return Surface.Get();
		}
	}
	return nullptr;
}

void USandSimulationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	FString HopList;
	if (FParse::Param(FCommandLine::Get(), TEXT("SandDeformationSelfTest")) || FParse::Param(FCommandLine::Get(), TEXT("SandDeformationLevelTest")) || FParse::Value(FCommandLine::Get(), TEXT("SandDeformationLevelHop="), HopList))
	{
		InWorld.SpawnActor<ASandSelfTest>();
	}
}
