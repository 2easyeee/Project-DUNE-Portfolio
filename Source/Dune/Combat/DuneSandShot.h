// Project DUNE (full game): the sand ball a thrower enemy lobs at the player. Flies on a gravity arc to where the player stood when it was thrown
// (no lead: moving sideways dodges it), hurts once when it passes close to the player, bursts on anything else.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DuneSandShot.generated.h"

class UStaticMeshComponent;

UCLASS()
class UE_HOLDITTOGETHER_API ADuneSandShot : public AActor
{
	GENERATED_BODY()

public:
	ADuneSandShot();

	/** Starts the throw from Origin to Target (flight time from Speed). */
	void Launch(const FVector& Origin, const FVector& Target, float Speed, float InDamage, AActor* InThrower);
	bool HasHitPlayer() const { return bHitPlayer; }

protected:
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Ball;
	FVector Velocity = FVector::ZeroVector;
	float Damage = 14.f;
	float Age = 0.f;
	bool bHitPlayer = false;
	TWeakObjectPtr<AActor> Thrower;
	static constexpr float GravityCm = 700.f;
};
