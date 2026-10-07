// Minecraft Physics & Portal Mod tests (-CrbTest=pp, also part of -CrbTest=all). Evidence: Java's PPController export
// (state, velocity, surface, counters, portal traversals with the transformed velocity), Java's portal / entity
// traversal counters, the SK_PPSteve component and its native anim instance, the UE portal views and physics cubes,
// and screenshots. Input goes through the host's own SetMove / PPShootPortal / roll key paths.
#include "CrbTest.h"
#include "CrbHost.h"
#include "CrbMenus.h"
#include "CrbPPDebug.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"

namespace
{
	constexpr double PPFloorY = -60.0;
}

void FCrbTest::AddPPTp(double X, double Y, double Z, float Yaw)
{
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
	A->SetNumberField(TEXT("x"), X); A->SetNumberField(TEXT("y"), Y); A->SetNumberField(TEXT("z"), Z); A->SetNumberField(TEXT("yaw"), Yaw);
	AddCommand(TEXT("test.tp"), A, [this](bool bOk, const FCrbResult* R) { if (!bOk) Fail(TEXT("test.tp refused: ") + (R ? R->Message : FString(TEXT("timeout")))); });
	Add(TEXT("pp settle after tp"), [this, Yaw] { Host->SetMove(0, 0); Host->SetButtons(false, false, false); Host->bPPRollHeld = false; Host->LookYaw = Yaw; Host->LookPitch = -10; Host->PPCommand(TEXT("resetVelocity")); },
		[](float T) { return T > 0.8f; }, 3);
}

void FCrbTest::AddPPVelocity(double X, double Y, double Z)
{
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("cmd"), TEXT("velocity"));
	A->SetNumberField(TEXT("x"), X); A->SetNumberField(TEXT("y"), Y); A->SetNumberField(TEXT("z"), Z);
	AddCommand(TEXT("pp.debug"), A, [](bool, const FCrbResult*) {});
}

void FCrbTest::AddPPPortal(int32 Which, int32 X, int32 Y, int32 Z, const TCHAR* Face)
{
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetNumberField(TEXT("which"), Which);
	A->SetNumberField(TEXT("x"), X); A->SetNumberField(TEXT("y"), Y); A->SetNumberField(TEXT("z"), Z); A->SetStringField(TEXT("face"), Face);
	AddCommand(TEXT("pp.portal"), A, [this, Which](bool bOk, const FCrbResult* R) { Check(bOk, FString::Printf(TEXT("Portal %s placed: %s"), Which == 0 ? TEXT("A") : TEXT("B"), R ? *R->Message : TEXT("timeout"))); });
}

void FCrbTest::BuildPP()
{
	auto P = [this]() -> const FCrbPPState& { return Host->GetState().PP; };
	auto Note = [this]() { if (const FCrbPPAnimDebug* D = Host->PPSteve.AnimDebug()) AnimSeen.Add(D->Clip); AnimSeen.Add(TEXT("state:") + Host->GetState().PP.State); };

	// ---- setup: vanilla, survival, course ----
	Add(TEXT("pp setup"), [this] { Host->bInputOverride = true; Host->SetMove(0, 0); Host->SetButtons(false, false, false); Host->bPPKeysFromTest = true;
		if (Host->bPhysicsPortalEnabled) Host->TogglePhysicsPortal(); if (Host->bSm64Enabled) Host->ToggleSm64(); if (Host->bAvatarEnabled) Host->ToggleAvatar(); if (Host->bCraft64Enabled) Host->ToggleCraft64(); Host->ViewMode = 0; },
		[](float T) { return T > 0.6f; }, 3);
	AddCommand(TEXT("fixture.off"), nullptr, [](bool, const FCrbResult*) {});
	AddCommand(TEXT("player.reset"), nullptr, [](bool, const FCrbResult*) {});
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("mode"), TEXT("survival"));
		AddCommand(TEXT("gamemode.set"), A, [](bool, const FCrbResult*) {});
		TSharedPtr<FJsonObject> F = MakeShared<FJsonObject>(); F->SetStringField(TEXT("id"), TEXT("pp"));
		AddCommand(TEXT("fixture.toggle"), F, [this](bool bOk, const FCrbResult* R) { Check(bOk && R && R->Message.Contains(TEXT("ortal")), TEXT("Physics & portal course built in the superflat test world: ") + (R ? R->Message : FString(TEXT("no reply")))); });
	}

	// ---- the character asset ----
	Add(TEXT("pp assets"), [] {}, [this](float T)
	{
		FCrbPPSteve& S = Host->PPSteve;
		if (!S.AssetsReady()) { Fail(TEXT("SK_PPSteve missing: ") + S.LoadError); return true; }
		FString Want; FParse::Value(FCommandLine::Get(), TEXT("CrbPPCrc="), Want);
		const FString Got = FString::Printf(TEXT("%08X"), S.SkeletonHash());
		Check(Want.IsEmpty() || Want.Equals(Got, ESearchCase::IgnoreCase), FString::Printf(TEXT("SK_PPSteve (the user's sculpted 3D Steve) on the Blender-built skeleton: %d bones incl. ik_foot / ik_hand, CRC %s (build %s)"), S.NumBones(), *Got, *Want));
		Check(S.ClipsLoaded == (int32)ECrbPPClip::Count, FString::Printf(TEXT("Animation set: %d / %d clips (missing: %s)"), S.ClipsLoaded, (int32)ECrbPPClip::Count, *FString::Join(S.MissingClips, TEXT(","))));
		Check(S.HasPhysicsAsset(), TEXT("Physics asset generated for the ragdoll"));
		Check(Host->PortalViews.bMaterialReady, TEXT("Portal material M_Portal cooked"));
		return true;
	}, 3);

	// ---- ON from the Mods menu ----
	AddPPTp(0.5, PPFloorY, 4.5, 0);
	AddKey(EKeys::M, TEXT("M"));
	Add(TEXT("mods for pp"), [] {}, [](float T) { return (CrbMenus::IsOpen() && T > 0.5f) || T > 5.f; }, 6);
	AddClickRow(TEXT("mod:physicsportal"), TEXT("Mods menu"));
	AddClickRow(TEXT("resume"), TEXT("Mods menu"));
	Add(TEXT("pp on"), [] {}, [this, P](float T)
	{
		if (!(P().bActive && T > 1.f) && T < 6.f) return false;
		Check(Host->bPhysicsPortalEnabled && P().bActive, TEXT("mod:physicsportal ON: Java PPController drives the player (") + P().State + TEXT(")"));
		Check(Host->PPSteve.GetMesh() && Host->PPSteve.GetMesh()->IsVisible(), TEXT("3D Steve skeletal character drawn; vanilla player model hidden"));
		Check(P().Surfaces >= 10, FString::Printf(TEXT("Data-driven surface table loaded (%d entries)"), P().Surfaces));
		Shot(TEXT("60_pp_on"));
		return true;
	}, 8);

	// ---- walking / running (animation follows physics) ----
	Add(TEXT("pp walk"), [this] { AnimSeen.Reset(); Host->SetMove(1, 0); }, [this, P, Note](float T)
	{
		Note();
		if (T < 1.6f) return false;
		Host->SetMove(0, 0);
		Check(P().Speed > 2.f && (AnimSeen.Contains(TEXT("Walk")) || AnimSeen.Contains(TEXT("Run"))), FString::Printf(TEXT("W walks with momentum: %.1f m/s, %s"), P().Speed, *SmSeen()));
		Shot(TEXT("61_pp_walk"));
		return true;
	}, 4);

	// ---- momentum on ice vs stone (surface physics) ----
	AddPPTp(0.5, PPFloorY, 15.5, 0);
	AddPPVelocity(0, 0, 8);
	Add(TEXT("pp ice"), [] {}, [this, P](float T)
	{
		if (T < 0.8f) return false;
		PPIce = P().Speed; PPSurf = P().SurfaceBlock;
		return true;
	}, 3);
	AddPPTp(0.5, PPFloorY, 5.5, 0);
	AddPPVelocity(0, 0, 8);
	Add(TEXT("pp stone"), [] {}, [this, P](float T)
	{
		if (T < 0.8f) return false;
		Check(PPIce > 4.f && P().Speed < PPIce * 0.4f && PPSurf.Contains(TEXT("ice")),
			FString::Printf(TEXT("Surface physics: 8 m/s coasts to %.1f m/s on %s but %.1f m/s on %s after 0.8 s"), PPIce, *PPSurf, P().Speed, *P().SurfaceBlock));
		return true;
	}, 3);

	// ---- rolling ----
	AddPPTp(0.5, PPFloorY, 4.5, 0);
	Add(TEXT("pp roll"), [this] { AnimSeen.Reset(); PPRoll0 = Host->PPSteve.RollAngle; Host->bPPRollHeld = true; Host->SetMove(1, 0); PPFlag = false; }, [this, P, Note](float T)
	{
		Note();
		if (!PPFlag && P().State == TEXT("ROLL") && T > 0.8f) { Shot(TEXT("62_pp_roll")); PPFlag = true; }
		if (T < 1.8f) return false;
		Host->bPPRollHeld = false; Host->SetMove(0, 0);
		Check(AnimSeen.Contains(TEXT("state:ROLL")) && AnimSeen.Contains(TEXT("Roll")) && FMath::Abs(Host->PPSteve.RollAngle - PPRoll0) > 1.f,
			FString::Printf(TEXT("Shift = roll: ROLL state, Roll clip, Steve spins as a ball (angle %.0f deg), %.1f m/s"), Host->PPSteve.RollAngle, P().Speed));
		return true;
	}, 4);

	// ---- slope + ramp launch (the west ramp: platform, half-block slope, kicker lip) ----
	AddPPTp(-5.5, PPFloorY + 4, 3.5, 0);
	Add(TEXT("pp ramp"), [this] { AnimSeen.Reset(); Host->bPPRollHeld = true; Host->SetMove(1, 0); PPMax = 0; PPLaunch0 = Host->GetState().PP.Count(TEXT("LAUNCH")) + Host->GetState().PP.Count(TEXT("JUMP")); PPFlag = false; }, [this, P, Note](float T)
	{
		Note();
		PPMax = FMath::Max(PPMax, P().Speed);
		if (!PPFlag && !P().bGrounded && Host->GetState().Z > 18.5) { Shot(TEXT("63_pp_ramp_launch")); PPFlag = true; }
		if (T < 4.f) return false;
		Host->bPPRollHeld = false; Host->SetMove(0, 0);
		Check(PPMax > 7.f && (AnimSeen.Contains(TEXT("state:FALL")) || AnimSeen.Contains(TEXT("state:LAUNCH"))),
			FString::Printf(TEXT("Rolling down the slope builds momentum (max %.1f m/s) and the kicker throws Steve into the air (%s)"), PPMax, *SmSeen()));
		return true;
	}, 6);

	// ---- slime bounce ----
	AddPPTp(0.5, PPFloorY + 6, 24.5, 0);
	Add(TEXT("pp slime"), [this, P] { PPBounce0 = P().BounceSeq; }, [this, P](float T)
	{
		if (!(P().BounceSeq > PPBounce0) && T < 3.f) return false;
		Check(P().BounceSeq > PPBounce0, FString::Printf(TEXT("Slime block bounces Steve back up (%s)"), *P().Event));
		Shot(TEXT("64_pp_slime"));
		return true;
	}, 5);

	// ---- soul sand vs sand ----
	AddPPTp(0.5, PPFloorY, 27.5, 0);
	Add(TEXT("pp sand"), [this] { Host->SetMove(1, 0); PPMax = 0; PPSurf.Empty(); PPSurf2.Empty(); }, [this, P](float T) { if (T > 0.6f) PPMax = FMath::Max(PPMax, P().Speed); if (T > 0.7f && PPSurf.IsEmpty()) PPSurf = P().SurfaceBlock; if (T < 1.3f) return false; Host->SetMove(0, 0); PPIce = PPMax; return true; }, 3);
	AddPPTp(0.5, PPFloorY, 33.5, 0);
	Add(TEXT("pp soulsand"), [this] { Host->SetMove(1, 0); PPMax = 0; }, [this, P](float T)
	{
		if (T > 0.6f) PPMax = FMath::Max(PPMax, P().Speed);
		if (T > 0.7f && PPSurf2.IsEmpty()) PPSurf2 = P().SurfaceBlock;
		if (T < 1.3f) return false;
		Host->SetMove(0, 0);
		Check(PPSurf.Contains(TEXT("sand")) && PPSurf2.Contains(TEXT("soul_sand")) && PPMax < PPIce * 0.8f,
			FString::Printf(TEXT("Sand slows (%.1f m/s on %s), soul sand is heavier (%.1f m/s on %s)"), PPIce, *PPSurf, PPMax, *PPSurf2));
		return true;
	}, 3);

	// ---- Gish modifiers ----
	for (const TCHAR* M : { TEXT("HEAVY"), TEXT("SLIPPERY"), TEXT("NORMAL") })
	{
		TSharedPtr<FJsonObject> V = MakeShared<FJsonObject>(); V->SetStringField(TEXT("modifier"), M);
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("cmd"), TEXT("tune")); A->SetObjectField(TEXT("values"), V);
		AddCommand(TEXT("pp.debug"), A, [](bool, const FCrbResult*) {});
		Add(FString(TEXT("pp modifier ")) + M, [] {}, [this, P, M](float T)
		{
			if (P().Modifier != M && T < 2.f) return false;
			Check(P().Modifier == M, FString::Printf(TEXT("Gish modifier %s applied (friction now %.2f)"), M, P().Friction));
			return true;
		}, 3);
	}

	// ---- portals: A on the x = 5 wall, B on the z = 46 wall ----
	AddPPPortal(0, 5, (int32)PPFloorY, 10, TEXT("west"));
	AddPPPortal(1, 0, (int32)PPFloorY, 46, TEXT("north"));
	AddPPTp(1.5, PPFloorY, 10.5, -90);
	Add(TEXT("pp portal view"), [this] { PPCap0 = Host->PortalViews.Captures; }, [this, P](float T)
	{
		if (T < 1.2f) return false;
		Check(P().bLinked && Host->PortalViews.Visible > 0 && Host->PortalViews.Captures > PPCap0, FString::Printf(TEXT("Portals linked and rendered: %d visible, %d live view captures"), Host->PortalViews.Visible, Host->PortalViews.Captures - PPCap0));
		Shot(TEXT("65_pp_portals"));
		return true;
	}, 4);
	AddPPVelocity(12, 2, 0);
	Add(TEXT("pp portal momentum"), [this, P] { PPPortal0 = P().PortalSeq; PPFlag = false; }, [this, P](float T)
	{
		if (!(P().PortalSeq > PPPortal0 && T > 0.25f) && T < 3.f) return false;
		const FCrbState& S = Host->GetState();
		Check(P().PortalSeq > PPPortal0 && S.Z > 42.0 && P().VZ < -4.f && FMath::Abs(P().VX) < 3.f,
			FString::Printf(TEXT("Steve goes into portal A moving +X at 12 m/s and leaves portal B moving -Z (v %.1f / %.1f / %.1f, at z %.1f) - %s"), P().VX, P().VY, P().VZ, S.Z, *P().LastPortal));
		Shot(TEXT("66_pp_portal_exit"));
		return true;
	}, 5);

	// ---- orientation: floor portal -> wall portal ----
	AddPPPortal(0, 0, (int32)PPFloorY - 1, 20, TEXT("up"));
	AddPPTp(0.5, PPFloorY + 7, 20.5, 0);
	Add(TEXT("pp portal orientation"), [this, P] { PPPortal0 = P().PortalSeq; }, [this, P](float T)
	{
		if (!(P().PortalSeq > PPPortal0 && T > 0.25f) && T < 4.f) return false;
		Check(P().PortalSeq > PPPortal0 && P().VZ < -3.f, FString::Printf(TEXT("Falling into a floor portal comes out of a wall portal moving horizontally (v %.1f / %.1f / %.1f)"), P().VX, P().VY, P().VZ));
		return true;
	}, 6);

	// ---- Minecraft physics object through a portal (server entity traversal) ----
	AddPPPortal(0, 5, (int32)PPFloorY, 10, TEXT("west"));
	AddPPTp(2.5, PPFloorY, 10.5, -90);
	Add(TEXT("pp object"), [this, P] { PPEnt0 = P().EntityTeleports; Host->LookPitch = -2; TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetNumberField(TEXT("speed"), 10); Host->SendCommand(TEXT("pp.object"), A); },
		[this, P](float T)
		{
			if (!(P().EntityTeleports > PPEnt0) && T < 4.f) return false;
			Check(P().EntityTeleports > PPEnt0, FString::Printf(TEXT("Minecraft physics object (item entity) thrown into portal A comes out of B with its momentum (%lld entity traversals)"), P().EntityTeleports));
			return true;
		}, 6);

	// ---- UE4 physics cube through a portal ----
	Add(TEXT("pp cube"), [this] { PPCube0 = Host->PPCubes.Teleports; Host->LookYaw = -90; Host->LookPitch = -4; }, [this](float T)
	{
		if (T > 0.4f && Host->PPCubes.Num() == 0) Host->PPSpawnCube();
		if (!(Host->PPCubes.Teleports > PPCube0) && T < 4.f) return false;
		Check(Host->PPCubes.Teleports > PPCube0, FString::Printf(TEXT("UE4 physics cube collides with Minecraft blocks and goes through the portal with its velocity (%d teleports)"), Host->PPCubes.Teleports - PPCube0));
		return true;
	}, 6);

	// ---- ragdoll ----
	AddPPTp(0.5, PPFloorY, 6.5, 0);
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("cmd"), TEXT("ragdoll"));
		AddCommand(TEXT("pp.debug"), A, [](bool, const FCrbResult*) {});
	}
	Add(TEXT("pp ragdoll"), [this] { PPFlag = false; }, [this, P](float T)
	{
		if (!PPFlag && Host->PPSteve.IsRagdolling() && T > 0.5f) { Shot(TEXT("67_pp_ragdoll")); PPFlag = true; }
		if (T < 1.0f) return false;
		Check(P().bRagdoll && Host->PPSteve.IsRagdolling() && Host->PPSteve.Colliders.Active() > 0,
			FString::Printf(TEXT("Ragdoll: physics asset bodies simulate against %d Minecraft block colliders"), Host->PPSteve.Colliders.Active()));
		return true;
	}, 3);
	Add(TEXT("pp ragdoll end"), [] {}, [this, P](float T)
	{
		if (!( !P().bRagdoll && !Host->PPSteve.IsRagdolling()) && T < 5.f) return false;
		Check(!P().bRagdoll && !Host->PPSteve.IsRagdolling(), TEXT("Ragdoll ends and Steve blends back to the animated character (") + P().State + TEXT(")"));
		return true;
	}, 6);

	// ---- debug menu ----
	Add(TEXT("pp debug open"), [this] { if (Host->PPDebug && !Host->PPDebug->IsPanelOpen()) PressKey(EKeys::F7); }, [this](float T)
	{
		if (T < 1.2f) return false;
		UCrbPPDebugComponent* D = Host->PPDebug;
		Check(D && D->IsPanelOpen() && D->IsMonitorShown() && D->Config.IsValid(), TEXT("F7 opens the Physics & Portal debug menu, live monitor and tunables"));
		if (D) { D->SetTab(1); }
		Shot(TEXT("68_pp_debug"));
		return true;
	}, 4);
	Add(TEXT("pp debug tune"), [this] { if (Host->PPDebug) { PPTune0 = Host->PPDebug->ConfigValue(TEXT("friction")); Host->PPDebug->TuneStep(TEXT("friction"), +1); } }, [this](float T)
	{
		if (T < 0.8f) return false;
		UCrbPPDebugComponent* D = Host->PPDebug;
		Check(D && D->ConfigValue(TEXT("friction")) > PPTune0, FString::Printf(TEXT("Live tuning: friction %.2f -> %.2f"), PPTune0, D ? D->ConfigValue(TEXT("friction")) : -1.0));
		if (D) D->Cmd(TEXT("tuneReset"));
		return true;
	}, 3);
	Add(TEXT("pp debug close"), [this] { if (Host->PPDebug && Host->PPDebug->IsPanelOpen()) PressKey(EKeys::F7); }, [this](float T)
	{
		if (T < 0.8f) return false;
		Check(Host->PPDebug && !Host->PPDebug->IsPanelOpen(), TEXT("F7 closes the debug menu"));
		if (Host->PPDebug) Host->PPDebug->SetMonitor(false);
		return true;
	}, 3);

	// ---- OFF ----
	AddKey(EKeys::M, TEXT("M"));
	Add(TEXT("mods for pp off"), [] {}, [](float T) { return (CrbMenus::IsOpen() && T > 0.5f) || T > 5.f; }, 6);
	AddClickRow(TEXT("mod:physicsportal"), TEXT("Mods menu"));
	AddClickRow(TEXT("resume"), TEXT("Mods menu"));
	Add(TEXT("pp off"), [this] { Host->bPPKeysFromTest = false; }, [this, P](float T)
	{
		if (!( !P().bActive && !P().Portals[0].bValid && T > 1.f) && T < 6.f) return false;
		Check(!Host->bPhysicsPortalEnabled && !P().bActive && !P().Portals[0].bValid && !P().Portals[1].bValid && Host->PPCubes.Num() == 0
			&& !(Host->PPSteve.GetMesh() && Host->PPSteve.GetMesh()->IsVisible()) && !Host->PortalViews.Planes[0]->IsVisible(),
			TEXT("mod OFF: vanilla movement back, portals and physics objects removed, 3D Steve hidden, no restart"));
		Shot(TEXT("69_pp_off"));
		return true;
	}, 8);
	AddCommand(TEXT("fixture.off"), nullptr, [](bool, const FCrbResult*) {});
	AddCommand(TEXT("player.reset"), nullptr, [](bool, const FCrbResult*) {});
}
