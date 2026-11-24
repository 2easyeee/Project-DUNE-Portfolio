// Copyright Epic Games, Inc. All Rights Reserved.

#include "SandContactPointComponent.h"
#include "SandSimulationSubsystem.h"
#include "SandSurfaceComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

USandContactPointComponent::USandContactPointComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void USandContactPointComponent::BeginPlay()
{
	Super::BeginPlay();
	PrimaryComponentTick.TickInterval = TraceIntervalSec; // the tick gets the time since the last one, so the speed stays right
	TInlineComponentArray<USandContactPointComponent*> Points;
	GetOwner()->GetComponents(Points);
	ContactPointsOnOwner = FMath::Max(1, Points.Num());
}

float USandContactPointComponent::ResolveLoadKg() const
{
	if (LoadKg > 0.f)
	{
		return LoadKg;
	}
	const UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
	return (Root ? Root->GetMass() : ReferenceLoadKg) / ContactPointsOnOwner;
}

void USandContactPointComponent::Geometry(FVector& OutLocation, FVector2D& OutDirection, float& OutHalfLength, float& OutHalfWidth) const
{
	const AActor* Owner = GetOwner();
	const FVector Forward = (Mode == ESandContactMode::Pad && bAutoExtentFromOwner) ? Owner->GetActorForwardVector() : GetForwardVector();
	OutDirection = FVector2D(Forward.X, Forward.Y).GetSafeNormal();
	OutLocation = GetComponentLocation();
	OutHalfLength = HalfLengthCm;
	OutHalfWidth = HalfWidthCm;
	const UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Owner->GetRootComponent());
	if (Mode == ESandContactMode::Pad && bAutoExtentFromOwner && Root)
	{
		// the footprint of the root primitive: its unscaled local bounds, scaled by the component scale, bottom centre in world space
		const FBoxSphereBounds Local = Root->CalcBounds(FTransform::Identity);
		const FVector Scale = Root->GetComponentScale().GetAbs();
		OutLocation = Root->GetComponentTransform().TransformPosition(Local.Origin - FVector(0.0, 0.0, Local.BoxExtent.Z));
		OutHalfLength = Local.BoxExtent.X * Scale.X;
		OutHalfWidth = Local.BoxExtent.Y * Scale.Y;
	}
}

void USandContactPointComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UWorld* World = GetWorld();
	const USandSimulationSubsystem* Subsystem = World ? World->GetSubsystem<USandSimulationSubsystem>() : nullptr;
	if (!bContactEnabled || !Subsystem || DeltaTime <= 0.f)
	{
		bHasPrevious = false;
		return;
	}
	FVector Location;
	FVector2D Direction;
	float HalfLength, HalfWidth;
	Geometry(Location, Direction, HalfLength, HalfWidth);
	const FVector Velocity = bHasPrevious ? (Location - PreviousLocation) / DeltaTime : FVector::ZeroVector;
	PreviousLocation = Location;
	bHasPrevious = true;

	const bool bPad = Mode == ESandContactMode::Pad;
	const float ContactDistance = bPad ? 0.f : WheelRadiusCm;
	const float ReachDown = ContactDistance + ToleranceCm;
	// the trace also looks as far down as the point came down since the last tick, so a hard landing that bounced up again between two ticks is still seen
	const float Extra = bInContact ? 0.f : AirMaxDown * DeltaTime;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SandContact), false, GetOwner());
	++Traces;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Location + FVector(0.0, 0.0, bPad ? 20.0 : 5.0), Location - FVector(0.0, 0.0, ReachDown + Extra), ECC_Visibility, Params);
	USandSurfaceComponent* Surface = bHit ? Subsystem->FindSurfaceAt(Hit.ImpactPoint) : nullptr;
	const float Clearance = bHit ? static_cast<float>(Location.Z - Hit.ImpactPoint.Z) - ContactDistance : 1e9f;
	const bool bTouching = bHit && Clearance <= ToleranceCm;
	const bool bBounced = bHit && !bTouching && !bInContact && AirMaxDown >= ImpactMinSpeedCm && Velocity.Z > 0.0;
	if (bDebugLog)
	{
		UE_LOG(LogSandDeformation, Log, TEXT("ContactPoint %s: dt %.3f at %s velocity z %.0f hit %d surface %d floor sand %d air-down %.0f in contact %d"), *GetName(), DeltaTime, *Location.ToString(), Velocity.Z, bHit, Surface != nullptr,
			Surface && Surface->IsSandFloor(Hit.GetActor()), AirMaxDown, bInContact);
	}
	if (!Surface || !Surface->IsSandFloor(Hit.GetActor()) || !(bTouching || bBounced))
	{
		bInContact = false;
		bHasLast = false;
		// what it touches down with: the downward speed, remembered for about 0.1 s so a tick that happens to catch the bounce does not hide a hard landing,
		// and a descent that slows down is not judged by its fastest moment
		AirMaxDown = FMath::Max(FMath::Max(0.f, -static_cast<float>(Velocity.Z)), AirMaxDown * FMath::Exp(-DeltaTime / 0.1f));
		return;
	}

	const FVector Pos = Hit.ImpactPoint;
	const float Strength = FMath::Clamp(ResolveLoadKg() / FMath::Max(ReferenceLoadKg, 1.f), 0.2f, 4.f);
	const float Speed2D = FVector2D(Velocity.X, Velocity.Y).Size();
	FSandInteractionRequest Request;
	Request.WorldPosition = Pos;
	Request.SurfaceNormal = Hit.ImpactNormal;
	Request.SourceActor = GetOwner();
	Request.Strength = Strength;
	Request.MovementDirection = FVector(Direction.X, Direction.Y, 0.0);
	Request.Radius = HalfWidth;
	LastHalfWidthCm = HalfWidth;
	const bool bBall = bSphericalBody && !bPad;
	if (bBall)
	{
		Request.Shape = ESandContactShape::Sphere;
		Request.Radius = WheelRadiusCm; // the radius of the sphere; the width of the dent follows from the depth
	}

	auto Static = [&]()
	{
		Request.Brush = ESandBrushType::ObjectContact;
		Request.Shape = HalfLength > 0.f ? ESandContactShape::Box : ESandContactShape::Round;
		Request.Extent = FVector2D(HalfLength, HalfWidth);
		Request.DepthOverrideCm = FMath::Min(Strength * Surface->DepthPerStrengthCm, StaticDepthLimitCm);
		if (Surface->ApplyInteraction(Request))
		{
			++StaticStamps;
			LastDepthCm = Request.DepthOverrideCm;
			LastStatic = Pos;
			bStaticDone = true;
		}
	};

	if (!bInContact)
	{
		// touchdown
		bInContact = true;
		const float Impact = AirMaxDown;
		AirMaxDown = 0.f;
		bHasLast = true;
		LastStamp = Pos;
		LastStatic = Pos;
		if (Impact >= ImpactMinSpeedCm)
		{
			Request.Brush = ESandBrushType::Impact;
			Request.bAccumulate = true;
			Request.Extent = FVector2D(HalfLength, HalfWidth);
			Request.ImpactSpeed = Impact;
			if (!bBall)
			{
				Request.Shape = (bPad && HalfLength > 0.f) ? ESandContactShape::Box : ESandContactShape::Round;
				Request.Radius = bPad ? HalfWidth : HalfWidth * 1.5f;
			}
			if (Surface->ApplyInteraction(Request))
			{
				++ImpactStamps;
				LastImpactSpeed = Impact;
				bStaticDone = true; // the crater is the resting dent
			}
			if (bDebugLog)
			{
				UE_LOG(LogSandDeformation, Log, TEXT("ContactPoint %s: impact %.0f cm/s at %s"), *GetName(), Impact, *Pos.ToString());
			}
			return;
		}
		bStaticDone = false;
		if (bPad)
		{
			Static();
		}
		return;
	}

	if (!bPad)
	{
		if (Speed2D >= MinSpeedCm && FVector::Dist2D(Pos, LastStamp) >= MinMoveCm)
		{
			Request.Brush = ESandBrushType::Wheel;
			Request.PreviousPosition = LastStamp;
			Request.bHasPreviousPosition = true;
			Request.MovementDirection = Pos - LastStamp;
			if (Surface->ApplyInteraction(Request))
			{
				++WheelSegments;
				StampPath.Add(Pos);
				LastStamp = Pos;
			}
		}
		return;
	}
	if (Speed2D < MinSpeedCm)
	{
		if (!bStaticDone || FVector::Dist2D(Pos, LastStatic) >= MinStaticMoveCm)
		{
			Static();
		}
		return;
	}
	if (FVector::Dist2D(Pos, LastStamp) >= MinMoveCm)
	{
		// dragged: a trench the width of the pad
		Request.Brush = ESandBrushType::ObjectContact;
		Request.Shape = ESandContactShape::Segment;
		Request.PreviousPosition = LastStamp;
		Request.bHasPreviousPosition = true;
		Request.MovementDirection = Pos - LastStamp;
		Request.DepthOverrideCm = FMath::Min(Strength * Surface->DepthPerStrengthCm, StaticDepthLimitCm);
		if (Surface->ApplyInteraction(Request))
		{
			++DragStamps;
			LastStamp = Pos;
			bStaticDone = false;
		}
	}
}
