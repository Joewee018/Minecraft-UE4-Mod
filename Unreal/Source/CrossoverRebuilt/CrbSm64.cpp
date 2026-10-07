// SM64 Steve Movement (mod:sm64). Java's crb.client.sm64.Sm64Controller replaces the local player's travel step with
// SM64-style physics (30 Hz, walk/run acceleration, skid turn-around, single/double/triple jump chain, backflip,
// side flip, long jump, wall kick, ground pound, bonk, hard landing); Unreal sends the raw stick relative to the
// camera, draws Steve (SK_Steve with the live skin) with the native UCrbSteveAnimInstance and owns the orbit camera.
// Turning the mod off stops the "sm64" input flag: Java hands the player back to vanilla travel within one tick.
#include "CrbHost.h"
#include "CrbMenus.h"
#include "CrbPawn.h"
#include "Dom/JsonObject.h"
#include "Components/SkeletalMeshComponent.h"

void ACrbHost::ToggleSm64()
{
	bSm64Enabled = !bSm64Enabled;
	if (bSm64Enabled && bAvatarEnabled) { bAvatarEnabled = false; bThrowPending = bMeleePending = false; } // one player-character mod at a time
	if (bSm64Enabled && bCraft64Enabled) ToggleCraft64();
	if (bSm64Enabled && bEldenCombatEnabled) ToggleEldenCombat();
	if (bSm64Enabled && bPhysicsPortalEnabled) TogglePhysicsPortal();
	SaveConfig();
	if (bSm64Enabled)
	{
		LookPitch = FMath::Clamp(LookPitch, -70.f, 60.f);
		if (Weapon != ECrbWeapon::Hand) SelectWeapon(ECrbWeapon::Hand);
		PushSm64Config();
		StatusLine = Steve.AssetsReady() ? TEXT("SM64 Steve Movement on") : TEXT("SM64 Steve Movement on (Steve rig not cooked: ") + Steve.LoadError + TEXT(")");
	}
	else StatusLine = TEXT("SM64 Steve Movement off: vanilla movement");
	SendInput(true);
}

void ACrbHost::PushSm64Config(const TSharedPtr<FJsonObject>& Extra)
{
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
	A->SetNumberField(TEXT("speedMultiplier"), Sm64SpeedMultiplier);
	A->SetNumberField(TEXT("jumpMultiplier"), Sm64JumpMultiplier);
	A->SetNumberField(TEXT("gravityMultiplier"), Sm64GravityMultiplier);
	A->SetBoolField(TEXT("wallKicks"), bSm64WallKicks);
	A->SetBoolField(TEXT("groundPound"), bSm64GroundPound);
	A->SetBoolField(TEXT("longJump"), bSm64LongJump);
	A->SetBoolField(TEXT("vanillaFallDamage"), bSm64VanillaFallDamage);
	A->SetBoolField(TEXT("ctrlWalks"), bSm64CtrlWalks);
	if (Extra.IsValid()) for (const auto& KV : Extra->Values) A->SetField(KV.Key, KV.Value);
	SendCommand(TEXT("sm64.config"), A);
	++Sm64ConfigsSent;
}

void ACrbHost::OpenSm64Menu()
{
	CloseMenus();
	bModMenuOpen = true;
	CrbMenus::OpenSm64Menu(this);
	SendInput(true);
}

void ACrbHost::UpdateOrbitCamera(const FVector& Feet, float MaxDist, float Dt)
{
	// Orbit camera behind the pivot, pulled in against copied Java block data (ray march, no Unreal collision).
	AvatarCameraPivot = Feet + FVector(0, 0, 140.f);
	const FVector Back = -ViewRotation().Vector();
	float Dist = MaxDist;
	for (float D = 20.f; D <= MaxDist; D += 10.f)
	{
		double MX, MY, MZ; Coords.ToMC(AvatarCameraPivot + Back * D, MX, MY, MZ);
		if (World.IsSolidAt(FIntVector(FMath::FloorToInt(MX), FMath::FloorToInt(MY), FMath::FloorToInt(MZ)))) { Dist = FMath::Max(30.f, D - 25.f); break; }
	}
	AvatarCameraDistance = FMath::FInterpTo(AvatarCameraDistance <= 0 ? Dist : AvatarCameraDistance, Dist, Dt, Dist < AvatarCameraDistance ? 30.f : 4.f);
}

void ACrbHost::TickSm64(float Dt)
{
	const bool bOn = IsSm64Active() && !IsAvatarActive();
	// Settings follow every (re)connection: Java keeps them in memory only.
	if (bOn && Connection && Connection->GetState() == ECrbLinkState::Connected && Sm64ConfigEpoch != SeenEpoch) { Sm64ConfigEpoch = SeenEpoch; PushSm64Config(); }
	const FCrbSm64State& M = State.Sm64;
	double X, Y, Z; PresentedFeet(X, Y, Z);
	const FVector Feet = Coords.ToUE(X, Y, Z);
	{
		const FLinearColor L = LightAt(State.EyeBlock, State.EyeSky);
		const float Level = FMath::Clamp(FMath::Max3(L.R, L.G, L.B), 0.05f, 1.f);
		Steve.SetBrightness(BaseSunLux / PI * VanillaWeight * Level, SunWeight);
	}
	SteveAnim.Action = M.bActive ? M.Action : (State.bOnGround ? (FVector2D(State.VX, State.VZ).Size() > 0.02 ? TEXT("WALK") : TEXT("IDLE")) : TEXT("FREEFALL"));
	SteveAnim.ActionSerial = M.bActive ? M.ActionSerial : -1;
	// Vanilla fallback (water, ladders, flying): forward speed from Java's motion, in SM64 units per frame.
	SteveAnim.FwdVel = M.bActive ? M.FwdVel : float(FVector2D(State.VX, State.VZ).Size() / (1.5 * 0.01125));
	SteveAnim.VelY = M.bActive ? M.VelY : float(State.VY / (1.5 * 0.01125));
	SteveAnim.bGrounded = M.bActive ? M.bGrounded : State.bOnGround;
	const bool bShow = bOn && State.bValid && !State.bDead;
	// Java's facing while SM64 drives; vanilla's (camera) yaw while it has handed control back.
	const float Facing = M.bActive ? M.FaceYaw : State.Yaw;
	if (!FMath::IsNearlyEqual(Steve.HeightUU, Sm64ModelHeight, 0.5f)) Steve.SetHeight(Sm64ModelHeight);
	// Steve skin (default): Minecraft's steve.png cooked with the rig, classic arms. Otherwise the account's live skin.
	const bool bSteveSkin = bSm64SteveSkin && Steve.DefaultSkin;
	UTexture2D* Skin = bSteveSkin ? Steve.DefaultSkin : (M.Skin.IsEmpty() ? nullptr : Textures.Get(M.Skin));
	Steve.Tick(bShow && Steve.AssetsReady(), Feet, Facing, SteveAnim, Skin, bSteveSkin ? false : M.bSlim, Dt);
	if (bOn && State.bValid) UpdateOrbitCamera(Feet, FMath::Clamp(Sm64CameraDistance, 150.f, 900.f), Dt);
}
