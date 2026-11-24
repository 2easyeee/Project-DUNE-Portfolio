#include "Dune/Combat/DuneSandDissolve.h"
#include "Dune/Boss/DuneBossCore.h"
#include "Dune/Combat/DuneBuriedCrate.h"
#include "Dune/SliceableStructure.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"

DEFINE_LOG_CATEGORY_STATIC(LogDuneDissolve, Log, All);

namespace
{
	constexpr int32 kGrains = 5000;
	const FTransform GHidden(FQuat::Identity, FVector(0.f, 0.f, -100000.f), FVector(0.01f));
}

ADuneSandDissolve::ADuneSandDissolve()
{
	PrimaryActorTick.bCanEverTick = true;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	GrainMesh = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Grains"));
	GrainMesh->SetupAttachment(Root);
	GrainMesh->SetMobility(EComponentMobility::Movable);
	GrainMesh->SetUsingAbsoluteLocation(true);
	GrainMesh->SetUsingAbsoluteRotation(true);
	GrainMesh->SetUsingAbsoluteScale(true);
	GrainMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GrainMesh->SetCastShadow(false);
	GrainMesh->SetCanEverAffectNavigation(false);
}

ADuneSandDissolve* ADuneSandDissolve::Start(UWorld* World, const FVector& Origin, const TArray<AActor*>& Keep)
{
	if (!World) return nullptr;
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADuneSandDissolve* D = World->SpawnActor<ADuneSandDissolve>(ADuneSandDissolve::StaticClass(), FTransform(Origin), SP);
	if (!D) return nullptr;

	if (UStaticMesh* Ball = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		D->GrainMesh->SetStaticMesh(Ball);
		D->GrainMesh->SetMaterial(0, ADuneBossCore::MakeGlow(D, FLinearColor(1.0f, 0.64f, 0.32f, 1.f)));
		for (int32 i = 0; i < kGrains; ++i) D->GrainMesh->AddInstance(GHidden, false);
		D->Grains.SetNum(kGrains);
		D->GrainTransforms.Init(GHidden, kGrains);
	}
	D->Wind = FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f).Vector() * 900.f;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (A == D || Keep.Contains(A) || A->IsHidden() || A->IsA<APawn>() || A->ActorHasTag(TEXT("DuneKeep"))) continue;
		if (!(A->IsA<AStaticMeshActor>() || A->IsA<ASliceableStructure>() || A->IsA<ADuneBuriedCrate>())) continue;
		if (A->GetActorLabel().Contains(TEXT("FloorBackstop"))) continue; // the floor stays: the player and the new pillars stand on it
		FVector C, E;
		A->GetActorBounds(true, C, E);
		if (E.GetMax() > 12000.f || E.IsNearlyZero()) continue;
		FTarget T;
		T.Actor = A;
		T.BaseLoc = A->GetActorLocation();
		T.BaseScale = A->GetActorScale3D();
		T.Box = FBox(C - E, C + E);
		T.Delay = FMath::Min(FVector::Dist2D(C, Origin) / 1000.f * D->SpreadSecondsPer1000Cm, D->MaxDelaySeconds) + FMath::FRandRange(0.f, 0.25f);
		D->Targets.Add(T);
	}
	UE_LOG(LogDuneDissolve, Log, TEXT("Dissolve: %d actors turn to sand"), D->Targets.Num());
	return D;
}

void ADuneSandDissolve::Emit(const FTarget& T, float TopZ, int32 Count)
{
	if (Grains.Num() == 0) return;
	const FVector Ext = T.Box.GetExtent();
	const FVector C = T.Box.GetCenter();
	for (int32 k = 0; k < Count; ++k)
	{
		FGrain& G = Grains[Cursor];
		Cursor = (Cursor + 1) % Grains.Num();
		const float Z = FMath::FRandRange(T.Box.Min.Z, FMath::Max(TopZ, T.Box.Min.Z + 1.f));
		G.Pos = FVector(C.X + FMath::FRandRange(-Ext.X, Ext.X), C.Y + FMath::FRandRange(-Ext.Y, Ext.Y), Z);
		G.Vel = Wind * FMath::FRandRange(0.5f, 1.3f) + FVector(FMath::FRandRange(-250.f, 250.f), FMath::FRandRange(-250.f, 250.f), FMath::FRandRange(60.f, 420.f));
		G.MaxLife = G.Life = FMath::FRandRange(1.4f, 3.2f);
		G.Size = FMath::FRandRange(3.f, 9.f);
	}
}

void ADuneSandDissolve::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;

	int32 Active = 0;
	for (const FTarget& T : Targets) { if (T.bStarted && !T.bDone) ++Active; }
	// the grain budget is shared by the actors that erode right now (a few big ones or many small ones)
	const float PerActorRate = Active > 0 ? FMath::Min(GrainBudgetPerSecond / Active, 220.f) : 0.f;

	bool bAllDone = true;
	for (FTarget& T : Targets)
	{
		if (T.bDone) continue;
		AActor* A = T.Actor.Get();
		if (!A) { T.bDone = true; continue; }
		bAllDone = false;
		const float U = (Time - T.Delay) / FMath::Max(ErodeSeconds, 0.1f);
		if (U < 0.f) continue;
		if (!T.bStarted)
		{
			T.bStarted = true;
			A->SetActorEnableCollision(false); // nothing blocks the player or the shots while it crumbles
		}
		const float P = FMath::Clamp(U, 0.f, 1.f);
		const float S = 1.f - P * P * (3.f - 2.f * P); // smoothstep: 1 -> 0
		const float FootZ = T.Box.Min.Z;
		A->SetActorScale3D(FVector(T.BaseScale.X, T.BaseScale.Y, FMath::Max(T.BaseScale.Z * S, 0.001f)));
		A->SetActorLocation(FVector(T.BaseLoc.X, T.BaseLoc.Y, FootZ + (T.BaseLoc.Z - FootZ) * S));
		const float TopZ = FootZ + (T.Box.Max.Z - FootZ) * S;
		Emit(T, TopZ, FMath::Max(1, FMath::RoundToInt(PerActorRate * DeltaSeconds)));
		if (U >= 1.f)
		{
			T.bDone = true;
			A->Destroy();
		}
	}

	// the grains
	bool bAnyGrain = false;
	for (int32 i = 0; i < Grains.Num(); ++i)
	{
		FGrain& G = Grains[i];
		if (G.Life <= 0.f)
		{
			if (!GrainTransforms[i].Equals(GHidden)) { GrainTransforms[i] = GHidden; bAnyGrain = true; }
			continue;
		}
		bAnyGrain = true;
		G.Life -= DeltaSeconds;
		G.Vel += (Wind - G.Vel) * FMath::Min(1.f, 0.9f * DeltaSeconds); // the wind takes them
		G.Vel.Z -= 260.f * DeltaSeconds;
		G.Pos += G.Vel * DeltaSeconds;
		const float A = FMath::Clamp(G.Life / G.MaxLife, 0.f, 1.f);
		const float Sz = G.Size / 100.f * (0.2f + 0.8f * FMath::Sqrt(A));
		const float Stretch = 1.f + FMath::Min(G.Vel.Size() / 260.f, 6.f);
		GrainTransforms[i] = FTransform(FRotationMatrix::MakeFromX(G.Vel.IsNearlyZero() ? FVector::UpVector : G.Vel).ToQuat(), G.Pos, FVector(Sz * Stretch, Sz * 0.7f, Sz * 0.7f));
	}
	if (bAnyGrain && GrainMesh) GrainMesh->BatchUpdateInstancesTransforms(0, GrainTransforms, true, true, true);

	if (bAllDone && !bFinished)
	{
		bFinished = true;
		UE_LOG(LogDuneDissolve, Log, TEXT("Dissolve: every actor is gone after %.1f s"), Time);
	}
	if (bFinished)
	{
		SettleTime += DeltaSeconds;
		if (SettleTime > 4.f && !bAnyGrain) Destroy();
	}
}
