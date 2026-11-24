// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dune/KrisKnifeComponent.h"
#include "Dune/SliceableStructure.h"
#include "Dune/DuneWeaponComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UE_HOLDITTOGETHER.h"

UKrisKnifeComponent::UKrisKnifeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UKrisKnifeComponent::BeginPlay()
{
	Super::BeginPlay();
	BuildKnife();
}

void UKrisKnifeComponent::BuildKnife()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || !Character->GetMesh())
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Knife: owner is not a character with a mesh, no knife built"));
		return;
	}
	USkeletalMeshComponent* Body = Character->GetMesh();
	bHandBone = Body->GetBoneIndex(HandBone) != INDEX_NONE;

	// the knife is only attached to the hand for its position; its rotation is set in world space every tick
	KnifeRoot = NewObject<USceneComponent>(Character, TEXT("KnifeRoot"));
	KnifeRoot->SetupAttachment(Body, bHandBone ? HandBone : NAME_None);
	KnifeRoot->RegisterComponent();
	KnifeRoot->SetRelativeLocation(FVector::ZeroVector);

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!Cube || !Base)
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Knife: basic shapes not found, no knife built"));
		return;
	}

	// silhouette from basic shapes (cm, along the local X axis, 0 = hand): handle, guard, wavy three-piece blade
	auto AddPart = [&](const FVector& Center, const FVector& SizeCm, const FRotator& Rot, const FLinearColor& Color)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Character);
		Part->SetStaticMesh(Cube);
		Part->SetupAttachment(KnifeRoot);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		Part->RegisterComponent();
		Part->SetRelativeLocationAndRotation(Center, Rot);
		Part->SetRelativeScale3D(SizeCm / 100.f);
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, Part);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Part->SetMaterial(0, Material);
		KnifeParts.Add(Part);
	};
	AddPart(FVector(-6.f, 0.f, 0.f), FVector(12.f, 2.5f, 2.5f), FRotator::ZeroRotator, FLinearColor(0.12f, 0.06f, 0.03f));
	AddPart(FVector(0.5f, 0.f, 0.f), FVector(1.5f, 8.f, 2.f), FRotator::ZeroRotator, FLinearColor(0.8f, 0.6f, 0.1f));
	const float Widths[3] = { 4.f, 3.5f, 2.5f };
	for (int32 i = 0; i < 3; ++i)
	{
		AddPart(FVector(6.f + 10.f * i, (i % 2 == 0) ? -0.7f : 0.7f, 0.f), FVector(10.f, Widths[i], 0.8f),
			FRotator(0.f, (i % 2 == 0) ? 8.f : -8.f, 0.f), FLinearColor(0.85f, 0.87f, 0.9f));
	}
	PartCount = KnifeParts.Num();
	// the parts are built when the component begins play, possibly after the weapon was chosen: show only if the knife is the weapon in hand
	if (const UDuneWeaponComponent* Weapons = Character->FindComponentByClass<UDuneWeaponComponent>())
	{
		SetKnifeVisible(Weapons->GetWeapon() == EDuneWeapon::Knife);
	}
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Knife: built from %d basic-shape parts, hand bone '%s' %s"), PartCount, *HandBone.ToString(), bHandBone ? TEXT("found") : TEXT("NOT FOUND (attached to the mesh origin)"));
}

void UKrisKnifeComponent::SetupInput(APlayerController* PC)
{
	// the attack input (left mouse button) belongs to UDuneWeaponComponent now, which calls Attack() while the knife is selected
	Controller = PC;
}

void UKrisKnifeComponent::SetKnifeVisible(bool bVisible)
{
	for (UPrimitiveComponent* Part : KnifeParts)
	{
		Part->SetVisibility(bVisible);
	}
}

void UKrisKnifeComponent::CancelSwing()
{
	if (bSwinging)
	{
		// a swing that is still running when the weapon is put away must not cut afterwards
		bSwinging = false;
		bResolved = true;
		bWindowOpen = false;
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Knife: swing cancelled (weapon switched)"));
	}
}

void UKrisKnifeComponent::Attack()
{
	if (bSwinging)
	{
		++IgnoredInputs;
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Knife: attack input ignored (swing running, %.2f s)"), SwingTime);
		return;
	}
	const APlayerController* PC = Controller.Get();
	if (!PC)
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Knife: attack input without a player controller"));
		return;
	}

	// the slash direction is fixed at the start of the swing from the recent mouse movement
	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FRotationMatrix ViewAxes(ViewRotation);
	SwingForward = ViewAxes.GetUnitAxis(EAxis::X);
	FVector2D Slash(RecentYaw, RecentPitch); // x = right, y = up, in the camera's view
	Slash = Slash.Size() < MinSlashDegrees ? FVector2D(1.f, 0.f) : Slash.GetSafeNormal();
	SwingSlash = ViewAxes.GetUnitAxis(EAxis::Y) * Slash.X + ViewAxes.GetUnitAxis(EAxis::Z) * Slash.Y;

	bSwinging = true;
	bLoggedNonTarget = false;
	bResolved = false;
	bWindowOpen = false;
	SwingTime = 0.f;
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Knife: attack input accepted, swing starts. slash direction %s (camera space %.2f, %.2f), aim %s"),
		*SwingSlash.ToString(), Slash.X, Slash.Y, *SwingForward.ToString());
}

void UKrisKnifeComponent::UpdateSlash(float DeltaTime)
{
	const APlayerController* PC = Controller.Get();
	if (!PC)
	{
		return;
	}
	const FRotator Rotation = PC->GetControlRotation();
	if (bHavePrevRotation)
	{
		const float Decay = FMath::Exp(-DeltaTime / FMath::Max(SlashMemory, 0.01f));
		RecentYaw = RecentYaw * Decay + FRotator::NormalizeAxis(Rotation.Yaw - PrevRotation.Yaw);
		RecentPitch = RecentPitch * Decay + FRotator::NormalizeAxis(Rotation.Pitch - PrevRotation.Pitch);
	}
	PrevRotation = Rotation;
	bHavePrevRotation = true;
}

void UKrisKnifeComponent::UpdateKnifePose()
{
	if (!KnifeRoot)
	{
		return;
	}
	// rest: blade held forward and up. swing: the tip sweeps along the slash direction in front of the player.
	FVector Dir = (SwingForward.IsNearlyZero() ? FVector::ForwardVector : SwingForward) * 0.8f + FVector::UpVector * 0.6f;
	if (bSwinging)
	{
		const float T = FMath::Clamp(SwingTime / SwingDuration, 0.f, 1.f);
		Dir = SwingForward * 0.8f + SwingSlash * (T * 2.f - 1.f) * 0.9f;
	}
	KnifeRoot->SetWorldRotation(FRotationMatrix::MakeFromX(Dir.GetSafeNormal()).Rotator());
}

void UKrisKnifeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateSlash(DeltaTime);

	if (bSwinging)
	{
		SwingTime += DeltaTime;
		const bool bInWindow = SwingTime >= HitWindowStart && SwingTime <= HitWindowEnd;
		if (bInWindow && !bWindowOpen)
		{
			bWindowOpen = true;
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Knife: hit window opens (%.2f s)"), SwingTime);
		}
		if (bInWindow && !bResolved)
		{
			bResolved = TryHit();
		}
		if (SwingTime > HitWindowEnd && bWindowOpen)
		{
			bWindowOpen = false;
			if (!bResolved)
			{
				bResolved = true;
				++Misses;
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Knife: hit window closed, nothing was cut"));
			}
			else
			{
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Knife: hit window closed"));
			}
		}
		if (SwingTime >= SwingDuration)
		{
			bSwinging = false;
		}
	}
	UpdateKnifePose();
}

bool UKrisKnifeComponent::TryHit()
{
	const APlayerController* PC = Controller.Get();
	const AActor* Owner = GetOwner();
	if (!PC || !Owner)
	{
		return true;
	}

	// aim: from the camera along the current view direction
	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Aim = ViewRotation.Vector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KrisKnife), false, Owner);
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, ViewLocation + Aim * 3000.f, ECC_Visibility, Params))
	{
		return false; // nothing in the line: keep looking until the window closes
	}

	// ground, walls and environment meshes are ignored (why is logged once per swing); the rule is shared with the gun
	FString NotTarget;
	const ECutTarget Kind = ASliceableStructure::ClassifyHit(Hit, NotTarget);
	if (Kind == ECutTarget::None)
	{
		if (!bLoggedNonTarget)
		{
			bLoggedNonTarget = true;
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Knife: aim hit %s / %s at %s: not a cut target (%s)"), *GetNameSafe(Hit.GetActor()),
				*GetNameSafe(Hit.GetComponent()), *Hit.ImpactPoint.ToString(), *NotTarget);
		}
		return false;
	}
	const bool bSliceable = Kind == ECutTarget::Sliceable;
	ASliceableStructure* Structure = Cast<ASliceableStructure>(Hit.GetActor());
	UProceduralMeshComponent* Piece = Cast<UProceduralMeshComponent>(Hit.GetComponent());
	AStaticMeshActor* PlainMeshActor = Cast<AStaticMeshActor>(Hit.GetActor());

	const float Distance = FVector::Dist(Owner->GetActorLocation(), Hit.ImpactPoint);
	if (Distance > ReachCm)
	{
		++Misses;
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Knife: hit %s / %s at %s but out of reach (%.0f cm > %.0f cm), not cut"),
			*GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()), *Hit.ImpactPoint.ToString(), Distance, ReachCm);
		return true;
	}

	if (!bSliceable)
	{
		// first cut of a prepared mesh: it is replaced in place by a sliceable structure with the same mesh and transform
		Structure = ASliceableStructure::ConvertStaticMeshActor(PlainMeshActor, ConvertedSeparation);
		Piece = Structure ? Structure->GetMesh() : nullptr;
		if (!Piece)
		{
			++Misses;
			UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Knife: could not make %s sliceable"), *GetNameSafe(PlainMeshActor));
			return true;
		}
	}

	// plane through the hit point (pushed into the mesh) containing the slash and the aim: normal = slash x aim
	const FVector Position = Hit.ImpactPoint + Aim * Penetration;
	const FVector Normal = FVector::CrossProduct(SwingSlash, Aim).GetSafeNormal();
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Knife: hit %s / %s at %s (%.0f cm), aim %s, slash %s -> plane normal %s; calling slice"),
		*Structure->GetName(), *Piece->GetName(), *Hit.ImpactPoint.ToString(), Distance, *Aim.ToString(), *SwingSlash.ToString(), *Normal.ToString());
	if (DuneWeaponDebug())
	{
		DrawDebugDirectionalArrow(GetWorld(), Hit.ImpactPoint - SwingSlash * 60.f, Hit.ImpactPoint + SwingSlash * 60.f, 30.f, FColor::Cyan, false, 3.f, 0, 2.f);
		DrawDebugDirectionalArrow(GetWorld(), Hit.ImpactPoint, Hit.ImpactPoint + Normal * 80.f, 30.f, FColor::Red, false, 3.f, 0, 2.f);
	}

	LastNormal = Normal;
	// the freed piece is nudged along the attack direction: the way the blade travelled (it lies in the cut plane)
	if (Structure->SliceComponentWithPlane(Piece, Position, Normal, SwingSlash))
	{
		++Slices;
	}
	else
	{
		++Misses;
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Knife: slice refused: %s"), *Structure->GetLastReport().Reason);
	}
	return true;
}
