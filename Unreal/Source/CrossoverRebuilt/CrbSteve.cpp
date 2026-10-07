#include "CrbSteve.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Crc.h"

void FCrbSteve::Init(AActor* Owner, USceneComponent* Root)
{
	Mesh = LoadObject<USkeletalMesh>(nullptr, MeshPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Mesh) { LoadError = FString::Printf(TEXT("%s not cooked (run Tools\\SteveBuild.ps1, then the asset build)"), MeshPath); return; }
	DefaultSkin = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Crb/Steve/T_SteveSkin_Default.T_SteveSkin_Default"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	ClipsLoaded = 0; MissingClips.Reset();
	for (int32 I = 0; I < (int32)ECrbSteveClip::Count; ++I)
	{
		const FString N = CrbSteveClipName((ECrbSteveClip)I);
		UAnimSequence* S = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/Crb/Steve/S_%s.S_%s"), *N, *N), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (S && S->GetSkeleton() != Mesh->GetSkeleton()) { S = nullptr; LoadError += FString::Printf(TEXT(" %s: wrong skeleton;"), *N); }
		Clips[I] = S;
		if (S) ++ClipsLoaded; else MissingClips.Add(N);
	}
	Comp = NewObject<USkeletalMeshComponent>(Owner, TEXT("CrbSteveMesh"));
	Comp->SetupAttachment(Root);
	Comp->RegisterComponent();
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetVisibility(false);
	Comp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Comp->bEnableUpdateRateOptimizations = false;
	Comp->SetCastShadow(true);
	ImportedHeight = Mesh->GetImportedBounds().BoxExtent.Z * 2.f;
	SetHeight(HeightUU);
	Comp->SetSkeletalMesh(Mesh);
	Comp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Comp->SetAnimInstanceClass(UCrbSteveAnimInstance::StaticClass());
	if (UCrbSteveAnimInstance* A = Cast<UCrbSteveAnimInstance>(Comp->GetAnimInstance())) A->SetClips(Clips);
	// Slot materials by name (Base = opaque skin, Overlay = masked hat/jacket/sleeves/pants). Loaded explicitly: the
	// mesh asset's slot assignment is not relied on (the cooked component reported invalid material indices).
	const int32 NumSlots = FMath::Max(2, Comp->GetNumMaterials());
	for (int32 I = 0; I < NumSlots; ++I)
	{
		const FName Slot = Mesh->Materials.IsValidIndex(I) ? Mesh->Materials[I].MaterialSlotName : NAME_None;
		const bool bOverlay = Slot.ToString().Contains(TEXT("Overlay")) || (Slot.IsNone() && I == 1);
		const TCHAR* Path = bOverlay ? TEXT("/Game/Crb/Steve/M_Steve_Overlay.M_Steve_Overlay") : TEXT("/Game/Crb/Steve/M_Steve_Base.M_Steve_Base");
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		UMaterialInstanceDynamic* M = Parent ? UMaterialInstanceDynamic::Create(Parent, Owner) : nullptr;
		if (M) Comp->SetMaterial(I, M);
		Mids.Add(M);
		MaterialReport.Add(FString::Printf(TEXT("SK_Steve[%d:%s]=%s"), I, *Slot.ToString(), Parent ? *Parent->GetName() : TEXT("none")));
	}
}

void FCrbSteve::SetHeight(float UU)
{
	HeightUU = FMath::Clamp(UU, 80.f, 240.f);
	if (Comp && ImportedHeight > KINDA_SMALL_NUMBER) Comp->SetWorldScale3D(FVector(HeightUU / ImportedHeight));
}

const FCrbSteveAnimDebug* FCrbSteve::AnimDebug() const
{
	const UCrbSteveAnimInstance* A = Comp ? Cast<UCrbSteveAnimInstance>(Comp->GetAnimInstance()) : nullptr;
	return A ? &A->Debug : nullptr;
}

int32 FCrbSteve::NumBones() const { return Comp && Comp->SkeletalMesh ? Comp->SkeletalMesh->GetRefSkeleton().GetNum() : 0; }

uint32 FCrbSteve::SkeletonHash() const
{
	// zlib CRC32 of the sorted lower-case "child>parent\n" lines; steve_build.py writes the same into skeleton.json.
	if (!Comp || !Comp->SkeletalMesh) return 0;
	const FReferenceSkeleton& R = Comp->SkeletalMesh->GetRefSkeleton();
	TArray<FString> Lines;
	for (int32 B = 0; B < R.GetNum(); ++B)
	{
		const int32 P = R.GetParentIndex(B);
		Lines.Add((R.GetBoneName(B).ToString() + TEXT(">") + (P == INDEX_NONE ? FString() : R.GetBoneName(P).ToString())).ToLower());
	}
	Lines.Sort();
	FString All; for (const FString& L : Lines) { All += L; All += TEXT("\n"); }
	FTCHARToUTF8 U(*All);
	return FCrc::MemCrc32(U.Get(), U.Length());
}

FVector FCrbSteve::BoneLocation(FName Bone) const
{
	if (!Comp || Comp->GetBoneIndex(Bone) == INDEX_NONE) return FVector::ZeroVector;
	return Comp->GetBoneLocation(Bone);
}

void FCrbSteve::Tick(bool bShow, const FVector& Feet, float FacingYaw, const FCrbSteveAnimInputs& Inputs, UTexture2D* Skin, bool bSlim, float Dt)
{
	if (!Comp) return;
	Comp->SetVisibility(bShow);
	if (!bShow) { bYawInit = false; return; }
	if (!bYawInit) { SmoothedYaw = FacingYaw; bYawInit = true; }
	// Java turns at most ~11 degrees per 30 Hz frame on the ground; flips can snap 180. Follow quickly but smoothly.
	const float Diff = FRotator::NormalizeAxis(FacingYaw - SmoothedYaw);
	SmoothedYaw = FRotator::NormalizeAxis(SmoothedYaw + Diff * FMath::Clamp(Dt * 18.f, 0.f, 1.f));
	Comp->SetWorldLocationAndRotation(Feet, FRotator(0, SmoothedYaw + MeshYawOffset, 0));
	if (Skin && Skin != AppliedSkin)
	{
		for (UMaterialInstanceDynamic* M : Mids) if (M) M->SetTextureParameterValue(TEXT("Skin"), Skin);
		AppliedSkin = Skin; ++SkinChanges;
	}
	if (!bSlimInit || bSlim != bAppliedSlim)
	{
		// Classic (4 px) and slim (3 px) arms are both in the mesh on their own bones; hide the other set.
		const FName Wide[2] = { TEXT("arm_r"), TEXT("arm_l") }, Slim[2] = { TEXT("arm_r_slim"), TEXT("arm_l_slim") };
		for (int32 K = 0; K < 2; ++K)
		{
			if (bSlim) { Comp->UnHideBoneByName(Slim[K]); Comp->HideBoneByName(Wide[K], PBO_None); }
			else { Comp->UnHideBoneByName(Wide[K]); Comp->HideBoneByName(Slim[K], PBO_None); }
		}
		bAppliedSlim = bSlim; bSlimInit = true;
	}
	if (UCrbSteveAnimInstance* A = Cast<UCrbSteveAnimInstance>(Comp->GetAnimInstance()))
	{
		if (A->ClipRefs.Num() == 0) A->SetClips(Clips);
		A->Inputs = Inputs;
	}
}

void FCrbSteve::SetBrightness(float B, float Lit)
{
	if (FMath::IsNearlyEqual(B, LastBrightness, FMath::Max(1.f, B * 0.01f)) && FMath::IsNearlyEqual(Lit, LastLit, 0.01f)) return;
	LastBrightness = B; LastLit = Lit;
	for (UMaterialInstanceDynamic* M : Mids) if (M) { M->SetScalarParameterValue(TEXT("Brightness"), B); M->SetScalarParameterValue(TEXT("LitWeight"), Lit); }
}

void FCrbSteve::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Mesh);
	Collector.AddReferencedObject(Comp);
	Collector.AddReferencedObject(AppliedSkin);
	Collector.AddReferencedObject(DefaultSkin);
	for (UMaterialInstanceDynamic*& M : Mids) Collector.AddReferencedObject(M);
	for (UAnimSequence*& S : Clips) Collector.AddReferencedObject(S);
}
