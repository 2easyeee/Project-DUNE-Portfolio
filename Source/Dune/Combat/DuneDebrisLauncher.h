// Project DUNE (full game): throws debris. Hold the right mouse button to charge (3 levels), release to throw the broken piece you look at
// along the view direction. A thrown piece hurts whatever with a health component it hits: damage = LevelDamage[charge level] x a mass factor,
// so a heavy piece is slow and strong, a small shard fast and weak. Speed and damage are capped, so a throw can surprise but never explode.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DuneDebrisLauncher.generated.h"

class UProceduralMeshComponent;

UCLASS(ClassGroup=(Dune), meta=(BlueprintSpawnableComponent))
class UE_HOLDITTOGETHER_API UDuneDebrisLauncher : public UActorComponent
{
	GENERATED_BODY()

public:
	UDuneDebrisLauncher();

	/** Seconds from nothing to full charge (level 3). */
	UPROPERTY(EditAnywhere, Category="Dune|Debris") float ChargeSeconds = 1.5f;
	/** How far away a piece can be picked by looking at it. */
	UPROPERTY(EditAnywhere, Category="Dune|Debris") float AimRangeCm = 3500.f;
	/** Speed (cm/s) of a piece of RefMassKg at charge level 1, 2 and 3. Heavier pieces are slower, lighter faster (within the limits below). */
	UPROPERTY(EditAnywhere, Category="Dune|Debris") TArray<float> LevelSpeedCm = { 900.f, 1700.f, 2600.f };
	UPROPERTY(EditAnywhere, Category="Dune|Debris") float RefMassKg = 300.f; // the pieces of a pillar weigh 20 .. 2600 kg, most 100 .. 600
	UPROPERTY(EditAnywhere, Category="Dune|Debris") float MinSpeedCm = 250.f;
	UPROPERTY(EditAnywhere, Category="Dune|Debris") float MaxSpeedCm = 3000.f;
	/**
	 * The piece is pulled in front of the player when it is thrown: it starts this far (cm, plus its own radius) ahead of the eye, along the view
	 * direction, and flies from there. So any piece can be thrown at what the crosshair is on, also one buried in a heap of rubble.
	 */
	UPROPERTY(EditAnywhere, Category="Dune|Debris") float HoldDistanceCm = 220.f;
	/** A thrown piece slower than this (cm/s) does no damage. */
	UPROPERTY(EditAnywhere, Category="Dune|Debris") float MinDamageSpeedCm = 400.f;
	/**
	 * Damage of a piece of RefMassKg that hits at full launch speed, per charge level (1, 2, 3). Level 1 is about a pistol shot (30), level 2 two,
	 * level 3 kills a 60 HP chaser outright. Heavier pieces hit harder (square root of the mass ratio, see DamageFor), a piece slowed since its launch less.
	 */
	UPROPERTY(EditAnywhere, Category="Dune|Debris") TArray<float> LevelDamage = { 25.f, 55.f, 100.f };
	/** Run upgrades: damage multiplier (Sharp Shards) and extra speed of pieces of HeavyMassKg and more (Giant's Grip). */
	UPROPERTY(EditAnywhere, Category="Dune|Debris") float DamageMultiplier = 1.f;
	UPROPERTY(EditAnywhere, Category="Dune|Debris") float HeavySpeedBonus = 0.f;
	UPROPERTY(EditAnywhere, Category="Dune|Debris") float HeavyMassKg = 300.f;
	UPROPERTY(EditAnywhere, Category="Dune|Debris") float MinMassFactor = 0.5f;
	UPROPERTY(EditAnywhere, Category="Dune|Debris") float MaxMassFactor = 1.5f;
	UPROPERTY(EditAnywhere, Category="Dune|Debris") float MinDamage = 5.f;
	UPROPERTY(EditAnywhere, Category="Dune|Debris") float MaxDamage = 150.f;
	/** A thrown piece stops being a weapon after this many seconds. */
	UPROPERTY(EditAnywhere, Category="Dune|Debris") float FlightSeconds = 6.f;

	void BeginCharge();
	/** Throws the piece in the crosshair at the charged level. @return the piece, or null if nothing throwable was in sight. */
	UProceduralMeshComponent* ReleaseCharge();
	/**
	 * Throws this piece along Dir at level 1..3, from in front of the eye (Eye + Dir * (HoldDistanceCm + the piece's radius)).
	 * @return the launch speed (cm/s), 0 if it is not a throwable piece.
	 */
	float LaunchPiece(UProceduralMeshComponent* Piece, const FVector& Eye, FVector Dir, int32 Level);

	/** The damage a piece of MassKg thrown at Level does when it hits at HitSpeed (cm/s) after being launched at LaunchSpeed. */
	float DamageFor(int32 Level, float MassKg, float HitSpeed, float LaunchSpeed) const;

	/** The broken piece the crosshair is on (the one a release would throw), or null. */
	UProceduralMeshComponent* FindAimedPiece() const;

	/** The piece held in front of the player while charging (null when none), so the HUD / view model can react. */
	UProceduralMeshComponent* GetHeld() const { return Held.Get(); }

	bool IsCharging() const { return bCharging; }
	float GetCharge() const { return Charge; }
	/** 0 when not charging, otherwise 1..3. */
	int32 GetLevel() const;
	int32 GetLaunches() const { return Launches; }
	int32 GetHits() const { return Hits; }
	/** Where the last thrown piece first touched the ground (false until one has). */
	bool GetLastLanding(FVector& Out) const { Out = LastLanding; return bHasLanding; }

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FFlying
	{
		TWeakObjectPtr<UProceduralMeshComponent> Piece;
		float Age = 0.f;
		float PrevSpeed = 0.f;
		float LaunchSpeed = 1.f;
		int32 Level = 1;
		TSet<TWeakObjectPtr<AActor>> Hit;
		bool bOnGround = false;           // touched the ground on the last tick (a landing is the change from false to true)
		FVector PrevGround = FVector::ZeroVector;
	};
	/** The piece digs a crater where it lands and ploughs a furrow while it slides over the ground (nothing where there is no sand patch). */
	void UpdateSand(FFlying& F, UProceduralMeshComponent* Piece, float Speed);
	FVector LastLanding = FVector::ZeroVector;
	bool bHasLanding = false;
	void UpdateFlying(float DeltaTime);
	void UpdateHeld();

	TWeakObjectPtr<UProceduralMeshComponent> Held;

	TArray<FFlying> Flying;
	bool bCharging = false;
	float Charge = 0.f;
	int32 Launches = 0;
	int32 Hits = 0;
};
