// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DuneGunComponent.generated.h"

class APlayerController;
class USceneComponent;

/**
 * Project DUNE: the player's gun, a hitscan weapon (no bullet actors). Added to the character by UDuneWeaponComponent.
 * The gun does NOT cut. Cutting is the knife's attack (UKrisKnifeComponent -> ASliceableStructure::SliceComponentWithPlane);
 * a bullet only marks the structure it hits.
 *
 * Fire():
 *   1. aim: a trace from the camera along the view direction through the screen centre finds the point being aimed at;
 *   2. bullet: a second trace from the muzzle to that point (the muzzle is beside the camera, so the bullet direction
 *      differs a little from the view direction; if something stands between muzzle and aim point, that is what is hit);
 *   3. the hit is classified by ASliceableStructure::ClassifyHit, the same rule the knife uses; only structures get an effect;
 *   4. ApplyBulletImpact: a bullet hole (decal) at the hit point facing along the surface normal, and a few chips that fly
 *      off it. Both live in ADuneImpactManager. The structure itself (mesh, collision, physics) is not changed.
 */
UCLASS()
class UDuneGunComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDuneGunComponent();

	void SetController(APlayerController* PC) { Controller = PC; }

	/** One shot. @return true if the bullet hit a structure and left an impact. */
	bool Fire();

	void SetGunVisible(bool bVisible);
	bool IsGunVisible() const { return GunParts.Num() > 0 && GunParts[0]->IsVisible(); }

	/** Max distance of the aim trace (cm). */
	UPROPERTY(EditAnywhere, Category="Gun")
	float RangeCm = 20000.f;

	/** Seconds between shots; an input inside this time is ignored. */
	UPROPERTY(EditAnywhere, Category="Gun")
	float FireCooldown = 0.12f;

	UPROPERTY(EditAnywhere, Category="Gun")
	FName HandBone = TEXT("hand_r");

	/** Bullet hole decals (Day 13). Off: the visible result of a shot is a broken structure, not a mark. */
	UPROPERTY(EditAnywhere, Category="Gun")
	bool bLeaveBulletHoles = false;

	int32 GetShots() const { return Shots; }
	/** Shots that left a bullet hole and chips on a structure. */
	int32 GetImpacts() const { return Impacted; }
	/** Shots that broke a structure the level allows to be broken. */
	int32 GetFractures() const { return Fractures; }
	int32 GetNonTargetHits() const { return NonTargetHits; }
	int32 GetMisses() const { return Misses; }
	int32 GetIgnoredInputs() const { return IgnoredInputs; }
	int32 GetPartCount() const { return PartCount; }
	const FVector& GetLastBulletDirection() const { return LastBullet; }

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void BuildGun();
	FVector MuzzleLocation() const;
	/** The gun's only effect on a structure: a bullet hole and chips at the hit point. No slicing. */
	bool ApplyBulletImpact(const FHitResult& Hit, const FVector& Bullet);

	UPROPERTY()
	TObjectPtr<USceneComponent> GunRoot;

	UPROPERTY()
	TArray<TObjectPtr<UPrimitiveComponent>> GunParts;

	TWeakObjectPtr<APlayerController> Controller;

	float LastShotTime = -100.f;
	int32 Shots = 0;
	int32 Impacted = 0;
	int32 Fractures = 0;
	int32 NonTargetHits = 0;
	int32 Misses = 0;
	int32 IgnoredInputs = 0;
	int32 PartCount = 0;
	FVector LastBullet = FVector::ZeroVector;
};
