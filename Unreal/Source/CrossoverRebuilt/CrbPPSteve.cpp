#include "CrbPPSteve.h"
#include "CrbHost.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Misc/Crc.h"

const TCHAR* CrbPPClipName(ECrbPPClip C)
{
	static const TCHAR* Names[] = { TEXT("Idle"), TEXT("Walk"), TEXT("Run"), TEXT("Jump"), TEXT("Fall"), TEXT("Land"), TEXT("Roll"), TEXT("Slide"),
		TEXT("Launch"), TEXT("PortalEnter"), TEXT("PortalExit"), TEXT("HitReact") };
	static_assert(UE_ARRAY_COUNT(Names) == (int32)ECrbPPClip::Count, "pp clip names");
	return (int32)C < (int32)ECrbPPClip::Count ? Names[(int32)C] : TEXT("");
}

// ============================================================================================ animation blueprint (native)
void UCrbPPAnimInstance::SetClips(UAnimSequence* const InClips[(int32)ECrbPPClip::Count])
{
	ClipRefs.SetNum((int32)ECrbPPClip::Count);
	for (int32 I = 0; I < (int32)ECrbPPClip::Count; ++I) ClipRefs[I] = InClips[I];
}

void FCrbPPAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	UCrbPPAnimInstance* A = CastChecked<UCrbPPAnimInstance>(InAnimInstance);
	Debug.ClipsLoaded = 0;
	for (int32 I = 0; I < (int32)ECrbPPClip::Count; ++I) { Clips[I] = A->ClipRefs.IsValidIndex(I) ? A->ClipRefs[I] : nullptr; if (Clips[I]) ++Debug.ClipsLoaded; }
	In = A->Inputs;
}

void FCrbPPAnimProxy::PostUpdate(UAnimInstance* InAnimInstance) const
{
	FAnimInstanceProxy::PostUpdate(InAnimInstance);
	CastChecked<UCrbPPAnimInstance>(InAnimInstance)->Debug = Debug;
}

float FCrbPPAnimProxy::Len(ECrbPPClip C) const { const UAnimSequence* S = Clips[(int32)C]; return S ? FMath::Max(0.05f, S->SequenceLength) : 1.f; }

void FCrbPPAnimProxy::Start(ECrbPPClip C, bool bLoop, float Fade)
{
	Prev = Cur; Cur.Clip = C; Cur.Time = 0; Cur.bLoop = bLoop;
	Blend = 0.f; BlendTime = FMath::Max(0.01f, Fade); ++Debug.Changes;
}

void FCrbPPAnimProxy::Update(float Dt)
{
	FAnimInstanceProxy::Update(Dt);
	Dt = FMath::Clamp(Dt, 0.f, 0.1f) * (In.bPaused ? 0.f : FMath::Clamp(In.PlaybackRate, 0.f, 4.f));
	// ---- state machine: Java physics state -> clip (transition fade per target state)
	const FString& S = In.State;
	ECrbPPClip Want = ECrbPPClip::Idle; bool bLoop = true; float Fade = 0.18f;
	if (S == TEXT("WALK")) Want = ECrbPPClip::Walk;
	else if (S == TEXT("RUN")) { Want = ECrbPPClip::Run; Fade = 0.15f; }
	else if (S == TEXT("JUMP")) { Want = ECrbPPClip::Jump; bLoop = false; Fade = 0.06f; }
	else if (S == TEXT("FALL")) { Want = ECrbPPClip::Fall; Fade = 0.2f; }
	else if (S == TEXT("LAND")) { Want = ECrbPPClip::Land; bLoop = false; Fade = 0.05f; }
	else if (S == TEXT("ROLL")) { Want = ECrbPPClip::Roll; Fade = 0.1f; }
	else if (S == TEXT("SLIDE")) { Want = ECrbPPClip::Slide; Fade = 0.2f; }
	else if (S == TEXT("LAUNCH")) { Want = ECrbPPClip::Launch; bLoop = false; Fade = 0.08f; }
	else if (S == TEXT("PORTAL_ENTER")) { Want = ECrbPPClip::PortalEnter; bLoop = false; Fade = 0.05f; }
	else if (S == TEXT("PORTAL_EXIT")) { Want = ECrbPPClip::PortalExit; bLoop = false; Fade = 0.05f; }
	else if (S == TEXT("HIT")) { Want = ECrbPPClip::HitReact; bLoop = false; Fade = 0.04f; }
	else if (S == TEXT("RAGDOLL")) { Want = ECrbPPClip::Fall; }
	if (In.ForceClip >= 0 && In.ForceClip < (int32)ECrbPPClip::Count) { Want = (ECrbPPClip)In.ForceClip; bLoop = true; }
	const FString NewKey = FString::Printf(TEXT("%d|%s"), (int32)Want, *S);
	if (NewKey != Key) { Key = NewKey; if (Want != Cur.Clip || !bLoop) Start(Want, bLoop, Fade); }
	// locomotion clips play at the physics speed (feet match the ground speed)
	Rate = 1.f;
	if (Cur.Clip == ECrbPPClip::Walk) Rate = FMath::Clamp(In.Speed / FMath::Max(0.5f, In.WalkSpeed), 0.4f, 2.f);
	else if (Cur.Clip == ECrbPPClip::Run) Rate = FMath::Clamp(In.Speed / FMath::Max(0.5f, In.RunSpeed), 0.6f, 2.2f);
	auto Advance = [&](FPlay& P, float R) { P.Time += Dt * R; if (P.bLoop) P.Time = FMath::Fmod(P.Time, Len(P.Clip)); else P.Time = FMath::Min(P.Time, Len(P.Clip)); };
	Advance(Cur, Rate); Advance(Prev, 1.f);
	// a finished jump / launch settles into the fall loop
	if (!Cur.bLoop && Cur.Time >= Len(Cur.Clip) && (Cur.Clip == ECrbPPClip::Jump || Cur.Clip == ECrbPPClip::Launch || Cur.Clip == ECrbPPClip::PortalExit) && S != TEXT("LAND"))
		Start(ECrbPPClip::Fall, true, 0.25f);
	if (Dt > 0) Blend = FMath::Min(1.f, Blend + Dt / BlendTime);
	// slope IK: tilt the whole skeleton toward the ground normal (feet follow the surface), only on the ground
	const bool bGroundState = S == TEXT("IDLE") || S == TEXT("WALK") || S == TEXT("RUN") || S == TEXT("SLIDE") || S == TEXT("LAND");
	FQuat Want64 = FQuat::Identity;
	if (bGroundState)
	{
		FVector N = In.SlopeNormal.GetSafeNormal();
		if (N.Z > 0.5f) { Want64 = FQuat::FindBetweenNormals(FVector::UpVector, N); FVector Axis; float Ang; Want64.ToAxisAndAngle(Axis, Ang); Want64 = FQuat(Axis, FMath::Clamp(Ang, 0.f, FMath::DegreesToRadians(25.f))); }
	}
	Tilt = FQuat::Slerp(Tilt, Want64, FMath::Clamp(Dt * 8.f, 0.f, 1.f));

	Debug.Clip = CrbPPClipName(Cur.Clip); Debug.PrevClip = CrbPPClipName(Prev.Clip);
	Debug.Time = Cur.Time; Debug.Length = Len(Cur.Clip); Debug.Blend = Blend; Debug.Rate = Rate; Debug.AnimState = S;
}

void FCrbPPAnimProxy::Sample(ECrbPPClip C, float Time, bool bLoop, FPoseContext& Out) const
{
	USkeleton* Mine = const_cast<FCrbPPAnimProxy*>(this)->GetSkeleton();
	const UAnimSequence* Sq = Clips[(int32)C];
	if (!Sq || Sq->GetSkeleton() != Mine) Sq = Clips[(int32)ECrbPPClip::Idle];
	if (!Sq || Sq->GetSkeleton() != Mine) { Out.ResetToRefPose(); return; }
	const float L = FMath::Max(0.05f, Sq->SequenceLength);
	const float Tm = bLoop ? FMath::Fmod(FMath::Max(0.f, Time), L) : FMath::Clamp(Time, 0.f, L);
	FAnimationPoseData Data(Out);
	Sq->GetAnimationPose(Data, FAnimExtractContext(Tm, false));
}

bool FCrbPPAnimProxy::Evaluate(FPoseContext& Output)
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
	// slope IK on the root bone (the root carries the whole body and the IK helper bones)
	const FCompactPoseBoneIndex RootI(0);
	if (Output.Pose.IsValidIndex(RootI) && !Tilt.Equals(FQuat::Identity, 1e-4f)) Output.Pose[RootI].SetRotation(Tilt * Output.Pose[RootI].GetRotation());
	Output.Pose.NormalizeRotations();
	return true;
}

// ============================================================================================ block colliders
void FCrbPPColliders::Init(AActor* InOwner, USceneComponent* InRoot) { Owner = InOwner; Root = InRoot; }

void FCrbPPColliders::Around(ACrbHost* Host, const FVector& UE, int32 Radius)
{
	if (!Host || !Owner) return;
	double X, Y, Z; Host->Coords.ToMC(UE, X, Y, Z);
	const FIntVector C(FMath::FloorToInt(X), FMath::FloorToInt(Y), FMath::FloorToInt(Z));
	for (int32 DX = -Radius; DX <= Radius; ++DX) for (int32 DY = -Radius; DY <= Radius; ++DY) for (int32 DZ = -Radius; DZ <= Radius; ++DZ)
	{
		const FIntVector B = C + FIntVector(DX, DY, DZ);
		if (Seen.Contains(B) || !Host->World.IsSolidAt(B)) continue;
		Seen.Add(B);
		if (Used >= 256) return;
		if (!Boxes.IsValidIndex(Used))
		{
			UBoxComponent* Bx = NewObject<UBoxComponent>(Owner);
			Bx->SetupAttachment(Root); Bx->RegisterComponent();
			Bx->SetBoxExtent(FVector(50.f)); Bx->SetCollisionProfileName(TEXT("BlockAll")); Bx->SetHiddenInGame(true);
			Boxes.Add(Bx);
		}
		UBoxComponent* Bx = Boxes[Used++];
		Bx->SetWorldLocation(Host->Coords.ToUE(B.X + 0.5, B.Y + 0.5, B.Z + 0.5));
		Bx->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
}

void FCrbPPColliders::End()
{
	for (int32 I = Used; I < Boxes.Num(); ++I) if (Boxes[I]) Boxes[I]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Seen.Reset();
}

// ============================================================================================ character
void FCrbPPSteve::Init(AActor* Owner, USceneComponent* Root)
{
	RootComp = Root;
	Colliders.Init(Owner, Root);
	Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Crb/PP/SK_PPSteve.SK_PPSteve"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Mesh) LoadError = TEXT("SK_PPSteve not cooked (run Tools\\PPBuild.ps1, then the asset build)");
	for (int32 I = 0; I < (int32)ECrbPPClip::Count; ++I)
	{
		const FString N = CrbPPClipName((ECrbPPClip)I);
		UAnimSequence* S = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/Crb/PP/P_%s.P_%s"), *N, *N), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (S && Mesh && S->GetSkeleton() != Mesh->GetSkeleton()) { S = nullptr; LoadError += TEXT(" ") + N + TEXT(": wrong skeleton;"); }
		Clips[I] = S; if (S) ++ClipsLoaded; else MissingClips.Add(N);
	}
	if (!Mesh) return;
	Comp = NewObject<USkeletalMeshComponent>(Owner, TEXT("CrbPPSteveMesh"));
	Comp->SetupAttachment(Root); Comp->RegisterComponent();
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision); Comp->SetVisibility(false); Comp->SetCastShadow(true);
	Comp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Comp->bEnableUpdateRateOptimizations = false;
	const float Imported = Mesh->GetImportedBounds().BoxExtent.Z * 2.f;
	if (Imported > KINDA_SMALL_NUMBER) Comp->SetWorldScale3D(FVector(HeightUU / Imported));
	Comp->SetSkeletalMesh(Mesh);
	Comp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Comp->SetAnimInstanceClass(UCrbPPAnimInstance::StaticClass());
	if (UCrbPPAnimInstance* A = Cast<UCrbPPAnimInstance>(Comp->GetAnimInstance())) A->SetClips(Clips);
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Crb/PP/M_PPSteve.M_PPSteve"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	for (int32 I = 0; I < FMath::Max(1, Comp->GetNumMaterials()); ++I)
	{
		UMaterialInstanceDynamic* M = Parent ? UMaterialInstanceDynamic::Create(Parent, Owner) : nullptr;
		if (M) Comp->SetMaterial(I, M);
		Mids.Add(M);
	}
}

const FCrbPPAnimDebug* FCrbPPSteve::AnimDebug() const
{
	const UCrbPPAnimInstance* A = Comp ? Cast<UCrbPPAnimInstance>(Comp->GetAnimInstance()) : nullptr;
	return A ? &A->Debug : nullptr;
}

int32 FCrbPPSteve::NumBones() const { return Comp && Comp->SkeletalMesh ? Comp->SkeletalMesh->GetRefSkeleton().GetNum() : 0; }
bool FCrbPPSteve::HasPhysicsAsset() const { return Comp && Comp->GetPhysicsAsset() && Comp->GetPhysicsAsset()->SkeletalBodySetups.Num() > 0; }

uint32 FCrbPPSteve::SkeletonHash() const
{
	if (!Comp || !Comp->SkeletalMesh) return 0;
	const FReferenceSkeleton& R = Comp->SkeletalMesh->GetRefSkeleton();
	TArray<FString> Lines;
	for (int32 B = 0; B < R.GetNum(); ++B) { const int32 P = R.GetParentIndex(B); Lines.Add((R.GetBoneName(B).ToString() + TEXT(">") + (P == INDEX_NONE ? FString() : R.GetBoneName(P).ToString())).ToLower()); }
	Lines.Sort();
	FString All; for (const FString& L : Lines) { All += L; All += TEXT("\n"); }
	FTCHARToUTF8 U(*All);
	return FCrc::MemCrc32(U.Get(), U.Length());
}

void FCrbPPSteve::StartRagdoll(const FVector& Impulse)
{
	if (!Comp || !HasPhysicsAsset()) return;
	bRagdoll = true;
	Comp->SetCollisionObjectType(ECC_PhysicsBody);
	Comp->SetCollisionResponseToAllChannels(ECR_Block);
	Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Comp->SetAllBodiesSimulatePhysics(true);
	Comp->SetSimulatePhysics(true);
	Comp->WakeAllRigidBodies();
	Comp->SetAllPhysicsLinearVelocity(Impulse);
}

void FCrbPPSteve::EndRagdoll()
{
	bRagdoll = false;
	if (!Comp) return;
	Comp->SetSimulatePhysics(false);
	Comp->SetAllBodiesSimulatePhysics(false);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (RootComp) Comp->AttachToComponent(RootComp, FAttachmentTransformRules::KeepWorldTransform);
	Colliders.Clear();
}

void FCrbPPSteve::Tick(ACrbHost* Host, const FFrame& F, float Dt)
{
	if (!Comp) return;
	Comp->SetVisibility(F.bShow);
	if (!F.bShow) { bYawInit = false; if (bRagdoll) EndRagdoll(); return; }
	// ragdoll edges come from Java's ragdoll counter / state
	if (F.bRagdoll && F.RagdollSeq != SeenRagdoll) { SeenRagdoll = F.RagdollSeq; if (!bRagdoll) StartRagdoll(F.ImpulseUE); }
	if (!F.bRagdoll && bRagdoll) EndRagdoll();
	if (bRagdoll)
	{
		// keep collision boxes on the blocks around the tumbling body (pooled, rebuilt per frame from block data)
		const FVector P = Comp->GetBoneLocation(TEXT("pelvis"));
		RagdollPelvis = P;
		Colliders.Begin(); Colliders.Around(Host, P, 2); Colliders.Around(Host, F.Feet, 1); Colliders.End();
		return;
	}
	if (!bYawInit) { SmoothedYaw = F.Yaw; bYawInit = true; }
	SmoothedYaw = FRotator::NormalizeAxis(SmoothedYaw + FRotator::NormalizeAxis(F.Yaw - SmoothedYaw) * FMath::Clamp(Dt * 18.f, 0.f, 1.f));
	// rolling: the tucked ball spins about its centre from the real rolling speed (no roll = upright)
	const bool bRoll = F.State == TEXT("ROLL");
	if (bRoll) RollAngle = FMath::Fmod(RollAngle + F.Speed * 100.f / 45.f * Dt * 57.2958f, 360.f);
	else RollAngle = FMath::FInterpTo(RollAngle > 180.f ? RollAngle - 360.f : RollAngle, 0.f, Dt, 12.f);
	const FQuat Body = FQuat(FRotator(-RollAngle, SmoothedYaw, 0));
	const FVector Pivot = F.Feet + FVector(0, 0, 45.f);
	const FVector Loc = Pivot + Body.RotateVector(FVector(0, 0, -45.f));
	Comp->SetWorldLocationAndRotation(Loc, Body * FQuat(FRotator(0, MeshYawOffset, 0)));
	if (UCrbPPAnimInstance* A = Cast<UCrbPPAnimInstance>(Comp->GetAnimInstance()))
	{
		if (A->ClipRefs.Num() == 0) A->SetClips(Clips);
		A->Inputs = F.Anim;
		A->Inputs.SlopeNormal = Comp->GetComponentTransform().InverseTransformVectorNoScale(F.NormalUE);
	}
}

void FCrbPPSteve::SetBrightness(float B, float Lit)
{
	if (FMath::IsNearlyEqual(B, LastBrightness, FMath::Max(1.f, B * 0.01f)) && FMath::IsNearlyEqual(Lit, LastLit, 0.01f)) return;
	LastBrightness = B; LastLit = Lit;
	for (UMaterialInstanceDynamic* M : Mids) if (M) { M->SetScalarParameterValue(TEXT("Brightness"), B); M->SetScalarParameterValue(TEXT("LitWeight"), Lit); }
}

void FCrbPPSteve::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Mesh); Collector.AddReferencedObject(Comp); Collector.AddReferencedObject(RootComp);
	for (UMaterialInstanceDynamic*& M : Mids) Collector.AddReferencedObject(M);
	for (UAnimSequence*& S : Clips) Collector.AddReferencedObject(S);
	for (UBoxComponent*& B : Colliders.Boxes) Collector.AddReferencedObject(B);
}
