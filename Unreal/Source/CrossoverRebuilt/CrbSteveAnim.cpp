#include "CrbSteveAnim.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"

const TCHAR* CrbSteveClipName(ECrbSteveClip C)
{
	static const TCHAR* Names[] = { TEXT("Idle"), TEXT("Walk"), TEXT("Run"), TEXT("Skid"), TEXT("Brake"), TEXT("Crouch"), TEXT("CrouchSlide"),
		TEXT("Jump"), TEXT("DoubleJump"), TEXT("TripleJump"), TEXT("Backflip"), TEXT("Sideflip"), TEXT("LongJump"), TEXT("Fall"), TEXT("Land"),
		TEXT("HardLand"), TEXT("GroundPoundSpin"), TEXT("GroundPoundFall"), TEXT("GroundPoundLand"), TEXT("WallCling"), TEXT("WallKick"), TEXT("Bonk") };
	static_assert(UE_ARRAY_COUNT(Names) == (int32)ECrbSteveClip::Count, "steve clip names");
	return (int32)C < (int32)ECrbSteveClip::Count ? Names[(int32)C] : TEXT("");
}

void UCrbSteveAnimInstance::SetClips(UAnimSequence* const InClips[(int32)ECrbSteveClip::Count])
{
	ClipRefs.SetNum((int32)ECrbSteveClip::Count);
	for (int32 I = 0; I < (int32)ECrbSteveClip::Count; ++I) ClipRefs[I] = InClips[I];
}

void FCrbSteveAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	UCrbSteveAnimInstance* A = CastChecked<UCrbSteveAnimInstance>(InAnimInstance);
	Debug.ClipsLoaded = 0;
	for (int32 I = 0; I < (int32)ECrbSteveClip::Count; ++I)
	{
		Clips[I] = A->ClipRefs.IsValidIndex(I) ? A->ClipRefs[I] : nullptr;
		if (Clips[I]) ++Debug.ClipsLoaded;
	}
	In = A->Inputs;
}

void FCrbSteveAnimProxy::PostUpdate(UAnimInstance* InAnimInstance) const
{
	FAnimInstanceProxy::PostUpdate(InAnimInstance);
	CastChecked<UCrbSteveAnimInstance>(InAnimInstance)->Debug = Debug;
}

float FCrbSteveAnimProxy::Len(ECrbSteveClip C) const
{
	const UAnimSequence* S = Clips[(int32)C];
	return S ? FMath::Max(0.05f, S->SequenceLength) : 1.f;
}

void FCrbSteveAnimProxy::Start(ECrbSteveClip C, bool bLoop, float Fade)
{
	Prev = Cur;
	Cur.Clip = C; Cur.Time = 0; Cur.bLoop = bLoop;
	Blend = Fade <= 0 ? 1.f : 0.f; BlendTime = FMath::Max(0.01f, Fade);
	++Debug.Changes;
}

void FCrbSteveAnimProxy::Update(float Dt)
{
	FAnimInstanceProxy::Update(Dt);
	Dt = FMath::Clamp(Dt, 0.f, 0.1f);

	// ---- Java action -> clip ----
	if (In.Action != CurAction || In.ActionSerial != CurSerial)
	{
		CurAction = In.Action; CurSerial = In.ActionSerial; ActionTime = 0;
		struct FMap { const TCHAR* Action; ECrbSteveClip Clip; bool bLoop; float Fade; };
		static const FMap Map[] = {
			{ TEXT("IDLE"), ECrbSteveClip::Idle, true, 0.2f }, { TEXT("WALK"), ECrbSteveClip::Walk, true, 0.12f },
			{ TEXT("SKID"), ECrbSteveClip::Skid, true, 0.08f }, { TEXT("BRAKE"), ECrbSteveClip::Brake, true, 0.1f },
			{ TEXT("CROUCH"), ECrbSteveClip::Crouch, true, 0.1f }, { TEXT("CROUCH_SLIDE"), ECrbSteveClip::CrouchSlide, true, 0.08f },
			{ TEXT("LAND"), ECrbSteveClip::Land, false, 0.05f }, { TEXT("HARD_LAND"), ECrbSteveClip::HardLand, false, 0.05f },
			{ TEXT("JUMP"), ECrbSteveClip::Jump, false, 0.06f }, { TEXT("DOUBLE_JUMP"), ECrbSteveClip::DoubleJump, false, 0.06f },
			{ TEXT("TRIPLE_JUMP"), ECrbSteveClip::TripleJump, false, 0.05f }, { TEXT("BACKFLIP"), ECrbSteveClip::Backflip, false, 0.05f },
			{ TEXT("SIDE_FLIP"), ECrbSteveClip::Sideflip, false, 0.05f }, { TEXT("LONG_JUMP"), ECrbSteveClip::LongJump, false, 0.06f },
			{ TEXT("FREEFALL"), ECrbSteveClip::Fall, true, 0.2f }, { TEXT("GROUND_POUND"), ECrbSteveClip::GroundPoundSpin, false, 0.05f },
			{ TEXT("GROUND_POUND_LAND"), ECrbSteveClip::GroundPoundLand, false, 0.03f }, { TEXT("AIR_HIT_WALL"), ECrbSteveClip::WallCling, true, 0.04f },
			{ TEXT("WALL_KICK"), ECrbSteveClip::WallKick, false, 0.04f }, { TEXT("BONK"), ECrbSteveClip::Bonk, false, 0.05f } };
		const FMap* Found = nullptr;
		for (const FMap& M : Map) if (CurAction == M.Action) { Found = &M; break; }
		if (Found) Start(Found->Clip, Found->bLoop, Found->Fade);
	}
	ActionTime += Dt;

	// ---- per-clip playback ----
	const float Fwd = FMath::Max(0.f, In.FwdVel);
	RunW = FMath::FInterpTo(RunW, FMath::Clamp((Fwd - 10.f) / 12.f, 0.f, 1.f), Dt, 8.f);
	{
		// One shared phase keeps the walk and run foot cycles aligned while blending; cadence follows the speed.
		const float WalkRate = FMath::Clamp(Fwd / 9.f, 0.5f, 1.8f), RunRate = FMath::Clamp(Fwd / 28.f, 0.6f, 1.6f);
		const float CycleHz = FMath::Lerp(WalkRate / Len(ECrbSteveClip::Walk), RunRate / Len(ECrbSteveClip::Run), RunW);
		WalkPhase = FMath::Fmod(WalkPhase + Dt * CycleHz, 1.f);
	}
	auto Advance = [&](FPlay& P) { P.Time += Dt; if (P.bLoop) P.Time = FMath::Fmod(P.Time, Len(P.Clip)); else P.Time = FMath::Min(P.Time, Len(P.Clip)); };
	Advance(Cur); Advance(Prev);
	Blend = FMath::Min(1.f, Blend + Dt / BlendTime);
	// Ground pound: spin, then the tucked plummet.
	if (Cur.Clip == ECrbSteveClip::GroundPoundSpin && Cur.Time >= Len(Cur.Clip)) Start(ECrbSteveClip::GroundPoundFall, true, 0.05f);
	// A jump that has finished its rise animation and is falling fast settles into the fall loop.
	if ((Cur.Clip == ECrbSteveClip::Jump || Cur.Clip == ECrbSteveClip::DoubleJump || Cur.Clip == ECrbSteveClip::WallKick) && Cur.Time >= Len(Cur.Clip) && In.VelY < -30.f)
		Start(ECrbSteveClip::Fall, true, 0.25f);

	Debug.Clip = CrbSteveClipName(Cur.Clip);
	if (Cur.Clip == ECrbSteveClip::Walk && RunW > 0.5f) Debug.Clip = TEXT("Run");
	Debug.PrevClip = CrbSteveClipName(Prev.Clip);
	Debug.Time = Cur.Time; Debug.Blend = Blend; Debug.RunW = RunW;
}

void FCrbSteveAnimProxy::Sample(ECrbSteveClip C, float Time, bool bLoop, FPoseContext& Out) const
{
	USkeleton* Mine = const_cast<FCrbSteveAnimProxy*>(this)->GetSkeleton();
	const UAnimSequence* S = Clips[(int32)C];
	if (!S || S->GetSkeleton() != Mine) S = Clips[(int32)ECrbSteveClip::Idle];
	if (!S || S->GetSkeleton() != Mine) { Out.ResetToRefPose(); return; }
	const float L = FMath::Max(0.05f, S->SequenceLength);
	const float T = bLoop ? FMath::Fmod(FMath::Max(0.f, Time), L) : FMath::Clamp(Time, 0.f, L);
	FAnimationPoseData Data(Out);
	S->GetAnimationPose(Data, FAnimExtractContext(T, false));
}

void FCrbSteveAnimProxy::SamplePlay(const FPlay& P, FPoseContext& Out) const
{
	if (P.Clip != ECrbSteveClip::Walk) { Sample(P.Clip, P.Time, P.bLoop, Out); return; }
	Sample(ECrbSteveClip::Walk, WalkPhase * Len(ECrbSteveClip::Walk), true, Out);
	if (RunW > 0.01f)
	{
		FPoseContext Run(Out);
		Sample(ECrbSteveClip::Run, WalkPhase * Len(ECrbSteveClip::Run), true, Run);
		for (FCompactPoseBoneIndex I : Out.Pose.ForEachBoneIndex()) Out.Pose[I].BlendWith(Run.Pose[I], RunW);
	}
}

bool FCrbSteveAnimProxy::Evaluate(FPoseContext& Output)
{
	++Debug.Evaluations;
	if (Blend >= 1.f) SamplePlay(Cur, Output);
	else
	{
		SamplePlay(Prev, Output);
		FPoseContext Tmp(Output);
		SamplePlay(Cur, Tmp);
		const float W = FMath::SmoothStep(0.f, 1.f, Blend);
		for (FCompactPoseBoneIndex I : Output.Pose.ForEachBoneIndex()) Output.Pose[I].BlendWith(Tmp.Pose[I], W);
	}
	Output.Pose.NormalizeRotations();
	return true;
}
