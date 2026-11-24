#include "Dune/Combat/DuneFirstPersonArms.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogDuneArms, Log, All);

// ---------------------------------------------------------------- the proxy: blends the animations the instance picked

struct FDuneArmsProxy : public FAnimInstanceProxy
{
	FDuneArmsProxy() = default;
	explicit FDuneArmsProxy(UAnimInstance* InInstance) : FAnimInstanceProxy(InInstance) {}

	struct FSample { const UAnimSequence* Seq = nullptr; float Time = 0.f; bool bLoop = true; };
	FSample A;
	FSample B;
	float AlphaB = 0.f;      // 0 = only A, 1 = only B
	float FadeAlpha = 1.f;   // < 1: cross-fading from the pose of the moment the state changed
	bool bSnapshot = false;  // take that pose now
	TArray<FTransform> Last;
	TArray<FTransform> FadeFrom;
	const UAnimSequence* BaseSeq = nullptr; // the pose the others are pulled towards (the first frame of the idle)
	TArray<FTransform> Base;
	float Strength = 1.f;                   // 1 = the animation as it is, 0 = only the base pose
	int32 HeadBone = INDEX_NONE;      // mesh bone index of the head (set by the instance)
	FTransform HeadTarget = FTransform::Identity;
	bool bHeadTargetSet = false;

	virtual bool Evaluate(FPoseContext& Output) override
	{
		const int32 N = Output.Pose.GetNumBones();
		auto Sample = [&](const FSample& S, TArray<FTransform>& Out)
		{
			FPoseContext Tmp(this);
			Tmp.ResetToRefPose();
			if (S.Seq)
			{
				FAnimationPoseData Data(Tmp);
				S.Seq->GetAnimationPose(Data, FAnimExtractContext(static_cast<double>(S.Time), false, FDeltaTimeRecord(), S.bLoop));
			}
			Out.SetNumUninitialized(N);
			for (int32 i = 0; i < N; ++i) Out[i] = Tmp.Pose[FCompactPoseBoneIndex(i)];
		};
		auto BlendInto = [&](TArray<FTransform>& Dest, const TArray<FTransform>& Other, float Alpha)
		{
			for (int32 i = 0; i < N; ++i)
			{
				FTransform T;
				T.Blend(Dest[i], Other[i], Alpha);
				Dest[i] = T;
			}
		};
		TArray<FTransform> Pose;
		Sample(A, Pose);
		if (B.Seq && AlphaB > 0.001f)
		{
			TArray<FTransform> PoseB;
			Sample(B, PoseB);
			BlendInto(Pose, PoseB, AlphaB);
		}
		// the animations were made to be seen from outside: the hands of a run swing low and wide. In the view the pose is pulled towards the idle pose
		if (Strength < 0.999f && BaseSeq)
		{
			if (Base.Num() != N) Sample(FSample{ BaseSeq, 0.f, false }, Base);
			TArray<FTransform> Damped = Base;
			BlendInto(Damped, Pose, Strength);
			Pose = Damped;
		}
		// the camera sits at the head: the root bone is moved so that the head stays where it was in the first pose. The leaning and bobbing of the body then
		// do not carry the hands away (and the animations that walk away with their root stay in place)
		const FBoneContainer& BC = Output.Pose.GetBoneContainer();
		const FCompactPoseBoneIndex HeadIndex = HeadBone != INDEX_NONE ? BC.MakeCompactPoseIndex(FMeshPoseBoneIndex(HeadBone)) : FCompactPoseBoneIndex(INDEX_NONE);
		if (HeadIndex.IsValid())
		{
			FTransform HeadRel = FTransform::Identity; // the head relative to the root bone
			for (FCompactPoseBoneIndex I = HeadIndex; I.GetInt() > 0; I = BC.GetParentBoneIndex(I)) HeadRel = HeadRel * Pose[I.GetInt()];
			if (!bHeadTargetSet)
			{
				HeadTarget = HeadRel * BC.GetRefPoseTransform(FCompactPoseBoneIndex(0));
				bHeadTargetSet = true;
			}
			Pose[0] = HeadRel.Inverse() * HeadTarget;
		}
		else
		{
			Pose[0] = BC.GetRefPoseTransform(FCompactPoseBoneIndex(0));
		}
		if (bSnapshot)
		{
			FadeFrom = Last;
			bSnapshot = false;
		}
		if (FadeAlpha < 1.f && FadeFrom.Num() == N)
		{
			TArray<FTransform> From = FadeFrom;
			BlendInto(From, Pose, FadeAlpha);
			Pose = From;
		}
		Last = Pose;
		for (int32 i = 0; i < N; ++i) Output.Pose[FCompactPoseBoneIndex(i)] = Pose[i];
		return true;
	}
};

FAnimInstanceProxy* UDuneArmsAnimInstance::CreateAnimInstanceProxy()
{
	return new FDuneArmsProxy(this);
}

void UDuneArmsAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FDuneArmsProxy*>(InProxy);
}

void UDuneArmsAnimInstance::Enter(EState NewState, float FadeSeconds)
{
	State = NewState;
	StateTime = 0.f;
	FadeTime = 0.f;
	FadeDuration = FMath::Max(FadeSeconds, 0.01f);
	GetProxyOnGameThread<FDuneArmsProxy>().bSnapshot = true;
}

void UDuneArmsAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	FDuneArmsProxy& P = GetProxyOnGameThread<FDuneArmsProxy>();
	if (P.HeadBone == INDEX_NONE && GetSkelMeshComponent()) P.HeadBone = GetSkelMeshComponent()->GetBoneIndex(HeadBoneName);
	const ACharacter* C = Cast<ACharacter>(TryGetPawnOwner());
	const float Speed = TestSpeed >= 0.f ? TestSpeed : (C ? C->GetVelocity().Size2D() : 0.f);
	const float VZ = C ? C->GetVelocity().Z : 0.f;
	const bool bAir = bTestAir || (C && C->GetCharacterMovement() && C->GetCharacterMovement()->IsFalling());
	StateTime += DeltaSeconds;

	// the state machine
	switch (State)
	{
	case EState::Ground:
	case EState::Break:
		if (bAir) { Enter(VZ > 150.f || bTestAir ? EState::JumpStart : EState::Fall, 0.12f); break; }
		if (State == EState::Ground)
		{
			IdleTime = Speed < 10.f ? IdleTime + DeltaSeconds : 0.f;
			if ((IdleTime > IdleBreakAfter || TestBreak >= 0) && Breaks.Num() > 0)
			{
				BreakIndex = TestBreak >= 0 ? TestBreak % Breaks.Num() : FMath::RandRange(0, Breaks.Num() - 1);
				TestBreak = -1;
				IdleTime = 0.f;
				Enter(EState::Break, 0.3f);
			}
		}
		else if (Speed > 30.f || !Breaks.IsValidIndex(BreakIndex) || StateTime >= Breaks[BreakIndex]->GetPlayLength() - 0.25f)
		{
			Enter(EState::Ground, 0.3f);
		}
		break;
	case EState::JumpStart:
		if (!bAir) { LandSeq = Speed > 300.f ? LandRun : LandStand; Enter(EState::Land, 0.1f); }
		else if (StateTime > 0.55f) Enter(EState::Fall, 0.25f);
		break;
	case EState::Fall:
		if (!bAir) { LandSeq = Speed > 300.f ? LandRun : LandStand; Enter(EState::Land, 0.1f); }
		break;
	case EState::Land:
		if (bAir) Enter(EState::Fall, 0.12f);
		else if (StateTime > 0.5f) Enter(EState::Ground, 0.3f);
		break;
	}

	// what the proxy blends this frame
	switch (State)
	{
	case EState::Ground:
	{
		struct FPoint { float Speed; UAnimSequence* Seq; };
		const FPoint Points[4] = { { 0.f, Idle }, { 250.f, Walk }, { 620.f, Run }, { 920.f, Sprint } };
		int32 Seg = 0;
		while (Seg < 2 && Speed > Points[Seg + 1].Speed) ++Seg;
		const float Span = Points[Seg + 1].Speed - Points[Seg].Speed;
		const float Alpha = FMath::Clamp((Speed - Points[Seg].Speed) / Span, 0.f, 1.f);
		const UAnimSequence* SeqA = Points[Seg].Seq;
		const UAnimSequence* SeqB = Points[Seg + 1].Seq;
		const float LenA = SeqA ? SeqA->GetPlayLength() : 1.f;
		const float LenB = SeqB ? SeqB->GetPlayLength() : 1.f;
		Phase = FMath::Fmod(Phase + DeltaSeconds / FMath::Lerp(LenA, LenB, Alpha), 1.f); // one shared phase: the steps of two clips stay together
		P.A = { SeqA, Phase * LenA, true };
		P.B = { SeqB, Phase * LenB, true };
		P.AlphaB = Alpha;
		break;
	}
	case EState::Break:
		P.A = { Breaks.IsValidIndex(BreakIndex) ? Breaks[BreakIndex].Get() : Idle.Get(), StateTime, false };
		P.B = {};
		P.AlphaB = 0.f;
		break;
	case EState::JumpStart:
		P.A = { JumpStart, StateTime * 1.5f, false };
		P.B = {};
		P.AlphaB = 0.f;
		break;
	case EState::Fall:
		P.A = { Fall, Fall ? FMath::Fmod(StateTime, FMath::Max(Fall->GetPlayLength(), 0.1f)) : 0.f, true };
		P.B = {};
		P.AlphaB = 0.f;
		break;
	case EState::Land:
		P.A = { LandSeq, StateTime * 2.f, false };
		P.B = {};
		P.AlphaB = 0.f;
		break;
	}
	P.BaseSeq = Idle;
	P.Strength = State == EState::Ground ? FMath::Lerp(1.f, MoveStrength, FMath::Clamp(Speed / 620.f, 0.f, 1.f)) : (State == EState::Break ? 0.85f : AirStrength);
	FadeTime = FMath::Min(FadeTime + DeltaSeconds / FadeDuration, 1.f);
	P.FadeAlpha = FadeTime * FadeTime * (3.f - 2.f * FadeTime);
}

// ---------------------------------------------------------------- the component

UDuneFirstPersonArms::UDuneFirstPersonArms()
{
	PrimaryComponentTick.bCanEverTick = true;
}

UDuneArmsAnimInstance* UDuneFirstPersonArms::GetAnim() const
{
	return Arms ? Cast<UDuneArmsAnimInstance>(Arms->GetAnimInstance()) : nullptr;
}

void UDuneFirstPersonArms::SetArmsVisible(bool bVisible)
{
	if (Arms) Arms->SetVisibility(bVisible && bAligned);
}

UDuneFpViewConfig::UDuneFpViewConfig()
{
	auto Path = [](const TCHAR* P) { return FSoftObjectPath(P); };
	ArmsMesh = TSoftObjectPtr<USkeletalMesh>(Path(TEXT("/Game/DUNE/Player/FP/Mesh/SKM_FP_Mannequin.SKM_FP_Mannequin")));
	ArmsMaterial = TSoftObjectPtr<UMaterialInterface>(Path(TEXT("/Game/DUNE/Player/FP/M_FP_ArmsMasked.M_FP_ArmsMasked")));
	auto Anim = [&](const TCHAR* Name) { return TSoftObjectPtr<UAnimSequence>(Path(*FString::Printf(TEXT("/Game/DUNE/Player/FP/Anims/%s.%s"), Name, Name))); };
	Idle = Anim(TEXT("A_FP_Idle"));
	Walk = Anim(TEXT("A_FP_Walk"));
	Run = Anim(TEXT("A_FP_Run"));
	Sprint = Anim(TEXT("A_FP_Sprint"));
	JumpStart = Anim(TEXT("A_FP_JumpStart"));
	Fall = Anim(TEXT("A_FP_Fall"));
	LandStand = Anim(TEXT("A_FP_LandStand"));
	LandRun = Anim(TEXT("A_FP_LandRun"));
	IdleBreaks = { Anim(TEXT("A_FP_IdleBreak1")), Anim(TEXT("A_FP_IdleBreak2")), Anim(TEXT("A_FP_IdleBreak3")) };
	HideBones = { TEXT("head"), TEXT("clavicle_l") };
}

bool UDuneFirstPersonArms::Setup(UCameraComponent* InCamera, UStaticMeshComponent* InPistol)
{
	AActor* Owner = GetOwner();
	// the settings: the data asset the user can edit (swap the mesh and the animations, move the pistol), or the defaults of the class
	Config = LoadObject<UDuneFpViewConfig>(nullptr, TEXT("/Game/DUNE/Player/FP/DA_FP_View.DA_FP_View"));
	if (!Config) Config = GetMutableDefault<UDuneFpViewConfig>();
	USkeletalMesh* Mesh = Config->ArmsMesh.LoadSynchronous();
	UAnimSequence* Idle = Config->Idle.LoadSynchronous();
	UAnimSequence* Walk = Config->Walk.LoadSynchronous();
	UAnimSequence* Run = Config->Run.LoadSynchronous();
	UAnimSequence* Sprint = Config->Sprint.LoadSynchronous();
	if (!Owner || !InCamera || !Mesh || !Idle || !Walk || !Run || !Sprint)
	{
		UE_LOG(LogDuneArms, Warning, TEXT("First-person arms: mesh or animations missing (mesh %d, idle %d, walk %d, run %d, sprint %d): the pistol stays on the camera"), Mesh != nullptr, Idle != nullptr, Walk != nullptr, Run != nullptr, Sprint != nullptr);
		return false;
	}
	Pistol = InPistol;
	CameraRef = InCamera;
	PistolRot = Config->PistolViewRotation;
	PistolScaleValue = Config->PistolScale;
	GripLocal = Config->GripLocal;
	GripRotation = Config->GripRotation;
	ArmsYawDeg = Config->ArmsYawDeg;
	PistolOffset = Config->PistolViewOffset;

	// -DuneFpGrip=x;y;z / -DuneFpPistol=x;y;z / -DuneFpYaw=deg / -DuneFpGripRot=p;y;r: tried without a build
	auto ParseVec = [](const TCHAR* Key, FVector& Out)
	{
		FString Text;
		if (!FParse::Value(FCommandLine::Get(), Key, Text)) return;
		TArray<FString> V; Text.ParseIntoArray(V, TEXT(";"));
		if (V.Num() == 3) Out = FVector(FCString::Atof(*V[0]), FCString::Atof(*V[1]), FCString::Atof(*V[2]));
	};
	ParseVec(TEXT("DuneFpGrip="), GripLocal);
	ParseVec(TEXT("DuneFpPistol="), PistolOffset);
	FVector GripRotVec(GripRotation.Pitch, GripRotation.Yaw, GripRotation.Roll);
	ParseVec(TEXT("DuneFpGripRot="), GripRotVec);
	GripRotation = FRotator(GripRotVec.X, GripRotVec.Y, GripRotVec.Z);
	FParse::Value(FCommandLine::Get(), TEXT("DuneFpYaw="), ArmsYawDeg);

	ViewRoot = NewObject<USceneComponent>(Owner, TEXT("FpViewRoot"));
	ViewRoot->SetupAttachment(InCamera);
	ViewRoot->SetRelativeLocation(PistolOffset); // the gun kick turns the view root about the pistol
	ViewRoot->RegisterComponent();

	Arms = NewObject<USkeletalMeshComponent>(Owner, TEXT("FpArms"));
	Arms->SetSkeletalMesh(Mesh);
	Arms->SetupAttachment(ViewRoot);
	Arms->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Arms->SetCastShadow(false);
	Arms->bReceivesDecals = false;
	Arms->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Arms->SetAnimInstanceClass(UDuneArmsAnimInstance::StaticClass());
	Arms->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Arms->RegisterComponent();
	Arms->SetVisibility(false); // until it is aligned with the pistol

	UDuneArmsAnimInstance* Anim = GetAnim();
	if (!Anim)
	{
		UE_LOG(LogDuneArms, Error, TEXT("First-person arms: the animation instance was not created"));
		return false;
	}
	Anim->Idle = Idle;
	Anim->Walk = Walk;
	Anim->Run = Run;
	Anim->Sprint = Sprint;
	Anim->JumpStart = Config->JumpStart.LoadSynchronous();
	Anim->Fall = Config->Fall.LoadSynchronous();
	Anim->LandStand = Config->LandStand.LoadSynchronous();
	Anim->LandRun = Config->LandRun.LoadSynchronous();
	for (const TSoftObjectPtr<UAnimSequence>& B : Config->IdleBreaks)
	{
		if (UAnimSequence* Seq = B.LoadSynchronous()) Anim->Breaks.Add(Seq);
	}
	Anim->IdleBreakAfter = Config->IdleBreakAfter;
	Anim->MoveStrength = Config->MoveStrength;
	Anim->AirStrength = Config->AirStrength;
	Anim->HeadBoneName = Config->HeadBone;

	// leather gloves and sleeves: the mesh came without materials
	UMaterialInterface* Base = Config->ArmsMaterial.LoadSynchronous(); // has the skeletal mesh usage flag
	if (!Base) Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (Base)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, this);
		ArmsMat = M;
		M->SetVectorParameterValue(TEXT("Color"), FParse::Param(FCommandLine::Get(), TEXT("DuneFpBright")) ? FLinearColor(0.9f, 0.5f, 0.2f, 1.f) : Config->SleeveColor); // -DuneFpBright: to find the arms in a picture
		for (int32 i = 0; i < Arms->GetNumMaterials(); ++i) Arms->SetMaterial(i, M);
	}

	// the camera sits in the head: hide it (hiding a bone hides everything below it, so the arms, which hang below the spine, cannot be shown alone; the
	// body is below the view anyway)
	if (!FParse::Param(FCommandLine::Get(), TEXT("DuneFpAllBones"))) // -DuneFpAllBones: the whole mannequin
	{
		for (const FName& Bone : Config->HideBones) Arms->HideBoneByName(Bone, PBO_None);
	}
	UE_LOG(LogDuneArms, Log, TEXT("First-person arms: %d bones, %s bone index %d, idle %.1f s, grip %s, pistol at %s, arms yaw %.1f"),
		Arms->GetNumBones(), *Config->HandBone.ToString(), Arms->GetBoneIndex(Config->HandBone), Idle->GetPlayLength(), *GripLocal.ToString(), *PistolOffset.ToString(), ArmsYawDeg);
	if (Pistol) Pistol->SetVisibility(false);

	if (FParse::Param(FCommandLine::Get(), TEXT("DuneFpTestBreak"))) Anim->TestBreak = 0;
	return true;
}

FVector UDuneFirstPersonArms::GetMuzzleInCameraSpace(const FVector& MuzzleLocalInPistol) const
{
	return PistolOffset + PistolRot.RotateVector(MuzzleLocalInPistol * PistolScaleValue);
}

void UDuneFirstPersonArms::AlignToPistol()
{
	if (!Arms || !ViewRoot || !Config) return;
	const FName HandBone = Config->HandBone;
	const int32 HandIndex = Arms->GetBoneIndex(HandBone);
	if (HandIndex == INDEX_NONE)
	{
		UE_LOG(LogDuneArms, Error, TEXT("First-person arms: no %s bone, the pistol stays where it is"), *HandBone.ToString());
		bAligned = true;
		Arms->SetVisibility(false);
		if (Pistol) Pistol->SetVisibility(true);
		return;
	}
	const FTransform HandComp = Arms->GetBoneTransform(HandIndex, FTransform::Identity); // in component space
	const FTransform RootW = ViewRoot->GetComponentTransform();
	// where the pistol is in the view (centred, pointing along the view), turned a little in the hand with GripRotation
	const FQuat PistolQ = PistolRot.Quaternion() * GripRotation.Quaternion();
	const FTransform PistolDesired = FTransform(PistolQ, FVector::ZeroVector, FVector(PistolScaleValue)) * RootW;
	const FVector GripWorld = PistolDesired.TransformPosition(GripLocal);
	// the body is turned about the vertical through the hand so that the forearm points along the view (the animations hold the gun out in front of a body that
	// faces forward; with the pistol moved to the middle of the view the arm would cross the picture at an angle). ArmsYawDeg is added for fine tuning.
	float Yaw = ArmsYawDeg;
	if (Config->bStraightenForearm)
	{
		const int32 ElbowIndex = Arms->GetBoneIndex(Config->ElbowBone);
		if (ElbowIndex != INDEX_NONE)
		{
			const FVector Forearm = HandComp.GetLocation() - Arms->GetBoneTransform(ElbowIndex, FTransform::Identity).GetLocation();
			Yaw += -FMath::RadiansToDegrees(FMath::Atan2(Forearm.Y, Forearm.X)); // the mesh faces +X
		}
	}
	AppliedYaw = Yaw;
	const FQuat BodyQ = RootW.GetRotation() * FRotator(0.f, Yaw, 0.f).Quaternion();
	Arms->SetWorldLocationAndRotation(GripWorld - BodyQ.RotateVector(HandComp.GetLocation()), BodyQ);
	if (Pistol)
	{
		Pistol->SetWorldTransform(PistolDesired);
		Pistol->AttachToComponent(Arms, FAttachmentTransformRules::KeepWorldTransform, HandBone);
		Pistol->SetVisibility(true);
	}
	Arms->SetVisibility(true);
	bAligned = true;
	if (FParse::Param(FCommandLine::Get(), TEXT("DuneFpBones"))) // -DuneFpBones: the bones in camera space (to place the pistol)
	{
		for (const FName& B : { Config->HeadBone, Config->HandBone, Config->ElbowBone, FName(TEXT("upperarm_r")), FName(TEXT("clavicle_r")), FName(TEXT("spine_03")) })
		{
			const FVector W = Arms->GetBoneLocation(B);
			UE_LOG(LogDuneArms, Log, TEXT("First-person arms: bone %s in camera space %s"), *B.ToString(), *CameraRef->GetComponentTransform().InverseTransformPosition(W).ToString());
		}
	}
	UE_LOG(LogDuneArms, Log, TEXT("First-person arms: aligned, body yaw %.1f deg, pistol in camera space %s, visible %d"), AppliedYaw, *PistolOffset.ToString(), Arms->IsVisible());
}

void UDuneFirstPersonArms::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	++TickCount;
	if (!bAligned && TickCount >= 4) AlignToPistol();
	if (ArmsMat && CameraRef && Config)
	{
		// the cut plane: everything of the arms farther left than ClipLeftCm of the view is not drawn (the stump of the left shoulder)
		const FVector Right = CameraRef->GetRightVector();
		ArmsMat->SetVectorParameterValue(TEXT("PlanePoint"), FLinearColor(CameraRef->GetComponentLocation() - Right * (Config ? Config->ClipLeftCm : 14.f)));
		ArmsMat->SetVectorParameterValue(TEXT("PlaneNormal"), FLinearColor(Right));
	}
	Clock += DeltaTime;
	static const bool bShots = FParse::Param(FCommandLine::Get(), TEXT("DuneFpShot"));
	if (bShots && bAligned) RunSelfTest(Clock);
}

void UDuneFirstPersonArms::RunSelfTest(float Time)
{
	// -DuneFpShot: idle, run, sprint, in the air, landing (screenshots), then quit
	UDuneArmsAnimInstance* Anim = GetAnim();
	if (!Anim) return;
	auto Shot = [&](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(Name, true, false); };
	auto Once = [&](int32 Index, float At, TFunction<void()> Do) { if (TestStep == Index && Time >= At) { Do(); ++TestStep; } };
	Once(0, 2.5f, [&]() { Shot(TEXT("Fp_1_idle.png")); });
	Once(1, 3.0f, [&]() { Anim->TestSpeed = 620.f; });
	Once(2, 4.2f, [&]() { Shot(TEXT("Fp_2_run.png")); });
	Once(3, 4.5f, [&]() { Anim->TestSpeed = 920.f; });
	Once(4, 5.7f, [&]() { Shot(TEXT("Fp_3_sprint.png")); });
	Once(5, 6.0f, [&]() { Anim->TestSpeed = 0.f; Anim->bTestAir = true; });
	Once(6, 6.9f, [&]() { Shot(TEXT("Fp_4_air.png")); });
	Once(7, 7.2f, [&]() { Anim->bTestAir = false; });
	Once(8, 7.5f, [&]() { Shot(TEXT("Fp_5_land.png")); });
	Once(9, 8.8f, [&]() { Shot(TEXT("Fp_6_idle_again.png")); });
	Once(10, 9.6f, []() { FPlatformMisc::RequestExit(false); });
}
