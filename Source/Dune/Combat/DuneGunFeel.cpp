#include "Dune/Combat/DuneGunFeel.h"
#include "Dune/DuneFeelFlags.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Dune/Combat/DuneDebrisLauncher.h"
#include "Dune/Combat/DunePlayerCharacter.h"
#include "Kismet/GameplayStatics.h"

UDuneGunFeel::UDuneGunFeel()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UDuneGunFeel::Setup(UCameraComponent* InCamera, USceneComponent* InPistol, const FVector& MuzzleLocal)
{
	Camera = InCamera;
	Pistol = InPistol;
	if (Pistol)
	{
		PistolRestLocation = Pistol->GetRelativeLocation();
		PistolRestRotation = Pistol->GetRelativeRotation().Quaternion();
	}
	if (Camera && GetOwner())
	{
		Flash = NewObject<UPointLightComponent>(GetOwner());
		Flash->SetupAttachment(Camera);
		Flash->SetRelativeLocation(MuzzleLocal + FVector(10.f, 0.f, 0.f));
		Flash->SetLightColor(FLinearColor(1.f, 0.62f, 0.25f));
		Flash->SetAttenuationRadius(450.f);
		Flash->SetCastShadows(false);
		Flash->SetIntensity(0.f);
		Flash->SetVisibility(false);
		Flash->RegisterComponent();
	}
}

void UDuneGunFeel::AddTrauma(float Amount)
{
	if (!bEnabled) return;
	Trauma = FMath::Clamp(Trauma + Amount, 0.f, 1.f);
	++ShakeEvents;
}

void UDuneGunFeel::StartHitmarker(bool bKill)
{
	HitmarkerTime = HitmarkerSeconds;
	bHitmarkerKill = bKill;
}

void UDuneGunFeel::AddNumber(const FVector& Location, float Damage, bool bKill, bool bCrit)
{
	FDuneDamageNumber N;
	N.Location = Location + FVector(FMath::FRandRange(-12.f, 12.f), FMath::FRandRange(-12.f, 12.f), 0.f);
	N.Damage = Damage;
	N.bKill = bKill;
	N.bCrit = bCrit;
	Numbers.Add(N);
	if (Numbers.Num() > 12) Numbers.RemoveAt(0);
}

void UDuneGunFeel::HitStop(float Seconds)
{
	UWorld* World = GetWorld();
	if (!bEnabled || !bHitStop || bHitStopActive || !World || Seconds <= 0.f) return;
	bHitStopActive = true;
	++HitStops;
	UGameplayStatics::SetGlobalTimeDilation(World, HitStopScale);
	// the ticker runs in real time, so the freeze lasts Seconds however slow the game is meanwhile
	TWeakObjectPtr<UDuneGunFeel> Weak(this);
	TWeakObjectPtr<UWorld> WeakWorld(World);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak, WeakWorld](float) -> bool
	{
		if (WeakWorld.IsValid()) UGameplayStatics::SetGlobalTimeDilation(WeakWorld.Get(), 1.f);
		if (Weak.IsValid()) Weak->bHitStopActive = false;
		return false;
	}), Seconds);
}

void UDuneGunFeel::OnShot()
{
	if (!bEnabled) return;
	++Shots;
	RecoilPitch += RecoilPitchDeg;
	FovPunch += FireFovPunch;
	KickBack = KickBackCm;
	KickPitch = KickPitchDeg;
	FlashTime = FlashSeconds;
	AddTrauma(FireTrauma);
}

void UDuneGunFeel::OnShotHit(const FVector& Location, float Damage, bool bKill, bool bCrit)
{
	if (!bEnabled) return;
	++Hits;
	StartHitmarker(bKill);
	AddNumber(Location, Damage, bKill, bCrit);
	if (bKill)
	{
		++Kills;
		AddTrauma(KillTrauma);
		HitStop(KillHitStopSeconds);
	}
}

void UDuneGunFeel::OnFracture()
{
	AddTrauma(DuneOldFeel() ? 0.35f : FractureTrauma);
	if (!bEnabled || DuneOldFeel()) return;
	// the beat of the break: the game stops for a moment, the view jolts up and widens
	HitStop(FractureHitStopSeconds);
	RecoilPitch += FractureKickDeg;
	FovPunch += FractureFovPunch;
}

void UDuneGunFeel::OnDebrisThrow(int32 Level)
{
	if (!bEnabled) return;
	++DebrisThrows;
	AddTrauma(0.2f + 0.15f * Level);
	RecoilPitch += 0.8f * Level;
	FovPunch += 1.5f * Level;
}

void UDuneGunFeel::OnDebrisHit(const FVector& Location, float Damage, bool bKill)
{
	if (!bEnabled) return;
	++Hits;
	StartHitmarker(bKill);
	AddNumber(Location, Damage, bKill, false);
	AddTrauma(0.25f + 0.45f * FMath::Clamp(Damage / 150.f, 0.f, 1.f));
	if (bKill) ++Kills;
	if (bKill || Damage >= HeavyHitDamage) HitStop(HeavyHitStopSeconds);
}

void UDuneGunFeel::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bEnabled) return;

	// the pistol in the view
	KickBack = FMath::FInterpTo(KickBack, 0.f, DeltaTime, KickRecover);
	KickPitch = FMath::FInterpTo(KickPitch, 0.f, DeltaTime, KickRecover);
	if (Pistol)
	{
		Pistol->SetRelativeLocation(PistolRestLocation + FVector(-KickBack, 0.f, 0.2f * KickBack));
		Pistol->SetRelativeRotation(FRotator(KickPitch, 0.f, 0.f).Quaternion() * PistolRestRotation); // tipped up around the camera's right axis
	}

	// the muzzle flash
	FlashTime = FMath::Max(0.f, FlashTime - DeltaTime);
	if (Flash)
	{
		const bool bOn = FlashTime > 0.f;
		Flash->SetVisibility(bOn);
		Flash->SetIntensity(bOn ? FlashIntensity * FlashTime / FMath::Max(FlashSeconds, 0.01f) : 0.f);
	}

	// charging debris narrows the view and trembles a little
	float Charge = 0.f;
	if (const ADunePlayerCharacter* Player = Cast<ADunePlayerCharacter>(GetOwner()))
	{
		if (const UDuneDebrisLauncher* Debris = Player->GetDebris()) Charge = Debris->IsCharging() ? Debris->GetCharge() : 0.f;
	}
	ChargeFov = FMath::FInterpTo(ChargeFov, -ChargeFovDeg * Charge, DeltaTime, 8.f);
	const float ChargeTrauma = Charge * Charge * 0.12f;

	// the camera: recoil + shake + FOV on top of the view the controller sets
	RecoilPitch = FMath::FInterpTo(RecoilPitch, 0.f, DeltaTime, RecoilRecover);
	FovPunch = FMath::FInterpTo(FovPunch, 0.f, DeltaTime, FovRecover);
	NoiseTime += DeltaTime * ShakeSpeed;
	const float Shake = FMath::Square(FMath::Max(Trauma, ChargeTrauma));
	const float Pitch = RecoilPitch + MaxShakeAngleDeg * Shake * FMath::PerlinNoise1D(NoiseTime);
	const float Yaw = MaxShakeAngleDeg * Shake * FMath::PerlinNoise1D(NoiseTime + 31.7f);
	const float Roll = MaxShakeAngleDeg * 0.7f * Shake * FMath::PerlinNoise1D(NoiseTime + 63.1f);
	const FVector Offset = FVector(0.f, FMath::PerlinNoise1D(NoiseTime + 11.f), FMath::PerlinNoise1D(NoiseTime + 47.f)) * (MaxShakeOffsetCm * Shake);
	// AddAdditiveOffset accumulates (FOV += ...) and nothing clears it, so clear first or the view drifts a little more every frame
	if (Camera) { Camera->ClearAdditiveOffset(); Camera->AddAdditiveOffset(FTransform(FRotator(Pitch, Yaw, Roll), Offset), FovPunch + ChargeFov); }
	Trauma = FMath::Max(0.f, Trauma - TraumaDecay * DeltaTime);

	// the hit marker and the damage numbers
	HitmarkerTime = FMath::Max(0.f, HitmarkerTime - DeltaTime);
	for (int32 i = Numbers.Num() - 1; i >= 0; --i)
	{
		Numbers[i].Age += DeltaTime;
		if (Numbers[i].Age > NumberSeconds) Numbers.RemoveAt(i);
	}
}

void UDuneGunFeel::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld()) UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
	Super::EndPlay(EndPlayReason);
}
