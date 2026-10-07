#include "CrbPortals.h"
#include "CrbHost.h"
#include "CrbPPSteve.h"
#include "Components/BoxComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	FCrbPPColliders GCubeColliders;   // block colliders for the UE physics objects (separate pool from the ragdoll's)
}

// ============================================================================================ portal views
void FCrbPortalViews::Init(AActor* Owner, USceneComponent* Root)
{
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Crb/PP/M_Portal.M_Portal"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	bMaterialReady = M != nullptr;
	for (int32 I = 0; I < 2; ++I)
	{
		Planes[I] = NewObject<UStaticMeshComponent>(Owner, *FString::Printf(TEXT("CrbPortalPlane%d"), I));
		Planes[I]->SetupAttachment(Root); Planes[I]->RegisterComponent();
		Planes[I]->SetStaticMesh(Plane); Planes[I]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Planes[I]->SetCastShadow(false); Planes[I]->SetVisibility(false);
		if (M)
		{
			Mids[I] = UMaterialInstanceDynamic::Create(M, Owner);
			Mids[I]->SetVectorParameterValue(TEXT("Rim"), I == 0 ? FLinearColor(0.1f, 0.5f, 1.f) : FLinearColor(1.f, 0.5f, 0.08f));
			Planes[I]->SetMaterial(0, Mids[I]);
		}
		Caps[I] = NewObject<USceneCaptureComponent2D>(Owner, *FString::Printf(TEXT("CrbPortalCapture%d"), I));
		Caps[I]->SetupAttachment(Root); Caps[I]->RegisterComponent();
		Caps[I]->bCaptureEveryFrame = false; Caps[I]->bCaptureOnMovement = false;
		Caps[I]->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		Caps[I]->bOverride_CustomNearClippingPlane = true;
		Caps[I]->ShowFlags.SetMotionBlur(false);
		Caps[I]->PostProcessBlendWeight = 0.f;
	}
}

void FCrbPortalViews::Tick(ACrbHost* Host, bool bOn, const FCrbPortalFrame InFrames[2], const FVector& CamPos, const FRotator& CamRot, float Fov, float Dt)
{
	Visible = 0;
	for (int32 I = 0; I < 2; ++I) Frames[I] = InFrames[I];
	const bool bLinked = Frames[0].bValid && Frames[1].bValid;
	FIntPoint VP(1280, 720);
	if (GEngine && GEngine->GameViewport) { FVector2D S; GEngine->GameViewport->GetViewportSize(S); if (S.X > 8) VP = FIntPoint((int32)S.X, (int32)S.Y); }
	const FIntPoint RtSize(FMath::Max(64, VP.X / 2), FMath::Max(64, VP.Y / 2));
	const FVector CamFwd = CamRot.Vector();
	const double Now = FPlatformTime::Seconds();
	for (int32 I = 0; I < 2; ++I)
	{
		const FCrbPortalFrame& P = Frames[I];
		const bool bShow = bOn && P.bValid;
		Planes[I]->SetVisibility(bShow);
		if (!bShow) continue;
		// the plane: 1 block wide (along R), 2 tall (along U), facing N
		Planes[I]->SetWorldLocationAndRotation(P.C, FRotationMatrix::MakeFromXZ(P.U, P.N).Rotator());
		Planes[I]->SetWorldScale3D(FVector(2.f, 1.f, 1.f));
		if (!Mids[I]) continue;
		Mids[I]->SetScalarParameterValue(TEXT("Open"), bLinked ? 1.f : 0.f);
		Mids[I]->SetScalarParameterValue(TEXT("Glow"), 2.5f + 0.8f * FMath::Sin((float)Now * 4.f + I));
		if (!bLinked) continue;
		// render only portals in front of the camera, facing it, within 64 blocks; at most 30 captures / s each
		const FVector ToP = P.C - CamPos;
		if (FVector::DotProduct(ToP, CamFwd) < -100.f || ToP.Size() > 6400.f || P.Local(CamPos).Z < 0.f) continue;
		++Visible;
		if (!Targets[I] || Targets[I]->SizeX != RtSize.X || Targets[I]->SizeY != RtSize.Y)
		{
			Targets[I] = NewObject<UTextureRenderTarget2D>(Planes[I]);
			Targets[I]->RenderTargetFormat = RTF_RGBA8; Targets[I]->ClearColor = FLinearColor::Black;
			Targets[I]->InitAutoFormat(RtSize.X, RtSize.Y); Targets[I]->UpdateResourceImmediate(true);
			Caps[I]->TextureTarget = Targets[I];
			Mids[I]->SetTextureParameterValue(TEXT("View"), Targets[I]);
		}
		if (Now - LastCapture[I] < 1.0 / 30.0) continue;
		LastCapture[I] = Now;
		// the view through portal I is what the camera would see after walking through I and out of the other one
		const FCrbPortalFrame& O = Frames[1 - I];
		const FVector VPos = P.MapPointTo(O, CamPos);
		const FVector VFwd = P.MapDirTo(O, CamFwd), VUp = P.MapDirTo(O, FRotationMatrix(CamRot).GetUnitAxis(EAxis::Z));
		Caps[I]->SetWorldLocationAndRotation(VPos, FRotationMatrix::MakeFromXZ(VFwd, VUp).Rotator());
		Caps[I]->FOVAngle = Fov;
		Caps[I]->CustomNearClippingPlane = FMath::Max(1.f, FMath::Abs(O.Local(VPos).Z) - 5.f);   // start at the exit surface
		Caps[I]->CaptureScene();
		++Captures;
	}
}

void FCrbPortalViews::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (int32 I = 0; I < 2; ++I) { Collector.AddReferencedObject(Planes[I]); Collector.AddReferencedObject(Caps[I]); Collector.AddReferencedObject(Targets[I]); Collector.AddReferencedObject(Mids[I]); }
}

// ============================================================================================ UE physics objects
void FCrbPPCubes::Init(AActor* InOwner, USceneComponent* InRoot)
{
	Owner = InOwner; Root = InRoot;
	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		Mat = UMaterialInstanceDynamic::Create(Base, Owner);
		Mat->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.35f, 0.85f, 0.35f));
	}
	GCubeColliders.Init(InOwner, InRoot);
}

void FCrbPPCubes::Spawn(const FVector& At, const FVector& Velocity)
{
	if (!Owner || !CubeMesh) return;
	UStaticMeshComponent* C = nullptr;
	for (UStaticMeshComponent* X : Cubes) if (X && !X->IsVisible()) { C = X; break; }   // pooled
	if (!C)
	{
		if (Cubes.Num() >= 12) C = Cubes[0];
		else
		{
			C = NewObject<UStaticMeshComponent>(Owner);
			C->SetupAttachment(Root); C->RegisterComponent();
			C->SetStaticMesh(CubeMesh); if (Mat) C->SetMaterial(0, Mat);
			C->SetWorldScale3D(FVector(0.5f));
			C->SetCollisionProfileName(TEXT("PhysicsActor"));
			Cubes.Add(C); Cooldown.Add(0);
		}
	}
	C->SetVisibility(true);
	C->SetSimulatePhysics(false);
	C->SetWorldLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
	C->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	C->SetSimulatePhysics(true);
	C->SetPhysicsLinearVelocity(Velocity);
}

void FCrbPPCubes::Clear()
{
	for (UStaticMeshComponent* C : Cubes) if (C) { C->SetSimulatePhysics(false); C->SetVisibility(false); C->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
	GCubeColliders.Clear();
}

int32 FCrbPPCubes::Num() const { int32 N = 0; for (UStaticMeshComponent* C : Cubes) if (C && C->IsVisible()) ++N; return N; }

void FCrbPPCubes::Tick(ACrbHost* Host, const FCrbPortalFrame Frames[2], float Dt)
{
	if (Num() == 0) return;
	GCubeColliders.Begin();
	const double Now = FPlatformTime::Seconds();
	const bool bLinked = Frames[0].bValid && Frames[1].bValid;
	for (int32 K = 0; K < Cubes.Num(); ++K)
	{
		UStaticMeshComponent* C = Cubes[K];
		if (!C || !C->IsVisible()) continue;
		const FVector P = C->GetComponentLocation();
		GCubeColliders.Around(Host, P, 1);
		if (!bLinked || Now < Cooldown[K]) continue;
		const FVector V = C->GetPhysicsLinearVelocity();
		for (int32 I = 0; I < 2; ++I)
		{
			const FCrbPortalFrame& A = Frames[I];
			const FVector L = A.Local(P);
			if (FVector::DotProduct(V, A.N) > -5.f || L.Z > 45.f || L.Z < -60.f || FMath::Abs(L.X) > 60.f || FMath::Abs(L.Y) > 110.f) continue;
			const FCrbPortalFrame& B = Frames[1 - I];
			const FVector Out = B.C - B.R * FMath::Clamp(L.X, -40.f, 40.f) + B.U * FMath::Clamp(L.Y, -80.f, 80.f) + B.N * 50.f;
			C->SetWorldLocation(Out, false, nullptr, ETeleportType::TeleportPhysics);
			C->SetPhysicsLinearVelocity(A.MapDirTo(B, V));                               // momentum through the portal
			C->SetPhysicsAngularVelocityInDegrees(A.MapDirTo(B, C->GetPhysicsAngularVelocityInDegrees()));
			Cooldown[K] = Now + 0.25; ++Teleports;
			break;
		}
		if (P.Z < -100000.f) { C->SetSimulatePhysics(false); C->SetVisibility(false); }
	}
	GCubeColliders.End();
}

void FCrbPPCubes::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (UStaticMeshComponent*& C : Cubes) Collector.AddReferencedObject(C);
	Collector.AddReferencedObject(CubeMesh); Collector.AddReferencedObject(Mat);
	for (UBoxComponent*& B : GCubeColliders.Boxes) Collector.AddReferencedObject(B);
}
