// Project DUNE (full game): health of the player and of enemies. Listens to the owner's normal damage events (ApplyDamage / ApplyPointDamage).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DuneHealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDuneHealthChanged, float, Health, float, MaxHealth, float, Delta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDuneDied);

UCLASS(ClassGroup=(Dune), meta=(BlueprintSpawnableComponent))
class UE_HOLDITTOGETHER_API UDuneHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDuneHealthComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Health", meta=(ClampMin="1"))
	float MaxHealth = 100.f;

	UPROPERTY(BlueprintReadOnly, Category="Health")
	float Health = 100.f;

	/** Debug: while set, damage does nothing (the HUD's I key toggles it on the player). */
	UPROPERTY(BlueprintReadWrite, Category="Health")
	bool bInvulnerable = false;

	/** Where the last damage came from (its causer's position) and when (world seconds); the HUD's hit direction mark reads it. */
	FVector LastDamageFrom = FVector::ZeroVector;
	float LastDamageTime = -1000.f;
	bool bHasDamageFrom = false;

	UFUNCTION(BlueprintPure, Category="Health")
	bool IsDead() const { return Health <= 0.f; }

	/** Sets both values (e.g. from the run state when a level starts). Does not fire Died. */
	void Init(float InHealth, float InMax);

	UFUNCTION(BlueprintCallable, Category="Health")
	void Heal(float Amount);

	UPROPERTY(BlueprintAssignable, Category="Health")
	FDuneHealthChanged OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category="Health")
	FDuneDied OnDied;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void HandleAnyDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser);
};
