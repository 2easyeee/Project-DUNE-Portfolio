// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SandSurfaceComponent.h"
#include "DuneSandTrail.generated.h"

class ADuneSandTerrain;
class USandInteractionComponent;

using FSandTrailStats = FSandHeightStats;

/**
 * Project DUNE: the glue between the game and the SandDeformation plugin (Day 19). The sand itself (heightfield, GPU simulation, patch mesh, footprints,
 * landings, moving patch) lives in the plugin; this actor only knows the game:
 *   - it creates a USandSurfaceComponent and a USandInteractionComponent and configures them for the level: the sand test level (ADuneSandTerrain: analytic
 *     ground, moving patch that hides the terrain tiles it covers) or the older levels (a Landscape: a patch that floats 7 cm above it and stays);
 *   - it points the interaction at the player pawn and feeds it the weight of AHoldItCharacter;
 *   - it keeps the accessors the self tests (DuneSandSimTest, DunePortfolioShots, the DuneGameMode sand test) use.
 * The material is the project's M_DUNE_SandTrail (the sand material plus the plugin's heightfield parameters).
 */
UCLASS()
class ADuneSandTrail : public AActor
{
	GENERATED_BODY()

public:
	ADuneSandTrail();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sand")
	TObjectPtr<USandSurfaceComponent> Surface;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sand")
	TObjectPtr<USandInteractionComponent> Interaction;

	/** Older levels (a Landscape): centre and size of the patch, the patch floats LiftCm above the landscape. The patch stays there. */
	UPROPERTY(EditAnywhere, Category="Trail|Landscape")
	FVector2D PatchCenter = FVector2D(-1400.f, -900.f);

	UPROPERTY(EditAnywhere, Category="Trail|Landscape")
	float PatchSizeCm = 2400.f;

	UPROPERTY(EditAnywhere, Category="Trail|Landscape")
	float GridCm = 12.f;

	UPROPERTY(EditAnywhere, Category="Trail|Landscape")
	float LiftCm = 7.f;

	UPROPERTY(EditAnywhere, Category="Trail|Landscape")
	float EdgeBlendCm = 250.f;

	/** Throttled log of the contact and dispatch state (every 2 s); -DuneSandDebug turns it on. */
	UPROPERTY(EditAnywhere, Category="Trail|Debug")
	bool bDebugLog = false;

	/** Used by VolumeRidge (the Day 17 self test): share of the sand a dent displaces that is piled up beside it, and the tallest ridge (cm). */
	UPROPERTY(EditAnywhere, Category="Trail")
	float RidgeVolumeFraction = 0.6f;

	UPROPERTY(EditAnywhere, Category="Trail")
	float EdgeRidgeStrength = 3.f;

	bool IsReady() const;
	bool IsInsidePatch(const FVector& World, float MarginCm = 0.f) const;
	float SampleHeightCm(const FVector& World) const;
	FSandTrailStats ReadStats() const;
	bool SaveHeightfieldPNG(const FString& Path) const;
	float VolumeRidge(float DepthCm, bool bRound) const;
	float GetTrailMemoryMB() const;
	int32 GetPatchVertices() const;
	bool IsOnSand(const class ACharacter* Character) const;

	/** Test / debug: a round dent at a world position (width = half width of the pressed centre in cm, ridge = height of the pushed-up ring). */
	void DebugStamp(const FVector2D& WorldXY, float WidthCm, float DepthCm, float RidgeCm);
	/** A foot-shaped print (DirDeg = where the toes point) or, with bRound, a landing dent of half width WidthCm. Depth in cm, strength 1 = the reference weight. */
	void ApplySandDeformation(const FVector2D& WorldXY, float DirDeg, float WidthCm, float DepthCm, float Strength, bool bRound);
	void DebugReset();
	void SetSimPaused(bool bPause);

	int32 GetSimSteps() const;
	bool IsComputeActive() const;
	int32 GetWalkStamps() const;
	int32 GetRunStamps() const;
	int32 GetLandingStamps() const;
	int32 GetFootStamps() const;
	int32 GetLandingsIgnored() const;
	int32 GetBrushUpdates() const;
	int32 GetRecoveryPasses() const { return 0; }
	int32 GetSkippedStill() const;
	int32 GetSkippedAir() const;
	int32 GetSkippedNotSand() const;
	int32 GetSkippedOutside() const;
	float GetLastDepth() const;
	float GetLastWidth() const;
	float GetLastSpeed() const;
	const TCHAR* GetLastKind() const;
	float GetFootBallMin(int32 Foot) const;
	float GetFootBallMax(int32 Foot) const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	TWeakObjectPtr<ADuneSandTerrain> Terrain;
	TWeakObjectPtr<AActor> BoundPawn;
};
