// Craft 64 (mod:craft64): a Doom 64-style first-person shooter played in the open Minecraft world. Java's crb.c64.Craft64
// owns every rule (Doom 64 arsenal, ammo, 100 health + armor, pickups, autoaim, projectiles, damage on real mobs);
// Unreal sends the trigger and weapon selection, draws the Doom-style weapon sprites (Minecraft pixel art shipped by the
// bridge as textures), the Doom 64-style HUD in the Minecraft font, screen flashes, and the low-res N64 look. Only while
// the mode is on: the world lighting system is untouched; the look is a camera post-process + render resolution.
#include "CrbHost.h"
#include "CrbPawn.h"
#include "CrbConnection.h"
#include "Camera/CameraComponent.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"

void ACrbHost::ToggleCraft64()
{
	bCraft64Enabled = !bCraft64Enabled;
	if (bCraft64Enabled)
	{
		// one player-character mod at a time; Doom is first person with the plain hand slot
		if (bSm64Enabled) ToggleSm64();
		if (bAvatarEnabled) ToggleAvatar();
		if (bPhysicsPortalEnabled) TogglePhysicsPortal();
		if (bEldenCombatEnabled) ToggleEldenCombat();
		if (Weapon != ECrbWeapon::Hand) SelectWeapon(ECrbWeapon::Hand);
		ViewMode = 0;
		SendCommand(TEXT("c64.on"));
		C64LastOn = FPlatformTime::Seconds();
		C64Raise = 0; C64Shown.Empty();
		StatusLine = TEXT("Craft 64 on: click fire, 1-8 / wheel switch weapons, E use");
	}
	else
	{
		SendCommand(TEXT("c64.off"));
		StatusLine = TEXT("Craft 64 off");
	}
	SaveConfig();
	ApplyCraft64Look(bCraft64Enabled);
	SendInput(true);
}

void ACrbHost::Craft64Select(const FString& Want)
{
	C64Want = Want; ++C64Seq;
	SendInput(true);
}

void ACrbHost::ApplyCraft64Look(bool bOn)
{
	IConsoleVariable* Pct = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	IConsoleVariable* Up = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Upscale.Quality"));
	if (bOn && !bC64LookApplied)
	{
		if (Pct) { C64PrevScreenPct = Pct->GetFloat(); Pct->Set(FMath::Clamp(Craft64ScreenPercentage, 20.f, 100.f), ECVF_SetByCode); }
		if (Up) { C64PrevUpscale = Up->GetInt(); Up->Set(0, ECVF_SetByCode); }   // nearest upscale: chunky N64 pixels
		bC64LookApplied = true;
	}
	else if (!bOn && bC64LookApplied)
	{
		if (Pct) Pct->Set(C64PrevScreenPct, ECVF_SetByCode);
		if (Up) Up->Set(C64PrevUpscale, ECVF_SetByCode);
		bC64LookApplied = false;
	}
	if (Pawn && Pawn->Camera)
	{
		// Doom 64 mood: darker mids, deeper contrast, warm-red shadows, heavy vignette, a touch of grain.
		FPostProcessSettings& P = Pawn->Camera->PostProcessSettings;
		P.bOverride_VignetteIntensity = bOn; P.VignetteIntensity = 0.85f;
		P.bOverride_ColorContrast = bOn; P.ColorContrast = FVector4(1.3f, 1.25f, 1.22f, 1.f);
		P.bOverride_ColorSaturation = bOn; P.ColorSaturation = FVector4(1.35f, 1.15f, 1.1f, 1.f);
		P.bOverride_ColorGain = bOn; P.ColorGain = FVector4(0.86f, 0.78f, 0.8f, 1.f);             // dim, slightly blood-warm world
		P.bOverride_ColorGammaShadows = bOn; P.ColorGammaShadows = FVector4(1.05f, 0.9f, 0.95f, 1.f);
		P.bOverride_ColorGainShadows = bOn; P.ColorGainShadows = FVector4(0.7f, 0.55f, 0.62f, 1.f);
		P.bOverride_GrainIntensity = bOn; P.GrainIntensity = 0.18f;
		P.bOverride_BloomIntensity = bOn; P.BloomIntensity = 1.2f;
	}
}

void ACrbHost::TickCraft64(float Dt)
{
	const FCrbC64State& C = State.C64;
	const bool bOn = IsCraft64Active();
	if (bOn != bC64LookApplied) ApplyCraft64Look(bOn);
	const double Now0 = FPlatformTime::Seconds();
	if (!bOn)
	{
		// Unreal turned the mode off (or started with it off) while Java still runs it: switch Java off too.
		if (C.bOn && Now0 - C64LastOn > 2.0) { SendCommand(TEXT("c64.off")); C64LastOn = Now0; }
		return;
	}
	if (ViewMode != 0) ViewMode = 0;
	// Java restarted (or the world changed): the server forgot the mode; ask again.
	const double Now = FPlatformTime::Seconds();
	if (!C.bOn && Connection && Connection->GetState() == ECrbLinkState::Connected && State.bValid && Now - C64LastOn > 2.0) { SendCommand(TEXT("c64.on")); C64LastOn = Now; }
	if (!C.bOn) return;

	// Weapon raise / lower (Doom: the old gun drops, the new one comes up)
	const bool bSwitching = !C.Pending.IsEmpty();
	if (C64Shown.IsEmpty()) C64Shown = C.Weapon;
	if (bSwitching || C64Shown != C.Weapon) { C64Raise = FMath::Max(0.f, C64Raise - Dt / 0.15f); if (C64Raise <= 0.f) C64Shown = C.Weapon; }
	else C64Raise = FMath::Min(1.f, C64Raise + Dt / 0.18f);

	// Firing: every new shot restarts the weapon's fire animation
	if (C64SeenFire < 0) C64SeenFire = C.FireSeq;
	if (C.FireSeq != C64SeenFire) { C64SeenFire = C.FireSeq; C64SinceFire = 0; }
	else C64SinceFire += Dt;
	const FString& Wk = C64Shown;
	const float T = C64SinceFire;
	int32 F = 0;
	if (Wk == TEXT("fist")) F = T < 0.06f ? 2 : T < 0.2f ? 1 : T < 0.3f ? 2 : 0;
	// (frame 1 = muzzle flash: held ~4 Doom tics so it reads on screen)
	else if (Wk == TEXT("chainsaw")) F = T < 0.15f ? 1 + (int32)(Now * 20) % 2 : (int32)(Now * 10) % 2;   // idle engine shake
	else if (Wk == TEXT("pistol")) F = T < 0.11f ? 1 : T < 0.22f ? 2 : 0;
	else if (Wk == TEXT("shotgun")) F = T < 0.11f ? 1 : T < 0.3f ? 0 : T < 0.7f ? 2 : 0;
	else if (Wk == TEXT("super")) F = T < 0.11f ? 1 : T < 0.35f ? 0 : T < 1.15f ? 2 : 0;
	else if (Wk == TEXT("chaingun") || Wk == TEXT("plasma") || Wk == TEXT("unmaker")) F = T < 0.12f ? 1 + (int32)(C64SeenFire % 2) : 0;
	else if (Wk == TEXT("rocket")) F = T < 0.1f ? 1 : T < 0.3f ? 2 : 0;
	else if (Wk == TEXT("bfg")) F = C.BfgCharge > 0 ? 1 : T < 0.25f ? 2 : 0;
	C64Frame = F;

	// Doom view bob from horizontal speed (Java velocity, blocks/tick)
	const float Speed = FMath::Sqrt(State.VX * State.VX + State.VZ * State.VZ) * 20.f;
	C64BobAmp = FMath::FInterpTo(C64BobAmp, State.bOnGround ? FMath::Clamp(Speed / 6.f, 0.f, 1.f) : 0.f, Dt, 8.f);
	C64BobPhase += Dt * 2.f * PI * 1.6f;

	// Screen flashes and pickup messages
	if (C64SeenHurt < 0) C64SeenHurt = C.HurtSeq;
	if (C.HurtSeq != C64SeenHurt) { C64SeenHurt = C.HurtSeq; C64Hurt = 0.55f; }
	if (C64SeenBonus < 0) C64SeenBonus = C.BonusSeq;
	if (C.BonusSeq != C64SeenBonus) { C64SeenBonus = C.BonusSeq; C64Bonus = 0.35f; }
	C64Hurt = FMath::Max(0.f, C64Hurt - Dt * 1.2f); C64Bonus = FMath::Max(0.f, C64Bonus - Dt * 1.5f);
	if (C.MessageSeq != C64SeenMsg) { C64SeenMsg = C.MessageSeq; C64Msg = C.Message; C64MsgTime = Now; }
}
