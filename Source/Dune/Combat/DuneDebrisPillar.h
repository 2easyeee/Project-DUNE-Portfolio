// Project DUNE (full game): a sandstone pillar the pistol can break. A bullet fractures it into physics pieces that the player can throw.

#pragma once

#include "CoreMinimal.h"
#include "Dune/SliceableStructure.h"
#include "DuneDebrisPillar.generated.h"

UCLASS()
class UE_HOLDITTOGETHER_API ADuneDebrisPillar : public ASliceableStructure
{
	GENERATED_BODY()

public:
	ADuneDebrisPillar();

	/**
	 * The pillar is a copy of the DeepElderCaves pillar SM_Pillar_2x3_B (CPU access for the cutting, its own material). The mesh is 389 cm tall and its pivot
	 * lies 35 cm above its foot; PillarScale() makes it about 160 cm wide and 430 cm tall.
	 */
	static FVector PillarScale() { return FVector(1.1f); }
	static float HeightCm() { return 389.f * 1.1f; }
	/** Distance from the foot of the pillar up to the actor location (its pivot). */
	static float PivotAboveFootCm() { return 35.4f * 1.1f; }
};
