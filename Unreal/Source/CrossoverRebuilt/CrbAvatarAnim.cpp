#include "CrbAvatarAnim.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"

const TCHAR* CrbClipName(ECrbClip C)
{
	static const TCHAR* Names[] = { TEXT("Idle"), TEXT("WalkF"), TEXT("WalkB"), TEXT("WalkL"), TEXT("WalkR"), TEXT("RunF"), TEXT("RunB"),
		TEXT("JogL"), TEXT("JogR"), TEXT("Attack"), TEXT("Death"), TEXT("EnemyIdle"), TEXT("Swipe"), TEXT("EnemyDeath") };
	static_assert(UE_ARRAY_COUNT(Names) == (int32)ECrbClip::Count, "clip names");
	return (int32)C < (int32)ECrbClip::Count ? Names[(int32)C] : TEXT("");
}

void UCrbAvatarAnimInstance::SetClips(UAnimSequence* const InClips[(int32)ECrbClip::Count])
{
	ClipRefs.SetNum((int32)ECrbClip::Count);
	for (int32 I = 0; I < (int32)ECrbClip::Count; ++I) ClipRefs[I] = InClips[I];
}

void FCrbAvatarAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	UCrbAvatarAnimInstance* A = CastChecked<UCrbAvatarAnimInstance>(InAnimInstance);
	Debug.ClipsLoaded = 0;
	for (int32 I = 0; I < (int32)ECrbClip::Count; ++I)
	{
		Clips[I] = A->ClipRefs.IsValidIndex(I) ? A->ClipRefs[I] : nullptr;
		if (Clips[I]) ++Debug.ClipsLoaded;
	}
	In = A->Inputs;
}

void FCrbAvatarAnimProxy::PostUpdate(UAnimInstance* InAnimInstance) const
{
	FAnimInstanceProxy::PostUpdate(InAnimInstance);
	CastChecked<UCrbAvatarAnimInstance>(InAnimInstance)->Debug = Debug;
}

float FCrbAvatarAnimProxy::Len(ECrbClip C) const
{
	const UAnimSequence* S = Clips[(int32)C];
	return S ? FMath::Max(0.05f, S->SequenceLength) : 1.f;
}

void FCrbAvatarAnimProxy::Update(float Dt)
{
	FAnimInstanceProxy::Update(Dt);
	Dt = FMath::Clamp(Dt, 0.f, 0.1f);

	// ---- one-shots (Animator triggers) ----
	if (!In.bDead)
	{
		if (In.AttackSerial != Last.AttackSerial && OneShot == ECrbClip::Count) { OneShot = ECrbClip::Attack; OneShotTime = 0; OneShotRate = UCrbAvatarAnimInstance::AttackRate; }
		if (In.ThrowSerial != Last.ThrowSerial) { OneShot = ECrbClip::Attack; OneShotTime = 0; OneShotRate = UCrbAvatarAnimInstance::ThrowRate; }
		if (In.SwipeSerial != Last.SwipeSerial && OneShot == ECrbClip::Count) { OneShot = ECrbClip::Swipe; OneShotTime = 0; OneShotRate = 1.f; }
	}
	Last = In;
	if (OneShot != ECrbClip::Count)
	{
		OneShotTime += Dt * OneShotRate;
		if (OneShotTime >= Len(OneShot) || In.bDead) OneShot = ECrbClip::Count;
	}

	// ---- movement blend (PlayerAnimationManager: MoveTowards toward the local move direction * speed/maxSpeed) ----
	const float WalkSpeed = 4.3f; // vanilla walking speed, blocks/s (the repo's maxSpeed 5 m/s)
	FVector2D Target(In.LocalRight, In.LocalForward);
	Target /= WalkSpeed;
	if (Target.Size() > 1.f) Target.Normalize();
	if (!In.bGrounded || In.bFlying || In.bEnemy) Target = FVector2D::ZeroVector;
	const FVector2D D = Target - AnimDir;
	const float Step = 10.f * Dt;
	AnimDir = D.Size() <= Step ? Target : AnimDir + D.GetSafeNormal() * Step;
	const float Speed = FVector2D(In.LocalRight, In.LocalForward).Size();
	RunW = FMath::FInterpTo(RunW, In.bGrounded && (In.bSprint || Speed > 4.9f) ? 1.f : 0.f, Dt, 6.f);
	// One shared normalised phase keeps the directional clips' foot cycles aligned while blending.
	const float CycleLen = FMath::Lerp(Len(ECrbClip::WalkF), Len(ECrbClip::RunF), RunW);
	const float Rate = FMath::Clamp(Speed / FMath::Lerp(WalkSpeed, 5.6f, RunW), 0.4f, 1.6f);
	Phase = FMath::Fmod(Phase + Dt * Rate / FMath::Max(0.1f, CycleLen), 1.f);
	IdleTime += Dt;
	AirW = FMath::FInterpTo(AirW, (!In.bGrounded && !In.bFlying) ? 1.f : 0.f, Dt, 8.f);
	DeathTime = In.bDead ? DeathTime + Dt : 0.f;

	Debug.Base = In.bDead ? TEXT("Death") : In.bEnemy ? TEXT("EnemyIdle") : In.bFlying ? TEXT("Fly") : AirW > 0.5f ? TEXT("Air")
		: AnimDir.Size() < 0.2f ? TEXT("Idle") : RunW > 0.5f ? TEXT("Run") : TEXT("Walk");
	Debug.OneShot = OneShot == ECrbClip::Count ? FString() : OneShot == ECrbClip::Attack && OneShotRate != UCrbAvatarAnimInstance::AttackRate ? FString(TEXT("Throw")) : FString(CrbClipName(OneShot));
	Debug.DirX = AnimDir.X; Debug.DirZ = AnimDir.Y; Debug.RunW = RunW; Debug.OneShotTime = OneShotTime;
}

void FCrbAvatarAnimProxy::Sample(ECrbClip C, float Time, bool bLoop, FPoseContext& Out) const
{
	USkeleton* Mine = const_cast<FCrbAvatarAnimProxy*>(this)->GetSkeleton();
	auto Usable = [Mine](const UAnimSequence* X) { return X && X->GetSkeleton() == Mine; }; // player and enemy clips live on different skeletons
	const UAnimSequence* S = Clips[(int32)C];
	if (!Usable(S)) S = Clips[(int32)ECrbClip::Idle];
	if (!Usable(S)) { Out.ResetToRefPose(); return; }
	const float L = FMath::Max(0.05f, S->SequenceLength);
	const float T = bLoop ? FMath::Fmod(FMath::Max(0.f, Time), L) : FMath::Clamp(Time, 0.f, L);
	FAnimationPoseData Data(Out);
	S->GetAnimationPose(Data, FAnimExtractContext(T, false));
}

bool FCrbAvatarAnimProxy::Evaluate(FPoseContext& Output)
{
	++Debug.Evaluations;
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	const FReferenceSkeleton& Ref = Bones.GetReferenceSkeleton();
	auto Find = [&](const TCHAR* Base, const TCHAR* Epic)
	{
		for (const FString& N : { FString(Epic), FString(Base), FString(TEXT("mixamorig_")) + Base, FString(TEXT("mixamorig:")) + Base })
		{
			const int32 I = Ref.FindBoneIndex(FName(*N));
			if (I != INDEX_NONE) return I;
		}
		return (int32)INDEX_NONE;
	};
	const int32 HipsRef = Find(TEXT("Hips"), TEXT("pelvis")), SpineRef = Find(TEXT("Spine"), TEXT("spine_01"));
	TArray<float, TInlineAllocator<96>> Upper; Upper.SetNumZeroed(Output.Pose.GetNumBones());
	for (FCompactPoseBoneIndex I : Output.Pose.ForEachBoneIndex())
	{
		int32 R = Bones.MakeMeshPoseIndex(I).GetInt();
		while (R != INDEX_NONE) { if (R == SpineRef) { Upper[I.GetInt()] = 1.f; break; } R = Ref.GetParentIndex(R); }
	}
	auto Blend = [&](FPoseContext& Dst, const FPoseContext& Src, float W, bool bUpperOnly)
	{
		if (W <= KINDA_SMALL_NUMBER) return;
		for (FCompactPoseBoneIndex I : Dst.Pose.ForEachBoneIndex())
		{
			const float BW = bUpperOnly ? W * Upper[I.GetInt()] : W;
			if (BW > 0.f) Dst.Pose[I].BlendWith(Src.Pose[I], FMath::Min(BW, 1.f));
		}
	};

	FPoseContext Tmp(Output);
	if (In.bEnemy)
	{
		Sample(ECrbClip::EnemyIdle, IdleTime, true, Output);
	}
	else
	{
		// 2D directional blend: idle at the centre, F/B/L/R at the unit positions (walk set, or run/jog set when sprinting).
		Sample(ECrbClip::Idle, IdleTime, true, Output);
		const float X = AnimDir.X, Z = AnimDir.Y;
		const float Mag = FMath::Min(1.f, FMath::Abs(X) + FMath::Abs(Z));
		if (Mag > 0.01f)
		{
			const float Sum = FMath::Abs(X) + FMath::Abs(Z);
			struct FDirW { ECrbClip Walk, Run; float W; };
			const FDirW Dirs[4] = {
				{ ECrbClip::WalkF, ECrbClip::RunF, FMath::Max(0.f, Z) / Sum }, { ECrbClip::WalkB, ECrbClip::RunB, FMath::Max(0.f, -Z) / Sum },
				{ ECrbClip::WalkR, ECrbClip::JogR, FMath::Max(0.f, X) / Sum }, { ECrbClip::WalkL, ECrbClip::JogL, FMath::Max(0.f, -X) / Sum } };
			// Accumulate the directional mix into Move, then blend it over idle by the input magnitude.
			FPoseContext Move(Output);
			bool bFirst = true; float Acc = 0;
			for (const FDirW& Dw : Dirs)
			{
				if (Dw.W <= 0.001f) continue;
				Sample(Dw.Walk, Phase * Len(Dw.Walk), true, Tmp);
				if (RunW > 0.01f) { FPoseContext RunPose(Output); Sample(Dw.Run, Phase * Len(Dw.Run), true, RunPose); Blend(Tmp, RunPose, RunW, false); }
				if (bFirst) { Move.Pose.CopyBonesFrom(Tmp.Pose); bFirst = false; Acc = Dw.W; }
				else { Acc += Dw.W; Blend(Move, Tmp, Dw.W / Acc, false); }
			}
			if (!bFirst) Blend(Output, Move, Mag, false);
		}
	}
	if (OneShot != ECrbClip::Count)
	{
		const float L = Len(OneShot);
		const float W = FMath::Clamp(FMath::Min(OneShotTime / 0.1f, (L - OneShotTime) / 0.2f), 0.f, 1.f);
		Sample(OneShot, OneShotTime, false, Tmp);
		Blend(Output, Tmp, W, AnimDir.Size() > 0.2f || AirW > 0.5f); // upper body only while moving
	}
	if (DeathTime > 0) { Sample(In.bEnemy && Clips[(int32)ECrbClip::EnemyDeath] ? ECrbClip::EnemyDeath : ECrbClip::Death, DeathTime, false, Tmp); Blend(Output, Tmp, FMath::Clamp(DeathTime / 0.2f, 0.f, 1.f), false); }

	// In place: pin the Hips' horizontal translation to the reference pose (dominant reference component = up axis).
	const FCompactPoseBoneIndex Hips = HipsRef == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(HipsRef));
	if (Hips.GetInt() != INDEX_NONE && Output.Pose.IsValidIndex(Hips) && DeathTime <= 0)
	{
		const FVector RefT = Output.Pose.GetRefPose(Hips).GetTranslation();
		FVector T = Output.Pose[Hips].GetTranslation();
		const FVector A = RefT.GetAbs();
		const int32 Up = A.Z >= A.X && A.Z >= A.Y ? 2 : (A.Y >= A.X ? 1 : 0);
		for (int32 K = 0; K < 3; ++K) if (K != Up) T[K] = RefT[K];
		Output.Pose[Hips].SetTranslation(T);
	}
	Output.Pose.NormalizeRotations();
	return true;
}
