// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SandInteraction.h"
#include "SandSurfaceComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class UTextureRenderTarget2D;

DECLARE_LOG_CATEGORY_EXTERN(LogSandDeformation, Log, All);

UENUM(BlueprintType)
enum class ESandBrush : uint8
{
	/** A landing / impact: a round dent with a raised ring and a faint shock ring. Radius = half width of the dent. */
	Round,
	/** A foot-shaped print pointing along the direction (yaw). Radius = half width; the length is about 2.4 x Radius. */
	Foot
};

/** What the height texture holds right now. Only read back for tests and debugging (it stalls the GPU). */
struct FSandHeightStats
{
	int32 DentPixels = 0;		// pixels pressed down by more than 1 cm
	int32 RidgePixels = 0;		// pixels pushed up by more than 0.5 cm
	float MinCm = 0.f;
	float MaxCm = 0.f;
	FVector2D DentMinWorld = FVector2D::ZeroVector;
	FVector2D DentMaxWorld = FVector2D::ZeroVector;
	double SumCm = 0.0;
	double DentSumCm = 0.0;
	double RidgeSumCm = 0.0;
	float MaxGradient = 0.f;	// steepest height difference between neighbouring pixels (world cm per cm)
	FVector2D DentCentroid = FVector2D::ZeroVector;
	int32 NonFinite = 0;		// pixels that are NaN or beyond the stored range
};

/** One footprint or landing queued for the GPU. */
struct FSandStampRequest
{
	FVector2D World = FVector2D::ZeroVector;
	FVector2D Dir = FVector2D(1.0, 0.0);
	float HalfLenCm = 10.f;
	float HalfWidthCm = 5.f;
	float DepthCm = 2.f;
	float RidgeCm = 1.f;
	float Push = 0.f;
	bool bRound = false;
	int32 Shape = -1;	// 0 foot, 1 round, 2 segment (wheel track), 3 box; -1 = from bRound
	int32 Flags = 0;	// 1 = spherical cap profile, 2 = accumulate (add to the current depth, saturating at MaxDepthCm)
	float MaxDepthCm = 0.f;
	float Seed = 0.f;
};

/** A tile of the patch mesh was (re)built. OldTile is (MAX_int32, MAX_int32) when the slot was empty. Tiles are TileSizeCm squares on the world grid (tile i covers x = i*TileSizeCm ...). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSandTileBuiltSignature, FIntPoint, OldTile, FIntPoint, NewTile);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSandPatchScrolledSignature, FIntPoint, OldOriginTile, FIntPoint, NewOriginTile);

/**
 * A sand surface: a square patch of dense mesh around a point (or around an actor that it follows), with a GPU heightfield on top.
 * Add it to any actor. The patch draws sand with SurfaceMaterial, whose world position offset, normals and colour read the heightfield
 * (parameters TrailRT, TrailArea, TrailRange, TrailDisplace, TrailShade: see the README). Only the rendering changes, never the collision.
 *
 * Data flow: ApplyDeformation / stamps from USandInteractionComponent -> queued in world space -> compute pass writes the stamp shapes into the
 * second of two render targets (ping-pong) -> simulation steps (fade, wind, slope collapse) -> the material reads the newest target.
 * Height is stored in cm as 0.5 + h / HeightRangeCm (0.5 = untouched sand, below = pressed down).
 *
 * Moving patch: the patch is TilesPerSide x TilesPerSide tiles of TileSizeCm. When the follow target is more than ScrollThresholdCm from the
 * patch centre, the patch origin moves by whole tiles: the heightfield is copied shifted on the GPU (overlap kept, new strip = untouched sand), the
 * material gets the new origin, and the tiles that left the patch are rebuilt on the other side, TilesPerFrame per tick. Marks that leave the
 * patch are not stored.
 */
UCLASS(ClassGroup=(SandDeformation), meta=(BlueprintSpawnableComponent), BlueprintType)
class SANDDEFORMATION_API USandSurfaceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USandSurfaceComponent();

	// ---- area ----
	/** Centre of the patch at the start (world cm). With a follow target the patch moves; this is where it starts. Snapped to the tile grid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Area")
	FVector2D PatchCenter = FVector2D::ZeroVector;

	/** Side of the patch (cm). Rounded to a whole number of tiles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Area", meta=(ClampMin="200"))
	float PatchSizeCm = 1600.f;

	/** Side of one mesh tile (cm); the patch moves in steps of this size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Area", meta=(ClampMin="100"))
	float TileSizeCm = 400.f;

	/** Distance between the patch vertices (cm); a print can only be as fine as this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Area", meta=(ClampMin="1"))
	float GridCm = 2.5f;

	/** Heightfield resolution (texels per side). The patch scrolls only when this is a multiple of the tiles per side. Memory: 2 x R x R x 16 bytes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Area", meta=(ClampMin="64", ClampMax="4096"))
	int32 HeightfieldResolution = 1024;

	/** Height range of the texture in cm (+/- half of it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Area")
	float HeightRangeCm = 32.f;

	/** true: the patch follows FollowTarget (or the local player's pawn when none is set). false: it stays where it started. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Follow")
	bool bFollowTarget = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Follow")
	bool bAutoFollowLocalPlayer = true;

	/** The patch scrolls when the target is farther than this from the patch centre (cm, in X or Y). Keep it below half the patch size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Follow", meta=(ClampMin="50"))
	float ScrollThresholdCm = 400.f;

	/** Mesh tiles rebuilt per tick after a scroll. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Follow", meta=(ClampMin="1"))
	int32 TilesPerFrame = 1;

	// ---- ground and material ----
	/** Sand material. It must read the heightfield (see README). Empty = the plugin's M_SandDeformation_Default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Material")
	TObjectPtr<UMaterialInterface> SurfaceMaterial;

	/** Strength of the normals derived from the heightfield. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Material")
	float NormalStrength = 1.8f;

	/** Darkening per cm of depression. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Material")
	float DepthDarkening = 0.012f;

	/** 1 = the patch vertices follow the heightfield; 0 = only the shading changes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Material")
	float DisplacementScale = 1.f;

	/** The texture coordinates of the patch are world position / this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Material")
	float UVScaleCm = 400.f;

	/** The patch vertices take their height from a line trace down onto this channel (unless GroundSampler is set), from GroundSearchCm above to GroundSearchCm below this component's owner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Ground")
	TEnumAsByte<ECollisionChannel> GroundTraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Ground")
	float GroundSearchCm = 3000.f;

	/** The patch floats this far above the ground. 0 = on the ground (the base surface must then be hidden under the patch: OnTileBuilt). Above 0 the base can stay visible; the rim is blended down over EdgeBlendCm (only while the patch does not move). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Ground")
	float GroundOffsetCm = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Ground")
	float EdgeBlendCm = 250.f;

	/**
	 * The same lift, done by the material instead of the mesh, so it works for a patch that moves: the patch mesh lies on the ground and the material (parameters
	 * TrailLift and TrailEdge) raises it by this many cm, blended to 0 over MaterialEdgeBlendCm at the rim of the current patch. Use it over a ground that cannot be
	 * hidden (a Landscape). Keep MaxDepressionCm below it. Needs a material with those parameters; 0 = off.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Ground")
	float MaterialLiftCm = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Ground")
	float MaterialEdgeBlendCm = 300.f;

	/** Ground heights are sampled every N cells and interpolated (4 = 16 times fewer traces / sampler calls). 0 = every cell with a GroundSampler, 4 for line traces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Ground")
	int32 GroundSampleStride = 0;

	// ---- deformation ----
	/** Deepest a dent gets (cm). With GroundOffsetCm keep it below that. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Deformation", meta=(ClampMin="0.1"))
	float MaxDepressionCm = 9.f;

	/** Tallest a raised rim gets (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Deformation")
	float MaxRidgeCm = 3.f;

	/** Depth (cm) that ApplyDeformation makes at Strength 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Deformation")
	float DepthPerStrengthCm = 3.f;

	/** Rim height per cm of depth (about the volume the dent displaces; the rest is compacted), for a foot and for a round dent (its ring is wide, so lower). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Deformation")
	float FootRidgeRatio = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Deformation")
	float RoundRidgeRatio = 0.25f;

	/** The same for a wheel track and a box contact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Deformation")
	float ContactRidgeRatio = 0.35f;

	/** An impact of this speed (cm/s) is as deep as Strength says; the depth grows with the square root of the speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Deformation")
	float ImpactReferenceSpeed = 500.f;

	/** Requests waiting for the next GPU pass; more are dropped (counted) so a flood of contacts cannot grow without bound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Deformation")
	int32 MaxPendingStamps = 256;

	// ---- simulation ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Simulation")
	bool bSimulationEnabled = true;

	/** Simulation step (s), independent of the frame rate; at most MaxSubSteps steps per frame (a long frame drops the rest). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Simulation")
	float SimInterval = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Simulation")
	int32 MaxSubSteps = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Simulation")
	bool bRecoveryEnabled = true;

	/** Seconds until a mark of RecoveryReferenceCm depth has faded out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Simulation")
	float RecoverySeconds = 240.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Simulation")
	float RecoveryReferenceCm = 4.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Wind")
	FVector2D WindDirection = FVector2D(1.f, 0.35f);

	/** 0 = calm, 1 = strongest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Wind", meta=(ClampMin="0", ClampMax="1"))
	float WindStrength = 0.3f;

	/** The fade runs (1 + this x WindStrength) times faster. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Wind")
	float WindRecoveryMultiplier = 1.5f;

	/** Speed (cm/s) at which the wind carries the height deviation downwind at WindStrength 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Wind")
	float WindTransportStrength = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Slope")
	bool bSlopeCollapse = true;

	/** Sand steeper than this angle (degrees, between neighbouring texels) slides downhill. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Slope")
	float SlopeThresholdDeg = 18.f;

	/** Fraction of the slope above the threshold that moves per step (at most 0.2 so the exchange converges). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Slope")
	float SlopeRelaxRate = 0.15f;

	/** Most sand one pair of neighbouring texels exchanges in one step (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Slope")
	float MaxSlopeTransferCm = 0.4f;

	/** Tag that marks an actor as sand for USandInteractionComponent (the floor under a character). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Surface")
	FName SandActorTag = TEXT("SandSurface");

	/** true: any floor inside the patch counts as sand. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Surface")
	bool bAnyFloorIsSand = false;

	/** Initialize in BeginPlay. false = call InitializeSurface yourself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Surface")
	bool bAutoInitialize = true;

	UPROPERTY(BlueprintAssignable, Category="Sand")
	FSandTileBuiltSignature OnTileBuilt;

	UPROPERTY(BlueprintAssignable, Category="Sand")
	FSandPatchScrolledSignature OnPatchScrolled;

	/** C++ only: height of the ground at a world position; return false when there is none. Replaces the line trace (used for analytic terrain and landscapes). */
	TFunction<bool(float X, float Y, float& OutZ)> GroundSampler;

	/** C++ only: extra test for what counts as sand (besides the tag and bAnyFloorIsSand). */
	TFunction<bool(const AActor*)> SandFloorPredicate;

	// ---- Blueprint API ----
	/** Builds the patch and the GPU resources and registers the surface. Safe to call twice. Returns false when the platform has no compute support or the material is missing. */
	UFUNCTION(BlueprintCallable, Category="Sand")
	bool InitializeSurface();

	/**
	 * Requests a deformation. Radius (cm) and Strength (1 = DepthPerStrengthCm deep) are clamped to sane values; DirectionYawDeg is where a foot points.
	 * RidgeScale 0 = no raised rim. Returns false when the surface is not ready, the position is outside the patch or the inputs are not finite.
	 */
	/**
	 * The one entry point for every kind of contact (foot, wheel, box, landing). Validates the request and queues a stamp for the shared GPU heightfield.
	 * Returns false (and counts why) when the surface is not ready, a number is not finite, nothing would be pressed, or the contact is outside the patch.
	 */
	UFUNCTION(BlueprintCallable, Category="Sand")
	bool ApplyInteraction(const FSandInteractionRequest& Request);

	UFUNCTION(BlueprintCallable, Category="Sand")
	bool ApplyDeformation(FVector WorldPosition, float Radius = 30.f, float Strength = 1.f, ESandBrush Brush = ESandBrush::Round, float DirectionYawDeg = 0.f, float RidgeScale = 1.f);

	UFUNCTION(BlueprintCallable, Category="Sand")
	void SetFollowTarget(AActor* Target);

	UFUNCTION(BlueprintCallable, Category="Sand")
	void SetSimulationEnabled(bool bEnabled) { bSimulationEnabled = bEnabled; }

	UFUNCTION(BlueprintCallable, Category="Sand")
	void SetWind(FVector2D Direction, float Strength);

	/** Wipes the heightfield flat. */
	UFUNCTION(BlueprintCallable, Category="Sand")
	void ResetHeightfield();

	UFUNCTION(BlueprintPure, Category="Sand")
	bool IsSurfaceReady() const { return bReady; }

	UFUNCTION(BlueprintPure, Category="Sand")
	FVector2D GetPatchCenter() const;

	UFUNCTION(BlueprintPure, Category="Sand")
	bool IsInsidePatch(FVector WorldPosition, float MarginCm = 0.f) const;

	/**
	 * How far (cm) the rendered sand lies above the ground at this position when it is untouched: the lift the material (MaterialLiftCm) or the floating mesh (GroundOffsetCm) gives it,
	 * blended to 0 at the rim of the patch. The collision stays on the ground, so a character's feet are this much below the rendered surface.
	 */
	UFUNCTION(BlueprintPure, Category="Sand")
	float GetRenderedLiftAt(FVector WorldPosition) const;

	/** Height of the heightfield (cm) at a position. Reads the GPU texture back: for tests and debugging only. */
	UFUNCTION(BlueprintCallable, Category="Sand|Debug")
	float SampleHeightCm(FVector WorldPosition) const;

	UFUNCTION(BlueprintCallable, Category="Sand|Debug")
	bool SaveHeightfieldPNG(const FString& Path) const;

	// ---- C++ API ----
	/** Queues a stamp (world space; converted when the batch is sent, so a scroll in between is harmless). Returns false when outside the patch. */
	bool QueueStamp(const FSandStampRequest& Request);
	void FlushStamps();

	bool IsSandFloor(const AActor* Actor) const;
	FVector2D WorldToUV(const FVector2D& World) const;
	float GetDepthLimit() const { return FMath::Min(MaxDepressionCm, HeightRangeCm * 0.48f); }
	float GetTexelCm() const { return PatchSizeCm / FMath::Max(HeightfieldResolution, 1); }
	FIntPoint GetOriginTile() const { return OriginTile; }
	bool CanScroll() const { return bCanScroll; }

	FSandHeightStats ReadStats() const;
	/** Pixels that differ from untouched sand. */
	float GetMemoryMB() const;
	int32 GetPatchVertices() const { return PatchVertices; }
	int32 GetStampCount() const { return StampsQueued; }
	int32 GetRequestCount(ESandBrushType Brush) const { return RequestsByBrush[static_cast<int32>(Brush)]; }
	int32 GetDroppedStamps() const { return DroppedStamps; }
	int32 GetOutsideRejected() const { return OutsideRejected; }
	int32 GetInvalidRejected() const { return InvalidRejected; }
	int32 GetSimSteps() const { return SimSteps; }
	int32 GetScrollCount() const { return ScrollCount; }
	int32 GetTilesBuilt() const { return TilesBuilt; }
	int32 GetPendingTiles() const { return PendingTiles.Num(); }
	bool IsSimComputeActive() const { return bReady; }
	/** The heightfield stays frozen (no fade, wind or collapse) while paused. */
	void SetSimPaused(bool bPause) { bSimPaused = bPause; }
	UTextureRenderTarget2D* GetHeightfield() const { return Targets.IsValidIndex(Current) ? Targets[Current].Get() : nullptr; }
	UMaterialInstanceDynamic* GetSurfaceMaterial() const { return SurfaceMID; }
	/** Moves the patch right now so that its centre is near the position (whole tiles). */
	void ScrollToward(const FVector2D& World);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	FVector2D TileCorner(FIntPoint Tile) const { return LatticeOffset + FVector2D(Tile.X * TileSizeCm, Tile.Y * TileSizeCm); }
	FVector2D OriginXY() const { return TileCorner(OriginTile); }
	int32 SlotOf(FIntPoint Tile) const;
	void BuildTile(FIntPoint Tile);
	void RefreshPendingTiles();
	void ScrollTo(FIntPoint NewOrigin);
	void UpdateFollow();
	void StepSimulation(float DeltaSeconds);
	void SetTargetParam();
	bool SampleGround(float X, float Y, float& OutZ) const;

	/** The plugin's default material, held by the class default object so that cooking includes it (a path loaded at run time would not be cooked). */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> DefaultSurfaceMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Mesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SurfaceMID;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextureRenderTarget2D>> Targets;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> FollowTarget;

	bool bReady = false;
	bool bCanScroll = false;
	bool bSimPaused = false;
	int32 Current = 0;
	int32 TilesPerSide = 4;
	int32 CellsPerTile = 160;
	int32 TileTexels = 256;
	FIntPoint OriginTile = FIntPoint::ZeroValue;
	FVector2D LatticeOffset = FVector2D::ZeroVector;	// world position of tile (0,0); zero for a patch that follows
	FIntPoint BakedOriginTile = FIntPoint::ZeroValue;	// the tile origin the rim blend was baked for
	TArray<FIntPoint> SlotTile;		// the world tile each mesh slot holds
	TArray<FIntPoint> PendingTiles;
	TArray<FSandStampRequest> Pending;
	float SimAccum = 0.f;
	float SimActive = 0.f;
	int32 SimSteps = 0;
	int32 StampsQueued = 0;
	int32 RequestsByBrush[5] = { 0, 0, 0, 0, 0 };
	int32 DroppedStamps = 0;
	int32 OutsideRejected = 0;
	int32 InvalidRejected = 0;
	int32 ScrollCount = 0;
	int32 TilesBuilt = 0;
	int32 PatchVertices = 0;
	int32 StampSeed = 0;
};

