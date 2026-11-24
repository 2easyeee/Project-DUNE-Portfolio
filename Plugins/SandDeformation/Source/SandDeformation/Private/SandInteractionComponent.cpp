// Copyright Epic Games, Inc. All Rights Reserved.

#include "SandInteractionComponent.h"
#include "SandSimulationSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	const TCHAR* const KindWalk = TEXT("walk");
	const TCHAR* const KindRun = TEXT("run");
	const TCHAR* const KindLanding = TEXT("landing");
}

USandInteractionComponent::USandInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void USandInteractionComponent::SetTrackedActor(AActor* Actor)
{
	TrackedActor = Actor;
}

ACharacter* USandInteractionComponent::GetCharacter() const
{
	return Cast<ACharacter>(TrackedActor.IsValid() ? TrackedActor.Get() : GetOwner());
}

USandSurfaceComponent* USandInteractionComponent::FindSurface(const FVector& World, float MarginCm) const
{
	const UWorld* W = GetWorld();
	const USandSimulationSubsystem* Subsystem = W ? W->GetSubsystem<USandSimulationSubsystem>() : nullptr;
	return Subsystem ? Subsystem->FindSurfaceAt(World, MarginCm) : nullptr;
}

bool USandInteractionComponent::IsOnSand() const
{
	const ACharacter* Character = GetCharacter();
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement || !Movement->IsMovingOnGround() || !Movement->CurrentFloor.bBlockingHit)
	{
		return false;
	}
	const USandSurfaceComponent* Surface = FindSurface(Character->GetActorLocation(), 0.f);
	return Surface && Surface->IsSandFloor(Movement->CurrentFloor.HitResult.GetActor());
}

bool USandInteractionComponent::RequestDeformation(FVector WorldPosition, float Radius, float Strength, ESandBrush Brush, float DirectionYawDeg)
{
	USandSurfaceComponent* Surface = FindSurface(WorldPosition, 0.f);
	return Surface && Surface->ApplyDeformation(WorldPosition, Radius, Strength, Brush, DirectionYawDeg);
}

// moves the mesh by a world distance, as a delta on its relative location (whatever else moves it, animation or crouch, keeps working); relative to the parent, so a scaled character (the golem) is lifted by the same world distance
static void LiftMesh(USceneComponent* Mesh, float WorldCm)
{
	const USceneComponent* Parent = Mesh->GetAttachParent();
	const float ParentScale = Parent ? FMath::Max(static_cast<float>(Parent->GetComponentScale().Z), 0.01f) : 1.f;
	Mesh->AddRelativeLocation(FVector(0.0, 0.0, WorldCm / ParentScale));
}

void USandInteractionComponent::AlignMesh(ACharacter* Character, float DeltaTime)
{
	USceneComponent* Mesh = Character->GetMesh();
	if (!Mesh)
	{
		return;
	}
	if (AlignedMesh.Get() != Mesh)
	{
		AlignedMesh = Mesh;
		AppliedMeshLiftCm = 0.f;
	}
	const USandSurfaceComponent* Surface = bAlignMeshToSurface ? FindSurface(Character->GetActorLocation(), 0.f) : nullptr;
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	// only while standing on the sand: in the air or on another floor (a house, a rock) the mesh goes back
	const bool bOnSand = Surface && Movement && Movement->IsMovingOnGround() && Surface->IsSandFloor(Movement->CurrentFloor.HitResult.GetActor());
	const float Target = bOnSand ? Surface->GetRenderedLiftAt(Character->GetActorLocation()) : 0.f;
	const float Next = FMath::FInterpTo(AppliedMeshLiftCm, Target, DeltaTime, MeshAlignSpeed);
	if (!FMath::IsNearlyEqual(Next, AppliedMeshLiftCm, 0.001f))
	{
		LiftMesh(Mesh, Next - AppliedMeshLiftCm);
		AppliedMeshLiftCm = Next;
	}
}

void USandInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (USceneComponent* Mesh = AlignedMesh.Get())
	{
		LiftMesh(Mesh, -AppliedMeshLiftCm);
		AppliedMeshLiftCm = 0.f;
	}
	if (ACharacter* Old = Cast<ACharacter>(BoundCharacter.Get()))
	{
		Old->LandedDelegate.RemoveDynamic(this, &USandInteractionComponent::OnCharacterLanded);
	}
	BoundCharacter.Reset();
	Super::EndPlay(EndPlayReason);
}

void USandInteractionComponent::RecordKind(const TCHAR* Kind, float Depth, float Width, float Speed)
{
	++StampCount;
	WalkStamps += Kind == KindWalk ? 1 : 0;
	RunStamps += Kind == KindRun ? 1 : 0;
	LandingStamps += Kind == KindLanding ? 1 : 0;
	LastKind = Kind;
	LastDepth = Depth;
	LastWidth = Width;
	LastSpeed = Speed;
}

void USandInteractionComponent::OnCharacterLanded(const FHitResult& Hit)
{
	// one event per touchdown; FallSpeed is the speed before it (the velocity is already zero here)
	const float Speed = FallSpeed;
	FallSpeed = 0.f;
	const AActor* Character = BoundCharacter.Get();
	if (!Character || Speed < LandingMinSpeed)
	{
		++LandingsIgnored;
		return;
	}
	const USandSurfaceComponent* Surface = FindSurface(Character->GetActorLocation(), LandingMinWidth);
	if (!Surface)
	{
		++SkippedOutside;
		return;
	}
	if (!Surface->IsSandFloor(Hit.GetActor()))
	{
		++SkippedNotSand; // a rock, a house or a floor: no dent
		return;
	}
	bPendingLand = true;
	PendingLandSpeed = Speed;
	PendingLandPos = Character->GetActorLocation();
}

void USandInteractionComponent::PlantStamp(USandSurfaceComponent* Surface, const FVector2D& Centre, const FVector2D& Dir, float HalfLen, float Speed, float Pressure, const FVector& FloorNormal)
{
	const float Run = FMath::Clamp((Speed - 150.f) / FMath::Max(FullTrailSpeed - 150.f, 1.f), 0.f, 1.f);
	const float SlopePressure = Pressure * FMath::Clamp(FloorNormal.Z, 0.5f, 1.f);
	FSandInteractionRequest R;
	R.Brush = ESandBrushType::Footprint;
	R.WorldPosition = FVector(Centre.X, Centre.Y, 0.f);
	R.SurfaceNormal = FloorNormal;
	R.MovementDirection = FVector(Dir.X, Dir.Y, 0.f);
	R.Extent = FVector2D(HalfLen, 0.f);
	R.Radius = FootHalfWidthCm * FMath::Sqrt(SlopePressure);
	R.DepthOverrideCm = FMath::Min(FMath::Lerp(FootDepthWalk, FootDepthRun, Run) * SlopePressure, Surface->GetDepthLimit());
	R.Push = FMath::Lerp(FootPushWalk, FootPushRun, Run);
	R.SourceActor = GetOwner();
	if (Surface->ApplyInteraction(R))
	{
		++FootStampCount;
		if (FootStampPath.Num() >= 256)
		{
			FootStampPath.RemoveAt(0);
		}
		FootStampPath.Add(FVector(Centre.X, Centre.Y, 0.f));
		RecordKind(Speed >= RunSpeedThreshold ? KindRun : KindWalk, R.DepthOverrideCm, R.Radius, Speed);
	}
}

bool USandInteractionComponent::UpdateFeet(ACharacter* Character, USandSurfaceComponent* Surface, float Speed, float Pressure)
{
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (!bFootStamps || !Mesh || FootBones.Num() < 2 || BallBones.Num() < 2)
	{
		return false;
	}
	for (int32 i = 0; i < 2; ++i)
	{
		if (Mesh->GetBoneIndex(FootBones[i]) == INDEX_NONE || Mesh->GetBoneIndex(BallBones[i]) == INDEX_NONE)
		{
			return false;
		}
	}
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const FHitResult& Floor = Movement->CurrentFloor.HitResult;
	const FVector Normal = Floor.ImpactNormal;
	const FVector Point = Floor.ImpactPoint;
	for (int32 i = 0; i < 2; ++i)
	{
		const FVector Foot = Mesh->GetBoneLocation(FootBones[i], EBoneSpaces::WorldSpace);
		const FVector Ball = Mesh->GetBoneLocation(BallBones[i], EBoneSpaces::WorldSpace);
		// height of the ball bone above the floor plane under the foot
		const float GroundZ = Point.Z - (Normal.X * (Ball.X - Point.X) + Normal.Y * (Ball.Y - Point.Y)) / FMath::Max(Normal.Z, 0.3f);
		const float Height = Ball.Z - AppliedMeshLiftCm - GroundZ; // the bone is as high as the visual alignment lifted the mesh
		BallMin[i] = FMath::Min(BallMin[i], Height);
		BallMax[i] = FMath::Max(BallMax[i], Height);
		if (bPlanted[i])
		{
			bPlanted[i] = Height < PlantExitCm;
			continue;
		}
		if (Height >= PlantEnterCm)
		{
			continue;
		}
		bPlanted[i] = true;
		if (Speed < MinMovementSpeed)
		{
			continue; // standing still: no print for a foot that is only shifted
		}
		FVector2D Dir(Ball.X - Foot.X, Ball.Y - Foot.Y);
		const float BoneLen = Dir.Size();
		Dir = BoneLen > 1.f ? Dir / BoneLen : FVector2D(Movement->Velocity).GetSafeNormal();
		const FVector2D Centre = FVector2D(Foot.X + Ball.X, Foot.Y + Ball.Y) * 0.5f + Dir * 1.5f;
		const float HalfLen = FMath::Clamp(BoneLen * 0.5f + 7.5f * FootSizeScale, FootMinHalfLenCm, FootMaxHalfLenCm);
		PlantStamp(Surface, Centre, Dir, HalfLen, Speed, Pressure, Normal);
		if (bDebugLog)
		{
			UE_LOG(LogSandDeformation, Log, TEXT("Interaction: foot %s planted at %s, ball %.1f cm above the floor, speed %.0f, depth %.2f"), i == 0 ? TEXT("L") : TEXT("R"), *Centre.ToString(), Height, Speed, LastDepth);
		}
	}
	return true;
}

void USandInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ACharacter* Character = bAutomaticCharacterMarks ? GetCharacter() : nullptr;
	if (!Character)
	{
		return;
	}
	if (BoundCharacter.Get() != Character)
	{
		if (ACharacter* Old = Cast<ACharacter>(BoundCharacter.Get()))
		{
			Old->LandedDelegate.RemoveDynamic(this, &USandInteractionComponent::OnCharacterLanded);
		}
		Character->LandedDelegate.AddDynamic(this, &USandInteractionComponent::OnCharacterLanded);
		BoundCharacter = Character;
		FallSpeed = 0.f;
	}
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!Movement)
	{
		return;
	}
	AlignMesh(Character, DeltaTime);
	const FVector Position = Character->GetActorLocation();
	const float Pressure =FMath::Clamp(WeightKg / FMath::Max(ReferenceWeightKg, 1.f), 0.25f, MaxPressureFactor) * SandSoftness;

	if (bPendingLand)
	{
		bPendingLand = false;
		if (USandSurfaceComponent* Surface = FindSurface(PendingLandPos, LandingMinWidth))
		{
			const float A = FMath::Clamp((PendingLandSpeed - LandingMinSpeed) / FMath::Max(LandingMaxSpeed - LandingMinSpeed, 1.f), 0.f, 1.f);
			FSandInteractionRequest R;
			R.Brush = ESandBrushType::Impact;
			R.Shape = ESandContactShape::Round;
			R.bAccumulate = true; // landing again on the same spot digs deeper
			R.WorldPosition = PendingLandPos;
			R.ImpactSpeed = PendingLandSpeed;
			R.DepthOverrideCm = FMath::Min(FMath::Lerp(LandingMinDepth, LandingMaxDepth, FMath::Pow(A, 0.6f)) * Pressure, Surface->GetDepthLimit());
			R.Radius = FMath::Lerp(LandingMinWidth, LandingMaxWidth, A) * FMath::Sqrt(Pressure);
			R.SourceActor = GetOwner();
			if (Surface->ApplyInteraction(R))
			{
				RecordKind(KindLanding, R.DepthOverrideCm, R.Radius, PendingLandSpeed);
				if (bDebugLog)
				{
					UE_LOG(LogSandDeformation, Log, TEXT("Interaction: landing at %s fall speed %.0f cm/s -> depth %.2f cm, half width %.0f cm"), *PendingLandPos.ToString(), PendingLandSpeed, R.DepthOverrideCm, R.Radius);
				}
			}
		}
		bHasLast = false;
		return;
	}
	if (!Movement->IsMovingOnGround())
	{
		FallSpeed = FMath::Max(FallSpeed, -Movement->Velocity.Z);
		++SkippedAir;
		bHasLast = false; // a jump breaks the trail
		bPlanted[0] = bPlanted[1] = false;
		return;
	}
	USandSurfaceComponent* Surface = FindSurface(Position, 0.f);
	if (!Surface || !Movement->CurrentFloor.bBlockingHit || !Surface->IsSandFloor(Movement->CurrentFloor.HitResult.GetActor()))
	{
		++(Surface ? SkippedNotSand : SkippedOutside);
		bHasLast = false;
		return;
	}
	if (!Surface->IsInsidePatch(Position, FootHalfWidthCm * 3.f))
	{
		++SkippedOutside;
		bHasLast = false;
		return;
	}
	const float Speed = Movement->Velocity.Size2D();
	if (UpdateFeet(Character, Surface, Speed, Pressure))
	{
		SkippedStill += Speed < MinMovementSpeed ? 1 : 0;
		return; // footprints from the foot bones
	}
	// no foot bones: a print every FallbackStrideCm, alternating left and right of the path
	if (Speed < MinMovementSpeed)
	{
		++SkippedStill;
		return;
	}
	if (!bHasLast)
	{
		LastPoint = Position;
		bHasLast = true;
		return;
	}
	if (FVector::Dist2D(Position, LastPoint) >= FallbackStrideCm)
	{
		const FVector2D Dir = FVector2D(Position.X - LastPoint.X, Position.Y - LastPoint.Y).GetSafeNormal();
		bFallbackLeft = !bFallbackLeft;
		const FVector2D Centre = FVector2D(Position) + FVector2D(-Dir.Y, Dir.X) * (bFallbackLeft ? 9.f : -9.f);
		PlantStamp(Surface, Centre, Dir, 13.f, Speed, Pressure, Movement->CurrentFloor.HitResult.ImpactNormal);
		LastPoint = Position;
	}
}
