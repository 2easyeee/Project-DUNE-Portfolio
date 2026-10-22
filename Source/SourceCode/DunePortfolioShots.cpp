// Copyright Epic Games, Inc. All Rights Reserved.

// Day 18 portfolio screenshots (-DunePortfolio=knife|gun|sand). Rendered run only. It runs the real systems (knife swing, gun shot,
// the player's own movement and landing, the sand simulation) and takes 2560x1440 HighResShots from a free camera; nothing is
// composited. The camera is only the view target while a shot is taken. Shots land in Saved/Screenshots/WindowsEditor/.
//   -DunePfWarm=<s>   shader warm-up (default 45)
//   -DunePfCam=x,y,z,pitch,yaw,fov   camera of the building shots (default: 3/4 view from the south-west, computed from the bounds)
//   -DunePfCam2=...                   camera of the physics shot (default: camera 1)
//   -DunePfSandCam=...                camera of 04 / 05 / 06 (default: see below, one value per shot: -DunePfCam4=, -DunePfCam5=, -DunePfCam6=)
//   -DunePfRes=2560x1440

#include "Dune/DuneGameMode.h"
#include "Dune/DuneSandTrail.h"
#include "Dune/DuneGunComponent.h"
#include "Dune/DuneWeaponComponent.h"
#include "Dune/KrisKnifeComponent.h"
#include "Dune/SliceableStructure.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UE_HOLDITTOGETHER.h"

namespace
{
	/** "x;y;z;pitch;yaw;fov" from the command line */
	bool ParseCam(const TCHAR* Key, FVector& Loc, FRotator& Rot, float& Fov)
	{
		FString Text;
		TArray<FString> P;
		if (!FParse::Value(FCommandLine::Get(), Key, Text) || Text.ParseIntoArray(P, TEXT(";"), true) < 6)
		{
			return false;
		}
		Loc = FVector(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]));
		Rot = FRotator(FCString::Atof(*P[3]), FCString::Atof(*P[4]), 0.f);
		Fov = FCString::Atof(*P[5]);
		return true;
	}
}

void ADuneGameMode::RunPortfolio(float DeltaSeconds, ACharacter* Player)
{
	PfTime += DeltaSeconds;
	PfStepTime += DeltaSeconds;
	APlayerController* PC = Cast<APlayerController>(Player->GetController());
	UDuneWeaponComponent* Weapons = Player->FindComponentByClass<UDuneWeaponComponent>();
	if (!PC || !Weapons)
	{
		return;
	}
	float Warm = 45.f;
	FParse::Value(FCommandLine::Get(), TEXT("DunePfWarm="), Warm);
	FString Res = TEXT("2560x1440");
	FParse::Value(FCommandLine::Get(), TEXT("DunePfRes="), Res);
	if (PC->GetHUD() && !PfMode.StartsWith(TEXT("play"))) { PC->GetHUD()->bShowHUD = false; } // no crosshair or weapon text in the shots
	UKismetSystemLibrary::FlushPersistentDebugLines(GetWorld()); // the cutting plane and the slash arrows the knife draws for debugging
	float SlashYaw = 4.f, SlashPitch = 4.f; // mouse movement of the slash (degrees): the ratio sets the tilt of the cutting plane
	FParse::Value(FCommandLine::Get(), TEXT("DunePfSlashYaw="), SlashYaw);
	FParse::Value(FCommandLine::Get(), TEXT("DunePfSlashPitch="), SlashPitch);
	float AimZ = 260.f; // height of the crosshair on the building (cm above its origin)
	FParse::Value(FCommandLine::Get(), TEXT("DunePfAimZ="), AimZ);
	auto Go = [this](int32 S) { PfStep = S; PfStepTime = 0.f; };

	// a pending shot: the free camera takes the view, waits, requests the screenshot, gives the view back two ticks later
	if (!PfShotName.IsEmpty() || PfShotTicksLeft >= 0)
	{
		if (PfShotTicksLeft >= 0)
		{
			if (--PfShotTicksLeft < 0)
			{
				PC->SetViewTarget(Player);
				Go(PfShotAfter);
			}
			return;
		}
		PfShotTime += DeltaSeconds;
		if (!CityCamera)
		{
			CityCamera = GetWorld()->SpawnActor<ACameraActor>();
		}
		CityCamera->SetActorLocationAndRotation(PfShotFrom, PfShotRot);
		CityCamera->GetCameraComponent()->SetFieldOfView(PfShotFov);
		if (PC->GetViewTarget() != CityCamera)
		{
			PC->SetViewTarget(CityCamera);
		}
		if (PfShotTime > PfShotHold)
		{
			PC->ConsoleCommand(FString::Printf(TEXT("HighResShot %s filename=%s"), *Res, *PfShotName));
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Portfolio: %s requested (%s) camera %s %s fov %.0f, pawn at %s"), *PfShotName, *Res, *PfShotFrom.ToString(), *PfShotRot.ToString(), PfShotFov, *Player->GetActorLocation().ToString());
			PfShotName.Empty();
			PfShotTicksLeft = 3;
		}
		return;
	}
	auto Shot = [this](const TCHAR* Name, const FVector& From, const FRotator& Rot, float Fov, float Hold, int32 After)
	{
		PfShotName = Name; PfShotFrom = From; PfShotRot = Rot; PfShotFov = Fov; PfShotHold = Hold; PfShotAfter = After; PfShotTime = 0.f;
	};
	auto AimAt = [PC](const FVector& Point)
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		PC->SetControlRotation((Point - ViewLocation).Rotation());
	};
	auto Find = [this](const TCHAR* Label) -> AActor*
	{
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (It->GetActorLabel() == Label) { return *It; }
		}
		return nullptr;
	};

	// ---------------- play scenes (-DunePortfolio=playgun|playknife): the player's own camera, HUD on, the weapon in use ----------------
	if (PfMode == TEXT("playgun") || PfMode == TEXT("playknife"))
	{
		const bool bKnife = PfMode == TEXT("playknife");
		if (PC->GetHUD()) { PC->GetHUD()->bShowHUD = true; }
		AActor* Target = Find(TEXT("SM_house_07_Sliceable2"));
		static FVector Centre;
		switch (PfStep)
		{
		case 0:
			if (PfTime > Warm && Target)
			{
				Centre = Target->GetActorLocation();
				Weapons->GetKnife()->ConvertedSeparation = 120.f; // same debug gap as the 01/02 shots
				Weapons->SetWeapon(bKnife ? EDuneWeapon::Knife : EDuneWeapon::Gun);
				Go(1);
			}
			break;
		case 1:
		{
			AimAt(Centre + FVector(0.f, 0.f, AimZ));
			if (!bKnife)
			{
				if (PfStepTime > 1.0f) { Weapons->PrimaryAttack(); Go(3); }
				break;
			}
			Player->AddMovementInput((Centre - Player->GetActorLocation()).GetSafeNormal2D(), 1.f);
			FVector ViewLocation;
			FRotator ViewRotation;
			PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
			FHitResult Hit;
			FCollisionQueryParams Params(NAME_None, false, Player);
			if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, ViewLocation + ViewRotation.Vector() * 3000.f, ECC_Visibility, Params)
				&& Hit.GetActor() == Target && FVector::Dist(Player->GetActorLocation(), Hit.ImpactPoint) <= 230.f)
			{
				Go(2);
			}
			break;
		}
		case 2:
			AimAt(Centre + FVector(0.f, 0.f, AimZ));
			if (PfStepTime > 1.2f) { Weapons->PrimaryAttack(); Go(3); }
			break;
		case 3: // the knife: during the swing; the gun: just after the shot
			if (PfStepTime > (bKnife ? 0.2f : 0.25f))
			{
				PC->ConsoleCommand(FString::Printf(TEXT("HighResShot %s filename=%s"), *Res, bKnife ? TEXT("08_Play_Knife") : TEXT("07_Play_Gun")));
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Portfolio: play shot requested, pawn at %s, slice requests %d"), *Player->GetActorLocation().ToString(), ASliceableStructure::GetTotalSliceRequests());
				Go(4);
			}
			break;
		default:
			if (PfStepTime > 2.f) { FPlatformMisc::RequestExit(false); }
			break;
		}
		return;
	}

	// ---------------- the building (knife and gun) ----------------
	if (PfMode == TEXT("knife") || PfMode == TEXT("gun"))
	{
		static FVector Cam1L, Cam2L, TargetCentre;
		static FRotator Cam1R, Cam2R;
		static float Fov1 = 55.f, Fov2 = 55.f;
		const bool bKnife = PfMode == TEXT("knife");
		AActor* Target = Find(TEXT("SM_house_07_Sliceable2"));
		UKrisKnifeComponent* Knife = Weapons->GetKnife();
		UDuneGunComponent* Gun = Weapons->GetGun();
		switch (PfStep)
		{
		case 0:
			if (PfTime > Warm && Target)
			{
				const FBox Box = Target->GetComponentsBoundingBox();
				TargetCentre = Target->GetActorLocation();
				const FVector C = Box.GetCenter(), E = Box.GetExtent();
				const FVector Dir = FVector(-0.55f, -0.83f, 0.f).GetSafeNormal();
				const FVector LookAt(C.X, C.Y, Box.Min.Z + E.Z * 1.0f);
				Cam1L = LookAt + Dir * (FMath::Max(E.X, E.Y) * 3.4f + 300.f) + FVector(0.f, 0.f, E.Z * 0.15f);
				Cam1R = (LookAt - Cam1L).Rotation();
				if (!ParseCam(TEXT("DunePfCam="), Cam1L, Cam1R, Fov1)) { Fov1 = 55.f; }
				Cam2L = Cam1L; Cam2R = Cam1R; Fov2 = Fov1;
				ParseCam(TEXT("DunePfCam2="), Cam2L, Cam2R, Fov2);
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Portfolio: %s mode, target %s bounds centre %s extent %s, camera %s %s"), *PfMode, *Target->GetName(), *C.ToString(), *E.ToString(), *Cam1L.ToString(), *Cam1R.ToString());
				float Sep = 0.f; // -DunePfSep=<cm>: the knife's debug gap for a converted building (default 0: physics only)
				if (FParse::Value(FCommandLine::Get(), TEXT("DunePfSep="), Sep)) { Knife->ConvertedSeparation = Sep; }
				Weapons->SetWeapon(bKnife ? EDuneWeapon::Knife : EDuneWeapon::Gun);
				Player->SetActorHiddenInGame(true); // only the camera shots; the weapon still works
				if (bKnife)
				{
					Shot(TEXT("01_Slicing_Before"), Cam1L, Cam1R, Fov1, 3.f, 1);
				}
				else
				{
					Go(10);
				}
			}
			break;
		case 1: // walk up to the building like the move input does
		{
			AimAt(TargetCentre + FVector(0.f, 0.f, AimZ));
			Player->AddMovementInput((TargetCentre - Player->GetActorLocation()).GetSafeNormal2D(), 1.f);
			FVector ViewLocation;
			FRotator ViewRotation;
			PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
			FHitResult Hit;
			FCollisionQueryParams Params(NAME_None, false, Player);
			if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, ViewLocation + ViewRotation.Vector() * 3000.f, ECC_Visibility, Params)
				&& Hit.GetActor() == Target && FVector::Dist(Player->GetActorLocation(), Hit.ImpactPoint) <= 230.f)
			{
				Go(2);
			}
			else if (PfStepTime > 30.f)
			{
				UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Portfolio: the player did not reach the building in 30 s (at %s)"), *Player->GetActorLocation().ToString());
				FPlatformMisc::RequestExit(false);
			}
			break;
		}
		case 2: // settle, then a short mouse movement right and up (a diagonal slash: the upper part slides off) and one swing
			AimAt(TargetCentre + FVector(0.f, 0.f, AimZ));
			if (PfStepTime > 1.2f)
			{
				PfVal[0] = PC->GetControlRotation().Pitch; PfVal[1] = PC->GetControlRotation().Yaw;
			}
			if (PfStepTime > 1.2f)
			{
				const float A = FMath::Clamp((PfStepTime - 1.2f) / 0.1f, 0.f, 1.f);
				PC->SetControlRotation(FRotator(PfVal[0] + SlashPitch * A, PfVal[1] + SlashYaw * A, 0.f));
			}
			if (PfStepTime > 1.35f)
			{
				PfAux = Knife->GetSlices();
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Portfolio: swing, slices before %d, slice requests %d"), PfAux, ASliceableStructure::GetTotalSliceRequests());
				Weapons->PrimaryAttack();
				Go(3);
			}
			break;
		case 3: // the cut happens inside the swing
			if (Knife->GetSlices() > PfAux)
			{
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Portfolio: cut done after %.2f s, slices %d, gun fractures %d"), PfStepTime, Knife->GetSlices(), Gun->GetFractures());
				Shot(TEXT("01_Slicing_After"), Cam1L, Cam1R, Fov1, 0.25f, 4);
			}
			else if (PfStepTime > 5.f)
			{
				UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Portfolio: the knife made no cut in 5 s"));
				FPlatformMisc::RequestExit(false);
			}
			break;
		case 4:
			if (PfStepTime > 0.4f) // pieces are falling now
			{
				Shot(TEXT("02_Slicing_Physics"), Cam2L, Cam2R, Fov2, 0.1f, 5);
			}
			break;
		case 5:
			if (PfStepTime > 6.f)
			{
				Shot(TEXT("02_Slicing_Physics_Settled"), Cam2L, Cam2R, Fov2, 0.5f, 99);
			}
			break;

		// the gun
		case 10:
			AimAt(TargetCentre + FVector(0.f, 0.f, 260.f));
			if (PfStepTime > 1.0f)
			{
				PfAux = Gun->GetFractures();
				Weapons->PrimaryAttack();
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Portfolio: shot fired, slice requests %d (knife slices %d)"), ASliceableStructure::GetTotalSliceRequests(), Knife->GetSlices());
				Go(11);
			}
			break;
		case 11:
			if (PfStepTime > 0.15f)
			{
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Portfolio: gun fractures %d (was %d), slice requests %d"), Gun->GetFractures(), PfAux, ASliceableStructure::GetTotalSliceRequests());
				Shot(TEXT("03_Gun_Fracture"), Cam1L, Cam1R, Fov1, 0.2f, 12);
			}
			break;
		case 12:
			if (PfStepTime > 6.f)
			{
				Shot(TEXT("03_Gun_Fracture_Settled"), Cam1L, Cam1R, Fov1, 0.5f, 99);
			}
			break;
		default:
			if (PfStepTime > 0.5f) { FPlatformMisc::RequestExit(false); }
			break;
		}
		return;
	}

	// ---------------- the sand ----------------
	if (PfMode == TEXT("sand"))
	{
		if (!SandTrail || !SandTrail->IsReady())
		{
			return;
		}
		const float WalkY = -1900.f;
		auto Teleport = [this, Player](float X, float Y, float Above = 5.f)
		{
			FHitResult Hit;
			GetWorld()->LineTraceSingleByChannel(Hit, FVector(X, Y, 5000.f), FVector(X, Y, -1000.f), ECC_Visibility);
			Player->SetActorLocation(FVector(X, Y, Hit.ImpactPoint.Z + Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + Above), false, nullptr, ETeleportType::TeleportPhysics);
			Player->GetCharacterMovement()->Velocity = FVector::ZeroVector;
		};
		FVector CL;
		FRotator CR;
		float CF = 60.f;
		auto Cam = [&](const TCHAR* Key, const FVector& DefL, const FVector& DefAt, float DefFov)
		{
			CL = DefL; CR = (DefAt - DefL).Rotation(); CF = DefFov;
			ParseCam(Key, CL, CR, CF);
		};
		const bool bFresh = PfStepTime == DeltaSeconds; // first tick of a step
		const FVector2D Land(-1500.f, -1100.f);
		const FVector2D Origin(-1400.f, -900.f);
		const FVector2D SlopeAt(-1400.f, -1300.f);
		const bool bSlope = FParse::Param(FCommandLine::Get(), TEXT("DunePfSlope"));
		switch (PfStep)
		{
		case 0:
			if (PfTime > Warm)
			{
				SandTrail->TrailPersistence = 600.f;
				SandTrail->WindStrength = 0.f;
				SandTrail->DebugReset();
				Teleport(-2300.f, WalkY);
				PC->SetControlRotation(FRotator(-15.f, 0.f, 0.f));
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Portfolio: sand, compute simulation active=%d, displacement scale %.1f"), SandTrail->IsComputeActive(), SandTrail->DisplacementScale);
				Go(1);
			}
			break;
		case 1: // run along +X, full stick, the character's own movement
			Player->AddMovementInput(FVector::ForwardVector, 1.f);
			if (PfStepTime > 3.2f)
			{
				Go(2);
			}
			break;
		case 2: // stopped; the trail is behind the character
			if (PfStepTime > 1.0f)
			{
				const FSandTrailStats S = SandTrail->ReadStats();
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Portfolio: after the run: min %.2f max %.2f cm, walk stamps %d run stamps %d, pawn at %s"), S.MinCm, S.MaxCm, SandTrail->GetWalkStamps(), SandTrail->GetRunStamps(), *Player->GetActorLocation().ToString());
				Cam(TEXT("DunePfCam4="), FVector(Player->GetActorLocation().X - 900.f, WalkY - 90.f, 45.f), FVector(Player->GetActorLocation().X - 300.f, WalkY, 0.f), 70.f);
				Shot(TEXT("04_Sand_Footprints"), CL, CR, CF, 2.5f, 3);
			}
			break;
		case 3: // landing: before. The character is parked away, hidden
			if (bFresh)
			{
				SandTrail->DebugReset();
				Player->SetActorHiddenInGame(true);
				Teleport(-2300.f, -300.f);
			}
			if (PfStepTime > 1.0f)
			{
				Cam(TEXT("DunePfCam5="), FVector(Land.X - 230.f, Land.Y - 300.f, 120.f), FVector(Land.X, Land.Y, 0.f), 60.f);
				Shot(TEXT("05_Sand_Landing_Before"), CL, CR, CF, 2.5f, 4);
			}
			break;
		case 4: // the drop from 600 cm, onto the spot the camera looks at
			if (bFresh)
			{
				Teleport(Land.X, Land.Y, 600.f);
				PfVal[0] = float(SandTrail->GetLandingStamps());
			}
			if (PfStepTime > 3.f)
			{
				const FSandTrailStats S = SandTrail->ReadStats();
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Portfolio: landing stamps +%d, last stamp %s fall speed %.0f depth %.2f width %.1f, min %.2f max %.2f cm, dent centroid (%.0f, %.0f), pawn at %s"), SandTrail->GetLandingStamps() - int32(PfVal[0]),
					SandTrail->GetLastKind(), SandTrail->GetLastSpeed(), SandTrail->GetLastDepth(), SandTrail->GetLastWidth(), S.MinCm, S.MaxCm, S.DentCentroid.X, S.DentCentroid.Y, *Player->GetActorLocation().ToString());
				Cam(TEXT("DunePfCam5="), FVector(Land.X - 230.f, Land.Y - 300.f, 120.f), FVector(Land.X, Land.Y, 0.f), 60.f);
				Shot(TEXT("05_Sand_Landing_After"), CL, CR, CF, 0.3f, 5);
			}
			break;
		case 5: // wind: one dent, wind off for the first shot (-DunePfSlope: a steep spike with the slope collapse off for the first shot)
			if (bFresh && bSlope)
			{
				Teleport(-2300.f, -300.f);
				SandTrail->DebugReset();
				SandTrail->bRecoveryEnabled = false;
				SandTrail->WindStrength = 0.f;
				SandTrail->bSlopeCollapse = false;
				SandTrail->DebugStamp(SlopeAt, 5.f, 6.f, 10.f);
			}
			else if (bFresh)
			{
				Teleport(-2300.f, -300.f);
				SandTrail->DebugReset();
				SandTrail->TrailPersistence = 1000.f;
				SandTrail->WindDirection = FVector2D(1.f, 0.f);
				SandTrail->WindStrength = 0.f;
				SandTrail->WindTransportStrength = 25.f;
				SandTrail->WindRecoveryMultiplier = 0.f;
				SandTrail->DebugStamp(Origin, 30.f, 5.f, SandTrail->VolumeRidge(5.f, true));
			}
			if (PfStepTime > 0.5f)
			{
				Cam(TEXT("DunePfCam6="), bSlope ? FVector(SlopeAt.X - 150.f, SlopeAt.Y, 55.f) : FVector(Origin.X - 120.f, Origin.Y - 420.f, 210.f), bSlope ? FVector(SlopeAt.X, SlopeAt.Y, 0.f) : FVector(Origin.X + 70.f, Origin.Y, 0.f), bSlope ? 45.f : 62.f);
				Shot(TEXT("06_Sand_Simulation_Before"), CL, CR, CF, 2.0f, 6);
			}
			break;
		case 6:
			if (bFresh && bSlope)
			{
				SandTrail->bSlopeCollapse = true;
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Portfolio: slope collapse on"));
			}
			else if (bFresh)
			{
				SandTrail->WindStrength = 1.f;
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Portfolio: wind on, towards +X, strength 1"));
			}
			if (PfStepTime > (bSlope ? 10.f : 8.f))
			{
				const FSandTrailStats S = SandTrail->ReadStats();
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Portfolio: after 8 s of wind: dent centroid moved (%.0f, %.0f) cm, min %.2f max %.2f, non-finite %d"), S.DentCentroid.X - Origin.X, S.DentCentroid.Y - Origin.Y, S.MinCm, S.MaxCm, S.NonFinite);
				Cam(TEXT("DunePfCam6="), bSlope ? FVector(SlopeAt.X - 150.f, SlopeAt.Y, 55.f) : FVector(Origin.X - 120.f, Origin.Y - 420.f, 210.f), bSlope ? FVector(SlopeAt.X, SlopeAt.Y, 0.f) : FVector(Origin.X + 70.f, Origin.Y, 0.f), bSlope ? 45.f : 62.f);
				Shot(TEXT("06_Sand_Simulation_After"), CL, CR, CF, 0.3f, 99);
			}
			break;
		default:
			if (PfStepTime > 0.5f) { FPlatformMisc::RequestExit(false); }
			break;
		}
		return;
	}
	UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Portfolio: unknown mode '%s' (knife, gun, sand)"), *PfMode);
	FPlatformMisc::RequestExit(false);
}
