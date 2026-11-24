// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dune/DuneWeaponComponent.h"
#include "Dune/DuneGunComponent.h"
#include "Dune/KrisKnifeComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "UE_HOLDITTOGETHER.h"

static TAutoConsoleVariable<int32> CVarWeaponDebug(TEXT("dune.WeaponDebug"), 1,
	TEXT("1 = draw the gun trace, hit point and cut plane (and the knife's), 0 = off. Display only."));

bool DuneWeaponDebug()
{
	return CVarWeaponDebug.GetValueOnGameThread() != 0;
}

void UDuneWeaponComponent::SetupInput(APlayerController* PC)
{
	AActor* Owner = GetOwner();
	if (!Knife)
	{
		Knife = NewObject<UKrisKnifeComponent>(Owner, TEXT("KrisKnife"));
		Knife->RegisterComponent();
	}
	if (!Gun)
	{
		Gun = NewObject<UDuneGunComponent>(Owner, TEXT("DuneGun"));
		Gun->RegisterComponent();
	}
	Knife->SetupInput(PC);
	Gun->SetController(PC);
	Weapon = DefaultWeapon;
	ApplyWeapon();
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Weapon: default weapon %s"), Weapon == EDuneWeapon::Gun ? TEXT("Gun") : TEXT("Knife"));

	if (bInputBound || !PC || !PC->GetLocalPlayer())
	{
		return; // bound once: setting up again must not stack a second binding
	}
	// no input asset exists for these, so the actions and their keys are created here (same way as the carry key)
	AttackAction = NewObject<UInputAction>(this, TEXT("IA_PrimaryAttack"));
	AttackAction->ValueType = EInputActionValueType::Boolean;
	SwitchAction = NewObject<UInputAction>(this, TEXT("IA_SwitchWeapon"));
	SwitchAction->ValueType = EInputActionValueType::Boolean;
	Context = NewObject<UInputMappingContext>(this, TEXT("IMC_Weapon"));
	Context->MapKey(AttackAction, EKeys::LeftMouseButton);
	Context->MapKey(SwitchAction, EKeys::Tab);

	if (UEnhancedInputLocalPlayerSubsystem* Input = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
	{
		Input->AddMappingContext(Context, 2);
	}
	if (UEnhancedInputComponent* Enhanced = Cast<UEnhancedInputComponent>(PC->InputComponent))
	{
		Enhanced->BindAction(AttackAction, ETriggerEvent::Started, this, &UDuneWeaponComponent::PrimaryAttack);
		Enhanced->BindAction(SwitchAction, ETriggerEvent::Started, this, &UDuneWeaponComponent::ToggleWeapon);
		bInputBound = true;
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Weapon: left mouse button = attack, Tab = switch weapon"));
	}
	else
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Weapon: player controller has no Enhanced Input component, weapon input not bound"));
	}
}

void UDuneWeaponComponent::ToggleWeapon()
{
	SetWeapon(Weapon == EDuneWeapon::Gun ? EDuneWeapon::Knife : EDuneWeapon::Gun);
}

void UDuneWeaponComponent::SetWeapon(EDuneWeapon NewWeapon)
{
	if (NewWeapon == Weapon)
	{
		return;
	}
	if (Weapon == EDuneWeapon::Knife && Knife)
	{
		Knife->CancelSwing(); // the knife swing in progress must not hit after the knife is put away
	}
	Weapon = NewWeapon;
	++Switches;
	ApplyWeapon();
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Weapon: switched to %s (switch %d)"), Weapon == EDuneWeapon::Gun ? TEXT("Gun") : TEXT("Knife"), Switches);
}

void UDuneWeaponComponent::ApplyWeapon()
{
	// exactly one model is shown, the one that matches the state
	if (Gun)
	{
		Gun->SetGunVisible(Weapon == EDuneWeapon::Gun);
	}
	if (Knife)
	{
		Knife->SetKnifeVisible(Weapon == EDuneWeapon::Knife);
	}
	// with the gun the camera sits over the right shoulder: with the camera dead behind the character, the body would
	// cover the crosshair. The knife keeps the original centred camera.
	if (USpringArmComponent* Boom = GetOwner() ? GetOwner()->FindComponentByClass<USpringArmComponent>() : nullptr)
	{
		Boom->SocketOffset = Weapon == EDuneWeapon::Gun ? FVector(0.f, 70.f, 30.f) : FVector::ZeroVector;
	}
}

void UDuneWeaponComponent::PrimaryAttack()
{
	// one entry point for the click: only the selected weapon's attack runs
	if (Weapon == EDuneWeapon::Gun)
	{
		if (Gun)
		{
			++GunInputs;
			Gun->Fire();
		}
	}
	else if (Knife)
	{
		++KnifeInputs;
		Knife->Attack();
	}
}
