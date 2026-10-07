// Minecraft x Elden Combat test suite (-CrbTest=ec): drives the mod through the real Mods menu and input paths in the
// disposable superflat test world and verifies the server's combat frames, the 3D Steve presentation and - most of
// all - the ON / OFF lifecycle (everything the mod adds is gone when it is off; vanilla combat works again).
#include "CrbTest.h"
#include "CrbHost.h"
#include "CrbMenus.h"
#include "CrbHUD.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "InputCoreTypes.h"

namespace { constexpr double ECFloorY = -60.0; }

void FCrbTest::AddECOp(const FString& Op, TSharedPtr<FJsonObject> Args, const FString& Expect)
{
	AddCommand(Op, Args.IsValid() ? Args : MakeShared<FJsonObject>(), [this, Op, Expect](bool bOk, const FCrbResult* R)
	{
		ECLastReply = R ? R->Message : FString(TEXT("timeout"));
		ECLastJson = R ? R->Json : nullptr;
		// Expect "!text": the op must be refused with that text (the mod's tools while it is off)
		if (Expect.StartsWith(TEXT("!"))) Check(!bOk && ECLastReply.Contains(Expect.Mid(1)), Op + TEXT(" refused: ") + ECLastReply);
		else if (!Expect.IsEmpty()) Check(bOk && ECLastReply.Contains(Expect), Op + TEXT(": ") + ECLastReply);
	});
}

void FCrbTest::BuildEC()
{
	auto E = [this]() -> const FCrbECState& { return Host->GetState().EC; };
	const FString Row = ECRing ? TEXT("mod:eldenring") : TEXT("mod:eldencombat");
	auto Note = [this]() { AnimSeen.Add(Host->ECAnimClipName); AnimSeen.Add(TEXT("act:") + Host->GetState().EC.Act); };
	auto Tp = [this](double X, double Y, double Z, float Yaw)
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetNumberField(TEXT("x"), X); A->SetNumberField(TEXT("y"), Y); A->SetNumberField(TEXT("z"), Z); A->SetNumberField(TEXT("yaw"), Yaw);
		AddCommand(TEXT("test.tp"), A, [](bool, const FCrbResult*) {});
		Add(TEXT("ec settle"), [this, Yaw] { Host->SetMove(0, 0); Host->LookYaw = Yaw; Host->LookPitch = -8; }, [](float T) { return T > 0.6f; }, 3);
	};

	// ---- setup: vanilla, survival, every character mod off ----
	Add(TEXT("ec setup"), [this] { Host->bInputOverride = true; Host->SetMove(0, 0); Host->SetButtons(false, false, false); Host->bECKeysFromTest = true;
		if (Host->bEldenCombatEnabled) Host->ToggleEldenCombat(); if (Host->bPhysicsPortalEnabled) Host->TogglePhysicsPortal(); if (Host->bSm64Enabled) Host->ToggleSm64();
		if (Host->bAvatarEnabled) Host->ToggleAvatar(); if (Host->bCraft64Enabled) Host->ToggleCraft64(); Host->ViewMode = 0; },
		[](float T) { return T > 0.8f; }, 3);
	AddCommand(TEXT("fixture.off"), nullptr, [](bool, const FCrbResult*) {});
	AddCommand(TEXT("player.reset"), nullptr, [](bool, const FCrbResult*) {});
	{ TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("mode"), TEXT("survival")); AddCommand(TEXT("gamemode.set"), A, [](bool, const FCrbResult*) {}); }
	Tp(0.5, ECFloorY, 0.5, 0);
	Add(TEXT("ec off baseline"), [] {}, [this, E](float T)
	{
		if (T < 0.5f) return false;
		Check(!Host->bEldenCombatEnabled && !E().bOn && !Host->ECSteve.IsAcquired() && Host->ViewMode == 0, TEXT("Before: Minecraft \u00d7 Elden Combat OFF - vanilla first person, no combat state, no mod components"));
		return true;
	}, 3);
	AddECOp(TEXT("ec.kit"), nullptr, TEXT("!is off"));   // the mod's tools are unavailable while it is off

	// ---- ON from the existing Mods menu ----
	AddKey(EKeys::M, TEXT("M"));
	Add(TEXT("mods for ec"), [] {}, [this](float T)
	{
		if (!((CrbMenus::IsOpen() && T > 0.6f) || T > 5.f)) return false;
		Check(CrbMenus::RowKeys().Contains(TEXT("mod:eldencombat")) && CrbMenus::RowKeys().Contains(TEXT("mod:eldenring")), TEXT("Mods menu lists Minecraft \u00d7 Elden Combat (mod:eldencombat) and Elden Ring Combat (Steve) (mod:eldenring)"));
		ECShotN(TEXT("70_ec_mods_menu"));
		return true;
	}, 6);
	AddClickRow(Row, TEXT("Mods menu"));
	AddClickRow(TEXT("resume"), TEXT("Mods menu"));
	Add(TEXT("ec on"), [] {}, [this, E](float T)
	{
		if (!(E().bOn && T > 1.5f) && T < 8.f) return false;
		const FCrbECSteve& S = Host->ECSteve;
		Check(Host->bEldenCombatEnabled && E().bOn && E().Style == (ECRing ? TEXT("ring") : TEXT("minecraft")), FString::Printf(TEXT("%s ON: Java combat rules active, style %s (%s)"), ECRing ? TEXT("mod:eldenring") : TEXT("mod:eldencombat"), *E().Style, *E().Act));
		if (ECRing) Check(Host->bECLookApplied && Host->ECSteve.IsRing(), TEXT("Elden Ring style: R_ animation rewrite loaded, camera grade applied (world lighting untouched)"));
		Check(S.IsAcquired() && S.GetMesh() && S.GetMesh()->IsVisible(), TEXT("Custom 3D Steve rig drawn as the combat character: ") + (S.LoadError.IsEmpty() ? FString(TEXT("ok")) : S.LoadError));
		Check(S.ClipsLoaded == (int32)ECrbECClip::Count, FString::Printf(TEXT("Combat animation set: %d / %d clips (missing: %s)"), S.ClipsLoaded, (int32)ECrbECClip::Count, *FString::Join(S.MissingClips, TEXT(","))));
		Check(Host->Avatar.bSuppressPlayer, TEXT("Vanilla player model / first-person hands hidden while the mod draws Steve"));
		ECShotN(TEXT("71_ec_on"));
		return true;
	}, 10);

	// ---- weapons: kit, sword in hand (voxel copy of the item sprite), shield ----
	AddECOp(TEXT("ec.kit"), nullptr, TEXT("kit"));
	{ TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("item"), TEXT("minecraft:iron_sword")); AddECOp(TEXT("ec.select"), A, TEXT("holding")); }
	Add(TEXT("ec weapon"), [] {}, [this, E](float T)
	{
		if (!(Host->ECSteve.WeaponVisible() && T > 0.8f) && T < 4.f) return false;
		if (ECRing) Check(E().Weapon == TEXT("sword") && Host->ECSteve.WeaponVisible(), FString::Printf(TEXT("Iron sword = straight sword class; modelled longsword (%d triangles) in Steve's right hand, heater shield on the left arm"), Host->ECSteve.WeaponTris));
		else Check(E().Weapon == TEXT("sword") && Host->ECSteve.WeaponVisible(), FString::Printf(TEXT("Iron sword = straight sword class; built from its own item sprite as %d voxels in Steve's right hand"), Host->ECSteve.WeaponVoxels));
		Check(E().bShield, TEXT("Shield in the offhand (guard blocks fully, longer parry window)"));
		ECShotN(TEXT("72_ec_weapon"));
		return true;
	}, 6);

	// ---- lock-on ----
	{ TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("kind"), TEXT("golem")); A->SetNumberField(TEXT("dist"), 2.8); A->SetBoolField(TEXT("passive"), true); A->SetNumberField(TEXT("yaw"), 0); AddECOp(TEXT("ec.dummy"), A, TEXT("spawned")); }
	Add(TEXT("ec lock"), [this] { ++Host->ECLock; }, [this, E](float T)
	{
		if (!(E().Lock.bValid && T > 1.0f) && T < 3.f) return false;
		Check(E().Lock.bValid, FString::Printf(TEXT("Lock-on: target #%d (%s), camera turns to it"), E().Lock.Id, *E().Lock.Name));
		ECShotN(TEXT("73_ec_lock"));
		return true;
	}, 5);

	// ---- light attack combo ----
	Add(TEXT("ec light combo"), [this, E] { AnimSeen.Reset(); ECCount0 = E().Hits; ECStam0 = E().Stamina; ECFlag = false; ECStep = 0; }, [this, E, Note](float T)
	{
		Note();
		if (ECStep < 3 && T > 0.15f + ECStep * 0.45f) { ++Host->ECLight; ++ECStep; }
		if (!ECFlag && E().Act == TEXT("LIGHT") && E().T >= E().W) { ECShotN(TEXT("74_ec_light")); ECFlag = true; }
		if (T < 2.2f) return false;
		Check(E().Hits >= ECCount0 + 2 && AnimSeen.Contains(TEXT("Light1")) && AnimSeen.Contains(TEXT("Light2")),
			FString::Printf(TEXT("Light attacks land (%d hits), combo plays Light1 -> Light2 -> Light3: %s"), E().Hits - ECCount0, *FString::Join(AnimSeen.Array(), TEXT(","))));
		if (ECRing) Check(Host->ECSteve.TrailQuadsMax > 0, FString::Printf(TEXT("Swing trail drawn on the active frames (%d segments)"), Host->ECSteve.TrailQuadsMax));
		Check(E().Lock.bValid ? E().Lock.Hp < E().Lock.Max : true, FString::Printf(TEXT("Real Minecraft damage on the iron golem: %.1f / %.1f"), E().Lock.Hp, E().Lock.Max));
		return true;
	}, 5);
	Add(TEXT("ec stamina"), [] {}, [this, E](float T)
	{
		if (T < 0.05f) return false;
		Check(E().Attacks > 0 && ECStam0 > 0, FString::Printf(TEXT("Stamina spent by attacks and regenerating: %.0f / %.0f"), E().Stamina, E().MaxStamina));
		return true;
	}, 2);

	// ---- charged heavy ----
	Add(TEXT("ec charged heavy"), [this, E] { AnimSeen.Reset(); Host->bECHeavyHeld = true; ++Host->ECHeavy; ECFlag = false; }, [this, E, Note](float T)
	{
		Note();
		if (!ECFlag && E().Act == TEXT("CHARGE") && T > 0.7f) { ECShotN(TEXT("75_ec_charge")); ECFlag = true; }
		if (T > 1.1f && Host->bECHeavyHeld) { Host->bECHeavyHeld = false; }
		if (T < 2.2f) return false;
		Check(AnimSeen.Contains(TEXT("act:CHARGE")) && AnimSeen.Contains(TEXT("Heavy")), TEXT("Holding heavy charges (Charge clip), release swings the charged heavy: ") + FString::Join(AnimSeen.Array(), TEXT(",")));
		return true;
	}, 5);

	// ---- riposte: the target's poise broken (debug tool), then a light attack = critical hit ----
	AddECOp(TEXT("ec.breakPoise"), nullptr, TEXT("staggered"));
	Add(TEXT("ec riposte"), [this, E] { AnimSeen.Reset(); ECCount0 = (int32)E().RiposteSeq; ++Host->ECLight; }, [this, E, Note](float T)
	{
		Note();
		if (T > 0.6f && T < 0.7f) ECShotN(TEXT("76_ec_riposte"));
		if (!(E().RiposteSeq > ECCount0 && T > 1.6f) && T < 3.f) return false;
		Check(E().RiposteSeq > ECCount0 && AnimSeen.Contains(TEXT("Riposte")), TEXT("Light attack on the staggered enemy = riposte (critical hit, Riposte clip): ") + FString::Join(AnimSeen.Array(), TEXT(",")));
		return true;
	}, 5);
	// ---- natural poise break on a zombie (no debug help) ----
	AddECOp(TEXT("ec.clearDummies"), nullptr, TEXT("removed"));
	{ TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("kind"), TEXT("zombie")); A->SetNumberField(TEXT("dist"), 2.4); A->SetBoolField(TEXT("passive"), true); A->SetNumberField(TEXT("yaw"), 0); AddECOp(TEXT("ec.dummy"), A, TEXT("spawned")); }
	Add(TEXT("ec stagger"), [this, E] { ECCount0 = (int32)E().MobStaggerSeq; ECStep = 0; Host->LookYaw = 0; }, [this, E](float T)
	{
		if (E().MobStaggerSeq == ECCount0 && T > 0.3f + ECStep * 0.5f && ECStep < 6) { ++Host->ECLight; ++ECStep; }
		if (E().MobStaggerSeq == ECCount0 && T < 4.f) return false;
		Check(E().MobStaggerSeq > ECCount0, FString::Printf(TEXT("Mob poise breaks after %d sword hits: the zombie is staggered (riposte open)"), ECStep));
		return true;
	}, 6);
	AddECOp(TEXT("ec.clearDummies"), nullptr, TEXT("removed"));

	// ---- defence against real mob hits (the nearest mob's own melee attack) ----
	{ TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("kind"), TEXT("zombie")); A->SetNumberField(TEXT("dist"), 1.6); A->SetBoolField(TEXT("passive"), true); A->SetNumberField(TEXT("yaw"), 0); AddECOp(TEXT("ec.dummy"), A, TEXT("spawned")); }
	AddECOp(TEXT("ec.heal"), nullptr, TEXT("healed"));
	// dodge roll: the hit lands inside the invincibility frames
	Add(TEXT("ec dodge"), [this, E] { AnimSeen.Reset(); ECCount0 = (int32)E().DodgeSeq; ECHp0 = Host->GetState().Health; Host->SetMove(0, 1); ++Host->ECDodge; ECFlag = false; }, [this, E, Note](float T)
	{
		Note();
		if (!ECFlag && E().bIFrames) { ECFlag = true; Host->SendCommand(TEXT("ec.mobHit")); ECShotN(TEXT("77_ec_dodge")); }
		if (T > 0.3f) Host->SetMove(0, 0);
		if (!(E().DodgeSeq > ECCount0 && T > 1.f) && T < 2.5f) return false;
		Check(E().DodgeSeq > ECCount0 && Host->GetState().Health >= ECHp0, FString::Printf(TEXT("Dodge roll i-frames: the zombie's hit passes through (health %.0f -> %.0f), %s"), ECHp0, Host->GetState().Health, *FString::Join(AnimSeen.Array(), TEXT(","))));
		return true;
	}, 4);
	// guard with the shield (a full stamina bar first: guarding on an empty bar is the guard-break test below)
	AddECOp(TEXT("ec.stamina"), nullptr, TEXT("stamina"));
	Add(TEXT("ec guard"), [this, E] { ECCount0 = (int32)E().BlockSeq; ECHp0 = Host->GetState().Health; Host->ECSecondary(true); ECFlag = false; }, [this, E](float T)
	{
		if (!ECFlag && E().Act == TEXT("BLOCK") && T > 0.4f) { ECFlag = true; Host->SendCommand(TEXT("ec.mobHit")); }
		if (ECFlag && T > 0.7f && T < 0.8f) ECShotN(TEXT("78_ec_guard"));
		if (!(E().BlockSeq > ECCount0 && T > 1.f) && T < 3.f) return false;
		Check(E().BlockSeq > ECCount0 && Host->GetState().Health >= ECHp0, FString::Printf(TEXT("Shield guard absorbs the hit (stamina %.0f, health %.0f)"), E().Stamina, Host->GetState().Health));
		Host->ECSecondary(false);
		return true;
	}, 5);
	// parry: the hit inside the window staggers the attacker
	AddECOp(TEXT("ec.stamina"), nullptr, TEXT("stamina"));
	Add(TEXT("ec parry"), [this, E] { AnimSeen.Reset(); ECCount0 = (int32)E().ParrySeq; ECHp0 = Host->GetState().Health; ++Host->ECParry; ECFlag = false; }, [this, E, Note](float T)
	{
		Note();
		if (!ECFlag && E().bParry) { ECFlag = true; Host->SendCommand(TEXT("ec.mobHit")); }
		if (ECFlag && T > 0.35f && T < 0.45f) ECShotN(TEXT("79_ec_parry"));
		if (!(E().ParrySeq > ECCount0 && T > 1.f) && T < 3.f) return false;
		Check(E().ParrySeq > ECCount0 && Host->GetState().Health >= ECHp0, TEXT("Parry window deflects the hit and staggers the zombie: ") + FString::Join(AnimSeen.Array(), TEXT(",")));
		return true;
	}, 5);
	// an undefended hit goes through vanilla damage
	Add(TEXT("ec hurt"), [this, E] { ECCount0 = (int32)E().HurtSeq; ECHp0 = Host->GetState().Health; }, [this, E](float T)
	{
		// the parried zombie is still staggered (weakened) for 3 s: wait it out, then take a real hit
		if (T < 3.4f) { ECFlag = false; return false; }
		if (!ECFlag) { ECFlag = true; Host->SendCommand(TEXT("ec.mobHit")); }
		if (!(E().HurtSeq > ECCount0 && T > 4.0f) && T < 5.5f) return false;
		Check(E().HurtSeq > ECCount0 && Host->GetState().Health < ECHp0, FString::Printf(TEXT("Undefended hit uses vanilla damage: health %.0f -> %.0f"), ECHp0, Host->GetState().Health));
		return true;
	}, 8);
	// guard break: guarding on an empty stamina bar
	AddECOp(TEXT("ec.heal"), nullptr, TEXT("healed"));
	Add(TEXT("ec guard break"), [this, E] { ECCount0 = (int32)E().GuardBreakSeq; Host->ECSecondary(true); ECFlag = false; ECStep = 0; }, [this, E](float T)
	{
		if (ECStep == 0 && E().Act == TEXT("BLOCK") && T > 0.3f) { TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetNumberField(TEXT("value"), 2); Host->SendCommand(TEXT("ec.stamina"), A); ECStep = 1; }
		if (ECStep == 1 && T > 0.5f) { Host->SendCommand(TEXT("ec.mobHit")); ECStep = 2; }
		if (!(E().GuardBreakSeq > ECCount0 && T > 1.f) && T < 3.f) return false;
		Check(E().GuardBreakSeq > ECCount0, TEXT("Guarding with no stamina breaks the guard (GUARD BROKEN)"));
		Host->ECSecondary(false);
		return true;
	}, 5);

	// ---- the mod's debug tools (F4 debug menu section + its own menu) ----
	Add(TEXT("ec debug menu"), [this] { Host->ToggleDebugMenu(); }, [this](float T)
	{
		if (T < 0.8f) return false;
		Check(CrbMenus::RowKeys().Contains(TEXT("ec:debug")), TEXT("F4 debug menu shows the Elden Combat section while the mod is on"));
		Host->CloseMenus(); Host->OpenECMenu();
		return true;
	}, 3);
	AddClickRow(TEXT("ec:overlay"), TEXT("Elden Combat menu"));
	Add(TEXT("ec debug shot"), [] {}, [this](float T)
	{
		if (T < 0.6f) return false;
		Check(Host->bECDebugOverlay && CrbMenus::RowKeys().Contains(TEXT("ec:infinite")), TEXT("Elden Combat debug menu: infinite stamina, attack arcs, overlay, spawners, poise break"));
		ECShotN(TEXT("80_ec_debug"));
		Host->CloseMenus();
		return true;
	}, 3);
	Add(TEXT("ec overlay shot"), [] {}, [this](float T) { if (T < 0.6f) return false; ECShotN(TEXT("81_ec_overlay")); return true; }, 3);

	// ---- OFF from the Mods menu: everything gone, vanilla back ----
	AddKey(EKeys::M, TEXT("M"));
	Add(TEXT("mods for ec off"), [] {}, [](float T) { return (CrbMenus::IsOpen() && T > 0.5f) || T > 5.f; }, 6);
	AddClickRow(Row, TEXT("Mods menu"));
	AddClickRow(TEXT("resume"), TEXT("Mods menu"));
	Add(TEXT("ec off"), [this] { Host->bECKeysFromTest = false; }, [this, E](float T)
	{
		if (!(!E().bOn && T > 1.2f) && T < 6.f) return false;
		Check(!Host->bEldenCombatEnabled && !E().bOn, TEXT("mod:eldencombat OFF: Java combat state cleared"));
		Check(!Host->ECSteve.IsAcquired() && !Host->bECDebugOverlay, TEXT("3D Steve, voxel weapon, shield and debug overlay destroyed"));
		Check(Host->ViewMode == 0 && !Host->Avatar.bSuppressPlayer && !Host->bECLookApplied, TEXT("Vanilla first-person camera, hands, player model and colour grade restored"));
		ECShotN(TEXT("82_ec_off"));
		return true;
	}, 8);
	AddECOp(TEXT("ec.status"), nullptr, TEXT("off"));
	Add(TEXT("ec off world"), [] {}, [this](float)
	{
		double Dummies = -1, Stag = -1; bool bSlowed = true;
		if (ECLastJson.IsValid()) { ECLastJson->TryGetNumberField(TEXT("dummies"), Dummies); ECLastJson->TryGetNumberField(TEXT("staggeredMobs"), Stag); ECLastJson->TryGetBoolField(TEXT("slowed"), bSlowed); }
		double DummyEntities = 0; if (ECLastJson.IsValid()) ECLastJson->TryGetNumberField(TEXT("dummyEntities"), DummyEntities);
		Check(Dummies == 0 && DummyEntities == 0 && Stag == 0 && !bSlowed, FString::Printf(TEXT("Nothing left in the world: %.0f dummies, %.0f staggered mobs, movement modifier %s"), DummyEntities, Stag, bSlowed ? TEXT("STILL ON") : TEXT("removed")));
		return true;
	}, 2);
	AddECOp(TEXT("ec.dummy"), nullptr, TEXT("!is off"));
	// vanilla combat is back: a plain left click hurts a pig
	Tp(0.5, ECFloorY, 0.5, 0);
	AddC64Spawn(TEXT("pig"), 2.0);
	Add(TEXT("ec vanilla aim"), [this] { Host->LookYaw = 0; Host->LookPitch = -22; }, [](float T) { return T > 1.0f; }, 3);
	AddC64MobCheck(TEXT("pig before"), false);
	Add(TEXT("ec vanilla hit"), [this] { ECHp0 = C64MobHealth; Host->PrimaryPressed(true); }, [this](float T) { if (T > 0.15f) Host->PrimaryPressed(false); return T > 0.8f; }, 3);
	AddC64MobCheck(TEXT("pig after"), true);
	Add(TEXT("ec vanilla check"), [] {}, [this](float)
	{
		Check(ECHp0 > 0 && C64MobHealth < ECHp0, FString::Printf(TEXT("Vanilla Minecraft combat after OFF: left click hits the pig (%.1f -> %.1f)"), ECHp0, C64MobHealth));
		return true;
	}, 2);
	AddCommand(TEXT("player.reset"), nullptr, [](bool, const FCrbResult*) {});
}
