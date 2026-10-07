// Craft 64 tests (-CrbTest=craft64, also part of -CrbTest=all). Evidence comes from the real paths: Java's Craft64 view
// (health, armor, ammo, owned weapons, shots / hits / kills, messages), Java mob health from test.entity, the HUD's own
// draw record (weapon sprite texture actually drawn, vanilla hotbar hidden), the render CVars, and screenshots. Input
// goes through the host's PrimaryPressed / SelectSlot / SetMove (the keyboard and mouse paths).
#include "CrbTest.h"
#include "CrbHost.h"
#include "CrbHUD.h"
#include "CrbMenus.h"
#include "Components/SceneComponent.h"
#include "ProceduralMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"

namespace
{
	ACrbHUD* C64HudOf(ACrbHost* H)
	{
		APlayerController* PC = H && H->GetWorld() ? H->GetWorld()->GetFirstPlayerController() : nullptr;
		return PC ? Cast<ACrbHUD>(PC->GetHUD()) : nullptr;
	}
	float C64CVar(const TCHAR* N) { IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(N); return V ? V->GetFloat() : -1.f; }
}

void FCrbTest::AddC64Spawn(const FString& Type, double Dist)
{
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("type"), Type); A->SetNumberField(TEXT("distance"), Dist);
	AddCommand(TEXT("test.spawn"), A, [this](bool bOk, const FCrbResult* R)
	{
		C64Mob.Empty();
		if (bOk && R && R->Json.IsValid()) R->Json->TryGetStringField(TEXT("uuid"), C64Mob);
		if (C64Mob.IsEmpty()) Fail(TEXT("test.spawn failed: ") + (R ? R->Message : FString(TEXT("timeout"))));
	});
}

void FCrbTest::AddC64MobCheck(const FString& Label, bool bDiscard)
{
	(void)Label;
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
	AddCommand(TEXT("test.entity"), A, [this, Label](bool bOk, const FCrbResult* R)
	{
		double Hp = -1; bool bExists = false, bAlive = false;
		if (R && R->Json.IsValid()) { R->Json->TryGetBoolField(TEXT("exists"), bExists); R->Json->TryGetNumberField(TEXT("health"), Hp); R->Json->TryGetBoolField(TEXT("alive"), bAlive); }
		C64MobHealth = bExists && bAlive ? (float)Hp : 0.f;
	});
	// the uuid is only known after the spawn reply: patch the args when this step begins
	Steps.Last().Begin = [this, A, bDiscard, Prev = Steps.Last().Begin]() { A->SetStringField(TEXT("uuid"), C64Mob); if (bDiscard) A->SetBoolField(TEXT("discard"), true); Prev(); };
}

void FCrbTest::BuildCraft64()
{
	auto C = [this]() -> const FCrbC64State& { return Host->GetState().C64; };

	// ---- setup: vanilla, survival, flat open lane ----
	Add(TEXT("c64 setup"), [this] { Host->bInputOverride = true; Host->SetMove(0, 0); Host->SetButtons(false, false, false); Host->PrimaryPressed(false);
		if (Host->bCraft64Enabled) Host->ToggleCraft64(); if (Host->bSm64Enabled) Host->ToggleSm64(); if (Host->bAvatarEnabled) Host->ToggleAvatar(); Host->ViewMode = 0; }, [](float T) { return T > 0.8f; }, 3);
	AddCommand(TEXT("fixture.off"), nullptr, [](bool, const FCrbResult*) {});
	AddCommand(TEXT("player.reset"), nullptr, [](bool, const FCrbResult*) {});
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("mode"), TEXT("survival"));
		AddCommand(TEXT("gamemode.set"), A, [](bool, const FCrbResult*) {});
	}
	Add(TEXT("c64 baseline"), [this] { C64Pct0 = C64CVar(TEXT("r.ScreenPercentage")); }, [](float T) { return T > 0.3f; }, 2);

	// ---- turn it on from the Mods menu ----
	AddKey(EKeys::M, TEXT("M"));
	Add(TEXT("mods for craft64"), [] {}, [](float T) { return (CrbMenus::IsOpen() && T > 0.5f) || T > 5.f; }, 6);
	AddClickRow(TEXT("mod:craft64"), TEXT("Mods menu"));
	AddClickRow(TEXT("resume"), TEXT("Mods menu"));
	Add(TEXT("c64 on"), [this] { Host->LookPitch = -4; }, [this, C](float T)
	{
		ACrbHUD* Hud = C64HudOf(Host);
		const bool bSprite = Hud && Hud->Drawn.bC64SpriteDrawn;
		if (!(C().bOn && bSprite && T > 1.5f) && T < 12.f) return false;
		Check(Host->bCraft64Enabled && C().bOn, TEXT("mod:craft64 ON from the Mods menu: Java Craft64 runs the player"));
		Check(C().Health == 100 && C().Weapon == TEXT("pistol") && C().Owned.Contains(TEXT("fist")) && C().AmmoOf(TEXT("bullets")) == 50,
			FString::Printf(TEXT("Doom start: 100 health, pistol + fist, 50 bullets (health %d, weapon %s, bullets %d)"), C().Health, *C().Weapon, C().AmmoOf(TEXT("bullets"))));
		Check(Host->GetState().MaxHealth >= 199.f, FString::Printf(TEXT("Minecraft max health raised for the 100 / 200 Doom scale (max %.0f)"), Host->GetState().MaxHealth));
		Check(bSprite && Hud->Drawn.C64Sprite.Contains(TEXT("pistol_")), TEXT("Doom-style weapon sprite drawn from the bridge texture: ") + (Hud ? Hud->Drawn.C64Sprite : FString()));
		Check(Hud && Hud->Drawn.bC64Hud && !Hud->Drawn.bHotbar && Hud->Drawn.C64Health == 100, TEXT("Doom 64 HUD drawn (health / armor / ammo), vanilla hotbar and hearts hidden"));
		Check(FMath::IsNearlyEqual(C64CVar(TEXT("r.ScreenPercentage")), Host->Craft64ScreenPercentage, 0.5f) && C64CVar(TEXT("r.Upscale.Quality")) == 0.f,
			FString::Printf(TEXT("N64 look: render at %.0f %% with nearest upscale (HUD stays sharp)"), C64CVar(TEXT("r.ScreenPercentage"))));
		Check(!(Host->Avatar.GetHands() && Host->Avatar.GetHands()->IsVisible()), TEXT("vanilla first-person hand hidden (Craft 64 draws Steve's arms on the weapon)"));
		Shot(TEXT("70_c64_on"));
		return true;
	}, 14);

	// ---- pistol on a real zombie ----
	AddC64Spawn(TEXT("minecraft:husk"), 6);
	Add(TEXT("c64 pistol"), [this, C] { C64Shots0 = C().Shots; C64Hits0 = C().Hits; C64Kills0 = C().Kills; C64Ammo0 = C().AmmoOf(TEXT("bullets")); C64Flag = false; Host->PrimaryPressed(true); }, [this, C](float T)
	{
		if (!C64Flag && Host->C64Frame == 1) { Shot(TEXT("71_c64_pistol_fire")); C64Flag = true; }
		if (!((C().Kills > C64Kills0 && T > 0.6f) || T > 5.f)) return false;
		Host->PrimaryPressed(false);
		Check(C().Shots > C64Shots0 && C().AmmoOf(TEXT("bullets")) < C64Ammo0, FString::Printf(TEXT("Fire = pistol shots (%lld shots, bullets %d -> %d)"), C().Shots - C64Shots0, C64Ammo0, C().AmmoOf(TEXT("bullets"))));
		Check(C().Hits > C64Hits0 && C().Kills > C64Kills0, FString::Printf(TEXT("Pistol bullets hit and kill a Minecraft husk (%lld hits, kills %d)"), C().Hits - C64Hits0, C().Kills));
		Check(C64Flag, TEXT("pistol fire frame shown (muzzle flash sprite)"));
		return true;
	}, 7);

	// ---- pickups (Doom rules) ----
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("kind"), TEXT("ammo")); A->SetStringField(TEXT("what"), TEXT("bullets")); A->SetNumberField(TEXT("dist"), 2.0);
		AddCommand(TEXT("c64.drop"), A, [](bool, const FCrbResult*) {});
	}
	Add(TEXT("c64 pickup"), [this, C] { C64Ammo0 = C().AmmoOf(TEXT("bullets")); C64Msg0 = C().MessageSeq; Host->SetMove(1, 0); }, [this, C](float T)
	{
		if (!(C().AmmoOf(TEXT("bullets")) > C64Ammo0 || T > 4.f)) return false;
		Host->SetMove(0, 0);
		Check(C().AmmoOf(TEXT("bullets")) == FMath::Min(C64Ammo0 + 10, 200) && C().MessageSeq > C64Msg0,
			FString::Printf(TEXT("Walking over a clip (iron nugget) adds 10 bullets: %d -> %d, \"%s\""), C64Ammo0, C().AmmoOf(TEXT("bullets")), *C().Message));
		Shot(TEXT("72_c64_pickup"));
		return true;
	}, 6);

	// ---- the whole arsenal (IDKFA) ----
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("what"), TEXT("all"));
		AddCommand(TEXT("c64.give"), A, [this](bool bOk, const FCrbResult*) { Check(bOk, TEXT("c64.give all (dev worlds): every weapon, full ammo, blue armor")); });
	}
	struct FW { const TCHAR* Key; int32 Slot; double Dist; float Hold; const TCHAR* Ammo; };
	for (const FW& W : { FW{ TEXT("fist"), 1, 1.6, 1.2f, TEXT("") }, FW{ TEXT("chainsaw"), 1, 1.6, 1.2f, TEXT("") }, FW{ TEXT("shotgun"), 3, 5, 1.3f, TEXT("shells") },
		FW{ TEXT("super"), 3, 5, 1.8f, TEXT("shells") }, FW{ TEXT("chaingun"), 4, 6, 1.2f, TEXT("bullets") }, FW{ TEXT("rocket"), 5, 9, 1.0f, TEXT("rockets") },
		FW{ TEXT("plasma"), 6, 7, 1.0f, TEXT("cells") }, FW{ TEXT("bfg"), 7, 10, 2.4f, TEXT("cells") }, FW{ TEXT("unmaker"), 8, 7, 1.0f, TEXT("cells") } })
	{
		{
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("weapon"), W.Key);
			AddCommand(TEXT("c64.select"), A, [](bool, const FCrbResult*) {});
		}
		AddC64Spawn(TEXT("minecraft:husk"), W.Dist);
		Add(FString(TEXT("c64 fire ")) + W.Key, [this, C, W] { C64Shots0 = C().Shots; C64Hits0 = C().Hits; C64Ammo0 = W.Ammo[0] ? C().AmmoOf(W.Ammo) : 0; C64Flag = false; }, [this, C, W](float T)
		{
			if (T > 0.5f && T < 0.5f + W.Hold) Host->PrimaryPressed(true);
			else Host->PrimaryPressed(false);
			if (!C64Flag && C().Weapon == W.Key && Host->C64Frame > 0 && Host->C64Shown == W.Key) { Shot(FString::Printf(TEXT("73_c64_%s"), W.Key)); C64Flag = true; }
			if (T < W.Hold + 1.6f) return false;
			const bool bUsesAmmo = W.Ammo[0] != 0;
			Check(C().Weapon == W.Key && C().Shots > C64Shots0 && (!bUsesAmmo || C().AmmoOf(W.Ammo) < C64Ammo0),
				FString::Printf(TEXT("%s fires (%lld shots%s)"), W.Key, C().Shots - C64Shots0, bUsesAmmo ? *FString::Printf(TEXT(", %s %d -> %d"), W.Ammo, C64Ammo0, C().AmmoOf(W.Ammo)) : TEXT("")));
			Check(C().Hits > C64Hits0, FString::Printf(TEXT("%s damages a Minecraft husk at %.0f blocks (%lld hits)"), W.Key, W.Dist, C().Hits - C64Hits0));
			Check(C64Flag, FString::Printf(TEXT("%s fire frame drawn"), W.Key));
			return true;
		}, W.Hold + 4.f);
		AddC64MobCheck(FString(W.Key), true);
	}

	// ---- armor (Doom: blue armor takes half) ----
	Add(TEXT("c64 armor before"), [this, C] { C64Hp0 = C().Health; C64Armor0 = C().Armor; }, [](float T) { return T > 0.3f; }, 2);
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetNumberField(TEXT("amount"), 10);
		AddCommand(TEXT("c64.hurt"), A, [](bool, const FCrbResult*) {});
	}
	Add(TEXT("c64 armor"), [] {}, [this, C](float T)
	{
		if (!(C().Armor < C64Armor0 || T > 3.f)) return false;
		// 10 Minecraft damage -> 30 Doom damage; MegaArmor absorbs 15
		Check(C64Armor0 - C().Armor == 15 && C64Hp0 - C().Health == 15 && Host->C64Hurt > 0,
			FString::Printf(TEXT("Damage on the Doom scale with MegaArmor absorbing half: armor %d -> %d, health %d -> %d, red pain flash"), C64Armor0, C().Armor, C64Hp0, C().Health));
		Shot(TEXT("74_c64_hurt"));
		return true;
	}, 5);

	// ---- switch weapons with the number keys ----
	Add(TEXT("c64 slot keys"), [this] { Host->SelectSlot(1); }, [this, C](float T)   // key 2 = pistol
	{
		if (!(C().Weapon == TEXT("pistol") || T > 3.f)) return false;
		Check(C().Weapon == TEXT("pistol"), TEXT("Number key 2 switches to the pistol (Doom slot)"));
		return true;
	}, 5);

	// ---- off ----
	AddKey(EKeys::M, TEXT("M"));
	Add(TEXT("mods for craft64 off"), [] {}, [](float T) { return (CrbMenus::IsOpen() && T > 0.5f) || T > 5.f; }, 6);
	AddClickRow(TEXT("mod:craft64"), TEXT("Mods menu"));
	AddClickRow(TEXT("resume"), TEXT("Mods menu"));
	Add(TEXT("c64 off"), [] {}, [this, C](float T)
	{
		if (!(!C().bOn && T > 1.f) && T < 6.f) return false;
		ACrbHUD* Hud = C64HudOf(Host);
		Check(!Host->bCraft64Enabled && !C().bOn && Hud && Hud->Drawn.bHotbar && !Hud->Drawn.bC64Hud && Host->GetState().MaxHealth <= 20.5f,
			FString::Printf(TEXT("mod:craft64 OFF: vanilla HUD back, max health %.0f"), Host->GetState().MaxHealth));
		Check(FMath::IsNearlyEqual(C64CVar(TEXT("r.ScreenPercentage")), C64Pct0, 0.5f), FString::Printf(TEXT("render resolution restored (%.0f %%)"), C64CVar(TEXT("r.ScreenPercentage"))));
		Shot(TEXT("75_c64_off"));
		return true;
	}, 8);
	AddCommand(TEXT("player.reset"), nullptr, [](bool, const FCrbResult*) {});
}
