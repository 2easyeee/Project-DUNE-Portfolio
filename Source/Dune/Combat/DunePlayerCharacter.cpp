#include "Dune/Combat/DunePlayerCharacter.h"
#include "Dune/Combat/DuneDebrisLauncher.h"
#include "Dune/Combat/DuneFirstPersonArms.h"
#include "Dune/Combat/DuneGunFeel.h"
#include "Dune/DuneFeelFlags.h"
#include "Dune/Combat/DuneHealthComponent.h"
#include "Dune/Combat/DuneTracer.h"
#include "SandDeformationLibrary.h"
#include "Dune/Flow/DuneRunSubsystem.h"
#include "Dune/SliceableStructure.h"
#include "ProceduralMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

ADunePlayerCharacter::ADunePlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	GetCapsuleComponent()->InitCapsuleSize(34.f, 88.f);
	GetMesh()->SetVisibility(false);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.f, 0.f, 64.f));
	Camera->bUsePawnControlRotation = true;
	Camera->SetFieldOfView(90.f);
	bUseControllerRotationYaw = true;

	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	GetCharacterMovement()->JumpZVelocity = 560.f;
	GetCharacterMovement()->AirControl = 0.5f;

	Health = CreateDefaultSubobject<UDuneHealthComponent>(TEXT("Health"));
	Debris = CreateDefaultSubobject<UDuneDebrisLauncher>(TEXT("Debris"));
	GunFeel = CreateDefaultSubobject<UDuneGunFeel>(TEXT("GunFeel"));

	DebrisAction = CreateDefaultSubobject<UInputAction>(TEXT("IA_Debris"));
	DebrisAction->ValueType = EInputActionValueType::Boolean;
	MoveAction = CreateDefaultSubobject<UInputAction>(TEXT("IA_Move"));
	MoveAction->ValueType = EInputActionValueType::Axis2D;
	LookAction = CreateDefaultSubobject<UInputAction>(TEXT("IA_Look"));
	LookAction->ValueType = EInputActionValueType::Axis2D;
	JumpAction = CreateDefaultSubobject<UInputAction>(TEXT("IA_Jump"));
	JumpAction->ValueType = EInputActionValueType::Boolean;
	FireAction = CreateDefaultSubobject<UInputAction>(TEXT("IA_Fire"));
	FireAction->ValueType = EInputActionValueType::Boolean;
	ReloadAction = CreateDefaultSubobject<UInputAction>(TEXT("IA_Reload"));
	ReloadAction->ValueType = EInputActionValueType::Boolean;
	SprintAction = CreateDefaultSubobject<UInputAction>(TEXT("IA_Sprint"));
	SprintAction->ValueType = EInputActionValueType::Boolean;

	Context = CreateDefaultSubobject<UInputMappingContext>(TEXT("IMC_Player"));
	auto Mapped = [this](UInputAction* A, FKey Key) -> FEnhancedActionKeyMapping& { return Context->MapKey(A, Key); };
	// Axis2D: X = right, Y = forward
	Mapped(MoveAction, EKeys::W).Modifiers.Add(CreateDefaultSubobject<UInputModifierSwizzleAxis>(TEXT("SwizzleW")));
	UInputModifierSwizzleAxis* SwizzleS = CreateDefaultSubobject<UInputModifierSwizzleAxis>(TEXT("SwizzleS"));
	UInputModifierNegate* NegS = CreateDefaultSubobject<UInputModifierNegate>(TEXT("NegS"));
	FEnhancedActionKeyMapping& S = Mapped(MoveAction, EKeys::S);
	S.Modifiers.Add(SwizzleS); S.Modifiers.Add(NegS);
	Mapped(MoveAction, EKeys::A).Modifiers.Add(CreateDefaultSubobject<UInputModifierNegate>(TEXT("NegA")));
	Mapped(MoveAction, EKeys::D);
	UInputModifierNegate* NegLookY = CreateDefaultSubobject<UInputModifierNegate>(TEXT("NegLookY"));
	NegLookY->bX = false; NegLookY->bZ = false; // mouse up = look up
	Mapped(LookAction, EKeys::Mouse2D).Modifiers.Add(NegLookY);
	Mapped(JumpAction, EKeys::SpaceBar);
	Mapped(FireAction, EKeys::LeftMouseButton);
	Mapped(DebrisAction, EKeys::RightMouseButton);
	Mapped(ReloadAction, EKeys::R);
	Mapped(SprintAction, EKeys::LeftShift);
}

bool ADunePlayerCharacter::IsDead() const { return Health && Health->IsDead(); }

void ADunePlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (const UDuneRunSubsystem* Run = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDuneRunSubsystem>() : nullptr)
	{
		// the upgrades of this run (Tough Hide is already in the run's max health)
		auto N = [Run](EDuneUpgrade U) { return Run->GetUpgradeStacks(U); };
		Debris->ChargeSeconds *= FMath::Pow(0.8f, N(EDuneUpgrade::QuickHands));
		Debris->DamageMultiplier = 1.f + 0.25f * N(EDuneUpgrade::SharpShards);
		Debris->HeavySpeedBonus = 0.4f * N(EDuneUpgrade::GiantHand);
		MagazineSize += 4 * N(EDuneUpgrade::LongMag);
		ReloadSeconds *= FMath::Pow(0.8f, N(EDuneUpgrade::LongMag));
		Damage *= 1.f + 0.3f * N(EDuneUpgrade::HardBullets);
	}
	Ammo = MagazineSize;
	if (UDuneRunSubsystem* Run = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDuneRunSubsystem>() : nullptr)
	{
		if (Run->IsRunActive() && Run->Health > 0.f) Health->Init(Run->Health, Run->MaxHealth);
	}
	Health->OnHealthChanged.AddDynamic(this, &ADunePlayerCharacter::OnHealthChanged);
	Health->OnDied.AddDynamic(this, &ADunePlayerCharacter::OnDied);
	BuildViewModel();
	TracerGlow = ADuneTracer::MakeGlow(this); // made now so its shader is ready before the first shot
}

void ADunePlayerCharacter::BuildViewModel()
{
	// the pistol model (imported from SourceAssets/mordern-pistol); the two boxes below are only the fallback when it is missing
	if (UStaticMesh* Pistol = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/DUNE/Weapons/Pistol/pistolmodern_2.pistolmodern_2")))
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(Pistol);
		C->SetupAttachment(Camera);
		C->SetRelativeLocation(PistolOffset);
		C->SetRelativeRotation(PistolRotation);
		C->SetRelativeScale3D(FVector(PistolScale));
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->RegisterComponent();
		ViewModel.Add(C);
		// the mannequin arms (Day 35) are off: the hand did not fit the pistol, so only the pistol is shown (-DuneFpArms switches the arms on)
		Arms = NewObject<UDuneFirstPersonArms>(this);
		Arms->RegisterComponent();
		if (FParse::Param(FCommandLine::Get(), TEXT("DuneFpArms")) && Arms->Setup(Camera, C))
		{
			// the arms' settings (UDuneFpViewConfig) decide where the pistol sits in the view: the muzzle flash follows it
			MuzzleOffset = Arms->GetMuzzleInCameraSpace(PistolMuzzleLocal);
			GunFeel->Setup(Camera, Arms->GetViewRoot(), MuzzleOffset); // the kick moves the arms and the pistol together
		}
		else
		{
			Arms->DestroyComponent();
			Arms = nullptr;
			GunFeel->Setup(Camera, C, MuzzleOffset);
		}
		return;
	}
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube) return;
	struct FPart { FVector Loc; FVector Scale; };
	const FPart Parts[] = { { FVector(38.f, 17.f, -18.f), FVector(0.55f, 0.09f, 0.11f) },   // barrel + body
							{ FVector(24.f, 17.f, -26.f), FVector(0.16f, 0.08f, 0.2f) } };  // grip
	for (const FPart& P : Parts)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(Cube);
		C->SetupAttachment(Camera);
		C->SetRelativeLocation(P.Loc);
		C->SetRelativeScale3D(P.Scale);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->RegisterComponent();
		ViewModel.Add(C);
	}
}

void ADunePlayerCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Sub->AddMappingContext(Context, 0);
		}
	}
}

void ADunePlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ADunePlayerCharacter::Move);
		Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ADunePlayerCharacter::Look);
		Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
		Input->BindAction(FireAction, ETriggerEvent::Triggered, this, &ADunePlayerCharacter::FireHeld);
		Input->BindAction(DebrisAction, ETriggerEvent::Started, this, &ADunePlayerCharacter::DebrisPress);
		Input->BindAction(DebrisAction, ETriggerEvent::Completed, this, &ADunePlayerCharacter::DebrisRelease);
		Input->BindAction(DebrisAction, ETriggerEvent::Canceled, this, &ADunePlayerCharacter::DebrisRelease);
		Input->BindAction(ReloadAction, ETriggerEvent::Started, this, &ADunePlayerCharacter::StartReload);
		Input->BindAction(SprintAction, ETriggerEvent::Started, this, &ADunePlayerCharacter::SprintOn);
		Input->BindAction(SprintAction, ETriggerEvent::Completed, this, &ADunePlayerCharacter::SprintOff);
	}
}

void ADunePlayerCharacter::SetMovementLocked(bool bLocked)
{
	bMoveLocked = bLocked;
	if (bLocked && GetCharacterMovement())
	{
		FVector V = GetCharacterMovement()->Velocity;
		GetCharacterMovement()->Velocity = FVector(0.f, 0.f, V.Z); // stands still (a fall goes on)
	}
}

void ADunePlayerCharacter::Move(const FInputActionValue& Value)
{
	if (IsDead() || bMoveLocked) return;
	const FVector2D V = Value.Get<FVector2D>();
	AddMovementInput(GetActorForwardVector(), V.Y);
	AddMovementInput(GetActorRightVector(), V.X);
}

void ADunePlayerCharacter::Look(const FInputActionValue& Value)
{
	if (IsDead()) return;
	const FVector2D V = Value.Get<FVector2D>();
	AddControllerYawInput(V.X);
	AddControllerPitchInput(V.Y);
}

void ADunePlayerCharacter::SprintOn() { GetCharacterMovement()->MaxWalkSpeed = SprintSpeed; }
void ADunePlayerCharacter::SprintOff() { GetCharacterMovement()->MaxWalkSpeed = WalkSpeed; }

void ADunePlayerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateScreenEffect(DeltaSeconds);
	const bool bHoldingPiece = Debris && Debris->GetHeld(); // the hand holds a piece instead of the pistol
	if (Arms) Arms->SetArmsVisible(!bHoldingPiece);
	for (UStaticMeshComponent* C : ViewModel) if (C && (!Arms || Arms->IsAligned())) C->SetVisibility(!bHoldingPiece); // until the arms hold it the pistol waits (hidden)
}

void ADunePlayerCharacter::UpdateScreenEffect(float DeltaSeconds)
{
	DamageFlash = FMath::Max(0.f, DamageFlash - DeltaSeconds * 2.0f);
	PulseTime += DeltaSeconds;
	float Low = 0.f;
	if (Health && Health->MaxHealth > 0.f)
	{
		const float Frac = Health->Health / Health->MaxHealth;
		if (Frac < 0.35f) Low = (1.f - Frac / 0.35f) * (0.4f + 0.2f * FMath::Sin(PulseTime * 5.f));
	}
	const float F = IsDead() ? 1.f : FMath::Clamp(FMath::Max(DamageFlash, Low), 0.f, 1.f);
	FPostProcessSettings& S = Camera->PostProcessSettings;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.35f + 1.5f * F;
	S.bOverride_SceneColorTint = true;
	S.SceneColorTint = FLinearColor(1.f, 1.f - 0.6f * F, 1.f - 0.65f * F);
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(1.f - 0.4f * F, 1.f - 0.4f * F, 1.f - 0.4f * F, 1.f);
}

void ADunePlayerCharacter::DebrisPress() { if (!IsDead()) Debris->BeginCharge(); }
void ADunePlayerCharacter::DebrisRelease() { if (!IsDead()) Debris->ReleaseCharge(); }

bool ADunePlayerCharacter::GetAimView(FVector& OutLoc, FVector& OutDir) const
{
	if (!Camera || !GetController()) return false;
	OutLoc = Camera->GetComponentLocation();
	OutDir = GetController()->GetControlRotation().Vector();
	return true;
}

bool ADunePlayerCharacter::Fire()
{
	UWorld* World = GetWorld();
	if (!World || IsDead() || bReloading) return false;
	if (World->GetTimeSeconds() - LastShotTime < FireCooldown) return false;
	if (Ammo <= 0) { StartReload(); return false; }
	LastShotTime = World->GetTimeSeconds();
	--Ammo;

	FHitResult Hit;
	FVector Loc, Dir;
	const bool bHit = TraceShot(Hit, Loc, Dir);
	const FRotator Rot = Dir.Rotation();
	GunFeel->OnShot();

	// the tracer runs from the muzzle to where the shot landed (the shot itself starts at the eye, so the two meet at the aim point)
	const FVector Muzzle = (ViewModel.Num() == 1 && ViewModel[0]) ? ViewModel[0]->GetComponentTransform().TransformPosition(PistolMuzzleLocal) // the real barrel tip, live
		: Loc + Rot.RotateVector(MuzzleOffset);                                                                                                     // fallback boxes
	const FVector End = bHit ? Hit.ImpactPoint : Loc + Dir * 8000.f;
	ADuneTracer::Fire(World, Muzzle, End, TracerGlow);
	if (bHit) DrawDebugSphere(World, Hit.ImpactPoint, 4.f, 6, FColor(255, 200, 80), false, 0.1f, 0, 0.8f);
	UE_LOG(LogTemp, Verbose, TEXT("Player shot from %s dir %s: %s"), *Loc.ToString(), *Dir.ToString(), bHit ? *GetNameSafe(Hit.GetActor()) : TEXT("miss"));
	if (bHit && Hit.GetActor())
	{
		// a structure the level allows to be broken: the bullet cracks it into pieces (the pieces can then be thrown, see UDuneDebrisLauncher)
		ASliceableStructure* Structure = Cast<ASliceableStructure>(Hit.GetActor());
		UProceduralMeshComponent* Piece = Cast<UProceduralMeshComponent>(Hit.GetComponent());
		const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
		if (Structure && Structure->bGunFracturable && Piece && Structure->IsSliceable(Piece) && !Normal.IsNearlyZero())
		{
			Structure->FractureAt(Piece, Hit.ImpactPoint, Normal, Dir);
			GunFeel->OnFracture();
			SandBurst(Structure, Hit.ImpactPoint);
		}
		else
		{
			// a hit on the head of a body (bone name with "head") counts 1.5 times
			const bool bCrit = Hit.BoneName.ToString().Contains(TEXT("head"), ESearchCase::IgnoreCase);
			const float Dealt = Damage * (bCrit ? 1.5f : 1.f);
			UGameplayStatics::ApplyPointDamage(Hit.GetActor(), Dealt, Dir, Hit, GetController(), this, UDamageType::StaticClass());
			if (const UDuneHealthComponent* Target = Hit.GetActor()->FindComponentByClass<UDuneHealthComponent>())
			{
				GunFeel->OnShotHit(Hit.ImpactPoint, Dealt, Target->IsDead(), bCrit);
			}
			else if (Hit.ImpactNormal.Z > 0.5f)
			{
				// a shot into the floor puts a small dent in the sand (nothing happens on a level without the sand patch)
				USandDeformationLibrary::ApplySandImpact(this, Hit.ImpactPoint, 16.f, 600.f, 0.5f);
			}
		}
	}
	if (Ammo <= 0) StartReload();
	return true;
}

void ADunePlayerCharacter::SandBurst(AActor* Structure, const FVector& Point)
{
	if (DuneOldFeel()) return;
	// the floor below a breaking structure: a crater and thrown-up lumps around it, as if the blast hit the sand
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DuneFractureSand), false, this);
	Params.AddIgnoredActor(Structure);
	FHitResult G;
	if (!GetWorld()->LineTraceSingleByChannel(G, Point, Point - FVector(0.f, 0.f, 2500.f), ECC_Visibility, Params) || G.ImpactNormal.Z < 0.5f) return;
	FSandInteractionRequest Crater;
	Crater.Brush = ESandBrushType::Impact;
	Crater.bAccumulate = true;
	Crater.WorldPosition = G.ImpactPoint;
	Crater.Radius = 85.f;
	Crater.ImpactSpeed = 1400.f;
	Crater.Strength = 2.f;
	Crater.RidgeScale = 3.f;
	UE_LOG(LogTemp, Log, TEXT("Fracture: sand burst at %s %s"), *G.ImpactPoint.ToCompactString(), USandDeformationLibrary::ApplySandInteraction(this, Crater) ? TEXT("dented") : TEXT("outside the sand patch"));
	for (int32 i = 0; i < 9; ++i)
	{
		const float A = FMath::FRandRange(0.f, 2.f * PI);
		const float R = FMath::FRandRange(130.f, 320.f);
		USandDeformationLibrary::ApplySandImpact(this, G.ImpactPoint + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 0.f), FMath::FRandRange(10.f, 26.f), 600.f, 1.f);
	}
}

bool ADunePlayerCharacter::TraceShot(FHitResult& OutHit, FVector& OutLoc, FVector& OutDir) const
{
	UWorld* World = GetWorld();
	if (!World || !Camera || !GetController()) return false;
	// the camera's position and the controller's rotation (the camera uses the control rotation); not the camera manager's cached view,
	// which can lag a frame behind and is not updated without a viewport
	OutLoc = Camera->GetComponentLocation();
	OutDir = GetController()->GetControlRotation().Vector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DunePlayerFire), false, this);
	bool bHit = World->LineTraceSingleByChannel(OutHit, OutLoc, OutLoc + OutDir * RangeCm, ECC_Visibility, Params);

	// aim assist: a thin sweep catches a living target the line slipped past (an edge of the body, a thin limb)
	auto Living = [](const FHitResult& H)
	{
		const UDuneHealthComponent* HP = H.GetActor() ? H.GetActor()->FindComponentByClass<UDuneHealthComponent>() : nullptr;
		return HP && !HP->IsDead();
	};
	if (AimAssistRadiusCm > 0.f && (!bHit || !Living(OutHit)))
	{
		FHitResult Sweep;
		if (World->SweepSingleByChannel(Sweep, OutLoc, OutLoc + OutDir * RangeCm, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(AimAssistRadiusCm), Params)
			&& Living(Sweep) && (!bHit || Sweep.Distance <= OutHit.Distance))
		{
			OutHit = Sweep;
			bHit = true;
		}
	}
	return bHit;
}

void ADunePlayerCharacter::StartReload()
{
	if (bReloading || Ammo >= MagazineSize || IsDead()) return;
	bReloading = true;
	GetWorldTimerManager().SetTimer(ReloadTimer, this, &ADunePlayerCharacter::FinishReload, ReloadSeconds, false);
}

void ADunePlayerCharacter::FinishReload()
{
	Ammo = MagazineSize;
	bReloading = false;
}

void ADunePlayerCharacter::HandleHealthChanged(float NewHealth, float NewMax, float Delta)
{
	if (Delta < 0.f)
	{
		DamageFlash = FMath::Clamp(DamageFlash + (-Delta) / 50.f + 0.15f, 0.25f, 1.f); // 12 damage ~ 0.4, a 40 damage hit ~ 0.95
		AddControllerYawInput(FMath::FRandRange(-0.8f, 0.8f)); // a small kick of the view
		AddControllerPitchInput(FMath::FRandRange(-0.6f, 0.2f));
	}
	if (UDuneRunSubsystem* Run = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDuneRunSubsystem>() : nullptr)
	{
		Run->Health = NewHealth;
		Run->MaxHealth = NewMax;
	}
}

void ADunePlayerCharacter::OnDied()
{
	GetCharacterMovement()->DisableMovement();
}
