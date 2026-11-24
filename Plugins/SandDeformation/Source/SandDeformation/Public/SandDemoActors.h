// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "SandDemoActors.generated.h"

class UStaticMesh;
class USandContactPointComponent;
class USandInteractionComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class ESandDemoKind : uint8
{
	/** A crate with real physics: dropped, it lands, rests and can be moved. One contact pad that takes the crate's footprint. */
	Box,
	/** Four wheels on a body; moves along a scripted arc (not a driving model). Four wheel contact points. */
	Vehicle,
	/** A body on four legs that descends and stops on the sand (scripted movement, not a flight model). Four pad contact points. */
	Lander,
	/** A ball with real physics: a round dent where it lands, a connected groove while it rolls (one wheel contact at its centre). BodyMesh should be a sphere. */
	Ball
};

/**
 * Demo / test actors for the plugin: engine shapes (or your own mesh through BodyMesh) with the contact components already set up, and a simple
 * scripted movement. They show how any actor feeds the one sand interaction API; they are not part of the sand system itself and can be ignored.
 * The components are created from the settings in OnConstruction (not saved), the movement starts at BeginPlay (bAutoRun) or with StartRun.
 */
UCLASS()
class SANDDEFORMATION_API ASandDemoActor : public AActor
{
	GENERATED_BODY()

public:
	ASandDemoActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Demo")
	ESandDemoKind Kind = ESandDemoKind::Box;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Demo")
	bool bAutoRun = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Demo")
	float StartDelaySec = 1.f;

	/** Replaces the engine cube as the body (the contact points are placed by the numbers below, not from this mesh). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Demo")
	TObjectPtr<UStaticMesh> BodyMesh;

	/** 0,0,0 = the default scale of the kind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Demo")
	FVector BodyScale = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Demo")
	float MassKg = 600.f;

	/** Box: simulate physics (falls, rests, bounces). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box")
	bool bSimulatePhysics = true;

	/** Box / Ball: velocity (cm/s) given when physics starts: a thrown ball rolls and leaves a trail. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box")
	FVector InitialVelocityCm = FVector::ZeroVector;

	// ---- Vehicle ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vehicle")
	float DriveSpeedCm = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vehicle")
	float DriveSeconds = 7.f;

	/** The vehicle starts to turn after TurnAfterSec at TurnRateDeg per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vehicle")
	float TurnAfterSec = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vehicle")
	float TurnRateDeg = 28.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vehicle")
	float WheelRadiusCm = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vehicle")
	float WheelHalfWidthCm = 12.5f;

	/** Half the distance between the axles and between the wheels (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vehicle")
	FVector2D WheelSpread = FVector2D(95.f, 72.f);

	// ---- Lander ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lander")
	float StartHeightCm = 500.f;

	/** Speed (cm/s) high up and during the last SlowHeightCm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lander")
	float ApproachSpeedCm = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lander")
	float TouchdownSpeedCm = 160.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lander")
	float SlowHeightCm = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lander")
	float LegSpreadCm = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lander")
	float PadHalfSizeCm = 35.f;

	/** Starts the scripted movement (vehicle: drive; lander: descend). The box needs none. */
	UFUNCTION(BlueprintCallable, Category="Demo")
	void StartRun();

	/** Spawns a Box or Ball with its physics settings applied before BeginPlay (a thrown ball needs its velocity at start). BodyMesh null = the engine cube / sphere; BodyScale zero = the default scale. */
	UFUNCTION(BlueprintCallable, Category="Demo", meta=(WorldContext="WorldContextObject"))
	static ASandDemoActor* SpawnDemo(const UObject* WorldContextObject, ESandDemoKind DemoKind, const FTransform& Transform, FVector InitialVelocity = FVector::ZeroVector, float Mass = 600.f, UStaticMesh* Mesh = nullptr, FVector Scale = FVector::ZeroVector);

	UFUNCTION(BlueprintPure, Category="Demo")
	bool IsRunFinished() const { return bFinished; }

	const TArray<USandContactPointComponent*>& GetContactPoints() const { return ContactPoints; }
	int32 GetTotalImpactStamps() const;
	int32 GetTotalWheelSegments() const;
	int32 GetTotalStaticStamps() const;
	int32 GetTotalTraces() const;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void Build();
	float GroundZ(const FVector& At, bool& bFound) const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UActorComponent>> Built;

	TArray<USandContactPointComponent*> ContactPoints;
	bool bVelocityPending = false;
	bool bRunning = false;
	bool bStarted = false;
	bool bFinished = false;
	float RunTime = 0.f;
	float WaitTime = 0.f;
	float Yaw = 0.f;
};

/**
 * A character that walks straight ahead by itself with a USandInteractionComponent (foot prints from its foot bones, or from its stride when the
 * mesh has none): for a heavy or large character. Set the skeletal mesh and animation on the Mesh component in the level; WeightKg and
 * FootSizeScale on the Interaction component make the prints heavier / larger.
 */
UCLASS()
class SANDDEFORMATION_API ASandDemoCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ASandDemoCharacter();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sand")
	TObjectPtr<USandInteractionComponent> Interaction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Demo")
	bool bAutoWalk = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Demo")
	float StartDelaySec = 1.f;

	/** Seconds walking straight ahead (the actor's forward direction); 0 = for ever. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Demo")
	float WalkSeconds = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Demo")
	float WalkInputScale = 0.4f;

	/** The walk is over (WalkSeconds passed, or it never starts). */
	bool IsWalkFinished() const { return !bAutoWalk || (WalkSeconds > 0.f && Time > StartDelaySec + WalkSeconds); }

protected:
	virtual void Tick(float DeltaSeconds) override;

private:
	float Time = 0.f;
};
