// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SandSurfaceComponent.h"
#include "SandDeformationLibrary.generated.h"

/** Blueprint / C++ entry points that need no component on the caller. */
UCLASS()
class SANDDEFORMATION_API USandDeformationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Presses a mark into the sand at a world position, from any actor or Blueprint. Uses Surface, or the surface whose patch contains the position.
	 * Radius (cm) is the half width of the dent, Strength 1 = DepthPerStrengthCm deep. Returns false (and does nothing) when no surface is
	 * there, the surface is not ready, or an input is not a finite number.
	 */
	UFUNCTION(BlueprintCallable, Category="Sand", meta=(WorldContext="WorldContextObject", AdvancedDisplay="DirectionYawDeg,Surface"))
	static bool ApplySandDeformation(const UObject* WorldContextObject, FVector WorldPosition, float Radius = 30.f, float Strength = 1.f, ESandBrush Brush = ESandBrush::Round, float DirectionYawDeg = 0.f, USandSurfaceComponent* Surface = nullptr);

	/** The general form: any kind of contact as a FSandInteractionRequest. Uses Surface, or the surface whose patch contains Request.WorldPosition. */
	UFUNCTION(BlueprintCallable, Category="Sand", meta=(WorldContext="WorldContextObject", AdvancedDisplay="Surface"))
	static bool ApplySandInteraction(const UObject* WorldContextObject, const FSandInteractionRequest& Request, USandSurfaceComponent* Surface = nullptr);

	/** Convenience: a landing or a hit (round crater, depth from ImpactSpeed in cm/s). */
	UFUNCTION(BlueprintCallable, Category="Sand", meta=(WorldContext="WorldContextObject", AdvancedDisplay="Surface"))
	static bool ApplySandImpact(const UObject* WorldContextObject, FVector WorldPosition, float Radius = 40.f, float ImpactSpeed = 500.f, float Strength = 1.f, USandSurfaceComponent* Surface = nullptr);

	/** Convenience: a body pressing on the sand (a box of HalfLength x HalfWidth cm along YawDeg; HalfLength 0 = round). Strength 1 = DepthPerStrengthCm. */
	UFUNCTION(BlueprintCallable, Category="Sand", meta=(WorldContext="WorldContextObject", AdvancedDisplay="Surface"))
	static bool ApplySandPressure(const UObject* WorldContextObject, FVector WorldPosition, float HalfLength = 40.f, float HalfWidth = 40.f, float YawDeg = 0.f, float Strength = 1.f, USandSurfaceComponent* Surface = nullptr);

	/** Convenience: a continuous track (wheel, runner, dragged object) from From to To, HalfWidth wide. */
	UFUNCTION(BlueprintCallable, Category="Sand", meta=(WorldContext="WorldContextObject", AdvancedDisplay="Surface"))
	static bool ApplySandTrail(const UObject* WorldContextObject, FVector From, FVector To, float HalfWidth = 12.f, float Strength = 1.f, USandSurfaceComponent* Surface = nullptr);

	/** The initialized surface whose patch contains the position, or null. */
	UFUNCTION(BlueprintPure, Category="Sand", meta=(WorldContext="WorldContextObject"))
	static USandSurfaceComponent* FindSandSurface(const UObject* WorldContextObject, FVector WorldPosition);

	/** false in a build or run without compute shaders (null RHI, below SM5): the sand then stays flat. */
	UFUNCTION(BlueprintPure, Category="Sand")
	static bool IsSandDeformationSupported();
};
