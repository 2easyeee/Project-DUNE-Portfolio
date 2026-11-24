// Copyright Epic Games, Inc. All Rights Reserved.

#include "SandDeformationLibrary.h"
#include "SandSim.h"
#include "SandSimulationSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

USandSurfaceComponent* USandDeformationLibrary::FindSandSurface(const UObject* WorldContextObject, FVector WorldPosition)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const USandSimulationSubsystem* Subsystem = World ? World->GetSubsystem<USandSimulationSubsystem>() : nullptr;
	return Subsystem ? Subsystem->FindSurfaceAt(WorldPosition) : nullptr;
}

bool USandDeformationLibrary::ApplySandDeformation(const UObject* WorldContextObject, FVector WorldPosition, float Radius, float Strength, ESandBrush Brush, float DirectionYawDeg, USandSurfaceComponent* Surface)
{
	if (!Surface)
	{
		Surface = FindSandSurface(WorldContextObject, WorldPosition);
	}
	return Surface && Surface->ApplyDeformation(WorldPosition, Radius, Strength, Brush, DirectionYawDeg);
}

bool USandDeformationLibrary::ApplySandInteraction(const UObject* WorldContextObject, const FSandInteractionRequest& Request, USandSurfaceComponent* Surface)
{
	if (!Surface)
	{
		Surface = FindSandSurface(WorldContextObject, Request.WorldPosition);
	}
	return Surface && Surface->ApplyInteraction(Request);
}

bool USandDeformationLibrary::ApplySandImpact(const UObject* WorldContextObject, FVector WorldPosition, float Radius, float ImpactSpeed, float Strength, USandSurfaceComponent* Surface)
{
	FSandInteractionRequest Request;
	Request.Brush = ESandBrushType::Impact;
	Request.bAccumulate = true; // landing again on the same spot digs deeper
	Request.WorldPosition = WorldPosition;
	Request.Radius = Radius;
	Request.ImpactSpeed = ImpactSpeed;
	Request.Strength = Strength;
	return ApplySandInteraction(WorldContextObject, Request, Surface);
}

bool USandDeformationLibrary::ApplySandPressure(const UObject* WorldContextObject, FVector WorldPosition, float HalfLength, float HalfWidth, float YawDeg, float Strength, USandSurfaceComponent* Surface)
{
	FSandInteractionRequest Request;
	Request.Brush = ESandBrushType::ObjectContact;
	Request.WorldPosition = WorldPosition;
	Request.Radius = HalfWidth;
	Request.Extent = FVector2D(HalfLength, HalfWidth);
	Request.OrientationYawDeg = YawDeg;
	Request.Strength = Strength;
	return ApplySandInteraction(WorldContextObject, Request, Surface);
}

bool USandDeformationLibrary::ApplySandTrail(const UObject* WorldContextObject, FVector From, FVector To, float HalfWidth, float Strength, USandSurfaceComponent* Surface)
{
	FSandInteractionRequest Request;
	Request.Brush = ESandBrushType::Wheel;
	Request.WorldPosition = To;
	Request.PreviousPosition = From;
	Request.bHasPreviousPosition = true;
	Request.MovementDirection = To - From;
	Request.Radius = HalfWidth;
	Request.Strength = Strength;
	return ApplySandInteraction(WorldContextObject, Request, Surface);
}

bool USandDeformationLibrary::IsSandDeformationSupported()
{
	return SandSim::IsSupported();
}
