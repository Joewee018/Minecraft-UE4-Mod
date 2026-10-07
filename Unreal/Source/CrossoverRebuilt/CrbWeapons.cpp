// Unreal-rendered add-on weapon. All world effects are Java's: the gravity gun only asks Java to grab/move/place;
// Unreal draws the gun, beam, orb and held block.
#include "CrbHost.h"
#include "CrbPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	void AddBox(TArray<FVector>& V, TArray<int32>& I, TArray<FVector>& N, TArray<FLinearColor>& C, TArray<FVector2D>& UV, const FVector& Center, const FVector& Half, const FLinearColor& Color)
	{
		static const FVector Normals[6] = { FVector(1,0,0), FVector(-1,0,0), FVector(0,1,0), FVector(0,-1,0), FVector(0,0,1), FVector(0,0,-1) };
		for (const FVector& Nn : Normals)
		{
			const FVector A = FMath::Abs(Nn.X) > 0 ? FVector(0, 1, 0) : FVector(1, 0, 0);
			const FVector B = FVector::CrossProduct(Nn, A);
			const FVector Fc = Center + Nn * Half;
			const FVector Sa = A * Half, Sb = B * Half;
			const int32 Base = V.Num();
			V.Append({ Fc - Sa - Sb, Fc + Sa - Sb, Fc + Sa + Sb, Fc - Sa + Sb });
			const float Shade = Nn.Z > 0 ? 1.f : (Nn.Z < 0 ? 0.55f : (FMath::Abs(Nn.X) > 0 ? 0.8f : 0.68f));
			for (int32 K = 0; K < 4; ++K) { N.Add(Nn); C.Add(FLinearColor(Color.R * Shade, Color.G * Shade, Color.B * Shade, 1)); }
			UV.Append({ FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) });
			I.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
		}
	}
}

void ACrbHost::BuildGunMesh()
{
	const int32 Kind = 0;
	if (Kind == GunMeshKind) return;
	GunMeshKind = Kind;
	TArray<FVector> V, N; TArray<int32> I; TArray<FLinearColor> C; TArray<FVector2D> UV;
	// Original blocky designs (camera-local cm: X forward, Y right, Z up).
	if (Kind == 0)
	{
		const FLinearColor Body(0.18f, 0.18f, 0.2f), Accent(0.95f, 0.55f, 0.12f), Core(0.4f, 0.9f, 1.f);
		AddBox(V, I, N, C, UV, FVector(38, 20, -18), FVector(15, 5, 5), Body);
		AddBox(V, I, N, C, UV, FVector(30, 20, -26), FVector(3, 3.5f, 7), Body);
		AddBox(V, I, N, C, UV, FVector(44, 20, -12.5f), FVector(9, 4, 1.5f), Accent);
		AddBox(V, I, N, C, UV, FVector(56, 20, -18), FVector(3, 6.5f, 6.5f), Accent);
		AddBox(V, I, N, C, UV, FVector(63, 14.5f, -18), FVector(5, 1, 1), Body);
		AddBox(V, I, N, C, UV, FVector(63, 25.5f, -18), FVector(5, 1, 1), Body);
		AddBox(V, I, N, C, UV, FVector(63, 20, -12.5f), FVector(5, 1, 1), Body);
		AddBox(V, I, N, C, UV, FVector(50, 20, -18), FVector(2, 3, 3), Core);
	}
	GunMesh->ClearAllMeshSections();
	GunMesh->CreateMeshSection_LinearColor(0, V, I, N, UV, C, TArray<FProcMeshTangent>(), false);
	GunMesh->SetMaterial(0, GunMid);
}

void ACrbHost::BuildHeldBlockMesh(int32 StateId)
{
	HeldStateMeshed = StateId;
	HeldBlock->ClearAllMeshSections();
	const FCrbModel* M = World.Model(StateId);
	if (!M) { HeldStateMeshed = -1; return; } // model not received yet; retried next frame
	TArray<FVector> V, N; TArray<int32> I; TArray<FLinearColor> C; TArray<FVector2D> UV0, UV1, UV2;
	static const float ShadeByFace[6] = { 0.5f, 1.0f, 0.8f, 0.8f, 0.6f, 0.6f };
	for (const FCrbQuad& Q : M->Quads)
	{
		const int32 Base = V.Num();
		const FVector McN = FCrbCoords::DirectionVector(Q.Face);
		for (int32 K = 0; K < 4; ++K)
		{
			V.Add(FCrbCoords::DirToUE(Q.Pos[K] - FVector(0.5f)) * 1.0f);
			N.Add(FCrbCoords::NormalToUE(McN));
			{ FVector2D Co, Fi; CrbSplitUV(Q.UV[K], Co, Fi); UV0.Add(Co); UV2.Add(Fi); }
			UV1.Add(FVector2D(15.5f / 16.f, 15.5f / 16.f));
			C.Add(FLinearColor(Q.Tint.R / 255.f, Q.Tint.G / 255.f, Q.Tint.B / 255.f, Q.Shade ? ShadeByFace[Q.Face] : 1.f));
		}
		I.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
	}
	if (V.Num()) HeldBlock->CreateMeshSection_LinearColor(0, V, I, N, UV0, UV1, UV2, TArray<FVector2D>(), C, TArray<FProcMeshTangent>(), false);
	HeldBlock->SetMaterial(0, HeldMid);
}

void ACrbHost::UpdateWeapons(float Dt)
{
	if (!Pawn) return;
	WeaponEquip = FMath::Min(1.f, WeaponEquip + Dt / 0.3f);
	const float Ease = 1.f - FMath::Pow(1.f - WeaponEquip, 3.f);
	const bool bGun = Weapon != ECrbWeapon::Hand && ViewMode == 0 && !IsMenuOpen();
	if (Weapon != ECrbWeapon::Hand) BuildGunMesh();
	GunMesh->SetVisibility(bGun);
	GunMesh->SetRelativeTransform(FTransform(FRotator(-25.f * (1.f - Ease), 0, 0), FVector(0, 0, -40.f * (1.f - Ease))) * ViewBob);
	if (GunMid) GunMid->SetScalarParameterValue(TEXT("EmissiveScale"), BaseSunLux / PI * 0.45f);

	const FVector Muzzle = Pawn->Camera->GetComponentTransform().TransformPosition(FVector(66, 20, -18 - 40.f * (1.f - Ease)));
	BeamPulse += Dt;
	// Gravity gun: orb at the muzzle, beam to the Java-held block, block model at Java's carry position.
	const bool bGravity = Weapon == ECrbWeapon::GravityGun;
	Orb->SetVisibility(bGun && bGravity);
	if (bGravity)
	{
		Orb->SetWorldLocation(Muzzle);
		const float R = (State.bGravityHolding ? 0.075f : 0.045f) * (1.f + 0.15f * FMath::Sin(BeamPulse * 9.f));
		Orb->SetWorldScale3D(FVector(R));
		OrbMid->SetScalarParameterValue(TEXT("Intensity"), BaseSunLux / PI * (State.bGravityHolding ? 1.6f : 0.9f));
	}
	const bool bHolding = State.bGravityHolding && State.GravityState > 0;
	if (bHolding && HeldStateMeshed != State.GravityState) BuildHeldBlockMesh(State.GravityState);
	HeldBlock->SetVisibility(bHolding && HeldStateMeshed == State.GravityState);
	if (bHolding)
	{
		const FVector Target = Coords.ToUE(State.GX, State.GY, State.GZ);
		HeldBlock->SetWorldLocationAndRotation(Target, FRotator(0, BeamPulse * 25.f, 0));
		const FVector From = bGun ? Muzzle : EyeLocationUE();
		const FVector D = Target - From;
		Beam->SetVisibility(bGravity && D.Size() > 10.f);
		Beam->SetWorldLocationAndRotation(From + D * 0.5f, FRotationMatrix::MakeFromZ(D.GetSafeNormal()).Rotator());
		Beam->SetWorldScale3D(FVector(0.035f + 0.01f * FMath::Sin(BeamPulse * 14.f), 0.035f, D.Size() / 100.f));
		BeamMid->SetScalarParameterValue(TEXT("Intensity"), BaseSunLux / PI * 1.2f);
	}
	else Beam->SetVisibility(false);

}
