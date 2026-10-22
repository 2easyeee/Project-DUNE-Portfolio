// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dune/DuneGameMode.h"
#include "Dune/SliceableStructure.h"
#include "Dune/KrisKnifeComponent.h"
#include "Dune/DuneGunComponent.h"
#include "Dune/DuneHUD.h"
#include "Dune/DuneImpactManager.h"
#include "Dune/DuneSandTrail.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Dune/DuneWeaponComponent.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/CameraActor.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "UE_HOLDITTOGETHER.h"

ADuneGameMode::ADuneGameMode()
{
	bSpawnTestCourse = false;
	HUDClass = ADuneHUD::StaticClass(); // crosshair and weapon name
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

void ADuneGameMode::BeginPlay()
{
	Super::BeginPlay();
	bSelfTest = FParse::Param(FCommandLine::Get(), TEXT("DuneSliceTest"));
	bKnifeTest = FParse::Param(FCommandLine::Get(), TEXT("DuneKnifeTest"));
	bShots = FParse::Param(FCommandLine::Get(), TEXT("DuneShot"));
	bGunTest = FParse::Param(FCommandLine::Get(), TEXT("DuneGunTest"));
	bFractureTest = FParse::Param(FCommandLine::Get(), TEXT("DuneFractureTest"));
	bSandTest = FParse::Param(FCommandLine::Get(), TEXT("DuneSandTest"));
	bSandSimTest = FParse::Param(FCommandLine::Get(), TEXT("DuneSandSimTest"));
	bCityWalk = FParse::Param(FCommandLine::Get(), TEXT("DuneCityWalk"));
	bPortfolio = FParse::Value(FCommandLine::Get(), TEXT("DunePortfolio="), PfMode);
	FString CityFile;
	if (FParse::Value(FCommandLine::Get(), TEXT("DuneCityShot="), CityFile) && FFileHelper::LoadFileToStringArray(CityViews, *CityFile))
	{
		bCityShots = CityViews.Num() > 0;
	}
	SetActorTickEnabled(bSelfTest || bKnifeTest || bShots || bGunTest || bFractureTest || bSandTest || bSandSimTest || bCityShots || bCityWalk || bPortfolio);
	if (!FParse::Param(FCommandLine::Get(), TEXT("NoDuneSand")))
	{
		// deferred, so the -DuneSandFlat switch (shading only, no vertex displacement; for comparing screenshots) is set before BeginPlay
		SandTrail = GetWorld()->SpawnActorDeferred<ADuneSandTrail>(ADuneSandTrail::StaticClass(), FTransform::Identity);
		if (SandTrail)
		{
			float Scale = FParse::Param(FCommandLine::Get(), TEXT("DuneSandFlat")) ? 0.f : 1.f;
			FParse::Value(FCommandLine::Get(), TEXT("DuneSandDisp="), Scale); // e.g. 8 exaggerates the vertex displacement to make it obvious in a screenshot
			SandTrail->DisplacementScale = Scale;
			SandTrail->bDebugLog = FParse::Param(FCommandLine::Get(), TEXT("DuneSandDebug"));
			if (FParse::Param(FCommandLine::Get(), TEXT("DuneSandNoCompute")))
			{
				SandTrail->bUseComputeSim = false; // the old material fade only, to compare
			}
			SandTrail->FinishSpawning(FTransform::Identity);
		}
	}
	for (TActorIterator<ASliceableStructure> It(GetWorld()); It; ++It)
	{
		It->LogMeshInfo(TEXT("level start"));
	}
}

void ADuneGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ACharacter* Player = Cast<ACharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!Player)
	{
		return;
	}
	if (bPortfolio)
	{
		RunPortfolio(DeltaSeconds, Player);
		return;
	}
	if (bKnifeTest)
	{
		RunKnifeTest(DeltaSeconds, Player);
		return;
	}
	if (bGunTest)
	{
		RunGunTest(DeltaSeconds, Player);
		return;
	}
	if (bFractureTest)
	{
		RunFractureTest(DeltaSeconds, Player);
		return;
	}
	if (bCityWalk)
	{
		RunCityWalk(DeltaSeconds, Player);
		return;
	}
	if (bCityShots)
	{
		RunCityShots(DeltaSeconds, Player);
		return;
	}
	if (bSandSimTest)
	{
		RunSandSimTest(DeltaSeconds, Player);
		return;
	}
	if (bSandTest)
	{
		RunSandTest(DeltaSeconds, Player);
		return;
	}
	if (bShots)
	{
		RunShots(DeltaSeconds, Player);
		return;
	}
	TestTime += DeltaSeconds;
	MaxZ = FMath::Max(MaxZ, Player->GetActorLocation().Z);

	// 0.5 s: what the level gave us
	if (Step == 0 && TestTime > 0.5f)
	{
		Step = 1;
		StartLocation = Player->GetActorLocation();
		MaxZ = StartLocation.Z;
		APlayerController* PC = Cast<APlayerController>(Player->GetController());
		const UCameraComponent* Camera = Player->FindComponentByClass<UCameraComponent>();
		bool bContext = false;
		if (PC && PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Input = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				bContext = Input->HasMappingContext(LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Default.IMC_Default")));
			}
		}
		// terrain collision: a trace straight down at a few places must hit the landscape
		for (const FVector2D& P : { FVector2D(0, 0), FVector2D(-700, 0), FVector2D(5000, 0), FVector2D(9000, 9000), FVector2D(-15000, 5000), FVector2D(24000, 0) })
		{
			FHitResult Hit;
			const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, FVector(P.X, P.Y, 20000.f), FVector(P.X, P.Y, -2000.f), ECC_Visibility);
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("DuneTest: terrain at (%.0f, %.0f): %s z=%.0f"), P.X, P.Y,
				bHit ? *GetNameSafe(Hit.GetActor()) : TEXT("NO HIT"), bHit ? Hit.ImpactPoint.Z : 0.f);
		}
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("DuneTest: map=%s gamemode=%s pawn=%s controller=%s camera=%s imc_default_active=%d start=%s"),
			*GetWorld()->GetMapName(), *GetClass()->GetName(), *Player->GetClass()->GetName(), PC ? *PC->GetClass()->GetName() : TEXT("none"),
			Camera ? *Camera->GetName() : TEXT("none"), bContext, *StartLocation.ToString());
	}
	// 0.5 - 1.5 s: walk forward like the move input does
	if (Step == 1)
	{
		Player->AddMovementInput(Player->GetActorForwardVector(), 1.f);
		if (TestTime > 1.5f)
		{
			Step = 2;
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("DuneTest: walked %.0f cm in 1 s"), FVector::DistXY(Player->GetActorLocation(), StartLocation));
			Player->Jump();
		}
	}
	if (Step == 2 && TestTime > 2.5f)
	{
		Step = 3;
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("DuneTest: jump rose %.0f cm"), MaxZ - StartLocation.Z);
		RunSliceRequests();
	}
	if (Step == 3 && TestTime > 3.5f)
	{
		FPlatformMisc::RequestExit(false);
	}
}

void ADuneGameMode::RunShots(float DeltaSeconds, ACharacter* Player)
{
	ShotTime += DeltaSeconds;
	APlayerController* PC = Cast<APlayerController>(Player->GetController());
	if (!PC)
	{
		return;
	}
	if (FMath::FloorToInt(ShotTime) != FMath::FloorToInt(ShotTime - DeltaSeconds) && FMath::FloorToInt(ShotTime) % 5 == 0)
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("DuneShot: t=%.0f pawn %s velocity %s mode %d"), ShotTime, *Player->GetActorLocation().ToString(),
			*Player->GetVelocity().ToString(), static_cast<int32>(Player->GetCharacterMovement()->MovementMode));
	}
	// views: 0 default (structures ahead), 1 close look at the sand, 2 turned towards the outpost, 3 looking back at the horizon towers
	static const FRotator Views[] = { FRotator(-8.f, 0.f, 0.f), FRotator(-55.f, 0.f, 0.f), FRotator(-6.f, -30.f, 0.f), FRotator(-4.f, 135.f, 0.f) };
	const float FirstShot = 45.f; // shader compilation of the landscape and kit materials
	if (ShotStep < 4 && ShotTime > FirstShot + ShotStep * 5.f)
	{
		PC->SetControlRotation(Views[ShotStep]);
	}
	if (ShotStep < 4 && ShotTime > FirstShot + ShotStep * 5.f + 2.5f)
	{
		PC->ConsoleCommand(FString::Printf(TEXT("HighResShot 1280x720 filename=DuneShot%d"), ShotStep));
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("DuneShot: view %d requested, pawn %s yaw %.0f, camera at %s looking %s"), ShotStep,
			*Player->GetActorLocation().ToString(), Player->GetActorRotation().Yaw,
			*PC->PlayerCameraManager->GetCameraLocation().ToString(), *PC->PlayerCameraManager->GetCameraRotation().ToString());
		++ShotStep;
	}
	if (ShotStep >= 4 && ShotTime > FirstShot + 4 * 5.f + 3.f)
	{
		FPlatformMisc::RequestExit(false);
	}
}

void ADuneGameMode::FinishRestartPlayer(AController* NewPlayer, const FRotator& StartRotation)
{
	Super::FinishRestartPlayer(NewPlayer, StartRotation);
	APlayerController* PC = Cast<APlayerController>(NewPlayer);
	APawn* Pawn = NewPlayer ? NewPlayer->GetPawn() : nullptr;
	if (!PC || !Pawn)
	{
		return;
	}
	// the weapon component creates the knife and the gun and owns the weapon input (Tab, left mouse button)
	UDuneWeaponComponent* Weapons = NewObject<UDuneWeaponComponent>(Pawn, TEXT("DuneWeapons"));
	Weapons->RegisterComponent();
	Weapons->SetupInput(PC);
}

// Gun self-test (-DuneGunTest, plus -DuneGunShot for screenshots): drives what the mouse and Tab call (PrimaryAttack,
// ToggleWeapon) and the player's control rotation, then logs what each attack did. The gun must never raise the slice
// request counter; the knife must.
void ADuneGameMode::RunGunTest(float DeltaSeconds, ACharacter* Player)
{
	GunTime += DeltaSeconds;
	GunStepTime += DeltaSeconds;
	APlayerController* PC = Cast<APlayerController>(Player->GetController());
	UDuneWeaponComponent* Weapons = Player->FindComponentByClass<UDuneWeaponComponent>();
	UDuneGunComponent* Gun = Weapons ? Weapons->GetGun() : nullptr;
	UKrisKnifeComponent* Knife = Weapons ? Weapons->GetKnife() : nullptr;
	if (!PC || !Gun || !Knife)
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("GunTest: no controller, gun or knife on the player"));
		FPlatformMisc::RequestExit(false);
		return;
	}
	const bool bShot = FParse::Param(FCommandLine::Get(), TEXT("DuneGunShot"));
	int32 RepeatTarget = 12; // -DuneGunRepeats=N: more repeated shots (to reach the bullet hole and chip caps)
	FParse::Value(FCommandLine::Get(), TEXT("-DuneGunRepeats="), RepeatTarget);
	auto Next = [this](int32 S) { GunStep = S; GunStepTime = 0.f; };
	auto Find = [this](const TCHAR* Label) -> AActor*
	{
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (It->GetActorLabel() == Label) { return *It; }
		}
		return nullptr;
	};
	// turn the view so the screen centre points at a world position (the camera moves with the view, so this is repeated every tick)
	auto AimAt = [PC](const FVector& Point)
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		PC->SetControlRotation((Point - ViewLocation).Rotation());
	};
	auto Report = [this, Gun, Weapons](const TCHAR* Label, ASliceableStructure* S)
	{
		const ADuneImpactManager* Impacts = ADuneImpactManager::Get(GetWorld());
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("GunTest: [%s] weapon=%s | gun: shots=%d impacts=%d non-structure hits=%d misses=%d ignored=%d | inputs gun=%d knife=%d switches=%d | holes=%d (made %d) fragments=%d (made %d) | slice requests=%d | target structure pieces=%d"),
			Label, Weapons->GetWeapon() == EDuneWeapon::Gun ? TEXT("Gun") : TEXT("Knife"), Gun->GetShots(), Gun->GetImpacts(), Gun->GetNonTargetHits(), Gun->GetMisses(),
			Gun->GetIgnoredInputs(), Weapons->GetGunInputs(), Weapons->GetKnifeInputs(), Weapons->GetSwitchCount(), Impacts->GetBulletHoleCount(), Impacts->GetTotalBulletHoles(),
			Impacts->GetFragmentCount(), Impacts->GetTotalFragments(), ASliceableStructure::GetTotalSliceRequests(), S ? S->GetPieces().Num() + 1 : 0);
	};
	ASliceableStructure* Pillar = Cast<ASliceableStructure>(Find(TEXT("Slice_Pillar")));
	ASliceableStructure* Block = Cast<ASliceableStructure>(Find(TEXT("Slice_Block")));

	switch (GunStep)
	{
	case 0: // the level starts with the gun in hand
		if (GunStepTime > 0.5f)
		{
			SliceBase = ASliceableStructure::GetTotalSliceRequests();
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("GunTest: start weapon=%s gun visible=%d knife visible=%d slice requests=%d"), Weapons->GetWeapon() == EDuneWeapon::Gun ? TEXT("Gun") : TEXT("Knife"),
				Gun->IsGunVisible(), Knife->IsKnifeVisible(), SliceBase);
			Next(1);
		}
		break;
	case 1: // sky: a miss, no impact
		AimAt(Player->GetActorLocation() + FVector(200.f, 0.f, 3000.f));
		if (GunStepTime > 0.5f) { Weapons->PrimaryAttack(); Next(2); }
		break;
	case 2: // ground: a hit, but not a structure, no impact
		AimAt(FVector(-400.f, 0.f, 0.f));
		if (GunStepTime > 0.6f) { Weapons->PrimaryAttack(); Report(TEXT("sky + ground"), nullptr); Next(3); }
		break;
	case 3: // an environment building: not a structure, no impact
	{
		const AActor* Env = Find(TEXT("Kit_house_07_1"));
		AimAt(Env ? Env->GetActorLocation() + FVector(0.f, 0.f, 200.f) : FVector(-3300.f, 2500.f, 200.f));
		if (GunStepTime > 0.6f) { Weapons->PrimaryAttack(); Report(TEXT("environment building"), nullptr); GunObs = 0; Next(4); }
		break;
	}
	case 4: // three shots at the pillar, low, middle, high: each leaves a hole and chips, none cuts
	{
		static const float Heights[] = { 80.f, 160.f, 250.f };
		if (Pillar) { AimAt(Pillar->GetActorLocation() + FVector(0.f, 0.f, Heights[FMath::Min(GunObs, 2)] - 200.f)); }
		if (GunStepTime > 0.9f)
		{
			Weapons->PrimaryAttack();
			Report(TEXT("pillar shot"), Pillar);
			GunStepTime = 0.f;
			if (++GunObs >= 3) { GunObs = 0; Next(5); }
		}
		break;
	}
	case 5: // the marks stay, the chips fly and are gone after their lifetime
	{
		static const float Times[] = { 0.4f, 2.f, 5.f };
		if (GunObs < UE_ARRAY_COUNT(Times) && GunStepTime > Times[GunObs])
		{
			Report(TEXT("pillar marks"), Pillar);
			if (bShot) { PC->ConsoleCommand(FString::Printf(TEXT("HighResShot 1280x720 filename=DuneGun%d"), GunObs)); }
			++GunObs;
		}
		if (GunObs >= UE_ARRAY_COUNT(Times) && GunStepTime > 6.f) { GunObs = 0; Next(6); }
		break;
	}
	case 6: // a plain mesh actor (a building the user placed): it gets marks too and is NOT turned into a sliceable structure
	{
		const AActor* House = Find(TEXT("SM_house_07_Sliceable2"));
		if (House) { AimAt(House->GetActorLocation() + FVector(0.f, 0.f, GunObs == 0 ? 260.f : 50.f)); }
		if (GunStepTime > 1.0f)
		{
			Weapons->PrimaryAttack();
			GunStepTime = 0.f;
			if (++GunObs >= 2)
			{
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("GunTest: the building is still a %s (StaticMeshActor expected: a shot must not convert or cut it)"), House ? *House->GetClass()->GetName() : TEXT("missing"));
				Report(TEXT("house shots"), nullptr);
				Next(7);
			}
		}
		break;
	}
	case 7: // Tab: knife in hand. A click from far away reaches nothing: the knife path ran (knife input counted) and no gun shot was fired
		if (bShot && GunStepTime > 3.f && RepeatShots == 0)
		{
			PC->ConsoleCommand(TEXT("HighResShot 1280x720 filename=DuneGunHouse")); // after the debug marks have faded, so the holes themselves show
			RepeatShots = 1;
		}
		if (GunStepTime > (bShot ? 4.f : 1.5f))
		{
			const int32 GunShots = Gun->GetShots();
			Weapons->ToggleWeapon();
			if (Pillar) { AimAt(Pillar->GetActorLocation() + FVector(0.f, 0.f, 50.f)); }
			Weapons->PrimaryAttack();
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("GunTest: knife in hand, click from afar: gun shots %d -> %d (must not change), knife inputs=%d"), GunShots, Gun->GetShots(), Weapons->GetKnifeInputs());
			Next(8);
		}
		break;
	case 8: // walk up to the block with the normal movement input, then cut it with the knife
	{
		AimAt(Block ? Block->GetActorLocation() : FVector::ZeroVector);
		Player->AddMovementInput((Block ? Block->GetActorLocation() - Player->GetActorLocation() : FVector::ForwardVector).GetSafeNormal2D(), 1.f);
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		FHitResult Hit;
		FCollisionQueryParams Params(NAME_None, false, Player);
		if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, ViewLocation + ViewRotation.Vector() * 3000.f, ECC_Visibility, Params)
			&& Hit.GetActor() == Block && FVector::Dist(Player->GetActorLocation(), Hit.ImpactPoint) <= 230.f)
		{
			Next(9);
		}
		break;
	}
	case 9: // the knife cuts: the slice request counter goes up, the block has two pieces
		if (GunStepTime > 0.8f)
		{
			const int32 Before = ASliceableStructure::GetTotalSliceRequests();
			const int32 GunShots = Gun->GetShots();
			Weapons->PrimaryAttack();
			GunObs = Before;
			Next(10);
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("GunTest: knife attack with the block in reach (slice requests before: %d, gun shots %d)"), Before, GunShots);
		}
		break;
	case 10:
		if (GunStepTime > 1.5f)
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("GunTest: after the knife attack slice requests %d -> %d (must go up), knife slices=%d"), GunObs, ASliceableStructure::GetTotalSliceRequests(), Knife->GetSlices());
			Report(TEXT("block cut by the knife"), Block);
			GunObs = 0;
			Next(11);
		}
		break;
	case 11: // gun again, four quick rounds of Tab + click: a gun round never raises the slice counter, a knife round does
		if (GunStepTime > 1.2f)
		{
			Weapons->ToggleWeapon(); // to Gun
			const int32 Before = ASliceableStructure::GetTotalSliceRequests();
			if (Block) { AimAt(Block->GetActorLocation() + FVector(0.f, 0.f, 20.f)); }
			Weapons->PrimaryAttack();
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("GunTest: round %d gun: slice requests %d -> %d (must not change), weapon=%s"), GunObs + 1, Before, ASliceableStructure::GetTotalSliceRequests(),
				Weapons->GetWeapon() == EDuneWeapon::Gun ? TEXT("Gun") : TEXT("Knife"));
			Weapons->ToggleWeapon(); // to Knife and attack: the knife path runs
			Weapons->PrimaryAttack();
			GunStepTime = 0.f;
			if (++GunObs >= 4) { Report(TEXT("after tab rounds"), Block); Weapons->SetWeapon(EDuneWeapon::Gun); GunObs = 0; Next(12); }
		}
		break;
	case 12: // many shots at the cut block: holes and chips stay under their caps, nothing is cut, marks sit on the pieces
		if (Block) { AimAt(Block->GetActorLocation() + FVector(0.f, 0.f, 20.f + 20.f * (GunObs % 8))); }
		if (GunStepTime > 0.4f)
		{
			const int32 Before = ASliceableStructure::GetTotalSliceRequests();
			const double Start = FPlatformTime::Seconds();
			Weapons->PrimaryAttack();
			if (GunObs % 10 == 0 || GunObs + 1 >= RepeatTarget)
			{
				const ADuneImpactManager* Impacts = ADuneImpactManager::Get(GetWorld());
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("GunTest: repeat shot %d took %.2f ms, slice requests %d -> %d, holes=%d fragments=%d, manager components=%d"), GunObs + 1, (FPlatformTime::Seconds() - Start) * 1000.0,
					Before, ASliceableStructure::GetTotalSliceRequests(), Impacts->GetBulletHoleCount(), Impacts->GetFragmentCount(), Impacts->GetComponents().Num());
			}
			GunStepTime = 0.f;
			if (++GunObs >= RepeatTarget)
			{
				Report(TEXT("after repeated shots"), Block);
				GunObs = 0;
				Next(13);
			}
		}
		break;
	case 13: // chips are gone after their lifetime; the marks stay
		if (bShot && GunStepTime < 4.f)
		{
			AimAt(Block ? Block->GetActorLocation() + FVector(0.f, 0.f, 80.f) : FVector::ZeroVector);
			if (GunStepTime > 3.f && RepeatShots == 1) { PC->ConsoleCommand(TEXT("HighResShot 1280x720 filename=DuneGunHoles")); RepeatShots = 2; } // debug marks have faded
		}
		if (GunStepTime > 10.f)
		{
			Report(TEXT("final"), Block);
			FPlatformMisc::RequestExit(false);
		}
		break;
	}
}

// Fracture self-test (-DuneFractureTest, plus -DuneGunShot for screenshots). Mode 1 (default): the gun. Targets that are not marked
// must not break, the marked building SM_house_07_Sliceable2 breaks into fragments of different sizes that fall, collide and rest.
// Mode 2 (-DuneFractureMode=2): the knife cuts the same building: one slice, no fragments, no gun effect.
void ADuneGameMode::RunFractureTest(float DeltaSeconds, ACharacter* Player)
{
	FracTime += DeltaSeconds;
	FracStepTime += DeltaSeconds;
	APlayerController* PC = Cast<APlayerController>(Player->GetController());
	UDuneWeaponComponent* Weapons = Player->FindComponentByClass<UDuneWeaponComponent>();
	UDuneGunComponent* Gun = Weapons ? Weapons->GetGun() : nullptr;
	UKrisKnifeComponent* Knife = Weapons ? Weapons->GetKnife() : nullptr;
	if (!PC || !Gun || !Knife)
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("FractureTest: no controller, gun or knife on the player"));
		FPlatformMisc::RequestExit(false);
		return;
	}
	const bool bShot = FParse::Param(FCommandLine::Get(), TEXT("DuneGunShot"));
	int32 Mode = 1;
	FParse::Value(FCommandLine::Get(), TEXT("-DuneFractureMode="), Mode);
	int32 ShotsAtTarget = 4; // -DuneFractureShots=N: repeated shots at the target
	FParse::Value(FCommandLine::Get(), TEXT("-DuneFractureShots="), ShotsAtTarget);
	auto Next = [this](int32 S) { FracStep = S; FracStepTime = 0.f; };
	auto Find = [this](const TCHAR* Label) -> AActor*
	{
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (It->GetActorLabel() == Label) { return *It; }
		}
		return nullptr;
	};
	auto AimAt = [PC](const FVector& Point)
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		PC->SetControlRotation((Point - ViewLocation).Rotation());
	};
	auto Counters = [this, Gun, Weapons](const TCHAR* Label)
	{
		const ADuneImpactManager* Impacts = ADuneImpactManager::Get(GetWorld());
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("FractureTest: [%s] weapon=%s | gun shots=%d impacts=%d fractures=%d | slice requests=%d | chips made=%d holes made=%d"), Label,
			Weapons->GetWeapon() == EDuneWeapon::Gun ? TEXT("Gun") : TEXT("Knife"), Gun->GetShots(), Gun->GetImpacts(), Gun->GetFractures(), ASliceableStructure::GetTotalSliceRequests(),
			Impacts->GetTotalFragments(), Impacts->GetTotalBulletHoles());
	};
	// what the pieces of one structure are doing
	auto Pieces = [](const TCHAR* Label, ASliceableStructure* S)
	{
		if (!S) { return; }
		TArray<UProceduralMeshComponent*> All{ S->GetMesh() };
		for (UProceduralMeshComponent* P : S->GetPieces()) { All.Add(P); }
		int32 Simulating = 0, Awake = 0, Visible = 0;
		float MaxSpeed = 0.f, MinZ = MAX_flt, MaxSpin = 0.f;
		for (UProceduralMeshComponent* P : All)
		{
			Visible += P->IsVisible() ? 1 : 0;
			if (P->IsSimulatingPhysics())
			{
				++Simulating;
				Awake += P->RigidBodyIsAwake() ? 1 : 0;
				MaxSpeed = FMath::Max(MaxSpeed, P->GetPhysicsLinearVelocity().Size());
				MaxSpin = FMath::Max(MaxSpin, P->GetPhysicsAngularVelocityInDegrees().Size());
			}
			MinZ = FMath::Min(MinZ, P->Bounds.Origin.Z - P->Bounds.BoxExtent.Z);
		}
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("FractureTest: %s: %d pieces, %d simulating (%d awake), fastest %.0f cm/s, fastest spin %.0f deg/s, lowest bound z %.0f, components on the actor %d, still ticking=%d"), Label,
			All.Num(), Simulating, Awake, MaxSpeed, MaxSpin, MinZ, S->GetComponents().Num(), S->IsActorTickEnabled());
	};
	ASliceableStructure* Target = Cast<ASliceableStructure>(Find(TEXT("SM_house_07_Sliceable2"))); // becomes a structure on its first hit
	const AActor* TargetActor = Find(TEXT("SM_house_07_Sliceable2"));
	if (TargetActor && !bFracCentre) { FracCentre = TargetActor->GetActorLocation(); bFracCentre = true; } // the original place: the root piece moves once it falls
	const FVector TargetCentre = bFracCentre ? FracCentre : FVector(-640.f, 890.f, 0.f);

	switch (FracStep)
	{
	case 0:
		if (FracStepTime > 0.5f)
		{
			FracBase = ASliceableStructure::GetTotalSliceRequests();
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("FractureTest: mode %d, start weapon=%s, slice requests=%d, target %s (%s)"), Mode, Weapons->GetWeapon() == EDuneWeapon::Gun ? TEXT("Gun") : TEXT("Knife"),
				FracBase, TargetActor ? *TargetActor->GetName() : TEXT("MISSING"), TargetActor ? *TargetActor->GetClass()->GetName() : TEXT(""));
			Next(Mode == 2 ? 20 : 1);
		}
		break;
	case 1: // targets that are not marked: the bullet may chip them but must not break them
	{
		static const TCHAR* Labels[] = { TEXT("SM_house_07_Sliceable"), TEXT("Slice_House"), TEXT("Slice_Pillar"), TEXT("Kit_house_07_1") };
		const AActor* A = Find(Labels[FMath::Min(FracAux, 3)]);
		if (A) { AimAt(A->GetActorLocation() + FVector(0.f, 0.f, 200.f)); }
		if (FracStepTime > 0.9f)
		{
			Weapons->PrimaryAttack();
			const ASliceableStructure* S = Cast<ASliceableStructure>(const_cast<AActor*>(A));
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("FractureTest: shot at %s (%s): fractures so far %d (0 expected), %s"), Labels[FMath::Min(FracAux, 3)], A ? *A->GetClass()->GetName() : TEXT("missing"), Gun->GetFractures(),
				S ? *FString::Printf(TEXT("pieces %d (1 expected)"), S->GetPieces().Num() + 1) : TEXT("not a structure, class unchanged"));
			FracStepTime = 0.f;
			if (++FracAux >= 4) { Counters(TEXT("not marked: no fracture")); FracAux = 0; Next(2); }
		}
		break;
	}
	case 2: // the marked building: the first shot
		AimAt(TargetCentre + FVector(0.f, 0.f, 260.f));
		if (FracStepTime > 1.0f)
		{
			if (bShot) { PC->ConsoleCommand(TEXT("HighResShot 1280x720 filename=DuneFractureBefore")); }
			Weapons->PrimaryAttack();
			Counters(TEXT("first shot at the marked building"));
			FracAux = 0;
			Next(3);
		}
		break;
	case 3: // watch the fragments: movement, collisions, rest
	{
		static const float Times[] = { 0.15f, 0.6f, 1.5f, 3.f, 6.f, 12.f };
		ASliceableStructure* S = Cast<ASliceableStructure>(Find(TEXT("SM_house_07_Sliceable2")));
		if (FracAux < UE_ARRAY_COUNT(Times) && FracStepTime > Times[FracAux])
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("FractureTest: t=%.2fs after the shot, building is now a %s"), FracStepTime, S ? *S->GetClass()->GetName() : TEXT("?"));
			Pieces(TEXT("fragments"), S);
			if (bShot && FracAux < 4) { PC->ConsoleCommand(FString::Printf(TEXT("HighResShot 1280x720 filename=DuneFracture%d"), FracAux)); }
			if (S && FracAux == 0)
			{
				const ASliceableStructure::FFractureStats& F = S->GetLastFracture();
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("FractureTest: last fracture: %d fragments (large %d, medium %d, small %d), %d dust, %d cuts, %d wall pieces, %.1f ms, volumes %.0f .. %.0f cm3"), F.Fragments, F.Large, F.Medium, F.Small,
					F.Dust, F.Cuts, F.OutsidePieces, F.Milliseconds, F.MinVolumeCm3, F.MaxVolumeCm3);
			}
			++FracAux;
		}
		if (FracAux >= UE_ARRAY_COUNT(Times) && FracStepTime > 13.f) { FracAux = 0; Next(4); }
		break;
	}
	case 4: // more shots at the same building: other places, the pieces that are left, the fragments on the ground
	{
		ASliceableStructure* S = Cast<ASliceableStructure>(Find(TEXT("SM_house_07_Sliceable2")));
		static const FVector Offsets[] = { FVector(150.f, 0.f, 120.f), FVector(-170.f, 0.f, 300.f), FVector(0.f, 0.f, 40.f), FVector(60.f, 0.f, 260.f) };
		AimAt(TargetCentre + Offsets[FMath::Min(FracAux, 3)]);
		if (FracStepTime > 1.2f)
		{
			const double Start = FPlatformTime::Seconds();
			Weapons->PrimaryAttack();
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("FractureTest: repeat shot %d took %.1f ms"), FracAux + 1, (FPlatformTime::Seconds() - Start) * 1000.0);
			Pieces(TEXT("after the repeat shot"), S);
			FracStepTime = 0.f;
			if (++FracAux >= ShotsAtTarget - 1) { Counters(TEXT("repeated shots")); Next(5); }
		}
		break;
	}
	case 5:
		if (bShot) { AimAt(TargetCentre + FVector(0.f, -120.f, 40.f)); } // look at the foot of the building: where the fragments came to rest
		if (FracStepTime > 9.f)
		{
			ASliceableStructure* S = Cast<ASliceableStructure>(Find(TEXT("SM_house_07_Sliceable2")));
			Counters(TEXT("final"));
			Pieces(TEXT("final"), S);
			if (bShot) { PC->ConsoleCommand(TEXT("HighResShot 1280x720 filename=DuneFractureFinal")); }
			Next(99);
		}
		break;
		break;

	// mode 2: the knife on the same building
	case 20:
	{
		Weapons->SetWeapon(EDuneWeapon::Knife);
		AimAt(TargetCentre + FVector(0.f, 0.f, 100.f));
		Player->AddMovementInput((TargetCentre - Player->GetActorLocation()).GetSafeNormal2D(), 1.f);
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		FHitResult Hit;
		FCollisionQueryParams Params(NAME_None, false, Player);
		if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, ViewLocation + ViewRotation.Vector() * 3000.f, ECC_Visibility, Params)
			&& Hit.GetActor() == TargetActor && FVector::Dist(Player->GetActorLocation(), Hit.ImpactPoint) <= 230.f)
		{
			Next(21);
		}
		break;
	}
	case 21:
		if (FracStepTime > 0.8f)
		{
			Counters(TEXT("before the knife"));
			Weapons->PrimaryAttack();
			Next(22);
		}
		break;
	case 22:
	{
		ASliceableStructure* S = Cast<ASliceableStructure>(Find(TEXT("SM_house_07_Sliceable2")));
		if (FracStepTime > 1.0f && FracAux == 0)
		{
			Counters(TEXT("after the knife cut"));
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("FractureTest: knife: slices=%d, building has %d pieces, gun fractures=%d (0 expected)"), Knife->GetSlices(), S ? S->GetPieces().Num() + 1 : 0, Gun->GetFractures());
			Pieces(TEXT("knife pieces"), S);
			if (bShot) { PC->SetControlRotation(FRotator(-5.f, PC->GetControlRotation().Yaw, 0.f)); PC->ConsoleCommand(TEXT("HighResShot 1280x720 filename=DuneKnifeOnTarget")); }
			FracAux = 1;
		}
		if (FracStepTime > 6.f)
		{
			Pieces(TEXT("knife pieces after 6 s"), S);
			Counters(TEXT("final (knife)"));
			Next(99);
		}
		break;
	}
	default:
		if (FracStepTime > 0.5f) { FPlatformMisc::RequestExit(false); }
		break;
	}
}

// Knife self-test (-DuneKnifeTest): drives the same Attack() the mouse button calls and the player's control rotation,
// then logs what happened. Aim and swing are real; nothing is cut unless the knife's own hit test finds a structure.
void ADuneGameMode::RunKnifeTest(float DeltaSeconds, ACharacter* Player)
{
	KnifeTime += DeltaSeconds;
	StepTime += DeltaSeconds;
	APlayerController* PC = Cast<APlayerController>(Player->GetController());
	UKrisKnifeComponent* Knife = Player->FindComponentByClass<UKrisKnifeComponent>();
	if (!PC || !Knife)
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("KnifeTest: no controller or knife on the player"));
		FPlatformMisc::RequestExit(false);
		return;
	}
	FString TargetLabel = TEXT("Slice_Block");
	FParse::Value(FCommandLine::Get(), TEXT("-DuneKnifeTarget="), TargetLabel);
	const bool bKnifeShot = FParse::Param(FCommandLine::Get(), TEXT("DuneKnifeShot"));
	AActor* Target = nullptr;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->GetActorLabel() == TargetLabel) { Target = *It; }
	}
	ASliceableStructure* Block = Cast<ASliceableStructure>(Target);
	const FBox TargetBox = Target ? Target->GetComponentsBoundingBox() : FBox(ForceInit);
	const FVector TargetCenter = TargetBox.GetCenter();
	const FVector ToTarget = (TargetCenter - Player->GetActorLocation()) * FVector(1.f, 1.f, 0.f);
	const float AimYaw = FMath::RadiansToDegrees(FMath::Atan2(ToTarget.Y, ToTarget.X)); // the view is turned to the target once, like a player would
	auto Aim = [PC, AimYaw](float Pitch, float YawOffset = 0.f) { PC->SetControlRotation(FRotator(Pitch, AimYaw + YawOffset, 0.f)); };
	const bool bDiagonal = FParse::Param(FCommandLine::Get(), TEXT("DuneKnifeDiagonal")); // first swing after a diagonal mouse movement
	const bool bPhysics = bKnifeShot || FParse::Param(FCommandLine::Get(), TEXT("DuneKnifePhysics"));
	auto Next = [this](int32 S) { KnifeStep = S; StepTime = 0.f; };

	switch (KnifeStep)
	{
	case 0: // let the level settle, report the setup
		if (StepTime > 0.5f)
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("KnifeTest: knife parts=%d hand bone found=%d target=%s (%s) sliceable=%s player at %s"),
				Knife->GetPartCount(), Knife->HasHandBone(), *TargetLabel, Target ? *Target->GetClass()->GetName() : TEXT("NOT FOUND"),
				Block ? TEXT("yes") : TEXT("no"), *Player->GetActorLocation().ToString());
			Knife->ConvertedSeparation = 0.f; // physics moves the pieces now, no artificial gap
			Target ? Next(1) : Next(99);
		}
		break;
	case 1: // swing at the sky
		Aim(60.f);
		if (StepTime > 0.3f) { Knife->Attack(); Next(2); }
		break;
	case 2:
		if (StepTime > 0.8f)
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("KnifeTest: [air] slices=%d misses=%d (0 slices expected)"), Knife->GetSlices(), Knife->GetMisses());
			Aim(0.f);
			Next(3);
		}
		break;
	case 3: // swing at the block from the start position, 6 m away: out of reach
		if (StepTime > 0.3f) { Knife->Attack(); Next(4); }
		break;
	case 4:
		if (StepTime > 0.8f)
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("KnifeTest: [far] slices=%d misses=%d (0 slices expected)"), Knife->GetSlices(), Knife->GetMisses());
			Next(5);
		}
		break;
	case 5: // walk up to the block with the normal movement input
	{
		Aim(0.f);
		Player->AddMovementInput(ToTarget.GetSafeNormal(), 1.f);
		// walk until the surface under the crosshair is within arm's reach (the same aim trace the knife uses)
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		FHitResult Hit;
		FCollisionQueryParams Params(NAME_None, false, Player);
		if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, ViewLocation + ViewRotation.Vector() * 3000.f, ECC_Visibility, Params)
			&& Hit.GetActor() == Target && FVector::Dist(Player->GetActorLocation(), Hit.ImpactPoint) <= 230.f)
		{
			Next(6);
		}
		break;
	}
	case 6: // settle, then swing twice in a row
	{
		// -DuneKnifeHigh looks 30 degrees up first (the cut lands higher on the building); the view is held for 1.2 s so
		// that this movement is forgotten before the swing, and the cut direction comes from the movement that follows
		const float Base = FParse::Param(FCommandLine::Get(), TEXT("DuneKnifeHigh")) ? 30.f : 0.f;
		const float Alpha = bDiagonal ? FMath::Clamp((StepTime - 1.2f) / 0.1f, 0.f, 1.f) : 0.f; // diagonal: right and up together
		Aim(Base + 4.f * Alpha, 4.f * Alpha);
		if (StepTime > 1.35f)
		{
			Knife->Attack();
			Next(7);
		}
		break;
	}
	case 7:
		if (StepTime > 0.12f) { Knife->Attack(); Next(8); } // second input while the first swing runs
		break;
	case 8:
		if (StepTime > 0.8f)
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("KnifeTest: [near, no mouse movement] slices=%d misses=%d ignored inputs=%d normal=%s pieces=%d"),
				Knife->GetSlices(), Knife->GetMisses(), Knife->GetIgnoredInputs(), *Knife->GetLastPlaneNormal().ToString(), Block ? Block->GetPieces().Num() : -1);
			Next(Block ? (bPhysics ? 97 : 9) : 99); // an environment mesh must not be cut: nothing more to do with it
		}
		break;
	case 9: // mouse moves up by 4 degrees over 0.1 s, then the swing: a vertical slash
	{
		const float Alpha = FMath::Clamp(StepTime / 0.1f, 0.f, 1.f);
		Aim(4.f * Alpha);
		if (StepTime > 0.15f) { Knife->Attack(); Next(10); }
		break;
	}
	case 10:
		if (StepTime > 0.8f)
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("KnifeTest: [near, mouse up] slices=%d misses=%d normal=%s pieces=%d"),
				Knife->GetSlices(), Knife->GetMisses(), *Knife->GetLastPlaneNormal().ToString(), Block->GetPieces().Num());
			// collision of the pieces: a trace from the side must hit each of them
			TArray<UProceduralMeshComponent*> All{ Block->GetMesh() };
			for (UProceduralMeshComponent* Piece : Block->GetPieces()) { All.Add(Piece); }
			for (UProceduralMeshComponent* Piece : All)
			{
				FHitResult Hit;
				const FVector Center = Piece->Bounds.Origin;
				FCollisionQueryParams Params(NAME_None, false, Player);
				const FVector Side = FRotator(0.f, AimYaw, 0.f).Vector() * -400.f; // trace from the player's side, towards the piece
				const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Center + Side, Center, ECC_Visibility, Params);
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("KnifeTest: piece %s collision=%d trace from the side hits %s"),
					*Piece->GetName(), static_cast<int32>(Piece->GetCollisionEnabled()), bHit && Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("nothing"));
			}
			Block->LogMeshInfo(TEXT("after knife test"));
			Next(99);
		}
		break;
	case 97: // physics observation: state of every piece at fixed times after the cut (+ screenshots with -DuneKnifeShot)
	{
		static const float Times[] = { 0.2f, 1.0f, 2.5f, 5.f, 10.f, 15.f };
		Aim(bKnifeShot ? 12.f : 0.f); // look at the cut, not at the sky
		if (ObsIndex < UE_ARRAY_COUNT(Times) && StepTime > Times[ObsIndex] && Block)
		{
			TArray<UProceduralMeshComponent*> All{ Block->GetMesh() };
			for (UProceduralMeshComponent* Piece : Block->GetPieces()) { All.Add(Piece); }
			for (UProceduralMeshComponent* Piece : All)
			{
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("KnifePhysics: t=%.1fs %s simulating=%d awake=%d at %s speed %.1f cm/s lowest z %.0f"), StepTime, *Piece->GetName(),
					Piece->IsSimulatingPhysics(), Piece->IsSimulatingPhysics() ? Piece->RigidBodyIsAwake() : 0, *Piece->GetComponentLocation().ToString(),
					Piece->GetPhysicsLinearVelocity().Size(), Piece->Bounds.Origin.Z - Piece->Bounds.BoxExtent.Z);
			}
			if (bKnifeShot && ObsIndex < 4)
			{
				PC->ConsoleCommand(FString::Printf(TEXT("HighResShot 1280x720 filename=DuneCut%d"), ObsIndex));
			}
			++ObsIndex;
		}
		if (ObsIndex >= UE_ARRAY_COUNT(Times) && StepTime > 16.f) { FPlatformMisc::RequestExit(false); }
		break;
	}
	default:
		if (StepTime > 0.3f) { FPlatformMisc::RequestExit(false); }
		break;
	}
}

void ADuneGameMode::RunSliceRequests()
{
	ASliceableStructure* Target = nullptr;
	for (TActorIterator<ASliceableStructure> It(GetWorld()); It; ++It)
	{
		Target = *It;
		break;
	}
	if (!Target)
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("DuneTest: no sliceable structure in the level"));
		return;
	}
	const FVector Center = Target->GetMesh()->Bounds.Origin;

	// failure paths first, none of them may change the mesh
	Target->SliceMeshWithPlane(Center, FVector::ZeroVector);
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("DuneTest: zero normal -> result %d (InvalidNormal=3 expected)"), static_cast<int32>(Target->GetLastReport().Result));
	Target->SliceMeshWithPlane(Center + FVector(0.f, 0.f, 100000.f), FVector::UpVector);
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("DuneTest: plane far above -> result %d (PlaneMissesMesh=4 expected)"), static_cast<int32>(Target->GetLastReport().Result));

	ASliceableStructure* Empty = GetWorld()->SpawnActorDeferred<ASliceableStructure>(ASliceableStructure::StaticClass(), FTransform(Center + FVector(0.f, 0.f, 500.f)));
	Empty->SourceMesh = nullptr;
	Empty->FinishSpawning(Empty->GetActorTransform());
	Empty->SliceMeshWithPlane(Empty->GetActorLocation(), FVector::UpVector);
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("DuneTest: structure without mesh -> result %d (NoMeshData=2 expected)"), static_cast<int32>(Empty->GetLastReport().Result));

	Target->LogMeshInfo(TEXT("before cut"));
	const bool bCut = Target->SliceMeshWithPlane(Center, FVector::UpVector);
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("DuneTest: horizontal cut through the centre -> %d (pieces %d)"), bCut, Target->GetPieces().Num());
	Target->LogMeshInfo(TEXT("after cut"));
	// a second, oblique cut on the remaining half
	const bool bCut2 = Target->SliceMeshWithPlane(Target->GetMesh()->Bounds.Origin, FVector(1.f, 0.f, 1.f));
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("DuneTest: oblique cut of the remaining half -> %d (pieces %d)"), bCut2, Target->GetPieces().Num());
}

// Sand trail self-test (-DuneSandTest, plus -DuneSandShot for screenshots). It needs a rendered run (the trail lives in a render target).
// Walks along a line over the sand, stands, jumps, steps on a rock, walks the same line again, then waits for the trail to fade,
// and reads the height texture back after each phase.
void ADuneGameMode::RunSandTest(float DeltaSeconds, ACharacter* Player)
{
	SandTime += DeltaSeconds;
	SandStepTime += DeltaSeconds;
	APlayerController* PC = Cast<APlayerController>(Player->GetController());
	if (!PC || !SandTrail || !SandTrail->IsReady())
	{
		if (SandTime > 5.f && !SandTrail)
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("SandTest: no sand trail actor, test skipped"));
			FPlatformMisc::RequestExit(false);
		}
		return;
	}
	const bool bShot = FParse::Param(FCommandLine::Get(), TEXT("DuneSandShot"));
	float Warm = 50.f; // shader compilation of the sand and the brush material
	FParse::Value(FCommandLine::Get(), TEXT("DuneSandWarm="), Warm);
	auto Go = [this](int32 Next) { SandStep = Next; SandStepTime = 0.f; };
	auto Teleport = [this, Player](float X, float Y)
	{
		FHitResult Hit;
		GetWorld()->LineTraceSingleByChannel(Hit, FVector(X, Y, 5000.f), FVector(X, Y, -1000.f), ECC_Visibility);
		Player->SetActorLocation(FVector(X, Y, Hit.ImpactPoint.Z + Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), false, nullptr, ETeleportType::TeleportPhysics);
		Player->GetCharacterMovement()->Velocity = FVector::ZeroVector;
		return Hit.ImpactPoint.Z;
	};
	auto Stats = [this](const TCHAR* When)
	{
		const FSandTrailStats S = SandTrail->ReadStats();
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandTest: [%s] texture: dent pixels %d (min %.2f cm), ridge pixels %d (max %.2f cm), dent area x %.0f..%.0f y %.0f..%.0f | brush updates %d, recovery passes %d, skipped still %d air %d not-sand %d outside %d"),
			When, S.DentPixels, S.MinCm, S.RidgePixels, S.MaxCm, S.DentMinWorld.X, S.DentMaxWorld.X, S.DentMinWorld.Y, S.DentMaxWorld.Y,
			SandTrail->GetBrushUpdates(), SandTrail->GetRecoveryPasses(), SandTrail->GetSkippedStill(), SandTrail->GetSkippedAir(), SandTrail->GetSkippedNotSand(), SandTrail->GetSkippedOutside());
	};
	const float Line = -1700.f; // the walking line: along +X at this Y, inside the patch, clear of rocks and structures
	const float Left = -2300.f;

	switch (SandStep)
	{
	case 0: // wait for the shaders, then start from the left end of the line
		if (SandTime > Warm)
		{
			SandTrail->TrailPersistence = 60.f;
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandTest: start. patch %d vertices, trail memory %.1f MB, persistence %.0f s, width %.0f depth %.1f ridge %.1f spacing %.0f"),
				SandTrail->GetPatchVertices(), SandTrail->GetTrailMemoryMB(), SandTrail->TrailPersistence, SandTrail->TrailWidth, SandTrail->TrailDepth, SandTrail->EdgeRidgeStrength, SandTrail->TrailSpacing);
			Teleport(Left, Line);
			PC->SetControlRotation(FRotator(-15.f, 0.f, 0.f));
			Go(1);
		}
		break;
	case 1: // standing on fresh sand: nothing must be recorded
		++IdleFrames; IdleMs += DeltaSeconds * 1000.0;
		if (SandStepTime > 2.f)
		{
			Stats(TEXT("standing at the start"));
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandTest: on sand=%d at %s"), SandTrail->IsOnSand(Player), *Player->GetActorLocation().ToString());
			Go(2);
		}
		break;
	case 2: // walk along +X
		Player->AddMovementInput(FVector::ForwardVector, 1.f);
		++WalkFrames; WalkMs += DeltaSeconds * 1000.0;
		if (SandStepTime > 2.8f)
		{
			const FVector P = Player->GetActorLocation();
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandTest: pass %d walked to %s speed %.0f"), SandPass + 1, *P.ToString(), Player->GetVelocity().Size2D());
			Stats(TEXT("after the walk"));
			for (float X : { -2000.f, -1500.f, -1000.f })
			{
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandTest: height on the line at x=%.0f: %.2f cm | 28 cm beside: %.2f | 60 cm beside: %.2f | 300 cm beside: %.2f"), X,
					SandTrail->SampleHeightCm(FVector(X, Line, 0.f)), SandTrail->SampleHeightCm(FVector(X, Line + 28.f, 0.f)),
					SandTrail->SampleHeightCm(FVector(X, Line + 60.f, 0.f)), SandTrail->SampleHeightCm(FVector(X, Line + 300.f, 0.f)));
			}
			SandMark = SandTrail->GetBrushUpdates();
			++SandPass;
			Go(SandPass == 1 ? 3 : 6);
		}
		break;
	case 3: // standing after walking: no new records
		++IdleFrames; IdleMs += DeltaSeconds * 1000.0;
		if (SandStepTime > 2.f)
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandTest: standing 2 s: brush updates %d -> %d (expected equal)"), SandMark, SandTrail->GetBrushUpdates());
			SandMark = SandTrail->GetBrushUpdates();
			SandMarkAir = SandTrail->GetSkippedAir();
			Player->Jump();
			Go(4);
		}
		break;
	case 4: // jump and keep moving: nothing is recorded in the air
		Player->AddMovementInput(FVector::ForwardVector, 1.f);
		if (SandStepTime > 0.4f)
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandTest: after 0.4 s of a jump (airborne=%d): brush updates %d -> %d, skipped air frames +%d"),
				Player->GetCharacterMovement()->IsFalling(), SandMark, SandTrail->GetBrushUpdates(), SandTrail->GetSkippedAir() - SandMarkAir);
			Go(5);
		}
		break;
	case 5: // land, then stand on a rock (not sand) and move a little
		if (SandStepTime > 2.f)
		{
			const float Top = Teleport(-1500.f, -600.f);
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandTest: rock top at z=%.0f"), Top);
			Go(7);
		}
		break;
	case 7:
		if (SandStepTime > 1.5f)
		{
			SandMark = SandTrail->GetBrushUpdates();
			SandMarkNotSand = SandTrail->GetSkippedNotSand();
			Go(8);
		}
		break;
	case 8:
		Player->AddMovementInput(FVector(0.f, 1.f, 0.f), 0.2f);
		if (SandStepTime > 0.5f)
		{
			const AActor* Floor = Player->GetCharacterMovement()->CurrentFloor.HitResult.GetActor();
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandTest: on %s (on sand=%d, speed %.0f): brush updates %d -> %d, not-sand frames +%d, height there %.2f cm"), *GetNameSafe(Floor),
				SandTrail->IsOnSand(Player), Player->GetVelocity().Size2D(), SandMark, SandTrail->GetBrushUpdates(), SandTrail->GetSkippedNotSand() - SandMarkNotSand,
				SandTrail->SampleHeightCm(Player->GetActorLocation()));
			Teleport(Left, Line);
			Go(2);
		}
		break;
	case 6: // the second and third pass over the same line: the trail must not get deeper than TrailDepth
		if (SandPass < 3)
		{
			Teleport(Left, Line);
			Go(2);
			break;
		}
		{
			Stats(TEXT("after 3 passes over the same line"));
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandTest: frame time idle %.2f ms (%d frames), walking %.2f ms (%d frames)"), IdleFrames ? IdleMs / IdleFrames : 0.0, IdleFrames, WalkFrames ? WalkMs / WalkFrames : 0.0, WalkFrames);
			Go(bShot ? 9 : 14);
		}
		break;
	case 9: // screenshots: looking back along the trail, from the side, close, and across it at a grazing angle
	case 10:
	case 11:
	case 12:
	{
		static const FRotator Views[] = { FRotator(-18.f, 180.f, 0.f), FRotator(-30.f, 120.f, 0.f), FRotator(-50.f, 180.f, 0.f), FRotator(-3.f, 90.f, 0.f) };
		const int32 View = SandStep - 9;
		PC->SetControlRotation(Views[View]); // every frame: something else may turn the camera back
		if (SandStepTime > 2.5f)
		{
			PC->ConsoleCommand(FString::Printf(TEXT("HighResShot 1280x720 filename=DuneSandTrail%d"), View));
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandTest: view %d requested, pawn %s, camera %s"), View, *Player->GetActorLocation().ToString(), *PC->PlayerCameraManager->GetCameraLocation().ToString());
			Go(SandStep == 12 ? 14 : SandStep + 1);
		}
		break;
	}
	case 14: // let the trail fade: short persistence, then wait it out
		SandMark = 0;
		SandTrail->TrailPersistence = 6.f;
		Go(15);
		break;
	case 15:
		if (SandStepTime > 9.f && SandMark == 0)
		{
			SandMark = 1;
			Stats(TEXT("9 s into a 6 s persistence"));
		}
		if (SandStepTime > 14.f)
		{
			Stats(TEXT("14 s after, trail should be gone"));
			FPlatformMisc::RequestExit(false);
		}
		break;
	}
}

// City art shots (-DuneCityShot=<file>). Each line of the file: name x y z pitch yaw fov. The pawn is hidden; a camera actor is the view
// target. A line starting with # is skipped. The frame time is averaged over the whole run (a rough number, one machine).
void ADuneGameMode::RunCityShots(float DeltaSeconds, ACharacter* Player)
{
	CityTime += DeltaSeconds;
	CityStepTime += DeltaSeconds;
	APlayerController* PC = Cast<APlayerController>(Player->GetController());
	if (!PC)
	{
		return;
	}
	float Warm = 60.f; // shaders of the whole city
	FParse::Value(FCommandLine::Get(), TEXT("DuneCityWarm="), Warm);
	if (CityTime > Warm)
	{
		CityFrameMs += DeltaSeconds * 1000.0;
		++CityFrames;
	}
	while (CityStep < CityViews.Num() && (CityViews[CityStep].IsEmpty() || CityViews[CityStep].StartsWith(TEXT("#"))))
	{
		++CityStep;
	}
	if (CityStep >= CityViews.Num())
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("CityShot: done, average frame time %.2f ms over %d frames (windowed 1280x720, this machine)"), CityFrames ? CityFrameMs / CityFrames : 0.0, CityFrames);
		FPlatformMisc::RequestExit(false);
		return;
	}
	if (CityTime < Warm)
	{
		return;
	}
	TArray<FString> Parts;
	CityViews[CityStep].ParseIntoArray(Parts, TEXT(" "), true);
	if (Parts.Num() < 7)
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("CityShot: bad line %d: %s"), CityStep, *CityViews[CityStep]);
		++CityStep;
		return;
	}
	if (!CityCamera)
	{
		Player->SetActorHiddenInGame(true);
		CityCamera = GetWorld()->SpawnActor<ACameraActor>();
		PC->SetViewTarget(CityCamera);
		CityStepTime = 0.f;
	}
	CityCamera->SetActorLocationAndRotation(FVector(FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]), FCString::Atof(*Parts[3])),
		FRotator(FCString::Atof(*Parts[4]), FCString::Atof(*Parts[5]), 0.f));
	CityCamera->GetCameraComponent()->SetFieldOfView(FCString::Atof(*Parts[6]));
	if (CityStepTime > 3.f)
	{
		PC->ConsoleCommand(FString::Printf(TEXT("HighResShot 1920x1080 filename=City_%s"), *Parts[0]));
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("CityShot: %s requested"), *Parts[0]);
		++CityStep;
		CityStepTime = 0.f;
	}
}

// City walk (-DuneCityWalk): drives the movement input along waypoints, the way the keyboard would. A waypoint counts as reached within
// 150 cm; a leg that makes less than 150 cm of progress in 3 s is logged as blocked and skipped. Frame times are per leg (after the first 20 s of shader warm-up).
void ADuneGameMode::RunCityWalk(float DeltaSeconds, ACharacter* Player)
{
	struct FLeg { const TCHAR* Name; FVector2D To; };
	static const FLeg Route[] = {
		{ TEXT("sand area, south of the cut structures"), FVector2D(-400.f, -1000.f) },
		{ TEXT("main street, east"), FVector2D(1000.f, -1000.f) },
		{ TEXT("main street, middle"), FVector2D(2200.f, -600.f) },
		{ TEXT("lane between the north houses"), FVector2D(1100.f, 700.f) },
		{ TEXT("lane, far end"), FVector2D(1100.f, 2350.f) },
		{ TEXT("back to the street"), FVector2D(1100.f, 300.f) },
		{ TEXT("plaza, before the landmark"), FVector2D(3600.f, -200.f) },
		{ TEXT("plaza, past the stairs"), FVector2D(4300.f, -900.f) },
		{ TEXT("plaza, south of the pillars"), FVector2D(5200.f, -1000.f) },
		{ TEXT("back terraces"), FVector2D(6300.f, -200.f) },
		{ TEXT("terraces, back to the plaza"), FVector2D(5200.f, -1000.f) },
		{ TEXT("way back, main street"), FVector2D(3000.f, -1300.f) },
		{ TEXT("way back, sand area"), FVector2D(-1500.f, -1300.f) },
		{ TEXT("west, canyon entrance"), FVector2D(-3800.f, -300.f) },
	};
	const int32 Legs = UE_ARRAY_COUNT(Route);
	WalkTime += DeltaSeconds;
	if (WalkTime > 20.f)
	{
		const double Ms = DeltaSeconds * 1000.0;
		WalkMsSeg += Ms; WalkMsAll += Ms; WalkMsMax = FMath::Max(WalkMsMax, Ms);
		++WalkFramesSeg; ++WalkFramesAll;
	}
	if (WalkTime < 20.f)
	{
		return; // shaders, the first frames
	}
	if (Player->GetActorLocation().Z < -500.f)
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("CityWalk: the player FELL out of the world at %s"), *Player->GetActorLocation().ToString());
		FPlatformMisc::RequestExit(false);
		return;
	}
	if (WalkIdx >= Legs)
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("CityWalk: route finished, %d of %d legs blocked, average frame time %.2f ms (max %.1f ms) over %d frames"), WalkBlocked, Legs,
			WalkFramesAll ? WalkMsAll / WalkFramesAll : 0.0, WalkMsMax, WalkFramesAll);
		FPlatformMisc::RequestExit(false);
		return;
	}
	const FVector Pos = Player->GetActorLocation();
	const FVector2D Target = Route[WalkIdx].To;
	const FVector2D Delta = Target - FVector2D(Pos.X, Pos.Y);
	WalkSegTime += DeltaSeconds;
	if (WalkSegTime == DeltaSeconds)
	{
		WalkLast = Pos;
		WalkStuckTime = 0.f;
	}
	WalkStuckTime += DeltaSeconds;
	if (WalkStuckTime > 3.f)
	{
		if (FVector::Dist2D(Pos, WalkLast) < 150.f)
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("CityWalk: leg %d (%s) BLOCKED at %s, %.0f cm from the waypoint"), WalkIdx, Route[WalkIdx].Name, *Pos.ToString(), Delta.Size());
			++WalkBlocked;
			WalkIdx = FMath::Min(WalkIdx + 1, Legs);
			WalkSegTime = 0.f; WalkMsSeg = 0.0; WalkFramesSeg = 0;
			return;
		}
		WalkLast = Pos;
		WalkStuckTime = 0.f;
	}
	if (Delta.Size() < 150.f || WalkSegTime > 40.f)
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("CityWalk: leg %d (%s) %s at %s in %.1f s, ground z %.0f, frame time %.2f ms"), WalkIdx, Route[WalkIdx].Name, Delta.Size() < 150.f ? TEXT("reached") : TEXT("TIMEOUT"),
			*Pos.ToString(), WalkSegTime, Pos.Z - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), WalkFramesSeg ? WalkMsSeg / WalkFramesSeg : 0.0);
		if (Delta.Size() >= 150.f) { ++WalkBlocked; }
		++WalkIdx;
		WalkSegTime = 0.f; WalkMsSeg = 0.0; WalkFramesSeg = 0;
		return;
	}
	Player->AddMovementInput(FVector(Delta.X, Delta.Y, 0.f).GetSafeNormal(), 1.f);
}
