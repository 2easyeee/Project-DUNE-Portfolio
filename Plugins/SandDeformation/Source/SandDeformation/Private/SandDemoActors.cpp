// Copyright Epic Games, Inc. All Rights Reserved.

#include "SandDemoActors.h"
#include "SandContactPointComponent.h"
#include "SandInteractionComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

ASandDemoActor::ASandDemoActor()
{
	PrimaryActorTick.bCanEverTick = true;
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->SetMobility(EComponentMobility::Movable);
}

int32 ASandDemoActor::GetTotalImpactStamps() const
{
	int32 N = 0;
	for (const USandContactPointComponent* P : ContactPoints) { N += P->GetImpactStamps(); }
	return N;
}

int32 ASandDemoActor::GetTotalWheelSegments() const
{
	int32 N = 0;
	for (const USandContactPointComponent* P : ContactPoints) { N += P->GetWheelSegments(); }
	return N;
}

int32 ASandDemoActor::GetTotalStaticStamps() const
{
	int32 N = 0;
	for (const USandContactPointComponent* P : ContactPoints) { N += P->GetStaticStamps(); }
	return N;
}

int32 ASandDemoActor::GetTotalTraces() const
{
	int32 N = 0;
	for (const USandContactPointComponent* P : ContactPoints) { N += P->GetTraces(); }
	return N;
}

void ASandDemoActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Build();
}

void ASandDemoActor::Build()
{
	for (UActorComponent* Old : Built)
	{
		if (Old)
		{
			Old->DestroyComponent();
		}
	}
	Built.Reset();
	ContactPoints.Reset();

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	auto AddMesh = [this](UStaticMesh* Mesh, const FVector& Location, const FRotator& Rotation, const FVector& Scale)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		C->SetStaticMesh(Mesh);
		C->SetupAttachment(Body);
		C->SetRelativeLocationAndRotation(Location, Rotation);
		C->SetRelativeScale3D(Scale);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->RegisterComponent();
		Built.Add(C);
		return C;
	};
	auto AddPoint = [this](const FVector& Location, ESandContactMode Mode)
	{
		USandContactPointComponent* P = NewObject<USandContactPointComponent>(this, NAME_None, RF_Transient);
		P->Mode = Mode;
		P->SetupAttachment(Body);
		P->SetRelativeLocation(Location);
		P->RegisterComponent();
		Built.Add(P);
		ContactPoints.Add(P);
		return P;
	};

	switch (Kind)
	{
	case ESandDemoKind::Box:
	{
		Body->SetStaticMesh(BodyMesh ? BodyMesh.Get() : Cube);
		Body->SetRelativeScale3D(BodyScale.IsNearlyZero() ? FVector(0.8) : BodyScale);
		Body->SetCollisionProfileName(TEXT("PhysicsActor"));
		USandContactPointComponent* P = AddPoint(FVector::ZeroVector, ESandContactMode::Pad);
		P->bAutoExtentFromOwner = true;
		P->StaticDepthLimitCm = 2.5f;
		break;
	}
	case ESandDemoKind::Ball:
	{
		UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		Body->SetStaticMesh(BodyMesh ? BodyMesh.Get() : Sphere);
		Body->SetRelativeScale3D(BodyScale.IsNearlyZero() ? FVector(0.8) : BodyScale);
		Body->SetCollisionProfileName(TEXT("PhysicsActor"));
		const FVector Extent = Body->GetStaticMesh() ? Body->GetStaticMesh()->GetBounds().BoxExtent * Body->GetRelativeScale3D() : FVector(40.0);
		// a wheel contact at the centre: the trace reaches one radius down, so it touches when the ball rests or rolls, and the dent follows its path
		USandContactPointComponent* P = AddPoint(FVector::ZeroVector, ESandContactMode::Wheel);
		P->WheelRadiusCm = FMath::Max3(Extent.X, Extent.Y, Extent.Z);
		P->bSphericalBody = true; // a round dent where it lands and a round bottomed groove while it rolls: the width follows from the depth and the radius
		P->LoadKg = MassKg;
		break;
	}
	case ESandDemoKind::Vehicle:
	{
		Body->SetStaticMesh(BodyMesh ? BodyMesh.Get() : Cube);
		Body->SetRelativeScale3D(BodyScale.IsNearlyZero() ? FVector(2.4, 1.1, 0.3) : BodyScale);
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector Hub((i & 1) ? -WheelSpread.X : WheelSpread.X, (i & 2) ? -WheelSpread.Y : WheelSpread.Y, 0.0);
			// the body is scaled, so the wheels (children) are given the inverse scale to keep their size
			const FVector Inverse(1.0 / Body->GetRelativeScale3D().X, 1.0 / Body->GetRelativeScale3D().Y, 1.0 / Body->GetRelativeScale3D().Z);
			AddMesh(Cylinder, Hub * Inverse, FRotator(0.0, 0.0, 90.0), FVector(WheelRadiusCm * 2.f / 100.f, WheelRadiusCm * 2.f / 100.f, WheelHalfWidthCm * 2.f / 100.f) * Inverse);
			USandContactPointComponent* P = AddPoint(Hub * Inverse, ESandContactMode::Wheel);
			P->SetRelativeScale3D(Inverse);
			P->WheelRadiusCm = WheelRadiusCm;
			P->HalfWidthCm = WheelHalfWidthCm;
			P->LoadKg = MassKg / 4.f;
		}
		break;
	}
	case ESandDemoKind::Lander:
	{
		Body->SetStaticMesh(BodyMesh ? BodyMesh.Get() : Cube);
		Body->SetRelativeScale3D(BodyScale.IsNearlyZero() ? FVector(2.4, 2.4, 1.2) : BodyScale);
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		const FVector Inverse(1.0 / Body->GetRelativeScale3D().X, 1.0 / Body->GetRelativeScale3D().Y, 1.0 / Body->GetRelativeScale3D().Z);
		const float LegLength = 110.f; // the pads are this far below the body centre
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector Foot((i & 1) ? -LegSpreadCm : LegSpreadCm, (i & 2) ? -LegSpreadCm : LegSpreadCm, -LegLength);
			AddMesh(Cylinder, (Foot * 0.5) * Inverse, FRotator::ZeroRotator, FVector(0.12, 0.12, LegLength / 100.f) * Inverse);
			AddMesh(Cylinder, Foot * Inverse + FVector(0.0, 0.0, 3.0) * Inverse.Z, FRotator::ZeroRotator, FVector(PadHalfSizeCm * 2.f / 100.f, PadHalfSizeCm * 2.f / 100.f, 0.06) * Inverse);
			USandContactPointComponent* P = AddPoint(Foot * Inverse, ESandContactMode::Pad);
			P->SetRelativeScale3D(Inverse);
			P->HalfWidthCm = PadHalfSizeCm;
			P->HalfLengthCm = PadHalfSizeCm;
			P->LoadKg = MassKg / 4.f;
			P->StaticDepthLimitCm = 5.f;
			P->ReferenceLoadKg = 250.f;
		}
		break;
	}
	}
}

float ASandDemoActor::GroundZ(const FVector& At, bool& bFound) const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SandDemoGround), false, this);
	bFound = GetWorld()->LineTraceSingleByChannel(Hit, At + FVector(0.0, 0.0, 3000.0), At - FVector(0.0, 0.0, 6000.0), ECC_Visibility, Params);
	return bFound ? Hit.ImpactPoint.Z : At.Z;
}

void ASandDemoActor::BeginPlay()
{
	Super::BeginPlay();
	const bool bProp = Kind == ESandDemoKind::Box || Kind == ESandDemoKind::Ball;
	if (bProp && bSimulatePhysics)
	{
		Body->SetMassOverrideInKg(NAME_None, MassKg, true);
		Body->SetSimulatePhysics(true);
		if (!InitialVelocityCm.IsNearlyZero())
		{
			bVelocityPending = true; // the physics body may not exist yet (an actor spawned in the first frames): Tick applies it
		}
	}
	Yaw =GetActorRotation().Yaw;
	if (!bAutoRun || bProp)
	{
		bFinished = bProp;
	}
}

ASandDemoActor* ASandDemoActor::SpawnDemo(const UObject* WorldContextObject, ESandDemoKind DemoKind, const FTransform& Transform, FVector InitialVelocity, float Mass, UStaticMesh* Mesh, FVector Scale)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}
	ASandDemoActor* A = World->SpawnActorDeferred<ASandDemoActor>(ASandDemoActor::StaticClass(), Transform);
	if (!A)
	{
		return nullptr;
	}
	A->Kind = DemoKind;
	A->bAutoRun = false;
	A->MassKg = Mass;
	A->BodyMesh = Mesh;
	A->BodyScale = Scale;
	A->InitialVelocityCm = InitialVelocity;
	A->FinishSpawning(Transform);
	return A;
}

void ASandDemoActor::StartRun()
{
	if (Kind == ESandDemoKind::Box || Kind == ESandDemoKind::Ball || bRunning)
	{
		return;
	}
	bRunning = true;
	bStarted = true;
	bFinished = false;
	RunTime = 0.f;
	if (Kind == ESandDemoKind::Lander)
	{
		bool bFound;
		const float Ground = GroundZ(GetActorLocation(), bFound);
		SetActorLocation(FVector(GetActorLocation().X, GetActorLocation().Y, Ground + StartHeightCm + 110.f));
	}
}

void ASandDemoActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bVelocityPending && Body->IsSimulatingPhysics())
	{
		Body->SetPhysicsLinearVelocity(InitialVelocityCm);
		bVelocityPending = false;
	}
	if (!bRunning)
	{
		if (bAutoRun && !bStarted && Kind != ESandDemoKind::Box)
		{
			WaitTime += DeltaSeconds;
			if (WaitTime >= StartDelaySec)
			{
				StartRun();
			}
		}
		return;
	}
	RunTime += DeltaSeconds;
	if (Kind == ESandDemoKind::Vehicle)
	{
		if (RunTime > TurnAfterSec)
		{
			Yaw += TurnRateDeg * DeltaSeconds;
		}
		const FVector Forward = FRotator(0.0, Yaw, 0.0).Vector();
		FVector Next = GetActorLocation() + Forward * DriveSpeedCm * DeltaSeconds;
		bool bFound;
		const float Ground = GroundZ(Next, bFound);
		Next.Z = Ground + WheelRadiusCm;
		SetActorLocationAndRotation(Next, FRotator(0.0, Yaw, 0.0));
		if (RunTime >= DriveSeconds)
		{
			bRunning = false;
			bFinished = true;
		}
	}
	else if (Kind == ESandDemoKind::Lander)
	{
		// the lowest pad's distance to the ground (the pads are LegLength below the body centre)
		float Gap = 1e9f;
		for (const USandContactPointComponent* P : ContactPoints)
		{
			bool bFound;
			const FVector At = P->GetComponentLocation();
			Gap = FMath::Min(Gap, At.Z - GroundZ(At, bFound));
		}
		const float Speed = Gap < SlowHeightCm ? TouchdownSpeedCm : ApproachSpeedCm;
		const float Step = FMath::Min(Speed * DeltaSeconds, Gap);
		SetActorLocation(GetActorLocation() - FVector(0.0, 0.0, Step));
		if (Gap <= 0.5f)
		{
			bRunning = false;
			bFinished = true;
		}
	}
}

ASandDemoCharacter::ASandDemoCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	Interaction = CreateDefaultSubobject<USandInteractionComponent>(TEXT("SandInteraction"));
}

void ASandDemoCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;
	if (bAutoWalk && Time >= StartDelaySec && (WalkSeconds <= 0.f || Time <= StartDelaySec + WalkSeconds))
	{
		AddMovementInput(GetActorForwardVector(), WalkInputScale);
	}
}
