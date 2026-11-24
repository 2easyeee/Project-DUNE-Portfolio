#include "Dune/Combat/DuneSandShot.h"
#include "Dune/Combat/DunePlayerCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

ADuneSandShot::ADuneSandShot()
{
	PrimaryActorTick.bCanEverTick = true;
	Ball = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ball"));
	SetRootComponent(Ball);
	Ball->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ball->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded()) Ball->SetStaticMesh(Sphere.Object);
	Ball->SetWorldScale3D(FVector(0.55f));
	InitialLifeSpan = 6.f;
}

void ADuneSandShot::Launch(const FVector& Origin, const FVector& Target, float Speed, float InDamage, AActor* InThrower)
{
	SetActorLocation(Origin);
	Damage = InDamage;
	Thrower = InThrower;
	const FVector D = Target - Origin;
	const float T = FMath::Max(0.3f, D.Size() / FMath::Max(Speed, 100.f));
	Velocity = D / T + FVector(0.f, 0.f, 0.5f * GravityCm * T); // reaches Target after T under gravity
	if (UMaterialInterface* M = Ball->GetMaterial(0))
	{
		if (UMaterialInstanceDynamic* MID = Ball->CreateDynamicMaterialInstance(0, M)) MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.62f, 0.42f, 0.2f));
	}
}

void ADuneSandShot::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	Velocity.Z -= GravityCm * DeltaSeconds;
	const FVector From = GetActorLocation();
	const FVector To = From + Velocity * DeltaSeconds;

	if (APawn* P = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		ADunePlayerCharacter* Player = Cast<ADunePlayerCharacter>(P);
		const FVector C = P->GetActorLocation();
		// distance from the player's centre to the step segment
		if (!bHitPlayer && Player && !Player->IsDead() && FMath::PointDistToSegment(C, From, To) < 95.f)
		{
			bHitPlayer = true;
			UGameplayStatics::ApplyDamage(Player, Damage, nullptr, Thrower.Get(), UDamageType::StaticClass());
			Destroy();
			return;
		}
	}
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(SandShot), false, this);
	if (Thrower.IsValid()) Q.AddIgnoredActor(Thrower.Get());
	if (GetWorld()->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Q)) { Destroy(); return; }
	SetActorLocation(To);
}
