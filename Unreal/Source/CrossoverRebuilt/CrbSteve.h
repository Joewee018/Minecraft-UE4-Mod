// SM64 Steve Movement: the player drawn as SK_Steve (Minecraft player boxes on an original rig, Avatar/blender/
// steve_build.py -> Scripts/CreateSteve.py -> /Game/Crb/Steve) wearing the live Minecraft skin. Pure presentation:
// Java's Sm64Controller moves the player; this places the mesh at the interpolated Java feet, turns it to Java's facing
// and drives UCrbSteveAnimInstance from the exported SM64 action.
#pragma once
#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "CrbSteveAnim.h"

class USkeletalMeshComponent;
class USkeletalMesh;
class UAnimSequence;
class UMaterialInstanceDynamic;
class UTexture2D;

class FCrbSteve : public FGCObject
{
public:
	static constexpr const TCHAR* MeshPath = TEXT("/Game/Crb/Steve/SK_Steve.SK_Steve");
	float HeightUU = 160.f;   // whole model incl. the hat layer (ACrbHost::Sm64ModelHeight); Mario-like stature
	float ImportedHeight = 0;
	void SetHeight(float UU);
	void Init(AActor* Owner, USceneComponent* Root);
	bool AssetsReady() const { return Mesh != nullptr && Comp != nullptr; }
	void Tick(bool bShow, const FVector& Feet, float FacingYaw, const FCrbSteveAnimInputs& Inputs, UTexture2D* Skin, bool bSlim, float Dt);
	void SetBrightness(float Emissive, float LitWeight);
	USkeletalMeshComponent* GetMesh() const { return Comp; }
	const FCrbSteveAnimDebug* AnimDebug() const;
	uint32 SkeletonHash() const;
	int32 NumBones() const;
	FVector BoneLocation(FName Bone) const;
	int32 ClipsLoaded = 0; TArray<FString> MissingClips; FString LoadError; TArray<FString> MaterialReport;
	UTexture2D* DefaultSkin = nullptr; // T_SteveSkin_Default: Minecraft's steve.png from the local jar (private build)
	UTexture2D* AppliedSkin = nullptr; bool bAppliedSlim = false; int32 SkinChanges = 0;
	float MeshYawOffset = -90.f; // Blender -Y forward imports facing UE +Y
	float SmoothedYaw = 0;

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FCrbSteve"); }

private:
	USkeletalMesh* Mesh = nullptr;
	USkeletalMeshComponent* Comp = nullptr;
	TArray<UMaterialInstanceDynamic*> Mids;
	UAnimSequence* Clips[(int32)ECrbSteveClip::Count] = {};
	bool bYawInit = false, bSlimInit = false; float LastBrightness = -1.f, LastLit = 1.f;
};
