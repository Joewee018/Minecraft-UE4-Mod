// Physics & Portal mod: the user's sculpted 3D Steve as a proper skeletal character (SK_PPSteve, /Game/Crb/PP, rigged
// by Avatar/blender/pp_steve_build.py with UE-style ik_foot / ik_hand helper bones and a generated physics asset).
// UCrbPPAnimInstance is the native animation blueprint: a state machine on Java's physics state (Idle, Walk, Run, Jump,
// Fall, Land, Roll, Slide, Launch, PortalEnter, PortalExit, HitReact) with crossfades, speed-scaled locomotion, a
// slope-alignment IK layer (pelvis tilt to the ground normal) and debug overrides. FCrbPPSteve places the character,
// spins the tucked ball while rolling from the real rolling speed, and runs the ragdoll (physics asset bodies against
// pooled block colliders built from the Minecraft blocks around him), blending back to animation when it ends.
#pragma once
#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "CrbPPSteve.generated.h"

class UAnimSequence;
class USkeletalMesh;
class USkeletalMeshComponent;
class UBoxComponent;
class UMaterialInstanceDynamic;
class ACrbHost;

// Order must match Unreal/Scripts/CreatePP.py CLIPS.
enum class ECrbPPClip : uint8 { Idle, Walk, Run, Jump, Fall, Land, Roll, Slide, Launch, PortalEnter, PortalExit, HitReact, Count };
const TCHAR* CrbPPClipName(ECrbPPClip C);

struct FCrbPPAnimInputs
{
	FString State = TEXT("IDLE");
	float Speed = 0, WalkSpeed = 4.3f, RunSpeed = 7.2f;
	FVector SlopeNormal = FVector::UpVector;  // character-local (X forward, Y right, Z up)
	int32 ForceClip = -1; bool bPaused = false; float PlaybackRate = 1.f;
};

struct FCrbPPAnimDebug { FString Clip, PrevClip, AnimState; float Time = 0, Length = 0, Blend = 1, Rate = 1; int32 Evaluations = 0, ClipsLoaded = 0, Changes = 0; };

struct FCrbPPAnimProxy : public FAnimInstanceProxy
{
	FCrbPPAnimProxy() {}
	FCrbPPAnimProxy(UAnimInstance* In) : FAnimInstanceProxy(In) {}
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void Update(float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;
	virtual void PostUpdate(UAnimInstance* InAnimInstance) const override;

	UAnimSequence* Clips[(int32)ECrbPPClip::Count] = {};
	FCrbPPAnimInputs In;
	struct FPlay { ECrbPPClip Clip = ECrbPPClip::Idle; float Time = 0; bool bLoop = true; };
	FPlay Cur, Prev; float Blend = 1.f, BlendTime = 0.15f, Rate = 1.f;
	FString Key; FQuat Tilt = FQuat::Identity;
	FCrbPPAnimDebug Debug;
private:
	void Start(ECrbPPClip C, bool bLoop, float Fade);
	void Sample(ECrbPPClip C, float Time, bool bLoop, FPoseContext& Out) const;
	float Len(ECrbPPClip C) const;
};

UCLASS(Transient, NotBlueprintable)
class UCrbPPAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	void SetClips(UAnimSequence* const InClips[(int32)ECrbPPClip::Count]);
	FCrbPPAnimInputs Inputs;
	FCrbPPAnimDebug Debug;
	UPROPERTY(Transient) TArray<UAnimSequence*> ClipRefs;
protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FCrbPPAnimProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete static_cast<FCrbPPAnimProxy*>(InProxy); }
	friend struct FCrbPPAnimProxy;
};

/** Pooled UE collision boxes on the solid Minecraft blocks around focus points (ragdoll, UE physics objects). */
class FCrbPPColliders
{
public:
	void Init(AActor* Owner, USceneComponent* Root);
	void Begin() { Used = 0; }
	void Around(ACrbHost* Host, const FVector& UE, int32 Radius);   // adds the solid blocks of a cube around a point
	void End();                                                      // parks the unused boxes
	void Clear() { Begin(); End(); }
	int32 Active() const { return Used; }
	TArray<UBoxComponent*> Boxes;
private:
	AActor* Owner = nullptr; USceneComponent* Root = nullptr; int32 Used = 0; TSet<FIntVector> Seen;
};

class FCrbPPSteve : public FGCObject
{
public:
	void Init(AActor* Owner, USceneComponent* Root);
	bool AssetsReady() const { return Mesh != nullptr && Comp != nullptr; }
	struct FFrame
	{
		bool bShow = false; FVector Feet = FVector::ZeroVector; float Yaw = 0, Speed = 0; FString State;
		FVector NormalUE = FVector::UpVector; bool bRagdoll = false; int64 RagdollSeq = 0; FVector ImpulseUE = FVector::ZeroVector;
		FCrbPPAnimInputs Anim;
	};
	void Tick(ACrbHost* Host, const FFrame& F, float Dt);
	void SetBrightness(float Emissive, float LitWeight);
	USkeletalMeshComponent* GetMesh() const { return Comp; }
	const FCrbPPAnimDebug* AnimDebug() const;
	uint32 SkeletonHash() const;
	int32 NumBones() const;
	bool HasPhysicsAsset() const;
	bool IsRagdolling() const { return bRagdoll; }
	float RollAngle = 0; int32 ClipsLoaded = 0; TArray<FString> MissingClips; FString LoadError;
	float HeightUU = 180.f, MeshYawOffset = -90.f, SmoothedYaw = 0;
	FCrbPPColliders Colliders;
	FVector RagdollPelvis = FVector::ZeroVector;

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FCrbPPSteve"); }
private:
	void StartRagdoll(const FVector& Impulse);
	void EndRagdoll();
	USkeletalMesh* Mesh = nullptr;
	USkeletalMeshComponent* Comp = nullptr;
	USceneComponent* RootComp = nullptr;
	TArray<UMaterialInstanceDynamic*> Mids;
	UAnimSequence* Clips[(int32)ECrbPPClip::Count] = {};
	bool bYawInit = false, bRagdoll = false; int64 SeenRagdoll = -1; float RagdollBlend = 0, LastBrightness = -1.f, LastLit = 1.f;
};
