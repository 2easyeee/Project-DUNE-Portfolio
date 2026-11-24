// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DuneSandTerrain.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

/**
 * Project DUNE (Day 18): the terrain of the sand test level: a flat sand field that rises into asymmetric dunes. The height is an analytic
 * function (HeightAt), so the sand patch (ADuneSandTrail) reads the same surface the collision uses and needs no landscape.
 *   - BaseHeight = HeightAt(x, y): never changes at runtime. The player's footprints are DynamicDeformation in the sand heightfield;
 *     the sand material adds it on top (FinalHeight = BaseHeight + DynamicDeformation).
 *   - Collision: one invisible mesh over the whole terrain (the player walks on it, the deformation never touches it).
 *   - Visual: the same surface in tiles; the tiles the sand patch covers are hidden (the patch draws them itself, finer, and moves its vertices) and shown again when the patch moves on.
 * Everything is built from code in OnConstruction / BeginPlay, so the level file only holds this actor and its settings.
 */
UCLASS()
class ADuneSandTerrain : public AActor
{
	GENERATED_BODY()

public:
	ADuneSandTerrain();

	/** Half size of the whole terrain (cm). Beyond it the player falls off the world: keep it larger than anything walkable. */
	UPROPERTY(EditAnywhere, Category="Terrain")
	float HalfSizeCm = 4800.f;

	/** Vertex distance of the collision and far mesh (cm). */
	UPROPERTY(EditAnywhere, Category="Terrain")
	float CellCm = 25.f;

	/** Where the sand patch starts (centre and full size, cm); it follows the player from there. */
	UPROPERTY(EditAnywhere, Category="Patch")
	FVector2D PatchCenter = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, Category="Patch")
	float PatchSizeCm = 1600.f;

	/** Size of a visual tile (cm): the terrain draws tiles, the sand patch takes some over. Must equal the sand surface TileSizeCm and divide 2 x HalfSizeCm. */
	UPROPERTY(EditAnywhere, Category="Patch")
	float TileCm = 400.f;

	/** The flat field is x < FlatEndX; the dunes rise between FlatEndX and FlatEndX + RiseCm. */
	UPROPERTY(EditAnywhere, Category="Dunes")
	float FlatEndX = -120.f;

	UPROPERTY(EditAnywhere, Category="Dunes")
	float RiseCm = 600.f;

	/** Small dunes (inside the patch): height and wavelength along the wind (cm). */
	UPROPERTY(EditAnywhere, Category="Dunes")
	float SmallHeightCm = 110.f;

	UPROPERTY(EditAnywhere, Category="Dunes")
	float SmallWavelengthCm = 1100.f;

	/** Big dunes in the background. */
	UPROPERTY(EditAnywhere, Category="Dunes")
	float BigHeightCm = 380.f;

	UPROPERTY(EditAnywhere, Category="Dunes")
	float BigWavelengthCm = 3200.f;

	/** The wind blows along +X rotated by this angle (degrees). The crests run across it; the windward side is long and gentle, the lee side short and steep. */
	UPROPERTY(EditAnywhere, Category="Dunes")
	float WindYawDeg = 18.f;

	/** The terrain rises towards its edge so the horizon is dunes, not the end of the world (cm at the very edge). */
	UPROPERTY(EditAnywhere, Category="Dunes")
	float EdgeRiseCm = 700.f;

	/** How strongly the lee side is steepened (0 = symmetric sine waves). */
	UPROPERTY(EditAnywhere, Category="Dunes")
	float Asymmetry = 0.55f;

	/** Terrain height (cm, world Z) at a world position. Pure function of x, y and the settings above. */
	float HeightAt(float X, float Y) const;

	/** Unit normal of the surface at a world position (finite differences). */
	FVector NormalAt(float X, float Y) const;

	int32 GetVertexCount() const { return VertexCount; }

	/** Bound to the sand surface: the patch built NewTile (stop drawing it here) in place of OldTile (draw it here again). */
	UFUNCTION()
	void OnSandTileBuilt(FIntPoint OldTile, FIntPoint NewTile);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	/** In PIE the actor is copied from the editor world without its transient meshes and OnConstruction does not run again: build them here. */
	virtual void BeginPlay() override;

private:
	void Build();

	// transient: the meshes are rebuilt from the settings, they are not saved with the level
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Collision;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Visual;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> SandMaterial;

	int32 VertexCount = 0;
};

