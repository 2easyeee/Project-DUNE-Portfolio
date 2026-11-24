// Project DUNE (full game): the arena turns to sand. When the arena is cleared every mesh actor (walls, ceilings, cover, platforms, pillars, crates) except the
// ones kept is eroded from the top down (the actor shrinks onto its foot while grains of sand blow off its top) and then removed. The wave starts at the
// player and spreads outwards. One instanced mesh holds the grains (a pool, like the boss' sand).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DuneSandDissolve.generated.h"

class UInstancedStaticMeshComponent;

UCLASS()
class UE_HOLDITTOGETHER_API ADuneSandDissolve : public AActor
{
	GENERATED_BODY()

public:
	ADuneSandDissolve();

	/** Collects the mesh actors of the world (not those in Keep, the player's, the floor) and starts eroding them. Returns the dissolve actor. */
	static ADuneSandDissolve* Start(UWorld* World, const FVector& Origin, const TArray<AActor*>& Keep);

	/** True when every target is gone (the grains may still be settling). */
	bool IsFinished() const { return bFinished; }
	int32 GetTargetCount() const { return Targets.Num(); }
	UPROPERTY(EditAnywhere, Category="Dissolve") float ErodeSeconds = 2.2f;
	/** The wave reaches an actor this many seconds per 1000 cm of distance from the player. */
	UPROPERTY(EditAnywhere, Category="Dissolve") float SpreadSecondsPer1000Cm = 0.35f;
	UPROPERTY(EditAnywhere, Category="Dissolve") float MaxDelaySeconds = 2.4f;
	UPROPERTY(EditAnywhere, Category="Dissolve") float GrainBudgetPerSecond = 1800.f;

protected:
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FTarget
	{
		TWeakObjectPtr<AActor> Actor;
		FVector BaseLoc = FVector::ZeroVector;   // location at the start
		FVector BaseScale = FVector::OneVector;
		FBox Box = FBox(ForceInit);
		float Delay = 0.f;
		bool bStarted = false;
		bool bDone = false;
	};
	struct FGrain { FVector Pos = FVector::ZeroVector; FVector Vel = FVector::ZeroVector; float Life = 0.f, MaxLife = 1.f, Size = 1.f; };

	void Emit(const FTarget& T, float TopZ, int32 Count);

	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> GrainMesh;
	TArray<FTarget> Targets;
	TArray<FGrain> Grains;
	TArray<FTransform> GrainTransforms;
	int32 Cursor = 0;
	float Time = 0.f;
	float SettleTime = 0.f;
	bool bFinished = false;
	FVector Wind = FVector(900.f, 300.f, 0.f);
};
