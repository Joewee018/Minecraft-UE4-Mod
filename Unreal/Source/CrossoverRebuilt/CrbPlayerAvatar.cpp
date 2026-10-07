#include "CrbPlayerAvatar.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Crc.h"
#include "CrbCoords.h"
#include "Materials/MaterialInstanceDynamic.h"

// Apply the imported per-slot materials (M_<Mesh>_<Slot>) explicitly and report what each slot renders with.
static void ApplySlotMaterials(UMeshComponent* C, const FString& MeshName, TArray<FString>& Report)
{
	for (int32 I = 0; I < C->GetNumMaterials(); ++I)
	{
		const FString Name = FString::Printf(TEXT("M_%s_%d"), *MeshName, I);
		if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Crb/Avatar/%s.%s"), *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
			C->SetMaterial(I, M);
		UMaterialInterface* Now = C->GetMaterial(I);
		Report.Add(FString::Printf(TEXT("%s[%d]=%s"), *MeshName, I, Now ? *Now->GetName() : TEXT("none")));
	}
}

static const TCHAR* const BonePrefixes[] = { TEXT(""), TEXT("mixamorig_"), TEXT("mixamorig:"), TEXT("mixamorig1_") };

static FString StripPrefix(const FString& N)
{
	for (const TCHAR* P : BonePrefixes) if (*P && N.StartsWith(P)) return N.Mid(FCString::Strlen(P));
	return N;
}

void FCrbPlayerAvatar::Init(AActor* Owner, USceneComponent* Root, UMaterialInterface* VertexColorMat)
{
	AxeMat = VertexColorMat;
	Mesh = LoadObject<USkeletalMesh>(nullptr, MeshPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Mesh) LoadError = FString::Printf(TEXT("player mesh %s not cooked (run Scripts/CreateAvatar.py)"), MeshPath);
	EnemyMesh = LoadObject<USkeletalMesh>(nullptr, EnemyMeshPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!EnemyMesh) LoadError += FString::Printf(TEXT(" enemy mesh %s not cooked;"), EnemyMeshPath);
	ClipsLoaded = 0; MissingClips.Reset();
	for (int32 I = 0; I < (int32)ECrbClip::Count; ++I)
	{
		const bool bEnemy = CrbIsEnemyClip((ECrbClip)I);
		const TCHAR* Prefix = bEnemy ? TEXT("E_") : TEXT("A_");
		const FString Path = FString::Printf(TEXT("/Game/Crb/Avatar/%s%s.%s%s"), Prefix, CrbClipName((ECrbClip)I), Prefix, CrbClipName((ECrbClip)I));
		UAnimSequence* S = LoadObject<UAnimSequence>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		USkeletalMesh* Owning = bEnemy ? EnemyMesh : Mesh;
		if (S && Owning && S->GetSkeleton() != Owning->GetSkeleton()) { S = nullptr; LoadError += FString::Printf(TEXT(" %s: wrong skeleton;"), CrbClipName((ECrbClip)I)); }
		Clips[I] = S;
		if (S) ++ClipsLoaded; else MissingClips.Add(CrbClipName((ECrbClip)I));
	}
	OwnerActor = Owner; RootComp = Root;
	Comp = MakeMeshComponent(TEXT("CrbAvatarMesh"), Mesh);
	AxeMesh = NewObject<UProceduralMeshComponent>(Owner, TEXT("CrbAvatarAxe"));
	AxeMesh->SetupAttachment(Root);
	AxeMesh->RegisterComponent();
	AxeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AxeMesh->SetVisibility(false);
	BuildAxe();
	AxeComp = AxeMesh;
	if (UStaticMesh* SM = LoadObject<UStaticMesh>(nullptr, AxeMeshPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		// The Leviathan Axe was re-framed by gow_build.py to the same local frame as the procedural axe.
		AxeStatic = NewObject<UStaticMeshComponent>(Owner, TEXT("CrbLeviathanAxe"));
		AxeStatic->SetupAttachment(Root);
		AxeStatic->RegisterComponent();
		AxeStatic->SetStaticMesh(SM);
		const FVector Ext = SM->GetBounds().BoxExtent;
		const float Longest = 2.f * FMath::Max3(Ext.X, Ext.Y, Ext.Z);
		if (Longest > KINDA_SMALL_NUMBER) AxeStatic->SetWorldScale3D(FVector(80.f / Longest)); // 0.8 m axe
		ApplySlotMaterials(AxeStatic, TEXT("SM_LeviathanAxe"), MaterialReport);
		AxeStatic->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		AxeStatic->SetVisibility(false);
		AxeComp = AxeStatic;
	}
}

// Original axe: a straight ash-coloured haft with a leather grip and a bearded steel head. Local frame: haft along +Z
// (grip at the origin), blade edge toward +X. Units are cm.
void FCrbPlayerAvatar::BuildAxe()
{
	TArray<FVector> V; TArray<int32> I; TArray<FVector> N; TArray<FLinearColor> C; TArray<FVector2D> UV; TArray<FProcMeshTangent> T;
	auto Quad = [&](FVector A, FVector B, FVector Cc, FVector D, FLinearColor Col)
	{
		const FVector Nn = FVector::CrossProduct(B - A, D - A).GetSafeNormal();
		const int32 Base = V.Num();
		for (const FVector& P : { A, B, Cc, D }) { V.Add(P); N.Add(Nn); C.Add(Col); UV.Add(FVector2D::ZeroVector); T.Add(FProcMeshTangent()); }
		I.Append({ Base, Base + 2, Base + 1, Base, Base + 3, Base + 2 });
	};
	auto Tri = [&](FVector A, FVector B, FVector Cc, FLinearColor Col)
	{
		const FVector Nn = FVector::CrossProduct(Cc - A, B - A).GetSafeNormal();
		const int32 Base = V.Num();
		for (const FVector& P : { A, B, Cc }) { V.Add(P); N.Add(Nn); C.Add(Col); UV.Add(FVector2D::ZeroVector); T.Add(FProcMeshTangent()); }
		I.Append({ Base, Base + 2, Base + 1 });
	};
	auto Box = [&](FVector Min, FVector Max, FLinearColor Col)
	{
		const FVector P[8] = { {Min.X,Min.Y,Min.Z},{Max.X,Min.Y,Min.Z},{Max.X,Max.Y,Min.Z},{Min.X,Max.Y,Min.Z},{Min.X,Min.Y,Max.Z},{Max.X,Min.Y,Max.Z},{Max.X,Max.Y,Max.Z},{Min.X,Max.Y,Max.Z} };
		Quad(P[0], P[1], P[2], P[3], Col * 0.8f); Quad(P[4], P[7], P[6], P[5], Col);
		Quad(P[0], P[4], P[5], P[1], Col * 0.9f); Quad(P[2], P[6], P[7], P[3], Col * 0.9f);
		Quad(P[1], P[5], P[6], P[2], Col * 0.95f); Quad(P[0], P[3], P[7], P[4], Col * 0.85f);
	};
	const FLinearColor Wood(0.36f, 0.22f, 0.11f), Grip(0.12f, 0.07f, 0.04f), Steel(0.55f, 0.57f, 0.6f), Edge(0.85f, 0.87f, 0.9f);
	Box(FVector(-1.6f, -1.6f, -14.f), FVector(1.6f, 1.6f, 48.f), Wood);       // haft
	Box(FVector(-2.0f, -2.0f, -10.f), FVector(2.0f, 2.0f, 6.f), Grip);        // wrapped grip
	Box(FVector(-2.4f, -2.4f, -17.f), FVector(2.4f, 2.4f, -14.f), Steel);     // pommel cap
	Box(FVector(-5.f, -1.4f, 38.f), FVector(4.f, 1.4f, 50.f), Steel);         // eye / poll
	// Bearded blade (a flat slab with a bevel), from the eye toward +X.
	const float Y0 = 0.9f;
	const FVector A(4, 0, 50), B(16, 0, 56), Cc(19, 0, 34), D(4, 0, 38), Beard(9, 0, 28);
	for (float S : { -1.f, 1.f })
	{
		const FVector O(0, S * Y0, 0);
		if (S > 0) { Quad(A + O, B + O, Cc + O, D + O, Steel); Tri(D + O, Cc + O, Beard + O, Steel); }
		else { Quad(A + O, D + O, Cc + O, B + O, Steel * 0.85f); Tri(D + O, Beard + O, Cc + O, Steel * 0.85f); }
	}
	const FVector Oy(0, Y0, 0);
	Quad(B - Oy, B + Oy, Cc + Oy, Cc - Oy, Edge);           // cutting edge
	Quad(A - Oy, A + Oy, B + Oy, B - Oy, Steel * 0.9f);     // top
	Quad(Cc - Oy, Cc + Oy, Beard + Oy, Beard - Oy, Edge * 0.9f);
	Quad(Beard - Oy, Beard + Oy, D + Oy, D - Oy, Steel * 0.8f);
	AxeMesh->CreateMeshSection_LinearColor(0, V, I, N, UV, C, T, false);
	if (AxeMat) AxeMesh->SetMaterial(0, AxeMat);
	AxeMesh->SetCastShadow(true);
}

FName FCrbPlayerAvatar::BoneName(const TCHAR* BaseName) const
{
	if (!Comp || !Comp->SkeletalMesh) return NAME_None;
	// Mixamo base names, with the Epic/Fortnite equivalents used by the Kratos rig.
	static const TMap<FString, FString> Epic = {
		{ TEXT("Hips"), TEXT("pelvis") }, { TEXT("Head"), TEXT("head") }, { TEXT("RightHand"), TEXT("hand_r") }, { TEXT("LeftHand"), TEXT("hand_l") },
		{ TEXT("RightForeArm"), TEXT("lowerarm_r") }, { TEXT("RightHandMiddle1"), TEXT("middle_01_r") }, { TEXT("RightHandThumb1"), TEXT("thumb_01_r") } };
	if (const FString* E = Epic.Find(BaseName)) { const FName Nm(**E); if (Comp->GetBoneIndex(Nm) != INDEX_NONE) return Nm; }
	for (const TCHAR* P : BonePrefixes)
	{
		const FName Nm(*(FString(P) + BaseName));
		if (Comp->GetBoneIndex(Nm) != INDEX_NONE) return Nm;
	}
	return NAME_None;
}

FVector FCrbPlayerAvatar::BoneLocation(const TCHAR* BaseName) const
{
	const FName Nm = BoneName(BaseName);
	return Nm.IsNone() ? FVector::ZeroVector : Comp->GetBoneLocation(Nm, EBoneSpaces::WorldSpace);
}

int32 FCrbPlayerAvatar::NumBones() const { return Comp && Comp->SkeletalMesh ? Comp->SkeletalMesh->GetRefSkeleton().GetNum() : 0; }

uint32 FCrbPlayerAvatar::SkeletonHash() const
{
	// CRC32 (zlib) of the sorted "child>parent\n" lines, Mixamo prefixes stripped. Avatar/blender/skin_build.py writes
	// the same value into skeleton.json so the test can compare the cooked skeleton against the rig that was built.
	if (!Comp || !Comp->SkeletalMesh) return 0;
	const FReferenceSkeleton& R = Comp->SkeletalMesh->GetRefSkeleton();
	TArray<FString> Lines;
	for (int32 B = 0; B < R.GetNum(); ++B)
	{
		const int32 P = R.GetParentIndex(B);
		Lines.Add((StripPrefix(R.GetBoneName(B).ToString()) + TEXT(">") + (P == INDEX_NONE ? FString() : StripPrefix(R.GetBoneName(P).ToString()))).ToLower());
	}
	Lines.Sort();
	FString All; for (const FString& L : Lines) { All += L; All += TEXT("\n"); }
	FTCHARToUTF8 U(*All);
	return FCrc::MemCrc32(U.Get(), U.Length());
}

const FCrbAvatarAnimDebug* FCrbPlayerAvatar::AnimDebug() const
{
	const UCrbAvatarAnimInstance* A = Comp ? Cast<UCrbAvatarAnimInstance>(Comp->GetAnimInstance()) : nullptr;
	return A ? &A->Debug : nullptr;
}

void FCrbPlayerAvatar::Tick(bool bEnabled, const FVector& Feet, float FacingYaw, const FCrbAvatarAnimInputs& Inputs, const FCrbAxeView& Axe, float Dt)
{
	const bool bShow = bEnabled && Mesh;
	if (!Comp) return;
	Comp->SetVisibility(bShow);
	AxeComp->SetVisibility(bShow || (bEnabled && Axe.bActive));
	if (!bShow) { bYawInit = false; return; }

	if (!bYawInit) { SmoothedYaw = FacingYaw; bYawInit = true; }
	const float Diff = FRotator::NormalizeAxis(FacingYaw - SmoothedYaw);
	SmoothedYaw = FRotator::NormalizeAxis(SmoothedYaw + FMath::Clamp(Diff, -720.f * Dt, 720.f * Dt));
	Comp->SetWorldLocationAndRotation(Feet, FRotator(0, SmoothedYaw + MeshYawOffset, 0));
	if (UCrbAvatarAnimInstance* A = Cast<UCrbAvatarAnimInstance>(Comp->GetAnimInstance()))
	{
		if (A->ClipRefs.Num() == 0) A->SetClips(Clips); // anim instance re-created (e.g. by a re-init)
		A->Inputs = Inputs;
	}

	// Axe: in the right hand (grip frame from the hand -> middle finger and hand -> thumb directions, so it does not
	// depend on how the importer oriented the bone axes), or flying/stuck/returning at the Java entity position.
	bAxeInHand = !Axe.bActive;
	if (bAxeInHand)
	{
		const FVector Hand = BoneLocation(TEXT("RightHand"));
		const FVector Mid = BoneLocation(TEXT("RightHandMiddle1"));
		const FVector Thumb = BoneLocation(TEXT("RightHandThumb1"));
		FVector F = Mid.IsZero() ? (Hand - BoneLocation(TEXT("RightForeArm"))).GetSafeNormal() : (Mid - Hand).GetSafeNormal();
		FVector Tb = Thumb.IsZero() ? FVector::UpVector : (Thumb - Hand).GetSafeNormal();
		if (F.IsNearlyZero()) F = Comp->GetForwardVector();
		FVector Haft = (Tb - F * FVector::DotProduct(Tb, F)).GetSafeNormal();
		if (Haft.IsNearlyZero()) Haft = FVector::UpVector;
		const FVector BladeDir = FVector::CrossProduct(Haft, F).GetSafeNormal();
		const FMatrix M(BladeDir, FVector::CrossProduct(Haft, BladeDir), Haft, FVector::ZeroVector);
		AxeWorld = Hand + F * 6.f;
		AxeComp->SetWorldLocationAndRotation(AxeWorld, M.Rotator());
		AxeAt = 0;
	}
	else
	{
		if (!Axe.Pos.Equals(AxeCur, 0.01f)) { AxePrev = AxeAt == 0 ? Axe.Pos : AxeCur; AxeCur = Axe.Pos; AxeAt = FPlatformTime::Seconds(); }
		const float Al = FMath::Clamp(float((FPlatformTime::Seconds() - AxeAt) / 0.05), 0.f, 1.f);
		AxeWorld = FMath::Lerp(AxePrev, AxeCur, Al);
		// Spin end over end around the axis perpendicular to the throw direction; stuck axes keep their last angle.
		if (Axe.Phase != 1) AxeSpin = FMath::Fmod(AxeSpin + (Axe.Phase == 2 ? -1.f : 1.f) * 1100.f * Dt, 360.f);
		const FRotator Base(0, Axe.Yaw, 0);
		AxeComp->SetWorldLocationAndRotation(AxeWorld, (FQuat(Base) * FQuat(FVector::RightVector, FMath::DegreesToRadians(AxeSpin)) * FQuat(FRotator(-90, 0, 0))).Rotator());
	}
}

void FCrbPlayerAvatar::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Mesh);
	Collector.AddReferencedObject(Comp);
	Collector.AddReferencedObject(AxeMesh);
	Collector.AddReferencedObject(AxeMat);
	Collector.AddReferencedObject(EnemyMesh);
	Collector.AddReferencedObject(AxeStatic);
	Collector.AddReferencedObject(AxeComp);
	for (FEnemy& E : Enemies) Collector.AddReferencedObject(E.Comp);
	for (UAnimSequence*& S : Clips) Collector.AddReferencedObject(S);
}

USkeletalMeshComponent* FCrbPlayerAvatar::MakeMeshComponent(const TCHAR* Name, USkeletalMesh* ForMesh)
{
	USkeletalMeshComponent* C = NewObject<USkeletalMeshComponent>(OwnerActor, MakeUniqueObjectName(OwnerActor, USkeletalMeshComponent::StaticClass(), Name));
	C->SetupAttachment(RootComp);
	C->RegisterComponent();
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetVisibility(false);
	C->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	C->bEnableUpdateRateOptimizations = false;
	if (ForMesh)
	{
		// Normalise to the intended height from the imported bounds, whatever unit scale the FBX importer applied
		// (Kratos 190 UU = 1.9 blocks, the Mutant 205 UU). Component scale scales its animation translations too.
		const float Imported = ForMesh->GetImportedBounds().BoxExtent.Z * 2.f;
		const float Target = ForMesh == Mesh ? 190.f : 205.f;
		if (Imported > KINDA_SMALL_NUMBER) C->SetWorldScale3D(FVector(Target / Imported));
		C->SetSkeletalMesh(ForMesh);
		C->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		C->SetAnimInstanceClass(UCrbAvatarAnimInstance::StaticClass());
		ApplySlotMaterials(C, ForMesh->GetName(), MaterialReport);
		if (UCrbAvatarAnimInstance* A = Cast<UCrbAvatarAnimInstance>(C->GetAnimInstance())) A->SetClips(Clips); // clips are skeleton-checked at load; the anim instance samples only the roles for its kind
	}
	return C;
}

float FCrbPlayerAvatar::ClipLength(ECrbClip C) const
{
	const UAnimSequence* S = Clips[(int32)C];
	return S ? S->SequenceLength : 0.f;
}

int32 FCrbPlayerAvatar::VisibleEnemies() const
{
	int32 N = 0;
	for (const FEnemy& E : Enemies) if (E.bLive && E.Comp && E.Comp->IsVisible()) ++N;
	return N;
}

void FCrbPlayerAvatar::TickEnemies(const TArray<FCrbMutantState>& Mutants, const FCrbCoords& Coords, float Dt)
{
	for (FEnemy& E : Enemies) E.bLive = false;
	if (EnemyMesh)
		for (const FCrbMutantState& M : Mutants)
		{
			FEnemy* E = Enemies.FindByPredicate([&](const FEnemy& X) { return X.Id == M.Id && X.Comp; });
			if (!E) E = Enemies.FindByPredicate([](const FEnemy& X) { return X.Id == 0 && X.Comp; });
			if (!E) { FEnemy N; N.Comp = MakeMeshComponent(TEXT("CrbMutant"), EnemyMesh); if (LastBrightness >= 0) { N.Comp->SetScalarParameterValueOnMaterials(TEXT("Brightness"), LastBrightness); N.Comp->SetScalarParameterValueOnMaterials(TEXT("LitWeight"), LastLitWeight); } Enemies.Add(N); E = &Enemies.Last(); }
			const FVector Pos = Coords.ToUE(M.X, M.Y, M.Z);
			if (E->Id != M.Id) { E->Id = M.Id; E->Prev = E->Cur = Pos; E->At = FPlatformTime::Seconds(); E->Anim = FCrbAvatarAnimInputs(); }
			if (!Pos.Equals(E->Cur, 0.01f)) { E->Prev = E->Cur; E->Cur = Pos; E->At = FPlatformTime::Seconds(); }
			E->Yaw = M.Yaw; E->Health = M.Health; E->MaxHealth = M.MaxHealth; E->DeathTime = M.DeathTime; E->bLive = true;
			E->Anim.bEnemy = true; E->Anim.bDead = M.DeathTime > 0 || M.Health <= 0;
		}
	for (FEnemy& E : Enemies)
	{
		if (!E.Comp) continue;
		if (!E.bLive) { E.Id = 0; E.Comp->SetVisibility(false); continue; }
		const float Al = FMath::Clamp(float((FPlatformTime::Seconds() - E.At) / 0.05), 0.f, 1.f);
		E.Comp->SetVisibility(true);
		E.Comp->SetWorldLocationAndRotation(FMath::Lerp(E.Prev, E.Cur, Al), FRotator(0, E.Yaw + MeshYawOffset, 0));
		if (UCrbAvatarAnimInstance* A = Cast<UCrbAvatarAnimInstance>(E.Comp->GetAnimInstance()))
		{
			if (A->ClipRefs.Num() == 0) A->SetClips(Clips);
			A->Inputs = E.Anim;
		}
	}
}

void FCrbPlayerAvatar::SetBrightness(float B, float LitWeight)
{
	if (FMath::IsNearlyEqual(B, LastBrightness, FMath::Max(1.f, B * 0.01f)) && FMath::IsNearlyEqual(LitWeight, LastLitWeight, 0.01f)) return;
	LastBrightness = B; LastLitWeight = LitWeight;
	static const FName Param(TEXT("Brightness")), Lit(TEXT("LitWeight"));
	auto Apply = [&](UMeshComponent* P) { if (P) { P->SetScalarParameterValueOnMaterials(Param, B); P->SetScalarParameterValueOnMaterials(Lit, LitWeight); } };
	Apply(Comp); Apply(AxeStatic);
	for (FEnemy& E : Enemies) Apply(E.Comp);
}
