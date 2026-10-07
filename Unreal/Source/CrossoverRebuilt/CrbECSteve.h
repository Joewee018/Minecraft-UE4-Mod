// Minecraft x Elden Combat (mod:eldencombat): the user's custom 3D Steve rig (Avatar/blender/pp_steve_build.py, E_* combat
// clips, imported to /Game/Crb/EC by Unreal/Scripts/CreateEC.py) as the combat character. UCrbECAnimInstance is the
// native animation blueprint: the game thread picks a clip and (for attacks) its phase from Java's combat frames; the
// proxy crossfades and samples. FCrbECSteve also builds the held weapon as a 3D extruded voxel sprite from the item's own
// Minecraft texture pixels (sent by the bridge) and a voxel shield on the left arm, both attached to the hands.
// Everything here exists only while the mod is ON: Release() destroys the components when it is turned OFF.
#pragma once
#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "CrbECSteve.generated.h"

class UAnimSequence;
class USkeletalMesh;
class USkeletalMeshComponent;
class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class ACrbHost;

// Order must match Unreal/Scripts/CreateEC.py CLIPS.
enum class ECrbECClip : uint8 { Idle, Walk, Run, Jump, Fall, Light1, Light2, Light3, Heavy, Charge, Thrust, Dodge, Backstep, Block, BlockHit, Parry, GuardBreak, Stagger, Riposte, Death, Count };
const TCHAR* CrbECClipName(ECrbECClip C);

struct FCrbECAnimInputs
{
	int32 Clip = 0;           // ECrbECClip
	float Phase = -1.f;       // 0..1 = driven from the server frames; < 0 = free-running loop
	bool bLoop = true; float Rate = 1.f, Fade = 0.15f; int64 Token = 0;   // a new token restarts the clip (new swing)
	bool bPaused = false;
};
struct FCrbECAnimDebug { FString Clip, PrevClip; float Time = 0, Length = 0, Blend = 1; int32 Evaluations = 0, ClipsLoaded = 0, Changes = 0; };

struct FCrbECAnimProxy : public FAnimInstanceProxy
{
	FCrbECAnimProxy() {}
	FCrbECAnimProxy(UAnimInstance* In) : FAnimInstanceProxy(In) {}
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void Update(float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;
	virtual void PostUpdate(UAnimInstance* InAnimInstance) const override;
	UAnimSequence* Clips[(int32)ECrbECClip::Count] = {};
	FCrbECAnimInputs In;
	struct FPlay { int32 Clip = 0; float Time = 0; bool bLoop = true; };
	FPlay Cur, Prev; float Blend = 1.f, BlendTime = 0.15f; int64 Token = -1;
	FCrbECAnimDebug Debug;
private:
	void Sample(int32 C, float Time, bool bLoop, FPoseContext& Out) const;
	float Len(int32 C) const;
};

UCLASS(Transient, NotBlueprintable)
class UCrbECAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	FCrbECAnimInputs Inputs;
	FCrbECAnimDebug Debug;
	UPROPERTY(Transient) TArray<UAnimSequence*> ClipRefs;
protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FCrbECAnimProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete static_cast<FCrbECAnimProxy*>(InProxy); }
	friend struct FCrbECAnimProxy;
};

class FCrbECSteve : public FGCObject
{
public:
	/** Loads the rig and creates the components (mod ON). bRing = the Elden Ring style: R_* animation rewrite, modelled
	 *  weapons / heater shield instead of voxel item sprites, and a swing trail. Safe to call again. */
	bool Acquire(AActor* Owner, USceneComponent* Root, UMaterialInterface* VoxelMaterial, bool bRing = false);
	bool IsRing() const { return bRingStyle; }
	/** Destroys every component this created (mod OFF). */
	void Release();
	bool IsAcquired() const { return Comp != nullptr; }
	bool AssetsAvailable() const;   // the cooked rig exists (checked without creating anything)
	struct FFrame
	{
		bool bShow = false; FVector Feet = FVector::ZeroVector; float Yaw = 0;
		FCrbECAnimInputs Anim;
		FString ItemKey; const FString* ItemPixels = nullptr; bool bWeapon = false, bShield = false;
		float WeaponPixel = 5.f;
		FString WeaponClass; bool bTrail = false;   // Elden Ring style: modelled weapon per class, trail while swinging
	};
	void Tick(ACrbHost* Host, const FFrame& F, float Dt);
	void SetBrightness(float Emissive, float LitWeight, float VoxelEmissive);
	const FCrbECAnimDebug* AnimDebug() const;
	USkeletalMeshComponent* GetMesh() const { return Comp; }
	bool WeaponVisible() const;
	FVector HandLocation() const { return LastHand; }
	FString WeaponKey() const { return BuiltKey; }
	int32 ClipsLoaded = 0, WeaponVoxels = 0, WeaponTris = 0, TrailQuads = 0, TrailQuadsMax = 0; TArray<FString> MissingClips; FString LoadError;
	float HeightUU = 180.f, MeshYawOffset = -90.f, SmoothedYaw = 0;

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FCrbECSteve"); }
private:
	void BuildWeapon(const FString& Key, const FString& Hex);
	void BuildShield();
	void BuildRingWeapon(const FString& Class);
	void BuildRingShield();
	void TickTrail(bool bOn, float Dt);
	bool BoneFrame(FName Bone, FName End, const FVector& FwdWorld, const FVector& RightWorld, FVector& OutPos, FQuat& OutDelta) const;
	USkeletalMesh* Mesh = nullptr;
	USkeletalMeshComponent* Comp = nullptr;
	UProceduralMeshComponent* Weapon = nullptr;
	UProceduralMeshComponent* Shield = nullptr;
	UProceduralMeshComponent* Trail = nullptr;
	UMaterialInstanceDynamic* TrailMid = nullptr;
	bool bRingStyle = false; float BladeLen = 0; FString BuiltClass;
	struct FTrailSample { FVector Base, Tip; float Age; };
	TArray<FTrailSample> TrailSamples;
	UMaterialInstanceDynamic* VoxelMid = nullptr;
	TArray<UMaterialInstanceDynamic*> Mids;
	UAnimSequence* Clips[(int32)ECrbECClip::Count] = {};
	FString BuiltKey; bool bYawInit = false; float LastBrightness = -1.f, LastLit = 1.f; FVector LastHand = FVector::ZeroVector;
	FVector2D Grip = FVector2D(3.5f, 12.5f);
};
