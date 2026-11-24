// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dune/DuneImpactManager.h"
#include "Components/DecalComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UE_HOLDITTOGETHER.h"

ADuneImpactManager::ADuneImpactManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false; // only while fragments exist
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Hole(TEXT("/Game/DUNE/Materials/M_DUNE_BulletHole.M_DUNE_BulletHole"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Basic(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	HoleMaterial = Hole.Object;
	FragmentMesh = Cube.Object;
	FragmentBase = Basic.Object;
}

ADuneImpactManager* ADuneImpactManager::Get(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ADuneImpactManager> It(World); It; ++It)
	{
		return *It;
	}
	FActorSpawnParameters Params;
	Params.Name = TEXT("DuneImpactManager");
	return World->SpawnActor<ADuneImpactManager>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
}

UDecalComponent* ADuneImpactManager::AddBulletHole(UPrimitiveComponent* Surface, const FVector& Location, const FVector& Normal)
{
	if (!HoleMaterial || !Surface)
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Impact: no bullet hole made (%s)"), !HoleMaterial ? TEXT("material /Game/DUNE/Materials/M_DUNE_BulletHole is missing") : TEXT("no surface component"));
		return nullptr;
	}
	const FVector N = Normal.GetSafeNormal().IsNearlyZero() ? FVector::UpVector : Normal.GetSafeNormal();

	// the decal projects along its X axis, into the surface; the roll turns the hole so repeated holes do not all look the same
	FRotator Rotation = FRotationMatrix::MakeFromX(-N).Rotator();
	Rotation.Roll = FMath::Fmod(static_cast<float>(TotalHoles) * 137.5f, 360.f); // golden angle: a different turn for each hole, same every run

	// attached to the surface component: the hole moves with a piece that falls, and goes when the piece goes
	UDecalComponent* Decal = UGameplayStatics::SpawnDecalAttached(HoleMaterial, HoleHalfSize, Surface, NAME_None, Location, Rotation, EAttachLocation::KeepWorldPosition, 0.f);
	if (!Decal)
	{
		return nullptr;
	}
	Decal->SetFadeScreenSize(0.f); // small, but must not fade out with distance
	++TotalHoles;
	Holes.Add(Decal);
	while (Holes.Num() > MaxBulletHoles)
	{
		if (UDecalComponent* Oldest = Holes[0].Get())
		{
			Oldest->DestroyComponent(); // the oldest hole goes first
		}
		Holes.RemoveAt(0);
	}
	return Decal;
}

void ADuneImpactManager::SpawnFragments(const FVector& Location, const FVector& Normal, const FVector& BulletDirection, const FLinearColor& Color)
{
	UWorld* World = GetWorld();
	if (!World || !FragmentMesh || !FragmentBase || FragmentsPerHit <= 0)
	{
		return;
	}
	const FVector N = Normal.GetSafeNormal().IsNearlyZero() ? FVector::UpVector : Normal.GetSafeNormal();
	// the bullet's direction along the surface: chips are thrown sideways the way the bullet was travelling across it
	FVector Along = BulletDirection - N * FVector::DotProduct(BulletDirection, N);
	Along = Along.GetSafeNormal();
	FVector Tangent, Bitangent;
	N.FindBestAxisVectors(Tangent, Bitangent);

	for (int32 i = 0; i < FragmentsPerHit; ++i)
	{
		const float Angle = (2.f * PI * i) / FragmentsPerHit + 0.7f * TotalFragments; // fanned out, a different start each hit, same every run
		const FVector Fan = Tangent * FMath::Cos(Angle) + Bitangent * FMath::Sin(Angle);
		const FVector Direction = (N * 0.7f + Along * 0.5f + Fan * 0.5f).GetSafeNormal();
		const float Speed = 160.f + 40.f * i;
		const float Size = 2.5f + 0.6f * i; // cm

		UStaticMeshComponent* Chip = NewObject<UStaticMeshComponent>(this);
		Chip->SetStaticMesh(FragmentMesh);
		Chip->SetCollisionProfileName(TEXT("PhysicsActor"));
		Chip->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore); // a chip never pushes the player
		Chip->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Chip->SetCastShadow(false);
		Chip->SetWorldLocationAndRotation(Location + N * (Size * 0.6f), FRotator(Angle * 57.3f, Angle * 31.f, Angle * 17.f));
		Chip->SetWorldScale3D(FVector(Size / 100.f));
		Chip->RegisterComponent();
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(FragmentBase, Chip);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Chip->SetMaterial(0, Material);
		Chip->SetSimulatePhysics(true);
		Chip->SetUseCCD(true);
		Chip->SetMassOverrideInKg(NAME_None, 0.1f + 0.05f * i, true);
		Chip->SetLinearDamping(0.3f);
		Chip->SetAngularDamping(0.5f);
		Chip->SetPhysicsLinearVelocity(Direction * Speed);

		FFragment Entry;
		Entry.Component = Chip;
		Entry.Born = World->GetTimeSeconds();
		Fragments.Add(Entry);
		++TotalFragments;
	}
	while (Fragments.Num() > MaxFragments)
	{
		if (UStaticMeshComponent* Oldest = Fragments[0].Component.Get())
		{
			Oldest->DestroyComponent(); // over the cap: the oldest chip goes first
		}
		Fragments.RemoveAt(0);
	}
	SetActorTickEnabled(true);
}

void ADuneImpactManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Now = GetWorld()->GetTimeSeconds();
	for (int32 i = Fragments.Num() - 1; i >= 0; --i)
	{
		UStaticMeshComponent* Chip = Fragments[i].Component.Get();
		if (!Chip)
		{
			Fragments.RemoveAt(i);
		}
		else if (Now - Fragments[i].Born > FragmentLifetime)
		{
			Chip->DestroyComponent(); // gone after its lifetime, so settled chips cost nothing
			Fragments.RemoveAt(i);
		}
	}
	if (Fragments.Num() == 0)
	{
		SetActorTickEnabled(false);
	}
}
