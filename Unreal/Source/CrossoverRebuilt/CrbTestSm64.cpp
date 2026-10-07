// SM64 Steve Movement tests (-CrbTest=sm64, also part of -CrbTest=all). Every check reads evidence from the real
// paths: Java's Sm64Controller export (action, speeds, counters, jump heights), Java positions/health from op results,
// the SK_Steve component and its anim instance debug record, and screenshots. Input goes through the host's own
// SetMove / SetButtons (the same path as the keyboard), so the bridge latency is part of what is tested.
// Only accommodation: the jump-chain and wall-kick windows are widened from 6/5 to 15 frames while the scripted input
// reacts to Java state over the bridge (a human presses on sight); both are restored at the end.
#include "CrbTest.h"
#include "CrbHost.h"
#include "CrbMenus.h"
#include "CrbPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Misc/CommandLine.h"
#include "Dom/JsonObject.h"
#include "InputCoreTypes.h"
#include "ProceduralMeshComponent.h"

namespace
{
	constexpr double FeetY = -60.0; // superflat floor top (Fixtures.FLOOR_Y + 1)
}

void FCrbTest::AddSm64Tp(double X, double Y, double Z, float Yaw)
{
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
	A->SetNumberField(TEXT("x"), X); A->SetNumberField(TEXT("y"), Y); A->SetNumberField(TEXT("z"), Z); A->SetNumberField(TEXT("yaw"), Yaw);
	AddCommand(TEXT("test.tp"), A, [this](bool bOk, const FCrbResult* R) { if (!bOk) Fail(TEXT("test.tp refused: ") + (R ? R->Message : FString(TEXT("timeout")))); });
	Add(TEXT("sm64 settle after tp"), [this, Yaw] { Host->SetMove(0, 0); Host->SetButtons(false, false, false); Host->LookYaw = Yaw; Host->LookPitch = -12; }, [this](float T)
	{
		const FCrbSm64State& M = Host->GetState().Sm64;
		return (T > 0.6f && M.bGrounded && (M.Action == TEXT("IDLE") || M.Action == TEXT("LAND"))) || T > 4.f;
	}, 5);
}

FString FCrbTest::SmSeen() const { TArray<FString> A = AnimSeen.Array(); A.Sort(); return FString::Join(A, TEXT(",")); }

void FCrbTest::NoteSm64()
{
	if (const FCrbSteveAnimDebug* D = Host->Steve.AnimDebug()) AnimSeen.Add(D->Clip);
	AnimSeen.Add(TEXT("action:") + Host->GetState().Sm64.Action);
}

void FCrbTest::BuildSm64()
{
	auto M = [this]() -> const FCrbSm64State& { return Host->GetState().Sm64; };
	auto Window = [this](int32 Chain, int32 Kick)
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetNumberField(TEXT("chainWindowFrames"), Chain); A->SetNumberField(TEXT("wallKickWindowFrames"), Kick);
		AddCommand(TEXT("sm64.config"), A, [](bool, const FCrbResult*) {});
	};

	// ---- setup: vanilla, survival, the course ----
	Add(TEXT("sm64 setup"), [this] { Host->bInputOverride = true; Host->SetMove(0, 0); Host->SetButtons(false, false, false); if (Host->bSm64Enabled) Host->ToggleSm64(); if (Host->bAvatarEnabled) Host->ToggleAvatar(); Host->ViewMode = 0; }, [](float T) { return T > 0.5f; }, 3);
	AddCommand(TEXT("fixture.off"), nullptr, [](bool, const FCrbResult*) {});
	AddCommand(TEXT("player.reset"), nullptr, [](bool, const FCrbResult*) {});
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("mode"), TEXT("survival"));
		AddCommand(TEXT("gamemode.set"), A, [](bool, const FCrbResult*) {});
		TSharedPtr<FJsonObject> F = MakeShared<FJsonObject>(); F->SetStringField(TEXT("id"), TEXT("sm64"));
		AddCommand(TEXT("fixture.toggle"), F, [this](bool bOk, const FCrbResult* R)
		{
			Check(bOk && R && R->Message.Contains(TEXT("SM64")), TEXT("SM64 practice course built in the superflat test world: ") + (R ? R->Message : FString(TEXT("no reply"))));
		});
	}

	// ---- the cooked rig ----
	Add(TEXT("sm64 assets"), [] {}, [this](float T)
	{
		FCrbSteve& S = Host->Steve;
		if (!S.AssetsReady()) { Fail(TEXT("SM64 Steve assets missing: ") + S.LoadError); return true; }
		FString Expect; FParse::Value(FCommandLine::Get(), TEXT("-CrbSteveCrc="), Expect);
		const FString Got = FString::Printf(TEXT("%08X"), S.SkeletonHash());
		Check(!Expect.IsEmpty() && Expect.Equals(Got, ESearchCase::IgnoreCase),
			FString::Printf(TEXT("SK_Steve skeleton is the Blender-built Steve rig: %d bones, CRC %s (build %s)"), S.NumBones(), *Got, Expect.IsEmpty() ? TEXT("not given") : *Expect));
		Check(S.ClipsLoaded == (int32)ECrbSteveClip::Count, FString::Printf(TEXT("Original Steve clips on the Steve skeleton: %d/%d (missing: %s)"), S.ClipsLoaded, (int32)ECrbSteveClip::Count, *FString::Join(S.MissingClips, TEXT(","))));
		const FString Mats = FString::Join(S.MaterialReport, TEXT(" "));
		Check(Mats.Contains(TEXT("M_Steve_Base")) && Mats.Contains(TEXT("M_Steve_Overlay")), TEXT("Steve material slots: ") + Mats);
		Metrics->SetStringField(TEXT("steveSkeletonCrc"), Got);
		return true;
	}, 3);
	Add(TEXT("sm64 vanilla baseline"), [] {}, [this, M](float T)
	{
		if (T < 0.6f) return false;
		Check(!M().bRequested && !M().bActive, FString::Printf(TEXT("Mod off: Java movement is vanilla (sm64 requested=%d active=%d)"), M().bRequested, M().bActive));
		return true;
	}, 3);

	// ---- toggle on through the Mods menu ----
	AddKey(EKeys::M, TEXT("M"));
	Add(TEXT("mods for sm64"), [] {}, [](float T) { return (CrbMenus::IsOpen() && T > 0.5f) || T > 5.f; }, 6);
	AddClickRow(TEXT("mod:sm64"), TEXT("Mods menu"));
	AddClickRow(TEXT("resume"), TEXT("Mods menu"));
	Add(TEXT("sm64 on"), [this] { Host->LookYaw = 0; Host->LookPitch = -12; }, [this, M](float T)
	{
		if (!(M().bActive && T > 1.2f) && T < 4.f) return false;
		USkeletalMeshComponent* C = Host->Steve.GetMesh();
		Check(Host->bSm64Enabled && M().bRequested && M().bActive, FString::Printf(TEXT("mod:sm64 ON: Java Sm64Controller drives the player (requested=%d active=%d action=%s)"), M().bRequested, M().bActive, *M().Action));
		Check(C && C->IsVisible() && !Host->Avatar.GetBody()->IsVisible() && !Host->Avatar.GetHands()->IsVisible(), TEXT("Steve skeletal mesh shown; the vanilla pose model hidden"));
		Check(M().bServerNoFall, TEXT("Server marks the player as SM64-driven (no vanilla fall damage)"));
		if (C) { const float H = C->Bounds.BoxExtent.Z * 2.f, Want = Host->Sm64ModelHeight; Check(H > Want * 0.85f && H < Want * 1.2f, FString::Printf(TEXT("Blocky Mario-proportioned Steve drawn %.0f UU tall (setting %.0f)"), H, Want)); }
		const float CamDist = FVector::Dist(Host->Pawn->Camera->GetComponentLocation(), Host->AvatarCameraPivot);
		Check(CamDist > 150.f, FString::Printf(TEXT("Third-person orbit camera %.0f UU from Steve"), CamDist));
		Shot(TEXT("60_sm64_steve_back"));
		return true;
	}, 6);
	Add(TEXT("sm64 front"), [this] { Host->LookYaw = 180; Host->LookPitch = -8; }, [this, M](float T)
	{
		if (T < 1.5f) return false;
		const FCrbSteveAnimDebug* D = Host->Steve.AnimDebug();
		Check(D && D->Clip == TEXT("Idle") && D->Evaluations > 0 && M().Action == TEXT("IDLE"), FString::Printf(TEXT("Standing still: Java IDLE, Steve plays Idle (%s, %d evaluations)"), D ? *D->Clip : TEXT("?"), D ? D->Evaluations : 0));
		if (Host->bSm64SteveSkin)
			Check(Host->Steve.DefaultSkin && Host->Steve.AppliedSkin == Host->Steve.DefaultSkin && !Host->Steve.bAppliedSlim, TEXT("Steve wears Minecraft's own steve.png 1:1 (classic arms), whatever the account skin is (account: ") + M().Skin + TEXT(")"));
		else
			Check(Host->Steve.AppliedSkin != nullptr && Host->Steve.bAppliedSlim == M().bSlim, FString::Printf(TEXT("Live Minecraft skin on Steve (%s, %s arms)"), *M().Skin, M().bSlim ? TEXT("slim") : TEXT("classic")));
		Shot(TEXT("61_sm64_steve_front"));
		return true;
	}, 4);

	// ---- run: SM64 acceleration to 32 units/frame, facing follows the stick ----
	AddSm64Tp(0.5, FeetY, 0.5, 0);
	Add(TEXT("sm64 run"), [this] { X0 = Host->GetState().X; Z0 = Host->GetState().Z; Host->LookYaw = 0; AnimSeen.Reset(); Walk0 = 0; }, [this, M](float T)
	{
		Host->SetMove(1, 0);
		NoteSm64(); Walk0 = FMath::Max(Walk0, M().FwdVel);
		if (Cross(1.6f)) Shot(TEXT("62_sm64_run"));
		if (T < 2.2f) return false;
		const FCrbState& S = Host->GetState();
		Check(Walk0 >= 28.f, FString::Printf(TEXT("Run reaches SM64 full speed: fwdVel %.1f units/frame (target 32)"), Walk0));
		Check(S.Z - Z0 > 8.0 && FMath::Abs(S.X - X0) < 1.0, FString::Printf(TEXT("Java moved Steve +Z %.1f blocks in 2.2 s (x drift %.2f)"), S.Z - Z0, S.X - X0));
		Check(FMath::Abs(FRotator::NormalizeAxis(M().FaceYaw)) < 10.f && FMath::Abs(FRotator::NormalizeAxis(S.Yaw)) < 10.f, FString::Printf(TEXT("Steve faces the stick direction (faceYaw %.1f, Java yaw %.1f)"), M().FaceYaw, S.Yaw));
		Check(AnimSeen.Contains(TEXT("Run")) && AnimSeen.Contains(TEXT("action:WALK")), TEXT("Walk -> Run animation blend while accelerating: ") + SmSeen());
		return true;
	}, 5);
	Add(TEXT("sm64 brake"), [this] { Host->SetMove(0, 0); AnimSeen.Reset(); }, [this, M](float T)
	{
		NoteSm64();
		if (T < 1.5f) return false;
		Check(AnimSeen.Contains(TEXT("action:BRAKE")) && M().Action == TEXT("IDLE") && M().FwdVel == 0.f, TEXT("Releasing the stick at speed brakes, then idles: ") + SmSeen());
		return true;
	}, 4);

	// ---- skid turn-around and side flip ----
	Window(15, 15);
	AddSm64Tp(0.5, FeetY, 0.5, 0);
	Add(TEXT("sm64 side flip"), [this] { AnimSeen.Reset(); SmStage = 0; SmT = 0; }, [this, M](float T)
	{
		NoteSm64();
		if (SmStage == 0) { Host->SetMove(1, 0); if (T > 1.4f) { Host->SetMove(-1, 0); SmStage = 1; } }
		else if (SmStage == 1 && M().Action == TEXT("SKID")) { Host->SetButtons(true, false, false); SmStage = 2; SmT = T; Shot(TEXT("63_sm64_skid")); }
		else if (SmStage == 2 && T - SmT > 0.3f) { Host->SetButtons(false, false, false); SmStage = 3; }
		else if (SmStage == 3 && M().Action == TEXT("SIDE_FLIP") && M().VelY < 30.f && !SmFlag) { Shot(TEXT("64_sm64_side_flip")); SmFlag = true; }
		if (Cross(2.0f) && SmStage < 2) Fail(TEXT("no SKID while reversing the stick at speed: ") + SmSeen() + Diag());
		if (SmStage < 3 || !(M().bGrounded && AnimSeen.Contains(TEXT("action:SIDE_FLIP"))) ) { if (T > 5.f) { Fail(TEXT("side flip not completed: ") + SmSeen()); return true; } return false; }
		Host->SetMove(0, 0);
		const float Face = FRotator::NormalizeAxis(M().FaceYaw);
		Check(AnimSeen.Contains(TEXT("action:SKID")) && AnimSeen.Contains(TEXT("Skid")), TEXT("Reversing the stick at run speed skids (Skid clip): ") + SmSeen());
		Check(AnimSeen.Contains(TEXT("action:SIDE_FLIP")) && AnimSeen.Contains(TEXT("Sideflip")) && FMath::Abs(Face) > 150.f, FString::Printf(TEXT("Jump during the skid = side flip, landing turned around (faceYaw %.0f)"), Face));
		SmFlag = false;
		return true;
	}, 7);

	// ---- single -> double -> triple jump ----
	AddSm64Tp(0.5, FeetY, -1.5, 0);
	AddCommand(TEXT("sm64.resetStats"), nullptr, [](bool, const FCrbResult*) {});
	Add(TEXT("sm64 triple jump"), [this] { AnimSeen.Reset(); SmStage = 0; SmT = 0; SmLand0 = 0; SmMax = SmMax2 = SmMax3 = 0; }, [this, M](float T)
	{
		NoteSm64();
		Host->SetMove(1, 0);
		const FString A = M().Action;
		if (A == TEXT("JUMP")) SmMax = FMath::Max(SmMax, M().LastJumpHeight);
		if (A == TEXT("DOUBLE_JUMP")) SmMax2 = FMath::Max(SmMax2, M().LastJumpHeight);
		if (A == TEXT("TRIPLE_JUMP")) { SmMax3 = FMath::Max(SmMax3, M().LastJumpHeight); if (!SmFlag && M().VelY < 20.f) { Shot(TEXT("65_sm64_triple_jump")); SmFlag = true; } }
		// Hold A 0.35 s per jump (full height), release, press again as soon as Java reports the landing.
		if (SmStage == 0 && T > 1.3f) { Host->SetButtons(true, false, false); SmT = T; SmLand0 = M().LandSerial; SmStage = 1; }
		else if (SmStage % 2 == 1 && T - SmT > 0.35f) { Host->SetButtons(false, false, false); SmStage++; }
		else if (SmStage % 2 == 0 && SmStage > 0 && SmStage < 6 && M().LandSerial != SmLand0 && M().bGrounded) { Host->SetButtons(true, false, false); SmT = T; SmLand0 = M().LandSerial; SmStage++; }
		const bool bDone = SmStage >= 6 && M().bGrounded && AnimSeen.Contains(TEXT("action:TRIPLE_JUMP"));
		if (!bDone && T < 9.f) return false;
		Host->SetMove(0, 0); Host->SetButtons(false, false, false);
		Check(M().Count(TEXT("JUMP")) >= 1 && SmMax > 1.8f && SmMax < 4.5f, FString::Printf(TEXT("Single jump: %.2f blocks high (SM64 42 + speed/4 units/frame)"), SmMax));
		Check(M().Count(TEXT("DOUBLE_JUMP")) >= 1 && SmMax2 > SmMax, FString::Printf(TEXT("Jump on landing = double jump, higher: %.2f blocks"), SmMax2));
		Check(M().Count(TEXT("TRIPLE_JUMP")) >= 1 && SmMax3 > 5.f, FString::Printf(TEXT("Third jump at speed = triple jump somersault: %.2f blocks high"), SmMax3));
		Check(AnimSeen.Contains(TEXT("Jump")) && AnimSeen.Contains(TEXT("DoubleJump")) && AnimSeen.Contains(TEXT("TripleJump")), TEXT("Jump, DoubleJump and TripleJump clips played: ") + SmSeen());
		Metrics->SetNumberField(TEXT("sm64JumpHeights"), SmMax3);
		SmFlag = false;
		return true;
	}, 11);

	// ---- long jump: run, crouch (slide), jump ----
	AddSm64Tp(0.5, FeetY, -1.5, 0);
	Add(TEXT("sm64 long jump"), [this] { AnimSeen.Reset(); SmStage = 0; SmT = 0; SmZ0 = 0; }, [this, M](float T)
	{
		NoteSm64();
		if (SmStage == 0) { Host->SetMove(1, 0); if (T > 1.3f) { Host->SetButtons(false, true, false); SmStage = 1; SmT = T; } }
		else if (SmStage == 1 && (M().Action == TEXT("CROUCH_SLIDE") || T - SmT > 0.25f)) { Host->SetButtons(true, true, false); SmStage = 2; SmT = T; }
		else if (SmStage == 2 && M().Action == TEXT("LONG_JUMP")) { SmZ0 = Host->GetState().Z; SmStage = 3; }
		else if (SmStage == 2 && T - SmT > 1.f) { Fail(TEXT("no LONG_JUMP from the crouch slide: ") + SmSeen() + Diag()); Host->SetButtons(false, false, false); Host->SetMove(0, 0); return true; }
		else if (SmStage == 3) { Host->SetButtons(false, false, false); if (T - SmT > 0.4f && !SmFlag) { Shot(TEXT("66_sm64_long_jump")); SmFlag = true; } if (M().bGrounded) SmStage = 4; }
		if (SmStage < 4) { if (T > 6.f) { Fail(TEXT("long jump timed out: ") + SmSeen()); return true; } return false; }
		Host->SetMove(0, 0);
		const double D = Host->GetState().Z - SmZ0;
		Check(AnimSeen.Contains(TEXT("action:CROUCH_SLIDE")) && AnimSeen.Contains(TEXT("CrouchSlide")), TEXT("Crouch at run speed = crouch slide: ") + SmSeen());
		Check(AnimSeen.Contains(TEXT("action:LONG_JUMP")) && AnimSeen.Contains(TEXT("LongJump")) && D > 5.0, FString::Printf(TEXT("Jump out of the slide = long jump: %.1f blocks (fwdVel x1.5, 30 units/frame up)"), D));
		SmFlag = false;
		return true;
	}, 8);

	// ---- backflip: crouch + jump ----
	AddSm64Tp(0.5, FeetY, 10.5, 0);
	Add(TEXT("sm64 backflip"), [this] { AnimSeen.Reset(); SmStage = 0; SmT = 0; SmZ0 = Host->GetState().Z; SmMax = 0; }, [this, M](float T)
	{
		NoteSm64();
		Host->SetMove(0, 0);
		if (SmStage == 0) { Host->SetButtons(false, true, false); if (M().Action == TEXT("CROUCH") && T > 0.4f) { Host->SetButtons(true, true, false); SmStage = 1; SmT = T; } }
		else if (SmStage == 1 && T - SmT > 0.3f) { Host->SetButtons(false, false, false); SmStage = 2; }
		if (M().Action == TEXT("BACKFLIP")) { SmMax = FMath::Max(SmMax, M().LastJumpHeight); if (!SmFlag && M().VelY < 25.f) { Shot(TEXT("67_sm64_backflip")); SmFlag = true; } }
		if (!(SmStage == 2 && M().bGrounded && AnimSeen.Contains(TEXT("action:BACKFLIP")) && T - SmT > 0.5f)) { if (T > 5.f) { Fail(TEXT("backflip not completed: ") + SmSeen() + Diag()); Host->SetButtons(false, false, false); return true; } return false; }
		const double D = Host->GetState().Z - SmZ0;
		Check(AnimSeen.Contains(TEXT("Crouch")) && AnimSeen.Contains(TEXT("Backflip")) && SmMax > 4.f && D < -1.0,
			FString::Printf(TEXT("Crouch + jump = backflip: %.2f blocks high, %.1f blocks backwards (Crouch, Backflip clips)"), SmMax, D));
		SmFlag = false;
		return true;
	}, 7);

	// ---- ground pound ----
	AddSm64Tp(0.5, FeetY, 18.5, 0);
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetNumberField(TEXT("x"), 0.5); A->SetNumberField(TEXT("y"), FeetY + 8); A->SetNumberField(TEXT("z"), 18.5); A->SetNumberField(TEXT("yaw"), 0);
		AddCommand(TEXT("test.tp"), A, [](bool, const FCrbResult*) {});
	}
	Add(TEXT("sm64 ground pound"), [this] { AnimSeen.Reset(); SmStage = 0; }, [this, M](float T)
	{
		NoteSm64();
		if (SmStage == 0 && T > 0.15f && M().Action == TEXT("FREEFALL")) { Host->SetButtons(false, true, false); SmStage = 1; }
		if (SmStage == 1 && M().Action == TEXT("GROUND_POUND") && !SmFlag && M().VelY < 0) { Shot(TEXT("68_sm64_ground_pound")); SmFlag = true; }
		if (SmStage == 1 && M().Action == TEXT("GROUND_POUND_LAND")) { Host->SetButtons(false, false, false); SmStage = 2; SmT = T; }
		if (SmStage == 2 && T - SmT > 0.12f) { Shot(TEXT("69_sm64_ground_pound_land")); SmStage = 3; }
		if (SmStage < 2 || T - SmT < 0.45f) // the anim instance switches clips on its next update: keep watching briefly
		{ if (SmStage < 2 && T > 5.f) { Host->SetButtons(false, false, false); Fail(TEXT("ground pound not completed: ") + SmSeen() + Diag()); return true; } return false; }
		Check(AnimSeen.Contains(TEXT("GroundPoundSpin")) && AnimSeen.Contains(TEXT("GroundPoundFall")) && AnimSeen.Contains(TEXT("GroundPoundLand")),
			TEXT("Crouch in the air = ground pound: spin, plummet, impact (clips seen: ") + SmSeen() + TEXT(")"));
		SmFlag = false;
		return true;
	}, 7);

	// ---- a 22-block drop: hard landing, no vanilla fall damage ----
	{
		TSharedPtr<FJsonObject> H = MakeShared<FJsonObject>(); H->SetNumberField(TEXT("set"), 20);
		AddCommand(TEXT("test.health"), H, [](bool, const FCrbResult*) {});
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetNumberField(TEXT("x"), 0.5); A->SetNumberField(TEXT("y"), FeetY + 22); A->SetNumberField(TEXT("z"), 20.5); A->SetNumberField(TEXT("yaw"), 0);
		AddCommand(TEXT("test.tp"), A, [](bool, const FCrbResult*) {});
	}
	Add(TEXT("sm64 high drop"), [this] { AnimSeen.Reset(); Host->SetButtons(false, false, false); }, [this, M](float T)
	{
		NoteSm64();
		return (T > 2.0f && M().bGrounded && (AnimSeen.Contains(TEXT("action:HARD_LAND")) || AnimSeen.Contains(TEXT("action:LAND")))) || T > 6.f;
	}, 7);
	AddCommand(TEXT("test.health"), nullptr, [this](bool bOk, const FCrbResult* R)
	{
		double Hp = -1; bool bNoFall = false;
		if (R && R->Json.IsValid()) { R->Json->TryGetNumberField(TEXT("health"), Hp); R->Json->TryGetBoolField(TEXT("noFall"), bNoFall); }
		Check(AnimSeen.Contains(TEXT("action:HARD_LAND")) && AnimSeen.Contains(TEXT("HardLand")) && AnimSeen.Contains(TEXT("Fall")), TEXT("22-block drop: Fall loop, then the hard landing: ") + SmSeen());
		Check(bOk && Hp >= 20.0 && bNoFall, FString::Printf(TEXT("No vanilla fall damage while SM64 drives (health %.1f/20 after a 22-block fall; server flag %d)"), Hp, bNoFall));
	});

	// ---- wall kick off the end wall (z = 46) ----
	AddSm64Tp(0.5, FeetY, 28.5, 0);
	Add(TEXT("sm64 wall kick"), [this] { AnimSeen.Reset(); SmStage = 0; SmT = 0; SmZ0 = 0; }, [this, M](float T)
	{
		NoteSm64();
		const FCrbState& S = Host->GetState();
		if (SmStage == 0) { Host->SetMove(1, 0); if (S.Z > 40.0) { Host->SetButtons(true, false, false); SmStage = 1; SmT = T; } }
		else if (SmStage == 1) { if (T - SmT > 0.3f) Host->SetButtons(false, false, false); if (M().Action == TEXT("AIR_HIT_WALL")) { Host->SetButtons(false, false, false); SmStage = 2; SmT = T; Shot(TEXT("70_sm64_wall_cling")); } }
		else if (SmStage == 2 && T - SmT > 0.05f) { Host->SetMove(0, 0); Host->SetButtons(true, false, false); SmStage = 3; SmT = T; }
		else if (SmStage == 3 && M().Action == TEXT("WALL_KICK")) { SmZ0 = S.Z; SmStage = 4; }
		else if (SmStage == 4) { if (T - SmT > 0.3f) Host->SetButtons(false, false, false); if (!SmFlag && T - SmT > 0.2f) { Shot(TEXT("71_sm64_wall_kick")); SmFlag = true; } if (M().bGrounded) SmStage = 5; }
		if (SmStage < 5) { if (T > 7.f) { Host->SetMove(0, 0); Host->SetButtons(false, false, false); Fail(FString::Printf(TEXT("wall kick not completed (stage %d): "), SmStage) + SmSeen() + Diag()); return true; } return false; }
		const float Face = FRotator::NormalizeAxis(M().FaceYaw);
		Check(M().WallKicks >= 1 && AnimSeen.Contains(TEXT("WallCling")) && AnimSeen.Contains(TEXT("WallKick")) && FMath::Abs(Face) > 150.f && S.Z < SmZ0 - 1.0,
			FString::Printf(TEXT("Jump into a wall, jump again = wall kick: kicks %d, now facing %.0f, %.1f blocks back off the wall"), M().WallKicks, Face, SmZ0 - S.Z));
		SmFlag = false;
		return true;
	}, 9);
	Window(6, 5); // the real window: without a second jump Steve bonks off before landing
	AddSm64Tp(0.5, FeetY, 28.5, 0);
	Add(TEXT("sm64 bonk"), [this] { AnimSeen.Reset(); SmStage = 0; SmT = 0; }, [this, M](float T)
	{
		NoteSm64();
		if (SmStage == 0) { Host->SetMove(1, 0); if (Host->GetState().Z > 40.0) { Host->SetButtons(true, false, false); SmStage = 1; SmT = T; } }
		else if (SmStage == 1) { if (T - SmT > 0.3f) { Host->SetButtons(false, false, false); Host->SetMove(0, 0); } if (AnimSeen.Contains(TEXT("action:BONK")) && M().bGrounded) SmStage = 2; }
		if (SmStage < 2) { if (T > 7.f) { Host->SetMove(0, 0); Host->SetButtons(false, false, false); Fail(TEXT("no bonk off the wall: ") + SmSeen()); return true; } return false; }
		Check(AnimSeen.Contains(TEXT("Bonk")), TEXT("Hitting a wall in the air without kicking = bonk: ") + SmSeen());
		return true;
	}, 9);
	Window(6, 5);

	// ---- settings page: a row changes Java's live setting ----
	AddKey(EKeys::M, TEXT("M"));
	Add(TEXT("mods for sm64 settings"), [] {}, [](float T) { return (CrbMenus::IsOpen() && T > 0.5f) || T > 5.f; }, 6);
	AddClickRow(TEXT("sm64:settings"), TEXT("Mods menu"));
	AddClickRow(TEXT("sm64:speed"), TEXT("SM64 settings"));
	Add(TEXT("sm64 settings shot"), [] {}, [this](float T) { if (T < 0.6f) return false; Shot(TEXT("72_sm64_settings")); return true; }, 3);
	AddCommand(TEXT("sm64.status"), nullptr, [this](bool bOk, const FCrbResult* R)
	{
		double Speed = 0; const TSharedPtr<FJsonObject>* C = nullptr;
		if (R && R->Json.IsValid() && R->Json->TryGetObjectField(TEXT("config"), C)) (*C)->TryGetNumberField(TEXT("speedMultiplier"), Speed);
		Check(bOk && FMath::IsNearlyEqual(Speed, (double)Host->Sm64SpeedMultiplier, 0.01) && Host->Sm64SpeedMultiplier > 1.01f,
			FString::Printf(TEXT("Settings row 'Run speed' changed Java's live setting: %.2f (Unreal %.2f, saved to config)"), Speed, Host->Sm64SpeedMultiplier));
	});
	Add(TEXT("sm64 settings restore"), [this] { Host->Sm64SpeedMultiplier = 1.f; Host->SaveConfig(); Host->PushSm64Config(); }, [](float T) { return T > 0.3f; }, 2);
	AddClickRow(TEXT("resume"), TEXT("SM64 settings"));

	// ---- off again: vanilla movement restored ----
	AddKey(EKeys::M, TEXT("M"));
	Add(TEXT("mods for sm64 off"), [] {}, [](float T) { return (CrbMenus::IsOpen() && T > 0.5f) || T > 5.f; }, 6);
	AddClickRow(TEXT("mod:sm64"), TEXT("Mods menu"));
	AddClickRow(TEXT("resume"), TEXT("Mods menu"));
	Add(TEXT("sm64 off"), [this] { Host->ViewMode = 0; }, [this, M](float T)
	{
		if (T < 1.0f) return false;
		USkeletalMeshComponent* C = Host->Steve.GetMesh();
		Check(!Host->bSm64Enabled && !M().bActive && M().ExitReason == TEXT("disabled") && !M().bServerNoFall,
			FString::Printf(TEXT("mod:sm64 OFF: Java handed the player back to vanilla travel (active=%d, reason '%s', server flag %d)"), M().bActive, *M().ExitReason, M().bServerNoFall));
		Check(!(C && C->IsVisible()) && Host->Avatar.GetHands()->IsVisible(), TEXT("Steve hidden; first-person vanilla hands back"));
		return true;
	}, 4);
	Add(TEXT("sm64 vanilla walk"), [this] { X0 = Host->GetState().X; Z0 = Host->GetState().Z; Host->LookYaw = 90; Host->LookPitch = 0; }, [this](float T)
	{
		Host->SetMove(1, 0);
		if (T < 1.4f) return false;
		Host->SetMove(0, 0);
		const FCrbState& S = Host->GetState();
		const double D = X0 - S.X;
		Check(D > 3.0 && D < 9.0 && FMath::Abs(FRotator::NormalizeAxis(S.Yaw - 90.f)) < 3.f, FString::Printf(TEXT("Vanilla walking again: yaw follows the camera (%.1f), %.1f blocks toward -X in 1.4 s"), S.Yaw, D));
		return true;
	}, 4);
	AddCommand(TEXT("fixture.off"), nullptr, [](bool, const FCrbResult*) {});
	AddCommand(TEXT("player.reset"), nullptr, [](bool, const FCrbResult*) {});
}
