// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SandSurfaceComponent.h"
#include "SandInteractionComponent.generated.h"

class ACharacter;

/**
 * Makes an actor leave marks in sand. Two ways in:
 *  - RequestDeformation: any actor (a sphere, a vehicle, a Blueprint) asks for a mark at a position; no character needed.
 *  - Automatic marks for an ACharacter (the owner, or TrackedActor): a foot-shaped print every time a foot is planted (from the foot / ball
 *    bones, or every FallbackStrideCm when the mesh has none) and a crater when it lands from a fall. Depth follows speed, WeightKg and the slope.
 * Both end in the same USandSurfaceComponent (found through the USandSimulationSubsystem by world position).
 */
UCLASS(ClassGroup=(SandDeformation), meta=(BlueprintSpawnableComponent))
class SANDDEFORMATION_API USandInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USandInteractionComponent();

	/** Mass of the actor (kg). The depth of the automatic marks scales with WeightKg / ReferenceWeightKg. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Pressure")
	float WeightKg = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Pressure")
	float ReferenceWeightKg = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Pressure")
	float SandSoftness = 1.f;

	/** The most WeightKg / ReferenceWeightKg may scale the depth and width (a heavy character, e.g. a golem: raise it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Pressure")
	float MaxPressureFactor = 3.f;

	/** A large character: foot size follows the bones (heel to ball) plus 7.5 cm x FootSizeScale, limited to these half lengths (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Footprints")
	float FootSizeScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Footprints")
	float FootMinHalfLenCm = 11.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Footprints")
	float FootMaxHalfLenCm = 16.f;

	/** Automatic marks while a character walks and lands (the owner must be an ACharacter, or set TrackedActor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Character")
	bool bAutomaticCharacterMarks = true;

	/**
	 * The sand is drawn higher than the ground it lies on when the surface lifts it (MaterialLiftCm / GroundOffsetCm), but the capsule stands on the ground, so the feet would sink
	 * into the drawn sand. This raises the character's skeletal mesh (a visual offset only: capsule, collision and movement are untouched) by the lift of the untouched sand under it.
	 * Limit: a dent made by the character itself is not followed (reading the heightfield would stall the GPU), so the feet float above a deep dent by its depth.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Character")
	bool bAlignMeshToSurface = true;

	/** How fast the mesh offset follows the lift (1/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Character", meta=(ClampMin="0.1"))
	float MeshAlignSpeed = 10.f;

	/** The visual lift currently applied to the mesh (cm). */
	UFUNCTION(BlueprintPure, Category="Sand|Character")
	float GetAppliedMeshLiftCm() const { return AppliedMeshLiftCm; }

	/** Foot prints from the bones named below; false (or missing bones) = a print every FallbackStrideCm of travel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Character")
	bool bFootStamps = true;

	/** Bone names, left then right (UE mannequin names by default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Character")
	TArray<FName> FootBones = { FName(TEXT("foot_l")), FName(TEXT("foot_r")) };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Character")
	TArray<FName> BallBones = { FName(TEXT("ball_l")), FName(TEXT("ball_r")) };

	/** A foot counts as planted when its ball bone is below PlantEnterCm above the floor and lifted again above PlantExitCm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Character")
	float PlantEnterCm = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Character")
	float PlantExitCm = 4.f;

	/** Without foot bones: distance between two prints (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Character")
	float FallbackStrideCm = 60.f;

	/** Below this horizontal speed (cm/s) nothing is recorded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Character")
	float MinMovementSpeed = 40.f;

	/** At this speed the prints are deepest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Character")
	float FullTrailSpeed = 500.f;

	/** From this horizontal speed (cm/s) on, a stamp counts as running, below it as walking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Character")
	float RunSpeedThreshold = 380.f;

	/** Half width of a print (cm); the length comes from the bones (heel to toe). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Footprints")
	float FootHalfWidthCm = 5.8f;

	/** Depth of the pressed-down centre (cm) at walking and at running speed, for the reference weight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Footprints")
	float FootDepthWalk = 2.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Footprints")
	float FootDepthRun = 4.4f;

	/** How much sand a stepping-off foot throws back (0..1) when walking and when running. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Footprints")
	float FootPushWalk = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Footprints")
	float FootPushRun = 0.7f;

	/** Landings slower than this (cm/s, vertical speed just before touching down) leave nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Landing")
	float LandingMinSpeed = 200.f;

	/** Vertical speed (cm/s) of the strongest landing; faster is clamped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Landing")
	float LandingMaxSpeed = 1300.f;

	/** Depth (cm) and half width (cm) of the weakest and strongest landing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Landing")
	float LandingMinDepth = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Landing")
	float LandingMaxDepth = 9.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Landing")
	float LandingMinWidth = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Landing")
	float LandingMaxWidth = 80.f;

	/** Throttled log of contacts and stamps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Debug")
	bool bDebugLog = false;

	/** The actor whose movement makes the marks (default: the owner). */
	UFUNCTION(BlueprintCallable, Category="Sand")
	void SetTrackedActor(AActor* Actor);

	/** A mark at a world position, from any actor. Finds the surface under the position. Returns false when there is none or the inputs are invalid. */
	UFUNCTION(BlueprintCallable, Category="Sand")
	bool RequestDeformation(FVector WorldPosition, float Radius = 30.f, float Strength = 1.f, ESandBrush Brush = ESandBrush::Round, float DirectionYawDeg = 0.f);

	int32 GetWalkStamps() const { return WalkStamps; }
	int32 GetRunStamps() const { return RunStamps; }
	int32 GetLandingStamps() const { return LandingStamps; }
	int32 GetFootStamps() const { return FootStampCount; }
	int32 GetStampCount() const { return StampCount; }
	int32 GetLandingsIgnored() const { return LandingsIgnored; }
	int32 GetSkippedStill() const { return SkippedStill; }
	int32 GetSkippedAir() const { return SkippedAir; }
	int32 GetSkippedNotSand() const { return SkippedNotSand; }
	int32 GetSkippedOutside() const { return SkippedOutside; }
	float GetLastDepth() const { return LastDepth; }
	float GetLastWidth() const { return LastWidth; }
	float GetLastSpeed() const { return LastSpeed; }
	const TCHAR* GetLastKind() const { return LastKind; }
	/** Centres of the foot prints stamped so far (the last 256), for tests. */
	const TArray<FVector>& GetFootStampPath() const { return FootStampPath; }
	float GetFootBallMin(int32 Foot) const { return BallMin[Foot]; }
	float GetFootBallMax(int32 Foot) const { return BallMax[Foot]; }
	/** The character is standing on sand of a surface right now. */
	bool IsOnSand() const;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	ACharacter* GetCharacter() const;
	USandSurfaceComponent* FindSurface(const FVector& World, float MarginCm) const;
	bool UpdateFeet(ACharacter* Character, USandSurfaceComponent* Surface, float Speed, float Pressure);
	void PlantStamp(USandSurfaceComponent* Surface, const FVector2D& Centre, const FVector2D& Dir, float HalfLen, float Speed, float Pressure, const FVector& FloorNormal);
	void RecordKind(const TCHAR* Kind, float Depth, float Width, float Speed);
	UFUNCTION()
	void OnCharacterLanded(const FHitResult& Hit);

	void AlignMesh(ACharacter* Character, float DeltaTime);

	float AppliedMeshLiftCm = 0.f;
	TWeakObjectPtr<USceneComponent> AlignedMesh;	// the mesh the offset was applied to
	TArray<FVector> FootStampPath;
	TWeakObjectPtr<AActor> TrackedActor;
	TWeakObjectPtr<AActor> BoundCharacter;
	bool bPlanted[2] = { false, false };
	float BallMin[2] = { 1e6f, 1e6f };
	float BallMax[2] = { -1e6f, -1e6f };
	float FallSpeed = 0.f;
	float PendingLandSpeed = 0.f;
	FVector PendingLandPos = FVector::ZeroVector;
	bool bPendingLand = false;
	bool bHasLast = false;
	bool bFallbackLeft = false;
	FVector LastPoint = FVector::ZeroVector;
	float LogTimer = 0.f;
	int32 WalkStamps = 0;
	int32 RunStamps = 0;
	int32 LandingStamps = 0;
	int32 FootStampCount = 0;
	int32 StampCount = 0;
	int32 LandingsIgnored = 0;
	int32 SkippedStill = 0;
	int32 SkippedAir = 0;
	int32 SkippedNotSand = 0;
	int32 SkippedOutside = 0;
	float LastDepth = 0.f;
	float LastWidth = 0.f;
	float LastSpeed = 0.f;
	const TCHAR* LastKind = TEXT("none");
};
