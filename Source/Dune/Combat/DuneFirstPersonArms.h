// Project DUNE (full game): the first-person arms. The UEFN mannequin (/Game/DUNE/Player/FP) with only its arms shown, holding the pistol: the pistol model sits
// on the right hand and everything (gun kick, view model) hangs below one view root that UDuneGunFeel moves. The arms play the GaspFix pistol animations
// (SourceAssets/player): idle with now and then an idle break, walk / run / sprint blended by speed, jump, fall, landing. There is no Animation Blueprint:
// UDuneArmsAnimInstance picks the animations in C++ and its proxy blends them (and cross-fades every change of state).

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "DuneFirstPersonArms.generated.h"

class UAnimSequence;
class UCameraComponent;
class UMaterialInterface;
class USkeletalMesh;
class UMaterialInstanceDynamic;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

/**
 * Everything about the first-person arms that can be changed without code: the arm mesh, its animations, the bones, where the pistol sits in the view and how
 * the hand holds it. The game loads /Game/DUNE/Player/FP/DA_FP_View (edit or duplicate that asset to swap the model); without it the defaults below are used.
 * To use another model: set ArmsMesh and the animations (they must be made for that skeleton), name its hand / elbow / head bones, then tune GripLocal,
 * GripRotation and PistolViewOffset (the -DuneFpGrip, -DuneFpGripRot, -DuneFpPistol and -DuneFpYaw flags try values without a build).
 */
UCLASS(BlueprintType)
class UE_HOLDITTOGETHER_API UDuneFpViewConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	UDuneFpViewConfig();

	UPROPERTY(EditAnywhere, Category="Model") TSoftObjectPtr<USkeletalMesh> ArmsMesh;
	UPROPERTY(EditAnywhere, Category="Model") TSoftObjectPtr<UMaterialInterface> ArmsMaterial;
	/** The hand that holds the pistol (the pistol is attached to this bone), the elbow (for straightening the forearm) and the head (the camera sits there). */
	UPROPERTY(EditAnywhere, Category="Model") FName HandBone = TEXT("hand_r");
	UPROPERTY(EditAnywhere, Category="Model") FName ElbowBone = TEXT("lowerarm_r");
	UPROPERTY(EditAnywhere, Category="Model") FName HeadBone = TEXT("head");
	/** Bones hidden with everything below them (the head, the left arm). */
	UPROPERTY(EditAnywhere, Category="Model") TArray<FName> HideBones;
	/** Parts of the model farther left of the camera than this are cut away (the body is not meant to be seen). */
	UPROPERTY(EditAnywhere, Category="Model") float ClipLeftCm = 14.f;
	UPROPERTY(EditAnywhere, Category="Model") FLinearColor SleeveColor = FLinearColor(0.11f, 0.08f, 0.055f, 1.f);

	UPROPERTY(EditAnywhere, Category="Animations") TSoftObjectPtr<UAnimSequence> Idle;
	UPROPERTY(EditAnywhere, Category="Animations") TSoftObjectPtr<UAnimSequence> Walk;
	UPROPERTY(EditAnywhere, Category="Animations") TSoftObjectPtr<UAnimSequence> Run;
	UPROPERTY(EditAnywhere, Category="Animations") TSoftObjectPtr<UAnimSequence> Sprint;
	UPROPERTY(EditAnywhere, Category="Animations") TSoftObjectPtr<UAnimSequence> JumpStart;
	UPROPERTY(EditAnywhere, Category="Animations") TSoftObjectPtr<UAnimSequence> Fall;
	UPROPERTY(EditAnywhere, Category="Animations") TSoftObjectPtr<UAnimSequence> LandStand;
	UPROPERTY(EditAnywhere, Category="Animations") TSoftObjectPtr<UAnimSequence> LandRun;
	UPROPERTY(EditAnywhere, Category="Animations") TArray<TSoftObjectPtr<UAnimSequence>> IdleBreaks;
	/** Seconds standing still before an idle break plays. */
	UPROPERTY(EditAnywhere, Category="Animations") float IdleBreakAfter = 7.f;
	/** How much of the animation shows in the view (1 = all of it, 0 = the idle pose): at a run, and in the air or landing. */
	UPROPERTY(EditAnywhere, Category="Animations") float MoveStrength = 0.35f;
	UPROPERTY(EditAnywhere, Category="Animations") float AirStrength = 0.15f;

	/** The pistol in the view, relative to the camera (x forward, y right, z up), the turn that points it along the view (the model's barrel axis) and its scale. */
	UPROPERTY(EditAnywhere, Category="Pistol") FVector PistolViewOffset = FVector(30.f, 4.f, -17.f);
	UPROPERTY(EditAnywhere, Category="Pistol") FRotator PistolViewRotation = FRotator(0.f, -90.f, 0.f);
	UPROPERTY(EditAnywhere, Category="Pistol") float PistolScale = 1.f;
	/** Where the hand holds the pistol (pistol model space, cm) and an extra turn of the pistol in the hand (tunes the wrist). */
	UPROPERTY(EditAnywhere, Category="Pistol") FVector GripLocal = FVector(0.f, -2.f, -5.f);
	UPROPERTY(EditAnywhere, Category="Pistol") FRotator GripRotation = FRotator::ZeroRotator;
	/** The body is turned about the hand so the forearm points along the view (off: the body faces straight ahead). ArmsYawDeg is added on top. */
	UPROPERTY(EditAnywhere, Category="Pistol") bool bStraightenForearm = true;
	UPROPERTY(EditAnywhere, Category="Pistol") float ArmsYawDeg = 14.f;
};

UCLASS()
class UE_HOLDITTOGETHER_API UDuneArmsAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UPROPERTY() TObjectPtr<UAnimSequence> Idle;
	UPROPERTY() TObjectPtr<UAnimSequence> Walk;
	UPROPERTY() TObjectPtr<UAnimSequence> Run;
	UPROPERTY() TObjectPtr<UAnimSequence> Sprint;
	UPROPERTY() TObjectPtr<UAnimSequence> JumpStart;
	UPROPERTY() TObjectPtr<UAnimSequence> Fall;
	UPROPERTY() TObjectPtr<UAnimSequence> LandStand;
	UPROPERTY() TObjectPtr<UAnimSequence> LandRun;
	UPROPERTY() TArray<TObjectPtr<UAnimSequence>> Breaks;

	/** Seconds standing still before an idle break plays. */
	float IdleBreakAfter = 7.f;
	/** How much of the animation shows in the view (1 = all of it, 0 = the idle pose): at a run, and in the air or landing. */
	float MoveStrength = 0.35f;
	float AirStrength = 0.15f;
	FName HeadBoneName = TEXT("head");
	/** Self test: >= 0 forces the speed (cm/s), TestAir forces the jump / fall state. */
	float TestSpeed = -1.f;
	bool bTestAir = false;
	int32 TestBreak = -1;

	/** 0 ground, 1 idle break, 2 jump start, 3 fall, 4 landing (for the log and the self test). */
	int32 GetStateId() const { return static_cast<int32>(State); }

	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
	enum class EState : uint8 { Ground, Break, JumpStart, Fall, Land };
	void Enter(EState NewState, float FadeSeconds);

	EState State = EState::Ground;
	float StateTime = 0.f;
	float Phase = 0.f;
	float IdleTime = 0.f;
	float FadeTime = 1.f;
	float FadeDuration = 0.2f;
	int32 BreakIndex = 0;
	UPROPERTY() TObjectPtr<UAnimSequence> LandSeq;
};

UCLASS(ClassGroup=(Dune))
class UE_HOLDITTOGETHER_API UDuneFirstPersonArms : public UActorComponent
{
	GENERATED_BODY()

public:
	UDuneFirstPersonArms();

	/**
	 * Builds the view root (at the camera) and the arms from the UDuneFpViewConfig; the pistol (if any) is moved onto the hand. Returns false when the mesh or
	 * the animations are missing: nothing was changed then and the caller keeps its pistol on the camera.
	 */
	bool Setup(UCameraComponent* InCamera, UStaticMeshComponent* InPistol);
	/** The muzzle in camera space for a muzzle given in the pistol model's space (the muzzle flash sits there). */
	FVector GetMuzzleInCameraSpace(const FVector& MuzzleLocalInPistol) const;

	/** The component the gun kick moves (the arms and the pistol hang below it). */
	USceneComponent* GetViewRoot() const { return ViewRoot; }
	void SetArmsVisible(bool bVisible);
	/** True once the arms hold the pistol (the first frames wait for the animation pose). */
	bool IsAligned() const { return bAligned; }
	UDuneArmsAnimInstance* GetAnim() const;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void AlignToPistol();
	void RunSelfTest(float Time);

	UPROPERTY() TObjectPtr<USceneComponent> ViewRoot;
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> Arms;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Pistol;
	UPROPERTY() TObjectPtr<UCameraComponent> CameraRef;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ArmsMat;
	UPROPERTY() TObjectPtr<UDuneFpViewConfig> Config;
	FRotator PistolRot = FRotator::ZeroRotator;
	FRotator GripRotation = FRotator::ZeroRotator;
	FVector GripLocal = FVector::ZeroVector;
	FVector PistolOffset = FVector::ZeroVector;
	float ArmsYawDeg = 0.f;
	float AppliedYaw = 0.f;
	float PistolScaleValue = 1.f;
	int32 TickCount = 0;
	bool bAligned = false;
	float Clock = 0.f;
	int32 TestStep = 0;
};
