// Copyright Epic Games, Inc. All Rights Reserved.

#include "SandSurfaceComponent.h"
#include "SandSim.h"
#include "SandSimulationSubsystem.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "ProceduralMeshComponent.h"
#include "RenderingThread.h"

DEFINE_LOG_CATEGORY(LogSandDeformation);

namespace
{
	const FIntPoint NoTile(MAX_int32, MAX_int32);
	const FLinearColor Untouched(0.5f, 0.f, 0.f, 1.f); // 0.5 = untouched sand
}

USandSurfaceComponent::USandSurfaceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Default(TEXT("/SandDeformation/Materials/M_SandDeformation_Default.M_SandDeformation_Default"));
	DefaultSurfaceMaterial = Default.Object;
}

FVector2D USandSurfaceComponent::GetPatchCenter() const
{
	return OriginXY() + FVector2D(PatchSizeCm * 0.5f);
}

FVector2D USandSurfaceComponent::WorldToUV(const FVector2D& World) const
{
	// the patch is axis aligned: U along world X, V along world Y, both from the patch's minimum corner
	return (World - OriginXY()) / PatchSizeCm;
}

bool USandSurfaceComponent::IsInsidePatch(FVector WorldPosition, float MarginCm) const
{
	if (!bReady)
	{
		return false;
	}
	const FVector2D Delta = FVector2D(WorldPosition.X, WorldPosition.Y) - GetPatchCenter();
	const float Half = PatchSizeCm * 0.5f - MarginCm;
	return FMath::Abs(Delta.X) < Half && FMath::Abs(Delta.Y) < Half;
}

float USandSurfaceComponent::GetRenderedLiftAt(FVector WorldPosition) const
{
	const float Lift = MaterialLiftCm > 0.f ? MaterialLiftCm : GroundOffsetCm;
	if (!bReady || Lift <= 0.f)
	{
		return 0.f;
	}
	// the same smoothstep as the material (TrailLift / TrailEdge): 0 at the rim of the patch, the full lift further in than the blend width
	const FVector2D Delta = FVector2D(WorldPosition.X, WorldPosition.Y) - GetPatchCenter();
	const float Half = PatchSizeCm * 0.5f;
	const float Edge = FMath::Min(Half - FMath::Abs(Delta.X), Half - FMath::Abs(Delta.Y));
	const float Blend = MaterialLiftCm > 0.f ? MaterialEdgeBlendCm : EdgeBlendCm;
	const float T = FMath::Clamp(Edge / FMath::Max(Blend, 1.f), 0.f, 1.f);
	return Lift * T * T * (3.f - 2.f * T);
}

bool USandSurfaceComponent::IsSandFloor(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}
	return bAnyFloorIsSand || Actor == GetOwner() || (!SandActorTag.IsNone() && Actor->ActorHasTag(SandActorTag)) || (SandFloorPredicate && SandFloorPredicate(Actor));
}

float USandSurfaceComponent::GetMemoryMB() const
{
	return Targets.Num() * HeightfieldResolution * HeightfieldResolution * 16.f / (1024.f * 1024.f); // RGBA32F: 16 bytes per pixel
}

int32 USandSurfaceComponent::SlotOf(FIntPoint Tile) const
{
	const int32 N = TilesPerSide;
	return ((Tile.X % N) + N) % N + N * (((Tile.Y % N) + N) % N);
}

void USandSurfaceComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bAutoInitialize)
	{
		InitializeSurface();
	}
}

void USandSurfaceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bReady)
	{
		FlushRenderingCommands(); // queued passes still point at the render target resources
		bReady = false;
		if (UWorld* World = GetWorld())
		{
			if (USandSimulationSubsystem* Subsystem = World->GetSubsystem<USandSimulationSubsystem>())
			{
				Subsystem->UnregisterSurface(this);
			}
		}
	}
	if (Mesh)
	{
		Mesh->DestroyComponent();
		Mesh = nullptr;
	}
	Targets.Reset();
	SurfaceMID = nullptr;
	Pending.Reset();
	Super::EndPlay(EndPlayReason);
}

bool USandSurfaceComponent::InitializeSurface()
{
	if (bReady)
	{
		return true;
	}
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner || !World->IsGameWorld())
	{
		return false;
	}
	if (!SandSim::IsSupported())
	{
		UE_LOG(LogSandDeformation, Warning, TEXT("Surface %s: no compute shader support (null RHI?), the sand stays flat"), *GetNameSafe(Owner));
		return false;
	}
	if (!SurfaceMaterial)
	{
		SurfaceMaterial = DefaultSurfaceMaterial;
	}
	if (!SurfaceMaterial)
	{
		UE_LOG(LogSandDeformation, Warning, TEXT("Surface %s: no SurfaceMaterial and the plugin default material was not found"), *GetNameSafe(Owner));
		return false;
	}

	TileSizeCm = FMath::Max(TileSizeCm, 100.f);
	TilesPerSide = FMath::Max(1, FMath::RoundToInt(PatchSizeCm / TileSizeCm));
	PatchSizeCm = TilesPerSide * TileSizeCm;
	CellsPerTile = FMath::Max(2, FMath::RoundToInt(TileSizeCm / FMath::Max(GridCm, 0.5f)));
	HeightfieldResolution = FMath::Clamp(HeightfieldResolution, 64, 4096);
	bCanScroll = bFollowTarget && HeightfieldResolution % TilesPerSide == 0;
	if (bFollowTarget && !bCanScroll)
	{
		UE_LOG(LogSandDeformation, Warning, TEXT("Surface %s: HeightfieldResolution %d is not a multiple of %d tiles, the patch will not follow"), *GetNameSafe(Owner), HeightfieldResolution, TilesPerSide);
	}
	TileTexels = HeightfieldResolution / TilesPerSide;
	ScrollThresholdCm = FMath::Min(ScrollThresholdCm, PatchSizeCm * 0.45f);

	const float Half = PatchSizeCm * 0.5f;
	if (bCanScroll)
	{
		// a moving patch lies on the world tile grid (tile i covers x = i * TileSizeCm ...), so tiles keep their place when it moves
		LatticeOffset = FVector2D::ZeroVector;
		OriginTile = FIntPoint(FMath::RoundToInt((PatchCenter.X - Half) / TileSizeCm), FMath::RoundToInt((PatchCenter.Y - Half) / TileSizeCm));
	}
	else
	{
		LatticeOffset = PatchCenter - FVector2D(Half); // a patch that stays is placed exactly
		OriginTile = FIntPoint::ZeroValue;
	}
	BakedOriginTile = OriginTile;

	for (int32 i = 0; i < 2; ++i)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(this);
		Target->RenderTargetFormat = RTF_RGBA32f; // 32 bit so the slow fade steps are not rounded away
		Target->ClearColor = Untouched;
		Target->bAutoGenerateMips = false;
		Target->bSupportsUAV = true; // the compute shaders write into it
		Target->InitAutoFormat(HeightfieldResolution, HeightfieldResolution);
		Target->UpdateResourceImmediate(true);
		UKismetRenderingLibrary::ClearRenderTarget2D(this, Target, Untouched);
		Targets.Add(Target);
	}
	Current = 0;

	SurfaceMID = UMaterialInstanceDynamic::Create(SurfaceMaterial, this);
	SurfaceMID->SetScalarParameterValue(TEXT("TrailRange"), HeightRangeCm);
	SurfaceMID->SetScalarParameterValue(TEXT("TrailDisplace"), DisplacementScale);
	SurfaceMID->SetVectorParameterValue(TEXT("TrailShade"), FLinearColor(1.f / HeightfieldResolution, NormalStrength, DepthDarkening, 0.f));
	SurfaceMID->SetScalarParameterValue(TEXT("TrailLift"), MaterialLiftCm);
	SurfaceMID->SetScalarParameterValue(TEXT("TrailEdge"), MaterialEdgeBlendCm);
	SetTargetParam();

	Mesh = NewObject<UProceduralMeshComponent>(Owner, NAME_None, RF_Transient);
	if (USceneComponent* Root = Owner->GetRootComponent())
	{
		Mesh->SetupAttachment(Root);
	}
	Mesh->SetUsingAbsoluteLocation(true); // the vertices are world positions
	Mesh->SetUsingAbsoluteRotation(true);
	Mesh->SetUsingAbsoluteScale(true);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); // the ground keeps the collision; the patch is only drawn
	Mesh->SetCastShadow(false);
	Mesh->bAffectDistanceFieldLighting = false;
	Mesh->RegisterComponent();
	Mesh->SetWorldTransform(FTransform::Identity);

	SlotTile.Init(NoTile, TilesPerSide * TilesPerSide);
	for (int32 J = 0; J < TilesPerSide; ++J)
	{
		for (int32 I = 0; I < TilesPerSide; ++I)
		{
			BuildTile(OriginTile + FIntPoint(I, J));
		}
	}
	bReady = true;
	if (USandSimulationSubsystem* Subsystem = World->GetSubsystem<USandSimulationSubsystem>())
	{
		Subsystem->RegisterSurface(this);
	}
	UE_LOG(LogSandDeformation, Log, TEXT("Surface %s: patch %.0f cm (%d x %d tiles of %.0f cm) at %s, %d vertices (%.1f cm grid), 2 render targets %dx%d RGBA32F = %.1f MB, %.2f cm per texel, follow %s"),
		*GetNameSafe(Owner), PatchSizeCm, TilesPerSide, TilesPerSide, TileSizeCm, *GetPatchCenter().ToString(), PatchVertices, TileSizeCm / CellsPerTile,
		HeightfieldResolution, HeightfieldResolution, GetMemoryMB(), GetTexelCm(), bCanScroll ? TEXT("on") : TEXT("off"));
	return true;
}

void USandSurfaceComponent::SetTargetParam()
{
	if (!SurfaceMID)
	{
		return;
	}
	const FVector2D Origin = OriginXY();
	SurfaceMID->SetVectorParameterValue(TEXT("TrailArea"), FLinearColor(Origin.X, Origin.Y, PatchSizeCm, 0.f));
	SurfaceMID->SetTextureParameterValue(TEXT("TrailRT"), Targets[Current]);
}

bool USandSurfaceComponent::SampleGround(float X, float Y, float& OutZ) const
{
	if (GroundSampler)
	{
		return GroundSampler(X, Y, OutZ);
	}
	const float BaseZ = GetOwner()->GetActorLocation().Z;
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, FVector(X, Y, BaseZ + GroundSearchCm), FVector(X, Y, BaseZ - GroundSearchCm), GroundTraceChannel))
	{
		OutZ = Hit.ImpactPoint.Z;
		return true;
	}
	return false;
}

void USandSurfaceComponent::BuildTile(FIntPoint Tile)
{
	const int32 C = CellsPerTile;
	const int32 N = C + 1;
	const int32 G = C + 3; // one ring of border heights for the normals
	const float Step = TileSizeCm / C;
	const FVector2D Org = TileCorner(Tile);
	const float FallbackZ = GetOwner()->GetActorLocation().Z;

	// the rim of the patch is blended down to the ground over EdgeBlendCm when the patch floats above it (baked, so only for a patch that stays)
	const FVector2D BakedMin = TileCorner(BakedOriginTile);
	auto Ground = [&](float X, float Y)
	{
		float Z = FallbackZ;
		SampleGround(X, Y, Z);
		return Z;
	};

	TArray<float> Z;
	Z.SetNumUninitialized(G * G);
	// a coarser grid, then bilinear: 16 times fewer traces / sampler calls (line traces by default, a GroundSampler when GroundSampleStride says so)
	const int32 Requested = GroundSampleStride > 0 ? GroundSampleStride : (GroundSampler ? 1 : 4);
	const int32 Stride = (Requested > 1 && C % Requested == 0) ? Requested : 1;
	if (Stride == 1)
	{
		for (int32 J = -1; J <= C + 1; ++J)
		{
			for (int32 I = -1; I <= C + 1; ++I)
			{
				Z[(J + 1) * G + I + 1] = Ground(Org.X + I * Step, Org.Y + J * Step);
			}
		}
	}
	else
	{
		const int32 M = C / Stride + 3;
		TArray<float> Coarse;
		Coarse.SetNumUninitialized(M * M);
		for (int32 V = -1; V <= C / Stride + 1; ++V)
		{
			for (int32 U = -1; U <= C / Stride + 1; ++U)
			{
				Coarse[(V + 1) * M + U + 1] = Ground(Org.X + U * Stride * Step, Org.Y + V * Stride * Step);
			}
		}
		for (int32 J = -1; J <= C + 1; ++J)
		{
			const float Fv = float(J) / Stride;
			const int32 V0 = FMath::FloorToInt(Fv);
			const float Bv = Fv - V0;
			for (int32 I = -1; I <= C + 1; ++I)
			{
				const float Fu = float(I) / Stride;
				const int32 U0 = FMath::FloorToInt(Fu);
				const float Bu = Fu - U0;
				const float Z00 = Coarse[(V0 + 1) * M + U0 + 1], Z10 = Coarse[(V0 + 1) * M + U0 + 2];
				const float Z01 = Coarse[(V0 + 2) * M + U0 + 1], Z11 = Coarse[(V0 + 2) * M + U0 + 2];
				Z[(J + 1) * G + I + 1] = FMath::Lerp(FMath::Lerp(Z00, Z10, Bu), FMath::Lerp(Z01, Z11, Bu), Bv);
			}
		}
	}
	if (GroundOffsetCm != 0.f)
	{
		for (int32 J = -1; J <= C + 1; ++J)
		{
			for (int32 I = -1; I <= C + 1; ++I)
			{
				const float X = Org.X + I * Step - BakedMin.X, Y = Org.Y + J * Step - BakedMin.Y;
				const float Edge = FMath::Min(FMath::Min(X, Y), FMath::Min(PatchSizeCm - X, PatchSizeCm - Y));
				const float T = FMath::Clamp(Edge / FMath::Max(EdgeBlendCm, 1.f), 0.f, 1.f);
				Z[(J + 1) * G + I + 1] += GroundOffsetCm * T * T * (3.f - 2.f * T);
			}
		}
	}

	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	TArray<int32> Triangles;
	Vertices.Reserve(N * N);
	Normals.Reserve(N * N);
	UVs.Reserve(N * N);
	Colors.Reserve(N * N);
	Tangents.Reserve(N * N);
	Triangles.Reserve(C * C * 6);
	for (int32 J = 0; J < N; ++J)
	{
		for (int32 I = 0; I < N; ++I)
		{
			const float X = Org.X + I * Step, Y = Org.Y + J * Step;
			const float Zx = (Z[(J + 1) * G + I + 2] - Z[(J + 1) * G + I]) / (2.f * Step);
			const float Zy = (Z[(J + 2) * G + I + 1] - Z[J * G + I + 1]) / (2.f * Step);
			Vertices.Add(FVector(X, Y, Z[(J + 1) * G + I + 1]));
			Normals.Add(FVector(-Zx, -Zy, 1.f).GetSafeNormal());
			Tangents.Add(FProcMeshTangent(FVector(1.f, 0.f, Zx).GetSafeNormal(), false));
			UVs.Add(FVector2D(X, Y) / UVScaleCm);
			Colors.Add(FLinearColor::White);
		}
	}
	for (int32 J = 0; J < C; ++J)
	{
		for (int32 I = 0; I < C; ++I)
		{
			const int32 A = J * N + I, B = A + 1, Cc = A + N, D = Cc + 1;
			Triangles.Append({ A, Cc, B, B, Cc, D }); // the engine takes the normal as (P1-P2) x (P0-P2): this order faces up
		}
	}
	const int32 Slot = SlotOf(Tile);
	Mesh->CreateMeshSection_LinearColor(Slot, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
	Mesh->SetMaterial(Slot, SurfaceMID);
	PatchVertices = Vertices.Num() * TilesPerSide * TilesPerSide;
	++TilesBuilt;
	const FIntPoint Old = SlotTile[Slot];
	SlotTile[Slot] = Tile;
	OnTileBuilt.Broadcast(Old, Tile);
}

void USandSurfaceComponent::RefreshPendingTiles()
{
	PendingTiles.Reset();
	for (int32 J = 0; J < TilesPerSide; ++J)
	{
		for (int32 I = 0; I < TilesPerSide; ++I)
		{
			const FIntPoint Tile = OriginTile + FIntPoint(I, J);
			if (SlotTile[SlotOf(Tile)] != Tile)
			{
				PendingTiles.Add(Tile);
			}
		}
	}
}

void USandSurfaceComponent::ScrollTo(FIntPoint NewOrigin)
{
	if (!bReady || !bCanScroll || NewOrigin == OriginTile)
	{
		return;
	}
	FlushStamps(); // the stamps queued so far belong to the old origin
	const FIntPoint Delta = NewOrigin - OriginTile;
	const int32 Next = 1 - Current;
	// a world point moves by -Delta tiles inside the texture: the new texel p reads the old texel p + Delta x tile
	SandSim::DispatchScroll(Targets[Current], Targets[Next], Delta * TileTexels);
	Current = Next;
	const FIntPoint Old = OriginTile;
	OriginTile = NewOrigin;
	SetTargetParam();
	RefreshPendingTiles();
	++ScrollCount;
	OnPatchScrolled.Broadcast(Old, NewOrigin);
}

void USandSurfaceComponent::ScrollToward(const FVector2D& World)
{
	const FVector2D Min = World - FVector2D(PatchSizeCm * 0.5f) - LatticeOffset;
	ScrollTo(FIntPoint(FMath::RoundToInt(Min.X / TileSizeCm), FMath::RoundToInt(Min.Y / TileSizeCm)));
}

void USandSurfaceComponent::SetFollowTarget(AActor* Target)
{
	FollowTarget = Target;
}

void USandSurfaceComponent::UpdateFollow()
{
	if (!bCanScroll || !bFollowTarget || PendingTiles.Num() > 0)
	{
		return; // wait until the tiles of the last scroll are built
	}
	AActor* Target = FollowTarget.Get();
	if (!Target && bAutoFollowLocalPlayer)
	{
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		Target = PC ? PC->GetPawn() : nullptr;
	}
	if (!Target)
	{
		return;
	}
	const FVector Location = Target->GetActorLocation();
	const FVector2D Centre = GetPatchCenter();
	if (FMath::Max(FMath::Abs(Location.X - Centre.X), FMath::Abs(Location.Y - Centre.Y)) > ScrollThresholdCm)
	{
		ScrollToward(FVector2D(Location.X, Location.Y));
	}
}

void USandSurfaceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bReady)
	{
		return;
	}
	FlushStamps(); // the stamps of the last tick, before this tick's scroll and simulation step
	UpdateFollow();
	for (int32 K = 0; K < TilesPerFrame && PendingTiles.Num() > 0; ++K)
	{
		BuildTile(PendingTiles.Pop());
	}
	StepSimulation(DeltaTime);
}

bool USandSurfaceComponent::QueueStamp(const FSandStampRequest& Request)
{
	if (!bReady || Request.World.ContainsNaN() || !FMath::IsFinite(Request.DepthCm) || !FMath::IsFinite(Request.RidgeCm))
	{
		return false;
	}
	if (!IsInsidePatch(FVector(Request.World.X, Request.World.Y, 0.f), Request.HalfWidthCm))
	{
		++OutsideRejected; // policy: nothing outside the active patch is stored
		return false;
	}
	if (Pending.Num() >= MaxPendingStamps)
	{
		++DroppedStamps;
		return false;
	}
	FSandStampRequest& R = Pending.Add_GetRef(Request);
	R.Seed = (++StampSeed % 97) * 0.37f;
	++StampsQueued;
	SimActive = bRecoveryEnabled ? RecoverySeconds * 1.05f + 2.f : 1e9f; // the simulation only runs while something is left to settle
	return true;
}

void USandSurfaceComponent::FlushStamps()
{
	if (Pending.Num() == 0)
	{
		return;
	}
	if (!bReady)
	{
		Pending.Reset();
		return;
	}
	const float Cell = GetTexelCm();
	const FVector2D Origin = OriginXY();
	for (int32 Start = 0; Start < Pending.Num(); Start += SandMaxStamps)
	{
		TArray<FSandStamp> Batch;
		for (int32 i = Start; i < FMath::Min(Start + SandMaxStamps, Pending.Num()); ++i)
		{
			const FSandStampRequest& R = Pending[i];
			const FVector2D T = (R.World - Origin) / Cell;
			const FVector2D D = R.Dir.GetSafeNormal();
			FSandStamp S;
			S.A = FVector4f(T.X, T.Y, D.X, D.Y);
			S.B = FVector4f(R.HalfLenCm / Cell, R.HalfWidthCm / Cell, R.DepthCm, R.RidgeCm);
			S.C = FVector4f(R.Shape >= 0 ? float(R.Shape) : (R.bRound ? 1.f : 0.f), R.Seed, (R.Flags & 2) ? R.MaxDepthCm : R.Push, float(R.Flags)); // Push is only read by feet, which never accumulate
			Batch.Add(S);
		}
		const int32 Next = 1 - Current;
		SandSim::DispatchStamps(Targets[Current], Targets[Next], Batch, HeightRangeCm, Cell);
		Current = Next;
	}
	SurfaceMID->SetTextureParameterValue(TEXT("TrailRT"), Targets[Current]);
	Pending.Reset();
}

bool USandSurfaceComponent::ApplyDeformation(FVector WorldPosition, float Radius, float Strength, ESandBrush Brush, float DirectionYawDeg, float RidgeScale)
{
	if (!bReady)
	{
		return false;
	}
	if (WorldPosition.ContainsNaN() || !FMath::IsFinite(Radius) || !FMath::IsFinite(Strength) || !FMath::IsFinite(DirectionYawDeg) || !FMath::IsFinite(RidgeScale) || Strength <= 0.f)
	{
		return false;
	}
	const bool bRound = Brush == ESandBrush::Round;
	FSandInteractionRequest Request;
	Request.WorldPosition = WorldPosition;
	Request.OrientationYawDeg = DirectionYawDeg;
	Request.Radius = Radius;
	Request.Strength = Strength;
	Request.RidgeScale = RidgeScale;
	Request.Brush = bRound ? ESandBrushType::Impact : ESandBrushType::Footprint;
	Request.Shape = bRound ? ESandContactShape::Round : ESandContactShape::Foot;
	Request.Extent = bRound ? FVector2D::ZeroVector : FVector2D(Radius * 2.4f, Radius);
	return ApplyInteraction(Request);
}

bool USandSurfaceComponent::ApplyInteraction(const FSandInteractionRequest& Request)
{
	if (!bReady)
	{
		return false;
	}
	const bool bFinite = !Request.WorldPosition.ContainsNaN() && !Request.MovementDirection.ContainsNaN() && !Request.PreviousPosition.ContainsNaN() && !Request.Extent.ContainsNaN()
		&& FMath::IsFinite(Request.Radius) && FMath::IsFinite(Request.Strength) && FMath::IsFinite(Request.Pressure) && FMath::IsFinite(Request.ImpactSpeed)
		&& FMath::IsFinite(Request.DepthOverrideCm) && FMath::IsFinite(Request.RidgeScale) && FMath::IsFinite(Request.OrientationYawDeg) && FMath::IsFinite(Request.Push);
	if (!bFinite || (Request.DepthOverrideCm <= 0.f && (Request.Strength <= 0.f || Request.Pressure <= 0.f)))
	{
		++InvalidRejected;
		return false;
	}

	// the outline: Auto follows the brush type
	ESandContactShape Shape = Request.Shape;
	const bool bHasExtent = Request.Extent.X > 0.f && Request.Extent.Y > 0.f;
	if (Shape == ESandContactShape::Auto)
	{
		switch (Request.Brush)
		{
		case ESandBrushType::Footprint: Shape = ESandContactShape::Foot; break;
		case ESandBrushType::Wheel: Shape = ESandContactShape::Segment; break;
		case ESandBrushType::ObjectContact: Shape = bHasExtent ? ESandContactShape::Box : ESandContactShape::Round; break;
		case ESandBrushType::Impact: Shape = bHasExtent ? ESandContactShape::Box : ESandContactShape::Round; break;
		default: Shape = ESandContactShape::Round; break;
		}
	}
	const float Texel = GetTexelCm();
	const float Width = FMath::Clamp(Request.Radius, FMath::Max(2.f, 1.5f * Texel), PatchSizeCm * 0.25f);
	FVector2D Dir(Request.MovementDirection.X, Request.MovementDirection.Y);
	if (Dir.SquaredLength() < 1e-6)
	{
		Dir = FVector2D(FMath::Cos(FMath::DegreesToRadians(Request.OrientationYawDeg)), FMath::Sin(FMath::DegreesToRadians(Request.OrientationYawDeg)));
	}
	Dir = Dir.GetSafeNormal();

	float Depth = Request.DepthOverrideCm;
	if (Depth <= 0.f)
	{
		Depth = Request.Strength * Request.Pressure * DepthPerStrengthCm;
		if (Request.ImpactSpeed > 0.f)
		{
			Depth *= FMath::Sqrt(Request.ImpactSpeed / FMath::Max(ImpactReferenceSpeed, 1.f));
		}
	}
	Depth = FMath::Min(Depth, GetDepthLimit());

	FSandStampRequest R;
	R.Flags = Request.bAccumulate ? 2 : 0;
	R.MaxDepthCm = GetDepthLimit();
	R.World = FVector2D(Request.WorldPosition.X, Request.WorldPosition.Y);
	R.Dir = Dir;
	R.Push = Request.Push;
	R.HalfWidthCm = Width;
	R.HalfLenCm = Width;
	float RidgeRatio = RoundRidgeRatio;
	float RidgeCap = MaxRidgeCm * 1.6f;
	if (Shape == ESandContactShape::Sphere)
	{
		// the cap of a sphere of radius Radius pressed Depth deep is a circle of half width sqrt(2 r d - d^2); with a previous position it is a groove with a round bottom
		R.Flags |= 1;
		const float SphereRadius = FMath::Max(Request.Radius, 1.f);
		const float Cap = FMath::Sqrt(FMath::Max(2.f * SphereRadius * Depth - Depth * Depth, 0.f));
		R.HalfWidthCm = R.HalfLenCm = FMath::Clamp(Cap, FMath::Max(2.f, 1.5f * Texel), PatchSizeCm * 0.25f);
		Shape = Request.bHasPreviousPosition ? ESandContactShape::Segment : ESandContactShape::Round;
	}
	switch (Shape)
	{
	case ESandContactShape::Foot:
		R.Shape = 0;
		R.Flags &= ~2; // a foot print is never additive (its slot for the max depth carries Push)
		R.HalfLenCm = bHasExtent || Request.Extent.X > 0.f ? FMath::Max(Request.Extent.X, Texel) : Width * 2.4f;
		R.HalfWidthCm = Request.Extent.Y > 0.f ? FMath::Max(Request.Extent.Y, Texel) : Width;
		RidgeRatio = FootRidgeRatio;
		RidgeCap = MaxRidgeCm;
		break;
	case ESandContactShape::Segment:
	{
		R.Shape = 2;
		RidgeRatio = ContactRidgeRatio;
		RidgeCap = MaxRidgeCm;
		R.HalfLenCm = 0.f; // a dab until there is a previous position
		if (Request.bHasPreviousPosition)
		{
			const FVector2D From(Request.PreviousPosition.X, Request.PreviousPosition.Y);
			const FVector2D Delta = R.World - From;
			const float Length = Delta.Size();
			if (Length > 0.25f)
			{
				R.World = (From + R.World) * 0.5f;
				R.Dir = Delta / Length;
				R.HalfLenCm = Length * 0.5f;
			}
		}
		break;
	}
	case ESandContactShape::Box:
		R.Shape = 3;
		R.HalfLenCm = Request.Extent.X > 0.f ? FMath::Max(Request.Extent.X, Texel) : Width;
		R.HalfWidthCm = Request.Extent.Y > 0.f ? FMath::Max(Request.Extent.Y, Texel) : Width;
		RidgeRatio = ContactRidgeRatio;
		RidgeCap = MaxRidgeCm * 1.3f;
		break;
	default:
		R.Shape = 1;
		R.bRound = true;
		break;
	}

	R.DepthCm = Depth;
	R.RidgeCm = FMath::Min(RidgeRatio * R.DepthCm * FMath::Max(Request.RidgeScale, 0.f), RidgeCap);
	if (!QueueStamp(R))
	{
		return false;
	}
	++RequestsByBrush[FMath::Clamp(static_cast<int32>(Request.Brush), 0, 4)];
	return true;
}

void USandSurfaceComponent::SetWind(FVector2D Direction, float Strength)
{
	if (!Direction.ContainsNaN() && FMath::IsFinite(Strength))
	{
		WindDirection = Direction;
		WindStrength = FMath::Clamp(Strength, 0.f, 1.f);
	}
}

void USandSurfaceComponent::ResetHeightfield()
{
	for (UTextureRenderTarget2D* Target : Targets)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, Target, Untouched);
	}
	SimActive = 0.f;
	SimAccum = 0.f;
	Pending.Reset();
}

void USandSurfaceComponent::StepSimulation(float DeltaSeconds)
{
	if (!bSimulationEnabled || bSimPaused || SimActive <= 0.f)
	{
		SimAccum = 0.f;
		return;
	}
	SimActive -= DeltaSeconds;
	SimAccum += DeltaSeconds;
	const float Cell = GetTexelCm();
	for (int32 Steps = 0; SimAccum >= SimInterval && Steps < MaxSubSteps; ++Steps)
	{
		SimAccum -= SimInterval;
		FSandSimStep Step;
		Step.RangeCm = HeightRangeCm;
		Step.CellCm = Cell;
		const float FadeCmPerSec = RecoveryReferenceCm / FMath::Max(RecoverySeconds, 1.f) * (1.f + WindRecoveryMultiplier * WindStrength);
		Step.RecoverStepCm = bRecoveryEnabled ? FadeCmPerSec * SimInterval : 0.f;
		FVector2D Wind = WindDirection.GetSafeNormal() * (WindStrength * WindTransportStrength * SimInterval / Cell); // texels per step; the patch is axis aligned so world X/Y are texel x/y
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
		SandSim::Dispatch(Targets[Current], Targets[Next], Step);
		Current = Next;
		SurfaceMID->SetTextureParameterValue(TEXT("TrailRT"), Targets[Current]);
		++SimSteps;
	}
	if (SimAccum >= SimInterval)
	{
		SimAccum = 0.f; // a long frame: drop the rest instead of catching up
	}
}

float USandSurfaceComponent::SampleHeightCm(FVector WorldPosition) const
{
	if (!bReady)
	{
		return 0.f;
	}
	const FVector2D UV = WorldToUV(FVector2D(WorldPosition.X, WorldPosition.Y));
	const int32 X = FMath::Clamp(FMath::FloorToInt(UV.X * HeightfieldResolution), 0, HeightfieldResolution - 1);
	const int32 Y = FMath::Clamp(FMath::FloorToInt(UV.Y * HeightfieldResolution), 0, HeightfieldResolution - 1);
	const FLinearColor Pixel = UKismetRenderingLibrary::ReadRenderTargetRawPixel(const_cast<USandSurfaceComponent*>(this), Targets[Current], X, Y, false);
	return (Pixel.R - 0.5f) * HeightRangeCm;
}

FSandHeightStats USandSurfaceComponent::ReadStats() const
{
	FSandHeightStats Stats;
	if (!bReady)
	{
		return Stats;
	}
	const int32 Size = HeightfieldResolution;
	const TArray<FLinearColor> Pixels = UKismetRenderingLibrary::ReadRenderTargetRawPixelArea(const_cast<USandSurfaceComponent*>(this), Targets[Current], 0, 0, Size, Size, false);
	if (Pixels.Num() < Size * Size)
	{
		UE_LOG(LogSandDeformation, Warning, TEXT("read back %d pixels, expected %d"), Pixels.Num(), Size * Size);
		return Stats;
	}
	const FVector2D Origin = OriginXY();
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

bool USandSurfaceComponent::SaveHeightfieldPNG(const FString& Path) const
{
	if (!bReady)
	{
		return false;
	}
	const int32 Size = HeightfieldResolution;
	const TArray<FLinearColor> Pixels = UKismetRenderingLibrary::ReadRenderTargetRawPixelArea(const_cast<USandSurfaceComponent*>(this), Targets[Current], 0, 0, Size, Size, false);
	if (Pixels.Num() < Size * Size)
	{
		return false;
	}
	TArray<FColor> Image;
	Image.SetNumUninitialized(Size * Size);
	for (int32 i = 0; i < Size * Size; ++i)
	{
		const float H = (Pixels[i].R - 0.5f) * HeightRangeCm;
		const float Down = FMath::Clamp(-H / 6.f, 0.f, 1.f);
		const float Up = FMath::Clamp(H / 3.f, 0.f, 1.f);
		Image[i] = FLinearColor(0.35f + 0.65f * Up - 0.30f * Down, 0.35f - 0.15f * Down - 0.20f * Up, 0.35f + 0.65f * Down - 0.30f * Up).ToFColor(true);
	}
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
	if (!Wrapper.IsValid() || !Wrapper->SetRaw(Image.GetData(), Image.Num() * sizeof(FColor), Size, Size, ERGBFormat::BGRA, 8))
	{
		return false;
	}
	return FFileHelper::SaveArrayToFile(Wrapper->GetCompressed(), *Path);
}

