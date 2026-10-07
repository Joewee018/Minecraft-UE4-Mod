// God of War Unity port (mod:avatar): play as the Mixamo Mutant with the iltenahmet/god-of-war-unity controls.
//   PlayerController: strafe-style movement (the body turns to the camera yaw while moving or attacking, keeps its yaw
//   when idle), no movement while the melee clip plays; ThrowAxe at the throw clip's event toward the point under the
//   camera centre; RecallAxe on R / middle mouse.  AxeController: 30 damage, 1 s cooldown (Java AxeEntity / gow.melee).
// Java stays authoritative: movement is ordinary input, damage and the axe are server-side.
#include "CrbHost.h"
#include "CrbPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"

namespace
{
	constexpr float MeleeHitFraction = 0.45f;  // point of the downward swing where the axe connects
	// The repo's throw clip fires ThrowAxe at 0.733 s of 2.267 s; the port's throw reuses the overhand swing and
	// releases at the same fraction of the clip.
	constexpr float ThrowReleaseFraction = 0.733f / 2.267f;
}

float ACrbHost::AttackDuration() const { const float L = PlayerAvatar.ClipLength(ECrbClip::Attack); return (L > 0 ? L : 1.6f) / UCrbAvatarAnimInstance::AttackRate; }
float ACrbHost::ThrowDuration() const { const float L = PlayerAvatar.ClipLength(ECrbClip::Attack); return (L > 0 ? L : 1.6f) / UCrbAvatarAnimInstance::ThrowRate; }

void ACrbHost::ToggleAvatar()
{
	bAvatarEnabled = !bAvatarEnabled;
	if (bAvatarEnabled) { bSm64Enabled = false; if (bCraft64Enabled) ToggleCraft64(); if (bPhysicsPortalEnabled) TogglePhysicsPortal(); if (bEldenCombatEnabled) ToggleEldenCombat(); } // one player-character mod at a time
	SaveConfig();
	bThrowPending = bMeleePending = false;
	if (bAvatarEnabled) { AvatarFacingYaw = LookYaw; LookPitch = FMath::Clamp(LookPitch, -70.f, 60.f); }
	if (bAvatarEnabled && !PlayerAvatar.AssetsReady()) StatusLine = TEXT("God of War mod: ") + PlayerAvatar.LoadError;
	SendInput(true);
}

void ACrbHost::SpawnMutant()
{
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetNumberField(TEXT("distance"), 6);
	if (IsAvatarActive()) A->SetNumberField(TEXT("yaw"), LookYaw); // in front of the camera, not the body
	SendCommand(TEXT("gow.mutant.spawn"), A);
}

void ACrbHost::ClearMutants() { SendCommand(TEXT("gow.mutant.clear")); }

void ACrbHost::AvatarRecall()
{
	if (!IsAvatarActive() || !State.bAxeActive) return;
	SendCommand(TEXT("avatar.axe.recall"));
	++AxeRecallsSent;
}

void ACrbHost::AvatarPrimary()
{
	// Attack1: one swing at a time (the Animator state must finish before it can re-enter).
	if (IsAvatarAttacking() || FPlatformTime::Seconds() - ThrowStartedAt < ThrowDuration()) return;
	++AvatarAnim.AttackSerial;
	AttackStartedAt = FPlatformTime::Seconds();
	bMeleePending = true;
}

bool ACrbHost::AvatarSecondary(bool bDown)
{
	if (!IsAvatarActive() || !IsMainHandEmpty()) return false; // holding an item: vanilla use
	if (!bDown) return true;
	const double Now = FPlatformTime::Seconds();
	if (State.bAxeActive || bThrowPending || IsAvatarAttacking() || Now - ThrowStartedAt < ThrowDuration()) return true;
	++AvatarAnim.ThrowSerial;
	ThrowStartedAt = Now;
	bThrowPending = true;
	return true;
}

void ACrbHost::AvatarMoveInput(float& Fwd, float& Strafe, float& Yaw) const
{
	const bool bMoving = FMath::Abs(Fwd) > 0.1f || FMath::Abs(Strafe) > 0.1f;
	const bool bCombat = IsAvatarAttacking() || FPlatformTime::Seconds() - ThrowStartedAt < ThrowDuration();
	if (IsAvatarAttacking()) { Fwd = 0; Strafe = 0; }          // PlayerController: speed = 0 while attacking
	Yaw = (bMoving || bCombat) ? LookYaw : AvatarFacingYaw;   // turn to the camera when moving/attacking
}

void ACrbHost::TickAvatarMode(float Dt)
{
	PlayerAvatar.TickEnemies(State.Mutants, Coords, Dt); // enemies are drawn whether or not the player mod is on
	{
		// Vanilla entity brightness: Minecraft's light map at the player's eye (block light, sky light).
		// Same radiance scale as the world's emissive vanilla term (EmissiveScale = BaseSunLux / pi, x VanillaWeight).
		const FLinearColor L = LightAt(State.EyeBlock, State.EyeSky);
		const float Level = FMath::Clamp(FMath::Max3(L.R, L.G, L.B), 0.05f, 1.f);
		PlayerAvatar.SetBrightness(BaseSunLux / PI * VanillaWeight * Level, SunWeight);
	}
	const bool bActive = IsAvatarActive() && State.bValid;
	double X, Y, Z; PresentedFeet(X, Y, Z);
	const FVector Feet = Coords.ToUE(X, Y, Z);
	const double Now = FPlatformTime::Seconds();
	if (bActive)
	{
		float F = MoveForward, S = MoveStrafe, Yw = AvatarFacingYaw;
		if (!IsMenuOpen()) { AvatarMoveInput(F, S, Yw); AvatarFacingYaw = Yw; }

		// Melee hit and throw release at their clip moments.
		if (bMeleePending && Now - AttackStartedAt >= AttackDuration() * MeleeHitFraction)
		{
			bMeleePending = false;
			if (!State.bAxeActive) { SendCommand(TEXT("gow.melee")); ++MeleeSent; }
		}
		if (bThrowPending && Now - ThrowStartedAt >= ThrowDuration() * ThrowReleaseFraction)
		{
			bThrowPending = false;
			// SetAxeThrowDirection: ray from the camera through the screen centre; first solid block or Mutant, else 64 m.
			const FVector CamLoc = Pawn ? Pawn->Camera->GetComponentLocation() : Feet;
			const FVector CamDir = Pawn ? Pawn->Camera->GetForwardVector() : ViewRotation().Vector();
			FVector Hit = CamLoc + CamDir * 6400.f;
			for (float D = 50.f; D <= 6400.f; D += 20.f)
			{
				const FVector P = CamLoc + CamDir * D;
				double MX, MY, MZ; Coords.ToMC(P, MX, MY, MZ);
				bool bHit = World.IsSolidAt(FIntVector(FMath::FloorToInt(MX), FMath::FloorToInt(MY), FMath::FloorToInt(MZ)));
				for (const FCrbMutantState& M : State.Mutants)
					if (!bHit && M.Health > 0 && FVector2D(MX - M.X, MZ - M.Z).Size() < 0.6 && MY > M.Y && MY < M.Y + 2.1) bHit = true;
				if (bHit) { Hit = P; break; }
			}
			FVector Hand = PlayerAvatar.BoneLocation(TEXT("RightHand"));
			if (Hand.IsZero()) Hand = Feet + FVector(0, 0, 150.f);
			const FVector Dir = (Hit - Hand).GetSafeNormal();
			double OX, OY, OZ; Coords.ToMC(Hand, OX, OY, OZ);
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetNumberField(TEXT("ox"), OX); A->SetNumberField(TEXT("oy"), OY); A->SetNumberField(TEXT("oz"), OZ);
			A->SetNumberField(TEXT("dx"), -Dir.Y); A->SetNumberField(TEXT("dy"), Dir.Z); A->SetNumberField(TEXT("dz"), Dir.X); // UE -> MC direction
			SendCommand(TEXT("avatar.axe.throw"), A);
			LastThrowTarget = Hit;
			++AxeThrowsSent;
		}

		// Animation inputs: velocity in the character's own frame (strafe locomotion).
		// Velocity from consecutive Java positions (deltaMovement is post-friction and reads ~half the real speed).
		const double TickDt = FMath::Max<int64>(1, State.Tick - PrevState.Tick) / 20.0;
		const bool bFresh = PrevState.bValid && State.Tick > PrevState.Tick && State.Tick - PrevState.Tick < 10;
		const FVector V = bFresh ? FVector(float((State.Z - PrevState.Z) / TickDt), -float((State.X - PrevState.X) / TickDt), 0.f) : FVector::ZeroVector; // blocks/s, UE axes
		const FVector Fw = FRotator(0, AvatarFacingYaw, 0).Vector(), Rt = FRotator(0, AvatarFacingYaw + 90.f, 0).Vector();
		AvatarAnim.LocalForward = FVector::DotProduct(V, Fw); AvatarAnim.LocalRight = FVector::DotProduct(V, Rt);
		AvatarAnim.VerticalSpeed = (float)State.VY * 20.f;
		AvatarAnim.bGrounded = State.bOnGround; AvatarAnim.bFlying = State.bFlying; AvatarAnim.bDead = State.bDead; AvatarAnim.bSprint = State.bSprint;
		AvatarAnim.bEnemy = false;
		if (bPrevAxeActive && !State.bAxeActive && PrevAxePhase == 2) ++AxeCatches;
	}
	else { bThrowPending = bMeleePending = false; }
	bPrevAxeActive = State.bAxeActive; PrevAxePhase = State.AxePhase; PrevHurtTime = State.HurtTime;

	FCrbAxeView Axe;
	Axe.bActive = State.bAxeActive; Axe.Phase = State.AxePhase; Axe.Pos = Coords.ToUE(State.AxeX, State.AxeY, State.AxeZ);
	Axe.Spin = State.AxeSpin; Axe.Yaw = State.AxeYaw; Axe.Travelled = State.AxeTravelled; Axe.Hits = State.AxeHits;
	PlayerAvatar.Tick(bActive, Feet, AvatarFacingYaw, AvatarAnim, Axe, Dt);
	if (!bActive) return;

	// Orbit camera over the shoulder, pulled in against copied Java block data (ray march, no Unreal collision).
	AvatarCameraPivot = Feet + FVector(0, 0, State.bSneak ? 135.f : 160.f);
	const FVector Back = -ViewRotation().Vector();
	float Dist = 360.f;
	for (float D = 20.f; D <= 360.f; D += 10.f)
	{
		double MX, MY, MZ; Coords.ToMC(AvatarCameraPivot + Back * D, MX, MY, MZ);
		if (World.IsSolidAt(FIntVector(FMath::FloorToInt(MX), FMath::FloorToInt(MY), FMath::FloorToInt(MZ)))) { Dist = FMath::Max(30.f, D - 25.f); break; }
	}
	AvatarCameraDistance = FMath::FInterpTo(AvatarCameraDistance <= 0 ? Dist : AvatarCameraDistance, Dist, Dt, Dist < AvatarCameraDistance ? 30.f : 4.f);
}
