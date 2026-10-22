// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dune/DuneSandTrail.h"
#include "DuneSandSim.h"
#include "HoldItCharacter.h"
#include "RenderingThread.h"
#include "Misc/App.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "UE_HOLDITTOGETHER.h"

ADuneSandTrail::ADuneSandTrail()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Patch = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SandPatch"));
	Patch->SetupAttachment(RootComponent);
	Patch->SetCollisionEnabled(ECollisionEnabled::NoCollision); // the landscape keeps the collision; the patch is only drawn
	Patch->SetCastShadow(false);
	Patch->bAffectDistanceFieldLighting = false;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Sand(TEXT("/Game/DUNE/Materials/M_DUNE_SandTrail.M_DUNE_SandTrail"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Composite(TEXT("/Game/DUNE/Materials/M_DUNE_SandTrailComposite.M_DUNE_SandTrailComposite"));
	SandMaterial = Sand.Object;
	CompositeMaterial = Composite.Object;
}

FVector2D ADuneSandTrail::WorldToTrailUV(const FVector& World) const
{
	// the patch is axis aligned: U along world X, V along world Y, both from the patch's minimum corner
	const FVector2D Origin = PatchCenter - FVector2D(PatchSizeCm * 0.5f);
	return (FVector2D(World.X, World.Y) - Origin) / PatchSizeCm;
}

bool ADuneSandTrail::IsInsidePatch(const FVector& World, float MarginCm) const
{
	const FVector2D Delta = FVector2D(World.X, World.Y) - PatchCenter;
	const float Half = PatchSizeCm * 0.5f - MarginCm;
	return FMath::Abs(Delta.X) < Half && FMath::Abs(Delta.Y) < Half;
}

bool ADuneSandTrail::IsOnSand(const ACharacter* Character) const
{
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement || !Movement->IsMovingOnGround() || !Movement->CurrentFloor.bBlockingHit)
	{
		return false;
	}
	return Cast<ALandscapeProxy>(Movement->CurrentFloor.HitResult.GetActor()) != nullptr && IsInsidePatch(Character->GetActorLocation());
}

float ADuneSandTrail::GetTrailMemoryMB() const
{
	return Targets.Num() * RenderTargetSize * RenderTargetSize * 16.f / (1024.f * 1024.f); // RGBA32F: 16 bytes per pixel. 32 bit so the slow fade steps are not rounded away; the engine can only read back RGBA formats
}

void ADuneSandTrail::BeginPlay()
{
	Super::BeginPlay();
	if (!SandMaterial || !CompositeMaterial)
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("SandTrail: material /Game/DUNE/Materials/M_DUNE_SandTrail or ..Composite is missing, no trail"));
		return;
	}
	SetActorLocation(FVector(PatchCenter.X - PatchSizeCm * 0.5f, PatchCenter.Y - PatchSizeCm * 0.5f, 0.f));
	for (int32 i = 0; i < 2; ++i)
	{
		Targets.Add(MakeTarget());
		UKismetRenderingLibrary::ClearRenderTarget2D(this, Targets[i], FLinearColor(0.5f, 0.f, 0.f, 1.f)); // 0.5 = untouched sand
	}
	bComputeReady = bUseComputeSim && DuneSandSim::IsSupported();
	BrushMID = UMaterialInstanceDynamic::Create(CompositeMaterial, this);
	SurfaceMID = UMaterialInstanceDynamic::Create(SandMaterial, this);
	const FVector Origin = GetActorLocation();
	SurfaceMID->SetVectorParameterValue(TEXT("TrailArea"), FLinearColor(Origin.X, Origin.Y, PatchSizeCm, 0.f));
	SurfaceMID->SetScalarParameterValue(TEXT("TrailRange"), HeightRangeCm);
	SurfaceMID->SetScalarParameterValue(TEXT("TrailDisplace"), DisplacementScale);
	SurfaceMID->SetVectorParameterValue(TEXT("TrailShade"), FLinearColor(1.f / RenderTargetSize, 1.5f, 0.03f, 0.f));
	SurfaceMID->SetTextureParameterValue(TEXT("TrailRT"), Targets[Current]);
	BuildPatch();
	Patch->SetMaterial(0, SurfaceMID);
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandTrail: patch %.0f x %.0f cm at %s, %d vertices (%.0f cm grid), 2 render targets %dx%d RGBA32F = %.1f MB, %.1f cm per pixel, GPU compute simulation %s"),
		PatchSizeCm, PatchSizeCm, *PatchCenter.ToString(), PatchVertices, GridCm, RenderTargetSize, RenderTargetSize, GetTrailMemoryMB(), PatchSizeCm / RenderTargetSize, bComputeReady ? TEXT("on") : TEXT("off (old material fade only)"));
}

void ADuneSandTrail::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bComputeReady)
	{
		FlushRenderingCommands(); // queued simulation steps still point at the render target resources
		bComputeReady = false;
	}
	if (const ACharacter* Character = Cast<ACharacter>(BoundCharacter.Get()))
	{
		const_cast<ACharacter*>(Character)->LandedDelegate.RemoveDynamic(this, &ADuneSandTrail::OnCharacterLanded);
	}
	Super::EndPlay(EndPlayReason);
}

UTextureRenderTarget2D* ADuneSandTrail::MakeTarget()
{
	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(this);
	Target->RenderTargetFormat = RTF_RGBA32f;
	Target->ClearColor = FLinearColor(0.5f, 0.f, 0.f, 1.f);
	Target->bAutoGenerateMips = false;
	Target->bSupportsUAV = bUseComputeSim && DuneSandSim::IsSupported(); // the compute shaders write into it
	Target->InitAutoFormat(RenderTargetSize, RenderTargetSize);
	Target->UpdateResourceImmediate(true);
	return Target;
}

void ADuneSandTrail::BuildPatch()
{
	ALandscapeProxy* Landscape = nullptr;
	for (TActorIterator<ALandscapeProxy> It(GetWorld()); It; ++It)
	{
		Landscape = *It;
		break;
	}
	const int32 Cells = FMath::Max(1, FMath::RoundToInt(PatchSizeCm / GridCm));
	const float Step = PatchSizeCm / Cells;
	const int32 N = Cells + 1;
	const FVector Origin = GetActorLocation();

	// each vertex sits on the landscape surface (so the patch follows the dunes), lifted by LiftCm inside and sinking back to the surface at the rim
	TArray<float> Z;
	Z.SetNumUninitialized(N * N);
	int32 Missing = 0;
	for (int32 J = 0; J < N; ++J)
	{
		for (int32 I = 0; I < N; ++I)
		{
			const FVector World(Origin.X + I * Step, Origin.Y + J * Step, 0.f);
			TOptional<float> Height = Landscape ? Landscape->GetHeightAtLocation(World) : TOptional<float>();
			if (!Height.IsSet())
			{
				++Missing;
			}
			const float Edge = FMath::Min(FMath::Min(I * Step, J * Step), FMath::Min(PatchSizeCm - I * Step, PatchSizeCm - J * Step));
			const float T = FMath::Clamp(Edge / FMath::Max(EdgeBlendCm, 1.f), 0.f, 1.f);
			const float Blend = T * T * (3.f - 2.f * T);
			Z[J * N + I] = Height.Get(0.f) + 0.6f + (LiftCm - 0.6f) * Blend;
		}
	}
	if (!Landscape || Missing > 0)
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("SandTrail: %s (%d of %d vertices without a landscape height, those lie at z=0.6)"),
			Landscape ? TEXT("landscape height missing") : TEXT("no landscape in the level"), Landscape ? Missing : N * N, N * N);
	}

	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	TArray<int32> Triangles;
	Vertices.Reserve(N * N);
	Triangles.Reserve(Cells * Cells * 6);
	for (int32 J = 0; J < N; ++J)
	{
		for (int32 I = 0; I < N; ++I)
		{
			Vertices.Add(FVector(I * Step, J * Step, Z[J * N + I]));
			const float Zx = (Z[J * N + FMath::Min(I + 1, N - 1)] - Z[J * N + FMath::Max(I - 1, 0)]) / (Step * (I == 0 || I == N - 1 ? 1.f : 2.f));
			const float Zy = (Z[FMath::Min(J + 1, N - 1) * N + I] - Z[FMath::Max(J - 1, 0) * N + I]) / (Step * (J == 0 || J == N - 1 ? 1.f : 2.f));
			Normals.Add(FVector(-Zx, -Zy, 1.f).GetSafeNormal());
			Tangents.Add(FProcMeshTangent(FVector(1.f, 0.f, Zx).GetSafeNormal(), false));
			UVs.Add(FVector2D(I / float(Cells), J / float(Cells)));
			Colors.Add(FLinearColor::White);
		}
	}
	for (int32 J = 0; J < Cells; ++J)
	{
		for (int32 I = 0; I < Cells; ++I)
		{
			const int32 A = J * N + I, B = A + 1, C = A + N, D = C + 1;
			Triangles.Append({ A, C, B, B, C, D }); // the engine takes the normal as (P1-P2) x (P0-P2): this order faces up
		}
	}
	Patch->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
	PatchVertices = Vertices.Num();
}

void ADuneSandTrail::DrawPass(const FVector2D& A, const FVector2D& B, float WidthUV, float Depth, float Ridge, float RecoverCm)
{
	BrushMID->SetVectorParameterValue(TEXT("SegA"), FLinearColor(A.X, A.Y, 0.f, 0.f));
	BrushMID->SetVectorParameterValue(TEXT("SegB"), FLinearColor(B.X, B.Y, 0.f, 0.f));
	BrushMID->SetVectorParameterValue(TEXT("Brush"), FLinearColor(WidthUV, Depth, Ridge, 0.f));
	BrushMID->SetScalarParameterValue(TEXT("RecoverCm"), RecoverCm);
	BrushMID->SetScalarParameterValue(TEXT("Range"), HeightRangeCm);
	BrushMID->SetTextureParameterValue(TEXT("Prev"), Targets[Current]);
	const int32 Next = 1 - Current; // ping-pong: read the newest target, write the other one, then swap
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, Targets[Next], BrushMID);
	Current = Next;
	SurfaceMID->SetTextureParameterValue(TEXT("TrailRT"), Targets[Current]);
}

namespace
{
	const TCHAR* const KindWalk = TEXT("walk");
	const TCHAR* const KindRun = TEXT("run");
	const TCHAR* const KindLanding = TEXT("landing");
}

float ADuneSandTrail::GetCharacterWeight(const ACharacter* Character) const
{
	const AHoldItCharacter* HoldIt = Cast<AHoldItCharacter>(Character);
	return HoldIt ? HoldIt->PlayerWeight : DefaultWeightKg;
}

float ADuneSandTrail::VolumeRidge(float DepthCm, bool bRound) const
{
	// the brush shapes are fixed (smoothstep dent, smoothstep ridge of half width 0.6 W centred 1.35 W away), so the volumes are known:
	// trench per unit length: dent W x D, two ridges 1.2 W x R; round dent: 0.94 W^2 x D, ring 5.09 W^2 x R. The ridge is the part RidgeVolumeFraction of the dent volume
	const float Ratio = bRound ? 0.94f / 5.09f : 1.f / 1.2f;
	return FMath::Min(RidgeVolumeFraction * Ratio * DepthCm, EdgeRidgeStrength);
}

void ADuneSandTrail::WalkBrush(float Speed, float ContactFactor, float Weight, float& Width, float& Depth, float& Ridge) const
{
	// DeformationStrength = BasePressure (weight) x MovementMultiplier (actual speed) x ContactFactor (slope of the floor); a stamp is made of three numbers
	const float Alpha = FMath::Clamp((Speed - MinMovementSpeed) / FMath::Max(FullTrailSpeed - MinMovementSpeed, 1.f), 0.f, 1.f);
	const float Pressure = FMath::Clamp(Weight / FMath::Max(ReferenceWeightKg, 1.f), 0.25f, 3.f) * SandSoftness * ContactFactor;
	const float Limit = FMath::Min(HeightRangeCm * 0.48f, LiftCm - 0.8f);
	Width = TrailWidth * (0.75f + 0.45f * Alpha) * FMath::Sqrt(Pressure);
	Depth = FMath::Min(TrailDepth * (0.3f + 0.7f * FMath::Pow(Alpha, 1.5f)) * Pressure, Limit);
	Ridge = FMath::Min(VolumeRidge(Depth, false), Limit);
}

void ADuneSandTrail::Stamp(const FVector& From, const FVector& To, float Width, float Depth, float Ridge, const TCHAR* Kind, float Speed)
{
	DrawPass(WorldToTrailUV(From), WorldToTrailUV(To), Width / PatchSizeCm, Depth, Ridge, 0.f);
	++BrushUpdates;
	WalkStamps += Kind == KindWalk ? 1 : 0;
	RunStamps += Kind == KindRun ? 1 : 0;
	LandingStamps += Kind == KindLanding ? 1 : 0;
	LastKind = Kind;
	LastDepth = Depth;
	LastWidth = Width;
	LastSpeed = Speed;
	SinceStamp = 0.f;
	Recovered = 0.f;
	SinceRecover = 0.f;
	SimActive = bRecoveryEnabled ? TrailPersistence * 1.05f + 2.f : 1e9f; // the simulation only runs while something is left to settle
}

void ADuneSandTrail::DebugStamp(const FVector2D& WorldXY, float WidthCm, float DepthCm, float RidgeCm)
{
	if (!IsReady())
	{
		return;
	}
	const FVector2D UV = WorldToTrailUV(FVector(WorldXY.X, WorldXY.Y, 0.f));
	DrawPass(UV, UV, WidthCm / PatchSizeCm, DepthCm, RidgeCm, 0.f);
	SimActive = bRecoveryEnabled ? TrailPersistence * 1.05f + 2.f : 1e9f;
}

void ADuneSandTrail::DebugReset()
{
	for (UTextureRenderTarget2D* Target : Targets)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, Target, FLinearColor(0.5f, 0.f, 0.f, 1.f));
	}
	SimActive = 0.f;
	SimAccum = 0.f;
	bHasLast = false;
}

void ADuneSandTrail::Recover(float Seconds)
{
	// old fade (material pass, linear): used when the compute simulation is off
	const float Amplitude = FMath::Max(TrailDepth, EdgeRidgeStrength);
	const float Step = Amplitude / FMath::Max(TrailPersistence, 1.f) * Seconds;
	DrawPass(FVector2D(0.5f), FVector2D(0.5f), 0.02f, 0.f, 0.f, Step);
	++RecoveryPasses;
	Recovered += Step; // once the fade has covered the deepest possible trail, nothing is left and the passes stop
}

void ADuneSandTrail::StepSimulation(float DeltaSeconds)
{
	if (!bComputeReady || bSimPaused || SimActive <= 0.f)
	{
		SimAccum = 0.f;
		return;
	}
	SimActive -= DeltaSeconds;
	SimAccum += DeltaSeconds;
	const float Cell = PatchSizeCm / RenderTargetSize;
	for (int32 Steps = 0; SimAccum >= SimInterval && Steps < MaxSubSteps; ++Steps)
	{
		SimAccum -= SimInterval;
		FDuneSandSimStep Step;
		Step.RangeCm = HeightRangeCm;
		Step.CellCm = Cell;
		const float FadeCmPerSec = FMath::Max3(TrailDepth, EdgeRidgeStrength, LandingMaxDepth) / FMath::Max(TrailPersistence, 1.f) * (1.f + WindRecoveryMultiplier * WindStrength);
		Step.RecoverStepCm = bRecoveryEnabled ? FadeCmPerSec * SimInterval : 0.f;
		const FVector2D Dir = WindDirection.GetSafeNormal();
		FVector2D Wind = Dir * (WindStrength * WindTransportStrength * SimInterval / Cell); // texels per step; the patch is axis aligned so world X/Y are texel x/y
		const float Sum = FMath::Abs(Wind.X) + FMath::Abs(Wind.Y);
		if (Sum > 0.45f)
		{
			Wind *= 0.45f / Sum; // upwind transport is only stable while the total moved per step stays below about half a texel
		}
		Step.WindCells = FVector2f(Wind);
		Step.SlopeTan = FMath::Tan(FMath::DegreesToRadians(SlopeThresholdDeg));
		Step.SlopeRate = bSlopeCollapse ? FMath::Clamp(SlopeRelaxRate, 0.f, 0.2f) : 0.f;
		Step.MaxTransferCm = MaxSlopeTransferCm;
		const int32 Next = 1 - Current;
		DuneSandSim::Dispatch(Targets[Current], Targets[Next], Step);
		Current = Next;
		SurfaceMID->SetTextureParameterValue(TEXT("TrailRT"), Targets[Current]);
		++SimSteps;
	}
	if (SimAccum >= SimInterval)
	{
		SimAccum = 0.f; // a long frame: drop the rest instead of catching up
	}
}

void ADuneSandTrail::OnCharacterLanded(const FHitResult& Hit)
{
	// one event per touchdown; FallSpeed is the speed before it (the velocity is already zero here)
	const float Speed = FallSpeed;
	FallSpeed = 0.f;
	const AActor* Character = BoundCharacter.Get();
	if (!Character || Speed < LandingMinSpeed)
	{
		++LandingsIgnored;
		return;
	}
	if (!Cast<ALandscapeProxy>(Hit.GetActor()))
	{
		++SkippedNotSand; // a rock, a house or a floor: no dent
		return;
	}
	if (!IsInsidePatch(Character->GetActorLocation(), LandingMinWidth))
	{
		++SkippedOutside;
		return;
	}
	bPendingLand = true;
	PendingLandSpeed = Speed;
	PendingLandPos = Character->GetActorLocation();
}

void ADuneSandTrail::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsReady())
	{
		return;
	}
	SinceUpdate += DeltaSeconds;
	SinceStamp += DeltaSeconds;
	SinceRecover += DeltaSeconds;
	StepSimulation(DeltaSeconds);

	// without the compute simulation the texture only changes while there is something to fade: a pass per second until the last trail is gone
	if (!bComputeReady && Recovered < FMath::Max(TrailDepth, EdgeRidgeStrength) && SinceRecover >= 1.f)
	{
		Recover(SinceRecover);
		SinceRecover = 0.f;
	}

	if (bDebugLog)
	{
		LogTimer += DeltaSeconds;
		if (LogTimer >= 2.f)
		{
			LogTimer = 0.f;
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandDebug: last stamp %s speed %.0f depth %.2f width %.1f | stamps walk %d run %d landing %d | wind %s x %.2f | sim steps %d active %.1f s, compute dispatches %d failures %d | skipped still %d air %d not-sand %d"),
				LastKind, LastSpeed, LastDepth, LastWidth, WalkStamps, RunStamps, LandingStamps, *WindDirection.GetSafeNormal().ToString(), WindStrength,
				SimSteps, SimActive, DuneSandSim::GetDispatchCount(), DuneSandSim::GetFailureCount(), SkippedStill, SkippedAir, SkippedNotSand);
		}
	}

	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	ACharacter* Character = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
	if (!Character)
	{
		return;
	}
	if (BoundCharacter.Get() != Character)
	{
		if (ACharacter* Old = Cast<ACharacter>(BoundCharacter.Get()))
		{
			Old->LandedDelegate.RemoveDynamic(this, &ADuneSandTrail::OnCharacterLanded);
		}
		Character->LandedDelegate.AddDynamic(this, &ADuneSandTrail::OnCharacterLanded);
		BoundCharacter = Character;
		FallSpeed = 0.f;
	}
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const float Weight = GetCharacterWeight(Character);
	if (bPendingLand)
	{
		bPendingLand = false;
		const float A = FMath::Clamp((PendingLandSpeed - LandingMinSpeed) / FMath::Max(LandingMaxSpeed - LandingMinSpeed, 1.f), 0.f, 1.f);
		const float Pressure = FMath::Clamp(Weight / FMath::Max(ReferenceWeightKg, 1.f), 0.25f, 3.f) * SandSoftness;
		const float Limit = FMath::Min(HeightRangeCm * 0.48f, LiftCm - 0.8f);
		const float Strong = FMath::Pow(A, 0.6f);
		const float Depth = FMath::Min(FMath::Lerp(LandingMinDepth, LandingMaxDepth, Strong) * Pressure, Limit);
		const float Width = FMath::Lerp(LandingMinWidth, LandingMaxWidth, A) * FMath::Sqrt(Pressure);
		const float Ridge = FMath::Min(VolumeRidge(Depth, true), Limit);
		Stamp(PendingLandPos, PendingLandPos, Width, Depth, Ridge, KindLanding, PendingLandSpeed);
		bHasLast = false;
		if (bDebugLog)
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandDebug: landing at %s fall speed %.0f cm/s -> depth %.2f cm, half width %.0f cm, ridge %.2f cm"), *PendingLandPos.ToString(), PendingLandSpeed, Depth, Width, Ridge);
		}
		return;
	}
	if (!Movement->IsMovingOnGround())
	{
		FallSpeed = FMath::Max(FallSpeed, -Movement->Velocity.Z);
		++SkippedAir;
		bHasLast = false; // a jump breaks the trail: no line is drawn across it
		return;
	}
	if (!Movement->CurrentFloor.bBlockingHit || !Cast<ALandscapeProxy>(Movement->CurrentFloor.HitResult.GetActor()))
	{
		++SkippedNotSand;
		bHasLast = false;
		return;
	}
	const FVector Position = Character->GetActorLocation();
	if (!IsInsidePatch(Position, TrailWidth))
	{
		++SkippedOutside;
		bHasLast = false;
		return;
	}
	const float Speed = Movement->Velocity.Size2D();
	if (Speed < MinMovementSpeed)
	{
		++SkippedStill; // standing still records nothing; the trail continues from here when the character moves on
		return;
	}
	float Width, Depth, Ridge;
	WalkBrush(Speed, FMath::Clamp(Movement->CurrentFloor.HitResult.ImpactNormal.Z, 0.5f, 1.f), Weight, Width, Depth, Ridge);
	const TCHAR* Kind = Speed >= RunSpeedThreshold ? KindRun : KindWalk;
	if (!bHasLast)
	{
		LastPoint = Position;
		bHasLast = true;
		Stamp(Position, Position, Width, Depth, Ridge, Kind, Speed);
		SinceUpdate = 0.f;
		return;
	}
	const float Distance = FVector::Dist2D(Position, LastPoint);
	if (Distance > 800.f)
	{
		bHasLast = false; // teleported: no trench across the gap
		return;
	}
	if (Distance >= TrailSpacing && SinceUpdate >= TrailUpdateInterval)
	{
		Stamp(LastPoint, Position, Width, Depth, Ridge, Kind, Speed); // one segment from the last recorded point, so a fast character leaves no gaps
		LastPoint = Position;
		SinceUpdate = 0.f;
	}
}

float ADuneSandTrail::SampleHeightCm(const FVector& World) const
{
	if (Targets.Num() < 2)
	{
		return 0.f;
	}
	const FVector2D UV = WorldToTrailUV(World);
	const int32 X = FMath::Clamp(FMath::FloorToInt(UV.X * RenderTargetSize), 0, RenderTargetSize - 1);
	const int32 Y = FMath::Clamp(FMath::FloorToInt(UV.Y * RenderTargetSize), 0, RenderTargetSize - 1);
	const FLinearColor Pixel = UKismetRenderingLibrary::ReadRenderTargetRawPixel(const_cast<ADuneSandTrail*>(this), Targets[Current], X, Y, false);
	return (Pixel.R - 0.5f) * HeightRangeCm;
}

FSandTrailStats ADuneSandTrail::ReadStats() const
{
	FSandTrailStats Stats;
	if (Targets.Num() < 2)
	{
		return Stats;
	}
	const int32 Size = RenderTargetSize;
	const TArray<FLinearColor> Pixels = UKismetRenderingLibrary::ReadRenderTargetRawPixelArea(const_cast<ADuneSandTrail*>(this), Targets[Current], 0, 0, Size, Size, false);
	if (Pixels.Num() < Size * Size)
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("SandTrail: read back %d pixels, expected %d"), Pixels.Num(), Size * Size);
		return Stats;
	}
	const FVector2D Origin = PatchCenter - FVector2D(PatchSizeCm * 0.5f);
	const float Cell = PatchSizeCm / Size;
	FVector2D Lo(MAX_flt), Hi(-MAX_flt);
	FVector2D Weighted = FVector2D::ZeroVector;
	auto HeightAt = [&](int32 X, int32 Y) { return (Pixels[Y * Size + X].R - 0.5f) * HeightRangeCm; };
	for (int32 Y = 0; Y < Size; ++Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			const float H = HeightAt(X, Y);
			if (!FMath::IsFinite(H) || FMath::Abs(H) > HeightRangeCm * 0.5f + 0.01f)
			{
				++Stats.NonFinite;
				continue;
			}
			Stats.MinCm = FMath::Min(Stats.MinCm, H);
			Stats.MaxCm = FMath::Max(Stats.MaxCm, H);
			Stats.RidgePixels += H > 0.5f ? 1 : 0;
			Stats.SumCm += H;
			(H < 0.f ? Stats.DentSumCm : Stats.RidgeSumCm) += H;
			if (X + 1 < Size)
			{
				Stats.MaxGradient = FMath::Max(Stats.MaxGradient, FMath::Abs(HeightAt(X + 1, Y) - H) / Cell);
			}
			if (Y + 1 < Size)
			{
				Stats.MaxGradient = FMath::Max(Stats.MaxGradient, FMath::Abs(HeightAt(X, Y + 1) - H) / Cell);
			}
			const FVector2D World = Origin + FVector2D((X + 0.5f) / Size, (Y + 0.5f) / Size) * PatchSizeCm;
			if (H < 0.f)
			{
				Weighted += World * -H;
			}
			if (H < -1.f)
			{
				++Stats.DentPixels;
				Lo = FVector2D(FMath::Min(Lo.X, World.X), FMath::Min(Lo.Y, World.Y));
				Hi = FVector2D(FMath::Max(Hi.X, World.X), FMath::Max(Hi.Y, World.Y));
			}
		}
	}
	if (Stats.DentPixels > 0)
	{
		Stats.DentMinWorld = Lo;
		Stats.DentMaxWorld = Hi;
	}
	if (Stats.DentSumCm < -1e-6)
	{
		Stats.DentCentroid = Weighted / -Stats.DentSumCm;
	}
	return Stats;
}
