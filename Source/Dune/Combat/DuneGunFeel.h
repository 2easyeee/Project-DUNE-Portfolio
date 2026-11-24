// Project DUNE (full game): the "feel" of the weapons. Everything the player sees and feels when a shot or a thrown piece does something, in one component:
// camera recoil, camera shake (trauma model), FOV punch, pistol kick in the view, muzzle flash, hit marker, damage numbers, a short hit stop on kills and
// heavy hits. The pistol (ADunePlayerCharacter::Fire) and the debris launcher call the On... functions; the HUD reads the hit marker and numbers.
// All values are tunables, and bEnabled switches the whole thing off.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DuneGunFeel.generated.h"

class UCameraComponent;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;

USTRUCT()
struct FDuneDamageNumber
{
	GENERATED_BODY()

	FVector Location = FVector::ZeroVector;
	float Damage = 0.f;
	float Age = 0.f;
	bool bKill = false;
	bool bCrit = false;
};

UCLASS(ClassGroup=(Dune), meta=(BlueprintSpawnableComponent))
class UE_HOLDITTOGETHER_API UDuneGunFeel : public UActorComponent
{
	GENERATED_BODY()

public:
	UDuneGunFeel();

	UPROPERTY(EditAnywhere, Category="Feel") bool bEnabled = true;

	// camera: recoil is a pitch kick that springs back; shake follows the trauma model (shake = Trauma^2 x noise, trauma decays)
	UPROPERTY(EditAnywhere, Category="Feel|Camera") float RecoilPitchDeg = 1.4f;
	UPROPERTY(EditAnywhere, Category="Feel|Camera") float RecoilRecover = 16.f;
	UPROPERTY(EditAnywhere, Category="Feel|Camera") float MaxShakeAngleDeg = 2.4f;
	UPROPERTY(EditAnywhere, Category="Feel|Camera") float MaxShakeOffsetCm = 2.0f;
	UPROPERTY(EditAnywhere, Category="Feel|Camera") float ShakeSpeed = 32.f;
	UPROPERTY(EditAnywhere, Category="Feel|Camera") float TraumaDecay = 2.2f;
	UPROPERTY(EditAnywhere, Category="Feel|Camera") float FireTrauma = 0.2f;
	UPROPERTY(EditAnywhere, Category="Feel|Camera") float FractureTrauma = 0.5f;
	/** The beat when a structure breaks: a short freeze, an upward jolt of the view (deg) and a FOV punch (deg). */
	UPROPERTY(EditAnywhere, Category="Feel|Camera") float FractureHitStopSeconds = 0.07f;
	UPROPERTY(EditAnywhere, Category="Feel|Camera") float FractureKickDeg = 1.2f;
	UPROPERTY(EditAnywhere, Category="Feel|Camera") float FractureFovPunch = 4.f;
	/** FOV change (degrees) on a shot (positive = wider for a moment), and how far the view narrows while debris is fully charged. */
	UPROPERTY(EditAnywhere, Category="Feel|Camera") float FireFovPunch = 1.8f;
	UPROPERTY(EditAnywhere, Category="Feel|Camera") float FovRecover = 12.f;
	UPROPERTY(EditAnywhere, Category="Feel|Camera") float ChargeFovDeg = 6.f;

	// the pistol in the view: it jumps back and tips up, then settles
	UPROPERTY(EditAnywhere, Category="Feel|Pistol") float KickBackCm = 4.5f;
	UPROPERTY(EditAnywhere, Category="Feel|Pistol") float KickPitchDeg = 8.f;
	UPROPERTY(EditAnywhere, Category="Feel|Pistol") float KickRecover = 20.f;
	UPROPERTY(EditAnywhere, Category="Feel|Pistol") float FlashSeconds = 0.05f;
	UPROPERTY(EditAnywhere, Category="Feel|Pistol") float FlashIntensity = 9000.f;

	// hits
	UPROPERTY(EditAnywhere, Category="Feel|Hit") float HitmarkerSeconds = 0.2f;
	UPROPERTY(EditAnywhere, Category="Feel|Hit") float NumberSeconds = 0.8f;
	UPROPERTY(EditAnywhere, Category="Feel|Hit") float KillTrauma = 0.4f;
	/** Time dilation during a hit stop and how long (real seconds) it lasts. A kill and a thrown piece that does at least HeavyHitDamage freeze the game. */
	UPROPERTY(EditAnywhere, Category="Feel|Hit") bool bHitStop = true;
	UPROPERTY(EditAnywhere, Category="Feel|Hit") float HitStopScale = 0.06f;
	UPROPERTY(EditAnywhere, Category="Feel|Hit") float KillHitStopSeconds = 0.06f;
	UPROPERTY(EditAnywhere, Category="Feel|Hit") float HeavyHitStopSeconds = 0.08f;
	UPROPERTY(EditAnywhere, Category="Feel|Hit") float HeavyHitDamage = 40.f;

	/** Called by the owner once the pistol model exists: Pistol is kicked, the muzzle flash sits at MuzzleLocal (camera space). */
	void Setup(UCameraComponent* InCamera, USceneComponent* InPistol, const FVector& MuzzleLocal);

	void OnShot();
	/** The pistol hit something with health. */
	void OnShotHit(const FVector& Location, float Damage, bool bKill, bool bCrit);
	/** The pistol cracked a pillar. */
	void OnFracture();
	/** A piece was thrown at charge level 1..3. */
	void OnDebrisThrow(int32 Level);
	/** A thrown piece hit something with health. */
	void OnDebrisHit(const FVector& Location, float Damage, bool bKill);

	void AddTrauma(float Amount);

	// for the HUD
	float GetHitmarkerAlpha() const { return FMath::Clamp(HitmarkerTime / FMath::Max(HitmarkerSeconds, 0.01f), 0.f, 1.f); }
	bool IsHitmarkerKill() const { return bHitmarkerKill; }
	const TArray<FDuneDamageNumber>& GetNumbers() const { return Numbers; }

	// for the self test
	float GetTrauma() const { return Trauma; }
	float GetRecoilPitch() const { return RecoilPitch; }
	float GetKickBack() const { return KickBack; }
	float GetFlashTime() const { return FlashTime; }
	int32 GetShots() const { return Shots; }
	int32 GetHits() const { return Hits; }
	int32 GetKills() const { return Kills; }
	int32 GetShakeEvents() const { return ShakeEvents; }
	int32 GetHitStops() const { return HitStops; }
	int32 GetDebrisThrows() const { return DebrisThrows; }

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void StartHitmarker(bool bKill);
	void HitStop(float Seconds);
	void AddNumber(const FVector& Location, float Damage, bool bKill, bool bCrit);

	UPROPERTY() TObjectPtr<UCameraComponent> Camera;
	UPROPERTY() TObjectPtr<USceneComponent> Pistol;
	UPROPERTY() TObjectPtr<UPointLightComponent> Flash;
	FVector PistolRestLocation = FVector::ZeroVector;
	FQuat PistolRestRotation = FQuat::Identity;

	TArray<FDuneDamageNumber> Numbers;
	float Trauma = 0.f;
	float RecoilPitch = 0.f;
	float FovPunch = 0.f;
	float ChargeFov = 0.f;
	float KickBack = 0.f;
	float KickPitch = 0.f;
	float FlashTime = 0.f;
	float HitmarkerTime = 0.f;
	bool bHitmarkerKill = false;
	float NoiseTime = 0.f;
	bool bHitStopActive = false;

	int32 Shots = 0;
	int32 Hits = 0;
	int32 Kills = 0;
	int32 ShakeEvents = 0;
	int32 HitStops = 0;
	int32 DebrisThrows = 0;
};
