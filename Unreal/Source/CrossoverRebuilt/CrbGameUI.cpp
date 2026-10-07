// Game UI that sits on top of the Java game: pause menu (Esc), death screen with Respawn, survival/creative switch,
// the creative inventory (vanilla tabs, pages, carried stack, destroy slot, search) and the Herobrine (From The Fog)
// settings menu. Every game effect goes through Java: Unreal only shows state and sends the vanilla actions.
#include "CrbHost.h"
#include "CrbHUD.h"
#include "CrbMenus.h"
#include "CrbConnection.h"
#include "Dom/JsonObject.h"
#include "Containers/Ticker.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"

namespace
{
	TSharedPtr<FJsonObject> Args1(const TCHAR* K, const FString& V) { TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(K, V); return A; }
}

// ---------------- pause / quit / death ----------------

void ACrbHost::TogglePause()
{
	if (bDeathOpen) return; // the death screen has no pause menu (vanilla)
	if (bPauseOpen) { CloseMenus(); return; }
	CloseMenus();
	bPauseOpen = true;
	CrbMenus::OpenPauseMenu(this);
	SendInput(true);
}

void ACrbHost::QuitGame()
{
	if (bQuitting) return;
	bQuitting = true;
	StatusLine = TEXT("Saving the world and quitting...");
	// Java saves the integrated world and closes the Minecraft client; Unreal exits once Java acknowledged
	// (or after a short timeout if Minecraft is not connected).
	if (Connection && Connection->GetState() == ECrbLinkState::Connected) SendCommand(TEXT("game.quit"));
	TWeakObjectPtr<ACrbHost> W = this;
	const double Started = FPlatformTime::Seconds();
	FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([W, Started](float)
	{
		if (!W.IsValid()) return false;
		const bool bJavaGone = !W->Link() || W->Link()->GetState() != ECrbLinkState::Connected;
		if (!bJavaGone && FPlatformTime::Seconds() - Started < 6.0) return true;
		APlayerController* PC = W->GetWorld() ? W->GetWorld()->GetFirstPlayerController() : nullptr;
		UKismetSystemLibrary::QuitGame(W.Get(), PC, EQuitPreference::Quit, false);
		return false;
	}), 0.1f);
}

void ACrbHost::Respawn()
{
	if (!State.bDead) return;
	SendCommand(TEXT("player.respawn"));
}

void ACrbHost::SetGameMode(const FString& Mode)
{
	SendCommand(TEXT("gamemode.set"), Args1(TEXT("mode"), Mode));
}

// ---------------- creative inventory ----------------

void ACrbHost::CreativeRequestPage(bool bForce)
{
	if (!CreativeTabs.IsValidIndex(CreativeTab) || IsCreativeInventoryTab()) return;
	if (!bForce && CreativePageTab == CreativeTab && CreativePageRow == CreativeRow && CreativePageQuery == CreativeQuery) return;
	CreativePageTab = CreativeTab; CreativePageRow = CreativeRow; CreativePageQuery = CreativeQuery;
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
	A->SetNumberField(TEXT("tab"), CreativeTab); A->SetNumberField(TEXT("row"), CreativeRow); A->SetStringField(TEXT("query"), CreativeQuery);
	SendCommand(TEXT("creative.page"), A);
}

void ACrbHost::CreativeSelectTab(int32 Index)
{
	if (!CreativeTabs.IsValidIndex(Index)) return;
	CreativeTab = Index; CreativeRow = 0; CreativeMaxRow = 0; CreativeItems.Reset();
	if (!IsCreativeInventoryTab()) CreativeRequestPage(true);
}

void ACrbHost::CreativeSetRow(int32 Row)
{
	const int32 R = FMath::Clamp(Row, 0, FMath::Max(0, CreativeMaxRow));
	if (R != CreativeRow) { CreativeRow = R; CreativeRequestPage(); }
}

void ACrbHost::CreativeScroll(float Delta)
{
	if (IsCreativeInventoryTab()) return;
	CreativeSetRow(CreativeRow + (Delta > 0 ? -1 : 1)); // vanilla: wheel up scrolls up
}

void ACrbHost::CreativeType(const FString& Chars)
{
	if (!IsCreativeSearch()) return;
	CreativeQuery = (CreativeQuery + Chars).Left(50);
	CreativeRow = 0; CreativeQueryChangedAt = FPlatformTime::Seconds();
}

void ACrbHost::CreativeBackspace()
{
	if (!IsCreativeSearch() || CreativeQuery.IsEmpty()) return;
	CreativeQuery.LeftChopInline(1);
	CreativeRow = 0; CreativeQueryChangedAt = FPlatformTime::Seconds();
}

void ACrbHost::CreativeClickAt(const FVector2D& Screen, int32 Button)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	ACrbHUD* H = PC ? Cast<ACrbHUD>(PC->GetHUD()) : nullptr;
	if (!H) return;
	const FCrbCreativeHit Hit = H->CreativeHitAt(Screen);
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
	switch (Hit.Kind)
	{
	case ECrbCreativeHit::Tab: CreativeSelectTab(Hit.Index); break;
	case ECrbCreativeHit::Grid:
		A->SetNumberField(TEXT("tab"), CreativeTab); A->SetNumberField(TEXT("row"), CreativeRow); A->SetStringField(TEXT("query"), CreativeQuery);
		A->SetNumberField(TEXT("cell"), Hit.Index); A->SetNumberField(TEXT("button"), Button);
		SendCommand(TEXT("creative.pick"), A);
		break;
	case ECrbCreativeHit::Slot:
		A->SetNumberField(TEXT("slot"), Hit.Index); A->SetNumberField(TEXT("button"), Button);
		SendCommand(TEXT("creative.slot"), A);
		break;
	case ECrbCreativeHit::Destroy:
		A->SetBoolField(TEXT("all"), bSneakHeld);
		SendCommand(TEXT("creative.destroy"), A);
		break;
	case ECrbCreativeHit::Scroll: CreativeSetRow(FMath::RoundToInt(Hit.ScrollFrac * CreativeMaxRow)); break;
	default: break;
	}
}

// ---------------- Herobrine (From The Fog) ----------------

bool ACrbHost::IsHerobrineInstalled() const
{
	for (const FCrbModInfo& M : Mods) if (M.Id == TEXT("watching")) return true;
	return false;
}

void ACrbHost::OpenHerobrineMenu()
{
	CloseMenus();
	bModMenuOpen = true;
	CrbMenus::OpenHerobrineMenu(this);
	SendCommand(TEXT("herobrine.status"));
	SendInput(true);
}

void ACrbHost::HerobrineCommand(const FString& Op, const TSharedPtr<FJsonObject>& Args)
{
	SendCommand(Op, Args);
}

// ---------------- per-frame + results ----------------

void ACrbHost::OnGameUIResult(const FCrbResult& R)
{
	const TSharedPtr<FJsonObject>& J = R.Json;
	if (!J.IsValid()) return;
	if (R.Op == TEXT("creative.tabs") && R.bOk)
	{
		CreativeTabs.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (J->TryGetArrayField(TEXT("tabs"), Arr))
			for (int32 I = 0; I < Arr->Num() && I < 32; ++I)
			{
				const TSharedPtr<FJsonObject> O = (*Arr)[I]->AsObject();
				if (!O.IsValid()) continue;
				FCreativeTab T; double D = 0;
				T.Index = I; O->TryGetStringField(TEXT("id"), T.Id); O->TryGetStringField(TEXT("name"), T.Name); O->TryGetStringField(TEXT("type"), T.Type); O->TryGetStringField(TEXT("bg"), T.Bg);
				if (O->TryGetNumberField(TEXT("row"), D)) T.Row = (int32)D;
				if (O->TryGetNumberField(TEXT("col"), D)) T.Col = FMath::Clamp((int32)D, 0, 7);
				O->TryGetBoolField(TEXT("right"), T.bRight); O->TryGetBoolField(TEXT("scroll"), T.bScroll); O->TryGetBoolField(TEXT("showTitle"), T.bShowTitle);
				T.Name = T.Name.Left(40); T.Bg = T.Bg.Left(40);
				CreativeTabs.Add(T);
			}
		// Vanilla opens on the first category tab (Building Blocks).
		int32 First = 0;
		for (const FCreativeTab& T : CreativeTabs) if (T.Type == TEXT("category")) { First = T.Index; break; }
		if (!CreativeTabs.IsValidIndex(CreativeTab)) CreativeTab = First;
		CreativeSelectTab(CreativeTab);
	}
	else if (R.Op == TEXT("creative.page") && R.bOk)
	{
		double D = 0;
		if (J->TryGetNumberField(TEXT("tab"), D) && (int32)D != CreativeTab) return; // stale page for another tab
		CreativeItems.Reset();
		if (J->TryGetNumberField(TEXT("row"), D)) CreativeRow = (int32)D;
		if (J->TryGetNumberField(TEXT("maxRow"), D)) CreativeMaxRow = (int32)D;
		if (J->TryGetNumberField(TEXT("total"), D)) CreativeTotal = (int32)D;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (J->TryGetArrayField(TEXT("items"), Arr))
			for (int32 I = 0; I < Arr->Num() && I < 45; ++I)
			{
				const TSharedPtr<FJsonObject> O = (*Arr)[I]->AsObject();
				FCreativeItem It;
				if (O.IsValid()) { O->TryGetStringField(TEXT("id"), It.Id); O->TryGetStringField(TEXT("name"), It.Name); if (O->TryGetNumberField(TEXT("count"), D)) It.Count = (int32)D; }
				It.Name = It.Name.Left(64);
				CreativeItems.Add(It);
			}
	}
	else if (R.Op.StartsWith(TEXT("herobrine.")) && J->HasField(TEXT("options")))
	{
		HerobrineStatus = J;
		if (bModMenuOpen) CrbMenus::RefreshHerobrine(this);
	}
	else if (R.Op == TEXT("gamemode.set") && R.bOk && bDebugMenuOpen) CrbMenus::Refresh(this);
	else if (R.Op == TEXT("map.list")) OnMapList(J);
}

void ACrbHost::TickGameUI(float Dt)
{
	// Death screen follows Java: open while the player is dead, close after the respawn.
	if (State.bValid && State.bDead && !bDeathOpen && !bQuitting)
	{
		CloseMenus();
		bDeathOpen = true;
		CrbMenus::OpenDeathMenu(this);
		SendInput(true);
	}
	else if (bDeathOpen && State.bValid && !State.bDead) CloseMenus();
	// Leaving creative forgets the cached page so re-entering requests a fresh one.
	if (!IsCreative()) CreativePageTab = -1;
	// Search typing is debounced (one page request per pause in typing).
	if (IsCreativeSearch() && CreativeQuery != CreativePageQuery && FPlatformTime::Seconds() - CreativeQueryChangedAt > 0.15) CreativeRequestPage();
}
