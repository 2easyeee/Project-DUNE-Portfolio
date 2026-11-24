// Project DUNE (full game): the Slab Bearer boss test. A copy of the boss arena (L_BossArena_Slab) whose game mode puts the hand-made boss, scaled up to
// giant size, where the core hovers. ADuneSlabBoss is an ADuneBossCore with another body: it has the core's health, phases, orb barrage, summons and
// shockwave (the HUD bar, the sand furrows, the self test all work), but the glowing sphere and its rings are hidden, the hit boxes sit on the giant body
// and the retargeted roar plays whenever it starts an attack. The mesh has its own skeleton: the sample animation is retargeted to it
// (A_MonsterRoar_Slab, see Saved/Day30/retarget.py). A UE5 mannequin with the original animation can be added next to the player's start for comparison.

#pragma once

#include "CoreMinimal.h"
#include "Dune/Boss/DuneBossCore.h"
#include "Dune/Boss/DuneBossGameMode.h"
#include "GameFramework/Actor.h"
#include "DuneSlabBoss.generated.h"

class UAnimSequence;
class UBoxComponent;
class UCapsuleComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class USphereComponent;

/** The fighting boss: the core's logic in the Slab Bearer's body. */
UCLASS()
class UE_HOLDITTOGETHER_API ADuneSlabBoss : public ADuneBossCore
{
	GENERATED_BODY()

public:
	ADuneSlabBoss();

	/** The mesh is about 190 cm tall as imported; this makes it about 21 m. */
	UPROPERTY(EditAnywhere, Category="Slab") float MeshScale = 11.f;
	/** The body is turned this far (degrees, negative = to the left of the boss) against the actor yaw that points at the player: the mesh did not face the player. */
	UPROPERTY(EditAnywhere, Category="Slab") float BodyYawOffsetDeg = -45.f;
	/** The light that shows the boss (the arena is nearly black): height above the feet and strength. */
	UPROPERTY(EditAnywhere, Category="Slab") float LightHeightCm = 1500.f;
	UPROPERTY(EditAnywhere, Category="Slab") float LightIntensity = 600000.f;
	/** The sand storm while the boss acts: two dust cylinders (outer = the arena, inner = around the boss), and ripples running over the sand. */
	UPROPERTY(EditAnywhere, Category="Storm") float StormOuterRadiusCm = 4300.f;
	UPROPERTY(EditAnywhere, Category="Storm") float StormInnerRadiusCm = 1300.f;
	UPROPERTY(EditAnywhere, Category="Storm") float StormHeightCm = 3000.f;
	/** How long the storm stays after an action starts (on top of the roar), and how fast it rises / dies (per second). */
	UPROPERTY(EditAnywhere, Category="Storm") float StormHoldAfterRoar = 1.5f;
	UPROPERTY(EditAnywhere, Category="Storm") float StormRiseRate = 2.5f;
	UPROPERTY(EditAnywhere, Category="Storm") float StormFallRate = 0.45f;
	/** Ripples: a ring leaves the boss every RippleInterval s, runs out at RippleSpeed cm/s and ploughs a trench with raised sides where it passes the player. */
	UPROPERTY(EditAnywhere, Category="Storm") float RippleInterval = 0.9f;
	UPROPERTY(EditAnywhere, Category="Storm") float RippleSpeed = 1100.f;
	UPROPERTY(EditAnywhere, Category="Storm") float RippleDepthCm = 7.f;
	/** Seconds the dead boss takes to topple over. */
	UPROPERTY(EditAnywhere, Category="Slab") float FallSeconds = 5.f;
	/** The stone slabs on the shoulders: they are the boss's attack (replacing the orbs). Six sit around the shoulders; an attack sends some of them at the player. */
	UPROPERTY(EditAnywhere, Category="Slabs") float SlabScale = 2.8f;
	/** How many slabs one attack sends (phase 1 / phase 2) and the delay between them (s). */
	UPROPERTY(EditAnywhere, Category="Slabs") int32 SlabsPerAttack1 = 3;
	UPROPERTY(EditAnywhere, Category="Slabs") int32 SlabsPerAttack2 = 4;
	UPROPERTY(EditAnywhere, Category="Slabs") int32 SlabsPerAttack3 = 5;
	UPROPERTY(EditAnywhere, Category="Slabs") float SlabStagger = 0.6f;
	/** Windup (the slab rises over the boss), then it flies with homing: how fast it turns towards the player (deg/s) and how fast it goes (cm/s). */
	UPROPERTY(EditAnywhere, Category="Slabs") float SlabWindupSeconds = 0.9f;
	UPROPERTY(EditAnywhere, Category="Slabs") float SlabTurnDegPerSec = 70.f;
	UPROPERTY(EditAnywhere, Category="Slabs") float SlabSpeedStart = 1500.f;
	UPROPERTY(EditAnywhere, Category="Slabs") float SlabSpeedMax = 2200.f;
	/** Closer than this to the player the slab stops steering (so it can be dodged). */
	UPROPERTY(EditAnywhere, Category="Slabs") float SlabCommitDistance = 2500.f;
	UPROPERTY(EditAnywhere, Category="Slabs") float SlabDamage = 25.f;
	/** The impact: the quake throws a player standing on the floor up (cm/s) within this radius, camera shake included. */
	UPROPERTY(EditAnywhere, Category="Slabs") float QuakeRadiusCm = 2200.f;
	UPROPERTY(EditAnywhere, Category="Slabs") float QuakeLaunchSpeed = 1600.f;
	/** The parry: one of the slabs of an attack has a yellow rim. A shot that hits it from at least ParryMinDistance sends it back into the boss (damage as a fraction of its health, then a pause). Unparried, it hits harder. */
	UPROPERTY(EditAnywhere, Category="Parry") float ParryMinDistance = 1200.f;
	UPROPERTY(EditAnywhere, Category="Parry") float ParryDamageFraction = 0.1f;
	UPROPERTY(EditAnywhere, Category="Parry") float ParryStaggerSeconds = 3.5f;
	UPROPERTY(EditAnywhere, Category="Parry") float ParryReturnSpeed = 4200.f;
	/** An unparried parry slab: the quake and the damage are this many times stronger. */
	UPROPERTY(EditAnywhere, Category="Parry") float FailMultiplier = 1.6f;

	// ---- phases: 1 above Phase2Fraction (slabs), 2 below it (slabs + sand eruptions), 3 below Phase3Fraction (slabs, eruptions, the sand tsunami)
	UPROPERTY(EditAnywhere, Category="Phases") float Phase3Fraction = 0.30f;
	/** Seconds of rest after an action, per phase. */
	UPROPERTY(EditAnywhere, Category="Phases") float RestSeconds1 = 3.5f;
	UPROPERTY(EditAnywhere, Category="Phases") float RestSeconds2 = 2.8f;
	UPROPERTY(EditAnywhere, Category="Phases") float RestSeconds3 = 2.2f;
	/** The boss turns towards the player (deg/s per phase) and glides (cm/s per phase) when the player is farther than MoveFarCm. */
	UPROPERTY(EditAnywhere, Category="Move") float TurnDegPerSec1 = 40.f;
	UPROPERTY(EditAnywhere, Category="Move") float TurnDegPerSec2 = 55.f;
	UPROPERTY(EditAnywhere, Category="Move") float TurnDegPerSec3 = 75.f;
	UPROPERTY(EditAnywhere, Category="Move") float GlideSpeed1 = 330.f;
	UPROPERTY(EditAnywhere, Category="Move") float GlideSpeed2 = 480.f;
	UPROPERTY(EditAnywhere, Category="Move") float GlideSpeed3 = 650.f;
	UPROPERTY(EditAnywhere, Category="Move") float MoveFarCm = 3200.f;
	/** Off (the default): the boss only turns to face the player and stays where it stands. On: it also glides closer when the player is farther than MoveFarCm. */
	UPROPERTY(EditAnywhere, Category="Move") bool bGlideTowardPlayer = false;
	/** The boss never leaves this radius round the arena centre. */
	UPROPERTY(EditAnywhere, Category="Move") float RoamRadiusCm = 1800.f;
	// ---- the barrier: closer than BarrierOuterCm the sand covers the player's view (fully at BarrierInnerCm); touching the body (ContactRadiusCm) hurts
	UPROPERTY(EditAnywhere, Category="Barrier") float BarrierOuterCm = 1600.f;
	UPROPERTY(EditAnywhere, Category="Barrier") float BarrierInnerCm = 850.f;
	UPROPERTY(EditAnywhere, Category="Barrier") float ContactRadiusCm = 640.f;
	UPROPERTY(EditAnywhere, Category="Barrier") float ContactDamage = 14.f;
	UPROPERTY(EditAnywhere, Category="Barrier") float ContactInterval = 0.7f;
	// ---- sand eruptions (the digging attack): marks on the floor, then the sand bursts up; the player standing on a mark is hurt and thrown
	UPROPERTY(EditAnywhere, Category="Eruption") int32 EruptionsPhase2 = 4;
	UPROPERTY(EditAnywhere, Category="Eruption") int32 EruptionsPhase3 = 7;
	UPROPERTY(EditAnywhere, Category="Eruption") float EruptionRadiusCm = 270.f;
	UPROPERTY(EditAnywhere, Category="Eruption") float EruptionWarnSeconds = 1.4f;
	UPROPERTY(EditAnywhere, Category="Eruption") float EruptionStagger = 0.4f;
	UPROPERTY(EditAnywhere, Category="Eruption") float EruptionDamage = 18.f;
	// ---- the sand tsunami: a wall of sand runs out from the boss; six lanes (marked green on the floor) are safe
	UPROPERTY(EditAnywhere, Category="Tsunami") float TsunamiWarnSeconds = 2.6f;
	UPROPERTY(EditAnywhere, Category="Tsunami") float TsunamiSpeed = 1250.f;
	UPROPERTY(EditAnywhere, Category="Tsunami") float TsunamiHeightCm = 1100.f;
	UPROPERTY(EditAnywhere, Category="Tsunami") float LaneWidthCm = 950.f;
	UPROPERTY(EditAnywhere, Category="Tsunami") float TsunamiDamage = 30.f;

	/** 0..1: how much the sand covers the view (the HUD draws it). */
	float GetVeil() const { return Veil; }
	/** Self tests: force an action now (0 slabs, 1 eruptions, 2 tsunami, 3 stagger). */
	void DebugAction(int32 Which);
	int32 GetActionId() const { return static_cast<int32>(Action); }
	void PatternTimer_Debug(float Seconds) { PatternTimer = Seconds; } // self tests: hold the next automatic action off
	/** Where the last slab landed and the direction it came from (a picture aid: the crater and the skid trench lie there). */
	FVector LastSlabHitPos = FVector::ZeroVector;
	FVector LastSlabHitDir = FVector::ForwardVector;
	int32 EruptionCount = 0;
	int32 EruptionHits = 0;
	int32 TsunamiCount = 0;
	int32 TsunamiHits = 0;
	int32 ContactHits = 0;

	/** For the HUD: is the aim ray on a flying parry slab? OutDistance = the distance to it, bOutInRange = far enough to parry. */
	bool QueryParry(const FVector& Eye, const FVector& Dir, float& OutDistance, bool& bOutInRange) const;
	/** Self test: where the flying parry slab is (false when there is none). */
	bool GetParrySlab(FVector& OutPos) const
	{
		for (const FSlabState& S : Slabs) { if (S.bParry && S.State == ESlabState::Flying) { OutPos = S.Pos; return true; } }
		return false;
	}
	int32 ParrySuccesses = 0;
	int32 ParryFailures = 0;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual FVector GetProjectileOrigin() const override;
	virtual void OnPatternStarted() override;
	virtual void OnPhase2Entered() override;
	virtual void ChoosePattern() override;
	virtual void HandleDeath() override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> BodyMesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCapsuleComponent> BodyHitbox;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> HeadHitbox;
	UPROPERTY() TObjectPtr<UAnimSequence> Roar;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> StormOuter;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> StormInner;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> StormMatOuter;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> StormMatInner;
	void TickStorm(float DeltaSeconds);
	void StampRipple(float Radius, const FVector& PlayerLocation, float TrenchRadius, float DepthCm, float Ridge);

	// ---- animations (the _SlabArm retargets of the Day 32 work; _Slab where there is none) and the action the boss is in
	enum class EAction : uint8 { Idle, Volley, Eruption, Tsunami, Stagger, Transition, Dead };
	UPROPERTY() TObjectPtr<UAnimSequence> AnimIdle;
	UPROPERTY() TObjectPtr<UAnimSequence> AnimPeck;
	UPROPERTY() TObjectPtr<UAnimSequence> AnimHowl;
	UPROPERTY() TObjectPtr<UAnimSequence> AnimDig;
	UPROPERTY() TObjectPtr<UAnimSequence> AnimStagger;
	UPROPERTY() TObjectPtr<UAnimSequence> AnimDeath;
	UAnimSequence* LoadSlabAnim(const TCHAR* Name) const;
	void PlayAnim(UAnimSequence* Seq, bool bLoop, float Rate = 1.f);
	void StartAction(EAction NewAction);
	void EndAction(float RestSeconds);
	void TickAction(float DeltaSeconds);
	void TickMovement(float DeltaSeconds);
	void TickBarrier(float DeltaSeconds);
	float RestSeconds() const { return Phase == 1 ? RestSeconds1 : (Phase == 2 ? RestSeconds2 : RestSeconds3); }
	EAction Action = EAction::Idle;
	EAction LastAttack = EAction::Idle;
	float ActionTime = 0.f;
	float ActionLength = 0.f;
	bool bVolleySent = false;
	bool bPhase3Entered = false;
	float Veil = 0.f;
	float ContactTimer = 0.f;
	float GlideDustTimer = 0.f;

	// ---- sand grains: one instanced mesh of small spheres used as a particle pool (bursts, dust of the gliding boss, the blown sand of the barrier)
	struct FGrain { FVector Pos = FVector::ZeroVector; FVector Vel = FVector::ZeroVector; float Life = 0.f; float MaxLife = 1.f; float Size = 20.f; };
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> GrainMesh;
	TArray<FGrain> GrainData;
	TArray<FTransform> GrainTransforms;
	int32 GrainCursor = 0;
	void SpawnGrains(const FVector& Center, int32 Count, float RadiusCm, float MinSpeed, float MaxSpeed, float UpBias, float Life, float SizeCm, const FVector& Drift = FVector::ZeroVector);
	void TickGrains(float DeltaSeconds);

	// ---- eruptions
	struct FEruptSpot { FVector Pos = FVector::ZeroVector; float Time = 0.f; bool bDone = false; };
	TArray<FEruptSpot> Spots;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> SpotMarkers;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> SpotMats;
	void BuildEruptions();
	void TickEruptions();
	void ClearEruptions();
	void Erupt(const FVector& Pos);

	// ---- the tsunami
	struct FWaveGrain { int32 Sector = 0; float T = 0.f; float H = 0.f; float DR = 0.f; float Size = 30.f; float Phase = 0.f; };
	/** A vertical sheet of the wave (an instanced plane with the noisy dust material): the dust cloud the fine grains fly in. */
	struct FCurtain { int32 Sector = 0; float T = 0.f; int32 Layer = 0; float Height = 1000.f; float Phase = 0.f; float Width = 1.f; };
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> CurtainMesh;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CurtainMat;
	TArray<FCurtain> Curtains;
	TArray<FTransform> CurtainTransforms;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> WaveMesh;
	TArray<FWaveGrain> WaveGrains;
	TArray<FTransform> WaveTransforms;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> LaneMarkers;
	TArray<float> LaneAngles;   // radians, six safe lanes
	bool bTsunamiRunning = false;
	bool bTsunamiHitPlayer = false;
	float TsunamiRadius = 0.f;
	float TsunamiSandTimer = 0.f;
	void BuildTsunami();
	void TickTsunami(float DeltaSeconds);
	void ClearTsunami();
	bool IsInLane(const FVector& Location) const;
	float Storm = 0.f;         // 0..1, the strength of the storm
	float StormHold = 0.f;     // seconds the storm is held up
	float RippleTimer = 0.f;
	float BoilTimer = 0.f;
	TArray<float> Ripples;     // radii of the rings on their way out
	float DeathTime = 0.f;
public:
	int32 SlabLandings = 0; // slabs that have hit the ground (self test)
private:
	float StartYaw = 0.f;
	// ---- the slabs
	enum class ESlabState : uint8 { Attached, Windup, Flying, Reflected, Embedded, Returning };
	struct FSlabState
	{
		ESlabState State = ESlabState::Attached;
		FVector RestOffset = FVector::ZeroVector; // from the shoulder anchor, in the boss's own axes (x forward, y right, z up), cm
		FRotator RestRot = FRotator::ZeroRotator;
		float Delay = 0.f;       // seconds until it starts its windup (the stagger of an attack)
		float Timer = 0.f;
		float Speed = 0.f;
		FVector Pos = FVector::ZeroVector;
		FVector Dir = FVector::ForwardVector;
		FQuat Rot = FQuat::Identity;
		FVector ReturnFrom = FVector::ZeroVector;
		FQuat ReturnRot = FQuat::Identity;
		bool bParry = false;     // the slab with the yellow rim
	};
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> SlabRoots;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> SlabMeshes;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> SlabGlows;   // a slightly bigger yellow shell around the slab that can be parried
	TArray<FSlabState> Slabs;
	UPROPERTY() TArray<TObjectPtr<UBoxComponent>> SlabHitboxes;              // only a flying parry slab can be shot
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> SlabMarkers;        // the landing mark on the floor
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> SlabMats;       // for the yellow rim
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> MarkerMats;
	void TryParry(int32 Index);
	void SetParryLook(int32 Index, bool bOn);
	void TickSlabs(float DeltaSeconds);
	void SlabRestTransform(const FSlabState& S, FVector& OutPos, FQuat& OutRot) const;
	void SlabImpact(int32 Index, bool bHitPlayer);
	FVector SlabAnchor() const;
	virtual void DoBarrage() override;
};

/** A skeletal mesh (optionally playing one animation in a loop), scaled as a whole. Spawn it deferred so the paths are set before BeginPlay. */
UCLASS()
class UE_HOLDITTOGETHER_API ADuneAnimPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	ADuneAnimPreviewActor();

	UPROPERTY(EditAnywhere, Category="Preview") FSoftObjectPath MeshPath;
	/** Optional: an animation for the mesh's skeleton, looped. */
	UPROPERTY(EditAnywhere, Category="Preview") FSoftObjectPath AnimPath;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> Mesh;
};

UCLASS()
class UE_HOLDITTOGETHER_API ADuneSlabBossGameMode : public ADuneBossGameMode
{
	GENERATED_BODY()

public:
	/** The boss faces -X (towards the player start at X -2600). */
	UPROPERTY(EditDefaultsOnly, Category="Slab") float BossYaw = 180.f;
	/** A mannequin playing the original roar next to the player's start (for comparing the retarget). Off: it is in the way of the fight. */
	UPROPERTY(EditDefaultsOnly, Category="Slab") bool bSpawnMannequin = false;
	UPROPERTY(EditDefaultsOnly, Category="Slab") FVector MannequinLocation = FVector(-1700.f, -350.f, 0.f);
	UPROPERTY(EditDefaultsOnly, Category="Slab") float MannequinYaw = 180.f;

protected:
	virtual void SpawnBoss() override;
	virtual void BeginPlay() override;

private:
	ADuneAnimPreviewActor* SpawnPreview(const TCHAR* Mesh, const TCHAR* Anim, const FVector& Location, float Yaw, float Scale);
	void TakeShot(const TCHAR* Name);
};
