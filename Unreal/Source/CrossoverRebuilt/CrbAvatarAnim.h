// God of War Unity port: native animation instance (no Animation Blueprint) reproducing the repo's PlayerAnimator /
// Mutant Animator: a 2D directional movement blend (idle + walk F/B/L/R, run F/B + jog strafe L/R when sprinting) driven
// by the local move direction smoothed like PlayerAnimationManager (MoveTowards, 10/s), a melee one-shot (state speed 2),
// the throw one-shot (speed 1.8), death, and the enemy's idle/dying. Clips are sampled from UAnimSequences on the shared
// Mutant skeleton and blended per bone; Hips horizontal translation is pinned (Java owns position).
#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "CrbAvatarAnim.generated.h"

class UAnimSequence;

// Assets under /Game/Crb/Avatar (Avatar/blender/gow_build.py).
// Player clips (A_<Role>) are on the player's skeleton, enemy clips (E_<Role>) on the Mutant's.
enum class ECrbClip : uint8 { Idle, WalkF, WalkB, WalkL, WalkR, RunF, RunB, JogL, JogR, Attack, Death, EnemyIdle, Swipe, EnemyDeath, Count };
inline bool CrbIsEnemyClip(ECrbClip C) { return C == ECrbClip::EnemyIdle || C == ECrbClip::Swipe || C == ECrbClip::EnemyDeath; }
const TCHAR* CrbClipName(ECrbClip C);

struct FCrbAvatarAnimInputs
{
	float LocalRight = 0, LocalForward = 0; // horizontal velocity in the character's frame, blocks per second
	float VerticalSpeed = 0;
	bool bGrounded = true, bFlying = false, bDead = false, bSprint = false;
	bool bEnemy = false;                    // Mutant enemy: EnemyIdle base, Swipe one-shot
	int32 AttackSerial = 0, ThrowSerial = 0, SwipeSerial = 0; // a change starts the one-shot
};

struct FCrbAvatarAnimDebug
{
	FString Base, OneShot; float DirX = 0, DirZ = 0, RunW = 0, OneShotTime = 0; int32 Evaluations = 0, ClipsLoaded = 0;
};

struct FCrbAvatarAnimProxy : public FAnimInstanceProxy
{
	FCrbAvatarAnimProxy() {}
	FCrbAvatarAnimProxy(UAnimInstance* In) : FAnimInstanceProxy(In) {}
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void Update(float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;
	virtual void PostUpdate(UAnimInstance* InAnimInstance) const override;

	UAnimSequence* Clips[(int32)ECrbClip::Count] = {};
	FCrbAvatarAnimInputs In, Last;
	FVector2D AnimDir = FVector2D::ZeroVector; // x right, y forward; magnitude = speed / walk speed (<= 1)
	float Phase = 0, IdleTime = 0, RunW = 0, AirW = 0, DeathTime = 0;
	ECrbClip OneShot = ECrbClip::Count; float OneShotTime = 0, OneShotRate = 1;
	FCrbAvatarAnimDebug Debug;

private:
	void Sample(ECrbClip C, float Time, bool bLoop, FPoseContext& Out) const;
	float Len(ECrbClip C) const;
};

UCLASS(Transient, NotBlueprintable)
class UCrbAvatarAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	void SetClips(UAnimSequence* const InClips[(int32)ECrbClip::Count]);
	FCrbAvatarAnimInputs Inputs;
	FCrbAvatarAnimDebug Debug;   // copied back from the proxy after each update
	UPROPERTY(Transient) TArray<UAnimSequence*> ClipRefs;
	static constexpr float AttackRate = 2.f;  // PlayerAnimator: "Standing Melee Attack Downward" m_Speed 2
	static constexpr float ThrowRate = 1.8f;  // PlayerAnimator: "Axe Throw" m_Speed 1.8
protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FCrbAvatarAnimProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete static_cast<FCrbAvatarAnimProxy*>(InProxy); }
	friend struct FCrbAvatarAnimProxy;
};
