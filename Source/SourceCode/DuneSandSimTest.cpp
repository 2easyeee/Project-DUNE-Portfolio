// Copyright Epic Games, Inc. All Rights Reserved.

// Day 17 sand simulation self-test (-DuneSandSimTest). It runs in ADuneGameMode like the other self-tests and needs a rendered run
// (the heightfield lives in render targets and the simulation is a compute shader). Every number it logs is read back from the
// heightfield texture (stalls the GPU, so only used here); nothing is judged by the test itself, the log is read afterwards.

#include "Dune/DuneGameMode.h"
#include "Dune/DuneSandTrail.h"
#include "DuneSandSim.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "DynamicRHI.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Math/RandomStream.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UE_HOLDITTOGETHER.h"

namespace
{
	/** Heights across a line (along Y) at one X: deepest point, highest point, and the width of the part deeper than half the deepest. */
	struct FProfile
	{
		float Min = 0.f;
		float Max = 0.f;
		float HalfDepthWidth = 0.f;
	};

	FProfile ProfileAcross(const ADuneSandTrail* Sand, float X, float CentreY)
	{
		FProfile P;
		TArray<float> H;
		for (float Dy = -100.f; Dy <= 100.f; Dy += 5.f)
		{
			H.Add(Sand->SampleHeightCm(FVector(X, CentreY + Dy, 0.f)));
			P.Min = FMath::Min(P.Min, H.Last());
			P.Max = FMath::Max(P.Max, H.Last());
		}
		int32 Deep = 0;
		for (float V : H)
		{
			Deep += V < P.Min * 0.5f ? 1 : 0;
		}
		P.HalfDepthWidth = P.Min < -0.1f ? Deep * 5.f : 0.f;
		return P;
	}
}

void ADuneGameMode::RunSandSimTest(float DeltaSeconds, ACharacter* Player)
{
	SSTime += DeltaSeconds;
	SSStepTime += DeltaSeconds;
	APlayerController* PC = Cast<APlayerController>(Player->GetController());
	if (!PC || !SandTrail || !SandTrail->IsReady())
	{
		if (SSTime > 5.f && !SandTrail)
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("SandSim: no sand trail actor, test skipped"));
			FPlatformMisc::RequestExit(false);
		}
		return;
	}
	const bool bShot = FParse::Param(FCommandLine::Get(), TEXT("DuneSandSimShot"));
	float Warm = 45.f; // shader compilation of the sand, the brush material and the compute shaders
	FParse::Value(FCommandLine::Get(), TEXT("DuneSandWarm="), Warm);
	float SoakSeconds = 60.f;
	FParse::Value(FCommandLine::Get(), TEXT("DuneSandSoak="), SoakSeconds);
	const bool bFresh = bSSFresh;
	bSSFresh = false;
	auto Go = [this](int32 Next) { SSStep = Next; SSStepTime = 0.f; bSSFresh = true; };
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
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandSim: [%s] min %.2f max %.2f cm | dent sum %.1f ridge sum %.1f net %.1f (cm x px) | steepest %.3f (rise/run) | dent centroid (%.0f, %.0f) | dent box x %.0f..%.0f y %.0f..%.0f | non-finite %d | sim steps %d"),
			When, S.MinCm, S.MaxCm, S.DentSumCm, S.RidgeSumCm, S.SumCm, S.MaxGradient, S.DentCentroid.X, S.DentCentroid.Y,
			S.DentMinWorld.X, S.DentMaxWorld.X, S.DentMinWorld.Y, S.DentMaxWorld.Y, S.NonFinite, SandTrail->GetSimSteps());
		return S;
	};
	// a screenshot request: the next Tick moves a camera there, waits, shoots, then continues with the given step
	auto Shot = [this, bShot, Go](const TCHAR* Name, const FVector& From, const FVector& At, int32 After)
	{
		if (!bShot)
		{
			Go(After);
			return;
		}
		SSShotName = Name;
		SSShotFrom = From;
		SSShotAt = At;
		SSShotAfter = After;
		SSShotTime = 0.f;
	};
	if (!SSShotName.IsEmpty())
	{
		SSShotTime += DeltaSeconds;
		if (!CityCamera)
		{
			Player->SetActorHiddenInGame(true);
			CityCamera = GetWorld()->SpawnActor<ACameraActor>();
			PC->SetViewTarget(CityCamera);
		}
		CityCamera->SetActorLocationAndRotation(SSShotFrom, (SSShotAt - SSShotFrom).Rotation());
		CityCamera->GetCameraComponent()->SetFieldOfView(70.f);
		if (SSShotTime > 2.5f)
		{
			PC->ConsoleCommand(FString::Printf(TEXT("HighResShot 1280x720 filename=SandSim_%s"), *SSShotName));
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandSim: screenshot %s requested"), *SSShotName);
			SSShotName.Empty();
			Go(SSShotAfter);
		}
		return;
	}

	const float WalkY = -1900.f, RunY = -1500.f; // the walking lines: along +X, inside the patch, clear of structures
	switch (SSStep)
	{
	case 0: // wait for the shaders, then set up a calm, slow-fading test sand
		if (SSTime > Warm)
		{
			SandTrail->TrailPersistence = 600.f;
			SandTrail->WindStrength = 0.f;
			SandTrail->DebugReset();
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandSim: start. compute simulation active=%d (GPU supported=%d), dispatches %d failures %d, trail memory %.1f MB, sim interval %.3f s, slope threshold %.0f deg (tan %.3f), ridge volume fraction %.2f"),
				SandTrail->IsComputeActive(), DuneSandSim::IsSupported(), DuneSandSim::GetDispatchCount(), DuneSandSim::GetFailureCount(), SandTrail->GetTrailMemoryMB(),
				SandTrail->SimInterval, SandTrail->SlopeThresholdDeg, FMath::Tan(FMath::DegreesToRadians(SandTrail->SlopeThresholdDeg)), SandTrail->RidgeVolumeFraction);
			Teleport(-2300.f, WalkY);
			PC->SetControlRotation(FRotator(-15.f, 0.f, 0.f));
			Go(1);
		}
		break;
	case 1: // standing on fresh sand: nothing is recorded
		if (SSStepTime > 1.5f)
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandSim: standing 1.5 s: brush updates %d (expected 0), stamps walk %d run %d landing %d"),
				SandTrail->GetBrushUpdates(), SandTrail->GetWalkStamps(), SandTrail->GetRunStamps(), SandTrail->GetLandingStamps());
			SSVal[0] = SSVal[1] = 0.f;
			SSMark = 0;
			Go(2);
		}
		break;
	case 2: // walking: half stick, the speed the movement actually reaches decides the stamp
		Player->AddMovementInput(FVector::ForwardVector, 0.4f);
		if (SSStepTime > 1.5f)
		{
			SSVal[0] += Player->GetVelocity().Size2D();
			++SSMark;
		}
		if (SSStepTime > 3.f)
		{
			const FProfile P = ProfileAcross(SandTrail, -1800.f, WalkY);
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandSim: WALK speed %.0f cm/s (stamps walk %d run %d, last stamp %s depth %.2f width %.1f): measured at x=-1800 deepest %.2f cm, ridge %.2f cm, width at half depth %.0f cm"),
				SSVal[0] / FMath::Max(SSMark, 1), SandTrail->GetWalkStamps(), SandTrail->GetRunStamps(), SandTrail->GetLastKind(), SandTrail->GetLastDepth(), SandTrail->GetLastWidth(), P.Min, P.Max, P.HalfDepthWidth);
			SSVal[2] = P.Min;
			SSVal[3] = P.Max;
			SSVal[4] = P.HalfDepthWidth;
			Teleport(-2300.f, RunY);
			SSVal[1] = 0.f;
			SSMark = 0;
			Go(3);
		}
		break;
	case 3: // running: full stick
		Player->AddMovementInput(FVector::ForwardVector, 1.f);
		if (SSStepTime > 1.5f)
		{
			SSVal[1] += Player->GetVelocity().Size2D();
			++SSMark;
		}
		if (SSStepTime > 3.f)
		{
			const FProfile P = ProfileAcross(SandTrail, -1200.f, RunY);
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandSim: RUN speed %.0f cm/s (stamps walk %d run %d, last stamp %s depth %.2f width %.1f): measured at x=-1200 deepest %.2f cm, ridge %.2f cm, width at half depth %.0f cm"),
				SSVal[1] / FMath::Max(SSMark, 1), SandTrail->GetWalkStamps(), SandTrail->GetRunStamps(), SandTrail->GetLastKind(), SandTrail->GetLastDepth(), SandTrail->GetLastWidth(), P.Min, P.Max, P.HalfDepthWidth);
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandSim: walk vs run: depth %.2f vs %.2f cm, ridge %.2f vs %.2f cm, width %.0f vs %.0f cm"), SSVal[2], P.Min, SSVal[3], P.Max, SSVal[4], P.HalfDepthWidth);
			// is the trail continuous: height on the line every 40 cm along the run
			int32 Gaps = 0, Samples = 0;
			for (float X = -2000.f; X <= -1000.f; X += 40.f)
			{
				++Samples;
				Gaps += ProfileAcross(SandTrail, X, RunY).Min > -0.5f ? 1 : 0;
			}
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandSim: run trail continuity: %d of %d cross sections (x -2000..-1000, every 40 cm) without a dent"), Gaps, Samples);
			Stats(TEXT("after walk and run"));
			Shot(TEXT("walk_run"), FVector(-1500.f, -1000.f, 380.f), FVector(-1500.f, -1700.f, 0.f), 4);
		}
		break;
	case 4: // standing again: no new records
		if (bFresh)
		{
			SSMark = SandTrail->GetBrushUpdates(); // taken now: the character has stopped by the time the shot above is done
		}
		if (SSStepTime > 2.f)
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandSim: standing 2 s after the run: brush updates %d -> %d (expected equal)"), SSMark, SandTrail->GetBrushUpdates());
			Go(5);
		}
		break;
	case 5: // landing after a 60 cm drop
	case 6: // 600 cm drop
	case 7: // a normal jump
	{
		const bool bJump = SSStep == 7;
		const float Drop = SSStep == 5 ? 60.f : 600.f;
		const FVector2D At(SSStep == 5 ? -1900.f : SSStep == 6 ? -1300.f : -1600.f, -1100.f);
		if (bFresh)
		{
			SandTrail->DebugReset();
			const float Z = Teleport(At.X, At.Y);
			SSMark = SandTrail->GetLandingStamps();
			SSVal[5] = SandTrail->GetLandingsIgnored();
			if (bJump)
			{
				Player->Jump();
			}
			else
			{
				Player->SetActorLocation(FVector(At.X, At.Y, Z + Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + Drop), false, nullptr, ETeleportType::TeleportPhysics);
			}
		}
		if (bJump && SSStepTime > 0.3f)
		{
			Player->StopJumping();
		}
		if (SSStepTime > (bJump ? 2.5f : 3.f))
		{
			const FSandTrailStats S = Stats(bJump ? TEXT("after a jump landing") : Drop < 100.f ? TEXT("after a 60 cm drop") : TEXT("after a 600 cm drop"));
			const FProfile P = ProfileAcross(SandTrail, Player->GetActorLocation().X, At.Y);
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandSim: LANDING (%s): landing stamps +%d (expected 1), ignored +%d, last stamp %s fall speed %.0f cm/s -> depth %.2f cm, half width %.0f cm | measured deepest %.2f cm, dent box %.0f x %.0f cm | ridge sum / dent sum = %.2f"),
				bJump ? TEXT("jump") : Drop < 100.f ? TEXT("60 cm") : TEXT("600 cm"), SandTrail->GetLandingStamps() - SSMark, int32(SandTrail->GetLandingsIgnored() - SSVal[5]),
				SandTrail->GetLastKind(), SandTrail->GetLastSpeed(), SandTrail->GetLastDepth(), SandTrail->GetLastWidth(), P.Min,
				S.DentMaxWorld.X - S.DentMinWorld.X, S.DentMaxWorld.Y - S.DentMinWorld.Y, S.DentSumCm < -1e-6 ? S.RidgeSumCm / -S.DentSumCm : 0.0);
			if (SSStep == 6)
			{
				Shot(TEXT("landing_high"), FVector(At.X, At.Y - 380.f, 190.f), FVector(At.X, At.Y, 0.f), 7);
			}
			else
			{
				Go(SSStep + 1);
			}
		}
		break;
	}
	case 8: // landing on something that is not sand (the cut test block): no dent
		if (bFresh)
		{
			SandTrail->DebugReset();
			const float Top = Teleport(0.f, 0.f);
			Player->SetActorLocation(FVector(0.f, 0.f, Top + Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 250.f), false, nullptr, ETeleportType::TeleportPhysics);
			SSMark = SandTrail->GetLandingStamps();
			SSVal[5] = SandTrail->GetSkippedNotSand();
		}
		if (SSStepTime > 3.f)
		{
			const AActor* Floor = Player->GetCharacterMovement()->CurrentFloor.HitResult.GetActor();
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandSim: landing on %s (not sand): landing stamps +%d (expected 0), not-sand frames +%d (counted per frame while on it)"), *GetNameSafe(Floor),
				SandTrail->GetLandingStamps() - SSMark, int32(SandTrail->GetSkippedNotSand() - SSVal[5]));
			Go(9);
		}
		break;
	case 9: // recovery without wind: one dent, sampled over time
	{
		static const float Times[] = { 0.3f, 4.f, 8.f, 12.f, 16.f, 21.f };
		if (bFresh)
		{
			Teleport(-2300.f, -300.f);
			SandTrail->DebugReset();
			SandTrail->TrailPersistence = 20.f;
			SandTrail->WindStrength = 0.f;
			SandTrail->DebugStamp(FVector2D(-1400.f, -1300.f), 30.f, 5.f, SandTrail->VolumeRidge(5.f, true));
			SSMark = 0;
		}
		if (SSMark < UE_ARRAY_COUNT(Times) && SSStepTime >= Times[SSMark])
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandSim: FADE t=%.1f s: centre %.2f cm (persistence 20 s, no wind), ridge side (45 cm away) %.2f cm"), SSStepTime,
				SandTrail->SampleHeightCm(FVector(-1400.f, -1300.f, 0.f)), SandTrail->SampleHeightCm(FVector(-1400.f, -1255.f, 0.f)));
			++SSMark;
		}
		if (SSMark >= UE_ARRAY_COUNT(Times))
		{
			Stats(TEXT("after the fade"));
			SSAux = 0;
			Go(10);
		}
		break;
	}
	case 10: // wind: the same dent under different winds
	{
		struct FWindCase { const TCHAR* Name; FVector2D Dir; float Strength; float Transport; float RecoveryMul; float Persistence; float Seconds; bool bFade; };
		static const FWindCase Cases[] = {
			{ TEXT("calm"), FVector2D(1.f, 0.f), 0.f, 25.f, 0.f, 1000.f, 8.f, true },
			{ TEXT("towards +X, strength 1"), FVector2D(1.f, 0.f), 1.f, 25.f, 0.f, 1000.f, 8.f, true },
			{ TEXT("towards -X, strength 1"), FVector2D(-1.f, 0.f), 1.f, 25.f, 0.f, 1000.f, 8.f, true },
			{ TEXT("towards +Y, strength 1"), FVector2D(0.f, 1.f), 1.f, 25.f, 0.f, 1000.f, 8.f, true },
			{ TEXT("towards +X, strength 0.5"), FVector2D(1.f, 0.f), 0.5f, 25.f, 0.f, 1000.f, 8.f, true },
			{ TEXT("recovery only, calm, persistence 30 s"), FVector2D(1.f, 0.f), 0.f, 0.f, 1.5f, 30.f, 6.f, true },
			{ TEXT("recovery only, wind strength 1 (multiplier 1.5, no transport)"), FVector2D(1.f, 0.f), 1.f, 0.f, 1.5f, 30.f, 6.f, true },
			{ TEXT("towards +X, strength 1, NO fade (sand conservation)"), FVector2D(1.f, 0.f), 1.f, 25.f, 0.f, 1000.f, 8.f, false },
			{ TEXT("calm, NO fade (sand conservation)"), FVector2D(1.f, 0.f), 0.f, 25.f, 0.f, 1000.f, 8.f, false },
		};
		const FWindCase& C = Cases[SSAux];
		const FVector2D Origin(-1400.f, -900.f);
		if (bFresh)
		{
			SandTrail->DebugReset();
			SandTrail->TrailPersistence = C.Persistence;
			SandTrail->WindDirection = C.Dir;
			SandTrail->WindStrength = C.Strength;
			SandTrail->WindTransportStrength = C.Transport;
			SandTrail->WindRecoveryMultiplier = C.RecoveryMul;
			SandTrail->bRecoveryEnabled = C.bFade;
			SandTrail->DebugStamp(Origin, 30.f, 5.f, SandTrail->VolumeRidge(5.f, true));
			const FSandTrailStats S0 = SandTrail->ReadStats();
			SSSum = S0.SumCm;
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandSim: WIND case %d (%s) start: dent sum %.1f, ridge sum %.1f (ridge / dent = %.2f, set by RidgeVolumeFraction %.2f)"), SSAux, C.Name, S0.DentSumCm, S0.RidgeSumCm,
				S0.DentSumCm < -1e-6 ? S0.RidgeSumCm / -S0.DentSumCm : 0.0, SandTrail->RidgeVolumeFraction);
		}
		if (SSStepTime > C.Seconds)
		{
			const FSandTrailStats S = Stats(C.Name);
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandSim: WIND case %d (%s), %.0f s: dent centroid moved (%.0f, %.0f) cm from the stamp, deepest %.2f cm, net sand %.1f -> %.1f"),
				SSAux, C.Name, C.Seconds, S.DentCentroid.X - Origin.X, S.DentCentroid.Y - Origin.Y, S.MinCm, SSSum, S.SumCm);
			++SSAux;
			if (SSAux >= UE_ARRAY_COUNT(Cases))
			{
				SandTrail->bRecoveryEnabled = true;
				SandTrail->WindDirection = FVector2D(1.f, 0.35f);
				SandTrail->WindStrength = 0.f;
				SandTrail->WindTransportStrength = 12.f;
				SandTrail->WindRecoveryMultiplier = 1.5f;
				SSAux = 0;
				Go(11);
			}
			else if (SSAux == 2 || SSAux == 3)
			{
				Shot(SSAux == 2 ? TEXT("wind_plusx") : TEXT("wind_minusx"), FVector(Origin.X, Origin.Y - 320.f, 240.f), FVector(Origin.X, Origin.Y, 0.f), 10);
			}
			else
			{
				Go(10);
			}
		}
		break;
	}
	case 11: // slope collapse: a steep spike and pit, first frozen by switching the collapse off (control), then collapsing
	{
		const FVector2D At(-1400.f, -1300.f);
		static const float Times[] = { 0.f, 1.f, 3.f, 6.f, 12.f, 20.f };
		if (bFresh)
		{
			SandTrail->DebugReset();
			SandTrail->bRecoveryEnabled = false;
			SandTrail->WindStrength = 0.f;
			SandTrail->bSlopeCollapse = SSAux == 1;
			SandTrail->DebugStamp(At, 5.f, 6.f, 10.f);
			SSMark = 0;
			SSSum = SandTrail->ReadStats().SumCm;
			SSVal[6] = 0.f;
		}
		if (SSAux == 0)
		{
			if (SSStepTime > 3.f)
			{
				const FSandTrailStats S = Stats(TEXT("steep stamp, collapse OFF (control), 3 s"));
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandSim: SLOPE control: steepest %.3f, net sand %.1f (collapse off: should stay as stamped)"), S.MaxGradient, S.SumCm);
				SSAux = 1;
				Shot(TEXT("slope_before"), FVector(At.X, At.Y - 140.f, 90.f), FVector(At.X, At.Y, 0.f), 11);
			}
		}
		else
		{
			if (SSMark < UE_ARRAY_COUNT(Times) && SSStepTime >= Times[SSMark])
			{
				const FSandTrailStats S = Stats(*FString::Printf(TEXT("slope collapse ON, t=%.0f s"), SSStepTime));
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandSim: SLOPE t=%.1f s: steepest %.3f (limit tan %.3f), net sand %.2f (start %.2f), min %.2f max %.2f"), SSStepTime, S.MaxGradient,
					FMath::Tan(FMath::DegreesToRadians(SandTrail->SlopeThresholdDeg)), S.SumCm, SSSum, S.MinCm, S.MaxCm);
				++SSMark;
			}
			if (SSMark >= UE_ARRAY_COUNT(Times))
			{
				SandTrail->bRecoveryEnabled = true;
				SSAux = 0;
				Shot(TEXT("slope_after"), FVector(At.X, At.Y - 140.f, 90.f), FVector(At.X, At.Y, 0.f), 12);
			}
		}
		break;
	}
	case 12: // soak: random heavy stamps under full wind with collapse on and no fade for a long time
	{
		if (bFresh)
		{
			SandTrail->DebugReset();
			SandTrail->bRecoveryEnabled = false;
			SandTrail->bSlopeCollapse = true;
			SandTrail->WindDirection = FVector2D(1.f, 0.35f);
			SandTrail->WindStrength = 1.f;
			SandTrail->WindTransportStrength = 12.f;
			SSMark = 0;
			SSVal[6] = 0.f;
			SSVal[7] = 0.f;
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandSim: SOAK for %.0f s: a random steep stamp every 0.5 s, wind strength 1, collapse on, no fade"), SoakSeconds);
		}
		static FRandomStream Random(1701);
		SSVal[6] += DeltaSeconds;
		if (SSVal[6] >= 0.5f)
		{
			SSVal[6] = 0.f;
			SandTrail->DebugStamp(FVector2D(Random.FRandRange(-2200.f, -600.f), Random.FRandRange(-1700.f, -100.f)), Random.FRandRange(4.f, 60.f), Random.FRandRange(1.f, 6.3f), Random.FRandRange(0.f, 8.f));
		}
		SSVal[7] += DeltaSeconds;
		if (SSVal[7] >= 30.f)
		{
			SSVal[7] = 0.f;
			Stats(*FString::Printf(TEXT("soak t=%.0f s"), SSStepTime));
		}
		if (SSStepTime > SoakSeconds)
		{
			Stats(TEXT("soak end"));
			SandTrail->bRecoveryEnabled = true;
			Shot(TEXT("soak_end"), FVector(-1400.f, -1900.f, 700.f), FVector(-1400.f, -900.f, 0.f), 13);
		}
		break;
	}
	case 13: // frame times: the same still scene with the simulation paused, then each part switched on
	{
		static const TCHAR* Names[] = { TEXT("simulation paused (no dispatch)"), TEXT("simulation on: fade only"), TEXT("+ wind"), TEXT("+ wind + slope collapse"), TEXT("+ the player running (stamps)"),
			TEXT("stress: 4 dispatches per frame (step 2 ms), wind + slope + running") };
		if (bFresh)
		{
			SandTrail->DebugReset();
			Teleport(-2300.f, -200.f);
			SandTrail->TrailPersistence = 1000.f;
			SandTrail->bRecoveryEnabled = true;
			SandTrail->bSlopeCollapse = false;
			SandTrail->WindStrength = 0.f;
			SandTrail->SetSimPaused(SSAux == 0);
			SandTrail->SimInterval = SSAux == 5 ? 0.002f : 0.05f;
			SandTrail->MaxSubSteps = 4;
			if (SSAux >= 2)
			{
				SandTrail->WindStrength = 1.f;
			}
			if (SSAux >= 3)
			{
				SandTrail->bSlopeCollapse = true;
			}
			SandTrail->DebugStamp(FVector2D(-1400.f, -900.f), 30.f, 5.f, 2.f);
			SSFrameMs = 0.0;
			SSGpuMs = 0.0;
			SSFrames = 0;
			SSMark = SandTrail->GetSimSteps();
			SSVal[6] = float(DuneSandSim::GetDispatchCount());
		}
		if (SSAux >= 4)
		{
			Player->AddMovementInput(FVector::ForwardVector, 1.f);
			if (Player->GetActorLocation().X > -600.f)
			{
				Teleport(-2300.f, -200.f);
			}
		}
		if (SSStepTime > 1.f) // the first second after a change is not counted
		{
			SSFrameMs += DeltaSeconds * 1000.0;
			SSGpuMs += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
			++SSFrames;
		}
		if (SSStepTime > 6.f)
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandSim: FRAME TIME [%s]: %.2f ms per frame (%d frames), GPU frame %.2f ms | sim steps +%d, compute dispatches +%d"), Names[SSAux],
				SSFrames ? SSFrameMs / SSFrames : 0.0, SSFrames, SSFrames ? SSGpuMs / SSFrames : 0.0, SandTrail->GetSimSteps() - SSMark, int32(DuneSandSim::GetDispatchCount() - SSVal[6]));
			++SSAux;
			if (SSAux >= 6)
			{
				Go(14);
			}
			else
			{
				Go(13);
			}
		}
		break;
	}
	case 14:
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandSim: done. compute dispatches %d, failures %d, sim steps %d, brush updates %d, stamps walk %d run %d landing %d"),
			DuneSandSim::GetDispatchCount(), DuneSandSim::GetFailureCount(), SandTrail->GetSimSteps(), SandTrail->GetBrushUpdates(),
			SandTrail->GetWalkStamps(), SandTrail->GetRunStamps(), SandTrail->GetLandingStamps());
		FPlatformMisc::RequestExit(false);
		SSStep = 99;
		break;
	}
}
