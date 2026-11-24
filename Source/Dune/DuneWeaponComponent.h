// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DuneWeaponComponent.generated.h"

class APlayerController;
class UDuneGunComponent;
class UInputAction;
class UInputMappingContext;
class UKrisKnifeComponent;

/** The weapon in the player's hand. */
UENUM(BlueprintType)
enum class EDuneWeapon : uint8
{
	Gun,
	Knife
};

/** True while the console variable dune.WeaponDebug is on (default): trace lines, hit points and cut planes are drawn. */
bool DuneWeaponDebug();

/**
 * Project DUNE: which weapon the player holds, and the one place where the weapon input arrives.
 * Tab switches between Gun and Knife; the left mouse button calls the attack of the selected weapon only, so one
 * click can never reach both. Added to the existing HoldItCharacter at runtime by ADuneGameMode; it creates the knife
 * and the gun components, which keep their own attack logic (and share the cutting code in ASliceableStructure).
 */
UCLASS()
class UDuneWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Creates the weapons and maps Tab / left mouse button for this player. */
	void SetupInput(APlayerController* PC);

	/** Tab. */
	void ToggleWeapon();
	void SetWeapon(EDuneWeapon NewWeapon);

	/** Left mouse button: attacks with the selected weapon. */
	void PrimaryAttack();

	EDuneWeapon GetWeapon() const { return Weapon; }
	UKrisKnifeComponent* GetKnife() const { return Knife; }
	UDuneGunComponent* GetGun() const { return Gun; }
	int32 GetSwitchCount() const { return Switches; }
	/** How many attack inputs went to each weapon (a click goes to exactly one of them). */
	int32 GetGunInputs() const { return GunInputs; }
	int32 GetKnifeInputs() const { return KnifeInputs; }

	/** The weapon in hand at the start of the level. */
	UPROPERTY(EditAnywhere, Category="Weapon")
	EDuneWeapon DefaultWeapon = EDuneWeapon::Gun;

private:
	void ApplyWeapon();

	UPROPERTY()
	TObjectPtr<UKrisKnifeComponent> Knife;

	UPROPERTY()
	TObjectPtr<UDuneGunComponent> Gun;

	UPROPERTY()
	TObjectPtr<UInputAction> AttackAction;

	UPROPERTY()
	TObjectPtr<UInputAction> SwitchAction;

	UPROPERTY()
	TObjectPtr<UInputMappingContext> Context;

	EDuneWeapon Weapon = EDuneWeapon::Gun;
	int32 Switches = 0;
	int32 GunInputs = 0;
	int32 KnifeInputs = 0;
	bool bInputBound = false;
};
