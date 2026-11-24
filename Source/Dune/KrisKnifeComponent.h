// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "KrisKnifeComponent.generated.h"

class APlayerController;
class ASliceableStructure;
class UInputAction;
class UInputMappingContext;
class USceneComponent;

/**
 * Project DUNE: the player's kris knife. Added to the existing HoldItCharacter at runtime (see ADuneGameMode), so the
 * character class is untouched.
 *
 * Attack (left mouse button) starts a swing. The swing is only animation; the hit test is separate and runs inside a short
 * window of the swing, once per swing:
 *   - aim: a trace from the camera along the view direction picks the target, which has to be an ASliceableStructure
 *     within ReachCm of the player;
 *   - slash direction: the mouse movement of the last moments (turning the camera right / up / diagonal), horizontal if
 *     the mouse did not move;
 *   - cut plane: passes through the hit point (pushed slightly into the mesh) and contains both the slash direction and
 *     the view direction, so its normal is slash x view. The blade direction itself is not the normal.
 */
UCLASS()
class UKrisKnifeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UKrisKnifeComponent();

	/** Tells the knife which player controller supplies the view (the input itself is handled by UDuneWeaponComponent). */
	void SetupInput(APlayerController* PC);

	/** Shows or hides the knife model. */
	void SetKnifeVisible(bool bVisible);
	bool IsKnifeVisible() const { return KnifeParts.Num() > 0 && KnifeParts[0]->IsVisible(); }

	/** Stops a running swing before it can hit (used when the weapon is switched). */
	void CancelSwing();

	/** The attack entry point, called by the weapon component. Ignored while a swing is running. */
	void Attack();

	/** Max distance (cm) between the player and the hit point. */
	UPROPERTY(EditAnywhere, Category="Knife")
	float ReachCm = 300.f;

	UPROPERTY(EditAnywhere, Category="Knife")
	float SwingDuration = 0.4f;

	/** Hit test runs between these two times (s) of the swing. */
	UPROPERTY(EditAnywhere, Category="Knife")
	float HitWindowStart = 0.08f;

	UPROPERTY(EditAnywhere, Category="Knife")
	float HitWindowEnd = 0.22f;

	/** The cut plane passes this many cm behind the hit surface, so a hit on a face really goes through the mesh. */
	UPROPERTY(EditAnywhere, Category="Knife")
	float Penetration = 8.f;

	/** Seconds the mouse movement is remembered for the slash direction. */
	UPROPERTY(EditAnywhere, Category="Knife")
	float SlashMemory = 0.25f;

	/** Camera movement below this (degrees) counts as no movement: horizontal slash. */
	UPROPERTY(EditAnywhere, Category="Knife")
	float MinSlashDegrees = 1.5f;

	UPROPERTY(EditAnywhere, Category="Knife")
	FName HandBone = TEXT("hand_r");

	/** Debug separation (cm) given to a static mesh actor that is turned into a sliceable structure by its first cut. */
	UPROPERTY(EditAnywhere, Category="Knife")
	float ConvertedSeparation = 0.f;

	bool IsSwinging() const { return bSwinging; }
	int32 GetSlices() const { return Slices; }
	int32 GetMisses() const { return Misses; }
	int32 GetIgnoredInputs() const { return IgnoredInputs; }
	const FVector& GetLastPlaneNormal() const { return LastNormal; }
	int32 GetPartCount() const { return PartCount; }
	bool HasHandBone() const { return bHandBone; }

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void BuildKnife();
	void UpdateSlash(float DeltaTime);
	void UpdateKnifePose();
	/** One hit test with the current aim. @return true once the swing has been resolved (cut done, or a final refusal) */
	bool TryHit();

	UPROPERTY()
	TObjectPtr<USceneComponent> KnifeRoot;

	UPROPERTY()
	TArray<TObjectPtr<UPrimitiveComponent>> KnifeParts;

	TWeakObjectPtr<APlayerController> Controller;

	bool bLoggedNonTarget = false;
	bool bSwinging = false;
	bool bResolved = false;
	bool bWindowOpen = false;
	float SwingTime = 0.f;
	FVector SwingForward = FVector::ForwardVector;
	FVector SwingSlash = FVector::RightVector;

	// mouse movement of the recent past, in degrees, decaying
	bool bHavePrevRotation = false;
	FRotator PrevRotation = FRotator::ZeroRotator;
	float RecentYaw = 0.f;
	float RecentPitch = 0.f;

	int32 Slices = 0;
	int32 Misses = 0;
	int32 IgnoredInputs = 0;
	int32 PartCount = 0;
	bool bHandBone = false;
	FVector LastNormal = FVector::ZeroVector;
};
