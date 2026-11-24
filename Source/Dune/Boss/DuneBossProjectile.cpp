#include "Dune/Boss/DuneBossProjectile.h"
#include "Dune/Boss/DuneBossCore.h"
#include "Dune/Combat/DunePlayerCharacter.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SandDeformationLibrary.h"

ADuneBossProjectile::ADuneBossProjectile()
{
	PrimaryActorTick.bCanEverTick = false;
	InitialLifeSpan = 7.f;

	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	Sphere->InitSphereRadius(26.f);
	Sphere->SetCollisionProfileName(TEXT("Projectile"));
	Sphere->SetNotifyRigidBodyCollision(true);
	Sphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore); // the player's shots pass through orbs (they fly along the line to the player)
	RootComponent = Sphere;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Sphere);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	if (UStaticMesh* S = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		Mesh->SetStaticMesh(S);
		Mesh->SetRelativeScale3D(FVector(0.5f));
	}

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Sphere);
	Light->SetIntensity(5000.f);
	Light->SetAttenuationRadius(700.f);
	Light->SetCastShadows(false);

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->ProjectileGravityScale = 0.f;
	Movement->bRotationFollowsVelocity = true;
	Movement->InitialSpeed = Speed;
	Movement->MaxSpeed = 2000.f;
}

void ADuneBossProjectile::Launch(const FVector& Dir, AActor* Boss, const FLinearColor& Color)
{
	SetOwner(Boss);
	if (Boss) Sphere->IgnoreActorWhenMoving(Boss, true);
	Light->SetLightColor(Color);
	if (UMaterialInstanceDynamic* M = ADuneBossCore::MakeGlow(this, Color)) Mesh->SetMaterial(0, M);
	Movement->Velocity = Dir.GetSafeNormal() * Speed;
	Sphere->OnComponentHit.AddDynamic(this, &ADuneBossProjectile::OnHit);
}

void ADuneBossProjectile::OnHit(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, FVector, const FHitResult& Hit)
{
	if (Other == GetOwner()) return;
	if (Other && Other->IsA<ADunePlayerCharacter>())
	{
		UGameplayStatics::ApplyDamage(Other, Damage, nullptr, this, UDamageType::StaticClass());
	}
	else if (Hit.ImpactNormal.Z > 0.5f)
	{
		// an orb that lands on the floor pushes a dent into the sand
		// (a wide crater and sand thrown out around it; the sand heals over time)
			USandDeformationLibrary::ApplySandImpact(this, Hit.ImpactPoint, 95.f, 1200.f, 1.6f);
			for (int32 i = 0; i < 7; ++i)
			{
				const FVector2D Off = FVector2D(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f)).GetSafeNormal() * FMath::FRandRange(130.f, 260.f);
				USandDeformationLibrary::ApplySandImpact(this, Hit.ImpactPoint + FVector(Off.X, Off.Y, 0.f), FMath::FRandRange(10.f, 24.f), 500.f, 0.9f);
			}
	}
	Destroy();
}
