// Project DUNE (full game): the Omega-0 automaton chaser. Walks to the player over the nav mesh (straight at the player when there is
// no path) and hits when close. Dies to damage; the body falls as a ragdoll and is removed after a few seconds.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "DuneChaserEnemy.generated.h"

class UDuneHealthComponent;
class UAnimSequence;
class ADunePlayerCharacter;

UCLASS()
class UE_HOLDITTOGETHER_API ADuneChaserEnemy : public ACharacter
{
	GENERATED_BODY()

public:
	ADuneChaserEnemy();

	UPROPERTY(EditAnywhere, Category="Dune|Enemy") float MaxHealth = 60.f;
	UPROPERTY(EditAnywhere, Category="Dune|Enemy") float MoveSpeed = 540.f;
	UPROPERTY(EditAnywhere, Category="Dune|Enemy") float AttackRangeCm = 170.f;
	UPROPERTY(EditAnywhere, Category="Dune|Enemy") float AttackDamage = 12.f;
	UPROPERTY(EditAnywhere, Category="Dune|Enemy") float AttackInterval = 1.0f;

	/**
	 * Pack behaviour: a dormant enemy stands idle until the player comes within AggroRadiusCm or any enemy of its pack is hurt; then the whole pack chases.
	 * (An enemy that is not made dormant chases at once, as the boss summons do.)
	 */
	void SetDormant(int32 InPackId, float InAggroRadiusCm);

	/** Normal = walks up and hits. Charger = stops, hunkers down (telegraph), dashes in a straight line, then is open for a moment. Thrower = keeps its distance and lobs sand balls. */
	enum class EKind : uint8 { Normal, Charger, Thrower };
	void SetKind(EKind InKind);
	EKind GetKind() const { return Kind; }
	int32 GetDashCount() const { return DashCount; }
	int32 GetThrowCount() const { return ThrowCount; }
	UPROPERTY(EditAnywhere, Category="Dune|Enemy|Charger") float DashSpeed = 1900.f;
	UPROPERTY(EditAnywhere, Category="Dune|Enemy|Charger") float DashWindupSeconds = 0.9f;
	UPROPERTY(EditAnywhere, Category="Dune|Enemy|Charger") float DashMaxSeconds = 0.75f;
	UPROPERTY(EditAnywhere, Category="Dune|Enemy|Charger") float DashRecoverSeconds = 1.2f;
	UPROPERTY(EditAnywhere, Category="Dune|Enemy|Charger") float DashCooldownSeconds = 3.5f;
	UPROPERTY(EditAnywhere, Category="Dune|Enemy|Charger") float DashDamage = 22.f;
	UPROPERTY(EditAnywhere, Category="Dune|Enemy|Thrower") float ThrowWindupSeconds = 0.8f;
	UPROPERTY(EditAnywhere, Category="Dune|Enemy|Thrower") float ThrowCooldownSeconds = 3.2f;
	UPROPERTY(EditAnywhere, Category="Dune|Enemy|Thrower") float ThrowDamage = 14.f;
	UPROPERTY(EditAnywhere, Category="Dune|Enemy|Thrower") float PreferredDistanceCm = 1100.f;
	bool IsAggro() const { return bAggro; }
	int32 GetPackId() const { return PackId; }

	UFUNCTION(BlueprintPure, Category="Dune") bool IsDead() const;
	UFUNCTION(BlueprintPure, Category="Dune") UDuneHealthComponent* GetHealth() const { return Health; }
	/** How many times it flinched from a hit (a hit staggers it, throws it back a little and interrupts a swing when the hit is heavy). */
	int32 GetFlinchCount() const { return FlinchCount; }
	/** Hits of at least this much damage also interrupt an attack and stagger for longer. */
	UPROPERTY(EditAnywhere, Category="Dune|Enemy") float HeavyHitDamage = 40.f;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UFUNCTION() void OnDied();
	UFUNCTION() void OnHurt(float NewHealth, float NewMax, float Delta);
	/** Plays an animation of the bull on the mesh (single node mode: no anim blueprint). Does nothing when it is already the current one and looping. */
	void PlayBullAnim(UAnimSequence* Seq, bool bLoop);
	void DealAttackDamage();
	void WakePack();
	/** The monster's own marks in the sand: a cloven hoof print every stride (two narrow ovals side by side), a furrow while it dashes. */
	void StampTracks();
	FVector LastTrackPos = FVector::ZeroVector;
	bool bHasLastTrack = false;
	bool bTrackLeft = false;
	/** Special behaviour of the kind; returns true when it took over this tick. */
	bool TickSpecial(ADunePlayerCharacter& Target, float DeltaSeconds);
	EKind Kind = EKind::Normal;
	enum class ESpecial : uint8 { None, Windup, Dash, Recover };
	ESpecial Special = ESpecial::None;
	float SpecialTimer = 0.f;
	float NextSpecialTime = 0.f;
	FVector DashDir = FVector::ForwardVector;
	bool bDashHit = false;
	int32 DashCount = 0;
	int32 ThrowCount = 0;
	bool bAggro = true;
	int32 PackId = -1;
	float AggroRadiusCm = 1500.f;

	// SciFi_Beast06 "Bull" animations, loaded by path in the constructor
	UPROPERTY() TObjectPtr<UAnimSequence> AnimRun;
	UPROPERTY() TObjectPtr<UAnimSequence> AnimIdle;
	UPROPERTY() TArray<TObjectPtr<UAnimSequence>> AnimAttacks;
	UPROPERTY() TArray<TObjectPtr<UAnimSequence>> AnimDeaths;
	UPROPERTY() TObjectPtr<UAnimSequence> CurrentAnim;
	UPROPERTY() TObjectPtr<UAnimSequence> AnimHitFront;
	UPROPERTY() TObjectPtr<UAnimSequence> AnimHitBack;
	UPROPERTY() TObjectPtr<UAnimSequence> AnimHitLeft;
	UPROPERTY() TObjectPtr<UAnimSequence> AnimHitRight;
	float AttackAnimUntil = 0.f;
	float StunUntil = 0.f;
	int32 FlinchCount = 0;

	UPROPERTY(VisibleAnywhere) TObjectPtr<UDuneHealthComponent> Health;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class USandInteractionComponent> Sand;
	float LastAttackTime = -100.f;
	float RepathTimer = 0.f;
	// stuck handling: every 0.7 s the progress is checked; no progress = a short sidestep, then try the path again
	FVector LastCheckPos = FVector::ZeroVector;
	float StuckTimer = 0.f;
	float SidestepTime = 0.f;
	float SidestepSign = 1.f;
};
