// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "SliceableStructure.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;

/** Why a slice request did or did not work. */
UENUM(BlueprintType)
enum class ESliceResult : uint8
{
	None,
	Success,
	NoMeshData,		// no source mesh, or the mesh could not be read into the procedural component
	InvalidNormal,	// zero / NaN plane normal
	PlaneMissesMesh,	// the whole mesh lies on one side of the plane
	SliceFailed,	// the engine slice returned no second half
	PieceLimit		// the structure already has MaxPieces pieces
};

/** What a trace hit means for cutting. */
enum class ECutTarget : uint8
{
	None,		// ground, environment, a mesh that was not prepared for cutting
	Sliceable,	// a piece of an ASliceableStructure
	Convertible	// a plain static mesh actor whose mesh has Allow CPU Access on: becomes a sliceable structure on its first cut
};

/**
 * A one-off push for the pieces a cut sets free (a bullet). Momentum is in kg cm/s, so the speed a piece gets is
 * momentum / its mass: heavy pieces barely move, light ones fly. Applied at Location, so a hit away from the
 * piece's centre of mass also spins it.
 */
struct FSliceImpulse
{
	FVector Location = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	float MomentumKgCmPerSec = 0.f;
	float MaxSpeed = 350.f;	// cm/s: no piece is thrown faster than this
	/** 1 = the impulse acts at Location; less moves its point of action towards the piece centre, which calms the spin a far hit would cause. */
	float Lever = 1.f;
};

/** Everything worth looking at after one slice request. */
struct FSliceReport
{
	ESliceResult Result = ESliceResult::None;
	FString Reason;
	FVector PlanePosition = FVector::ZeroVector;
	FVector PlaneNormal = FVector::ZeroVector;
	/** Signed distance (cm) of the closest vertices on each side of the plane, world space. */
	float DistMin = 0.f;
	float DistMax = 0.f;
	int32 TrisCrossing = 0;	// triangles with corners on both sides of the plane
	int32 VertsBefore = 0, TrisBefore = 0;
	int32 VertsKept = 0, TrisKept = 0;
	int32 VertsOther = 0, TrisOther = 0;
};

/**
 * Project DUNE: a structure whose mesh can be cut by a plane at runtime.
 *
 * Mesh component: UProceduralMeshComponent (plugin "ProceduralMeshComponent", enabled for this project).
 * Its sections hold plain vertex / index arrays that can be read and rewritten, the engine library already
 * provides a plane slice with a cap section, and the result is an ordinary component. Not done yet:
 * convex collision for the halves, physics for the pieces, pooling.
 */
UCLASS()
class ASliceableStructure : public AActor
{
	GENERATED_BODY()

public:
	ASliceableStructure();

	/** Engine static mesh copied into the procedural component (Cube, Cylinder ...). Empty = nothing to cut. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice")
	TObjectPtr<UStaticMesh> SourceMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice")
	FLinearColor Tint = FLinearColor(0.75f, 0.6f, 0.35f);

	/** Colour of the cut face, so a cut is easy to tell from the original surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice")
	FLinearColor CapTint = FLinearColor(0.9f, 0.15f, 0.05f);

	/**
	 * Explicit permission for the gun to break this structure (a crack pattern that splits it into many physics pieces, see FractureAt).
	 * Off by default: a bullet only chips every other structure. It is set when a static mesh actor tagged DuneGunFractureTag is
	 * turned into a structure.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Fracture")
	bool bGunFracturable = false;

	/** Tag on a static mesh actor that allows the gun to fracture it (checked together with Allow CPU Access of its mesh). */
	static const FName DuneGunFractureTag;

	/** Half size (cm) of the damaged chunk cut out around the hit point along the wall, and its depth into the wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Fracture")
	float ChunkHalfSizeCm = 140.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Fracture")
	float ChunkDepthCm = 70.f;

	/** Cracks through the hit point, each splitting the chunk into more wedges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Fracture", meta=(ClampMin="1", ClampMax="4"))
	int32 PrimaryCracks = 3;

	/** A fragment smaller than this (cm3) is dust: it is removed instead of becoming a physics piece. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Fracture")
	float MinFragmentVolumeCm3 = 6000.f;

	/** Bullet momentum (kg cm/s) shared by the fragments; a fragment's speed change is its share divided by its mass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Fracture")
	float FractureMomentum = 500000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Fracture")
	float FractureMaxSpeed = 380.f;

	/** A fragment thinner than this (cm) in any direction is a flat sliver: dust, not a fragment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Fracture")
	float MinFragmentWidthCm = 6.f;

	/** Fragment collision hulls are this much (cm) smaller than the fragment, so that resting neighbours do not start overlapping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Fracture")
	float FractureHullInsetCm = 1.2f;

	/** No fragment spins faster than this (deg/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Fracture")
	float FractureMaxSpinDeg = 240.f;

	/** How much of the way from a fragment's centre to the hit point the bullet's impulse acts at (1 = at the hit point). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Fracture", meta=(ClampMin="0", ClampMax="1"))
	float FractureLever = 0.35f;

	/** Speed (cm/s) fragments get away from the hit point along the wall, so the cracks open. Small: no explosion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Fracture")
	float FractureOpenSpeed = 30.f;

	/**
	 * Gun fracture. A bullet at HitPoint (on a surface with outward normal Normal, travelling along BulletDirection) breaks the
	 * piece it hit: the damaged chunk around the hit is cut out of the wall (the rest stays, now with a hole), then cracks radiate
	 * from the hit point and branch, and every crack is a real cut through the mesh. Every fragment gets a convex collision and
	 * becomes a physics body; the bullet's momentum is applied at the hit point. Fragments are of very different sizes: big ones
	 * far from the hit, small ones where cracks cross near it.
	 * @return the number of fragments set free, 0 if nothing was broken
	 */
	int32 FractureAt(UProceduralMeshComponent* HitPiece, const FVector& HitPoint, const FVector& Normal, const FVector& BulletDirection);

	/** Result of the last FractureAt, for the log and the tests. */
	struct FFractureStats
	{
		int32 Fragments = 0, Large = 0, Medium = 0, Small = 0, Dust = 0, Cuts = 0, OutsidePieces = 0;
		float Milliseconds = 0.f;
		float MinVolumeCm3 = 0.f, MaxVolumeCm3 = 0.f;
	};
	const FFractureStats& GetLastFracture() const { return LastFracture; }

	/** A structure is cut into at most this many pieces; one more cut is refused. Keeps components and collision from growing without end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice", meta=(ClampMin="2"))
	int32 MaxPieces = 12;

	/** Keep the materials of the source mesh (a Fab building) instead of painting every section with Tint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice")
	bool bUseSourceMaterials = false;

	/** True: one convex hull per piece (right for boxes and cylinders). False: triangle collision (needed for buildings, static only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice")
	bool bConvexCollision = true;

	/** Debug only: after a cut the two halves are moved apart by this many cm each, so the cut can be seen. Leave at 0 with physics on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice")
	float DebugSeparation = 0.f;

	/**
	 * Physics after a cut. A half that still stands on the ground (its contact with the ground covers at least
	 * GroundContactFraction of the original footprint) stays where it is; every other half becomes a physics body
	 * with a convex hull as collision and falls, slides or tips by itself. Cutting a falling piece makes both halves fall.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Physics")
	bool bSimulatePieces = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Physics", meta=(ClampMin="0", ClampMax="1"))
	float GroundContactFraction = 0.15f;

	/** Start speed (cm/s) given to a freed piece along the attack direction (the blade's travel). A nudge, not an explosion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Physics")
	float PushSpeed = 0.f;

	/** Start speed (cm/s) away from the other half, perpendicular to the cut, so the cut opens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Physics")
	float SeparationSpeed = 0.f;

	/** Effective density in kg/m3 (hollow concrete buildings are far lighter than solid concrete). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Physics")
	float DensityKgPerM3 = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Physics")
	float LinearDamping = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Physics")
	float AngularDamping = 0.6f;

	/** A freed piece slower than this (cm/s and deg/s) for FreezeAfterSeconds is switched back to a static body: no endless micro movement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Physics")
	float FreezeSpeed = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Physics")
	float FreezeAfterSeconds = 1.f;

	/** True if a cut can be attempted at all; Reason says why not. */
	UFUNCTION(BlueprintCallable, Category="Slice")
	bool CanSlice(FString& Reason) const;

	/**
	 * Cuts the mesh with a plane given in world space. The original component keeps one half, the other half
	 * becomes a new component (see GetPieces). Invalid input is rejected and explained in GetLastReport().
	 * @return true if the mesh was cut
	 */
	UFUNCTION(BlueprintCallable, Category="Slice")
	bool SliceMeshWithPlane(FVector PlanePosition, FVector PlaneNormal);

	/**
	 * Replaces a plain static mesh actor by a sliceable structure with the same mesh, materials and transform (the original
	 * actor is destroyed). Only meshes with Allow CPU Access on are accepted.
	 * @return the new structure, or null with the reason logged
	 */
	static ASliceableStructure* ConvertStaticMeshActor(class AStaticMeshActor* Actor, float DebugSeparationCm);

	/** Same cut on one specific piece of this structure (the root mesh or a half made by an earlier cut). */
	bool SliceComponentWithPlane(UProceduralMeshComponent* Target, FVector PlanePosition, FVector PlaneNormal, FVector PushDirection = FVector::ZeroVector,
		const FSliceImpulse* Impulse = nullptr);

	/**
	 * Decides what a trace hit is for cutting. Shared by the knife and the gun so both treat targets the same way.
	 * WhyNot explains a None result.
	 */
	static ECutTarget ClassifyHit(const FHitResult& Hit, FString& WhyNot);

	/** True for the root mesh and for every half this structure created. */
	bool IsSliceable(const UPrimitiveComponent* Component) const;

	/** How many times any structure was asked to cut since the game started (a debug counter: a gun shot must not raise it). */
	static int32 GetTotalSliceRequests() { return TotalSliceRequests; }

	const FSliceReport& GetLastReport() const { return LastReport; }
	const TArray<TObjectPtr<UProceduralMeshComponent>>& GetPieces() const { return Pieces; }
	UProceduralMeshComponent* GetMesh() const { return Mesh; }

	/** Logs component class, section / vertex / triangle counts and bounds. */
	void LogMeshInfo(const TCHAR* Label) const;

	/** Vertex, triangle and section counts of a procedural mesh. */
	static void CountMesh(UProceduralMeshComponent* Component, int32& Verts, int32& Tris, int32& Sections);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void BuildMesh();
	bool CheckComponent(UProceduralMeshComponent* Target, FString& Reason) const;
	/**
	 * The mesh split itself: validates the plane, splits Target at it, closes both halves with a cap (Cap, or the red cut cap if null)
	 * and registers the new half as a piece. No physics, no collision: what to do with the halves is the caller's decision.
	 * Used by the knife's SliceComponentWithPlane and by the gun's FractureAt (which does not use the knife's function).
	 * @return true if Target was split; OutOther is the new half
	 */
	bool CutByPlane(UProceduralMeshComponent* Target, const FVector& PlanePosition, const FVector& PlaneNormal, UMaterialInterface* Cap,
		UProceduralMeshComponent*& OutOther, FSliceReport& Report, bool bQuiet);
	/** Pushes freed pieces: see FSliceImpulse. */
	void ApplyImpulse(const TArray<UProceduralMeshComponent*>& Targets, const FSliceImpulse& Impulse);
	/** Fracture helper: cuts Piece with the plane and sorts the halves into the side the normal points to and the other side. */
	void CutSorted(UProceduralMeshComponent* Piece, const FVector& Position, const FVector& Normal, TArray<UProceduralMeshComponent*>& Positive, TArray<UProceduralMeshComponent*>& Negative);

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> FractureCapMaterial;
	int32 FractureShots = 0;
	int32 FractureCutCount = 0;
	/** Area (cm2) of the convex outline of the vertices of a piece that lie at ground level: how much of it rests on the ground. */
	float GroundContactArea(UProceduralMeshComponent* Component) const;
	/** Enclosed volume (cm3, scaled) of a piece. Cut pieces are closed by the cap, so this is reliable for them. */
	static float MeshVolumeCm3(UProceduralMeshComponent* Component);
	/** Convex collision from a limited set of extreme points, then simulates physics. */
	void MakeDynamic(UProceduralMeshComponent* Piece, const FVector& StartVelocity, const FVector& StartAngularVelocityDeg, float HullInsetCm = 0.f, float MaxSpinDeg = 0.f);

	struct FFreedPiece
	{
		TWeakObjectPtr<UProceduralMeshComponent> Piece;
		float StillTime = 0.f;
	};
	TArray<FFreedPiece> Freed;

	/** Collision hulls are never thinner than this (cm) along any axis: slivers would crash the physics solver or tunnel through the ground. */
	static constexpr float MinHullThicknessCm = 6.f;

	float GroundZ = 0.f;
	float OriginalFootprintArea = 0.f;

	/** One convex hull around the vertices of a piece, used as its collision. */
	static void RebuildCollision(UProceduralMeshComponent* Component, bool bConvex, float InsetCm = 0.f);
	/** Thickness (cm) of a piece: the smallest width over 13 directions. A flat sliver gives a small value. */
	static float MinWidthCm(UProceduralMeshComponent* Component);
	static FVector WorldCentroid(UProceduralMeshComponent* Component);

	UPROPERTY(VisibleAnywhere, Category="Slice")
	TObjectPtr<UProceduralMeshComponent> Mesh;

	/** Halves created by cuts, other than the original component. */
	UPROPERTY()
	TArray<TObjectPtr<UProceduralMeshComponent>> Pieces;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> CapMaterial;

	FSliceReport LastReport;
	FFractureStats LastFracture;
	static inline int32 TotalSliceRequests = 0;
};
