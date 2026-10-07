#include "CrbECSteve.h"
#include "CrbHost.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "ProceduralMeshComponent.h"

const TCHAR* CrbECClipName(ECrbECClip C)
{
	static const TCHAR* Names[] = { TEXT("Idle"), TEXT("Walk"), TEXT("Run"), TEXT("Jump"), TEXT("Fall"), TEXT("Light1"), TEXT("Light2"), TEXT("Light3"),
		TEXT("Heavy"), TEXT("Charge"), TEXT("Thrust"), TEXT("Dodge"), TEXT("Backstep"), TEXT("Block"), TEXT("BlockHit"), TEXT("Parry"), TEXT("GuardBreak"),
		TEXT("Stagger"), TEXT("Riposte"), TEXT("Death") };
	static_assert(UE_ARRAY_COUNT(Names) == (int32)ECrbECClip::Count, "ec clip names");
	return (int32)C < (int32)ECrbECClip::Count ? Names[(int32)C] : TEXT("");
}

// ============================================================================================ animation blueprint (native)
void FCrbECAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	UCrbECAnimInstance* A = CastChecked<UCrbECAnimInstance>(InAnimInstance);
	Debug.ClipsLoaded = 0;
	for (int32 I = 0; I < (int32)ECrbECClip::Count; ++I) { Clips[I] = A->ClipRefs.IsValidIndex(I) ? A->ClipRefs[I] : nullptr; if (Clips[I]) ++Debug.ClipsLoaded; }
	In = A->Inputs;
}

void FCrbECAnimProxy::PostUpdate(UAnimInstance* InAnimInstance) const
{
	FAnimInstanceProxy::PostUpdate(InAnimInstance);
	CastChecked<UCrbECAnimInstance>(InAnimInstance)->Debug = Debug;
}

float FCrbECAnimProxy::Len(int32 C) const { const UAnimSequence* S = C >= 0 && C < (int32)ECrbECClip::Count ? Clips[C] : nullptr; return S ? FMath::Max(0.05f, S->SequenceLength) : 1.f; }

void FCrbECAnimProxy::Update(float Dt)
{
	FAnimInstanceProxy::Update(Dt);
	Dt = FMath::Clamp(Dt, 0.f, 0.1f) * (In.bPaused ? 0.f : 1.f);
	const int32 Want = FMath::Clamp(In.Clip, 0, (int32)ECrbECClip::Count - 1);
	if (Want != Cur.Clip || In.Token != Token)
	{
		Token = In.Token;
		Prev = Cur; Cur.Clip = Want; Cur.Time = 0; Cur.bLoop = In.bLoop;
		Blend = 0.f; BlendTime = FMath::Max(0.01f, In.Fade); ++Debug.Changes;
	}
	Cur.bLoop = In.bLoop;
	const float L = Len(Cur.Clip);
	if (In.Phase >= 0.f) Cur.Time = FMath::Clamp(In.Phase, 0.f, 1.f) * L;                  // server frames drive the clip
	else { Cur.Time += Dt * In.Rate; Cur.Time = Cur.bLoop ? FMath::Fmod(Cur.Time, L) : FMath::Min(Cur.Time, L); }
	Prev.Time = Prev.bLoop ? FMath::Fmod(Prev.Time + Dt, Len(Prev.Clip)) : FMath::Min(Prev.Time + Dt, Len(Prev.Clip));
	if (Dt > 0) Blend = FMath::Min(1.f, Blend + Dt / BlendTime);
	Debug.Clip = CrbECClipName((ECrbECClip)Cur.Clip); Debug.PrevClip = CrbECClipName((ECrbECClip)Prev.Clip);
	Debug.Time = Cur.Time; Debug.Length = L; Debug.Blend = Blend;
}

void FCrbECAnimProxy::Sample(int32 C, float Time, bool bLoop, FPoseContext& Out) const
{
	USkeleton* Mine = const_cast<FCrbECAnimProxy*>(this)->GetSkeleton();
	const UAnimSequence* Sq = C >= 0 && C < (int32)ECrbECClip::Count ? Clips[C] : nullptr;
	if (!Sq || Sq->GetSkeleton() != Mine) Sq = Clips[(int32)ECrbECClip::Idle];
	if (!Sq || Sq->GetSkeleton() != Mine) { Out.ResetToRefPose(); return; }
	const float L = FMath::Max(0.05f, Sq->SequenceLength);
	FAnimationPoseData Data(Out);
	Sq->GetAnimationPose(Data, FAnimExtractContext(bLoop ? FMath::Fmod(FMath::Max(0.f, Time), L) : FMath::Clamp(Time, 0.f, L), false));
}

bool FCrbECAnimProxy::Evaluate(FPoseContext& Output)
{
	++Debug.Evaluations;
	Sample(Prev.Clip, Prev.Time, Prev.bLoop, Output);
	if (Blend < 1.f || Prev.Clip != Cur.Clip)
	{
		FPoseContext Tmp(Output);
		Sample(Cur.Clip, Cur.Time, Cur.bLoop, Tmp);
		const float W = FMath::SmoothStep(0.f, 1.f, Blend);
		for (FCompactPoseBoneIndex I : Output.Pose.ForEachBoneIndex()) Output.Pose[I].BlendWith(Tmp.Pose[I], W);
	}
	Output.Pose.NormalizeRotations();
	return true;
}

// ============================================================================================ character
bool FCrbECSteve::AssetsAvailable() const { return Mesh != nullptr || FPackageName::DoesPackageExist(TEXT("/Game/Crb/EC/SK_ECSteve")); }

bool FCrbECSteve::Acquire(AActor* Owner, USceneComponent* Root, UMaterialInterface* VoxelMaterial, bool bRing)
{
	if (Comp) return true;
	bRingStyle = bRing;
	const TCHAR* Prefix = bRing ? TEXT("R_") : TEXT("E_");
	LoadError.Reset(); MissingClips.Reset(); ClipsLoaded = 0;
	Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Crb/EC/SK_ECSteve.SK_ECSteve"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Mesh) { LoadError = TEXT("SK_ECSteve not cooked (run Tools\\PPBuild.ps1, then the asset build)"); return false; }
	for (int32 I = 0; I < (int32)ECrbECClip::Count; ++I)
	{
		const FString N = CrbECClipName((ECrbECClip)I);
		UAnimSequence* S = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/Crb/EC/%s%s.%s%s"), Prefix, *N, Prefix, *N), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (S && S->GetSkeleton() != Mesh->GetSkeleton()) { S = nullptr; LoadError += TEXT(" ") + N + TEXT(": wrong skeleton;"); }
		Clips[I] = S; if (S) ++ClipsLoaded; else MissingClips.Add(N);
	}
	Comp = NewObject<USkeletalMeshComponent>(Owner, TEXT("CrbECSteveMesh"));
	Comp->SetupAttachment(Root); Comp->RegisterComponent();
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision); Comp->SetVisibility(false); Comp->SetCastShadow(true);
	Comp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Comp->bEnableUpdateRateOptimizations = false;
	const float Imported = Mesh->GetImportedBounds().BoxExtent.Z * 2.f;
	if (Imported > KINDA_SMALL_NUMBER) Comp->SetWorldScale3D(FVector(HeightUU / Imported));
	Comp->SetSkeletalMesh(Mesh);
	Comp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Comp->SetAnimInstanceClass(UCrbECAnimInstance::StaticClass());
	if (UCrbECAnimInstance* A = Cast<UCrbECAnimInstance>(Comp->GetAnimInstance())) { A->ClipRefs.SetNum((int32)ECrbECClip::Count); for (int32 I = 0; I < (int32)ECrbECClip::Count; ++I) A->ClipRefs[I] = Clips[I]; }
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Crb/EC/M_ECSteve.M_ECSteve"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	for (int32 I = 0; I < FMath::Max(1, Comp->GetNumMaterials()); ++I)
	{
		UMaterialInstanceDynamic* M = Parent ? UMaterialInstanceDynamic::Create(Parent, Owner) : nullptr;
		if (M) Comp->SetMaterial(I, M);
		Mids.Add(M);
	}
	VoxelMid = VoxelMaterial ? UMaterialInstanceDynamic::Create(VoxelMaterial, Owner) : nullptr;
	Weapon = NewObject<UProceduralMeshComponent>(Owner, TEXT("CrbECWeapon"));
	Weapon->SetupAttachment(Root); Weapon->RegisterComponent();
	Weapon->SetCollisionEnabled(ECollisionEnabled::NoCollision); Weapon->SetVisibility(false); Weapon->SetCastShadow(true);
	Shield = NewObject<UProceduralMeshComponent>(Owner, TEXT("CrbECShield"));
	Shield->SetupAttachment(Root); Shield->RegisterComponent();
	Shield->SetCollisionEnabled(ECollisionEnabled::NoCollision); Shield->SetVisibility(false); Shield->SetCastShadow(true);
	if (bRingStyle)
	{
		BuildRingShield();
		Trail = NewObject<UProceduralMeshComponent>(Owner, TEXT("CrbECTrail"));
		Trail->SetupAttachment(Root); Trail->RegisterComponent();
		Trail->SetCollisionEnabled(ECollisionEnabled::NoCollision); Trail->SetCastShadow(false); Trail->SetVisibility(false);
		Trail->SetUsingAbsoluteLocation(true); Trail->SetUsingAbsoluteRotation(true); Trail->SetWorldTransform(FTransform::Identity);
		if (UMaterialInterface* TM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Crb/EC/M_ECTrail.M_ECTrail"), nullptr, LOAD_NoWarn | LOAD_Quiet)) TrailMid = UMaterialInstanceDynamic::Create(TM, Owner);
	}
	else BuildShield();
	BuiltKey.Reset(); BuiltClass.Reset(); bYawInit = false; LastBrightness = -1.f; TrailSamples.Reset();
	return true;
}

void FCrbECSteve::Release()
{
	for (UPrimitiveComponent* C : TArray<UPrimitiveComponent*>{ Comp, Weapon, Shield, Trail }) if (C) C->DestroyComponent();
	Comp = nullptr; Weapon = nullptr; Shield = nullptr; Trail = nullptr; TrailMid = nullptr; VoxelMid = nullptr; Mesh = nullptr;
	BuiltClass.Reset(); WeaponTris = 0; TrailSamples.Reset(); TrailQuads = 0; bRingStyle = false;
	Mids.Reset(); for (UAnimSequence*& S : Clips) S = nullptr;
	BuiltKey.Reset(); WeaponVoxels = 0; bYawInit = false;
}

const FCrbECAnimDebug* FCrbECSteve::AnimDebug() const
{
	const UCrbECAnimInstance* A = Comp ? Cast<UCrbECAnimInstance>(Comp->GetAnimInstance()) : nullptr;
	return A ? &A->Debug : nullptr;
}

bool FCrbECSteve::WeaponVisible() const { return Weapon && Weapon->IsVisible() && (bRingStyle ? WeaponTris > 0 : WeaponVoxels > 0); }

void FCrbECSteve::SetBrightness(float B, float Lit, float VoxelEmissive)
{
	if (VoxelMid) VoxelMid->SetScalarParameterValue(TEXT("EmissiveScale"), VoxelEmissive);
	if (FMath::IsNearlyEqual(B, LastBrightness, FMath::Max(1.f, B * 0.01f)) && FMath::IsNearlyEqual(Lit, LastLit, 0.01f)) return;
	LastBrightness = B; LastLit = Lit;
	for (UMaterialInstanceDynamic* M : Mids) if (M) { M->SetScalarParameterValue(TEXT("Brightness"), B); M->SetScalarParameterValue(TEXT("LitWeight"), Lit); }
}

// ---- voxel sprites: every opaque pixel is a 1 x 1 x 1 cube; faces only where the neighbour is empty. Built in pixel
// units around the pivot; the component scale sets the size. Each face is emitted with both windings (the inward copy
// is always hidden behind the solid voxel), so the build does not depend on the renderer's front-face convention.
namespace
{
	void ECVoxels(UProceduralMeshComponent* PM, UMaterialInterface* Mat, const TArray<FColor>& Px, int32 W, int32 H, const FVector2D& Pivot, bool bDiagonal, int32& OutVoxels)
	{
		OutVoxels = 0;
		if (!PM) return;
		PM->ClearAllMeshSections();
		auto Solid = [&](int32 X, int32 Y) { return X >= 0 && Y >= 0 && X < W && Y < H && Px[Y * W + X].A >= 128; };
		const float S2 = 0.70710678f;
		auto P = [&](float IX, float IY, float T) -> FVector
		{
			const float U = IX - Pivot.X, V = Pivot.Y - IY;
			return bDiagonal ? FVector((U + V) * S2, T, (V - U) * S2) : FVector(U, T, V);
		};
		const FVector UDir = bDiagonal ? FVector(S2, 0, -S2) : FVector(1, 0, 0), VDir = bDiagonal ? FVector(S2, 0, S2) : FVector(0, 0, 1);
		TArray<FVector> Vs, Ns; TArray<int32> Tris; TArray<FLinearColor> Cs; TArray<FVector2D> UV;
		auto Quad = [&](const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& N, const FLinearColor& Col)
		{
			const int32 I = Vs.Num();
			Vs.Append({ A, B, C, D }); Ns.Append({ N, N, N, N }); Cs.Append({ Col, Col, Col, Col }); UV.Append({ FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) });
			Tris.Append({ I, I + 1, I + 2, I, I + 2, I + 3, I, I + 2, I + 1, I, I + 3, I + 2 });
		};
		for (int32 Y = 0; Y < H; ++Y) for (int32 X = 0; X < W; ++X)
		{
			if (!Solid(X, Y)) continue;
			++OutVoxels;
			const FLinearColor Col = FLinearColor::FromSRGBColor(Px[Y * W + X]);
			const float X0 = X, X1 = X + 1, Y0 = Y, Y1 = Y + 1;
			Quad(P(X0, Y0, 0.5f), P(X1, Y0, 0.5f), P(X1, Y1, 0.5f), P(X0, Y1, 0.5f), FVector(0, 1, 0), Col);
			Quad(P(X0, Y0, -0.5f), P(X0, Y1, -0.5f), P(X1, Y1, -0.5f), P(X1, Y0, -0.5f), FVector(0, -1, 0), Col);
			const FLinearColor Side = Col * 0.8f;
			if (!Solid(X - 1, Y)) Quad(P(X0, Y0, -0.5f), P(X0, Y0, 0.5f), P(X0, Y1, 0.5f), P(X0, Y1, -0.5f), -UDir, Side);
			if (!Solid(X + 1, Y)) Quad(P(X1, Y0, -0.5f), P(X1, Y1, -0.5f), P(X1, Y1, 0.5f), P(X1, Y0, 0.5f), UDir, Side);
			if (!Solid(X, Y - 1)) Quad(P(X0, Y0, -0.5f), P(X1, Y0, -0.5f), P(X1, Y0, 0.5f), P(X0, Y0, 0.5f), VDir, Side);
			if (!Solid(X, Y + 1)) Quad(P(X0, Y1, -0.5f), P(X0, Y1, 0.5f), P(X1, Y1, 0.5f), P(X1, Y1, -0.5f), -VDir, Side);
		}
		if (Vs.Num() == 0) return;
		PM->CreateMeshSection_LinearColor(0, Vs, Tris, Ns, UV, Cs, TArray<FProcMeshTangent>(), false);
		if (Mat) PM->SetMaterial(0, Mat);
	}
}

void FCrbECSteve::BuildWeapon(const FString& Key, const FString& Hex)
{
	BuiltKey = Key; WeaponVoxels = 0;
	if (!Weapon) return;
	Weapon->ClearAllMeshSections();
	if (Hex.Len() < 2) return;
	const int32 N = FParse::HexNumber(*Hex.Left(2));
	if (N <= 0 || N > 32 || Hex.Len() < 2 + N * N * 8) return;
	TArray<FColor> Px; Px.SetNum(N * N);
	auto Byte = [&](int32 At) { const TCHAR C[3] = { Hex[At], Hex[At + 1], 0 }; return (uint8)FParse::HexNumber(C); };
	for (int32 I = 0; I < N * N; ++I) { const int32 O = 2 + I * 8; Px[I] = FColor(Byte(O), Byte(O + 2), Byte(O + 4), Byte(O + 6)); }
	// Minecraft tool / weapon sprites run handle bottom-left -> tip top-right: the grip sits just above the pommel
	const float K = N / 16.f;
	ECVoxels(Weapon, VoxelMid, Px, N, N, FVector2D(Grip.X * K, Grip.Y * K), true, WeaponVoxels);
}

void FCrbECSteve::BuildShield()
{
	// a Minecraft-style wooden shield: oak planks with an iron rim and boss (12 x 14 pixels), original pattern
	const int32 W = 12, H = 14;
	TArray<FColor> Px; Px.SetNum(W * H);
	for (int32 Y = 0; Y < H; ++Y) for (int32 X = 0; X < W; ++X)
	{
		const bool bRim = X == 0 || Y == 0 || X == W - 1 || Y == H - 1;
		const bool bBoss = (X == 5 || X == 6) && (Y == 6 || Y == 7);
		const uint8 Grain = (uint8)(((X * 7 + Y * 3) % 5) * 4);
		Px[Y * W + X] = bRim || bBoss ? FColor(150 + Grain, 150 + Grain, 158 + Grain, 255) : ((X / 3) % 2 ? FColor(150 + Grain, 108 + Grain, 62, 255) : FColor(132 + Grain, 94 + Grain, 52, 255));
	}
	int32 Vox = 0;
	ECVoxels(Shield, VoxelMid, Px, W, H, FVector2D(W * 0.5f, H * 0.5f), false, Vox);
}

bool FCrbECSteve::BoneFrame(FName Bone, FName End, const FVector& FwdWorld, const FVector& RightWorld, FVector& OutPos, FQuat& OutDelta) const
{
	if (!Comp || !Comp->SkeletalMesh) return false;
	const FReferenceSkeleton& Ref = Comp->SkeletalMesh->GetRefSkeleton();
	const int32 BI = Ref.FindBoneIndex(Bone), EI = Ref.FindBoneIndex(End);
	if (BI == INDEX_NONE || EI == INDEX_NONE) return false;
	const FTransform RefB = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, BI);
	const FTransform RefE = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, EI);
	const FTransform World = Comp->GetBoneTransform(BI);
	OutPos = World.TransformPosition(RefB.InverseTransformPosition(RefE.GetLocation()));
	OutDelta = World.GetRotation() * (Comp->GetComponentTransform().GetRotation() * RefB.GetRotation()).Inverse();
	return true;
}

void FCrbECSteve::Tick(ACrbHost* Host, const FFrame& F, float Dt)
{
	if (!Comp) return;
	Comp->SetVisibility(F.bShow);
	if (!F.bShow) { bYawInit = false; if (Weapon) Weapon->SetVisibility(false); if (Shield) Shield->SetVisibility(false); return; }
	if (!bYawInit) { SmoothedYaw = F.Yaw; bYawInit = true; }
	SmoothedYaw = FRotator::NormalizeAxis(SmoothedYaw + FRotator::NormalizeAxis(F.Yaw - SmoothedYaw) * FMath::Clamp(Dt * 16.f, 0.f, 1.f));
	Comp->SetWorldLocationAndRotation(F.Feet, FQuat(FRotator(0, SmoothedYaw + MeshYawOffset, 0)));
	if (UCrbECAnimInstance* A = Cast<UCrbECAnimInstance>(Comp->GetAnimInstance())) A->Inputs = F.Anim;

	// held weapon: rebuilt when the item (voxel sprite) or weapon class (modelled, Elden Ring style) changes
	if (bRingStyle) { if (F.WeaponClass != BuiltClass) BuildRingWeapon(F.WeaponClass); }
	else if (F.ItemPixels && !F.ItemKey.IsEmpty() && F.ItemKey != BuiltKey) BuildWeapon(F.ItemKey, *F.ItemPixels);
	const FVector Fwd = FRotator(0, SmoothedYaw, 0).Vector(), Right = FRotator(0, SmoothedYaw + 90.f, 0).Vector();
	FVector Hand; FQuat D;
	const bool bHave = bRingStyle ? (WeaponTris > 0 && !F.WeaponClass.IsEmpty() && F.WeaponClass != TEXT("fist")) : (F.bWeapon && WeaponVoxels > 0 && BuiltKey == F.ItemKey);
	const bool bWeapon = bHave && BoneFrame(TEXT("forearm_r"), TEXT("ik_hand_r"), Fwd, Right, Hand, D);
	if (Weapon)
	{
		Weapon->SetVisibility(bWeapon);
		if (bWeapon)
		{
			LastHand = Hand;
			const FVector Blade = D.RotateVector((Fwd + FVector(0, 0, 0.25f)).GetSafeNormal()), Side = D.RotateVector(Right);
			Weapon->SetWorldTransform(FTransform(FRotationMatrix::MakeFromXY(Blade, Side).ToQuat(), Hand, FVector(bRingStyle ? HeightUU / 180.f : F.WeaponPixel)));
		}
	}
	if (bRingStyle) TickTrail(bWeapon && F.bTrail, Dt);
	// shield on the left forearm: faces forward while guarding / parrying, outward when carried
	FVector HandL, Elbow; FQuat DL;
	const bool bShield = F.bShield && BoneFrame(TEXT("forearm_l"), TEXT("ik_hand_l"), Fwd, Right, HandL, DL);
	if (Shield)
	{
		Shield->SetVisibility(bShield);
		if (bShield)
		{
			Elbow = Comp->GetBoneLocation(TEXT("forearm_l"));
			const bool bGuard = F.Anim.Clip == (int32)ECrbECClip::Block || F.Anim.Clip == (int32)ECrbECClip::BlockHit || F.Anim.Clip == (int32)ECrbECClip::Parry;
			const FVector N = bGuard ? Fwd : -Right;
			FVector Up = (Elbow - HandL).GetSafeNormal(); Up = (Up - N * FVector::DotProduct(Up, N)).GetSafeNormal();
			if (Up.IsNearlyZero()) Up = FVector::UpVector;
			const FVector Center = FMath::Lerp(Elbow, HandL, 0.55f) + N * (F.WeaponPixel * 1.5f);
			Shield->SetWorldTransform(FTransform(FRotationMatrix::MakeFromYZ(N, Up).ToQuat(), bRingStyle ? FMath::Lerp(Elbow, HandL, 0.5f) + N * 6.f : Center, FVector(bRingStyle ? HeightUU / 180.f : F.WeaponPixel)));
		}
	}
}

void FCrbECSteve::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Mesh); Collector.AddReferencedObject(Comp); Collector.AddReferencedObject(Weapon); Collector.AddReferencedObject(Shield); Collector.AddReferencedObject(VoxelMid);
	Collector.AddReferencedObject(Trail); Collector.AddReferencedObject(TrailMid);
	for (UMaterialInstanceDynamic*& M : Mids) Collector.AddReferencedObject(M);
	for (UAnimSequence*& S : Clips) Collector.AddReferencedObject(S);
}

// ============================================================================================ Elden Ring style meshes
// Modelled (not voxel) weapons built from hexahedra in centimetres around the grip: X runs along the blade from the
// hand, Y is the flat side, Z the edge. Each class gets its own silhouette (longsword, battle axe, great hammer, spear,
// war scythe, mace); colours are vertex colours on the lit vertex-colour material.
namespace
{
	struct FECMesh
	{
		TArray<FVector> V, N; TArray<int32> T; TArray<FLinearColor> C; TArray<FVector2D> UV;
		void Face(const FVector& A, const FVector& B, const FVector& Cc, const FVector& D, const FVector& Out, const FLinearColor& Col)
		{
			FVector Nn = FVector::CrossProduct(B - A, D - A).GetSafeNormal();
			if (Nn.IsNearlyZero()) Nn = FVector::CrossProduct(Cc - B, A - B).GetSafeNormal();
			if (FVector::DotProduct(Nn, Out) < 0) Nn = -Nn;
			const int32 I = V.Num();
			V.Append({ A, B, Cc, D }); N.Append({ Nn, Nn, Nn, Nn }); C.Append({ Col, Col, Col, Col }); UV.Append({ FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) });
			T.Append({ I, I + 1, I + 2, I, I + 2, I + 3, I, I + 2, I + 1, I, I + 3, I + 2 });
		}
		/** 8 corners: 0-3 the back face (x0), 4-7 the front face, both ordered (y-,z-), (y+,z-), (y+,z+), (y-,z+). */
		void Hex(const FVector P[8], const FLinearColor& Col)
		{
			FVector Ctr = FVector::ZeroVector; for (int32 I = 0; I < 8; ++I) Ctr += P[I]; Ctr /= 8.f;
			auto F4 = [&](int32 A, int32 B, int32 Cc, int32 D, const FLinearColor& K) { const FVector Fc = (P[A] + P[B] + P[Cc] + P[D]) / 4.f; Face(P[A], P[B], P[Cc], P[D], Fc - Ctr, K); };
			const FLinearColor Dim = Col * 0.82f;
			F4(0, 3, 2, 1, Dim); F4(4, 5, 6, 7, Col); F4(0, 1, 5, 4, Dim); F4(3, 7, 6, 2, Col); F4(0, 4, 7, 3, Col); F4(1, 2, 6, 5, Dim);
		}
		/** Tapered block along X: cross-section half sizes (Y, Z) at x0 and x1, Z centre offsets z0 / z1. */
		void Taper(float X0, float X1, float Y0, float Z0, float Y1, float Z1, const FLinearColor& Col, float ZC0 = 0, float ZC1 = 0)
		{
			const FVector P[8] = { FVector(X0, -Y0, ZC0 - Z0), FVector(X0, Y0, ZC0 - Z0), FVector(X0, Y0, ZC0 + Z0), FVector(X0, -Y0, ZC0 + Z0),
				FVector(X1, -Y1, ZC1 - Z1), FVector(X1, Y1, ZC1 - Z1), FVector(X1, Y1, ZC1 + Z1), FVector(X1, -Y1, ZC1 + Z1) };
			Hex(P, Col);
		}
		void Box(const FVector& Ctr, const FVector& H, const FLinearColor& Col) { Taper(Ctr.X - H.X, Ctr.X + H.X, H.Y, H.Z, H.Y, H.Z, Col, Ctr.Z, Ctr.Z); }
		/** Block whose X runs along Z instead (for heads across the haft): X range -> Z, Z range -> X. */
		void Cross(float Z0, float Z1, float HX0, float HX1, float HY, const FLinearColor& Col, float XC = 0)
		{
			const FVector P[8] = { FVector(XC - HX0, -HY, Z0), FVector(XC - HX0, HY, Z0), FVector(XC + HX0, HY, Z0), FVector(XC + HX0, -HY, Z0),
				FVector(XC - HX1, -HY, Z1), FVector(XC - HX1, HY, Z1), FVector(XC + HX1, HY, Z1), FVector(XC + HX1, -HY, Z1) };
			Hex(P, Col);
		}
		int32 Commit(UProceduralMeshComponent* PM, UMaterialInterface* M)
		{
			if (!PM) return 0;
			PM->ClearAllMeshSections();
			if (V.Num() == 0) return 0;
			PM->CreateMeshSection_LinearColor(0, V, T, N, UV, C, TArray<FProcMeshTangent>(), false);
			if (M) PM->SetMaterial(0, M);
			return T.Num() / 3;
		}
	};
	const FLinearColor ECSteel(0.62f, 0.64f, 0.68f), ECSteelDark(0.28f, 0.29f, 0.32f), ECLeather(0.16f, 0.09f, 0.05f), ECWood(0.32f, 0.2f, 0.11f), ECGold(0.62f, 0.48f, 0.2f);
}

void FCrbECSteve::BuildRingWeapon(const FString& Class)
{
	BuiltClass = Class; WeaponTris = 0; BladeLen = 0;
	if (!Weapon) return;
	FECMesh M;
	if (Class == TEXT("sword"))
	{
		M.Box(FVector(-17, 0, 0), FVector(2.4f, 2.4f, 2.4f), ECGold);                 // pommel
		M.Taper(-15, 0, 1.5f, 1.7f, 1.5f, 1.7f, ECLeather);                           // grip
		M.Box(FVector(1.2f, 0, 0), FVector(1.4f, 2.2f, 10.f), ECGold);                // crossguard
		M.Taper(2.5f, 80, 0.8f, 4.2f, 0.6f, 3.2f, ECSteel);                            // blade
		M.Taper(80, 93, 0.6f, 3.2f, 0.15f, 0.2f, ECSteel);                             // point
		M.Taper(5, 70, 0.75f, 0.5f, 0.6f, 0.4f, ECSteelDark);                          // fuller
		BladeLen = 90.f;
	}
	else if (Class == TEXT("axe"))
	{
		M.Taper(-22, 62, 1.8f, 1.8f, 1.6f, 1.6f, ECWood);                               // haft
		M.Box(FVector(-23, 0, 0), FVector(1.5f, 2.2f, 2.2f), ECSteelDark);
		M.Cross(1.5f, 20.f, 5.f, 11.f, 0.9f, ECSteel, 52.f);                          // bearded blade (flares out along Z)
		M.Box(FVector(52, 0, 2.5f), FVector(6.f, 2.4f, 4.f), ECSteelDark);              // socket
		M.Cross(-1.5f, -9.f, 3.f, 1.5f, 1.5f, ECSteelDark, 52.f);                     // back spike
		BladeLen = 64.f;
	}
	else if (Class == TEXT("hammer"))
	{
		M.Taper(-24, 70, 2.2f, 2.2f, 2.f, 2.f, ECWood);
		M.Box(FVector(-25, 0, 0), FVector(2.f, 2.8f, 2.8f), ECSteelDark);
		M.Box(FVector(72, 0, 0), FVector(9.f, 8.f, 16.f), ECSteelDark);                // great hammer head
		M.Box(FVector(72, 0, 0), FVector(9.6f, 8.6f, 3.f), ECGold);                    // band
		BladeLen = 82.f;
	}
	else if (Class == TEXT("spear"))
	{
		M.Taper(-60, 110, 1.5f, 1.5f, 1.3f, 1.3f, ECWood);
		M.Box(FVector(-61, 0, 0), FVector(2.f, 1.8f, 1.8f), ECSteelDark);
		M.Box(FVector(111, 0, 0), FVector(3.f, 2.f, 2.f), ECSteelDark);                // socket
		M.Taper(113, 125, 0.6f, 1.5f, 0.6f, 3.4f, ECSteel);                           // leaf blade
		M.Taper(125, 142, 0.6f, 3.4f, 0.1f, 0.2f, ECSteel);
		BladeLen = 142.f;
	}
	else if (Class == TEXT("scythe"))
	{
		M.Taper(-40, 95, 1.5f, 1.5f, 1.4f, 1.4f, ECWood);
		M.Box(FVector(96, 0, 2.f), FVector(3.f, 2.f, 4.f), ECSteelDark);
		M.Cross(4.f, 58.f, 4.5f, 1.f, 0.6f, ECSteel, 92.f);                           // long blade sweeping out along Z
		M.Cross(40.f, 60.f, 1.2f, 0.3f, 0.6f, ECSteel, 86.f);
		BladeLen = 98.f;
	}
	else if (Class == TEXT("club"))
	{
		M.Taper(-14, 0, 1.6f, 1.6f, 1.6f, 1.6f, ECLeather);
		M.Taper(0, 58, 2.f, 2.f, 4.6f, 4.6f, ECWood);
		for (int32 K = 0; K < 4; ++K) M.Box(FVector(46 + K * 3.5f, 0, 0), FVector(0.8f, 5.2f, 5.2f), ECSteelDark);  // iron studs bands
		BladeLen = 58.f;
	}
	WeaponTris = M.Commit(Weapon, VoxelMid);
}

void FCrbECSteve::BuildRingShield()
{
	// heater shield: wood field, iron rim, gold boss (local X = width, Z = height, Y = facing)
	FECMesh M;
	M.Box(FVector(0, 0, 8), FVector(17.f, 1.4f, 13.f), ECWood);
	{
		const FVector P[8] = { FVector(-17, -1.4f, -5), FVector(-17, 1.4f, -5), FVector(17, 1.4f, -5), FVector(17, -1.4f, -5),
			FVector(-2, -1.4f, -27), FVector(-2, 1.4f, -27), FVector(2, 1.4f, -27), FVector(2, -1.4f, -27) };
		M.Hex(P, ECWood);
	}
	M.Box(FVector(0, 0, 21.5f), FVector(18.f, 1.8f, 1.3f), ECSteelDark);              // top rim
	M.Box(FVector(-17.5f, 0, 8), FVector(1.2f, 1.8f, 13.5f), ECSteelDark);            // side rims
	M.Box(FVector(17.5f, 0, 8), FVector(1.2f, 1.8f, 13.5f), ECSteelDark);
	M.Box(FVector(0, 1.6f, 4), FVector(4.f, 1.f, 4.f), ECGold);                       // boss
	M.Box(FVector(0, 1.5f, 4), FVector(1.f, 0.8f, 20.f), ECSteelDark);               // vertical band
	M.Commit(Shield, VoxelMid);
}

void FCrbECSteve::TickTrail(bool bOn, float Dt)
{
	if (!Trail || !Weapon) return;
	for (FTrailSample& S : TrailSamples) S.Age += Dt;
	TrailSamples.RemoveAll([](const FTrailSample& S) { return S.Age > 0.16f; });
	if (bOn && BladeLen > 0)
	{
		const FTransform W = Weapon->GetComponentTransform();
		TrailSamples.Add({ W.TransformPosition(FVector(BladeLen * 0.3f, 0, 0)), W.TransformPosition(FVector(BladeLen, 0, 0)), 0.f });
	}
	TrailQuads = FMath::Max(0, TrailSamples.Num() - 1);
	TrailQuadsMax = FMath::Max(TrailQuadsMax, TrailQuads);
	Trail->SetVisibility(TrailQuads > 0);
	Trail->ClearAllMeshSections();
	if (TrailQuads == 0) return;
	TArray<FVector> V, N; TArray<int32> T; TArray<FLinearColor> C; TArray<FVector2D> UV;
	for (int32 I = 0; I < TrailSamples.Num(); ++I)
	{
		const FTrailSample& S = TrailSamples[I];
		const float A = FMath::Clamp(1.f - S.Age / 0.16f, 0.f, 1.f) * 0.55f;
		V.Add(S.Base); V.Add(S.Tip); N.Add(FVector::UpVector); N.Add(FVector::UpVector);
		C.Add(FLinearColor(1.f, 0.86f, 0.6f, 0.f)); C.Add(FLinearColor(1.f, 0.9f, 0.7f, A));
		UV.Add(FVector2D(I, 0)); UV.Add(FVector2D(I, 1));
		if (I > 0) { const int32 B = (I - 1) * 2; T.Append({ B, B + 1, B + 3, B, B + 3, B + 2, B, B + 3, B + 1, B, B + 2, B + 3 }); }
	}
	Trail->CreateMeshSection_LinearColor(0, V, T, N, UV, C, TArray<FProcMeshTangent>(), false);
	if (TrailMid) Trail->SetMaterial(0, TrailMid);
}
