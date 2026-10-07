// Zombies game mode (Black Ops 2 style on Minecraft mechanics). Java (crb.zm.ZombiesGame) owns the arena, rounds,
// points, perks, crate and the zombies; Guns++ owns the guns and bullets. Unreal starts/stops the match from the
// pause menu's Game Modes page, sends F-interact (held = repeated, for rebuilding barriers) and draws the HUD.
#include "CrbHost.h"
#include "CrbMenus.h"
#include "Dom/JsonObject.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"

void ACrbHost::OpenGameModesMenu()
{
	CloseMenus();
	bPauseOpen = true; // a page of the pause menu: Esc closes it
	CrbMenus::OpenGameModesMenu(this);
	SendInput(true);
}

void ACrbHost::StartZombies()
{
	if (IsAvatarActive()) ToggleAvatar(); // Kratos has no gun hand: zombies is played first person with Guns++
	if (Weapon != ECrbWeapon::Hand) SelectWeapon(ECrbWeapon::Hand);
	ViewMode = 0;
	// Matchmaking lobby first (BO2 feel): the countdown runs, then Java builds the arena and the match starts.
	bZmLobby = true; ZmLobbyStart = FPlatformTime::Seconds();
	StatusLine = TEXT("Zombies: waiting for players...");
}

void ACrbHost::StopZombies()
{
	SendCommand(TEXT("zm.stop"));
	StatusLine = TEXT("Zombies: match ended");
}

void ACrbHost::InteractPressed(bool bDown)
{
	bInteractHeld = bDown && !IsMenuOpen();
	if (bInteractHeld) LastInteractSent = 0; // send on this frame
}

void ACrbHost::TickZombies(float Dt)
{
	TickChalk();
	if (bZmLobby && FPlatformTime::Seconds() - ZmLobbyStart >= ZmLobbySeconds) { bZmLobby = false; SendCommand(TEXT("zm.start")); StatusLine = TEXT("Zombies: building the arena..."); }
	if (!IsZombiesActive() || IsMenuOpen()) { bInteractHeld = false; return; }
	const double Now = FPlatformTime::Seconds();
	// Held F: one request every 0.25 s (Java rate-limits buys to one per second and repairs to one board per 0.75 s).
	if (bInteractHeld && Now - LastInteractSent >= 0.25)
	{
		LastInteractSent = Now;
		PendingInteractId = SendCommand(TEXT("zm.interact"));
		++InteractsSent;
	}
	if (!PendingInteractId.IsEmpty())
		if (const FCrbResult* R = FindResult(PendingInteractId)) { LastInteractResult = R->Message; PendingInteractId.Reset(); }
}

// BO2-style chalk wall-buys: one quad per wall-buy on the wall surface, textured with Java's traced outline sheet
// (crb:chalk, 32 px cells from the real gun icons). No frame and no item are drawn - only the outline.
void ACrbHost::TickChalk()
{
	const FCrbZmState& Z = State.Zm;
	UTexture2D* Sheet = Z.ChalkSheet.IsEmpty() ? nullptr : Textures.Get(Z.ChalkSheet);
	FString Sig = FString::Printf(TEXT("%d|%d|"), Z.ChalkGen, Sheet ? 1 : 0);
	for (const FCrbZmState::FWallBuy& W : Z.WallBuys) Sig += FString::Printf(TEXT("%d,%d,%d,%s,%d;"), W.X, W.Y, W.Z, *W.Face, W.Cell);
	// Nothing to draw: clear once and stop. (The anchor suffix below used to turn "none" into "none@x,y,z", so the
	// early-out never fired and a missing sheet was dereferenced - the crash on every run without Zombies.)
	const bool bNone = !Z.IsActive() || !Sheet || Sheet->GetSizeX() <= 0 || Z.WallBuys.Num() == 0;
	if (bNone) Sig = TEXT("none");
	if (!MatParticle || !RootComponent) return; // presentation not ready yet (first frames): nothing to draw
	if (!ChalkMesh)
	{
		ChalkMesh = NewObject<UProceduralMeshComponent>(this);
		ChalkMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ChalkMesh->SetCastShadow(false);
		ChalkMesh->SetupAttachment(GetRootComponent());
		ChalkMesh->RegisterComponent();
		ChalkMid = UMaterialInstanceDynamic::Create(MatParticle, this);
		ChalkMid->SetScalarParameterValue(TEXT("EmissiveScale"), BaseSunLux / PI);
	}
	// Anchor moves with Coords: rebuild relative to the current anchor whenever anything changes.
	const FVector Anchor = Coords.ToUE(0, 0, 0);
	Sig += FString::Printf(TEXT("@%.0f,%.0f,%.0f"), Anchor.X, Anchor.Y, Anchor.Z);
	if (Sig == ChalkSignature) return;
	ChalkSignature = Sig;
	ChalkMesh->ClearAllMeshSections();
	ChalkQuads = 0;
	if (bNone || !ChalkMid || !Sheet) return;
	ChalkMid->SetTextureParameterValue(TEXT("Tex"), Sheet);
	ChalkMid->SetScalarParameterValue(TEXT("HasTexture"), 1.f);
	const float SW = Sheet->GetSizeX(), SH = Sheet->GetSizeY(), Cell = 32.f;
	const int32 Cols = FMath::Max(1, (int32)(SW / Cell));
	TArray<FVector> V, N; TArray<int32> I; TArray<FVector2D> UV, UV2; TArray<FLinearColor> C;
	for (const FCrbZmState::FWallBuy& W : Z.WallBuys)
	{
		FVector Nm(0, 0, 1);
		if (W.Face == TEXT("north")) Nm = FVector(0, 0, -1); else if (W.Face == TEXT("east")) Nm = FVector(1, 0, 0); else if (W.Face == TEXT("west")) Nm = FVector(-1, 0, 0);
		const FVector Up(0, 1, 0), Right = FVector::CrossProduct(Up, Nm);
		// The frame block sits in front of the wall: draw on the wall surface (back face of that block), nudged out.
		const FVector Ctr = FVector(W.X + 0.5f, W.Y + 0.5f, W.Z + 0.5f) - Nm * 0.49f;
		const float H = 0.8f;
		const FVector Mc[4] = { Ctr - Right * H + Up * H, Ctr + Right * H + Up * H, Ctr + Right * H - Up * H, Ctr - Right * H - Up * H };
		const float U0 = (W.Cell % Cols) * Cell / SW, V0 = (W.Cell / Cols) * Cell / SH, DU = Cell / SW, DV = Cell / SH;
		const FVector2D T[4] = { FVector2D(U0, V0), FVector2D(U0 + DU, V0), FVector2D(U0 + DU, V0 + DV), FVector2D(U0, V0 + DV) };
		const int32 B = V.Num();
		for (int32 K = 0; K < 4; ++K)
		{
			V.Add(Coords.ToUE(Mc[K].X, Mc[K].Y, Mc[K].Z) - Anchor);
			N.Add(FCrbCoords::NormalToUE(Nm));
			FVector2D Co, Fi; CrbSplitUV(T[K], Co, Fi); UV.Add(Co); UV2.Add(Fi);
			C.Add(FLinearColor(0.75f, 0.75f, 0.72f, 1.f));
		}
		I.Append({ B, B + 1, B + 2, B, B + 2, B + 3 });
		++ChalkQuads;
	}
	ChalkMesh->SetWorldLocation(Anchor);
	ChalkMesh->CreateMeshSection_LinearColor(0, V, I, N, UV, TArray<FVector2D>(), UV2, TArray<FVector2D>(), C, TArray<FProcMeshTangent>(), false);
	ChalkMesh->SetMaterial(0, ChalkMid);
}
