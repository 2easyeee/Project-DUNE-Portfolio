// Project DUNE (full game): a slow glowing orb the boss core fires. Hits the player for damage; on the ground it leaves a dent in the sand.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DuneBossProjectile.generated.h"

class UPointLightComponent;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;

UCLASS()
class UE_HOLDITTOGETHER_API ADuneBossProjectile : public AActor
{
	GENERATED_BODY()

public:
	ADuneBossProjectile();

	UPROPERTY(EditAnywhere, Category="Projectile") float Damage = 10.f;
	UPROPERTY(EditAnywhere, Category="Projectile") float Speed = 1100.f;

	/** Fires along Dir from the current location; the owner (the boss) is not hit. */
	void Launch(const FVector& Dir, AActor* Boss, const FLinearColor& Color);

private:
	UFUNCTION()
	void OnHit(UPrimitiveComponent* HitComp, AActor* Other, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Sphere;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProjectileMovementComponent> Movement;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Light;
};
