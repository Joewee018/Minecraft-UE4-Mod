// God of War Unity port tests (part of -CrbTest=all, or alone with -CrbTest=avatar). Every check reads evidence from the
// real paths: Java state / op results for gameplay (positions, Mutant health, the axe entity), the skeletal mesh
// components for the rig, the anim instance's debug record for animation state, and the HUD's drawn record for UI.
#include "CrbTest.h"
#include "CrbHost.h"
#include "CrbHUD.h"
#include "CrbMenus.h"
#include "CrbPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Dom/JsonObject.h"
#include "ProceduralMeshComponent.h"
#include "InputCoreTypes.h"

int32 FCrbTest::EmptySlot() const
{
	const FCrbState& S = Host->GetState();
	for (int32 I = 8; I >= 0; --I) if (!S.Slots.IsValidIndex(I) || S.Slots[I].Id.IsEmpty() || S.Slots[I].Id == TEXT("minecraft:air")) return I;
	return -1;
}

void FCrbTest::AddDiscard(FString* Id) { (void)Id; AddCommand(TEXT("gow.mutant.clear"), nullptr, [](bool, const FCrbResult*) {}); }

static const FCrbMutantState* FirstMutant(const FCrbState& S) { return S.Mutants.Num() ? &S.Mutants[0] : nullptr; }

static const FCrbAvatarAnimDebug* EnemyAnim(ACrbHost* Host, int32 Id)
{
	const FCrbPlayerAvatar::FEnemy* E = Host->PlayerAvatar.Enemies.FindByPredicate([Id](const FCrbPlayerAvatar::FEnemy& X) { return X.Id == Id; });
	const UCrbAvatarAnimInstance* A = E && E->Comp ? Cast<UCrbAvatarAnimInstance>(E->Comp->GetAnimInstance()) : nullptr;
	return A ? &A->Debug : nullptr;
}

void FCrbTest::BuildAvatar()
{
	auto HUD = [this]() { return Cast<ACrbHUD>(Host->GetWorld()->GetFirstPlayerController()->GetHUD()); };
	auto Anim = [this]() { const FCrbAvatarAnimDebug* D = Host->PlayerAvatar.AnimDebug(); return D ? *D : FCrbAvatarAnimDebug(); };
	auto NoteAnim = [this, Anim]() { const FCrbAvatarAnimDebug D = Anim(); AnimSeen.Add(D.Base); if (!D.OneShot.IsEmpty()) AnimSeen.Add(D.OneShot); };
	auto SeenStr = [this]() { return FString::Join(AnimSeen.Array(), TEXT(",")); };

	AddCommand(TEXT("player.reset"), nullptr, [](bool, const FCrbResult*) {});
	AddCommand(TEXT("fixture.off"), nullptr, [](bool, const FCrbResult*) {});
	AddCommand(TEXT("gow.mutant.clear"), nullptr, [](bool, const FCrbResult*) {});
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("mode"), TEXT("survival"));
		AddCommand(TEXT("gamemode.set"), A, [](bool, const FCrbResult*) {});
	}

	// ---- the cooked rig ----
	Add(TEXT("gow assets"), [] {}, [this](float T)
	{
		FCrbPlayerAvatar& P = Host->PlayerAvatar;
		if (!P.AssetsReady()) { Fail(TEXT("God of War mod assets missing: ") + P.LoadError); return true; }
		FString Expect; FParse::Value(FCommandLine::Get(), TEXT("-CrbAvatarCrc="), Expect);
		const FString Got = FString::Printf(TEXT("%08X"), P.SkeletonHash());
		Check(!Expect.IsEmpty() && Expect.Equals(Got, ESearchCase::IgnoreCase),
			FString::Printf(TEXT("SK_Avatar is the repo's Kratos (Epic) skeleton: %d bones, CRC %s (Blender build %s)"), P.NumBones(), *Got, Expect.IsEmpty() ? TEXT("not given") : *Expect));
		Check(P.ClipsLoaded == (int32)ECrbClip::Count, FString::Printf(TEXT("Repo animation clips (player on Kratos, enemy on Mutant): %d/%d (missing: %s)"), P.ClipsLoaded, (int32)ECrbClip::Count, *FString::Join(P.MissingClips, TEXT(","))));
		Metrics->SetStringField(TEXT("avatarSkeletonCrc"), Got);
		const FString Mats = FString::Join(P.MaterialReport, TEXT(" "));
		Check(!Mats.Contains(TEXT("WorldGridMaterial")) && !Mats.Contains(TEXT("DefaultMaterial")) && !Mats.Contains(TEXT("=none")) && Mats.Contains(TEXT("M_SK_Avatar_0")),
			TEXT("Imported textured materials on every slot: ") + Mats);
		return true;
	}, 3);

	// ---- toggle on through the Mods menu ----
	AddKey(EKeys::M, TEXT("M"));
	Add(TEXT("mods for gow"), [] {}, [](float T) { return (CrbMenus::IsOpen() && T > 0.5f) || T > 5.f; }, 6);
	AddClickRow(TEXT("mod:avatar"), TEXT("Mods menu"));
	AddClickRow(TEXT("resume"), TEXT("Mods menu"));
	Add(TEXT("gow on"), [this] { Host->LookYaw = 0; Host->LookPitch = -10; }, [this, HUD](float T)
	{
		if (T < 1.2f) return false;
		ACrbHUD* Hd = HUD();
		USkeletalMeshComponent* M = Host->PlayerAvatar.GetMesh();
		const bool bVanillaHidden = !Host->Avatar.GetBody()->IsVisible() && !Host->Avatar.GetHands()->IsVisible();
		Check(Host->bAvatarEnabled && Host->IsAvatarActive() && M && M->IsVisible() && bVanillaHidden,
			FString::Printf(TEXT("mod:avatar ON: Kratos is the player; Java pose groups 0/1 hidden (body %d, hands %d)"), Host->Avatar.GetBody()->IsVisible(), Host->Avatar.GetHands()->IsVisible()));
		Check(Hd && Hd->Drawn.bAvatarHud && Hd->Drawn.bReticle && !Hd->Drawn.bHotbar && Hd->Drawn.HeartContainers == 0,
			TEXT("Mod HUD: vanilla hotbar/vitals hidden, the repo's square reticle shown"));
		if (M)
		{
			const float Height = M->Bounds.BoxExtent.Z * 2.f;
			Check(Height > 150.f && Height < 260.f, FString::Printf(TEXT("Kratos scale: bounds height %.0f UU (built to 190)"), Height));
		}
		const float CamDist = FVector::Dist(Host->Pawn->Camera->GetComponentLocation(), Host->AvatarCameraPivot);
		Check(CamDist > 100.f, FString::Printf(TEXT("Third-person orbit camera %.0f UU behind the shoulder pivot"), CamDist));
		Shot(TEXT("30_gow_back"));
		return true;
	}, 4);
	Add(TEXT("gow front"), [this] { Host->LookYaw = 180; Host->LookPitch = -5; }, [this, Anim](float T)
	{
		if (T < 1.2f) return false;
		const FCrbAvatarAnimDebug D = Anim();
		Check(D.Base == TEXT("Idle") && D.Evaluations > 0, FString::Printf(TEXT("Idle (repo 'Standing Idle'): native anim instance evaluating (%d evaluations)"), D.Evaluations));
		Shot(TEXT("31_gow_front"));
		return true;
	}, 4);
	Add(TEXT("gow three quarter"), [this] { Host->LookYaw = 135; Host->LookPitch = -8; }, [this](float T) { if (T < 1.0f) return false; Shot(TEXT("32_gow_three_quarter")); return true; }, 3);

	// ---- strafe locomotion (PlayerController + PlayerAnimationManager) ----
	Add(TEXT("gow walk"), [this] { X0 = Host->GetState().X; Z0 = Host->GetState().Z; Host->LookYaw = 270; Host->LookPitch = -10; AnimSeen.Reset(); Y0 = 0; }, [this, NoteAnim, Anim, SeenStr](float T)
	{
		Host->SetMove(1, 0);
		NoteAnim(); Y0 = FMath::Max(Y0, (double)Anim().DirZ);
		if (Cross(1.0f)) Shot(TEXT("33_gow_walk_forward"));
		if (T < 1.6f) return false;
		Host->SetMove(0, 0);
		const FCrbState& S = Host->GetState();
		Check(S.X - X0 > 3.0 && FMath::Abs(S.Z - Z0) < 1.0 && FMath::Abs(FRotator::NormalizeAxis(S.Yaw - 270.f)) < 5.f,
			FString::Printf(TEXT("W walks toward the camera direction: Java dX=%.2f dZ=%.2f, yaw %.1f (camera 270)"), S.X - X0, S.Z - Z0, S.Yaw));
		Check(AnimSeen.Contains(TEXT("Walk")) && Y0 > 0.6, FString::Printf(TEXT("Walk Forward blend (forward weight %.2f; states %s)"), Y0, *SeenStr()));
		return true;
	}, 5);
	Add(TEXT("gow strafe"), [this] { X0 = Host->GetState().X; Z0 = Host->GetState().Z; Host->LookYaw = 270; Y0 = 0; }, [this, Anim](float T)
	{
		Host->SetMove(0, 1); // D: strafe right while still facing the camera direction
		Y0 = FMath::Max(Y0, (double)Anim().DirX);
		if (Cross(1.0f)) Shot(TEXT("34_gow_strafe_right"));
		if (T < 1.4f) return false;
		Host->SetMove(0, 0);
		const FCrbState& S = Host->GetState();
		Check(S.Z - Z0 > 2.0 && FMath::Abs(FRotator::NormalizeAxis(S.Yaw - 270.f)) < 5.f && Y0 > 0.6,
			FString::Printf(TEXT("Strafe: moved dZ=%.2f to camera-right while facing yaw %.1f; Walk Right blend weight %.2f"), S.Z - Z0, S.Yaw, Y0));
		return true;
	}, 4);
	Add(TEXT("gow sprint"), [this] { AnimSeen.Reset(); Host->LookYaw = 90; }, [this, NoteAnim, SeenStr](float T)
	{
		Host->SetMove(1, 0); Host->SetButtons(false, false, true);
		NoteAnim();
		if (T < 1.6f) return false;
		Host->SetMove(0, 0); Host->SetButtons(false, false, false);
		Check(AnimSeen.Contains(TEXT("Run")), TEXT("Sprint uses the Run/Jog set (states ") + SeenStr() + TEXT(")"));
		return true;
	}, 4);

	// ---- the Mutant enemy ----
	AddCommand(TEXT("player.reset"), nullptr, [](bool, const FCrbResult*) {});
	Add(TEXT("empty hand"), [this] { const int32 E = EmptySlot(); if (E >= 0) Host->SelectSlot(E); Host->LookYaw = 0; Host->LookPitch = -8; }, [this](float T)
	{
		if (T < 0.6f) return false;
		Check(Host->IsMainHandEmpty(), FString::Printf(TEXT("Main hand empty: the axe is in hand (slot %d)"), Host->GetState().Selected + 1));
		return true;
	}, 3);
	AddKey(EKeys::M, TEXT("M"));
	Add(TEXT("mods for spawn"), [] {}, [](float T) { return (CrbMenus::IsOpen() && T > 0.5f) || T > 5.f; }, 6);
	AddClickRow(TEXT("gow:spawn"), TEXT("Mods menu"));
	AddClickRow(TEXT("resume"), TEXT("Mods menu"));
	Add(TEXT("mutant visible"), [] {}, [this, HUD](float T)
	{
		const FCrbMutantState* M = FirstMutant(Host->GetState());
		ACrbHUD* Hd = HUD();
		if (M && Host->PlayerAvatar.VisibleEnemies() > 0 && Hd && Hd->Drawn.EnemyBars > 0 && T > 1.5f)
		{
			const FCrbAvatarAnimDebug* D = EnemyAnim(Host, M->Id);
			Check(M->Health == 100.f && D && D->Base == TEXT("EnemyIdle"), FString::Printf(TEXT("Spawn Mutant: Java mutant (health %.0f) drawn with the Mutant mesh in its Idle clip, health bar shown"), M->Health));
			Shot(TEXT("35_mutant_enemy"));
			return true;
		}
		if (T > 6.f) { Fail(FString::Printf(TEXT("Mutant not shown (java %d, visible %d, bars %d)"), Host->GetState().Mutants.Num(), Host->PlayerAvatar.VisibleEnemies(), Hd ? Hd->Drawn.EnemyBars : -1)); return true; }
		return false;
	}, 8);

	// ---- axe throw (RMB) -> sticks in the Mutant, 30 damage ----
	Add(TEXT("axe throw"), [this] { X0 = Host->AxeThrowsSent; Host->SecondaryPressed(true); Host->SecondaryPressed(false); AnimSeen.Reset(); }, [this, NoteAnim, SeenStr](float T)
	{
		NoteAnim();
		const FCrbState& S = Host->GetState();
		if (Cross(0.75f)) Shot(TEXT("36_axe_throw"));
		const FCrbMutantState* M = FirstMutant(S);
		if (S.bAxeActive && S.AxePhase == 1 && M && M->Health <= 70.f && T > 1.0f)
		{
			Check(Host->AxeThrowsSent > X0 && AnimSeen.Contains(TEXT("Throw")) && !Host->PlayerAvatar.bAxeInHand,
				FString::Printf(TEXT("RMB: throw clip, release at the animation event; Java axe flew %.1f blocks and stuck in the Mutant (health %.0f, axe hits %d)"), S.AxeTravelled, M->Health, S.AxeHits));
			Shot(TEXT("37_axe_stuck_in_mutant"));
			return true;
		}
		if (T > 6.f) { Fail(FString::Printf(TEXT("Throw failed (sent %d, axe active %d phase %d travelled %.1f, mutant health %.0f; states %s)"), Host->AxeThrowsSent, S.bAxeActive, S.AxePhase, S.AxeTravelled, M ? M->Health : -1.f, *SeenStr())); return true; }
		return false;
	}, 8);
	Add(TEXT("axe recall"), [this] { X0 = Host->AxeCatches; Y0 = Host->AxeRecallsSent; PressKey(EKeys::R); }, [this](float T)
	{
		const FCrbState& S = Host->GetState();
		if (Cross(0.5f)) Shot(TEXT("38_axe_returning"));
		if (!S.bAxeActive && Host->AxeCatches > X0 && T > 0.3f)
		{
			Check(Host->AxeRecallsSent > Y0 && Host->PlayerAvatar.bAxeInHand, FString::Printf(TEXT("R: the axe returned along the Bezier curve and is back in the hand after %.2fs"), T));
			return true;
		}
		if (T > 5.f) { Fail(FString::Printf(TEXT("Recall did not complete (active %d phase %d, recalls %d)"), S.bAxeActive, S.AxePhase, Host->AxeRecallsSent)); return true; }
		return false;
	}, 6);

	// ---- melee until the Mutant dies (30 per hit, 1 s cooldown) ----
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetNumberField(TEXT("distance"), 2.0);
		AddCommand(TEXT("gow.mutant.clear"), nullptr, [](bool, const FCrbResult*) {});
		AddCommand(TEXT("gow.mutant.spawn"), A, [this](bool bOk, const FCrbResult*) { Check(bOk, TEXT("Mutant spawned at arm's length")); });
	}
	Add(TEXT("melee to death"), [this] { AnimSeen.Reset(); X0 = Host->MeleeSent; Host->LookYaw = 0; Host->LookPitch = -10; }, [this, NoteAnim, SeenStr](float T)
	{
		NoteAnim();
		if (!Host->IsAvatarAttacking() && T > 0.5f) { Host->PrimaryPressed(true); Host->PrimaryPressed(false); }
		if (Cross(1.0f)) Shot(TEXT("39_melee"));
		const FCrbMutantState* M = FirstMutant(Host->GetState());
		const FCrbAvatarAnimDebug* D = M ? EnemyAnim(Host, M->Id) : nullptr;
		if (M && M->DeathTime > 8 && D && D->Base == TEXT("Death"))
		{
			Check(AnimSeen.Contains(TEXT("Attack")) && Host->MeleeSent - X0 >= 4, FString::Printf(TEXT("LMB melee (axe in hand, 30 per hit): %d swings killed the 100-health Mutant; it plays Mutant Dying"), (int32)(Host->MeleeSent - X0)));
			Shot(TEXT("40_mutant_dying"));
			return true;
		}
		if (T > 14.f) { Fail(FString::Printf(TEXT("Mutant not killed by melee (health %.0f, swings %d, states %s)"), M ? M->Health : -1.f, (int32)(Host->MeleeSent - X0), *SeenStr())); return true; }
		return false;
	}, 16);
	Add(TEXT("mutant removed"), [] {}, [this](float T)
	{
		if (Host->GetState().Mutants.Num() == 0 && T > 0.3f) { Check(Host->PlayerAvatar.VisibleEnemies() == 0, TEXT("Dead Mutant removed after its death clip")); return true; }
		if (T > 5.f) { Fail(TEXT("Dead Mutant not removed")); return true; }
		return false;
	}, 6);

	// ---- everything else keeps working in mod mode ----
	AddKey(EKeys::E, TEXT("E"));
	Add(TEXT("gow inventory"), [] {}, [this](float T)
	{
		if (T < 0.6f) return false;
		Check(Host->bInventoryOpen, TEXT("Inventory (E) opens with the mod on"));
		Shot(TEXT("41_gow_inventory"));
		PressKey(EKeys::E);
		return true;
	}, 3);
	Add(TEXT("gow inventory closed"), [] {}, [this](float T) { if (T < 0.5f) return false; Check(!Host->bInventoryOpen, TEXT("Inventory closes with the mod on")); return true; }, 3);
	AddKey(EKeys::Escape, TEXT("Esc"));
	Add(TEXT("gow pause"), [] {}, [this](float T) { if (T < 0.5f) return false; Check(Host->bPauseOpen, TEXT("Esc opens the pause menu with the mod on")); Host->CloseMenus(); return true; }, 3);
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("mode"), TEXT("creative"));
		AddCommand(TEXT("gamemode.set"), A, [this](bool bOk, const FCrbResult*) { Check(bOk, TEXT("Creative mode with the mod on")); });
	}
	Add(TEXT("gow fly"), [this] { AnimSeen.Reset(); }, [this, NoteAnim, SeenStr](float T)
	{
		for (float B : { 0.2f, 1.4f, 2.6f })
		{
			if (Host->GetState().bFlying) break;
			if (Cross(B)) Host->SetButtons(true, false, false);
			if (Cross(B + 0.1f)) Host->SetButtons(false, false, false);
			if (Cross(B + 0.2f)) Host->SetButtons(true, false, false);
			if (Cross(B + 0.3f)) Host->SetButtons(false, false, false);
		}
		NoteAnim();
		if (Host->GetState().bFlying && T > 2.0f) { Check(AnimSeen.Contains(TEXT("Fly")), TEXT("Creative flight works with the mod on (states ") + SeenStr() + TEXT(")")); return true; }
		if (T > 5.f) { Fail(TEXT("Creative flight did not start with the mod on") + Diag()); return true; }
		return false;
	}, 6);
	Add(TEXT("gow land"), [this] { Host->SetButtons(false, true, false); }, [this](float T) { if (T < 2.5f) return false; Host->SetButtons(false, false, false); return true; }, 4);
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("mode"), TEXT("survival"));
		AddCommand(TEXT("gamemode.set"), A, [](bool, const FCrbResult*) {});
	}
	AddCommand(TEXT("player.kill"), nullptr, [](bool, const FCrbResult*) {});
	Add(TEXT("gow death"), [this] { AnimSeen.Reset(); }, [this, NoteAnim, SeenStr](float T)
	{
		NoteAnim();
		if (Host->bDeathOpen && AnimSeen.Contains(TEXT("Death")) && T > 1.5f)
		{
			Check(Host->PlayerAvatar.GetMesh()->IsVisible(), TEXT("Player death: death screen opens and Kratos plays the dying clip"));
			Shot(TEXT("42_gow_player_death"));
			return true;
		}
		if (T > 8.f) { Fail(TEXT("Player death not shown (states ") + SeenStr() + TEXT(")")); return true; }
		return false;
	}, 10);
	AddClickRow(TEXT("death:respawn"), TEXT("Death screen (mod on)"));
	Add(TEXT("gow respawned"), [] {}, [this, Anim](float T)
	{
		if (T < 1.5f) return false;
		Check(!Host->GetState().bDead && !Host->bDeathOpen && Anim().Base != TEXT("Death"), TEXT("Respawn with the mod on returns to locomotion (") + Anim().Base + TEXT(")"));
		return true;
	}, 4);

	// ---- toggle off restores vanilla ----
	AddKey(EKeys::M, TEXT("M"));
	Add(TEXT("mods for gow off"), [] {}, [](float T) { return (CrbMenus::IsOpen() && T > 0.5f) || T > 5.f; }, 6);
	AddClickRow(TEXT("mod:avatar"), TEXT("Mods menu"));
	AddClickRow(TEXT("resume"), TEXT("Mods menu"));
	Add(TEXT("gow off"), [this] { Host->ViewMode = 0; Host->LookPitch = 0; }, [this, HUD](float T)
	{
		if (T < 1.2f) return false;
		ACrbHUD* Hd = HUD();
		Check(!Host->bAvatarEnabled && !Host->PlayerAvatar.GetMesh()->IsVisible() && !Host->PlayerAvatar.GetAxe()->IsVisible() && Host->Avatar.GetHands()->IsVisible() && Hd && Hd->Drawn.bHotbar && Hd->Drawn.HeartContainers > 0,
			TEXT("mod:avatar OFF: Kratos and the axe hidden; first-person hands, hotbar and vitals restored"));
		Shot(TEXT("43_gow_off"));
		return true;
	}, 4);
	AddMovementCheck(TEXT("After God of War mod"));
}
