// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dune/DuneSandTerrain.h"
#include "Materials/MaterialInterface.h"
#include "NavigationSystem.h"
#include "ProceduralMeshComponent.h"
#include "UE_HOLDITTOGETHER.h"

namespace
{
	float Smooth(float X)
	{
		X = FMath::Clamp(X, 0.f, 1.f);
		return X * X * (3.f - 2.f * X);
	}
}

ADuneSandTerrain::ADuneSandTerrain()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Tags.Add(TEXT("DuneSandTerrain"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Sand(TEXT("/Game/DUNE/Materials/M_DUNE_Sand.M_DUNE_Sand"));
	SandMaterial = Sand.Object;
}

float ADuneSandTerrain::HeightAt(float X, float Y) const
{
	const float Yaw = FMath::DegreesToRadians(WindYawDeg);
	const float S = X * FMath::Cos(Yaw) + Y * FMath::Sin(Yaw); // along the wind
	const float T = -X * FMath::Sin(Yaw) + Y * FMath::Cos(Yaw); // across it, along the crests
	// the phase falls along the wind, so the height rises gently on the windward side and drops steeply behind the crest (the lee side)
	auto Wave = [&](float Length, float Phase)
	{
		const float Meander = 0.9f * FMath::Sin(2.f * PI * T / (Length * 1.1f) + 1.3f) + 0.35f * FMath::Sin(2.f * PI * T / (Length * 0.37f) + 0.4f); // crests are not straight
		const float P = -2.f * PI * S / Length + Phase + Meander;
		return 0.5f * (1.f + FMath::Sin(P + Asymmetry * FMath::Sin(P)));
	};
	const float Variety = 0.75f + 0.25f * FMath::Sin(2.f * PI * T / 2100.f + 0.8f); // dunes of different size
	const float SmallMask = Smooth((X - FlatEndX) / FMath::Max(RiseCm, 1.f));
	const float R = FMath::Sqrt(X * X + Y * Y);
	const float BigMask = Smooth((R - 1500.f) / 1500.f);
	const float Edge = Smooth((FMath::Max(FMath::Abs(X), FMath::Abs(Y)) - HalfSizeCm * 0.62f) / (HalfSizeCm * 0.38f));
	return EdgeRiseCm * Edge * Edge + SmallHeightCm * Variety * Wave(SmallWavelengthCm, 0.6f) * SmallMask + BigHeightCm * Wave(BigWavelengthCm, 2.1f) * BigMask;
}

FVector ADuneSandTerrain::NormalAt(float X, float Y) const
{
	const float E = 5.f;
	const float Dx = (HeightAt(X + E, Y) - HeightAt(X - E, Y)) / (2.f * E);
	const float Dy = (HeightAt(X, Y + E) - HeightAt(X, Y - E)) / (2.f * E);
	return FVector(-Dx, -Dy, 1.f).GetSafeNormal();
}

void ADuneSandTerrain::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Build();
}

void ADuneSandTerrain::BeginPlay()
{
	if (!Collision || !Visual)
	{
		Build();
	}
	Super::BeginPlay();
}

void ADuneSandTerrain::OnSandTileBuilt(FIntPoint OldTile, FIntPoint NewTile)
{
	// the sand patch draws NewTile now (the terrain stops drawing it) and no longer draws OldTile (the terrain draws it again)
	auto Set = [this](FIntPoint Tile, bool bVisible)
	{
		const int32 X = Tile.X - FMath::RoundToInt(-HalfSizeCm / TileCm);
		const int32 Y = Tile.Y - FMath::RoundToInt(-HalfSizeCm / TileCm);
		const int32 Count = FMath::RoundToInt(2.f * HalfSizeCm / TileCm);
		if (Visual && X >= 0 && Y >= 0 && X < Count && Y < Count)
		{
			Visual->SetMeshSectionVisible(Y * Count + X, bVisible);
		}
	};
	Set(OldTile, true);
	Set(NewTile, false);
}

void ADuneSandTerrain::Build()
{
	for (UProceduralMeshComponent* Old : { Collision.Get(), Visual.Get() })
	{
		if (Old)
		{
			Old->DestroyComponent();
		}
	}
	const int32 Cells = FMath::Max(2, FMath::RoundToInt(2.f * HalfSizeCm / CellCm));
	const float Step = 2.f * HalfSizeCm / Cells;
	const int32 N = Cells + 1;
	const FVector Origin = GetActorLocation();

	// collision: one invisible mesh over the whole terrain
	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	TArray<int32> Triangles;
	Vertices.Reserve(N * N);
	for (int32 J = 0; J < N; ++J)
	{
		for (int32 I = 0; I < N; ++I)
		{
			const float X = Origin.X - HalfSizeCm + I * Step;
			const float Y = Origin.Y - HalfSizeCm + J * Step;
			Vertices.Add(FVector(X - Origin.X, Y - Origin.Y, HeightAt(X, Y)));
			Normals.Add(NormalAt(X, Y));
			Tangents.Add(FProcMeshTangent(FVector(1.f, 0.f, 0.f), false));
			UVs.Add(FVector2D(X, Y) / 400.f);
			Colors.Add(FLinearColor::White);
		}
	}
	Triangles.Reserve(Cells * Cells * 6);
	for (int32 J = 0; J < Cells; ++J)
	{
		for (int32 I = 0; I < Cells; ++I)
		{
			const int32 A = J * N + I, B = A + 1, C = A + N, D = C + 1;
			Triangles.Append({ A, C, B, B, C, D }); // this order faces up
		}
	}

	auto Make = [this](const TCHAR* Name) -> UProceduralMeshComponent*
	{
		UProceduralMeshComponent* Comp = NewObject<UProceduralMeshComponent>(this, Name, RF_Transient);
		Comp->SetupAttachment(GetRootComponent());
		Comp->RegisterComponent();
		return Comp;
	};
	Collision = Make(TEXT("TerrainCollision"));
	Collision->bUseComplexAsSimpleCollision = true;
	Collision->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, true);
	Collision->SetVisibility(false);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Collision->SetCollisionResponseToAllChannels(ECR_Block);
	// the component was registered before it had a mesh: the navigation system ignored it ("Empty bounds"), so the sand floor had no navmesh. Tell it again now.
	Collision->UpdateBounds();
	FNavigationSystem::UpdateComponentData(*Collision);
	VertexCount = Vertices.Num();

	// visual: one section per tile of the sand patch grid, so the sand patch can take a tile over (hide it) and give it back (show it) as it moves
	Visual = Make(TEXT("TerrainVisual"));
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	const int32 Tiles = FMath::RoundToInt(2.f * HalfSizeCm / TileCm);
	const int32 TileCells = FMath::Max(1, FMath::RoundToInt(TileCm / CellCm));
	const float TileStep = TileCm / TileCells;
	for (int32 TY = 0; TY < Tiles; ++TY)
	{
		for (int32 TX = 0; TX < Tiles; ++TX)
		{
			TArray<FVector> TV;
			TArray<FVector> TN;
			TArray<FVector2D> TUV;
			TArray<FLinearColor> TC;
			TArray<FProcMeshTangent> TT;
			TArray<int32> TI;
			const float X0 = Origin.X - HalfSizeCm + TX * TileCm;
			const float Y0 = Origin.Y - HalfSizeCm + TY * TileCm;
			for (int32 J = 0; J <= TileCells; ++J)
			{
				for (int32 I = 0; I <= TileCells; ++I)
				{
					const float X = X0 + I * TileStep, Y = Y0 + J * TileStep;
					TV.Add(FVector(X - Origin.X, Y - Origin.Y, HeightAt(X, Y)));
					TN.Add(NormalAt(X, Y));
					TT.Add(FProcMeshTangent(FVector(1.f, 0.f, 0.f), false));
					TUV.Add(FVector2D(X, Y) / 400.f);
					TC.Add(FLinearColor::White);
				}
			}
			for (int32 J = 0; J < TileCells; ++J)
			{
				for (int32 I = 0; I < TileCells; ++I)
				{
					const int32 A = J * (TileCells + 1) + I, B = A + 1, C = A + TileCells + 1, D = C + 1;
					TI.Append({ A, C, B, B, C, D });
				}
			}
			Visual->CreateMeshSection_LinearColor(TY * Tiles + TX, TV, TI, TN, TUV, TC, TT, false);
			Visual->SetMaterial(TY * Tiles + TX, SandMaterial);
		}
	}
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("SandTerrain: collision %d vertices (%.0f cm grid), %.0f x %.0f cm, %d visual tiles of %.0f cm, heights %.0f .. %.0f cm"),
		VertexCount, Step, 2.f * HalfSizeCm, 2.f * HalfSizeCm, Tiles * Tiles, TileCm, HeightAt(-2000.f, 0.f), HeightAt(300.f, 100.f));
}
