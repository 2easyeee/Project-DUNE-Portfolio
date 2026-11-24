#include "Dune/Combat/DuneDebrisLauncher.h"
#include "Dune/Combat/DuneGunFeel.h"
#include "Dune/DuneFeelFlags.h"
#include "Dune/Combat/DuneHealthComponent.h"
#include "Dune/Combat/DunePlayerCharacter.h"
#include "Dune/SliceableStructure.h"
#include "Engine/OverlapResult.h"
#include "SandDeformationLibrary.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "ProceduralMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogDuneDebris, Log, All);

UDuneDebrisLauncher::UDuneDebrisLauncher()
{
	PrimaryComponentTick.bCanEverTick = true;
}

int32 UDuneDebrisLauncher::GetLevel() const
{
	if (!bCharging) return 0;
	return FMath::Clamp(1 + FMath::FloorToInt(Charge * 3.f), 1, 3);
}

void UDuneDebrisLauncher::BeginCharge()
{
	bCharging = true;
	Charge = 0.f;
	// the piece in the crosshair is taken into the hand: no physics and no collision while it is held (LaunchPiece wakes it again)
	Held = FindAimedPiece();
	if (UProceduralMeshComponent* P = Held.Get())
	{
		P->SetSimulatePhysics(false);
		P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		UpdateHeld();
	}
}

void UDuneDebrisLauncher::UpdateHeld()
{
	UProceduralMeshComponent* P = Held.Get();
	const ADunePlayerCharacter* Player = Cast<ADunePlayerCharacter>(GetOwner());
	FVector Loc, Dir;
	if (!P || !Player || !Player->GetAimView(Loc, Dir)) return;
	// lower right of the view, further away for a bigger piece so it never fills the screen
	const float R = FMath::Min(P->Bounds.SphereRadius, 90.f);
	const FRotator Rot = Dir.Rotation();
	const FVector Target = Loc + Rot.RotateVector(FVector(60.f + R * 1.2f, 18.f + R * 0.5f, -14.f - R * 0.3f));
	P->AddWorldOffset(Target - P->Bounds.Origin, false, nullptr, ETeleportType::TeleportPhysics);
}

UProceduralMeshComponent* UDuneDebrisLauncher::ReleaseCharge()
{
	if (!bCharging) return nullptr;
	const int32 Level = GetLevel();
	bCharging = false;
	Charge = 0.f;

	const ADunePlayerCharacter* Player = Cast<ADunePlayerCharacter>(GetOwner());
	FVector Loc, Dir;
	if (!Player || !GetWorld() || !Player->GetAimView(Loc, Dir)) return nullptr;

	UProceduralMeshComponent* Piece = Held.Get() ? Held.Get() : FindAimedPiece();
	Held.Reset();
	if (!Piece)
	{
		UE_LOG(LogDuneDebris, Log, TEXT("Debris: nothing in sight to throw"));
		return nullptr;
	}
	if (LaunchPiece(Piece, Loc, Dir, Level) <= 0.f) return nullptr;
	return Piece;
}

UProceduralMeshComponent* UDuneDebrisLauncher::FindAimedPiece() const
{
	const ADunePlayerCharacter* Player = Cast<ADunePlayerCharacter>(GetOwner());
	UWorld* World = GetWorld();
	FVector Loc, Dir;
	if (!Player || !World || !Player->GetAimView(Loc, Dir)) return nullptr;

	// the piece in the crosshair: a small sphere sweep so a thin fragment is easy to pick
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DuneDebrisAim), false, GetOwner());
	FHitResult Hit;
	if (!World->SweepSingleByChannel(Hit, Loc, Loc + Dir * AimRangeCm, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(12.f), Params)) return nullptr;
	UProceduralMeshComponent* Piece = Cast<UProceduralMeshComponent>(Hit.GetComponent());
	const ASliceableStructure* Structure = Piece ? Cast<ASliceableStructure>(Piece->GetOwner()) : nullptr;
	return Structure && Structure->GetPieces().Contains(Piece) ? Piece : nullptr; // same rule as LaunchPiece
}

float UDuneDebrisLauncher::LaunchPiece(UProceduralMeshComponent* Piece, const FVector& Eye, FVector Dir, int32 Level)
{
	ASliceableStructure* Structure = Piece ? Cast<ASliceableStructure>(Piece->GetOwner()) : nullptr;
	if (!Structure || !Structure->GetPieces().Contains(Piece) || LevelSpeedCm.Num() == 0)
	{
		UE_LOG(LogDuneDebris, Log, TEXT("Debris: %s is not a broken piece, not thrown"), *GetNameSafe(Piece));
		return 0.f;
	}
	Level = FMath::Clamp(Level, 1, LevelSpeedCm.Num());
	Dir = Dir.GetSafeNormal();

	Structure->WakeForLaunch(Piece); // a settled piece is a static body: it must simulate again before it can fly
	const FVector Start = Eye + Dir * (HoldDistanceCm + Piece->Bounds.SphereRadius);
	Piece->AddWorldOffset(Start - Piece->Bounds.Origin, false, nullptr, ETeleportType::TeleportPhysics);
	Piece->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Piece->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	const float Mass = FMath::Max(Piece->GetMass(), 1.f);
	float Speed = FMath::Clamp(LevelSpeedCm[Level - 1] * RefMassKg / Mass, MinSpeedCm, MaxSpeedCm);
	if (Mass >= HeavyMassKg) Speed = FMath::Min(Speed * (1.f + HeavySpeedBonus), MaxSpeedCm * (1.f + HeavySpeedBonus)); // Giant's Grip may pass the normal cap
	Piece->AddImpulse(Dir * Speed, NAME_None, true); // velocity change: the speed does not depend on the mass beyond the formula above

	if (const ADunePlayerCharacter* Thrower = Cast<ADunePlayerCharacter>(GetOwner()))
	{
		if (UDuneGunFeel* Feel = Thrower->GetGunFeel()) Feel->OnDebrisThrow(Level);
	}
	FFlying F;
	F.Piece = Piece;
	F.LaunchSpeed = Speed;
	F.Level = Level;
	Flying.Add(F);
	++Launches;
	UE_LOG(LogDuneDebris, Log, TEXT("Debris: threw %s (mass %.0f kg) at level %d: speed %.0f cm/s, momentum %.0f, from %s along %s"), *Piece->GetName(), Mass, Level, Speed, Mass * Speed,
		*Piece->Bounds.Origin.ToCompactString(), *Dir.ToCompactString());
	return Speed;
}

float UDuneDebrisLauncher::DamageFor(int32 Level, float MassKg, float HitSpeed, float LaunchSpeed) const
{
	if (LevelDamage.Num() == 0) return MinDamage;
	const float Base = LevelDamage[FMath::Clamp(Level, 1, LevelDamage.Num()) - 1];
	const float MassFactor = FMath::Clamp(FMath::Sqrt(FMath::Max(MassKg, 1.f) / FMath::Max(RefMassKg, 1.f)), MinMassFactor, MaxMassFactor);
	const float SpeedFactor = FMath::Clamp(HitSpeed / FMath::Max(LaunchSpeed, 1.f), 0.25f, 1.f); // a piece that already bounced off something hits softer
	return FMath::Clamp(Base * DamageMultiplier * MassFactor * SpeedFactor, MinDamage, MaxDamage);
}

void UDuneDebrisLauncher::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bCharging) Charge = FMath::Min(1.f, Charge + DeltaTime / FMath::Max(ChargeSeconds, 0.1f));
	if (bCharging) UpdateHeld();
	UpdateFlying(DeltaTime);
}

void UDuneDebrisLauncher::UpdateSand(FFlying& F, UProceduralMeshComponent* Piece, float Speed)
{
	UWorld* World = GetWorld();
	const FVector Pos = Piece->Bounds.Origin;
	const float R = Piece->Bounds.SphereRadius;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DuneDebrisSand), false, GetOwner());
	Params.AddIgnoredActor(Piece->GetOwner()); // the structure the piece came from is not the ground
	FHitResult G;
	const bool bGround = World->LineTraceSingleByChannel(G, Pos, Pos - FVector(0.f, 0.f, R + 25.f), ECC_Visibility, Params) && G.ImpactNormal.Z > 0.5f;
	if (bGround)
	{
		const float Strength = 0.8f + 0.7f * F.Level;
		if (!F.bOnGround)
		{
			// the landing: a crater as wide as the piece (exaggerated on purpose, the sand heals itself) plus a few small ones where sand was thrown
			LastLanding = G.ImpactPoint; bHasLanding = true;
			if (DuneOldFeel()) { F.bOnGround = true; return; } // before: the scene is the same, only the sand stays untouched
			const float Radius = FMath::Clamp(R * 1.2f, 40.f, 180.f);
			FSandInteractionRequest Crater; // a deep bowl with a tall raised rim
			Crater.Brush = ESandBrushType::Impact;
			Crater.bAccumulate = true;
			Crater.WorldPosition = G.ImpactPoint;
			Crater.Radius = Radius;
			Crater.ImpactSpeed = FMath::Max(Speed, 600.f);
			Crater.Strength = Strength;
			Crater.RidgeScale = 3.f;
			const bool bDug = USandDeformationLibrary::ApplySandInteraction(this, Crater);
			UE_LOG(LogDuneDebris, Log, TEXT("Debris: %s landed at %s (%.0f cm/s), crater radius %.0f cm, sand %s"), *Piece->GetName(), *G.ImpactPoint.ToCompactString(), Speed, Radius, bDug ? TEXT("dented") : TEXT("not under a patch"));
			for (int32 i = 0; i < 2 + 2 * F.Level; ++i)
			{
				const FVector2D Off = FVector2D(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f)).GetSafeNormal() * Radius * FMath::FRandRange(1.2f, 2.2f);
				USandDeformationLibrary::ApplySandImpact(this, G.ImpactPoint + FVector(Off.X, Off.Y, 0.f), FMath::FRandRange(8.f, 18.f), 450.f, 0.8f);
			}
		}
		else if (Speed > 350.f && !DuneOldFeel())
		{
			// sliding: a furrow from where the piece touched the ground last tick (deeper and wider the faster and heavier it is)
			USandDeformationLibrary::ApplySandTrail(this, F.PrevGround, G.ImpactPoint, FMath::Clamp(R * 0.4f, 10.f, 55.f), 0.5f + Speed / 2400.f);
		}
		F.PrevGround = G.ImpactPoint;
	}
	F.bOnGround = bGround;
}

void UDuneDebrisLauncher::UpdateFlying(float DeltaTime)
{
	UWorld* World = GetWorld();
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	for (int32 i = Flying.Num() - 1; i >= 0; --i)
	{
		FFlying& F = Flying[i];
		UProceduralMeshComponent* Piece = F.Piece.Get();
		F.Age += DeltaTime;
		if (!Piece || !World || F.Age > FlightSeconds || !Piece->IsSimulatingPhysics())
		{
			Flying.RemoveAtSwap(i);
			continue;
		}
		const FVector Vel = Piece->GetPhysicsLinearVelocity();
		// the speed of the last tick counts too: a piece that touches a target is slowed by the contact before this check runs
		const float Speed = FMath::Max(Vel.Size(), F.PrevSpeed);
		F.PrevSpeed = Vel.Size();
		UpdateSand(F, Piece, Speed);
		if (Speed < MinDamageSpeedCm)
		{
			if (F.Age > 0.4f) Flying.RemoveAtSwap(i);
			continue;
		}

		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DuneDebrisFlight), false, GetOwner());
		const FBoxSphereBounds B = Piece->Bounds;
		World->OverlapMultiByObjectType(Overlaps, B.Origin, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeBox(B.BoxExtent + FVector(30.f)), Params); // a margin: the contact itself can stop the piece just short of an overlap
		for (const FOverlapResult& R : Overlaps)
		{
			AActor* Target = R.GetActor();
			if (!Target || F.Hit.Contains(Target)) continue;
			UDuneHealthComponent* Health = Target->FindComponentByClass<UDuneHealthComponent>();
			if (!Health || Health->IsDead()) continue;
			const float Mass = FMath::Max(Piece->GetMass(), 1.f);
			const float Damage = DamageFor(F.Level, Mass, Speed, F.LaunchSpeed);
			UGameplayStatics::ApplyPointDamage(Target, Damage, Vel.GetSafeNormal(), FHitResult(), OwnerPawn ? OwnerPawn->GetController() : nullptr, GetOwner(), UDamageType::StaticClass());
			F.Hit.Add(Target);
			++Hits;
			if (const ADunePlayerCharacter* Thrower = Cast<ADunePlayerCharacter>(GetOwner()))
			{
				if (UDuneGunFeel* Feel = Thrower->GetGunFeel()) Feel->OnDebrisHit(Target->GetActorLocation() + FVector(0.f, 0.f, 60.f), Damage, Health->IsDead());
			}
			Piece->SetPhysicsLinearVelocity(Vel * 0.5f); // the piece gives up part of its speed to what it hit
			UE_LOG(LogDuneDebris, Log, TEXT("Debris: %s hit %s at %.0f cm/s (mass %.0f kg): %.0f damage"), *Piece->GetName(), *Target->GetName(), Speed, Mass, Damage);
		}
	}
}
