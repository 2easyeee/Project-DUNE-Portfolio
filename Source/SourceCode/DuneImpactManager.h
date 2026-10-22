// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DuneImpactManager.generated.h"

class UDecalComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Project DUNE: where bullet impacts live. One manager actor per level owns every bullet hole (a decal projected onto the
 * surface that was hit) and every small fragment (a tiny physics cube), so a shot creates components on this one actor, never
 * new actors, and the number of both is capped: the oldest goes first. Nothing here touches the structure that was hit: its
 * mesh, collision and physics stay as they were; a bullet hole is a mark, not a hole in the mesh.
 */
UCLASS()
class ADuneImpactManager : public AActor
{
	GENERATED_BODY()

public:
	ADuneImpactManager();

	/** The manager of this world; created on first use. */
	static ADuneImpactManager* Get(UWorld* World);

	/**
	 * Projects a bullet hole onto Surface at Location. Normal is the surface normal there (the decal faces along it).
	 * @return the decal, or null if the hole material is missing
	 */
	UDecalComponent* AddBulletHole(UPrimitiveComponent* Surface, const FVector& Location, const FVector& Normal);

	/**
	 * A few chips fly off the hit point: mostly along the surface normal, sideways along the bullet's direction over the surface
	 * and fanned out, each a little faster than the one before. Small physics cubes on this actor; they ignore the player.
	 */
	void SpawnFragments(const FVector& Location, const FVector& Normal, const FVector& BulletDirection, const FLinearColor& Color);

	int32 GetBulletHoleCount() const { return Holes.Num(); }
	int32 GetFragmentCount() const { return Fragments.Num(); }
	int32 GetTotalBulletHoles() const { return TotalHoles; }
	int32 GetTotalFragments() const { return TotalFragments; }

	/** Decal size (cm, half extents: depth, width, height). The hole is 16 cm wide. */
	UPROPERTY(EditAnywhere, Category="Impact")
	FVector HoleHalfSize = FVector(6.f, 8.f, 8.f);

	UPROPERTY(EditAnywhere, Category="Impact")
	int32 MaxBulletHoles = 40;

	UPROPERTY(EditAnywhere, Category="Impact")
	int32 FragmentsPerHit = 4;

	UPROPERTY(EditAnywhere, Category="Impact")
	int32 MaxFragments = 48;

	/** Seconds until a fragment disappears. */
	UPROPERTY(EditAnywhere, Category="Impact")
	float FragmentLifetime = 8.f;

protected:
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FFragment
	{
		TWeakObjectPtr<UStaticMeshComponent> Component;
		float Born = 0.f;
	};

	UPROPERTY()
	TObjectPtr<UMaterialInterface> HoleMaterial;

	UPROPERTY()
	TObjectPtr<UStaticMesh> FragmentMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> FragmentBase;

	TArray<TWeakObjectPtr<UDecalComponent>> Holes;
	TArray<FFragment> Fragments;
	int32 TotalHoles = 0;
	int32 TotalFragments = 0;
};
