// Project DUNE (full game): the first-person player. WASD, mouse look, Space jump, Shift sprint, left mouse fire, R reload.
// Health lives in the run state (UDuneRunSubsystem) so it carries over to the next level. Input actions are created here (no input assets).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "DunePlayerCharacter.generated.h"

class UCameraComponent;
class UDuneHealthComponent;
class UDuneDebrisLauncher;
class UDuneFirstPersonArms;
class UDuneGunFeel;
class UInputAction;
class UInputMappingContext;
class UStaticMeshComponent;

UCLASS()
class UE_HOLDITTOGETHER_API ADunePlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ADunePlayerCharacter();

	UPROPERTY(EditAnywhere, Category="Dune|Move") float WalkSpeed = 620.f;
	UPROPERTY(EditAnywhere, Category="Dune|Move") float SprintSpeed = 920.f;

	// the pistol: the trigger of the game. A shot damages what it hits and cracks a pillar into pieces that can then be thrown (right mouse)
	UPROPERTY(EditAnywhere, Category="Dune|Gun") float Damage = 30.f;
	UPROPERTY(EditAnywhere, Category="Dune|Gun") float FireCooldown = 0.28f;
	UPROPERTY(EditAnywhere, Category="Dune|Gun") float RangeCm = 30000.f;
	UPROPERTY(EditAnywhere, Category="Dune|Gun") int32 MagazineSize = 12;
	UPROPERTY(EditAnywhere, Category="Dune|Gun") float ReloadSeconds = 1.2f;

	// where the pistol model sits in the view (relative to the camera) and how it is turned: the model's barrel runs along its Y axis
	UPROPERTY(EditAnywhere, Category="Dune|Gun") FVector PistolOffset = FVector(32.f, 14.f, -16.f);
	UPROPERTY(EditAnywhere, Category="Dune|Gun") FRotator PistolRotation = FRotator(0.f, -90.f, 0.f);
	UPROPERTY(EditAnywhere, Category="Dune|Gun") float PistolScale = 1.f;
	/** The muzzle in camera space (the pistol model at rest): the muzzle flash sits here. Equals PistolMuzzleLocal carried through PistolOffset/Rotation. */
	UPROPERTY(EditAnywhere, Category="Dune|Gun") FVector MuzzleOffset = FVector(47.3f, 14.f, -4.5f);
	/** The tip of the barrel in the pistol mesh's own space (mesh bounds: barrel along +Y up to 15.3, slide top at Z 14). The tracer starts at this point, live, so it follows the kick. */
	UPROPERTY(EditAnywhere, Category="Dune|Gun") FVector PistolMuzzleLocal = FVector(0.f, 15.3f, 11.5f);
	/** A shot that misses by less than this (cm) still hits a living target (a thin sphere sweep after the line trace). 0 = off. */
	UPROPERTY(EditAnywhere, Category="Dune|Gun") float AimAssistRadiusCm = 10.f;

	UFUNCTION(BlueprintPure, Category="Dune") int32 GetAmmo() const { return Ammo; }
	UFUNCTION(BlueprintPure, Category="Dune") bool IsReloading() const { return bReloading; }
	/** 0..1 while reloading (for the HUD gauge), 0 otherwise. */
	float GetReloadFraction() const
	{
		const FTimerManager& TM = GetWorldTimerManager();
		const float Rate = bReloading ? TM.GetTimerRate(ReloadTimer) : 0.f;
		return Rate > 0.f ? FMath::Clamp(TM.GetTimerElapsed(ReloadTimer) / Rate, 0.f, 1.f) : 0.f;
	}
	UFUNCTION(BlueprintPure, Category="Dune") UDuneHealthComponent* GetHealth() const { return Health; }
	UFUNCTION(BlueprintPure, Category="Dune") bool IsDead() const;
	UFUNCTION(BlueprintPure, Category="Dune") UDuneDebrisLauncher* GetDebris() const { return Debris; }

	/** While locked the player cannot walk or jump (looking and shooting still work): the reward choice. */
	void SetMovementLocked(bool bLocked);
	bool IsMovementLocked() const { return bMoveLocked; }
	virtual void Jump() override { if (!bMoveLocked) Super::Jump(); }

	/** The eye position and the direction the player looks (control rotation: also valid without a viewport). */
	bool GetAimView(FVector& OutLoc, FVector& OutDir) const;

	UFUNCTION(BlueprintPure, Category="Dune") UDuneGunFeel* GetGunFeel() const { return GunFeel; }

	/** What a shot along the view direction would hit (the line trace with the aim assist). Also gives the eye and the direction. */
	bool TraceShot(FHitResult& OutHit, FVector& OutLoc, FVector& OutDir) const;

	/** One shot along the view direction. @return true if a shot was fired (not on cooldown, not reloading, ammo left). */
	bool Fire();
	void StartReload();

protected:
	virtual void BeginPlay() override;
	virtual void PawnClientRestart() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	bool bMoveLocked = false;
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void FireHeld() { Fire(); }
	void DebrisPress();
	void DebrisRelease();
	void SprintOn();
	void SprintOff();
	void HandleHealthChanged(float NewHealth, float NewMax, float Delta);
	UFUNCTION() void OnHealthChanged(float NewHealth, float NewMax, float Delta) { HandleHealthChanged(NewHealth, NewMax, Delta); }
	UFUNCTION() void OnDied();
	void FinishReload();
	void BuildViewModel();
	/** Crater and thrown-up sand on the floor below a structure the gun just broke (nothing where there is no sand patch). */
	void SandBurst(AActor* Structure, const FVector& Point);
	/** Red tint + vignette flash when hurt, and a slow red pulse while health is low. Uses the camera's own post process settings. */
	void UpdateScreenEffect(float DeltaSeconds);

	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UDuneHealthComponent> Health;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UDuneDebrisLauncher> Debris;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UDuneGunFeel> GunFeel;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> ViewModel;
	UPROPERTY() TObjectPtr<UDuneFirstPersonArms> Arms; // the mannequin arms that hold the pistol (null: the pistol hangs on the camera)
	UPROPERTY() TObjectPtr<UMaterialInterface> TracerGlow;
	UPROPERTY() TObjectPtr<UInputAction> DebrisAction;

	UPROPERTY() TObjectPtr<UInputAction> MoveAction;
	UPROPERTY() TObjectPtr<UInputAction> LookAction;
	UPROPERTY() TObjectPtr<UInputAction> JumpAction;
	UPROPERTY() TObjectPtr<UInputAction> FireAction;
	UPROPERTY() TObjectPtr<UInputAction> ReloadAction;
	UPROPERTY() TObjectPtr<UInputAction> SprintAction;
	UPROPERTY() TObjectPtr<UInputMappingContext> Context;

	int32 Ammo = 12;
	bool bReloading = false;
	float LastShotTime = -100.f;
	float DamageFlash = 0.f;
	float PulseTime = 0.f;
	FTimerHandle ReloadTimer;
};
