// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DuneSandTrail.generated.h"

class ALandscapeProxy;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class UTextureRenderTarget2D;

/** What the height texture holds right now; only read back for tests and debugging (it stalls the GPU). */
struct FSandTrailStats
{
	int32 DentPixels = 0;		// pixels pressed down by more than 1 cm
	int32 RidgePixels = 0;		// pixels pushed up by more than 0.5 cm
	float MinCm = 0.f;
	float MaxCm = 0.f;
	FVector2D DentMinWorld = FVector2D::ZeroVector;	// bounding box of the pressed-down pixels
	FVector2D DentMaxWorld = FVector2D::ZeroVector;
	double SumCm = 0.0;		// sum of all heights: the sand the dents took out and the ridges put back (cm x pixels)
	double DentSumCm = 0.0;	// pressed-down part only (negative)
	double RidgeSumCm = 0.0;	// pushed-up part only
	float MaxGradient = 0.f;	// steepest height difference between neighbouring pixels, as rise per run (world cm per cm)
	FVector2D DentCentroid = FVector2D::ZeroVector;	// height-weighted centre of the dents (world)
	int32 NonFinite = 0;	// pixels that are NaN or beyond the stored range
};

/**
 * Project DUNE: the player's trail in the sand. Landscape vertices are 100 cm apart, too coarse for a footpath, so the actor lays a
 * dense patch of mesh (a procedural mesh that follows the landscape height, M_DUNE_SandTrail = the sand material plus the trail) over
 * a limited area of the sand. Only the rendering changes: the landscape and its collision stay exactly as they were.
 *
 * Data flow: character movement -> sand check (the floor under the feet is the landscape) -> a brush segment from the last
 * recorded point to the current one -> M_DUNE_SandTrailComposite draws "previous height + brush" into the second of two render
 * targets (ping-pong) -> the sand material reads the newest one: it moves the patch vertices (world position offset) and shades the
 * normals and colour from it. Height is stored in cm as 0.5 + h / HeightRangeCm (0.5 = untouched sand, below = pressed down).
 *
 * Day 17: the brush strength follows the actual speed and weight (walk / run) and a landing (vertical speed before touching down); the height
 * is then simulated on the GPU every SimInterval (compute shaders, Shaders/DuneSandSim.usf): trails fade, the wind carries the height deviation
 * downwind, and sand steeper than the repose angle slides downhill. Only the rendering changes, never the collision.
 */
UCLASS()
class ADuneSandTrail : public AActor
{
	GENERATED_BODY()

public:
	ADuneSandTrail();

	/** Centre and size of the sand area that records trails (world cm). Only this area can show a trail. */
	UPROPERTY(EditAnywhere, Category="Trail|Area")
	FVector2D PatchCenter = FVector2D(-1400.f, -900.f);

	UPROPERTY(EditAnywhere, Category="Trail|Area")
	float PatchSizeCm = 2400.f;

	/** Distance between the patch vertices; the trail can only be as fine as this. */
	UPROPERTY(EditAnywhere, Category="Trail|Area")
	float GridCm = 12.f;

	UPROPERTY(EditAnywhere, Category="Trail|Area")
	int32 RenderTargetSize = 1024;

	/** The patch floats this far above the landscape (it must lie above the landscape for the dent to show); fades to 0 over EdgeBlendCm. */
	UPROPERTY(EditAnywhere, Category="Trail|Area")
	float LiftCm = 7.f;

	UPROPERTY(EditAnywhere, Category="Trail|Area")
	float EdgeBlendCm = 250.f;

	/** Half width of the pressed-down centre (cm) at walking speed. */
	UPROPERTY(EditAnywhere, Category="Trail")
	float TrailWidth = 28.f;

	/** Deepest the centre gets when running at full speed (cm); walking over it again never makes it deeper. Keep it below LiftCm. */
	UPROPERTY(EditAnywhere, Category="Trail")
	float TrailDepth = 5.5f;

	/** Tallest the pushed-up edge beside a dent may get (cm). */
	UPROPERTY(EditAnywhere, Category="Trail")
	float EdgeRidgeStrength = 3.f;

	/** Share of the sand a dent displaces that is piled up beside it (the rest is "compacted"). The ridge height follows from the dent volume and its shape, so a deeper dent gets a taller ridge. Approximate: the shapes are fixed, no grains are tracked. */
	UPROPERTY(EditAnywhere, Category="Trail")
	float RidgeVolumeFraction = 0.6f;

	/** Pressure model: DeformationStrength = weight / ReferenceWeightKg x SandSoftness x movement x contact (depth, width and ridge all scale with it). */
	UPROPERTY(EditAnywhere, Category="Trail|Pressure")
	float ReferenceWeightKg = 80.f;

	/** Weight of a character that is not an AHoldItCharacter (kg). */
	UPROPERTY(EditAnywhere, Category="Trail|Pressure")
	float DefaultWeightKg = 80.f;

	/** How soft this sand is (the only ground material the patch has). */
	UPROPERTY(EditAnywhere, Category="Trail|Pressure")
	float SandSoftness = 1.f;

	/** From this actual horizontal speed (cm/s) on, the stamp counts as running, below it as walking. */
	UPROPERTY(EditAnywhere, Category="Trail|Pressure")
	float RunSpeedThreshold = 380.f;

	/** Landings slower than this (cm/s, vertical speed just before touching down) leave nothing. */
	UPROPERTY(EditAnywhere, Category="Trail|Landing")
	float LandingMinSpeed = 200.f;

	/** Vertical speed (cm/s) of the strongest landing; faster is clamped (about a 8.6 m fall). */
	UPROPERTY(EditAnywhere, Category="Trail|Landing")
	float LandingMaxSpeed = 1300.f;

	/** Depth (cm) and half width (cm) of the weakest and strongest landing. MaxDepth must stay below LiftCm. */
	UPROPERTY(EditAnywhere, Category="Trail|Landing")
	float LandingMinDepth = 1.5f;
	UPROPERTY(EditAnywhere, Category="Trail|Landing")
	float LandingMaxDepth = 6.3f;
	UPROPERTY(EditAnywhere, Category="Trail|Landing")
	float LandingMinWidth = 40.f;
	UPROPERTY(EditAnywhere, Category="Trail|Landing")
	float LandingMaxWidth = 110.f;

	/** The heightfield runs on the GPU (compute shaders through the render graph). false = the old material pass for fading only (no wind, no slope). */
	UPROPERTY(EditAnywhere, Category="Trail|Simulation")
	bool bUseComputeSim = true;

	/** Simulation step (s), independent of the frame rate; at most MaxSubSteps steps per frame (a long frame drops the rest instead of catching up). */
	UPROPERTY(EditAnywhere, Category="Trail|Simulation")
	float SimInterval = 0.05f;

	UPROPERTY(EditAnywhere, Category="Trail|Simulation")
	int32 MaxSubSteps = 4;

	/** Trails fade at TrailDepth/TrailPersistence cm per second, faster with wind. false only for tests. */
	UPROPERTY(EditAnywhere, Category="Trail|Simulation")
	bool bRecoveryEnabled = true;

	/** Wind blows towards this direction in world X/Y (it is turned into texel directions: the patch is axis aligned, so they are the same). */
	UPROPERTY(EditAnywhere, Category="Trail|Wind")
	FVector2D WindDirection = FVector2D(1.f, 0.35f);

	/** 0 = calm, 1 = strongest. */
	UPROPERTY(EditAnywhere, Category="Trail|Wind")
	float WindStrength = 0.3f;

	/** The fade runs (1 + this x WindStrength) times faster. */
	UPROPERTY(EditAnywhere, Category="Trail|Wind")
	float WindRecoveryMultiplier = 1.5f;

	/** Speed (cm/s) at which the wind carries the height deviation downwind at WindStrength 1. */
	UPROPERTY(EditAnywhere, Category="Trail|Wind")
	float WindTransportStrength = 12.f;

	/** Sand steeper than this angle (degrees, between neighbouring texels) slides downhill. The terrain under the patch is not part of it. */
	UPROPERTY(EditAnywhere, Category="Trail|Slope")
	float SlopeThresholdDeg = 18.f;

	UPROPERTY(EditAnywhere, Category="Trail|Slope")
	bool bSlopeCollapse = true;

	/** Fraction of the slope above the threshold that moves per step (keep it at or below 0.2 so the exchange converges). */
	UPROPERTY(EditAnywhere, Category="Trail|Slope")
	float SlopeRelaxRate = 0.15f;

	/** Most sand one pair of neighbouring texels exchanges in one step (cm). */
	UPROPERTY(EditAnywhere, Category="Trail|Slope")
	float MaxSlopeTransferCm = 0.4f;

	/** Throttled log of the contact, pressure, wind and dispatch state (every 2 s); -DuneSandDebug turns it on. */
	UPROPERTY(EditAnywhere, Category="Trail|Debug")
	bool bDebugLog = false;

	/** A new brush segment is recorded after this much movement (cm). */
	UPROPERTY(EditAnywhere, Category="Trail")
	float TrailSpacing = 18.f;

	/** At most this often (s). */
	UPROPERTY(EditAnywhere, Category="Trail")
	float TrailUpdateInterval = 0.033f;

	/** Seconds until a trail has faded out completely. */
	UPROPERTY(EditAnywhere, Category="Trail")
	float TrailPersistence = 90.f;

	/** Below this horizontal speed (cm/s) nothing is recorded. */
	UPROPERTY(EditAnywhere, Category="Trail")
	float MinMovementSpeed = 40.f;

	/** At this speed the trail is widest and deepest; slower it is narrower and shallower. */
	UPROPERTY(EditAnywhere, Category="Trail")
	float FullTrailSpeed = 500.f;

	/** Height range of the texture in cm (+/- half of it). */
	UPROPERTY(EditAnywhere, Category="Trail")
	float HeightRangeCm = 32.f;

	/** 1 = the patch vertices follow the trail height; 0 = only the shading changes (to tell the two apart in a screenshot). */
	UPROPERTY(EditAnywhere, Category="Trail")
	float DisplacementScale = 1.f;

	/** World position -> 0..1 position on the patch / render target. Kept separate from the drawing so the area can grow later. */
	FVector2D WorldToTrailUV(const FVector& World) const;
	bool IsInsidePatch(const FVector& World, float MarginCm = 0.f) const;

	/** Height (cm) of the trail texture at a world position; reads the GPU texture back, for tests only. */
	float SampleHeightCm(const FVector& World) const;
	FSandTrailStats ReadStats() const;

	/** Height (cm) of the ridge that holds RidgeVolumeFraction of the sand a dent of this depth displaces: a trench (along a path) or a round dent (a landing). */
	float VolumeRidge(float DepthCm, bool bRound) const;

	/** Test / debug: one brush at a world position as if something had pressed there (width = half width of the pressed centre in cm, ridge = height of the pushed-up ring). */
	void DebugStamp(const FVector2D& WorldXY, float WidthCm, float DepthCm, float RidgeCm);
	/** Test / debug: wipes the heightfield flat. */
	void DebugReset();
	/** Test / debug: the heightfield stays frozen (no fade, wind or collapse) while true. */
	void SetSimPaused(bool bPause) { bSimPaused = bPause; }

	int32 GetSimSteps() const { return SimSteps; }
	bool IsComputeActive() const { return bComputeReady; }
	int32 GetWalkStamps() const { return WalkStamps; }
	int32 GetRunStamps() const { return RunStamps; }
	int32 GetLandingStamps() const { return LandingStamps; }
	int32 GetLandingsIgnored() const { return LandingsIgnored; }
	/** Of the last stamp: what it was, its centre depth (cm) and half width (cm). */
	float GetLastDepth() const { return LastDepth; }
	float GetLastWidth() const { return LastWidth; }
	float GetLastSpeed() const { return LastSpeed; }
	const TCHAR* GetLastKind() const { return LastKind; }

	int32 GetBrushUpdates() const { return BrushUpdates; }
	int32 GetRecoveryPasses() const { return RecoveryPasses; }
	int32 GetSkippedStill() const { return SkippedStill; }
	int32 GetSkippedAir() const { return SkippedAir; }
	int32 GetSkippedNotSand() const { return SkippedNotSand; }
	int32 GetSkippedOutside() const { return SkippedOutside; }
	bool IsReady() const { return Patch != nullptr && BrushMID != nullptr; }
	float GetTrailMemoryMB() const;
	int32 GetPatchVertices() const { return PatchVertices; }
	/** Is the character standing on the sand right now (floor = the landscape, inside the patch)? */
	bool IsOnSand(const class ACharacter* Character) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void BuildPatch();
	/** Pressure model for a step of the character: speed (cm/s) -> half width, centre depth and ridge height in cm. */
	void WalkBrush(float Speed, float ContactFactor, float Weight, float& Width, float& Depth, float& Ridge) const;
	void Stamp(const FVector& From, const FVector& To, float Width, float Depth, float Ridge, const TCHAR* Kind, float Speed);
	void Recover(float Seconds);
	void StepSimulation(float DeltaSeconds);
	float GetCharacterWeight(const class ACharacter* Character) const;
	UFUNCTION()
	void OnCharacterLanded(const FHitResult& Hit);
	UTextureRenderTarget2D* MakeTarget();
	void DrawPass(const FVector2D& A, const FVector2D& B, float WidthUV, float Depth, float Ridge, float RecoverCm);

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Patch;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> SandMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> CompositeMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> SurfaceMID;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BrushMID;

	UPROPERTY()
	TArray<TObjectPtr<UTextureRenderTarget2D>> Targets;

	int32 Current = 0;		// the target that holds the newest height
	bool bHasLast = false;
	FVector LastPoint = FVector::ZeroVector;
	float SinceUpdate = 0.f;
	float SinceStamp = 1e6f;
	float Recovered = 1e6f;	// how much the fade has already taken off since the last brush; a huge start value = nothing to fade
	float SinceRecover = 0.f;
	float SpeedAlphaLast = 0.f;
	int32 BrushUpdates = 0;
	int32 RecoveryPasses = 0;
	int32 SkippedStill = 0;
	int32 SkippedAir = 0;
	int32 SkippedNotSand = 0;
	int32 SkippedOutside = 0;
	int32 PatchVertices = 0;
	float LogTimer = 0.f;

	bool bComputeReady = false;
	bool bSimPaused = false;
	float SimAccum = 0.f;
	float SimActive = 0.f;		// seconds of simulation left; 0 = the heightfield is flat and nothing is dispatched
	int32 SimSteps = 0;
	int32 WalkStamps = 0;
	int32 RunStamps = 0;
	int32 LandingStamps = 0;
	int32 LandingsIgnored = 0;
	float LastDepth = 0.f;
	float LastWidth = 0.f;
	float LastSpeed = 0.f;
	const TCHAR* LastKind = TEXT("none");

	/** Fastest downward speed of the current fall (the velocity is already gone when the landing event comes), and the landing waiting for the next Tick. */
	float FallSpeed = 0.f;
	float PendingLandSpeed = 0.f;
	FVector PendingLandPos = FVector::ZeroVector;
	bool bPendingLand = false;
	TWeakObjectPtr<AActor> BoundCharacter;
};
