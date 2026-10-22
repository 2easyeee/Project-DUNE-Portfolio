// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dune/SliceableStructure.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "KismetProceduralMeshLibrary.h"
#include "Math/RandomStream.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "UE_HOLDITTOGETHER.h"

// dune.Slice [nx ny nz [offset]]  cuts the structure nearest to the player (default normal 0 0 1, plane through its centre,
// offset in cm along the normal). Development helper: the cut itself is ASliceableStructure::SliceMeshWithPlane.
static FAutoConsoleCommandWithWorldAndArgs GSliceCommand(TEXT("dune.Slice"),
	TEXT("dune.Slice [nx ny nz [offset]]  cut the nearest sliceable structure with a plane through its centre"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		const AActor* Player = UGameplayStatics::GetPlayerPawn(World, 0);
		ASliceableStructure* Target = nullptr;
		float BestDistSq = MAX_flt;
		for (TActorIterator<ASliceableStructure> It(World); It; ++It)
		{
			const float DistSq = Player ? FVector::DistSquared(It->GetActorLocation(), Player->GetActorLocation()) : 0.f;
			if (DistSq < BestDistSq)
			{
				Target = *It;
				BestDistSq = DistSq;
			}
		}
		if (!Target)
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Slice: no ASliceableStructure in this level"));
			return;
		}
		const FVector Normal = Args.Num() >= 3 ? FVector(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), FCString::Atof(*Args[2])) : FVector::UpVector;
		const float Offset = Args.Num() >= 4 ? FCString::Atof(*Args[3]) : 0.f;
		const FVector Center = Target->GetMesh()->Bounds.Origin;
		Target->SliceMeshWithPlane(Center + Normal.GetSafeNormal() * Offset, Normal);
	}));

ASliceableStructure::ASliceableStructure()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false; // only runs while a freed piece is still moving

	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->bUseComplexAsSimpleCollision = false; // collision is one convex hull per piece, see RebuildCollision

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Basic(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	BaseMaterial = Basic.Object;
	SourceMesh = Cube.Object;
}

void ASliceableStructure::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	BuildMesh();
}

void ASliceableStructure::BuildMesh()
{
	Mesh->ClearAllMeshSections();
	Pieces.Empty();
	if (!SourceMesh)
	{
		return;
	}

	// the engine helper reads a UStaticMeshComponent, so a throwaway one carries the source mesh
	UStaticMeshComponent* Source = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
	Source->SetStaticMesh(SourceMesh);
	UKismetProceduralMeshLibrary::CopyProceduralMeshFromStaticMeshComponent(Source, 0, Mesh, true);
	RebuildCollision(Mesh, bConvexCollision);

	// what was read from the static mesh (LOD 0): enough to tell "no CPU data" from "cut failed"
	int32 Verts, Tris, Sections;
	CountMesh(Mesh, Verts, Tris, Sections);
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Slice: [%s] source mesh %s | Allow CPU Access=%d Nanite=%d | LOD0 %d sections %d verts %d tris -> procedural %d sections %d verts %d tris"),
		*GetName(), *SourceMesh->GetPathName(), SourceMesh->bAllowCPUAccess, SourceMesh->IsNaniteEnabled(),
		SourceMesh->GetNumSections(0), SourceMesh->GetNumVertices(0), SourceMesh->GetNumTriangles(0), Sections, Verts, Tris);
	if (Verts == 0)
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Error, TEXT("Slice: [%s] no mesh data read from %s (Allow CPU Access=%d); it cannot be cut"),
			*GetName(), *SourceMesh->GetPathName(), SourceMesh->bAllowCPUAccess);
	}
	else if (!SourceMesh->bAllowCPUAccess)
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Slice: [%s] %s has Allow CPU Access off: the read worked here but fails in a cooked build"),
			*GetName(), *SourceMesh->GetPathName());
	}

	// ground level and footprint of the uncut structure: what "still standing on the ground" is measured against later
	GroundZ = MAX_flt;
	for (int32 i = 0; i < Mesh->GetNumSections(); ++i)
	{
		for (const FProcMeshVertex& V : Mesh->GetProcMeshSection(i)->ProcVertexBuffer)
		{
			GroundZ = FMath::Min(GroundZ, Mesh->GetComponentTransform().TransformPosition(V.Position).Z);
		}
	}
	OriginalFootprintArea = Verts > 0 ? GroundContactArea(Mesh) : 0.f;
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Slice: [%s] ground level z=%.0f, footprint %.0f cm2"), *GetName(), GroundZ, OriginalFootprintArea);

	BodyMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	CapMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	BodyMaterial->SetVectorParameterValue(TEXT("Color"), Tint);
	CapMaterial->SetVectorParameterValue(TEXT("Color"), CapTint);
	for (int32 i = 0; i < Mesh->GetNumSections() && !bUseSourceMaterials; ++i)
	{
		Mesh->SetMaterial(i, BodyMaterial);
	}
}

ASliceableStructure* ASliceableStructure::ConvertStaticMeshActor(AStaticMeshActor* Actor, float DebugSeparationCm)
{
	UStaticMeshComponent* Component = Actor ? Actor->GetStaticMeshComponent() : nullptr;
	UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
	if (!Mesh || !Mesh->bAllowCPUAccess)
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Slice: cannot convert %s: %s"), *GetNameSafe(Actor), Mesh ? TEXT("mesh has Allow CPU Access off") : TEXT("no static mesh"));
		return nullptr;
	}
	const FTransform Transform = Actor->GetActorTransform();
	ASliceableStructure* Structure = Actor->GetWorld()->SpawnActorDeferred<ASliceableStructure>(StaticClass(), Transform);
	Structure->SourceMesh = Mesh;
	Structure->bUseSourceMaterials = true;
	Structure->bConvexCollision = false; // buildings are not convex
	Structure->DebugSeparation = DebugSeparationCm;
	if (Actor->ActorHasTag(DuneGunFractureTag))
	{
		// the level designer marked this actor: the gun may break it into many pieces, which needs room for them
		Structure->bGunFracturable = true;
		Structure->MaxPieces = 64;
		Structure->Tags.Add(DuneGunFractureTag);
	}
	Structure->FinishSpawning(Transform);
#if WITH_EDITOR
	Structure->SetActorLabel(Actor->GetActorLabel());
#endif
	// material overrides placed on the actor (sections follow the material slots of the mesh)
	if (Component->GetNumMaterials() == Structure->Mesh->GetNumMaterials())
	{
		for (int32 i = 0; i < Component->GetNumMaterials(); ++i)
		{
			if (Component->GetMaterial(i) != Mesh->GetMaterial(i))
			{
				Structure->Mesh->SetMaterial(i, Component->GetMaterial(i));
			}
		}
	}
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Slice: converted static mesh actor %s (mesh %s) into sliceable structure %s"), *Actor->GetName(), *Mesh->GetPathName(), *Structure->GetName());
	Actor->Destroy();
	return Structure;
}

void ASliceableStructure::RebuildCollision(UProceduralMeshComponent* Component, bool bConvex, float InsetCm)
{
	if (!bConvex)
	{
		// non-convex source (a building): the triangles of every section are the collision, exact but static only
		for (int32 i = 0; i < Component->GetNumSections(); ++i)
		{
			Component->GetProcMeshSection(i)->bEnableCollision = true;
		}
		Component->bUseComplexAsSimpleCollision = true;
		Component->ClearCollisionConvexMeshes(); // rebuilds the collision from the sections
		return;
	}

	// the engine slice only cuts convex shapes that came with the source mesh, and basic shapes carry none; a cut of a
	// convex mesh is convex, so one hull around the vertices of the piece is exact
	TArray<FVector> Points;
	for (int32 i = 0; i < Component->GetNumSections(); ++i)
	{
		for (const FProcMeshVertex& V : Component->GetProcMeshSection(i)->ProcVertexBuffer)
		{
			Points.Add(V.Position);
		}
	}
	// a building has thousands of vertices; the hull only needs the extreme ones. Extreme points in 98 directions plus
	// the vertices of the last section (the cut face), so the hull follows both the outline and the cut plane.
	if (Points.Num() > 400)
	{
		TArray<FVector> Reduced;
		for (int32 X = -2; X <= 2; ++X)
		{
			for (int32 Y = -2; Y <= 2; ++Y)
			{
				for (int32 Z = -2; Z <= 2; ++Z)
				{
					if (X == 0 && Y == 0 && Z == 0)
					{
						continue;
					}
					const FVector Dir = FVector(X, Y, Z).GetSafeNormal();
					const FVector* Best = &Points[0];
					for (const FVector& P : Points)
					{
						if (FVector::DotProduct(P, Dir) > FVector::DotProduct(*Best, Dir))
						{
							Best = &P;
						}
					}
					Reduced.AddUnique(*Best);
				}
			}
		}
		const FProcMeshSection* Last = Component->GetProcMeshSection(Component->GetNumSections() - 1);
		const int32 Step = FMath::Max(1, Last->ProcVertexBuffer.Num() / 128);
		for (int32 i = 0; i < Last->ProcVertexBuffer.Num(); i += Step)
		{
			Reduced.Add(Last->ProcVertexBuffer[i].Position);
		}
		Points = MoveTemp(Reduced);
	}
	// fragments of one wall touch along their cuts; hulls that are a little smaller than the pieces leave a hair of room so that resting
	// neighbours do not start inside each other (that is what throws fragments away at start). Each point moves towards the centre.
	if (InsetCm > 0.f && Points.Num() >= 4)
	{
		FVector Centre(0.f);
		for (const FVector& P : Points) { Centre += P; }
		Centre /= Points.Num();
		for (FVector& P : Points)
		{
			P -= (P - Centre).GetSafeNormal() * FMath::Min(InsetCm, (P - Centre).Size() * 0.4f);
		}
	}

	// a sliver (a thin shaving cut off a surface) gives a flat hull the physics engine cannot handle (it asserts in GJK and in
	// the contact generation, and thin bodies tunnel through the ground): thicken every axis to at least MinHullThicknessCm
	if (Points.Num() >= 4)
	{
		const FVector Scale = Component->GetComponentScale().GetAbs().ComponentMax(FVector(KINDA_SMALL_NUMBER));
		FVector Min(MAX_flt), Max(-MAX_flt);
		for (const FVector& P : Points)
		{
			Min = Min.ComponentMin(P);
			Max = Max.ComponentMax(P);
		}
		const FVector Center = (Min + Max) * 0.5f;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const float Size = (Max[Axis] - Min[Axis]) * Scale[Axis];
			if (Size < MinHullThicknessCm)
			{
				const float Factor = MinHullThicknessCm / FMath::Max(Size, 0.01f);
				for (FVector& P : Points)
				{
					P[Axis] = Center[Axis] + (P[Axis] - Center[Axis]) * Factor;
				}
				UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Slice: %s is only %.1f cm thick along local axis %d, its collision was thickened to %.0f cm"), *Component->GetName(), Size, Axis, MinHullThicknessCm);
			}
		}
	}
	// weld points closer than 1 cm: near-duplicate points make hair-thin hull faces that the contact solver cannot handle
	TArray<FVector> Welded;
	for (const FVector& P : Points)
	{
		const FVector Snapped(FMath::RoundToFloat(P.X), FMath::RoundToFloat(P.Y), FMath::RoundToFloat(P.Z));
		Welded.AddUnique(Snapped);
	}
	if (Welded.Num() >= 4)
	{
		Points = MoveTemp(Welded);
	}
	Component->bUseComplexAsSimpleCollision = false;
	Component->ClearCollisionConvexMeshes();
	if (Points.Num() >= 4)
	{
		Component->AddCollisionConvexMesh(Points);
		// a hull the engine could not build would leave the piece without any collision (it would fall through everything):
		// then the bounding box of the points is used instead
		const UBodySetup* Setup = Component->GetBodySetup();
		if (!Setup || Setup->AggGeom.ConvexElems.Num() == 0)
		{
			FVector Lo(MAX_flt), Hi(-MAX_flt);
			for (const FVector& P : Points)
			{
				Lo = Lo.ComponentMin(P);
				Hi = Hi.ComponentMax(P);
			}
			const FVector Pad(MinHullThicknessCm * 0.5f);
			Lo -= Pad;
			Hi += Pad;
			TArray<FVector> Box;
			for (int32 i = 0; i < 8; ++i)
			{
				Box.Add(FVector((i & 1) ? Hi.X : Lo.X, (i & 2) ? Hi.Y : Lo.Y, (i & 4) ? Hi.Z : Lo.Z));
			}
			Component->ClearCollisionConvexMeshes();
			Component->AddCollisionConvexMesh(Box);
			UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Slice: %s: no convex hull could be built from %d points, its bounding box is the collision"), *Component->GetName(), Points.Num());
		}
	}
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Slice: %s convex collision from %d points"), *Component->GetName(), Points.Num());
}

void ASliceableStructure::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	for (int32 i = Freed.Num() - 1; i >= 0; --i)
	{
		UProceduralMeshComponent* Piece = Freed[i].Piece.Get();
		if (!Piece || !Piece->IsSimulatingPhysics())
		{
			Freed.RemoveAt(i);
			continue;
		}
		// safety net: a piece that went through the ground (a fast thin piece can tunnel between two physics steps) would fall forever
		if (Piece->GetComponentLocation().Z < GroundZ - 400.f)
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Slice: piece %s fell out of the world (z=%.0f, speed %.0f cm/s) and was removed"), *Piece->GetName(), Piece->GetComponentLocation().Z,
				Piece->GetPhysicsLinearVelocity().Size());
			Pieces.Remove(Piece);
			if (Piece == Mesh)
			{
				Piece->SetSimulatePhysics(false);
				Piece->SetVisibility(false);
				Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
			else
			{
				Piece->DestroyComponent();
			}
			Freed.RemoveAt(i);
			continue;
		}
		const bool bStill = Piece->GetPhysicsLinearVelocity().Size() < FreezeSpeed && Piece->GetPhysicsAngularVelocityInDegrees().Size() < FreezeSpeed;
		Freed[i].StillTime = bStill ? Freed[i].StillTime + DeltaSeconds : 0.f;
		if (Freed[i].StillTime >= FreezeAfterSeconds)
		{
			// it came to rest: stop simulating it, so a settled piece costs nothing and cannot creep
			Piece->SetSimulatePhysics(false);
			Piece->SetCollisionProfileName(TEXT("BlockAll"));
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Slice: piece %s came to rest at %s (lowest point z=%.0f) and was frozen"), *Piece->GetName(), *Piece->GetComponentLocation().ToString(),
				Piece->Bounds.Origin.Z - Piece->Bounds.BoxExtent.Z);
			Freed.RemoveAt(i);
		}
	}
	if (Freed.Num() == 0)
	{
		SetActorTickEnabled(false);
	}
}

float ASliceableStructure::MinWidthCm(UProceduralMeshComponent* Component)
{
	static const FVector Directions[13] = {
		FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), FVector(1, 1, 0).GetSafeNormal(), FVector(1, -1, 0).GetSafeNormal(), FVector(1, 0, 1).GetSafeNormal(),
		FVector(1, 0, -1).GetSafeNormal(), FVector(0, 1, 1).GetSafeNormal(), FVector(0, 1, -1).GetSafeNormal(), FVector(1, 1, 1).GetSafeNormal(), FVector(1, 1, -1).GetSafeNormal(),
		FVector(1, -1, 1).GetSafeNormal(), FVector(1, -1, -1).GetSafeNormal() };
	float Best = MAX_flt;
	for (const FVector& D : Directions)
	{
		float Lo = MAX_flt, Hi = -MAX_flt;
		for (int32 i = 0; i < Component->GetNumSections(); ++i)
		{
			for (const FProcMeshVertex& V : Component->GetProcMeshSection(i)->ProcVertexBuffer)
			{
				const float Along = FVector::DotProduct(Component->GetComponentTransform().TransformPosition(V.Position), D);
				Lo = FMath::Min(Lo, Along);
				Hi = FMath::Max(Hi, Along);
			}
		}
		Best = FMath::Min(Best, Hi - Lo);
	}
	return Best;
}

float ASliceableStructure::GroundContactArea(UProceduralMeshComponent* Component) const
{
	// vertices within 8 cm of the ground level, as a 2D convex outline (monotone chain), area by the shoelace formula
	TArray<FVector2D> P;
	for (int32 i = 0; i < Component->GetNumSections(); ++i)
	{
		for (const FProcMeshVertex& V : Component->GetProcMeshSection(i)->ProcVertexBuffer)
		{
			const FVector W = Component->GetComponentTransform().TransformPosition(V.Position);
			if (W.Z <= GroundZ + 8.f)
			{
				P.Add(FVector2D(W.X, W.Y));
			}
		}
	}
	if (P.Num() < 3)
	{
		return 0.f;
	}
	P.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X || (A.X == B.X && A.Y < B.Y); });
	auto Cross = [](const FVector2D& O, const FVector2D& A, const FVector2D& B) { return (A.X - O.X) * (B.Y - O.Y) - (A.Y - O.Y) * (B.X - O.X); };
	TArray<FVector2D> H;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const int32 Start = H.Num();
		for (int32 i = 0; i < P.Num(); ++i)
		{
			const FVector2D& Pt = P[Pass == 0 ? i : P.Num() - 1 - i];
			while (H.Num() >= Start + 2 && Cross(H[H.Num() - 2], H.Last(), Pt) <= 0.f)
			{
				H.Pop();
			}
			H.Add(Pt);
		}
		H.Pop();
	}
	float Area = 0.f;
	for (int32 i = 0; i < H.Num(); ++i)
	{
		const FVector2D& A = H[i];
		const FVector2D& B = H[(i + 1) % H.Num()];
		Area += A.X * B.Y - B.X * A.Y;
	}
	return FMath::Abs(Area) * 0.5f;
}

float ASliceableStructure::MeshVolumeCm3(UProceduralMeshComponent* Component)
{
	double Volume = 0.0;
	for (int32 i = 0; i < Component->GetNumSections(); ++i)
	{
		const FProcMeshSection* S = Component->GetProcMeshSection(i);
		if (!S)
		{
			continue;
		}
		for (int32 T = 0; T + 2 < S->ProcIndexBuffer.Num(); T += 3)
		{
			const uint32 Count = S->ProcVertexBuffer.Num();
			if (S->ProcIndexBuffer[T] >= Count || S->ProcIndexBuffer[T + 1] >= Count || S->ProcIndexBuffer[T + 2] >= Count)
			{
				continue; // a broken triangle must not crash the volume estimate
			}
			const FVector A = S->ProcVertexBuffer[S->ProcIndexBuffer[T]].Position;
			const FVector B = S->ProcVertexBuffer[S->ProcIndexBuffer[T + 1]].Position;
			const FVector C = S->ProcVertexBuffer[S->ProcIndexBuffer[T + 2]].Position;
			Volume += FVector::DotProduct(A, FVector::CrossProduct(B, C)) / 6.0;
		}
	}
	const FVector Scale = Component->GetComponentScale();
	return static_cast<float>(FMath::Abs(Volume) * FMath::Abs(Scale.X * Scale.Y * Scale.Z));
}

void ASliceableStructure::MakeDynamic(UProceduralMeshComponent* Piece, const FVector& StartVelocity, const FVector& StartAngularVelocityDeg, float HullInsetCm, float MaxSpinDeg)
{
	// 1. collision first: a physics body needs convex shapes, triangle collision cannot be simulated
	for (int32 i = 0; i < Piece->GetNumSections(); ++i)
	{
		Piece->GetProcMeshSection(i)->bEnableCollision = false;
	}
	RebuildCollision(Piece, true, HullInsetCm);

	// 2. then the body: mass from the enclosed volume, damping and sleeping so it settles instead of jittering
	Piece->SetCollisionProfileName(TEXT("PhysicsActor"));
	Piece->SetSimulatePhysics(true);
	Piece->SetUseCCD(true); // a small piece thrown by a bullet must not tunnel through the ground between two physics steps
	if (MaxSpinDeg > 0.f)
	{
		Piece->SetPhysicsMaxAngularVelocityInDegrees(MaxSpinDeg); // a hit far from a flat piece's centre would otherwise spin it like a coin
	}
	const float Volume = MeshVolumeCm3(Piece);
	const float MassKg = FMath::Clamp(Volume * 1e-6f * DensityKgPerM3, 5.f, 500000.f); // cm3 -> m3
	Piece->SetMassOverrideInKg(NAME_None, MassKg, true);
	Piece->SetLinearDamping(LinearDamping);
	Piece->SetAngularDamping(AngularDamping);
	Piece->BodyInstance.SleepFamily = ESleepFamily::Custom;
	Piece->BodyInstance.CustomSleepThresholdMultiplier = 5.f; // sleeps sooner than the default
	Piece->WakeAllRigidBodies();
	Freed.RemoveAll([Piece](const FFreedPiece& F) { return F.Piece == Piece; });
	Freed.Add({ Piece, 0.f });
	SetActorTickEnabled(true);
	if (!StartVelocity.IsNearlyZero())
	{
		Piece->SetPhysicsLinearVelocity(StartVelocity);
	}
	if (!StartAngularVelocityDeg.IsNearlyZero())
	{
		Piece->SetPhysicsAngularVelocityInDegrees(StartAngularVelocityDeg);
	}
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Slice: piece %s is now a physics body: %.0f m3 -> mass %.0f kg, start velocity %s, simulating=%d"),
		*Piece->GetName(), Volume * 1e-6f, MassKg, *StartVelocity.ToString(), Piece->IsSimulatingPhysics());
}

void ASliceableStructure::CountMesh(UProceduralMeshComponent* Component, int32& Verts, int32& Tris, int32& Sections)
{
	Verts = Tris = 0;
	Sections = Component ? Component->GetNumSections() : 0;
	for (int32 i = 0; i < Sections; ++i)
	{
		if (const FProcMeshSection* Section = Component->GetProcMeshSection(i))
		{
			Verts += Section->ProcVertexBuffer.Num();
			Tris += Section->ProcIndexBuffer.Num() / 3;
		}
	}
}

FVector ASliceableStructure::WorldCentroid(UProceduralMeshComponent* Component)
{
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	for (int32 i = 0; i < Component->GetNumSections(); ++i)
	{
		for (const FProcMeshVertex& V : Component->GetProcMeshSection(i)->ProcVertexBuffer)
		{
			Sum += Component->GetComponentTransform().TransformPosition(V.Position);
			++Count;
		}
	}
	return Count > 0 ? Sum / Count : Component->GetComponentLocation();
}

void ASliceableStructure::LogMeshInfo(const TCHAR* Label) const
{
	int32 Verts, Tris, Sections;
	CountMesh(Mesh, Verts, Tris, Sections);
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Slice: %s [%s] component=%s sections=%d verts=%d tris=%d extent=%s pieces=%d"),
		Label, *GetName(), *Mesh->GetClass()->GetName(), Sections, Verts, Tris, *Mesh->Bounds.BoxExtent.ToString(), Pieces.Num());
}

bool ASliceableStructure::CanSlice(FString& Reason) const
{
	return CheckComponent(Mesh, Reason);
}

bool ASliceableStructure::SliceMeshWithPlane(FVector PlanePosition, FVector PlaneNormal)
{
	return SliceComponentWithPlane(Mesh, PlanePosition, PlaneNormal);
}

bool ASliceableStructure::IsSliceable(const UPrimitiveComponent* Component) const
{
	return Component && (Component == Mesh || Pieces.Contains(Component));
}

bool ASliceableStructure::CheckComponent(UProceduralMeshComponent* Target, FString& Reason) const
{
	int32 Verts = 0, Tris = 0, Sections = 0;
	if (Target)
	{
		CountMesh(Target, Verts, Tris, Sections);
	}
	if (!SourceMesh)
	{
		Reason = TEXT("no source mesh set");
	}
	else if (!IsSliceable(Target))
	{
		Reason = TEXT("component does not belong to this structure");
	}
	else if (Verts == 0 || Tris == 0)
	{
		Reason = FString::Printf(TEXT("procedural mesh has no data (sections=%d, verts=%d, tris=%d); source mesh '%s' could not be read"), Sections, Verts, Tris, *SourceMesh->GetName());
	}
	else
	{
		return true;
	}
	return false;
}

ECutTarget ASliceableStructure::ClassifyHit(const FHitResult& Hit, FString& WhyNot)
{
	if (const ASliceableStructure* Structure = Cast<ASliceableStructure>(Hit.GetActor()))
	{
		if (Structure->IsSliceable(Hit.GetComponent()))
		{
			return ECutTarget::Sliceable;
		}
	}
	// a plain static mesh actor is a cut target only if its mesh was prepared for cutting (Allow CPU Access on)
	const AStaticMeshActor* PlainActor = Cast<AStaticMeshActor>(Hit.GetActor());
	const UStaticMesh* Mesh = PlainActor && PlainActor->GetStaticMeshComponent() ? PlainActor->GetStaticMeshComponent()->GetStaticMesh() : nullptr;
	if (!PlainActor || !Mesh)
	{
		WhyNot = TEXT("not a sliceable structure or a static mesh actor");
		return ECutTarget::None;
	}
	if (!Mesh->bAllowCPUAccess)
	{
		WhyNot = FString::Printf(TEXT("mesh %s has Allow CPU Access off"), *Mesh->GetPathName());
		return ECutTarget::None;
	}
	return ECutTarget::Convertible;
}

void ASliceableStructure::ApplyImpulse(const TArray<UProceduralMeshComponent*>& Targets, const FSliceImpulse& Impulse)
{
	// the momentum is shared out by distance to the hit (the nearer piece takes more); each piece's speed change is its share divided
	// by its own mass, so small pieces fly and big ones hardly move. Applied after the body exists, at the hit point, so a piece
	// hit away from its centre of mass also spins.
	if (Targets.Num() == 0)
	{
		return;
	}
	float WeightSum = 0.f;
	TArray<float> Weights;
	for (UProceduralMeshComponent* Piece : Targets)
	{
		Weights.Add(1.f / (1.f + FVector::Dist(Impulse.Location, WorldCentroid(Piece)) / 200.f));
		WeightSum += Weights.Last();
	}
	for (int32 i = 0; i < Targets.Num(); ++i)
	{
		UProceduralMeshComponent* Piece = Targets[i];
		const float Mass = FMath::Max(Piece->GetMass(), 1.f);
		FVector Impulsive = Impulse.Direction.GetSafeNormal() * Impulse.MomentumKgCmPerSec * Weights[i] / WeightSum;
		const float Speed = Impulsive.Size() / Mass;
		if (Speed > Impulse.MaxSpeed)
		{
			Impulsive *= Impulse.MaxSpeed / Speed; // never faster than MaxSpeed, however light the piece
		}
		const FVector Centre = WorldCentroid(Piece);
		Piece->AddImpulseAtLocation(Impulsive, Centre + (Impulse.Location - Centre) * Impulse.Lever);
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Slice: impulse on %s: %.0f kg cm/s at %s (piece mass %.0f kg, speed change %.1f cm/s, centre %s)"), *Piece->GetName(),
			Impulsive.Size(), *Impulse.Location.ToString(), Mass, Impulsive.Size() / Mass, *WorldCentroid(Piece).ToString());
	}
}

bool ASliceableStructure::CutByPlane(UProceduralMeshComponent* Target, const FVector& PlanePosition, const FVector& PlaneNormal, UMaterialInterface* Cap,
	UProceduralMeshComponent*& OutOther, FSliceReport& Report, bool bQuiet)
{
	OutOther = nullptr;
	Report.PlanePosition = PlanePosition;
	Report.PlaneNormal = PlaneNormal;
	int32 Sections;
	CountMesh(Target, Report.VertsBefore, Report.TrisBefore, Sections);

	auto Fail = [&](ESliceResult Result, const FString& Why)
	{
		Report.Result = Result;
		Report.Reason = Why;
		if (!bQuiet || Result != ESliceResult::PlaneMissesMesh)
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Slice: FAILED [%s] %s: %s (plane at %s normal %s)"),
				*GetName(), *StaticEnum<ESliceResult>()->GetNameStringByValue(static_cast<int64>(Result)), *Why, *PlanePosition.ToString(), *PlaneNormal.ToString());
		}
		return false;
	};

	FString Why;
	if (!CheckComponent(Target, Why))
	{
		return Fail(ESliceResult::NoMeshData, Why);
	}
	if (Pieces.Num() + 1 >= MaxPieces)
	{
		return Fail(ESliceResult::PieceLimit, FString::Printf(TEXT("the structure already has %d pieces (limit %d)"), Pieces.Num() + 1, MaxPieces));
	}
	FVector Normal = PlaneNormal;
	if (PlaneNormal.ContainsNaN() || PlanePosition.ContainsNaN() || !Normal.Normalize(1e-4f))
	{
		return Fail(ESliceResult::InvalidNormal, TEXT("plane normal is zero or not a number"));
	}

	// signed distance of every vertex to the plane: the plane must have vertices on both sides
	Report.DistMin = MAX_flt;
	Report.DistMax = -MAX_flt;
	for (int32 i = 0; i < Target->GetNumSections(); ++i)
	{
		for (const FProcMeshVertex& V : Target->GetProcMeshSection(i)->ProcVertexBuffer)
		{
			const float Dist = FVector::DotProduct(Target->GetComponentTransform().TransformPosition(V.Position) - PlanePosition, Normal);
			Report.DistMin = FMath::Min(Report.DistMin, Dist);
			Report.DistMax = FMath::Max(Report.DistMax, Dist);
		}
	}
	if (Report.DistMin >= -0.1f || Report.DistMax <= 0.1f)
	{
		return Fail(ESliceResult::PlaneMissesMesh, FString::Printf(TEXT("all vertices on one side of the plane (distances %.1f .. %.1f cm)"), Report.DistMin, Report.DistMax));
	}

	// triangles that really cross the plane (corners on both sides): these get split, the rest go whole to one half
	Report.TrisCrossing = 0;
	for (int32 i = 0; i < Target->GetNumSections(); ++i)
	{
		const FProcMeshSection* Section = Target->GetProcMeshSection(i);
		auto Side = [&](int32 Index)
		{
			return FVector::DotProduct(Target->GetComponentTransform().TransformPosition(Section->ProcVertexBuffer[Index].Position) - PlanePosition, Normal);
		};
		for (int32 T = 0; T + 2 < Section->ProcIndexBuffer.Num(); T += 3)
		{
			const float A = Side(Section->ProcIndexBuffer[T]), B = Side(Section->ProcIndexBuffer[T + 1]), C = Side(Section->ProcIndexBuffer[T + 2]);
			Report.TrisCrossing += (FMath::Min3(A, B, C) < 0.f && FMath::Max3(A, B, C) > 0.f) ? 1 : 0;
		}
	}

	UProceduralMeshComponent* Other = nullptr;
	UKismetProceduralMeshLibrary::SliceProceduralMesh(Target, PlanePosition, Normal, true, Other, EProcMeshSliceCapOption::CreateNewSectionForCap, Cap ? Cap : CapMaterial);
	if (!Other)
	{
		return Fail(ESliceResult::SliceFailed, TEXT("engine slice returned no second half"));
	}
	Pieces.Add(Other);
	OutOther = Other;
	return true;
}

bool ASliceableStructure::SliceComponentWithPlane(UProceduralMeshComponent* Target, FVector PlanePosition, FVector PlaneNormal, FVector PushDirection,
	const FSliceImpulse* Impulse)
{
	const double StartTime = FPlatformTime::Seconds();
	++TotalSliceRequests;
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Slice: cut requested on %s / %s (request %d)"), *GetName(), *GetNameSafe(Target), TotalSliceRequests);
	FSliceReport Report;
	int32 Sections = 0;

	// a piece that is already falling keeps its motion in both halves
	const bool bWasSimulating = Target && Target->IsSimulatingPhysics();
	const FVector OldVelocity = bWasSimulating ? Target->GetPhysicsLinearVelocity() : FVector::ZeroVector;
	const FVector OldSpin = bWasSimulating ? Target->GetPhysicsAngularVelocityInDegrees() : FVector::ZeroVector;

	// the mesh split itself (validation, cap, new half) is CutByPlane, shared with the gun's fracture; what follows is the knife's policy
	UProceduralMeshComponent* Other = nullptr;
	if (!CutByPlane(Target, PlanePosition, PlaneNormal, CapMaterial, Other, Report, false))
	{
		LastReport = Report;
		return false;
	}
	const FVector Normal = PlaneNormal.GetSafeNormal();

	if (!bSimulatePieces)
	{
		RebuildCollision(Target, bConvexCollision);
		RebuildCollision(Other, bConvexCollision);
	}
	else
	{
		// halves that still stand on the ground stay put (the rest of the structure does not move); the others are freed
		// the half with the larger ground contact is the one still carried by the ground (when it covers enough of the
		// footprint); the other half is a free body, even if it touches the ground too: physics decides if it stays
		TArray<UProceduralMeshComponent*> FreedNow;
		const float ContactTarget = GroundContactArea(Target);
		const float ContactOther = GroundContactArea(Other);
		UProceduralMeshComponent* Anchor = ContactTarget >= ContactOther ? Target : Other;
		for (UProceduralMeshComponent* Half : { Target, Other })
		{
			const float Contact = Half == Target ? ContactTarget : ContactOther;
			const bool bSupported = !bWasSimulating && Half == Anchor && Contact >= GroundContactFraction * OriginalFootprintArea;
			UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Slice: half %s ground contact %.0f cm2 of %.0f cm2 original footprint -> %s"), *Half->GetName(), Contact,
				OriginalFootprintArea, bSupported ? TEXT("supported, stays") : TEXT("freed, simulates"));
			if (bSupported)
			{
				RebuildCollision(Half, bConvexCollision);
			}
			else
			{
				// a small start speed: along the blade's travel (the attack direction, it lies in the cut plane) and a little
				// away from the other half so the cut opens; gravity and the contacts do the rest
				const float Away = FVector::DotProduct(WorldCentroid(Half) - WorldCentroid(Half == Target ? Other : Target), Normal) >= 0.f ? 1.f : -1.f;
				const FVector Push = PushDirection.GetSafeNormal() * PushSpeed + Normal * Away * SeparationSpeed;
				MakeDynamic(Half, bWasSimulating ? OldVelocity : Push, OldSpin);
				FreedNow.Add(Half);
			}
		}

		// a bullet pushes only what the cut set free, and each piece by its distance to the hit (the nearer one takes more).
		// The impulse is applied after the body exists, at the hit point, so it also spins a piece that was hit off-centre.
		if (Impulse && Impulse->MomentumKgCmPerSec > 0.f)
		{
			ApplyImpulse(FreedNow, *Impulse);
		}
	}

	// debug: pull the halves apart along the normal, the half that lies on the normal side goes forward
	if (DebugSeparation > 0.f)
	{
		const float Side = FVector::DotProduct(WorldCentroid(Other) - WorldCentroid(Target), Normal);
		Other->AddWorldOffset(Normal * DebugSeparation * (Side >= 0.f ? 1.f : -1.f));
		Target->AddWorldOffset(Normal * DebugSeparation * (Side >= 0.f ? -1.f : 1.f));
	}

	CountMesh(Target, Report.VertsKept, Report.TrisKept, Sections);
	CountMesh(Other, Report.VertsOther, Report.TrisOther, Sections);
	Report.Result = ESliceResult::Success;
	LastReport = Report;
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Slice: OK [%s] component=%s plane at %s normal %s | before: %d verts %d tris (%d cross the plane) | kept half: %d verts %d tris | other half: %d verts %d tris | collision: %s, %d sections"),
		*GetName(), *Target->GetClass()->GetName(), *PlanePosition.ToString(), *Normal.ToString(),
		Report.VertsBefore, Report.TrisBefore, Report.TrisCrossing, Report.VertsKept, Report.TrisKept, Report.VertsOther, Report.TrisOther,
		bConvexCollision ? TEXT("convex hull") : TEXT("triangle mesh"), Sections);
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Slice: [%s] cut + collision + physics took %.1f ms; pieces now %d (components %d), freed pieces still moving %d"),
		*GetName(), (FPlatformTime::Seconds() - StartTime) * 1000.0, Pieces.Num() + 1, GetComponents().Num(), Freed.Num());
	DrawDebugSolidPlane(GetWorld(), FPlane(PlanePosition, Normal), PlanePosition, 150.f, FColor(255, 80, 0, 90), false, 5.f);
	return true;
}

const FName ASliceableStructure::DuneGunFractureTag(TEXT("DuneGunFracture"));

void ASliceableStructure::CutSorted(UProceduralMeshComponent* Piece, const FVector& Position, const FVector& Normal,
	TArray<UProceduralMeshComponent*>& Positive, TArray<UProceduralMeshComponent*>& Negative)
{
	UProceduralMeshComponent* Other = nullptr;
	FSliceReport Report;
	if (CutByPlane(Piece, Position, Normal, FractureCapMaterial, Other, Report, true))
	{
		++FractureCutCount;
		// the half whose centre lies on the side the normal points to is the positive one
		const bool bPieceIsPositive = FVector::DotProduct(WorldCentroid(Piece) - Position, Normal) >= FVector::DotProduct(WorldCentroid(Other) - Position, Normal);
		(bPieceIsPositive ? Positive : Negative).Add(Piece);
		(bPieceIsPositive ? Negative : Positive).Add(Other);
		return;
	}
	// the plane missed this piece (or the piece limit was reached): the whole piece is on one side
	(FVector::DotProduct(WorldCentroid(Piece) - Position, Normal) >= 0.f ? Positive : Negative).Add(Piece);
}

int32 ASliceableStructure::FractureAt(UProceduralMeshComponent* HitPiece, const FVector& HitPoint, const FVector& NormalIn, const FVector& Bullet)
{
	const double StartTime = FPlatformTime::Seconds();
	FFractureStats Stats;
	FString Why;
	if (!bGunFracturable)
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Fracture: %s is not marked gun-fracturable, nothing broken"), *GetName());
		return 0;
	}
	if (!CheckComponent(HitPiece, Why))
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Fracture: cannot break %s: %s"), *GetNameSafe(HitPiece), *Why);
		return 0;
	}
	FVector N = NormalIn.GetSafeNormal();
	if (N.IsNearlyZero() || N.ContainsNaN())
	{
		N = -Bullet.GetSafeNormal();
	}
	if (N.IsNearlyZero() || HitPoint.ContainsNaN())
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Fracture: no usable surface normal or hit point"));
		return 0;
	}
	if (MaxPieces - (Pieces.Num() + 1) < 8)
	{
		UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Fracture: %s already has %d pieces (limit %d), not breaking it further"), *GetName(), Pieces.Num() + 1, MaxPieces);
		return 0;
	}
	if (!FractureCapMaterial)
	{
		FractureCapMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		FractureCapMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.38f, 0.37f, 0.35f)); // raw concrete: the inside of a broken wall
	}
	const int32 CutsBefore = FractureCutCount;
	FRandomStream Rng(1337 + FractureShots * 7919 + Pieces.Num()); // the same shot always breaks the same way
	++FractureShots;

	// a frame on the wall at the hit point, turned by a random angle so the cracks do not line up with the mesh axes
	FVector T1, T2;
	N.FindBestAxisVectors(T1, T2);
	const float Twist = Rng.FRandRange(0.f, PI);
	const FVector A1 = T1 * FMath::Cos(Twist) + T2 * FMath::Sin(Twist);
	const FVector A2 = FVector::CrossProduct(N, A1).GetSafeNormal();

	TArray<UProceduralMeshComponent*> Work{ HitPiece };
	TArray<UProceduralMeshComponent*> Outside;
	TArray<UProceduralMeshComponent*> Pos, Neg;

	// 1. the damaged chunk: five cuts around the hit point (four along the wall, one behind it) separate it from the structure.
	//    What is left stays where it is, with a real hole. A fragment that is already small is not isolated again.
	if (HitPiece->Bounds.BoxExtent.GetMax() > ChunkHalfSizeCm * 1.6f)
	{
		struct FBoundary { FVector Outward; float Extent; };
		const FBoundary Boundaries[5] = {
			{ A1, ChunkHalfSizeCm * Rng.FRandRange(0.85f, 1.2f) }, { -A1, ChunkHalfSizeCm * Rng.FRandRange(0.85f, 1.2f) },
			{ A2, ChunkHalfSizeCm * Rng.FRandRange(0.8f, 1.15f) }, { -A2, ChunkHalfSizeCm * Rng.FRandRange(0.8f, 1.15f) },
			{ -N, ChunkDepthCm * Rng.FRandRange(0.85f, 1.15f) } };
		for (const FBoundary& B : Boundaries)
		{
			Pos.Reset();
			Neg.Reset();
			for (UProceduralMeshComponent* W : Work)
			{
				CutSorted(W, HitPoint + B.Outward * B.Extent, -B.Outward, Pos, Neg); // the normal points to the hit: the chunk is the positive side
			}
			Outside.Append(Neg);
			Work = Pos;
		}
		if (Work.Num() == 0)
		{
			UE_LOG(LogUE_HOLDITTOGETHER, Warning, TEXT("Fracture: the chunk around %s is empty (hit outside the mesh?)"), *HitPoint.ToString());
			return 0;
		}
	}

	// 2. primary cracks: planes through the hit point, at uneven angles and a little tilted, so the chunk breaks into wedges of different size
	const int32 K = FMath::Clamp(PrimaryCracks, 1, 4);
	const float BaseAngle = Rng.FRandRange(0.f, PI);
	for (int32 k = 0; k < K; ++k)
	{
		const float Angle = BaseAngle + (k + Rng.FRandRange(-0.2f, 0.2f)) * PI / K;
		const FVector Crack = (A1 * FMath::Cos(Angle) + A2 * FMath::Sin(Angle) + N * Rng.FRandRange(-0.3f, 0.3f)).GetSafeNormal();
		Pos.Reset();
		Neg.Reset();
		for (UProceduralMeshComponent* W : Work)
		{
			CutSorted(W, HitPoint, Crack, Pos, Neg);
		}
		Work = Pos;
		Work.Append(Neg);
	}

	// 3. branches: most wedges are cut again across, at a random distance along their own direction. The inner part, near the hit, is
	//    small; the outer part stays big. This is what gives pieces of all sizes instead of equal slices.
	TArray<UProceduralMeshComponent*> Branched;
	for (UProceduralMeshComponent* W : Work)
	{
		FVector Radial = WorldCentroid(W) - HitPoint;
		Radial -= N * FVector::DotProduct(Radial, N);
		const float Reach = Radial.Size();
		Radial = Radial.GetSafeNormal();
		if (Radial.IsNearlyZero())
		{
			Radial = A1.RotateAngleAxis(Rng.FRandRange(0.f, 360.f), N);
		}
		if (Rng.FRand() < 0.85f)
		{
			const float Distance = FMath::Clamp(Reach * Rng.FRandRange(0.45f, 0.9f), 18.f, ChunkHalfSizeCm);
			const FVector Branch = (Radial.RotateAngleAxis(Rng.FRandRange(-28.f, 28.f), N) + N * Rng.FRandRange(-0.3f, 0.3f)).GetSafeNormal();
			Pos.Reset();
			Neg.Reset();
			CutSorted(W, HitPoint + Radial * Distance, Branch, Pos, Neg);
			Branched.Append(Pos);
			Branched.Append(Neg);
		}
		else
		{
			Branched.Add(W);
		}
	}
	Work = Branched;

	// 4. spalling: the biggest fragments split into a front and a back layer at different depths, like a wall that breaks in layers
	TArray<TPair<float, UProceduralMeshComponent*>> BySize;
	for (UProceduralMeshComponent* W : Work)
	{
		BySize.Emplace(MeshVolumeCm3(W), W);
	}
	BySize.Sort([](const TPair<float, UProceduralMeshComponent*>& A, const TPair<float, UProceduralMeshComponent*>& B) { return A.Key > B.Key; });
	const int32 BigCount = FMath::Max(1, BySize.Num() * 35 / 100);
	TArray<UProceduralMeshComponent*> Layered;
	for (int32 i = 0; i < BySize.Num(); ++i)
	{
		if (i < BigCount)
		{
			Pos.Reset();
			Neg.Reset();
			CutSorted(BySize[i].Value, HitPoint - N * Rng.FRandRange(18.f, 40.f), N, Pos, Neg);
			Layered.Append(Pos);
			Layered.Append(Neg);
		}
		else
		{
			Layered.Add(BySize[i].Value);
		}
	}
	Work = Layered;

	// 5. dust is removed, the rest become fragments. The root component cannot be destroyed, it is only hidden.
	TArray<UProceduralMeshComponent*> Fragments;
	TArray<float> Volumes;
	for (UProceduralMeshComponent* W : Work)
	{
		const float Volume = MeshVolumeCm3(W);
		if (Volume < MinFragmentVolumeCm3 || MinWidthCm(W) < MinFragmentWidthCm)
		{
			++Stats.Dust;
			Pieces.Remove(W);
			if (W == Mesh)
			{
				W->SetVisibility(false);
				W->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
			else
			{
				W->DestroyComponent();
			}
			continue;
		}
		Fragments.Add(W);
		Volumes.Add(Volume);
	}
	float MaxVolume = 0.f;
	for (float V : Volumes) { MaxVolume = FMath::Max(MaxVolume, V); }
	Stats.MaxVolumeCm3 = MaxVolume;
	Stats.MinVolumeCm3 = Volumes.Num() > 0 ? FMath::Min(Volumes) : 0.f;
	for (float V : Volumes)
	{
		if (V >= 0.45f * MaxVolume) { ++Stats.Large; }
		else if (V >= 0.12f * MaxVolume) { ++Stats.Medium; }
		else { ++Stats.Small; }
	}

	// 6. collision and physics: the rest of the structure keeps its (triangle) collision; each fragment gets a convex hull, becomes a body,
	//    opens a little away from the hit along the wall, and then the bullet's momentum is applied at the hit point
	for (UProceduralMeshComponent* O : Outside)
	{
		RebuildCollision(O, bConvexCollision);
	}
	for (UProceduralMeshComponent* F : Fragments)
	{
		FVector Open = WorldCentroid(F) - HitPoint;
		Open = (Open - N * FVector::DotProduct(Open, N)).GetSafeNormal() * 0.8f + N * 0.3f;
		MakeDynamic(F, Open.GetSafeNormal() * FractureOpenSpeed, FVector::ZeroVector, FractureHullInsetCm, FractureMaxSpinDeg);
	}
	FSliceImpulse Impulse;
	Impulse.Location = HitPoint;
	Impulse.Direction = Bullet;
	Impulse.MomentumKgCmPerSec = FractureMomentum;
	Impulse.MaxSpeed = FractureMaxSpeed;
	Impulse.Lever = FractureLever;
	ApplyImpulse(Fragments, Impulse);

	Stats.Fragments = Fragments.Num();
	Stats.Cuts = FractureCutCount - CutsBefore;
	Stats.OutsidePieces = Outside.Num();
	Stats.Milliseconds = static_cast<float>((FPlatformTime::Seconds() - StartTime) * 1000.0);
	LastFracture = Stats;
	UE_LOG(LogUE_HOLDITTOGETHER, Log, TEXT("Fracture: %s broken at %s (normal %s, bullet %s): %d cuts, %d fragments (large %d, medium %d, small %d; volume %.0f .. %.0f cm3), %d dust removed, %d wall pieces left, took %.1f ms, structure now has %d pieces"),
		*GetName(), *HitPoint.ToString(), *N.ToString(), *Bullet.ToString(), Stats.Cuts, Stats.Fragments, Stats.Large, Stats.Medium, Stats.Small, Stats.MinVolumeCm3, Stats.MaxVolumeCm3,
		Stats.Dust, Stats.OutsidePieces, Stats.Milliseconds, Pieces.Num() + 1);
	return Stats.Fragments;
}
