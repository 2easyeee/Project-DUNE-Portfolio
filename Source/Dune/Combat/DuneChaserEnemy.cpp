#include "Dune/Combat/DuneChaserEnemy.h"
#include "Dune/Combat/DuneHealthComponent.h"
#include "Dune/Combat/DunePlayerCharacter.h"
#include "Dune/Combat/DuneSandShot.h"
#include "Dune/Flow/DuneRunSubsystem.h"
#include "Engine/GameInstance.h"
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Navigation/PathFollowingComponent.h"
#include "SandInteractionComponent.h"
#include "SandDeformationLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

ADuneChaserEnemy::ADuneChaserEnemy()
{
	PrimaryActorTick.bCanEverTick = true;
	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	GetCapsuleComponent()->InitCapsuleSize(52.f, 105.f);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block); // the player's shot is a visibility trace

	// the enemy is a third-party creature mesh; no anim blueprint: single animations picked in code
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshAsset(TEXT("/Game/SciFi_Beast06/Mesh/SK_SciFi_Beast06_Skin1.SK_SciFi_Beast06_Skin1"));
	auto Anim = [](const TCHAR* Name) { return Cast<UAnimSequence>(StaticLoadObject(UAnimSequence::StaticClass(), nullptr, *FString::Printf(TEXT("/Game/SciFi_Beast06/Animations/Anim_SciFi_Beast06_%s.Anim_SciFi_Beast06_%s"), Name, Name))); };
	AnimRun = Anim(TEXT("Run"));
	AnimIdle = Anim(TEXT("Idle2"));
	for (const TCHAR* N : { TEXT("Attack1"), TEXT("Attack2"), TEXT("Attack3") }) AnimAttacks.Add(Anim(N));
	for (const TCHAR* N : { TEXT("Death1"), TEXT("Death2") }) AnimDeaths.Add(Anim(N));
	AnimHitFront = Anim(TEXT("GetHitF"));
	AnimHitBack = Anim(TEXT("GetHitB"));
	AnimHitLeft = Anim(TEXT("GetHitL"));
	AnimHitRight = Anim(TEXT("GetHitR"));
	// the shots hit the body shapes of the physics asset (a visibility trace); with the capsule alone a shot at the horns or the tail went through
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	GetMesh()->SetCollisionResponseToAllChannels(ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	if (MeshAsset.Succeeded())
	{
		GetMesh()->SetSkeletalMesh(MeshAsset.Object);
		GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -105.f));
		GetMesh()->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	}
	GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	GetCharacterMovement()->MaxWalkSpeed = MoveSpeed;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->bUseRVOAvoidance = true; // chasers steer around each other instead of piling up
	GetCharacterMovement()->AvoidanceConsiderationRadius = 220.f;
	GetCharacterMovement()->MaxAcceleration = 2400.f;
	bUseControllerRotationYaw = false;
	Health = CreateDefaultSubobject<UDuneHealthComponent>(TEXT("Health"));
	// foot prints and landings in the sand where the patch is (no effect on levels without sand)
	Sand = CreateDefaultSubobject<USandInteractionComponent>(TEXT("SandInteraction"));
	Sand->WeightKg = 90.f;
	Sand->bFootStamps = false; // the human-shaped print does not fit this monster: StampTracks leaves hoof prints and dash furrows instead
}

bool ADuneChaserEnemy::IsDead() const { return Health && Health->IsDead(); }

void ADuneChaserEnemy::BeginPlay()
{
	Super::BeginPlay();
	Health->Init(MaxHealth, MaxHealth);
	Health->OnDied.AddDynamic(this, &ADuneChaserEnemy::OnDied);
	Health->OnHealthChanged.AddDynamic(this, &ADuneChaserEnemy::OnHurt);
	GetCharacterMovement()->MaxWalkSpeed = MoveSpeed;
	PlayBullAnim(AnimRun, true);
}

void ADuneChaserEnemy::PlayBullAnim(UAnimSequence* Seq, bool bLoop)
{
	if (!Seq || !GetMesh()) return;
	if (bLoop && CurrentAnim == Seq) return;
	CurrentAnim = Seq;
	GetMesh()->PlayAnimation(Seq, bLoop);
}

void ADuneChaserEnemy::SetDormant(int32 InPackId, float InAggroRadiusCm)
{
	bAggro = false;
	PackId = InPackId;
	AggroRadiusCm = InAggroRadiusCm;
	PlayBullAnim(AnimIdle, true);
}

void ADuneChaserEnemy::SetKind(EKind InKind)
{
	Kind = InKind;
	const float Hp = Kind == EKind::Charger ? 80.f : (Kind == EKind::Thrower ? 45.f : MaxHealth);
	MaxHealth = Hp;
	if (Health) Health->Init(Hp, Hp); // BeginPlay has already run when the game mode picks the kind
	NextSpecialTime = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f) + 1.f;
}

bool ADuneChaserEnemy::TickSpecial(ADunePlayerCharacter& T, float Dt)
{
	const float Now = GetWorld()->GetTimeSeconds();
	const FVector ToT = T.GetActorLocation() - GetActorLocation();
	const float Dist = ToT.Size2D();
	const FVector Dir = ToT.GetSafeNormal2D();
	AAIController* AI = Cast<AAIController>(GetController());
	UCharacterMovementComponent* Move = GetCharacterMovement();
	auto Face = [&](float Speed) { SetActorRotation(FMath::RInterpTo(GetActorRotation(), Dir.Rotation(), Dt, Speed)); };
	auto HasLineOfSight = [&]() {
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(EnemySight), false, this);
		const FVector Eye = GetActorLocation() + FVector(0.f, 0.f, 60.f);
		return !GetWorld()->LineTraceSingleByChannel(Hit, Eye, T.GetActorLocation() + FVector(0.f, 0.f, 40.f), ECC_Visibility, Q) || Hit.GetActor() == &T;
	};

	if (Kind == EKind::Charger)
	{
		switch (Special)
		{
		case ESpecial::None:
			if (Now >= NextSpecialTime && Dist >= 550.f && Dist <= 1800.f && HasLineOfSight())
			{
				Special = ESpecial::Windup;
				SpecialTimer = DashWindupSeconds;
				if (AI) AI->StopMovement();
				PlayBullAnim(AnimIdle, true);
			}
			return false;
		case ESpecial::Windup:
			SpecialTimer -= Dt;
			Face(12.f);
			GetMesh()->SetRelativeScale3D(FVector(1.12f, 1.12f, 0.78f)); // hunkers down: the warning
			if (SpecialTimer <= 0.f)
			{
				Special = ESpecial::Dash;
				SpecialTimer = DashMaxSeconds;
				DashDir = Dir;
				bDashHit = false;
				++DashCount;
				GetMesh()->SetRelativeScale3D(FVector::OneVector);
				Move->MaxWalkSpeed = DashSpeed;
				Move->MaxAcceleration = 12000.f;
				PlayBullAnim(AnimRun, true);
			}
			return true;
		case ESpecial::Dash:
		{
			SpecialTimer -= Dt;
			AddMovementInput(DashDir, 1.f);
			if (!bDashHit && Dist <= 150.f)
			{
				bDashHit = true;
				UGameplayStatics::ApplyDamage(&T, DashDamage, GetController(), this, UDamageType::StaticClass());
				T.LaunchCharacter(DashDir * 650.f + FVector(0.f, 0.f, 250.f), true, true);
			}
			const bool bBlocked = (DashMaxSeconds - SpecialTimer) > 0.25f && GetVelocity().Size2D() < 250.f; // ran into a wall
			if (SpecialTimer <= 0.f || bDashHit || bBlocked)
			{
				Special = ESpecial::Recover;
				SpecialTimer = DashRecoverSeconds;
				Move->MaxWalkSpeed = MoveSpeed;
				Move->MaxAcceleration = 2400.f;
				Move->StopMovementImmediately();
				PlayBullAnim(AnimIdle, true);
			}
			return true;
		}
		case ESpecial::Recover: // open for a moment
			SpecialTimer -= Dt;
			if (SpecialTimer <= 0.f) { Special = ESpecial::None; NextSpecialTime = Now + DashCooldownSeconds; }
			return true;
		}
		return false;
	}

	// Thrower
	switch (Special)
	{
	case ESpecial::None:
		if (Dist <= AttackRangeCm || !HasLineOfSight()) return false; // cornered: it hits; no sight: it walks up like the others
		if (Dist < 750.f) // too close: backs off
		{
			if (AI) AI->StopMovement();
			AddMovementInput(-Dir, 1.f);
			PlayBullAnim(AnimRun, true);
			return true;
		}
		if (Dist > PreferredDistanceCm + 350.f) return false; // too far: walks closer
		if (AI) AI->StopMovement();
		Face(8.f);
		PlayBullAnim(AnimIdle, true);
		if (Now >= NextSpecialTime)
		{
			Special = ESpecial::Windup;
			SpecialTimer = ThrowWindupSeconds;
			if (AnimAttacks.Num() > 0) PlayBullAnim(AnimAttacks[0], false);
		}
		return true;
	case ESpecial::Windup:
		SpecialTimer -= Dt;
		Face(12.f);
		if (SpecialTimer <= 0.f)
		{
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			const FVector Origin = GetActorLocation() + FVector(0.f, 0.f, 120.f) + Dir * 80.f;
			if (ADuneSandShot* Shot = GetWorld()->SpawnActor<ADuneSandShot>(ADuneSandShot::StaticClass(), Origin, FRotator::ZeroRotator, P))
			{
				Shot->Launch(Origin, T.GetActorLocation(), 1100.f, ThrowDamage, this);
				++ThrowCount;
			}
			Special = ESpecial::Recover;
			SpecialTimer = 0.6f;
			NextSpecialTime = Now + ThrowCooldownSeconds;
		}
		return true;
	case ESpecial::Recover:
		SpecialTimer -= Dt;
		if (SpecialTimer <= 0.f) Special = ESpecial::None;
		return true;
	default:
		return false;
	}
}

void ADuneChaserEnemy::WakePack()
{
	bAggro = true;
	if (PackId < 0 || !GetWorld()) return;
	for (TActorIterator<ADuneChaserEnemy> It(GetWorld()); It; ++It)
	{
		if (It->PackId == PackId) It->bAggro = true;
	}
}

void ADuneChaserEnemy::OnHurt(float NewHealth, float NewMax, float Delta)
{
	if (Delta < 0.f && !bAggro) WakePack(); // a shot at one wakes the pack
	// a hit staggers: it turns the way the blow came from, is thrown back a little (more for a heavy hit) and stands still for a moment
	if (Delta >= 0.f || IsDead() || !GetWorld()) return;
	const float Damage = -Delta;
	const bool bHeavy = Damage >= HeavyHitDamage;
	const float Now = GetWorld()->GetTimeSeconds();
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	const FVector FromPlayer = Player ? (GetActorLocation() - Player->GetActorLocation()).GetSafeNormal2D() : -GetActorForwardVector();
	++FlinchCount;
	StunUntil = Now + (bHeavy ? 0.45f : 0.16f);
	if (Special == ESpecial::Windup && bHeavy) // a heavy hit knocks the wind-up out of it
	{
		Special = ESpecial::None;
		NextSpecialTime = Now + 1.5f;
		GetMesh()->SetRelativeScale3D(FVector::OneVector);
	}
	if (AAIController* AI = Cast<AAIController>(GetController())) AI->StopMovement();
	if (GetCharacterMovement()->MovementMode != MOVE_None) // an enemy that is held still (self tests) stays where it is
	{
		LaunchCharacter(FromPlayer * FMath::Clamp(120.f + Damage * 4.f, 150.f, 520.f), true, false);
	}

	// which side the blow landed on, in the enemy's own frame
	const FVector Local = GetActorTransform().InverseTransformVectorNoScale(-FromPlayer);
	UAnimSequence* Reaction = AnimHitFront;
	if (FMath::Abs(Local.Y) > FMath::Abs(Local.X)) Reaction = Local.Y > 0.f ? AnimHitRight : AnimHitLeft;
	else if (Local.X < 0.f) Reaction = AnimHitBack;
	if (Reaction)
	{
		PlayBullAnim(Reaction, false);
		AttackAnimUntil = Now + FMath::Min(Reaction->GetPlayLength(), bHeavy ? 0.45f : 0.2f);
	}
}

void ADuneChaserEnemy::DealAttackDamage()
{
	// the swing lands part way through the animation: it only hurts if the player is still close
	ADunePlayerCharacter* Target = Cast<ADunePlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (IsDead() || !Target || Target->IsDead()) return;
	if (GetWorld()->GetTimeSeconds() < StunUntil && StunUntil - GetWorld()->GetTimeSeconds() > 0.2f) return; // a heavy hit knocked the swing out of the way
	if (FVector::Dist2D(GetActorLocation(), Target->GetActorLocation()) <= AttackRangeCm + 80.f)
	{
		UGameplayStatics::ApplyDamage(Target, AttackDamage, GetController(), this, UDamageType::StaticClass());
	}
}

void ADuneChaserEnemy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (IsDead()) return;
	ADunePlayerCharacter* Target = Cast<ADunePlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!Target || Target->IsDead()) { PlayBullAnim(AnimIdle, true); return; }

	const float Now = GetWorld()->GetTimeSeconds();
	if (!bAggro)
	{
		// dormant: stands in its pack until the player comes close
		if (FVector::Dist2D(GetActorLocation(), Target->GetActorLocation()) <= AggroRadiusCm) WakePack();
		else { PlayBullAnim(AnimIdle, true); return; }
	}
	StampTracks();
	if (Now < StunUntil) return; // staggered by a hit
	if (Kind != EKind::Normal && TickSpecial(*Target, DeltaSeconds)) return;
	const float Dist = FVector::Dist2D(GetActorLocation(), Target->GetActorLocation());
	if (Dist <= AttackRangeCm)
	{
		if (AAIController* AI = Cast<AAIController>(GetController())) AI->StopMovement();
		// face the player while hitting
		const FRotator Face = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D().Rotation();
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), Face, DeltaSeconds, 10.f));
		if (Now - LastAttackTime >= AttackInterval && AnimAttacks.Num() > 0)
		{
			LastAttackTime = Now;
			UAnimSequence* Swing = AnimAttacks[FMath::RandRange(0, AnimAttacks.Num() - 1)];
			PlayBullAnim(Swing, false);
			AttackAnimUntil = Now + (Swing ? Swing->GetPlayLength() : 0.5f);
			FTimerHandle H;
			GetWorldTimerManager().SetTimer(H, this, &ADuneChaserEnemy::DealAttackDamage, 0.3f, false);
		}
		else if (Now >= AttackAnimUntil)
		{
			PlayBullAnim(AnimIdle, true);
		}
		return;
	}
	if (Now >= AttackAnimUntil) PlayBullAnim(AnimRun, true);

	const FVector ToTarget = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();

	// stuck check: far from the player but barely moved in the last 0.7 s = step sideways for a moment (around an edge of cover)
	StuckTimer += DeltaSeconds;
	if (StuckTimer >= 0.7f)
	{
		const bool bStuck = FVector::Dist2D(GetActorLocation(), LastCheckPos) < 50.f;
		LastCheckPos = GetActorLocation();
		StuckTimer = 0.f;
		if (bStuck)
		{
			SidestepTime = 0.7f;
			SidestepSign = FMath::RandBool() ? 1.f : -1.f;
			if (AAIController* AI = Cast<AAIController>(GetController())) AI->StopMovement();
			RepathTimer = 0.f;
		}
	}
	if (SidestepTime > 0.f)
	{
		SidestepTime -= DeltaSeconds;
		AddMovementInput(FVector::CrossProduct(FVector::UpVector, ToTarget) * SidestepSign + ToTarget * 0.4f, 1.f);
		return;
	}

	RepathTimer -= DeltaSeconds;
	if (RepathTimer > 0.f) return;
	RepathTimer = 0.4f;
	AAIController* AI = Cast<AAIController>(GetController());
	// partial paths are allowed: when the player stands where there is no nav mesh (a ledge), the enemy goes as near as it can
	const EPathFollowingRequestResult::Type R = AI ? AI->MoveToActor(Target, AttackRangeCm * 0.5f, true, true, true, nullptr, true) : EPathFollowingRequestResult::Failed;
	if (R == EPathFollowingRequestResult::Failed)
	{
		// no nav path at all: walk straight at the player
		AddMovementInput(ToTarget, 1.f);
		RepathTimer = 0.f;
	}
}

void ADuneChaserEnemy::OnDied()
{
	if (UDuneRunSubsystem* Run = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDuneRunSubsystem>() : nullptr) ++Run->Kills;
	if (AAIController* AI = Cast<AAIController>(GetController())) { AI->StopMovement(); AI->UnPossess(); }
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UAnimSequence* Death = AnimDeaths.Num() > 0 ? AnimDeaths[FMath::RandRange(0, AnimDeaths.Num() - 1)] : nullptr;
	if (Death) { CurrentAnim = Death; GetMesh()->PlayAnimation(Death, false); } // plays once and holds the last pose
	SetLifeSpan(3.f);
}

void ADuneChaserEnemy::StampTracks()
{
	const FVector Pos = GetActorLocation();
	const bool bDashing = Special == ESpecial::Dash;
	if (!GetCharacterMovement()->IsMovingOnGround() || GetVelocity().SizeSquared2D() < 60.f * 60.f) { bHasLastTrack = false; return; }
	if (!bHasLastTrack) { LastTrackPos = Pos; bHasLastTrack = true; return; }
	const FVector Step = FVector(Pos.X - LastTrackPos.X, Pos.Y - LastTrackPos.Y, 0.f);
	const float Dist = Step.Size();
	const FVector Dir = Step.GetSafeNormal();
	const FVector Side(-Dir.Y, Dir.X, 0.f);
	if (bDashing)
	{
		// the charge ploughs the sand: a furrow as wide as the body with a raised rim
		if (Dist < 40.f) return;
		FSandInteractionRequest Furrow;
		Furrow.Brush = ESandBrushType::Wheel;
		Furrow.PreviousPosition = LastTrackPos;
		Furrow.WorldPosition = Pos;
		Furrow.bHasPreviousPosition = true;
		Furrow.MovementDirection = Step;
		Furrow.Radius = 38.f;
		Furrow.DepthOverrideCm = 6.f;
		Furrow.RidgeScale = 2.f;
		Furrow.SourceActor = this;
		USandDeformationLibrary::ApplySandInteraction(this, Furrow);
		LastTrackPos = Pos;
		return;
	}
	if (Dist < 125.f) return; // one stride
	bTrackLeft = !bTrackLeft;
	const FVector Centre = Pos + Side * (bTrackLeft ? -28.f : 28.f) - Dir * 40.f; // the left and right hooves lie on either side of the path
	for (int32 Toe = 0; Toe < 2; ++Toe)
	{
		FSandInteractionRequest Hoof; // two narrow ovals side by side
		Hoof.Brush = ESandBrushType::Footprint;
		Hoof.Shape = ESandContactShape::Foot;
		Hoof.WorldPosition = Centre + Side * (Toe == 0 ? -10.f : 10.f);
		Hoof.MovementDirection = Dir;
		Hoof.Extent = FVector2D(17.f, 6.f);
		Hoof.Radius = 8.f;
		Hoof.DepthOverrideCm = 8.f;
		Hoof.Push = 0.3f;
		Hoof.SourceActor = this;
		USandDeformationLibrary::ApplySandInteraction(this, Hoof);
	}
	LastTrackPos = Pos;
}
