// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SandSimulationSubsystem.generated.h"

class USandSurfaceComponent;

/**
 * Per world list of the sand surfaces. A surface registers itself when it is initialized, so anything that wants to deform sand
 * (USandInteractionComponent, USandDeformationLibrary) finds the surface under a world position without holding a reference to it.
 * Also starts the plugin self test when the game is launched with -SandDeformationSelfTest.
 */
UCLASS()
class SANDDEFORMATION_API USandSimulationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void RegisterSurface(USandSurfaceComponent* Surface);
	void UnregisterSurface(USandSurfaceComponent* Surface);

	/** The first ready surface whose patch contains the position (inside by MarginCm), or null. */
	USandSurfaceComponent* FindSurfaceAt(const FVector& World, float MarginCm = 0.f) const;

	const TArray<TWeakObjectPtr<USandSurfaceComponent>>& GetSurfaces() const { return Surfaces; }

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	TArray<TWeakObjectPtr<USandSurfaceComponent>> Surfaces;
};
