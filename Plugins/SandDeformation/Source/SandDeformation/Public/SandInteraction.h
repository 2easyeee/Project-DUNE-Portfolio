// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SandInteraction.generated.h"

class AActor;

/** What is pressing into the sand. Every brush type ends in the same GPU heightfield; they only differ in the pressure distribution and the defaults. */
UENUM(BlueprintType)
enum class ESandBrushType : uint8
{
	/** A foot: egg shaped print pointing along the movement direction. */
	Footprint,
	/** A wheel or runner: a continuous trench from PreviousPosition to WorldPosition. */
	Wheel,
	/** A body resting on or dragged over the sand (box, crate, landing pad). */
	ObjectContact,
	/** A landing or a hit: a crater with a raised ring; depth follows ImpactSpeed. */
	Impact,
	/** Free choice: set Shape yourself. */
	Custom
};

/** The outline of the pressed area. Auto picks one from the brush type (Footprint: Foot, Wheel: Segment, ObjectContact: Box (Round without Extent), Impact: Round (Box with Extent)). */
UENUM(BlueprintType)
enum class ESandContactShape : uint8
{
	Auto,
	Round,
	Foot,
	Segment,
	Box,
	/** A ball pressing into the sand: Radius is the radius of the SPHERE (not of the dent). The dent is the cap of that sphere, so its width follows from the depth; with a previous position it is a groove with a round bottom. */
	Sphere
};

/**
 * One request to deform the sand, the same for every kind of actor. Lengths in cm, speeds in cm/s, angles in degrees.
 * Depth: DepthOverrideCm when it is above 0, else Strength x Pressure x the surface's DepthPerStrengthCm, times sqrt(ImpactSpeed / ImpactReferenceSpeed)
 * when ImpactSpeed is above 0; never deeper than the surface's MaxDepressionCm. The raised rim follows from the depth (RidgeScale 0 = none).
 */
USTRUCT(BlueprintType)
struct SANDDEFORMATION_API FSandInteractionRequest
{
	GENERATED_BODY()

	/** Centre of the contact (world). Only X and Y are used to place the mark; the surface under it is found by this position. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand")
	FVector WorldPosition = FVector::ZeroVector;

	/** Normal of the ground at the contact (informational; a steep slope can lower the pressure in the callers). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand")
	FVector SurfaceNormal = FVector::UpVector;

	/** Where the object moves (X and Y used). Gives the direction of a foot, a wheel track or a box. Zero = OrientationYawDeg. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand")
	FVector MovementDirection = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand")
	float OrientationYawDeg = 0.f;

	/** Wheel: where the contact was at the last request. The trench runs from here to WorldPosition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand")
	FVector PreviousPosition = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand")
	bool bHasPreviousPosition = false;

	/** Half width of the pressed area (cm): a foot, a wheel, the radius of a round dent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand", meta=(ClampMin="0"))
	float Radius = 20.f;

	/** Half length and half width (cm) of a Foot or Box. 0 = derived from Radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand")
	FVector2D Extent = FVector2D::ZeroVector;

	/** 1 = DepthPerStrengthCm deep. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand", meta=(ClampMin="0"))
	float Strength = 1.f;

	/** Applied pressure as a factor on the depth (a heavier or sharper contact). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand", meta=(ClampMin="0"))
	float Pressure = 1.f;

	/** Speed of the contact just before it touched (cm/s): a landing or a hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand", meta=(ClampMin="0"))
	float ImpactSpeed = 0.f;

	/** > 0: the depth in cm, ignoring Strength, Pressure and ImpactSpeed (still limited by the surface). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand", meta=(ClampMin="0"))
	float DepthOverrideCm = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand", meta=(ClampMin="0"))
	float RidgeScale = 1.f;

	/** Foot: how much sand a stepping-off foot throws back (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand", meta=(ClampMin="0", ClampMax="1"))
	float Push = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand")
	ESandBrushType Brush = ESandBrushType::Custom;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand")
	ESandContactShape Shape = ESandContactShape::Auto;

	/** Add this dent to what is already there (a repeated landing digs deeper; the depth saturates smoothly at the surface's MaxDepressionCm). Off = the dent is never deeper than this one request (footprints, tracks). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand")
	bool bAccumulate = false;

	/** Who asks (informational, for logs and tests). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sand")
	TObjectPtr<AActor> SourceActor = nullptr;
};
