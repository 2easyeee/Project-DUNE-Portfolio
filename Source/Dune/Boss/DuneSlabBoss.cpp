#include "Dune/Boss/DuneSlabBoss.h"
#include "Animation/AnimSequence.h"
#include "Dune/Combat/DuneGunFeel.h"
#include "Dune/Combat/DuneHealthComponent.h"
#include "Dune/Combat/DunePlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "Engine/DamageEvents.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SandDeformationLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogDuneSlab, Log, All);

ADuneSlabBoss::ADuneSlabBoss()
{
	// the core's numbers, for a boss that stands on the floor
	MaxHealth = 3000.f;
	PatternInterval1 = 7.5f; // (unused: the boss picks its own actions, see ChoosePattern and RestSeconds)
	PatternInterval2 = 6.0f;
	Phase2Fraction = 0.65f;
	GroundOffsetCm = 0.f;
	BobCm = 0.f;

	// no glowing sphere, no rings
	CoreMesh->SetVisibility(false);
	for (UStaticMeshComponent* P : RingParts) P->SetVisibility(false);

	BodyMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SlabMesh"));
	BodyMesh->SetupAttachment(Hitbox);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones; // a giant scale must not cull its bones

	// hit boxes in world centimetres (the actor is not scaled, only the mesh): the base of the robe, the torso, the head
	Hitbox->SetSphereRadius(520.f);
	BodyHitbox = CreateDefaultSubobject<UCapsuleComponent>(TEXT("BodyHitbox"));
	BodyHitbox->SetupAttachment(Hitbox);
	BodyHitbox->InitCapsuleSize(430.f, 700.f);
	BodyHitbox->SetRelativeLocation(FVector(0.f, 0.f, 1000.f));
	BodyHitbox->SetCollisionProfileName(TEXT("BlockAll"));
	HeadHitbox = CreateDefaultSubobject<USphereComponent>(TEXT("HeadHitbox"));
	HeadHitbox->SetupAttachment(Hitbox); // moved to the head bone every tick (not attached to the bone: this skeleton has a 100x scale on its Hips, a child sphere would grow to km size)
	HeadHitbox->InitSphereRadius(340.f);
	HeadHitbox->SetCollisionProfileName(TEXT("BlockAll"));

	Light->SetRelativeLocation(FVector(0.f, 0.f, LightHeightCm));
	Light->SetIntensity(LightIntensity);
	Light->SetAttenuationRadius(9000.f);

	// the dust sheets: translucent cylinders seen from inside (the arena) and around the boss, driven by the strength of the storm
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	for (int32 i = 0; i < 2; ++i)
	{
		UStaticMeshComponent* S = CreateDefaultSubobject<UStaticMeshComponent>(i == 0 ? TEXT("StormOuter") : TEXT("StormInner"));
		S->SetupAttachment(Hitbox);
		if (Cylinder) S->SetStaticMesh(Cylinder);
		S->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		S->SetCastShadow(false);
		S->SetVisibility(false);
		S->TranslucencySortPriority = i;
		(i == 0 ? StormOuter : StormInner) = S;
	}

	// the six stone slabs on the shoulders (their meshes and places are set in BeginPlay)
	for (int32 i = 0; i < 6; ++i)
	{
		USceneComponent* R = CreateDefaultSubobject<USceneComponent>(*FString::Printf(TEXT("SlabRoot%d"), i));
		R->SetupAttachment(Hitbox);
		UStaticMeshComponent* M = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("SlabMesh%d"), i));
		M->SetupAttachment(R);
		M->SetMobility(EComponentMobility::Movable); // the slabs fly: a static mesh component would not move
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SlabRoots.Add(R);
		SlabMeshes.Add(M);
		UStaticMeshComponent* G = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("SlabGlow%d"), i));
		G->SetupAttachment(M);
		G->SetMobility(EComponentMobility::Movable);
		G->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		G->SetCastShadow(false);
		G->SetRelativeScale3D(FVector(1.12f));
		G->SetVisibility(false);
		SlabGlows.Add(G);

		// a hit box that exists only while the parry slab flies (the shot's trace must hit it); the others cannot be shot
		UBoxComponent* B = CreateDefaultSubobject<UBoxComponent>(*FString::Printf(TEXT("SlabHitbox%d"), i));
		B->SetupAttachment(R);
		B->SetBoxExtent(FVector(210.f, 170.f, 270.f)); // far bigger than the slab (in its own scaled units): a rough aim parries
		B->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		B->SetCollisionResponseToAllChannels(ECR_Ignore);
		B->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		B->SetCanEverAffectNavigation(false);
		SlabHitboxes.Add(B);

		// the landing mark on the floor
		UStaticMeshComponent* K = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("SlabMarker%d"), i));
		K->SetupAttachment(Hitbox);
		K->SetMobility(EComponentMobility::Movable);
		K->SetUsingAbsoluteLocation(true);
		K->SetUsingAbsoluteScale(true);
		K->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		K->SetCastShadow(false);
		K->SetVisibility(false);
		SlabMarkers.Add(K);
	}

	// the sand: a pool of grains (bursts, dust, blown sand), the wall of the tsunami, the floor marks of the eruptions and of the safe lanes
	auto MakeInstanced = [this](const TCHAR* Name)
	{
		UInstancedStaticMeshComponent* M = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		M->SetupAttachment(Hitbox);
		M->SetMobility(EComponentMobility::Movable);
		M->SetUsingAbsoluteLocation(true);
		M->SetUsingAbsoluteRotation(true);
		M->SetUsingAbsoluteScale(true);
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetCastShadow(false);
		M->SetCanEverAffectNavigation(false);
		return M;
	};
	GrainMesh = MakeInstanced(TEXT("Grains"));
	WaveMesh = MakeInstanced(TEXT("TsunamiGrains"));
	CurtainMesh = MakeInstanced(TEXT("TsunamiCurtains"));
	UStaticMesh* Disc = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Strip = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	for (int32 i = 0; i < 8; ++i)
	{
		UStaticMeshComponent* K = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("SpotMarker%d"), i));
		K->SetupAttachment(Hitbox);
		K->SetMobility(EComponentMobility::Movable);
		K->SetUsingAbsoluteLocation(true);
		K->SetUsingAbsoluteScale(true);
		K->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		K->SetCastShadow(false);
		K->SetVisibility(false);
		if (Disc) K->SetStaticMesh(Disc);
		SpotMarkers.Add(K);
	}
	for (int32 i = 0; i < 6; ++i)
	{
		UStaticMeshComponent* K = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("LaneMarker%d"), i));
		K->SetupAttachment(Hitbox);
		K->SetMobility(EComponentMobility::Movable);
		K->SetUsingAbsoluteLocation(true);
		K->SetUsingAbsoluteRotation(true);
		K->SetUsingAbsoluteScale(true);
		K->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		K->SetCastShadow(false);
		K->SetVisibility(false);
		if (Strip) K->SetStaticMesh(Strip);
		LaneMarkers.Add(K);
	}
}

void ADuneSlabBoss::BeginPlay()
{
	Super::BeginPlay();
	StartYaw = GetActorRotation().Yaw;
	Light->SetRelativeLocation(FVector(0.f, 0.f, LightHeightCm));
	Light->SetIntensity(LightIntensity);
	BodyMesh->SetRelativeScale3D(FVector(MeshScale));
	BodyMesh->SetRelativeRotation(FRotator(0.f, BodyYawOffsetDeg, 0.f));

	// the storm: the cylinders are sized here (the engine cylinder is 100 cm wide and 100 cm high), their material starts invisible
	if (UMaterialInterface* StormBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/DUNE/Boss/SlabBearer/M_SandStorm.M_SandStorm")))
	{
		StormMatOuter = UMaterialInstanceDynamic::Create(StormBase, this);
		StormMatInner = UMaterialInstanceDynamic::Create(StormBase, this);
		StormOuter->SetMaterial(0, StormMatOuter);
		StormInner->SetMaterial(0, StormMatInner);
		StormOuter->SetRelativeScale3D(FVector(StormOuterRadiusCm / 50.f, StormOuterRadiusCm / 50.f, StormHeightCm / 100.f));
		StormOuter->SetRelativeLocation(FVector(0.f, 0.f, StormHeightCm * 0.5f));
		StormInner->SetRelativeScale3D(FVector(StormInnerRadiusCm / 50.f, StormInnerRadiusCm / 50.f, StormHeightCm / 100.f));
		StormInner->SetRelativeLocation(FVector(0.f, 0.f, StormHeightCm * 0.5f));
		StormMatInner->SetScalarParameterValue(TEXT("NoiseScale"), 0.0022f); // finer clumps close to the boss
		StormMatInner->SetVectorParameterValue(TEXT("Wind"), FLinearColor(-1.4f, 0.9f, 0.f, 0.f));
	}
	else
	{
		UE_LOG(LogDuneSlab, Warning, TEXT("Slab boss: M_SandStorm is missing, no dust"));
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("DuneStormDemo"))) StormHold = 1.0e6f; // a picture of the storm
	if (USkeletalMesh* SkMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/DUNE/Boss/SlabBearer/SKM_SlabBearer.SKM_SlabBearer")))
	{
		BodyMesh->SetSkeletalMesh(SkMesh);
	}
	else
	{
		UE_LOG(LogDuneSlab, Error, TEXT("Slab boss: the mesh is missing"));
	}
	Roar = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/DUNE/Boss/SlabBearer/Anims/A_MonsterRoar_Slab.A_MonsterRoar_Slab"));
	// the animations of the actions: the arm-pose retargets (Day 32), no blending between them (there is no animation blueprint: a change is a cut)
	AnimIdle = LoadSlabAnim(TEXT("A_IdleLook"));
	AnimPeck = LoadSlabAnim(TEXT("A_CrowPeck"));
	AnimHowl = LoadSlabAnim(TEXT("A_WolfHowl"));
	AnimDig = LoadSlabAnim(TEXT("A_BeastRooting"));
	AnimStagger = LoadSlabAnim(TEXT("A_RobotMalfunction"));
	AnimDeath = LoadSlabAnim(TEXT("A_DeathKneeling"));
	if (AnimIdle)
	{
		PlayAnim(AnimIdle, true);
	}
	else if (Roar)
	{
		// no idle clip: the first frame of the roar, held
		BodyMesh->PlayAnimation(Roar, false);
		BodyMesh->SetPosition(0.f, false);
		BodyMesh->Stop();
	}
	PatternTimer = 3.5f;

	// the sand grains (a particle pool) and the wall of the tsunami: instanced spheres, hidden until used
	if (UStaticMesh* Ball = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		const FTransform Hidden(FQuat::Identity, FVector(0.f, 0.f, -100000.f), FVector(0.01f));
		constexpr int32 kGrains = 4000;
		constexpr int32 kWaveGrains = 3200;
		GrainMesh->SetStaticMesh(Ball);
		GrainMesh->SetMaterial(0, ADuneBossCore::MakeGlow(this, FLinearColor(1.1f, 0.68f, 0.34f, 1.f)));
		WaveMesh->SetStaticMesh(Ball);
		WaveMesh->SetMaterial(0, ADuneBossCore::MakeGlow(this, FLinearColor(0.9f, 0.55f, 0.28f, 1.f)));
		for (int32 i = 0; i < kGrains; ++i) GrainMesh->AddInstance(Hidden, false);
		for (int32 i = 0; i < kWaveGrains; ++i) WaveMesh->AddInstance(Hidden, false);
		GrainData.SetNum(kGrains);
		GrainTransforms.Init(Hidden, kGrains);
		WaveGrains.SetNum(kWaveGrains);
		WaveTransforms.Init(Hidden, kWaveGrains);
	}
	// the dust sheets of the wave: vertical planes with the noisy dust material (world-space noise, so the clouds seem to boil as the wave runs through them)
	if (UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
	{
		if (UMaterialInterface* DustBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/DUNE/Boss/SlabBearer/M_SandStorm.M_SandStorm")))
		{
			CurtainMat = UMaterialInstanceDynamic::Create(DustBase, this);
			CurtainMat->SetScalarParameterValue(TEXT("Intensity"), 0.95f);
			CurtainMat->SetScalarParameterValue(TEXT("NoiseScale"), 0.0035f);
			CurtainMat->SetScalarParameterValue(TEXT("MaxHeight"), 2200.f);
			CurtainMat->SetVectorParameterValue(TEXT("Wind"), FLinearColor(-1.8f, 1.1f, 0.f, 0.f));
			CurtainMesh->SetMaterial(0, CurtainMat);
		}
		CurtainMesh->SetStaticMesh(Plane);
		constexpr int32 kPerSector = 14;
		const FTransform HiddenCurtain(FQuat::Identity, FVector(0.f, 0.f, -100000.f), FVector(0.01f));
		for (int32 Layer = 0; Layer < 2; ++Layer)
		{
			for (int32 S = 0; S < 6; ++S)
			{
				for (int32 j = 0; j < kPerSector; ++j)
				{
					FCurtain C;
					C.Sector = S;
					C.T = (j + 0.5f) / kPerSector;
					C.Layer = Layer;
					C.Height = FMath::FRandRange(900.f, 1500.f);
					C.Phase = FMath::FRandRange(0.f, 2.f * PI);
					C.Width = FMath::FRandRange(1.3f, 1.9f);
					Curtains.Add(C);
					CurtainMesh->AddInstance(HiddenCurtain, false);
				}
			}
		}
		CurtainTransforms.Init(HiddenCurtain, Curtains.Num());
	}
	// the floor marks: orange discs for the eruptions, green strips for the safe lanes of the tsunami
	for (int32 i = 0; i < SpotMarkers.Num(); ++i)
	{
		UMaterialInstanceDynamic* M = ADuneBossCore::MakeGlow(this, FLinearColor(10.f, 2.f, 0.3f, 1.f));
		SpotMarkers[i]->SetMaterial(0, M);
		SpotMats.Add(M);
	}
	for (UStaticMeshComponent* L : LaneMarkers) L->SetMaterial(0, ADuneBossCore::MakeGlow(this, FLinearColor(0.04f, 0.45f, 0.18f, 1.f)));

	// the slabs: two models, three on each shoulder (offsets in the boss's own axes: x forward, y right, z up; the face of a slab points along yaw Face)
	UStaticMesh* Models[2] =
	{
		LoadObject<UStaticMesh>(nullptr, TEXT("/Game/DUNE/Boss/Slab/SM_Slab1.SM_Slab1")),
		LoadObject<UStaticMesh>(nullptr, TEXT("/Game/DUNE/Boss/Slab/SM_Slab2.SM_Slab2")),
	};
	struct FSpec { FVector Offset; float Face; float Lean; int32 Model; };
	const FSpec Specs[3] =
	{
		{ FVector(-140.f, 540.f, 220.f), 70.f, 28.f, 1 },   // on top of the shoulder, leaning out (z is relative to the Spine2 bone, about 15 m up)
		{ FVector(260.f, 380.f, -140.f), 28.f, 10.f, 0 },    // in front of the shoulder
		{ FVector(-430.f, 260.f, 320.f), 165.f, -14.f, 0 },  // behind the neck
	};
	Slabs.Reset();
	for (int32 i = 0; i < SlabMeshes.Num(); ++i)
	{
		const float Side = (i % 2 == 0) ? 1.f : -1.f; // even: the boss's right, odd: its left (mirrored)
		const FSpec& Sp = Specs[i / 2];
		FSlabState S;
		S.RestOffset = FVector(Sp.Offset.X, Sp.Offset.Y * Side, Sp.Offset.Z);
		S.RestRot = FRotator(Sp.Lean * Side, Sp.Face * Side - 90.f, 0.f); // pitch = lean in the plane of the slab, yaw turns its face (the mesh face looks along +Y)
		Slabs.Add(S);
		const int32 Model = (Sp.Model + (Side > 0.f ? 0 : 1)) % 2;       // both models on both sides
		if (Models[Model]) SlabMeshes[i]->SetStaticMesh(Models[Model]);
		SlabMeshes[i]->SetRelativeLocation(FVector(0.f, 0.f, -95.f));    // the models stand on their lower edge: move the pivot to the middle
		SlabRoots[i]->SetRelativeScale3D(FVector(SlabScale));
		SlabRoots[i]->SetUsingAbsoluteLocation(true);
		SlabRoots[i]->SetUsingAbsoluteRotation(true);
		FVector P; FQuat Q;
		SlabRestTransform(Slabs[i], P, Q);
		Slabs[i].Pos = P;
		Slabs[i].Rot = Q;
		SlabRoots[i]->SetWorldLocationAndRotation(P, Q);
		SlabMats.Add(SlabMeshes[i]->CreateDynamicMaterialInstance(0));
		SlabGlows[i]->SetStaticMesh(SlabMeshes[i]->GetStaticMesh());
		SlabGlows[i]->SetMaterial(0, ADuneBossCore::MakeGlow(this, FLinearColor(6.f, 3.6f, 0.2f, 1.f)));
		SetParryLook(i, false);
		if (UStaticMesh* Disc = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"))) SlabMarkers[i]->SetStaticMesh(Disc);
		UMaterialInstanceDynamic* Mark = ADuneBossCore::MakeGlow(this, FLinearColor(10.f, 0.5f, 0.15f, 1.f));
		SlabMarkers[i]->SetMaterial(0, Mark);
		MarkerMats.Add(Mark);
	}
	UE_LOG(LogDuneSlab, Log, TEXT("Slab boss: health %.0f, mesh scale %.1f, roar %s"), MaxHealth, MeshScale, Roar ? *Roar->GetName() : TEXT("MISSING"));
}

FVector ADuneSlabBoss::GetProjectileOrigin() const
{
	return BodyMesh && BodyMesh->GetSkeletalMeshAsset() ? BodyMesh->GetBoneLocation(TEXT("Head")) : GetActorLocation() + FVector(0.f, 0.f, 1700.f);
}

void ADuneSlabBoss::OnPatternStarted()
{
	// (the Slab Bearer picks its own actions in ChoosePattern: nothing to do here)
}

void ADuneSlabBoss::OnPhase2Entered()
{
	Light->SetLightColor(FLinearColor(1.f, 0.08f, 0.04f)); // the arena turns red
	if (!IsDead()) { EndAction(0.f); StartAction(EAction::Transition); }
}

void ADuneSlabBoss::HandleDeath()
{
	ClearEruptions();
	ClearTsunami();
	Action = EAction::Dead;
	StartYaw = GetActorRotation().Yaw;
	StormHold = 0.f; // the storm dies with it
	Light->SetIntensity(LightIntensity * 0.15f);
	if (AnimDeath) PlayAnim(AnimDeath, false); // kneels and stays down
	else BodyMesh->Stop();
}

// ---------------------------------------------------------------- animations and actions

UAnimSequence* ADuneSlabBoss::LoadSlabAnim(const TCHAR* Name) const
{
	const FString Base = TEXT("/Game/DUNE/Boss/SlabBearer/Anims/Src/");
	for (const TCHAR* Suffix : { TEXT("_SlabArm"), TEXT("_Slab") })
	{
		const FString Path = FString::Printf(TEXT("%s%s%s.%s%s"), *Base, Name, Suffix, Name, Suffix);
		if (UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet)) return Seq;
	}
	UE_LOG(LogDuneSlab, Warning, TEXT("Slab boss: animation %s is missing"), Name);
	return nullptr;
}

void ADuneSlabBoss::PlayAnim(UAnimSequence* Seq, bool bLoop, float Rate)
{
	if (!Seq || !BodyMesh) return;
	BodyMesh->PlayAnimation(Seq, bLoop);
	BodyMesh->SetPlayRate(FMath::Max(Rate, 0.05f));
}

void ADuneSlabBoss::StartAction(EAction NewAction)
{
	ClearEruptions();
	Action = NewAction;
	ActionTime = 0.f;
	bVolleySent = false;
	PatternTimer = 1000.f; // nothing new starts until this one ends (EndAction sets the rest)
	if (NewAction == EAction::Volley || NewAction == EAction::Eruption || NewAction == EAction::Tsunami) LastAttack = NewAction;
	const float Speed = Phase == 1 ? 1.f : (Phase == 2 ? 1.15f : 1.3f);
	switch (NewAction)
	{
	case EAction::Volley:
		PlayAnim(AnimPeck, false, Speed);
		ActionLength = FMath::Max(3.f, AnimPeck ? AnimPeck->GetPlayLength() / Speed : 3.f);
		StormHold = FMath::Max(StormHold, ActionLength);
		break;
	case EAction::Eruption:
		PlayAnim(AnimDig, false, 1.f);
		ActionLength = AnimDig ? AnimDig->GetPlayLength() : 5.5f;
		StormHold = FMath::Max(StormHold, ActionLength);
		break;
	case EAction::Tsunami:
		PlayAnim(AnimHowl, false, 1.f);
		BuildTsunami();
		ActionLength = TsunamiWarnSeconds + (4700.f - 600.f) / FMath::Max(TsunamiSpeed, 100.f) + 0.4f;
		StormHold = FMath::Max(StormHold, ActionLength);
		break;
	case EAction::Stagger:
	{
		const float Rate = AnimStagger ? AnimStagger->GetPlayLength() / FMath::Max(ParryStaggerSeconds, 0.5f) : 1.f; // the clip plays out over the pause
		PlayAnim(AnimStagger, false, Rate);
		ActionLength = ParryStaggerSeconds;
		StormHold = 0.f;
		break;
	}
	case EAction::Transition:
		PlayAnim(Roar, false, 1.f);
		ActionLength = Roar ? Roar->GetPlayLength() : 3.f;
		StormHold = FMath::Max(StormHold, ActionLength + StormHoldAfterRoar);
		break;
	default:
		break;
	}
	static const TCHAR* Names[] = { TEXT("Idle"), TEXT("Volley"), TEXT("Eruption"), TEXT("Tsunami"), TEXT("Stagger"), TEXT("Transition"), TEXT("Dead") };
	UE_LOG(LogDuneSlab, Log, TEXT("Slab action: %s (phase %d, %.1f s)"), Names[static_cast<int32>(NewAction)], Phase, ActionLength);
}

void ADuneSlabBoss::EndAction(float Rest)
{
	ClearEruptions();
	if (Action == EAction::Dead) return;
	Action = EAction::Idle;
	if (AnimIdle) PlayAnim(AnimIdle, true);
	PatternTimer = Rest;
}

void ADuneSlabBoss::ChoosePattern()
{
	if (IsDead()) return;
	if (Action != EAction::Idle) { PatternTimer = 0.5f; return; }
	// phase 1: the slabs. phase 2: slabs or sand eruptions. phase 3: all three, the first one after the change is the tsunami. The last attack is less likely to repeat.
	float WVolley = 1.f, WEruption = 0.f, WTsunami = 0.f;
	if (Phase == 2) { WVolley = 1.f; WEruption = 1.f; }
	if (Phase >= 3) { WVolley = 1.f; WEruption = 1.f; WTsunami = 1.3f; }
	if (LastAttack == EAction::Volley) WVolley *= 0.35f;
	if (LastAttack == EAction::Eruption) WEruption *= 0.35f;
	if (LastAttack == EAction::Tsunami) WTsunami *= 0.2f;
	if (Phase >= 3 && TsunamiCount == 0) { WVolley = 0.f; WEruption = 0.f; WTsunami = 1.f; }
	const float R = FMath::FRand() * (WVolley + WEruption + WTsunami);
	EAction Pick = EAction::Volley;
	if (R < WVolley) Pick = EAction::Volley;
	else if (R < WVolley + WEruption) Pick = EAction::Eruption;
	else Pick = EAction::Tsunami;
	StartAction(Pick);
}

void ADuneSlabBoss::DebugAction(int32 Which)
{
	if (IsDead()) return;
	EndAction(0.f);
	switch (Which)
	{
	case 0: StartAction(EAction::Volley); break;
	case 1: StartAction(EAction::Eruption); break;
	case 2: StartAction(EAction::Tsunami); break;
	default: StartAction(EAction::Stagger); break;
	}
}

void ADuneSlabBoss::TickAction(float DeltaSeconds)
{
	if (IsDead() || Action == EAction::Dead) return;
	ActionTime += DeltaSeconds;
	switch (Action)
	{
	case EAction::Volley:
		if (!bVolleySent && ActionTime >= 1.0f) { bVolleySent = true; DoBarrage(); }
		if (ActionTime >= ActionLength) EndAction(RestSeconds());
		break;
	case EAction::Eruption:
		if (!bVolleySent && ActionTime >= 1.0f) { bVolleySent = true; BuildEruptions(); }
		TickEruptions();
		if (ActionTime >= ActionLength) EndAction(RestSeconds());
		break;
	case EAction::Tsunami:
		if (!bVolleySent && ActionTime >= TsunamiWarnSeconds)
		{
			bVolleySent = true;
			bTsunamiRunning = true;
			bTsunamiHitPlayer = false;
			TsunamiRadius = 600.f;
			++TsunamiCount;
			UE_LOG(LogDuneSlab, Log, TEXT("Slab: the tsunami starts"));
		}
		if (ActionTime >= ActionLength && !bTsunamiRunning) EndAction(RestSeconds());
		break;
	case EAction::Stagger:
		if (ActionTime >= ActionLength) EndAction(1.5f);
		break;
	case EAction::Transition:
		if (ActionTime >= ActionLength) EndAction(1.5f);
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------- movement and the barrier

void ADuneSlabBoss::TickMovement(float DeltaSeconds)
{
	if (IsDead() || Action == EAction::Dead) return;
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player) return;
	const FVector2D To(Player->GetActorLocation().X - BaseLocation.X, Player->GetActorLocation().Y - BaseLocation.Y);
	const float Dist = To.Size();
	if (Dist < 1.f) return;
	// turn towards the player (slower in the pause after a parry)
	const float TurnRate = (Phase == 1 ? TurnDegPerSec1 : (Phase == 2 ? TurnDegPerSec2 : TurnDegPerSec3)) * (Action == EAction::Stagger ? 0.25f : 1.f);
	const float TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X));
	SetActorRotation(FRotator(0.f, FMath::FixedTurn(GetActorRotation().Yaw, TargetYaw, TurnRate * DeltaSeconds), 0.f));
	// glide closer while the player is far away (a robed boss without legs: it slides, the sand streams behind it)
	if (bGlideTowardPlayer && Dist > MoveFarCm && Action != EAction::Stagger)
	{
		const float Speed = Phase == 1 ? GlideSpeed1 : (Phase == 2 ? GlideSpeed2 : GlideSpeed3);
		const FVector2D Dir = To / Dist;
		BaseLocation += FVector(Dir.X, Dir.Y, 0.f) * Speed * DeltaSeconds;
		const FVector2D Flat(BaseLocation.X, BaseLocation.Y);
		if (Flat.Size() > RoamRadiusCm) { const FVector2D Clamped = Flat / Flat.Size() * RoamRadiusCm; BaseLocation.X = Clamped.X; BaseLocation.Y = Clamped.Y; }
		GlideDustTimer -= DeltaSeconds;
		if (GlideDustTimer <= 0.f)
		{
			GlideDustTimer = 0.06f;
			SpawnGrains(FVector(BaseLocation.X, BaseLocation.Y, GroundZ + 20.f), 7, 450.f, 150.f, 380.f, 0.25f, 1.0f, 30.f, FVector(-Dir.X, -Dir.Y, 0.f) * 260.f);
		}
	}
}

void ADuneSlabBoss::TickBarrier(float DeltaSeconds)
{
	ACharacter* Player = Cast<ACharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	const bool bAlive = Player && !IsDead();
	const float Dist = bAlive ? FVector::Dist2D(Player->GetActorLocation(), GetActorLocation()) : 100000.f;
	const float Target = bAlive ? FMath::Clamp((BarrierOuterCm - Dist) / FMath::Max(BarrierOuterCm - BarrierInnerCm, 1.f), 0.f, 1.f) : 0.f;
	Veil = FMath::FInterpTo(Veil, Target, DeltaSeconds, Target > Veil ? 4.f : 2.f);
	if (!bAlive) return;
	if (Veil > 0.12f)
	{
		// the sand blown into the player's face
		SpawnGrains(Player->GetActorLocation() + FVector(0.f, 0.f, 50.f), FMath::CeilToInt(5.f * Veil), 300.f, 350.f, 900.f, 0.1f, 0.9f, 16.f, FVector(-1.4f, 0.9f, 0.f) * 700.f);
	}
	ContactTimer = FMath::Max(0.f, ContactTimer - DeltaSeconds);
	if (Dist <= ContactRadiusCm && ContactTimer <= 0.f && Player->GetActorLocation().Z < GroundZ + 1500.f)
	{
		// touching the body: hurt and thrown back
		ContactTimer = ContactInterval;
		++ContactHits;
		const FVector Out = FVector(Player->GetActorLocation().X - GetActorLocation().X, Player->GetActorLocation().Y - GetActorLocation().Y, 0.f).GetSafeNormal();
		UGameplayStatics::ApplyDamage(Player, ContactDamage, nullptr, this, UDamageType::StaticClass());
		Player->LaunchCharacter(Out * 1500.f + FVector(0.f, 0.f, 450.f), true, true);
		SpawnGrains(Player->GetActorLocation(), 60, 150.f, 500.f, 1400.f, 0.5f, 1.2f, 24.f);
		if (const ADunePlayerCharacter* DunePlayer = Cast<ADunePlayerCharacter>(Player)) { if (UDuneGunFeel* Feel = DunePlayer->GetGunFeel()) Feel->AddTrauma(0.7f); }
		UE_LOG(LogDuneSlab, Log, TEXT("Slab: the player touches the body (%.0f cm): contact damage %.0f"), Dist, ContactDamage);
	}
}

// ---------------------------------------------------------------- sand grains (the particle pool)

void ADuneSlabBoss::SpawnGrains(const FVector& Center, int32 Count, float RadiusCm, float MinSpeed, float MaxSpeed, float UpBias, float Life, float SizeCm, const FVector& Drift)
{
	if (GrainData.Num() == 0) return;
	// real sand is fine: a call asks for a size and a count, the pool draws about twice as many grains at under half the size, stretched along their flight (TickGrains)
	SizeCm *= 0.32f;
	Count = FMath::CeilToInt(Count * 2.6f);
	for (int32 k = 0; k < Count; ++k)
	{
		FGrain& G = GrainData[GrainCursor];
		GrainCursor = (GrainCursor + 1) % GrainData.Num();
		const float A = FMath::FRandRange(0.f, 2.f * PI);
		const float R = RadiusCm * FMath::Sqrt(FMath::FRand());
		G.Pos = Center + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, FMath::FRandRange(0.f, 30.f));
		const float B = FMath::FRandRange(0.f, 2.f * PI);
		const float Speed = FMath::FRandRange(MinSpeed, MaxSpeed);
		G.Vel = FVector(FMath::Cos(B) * (1.f - UpBias), FMath::Sin(B) * (1.f - UpBias), UpBias * FMath::FRandRange(0.6f, 1.2f)) * Speed + Drift;
		G.MaxLife = G.Life = Life * FMath::FRandRange(0.7f, 1.2f);
		G.Size = SizeCm * FMath::FRandRange(0.6f, 1.4f);
	}
}

void ADuneSlabBoss::TickGrains(float DeltaSeconds)
{
	if (!GrainMesh || GrainData.Num() == 0) return;
	bool bAny = false;
	const FTransform Hidden(FQuat::Identity, FVector(0.f, 0.f, -100000.f), FVector(0.01f));
	for (int32 i = 0; i < GrainData.Num(); ++i)
	{
		FGrain& G = GrainData[i];
		if (G.Life <= 0.f)
		{
			if (!GrainTransforms[i].Equals(Hidden)) { GrainTransforms[i] = Hidden; bAny = true; }
			continue;
		}
		bAny = true;
		G.Life -= DeltaSeconds;
		G.Vel.Z -= 900.f * DeltaSeconds;
		G.Vel *= FMath::Max(0.f, 1.f - 0.7f * DeltaSeconds);
		G.Pos += G.Vel * DeltaSeconds;
		if (G.Pos.Z < GroundZ + 6.f) { G.Pos.Z = GroundZ + 6.f; G.Vel = FVector(G.Vel.X * 0.4f, G.Vel.Y * 0.4f, 0.f); }
		const float A = FMath::Clamp(G.Life / G.MaxLife, 0.f, 1.f);
		const float Fade = G.Size / 100.f * (0.25f + 0.75f * FMath::Sqrt(A));
		const float Stretch = 1.f + FMath::Min(G.Vel.Size() / 200.f, 8.f); // a grain in flight is a streak along its velocity
		GrainTransforms[i] = FTransform(FRotationMatrix::MakeFromX(G.Vel.IsNearlyZero() ? FVector::UpVector : G.Vel).ToQuat(), G.Pos, FVector(Fade * Stretch, Fade * 0.7f, Fade * 0.7f));
	}
	if (bAny) GrainMesh->BatchUpdateInstancesTransforms(0, GrainTransforms, true, true, true);
}

// ---------------------------------------------------------------- sand eruptions

void ADuneSlabBoss::BuildEruptions()
{
	Spots.Reset();
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player) return;
	const int32 N = FMath::Min(Phase >= 3 ? EruptionsPhase3 : EruptionsPhase2, SpotMarkers.Num());
	const FVector P = Player->GetActorLocation();
	const FVector V = Player->GetVelocity();
	for (int32 k = 0; k < N; ++k)
	{
		FVector Pos;
		if (k == 0) Pos = P + FVector(V.X, V.Y, 0.f) * 0.6f; // the first one leads the player's run
		else
		{
			const float A = FMath::FRandRange(0.f, 2.f * PI);
			const float R = FMath::FRandRange(350.f, 1100.f);
			Pos = P + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 0.f);
		}
		FVector2D Out(Pos.X - BaseLocation.X, Pos.Y - BaseLocation.Y);
		if (Out.Size() < 900.f) { Out = Out.GetSafeNormal() * 900.f; Pos.X = BaseLocation.X + Out.X; Pos.Y = BaseLocation.Y + Out.Y; }
		const FVector2D Flat(Pos.X, Pos.Y);
		if (Flat.Size() > 4200.f) { const FVector2D C = Flat / Flat.Size() * 4200.f; Pos.X = C.X; Pos.Y = C.Y; }
		FEruptSpot S;
		S.Pos = FVector(Pos.X, Pos.Y, GroundZ);
		S.Time = 1.0f + EruptionWarnSeconds + k * EruptionStagger;
		Spots.Add(S);
	}
	++EruptionCount;
	UE_LOG(LogDuneSlab, Log, TEXT("Slab: %d sand eruptions marked"), Spots.Num());
}

void ADuneSlabBoss::TickEruptions()
{
	for (int32 i = 0; i < Spots.Num(); ++i)
	{
		FEruptSpot& S = Spots[i];
		UStaticMeshComponent* Mark = SpotMarkers.IsValidIndex(i) ? SpotMarkers[i].Get() : nullptr;
		if (S.bDone) { if (Mark) Mark->SetVisibility(false); continue; }
		const float Progress = FMath::Clamp(1.f - (S.Time - ActionTime) / FMath::Max(EruptionWarnSeconds, 0.1f), 0.f, 1.f);
		if (Mark)
		{
			const float Pulse = 1.f + 0.08f * FMath::Sin(GetGameTimeSinceCreation() * (8.f + 14.f * Progress) + i);
			const float D = EruptionRadiusCm * 2.f / 100.f * Pulse * (0.7f + 0.3f * Progress);
			Mark->SetVisibility(true);
			Mark->SetWorldLocation(FVector(S.Pos.X, S.Pos.Y, GroundZ + 7.f));
			Mark->SetWorldScale3D(FVector(D, D, 0.03f));
			if (SpotMats.IsValidIndex(i) && SpotMats[i]) SpotMats[i]->SetVectorParameterValue(TEXT("Glow"), FMath::Lerp(FLinearColor(10.f, 2.f, 0.3f, 1.f), FLinearColor(16.f, 0.3f, 0.15f, 1.f), Progress));
		}
		if (ActionTime >= S.Time)
		{
			S.bDone = true;
			if (Mark) Mark->SetVisibility(false);
			Erupt(S.Pos);
		}
	}
}

void ADuneSlabBoss::ClearEruptions()
{
	Spots.Reset();
	for (UStaticMeshComponent* M : SpotMarkers) { if (M) M->SetVisibility(false); }
}

void ADuneSlabBoss::Erupt(const FVector& Pos)
{
	// the sand bursts up: a soft crater, lumps round it, a column of grains; the player on the mark is hurt and thrown up
	USandDeformationLibrary::ApplySandImpact(this, Pos, 250.f, 1000.f, 2.f);
	for (int32 i = 0; i < 3; ++i)
	{
		const float A = FMath::FRandRange(0.f, 2.f * PI);
		USandDeformationLibrary::ApplySandImpact(this, Pos + FVector(FMath::Cos(A) * 330.f, FMath::Sin(A) * 330.f, 0.f), 110.f, 500.f, 1.f);
	}
	SpawnGrains(Pos + FVector(0.f, 0.f, 20.f), 110, 210.f, 700.f, 1900.f, 0.88f, 1.8f, 28.f);
	if (ACharacter* Player = Cast<ACharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		const float D = FVector::Dist2D(Player->GetActorLocation(), Pos);
		if (D < EruptionRadiusCm + 40.f)
		{
			++EruptionHits;
			UGameplayStatics::ApplyDamage(Player, EruptionDamage, nullptr, this, UDamageType::StaticClass());
			Player->LaunchCharacter(FVector(0.f, 0.f, 1100.f), false, true);
			UE_LOG(LogDuneSlab, Log, TEXT("Slab: an eruption hits the player (%.0f cm from its centre)"), D);
		}
		if (D < 2500.f)
		{
			if (const ADunePlayerCharacter* DunePlayer = Cast<ADunePlayerCharacter>(Player)) { if (UDuneGunFeel* Feel = DunePlayer->GetGunFeel()) Feel->AddTrauma(FMath::Clamp(1.f - D / 2500.f, 0.1f, 0.7f)); }
		}
	}
}

// ---------------------------------------------------------------- the sand tsunami

bool ADuneSlabBoss::IsInLane(const FVector& Location) const
{
	const FVector2D D(Location.X - BaseLocation.X, Location.Y - BaseLocation.Y);
	const float Dist = D.Size();
	if (Dist < 1.f) return true;
	const float Ang = FMath::Atan2(D.Y, D.X);
	for (const float Lane : LaneAngles)
	{
		const float Delta = FMath::Abs(FMath::UnwindRadians(Ang - Lane));
		if (Delta < PI * 0.5f && Dist * FMath::Sin(Delta) <= LaneWidthCm * 0.5f - 30.f) return true;
	}
	return false;
}

void ADuneSlabBoss::BuildTsunami()
{
	LaneAngles.Reset();
	const float A0 = FMath::FRandRange(0.f, 2.f * PI);
	for (int32 k = 0; k < 6; ++k) LaneAngles.Add(A0 + k * PI / 3.f);
	for (int32 k = 0; k < LaneMarkers.Num() && k < 6; ++k)
	{
		const FVector Dir(FMath::Cos(LaneAngles[k]), FMath::Sin(LaneAngles[k]), 0.f);
		LaneMarkers[k]->SetWorldLocationAndRotation(FVector(BaseLocation.X, BaseLocation.Y, GroundZ + 8.f) + Dir * 2700.f, FRotator(0.f, FMath::RadiansToDegrees(LaneAngles[k]), 0.f));
		LaneMarkers[k]->SetWorldScale3D(FVector(40.f, LaneWidthCm / 100.f, 0.03f));
		LaneMarkers[k]->SetVisibility(true);
	}
	for (int32 i = 0; i < WaveGrains.Num(); ++i)
	{
		FWaveGrain& G = WaveGrains[i];
		G.Sector = i % 6;
		G.T = FMath::FRand();
		G.H = FMath::Pow(FMath::FRand(), 0.9f);
		G.DR = FMath::FRandRange(-260.f, 80.f);
		G.Size = FMath::FRandRange(7.f, 20.f); // fine grains, stretched along their flight in the tick: the dust sheets carry the body of the wave
		G.Phase = FMath::FRandRange(0.f, 2.f * PI);
	}
	bTsunamiRunning = false;
	bTsunamiHitPlayer = false;
	TsunamiRadius = 0.f;
}

void ADuneSlabBoss::ClearTsunami()
{
	bTsunamiRunning = false;
	for (UStaticMeshComponent* L : LaneMarkers) { if (L) L->SetVisibility(false); }
}

void ADuneSlabBoss::TickTsunami(float DeltaSeconds)
{
	const bool bShowLanes = bTsunamiRunning || Action == EAction::Tsunami;
	for (UStaticMeshComponent* L : LaneMarkers) { if (L && L->IsVisible() != bShowLanes) L->SetVisibility(bShowLanes); }
	if (!WaveMesh || WaveGrains.Num() == 0) return;
	const FTransform Hidden(FQuat::Identity, FVector(0.f, 0.f, -100000.f), FVector(0.01f));
	if (!bTsunamiRunning)
	{
		if (!WaveTransforms[0].Equals(Hidden))
		{
			for (FTransform& T : WaveTransforms) T = Hidden;
			WaveMesh->BatchUpdateInstancesTransforms(0, WaveTransforms, true, true, true);
			for (FTransform& T : CurtainTransforms) T = Hidden;
			if (CurtainMesh && CurtainTransforms.Num() > 0) CurtainMesh->BatchUpdateInstancesTransforms(0, CurtainTransforms, true, true, true);
		}
		return;
	}

	TsunamiRadius += TsunamiSpeed * DeltaSeconds;
	const float Now = GetGameTimeSinceCreation();
	const float HeightScale = FMath::Clamp((TsunamiRadius - 500.f) / 700.f, 0.2f, 1.f);
	auto AngularGap = [this](float Rg) { return FMath::Asin(FMath::Clamp(LaneWidthCm * 0.5f / FMath::Max(Rg, 1.f), 0.f, 0.95f)); };

	// the dust sheets: two layers of vertical planes along every stretch between two lanes, the top leaning forward (the crest), swaying
	for (int32 i = 0; i < Curtains.Num(); ++i)
	{
		const FCurtain& C = Curtains[i];
		const float Rg = TsunamiRadius + (C.Layer == 0 ? 0.f : -190.f) + FMath::Sin(Now * 1.7f + C.Phase) * 45.f;
		const float Alpha = AngularGap(Rg);
		const float A0 = LaneAngles[C.Sector] + Alpha;
		const float A1 = LaneAngles[C.Sector] + PI / 3.f - Alpha;
		if (A1 - A0 < 0.06f) { CurtainTransforms[i] = Hidden; continue; } // near the boss the lanes overlap: no wall yet
		const float Ang = FMath::Lerp(A0, A1, C.T);
		const FVector Radial(FMath::Cos(Ang), FMath::Sin(Ang), 0.f);
		const FVector Tangent(-FMath::Sin(Ang), FMath::Cos(Ang), 0.f);
		const float H = C.Height * HeightScale * (TsunamiHeightCm / 1100.f) * (0.45f + 0.55f * FMath::Sin(PI * C.T)); // lower towards the lanes: the ends of a real wave taper
		const float Width = (A1 - A0) * Rg / 14.f * 1.7f * C.Width; // (the sheets of a stretch overlap)
		const FQuat Basis = FMatrix(Tangent, FVector::UpVector, Radial, FVector::ZeroVector).ToQuat();
		const float Lean = FMath::DegreesToRadians((C.Layer == 0 ? 22.f : 34.f) + 6.f * FMath::Sin(Now * 2.1f + C.Phase));
		const FQuat Q = FQuat(Tangent, Lean) * Basis;
		const FVector Pos(BaseLocation.X + Radial.X * Rg, BaseLocation.Y + Radial.Y * Rg, GroundZ + H * 0.5f * FMath::Cos(Lean));
		CurtainTransforms[i] = FTransform(Q, Pos + Radial * (H * 0.5f * FMath::Sin(Lean)), FVector(FMath::Min(Width, 1600.f) / 100.f, H / 100.f, 1.f));
	}
	if (CurtainMesh && CurtainTransforms.Num() > 0) CurtainMesh->BatchUpdateInstancesTransforms(0, CurtainTransforms, true, true, true);

	// the fine grains: thousands of small streaks in the wall, flying with it, flickering in and out
	for (int32 i = 0; i < WaveGrains.Num(); ++i)
	{
		const FWaveGrain& G = WaveGrains[i];
		const float Rg = TsunamiRadius + G.DR + G.H * G.H * 360.f + FMath::Sin(Now * 3.1f + G.Phase) * 40.f; // the crest leans forward
		const float Alpha = AngularGap(Rg);
		const float A0 = LaneAngles[G.Sector] + Alpha;
		const float A1 = LaneAngles[G.Sector] + PI / 3.f - Alpha;
		if (A1 - A0 < 0.06f) { WaveTransforms[i] = Hidden; continue; }
		const float Ang = FMath::Lerp(A0, A1, G.T);
		const FVector Radial(FMath::Cos(Ang), FMath::Sin(Ang), 0.f);
		const FVector Pos(BaseLocation.X + Radial.X * Rg, BaseLocation.Y + Radial.Y * Rg, GroundZ + 30.f + G.H * TsunamiHeightCm * HeightScale * 1.05f * (0.45f + 0.55f * FMath::Sin(PI * G.T)) + FMath::Sin(Now * 2.3f + G.Phase * 1.7f) * 35.f);
		const float Flicker = 0.55f + 0.45f * FMath::Sin(Now * 5.f + G.Phase * 3.f);
		const float S = G.Size / 100.f * Flicker;
		WaveTransforms[i] = FTransform(FRotationMatrix::MakeFromX(Radial + FVector(0.f, 0.f, 0.35f)).ToQuat(), Pos, FVector(S * 3.5f, S, S));
	}
	WaveMesh->BatchUpdateInstancesTransforms(0, WaveTransforms, true, true, true);

	// loose grains thrown off the crest
	{
		const float Ang = LaneAngles[FMath::RandRange(0, 5)] + PI / 6.f + FMath::FRandRange(-0.4f, 0.4f);
		SpawnGrains(FVector(BaseLocation.X + FMath::Cos(Ang) * TsunamiRadius, BaseLocation.Y + FMath::Sin(Ang) * TsunamiRadius, GroundZ + 250.f), 6, 150.f, 300.f, 900.f, 0.45f, 1.4f, 26.f, FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * 650.f);
	}

	ACharacter* Player = Cast<ACharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (Player)
	{
		const float D = FVector::Dist2D(Player->GetActorLocation(), GetActorLocation());
		if (!bTsunamiHitPlayer && D <= TsunamiRadius + 60.f)
		{
			bTsunamiHitPlayer = true; // decided once, when the front reaches the player
			if (!IsInLane(Player->GetActorLocation()))
			{
				++TsunamiHits;
				const FVector Out = FVector(Player->GetActorLocation().X - BaseLocation.X, Player->GetActorLocation().Y - BaseLocation.Y, 0.f).GetSafeNormal();
				UGameplayStatics::ApplyDamage(Player, TsunamiDamage, nullptr, this, UDamageType::StaticClass());
				Player->LaunchCharacter(Out * 1100.f + FVector(0.f, 0.f, 800.f), true, true);
				SpawnGrains(Player->GetActorLocation(), 90, 200.f, 500.f, 1500.f, 0.4f, 1.4f, 26.f, Out * 600.f);
				if (const ADunePlayerCharacter* DunePlayer = Cast<ADunePlayerCharacter>(Player)) { if (UDuneGunFeel* Feel = DunePlayer->GetGunFeel()) Feel->AddTrauma(0.9f); }
				UE_LOG(LogDuneSlab, Log, TEXT("Slab: the tsunami hits the player (outside the lanes, %.0f cm)"), D);
			}
			else
			{
				UE_LOG(LogDuneSlab, Log, TEXT("Slab: the player stands in a safe lane when the tsunami passes (%.0f cm)"), D);
			}
		}
		// the wave ploughs the sand near the player
		TsunamiSandTimer -= DeltaSeconds;
		if (TsunamiSandTimer <= 0.f)
		{
			TsunamiSandTimer = 0.08f;
			StampRipple(TsunamiRadius, Player->GetActorLocation(), 260.f, 9.f, 1.5f);
		}
	}
	if (TsunamiRadius > 4700.f) bTsunamiRunning = false;
}

FVector ADuneSlabBoss::SlabAnchor() const
{
	return BodyMesh && BodyMesh->GetSkeletalMeshAsset() ? BodyMesh->GetBoneLocation(TEXT("Spine2")) : GetActorLocation() + FVector(0.f, 0.f, 1400.f);
}

void ADuneSlabBoss::SlabRestTransform(const FSlabState& S, FVector& OutPos, FQuat& OutRot) const
{
	OutPos = SlabAnchor() + GetActorQuat().RotateVector(S.RestOffset);
	OutRot = GetActorQuat() * S.RestRot.Quaternion();
}

void ADuneSlabBoss::SetParryLook(int32 Index, bool bOn)
{
	if (SlabGlows.IsValidIndex(Index)) SlabGlows[Index]->SetVisibility(bOn);
	if (SlabMats.IsValidIndex(Index) && SlabMats[Index])
	{
		SlabMats[Index]->SetVectorParameterValue(TEXT("GlowColor"), FLinearColor(1.f, 0.72f, 0.05f, 1.f));
		SlabMats[Index]->SetScalarParameterValue(TEXT("GlowIntensity"), bOn ? 14.f : 0.f);
	}
}

void ADuneSlabBoss::DoBarrage()
{
	// the attack: slabs leave the shoulders one after the other; one of them (random) has a yellow rim and can be parried with a shot
	++BarrageCount;
	const int32 Want = Phase == 1 ? SlabsPerAttack1 : (Phase == 2 ? SlabsPerAttack2 : SlabsPerAttack3);
	TArray<int32> Ready;
	for (int32 i = 0; i < Slabs.Num(); ++i)
	{
		if (Slabs[i].State == ESlabState::Attached) Ready.Add(i);
	}
	for (int32 i = Ready.Num() - 1; i > 0; --i) Ready.Swap(i, FMath::RandRange(0, i));
	const int32 Sent = FMath::Min(Want, Ready.Num());
	const int32 ParryK = Sent >= 2 ? FMath::RandRange(0, Sent - 1) : -1;
	for (int32 k = 0; k < Sent; ++k)
	{
		FSlabState& S = Slabs[Ready[k]];
		S.State = ESlabState::Windup;
		S.Delay = k * SlabStagger;
		S.Timer = -S.Delay; // negative until its turn
		S.bParry = (k == ParryK);
		SetParryLook(Ready[k], S.bParry);
		++OrbsFired;
	}
}

bool ADuneSlabBoss::QueryParry(const FVector& Eye, const FVector& Dir, float& OutDistance, bool& bOutInRange) const
{
	for (const FSlabState& S : Slabs)
	{
		if (!S.bParry || S.State != ESlabState::Flying) continue;
		const FVector ToSlab = S.Pos - Eye;
		if (FVector::DotProduct(ToSlab, Dir) <= 0.f) continue;
		if (FMath::PointDistToLine(S.Pos, Dir, Eye) < 140.f * SlabScale)
		{
			OutDistance = ToSlab.Size();
			bOutInRange = OutDistance >= ParryMinDistance;
			return true;
		}
	}
	return false;
}

void ADuneSlabBoss::TryParry(int32 Index)
{
	if (!Slabs.IsValidIndex(Index)) return;
	FSlabState& S = Slabs[Index];
	if (!S.bParry || S.State != ESlabState::Flying) return;
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	const float Dist = Player ? FVector::Dist(Player->GetActorLocation(), S.Pos) : 0.f;
	if (Dist < ParryMinDistance)
	{
		UE_LOG(LogDuneSlab, Log, TEXT("Slab %d: shot too close (%.0f cm) to parry"), Index, Dist);
		return;
	}
	S.State = ESlabState::Reflected;
	if (SlabHitboxes.IsValidIndex(Index)) SlabHitboxes[Index]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	++ParrySuccesses;
	UE_LOG(LogDuneSlab, Log, TEXT("Slab %d parried from %.0f cm"), Index, Dist);
	if (ADunePlayerCharacter* DunePlayer = Cast<ADunePlayerCharacter>(Player))
	{
		if (UDuneGunFeel* Feel = DunePlayer->GetGunFeel())
		{
			Feel->OnFracture(); // the beat of the parry: a short stop, a jolt, a wider view
			Feel->AddTrauma(0.4f);
		}
	}
}

float ADuneSlabBoss::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// a shot that hits a slab hit box is a parry attempt, not damage; the boss's own parry damage (causer = itself) goes through
	if (DamageCauser != this && DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const UPrimitiveComponent* Hit = static_cast<const FPointDamageEvent&>(DamageEvent).HitInfo.GetComponent();
		for (int32 i = 0; i < SlabHitboxes.Num(); ++i)
		{
			if (SlabHitboxes[i] == Hit)
			{
				TryParry(i);
				return 0.f;
			}
		}
	}
	return Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
}

void ADuneSlabBoss::SlabImpact(int32 Index, bool bHitPlayer)
{
	FSlabState& S = Slabs[Index];
	const FVector Hit(S.Pos.X, S.Pos.Y, GroundZ);
	const FVector Flat = FVector(S.Dir.X, S.Dir.Y, 0.f).GetSafeNormal();
	LastSlabHitPos = Hit; LastSlabHitDir = Flat;
	const float Mult = S.bParry ? FailMultiplier : 1.f; // the parry slab that was not parried hits harder
	if (SlabHitboxes.IsValidIndex(Index)) SlabHitboxes[Index]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (S.bParry) ++ParryFailures;

	// the mark in the sand follows the slab: it lands with its cross-section (a box as wide as the slab, turned across its flight), digs a trench of the
	// same width where it skidded, and throws the sand ahead in a few thin streaks (the marks used to be round craters and round lumps, which looked like a ball)
	FVector2D Half(110.f, 40.f); // the slab's cross-section (half sizes), measured from its mesh
	if (SlabMeshes.IsValidIndex(Index) && SlabMeshes[Index] && SlabMeshes[Index]->GetStaticMesh())
	{
		const FVector Ext = SlabMeshes[Index]->GetStaticMesh()->GetBounds().BoxExtent * SlabMeshes[Index]->GetComponentScale();
		Half = FVector2D(FMath::Max(Ext.X, Ext.Y), FMath::Min(Ext.X, Ext.Y));
	}
	const float ParryScale = S.bParry ? 1.2f : 1.f;
	{
		FSandInteractionRequest Slam;
		Slam.Brush = ESandBrushType::Impact;
		Slam.Shape = ESandContactShape::Box;
		Slam.WorldPosition = Hit;
		Slam.OrientationYawDeg = Flat.Rotation().Yaw + 90.f;
		Slam.Extent = Half * ParryScale;
		Slam.Radius = Half.X * ParryScale;
		Slam.ImpactSpeed = 1500.f;
		Slam.Strength = 3.f;
		Slam.RidgeScale = 2.f;
		Slam.bAccumulate = true;
		USandDeformationLibrary::ApplySandInteraction(this, Slam);
	}
	const FVector From = Hit - Flat * 900.f;
	constexpr int32 Segments = 6;
	for (int32 i = 0; i < Segments; ++i)
	{
		FSandInteractionRequest Skid;
		Skid.Brush = ESandBrushType::Wheel;
		Skid.PreviousPosition = FMath::Lerp(From, Hit, static_cast<float>(i) / Segments);
		Skid.WorldPosition = FMath::Lerp(From, Hit, static_cast<float>(i + 1) / Segments);
		Skid.bHasPreviousPosition = true;
		Skid.MovementDirection = Skid.WorldPosition - Skid.PreviousPosition;
		Skid.Radius = Half.X * 0.9f * ParryScale; // the trench is as wide as the slab
		Skid.DepthOverrideCm = 12.f;
		Skid.RidgeScale = 2.f;
		USandDeformationLibrary::ApplySandInteraction(this, Skid);
	}
	for (int32 i = 0; i < 5; ++i)
	{
		// sand thrown ahead: a thin streak fanned out in front of the slab
		const FVector Fan = Flat.RotateAngleAxis(FMath::FRandRange(-40.f, 40.f), FVector::UpVector);
		FSandInteractionRequest Streak;
		Streak.Brush = ESandBrushType::Wheel;
		Streak.PreviousPosition = Hit + Fan * (Half.X + 60.f);
		Streak.WorldPosition = Hit + Fan * FMath::FRandRange(450.f, 900.f);
		Streak.bHasPreviousPosition = true;
		Streak.MovementDirection = Streak.WorldPosition - Streak.PreviousPosition;
		Streak.Radius = FMath::FRandRange(18.f, 32.f);
		Streak.DepthOverrideCm = 4.f;
		Streak.RidgeScale = 1.5f;
		USandDeformationLibrary::ApplySandInteraction(this, Streak);
	}
	UE_LOG(LogTemp, Log, TEXT("SlabBoss: slab %d landed at %s, mark %.0f x %.0f cm"), Index, *Hit.ToCompactString(), Half.X * 2.f, Half.Y * 2.f);
	SpawnGrains(Hit + FVector(0.f, 0.f, 30.f), 60, 250.f, 400.f, 1300.f, 0.7f, 1.4f, 24.f); // the sand thrown up by the landing

	// the quake: a player standing on the floor is thrown up (the closer, the higher), the camera shakes; a direct hit also hurts
	if (ACharacter* Player = Cast<ACharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		const float D = FVector::Dist2D(Player->GetActorLocation(), Hit);
		if (D < QuakeRadiusCm)
		{
			const float Strength = 1.f - D / QuakeRadiusCm;
			if (Player->GetCharacterMovement()->IsMovingOnGround())
			{
				Player->LaunchCharacter(FVector(0.f, 0.f, QuakeLaunchSpeed * Mult * (0.55f + 0.45f * Strength)), false, true);
			}
			if (ADunePlayerCharacter* DunePlayer = Cast<ADunePlayerCharacter>(Player))
			{
				if (UDuneGunFeel* Feel = DunePlayer->GetGunFeel()) Feel->AddTrauma(FMath::Min(1.f, (0.5f + 0.5f * Strength) * Mult));
			}
			UE_LOG(LogDuneSlab, Log, TEXT("Slab %d lands %.0f cm from the player: quake %.2f%s (hit player %d)"), Index, D, Strength, S.bParry ? TEXT(" (parry slab, not parried)") : TEXT(""), bHitPlayer ? 1 : 0);
		}
		if (bHitPlayer) UGameplayStatics::ApplyDamage(Player, SlabDamage * Mult, nullptr, this, UDamageType::StaticClass());
	}
	++SlabLandings;

	// it stays stuck in the sand for a while, then goes back to the shoulder
	S.State = ESlabState::Embedded;
	S.Timer = 0.f;
	S.Pos = FVector(S.Pos.X, S.Pos.Y, GroundZ + 150.f);
}

void ADuneSlabBoss::TickSlabs(float DeltaSeconds)
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	for (int32 i = 0; i < Slabs.Num(); ++i)
	{
		FSlabState& S = Slabs[i];
		FVector RestPos; FQuat RestQ;
		SlabRestTransform(S, RestPos, RestQ);
		switch (S.State)
		{
		case ESlabState::Attached:
		{
			// rests on the shoulder and sways a little with the body
			const float Sway = 28.f * FMath::Sin(GetGameTimeSinceCreation() * 1.3f + i);
			S.Pos = RestPos + FVector(0.f, 0.f, Sway);
			S.Rot = RestQ;
			break;
		}
		case ESlabState::Windup:
		{
			S.Timer += DeltaSeconds;
			if (S.Timer < 0.f) { S.Pos = RestPos; S.Rot = RestQ; break; } // waits for its turn
			const float A = FMath::Clamp(S.Timer / FMath::Max(SlabWindupSeconds, 0.1f), 0.f, 1.f);
			const float E = A * A * (3.f - 2.f * A);
			// rises over the boss and turns its lower edge towards the player
			S.Pos = RestPos + FVector(0.f, 0.f, 1200.f * E);
			const FVector ToPlayer = Player ? (Player->GetActorLocation() - S.Pos).GetSafeNormal() : GetActorForwardVector();
			S.Rot = FQuat::Slerp(RestQ, FRotationMatrix::MakeFromZ(ToPlayer).ToQuat(), E);
			if (S.Timer >= SlabWindupSeconds)
			{
				S.State = ESlabState::Flying;
				S.Timer = 0.f;
				S.Speed = SlabSpeedStart;
				S.Dir = ToPlayer;
				if (S.bParry && SlabHitboxes.IsValidIndex(i)) SlabHitboxes[i]->SetCollisionEnabled(ECollisionEnabled::QueryOnly); // now it can be shot
			}
			break;
		}
		case ESlabState::Flying:
		{
			S.Timer += DeltaSeconds;
			S.Speed = FMath::Min(SlabSpeedMax, S.Speed + (SlabSpeedMax - SlabSpeedStart) * 0.5f * DeltaSeconds);
			bool bHitPlayer = false;
			if (Player)
			{
				const FVector ToPlayer = Player->GetActorLocation() - S.Pos;
				const float Dist = ToPlayer.Size();
				if (Dist > SlabCommitDistance)
				{
					// homing: turn towards the player, limited by the turn rate (phase 2 turns faster); closer than the commit distance it flies straight
					const float MaxTurn = FMath::DegreesToRadians(SlabTurnDegPerSec * (Phase == 1 ? 1.f : 1.5f)) * DeltaSeconds;
					const FVector Want = ToPlayer / Dist;
					const float Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(S.Dir, Want), -1.f, 1.f));
					const float T = Angle > KINDA_SMALL_NUMBER ? FMath::Min(1.f, MaxTurn / Angle) : 1.f;
					S.Dir = FMath::Lerp(S.Dir, Want, T).GetSafeNormal();
				}
				bHitPlayer = Dist < 230.f * SlabScale * 0.5f + 90.f;
			}
			S.Pos += S.Dir * S.Speed * DeltaSeconds;
			S.Rot = FQuat::Slerp(S.Rot, FRotationMatrix::MakeFromZ(S.Dir).ToQuat(), FMath::Min(1.f, DeltaSeconds * 8.f));
			if (bHitPlayer || S.Pos.Z <= GroundZ + 180.f || S.Timer > 8.f)
			{
				SlabImpact(i, bHitPlayer);
			}
			break;
		}
		case ESlabState::Reflected:
		{
			// the parried slab flies back into the boss's chest
			const FVector Target = GetActorLocation() + FVector(0.f, 0.f, 1100.f);
			const FVector To = Target - S.Pos;
			const float Dist = To.Size();
			S.Dir = To / FMath::Max(Dist, 1.f);
			S.Pos += S.Dir * ParryReturnSpeed * DeltaSeconds;
			S.Rot = FQuat::Slerp(S.Rot, FRotationMatrix::MakeFromZ(S.Dir).ToQuat(), FMath::Min(1.f, DeltaSeconds * 10.f));
			if (Dist < 700.f)
			{
				// it hits: damage, a pause in the boss's attacks, the arena calms down
				UGameplayStatics::ApplyDamage(this, MaxHealth * ParryDamageFraction, nullptr, this, UDamageType::StaticClass());
				if (Action != EAction::Transition && !IsDead()) StartAction(EAction::Stagger); // the pause: it malfunctions (the retargeted clip plays out over ParryStaggerSeconds)
				StormHold = 0.f;
				if (const ADunePlayerCharacter* DunePlayer = Cast<ADunePlayerCharacter>(Player))
				{
					if (UDuneGunFeel* Feel = DunePlayer->GetGunFeel()) Feel->AddTrauma(0.6f);
				}
				UE_LOG(LogDuneSlab, Log, TEXT("Slab %d: the parried slab hits the boss (health %.0f / %.0f)"), i, GetHealth()->Health, MaxHealth);
				SetParryLook(i, false);
				S.bParry = false;
				S.State = ESlabState::Returning;
				S.Timer = 0.f;
				S.ReturnFrom = S.Pos;
				S.ReturnRot = S.Rot;
			}
			break;
		}
		case ESlabState::Embedded:
		{
			S.Timer += DeltaSeconds;
			if (S.Timer >= 2.2f)
			{
				SetParryLook(i, false);
				S.bParry = false;
				S.State = ESlabState::Returning;
				S.Timer = 0.f;
				S.ReturnFrom = S.Pos;
				S.ReturnRot = S.Rot;
			}
			break;
		}
		case ESlabState::Returning:
		{
			S.Timer += DeltaSeconds;
			const float A = FMath::Clamp(S.Timer / 2.0f, 0.f, 1.f);
			const float E = A * A * (3.f - 2.f * A);
			S.Pos = FMath::Lerp(S.ReturnFrom, RestPos, E) + FVector(0.f, 0.f, 900.f * FMath::Sin(A * PI));
			S.Rot = FQuat::Slerp(S.ReturnRot, RestQ, E);
			if (A >= 1.f) S.State = ESlabState::Attached;
			break;
		}
		}
		if (SlabRoots.IsValidIndex(i)) SlabRoots[i]->SetWorldLocationAndRotation(S.Pos, S.Rot);
		if (S.bParry && SlabMats.IsValidIndex(i) && SlabMats[i]) SlabMats[i]->SetScalarParameterValue(TEXT("GlowIntensity"), 14.f + 5.f * FMath::Sin(GetGameTimeSinceCreation() * 9.f)); // pulses

		// the landing mark on the floor (yellow for the parry slab, red for the others), from the windup until it lands
		if (SlabMarkers.IsValidIndex(i))
		{
			const bool bShowMark = (S.State == ESlabState::Windup && S.Timer >= 0.f) || S.State == ESlabState::Flying;
			SlabMarkers[i]->SetVisibility(bShowMark);
			if (bShowMark)
			{
				FVector Impact = Player ? Player->GetActorLocation() : S.Pos;
				if (S.State == ESlabState::Flying && S.Dir.Z < -0.05f)
				{
					Impact = S.Pos + S.Dir * ((S.Pos.Z - (GroundZ + 180.f)) / -S.Dir.Z);
				}
				const float Pulse = 1.f + 0.1f * FMath::Sin(GetGameTimeSinceCreation() * 12.f + i);
				SlabMarkers[i]->SetWorldLocation(FVector(Impact.X, Impact.Y, GroundZ + 6.f));
				SlabMarkers[i]->SetWorldScale3D(FVector(7.f * Pulse, 7.f * Pulse, 0.03f));
				if (MarkerMats.IsValidIndex(i) && MarkerMats[i])
				{
					MarkerMats[i]->SetVectorParameterValue(TEXT("Glow"), S.bParry ? FLinearColor(12.f, 8.f, 0.3f, 1.f) : FLinearColor(10.f, 0.5f, 0.15f, 1.f));
				}
			}
		}
	}
}

void ADuneSlabBoss::TickStorm(float DeltaSeconds)
{
	// the strength of the storm: up fast when the boss acts, a slow fade afterwards
	const bool bBlowing = StormHold > 0.f && !IsDead();
	StormHold = FMath::Max(0.f, StormHold - DeltaSeconds);
	Storm = FMath::FInterpConstantTo(Storm, bBlowing ? 1.f : 0.f, DeltaSeconds, bBlowing ? StormRiseRate : StormFallRate);
	const bool bShow = Storm > 0.01f;
	if (StormOuter) StormOuter->SetVisibility(bShow);
	if (StormInner) StormInner->SetVisibility(bShow);
	if (StormMatOuter) StormMatOuter->SetScalarParameterValue(TEXT("Intensity"), Storm * 0.55f); // the view stays readable: the veil of the barrier is what hides the player's view
	if (StormMatInner) StormMatInner->SetScalarParameterValue(TEXT("Intensity"), Storm * 1.0f);

	// the ripples: rings leave the boss while the storm is strong, run outwards and plough the sand where they pass the player
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	const FVector Origin = GetActorLocation();
	if (Storm > 0.25f && !IsDead())
	{
		RippleTimer -= DeltaSeconds;
		if (RippleTimer <= 0.f)
		{
			RippleTimer = RippleInterval;
			Ripples.Add(450.f);
		}
	}
	for (int32 i = Ripples.Num() - 1; i >= 0; --i)
	{
		Ripples[i] += RippleSpeed * DeltaSeconds;
		if (Player) StampRipple(Ripples[i], Player->GetActorLocation(), 260.f, RippleDepthCm, 1.5f); // wide, shallow and soft (it was narrow, deep and sharp)
		if (Ripples[i] > 4500.f) Ripples.RemoveAt(i);
	}
	// (the random boiling of the sand round the player is gone: it read as noise, not as wind)
}

void ADuneSlabBoss::StampRipple(float Radius, const FVector& PlayerLocation, float TrenchRadius, float DepthCm, float Ridge)
{
	// only the arc of the ring near the player is worth stamping (the sand patch is about 16 m around the player)
	const FVector Center(GetActorLocation().X, GetActorLocation().Y, GroundZ);
	const FVector2D ToPlayer(PlayerLocation.X - Center.X, PlayerLocation.Y - Center.Y);
	const float Dist = ToPlayer.Size();
	if (FMath::Abs(Dist - Radius) > 1500.f || Radius < 100.f) return;
	const float Theta = FMath::Atan2(ToPlayer.Y, ToPlayer.X);
	const float Half = FMath::Clamp(FMath::Asin(FMath::Min(1500.f / Radius, 1.f)), 0.05f, PI * 0.5f);
	const int32 N = FMath::Clamp(FMath::RoundToInt(2.f * Radius * Half / 80.f), 6, 40);
	auto OnRing = [&](float A) { return FVector(Center.X + FMath::Cos(A) * Radius, Center.Y + FMath::Sin(A) * Radius, GroundZ); };
	for (int32 i = 0; i < N; ++i)
	{
		const float A0 = Theta - Half + 2.f * Half * i / N;
		const float A1 = Theta - Half + 2.f * Half * (i + 1) / N;
		FSandInteractionRequest Trench; // a trench with raised sides: from the side it reads as a wave in the sand
		Trench.Brush = ESandBrushType::Wheel;
		Trench.WorldPosition = OnRing(A1);
		Trench.PreviousPosition = OnRing(A0);
		Trench.bHasPreviousPosition = true;
		Trench.MovementDirection = Trench.WorldPosition - Trench.PreviousPosition;
		Trench.Radius = TrenchRadius;
		Trench.DepthOverrideCm = DepthCm;
		Trench.RidgeScale = Ridge;
		USandDeformationLibrary::ApplySandInteraction(this, Trench);
	}
}

void ADuneSlabBoss::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsDead() && !bPhase3Entered && Phase >= 2 && GetHealthFraction() <= Phase3Fraction)
	{
		// phase 3: the arena turns dark red, the boss roars, from now on it can also call the tsunami
		bPhase3Entered = true;
		Phase = 3;
		Light->SetLightColor(FLinearColor(1.f, 0.02f, 0.02f));
		EndAction(0.f);
		StartAction(EAction::Transition);
	}
	TickMovement(DeltaSeconds);
	TickAction(DeltaSeconds);
	TickStorm(DeltaSeconds);
	TickSlabs(DeltaSeconds);
	TickTsunami(DeltaSeconds);
	TickBarrier(DeltaSeconds);
	TickGrains(DeltaSeconds);
	if (BodyMesh && BodyMesh->GetSkeletalMeshAsset() && !IsDead()) HeadHitbox->SetWorldLocation(BodyMesh->GetBoneLocation(TEXT("Head")));
	if (IsDead() && !AnimDeath && DeathTime < FallSeconds)
	{
		// the dead boss topples over (pivot at the feet)
		DeathTime += DeltaSeconds;
		const float Alpha = FMath::Clamp(DeathTime / FallSeconds, 0.f, 1.f);
		SetActorRotation(FRotator(FMath::InterpEaseIn(0.f, 82.f, Alpha, 2.5f), StartYaw, 0.f));
	}
}

ADuneAnimPreviewActor::ADuneAnimPreviewActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->bPauseAnims = false;
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones; // a giant scale must not cull its bones
}

void ADuneAnimPreviewActor::BeginPlay()
{
	Super::BeginPlay();
	USkeletalMesh* SkMesh = Cast<USkeletalMesh>(MeshPath.TryLoad());
	if (!SkMesh)
	{
		UE_LOG(LogDuneSlab, Error, TEXT("Slab: mesh %s not found"), *MeshPath.ToString());
		return;
	}
	Mesh->SetSkeletalMesh(SkMesh);
	const FBoxSphereBounds B = SkMesh->GetBounds();
	UE_LOG(LogDuneSlab, Log, TEXT("Slab: %s loaded, %d bones, bounds extent %s, actor scale %.1f"), *SkMesh->GetName(), SkMesh->GetRefSkeleton().GetNum(), *B.BoxExtent.ToString(), GetActorScale3D().X);
	if (AnimPath.IsNull()) return;
	UAnimSequence* Anim = Cast<UAnimSequence>(AnimPath.TryLoad());
	if (!Anim)
	{
		UE_LOG(LogDuneSlab, Error, TEXT("Slab: animation %s not found"), *AnimPath.ToString());
		return;
	}
	if (Anim->GetSkeleton() != SkMesh->GetSkeleton())
	{
		UE_LOG(LogDuneSlab, Warning, TEXT("Slab: animation %s is for another skeleton than %s (it will not play)"), *Anim->GetName(), *SkMesh->GetName());
	}
	Mesh->PlayAnimation(Anim, true);
	if (FParse::Param(FCommandLine::Get(), TEXT("DuneSlabShot")))
	{
		// where do the bones go while the animation plays
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, [this]()
		{
			UE_LOG(LogDuneSlab, Log, TEXT("Slab: %s bounds origin %s extent %s, hips %s, head %s"), *GetName(), *Mesh->Bounds.Origin.ToCompactString(), *Mesh->Bounds.BoxExtent.ToCompactString(), *Mesh->GetBoneLocation(TEXT("Hips")).ToCompactString(), *Mesh->GetBoneLocation(TEXT("Head")).ToCompactString());
		}, 1.0f, true);
	}
	UE_LOG(LogDuneSlab, Log, TEXT("Slab: %s plays %s (%.2f s) in a loop"), *SkMesh->GetName(), *Anim->GetName(), Anim->GetPlayLength());
}

ADuneAnimPreviewActor* ADuneSlabBossGameMode::SpawnPreview(const TCHAR* Mesh, const TCHAR* Anim, const FVector& Location, float Yaw, float Scale)
{
	const FTransform T(FRotator(0.f, Yaw, 0.f), Location, FVector(Scale));
	ADuneAnimPreviewActor* A = GetWorld()->SpawnActorDeferred<ADuneAnimPreviewActor>(ADuneAnimPreviewActor::StaticClass(), T, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!A) return nullptr;
	A->MeshPath = FSoftObjectPath(Mesh);
	if (Anim) A->AnimPath = FSoftObjectPath(Anim);
	A->FinishSpawning(T);
	return A;
}

void ADuneSlabBossGameMode::SpawnBoss()
{
	// no core: the Slab Bearer stands where the core hovered (feet on the floor) and fights with the core's logic
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SetBoss(GetWorld()->SpawnActor<ADuneSlabBoss>(ADuneSlabBoss::StaticClass(), FVector::ZeroVector, FRotator(0.f, BossYaw, 0.f), P));
	if (bSpawnMannequin || FParse::Param(FCommandLine::Get(), TEXT("DuneSlabMannequin")))
	{
		SpawnPreview(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"), TEXT("/Game/DUNE/Boss/SlabBearer/Anims/A_MonsterRoar.A_MonsterRoar"), MannequinLocation, MannequinYaw, 1.f);
	}
}

void ADuneSlabBossGameMode::BeginPlay()
{
	Super::BeginPlay();
	// -DuneParryTest: an attack is forced; as soon as the slab with the yellow rim is far enough, the "player" aims at it and shoots; the result is logged
	if (FParse::Param(FCommandLine::Get(), TEXT("DuneParryTest")))
	{
		FTimerHandle T1, T2, T3;
		GetWorldTimerManager().SetTimer(T1, [this]() { if (GetBoss()) GetBoss()->DebugBarrage(); }, 1.5f, false);
		GetWorldTimerManager().SetTimer(T2, [this]()
		{
			ADunePlayerCharacter* Player = Cast<ADunePlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
			ADuneSlabBoss* Slab = Cast<ADuneSlabBoss>(GetBoss());
			FVector P;
			if (Player && Slab && Slab->GetParrySlab(P) && FVector::Dist(P, Player->GetActorLocation()) > 1800.f && Slab->ParrySuccesses == 0)
			{
				const FVector Eye = Player->GetActorLocation() + FVector(0.f, 0.f, 64.f);
				Player->GetController()->SetControlRotation((P - Eye).Rotation());
				Player->Fire();
			}
		}, 0.05f, true);
		GetWorldTimerManager().SetTimer(T3, [this]()
		{
			const ADuneSlabBoss* Slab = Cast<ADuneSlabBoss>(GetBoss());
			const bool bOk = Slab && Slab->ParrySuccesses >= 1 && Slab->GetHealthFraction() < 0.95f;
			UE_LOG(LogDuneSlab, Log, TEXT("SlabParryTest: parried %d, not parried %d, boss health %.0f %% -> %s"), Slab ? Slab->ParrySuccesses : -1, Slab ? Slab->ParryFailures : -1, Slab ? Slab->GetHealthFraction() * 100.f : -1.f, bOk ? TEXT("PASS") : TEXT("FAIL"));
			FPlatformMisc::RequestExit(false);
		}, 9.f, false);
	}
	// -DuneSlabAttackShots: the player looks at the boss slightly up, an attack is forced, four pictures (windup, flight, landing, embedded), quit
	if (FParse::Param(FCommandLine::Get(), TEXT("DuneSlabAttackShots")))
	{
		FTimerHandle A1, A2, A3, A4, A5, A6, A7;
		GetWorldTimerManager().SetTimer(A1, [this]()
		{
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0)) PC->SetControlRotation(FRotator(17.f, 0.f, 0.f));
			if (GetBoss()) GetBoss()->DebugBarrage();
		}, 1.5f, false);
		GetWorldTimerManager().SetTimer(A2, [this]() { TakeShot(TEXT("SlabAttack_1_windup.png")); }, 2.6f, false);
		GetWorldTimerManager().SetTimer(A3, [this]()
		{
			// look at the flying parry slab so the picture shows its glow
			FVector P;
			APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
			ADuneSlabBoss* Slab = Cast<ADuneSlabBoss>(GetBoss());
			if (PC && Slab && Slab->GetParrySlab(P)) PC->SetControlRotation((P - PC->PlayerCameraManager->GetCameraLocation()).Rotation());
			TakeShot(TEXT("SlabAttack_2_flight.png"));
		}, 3.2f, false);
		for (int32 n = 0; n < 4; ++n)
		{
			FTimerHandle H;
			GetWorldTimerManager().SetTimer(H, [this, n]()
			{
				FVector P;
				APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
				ADuneSlabBoss* Slab = Cast<ADuneSlabBoss>(GetBoss());
				if (PC && Slab && Slab->GetParrySlab(P)) PC->SetControlRotation((P - PC->PlayerCameraManager->GetCameraLocation()).Rotation());
				TakeShot(*FString::Printf(TEXT("SlabAttack_parry%d.png"), n));
			}, 3.6f + 0.5f * n, false);
		}
		GetWorldTimerManager().SetTimer(A4, [this]() { TakeShot(TEXT("SlabAttack_3_landing.png")); }, 4.5f, false);
		GetWorldTimerManager().SetTimer(A5, [this]() { TakeShot(TEXT("SlabAttack_4_after.png")); }, 5.6f, false);
		GetWorldTimerManager().SetTimer(A6, [this]()
		{
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0)) PC->SetControlRotation(FRotator(-14.f, 0.f, 0.f));
		}, 5.9f, false);
		GetWorldTimerManager().SetTimer(A7, [this]() { TakeShot(TEXT("SlabAttack_5_floor.png")); }, 6.8f, false);
		FTimerHandle A8;
		GetWorldTimerManager().SetTimer(A8, []() { FPlatformMisc::RequestExit(false); }, 8.2f, false);
	}
	// -DuneSlabPhaseTest: phases by forced health, the actions (eruptions, tsunami, stagger), the barrier (veil, contact damage); logs PASS / FAIL lines and quits
	if (FParse::Param(FCommandLine::Get(), TEXT("DuneSlabPhaseTest")))
	{
		TSharedRef<float> Clock = MakeShared<float>(0.f);
		TSharedRef<int32> Step = MakeShared<int32>(0);
		TSharedRef<int32> Fails = MakeShared<int32>(0);
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, [this, Clock, Step, Fails]()
		{
			*Clock += 0.1f;
			ADunePlayerCharacter* Player = Cast<ADunePlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
			ADuneSlabBoss* Slab = Cast<ADuneSlabBoss>(GetBoss());
			if (!Player || !Slab) return;
			auto Check = [Fails](bool bOk, const FString& What)
			{
				UE_LOG(LogDuneSlab, Log, TEXT("SlabPhaseTest: %s %s"), *What, bOk ? TEXT("PASS") : TEXT("FAIL"));
				if (!bOk) ++*Fails;
			};
			auto At = [&](float T) { return *Clock >= T && *Step == FMath::FloorToInt(T); };
			(void)At;
			const float T = *Clock;
			auto Once = [&](int32 Id, float When) { if (T >= When && *Step < Id) { *Step = Id; return true; } return false; };
			if (Once(1, 2.f))
			{
				Player->GetHealth()->Init(100000.f, 100000.f);
				Check(Slab->GetPhase() == 1, TEXT("starts in phase 1"));
				Slab->GetHealth()->Health = Slab->MaxHealth * 0.6f; // below 65 %
			}
			if (Once(2, 3.f)) Check(Slab->GetPhase() == 2, FString::Printf(TEXT("phase 2 below 65 %% (phase %d, action %d)"), Slab->GetPhase(), Slab->GetActionId()));
			if (Once(3, 9.f)) Slab->DebugAction(1);
			if (Once(4, 17.f)) Check(Slab->EruptionCount >= 1, FString::Printf(TEXT("an eruption action ran (marked %d times, hit the player %d times)"), Slab->EruptionCount, Slab->EruptionHits));
			if (Once(5, 18.f)) Slab->GetHealth()->Health = Slab->MaxHealth * 0.25f; // below 30 %
			if (Once(6, 19.f)) Check(Slab->GetPhase() == 3, FString::Printf(TEXT("phase 3 below 30 %% (phase %d)"), Slab->GetPhase()));
			if (Once(7, 26.f)) Slab->DebugAction(2);
			if (Once(8, 36.f)) Check(Slab->TsunamiCount >= 1, FString::Printf(TEXT("the tsunami ran (count %d, hits %d)"), Slab->TsunamiCount, Slab->TsunamiHits));
			if (Once(9, 37.f)) Slab->DebugAction(3);
			if (Once(10, 38.f)) Check(Slab->GetActionId() == 4, TEXT("the stagger action is active"));
			if (Once(11, 44.f))
			{
				// the barrier: 1100 cm from the boss the view is mostly covered
				const FVector B = Slab->GetActorLocation();
				Player->SetActorLocation(B + FVector(0.f, -1100.f, 120.f));
			}
			if (Once(12, 46.f)) Check(Slab->GetVeil() > 0.3f, FString::Printf(TEXT("the sand veil is %.2f at 1100 cm"), Slab->GetVeil()));
			if (Once(13, 46.5f)) Player->SetActorLocation(Slab->GetActorLocation() + FVector(0.f, -620.f, 120.f));
			if (Once(14, 49.f)) Check(Slab->ContactHits >= 1, FString::Printf(TEXT("touching the body hurts (contact hits %d)"), Slab->ContactHits));
			if (Once(15, 50.f))
			{
				UE_LOG(LogDuneSlab, Log, TEXT("SlabPhaseTest: RESULT %s (fails=%d)"), *Fails == 0 ? TEXT("PASS") : TEXT("FAIL"), *Fails);
				FPlatformMisc::RequestExit(false);
			}
		}, 0.1f, true);
	}
	// -DuneSlabShots2: pictures of the new parts (idle, eruption marks and burst, tsunami lanes and wall, the veil at 1100 and 700 cm) and quit. -DuneSlabShotPrefix=<p> names them
	if (FParse::Param(FCommandLine::Get(), TEXT("DuneSlabShots2")))
	{
		TSharedRef<float> Clock = MakeShared<float>(0.f);
		TSharedRef<int32> Step = MakeShared<int32>(0);
		FString Prefix = TEXT("S2");
		FParse::Value(FCommandLine::Get(), TEXT("DuneSlabShotPrefix="), Prefix);
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, [this, Clock, Step, Prefix]()
		{
			*Clock += 0.1f;
			ADunePlayerCharacter* Player = Cast<ADunePlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
			ADuneSlabBoss* Slab = Cast<ADuneSlabBoss>(GetBoss());
			APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
			if (!Player || !Slab || !PC) return;
			const float T = *Clock;
			auto Once = [&](int32 Id, float When) { if (T >= When && *Step < Id) { *Step = Id; return true; } return false; };
			auto Shot = [&](const TCHAR* Name) { TakeShot(*FString::Printf(TEXT("%s_%s.png"), *Prefix, Name)); };
			auto LookAtBoss = [&](float Pitch) { PC->SetControlRotation(FRotator(Pitch, (Slab->GetActorLocation() - Player->GetActorLocation()).Rotation().Yaw, 0.f)); };
			if (Once(1, 1.f)) { Player->GetHealth()->Init(100000.f, 100000.f); }
			if (Once(2, 1.5f)) { Slab->PatternTimer_Debug(1000.f); LookAtBoss(14.f); }
			if (Once(3, 3.f)) Shot(TEXT("1_idle"));
			if (Once(4, 3.5f)) { LookAtBoss(-30.f); Slab->DebugAction(1); }
			if (Once(5, 5.2f)) Shot(TEXT("2_eruption_marks"));
			if (Once(6, 6.3f)) Shot(TEXT("3_eruption_burst"));
			if (Once(7, 10.5f)) { LookAtBoss(5.f); Slab->DebugAction(2); }
			if (Once(8, 12.4f)) Shot(TEXT("4_tsunami_lanes"));
			if (Once(9, 14.4f)) Shot(TEXT("5_tsunami_wall"));
			if (Once(10, 15.6f)) Shot(TEXT("6_tsunami_wall2"));
			if (Once(11, 20.f)) { Slab->DebugAction(3); Player->SetActorLocation(Slab->GetActorLocation() + FVector(-1100.f, 0.f, 120.f)); LookAtBoss(14.f); }
			if (Once(12, 22.f)) Shot(TEXT("7_veil_1100"));
			if (Once(13, 22.2f)) Player->SetActorLocation(Slab->GetActorLocation() + FVector(-760.f, 0.f, 120.f));
			if (Once(14, 24.f)) Shot(TEXT("8_veil_760"));
			if (Once(15, 26.f)) FPlatformMisc::RequestExit(false);
		}, 0.1f, true);
	}
	// -DuneSlabShot: two pictures (the start view, then looking up at the boss) and quit
	if (FParse::Param(FCommandLine::Get(), TEXT("DuneSlabShot")))
	{
		FTimerHandle H1, H2, H3;
		GetWorldTimerManager().SetTimer(H1, [this]() { TakeShot(TEXT("Slab_1.png")); }, 4.4f, false);
		GetWorldTimerManager().SetTimer(H2, [this]()
		{
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0)) PC->SetControlRotation(FRotator(22.f, 0.f, 0.f));
		}, 3.f, false);
		GetWorldTimerManager().SetTimer(H3, [this]() { TakeShot(TEXT("Slab_2.png")); }, 6.0f, false);
		FTimerHandle H4, H5, H6;
		GetWorldTimerManager().SetTimer(H5, [this]()
		{
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0)) PC->SetControlRotation(FRotator(-16.f, 0.f, 0.f)); // looking down at the sand
		}, 6.6f, false);
		GetWorldTimerManager().SetTimer(H6, [this]() { TakeShot(TEXT("Slab_3_floor.png")); }, 7.8f, false);
		GetWorldTimerManager().SetTimer(H4, []() { FPlatformMisc::RequestExit(false); }, 9.5f, false);
	}
}

void ADuneSlabBossGameMode::TakeShot(const TCHAR* Name)
{
	FScreenshotRequest::RequestScreenshot(Name, true, false);
}
