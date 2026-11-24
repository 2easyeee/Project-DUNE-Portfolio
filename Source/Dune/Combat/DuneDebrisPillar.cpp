#include "Dune/Combat/DuneDebrisPillar.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ADuneDebrisPillar::ADuneDebrisPillar()
{
	// a copy (CPU access for the cutting) of the DeepElderCaves pillar; the original stays untouched and the copy keeps its own material
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Pillar(TEXT("/Game/DUNE/Meshes/Sliceable/SM_PillarArena_Sliceable.SM_PillarArena_Sliceable"));
	if (Pillar.Succeeded()) SourceMesh = Pillar.Object;
	bUseSourceMaterials = true;
	bGunFracturable = true;
	MaxPieces = 64;
	Tint = FLinearColor(0.78f, 0.62f, 0.38f);
	CapTint = FLinearColor(0.78f, 0.62f, 0.38f); // the broken faces show sandstone
	DensityKgPerM3 = 1800.f; // sandstone
}
