// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dune/DuneSandTrail.h"
#include "Dune/DuneSandTerrain.h"
#include "Dune/DuneSandTestActors.h"
#include "Dune/Boss/DuneBossGameMode.h"
#include "HoldItCharacter.h"
#include "SandInteractionComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "Materials/MaterialInterface.h"
#include "UE_HOLDITTOGETHER.h"

ADuneSandTrail::ADuneSandTrail()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Surface = CreateDefaultSubobject<USandSurfaceComponent>(TEXT("SandSurface"));
	Surface->bAutoInitialize = false; // configured for the level in BeginPlay first
	Interaction = CreateDefaultSubobject<USandInteractionComponent>(TEXT("SandInteraction"));
	Interaction->bAutomaticCharacterMarks = true;
	Surface->SurfaceMaterial = ConstructorHelpers::FObjectFinder<UMaterialInterface>(TEXT("/Game/DUNE/Materials/M_DUNE_SandTrail.M_DUNE_SandTrail")).Object;
}

void ADuneSandTrail::BeginPlay()
{
	for (TActorIterator<ADuneSandTerrain> It(GetWorld()); It; ++It)
	{
		Terrain = *It;
		break;
	}
	USandSurfaceComponent* S = Surface;
	USandInteractionComponent* I = Interaction;
	if (Terrain.IsValid())
	{
		// the sand test level: a patch that follows the player over analytic ground and takes over the terrain tiles it covers
		ADuneSandTerrain* T = Terrain.Get();
		S->PatchCenter = T->PatchCenter;
		S->PatchSizeCm = T->PatchSizeCm;
		S->TileSizeCm = T->TileCm;
		S->GridCm = 2.5f;
		S->GroundOffsetCm = 0.f;
		S->bFollowTarget = true;
		S->HeightRangeCm = 44.f;		// room for big craters (the depth limit is 0.48 of this); the material takes the range from the surface
		S->MaxDepressionCm = 21.f;
		S->MaxRidgeCm = 9.f;			// big impacts throw up a tall rim (cap 1.6 x this for round stamps)
		S->RecoverySeconds = 150.f;		// a print lasts minutes, a crater from a fight heals before the next stage
		S->RecoveryReferenceCm = I->FootDepthRun;
		S->WindTransportStrength = 6.f;	// at full wind a dent drifts 6 cm/s: craters soften and drift
		S->NormalStrength = 3.f;          // sharper shading: the prints and dents were too faint to read
		S->DepthDarkening = 0.03f;       // the bottom of a dent is darker
		I->FootDepthWalk = 4.5f;
		I->FootDepthRun = 7.5f;
		S->RecoveryReferenceCm = I->FootDepthRun;
		S->GroundSampler = [T](float X, float Y, float& OutZ) { OutZ = T->HeightAt(X, Y); return true; };
		S->SandFloorPredicate = [](const AActor* Actor) { return Cast<ADuneSandTerrain>(Actor) != nullptr; };
		S->OnTileBuilt.AddDynamic(T, &ADuneSandTerrain::OnSandTileBuilt);
		I->LandingMinWidth = 30.f;		// steeper, easier to read landing dents
		I->LandingMaxWidth = 80.f;
		if (GetWorld()->GetAuthGameMode<ADuneBossGameMode>())
		{
			// the boss arena: a patch twice as wide (the slabs, eruptions and the wave land far from the player; the marks outside a small patch were dropped or erased
			// when it moved), a coarser grid to pay for it, and softer marks (shallower, a lower rim, less sharp shading)
			S->PatchSizeCm = 3200.f;
			S->GridCm = 4.f;
			S->MaxDepressionCm = 16.f;
			S->MaxRidgeCm = 6.f;
			S->NormalStrength = 2.6f;
			S->WindTransportStrength = 8.f;
			I->FootDepthRun = 6.f;
			I->FootDepthWalk = 4.f;
		}
	}
	else
	{
		// the older levels: a Landscape under a patch that floats LiftCm above it and stays where it is
		ALandscapeProxy* Landscape = nullptr;
		for (TActorIterator<ALandscapeProxy> It(GetWorld()); It; ++It)
		{
			Landscape = *It;
			break;
		}
		const ASandTestDirector* TestDirector = nullptr;
		for (TActorIterator<ASandTestDirector> It(GetWorld()); It; ++It)
		{
			TestDirector = *It;
			break;
		}
		S->PatchCenter = PatchCenter;
		S->PatchSizeCm = PatchSizeCm;
		S->TileSizeCm = 400.f;
		S->GridCm = GridCm;
		S->GroundOffsetCm = LiftCm;
		S->EdgeBlendCm = EdgeBlendCm;
		S->bFollowTarget = false;
		S->MaxDepressionCm = LiftCm - 0.8f;	// a dent must not reach through the floating patch
		S->RecoverySeconds = 90.f;
		S->RecoveryReferenceCm = 9.f;
		S->WindTransportStrength = 12.f;
		S->NormalStrength = 1.5f;
		if (TestDirector)
		{
			// the sand interaction level (Day 21): a patch that follows the controlled actor over the Landscape. The mesh lies on the landscape and the
			// material lifts it (TrailLift) so dents can go below the landscape surface, blended to nothing at the rim of the moving patch.
			S->PatchSizeCm = 1600.f;
			S->GridCm = 2.5f;
			S->GroundOffsetCm = 0.f;
			S->MaterialLiftCm = 12.f;
			S->MaterialEdgeBlendCm = 320.f;
			S->bFollowTarget = true;
			S->GroundSampleStride = 4;
			S->ScrollThresholdCm = 80.f; // the patch re-centres early, so large actors (the spaceship's pads) stay inside it
			S->MaxDepressionCm = S->MaterialLiftCm - 0.8f;
			S->RecoverySeconds = 240.f;
			S->WindTransportStrength = 4.f;
			S->NormalStrength = 1.8f;
			const APlayerController* FirstPC = GetWorld()->GetFirstPlayerController();
			const APawn* FirstPawn = FirstPC ? FirstPC->GetPawn() : nullptr;
			S->PatchCenter = FirstPawn ? FVector2D(FirstPawn->GetActorLocation().X, FirstPawn->GetActorLocation().Y) : FVector2D(-700.f, 0.f);
		}
		S->GroundSampler = [Landscape](float X, float Y, float& OutZ)
		{
			const TOptional<float> Height = Landscape ? Landscape->GetHeightAtLocation(FVector(X, Y, 0.f)) : TOptional<float>();
			OutZ = Height.Get(0.6f);
			return Height.IsSet();
		};
		S->SandFloorPredicate = [](const AActor* Actor) { return Cast<ALandscapeProxy>(Actor) != nullptr; };
		I->LandingMinWidth = TestDirector ? 30.f : 40.f;
		I->LandingMaxWidth = TestDirector ? 80.f : 110.f;
		if (!Landscape)
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("SandTrail: no landscape and no ADuneSandTerrain in the level, the patch lies at z=0.6"));
		}
	}
	I->bDebugLog = bDebugLog;
	Super::BeginPlay();
	if (!S->InitializeSurface())
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("SandTrail: the SandDeformation surface could not be initialized (no compute shaders or no material), no trail"));
	}
}

void ADuneSandTrail::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	AActor* Pawn = PC ? PC->GetPawn() : nullptr;
	if (BoundPawn.Get() != Pawn)
	{
		// a pawn that carries its own interaction component (the test golem) makes its own marks: tracking it here as well would stamp every step twice
		const bool bOwnInteraction = Pawn && Pawn->FindComponentByClass<USandInteractionComponent>() != nullptr;
		Interaction->SetTrackedActor(bOwnInteraction ? nullptr : Pawn);
		BoundPawn = Pawn;
	}
	const AHoldItCharacter* HoldIt = Cast<AHoldItCharacter>(Pawn);
	Interaction->WeightKg = HoldIt ? HoldIt->PlayerWeight : Interaction->ReferenceWeightKg;
}

bool ADuneSandTrail::IsReady() const { return Surface->IsSurfaceReady(); }
bool ADuneSandTrail::IsInsidePatch(const FVector& World, float MarginCm) const { return Surface->IsInsidePatch(World, MarginCm); }
float ADuneSandTrail::SampleHeightCm(const FVector& World) const { return Surface->SampleHeightCm(World); }
FSandTrailStats ADuneSandTrail::ReadStats() const { return Surface->ReadStats(); }
bool ADuneSandTrail::SaveHeightfieldPNG(const FString& Path) const { return Surface->SaveHeightfieldPNG(Path); }
float ADuneSandTrail::GetTrailMemoryMB() const { return Surface->GetMemoryMB(); }
int32 ADuneSandTrail::GetPatchVertices() const { return Surface->GetPatchVertices(); }
bool ADuneSandTrail::IsOnSand(const ACharacter* Character) const { return Interaction->IsOnSand(); }
void ADuneSandTrail::DebugReset() { Surface->ResetHeightfield(); }
void ADuneSandTrail::SetSimPaused(bool bPause) { Surface->SetSimPaused(bPause); }
int32 ADuneSandTrail::GetSimSteps() const { return Surface->GetSimSteps(); }
bool ADuneSandTrail::IsComputeActive() const { return Surface->IsSurfaceReady(); }
int32 ADuneSandTrail::GetWalkStamps() const { return Interaction->GetWalkStamps(); }
int32 ADuneSandTrail::GetRunStamps() const { return Interaction->GetRunStamps(); }
int32 ADuneSandTrail::GetLandingStamps() const { return Interaction->GetLandingStamps(); }
int32 ADuneSandTrail::GetFootStamps() const { return Interaction->GetFootStamps(); }
int32 ADuneSandTrail::GetLandingsIgnored() const { return Interaction->GetLandingsIgnored(); }
int32 ADuneSandTrail::GetBrushUpdates() const { return Surface->GetStampCount(); }
int32 ADuneSandTrail::GetSkippedStill() const { return Interaction->GetSkippedStill(); }
int32 ADuneSandTrail::GetSkippedAir() const { return Interaction->GetSkippedAir(); }
int32 ADuneSandTrail::GetSkippedNotSand() const { return Interaction->GetSkippedNotSand(); }
int32 ADuneSandTrail::GetSkippedOutside() const { return Interaction->GetSkippedOutside(); }
float ADuneSandTrail::GetLastDepth() const { return Interaction->GetLastDepth(); }
float ADuneSandTrail::GetLastWidth() const { return Interaction->GetLastWidth(); }
float ADuneSandTrail::GetLastSpeed() const { return Interaction->GetLastSpeed(); }
const TCHAR* ADuneSandTrail::GetLastKind() const { return Interaction->GetLastKind(); }
float ADuneSandTrail::GetFootBallMin(int32 Foot) const { return Interaction->GetFootBallMin(Foot); }
float ADuneSandTrail::GetFootBallMax(int32 Foot) const { return Interaction->GetFootBallMax(Foot); }

float ADuneSandTrail::VolumeRidge(float DepthCm, bool bRound) const
{
	// the stamp shapes are fixed, so the volumes are known: the ridge is the part RidgeVolumeFraction of the dent volume
	const float Ratio = bRound ? 0.94f / 5.09f : 1.f / 1.2f;
	return FMath::Min(RidgeVolumeFraction * Ratio * DepthCm, EdgeRidgeStrength);
}

void ADuneSandTrail::DebugStamp(const FVector2D& WorldXY, float WidthCm, float DepthCm, float RidgeCm)
{
	FSandStampRequest R;
	R.World = WorldXY;
	R.bRound = true;
	R.HalfWidthCm = R.HalfLenCm = WidthCm;
	R.DepthCm = DepthCm;
	R.RidgeCm = RidgeCm;
	Surface->QueueStamp(R);
	Surface->FlushStamps();
}

void ADuneSandTrail::ApplySandDeformation(const FVector2D& WorldXY, float DirDeg, float WidthCm, float DepthCm, float Strength, bool bRound)
{
	Surface->ApplyDeformation(FVector(WorldXY.X, WorldXY.Y, 0.f), WidthCm, DepthCm * Strength / FMath::Max(Surface->DepthPerStrengthCm, 0.01f), bRound ? ESandBrush::Round : ESandBrush::Foot, DirDeg);
	Surface->FlushStamps();
}
