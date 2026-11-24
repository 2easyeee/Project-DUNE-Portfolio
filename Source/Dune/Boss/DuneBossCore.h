// Project DUNE (full game): the Omega-0 branch core, the boss. A glowing sphere with two rings of cubes turning around it, floating above the
// arena. It does not move toward the player. Patterns: Barrage (a fan of orbs), Summon (chasers), Shockwave (phase 2: a ring crossing the floor,
// jump over it). Phase 2 starts at 50% health. Health is the normal UDuneHealthComponent, so the player's shots damage it like any actor.
// Every ground hit (orb, wave) is also pressed into the sand through the SandDeformation plugin.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DuneBossCore.generated.h"

class UDuneHealthComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class USphereComponent;
class UStaticMeshComponent;

UCLASS()
class UE_HOLDITTOGETHER_API ADuneBossCore : public AActor
{
	GENERATED_BODY()

public:
	bool IsWaveActive() const { return bWaveActive; }
	float GetWaveRadius() const { return WaveRadius; }
	ADuneBossCore();

	UPROPERTY(EditAnywhere, Category="Boss") float MaxHealth = 1200.f;
	UPROPERTY(EditAnywhere, Category="Boss") float PatternInterval1 = 2.6f;
	UPROPERTY(EditAnywhere, Category="Boss") float PatternInterval2 = 1.9f;
	/** Health fraction at which phase 2 starts (the Slab Bearer has a third phase of its own). */
	UPROPERTY(EditAnywhere, Category="Boss") float Phase2Fraction = 0.5f;
	UPROPERTY(EditAnywhere, Category="Boss") float WaveSpeed = 1000.f;
	UPROPERTY(EditAnywhere, Category="Boss") float WaveDamage = 20.f;
	UPROPERTY(EditAnywhere, Category="Boss") float WaveMaxRadius = 4600.f;
	UPROPERTY(EditAnywhere, Category="Boss") int32 MaxChasers = 4;
	/** How far below the actor the floor is (the core floats 450 cm above it); the shockwave and the summons use it. */
	UPROPERTY(EditAnywhere, Category="Boss") float GroundOffsetCm = 450.f;
	/** Amplitude of the floating bob (cm). 0 = the actor stays put. */
	UPROPERTY(EditAnywhere, Category="Boss") float BobCm = 40.f;

	UFUNCTION(BlueprintPure, Category="Boss") int32 GetPhase() const { return Phase; }
	UFUNCTION(BlueprintPure, Category="Boss") bool IsDead() const;
	UFUNCTION(BlueprintPure, Category="Boss") float GetHealthFraction() const;
	UDuneHealthComponent* GetHealth() const { return Health; }

	// counters (self test and HUD)
	int32 BarrageCount = 0;
	int32 SummonCount = 0;
	int32 ShockwaveCount = 0;
	int32 OrbsFired = 0;
	int32 WaveHits = 0;

	/** Menu scenery: the core only turns and floats (no patterns, no waves). bDead shows it burnt out. Call right after spawning. */
	void SetDecorative(bool bDead);

	/** Forces a pattern now (self test). */
	void DebugBarrage() { DoBarrage(); }
	void DebugShockwave() { StartShockwave(); }

	/** Glowing material instance ("Glow" colour); falls back to the basic shape material. */
	static UMaterialInstanceDynamic* MakeGlow(UObject* Outer, const FLinearColor& Color);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	// hooks for a boss that looks different (ADuneSlabBoss) but fights the same way
	virtual FVector GetProjectileOrigin() const { return GetActorLocation(); }
	virtual void OnPatternStarted() {}
	virtual void OnPhase2Entered() {}
	virtual void HandleDeath() {}

	enum class EPattern : uint8 { Barrage, Summon, Shockwave };

	virtual void ChoosePattern();
	virtual void DoBarrage();
	void DoSummon();
	void StartShockwave();
	void TickShockwave(float DeltaSeconds);
	void EnterPhase2();
	UFUNCTION() void OnDied();

	UPROPERTY(VisibleAnywhere) TObjectPtr<UDuneHealthComponent> Health;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Hitbox;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> CoreMesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Light;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> RingA;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> RingB;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CoreMat;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> RingParts;

	bool bDecorative = false;
	int32 Phase = 1;
	float PatternTimer = 3.5f;
	float Age = 0.f;
	FVector BaseLocation = FVector::ZeroVector;

	// shockwave
	bool bWarning = false;
	float WarningTimer = 0.f;
	bool bWaveActive = false;
	float WaveRadius = 0.f;
	bool bWaveHitPlayer = false;
	float WaveSandTimer = 0.f;
	float GroundZ = 0.f;
};
