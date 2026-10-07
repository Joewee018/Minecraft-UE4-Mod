// Zombies game mode tests (part of -CrbTest=all, or alone with -CrbTest=zombies). Evidence comes from Java's exported
// match state (phase, round, points, perks, zombies and their animation state), op results, the inventory the match
// hands out (real Guns++ items) and the HUD's drawn record. Started and ended through the pause menu like a player.
#include "CrbTest.h"
#include "CrbHost.h"
#include "CrbHUD.h"
#include "CrbMenus.h"
#include "GameFramework/PlayerController.h"
#include "Dom/JsonObject.h"
#include "InputCoreTypes.h"

namespace
{
	int32 CountGuns(const FCrbState& S)
	{
		int32 N = 0;
		for (int32 I = 0; I < S.Slots.Num() && I < 41; ++I) if (S.Slots[I].Id == TEXT("minecraft:crossbow") || S.Slots[I].Id == TEXT("minecraft:carrot_on_a_stick")) ++N;
		return N;
	}
	int32 GunSlot(const FCrbState& S)
	{
		for (int32 I = 0; I < 9; ++I) if (S.Slots.IsValidIndex(I) && S.Slots[I].Id == TEXT("minecraft:crossbow")) return I;
		return -1;
	}
	const FCrbZmZombie* Nearest(const FCrbState& S, bool bInsideOnly)
	{
		const FCrbZmZombie* Best = nullptr; double BestD = 1e9;
		for (const FCrbZmZombie& Z : S.Zm.Zombies)
		{
			if (Z.Health <= 0 || Z.DeathTime > 0 || (bInsideOnly && !Z.bInside)) continue;
			const double D = FMath::Square(Z.X - S.X) + FMath::Square(Z.Z - S.Z);
			if (D < BestD) { BestD = D; Best = &Z; }
		}
		return Best;
	}
}

void FCrbTest::AddZmLookAt(const FString& Station)
{
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("lookAt"), Station);
	AddCommand(TEXT("zm.debug"), A, [this, Station](bool bOk, const FCrbResult* R)
	{
		TArray<FString> P; if (R) R->Message.ParseIntoArray(P, TEXT("|"));
		if (!bOk || P.Num() != 4) { Fail(TEXT("zm.debug lookAt ") + Station + TEXT(" failed: ") + (R ? R->Message : TEXT("timeout"))); return; }
		ZmTarget = FVector(FCString::Atod(*P[1]), FCString::Atod(*P[2]), FCString::Atod(*P[3]));
	});
	Add(TEXT("aim ") + Station, [] {}, [this](float T) { AimAt(ZmTarget.X, ZmTarget.Y, ZmTarget.Z); return T > 1.2f; }, 3);
}

void FCrbTest::AddZmInteract(const FString& Label, TFunction<void(const FString&)> OnResult)
{
	// Press F while aiming at the station; a "nothing here"/"wait" answer (aim or cooldown not settled yet) is retried
	// like a player pressing again, up to 4 presses.
	Add(TEXT("interact ") + Label, [this] { Host->LastInteractResult.Reset(); ZmPoints0 = Host->GetState().Zm.Points; Seq0 = 0; Walk0 = 0; }, [this, OnResult](float T)
	{
		AimAt(ZmTarget.X, ZmTarget.Y, ZmTarget.Z);
		if (T - Walk0 > 1.0f && Seq0 < 4 && (Seq0 == 0 || Host->LastInteractResult.StartsWith(TEXT("nothing here")) || Host->LastInteractResult == TEXT("wait")))
		{
			Host->LastInteractResult.Reset(); Host->InteractPressed(true); Walk0 = T; ++Seq0;
		}
		if (T - Walk0 > 0.1f) Host->InteractPressed(false);
		const FString& R = Host->LastInteractResult;
		if (!R.IsEmpty() && T - Walk0 > 0.5f && ((!R.StartsWith(TEXT("nothing here")) && R != TEXT("wait")) || Seq0 >= 4)) { OnResult(R); return true; }
		if (T > 7.f) { OnResult(R.IsEmpty() ? FString(TEXT("(no result)")) : R); return true; }
		return false;
	}, 8);
}

void FCrbTest::AddGlassShot(const FString& Block, const FString& ShotPrefix)
{
	TSharedPtr<FJsonObject> G = MakeShared<FJsonObject>(); G->SetStringField(TEXT("block"), Block); G->SetNumberField(TEXT("distance"), 5);
	AddCommand(TEXT("test.glass"), G, [this, Block](bool bOk, const FCrbResult* R)
	{
		double X = 0, Y = 0, Z = 0;
		if (R && R->Json.IsValid()) { R->Json->TryGetNumberField(TEXT("x"), X); R->Json->TryGetNumberField(TEXT("y"), Y); R->Json->TryGetNumberField(TEXT("z"), Z); }
		GlassPos = FIntVector((int32)X, (int32)Y, (int32)Z);
		if (!bOk) Fail(TEXT("test.glass refused for ") + Block + TEXT(": ") + (R ? R->Message : TEXT("timeout")));
	});
	Add(TEXT("glass view ") + Block, [this] { Host->ViewMode = 0; }, [this, ShotPrefix](float T)
	{
		AimAt(GlassPos.X + 0.5, GlassPos.Y + 0.5, GlassPos.Z + 0.5);
		if (T < 1.0f) return false;
		Shot(ShotPrefix + TEXT("_a_intact"));
		Bursts0 = Host->ShatterFx.BurstsStarted;
		return true;
	}, 3);
	Add(TEXT("arrow at ") + Block, [this]
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetNumberField(TEXT("x"), GlassPos.X); A->SetNumberField(TEXT("y"), GlassPos.Y); A->SetNumberField(TEXT("z"), GlassPos.Z);
		Host->SendCommand(TEXT("test.arrow"), A);
		Y0 = -1;
	}, [this, Block, ShotPrefix](float T)
	{
		AimAt(GlassPos.X + 0.5, GlassPos.Y + 0.4, GlassPos.Z + 0.5);
		if (Host->ShatterFx.BurstsStarted > Bursts0 && Y0 < 0) Y0 = T;
		if (Y0 >= 0 && T - Y0 > 0.12f && T - Y0 < 0.2f) Shot(ShotPrefix + TEXT("_b_burst"));
		if (Y0 >= 0 && T - Y0 > 0.25f && T - Y0 < 0.4f && !Metrics->HasField(TEXT("glassLive_") + Block)) Metrics->SetNumberField(TEXT("glassLive_") + Block, Host->ShatterFx.LiveShards());
		if (Y0 >= 0 && T - Y0 > 1.0f)
		{
			Shot(ShotPrefix + TEXT("_c_falling"));
			const int32 World0 = Host->World.StateAt(GlassPos);
			Check(Host->ShatterFx.LastSource == TEXT("arrow") && Host->ShatterFx.MissingModel == 0,
				FString::Printf(TEXT("%s hit by a real arrow shattered: Java event source '%s', the block's own model used (%d models missing)"), *Block, *Host->ShatterFx.LastSource, Host->ShatterFx.MissingModel));
			Check(Host->ShatterFx.LiveShards() >= 12 && Host->ShatterFx.MaxShardTravel > 0.8f,
				FString::Printf(TEXT("%s shard burst: %d shards alive after 1 s, furthest %.1f blocks from the window (gravity, spin, bounce)"), *Block, Host->ShatterFx.LiveShards(), Host->ShatterFx.MaxShardTravel));
			Check(World0 == 0, FString::Printf(TEXT("%s removed in Java (copied block state now %d)"), *Block, World0));
			return true;
		}
		if (T > 4.f) { Fail(FString::Printf(TEXT("%s: no shatter burst after an arrow (Java events total %lld, bursts %d)"), *Block, (long long)Host->GetState().ShatterTotal, Host->ShatterFx.BurstsStarted)); return true; }
		return false;
	}, 5);
	Add(TEXT("shards gone ") + Block, [] {}, [this, Block](float T)
	{
		if (T < 4.5f && Host->ShatterFx.LiveBursts() > 0) return false;
		Check(Host->ShatterFx.LiveBursts() == 0, FString::Printf(TEXT("%s shards shrank away and were recycled (%d bursts left)"), *Block, Host->ShatterFx.LiveBursts()));
		return true;
	}, 5);
}

void FCrbTest::BuildGlass()
{
	AddCommand(TEXT("player.reset"), nullptr, [](bool, const FCrbResult*) {});
	Add(TEXT("glass settle"), [this] { Host->bInputOverride = true; Host->SetMove(0, 0); Host->LookYaw = 0; Host->LookPitch = 0; Host->ViewMode = 0; }, [](float T) { return T > 0.7f; }, 3);
	AddGlassShot(TEXT("minecraft:glass"), TEXT("50_glass"));
	AddGlassShot(TEXT("minecraft:red_stained_glass_pane"), TEXT("51_pane"));
	AddGlassShot(TEXT("minecraft:tinted_glass"), TEXT("52_tinted"));
}

void FCrbTest::BuildZombies()
{
	auto HUD = [this]() { return Cast<ACrbHUD>(Host->GetWorld()->GetFirstPlayerController()->GetHUD()); };
	AddCommand(TEXT("gow.mutant.clear"), nullptr, [](bool, const FCrbResult*) {});
	AddCommand(TEXT("zm.stop"), nullptr, [](bool, const FCrbResult*) {});
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("mode"), TEXT("survival"));
		AddCommand(TEXT("gamemode.set"), A, [](bool, const FCrbResult*) {});
	}

	// ---- start from the pause menu: Esc -> Game Modes... -> Zombies ----
	Add(TEXT("pause for zombies"), [this] { Host->CloseMenus(); PressKey(EKeys::Escape); }, [this](float T)
	{
		if (Host->bPauseOpen && CrbMenus::IsOpen() && T > 0.6f) { Pass(TEXT("Esc opened the pause menu")); return true; }
		if (T > 3.f && !Host->bPauseOpen) { Fail(TEXT("Esc did not open the pause menu (viewport focus?); opening it directly")); Host->TogglePause(); return true; }
		return false;
	}, 6);
	AddClickRow(TEXT("pause:modes"), TEXT("Pause menu"));
	Add(TEXT("game modes shot"), [] {}, [this](float T) { if (T < 0.6f) return false; Shot(TEXT("40_zm_game_modes_menu")); return true; }, 3);
	AddClickRow(TEXT("zm:start"), TEXT("Game Modes menu"));
	Add(TEXT("zm started"), [] {}, [this, HUD](float T)
	{
		const FCrbState& S = Host->GetState();
		if (T < 2.f || (!S.Zm.IsActive() && T < 15.f)) return false;
		Check(S.Zm.IsActive() && !Host->IsMenuOpen(), FString::Printf(TEXT("Zombies started from Esc > Game Modes: Java phase %s, menu closed"), *S.Zm.Phase));
		Check(S.Zm.Points == 500 && S.Zm.Round <= 1, FString::Printf(TEXT("Match state: %d points (BO2 start 500), round %d"), S.Zm.Points, S.Zm.Round));
		Check(S.GameMode == TEXT("adventure"), TEXT("Arena is protected: player in adventure mode (was survival) -> ") + S.GameMode);
		const int32 Guns = CountGuns(S);
		Check(Guns >= 1, FString::Printf(TEXT("Guns++ starting pistol handed out from the mod's own loot table (%d Guns++ guns in inventory)"), Guns));
		Metrics->SetNumberField(TEXT("zmStartGuns"), Guns);
		ACrbHUD* Hd = HUD();
		Check(Hd && Hd->Drawn.bZombiesHud && Hd->Drawn.ZmPoints == 500, TEXT("Zombies HUD drawn (points, round tally, perks, prompts)"));
		Host->LookYaw = 0; Host->LookPitch = -5;
		return true;
	}, 18);
	// Held-item check (user report: the sword in hand looked broken): Unreal frame + the real Minecraft window, same moment.
	Add(TEXT("sword first person"), [this] { Host->SelectSlot(0); Host->ViewMode = 0; Host->LookYaw = 180; Host->LookPitch = -15; }, [this](float T)
	{
		if (Cross(1.2f)) { TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("name"), TEXT("zm_sword")); A->SetStringField(TEXT("camera"), TEXT("first")); Host->SendCommand(TEXT("debug.capture"), A); }
		if (Cross(1.6f)) Shot(TEXT("41b_sword_first_person_ue"));
		return T > 2.2f;
	}, 4);
	Add(TEXT("zm spawn room"), [this] { Host->LookYaw = 180; Host->LookPitch = 0; }, [this](float T) { if (T < 1.2f) return false; Shot(TEXT("41_zm_spawn_room")); return true; }, 3);

	// ---- round 1: zombies spawn outside, walk to a window and tear the boards ----
	Add(TEXT("zm round 1"), [this] { bZmTearSeen = false; }, [this](float T)
	{
		const FCrbState& S = Host->GetState();
		for (const FCrbZmZombie& Z : S.Zm.Zombies) if (Z.Anim == 5 /* TEAR */) bZmTearSeen = true;
		if (S.Zm.Phase == TEXT("ROUND") && S.Zm.Round == 1 && S.Zm.Zombies.Num() > 0 && bZmTearSeen)
		{
			Pass(FString::Printf(TEXT("Round 1 running: %d zombies near the arena, a zombie is tearing a barricade (state TEAR), %d left this round"), S.Zm.Zombies.Num(), S.Zm.Left));
			if (const FCrbZmZombie* Z = Nearest(S, false)) AimAt(Z->X, Z->Y + 1.4, Z->Z);
			return true;
		}
		if (T > 45.f) { Fail(FString::Printf(TEXT("Round 1 did not reach a barricade attack: phase %s round %d zombies %d tear %d"), *S.Zm.Phase, S.Zm.Round, S.Zm.Zombies.Num(), bZmTearSeen)); return true; }
		return false;
	}, 47);
	Add(TEXT("zm window shot"), [] {}, [this](float T)
	{
		if (const FCrbZmZombie* Z = Nearest(Host->GetState(), false)) AimAt(Z->X, Z->Y + 1.2, Z->Z);
		if (Cross(0.8f)) Shot(TEXT("42_zm_zombie_at_window"));
		return T > 1.0f;
	}, 3);

	// ---- shoot with the Guns++ pistol (hold RMB, real Guns++ bullets) at a zombie already inside the room ----
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetBoolField(TEXT("hold"), true);
		AddCommand(TEXT("zm.debug"), A, [](bool, const FCrbResult*) {});
	}
	Add(TEXT("face room"), [this] { Host->LookYaw = 180; Host->LookPitch = 0; }, [](float T) { return T > 0.6f; }, 2);
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetNumberField(TEXT("spawnZombie"), 6);
		AddCommand(TEXT("zm.debug"), A, [this](bool bOk, const FCrbResult* R) { Check(bOk, TEXT("Test zombie inside the spawn room: ") + (R ? R->Message : FString(TEXT("timeout")))); });
	}
	Add(TEXT("zm shoot"), [this] { ZmPoints0 = Host->GetState().Zm.Points; ZmKills0 = Host->GetState().Zm.Kills; const int32 G = GunSlot(Host->GetState()); if (G >= 0) Host->SelectSlot(G); }, [this](float T)
	{
		const FCrbState& S = Host->GetState();
		if (const FCrbZmZombie* Z = Nearest(S, false)) AimAt(Z->X, Z->Y + 1.3, Z->Z);
		if (T > 0.6f && T < 9.f) Host->SecondaryPressed(FMath::Fmod(T, 0.5f) < 0.3f); // pull the trigger repeatedly
		if (Cross(2.f)) Shot(TEXT("43_zm_shooting"));
		const bool bHit = S.Zm.Points > ZmPoints0 || S.Zm.Kills > ZmKills0;
		if ((bHit && T > 2.2f) || T > 10.f)
		{
			Host->SecondaryPressed(false);
			Check(bHit, FString::Printf(TEXT("Guns++ bullets hit zombies: points %d -> %d, kills %d -> %d, zombie hits %d (Guns++ projectiles seen %d, magazine %d, held %s)"),
				ZmPoints0, S.Zm.Points, ZmKills0, S.Zm.Kills, S.Zm.ZombieHits, S.Zm.BulletsFired, S.Zm.HeldMag, S.Slots.IsValidIndex(S.Selected) ? *S.Slots[S.Selected].Name : TEXT("?")));
			return true;
		}
		return false;
	}, 12);

	// ---- AI state machine: chase -> wind-up -> strike (damage only in the active frames) -> recover; stagger; death ----
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetBoolField(TEXT("hold"), true); A->SetBoolField(TEXT("heal"), true);
		AddCommand(TEXT("zm.debug"), A, [](bool, const FCrbResult*) {});
	}
	Add(TEXT("face ai"), [this] { Host->LookYaw = 180; Host->LookPitch = 5; Host->SetMove(0, 0); }, [](float T) { return T > 0.6f; }, 2);
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetNumberField(TEXT("spawnZombie"), 5);
		AddCommand(TEXT("zm.debug"), A, [](bool, const FCrbResult*) {});
	}
	Add(TEXT("ai attack cycle"), [this] { AnimSeen.Reset(); Y0 = Host->GetState().Health; X0 = -1; Z0 = -1; }, [this](float T)
	{
		const FCrbState& S = Host->GetState();
		const FCrbZmZombie* Z = Nearest(S, false);
		if (Z) AimAt(Z->X, Z->Y + 1.4, Z->Z);
		static const TCHAR* Names[] = { TEXT("SPAWN"), TEXT("IDLE"), TEXT("SEARCH"), TEXT("CHASE"), TEXT("BREACH"), TEXT("TEAR"), TEXT("WINDUP"), TEXT("STRIKE"), TEXT("RECOVER"), TEXT("STAGGER"), TEXT("DEATH") };
		if (Z && Z->Anim >= 0 && Z->Anim <= 10)
		{
			const FString N = Names[Z->Anim];
			if (!AnimSeen.Contains(N)) { AnimSeen.Add(N); if (N == TEXT("WINDUP") || N == TEXT("STRIKE")) Shot(TEXT("47b_ai_") + N.ToLower()); if (N == TEXT("CHASE")) Shot(TEXT("47a_ai_chase")); }
			if (N == TEXT("WINDUP") && X0 < 0) X0 = T;
			if (N == TEXT("STRIKE") && Z0 < 0) Z0 = T;
		}
		// Damage must not arrive before the strike frames (wind-up is the telegraph).
		if (S.Health < Y0 - 0.5f && PrevElapsed >= 0 && !Metrics->HasField(TEXT("aiFirstDamageT"))) Metrics->SetNumberField(TEXT("aiFirstDamageT"), T);
		const bool bCycle = AnimSeen.Contains(TEXT("CHASE")) && AnimSeen.Contains(TEXT("WINDUP")) && AnimSeen.Contains(TEXT("STRIKE")) && AnimSeen.Contains(TEXT("RECOVER"));
		if ((bCycle && T > Z0 + 0.6f && Z0 > 0) || T > 15.f)
		{
			double Dmg = 0; Metrics->TryGetNumberField(TEXT("aiFirstDamageT"), Dmg);
			Check(bCycle, TEXT("Zombie AI ran chase -> wind-up -> strike -> recover; states seen: ") + FString::Join(AnimSeen.Array(), TEXT(",")));
			Check(Z0 > 0 && Dmg > 0 && Dmg >= Z0 - 0.15 && X0 > 0 && Z0 - X0 > 0.25,
				FString::Printf(TEXT("Attack telegraphed then landed in the strike frames: wind-up at %.2f s, strike at %.2f s, first damage at %.2f s (health %.0f -> %.0f)"), X0, Z0, Dmg, Y0, S.Health));
			return true;
		}
		return false;
	}, 17);
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetNumberField(TEXT("hurtZombie"), 3); A->SetBoolField(TEXT("heal"), true);
		AddCommand(TEXT("zm.debug"), A, [](bool, const FCrbResult*) {});
	}
	Add(TEXT("ai stagger"), [] {}, [this](float T)
	{
		const FCrbZmZombie* Z = Nearest(Host->GetState(), false);
		if (Z) AimAt(Z->X, Z->Y + 1.4, Z->Z);
		if (Z && Z->Anim == 9) { Shot(TEXT("47c_ai_stagger")); Pass(TEXT("Hit reaction: the zombie entered STAGGER (attack cancelled, knocked back) when hit")); return true; }
		if (T > 2.f) { Fail(FString::Printf(TEXT("no STAGGER after a hit (state %d)"), Z ? Z->Anim : -1)); return true; }
		return false;
	}, 3);
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetBoolField(TEXT("killAll"), true);
		AddCommand(TEXT("zm.debug"), A, [](bool, const FCrbResult*) {});
	}
	Add(TEXT("ai death"), [] {}, [this](float T)
	{
		for (const FCrbZmZombie& Z : Host->GetState().Zm.Zombies)
			if (Z.Anim == 10 && Z.DeathTime > 0) { Shot(TEXT("47d_ai_death")); Pass(FString::Printf(TEXT("Death: state DEATH with the death fall playing (deathTime %d), no attacks or movement"), Z.DeathTime)); return true; }
		if (T > 2.f) { Fail(TEXT("no DEATH state exported after kill")); return true; }
		return false;
	}, 3);
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetBoolField(TEXT("heal"), true);
		AddCommand(TEXT("zm.debug"), A, [](bool, const FCrbResult*) {});
	}

	// ---- buys: door, perk, random weapon crate (F at the station) ----
	{
		// Hold the match between rounds while buying (no zombie can break in and end the run mid-test).
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetNumberField(TEXT("points"), 6000); A->SetBoolField(TEXT("hold"), true);
		AddCommand(TEXT("zm.debug"), A, [](bool, const FCrbResult*) {});
	}
	AddZmLookAt(TEXT("Open Door"));
	Add(TEXT("door prompt"), [] {}, [this, HUD](float T)
	{
		AimAt(ZmTarget.X, ZmTarget.Y, ZmTarget.Z);
		if (T < 0.6f) return false;
		ACrbHUD* Hd = HUD();
		const FString P = Hd ? Hd->Drawn.ZmPrompt : FString();
		if (P.Contains(TEXT("door")) || T > 4.f)
		{
			Check(P.Contains(TEXT("door")) && P.Contains(TEXT("750")), TEXT("Looking at the door shows the BO2 buy prompt: ") + P);
			Shot(TEXT("44_zm_door_prompt"));
			return true;
		}
		return false;
	}, 5);
	AddZmInteract(TEXT("door"), [this](const FString& R)
	{
		Check(R.Contains(TEXT("bought Open Door")), TEXT("F at the door: ") + R);
	});
	Add(TEXT("door opening"), [] {}, [this](float T)
	{
		// Door blocks are gone from Java at once; the panels slide away as display entities (rendered by Unreal).
		AimAt(ZmTarget.X, ZmTarget.Y, ZmTarget.Z);
		if (Cross(0.25f)) Shot(TEXT("44a_door_opening"));
		if (T < 1.6f) return false;
		const FIntVector B(FMath::FloorToInt(ZmTarget.X), FMath::FloorToInt(ZmTarget.Y), FMath::FloorToInt(ZmTarget.Z));
		Check(Host->World.StateAt(B) == 0, FString::Printf(TEXT("Door opened with its slide animation; doorway is passable (block state %d)"), Host->World.StateAt(B)));
		Shot(TEXT("44c_door_open"));
		return true;
	}, 3);
	AddZmLookAt(TEXT("Swift Step"));
	AddZmInteract(TEXT("perk"), [this](const FString& R)
	{
		Check(R.Contains(TEXT("bought Swift Step")), TEXT("F at the Swift Step machine: ") + R);
	});
	Add(TEXT("perk check"), [] {}, [this](float T)
	{
		if (T < 0.5f) return false;
		const FCrbZmState& Z = Host->GetState().Zm;
		Check(Z.Perks.Contains(TEXT("Swift Step")), TEXT("Perk owned and exported: ") + FString::Join(Z.Perks, TEXT(",")));
		return true;
	}, 2);
	// Guns++ bullet through glass (the datapack's own "setblock air destroy" on #ggunz:glass_pane, extended to all glass).
	Add(TEXT("face for bullet glass"), [this] { Host->LookYaw = 90; Host->LookPitch = 0; }, [](float T) { return T > 0.6f; }, 2);
	{
		TSharedPtr<FJsonObject> G = MakeShared<FJsonObject>(); G->SetStringField(TEXT("block"), TEXT("minecraft:glass")); G->SetNumberField(TEXT("distance"), 4);
		AddCommand(TEXT("test.glass"), G, [this](bool bOk, const FCrbResult* R)
		{
			double X = 0, Y = 0, Z = 0;
			if (R && R->Json.IsValid()) { R->Json->TryGetNumberField(TEXT("x"), X); R->Json->TryGetNumberField(TEXT("y"), Y); R->Json->TryGetNumberField(TEXT("z"), Z); }
			GlassPos = FIntVector((int32)X, (int32)Y, (int32)Z);
		});
	}
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetBoolField(TEXT("fillMags"), true);
		AddCommand(TEXT("zm.debug"), A, [](bool, const FCrbResult*) {});
	}
	Add(TEXT("shoot glass"), [this] { Bursts0 = Host->ShatterFx.BurstsStarted; const int32 G = GunSlot(Host->GetState()); if (G >= 0) Host->SelectSlot(G); Y0 = -1; }, [this](float T)
	{
		AimAt(GlassPos.X + 0.5, GlassPos.Y + 0.5, GlassPos.Z + 0.5);
		if (T > 0.6f && Y0 < 0) Host->SecondaryPressed(FMath::Fmod(T, 0.5f) < 0.3f);
		if (Host->ShatterFx.BurstsStarted > Bursts0 && Y0 < 0) { Y0 = T; Host->SecondaryPressed(false); }
		if (Y0 >= 0 && T - Y0 > 0.15f && T - Y0 < 0.25f) Shot(TEXT("49_zm_bullet_glass"));
		if (Y0 >= 0 && T - Y0 > 0.5f)
		{
			Check(Host->ShatterFx.LastSource == TEXT("bullet"), TEXT("Guns++ bullet shattered full glass (event source '") + Host->ShatterFx.LastSource + TEXT("')"));
			return true;
		}
		if (T > 8.f) { Host->SecondaryPressed(false); Fail(FString::Printf(TEXT("Guns++ bullet did not shatter glass (Java shatter total %lld)"), (long long)Host->GetState().ShatterTotal)); return true; }
		return false;
	}, 9);
	Add(TEXT("guns before crate"), [this] { ZmKills0 = CountGuns(Host->GetState()); }, [](float T) { return T > 0.1f; }, 2);
	AddZmLookAt(TEXT("Random Weapon"));
	AddZmInteract(TEXT("crate"), [this](const FString& R)
	{
		Check(R.Contains(TEXT("bought Random Weapon")), TEXT("F at the random weapon crate: ") + R);
	});
	Add(TEXT("box roll"), [] {}, [this](float T)
	{
		// Lid opens, guns cycle while rising out of the box, then the result floats waiting to be taken.
		AimAt(ZmTarget.X, ZmTarget.Y + 0.6, ZmTarget.Z);
		const FCrbZmState& Z = Host->GetState().Zm;
		if (Cross(0.6f)) Shot(TEXT("45a_box_lid_open"));
		if (Cross(1.8f)) { Check(!Z.Crate.IsEmpty() && Z.Box == TEXT("CYCLING"), TEXT("Mystery box cycling Guns++ weapons out of the open box: ") + Z.Crate + TEXT(" (") + Z.Box + TEXT(")")); Shot(TEXT("45b_box_cycling")); }
		if (Z.Box == TEXT("OFFER") && T > 2.f) { Shot(TEXT("45c_box_offer")); Pass(TEXT("Box offers the rolled gun: ") + Z.Prompt); return true; }
		if (T > 7.f) { Fail(TEXT("box never reached OFFER: ") + Z.Box); return true; }
		return false;
	}, 8);
	AddZmInteract(TEXT("take gun"), [this](const FString& R)
	{
		Check(R.StartsWith(TEXT("took ")), TEXT("F at the box takes the floating gun: ") + R);
	});
	Add(TEXT("box gave gun"), [] {}, [this](float T)
	{
		if (T < 0.8f) return false;
		const int32 N = CountGuns(Host->GetState());
		Check(N > ZmKills0, FString::Printf(TEXT("Box gave a Guns++ weapon: %d -> %d guns in inventory"), ZmKills0, N));
		AimAt(ZmTarget.X, ZmTarget.Y + 0.6, ZmTarget.Z);
		Shot(TEXT("45d_box_closing"));
		return true;
	}, 3);
	// Chalk wall-buys: Unreal draws one traced outline per wall gun; no item frames exist.
	AddZmLookAt(TEXT("Revolver"));
	Add(TEXT("chalk wallbuy"), [] {}, [this](float T)
	{
		AimAt(ZmTarget.X, ZmTarget.Y, ZmTarget.Z);
		if (T < 1.0f) return false;
		Check(Host->ChalkQuads >= 5 && Host->GetState().Zm.WallBuys.Num() >= 5, FString::Printf(TEXT("Wall-buys drawn as chalk outlines: %d quads from Java's traced icon sheet (%d wall-buys)"), Host->ChalkQuads, Host->GetState().Zm.WallBuys.Num()));
		Shot(TEXT("44b_chalk_wallbuy"));
		return true;
	}, 3);

	// ---- end of round: kill the rest, the next round starts after the break ----
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetNumberField(TEXT("round"), 2);
		AddCommand(TEXT("zm.debug"), A, [](bool, const FCrbResult*) {});
	}
	Add(TEXT("zm round 2"), [] {}, [this](float T)
	{
		const FCrbZmState& Z = Host->GetState().Zm;
		if (Z.Round == 2 && Z.Phase == TEXT("ROUND"))
		{
			Pass(FString::Printf(TEXT("Round 2 started (%d zombies this round, %d spawned so far)"), Z.Left, Z.Zombies.Num()));
			Host->LookYaw = 90; Host->LookPitch = -5;
			Shot(TEXT("46_zm_round2_banner"));
			return true;
		}
		if (T > 15.f) { Fail(FString::Printf(TEXT("Round 2 did not start: phase %s round %d"), *Z.Phase, Z.Round)); return true; }
		return false;
	}, 16);

	// ---- power-up drop and pickup ----
	Add(TEXT("drop power-up"), [this]
	{
		// Dropped 2.5 blocks ahead along the yaw Unreal is walking with (not the server's last-synced yaw).
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetBoolField(TEXT("powerUp"), true); A->SetNumberField(TEXT("distance"), 2.5); A->SetNumberField(TEXT("yaw"), Host->LookYaw);
		Host->SendCommand(TEXT("zm.debug"), A);
	}, [](float T) { return T > 0.5f; }, 2);
	Add(TEXT("zm power-up"), [this] { Host->LookPitch = 0; }, [this](float T)
	{
		const FCrbZmState& Z = Host->GetState().Zm;
		Host->SetMove(T > 0.3f && T < 1.2f ? 1.f : 0.f, 0); // a couple of steps forward into it
		if (!Z.PowerUp.IsEmpty() && Z.Tick - Z.PowerUpTick < 100) { Host->SetMove(0, 0); Pass(TEXT("Power-up picked up by walking into it: ") + Z.PowerUp); Shot(TEXT("47_zm_powerup")); return true; }
		if (T > 5.f) { Host->SetMove(0, 0); Fail(TEXT("power-up not picked up")); return true; }
		return false;
	}, 6);

	// ---- going down without Second Wind ends the match (death cancelled, inventory restored on exit) ----
	AddCommand(TEXT("player.kill"), nullptr, [](bool, const FCrbResult*) {});
	Add(TEXT("zm game over"), [] {}, [this](float T)
	{
		const FCrbState& S = Host->GetState();
		if (S.Zm.Phase == TEXT("GAME_OVER") || T > 5.f)
		{
			Check(S.Zm.Phase == TEXT("GAME_OVER") && !S.bDead, FString::Printf(TEXT("Going down ends the match (phase %s), vanilla death screen cancelled (dead=%d)"), *S.Zm.Phase, S.bDead));
			Shot(TEXT("48_zm_game_over"));
			return true;
		}
		return false;
	}, 6);
	AddKey(EKeys::Escape, TEXT("Esc"));
	Add(TEXT("pause for zm stop"), [] {}, [](float T) { return (CrbMenus::IsOpen() && T > 0.5f) || T > 5.f; }, 6);
	AddClickRow(TEXT("pause:modes"), TEXT("Pause menu"));
	Add(TEXT("click zm:stop"), [] {}, [this](float T)
	{
		// The game-over screen ends the match on its own after 10 s; click End only while it is still running.
		FVector2D P;
		if (!Host->GetState().Zm.IsActive()) { Pass(TEXT("Match ended by the game-over timer before the click")); Host->CloseMenus(); return true; }
		if (T > 0.15f && CrbMenus::RowCenter(TEXT("zm:stop"), P)) { Check(CrbMenus::ClickRow(TEXT("zm:stop")), TEXT("Game Modes menu: clicked 'End Zombies match'")); return true; }
		if (T > 5.f) { Fail(TEXT("zm:stop row not found")); return true; }
		return false;
	}, 8);
	Add(TEXT("zm stopped"), [] {}, [this](float T)
	{
		const FCrbState& S = Host->GetState();
		if (((S.Zm.IsActive() || S.GameMode != TEXT("survival")) || T < 1.5f) && T < 6.f) return false;
		Check(!S.Zm.IsActive() && S.GameMode == TEXT("survival") && CountGuns(S) == 0,
			FString::Printf(TEXT("Match ended from the menu: phase %s, game mode restored to %s, Guns++ items removed (%d)"), *S.Zm.Phase, *S.GameMode, CountGuns(S)));
		return true;
	}, 8);
}

void FCrbTest::BuildMaps()
{
	// Esc > Maps... lists the default superflat plus the Maps folder (Nuketown); picking one swaps the world through Java
	// (imported once, world generation off), then Zombies runs on the map itself.
	Add(TEXT("pause for maps"), [this] { Host->CloseMenus(); Host->TogglePause(); }, [](float T) { return CrbMenus::IsOpen() && T > 0.5f; }, 4);
	AddClickRow(TEXT("pause:maps"), TEXT("Pause menu"));
	Add(TEXT("maps list"), [this] { World0 = Host->GetState().MapWorld; }, [this](float T)
	{
		FVector2D P;
		if ((!Host->bMapListReceived || !CrbMenus::RowCenter(TEXT("map:nuketown"), P)) && T < 6.f) return false;
		Check(Host->bMapListReceived && Host->MapList.Num() >= 2 && Host->MapList[0].Id == TEXT("superflat") && Host->MapList.ContainsByPredicate([](const ACrbHost::FMapInfo& M) { return M.Id == TEXT("nuketown"); }),
			FString::Printf(TEXT("Maps menu lists %d maps from Java (folder %s): %s"), Host->MapList.Num(), *Host->MapsFolder,
				*FString::JoinBy(Host->MapList, TEXT(", "), [](const ACrbHost::FMapInfo& M) { return M.Name; })));
		Shot(TEXT("60_maps_menu"));
		return true;
	}, 8);
	AddClickRow(TEXT("map:nuketown"), TEXT("Maps menu"));
	Add(TEXT("map switched"), [] {}, [this](float T)
	{
		const FCrbState& S = Host->GetState();
		const bool bNew = S.bValid && !S.MapWorld.IsEmpty() && S.MapWorld != World0 && S.MapLoading.IsEmpty() && S.MapCurrent == TEXT("nuketown") && Host->World.NumMeshed() >= 20;
		if (bNew && T > 4.f)
		{
			Pass(FString::Printf(TEXT("Nuketown (1.12 Forge save) imported and opened through Java: world '%s' (was %s), %d sections meshed, player at %.0f %.0f %.0f"),
				*S.MapWorld, *World0, Host->World.NumMeshed(), S.X, S.Y, S.Z));
			Host->LookYaw = 0; Host->LookPitch = -5;
			return true;
		}
		if (T > 110.f) { Fail(FString::Printf(TEXT("map switch did not finish: world %s (was %s) loading '%s' status '%s' meshed %d"), *S.MapWorld, *World0, *S.MapLoading, *S.MapStatus, Host->World.NumMeshed())); return true; }
		return false;
	}, 112);
	for (int32 K = 0; K < 4; ++K)
	{
		const int32 Yaw = K * 90;
		Add(FString::Printf(TEXT("nuketown view %d"), Yaw), [this, Yaw] { Host->LookYaw = Yaw; Host->LookPitch = -5; }, [this, Yaw](float T) { if (T < 1.5f) return false; Shot(FString::Printf(TEXT("61_nuketown_%03d"), Yaw)); return true; }, 3);
	}
	Add(TEXT("not falling"), [this] { Y0 = Host->GetState().Y; }, [this](float T)
	{
		if (T < 3.f) return false;
		const FCrbState& S = Host->GetState();
		Check(S.Y > -60 && FMath::Abs(S.Y - Y0) < 2.0 && !S.bDead, FString::Printf(TEXT("Player stands on the map (y %.1f -> %.1f): no void fall with world generation off"), Y0, S.Y));
		return true;
	}, 4);
	AddCommand(TEXT("zm.start"), nullptr, [this](bool bOk, const FCrbResult* R) { Check(bOk, TEXT("Zombies on Nuketown (map mode, no arena built): ") + (R ? R->Message : FString(TEXT("timeout")))); });
	Add(TEXT("nuketown zombies"), [] {}, [this](float T)
	{
		const FCrbZmState& Z = Host->GetState().Zm;
		if (Z.Phase == TEXT("ROUND") && Z.Zombies.Num() >= 2 && T > 3.f)
		{
			if (const FCrbZmZombie* N = Nearest(Host->GetState(), false)) AimAt(N->X, N->Y + 1.2, N->Z);
			if (T > 9.f) { Pass(FString::Printf(TEXT("Nuketown round %d: %d zombies spawned on the map's own ground and hunting"), Z.Round, Z.Zombies.Num())); Shot(TEXT("62_nuketown_zombies")); return true; }
			return false;
		}
		if (T > 30.f) { Fail(FString::Printf(TEXT("no zombies on Nuketown: phase %s, %d zombies"), *Z.Phase, Z.Zombies.Num())); return true; }
		return false;
	}, 32);
	AddCommand(TEXT("zm.stop"), nullptr, [](bool, const FCrbResult*) {});
}
