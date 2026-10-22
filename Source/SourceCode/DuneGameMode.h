// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HoldItGameMode.h"
#include "DuneGameMode.generated.h"

/**
 * Project DUNE levels: the existing HoldIt player (character, input, camera) without the Day 4 course.
 * With -DuneSliceTest on the command line it runs a short headless self-test (player moves, jumps, slice requests)
 * and logs the results, then exits.
 */
UCLASS()
class ADuneGameMode : public AHoldItGameMode
{
	GENERATED_BODY()

public:
	ADuneGameMode();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	/** Gives the player the kris knife (the character class itself is not changed). */
	virtual void FinishRestartPlayer(AController* NewPlayer, const FRotator& StartRotation) override;

private:
	void RunSliceRequests();
	void RunKnifeTest(float DeltaSeconds, class ACharacter* Player);

	/** -DuneShot: waits for the shaders, takes screenshots from a few views, exits. For checking the look without an editor. */
	void RunShots(float DeltaSeconds, class ACharacter* Player);
	bool bShots = false;
	float ShotTime = 0.f;
	int32 ShotStep = 0;

	void RunFractureTest(float DeltaSeconds, class ACharacter* Player);
	bool bFractureTest = false;
	float FracTime = 0.f;
	float FracStepTime = 0.f;
	int32 FracStep = 0;
	int32 FracAux = 0;
	int32 FracBase = 0;
	FVector FracCentre = FVector::ZeroVector;
	bool bFracCentre = false;

	/** The player's trail in the sand (spawned at the start of the level; -NoDuneSand leaves it out). */
	UPROPERTY()
	TObjectPtr<class ADuneSandTrail> SandTrail;

	/** -DuneSandTest (with -DuneSandShot for screenshots): walks, stands, jumps and steps on a rock, reads the trail texture back and logs. Needs a rendered run. */
	void RunSandTest(float DeltaSeconds, class ACharacter* Player);
	bool bSandTest = false;

	/** -DuneSandSimTest (-DuneSandSimShot for screenshots, -DuneSandSoak=<s>): Day 17 sand simulation self-test (walk / run, landings, fade, wind, slope collapse, soak, frame times). Needs a rendered run. */
	void RunSandSimTest(float DeltaSeconds, class ACharacter* Player);
	bool bSandSimTest = false;
	int32 SSStep = 0;
	int32 SSAux = 0;
	int32 SSMark = 0;
	bool bSSFresh = true;
	float SSTime = 0.f;
	float SSStepTime = 0.f;
	float SSVal[8] = {};
	double SSSum = 0.0;
	double SSFrameMs = 0.0, SSGpuMs = 0.0;
	int32 SSFrames = 0;
	FString SSShotName;
	FVector SSShotFrom = FVector::ZeroVector, SSShotAt = FVector::ZeroVector;
	int32 SSShotAfter = 0;
	float SSShotTime = 0.f;

	/** -DuneCityShot=<file>: one free camera view per line ("name x y z pitch yaw fov"), a screenshot of each, a frame time average, then exit. For the level art. */
	void RunCityShots(float DeltaSeconds, class ACharacter* Player);
	bool bCityShots = false;

	/** -DuneCityWalk: the player walks a route through the city (entrance, sand, street, plaza, lane, back terraces); logs blocked places, falls and frame times. */
	void RunCityWalk(float DeltaSeconds, class ACharacter* Player);
	bool bCityWalk = false;
	int32 WalkIdx = 0;
	float WalkTime = 0.f;
	float WalkSegTime = 0.f;
	float WalkStuckTime = 0.f;
	FVector WalkLast = FVector::ZeroVector;
	double WalkMsSeg = 0.0, WalkMsAll = 0.0, WalkMsMax = 0.0;
	int32 WalkFramesSeg = 0, WalkFramesAll = 0, WalkBlocked = 0;
	float CityTime = 0.f;
	float CityStepTime = 0.f;
	int32 CityStep = 0;
	double CityFrameMs = 0.0;
	int32 CityFrames = 0;
	TArray<FString> CityViews;
	UPROPERTY()
	TObjectPtr<class ACameraActor> CityCamera;
	int32 SandStep = 0;
	float SandTime = 0.f;
	float SandStepTime = 0.f;
	int32 SandPass = 0;
	int32 SandMark = 0;
	int32 SandMarkAir = 0;
	int32 SandMarkNotSand = 0;
	double IdleMs = 0.0, WalkMs = 0.0;
	int32 IdleFrames = 0, WalkFrames = 0;

	void RunGunTest(float DeltaSeconds, class ACharacter* Player);
	bool bGunTest = false;
	float GunTime = 0.f;
	float GunStepTime = 0.f;
	int32 GunStep = 0;
	int32 GunObs = 0;
	int32 SliceBase = 0;
	int32 RepeatShots = 0;

	int32 ObsIndex = 0;
	bool bKnifeTest = false;
	float KnifeTime = 0.f;
	int32 KnifeStep = 0;
	float StepTime = 0.f;

	/** -DunePortfolio=knife|gun|sand: Day 18 portfolio screenshots (2560x1440) of the cutting, the gun fracture and the sand. Rendered run. See DunePortfolioShots.cpp. */
	void RunPortfolio(float DeltaSeconds, class ACharacter* Player);
	bool bPortfolio = false;
	FString PfMode;
	int32 PfStep = 0;
	int32 PfAux = 0;
	float PfTime = 0.f;
	float PfStepTime = 0.f;
	float PfVal[4] = {};
	FString PfShotName;
	FVector PfShotFrom = FVector::ZeroVector;
	FRotator PfShotRot = FRotator::ZeroRotator;
	float PfShotFov = 60.f;
	float PfShotHold = 2.5f;
	float PfShotTime = 0.f;
	int32 PfShotAfter = 0;
	int32 PfShotTicksLeft = -1;

	bool bSelfTest = false;
	float TestTime = 0.f;
	int32 Step = 0;
	FVector StartLocation = FVector::ZeroVector;
	float MaxZ = 0.f;
};
