// Minecraft Physics & Portal Mod (mod:physicsportal). Java owns the physics: crb.client.pp.PPController replaces the
// local player's travel step with Neverball-style momentum (rolling, slopes, launches, surface table, Gish modifiers,
// ragdoll on hard impacts) and crb.pp.Portals owns Portal A / B (placement, traversal with transformed momentum for the
// player and every entity). Unreal presents: the user's sculpted 3D Steve (FCrbPPSteve, native anim blueprint, slope IK,
// rolling spin, ragdoll), the portal views (FCrbPortalViews), UE4 physics objects that also use the portals
// (FCrbPPCubes), the orbit camera (turned with the player when a portal turns him), the HUD reticle and the debug menu
// (UCrbPPDebugComponent, F7, development builds only). Turning the mod off stops the "pp" input: Java hands the player
// back to vanilla movement, removes the portals and spawned physics objects; Unreal hides and cleans up its side.
#include "CrbHost.h"
#include "CrbPPDebug.h"
#include "CrbPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

bool ACrbHost::IsPPDebugOpen() const { return PPDebug && PPDebug->IsPanelOpen(); }

void ACrbHost::TogglePhysicsPortal()
{
	bPhysicsPortalEnabled = !bPhysicsPortalEnabled;
	if (bPhysicsPortalEnabled)
	{
		if (bSm64Enabled) ToggleSm64();
		if (bAvatarEnabled) ToggleAvatar();
		if (bCraft64Enabled) ToggleCraft64();
		if (bEldenCombatEnabled) ToggleEldenCombat();
		if (Weapon != ECrbWeapon::Hand) SelectWeapon(ECrbWeapon::Hand);
		LookPitch = FMath::Clamp(LookPitch, -70.f, 60.f);
		StatusLine = PPSteve.AssetsReady() ? FString(TEXT("Physics & Portal mod on: Shift roll, Z blue portal, X orange portal, C clear, F7 debug"))
			: TEXT("Physics & Portal mod on (3D Steve not cooked: ") + PPSteve.LoadError + TEXT(")");
	}
	else
	{
		// OFF: Unreal-side cleanup (Java removes portals / objects and restores vanilla movement on its own)
		PPCubes.Clear();
		if (PPDebug) { PPDebug->SetPanelOpen(false); PPDebug->SetMonitor(false); }
		StatusLine = TEXT("Physics & Portal mod off: vanilla movement");
	}
	SaveConfig();
	SendInput(true);
}

FString ACrbHost::PPCommand(const FString& Cmd, const TSharedPtr<FJsonObject>& Args)
{
	TSharedPtr<FJsonObject> A = Args.IsValid() ? Args : MakeShared<FJsonObject>();
	A->SetStringField(TEXT("cmd"), Cmd);
	return SendCommand(TEXT("pp.debug"), A);
}

void ACrbHost::PPSpawnCube()
{
	const FRotator R = ViewRotation();
	const FVector From = AvatarCameraPivot.IsNearlyZero() ? EyeLocationUE() : AvatarCameraPivot + FVector(0, 0, 40.f);
	PPCubes.Spawn(From + R.Vector() * 120.f, R.Vector() * 900.f + FVector(0, 0, 200.f));
}

void ACrbHost::TickPhysicsPortal(float Dt)
{
	const bool bOn = IsPhysicsPortalActive() && !IsAvatarActive();
	if (!PPDebug)
	{
#if !UE_BUILD_SHIPPING
		PPDebug = NewObject<UCrbPPDebugComponent>(this, TEXT("CrbPPDebug"));
		PPDebug->RegisterComponent();
		PPDebug->Host = this;
#endif
	}
	const FCrbPPState& P = State.PP;
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && bOn && !IsMenuOpen() && !bPPKeysFromTest)
	{
		if (PC->WasInputKeyJustPressed(EKeys::Z)) PPShootPortal(0);
		if (PC->WasInputKeyJustPressed(EKeys::X)) PPShootPortal(1);
		if (PC->WasInputKeyJustPressed(EKeys::C)) PPClearPortals();
	}
	if (PC && PPDebug && bOn && bPPDebugMode)
	{
		const FKey DebugKey(*PPDebugKey);
		if (DebugKey.IsValid() && PC->WasInputKeyJustPressed(DebugKey)) PPDebug->TogglePanel();
	}
	// a portal turned the player: turn the camera by the same amount (Portal-style continuity)
	if (PPSeenPortal < 0) PPSeenPortal = P.PortalSeq;
	if (P.PortalSeq != PPSeenPortal) { PPSeenPortal = P.PortalSeq; LookYaw = FMath::Fmod(LookYaw + P.YawDelta + 360.f, 360.f); }

	double X, Y, Z; PresentedFeet(X, Y, Z);
	const FVector Feet = Coords.ToUE(X, Y, Z);
	{
		const FLinearColor L = LightAt(State.EyeBlock, State.EyeSky);
		const float Level = FMath::Clamp(FMath::Max3(L.R, L.G, L.B), 0.05f, 1.f);
		PPSteve.SetBrightness(BaseSunLux / PI * VanillaWeight * Level, SunWeight);
	}
	FCrbPPSteve::FFrame F;
	F.bShow = bOn && State.bValid && !State.bDead && PPSteve.AssetsReady();
	F.Feet = Feet; F.Yaw = P.bActive ? P.BodyYaw : State.Yaw; F.Speed = P.Speed; F.State = P.bActive ? P.State : TEXT("IDLE");
	F.NormalUE = FCrbCoords::NormalToUE(FVector(P.NX, P.NY, P.NZ)).GetSafeNormal();
	F.bRagdoll = P.bRagdoll; F.RagdollSeq = P.RagdollSeq; F.ImpulseUE = FCrbCoords::DirToUE(FVector(P.IX, P.IY, P.IZ));
	F.Anim.State = F.State; F.Anim.Speed = P.Speed; F.Anim.ForceClip = PPForceClip; F.Anim.bPaused = bPPAnimPaused || P.bFrozen; F.Anim.PlaybackRate = P.TimeScale;
	PPSteve.Tick(this, F, Dt);
	if (bOn && State.bValid) UpdateOrbitCamera(PPSteve.IsRagdolling() ? PPSteve.RagdollPelvis - FVector(0, 0, 90.f) : Feet, PPCameraDistance, Dt);

	// portals (frames in UE space from Java's portal snapshot) + UE physics objects
	FCrbPortalFrame Frames[2];
	for (int32 I = 0; I < 2; ++I)
	{
		const FCrbPPState::FPortal& Q = P.Portals[I];
		Frames[I].bValid = bOn && Q.bValid; Frames[I].Id = I;
		if (!Frames[I].bValid) continue;
		Frames[I].C = Coords.ToUE(Q.X, Q.Y, Q.Z);
		Frames[I].N = FCrbCoords::NormalToUE(Q.N).GetSafeNormal();
		Frames[I].U = FCrbCoords::NormalToUE(Q.U).GetSafeNormal();
		Frames[I].R = FCrbCoords::NormalToUE(FVector::CrossProduct(Q.N, Q.U) * -1.f).GetSafeNormal();   // MC r = u x n
	}
	if (Pawn && Pawn->Camera) PortalViews.Tick(this, bOn, Frames, Pawn->Camera->GetComponentLocation(), Pawn->Camera->GetComponentRotation(), Pawn->Camera->FieldOfView, Dt);
	else PortalViews.Tick(this, false, Frames, FVector::ZeroVector, FRotator::ZeroRotator, 90.f, Dt);
	if (bOn) PPCubes.Tick(this, Frames, Dt);
	else if (PPCubes.Num() > 0) PPCubes.Clear();
}
