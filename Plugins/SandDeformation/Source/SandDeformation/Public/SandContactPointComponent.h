// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "SandContactPointComponent.generated.h"

UENUM(BlueprintType)
enum class ESandContactMode : uint8
{
	/** A wheel or runner: a continuous track while it rolls, a crater when it lands from the air. Place the component at the hub. */
	Wheel,
	/** A body that rests on or is dragged over the sand: a crate, a landing leg, a vehicle belly. Place it at the lowest point, or let it take the owner's footprint. */
	Pad
};

/**
 * One point where an actor touches the ground, for any actor (vehicle wheel, spaceship landing leg, crate). Add one per wheel or leg.
 * Every tick interval (not every frame) it traces down once; when the ground is sand of a surface it sends a request (FSandInteractionRequest) to that
 * surface: a landing becomes an Impact (depth from the speed it came down with), a rolling wheel a Wheel segment from the last stamped position,
 * a resting pad one ObjectContact (limited depth, sent once, again only after it has moved), a dragged pad a trench. A stationary contact sends nothing.
 * The speed is the movement of this point between ticks, so it works for physics bodies, scripted movement and attached components alike.
 */
UCLASS(ClassGroup=(SandDeformation), meta=(BlueprintSpawnableComponent))
class SANDDEFORMATION_API USandContactPointComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	USandContactPointComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand")
	ESandContactMode Mode = ESandContactMode::Wheel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand")
	bool bContactEnabled = true;

	/** Wheel: radius (cm); the wheel touches when the ground is this far below the hub (plus ToleranceCm). Pad: 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Shape")
	float WheelRadiusCm = 35.f;

	/** Wheel mode only: the body is a ball of radius WheelRadiusCm. Landings dent in the cap of that sphere and rolling leaves a groove with a round bottom (shape Sphere) instead of a flat tyre track. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Shape")
	bool bSphericalBody = false;

	/** Half width of the tyre / pad (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Shape")
	float HalfWidthCm = 12.f;

	/** Pad: half length (cm) along the owner's forward direction. 0 = a round pad of HalfWidthCm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Shape")
	float HalfLengthCm = 0.f;

	/** Pad: take the footprint (half length / width and the bottom centre) from the owner's root primitive, so a crate needs no numbers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Shape")
	bool bAutoExtentFromOwner = false;

	/** Load carried by this point (kg). <= 0 = the mass of the owner's root primitive divided by the number of contact points on the owner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Pressure")
	float LoadKg = 0.f;

	/** The load that makes Strength 1 (kg). Strength = load / this, between 0.2 and 4. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Pressure")
	float ReferenceLoadKg = 250.f;

	/** Deepest a resting pad presses (cm): standing still never makes it deeper. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Pressure")
	float StaticDepthLimitCm = 2.5f;

	/** Slower than this (cm/s) the contact counts as resting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Contact")
	float MinSpeedCm = 30.f;

	/** A track segment / drag stamp is sent after this much movement (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Contact")
	float MinMoveCm = 8.f;

	/** A resting pad stamps again only after it has been moved this far (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Contact")
	float MinStaticMoveCm = 12.f;

	/** A touchdown slower than this (cm/s) is not an impact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Contact")
	float ImpactMinSpeedCm = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Contact")
	float ToleranceCm = 3.f;

	/** Seconds between ground traces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Contact", meta=(ClampMin="0.005"))
	float TraceIntervalSec = 0.033f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand|Debug")
	bool bDebugLog = false;

	UFUNCTION(BlueprintPure, Category="Sand")
	bool IsInContact() const { return bInContact; }

	int32 GetImpactStamps() const { return ImpactStamps; }
	int32 GetWheelSegments() const { return WheelSegments; }
	int32 GetStaticStamps() const { return StaticStamps; }
	int32 GetDragStamps() const { return DragStamps; }
	int32 GetTraces() const { return Traces; }
	float GetLastImpactSpeed() const { return LastImpactSpeed; }
	float GetLastDepthCm() const { return LastDepthCm; }
	float GetLastHalfWidthCm() const { return LastHalfWidthCm; }
	const TArray<FVector>& GetStampPath() const { return StampPath; }

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void Geometry(FVector& OutLocation, FVector2D& OutDirection, float& OutHalfLength, float& OutHalfWidth) const;
	float ResolveLoadKg() const;

	bool bInContact = false;
	bool bHasPrevious = false;
	bool bHasLast = false;
	bool bStaticDone = false;
	FVector PreviousLocation = FVector::ZeroVector;
	FVector LastStamp = FVector::ZeroVector;
	FVector LastStatic = FVector::ZeroVector;
	float AirMaxDown = 0.f;
	int32 ContactPointsOnOwner = 1;
	int32 ImpactStamps = 0;
	int32 WheelSegments = 0;
	int32 StaticStamps = 0;
	int32 DragStamps = 0;
	int32 Traces = 0;
	float LastImpactSpeed = 0.f;
	float LastDepthCm = 0.f;
	float LastHalfWidthCm = 0.f;
	TArray<FVector> StampPath;	// where this point stamped (wheel positions), for tests
};
