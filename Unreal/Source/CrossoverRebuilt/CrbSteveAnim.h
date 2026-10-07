// SM64 Steve Movement: native animation instance for SK_Steve (no Animation Blueprint). Java (crb.client.sm64.
// Sm64Controller) owns the movement state machine; this maps its action to the original Steve clips
// (Avatar/blender/steve_build.py), crossfades between them, scales the walk/run cycle with the forward speed and holds
// one-shot clips (flips, landings) on their last frame until the action changes.
#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "CrbSteveAnim.generated.h"

class UAnimSequence;

// Order must match Unreal/Scripts/CreateSteve.py CLIPS.
enum class ECrbSteveClip : uint8
{
	Idle, Walk, Run, Skid, Brake, Crouch, CrouchSlide, Jump, DoubleJump, TripleJump, Backflip, Sideflip, LongJump, Fall,
	Land, HardLand, GroundPoundSpin, GroundPoundFall, GroundPoundLand, WallCling, WallKick, Bonk, Count
};
const TCHAR* CrbSteveClipName(ECrbSteveClip C);

struct FCrbSteveAnimInputs
{
	FString Action = TEXT("IDLE"); // Java Sm64Controller.Action name
	int32 ActionSerial = 0;        // changes on every action change (restart the clip even if the name repeats)
	float FwdVel = 0, VelY = 0;    // SM64 units per frame
	bool bGrounded = true;
};

struct FCrbSteveAnimDebug
{
	FString Clip, PrevClip; float Time = 0, Blend = 1, RunW = 0; int32 Evaluations = 0, ClipsLoaded = 0, Changes = 0;
};

struct FCrbSteveAnimProxy : public FAnimInstanceProxy
{
	FCrbSteveAnimProxy() {}
	FCrbSteveAnimProxy(UAnimInstance* In) : FAnimInstanceProxy(In) {}
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void Update(float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;
	virtual void PostUpdate(UAnimInstance* InAnimInstance) const override;

	UAnimSequence* Clips[(int32)ECrbSteveClip::Count] = {};
	FCrbSteveAnimInputs In;
	FString CurAction; int32 CurSerial = -1;
	struct FPlay { ECrbSteveClip Clip = ECrbSteveClip::Idle; float Time = 0; bool bLoop = true; };
	FPlay Cur, Prev; float Blend = 1.f, BlendTime = 0.12f;
	float WalkPhase = 0, RunW = 0, ActionTime = 0;
	FCrbSteveAnimDebug Debug;

private:
	void Start(ECrbSteveClip C, bool bLoop, float Fade);
	void SamplePlay(const FPlay& P, FPoseContext& Out) const;
	void Sample(ECrbSteveClip C, float Time, bool bLoop, FPoseContext& Out) const;
	float Len(ECrbSteveClip C) const;
};

UCLASS(Transient, NotBlueprintable)
class UCrbSteveAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	void SetClips(UAnimSequence* const InClips[(int32)ECrbSteveClip::Count]);
	FCrbSteveAnimInputs Inputs;
	FCrbSteveAnimDebug Debug;
	UPROPERTY(Transient) TArray<UAnimSequence*> ClipRefs;
protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FCrbSteveAnimProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete static_cast<FCrbSteveAnimProxy*>(InProxy); }
	friend struct FCrbSteveAnimProxy;
};
