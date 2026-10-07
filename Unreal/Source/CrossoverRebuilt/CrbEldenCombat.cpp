// Minecraft x Elden Combat (mod:eldencombat, internal id MinecraftEldenCombat) - a mod of the existing Mods menu.
// Java's crb.ec.EldenCombat owns every combat rule (light / heavy / charged attacks with windup, active and recovery
// frames, combos, stamina, poise, guard, parry, dodge roll i-frames, guard break, stagger, riposte, lock-on targets),
// all on real Minecraft damage, items and mobs. Unreal sends the buttons and presents: the user's custom 3D Steve with
// the combat clips and a voxel copy of the held item (FCrbECSteve), the combat orbit camera with lock-on, the HUD
// (CrbHUD::DrawEldenCombat) and the mod's own debug tools (OpenECMenu; also reachable from the F4 debug menu).
//
// Lifecycle: ON  -> ECStartup: one character mod at a time, create the Steve / weapon / shield components, Java "ec.on",
//                   the "ec" input block starts (attack / use buttons belong to the mod).
//            OFF -> ECShutdown: Java "ec.off" (state, movement modifier, mob staggers, debug dummies removed there), the
//                   input block stops (vanilla attack / use again), components destroyed, camera back to the vanilla
//                   first-person view, HUD / debug tools gone. No restart needed either way.
#include "CrbHost.h"
#include "CrbMenus.h"
#include "CrbPawn.h"
#include "Camera/CameraComponent.h"
#include "Dom/JsonObject.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

void ACrbHost::ToggleEldenCombat()
{
	bEldenCombatEnabled = !bEldenCombatEnabled;
	if (bEldenCombatEnabled) ECStartup(); else ECShutdown();
	SaveConfig();
	SendInput(true);
}

void ACrbHost::SelectEldenStyle(int32 Style)
{
	if (bEldenCombatEnabled && ECStyle == Style) { ToggleEldenCombat(); return; }      // its own row again: off
	if (bEldenCombatEnabled) { ECShutdown(); ECStyle = Style; ECStartup(); SaveConfig(); SendInput(true); return; }   // switch style
	ECStyle = Style;
	ToggleEldenCombat();
}

// Elden Ring style look: a camera post-process grade only (warm, slightly desaturated, vignette) - the world's lighting
// system is not touched. Removed when the mod turns off or switches to the Minecraft style.
void ACrbHost::ApplyECLook(bool bOn)
{
	if (bOn == bECLookApplied || !Pawn || !Pawn->Camera) return;
	FPostProcessSettings& P = Pawn->Camera->PostProcessSettings;
	P.bOverride_ColorSaturation = bOn; P.ColorSaturation = FVector4(0.84f, 0.84f, 0.84f, 1.f);
	P.bOverride_ColorContrast = bOn; P.ColorContrast = FVector4(1.08f, 1.08f, 1.08f, 1.f);
	P.bOverride_ColorGain = bOn; P.ColorGain = FVector4(1.02f, 0.98f, 0.9f, 1.f);
	P.bOverride_ColorGammaShadows = bOn; P.ColorGammaShadows = FVector4(0.98f, 0.98f, 1.04f, 1.f);
	P.bOverride_VignetteIntensity = bOn; P.VignetteIntensity = 0.55f;
	P.bOverride_BloomIntensity = bOn; P.BloomIntensity = 0.9f;
	bECLookApplied = bOn;
}

void ACrbHost::ECStartup()
{
	// one player-character mod at a time
	if (bSm64Enabled) ToggleSm64();
	if (bAvatarEnabled) ToggleAvatar();
	if (bCraft64Enabled) ToggleCraft64();
	if (bPhysicsPortalEnabled) TogglePhysicsPortal();
	if (Weapon != ECrbWeapon::Hand) SelectWeapon(ECrbWeapon::Hand);
	ECSteve.Acquire(this, Root, MatVertexColor, ECStyle == 1);
	{ TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("style"), ECStyle == 1 ? TEXT("ring") : TEXT("minecraft")); SendCommand(TEXT("ec.on"), A); }
	ApplyECLook(ECStyle == 1);
	ECLastOn = FPlatformTime::Seconds();
	bECStarted = true;
	bECHeavyHeld = bECBlockHeld = false; ECFlick = 0; ECVisualYaw = State.Yaw;
	ECBanner.Reset(); ECHurtFlash = 0; ECSeenEvent = ECSeenDamage = ECSeenHurt = ECSeenBlock = ECSeenDeath = -1;
	LookPitch = FMath::Clamp(LookPitch, -60.f, 50.f);
	StatusLine = ECSteve.IsAcquired()
		? FString(ECStyle == 1 ? TEXT("Elden Ring Combat (Steve) on:") : TEXT("Minecraft \u00d7 Elden Combat on:")) + TEXT(" LMB light, R heavy (hold to charge), RMB guard, F parry, C / Alt dodge, Q / middle mouse lock-on")
		: TEXT("Minecraft \u00d7 Elden Combat on (3D Steve not cooked: ") + ECSteve.LoadError + TEXT(")");
}

void ACrbHost::ECShutdown()
{
	SendCommand(TEXT("ec.off"));
	ECLastOn = FPlatformTime::Seconds();
	bECStarted = false;
	ApplyECLook(false);
	ECSteve.Release();
	bECHeavyHeld = bECBlockHeld = false; bECDebugOverlay = false; ECFlick = 0;
	ECBanner.Reset(); ECHurtFlash = 0; ECDamageTime = -100;
	if (bECMenuOpen) CloseMenus();
	bECMenuOpen = false;
	ViewMode = 0;
	StatusLine = TEXT("Minecraft \u00d7 Elden Combat off: vanilla Minecraft combat");
}

FString ACrbHost::ECCommand(const FString& Op, const TSharedPtr<FJsonObject>& Args)
{
	if (!IsEldenCombatActive()) return FString();   // the mod's debug tools exist only while it is on
	return SendCommand(Op, Args);
}

void ACrbHost::OpenECMenu()
{
	if (!IsEldenCombatActive()) return;
	CloseMenus();
	bModMenuOpen = true; bECMenuOpen = true;
	CrbMenus::OpenECMenu(this);
	SendInput(true);
}

// ---- input hooks (called from the shared input handlers; false = not the mod's button, vanilla path continues)
bool ACrbHost::ECPrimary(bool bDown)
{
	if (!IsEldenCombatActive() || IsMenuOpen() || bInventoryOpen) return false;
	if (bDown && !bAttackHeld) ++ECLight;
	bAttackHeld = bDown;
	SendInput(true);
	return true;
}
bool ACrbHost::ECSecondary(bool bDown)
{
	if (!IsEldenCombatActive() || IsMenuOpen() || bInventoryOpen) return false;
	bECBlockHeld = bDown; bUseHeld = false;
	SendInput(true);
	return true;
}
bool ACrbHost::ECLook(float DYaw)
{
	// locked on: the camera tracks the target; a mouse flick switches target (Elden Ring)
	if (!IsEldenCombatActive() || !State.EC.Lock.bValid) return false;
	ECFlick += DYaw;
	if (FMath::Abs(ECFlick) > 30.f) { ++ECSwitch; ECFlick = 0; SendInput(true); }
	return true;
}
bool ACrbHost::ECScroll(float Delta)
{
	if (!IsEldenCombatActive() || !State.EC.Lock.bValid || FMath::IsNearlyZero(Delta)) return false;
	++ECSwitch; SendInput(true);
	return true;
}

namespace
{
	// attack clips are phase-mapped onto the server's frames: [0,.35] windup, [.35,.55] active, [.55,1] recovery
	float ECAttackPhase(float T, int32 W, int32 A, int32 Len)
	{
		if (Len <= 0) return 0.f;
		W = FMath::Max(1, W); A = FMath::Max(1, A);
		if (T < W) return 0.35f * T / W;
		if (T < W + A) return 0.35f + 0.2f * (T - W) / A;
		return FMath::Min(1.f, 0.55f + 0.45f * (T - W - A) / FMath::Max(1, Len - W - A));
	}
}

void ACrbHost::TickEldenCombat(float Dt)
{
	const FCrbECState& E = State.EC;
	const bool bOn = IsEldenCombatActive();
	const double Now = FPlatformTime::Seconds();
	if (!bOn)
	{
		// OFF: nothing of the mod may stay alive. Java still on (e.g. the toggle was saved off): switch it off too.
		if (E.bOn && Now - ECLastOn > 2.0) { SendCommand(TEXT("ec.off")); ECLastOn = Now; }
		if (ECSteve.IsAcquired()) ECSteve.Release();
		return;
	}
	if (!bECStarted) ECStartup();                       // turned on by the saved config at launch
	// Java restarted / new world: the server forgot the mode; ask again
	const TCHAR* WantStyle = ECStyle == 1 ? TEXT("ring") : TEXT("minecraft");
	if ((!E.bOn || (!E.Style.IsEmpty() && E.Style != WantStyle)) && Connection && Connection->GetState() == ECrbLinkState::Connected && State.bValid && Now - ECLastOn > 2.0)
	{ TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("style"), WantStyle); SendCommand(TEXT("ec.on"), A); ECLastOn = Now; }
	if (ECStyle == 1 && !bECLookApplied) ApplyECLook(true);

	// ---- keys (the mod's own buttons; LMB / RMB come through ECPrimary / ECSecondary)
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && !IsMenuOpen() && !bInventoryOpen && !bECKeysFromTest)
	{
		bool bSend = false;
		if (PC->WasInputKeyJustPressed(EKeys::R)) { ++ECHeavy; bSend = true; }
		const bool bHeavy = PC->IsInputKeyDown(EKeys::R);
		if (bHeavy != bECHeavyHeld) { bECHeavyHeld = bHeavy; bSend = true; }
		if (PC->WasInputKeyJustPressed(EKeys::F)) { ++ECParry; bSend = true; }
		if (PC->WasInputKeyJustPressed(EKeys::C) || PC->WasInputKeyJustPressed(EKeys::LeftAlt)) { ++ECDodge; bSend = true; }
		if (PC->WasInputKeyJustPressed(EKeys::Q) || PC->WasInputKeyJustPressed(EKeys::MiddleMouseButton)) { ++ECLock; bSend = true; }
		if (bSend) SendInput(true);
	}
	if (ViewMode != 0) ViewMode = 0;

	// ---- events -> HUD (banners, damage numbers, flashes)
	if (ECSeenEvent < 0) ECSeenEvent = E.EventSeq;
	if (E.EventSeq != ECSeenEvent)
	{
		ECSeenEvent = E.EventSeq;
		const FString& Ev = E.Event;
		FLinearColor C = FLinearColor::White; bool bShow = true;
		if (Ev == TEXT("YOU DIED")) C = FLinearColor(0.62f, 0.04f, 0.03f);
		else if (Ev == TEXT("ENEMY FELLED")) C = FLinearColor(0.93f, 0.78f, 0.38f);
		else if (Ev == TEXT("CRITICAL")) C = FLinearColor(1.f, 0.85f, 0.55f);
		else if (Ev == TEXT("PARRY")) C = FLinearColor(0.6f, 0.85f, 1.f);
		else if (Ev == TEXT("GUARD BROKEN")) C = FLinearColor(1.f, 0.4f, 0.2f);
		else if (Ev == TEXT("STAGGER")) C = FLinearColor(1.f, 0.9f, 0.4f);
		else bShow = false;
		if (bShow) { ECBanner = Ev; ECBannerColor = C; ECBannerTime = Now; }
	}
	if (ECSeenDamage < 0) ECSeenDamage = E.DamageSeq;
	if (E.DamageSeq != ECSeenDamage) { ECSeenDamage = E.DamageSeq; ECDamageShown = E.LastDamage; ECDamageAt = Coords.ToUE(E.DX, E.DY, E.DZ); ECDamageTime = Now; }
	if (ECSeenHurt < 0) ECSeenHurt = E.HurtSeq;
	if (E.HurtSeq != ECSeenHurt) { ECSeenHurt = E.HurtSeq; ECHurtFlash = 0.45f; }
	if (ECSeenBlock < 0) ECSeenBlock = E.BlockSeq;
	if (E.BlockSeq != ECSeenBlock) { ECSeenBlock = E.BlockSeq; ECBlockHitTime = Now; }
	if (ECSeenDeath < 0) ECSeenDeath = E.DeathSeq;
	if (E.DeathSeq != ECSeenDeath) { ECSeenDeath = E.DeathSeq; ECDeathTime = Now; }
	ECHurtFlash = FMath::Max(0.f, ECHurtFlash - Dt * 1.4f);
	// Elden-style lagging bars (the lost chunk drains after a moment)
	const float StaminaFrac = E.MaxStamina > 0 ? FMath::Clamp(E.Stamina / E.MaxStamina, 0.f, 1.f) : 0.f;
	const float HpFrac = E.MaxHp > 0 ? FMath::Clamp(E.Hp / E.MaxHp, 0.f, 1.f) : 0.f;
	ECStaminaLag = StaminaFrac > ECStaminaLag ? StaminaFrac : FMath::FInterpConstantTo(ECStaminaLag, StaminaFrac, Dt, 0.6f);
	ECHpLag = HpFrac > ECHpLag ? HpFrac : FMath::FInterpConstantTo(ECHpLag, HpFrac, Dt, 0.35f);

	// ---- animation: the clip and its phase come from the server's combat frames
	const FString ActKey = E.Act + FString::Printf(TEXT("|%lld|%d"), E.SwingSeq, E.Dodges);
	if (ActKey != ECActKey) { ECActKey = ActKey; ECActT = E.T; ECSeenTick = E.Tick; }
	else if (E.Tick != ECSeenTick) { ECSeenTick = E.Tick; ECActT = FMath::Max(ECActT, (float)E.T); }
	else ECActT = FMath::Min(ECActT + Dt * 20.f, (float)FMath::Max(E.Len, E.T + 1));
	const float Lin = E.Len > 0 ? FMath::Clamp(ECActT / E.Len, 0.f, 1.f) : 0.f;
	FCrbECAnimInputs A;
	A.Token = E.SwingSeq * 64 + E.Dodges;
	const float HSpeed = FVector2D(State.VX, State.VZ).Size() * 20.f;      // m/s
	const FString& Act = E.Act;
	auto Pick = [&](ECrbECClip C, float Phase, bool bLoop, float Fade) { A.Clip = (int32)C; A.Phase = Phase; A.bLoop = bLoop; A.Fade = Fade; };
	if (Act == TEXT("LIGHT")) Pick(E.Weapon == TEXT("spear") ? ECrbECClip::Thrust : (ECrbECClip)((int32)ECrbECClip::Light1 + FMath::Clamp(E.Combo, 0, 2)), ECAttackPhase(ECActT, E.W, E.A, E.Len), false, 0.06f);
	else if (Act == TEXT("HEAVY")) Pick(ECrbECClip::Heavy, ECAttackPhase(ECActT, E.W, E.A, E.Len), false, 0.08f);
	else if (Act == TEXT("CHARGE")) Pick(ECrbECClip::Charge, -1.f, true, 0.12f);
	else if (Act == TEXT("DODGE")) Pick(ECrbECClip::Dodge, Lin, false, 0.05f);
	else if (Act == TEXT("BACKSTEP")) Pick(ECrbECClip::Backstep, Lin, false, 0.05f);
	else if (Act == TEXT("BLOCK")) { if (Now - ECBlockHitTime < 0.33) Pick(ECrbECClip::BlockHit, (float)((Now - ECBlockHitTime) / 0.33), false, 0.04f); else Pick(ECrbECClip::Block, -1.f, true, 0.1f); }
	else if (Act == TEXT("PARRY")) Pick(ECrbECClip::Parry, Lin, false, 0.04f);
	else if (Act == TEXT("GUARD_BREAK")) Pick(ECrbECClip::GuardBreak, Lin, false, 0.05f);
	else if (Act == TEXT("STAGGER")) Pick(ECrbECClip::Stagger, Lin, false, 0.04f);
	else if (Act == TEXT("RIPOSTE")) Pick(ECrbECClip::Riposte, Lin, false, 0.06f);
	else if (Act == TEXT("DEAD") || State.bDead) Pick(ECrbECClip::Death, FMath::Clamp((float)(Now - ECDeathTime) / 1.4f, 0.f, 1.f), false, 0.1f);
	else if (!State.bOnGround && State.VY > 0.05) Pick(ECrbECClip::Jump, -1.f, false, 0.08f);
	else if (!State.bOnGround && State.VY < -0.25) Pick(ECrbECClip::Fall, -1.f, true, 0.2f);
	else if (HSpeed > 4.8f) { Pick(ECrbECClip::Run, -1.f, true, 0.15f); A.Rate = FMath::Clamp(HSpeed / 5.6f, 0.6f, 1.8f); }
	else if (HSpeed > 0.5f) { Pick(ECrbECClip::Walk, -1.f, true, 0.15f); A.Rate = FMath::Clamp(HSpeed / 3.2f, 0.4f, 1.8f); }
	else Pick(ECrbECClip::Idle, -1.f, true, 0.2f);
	ECAnimClip = A.Clip; ECAnimClipName = CrbECClipName((ECrbECClip)A.Clip);

	// ---- facing: attacks, guard and lock-on face the camera / target; free movement faces the way Steve runs
	const bool bCommitted = Act != TEXT("IDLE") && Act != TEXT("DODGE") && Act != TEXT("DEAD");
	if (E.Lock.bValid || bCommitted) ECVisualYaw = State.Yaw;
	else if (HSpeed > 0.6f) ECVisualYaw = FMath::RadiansToDegrees(FMath::Atan2(-State.VX, State.VZ));
	if (Act == TEXT("DODGE") && HSpeed > 1.f) ECVisualYaw = FMath::RadiansToDegrees(FMath::Atan2(-State.VX, State.VZ));

	double X, Y, Z; PresentedFeet(X, Y, Z);
	const FVector Feet = Coords.ToUE(X, Y, Z);
	{
		const FLinearColor L = LightAt(State.EyeBlock, State.EyeSky);
		const float Level = FMath::Clamp(FMath::Max3(L.R, L.G, L.B), 0.05f, 1.f);
		ECSteve.SetBrightness(BaseSunLux / PI * VanillaWeight * Level, SunWeight, BaseSunLux / PI * VanillaWeight * Level * 0.45f);
	}
	FCrbECSteve::FFrame F;
	F.bShow = State.bValid && E.bOn && ECSteve.IsAcquired();
	F.Feet = Feet; F.Yaw = ECVisualYaw; F.Anim = A;
	F.ItemKey = E.ItemKey; F.ItemPixels = &E.ItemPx; F.bWeapon = !E.Item.IsEmpty() && E.Item == E.ItemKey; F.bShield = E.bShield;
	F.WeaponPixel = ECWeaponPixel;
	F.WeaponClass = E.Weapon;
	F.bTrail = (Act == TEXT("LIGHT") || Act == TEXT("HEAVY") || Act == TEXT("RIPOSTE")) && ECActT >= E.W - 1 && ECActT <= E.W + E.A + 3;
	ECSteve.Tick(this, F, Dt);
	if (State.bValid)
	{
		// lock-on camera: turn toward the target (and Steve with it - the input yaw is the camera yaw)
		if (E.Lock.bValid && !IsMenuOpen())
		{
			const FVector Target = Coords.ToUE(E.Lock.X, E.Lock.Y + E.Lock.H * 0.6, E.Lock.Z);
			const FVector From = Feet + FVector(0, 0, 150.f);
			const FRotator R = (Target - From).Rotation();
			LookYaw = FMath::Fmod(LookYaw + FRotator::NormalizeAxis(R.Yaw - LookYaw) * FMath::Clamp(Dt * 9.f, 0.f, 1.f) + 360.f, 360.f);
			LookPitch = FMath::FInterpTo(LookPitch, FMath::Clamp(R.Pitch - 12.f, -45.f, 25.f), Dt, 6.f);
		}
		UpdateOrbitCamera(Feet, ECCameraDistance, Dt);
	}
}
