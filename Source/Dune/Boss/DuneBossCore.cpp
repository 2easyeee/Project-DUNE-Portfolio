#include "Dune/Boss/DuneBossCore.h"
#include "Dune/Boss/DuneBossProjectile.h"
#include "Dune/Combat/DuneChaserEnemy.h"
#include "Dune/Combat/DuneHealthComponent.h"
#include "Dune/Combat/DunePlayerCharacter.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SandDeformationLibrary.h"

namespace
{
	const FLinearColor GCoreColor1(20.f, 5.f, 0.6f, 1.f);   // hot orange (emissive values are > 1)
	const FLinearColor GCoreColor2(30.f, 0.6f, 0.4f, 1.f);  // phase 2: angry red
}

UMaterialInstanceDynamic* ADuneBossCore::MakeGlow(UObject* Outer, const FLinearColor& Color)
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Boss/M_Boss_Glow.M_Boss_Glow"));
	const bool bGlow = Base != nullptr;
	if (!Base) Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UMaterialInstanceDynamic* M = Base ? UMaterialInstanceDynamic::Create(Base, Outer) : nullptr;
	if (M) M->SetVectorParameterValue(bGlow ? TEXT("Glow") : TEXT("Color"), Color);
	return M;
}

ADuneBossCore::ADuneBossCore()
{
	PrimaryActorTick.bCanEverTick = true;

	Hitbox = CreateDefaultSubobject<USphereComponent>(TEXT("Hitbox"));
	Hitbox->InitSphereRadius(130.f);
	Hitbox->SetCollisionProfileName(TEXT("BlockAll"));
	RootComponent = Hitbox;

	CoreMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Core"));
	CoreMesh->SetupAttachment(Hitbox);
	CoreMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CoreMesh->SetCastShadow(false);
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Sphere) { CoreMesh->SetStaticMesh(Sphere); CoreMesh->SetRelativeScale3D(FVector(2.4f)); }

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Hitbox);
	Light->SetIntensity(120000.f);
	Light->SetAttenuationRadius(5200.f);
	Light->SetLightColor(FLinearColor(1.f, 0.35f, 0.08f));
	Light->SetCastShadows(false);

	RingA = CreateDefaultSubobject<USceneComponent>(TEXT("RingA"));
	RingA->SetupAttachment(Hitbox);
	RingA->SetRelativeRotation(FRotator(0.f, 0.f, 62.f));
	RingB = CreateDefaultSubobject<USceneComponent>(TEXT("RingB"));
	RingB->SetupAttachment(Hitbox);
	RingB->SetRelativeRotation(FRotator(0.f, 0.f, -28.f));
	if (Cube)
	{
		for (int32 r = 0; r < 2; ++r)
		{
			USceneComponent* Ring = r == 0 ? RingA.Get() : RingB.Get();
			const float Radius = r == 0 ? 330.f : 430.f;
			for (int32 i = 0; i < 16; ++i)
			{
				UStaticMeshComponent* P = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Ring%d_%d"), r, i));
				P->SetupAttachment(Ring);
				P->SetStaticMesh(Cube);
				const float A = FMath::DegreesToRadians(i * (360.f / 16));
				P->SetRelativeLocation(FVector(FMath::Cos(A) * Radius, FMath::Sin(A) * Radius, 0.f));
				P->SetRelativeRotation(FRotator(0.f, i * (360.f / 16) + 90.f, 0.f));
				P->SetRelativeScale3D(FVector(0.5f, 0.12f, 0.12f));
				P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				P->SetCastShadow(false);
				RingParts.Add(P);
			}
		}
	}
	Health = CreateDefaultSubobject<UDuneHealthComponent>(TEXT("Health"));
}

bool ADuneBossCore::IsDead() const { return Health && Health->IsDead(); }
float ADuneBossCore::GetHealthFraction() const { return Health && Health->MaxHealth > 0.f ? Health->Health / Health->MaxHealth : 0.f; }

void ADuneBossCore::BeginPlay()
{
	Super::BeginPlay();
	Health->Init(MaxHealth, MaxHealth);
	Health->OnDied.AddDynamic(this, &ADuneBossCore::OnDied);
	BaseLocation = GetActorLocation();
	GroundZ = BaseLocation.Z - GroundOffsetCm;
	CoreMat = MakeGlow(this, GCoreColor1);
	if (CoreMat)
	{
		CoreMesh->SetMaterial(0, CoreMat);
		for (UStaticMeshComponent* P : RingParts) P->SetMaterial(0, MakeGlow(this, FLinearColor(8.f, 2.f, 0.3f, 1.f)));
	}
}

void ADuneBossCore::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	const float Spin = Phase == 1 ? 1.f : 2.2f;
	RingA->AddLocalRotation(FRotator(0.f, 25.f * Spin * DeltaSeconds, 0.f));
	RingB->AddLocalRotation(FRotator(0.f, -38.f * Spin * DeltaSeconds, 0.f));
	SetActorLocation(BaseLocation + FVector(0.f, 0.f, BobCm * FMath::Sin(Age * 1.2f)));
	if (IsDead() || bDecorative) return;

	if (Phase == 1 && GetHealthFraction() <= Phase2Fraction) EnterPhase2();
	TickShockwave(DeltaSeconds);

	// a warning flicker before the wave
	if (bWarning)
	{
		Light->SetIntensity(120000.f * (1.4f + FMath::Sin(Age * 30.f)));
	}

	PatternTimer -= DeltaSeconds;
	if (PatternTimer <= 0.f && !bWaveActive && !bWarning)
	{
		ChoosePattern();
		PatternTimer = Phase == 1 ? PatternInterval1 : PatternInterval2;
	}
}

void ADuneBossCore::EnterPhase2()
{
	Phase = 2;
	if (CoreMat) CoreMat->SetVectorParameterValue(TEXT("Glow"), GCoreColor2);
	Light->SetLightColor(FLinearColor(1.f, 0.08f, 0.04f));
	PatternTimer = 1.5f;
	UE_LOG(LogTemp, Log, TEXT("Boss: phase 2"));
	OnPhase2Entered();
}

void ADuneBossCore::ChoosePattern()
{
	const float R = FMath::FRand();
	EPattern P;
	if (Phase == 1) P = R < 0.7f ? EPattern::Barrage : EPattern::Summon;
	else P = R < 0.4f ? EPattern::Barrage : (R < 0.55f ? EPattern::Summon : EPattern::Shockwave);
	OnPatternStarted();
	switch (P)
	{
	case EPattern::Barrage: DoBarrage(); break;
	case EPattern::Summon: DoSummon(); break;
	case EPattern::Shockwave: StartShockwave(); break;
	}
}

void ADuneBossCore::DoBarrage()
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player) return;
	++BarrageCount;
	const int32 Count = Phase == 1 ? 5 : 9;
	const FVector From = GetProjectileOrigin();
	const FVector Eye = Player->GetActorLocation() + FVector(0.f, 0.f, 20.f);
	const FRotator Aim = (Eye - From).Rotation();
	for (int32 i = 0; i < Count; ++i)
	{
		const float Off = (i - (Count - 1) * 0.5f) * 7.f; // degrees of spread across the fan
		const FRotator R(Aim.Pitch, Aim.Yaw + Off, 0.f);
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ADuneBossProjectile* Orb = GetWorld()->SpawnActor<ADuneBossProjectile>(ADuneBossProjectile::StaticClass(), From + R.Vector() * 180.f, R, P))
		{
			Orb->Launch(R.Vector(), this, Phase == 1 ? FLinearColor(10.f, 2.f, 0.3f, 1.f) : FLinearColor(14.f, 0.5f, 0.3f, 1.f));
			++OrbsFired;
		}
	}
}

void ADuneBossCore::DoSummon()
{
	int32 Alive = 0;
	for (TActorIterator<ADuneChaserEnemy> It(GetWorld()); It; ++It) { if (!It->IsDead()) ++Alive; }
	if (Alive >= MaxChasers) { DoBarrage(); return; } // nothing to summon: shoot instead
	++SummonCount;
	for (int32 i = 0; i < 2; ++i)
	{
		const float A = FMath::FRandRange(0.f, 2.f * PI);
		const FVector L(BaseLocation.X + FMath::Cos(A) * 1500.f, BaseLocation.Y + FMath::Sin(A) * 1500.f, GroundZ + 110.f);
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		GetWorld()->SpawnActor<ADuneChaserEnemy>(ADuneChaserEnemy::StaticClass(), L, FRotator::ZeroRotator, P);
	}
}

void ADuneBossCore::StartShockwave()
{
	if (bWarning || bWaveActive) return;
	bWarning = true;
	WarningTimer = 1.1f;
	++ShockwaveCount;
}

void ADuneBossCore::TickShockwave(float DeltaSeconds)
{
	const FVector Center(BaseLocation.X, BaseLocation.Y, GroundZ + 6.f);
	if (bWarning)
	{
		WarningTimer -= DeltaSeconds;
		DrawDebugCircle(GetWorld(), Center, 250.f + 200.f * FMath::Frac(Age * 2.f), 48, FColor(255, 60, 20), false, 0.03f, 0, 8.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
		if (WarningTimer <= 0.f)
		{
			bWarning = false;
			bWaveActive = true;
			WaveRadius = 150.f;
			bWaveHitPlayer = false;
			Light->SetIntensity(120000.f);
		}
		return;
	}
	if (!bWaveActive) return;

	WaveRadius += WaveSpeed * DeltaSeconds;
	DrawDebugCircle(GetWorld(), Center, WaveRadius, 96, FColor(255, 120, 30), false, 0.03f, 0, 14.f, FVector(1, 0, 0), FVector(0, 1, 0), false);

	// the wave hurts a player who is on the floor while it passes; jumping over it is safe
	if (ACharacter* Player = Cast<ACharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		const float D = FVector::Dist2D(Player->GetActorLocation(), BaseLocation);
		if (!bWaveHitPlayer && FMath::Abs(D - WaveRadius) < 90.f && Player->GetCharacterMovement()->IsMovingOnGround())
		{
			bWaveHitPlayer = true;
			++WaveHits;
			UGameplayStatics::ApplyDamage(Player, WaveDamage, nullptr, this, UDamageType::StaticClass());
		}
	}

	// dents in the sand along the ring
	WaveSandTimer -= DeltaSeconds;
	if (WaveSandTimer <= 0.f)
	{
		WaveSandTimer = 0.05f;
		// one continuous furrow (track segments, no gaps) over a metre wide, and a few thrown-up lumps beside it: the wave ploughs the arena
		const int32 N = FMath::Clamp(FMath::RoundToInt(WaveRadius * 0.016f), 12, 64);
		auto OnRing = [&](float A, float R) { return FVector(BaseLocation.X + FMath::Cos(A) * R, BaseLocation.Y + FMath::Sin(A) * R, GroundZ); };
		for (int32 i = 0; i < N; ++i)
		{
			const float A0 = 2.f * PI * i / N;
			const float A1 = 2.f * PI * (i + 1) / N;
			FSandInteractionRequest Furrow; // a trench about a metre wide and deep with a raised rim on both sides
			Furrow.Brush = ESandBrushType::Wheel;
			Furrow.WorldPosition = OnRing(A1, WaveRadius);
			Furrow.PreviousPosition = OnRing(A0, WaveRadius);
			Furrow.bHasPreviousPosition = true;
			Furrow.MovementDirection = Furrow.WorldPosition - Furrow.PreviousPosition;
			Furrow.Radius = 55.f;
			Furrow.DepthOverrideCm = 16.f;
			Furrow.RidgeScale = 3.f;
			USandDeformationLibrary::ApplySandInteraction(this, Furrow);
			if (FMath::FRand() < 0.3f)
			{
				USandDeformationLibrary::ApplySandImpact(this, OnRing(A0 + FMath::FRandRange(0.f, A1 - A0), WaveRadius + FMath::FRandRange(-160.f, 160.f)), FMath::FRandRange(20.f, 45.f), 800.f, 1.f);
			}
		}
	}
	if (WaveRadius >= WaveMaxRadius) bWaveActive = false;
}

void ADuneBossCore::SetDecorative(bool bDead)
{
	bDecorative = true;
	if (bDead) OnDied();
}

void ADuneBossCore::OnDied()
{
	bWarning = false;
	bWaveActive = false;
	Light->SetIntensity(20000.f);
	if (CoreMat) CoreMat->SetVectorParameterValue(TEXT("Glow"), FLinearColor(0.2f, 0.02f, 0.02f, 1.f));
	UE_LOG(LogTemp, Log, TEXT("Boss: destroyed"));
	HandleDeath();
}
