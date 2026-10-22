// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dune/DuneGunComponent.h"
#include "Dune/DuneWeaponComponent.h"
#include "Dune/DuneImpactManager.h"
#include "Components/DecalComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Dune/SliceableStructure.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UE_HOLDITTOGETHER.h"

namespace
{
	constexpr float MuzzleForwardCm = 33.f; // from the gun's origin (the hand) to the end of the barrel
}

UDuneGunComponent::UDuneGunComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UDuneGunComponent::BeginPlay()
{
	Super::BeginPlay();
	BuildGun();
}

void UDuneGunComponent::BuildGun()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || !Character->GetMesh())
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Gun: owner is not a character with a mesh, no gun built"));
		return;
	}
	USkeletalMeshComponent* Body = Character->GetMesh();
	const bool bHand = Body->GetBoneIndex(HandBone) != INDEX_NONE;

	// like the knife: attached to the hand for its position, turned in world space every tick (it points where the player looks)
	GunRoot = NewObject<USceneComponent>(Character, TEXT("GunRoot"));
	GunRoot->SetupAttachment(Body, bHand ? HandBone : NAME_None);
	GunRoot->RegisterComponent();
	GunRoot->SetRelativeLocation(FVector::ZeroVector);

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!Cube || !Cylinder || !Base)
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Gun: basic shapes not found, no gun built"));
		return;
	}

	// a pistol silhouette from basic shapes (cm, local X = barrel direction): slide, barrel, grip, sight, coloured stripe
	auto AddPart = [&](UStaticMesh* Mesh, const FVector& Center, const FVector& SizeCm, const FRotator& Rot, const FLinearColor& Color)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Character);
		Part->SetStaticMesh(Mesh);
		Part->SetupAttachment(GunRoot);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		Part->RegisterComponent();
		Part->SetRelativeLocationAndRotation(Center, Rot);
		Part->SetRelativeScale3D(SizeCm / 100.f);
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, Part);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Part->SetMaterial(0, Material);
		GunParts.Add(Part);
	};
	const FLinearColor Metal(0.05f, 0.06f, 0.08f);
	AddPart(Cube, FVector(14.f, 0.f, 1.5f), FVector(26.f, 3.2f, 5.f), FRotator::ZeroRotator, Metal);
	AddPart(Cylinder, FVector(29.f, 0.f, 2.f), FVector(2.2f, 2.2f, 8.f), FRotator(90.f, 0.f, 0.f), Metal); // cylinder axis is Z, turned along X
	AddPart(Cube, FVector(4.f, 0.f, -6.f), FVector(5.f, 3.f, 12.f), FRotator(0.f, 0.f, 0.f), FLinearColor(0.12f, 0.1f, 0.09f));
	AddPart(Cube, FVector(23.f, 0.f, 5.2f), FVector(2.5f, 1.f, 1.5f), FRotator::ZeroRotator, Metal);
	AddPart(Cube, FVector(14.f, 1.7f, 1.5f), FVector(20.f, 0.2f, 1.2f), FRotator::ZeroRotator, FLinearColor(0.1f, 0.75f, 0.95f)); // accent, tells it apart from the knife at a glance
	PartCount = GunParts.Num();
	if (const UDuneWeaponComponent* Weapons = Character->FindComponentByClass<UDuneWeaponComponent>())
	{
		SetGunVisible(Weapons->GetWeapon() == EDuneWeapon::Gun); // built at begin play, possibly after the weapon was chosen
	}
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Gun: built from %d basic-shape parts, hand bone '%s' %s"), PartCount, *HandBone.ToString(), bHand ? TEXT("found") : TEXT("NOT FOUND (attached to the mesh origin)"));
}

void UDuneGunComponent::SetGunVisible(bool bVisible)
{
	for (UPrimitiveComponent* Part : GunParts)
	{
		Part->SetVisibility(bVisible);
	}
}

void UDuneGunComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const APlayerController* PC = Controller.Get();
	if (GunRoot && PC)
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		GunRoot->SetWorldRotation(ViewRotation); // the barrel points where the player looks
	}
}

FVector UDuneGunComponent::MuzzleLocation() const
{
	if (GunRoot)
	{
		return GunRoot->GetComponentLocation() + GunRoot->GetForwardVector() * MuzzleForwardCm;
	}
	return GetOwner()->GetActorLocation();
}

bool UDuneGunComponent::Fire()
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = Controller.Get();
	const AActor* Owner = GetOwner();
	if (!World || !PC || !Owner)
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Gun: shot without a world, controller or owner"));
		return false;
	}
	if (World->GetTimeSeconds() - LastShotTime < FireCooldown)
	{
		++IgnoredInputs;
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Gun: fire input ignored (%.2f s since the last shot)"), World->GetTimeSeconds() - LastShotTime);
		return false;
	}
	LastShotTime = World->GetTimeSeconds();
	++Shots;

	// 1. aim: camera position through the screen centre
	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector ViewDir = ViewRotation.Vector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DuneGun), false, Owner);
	FHitResult AimHit;
	const bool bAimHit = World->LineTraceSingleByChannel(AimHit, ViewLocation, ViewLocation + ViewDir * RangeCm, ECC_Visibility, Params);
	const FVector AimPoint = bAimHit ? AimHit.ImpactPoint : ViewLocation + ViewDir * RangeCm;

	// 2. bullet: muzzle to the aim point. Whatever is in between is what the bullet hits (a pillar edge, a wall beside the player).
	const FVector Muzzle = MuzzleLocation();
	FHitResult Hit = AimHit;
	bool bHit = bAimHit;
	FHitResult MuzzleHit;
	if (bAimHit && World->LineTraceSingleByChannel(MuzzleHit, Muzzle, AimPoint + (AimPoint - Muzzle).GetSafeNormal() * 2.f, ECC_Visibility, Params))
	{
		if (MuzzleHit.bStartPenetrating)
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Gun: muzzle %s is inside %s, using the camera hit"), *Muzzle.ToString(), *GetNameSafe(MuzzleHit.GetActor()));
		}
		else
		{
			if (MuzzleHit.GetActor() != AimHit.GetActor() || MuzzleHit.GetComponent() != AimHit.GetComponent())
			{
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Gun: muzzle trace is blocked by %s before the aim point (%s)"), *GetNameSafe(MuzzleHit.GetActor()), *GetNameSafe(AimHit.GetActor()));
			}
			Hit = MuzzleHit;
		}
	}
	const FVector Bullet = ((bHit ? Hit.ImpactPoint : AimPoint) - Muzzle).GetSafeNormal();
	LastBullet = Bullet;
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Gun: shot %d. camera trace from %s dir %s, muzzle %s, bullet dir %s, hit %s"), Shots, *ViewLocation.ToString(),
		*ViewDir.ToString(), *Muzzle.ToString(), *Bullet.ToString(), bHit ? *FString::Printf(TEXT("%s / %s at %s (normal %s)"), *GetNameSafe(Hit.GetActor()),
			*GetNameSafe(Hit.GetComponent()), *Hit.ImpactPoint.ToString(), *Hit.ImpactNormal.ToString()) : TEXT("nothing"));
	if (DuneWeaponDebug())
	{
		DrawDebugLine(World, Muzzle, bHit ? Hit.ImpactPoint : AimPoint, FColor::Yellow, false, 0.4f, 0, 1.5f);
		if (bHit)
		{
			DrawDebugSphere(World, Hit.ImpactPoint, 6.f, 8, FColor::Red, false, 2.f);
		}
	}
	if (!bHit)
	{
		++Misses;
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Gun: miss, the bullet hit nothing"));
		return false;
	}

	// 3. what was hit. A gun does not cut: nothing below calls the slice code (ASliceableStructure::SliceComponentWithPlane),
	//    that is the knife's path. A bullet leaves a local mark on a structure and nothing else.
	FString NotTarget;
	const ECutTarget Kind = ASliceableStructure::ClassifyHit(Hit, NotTarget);
	if (Kind == ECutTarget::None)
	{
		++NonTargetHits;
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Gun: hit %s / %s at %s: not a structure, no impact effect (%s)"), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()),
			*Hit.ImpactPoint.ToString(), *NotTarget);
		return false;
	}
	return ApplyBulletImpact(Hit, Bullet);
}

bool UDuneGunComponent::ApplyBulletImpact(const FHitResult& Hit, const FVector& Bullet)
{
	// the effect goes exactly where the trace hit, along the surface normal that came with the hit
	const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
	if (Normal.IsNearlyZero() || !Hit.GetComponent() || Hit.ImpactPoint.ContainsNaN())
	{
		++Misses;
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Gun: hit information is not usable (normal %s, component %s), no impact effect"), *Hit.ImpactNormal.ToString(), *GetNameSafe(Hit.GetComponent()));
		return false;
	}

	// 1. a structure the level explicitly allows to be broken (a structure with bGunFracturable, or a static mesh actor carrying the
	//    DuneGunFracture tag whose mesh has Allow CPU Access): the bullet cracks it and it falls apart. Not decided by name or by mesh alone,
	//    so another actor with the same mesh is not affected. FractureAt is the gun's own path; the knife's slice function is not called.
	ASliceableStructure* Structure = Cast<ASliceableStructure>(Hit.GetActor());
	UProceduralMeshComponent* Piece = Cast<UProceduralMeshComponent>(Hit.GetComponent());
	if (!Structure)
	{
		AStaticMeshActor* Plain = Cast<AStaticMeshActor>(Hit.GetActor());
		if (Plain && Plain->ActorHasTag(ASliceableStructure::DuneGunFractureTag))
		{
			Structure = ASliceableStructure::ConvertStaticMeshActor(Plain, 0.f); // needs the mesh as a procedural mesh to be broken; checks Allow CPU Access
			Piece = Structure ? Structure->GetMesh() : nullptr;
		}
	}
	if (Structure && Structure->bGunFracturable && Piece)
	{
		const int32 Slices = ASliceableStructure::GetTotalSliceRequests();
		const int32 Freed = Structure->FractureAt(Piece, Hit.ImpactPoint, Normal, Bullet);
		++Impacted;
		++Fractures;
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Gun: bullet hit %s / %s at %s, normal %s: %d fragments set free. No bullet hole, knife slice not called (slice requests %d -> %d)"),
			*GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()), *Hit.ImpactPoint.ToString(), *Normal.ToString(), Freed, Slices, ASliceableStructure::GetTotalSliceRequests());
		if (DuneWeaponDebug())
		{
			DrawDebugSphere(GetWorld(), Hit.ImpactPoint, 6.f, 8, FColor::Red, false, 1.5f);
		}
		return Freed > 0;
	}

	// 2. every other structure: the bullet only knocks a few chips off it (as before), nothing else changes. No bullet hole decal.
	ADuneImpactManager* Impacts = ADuneImpactManager::Get(GetWorld());
	if (!Impacts)
	{
		return false;
	}
	FLinearColor Color(0.5f, 0.49f, 0.46f); // chips take the colour of what was hit
	if (Structure && !Structure->bUseSourceMaterials)
	{
		Color = Structure->Tint;
	}
	if (bLeaveBulletHoles)
	{
		Impacts->AddBulletHole(Hit.GetComponent(), Hit.ImpactPoint, Normal);
	}
	Impacts->SpawnFragments(Hit.ImpactPoint, Normal, Bullet, Color);
	++Impacted;
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Gun: bullet hit %s / %s at %s, normal %s: not a fracture target, %d chips only (bullet holes %s). Slice not called (slice requests so far: %d)"),
		*GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()), *Hit.ImpactPoint.ToString(), *Normal.ToString(), Impacts->FragmentsPerHit,
		bLeaveBulletHoles ? TEXT("on") : TEXT("off"), ASliceableStructure::GetTotalSliceRequests());
	return true;
}
