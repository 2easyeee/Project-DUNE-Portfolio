// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SandSelfTest.generated.h"

class ACameraActor;
class ASandDemoActor;
class ASandSurfaceActor;
class USandInteractionComponent;

/**
 * Self test of the plugin, started by launching the game with -SandDeformationSelfTest (a rendered run: it needs compute shaders). It needs no
 * game code: it builds a floor, a sand surface and a plain actor ("Walker", a sphere) high above the origin and checks, with log lines
 * "SandSelfTest: PASS/FAIL ...": the material is wired to the heightfield, deformation requests from the library and from a component on
 * a plain actor reach the heightfield, invalid requests are refused, the patch follows the actor (data kept in the overlap, new strip untouched,
 * nothing shown at a wrong position) and many requests leave no invalid values. Takes two screenshots into Saved/SandDeformationSelfTest/.
 * -SandDeformationSelfTestQuit exits when it is done.
 */
UCLASS(Transient)
class ASandSelfTest : public AActor
{
	GENERATED_BODY()

public:
	ASandSelfTest();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void Check(bool bOk, const FString& What);
	void TickLevelMode(float DeltaSeconds);
	void TickHopMode(float DeltaSeconds);
	void Shot(const TCHAR* Name, const FVector& From, const FVector& At);
	void Finish();

	UPROPERTY()
	TObjectPtr<ASandSurfaceActor> SurfaceActor;

	UPROPERTY()
	TObjectPtr<AActor> Walker;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	UPROPERTY()
	TObjectPtr<ASandDemoActor> Box;

	UPROPERTY()
	TObjectPtr<ASandDemoActor> Vehicle;

	UPROPERTY()
	TObjectPtr<ASandDemoActor> Lander;

	/** -SandDeformationLevelTest: use the demo actors and the sand surface that are already in the level (nothing is spawned). */
	bool bLevelMode = false;

	/** -SandDeformationLevelHop=/Game/A,/Game/B,...: in every level wait for the surface, press a dent, read it back, then open the next level (the list starts with the level the game was launched on). */
	bool bHopMode = false;
	TArray<FString> HopLevels;
	int32 HopIndex = 0;

	UPROPERTY()
	TObjectPtr<class ASandDemoCharacter> Golem;

	UPROPERTY()
	TObjectPtr<class USandSurfaceComponent> LevelSurface;

	int32 MarkStamps = 0;
	int32 PerfFrames = 0;
	float PerfTime = 0.f;
	float IdleMs = 0.f;

	UPROPERTY()
	TObjectPtr<USandInteractionComponent> Interaction;

	FVector Centre = FVector(100000.0, 100000.0, 20000.0);
	int32 Step = 0;
	int32 Frames = 0;
	int32 StepFrames = 0;
	int32 Passed = 0;
	int32 Failed = 0;
	float TimeSum = 0.f;
	float TimeMax = 0.f;
	float DentD2 = 0.f;
	int32 TilesBefore = 0;
	FIntPoint OriginBefore = FIntPoint::ZeroValue;
};
