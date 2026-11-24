// Copyright Epic Games, Inc. All Rights Reserved.

#include "SandSelfTest.h"
#include "SandContactPointComponent.h"
#include "SandSimulationSubsystem.h"
#include "SandDemoActors.h"
#include "SandDeformationLibrary.h"
#include "SandInteractionComponent.h"
#include "SandSim.h"
#include "SandSurfaceActor.h"
#include "SandSurfaceComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/SkyAtmosphereComponent.h"

namespace
{
	int32 GSandHopIndex = 0; // survives the level change (same process): which level of -SandDeformationLevelHop this world is
}

ASandSelfTest::ASandSelfTest()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ASandSelfTest::Check(bool bOk, const FString& What)
{
	(bOk ? Passed : Failed)++;
	UE_LOG(LogSandDeformation, Log, TEXT("SandSelfTest: %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
}

void ASandSelfTest::Shot(const TCHAR* Name, const FVector& From, const FVector& At)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !Camera || FParse::Param(FCommandLine::Get(), TEXT("SandDeformationNoShots")))
	{
		return;
	}
	if (PC->GetHUD())
	{
		PC->GetHUD()->bShowHUD = false; // no game HUD in the pictures
	}
	Camera->SetActorLocationAndRotation(From, (At - From).Rotation());
	PC->SetViewTarget(Camera);
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SandDeformationSelfTest") / Name).Replace(TEXT("\\"), TEXT("/"));
	PC->ConsoleCommand(FString::Printf(TEXT("HighResShot 1920x1080 filename=\"%s\""), *Path));
}

void ASandSelfTest::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();
	bLevelMode = FParse::Param(FCommandLine::Get(), TEXT("SandDeformationLevelTest"));
	FString HopList;
	if (FParse::Value(FCommandLine::Get(), TEXT("SandDeformationLevelHop="), HopList))
	{
		HopList.ParseIntoArray(HopLevels, TEXT(";")); // FParse stops at a comma, so the separator is ;
		bHopMode = HopLevels.Num() > 0;
		HopIndex = GSandHopIndex;
		UE_LOG(LogSandDeformation, Log, TEXT("SandHop: world %d of %d: %s"), HopIndex + 1, HopLevels.Num(), *GetWorld()->GetMapName());
		return;
	}
	if (bLevelMode)
	{
		Camera = World->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator);
		Camera->GetCameraComponent()->SetFieldOfView(60.f);
		UE_LOG(LogSandDeformation, Log, TEXT("SandSelfTest: level mode, using the demo actors of the level"));
		return;
	}
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	auto SpawnMesh = [&](UStaticMesh* Mesh, const FVector& At, const FVector& Scale, bool bVisible)
	{
		AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>(At, FRotator::ZeroRotator);
		A->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		A->GetStaticMeshComponent()->SetStaticMesh(Mesh);
		A->SetActorScale3D(Scale);
		A->SetActorHiddenInGame(!bVisible);
		return A;
	};
	// the floor the surface reads its ground height from: collision only (the patch draws the sand); a lowered visible floor gives the horizon a reference
	SpawnMesh(Cube, Centre - FVector(0, 0, 50), FVector(100, 100, 1), false);
	SpawnMesh(Cube, Centre - FVector(0, 0, 80), FVector(100, 100, 1), true);
	Walker = SpawnMesh(Sphere, Centre + FVector(0, 0, 25), FVector(0.5), true);

	// an empty map has no light: add a sun, sky light and sky only when the level has none (a clean project would render black)
	if (!TActorIterator<ADirectionalLight>(World))
	{
		ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(Centre + FVector(0, 0, 500), FRotator(-28.f, 205.f, 0.f));
		Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		Sun->GetLightComponent()->SetIntensity(8.f);
		ASkyLight* Sky = World->SpawnActor<ASkyLight>(Centre + FVector(0, 0, 600), FRotator::ZeroRotator);
		Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		Sky->GetLightComponent()->SetRealTimeCaptureEnabled(true);
		World->SpawnActor<ASkyAtmosphere>(Centre, FRotator::ZeroRotator);
	}

	SurfaceActor = World->SpawnActorDeferred<ASandSurfaceActor>(ASandSurfaceActor::StaticClass(), FTransform(FRotator::ZeroRotator, Centre));
	USandSurfaceComponent* S = SurfaceActor->Surface;
	S->PatchCenter = FVector2D(Centre.X, Centre.Y);
	S->bAutoFollowLocalPlayer = false;
	S->bAnyFloorIsSand = true;
	SurfaceActor->FinishSpawning(FTransform(FRotator::ZeroRotator, Centre));
	S->SetFollowTarget(Walker);

	Interaction = NewObject<USandInteractionComponent>(Walker);
	Interaction->RegisterComponent();

	Camera = World->SpawnActor<ACameraActor>(Centre, FRotator::ZeroRotator);
	Camera->GetCameraComponent()->SetFieldOfView(60.f);
	UE_LOG(LogSandDeformation, Log, TEXT("SandSelfTest: started, surface at %s"), *Centre.ToString());
}

void ASandSelfTest::Finish()
{
	UE_LOG(LogSandDeformation, Log, TEXT("SandSelfTest: RESULT %s (%d passed, %d failed), %d frames, average frame %.2f ms, longest %.1f ms, GPU passes %d, failures %d"),
		Failed == 0 ? TEXT("PASS") : TEXT("FAIL"), Passed, Failed, Frames, Frames > 0 ? 1000.f * TimeSum / Frames : 0.f, 1000.f * TimeMax, SandSim::GetDispatchCount(), SandSim::GetFailureCount());
	Step = 99;
	if (FParse::Param(FCommandLine::Get(), TEXT("SandDeformationSelfTestQuit")))
	{
		FPlatformMisc::RequestExit(false);
	}
}

void ASandSelfTest::TickHopMode(float DeltaSeconds)
{
	// one level of the hop: the surface of THIS world must register, take a dent, give it back, and only this world's surfaces may be registered
	if (Step == 0)
	{
		if (StepFrames < 240)
		{
			return;
		}
		const USandSimulationSubsystem* Subsystem = GetWorld()->GetSubsystem<USandSimulationSubsystem>();
		const int32 Count = Subsystem ? Subsystem->GetSurfaces().Num() : 0;
		USandSurfaceComponent* S = Count > 0 ? Subsystem->GetSurfaces()[0].Get() : nullptr;
		bool bStale = false;
		for (const TWeakObjectPtr<USandSurfaceComponent>& Surface : Subsystem->GetSurfaces())
		{
			bStale |= !Surface.IsValid() || Surface->GetWorld() != GetWorld();
		}
		Check(S && S->IsSurfaceReady(), FString::Printf(TEXT("hop %d (%s): %d surface(s) registered in this world, ready"), HopIndex + 1, *GetWorld()->GetMapName(), Count));
		Check(!bStale, TEXT("hop: no registered surface of an older world"));
		if (S)
		{
			const FVector2D C = S->GetPatchCenter();
			const bool bOk = S->ApplyDeformation(FVector(C.X, C.Y, 0.f), 50.f, 2.f, ESandBrush::Round);
			Check(bOk, TEXT("hop: a dent request was accepted"));
		}
		Step = 1;
		StepFrames = 0;
		return;
	}
	if (Step == 1 && StepFrames > 20)
	{
		USandSurfaceComponent* S = GetWorld()->GetSubsystem<USandSimulationSubsystem>()->GetSurfaces().Num() > 0 ? GetWorld()->GetSubsystem<USandSimulationSubsystem>()->GetSurfaces()[0].Get() : nullptr;
		if (S)
		{
			const FVector2D C = S->GetPatchCenter();
			const FSandHeightStats Stats = S->ReadStats();
			const float Depth = S->SampleHeightCm(FVector(C.X, C.Y, 0.f));
			Check(Depth < -3.f && Stats.NonFinite == 0, FString::Printf(TEXT("hop %d: dent read back %.2f cm, non-finite %d, GPU passes %d, failures %d"), HopIndex + 1, Depth, Stats.NonFinite, SandSim::GetDispatchCount(), SandSim::GetFailureCount()));
		}
		++GSandHopIndex;
		if (GSandHopIndex < HopLevels.Num())
		{
			UE_LOG(LogSandDeformation, Log, TEXT("SandHop: opening %s"), *HopLevels[GSandHopIndex]);
			UGameplayStatics::OpenLevel(this, FName(*HopLevels[GSandHopIndex]));
			Step = 2;
		}
		else
		{
			Finish();
		}
	}
}

void ASandSelfTest::TickLevelMode(float DeltaSeconds)
{
	auto Next = [this]() { ++Step; StepFrames = 0; };
	USandSurfaceComponent* S = LevelSurface;
	switch (Step)
	{
	case 0: // find the surface and the demo actors
	{
		if (StepFrames < 30)
		{
			break;
		}
		const USandSimulationSubsystem* Subsystem = GetWorld()->GetSubsystem<USandSimulationSubsystem>();
		LevelSurface = Subsystem && Subsystem->GetSurfaces().Num() > 0 ? Subsystem->GetSurfaces()[0].Get() : nullptr;
		Check(LevelSurface != nullptr && LevelSurface->IsSurfaceReady(), TEXT("level: a ready sand surface is registered"));
		for (TActorIterator<ASandDemoActor> It(GetWorld()); It; ++It)
		{
			(It->Kind == ESandDemoKind::Box ? Box : It->Kind == ESandDemoKind::Vehicle ? Vehicle : Lander) = *It;
		}
		for (TActorIterator<ASandDemoCharacter> It(GetWorld()); It; ++It)
		{
			Golem = *It;
		}
		Check(Box && Vehicle && Lander && Golem, FString::Printf(TEXT("level: demo actors found (crate %d, vehicle %d, lander %d, heavy character %d)"), Box != nullptr, Vehicle != nullptr, Lander != nullptr, Golem != nullptr));
		if (!LevelSurface || !Box || !Vehicle || !Lander || !Golem)
		{
			Finish();
			break;
		}
		FString Only;
		if (FParse::Value(FCommandLine::Get(), TEXT("SandDeformationOnly="), Only))
		{
			// frame time comparison: only the named actor (crate, vehicle, lander, character) makes marks, "none" = no sand interaction at all
			auto Enable = [&](ASandDemoActor* A, const TCHAR* Name)
			{
				for (USandContactPointComponent* P : A->GetContactPoints())
				{
					P->bContactEnabled = Only == Name;
				}
			};
			Enable(Box, TEXT("crate"));
			Enable(Vehicle, TEXT("vehicle"));
			Enable(Lander, TEXT("lander"));
			Golem->Interaction->bAutomaticCharacterMarks = Only == TEXT("character");
			UE_LOG(LogSandDeformation, Log, TEXT("SandSelfTest: sand interaction only for '%s' (frame time comparison)"), *Only);
		}
		PerfFrames = 0;
		PerfTime = 0.f;
		Next();
		break;
	}

	case 1: // wait for the vehicle, the lander and the walk
		++PerfFrames;
		PerfTime += DeltaSeconds;
		if (!(Vehicle->IsRunFinished() && Lander->IsRunFinished() && Golem->IsWalkFinished() && StepFrames > 360))
		{
			if (StepFrames > 3600)
			{
				Check(false, FString::Printf(TEXT("level: all actors finished within 3600 frames (vehicle %d at %s, lander %d at %s, heavy character walk %d at %s)"), Vehicle->IsRunFinished(), *Vehicle->GetActorLocation().ToString(),
					Lander->IsRunFinished(), *Lander->GetActorLocation().ToString(), Golem->IsWalkFinished(), *Golem->GetActorLocation().ToString()));
				Finish();
			}
			break;
		}
		UE_LOG(LogSandDeformation, Log, TEXT("SandSelfTest: level run frame time %.2f ms average over %d frames"), 1000.f * PerfTime / FMath::Max(PerfFrames, 1), PerfFrames);
		Next();
		break;

	case 2:
		if (StepFrames < 90)
		{
			break;
		}
		{
			const FVector G = Golem->GetActorLocation();
			Shot(TEXT("L_5_Golem.png"), G + FVector(-420, -520, 260), G - FVector(0, 0, 90));
		}
		Next();
		break;

	case 3:
		if (StepFrames < 24)
		{
			break;
		}
		{
			const FVector V = Vehicle->GetActorLocation();
			Shot(TEXT("L_3_Vehicle.png"), V + FVector(-380, -330, 230), V - FVector(150, 0, 40));
		}
		Next();
		break;

	case 4:
		if (StepFrames < 24)
		{
			break;
		}
		{
			const FVector L = Lander->GetActorLocation();
			Shot(TEXT("L_4_Lander.png"), L + FVector(-520, -460, 160), L - FVector(0, 0, 90));
		}
		Next();
		break;

	case 5:
		if (StepFrames < 24)
		{
			break;
		}
		{
			const FVector B = Box->GetActorLocation();
			Shot(TEXT("L_2_Crate.png"), B + FVector(-260, -300, 160), B - FVector(0, 0, 30));
		}
		Next();
		break;

	case 6:
		if (StepFrames < 24)
		{
			break;
		}
		{
			const FVector C = FVector(LevelSurface->GetPatchCenter().X, LevelSurface->GetPatchCenter().Y, Lander->GetActorLocation().Z - 110.f);
			Shot(TEXT("L_6_Together.png"), C + FVector(-850, -780, 520), C + FVector(100, 100, -150));
		}
		Next();
		break;

	case 7:
		if (StepFrames < 24)
		{
			break;
		}
		{
			const FSandHeightStats Stats = S->ReadStats();
			USandContactPointComponent* BoxPad = Box->GetContactPoints()[0];
			const FVector BoxAt = Box->GetActorLocation();
			Check(BoxPad->GetImpactStamps() >= 1, FString::Printf(TEXT("level crate: real physics impacts %d, last speed %.0f cm/s"), BoxPad->GetImpactStamps(), BoxPad->GetLastImpactSpeed()));
			Check(S->SampleHeightCm(FVector(BoxAt.X, BoxAt.Y, 0.f)) < -1.f, FString::Printf(TEXT("level crate: dent under it %.2f cm"), S->SampleHeightCm(FVector(BoxAt.X, BoxAt.Y, 0.f))));
			int32 Segments = 0, Dented = 0, Total = 0;
			for (USandContactPointComponent* Wheel : Vehicle->GetContactPoints())
			{
				Segments += Wheel->GetWheelSegments();
				const TArray<FVector>& Path = Wheel->GetStampPath();
				for (int32 i = 1; i < Path.Num(); ++i)
				{
					++Total;
					Dented += S->SampleHeightCm((Path[i] + Path[i - 1]) * 0.5) < -0.8f ? 1 : 0;
				}
			}
			Check(Segments >= 80 && Total > 0 && Dented >= Total * 0.95, FString::Printf(TEXT("level vehicle: %d track segments, %d of %d midpoints dented"), Segments, Dented, Total));
			float PadDepth = 0.f;
			for (USandContactPointComponent* Pad : Lander->GetContactPoints())
			{
				const FVector P = Pad->GetComponentLocation();
				PadDepth = FMath::Min(PadDepth, S->SampleHeightCm(FVector(P.X, P.Y, 0.f)));
			}
			Check(Lander->GetTotalImpactStamps() >= 4 && PadDepth < -3.f, FString::Printf(TEXT("level lander: %d pad impacts, deepest crater %.2f cm"), Lander->GetTotalImpactStamps(), PadDepth));
			// the heavy character
			const USandInteractionComponent* GI = Golem->Interaction;
			const float Range = FMath::Max(GI->GetFootBallMax(0) - GI->GetFootBallMin(0), GI->GetFootBallMax(1) - GI->GetFootBallMin(1));
			UE_LOG(LogSandDeformation, Log, TEXT("SandSelfTest: heavy character: %d foot prints (walk %d run %d), ball bone height range %.1f cm, last depth %.2f cm, half width %.1f cm, %s"), GI->GetFootStamps(), GI->GetWalkStamps(), GI->GetRunStamps(), Range, GI->GetLastDepth(), GI->GetLastWidth(), GI->bFootStamps ? TEXT("bones") : TEXT("stride"));
			Check(GI->GetFootStamps() >= 4, FString::Printf(TEXT("level heavy character: %d foot prints"), GI->GetFootStamps()));
			float FootDent = 0.f;
			for (const FVector& P : GI->GetFootStampPath())
			{
				FootDent = FMath::Min(FootDent, S->SampleHeightCm(P));
			}
			Check(FootDent < -3.f, FString::Printf(TEXT("level heavy character: deepest print %.2f cm (a normal player walks at %.1f cm)"), FootDent, GI->FootDepthWalk));
			Check(GI->GetLastDepth() > GI->FootDepthWalk * 1.4f, FString::Printf(TEXT("level heavy character: last print %.2f cm deep, deeper than the reference weight (%.1f cm)"), GI->GetLastDepth(), GI->FootDepthWalk));
			Check(Stats.NonFinite == 0 && SandSim::GetFailureCount() == 0 && S->GetDroppedStamps() == 0, FString::Printf(TEXT("level, all actors together: invalid values %d, GPU failures %d, dropped %d, outside %d; min %.2f max %.2f cm, %d dent pixels, %d passes, %d stamp requests (foot %d wheel %d object %d impact %d)"),
				Stats.NonFinite, SandSim::GetFailureCount(), S->GetDroppedStamps(), S->GetOutsideRejected(), Stats.MinCm, Stats.MaxCm, Stats.DentPixels, SandSim::GetDispatchCount(), S->GetStampCount(),
				S->GetRequestCount(ESandBrushType::Footprint), S->GetRequestCount(ESandBrushType::Wheel), S->GetRequestCount(ESandBrushType::ObjectContact), S->GetRequestCount(ESandBrushType::Impact)));
			S->SaveHeightfieldPNG(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SandDeformationSelfTest") / TEXT("L_7_Heightfield_Debug.png")));
			MarkStamps = S->GetStampCount();
		}
		Next();
		break;

	case 8:
		if (StepFrames < 90)
		{
			break;
		}
		Check(S->GetStampCount() == MarkStamps, FString::Printf(TEXT("level: nothing is sent while all actors stand still: stamp requests %d -> %d in 90 frames"), MarkStamps, S->GetStampCount()));
		Finish();
		break;

	default:
		break;
	}
}

void ASandSelfTest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	++Frames;
	++StepFrames;
	if (Frames > 5)
	{
		TimeSum += DeltaSeconds;
		TimeMax = FMath::Max(TimeMax, DeltaSeconds);
	}
	if (bHopMode)
	{
		TickHopMode(DeltaSeconds);
		return;
	}
	if (bLevelMode)
	{
		TickLevelMode(DeltaSeconds);
		return;
	}
	USandSurfaceComponent* S = SurfaceActor ? SurfaceActor->Surface.Get() : nullptr;
	auto Next = [this]() { ++Step; StepFrames = 0; };
	const FVector C = Centre + FVector(0, 0, 0);
	auto At = [&](double X, double Y) { return FVector(C.X + X, C.Y + Y, C.Z); };

	switch (Step)
	{
	case 0: // wait for the surface
		if (StepFrames < 6)
		{
			break;
		}
		if (!S || !S->IsSurfaceReady())
		{
			Check(false, TEXT("surface initialized (compute shaders available? this test needs a rendered run)"));
			Finish();
			break;
		}
		Check(true, FString::Printf(TEXT("surface initialized: %d vertices, %.1f MB, patch %.0f cm"), S->GetPatchVertices(), S->GetMemoryMB(), S->PatchSizeCm));
		Check(S->GetSurfaceMaterial() && S->GetSurfaceMaterial()->K2_GetTextureParameterValue(TEXT("TrailRT")) == S->GetHeightfield(), TEXT("material TrailRT is the heightfield texture"));
		{
			// requests from the library (no component on the caller), from a component on a plain actor, and invalid ones
			Check(USandDeformationLibrary::ApplySandDeformation(this, At(0, 0), 40.f, 2.f, ESandBrush::Round), TEXT("library: round dent accepted"));
			Check(USandDeformationLibrary::ApplySandDeformation(this, At(200, 0), 6.f, 1.f, ESandBrush::Foot, 0.f), TEXT("library: foot print accepted"));
			Check(Interaction->RequestDeformation(At(-200, 200), 25.f, 1.5f), TEXT("plain actor component: request accepted"));
			Check(USandDeformationLibrary::ApplySandDeformation(this, At(500, 0), 30.f, 1.5f), TEXT("library: dent for the scroll test accepted"));
			// inside all three patch positions of this test (x = 0..400 of the patch grid) and farther than the absorbing rim (37 cm) from every edge
			Check(USandDeformationLibrary::ApplySandDeformation(this, At(200, -300), 30.f, 2.f), TEXT("library: dent that has to survive two scrolls accepted"));
			Check(!USandDeformationLibrary::ApplySandDeformation(this, FVector(NAN, 0, 0), 30.f, 1.f), TEXT("NaN position refused"));
			Check(!USandDeformationLibrary::ApplySandDeformation(this, At(0, 0), 30.f, 0.f), TEXT("strength 0 refused"));
			Check(!USandDeformationLibrary::ApplySandDeformation(this, At(50000, 0), 30.f, 1.f), TEXT("position outside every patch refused"));
			Check(!S->ApplyDeformation(At(0, 0), 30.f, FMath::Sqrt(-1.f)), TEXT("NaN strength refused"));
		}
		Next();
		break;

	case 1: // let the GPU run, then read the heightfield
		if (StepFrames < 12)
		{
			break;
		}
		{
			const float H1 = S->SampleHeightCm(At(0, 0)), H2 = S->SampleHeightCm(At(200, 0)), H3 = S->SampleHeightCm(At(-200, 200));
			DentD2 = S->SampleHeightCm(At(500, 0));
			const FSandHeightStats Stats = S->ReadStats();
			Check(H1 < -4.5f && H1 > -6.8f, FString::Printf(TEXT("round dent depth at the centre %.2f cm (6 expected)"), H1));
			Check(H2 < -1.5f, FString::Printf(TEXT("foot print depth %.2f cm"), H2));
			Check(H3 < -3.0f && H3 > -5.8f, FString::Printf(TEXT("plain actor dent depth %.2f cm (4.5 expected)"), H3));
			Check(DentD2 < -3.0f, FString::Printf(TEXT("scroll test dent depth %.2f cm"), DentD2));
			Check(Stats.MaxCm > 0.3f, FString::Printf(TEXT("raised rim exists: max %.2f cm"), Stats.MaxCm));
			Check(Stats.NonFinite == 0, FString::Printf(TEXT("no invalid heightfield values (%d)"), Stats.NonFinite));
			Check(S->GetSimSteps() > 0, FString::Printf(TEXT("simulation ran: %d steps"), S->GetSimSteps()));
			Shot(TEXT("SandSelfTest_1_Stamps.png"), At(-420, -520) + FVector(0, 0, 300), At(120, 60));
		}
		Next();
		break;

	case 2: // move the plain actor out of the patch range: the patch has to follow
		if (StepFrames < 40)
		{
			break;
		}
		TilesBefore = S->GetTilesBuilt();
		OriginBefore = S->GetOriginTile();
		Walker->SetActorLocation(At(700, 0) + FVector(0, 0, 25));
		Next();
		break;

	case 3:
		if (S->GetScrollCount() < 1 || S->GetPendingTiles() > 0)
		{
			if (StepFrames > 600)
			{
				Check(false, TEXT("patch followed the actor within 600 frames"));
				Finish();
			}
			break;
		}
		if (StepFrames < 6)
		{
			break;
		}
		{
			const FIntPoint Delta = S->GetOriginTile() - OriginBefore;
			Check(Delta == FIntPoint(2, 0), FString::Printf(TEXT("patch scrolled by %d,%d tiles (2,0 expected)"), Delta.X, Delta.Y));
			Check(S->GetTilesBuilt() - TilesBefore == 8, FString::Printf(TEXT("only the new tiles were built: %d (8 expected)"), S->GetTilesBuilt() - TilesBefore));
			const float KeptD2 = S->SampleHeightCm(At(500, 0));
			Check(FMath::Abs(KeptD2 - DentD2) < 0.3f, FString::Printf(TEXT("overlap kept: dent %.2f cm before, %.2f cm after"), DentD2, KeptD2));
			const float NewStrip = S->SampleHeightCm(At(1300, 0));
			Check(FMath::Abs(NewStrip) < 0.05f, FString::Printf(TEXT("new strip untouched: %.3f cm"), NewStrip));
			const float Wrong = FMath::Min(S->SampleHeightCm(At(500 + 800, 0)), S->SampleHeightCm(At(500 + 1600, 0)));
			Check(Wrong > -0.05f, FString::Printf(TEXT("old dent not shown at a shifted position: %.3f cm"), Wrong));
			Check(USandDeformationLibrary::ApplySandDeformation(this, At(1300, 0), 30.f, 2.f), TEXT("deformation in the new strip accepted"));
			Check(!USandDeformationLibrary::ApplySandDeformation(this, At(-400, 0), 30.f, 1.f), TEXT("position that left the patch refused"));
		}
		Next();
		break;

	case 4:
		if (StepFrames < 12)
		{
			break;
		}
		{
			const float H = S->SampleHeightCm(At(1300, 0));
			Check(H < -4.5f, FString::Printf(TEXT("dent in the new strip %.2f cm"), H));
			Shot(TEXT("SandSelfTest_2_Scrolled.png"), At(520, -520) + FVector(0, 0, 300), At(900, 0));
		}
		Next();
		break;

	case 5: // back again: the data of the first dent that is still inside the patch must be where it was, the part that left must be gone
		if (StepFrames < 40)
		{
			break;
		}
		OriginBefore = S->GetOriginTile();
		Walker->SetActorLocation(At(-400, 0) + FVector(0, 0, 25));
		Next();
		break;

	case 6:
		if (S->GetScrollCount() < 2 || S->GetPendingTiles() > 0 || StepFrames < 12)
		{
			if (StepFrames > 600)
			{
				Check(false, TEXT("patch followed the actor back within 600 frames"));
				Finish();
			}
			break;
		}
		{
			const FIntPoint Delta = S->GetOriginTile() - OriginBefore;
			Check(Delta == FIntPoint(-3, 0), FString::Printf(TEXT("patch scrolled back by %d,%d tiles (-3,0 expected)"), Delta.X, Delta.Y));
			const float Half = S->SampleHeightCm(At(200, -300));	// survived two scrolls
			const float Gone = S->SampleHeightCm(At(-25, 0));	// left half: left the patch at the first scroll, comes back as untouched sand
			Check(Half < -4.f, FString::Printf(TEXT("data kept through two scrolls: %.2f cm (6 before)"), Half));
			Check(Gone > -0.05f, FString::Printf(TEXT("data that left the patch is not stored: %.3f cm"), Gone));
		}
		Next();
		break;

	case 7: // many requests, then reset
		for (int32 i = 0; i < 200; ++i)
		{
			const FVector P = At(FMath::FRandRange(-1100.f, 300.f), FMath::FRandRange(-700.f, 700.f));
			USandDeformationLibrary::ApplySandDeformation(this, P, FMath::FRandRange(4.f, 60.f), FMath::FRandRange(0.2f, 4.f), i % 2 ? ESandBrush::Foot : ESandBrush::Round, FMath::FRandRange(0.f, 360.f));
		}
		Next();
		break;

	case 8:
		if (StepFrames < 30)
		{
			break;
		}
		{
			const FSandHeightStats Stats = S->ReadStats();
			Check(Stats.NonFinite == 0, FString::Printf(TEXT("200 random requests: no invalid values (%d), min %.2f max %.2f cm, steepest slope %.2f"), Stats.NonFinite, Stats.MinCm, Stats.MaxCm, Stats.MaxGradient));
			Check(Stats.MinCm >= -S->GetDepthLimit() - 0.05f, FString::Printf(TEXT("depth never beyond the limit: %.2f of %.2f cm"), Stats.MinCm, S->GetDepthLimit()));
			Check(SandSim::GetFailureCount() == 0, FString::Printf(TEXT("no GPU pass failed (%d passes)"), SandSim::GetDispatchCount()));
			S->ResetHeightfield();
		}
		Next();
		break;

	case 9:
		if (StepFrames < 6)
		{
			break;
		}
		{
			const FSandHeightStats Stats = S->ReadStats();
			Check(Stats.DentPixels == 0 && Stats.RidgePixels == 0, FString::Printf(TEXT("reset flattens the heightfield (dent %d, ridge %d pixels)"), Stats.DentPixels, Stats.RidgePixels));
		}
		Next();
		break;

	case 10: // Day 20: frame time with the sand idle, then a crate, a vehicle and a lander at once
		if (StepFrames == 1)
		{
			PerfFrames = 0;
			PerfTime = 0.f;
		}
		if (StepFrames > 1)
		{
			++PerfFrames;
			PerfTime += DeltaSeconds;
		}
		if (StepFrames < 62)
		{
			break;
		}
		IdleMs = 1000.f * PerfTime / FMath::Max(PerfFrames, 1);
		{
			UWorld* World = GetWorld();
			auto Spawn = [&](ESandDemoKind Kind, const FVector& At, float YawDeg)
			{
				ASandDemoActor* A = World->SpawnActorDeferred<ASandDemoActor>(ASandDemoActor::StaticClass(), FTransform(FRotator(0, YawDeg, 0), At));
				A->Kind = Kind;
				A->bAutoRun = false;
				return A;
			};
			Box = Spawn(ESandDemoKind::Box, At(-800, 450) + FVector(0, 0, 340), 20.f);
			Box->MassKg = 800.f;
			Box->FinishSpawning(Box->GetActorTransform());
			if (FParse::Param(FCommandLine::Get(), TEXT("SandDeformationDebugCrate")))
			{
				Box->GetContactPoints()[0]->bDebugLog = true;
			}
			Vehicle = Spawn(ESandDemoKind::Vehicle, At(-1050, -500) + FVector(0, 0, 30), 0.f);
			Vehicle->MassKg = 1600.f;
			Vehicle->DriveSpeedCm = 160.f;
			Vehicle->FinishSpawning(Vehicle->GetActorTransform());
			Lander = Spawn(ESandDemoKind::Lander, At(-100, 350), 0.f);
			Lander->MassKg = 8000.f;
			Lander->StartHeightCm = 450.f;
			Lander->FinishSpawning(Lander->GetActorTransform());
			Vehicle->StartRun();
			Lander->StartRun();
			Check(Box->GetContactPoints().Num() == 1 && Vehicle->GetContactPoints().Num() == 4 && Lander->GetContactPoints().Num() == 4, TEXT("demo actors built: crate 1 pad, vehicle 4 wheels, lander 4 pads"));
		}
		PerfFrames = 0;
		PerfTime = 0.f;
		Next();
		break;

	case 11:
		++PerfFrames;
		PerfTime += DeltaSeconds;
		if (!(Vehicle->IsRunFinished() && Lander->IsRunFinished() && StepFrames > 420))
		{
			if (StepFrames > 2400)
			{
				Check(false, TEXT("vehicle and lander finished their run within 2400 frames"));
				Finish();
			}
			break;
		}
		{
			const float ActiveMs = 1000.f * PerfTime / FMath::Max(PerfFrames, 1);
			UE_LOG(LogSandDeformation, Log, TEXT("SandSelfTest: frame time idle sand %.2f ms, crate + vehicle + lander active %.2f ms (%d frames, includes the run start)"), IdleMs, ActiveMs, PerfFrames);
			MarkStamps = S->GetStampCount();
			const FVector BoxAt = Box->GetActorLocation();
			Shot(TEXT("SandSelfTest_3_Crate.png"), BoxAt + FVector(-260, -300, 160), BoxAt - FVector(0, 0, 30));
		}
		Next();
		break;

	case 12:
		if (StepFrames < 24)
		{
			break;
		}
		{
			const FVector VehicleAt = Vehicle->GetActorLocation();
			Shot(TEXT("SandSelfTest_4_Vehicle.png"), VehicleAt + FVector(-380, -330, 230), VehicleAt - FVector(150, 0, 40));
		}
		Next();
		break;

	case 13:
		if (StepFrames < 24)
		{
			break;
		}
		{
			const FVector LanderAt = Lander->GetActorLocation();
			Shot(TEXT("SandSelfTest_5_Lander.png"), LanderAt + FVector(-520, -460, 160), LanderAt - FVector(0, 0, 90));
		}
		Next();
		break;

	case 14:
		if (StepFrames < 24)
		{
			break;
		}
		Shot(TEXT("SandSelfTest_6_Together.png"), At(-1230, -760) + FVector(0, 0, 330), At(-600, 100));
		Next();
		break;

	case 15:
		if (StepFrames < 24)
		{
			break;
		}
		{
			// results: crate
			const FSandHeightStats Stats = S->ReadStats();
			const FVector BoxAt = Box->GetActorLocation();
			USandContactPointComponent* BoxPad = Box->GetContactPoints()[0];
			Check(BoxPad->GetImpactStamps() >= 1, FString::Printf(TEXT("crate: dropped from 300 cm, real physics impacts %d, last impact speed %.0f cm/s, %d resting stamp(s)"), BoxPad->GetImpactStamps(), BoxPad->GetLastImpactSpeed(), BoxPad->GetStaticStamps()));
			const float BoxDent = S->SampleHeightCm(FVector(BoxAt.X, BoxAt.Y, 0.f));
			Check(BoxDent < -1.f, FString::Printf(TEXT("crate: dent under it %.2f cm (crate at %s)"), BoxDent, *BoxAt.ToString()));
			Check(FMath::Abs(BoxPad->GetLastHalfWidthCm() - 40.f) < 2.f, FString::Printf(TEXT("crate: pad footprint taken from the mesh, half width %.1f cm (40 expected)"), BoxPad->GetLastHalfWidthCm()));
			// vehicle: continuous tracks
			int32 Segments = 0, Dented = 0, Total = 0;
			for (USandContactPointComponent* Wheel : Vehicle->GetContactPoints())
			{
				Segments += Wheel->GetWheelSegments();
				const TArray<FVector>& Path = Wheel->GetStampPath();
				for (int32 i = 1; i < Path.Num(); ++i)
				{
					const FVector Mid = (Path[i] + Path[i - 1]) * 0.5;
					++Total;
					Dented += S->SampleHeightCm(Mid) < -0.8f ? 1 : 0;
				}
			}
			Check(Segments >= 80 && Total > 0 && Dented >= Total * 0.95, FString::Printf(TEXT("vehicle: %d track segments, %d of %d segment midpoints are dented (>= 95%% needed): no gaps"), Segments, Dented, Total));
			const TArray<FVector>& P0 = Vehicle->GetContactPoints()[0]->GetStampPath();
			if (P0.Num() > 12)
			{
				const FVector2D First = FVector2D(P0[3] - P0[1]).GetSafeNormal(), Last = FVector2D(P0.Last() - P0[P0.Num() - 3]).GetSafeNormal();
				const float Turn = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector2D::DotProduct(First, Last), -1.f, 1.f)));
				Check(Turn > 40.f, FString::Printf(TEXT("vehicle: the track turned %.0f degrees with the vehicle"), Turn));
			}
			else
			{
				Check(false, TEXT("vehicle: enough track points to measure the turn"));
			}
			Check(Vehicle->GetTotalImpactStamps() == 0, FString::Printf(TEXT("vehicle: rolling wheels made no impacts (%d)"), Vehicle->GetTotalImpactStamps()));
			// lander
			float PadDepth = 0.f;
			for (USandContactPointComponent* Pad : Lander->GetContactPoints())
			{
				const FVector P = Pad->GetComponentLocation();
				PadDepth = FMath::Min(PadDepth, S->SampleHeightCm(FVector(P.X, P.Y, 0.f)));
			}
			Check(Lander->GetTotalImpactStamps() >= 4, FString::Printf(TEXT("lander: %d pad impacts (4 pads), last touchdown speed %.0f cm/s (scripted descent, not a collision)"), Lander->GetTotalImpactStamps(), Lander->GetContactPoints()[0]->GetLastImpactSpeed()));
			Check(PadDepth < -3.f, FString::Printf(TEXT("lander: deepest pad crater %.2f cm"), PadDepth));
			Check(Lander->GetTotalStaticStamps() <= 8, FString::Printf(TEXT("lander: no repeated stamps while it stands (%d resting stamps)"), Lander->GetTotalStaticStamps()));
			// everything together
			Check(Stats.NonFinite == 0, FString::Printf(TEXT("all actors together: no invalid values (%d), min %.2f max %.2f cm, %d dent pixels"), Stats.NonFinite, Stats.MinCm, Stats.MaxCm, Stats.DentPixels));
			Check(S->GetDroppedStamps() == 0, FString::Printf(TEXT("no requests dropped (%d), %d outside the patch, %d invalid"), S->GetDroppedStamps(), S->GetOutsideRejected(), S->GetInvalidRejected()));
			Check(SandSim::GetFailureCount() == 0, FString::Printf(TEXT("no GPU pass failed (%d passes, %d stamp requests: foot %d wheel %d object %d impact %d)"), SandSim::GetDispatchCount(), S->GetStampCount(),
				S->GetRequestCount(ESandBrushType::Footprint), S->GetRequestCount(ESandBrushType::Wheel), S->GetRequestCount(ESandBrushType::ObjectContact), S->GetRequestCount(ESandBrushType::Impact)));
			UE_LOG(LogSandDeformation, Log, TEXT("SandSelfTest: ground traces of the contact points: crate %d, vehicle %d, lander %d"), Box->GetTotalTraces(), Vehicle->GetTotalTraces(), Lander->GetTotalTraces());
			S->SaveHeightfieldPNG(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SandDeformationSelfTest") / TEXT("Heightfield_Debug.png")));
			MarkStamps = S->GetStampCount();
		}
		Next();
		break;

	case 16: // nothing may be sent while everything stands still
		if (StepFrames < 90)
		{
			break;
		}
		Check(S->GetStampCount() == MarkStamps, FString::Printf(TEXT("nothing is sent while all actors stand still: stamp requests %d -> %d in 90 frames"), MarkStamps, S->GetStampCount()));
		Finish();
		break;

	default:
		break;
	}
}

