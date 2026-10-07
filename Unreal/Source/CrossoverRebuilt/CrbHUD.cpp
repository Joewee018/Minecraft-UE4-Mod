#include "CrbHUD.h"
#include "CrbHost.h"
#include "CrbMcUi.h"
#include "EngineUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
	// Vanilla InventoryMenu slot -> GUI position (relative to the 176x166 panel).
	bool SlotPos(int32 Slot, FVector2D& P)
	{
		if (Slot == 0) { P = FVector2D(154, 28); return true; }
		if (Slot >= 1 && Slot <= 4) { const int32 I = Slot - 1; P = FVector2D(98 + (I % 2) * 18, 18 + (I / 2) * 18); return true; }
		if (Slot >= 5 && Slot <= 8) { P = FVector2D(8, 8 + (Slot - 5) * 18); return true; }
		if (Slot >= 9 && Slot <= 35) { const int32 I = Slot - 9; P = FVector2D(8 + (I % 9) * 18, 84 + (I / 9) * 18); return true; }
		if (Slot >= 36 && Slot <= 44) { P = FVector2D(8 + (Slot - 36) * 18, 142); return true; }
		if (Slot == 45) { P = FVector2D(77, 62); return true; }
		return false;
	}
	// Vanilla menu slot -> index in the bridge's state/icon list.
	int32 IconFor(int32 Slot)
	{
		if (Slot == 0) return 46;
		if (Slot >= 1 && Slot <= 4) return 42 + Slot - 1;
		if (Slot >= 5 && Slot <= 8) return 13 - (Slot - 5);
		if (Slot >= 9 && Slot <= 35) return 14 + Slot - 9;
		if (Slot >= 36 && Slot <= 44) return Slot - 36;
		if (Slot == 45) return 9;
		return -1;
	}
}

ACrbHost* ACrbHUD::Host()
{
	if (!CachedHost.IsValid()) for (TActorIterator<ACrbHost> It(GetWorld()); It; ++It) { CachedHost = *It; break; }
	return CachedHost.Get();
}

// A texture can be drawn only once its render resource and sampler exist (a texture replaced or reset after a reconnect
// briefly has neither; drawing it asserted in BatchedElements.cpp).
static bool CrbHudTexOk(UTexture* T) { return T && T->Resource && T->Resource->SamplerStateRHI.IsValid(); }

void ACrbHUD::Sprite(UTexture2D* T, float X, float Y, float U, float V, float W, float H, float S, FLinearColor Tint)
{
	if (!CrbHudTexOk(T)) return;
	const float TW = T->GetSizeX(), TH = T->GetSizeY();
	DrawTexture(T, X, Y, W * S, H * S, U / TW, V / TH, W / TW, H / TH, Tint, BLEND_Translucent);
}

void ACrbHUD::BuildFont()
{
	ACrbHost* H = Host();
	const FString Name = TEXT("minecraft:textures/font/ascii.png");
	H->Textures.KeepPixels(Name);
	Font = H->Textures.Get(Name);
	const TArray<uint8>* Px = H->Textures.Pixels(Name);
	if (!Font || !Px || Font->GetSizeX() != 128 || Font->GetSizeY() != 128 || Px->Num() != 128 * 128 * 4) return;
	// Vanilla bitmap glyph width: rightmost non-transparent column + 1; space advances 4.
	for (int32 G = 0; G < 256; ++G)
	{
		int32 W = 0;
		for (int32 X = 7; X >= 0 && W == 0; --X)
			for (int32 Y = 0; Y < 8; ++Y)
				if ((*Px)[(((G / 16) * 8 + Y) * 128 + (G % 16) * 8 + X) * 4 + 3] > 0) { W = X + 1; break; }
		FontWidths[G] = W;
	}
	FontWidths[32] = 3;
	bFontReady = true;
}

float ACrbHUD::McWidth(const FString& S) const
{
	float W = 0;
	for (TCHAR Ch : S) { const int32 Gi = CrbMcUi::Glyph(Ch); if (Gi >= 0) W += (bFontReady ? FontWidths[Gi] : 5) + 1; }
	return W > 0 ? W - 1 : 0;
}

float ACrbHUD::McText(const FString& S, float X, float Y, FLinearColor C, float Scale, bool bShadow)
{
	if (!bFontReady)
	{
		UFont* F = GEngine ? GEngine->GetSmallFont() : nullptr;
		if (bShadow) DrawText(S, FLinearColor(C.R * 0.25f, C.G * 0.25f, C.B * 0.25f, C.A), X + Scale, Y + Scale, F, Scale * 0.7f);
		DrawText(S, C, X, Y, F, Scale * 0.7f);
		return McWidth(S) * Scale;
	}
	if (bShadow) McText(S, X + Scale, Y + Scale, FLinearColor(C.R * 0.25f, C.G * 0.25f, C.B * 0.25f, C.A), Scale, false);
	float PX = X;
	for (TCHAR Ch : S)
	{
		const int32 G = CrbMcUi::Glyph(Ch);
		if (G < 0) continue; // glyphs from other (mod) fonts are not in ascii.png
		if (G != 32) Sprite(Font, PX, Y, (G % 16) * 8, (G / 16) * 8, 8, 8, Scale, C);
		PX += (FontWidths[G] + 1) * Scale;
	}
	return PX - X;
}

void ACrbHUD::ItemIcon(int32 Index, float X, float Y, float S, bool bDecorations)
{
	ACrbHost* H = Host();
	const FCrbState& St = H->GetState();
	if (!St.Slots.IsValidIndex(Index) || St.Slots[Index].Id.IsEmpty()) return;
	const FCrbSlot& Slot = St.Slots[Index];
	if (Items)
	{
		const float TW = Items->GetSizeX(), TH = Items->GetSizeY();
		if (CrbHudTexOk(Items)) DrawTexture(Items, X, Y, 16 * S, 16 * S, (Index % IconCols) * IconCell / TW, (Index / IconCols) * IconCell / TH, IconCell / TW, IconCell / TH, FLinearColor::White, BLEND_Translucent);
	}
	if (!bDecorations) return;
	if (Slot.MaxDamage > 0 && Slot.Damage > 0)
	{
		// ItemRenderer.renderGuiItemDecorations durability bar.
		const float Frac = FMath::Clamp(1.f - (float)Slot.Damage / Slot.MaxDamage, 0.f, 1.f);
		DrawRect(FLinearColor::Black, X + 2 * S, Y + 13 * S, 13 * S, 2 * S);
		DrawRect(FLinearColor::MakeFromHSV8((uint8)(Frac / 3.f * 255.f), 255, 255), X + 2 * S, Y + 13 * S, FMath::RoundToFloat(13 * Frac) * S, S);
	}
	if (Slot.Count > 1)
	{
		const FString C = FString::FromInt(Slot.Count);
		McText(C, X + (19 - 2 - McWidth(C)) * S, Y + (6 + 3) * S, FLinearColor::White, S);
		++Drawn.CountLabels;
	}
}

void ACrbHUD::DrawOutline()
{
	ACrbHost* H = Host();
	const FCrbState& St = H->GetState();
	if (!St.bHasHit || H->IsMenuOpen() || H->Weapon != ECrbWeapon::Hand) return;
	FVector C[8];
	for (int32 I = 0; I < 8; ++I)
	{
		const double X = St.Hit.X + ((I & 1) ? 1.002 : -0.002), Y = St.Hit.Y + ((I & 2) ? 1.002 : -0.002), Z = St.Hit.Z + ((I & 4) ? 1.002 : -0.002);
		C[I] = Project(H->Coords.ToUE(X, Y, Z));
		if (C[I].Z <= 0) return; // behind the camera
	}
	static const int32 E[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
	for (auto& L : E) DrawLine(C[L[0]].X, C[L[0]].Y, C[L[1]].X, C[L[1]].Y, FLinearColor(0, 0, 0, 0.45f), 2.f);
	Drawn.bOutline = true;
}

bool ACrbHUD::InventorySlotCenter(int32 MenuSlot, FVector2D& Out) const
{
	FVector2D P;
	if (InvScale <= 0 || !SlotPos(MenuSlot, P)) return false;
	Out = InvOrigin + (P + FVector2D(8, 8)) * InvScale;
	return true;
}

int32 ACrbHUD::InventorySlotAt(const FVector2D& M) const
{
	if (InvScale <= 0) return -1;
	for (int32 Slot = 0; Slot <= 45; ++Slot)
	{
		FVector2D P; SlotPos(Slot, P);
		const FVector2D Min = InvOrigin + (P - FVector2D(1, 1)) * InvScale;
		if (M.X >= Min.X && M.Y >= Min.Y && M.X < Min.X + 18 * InvScale && M.Y < Min.Y + 18 * InvScale) return Slot;
	}
	return -1;
}

void ACrbHUD::DrawInventory(float S)
{
	ACrbHost* H = Host();
	const FCrbState& St = H->GetState();
	UTexture2D* Panel = H->Textures.Get(TEXT("minecraft:textures/gui/container/inventory.png"));
	const float W = Canvas->ClipX, Hh = Canvas->ClipY;
	DrawRect(FLinearColor(0.006f, 0.006f, 0.006f, 0.78f), 0, 0, W, Hh); // Screen.renderBackground
	InvScale = S;
	InvOrigin = FVector2D(FMath::FloorToFloat(W / 2 - 88 * S), FMath::FloorToFloat(Hh / 2 - 83 * S));
	if (Panel) Sprite(Panel, InvOrigin.X, InvOrigin.Y, 0, 0, 176, 166, S);
	else DrawRect(FLinearColor(0.78f, 0.78f, 0.78f, 1), InvOrigin.X, InvOrigin.Y, 176 * S, 166 * S);
	McText(TEXT("Crafting"), InvOrigin.X + 97 * S, InvOrigin.Y + 8 * S, FLinearColor(FColor(0x40, 0x40, 0x40)), S, false);
	float MX = 0, MY = 0;
	APlayerController* PC = GetOwningPlayerController();
	if (PC) PC->GetMousePosition(MX, MY);
	const int32 Hover = InventorySlotAt(FVector2D(MX, MY));
	for (int32 Slot = 0; Slot <= 45; ++Slot)
	{
		FVector2D P; SlotPos(Slot, P);
		const float X = InvOrigin.X + P.X * S, Y = InvOrigin.Y + P.Y * S;
		const int32 Icon = IconFor(Slot);
		if (St.Slots.IsValidIndex(Icon) && !St.Slots[Icon].Id.IsEmpty()) { ItemIcon(Icon, X, Y, S, true); ++Drawn.InventoryIcons; }
		if (Slot == Hover) DrawRect(FLinearColor(1, 1, 1, 0.5f), X, Y, 16 * S, 16 * S);
	}
	// Cursor (carried) stack follows the mouse; tooltip for hovered stack.
	if (St.Slots.IsValidIndex(41) && !St.Slots[41].Id.IsEmpty()) ItemIcon(41, MX - 8 * S, MY - 8 * S, S, true);
	else if (Hover >= 0 && St.Slots.IsValidIndex(IconFor(Hover)) && !St.Slots[IconFor(Hover)].Id.IsEmpty())
	{
		const FString& Name = St.Slots[IconFor(Hover)].Name;
		const float TW = McWidth(Name) * S;
		const float TX = MX + 12 * S, TY = MY - 12 * S;
		DrawRect(FLinearColor(0.063f, 0.0f, 0.063f, 0.94f), TX - 3 * S, TY - 3 * S, TW + 6 * S, 14 * S);
		McText(Name, TX, TY, FLinearColor::White, S);
	}
	Drawn.bInventory = true;
}

// ---------------- creative inventory (vanilla CreativeModeInventoryScreen layout) ----------------

namespace
{
	const TCHAR* CreativeTabsTex = TEXT("minecraft:textures/gui/container/creative_inventory/tabs.png");
	constexpr float CW = 195, CH = 136;
}

bool ACrbHUD::CreativeLayout(FVector2D& Origin, float& S) const
{
	if (LastS <= 0) return false;
	S = LastS;
	Origin = FVector2D(FMath::FloorToFloat((LastW / S - CW) / 2) * S, FMath::FloorToFloat((LastH / S - CH) / 2) * S);
	return true;
}

int32 ACrbHUD::CreativeTabX(int32 TabIndex) const
{
	ACrbHost* H = const_cast<ACrbHUD*>(this)->Host();
	if (!H || !H->CreativeTabs.IsValidIndex(TabIndex)) return 0;
	const ACrbHost::FCreativeTab& T = H->CreativeTabs[TabIndex];
	return T.bRight ? (int32)CW - 27 * (7 - T.Col) + 1 : 27 * T.Col;
}

bool ACrbHUD::CreativePlayerSlotPos(bool bInventoryTab, int32 Slot, FVector2D& P)
{
	if (Slot >= 36 && Slot <= 44) { P = FVector2D(9 + (Slot - 36) * 18, 112); return true; }
	if (!bInventoryTab) return false;
	if (Slot >= 5 && Slot <= 8) { const int32 N = Slot - 5; P = FVector2D(54 + (N / 2) * 54, 6 + (N % 2) * 27); return true; }
	if (Slot == 45) { P = FVector2D(35, 20); return true; }
	if (Slot >= 9 && Slot <= 35) { const int32 N = Slot - 9; P = FVector2D(9 + (N % 9) * 18, 54 + (N / 9) * 18); return true; }
	return false;
}

FCrbCreativeHit ACrbHUD::CreativeHitAt(const FVector2D& M) const
{
	FCrbCreativeHit Hit;
	ACrbHost* H = const_cast<ACrbHUD*>(this)->Host();
	FVector2D O; float S;
	if (!H || !CreativeLayout(O, S)) return Hit;
	const FVector2D G = (M - O) / S; // GUI pixels relative to the panel
	for (const ACrbHost::FCreativeTab& T : H->CreativeTabs)
	{
		const float TX = CreativeTabX(T.Index), TY = T.Row == 0 ? -32.f : CH;
		if (G.X >= TX && G.X <= TX + 26 && G.Y >= TY && G.Y <= TY + 32) { Hit.Kind = ECrbCreativeHit::Tab; Hit.Index = T.Index; return Hit; }
	}
	const bool bInv = H->IsCreativeInventoryTab();
	for (int32 Slot = 5; Slot <= 45; ++Slot)
	{
		FVector2D P;
		if (CreativePlayerSlotPos(bInv, Slot, P) && G.X >= P.X - 1 && G.Y >= P.Y - 1 && G.X < P.X + 17 && G.Y < P.Y + 17) { Hit.Kind = ECrbCreativeHit::Slot; Hit.Index = Slot; return Hit; }
	}
	if (bInv)
	{
		if (G.X >= 172 && G.Y >= 111 && G.X < 190 && G.Y < 129) { Hit.Kind = ECrbCreativeHit::Destroy; return Hit; }
		return Hit;
	}
	if (G.X >= 9 && G.Y >= 18 && G.X < 9 + 9 * 18 && G.Y < 18 + 5 * 18)
	{
		Hit.Kind = ECrbCreativeHit::Grid; Hit.Index = (int32)((G.Y - 18) / 18) * 9 + (int32)((G.X - 9) / 18); return Hit;
	}
	if (G.X >= 175 && G.X < 187 && G.Y >= 18 && G.Y < 130)
	{
		Hit.Kind = ECrbCreativeHit::Scroll; Hit.ScrollFrac = FMath::Clamp((G.Y - 18 - 7.5f) / (112 - 15), 0.f, 1.f); return Hit;
	}
	return Hit;
}

bool ACrbHUD::CreativeTabCenter(int32 TabIndex, FVector2D& Out) const
{
	ACrbHost* H = const_cast<ACrbHUD*>(this)->Host();
	FVector2D O; float S;
	if (!H || !H->CreativeTabs.IsValidIndex(TabIndex) || !CreativeLayout(O, S)) return false;
	const float TY = H->CreativeTabs[TabIndex].Row == 0 ? -32.f : CH;
	Out = O + FVector2D(CreativeTabX(TabIndex) + 13, TY + 16) * S;
	return true;
}

bool ACrbHUD::CreativeCellCenter(int32 Cell, FVector2D& Out) const
{
	FVector2D O; float S;
	if (Cell < 0 || Cell >= 45 || !CreativeLayout(O, S)) return false;
	Out = O + FVector2D(9 + (Cell % 9) * 18 + 8, 18 + (Cell / 9) * 18 + 8) * S;
	return true;
}

bool ACrbHUD::CreativeSlotCenter(int32 MenuSlot, FVector2D& Out) const
{
	ACrbHost* H = const_cast<ACrbHUD*>(this)->Host();
	FVector2D O, P; float S;
	if (!H || !CreativeLayout(O, S) || !CreativePlayerSlotPos(H->IsCreativeInventoryTab(), MenuSlot, P)) return false;
	Out = O + (P + FVector2D(8, 8)) * S;
	return true;
}

bool ACrbHUD::CreativeDestroyCenter(FVector2D& Out) const
{
	FVector2D O; float S;
	if (!CreativeLayout(O, S)) return false;
	Out = O + FVector2D(173 + 8, 112 + 8) * S;
	return true;
}

void ACrbHUD::SheetIcon(UTexture2D* Sheet, int32 Index, float X, float Y, float S)
{
	if (!Sheet) return;
	const float TW = Sheet->GetSizeX(), TH = Sheet->GetSizeY();
	if (CrbHudTexOk(Sheet)) DrawTexture(Sheet, X, Y, 16 * S, 16 * S, (Index % 16) * 32 / TW, (Index / 16) * 32 / TH, 32 / TW, 32 / TH, FLinearColor::White, BLEND_Translucent);
}

void ACrbHUD::DrawCreative(float S)
{
	ACrbHost* H = Host();
	const FCrbState& St = H->GetState();
	const float W = Canvas->ClipX, Hh = Canvas->ClipY;
	DrawRect(FLinearColor(0.006f, 0.006f, 0.006f, 0.78f), 0, 0, W, Hh);
	FVector2D O; float GS;
	if (!CreativeLayout(O, GS)) return;
	InvScale = 0; // survival slot geometry does not apply
	Drawn.bCreative = true;
	UTexture2D* TabsTex = H->Textures.Get(CreativeTabsTex);
	UTexture2D* TabIcons = H->Textures.Get(TEXT("crb:creative_tabs"));
	UTexture2D* PageIcons = H->Textures.Get(TEXT("crb:creative_page"));
	if (H->CreativeTabs.Num() == 0) { McText(TEXT("Loading creative tabs from Minecraft..."), O.X, O.Y, FLinearColor::White, S); return; }
	const int32 Sel = H->CreativeTab;
	const ACrbHost::FCreativeTab* SelTab = H->CreativeTabs.IsValidIndex(Sel) ? &H->CreativeTabs[Sel] : nullptr;
	auto DrawTab = [&](const ACrbHost::FCreativeTab& T, bool bSel)
	{
		const bool bTop = T.Row == 0;
		const float X = O.X + CreativeTabX(T.Index) * S, Y = bTop ? O.Y - 28 * S : O.Y + (CH - 4) * S;
		if (TabsTex) Sprite(TabsTex, X, Y, T.Col * 26, (bSel ? 32 : 0) + (bTop ? 0 : 64), 26, 32, S);
		else DrawRect(bSel ? FLinearColor(0.78f, 0.78f, 0.78f) : FLinearColor(0.5f, 0.5f, 0.5f), X, Y, 26 * S, 32 * S);
		SheetIcon(TabIcons, T.Index, X + 5 * S, Y + (8 + (bTop ? 1 : -1)) * S, S);
		++Drawn.CreativeTabsDrawn;
	};
	for (const ACrbHost::FCreativeTab& T : H->CreativeTabs) if (T.Index != Sel) DrawTab(T, false);
	UTexture2D* Bg = SelTab ? H->Textures.Get(TEXT("minecraft:textures/gui/container/creative_inventory/tab_") + SelTab->Bg) : nullptr;
	if (Bg) Sprite(Bg, O.X, O.Y, 0, 0, CW, CH, S);
	else DrawRect(FLinearColor(0.78f, 0.78f, 0.78f, 1), O.X, O.Y, CW * S, CH * S);
	const bool bInv = H->IsCreativeInventoryTab();
	if (SelTab && SelTab->bScroll && !bInv && TabsTex)
	{
		const float Frac = H->CreativeMaxRow > 0 ? (float)H->CreativeRow / H->CreativeMaxRow : 0.f;
		Sprite(TabsTex, O.X + 175 * S, O.Y + (18 + FMath::FloorToFloat((112 - 17) * Frac)) * S, 232 + (H->CreativeMaxRow > 0 ? 0 : 12), 0, 12, 15, S);
	}
	if (SelTab) DrawTab(*SelTab, true);
	if (SelTab && SelTab->Type == TEXT("search"))
	{
		const bool bBlink = FMath::Fmod(FPlatformTime::Seconds(), 0.6) < 0.3;
		McText(H->CreativeQuery + (bBlink ? TEXT("_") : TEXT("")), O.X + 82 * S, O.Y + 6 * S, FLinearColor::White, S);
	}
	else if (SelTab && SelTab->bShowTitle) McText(SelTab->Name, O.X + 8 * S, O.Y + 6 * S, FLinearColor(FColor(0x40, 0x40, 0x40)), S, false);
	Drawn.CreativeTitle = SelTab ? SelTab->Name : FString();
	float MX = 0, MY = 0;
	if (APlayerController* PC = GetOwningPlayerController()) PC->GetMousePosition(MX, MY);
	const FCrbCreativeHit Hover = CreativeHitAt(FVector2D(MX, MY));
	FString Tip;
	if (!bInv)
		for (int32 K = 0; K < H->CreativeItems.Num() && K < 45; ++K)
		{
			const float X = O.X + (9 + (K % 9) * 18) * S, Y = O.Y + (18 + (K / 9) * 18) * S;
			if (!H->CreativeItems[K].Id.IsEmpty()) { SheetIcon(PageIcons, K, X, Y, S); ++Drawn.CreativeIcons; }
			if (Hover.Kind == ECrbCreativeHit::Grid && Hover.Index == K) { DrawRect(FLinearColor(1, 1, 1, 0.5f), X, Y, 16 * S, 16 * S); Tip = H->CreativeItems[K].Name; }
		}
	for (int32 Slot = 5; Slot <= 45; ++Slot)
	{
		FVector2D P;
		if (!CreativePlayerSlotPos(bInv, Slot, P)) continue;
		const float X = O.X + P.X * S, Y = O.Y + P.Y * S;
		int32 Icon = -1;
		if (Slot >= 36 && Slot <= 44) Icon = Slot - 36; else if (Slot >= 5 && Slot <= 8) Icon = 13 - (Slot - 5); else if (Slot >= 9 && Slot <= 35) Icon = 14 + Slot - 9; else if (Slot == 45) Icon = 9;
		if (St.Slots.IsValidIndex(Icon) && !St.Slots[Icon].Id.IsEmpty()) { ItemIcon(Icon, X, Y, S, true); if (Hover.Kind == ECrbCreativeHit::Slot && Hover.Index == Slot) Tip = St.Slots[Icon].Name; }
		if (Hover.Kind == ECrbCreativeHit::Slot && Hover.Index == Slot) DrawRect(FLinearColor(1, 1, 1, 0.5f), X, Y, 16 * S, 16 * S);
	}
	if (Hover.Kind == ECrbCreativeHit::Tab && H->CreativeTabs.IsValidIndex(Hover.Index)) Tip = H->CreativeTabs[Hover.Index].Name;
	if (Hover.Kind == ECrbCreativeHit::Destroy) Tip = TEXT("Destroy Item");
	if (St.Slots.IsValidIndex(41) && !St.Slots[41].Id.IsEmpty()) ItemIcon(41, MX - 8 * S, MY - 8 * S, S, true);
	else if (!Tip.IsEmpty())
	{
		const float TW = McWidth(Tip) * S, TX = MX + 12 * S, TY = MY - 12 * S;
		DrawRect(FLinearColor(0.063f, 0.0f, 0.063f, 0.94f), TX - 3 * S, TY - 3 * S, TW + 6 * S, 14 * S);
		McText(Tip, TX, TY, FLinearColor::White, S);
	}
	Drawn.bInventory = true;
}

// ---------------- chat + title ----------------

void ACrbHUD::DrawChat(float S)
{
	ACrbHost* H = Host();
	const FCrbState& St = H->GetState();
	const float Hh = Canvas->ClipY;
	float Bottom = Hh / S - 40; // ChatComponent: lines stack up from (screen height - 40) GUI px
	for (const FCrbState::FChatLine& L : St.Chat)
	{
		if (L.Age >= 200) continue;
		float A = FMath::Clamp((1.f - L.Age / 200.f) * 10.f, 0.f, 1.f); A *= A;
		// Wrap to the 320 px chat width (newest message at the bottom).
		TArray<FString> Lines; FString Cur;
		TArray<FString> Words; L.Text.ParseIntoArray(Words, TEXT(" "));
		for (const FString& Wd : Words)
		{
			const FString Try = Cur.IsEmpty() ? Wd : Cur + TEXT(" ") + Wd;
			if (McWidth(Try) > 320 && !Cur.IsEmpty()) { Lines.Add(Cur); Cur = Wd; } else Cur = Try;
		}
		if (!Cur.IsEmpty()) Lines.Add(Cur);
		for (int32 I = Lines.Num() - 1; I >= 0; --I)
		{
			DrawRect(FLinearColor(0, 0, 0, 0.5f * A), 0, (Bottom - 9) * S, 328 * S, 9 * S);
			McText(Lines[I], 4 * S, (Bottom - 8) * S, FLinearColor(1, 1, 1, A), S);
			Bottom -= 9; ++Drawn.ChatLines;
		}
		if (Bottom < 20) break;
	}
}

void ACrbHUD::DrawTitle()
{
	ACrbHost* H = Host();
	const FCrbState& St = H->GetState();
	if (St.TitleAlpha <= 0.f) return;
	UTexture2D* T = H->Textures.Get(TEXT("crb:title"));
	if (!T) return;
	// Java drew the title exactly like Gui.render into a GUI-sized target; it covers the whole viewport.
	if (CrbHudTexOk(T)) DrawTexture(T, 0, 0, Canvas->ClipX, Canvas->ClipY, 0, 0, 1, 1, FLinearColor(1, 1, 1, St.TitleAlpha), BLEND_Translucent);
	Drawn.bTitle = true;
}

void ACrbHUD::DrawHUD()
{
	Super::DrawHUD();
	ACrbHost* H = Host();
	if (!H || !Canvas) return;
	const FCrbState& St = H->GetState();
	const float W = Canvas->ClipX, Hh = Canvas->ClipY;
	// Vanilla "auto" GUI scale: largest integer scale that keeps 320x240 GUI pixels on screen (max 4).
	const float S = FMath::Clamp(FMath::FloorToFloat(FMath::Min(W / 320.f, Hh / 240.f)), 1.f, 4.f);
	Drawn = FCrbHudDrawn(); Drawn.Scale = S; Drawn.Time = FPlatformTime::Seconds();
	LastW = W; LastH = Hh; LastS = S;
	if (bFontReady && H->Textures.Get(TEXT("minecraft:textures/font/ascii.png")) != Font) bFontReady = false;   // reset after a reconnect
	if (!bFontReady) BuildFont();
	Drawn.bUsedMinecraftFont = bFontReady;
	UTexture2D* Icons = H->Textures.Get(TEXT("minecraft:textures/gui/icons.png"));
	UTexture2D* Widgets = H->Textures.Get(TEXT("minecraft:textures/gui/widgets.png"));
	Items = St.IconSheet.IsEmpty() ? nullptr : H->Textures.Get(St.IconSheet);
	IconCell = St.IconCell; IconCols = St.IconCols;
	Drawn.bUsedMinecraftSheets = Icons && Widgets;
	InvScale = 0;

	Drawn.bOutline = H->bOutlineVisible;
	// Custom Avatar add-on and Craft 64 (its own Doom-style HUD) hide the crosshair, hotbar, vitals and held-item name
	const bool bAvatar = H->IsAvatarActive() || (H->IsCraft64Active() && St.C64.bOn) || (H->IsEldenRingStyle() && St.EC.bOn);   // Elden Ring style: its own HUD
	Drawn.bAvatarHud = bAvatar;
	if (!H->IsMenuOpen() && H->IsPhysicsPortalActive() && St.bValid)
	{
		// Physics & Portal reticle (Portal-style): blue left / orange right brackets, filled when that portal exists
		const float CX = FMath::FloorToFloat(W / 2), CY = FMath::FloorToFloat(Hh / 2);
		const FLinearColor Blue(0.15f, 0.55f, 1.f, St.PP.Portals[0].bValid ? 0.95f : 0.4f), Orange(1.f, 0.55f, 0.1f, St.PP.Portals[1].bValid ? 0.95f : 0.4f);
		DrawRect(Blue, CX - 8 * S, CY - 4 * S, 2 * S, 8 * S); DrawRect(Blue, CX - 8 * S, CY - 4 * S, 4 * S, S); DrawRect(Blue, CX - 8 * S, CY + 3 * S, 4 * S, S);
		DrawRect(Orange, CX + 6 * S, CY - 4 * S, 2 * S, 8 * S); DrawRect(Orange, CX + 4 * S, CY - 4 * S, 4 * S, S); DrawRect(Orange, CX + 4 * S, CY + 3 * S, 4 * S, S);
		Drawn.bPPReticle = true;
	}
	if (!H->IsMenuOpen() && H->ViewMode == 0 && !bAvatar && !H->IsSm64Active() && !H->IsPhysicsPortalActive() && !H->IsEldenCombatActive() && Icons && H->Weapon == ECrbWeapon::Hand)
		Sprite(Icons, FMath::FloorToFloat(W / 2 - 7.5f * S), FMath::FloorToFloat(Hh / 2 - 7.5f * S), 0, 0, 15, 15, S, FLinearColor(1, 1, 1, 0.85f)); // crosshair
	DrawTitle();
	if (St.bValid && H->IsCraft64Active() && St.C64.bOn) DrawCraft64(S);
	if (St.bValid && H->IsEldenCombatActive() && St.EC.bOn) DrawEldenCombat(S);
	if (St.bValid && St.Zm.IsActive()) DrawZombies(S);
	if (St.bValid && !H->IsMenuOpen()) DrawChat(S);
	if (St.bValid)
	{
		const bool bSpectator = H->IsSpectator();
		const float CX = FMath::FloorToFloat(W / 2);
		const float HX = CX - 91 * S, HY = Hh - 22 * S;
		Drawn.Hotbar = FVector2D(HX, HY);
		// Spectators have no hotbar or vitals (vanilla Gui: spectator GUI only).
		if (!bSpectator && !bAvatar) { Drawn.bHotbar = true;
		if (Widgets) { Sprite(Widgets, HX, HY, 0, 0, 182, 22, S); Sprite(Widgets, HX - S + St.Selected * 20 * S, HY - S, 0, 22, 24, 22, S); }
		else { DrawRect(FLinearColor(0, 0, 0, 0.5f), HX, HY, 182 * S, 22 * S); DrawRect(FLinearColor(1, 1, 1, 0.6f), HX + St.Selected * 20 * S, HY, 22 * S, 2 * S); }
		}
		if (!bSpectator && !bAvatar && St.Slots.IsValidIndex(9) && !St.Slots[9].Id.IsEmpty() && Widgets) { Sprite(Widgets, HX - 29 * S, Hh - 23 * S, 24, 22, 29, 24, S); ItemIcon(9, HX - 26 * S, Hh - 19 * S, S, true); }
		Drawn.Selected = St.Selected;
		for (int32 I = 0; I < 9 && !bSpectator && !bAvatar; ++I)
			if (St.Slots.IsValidIndex(I) && !St.Slots[I].Id.IsEmpty()) { ItemIcon(I, HX + (3 + I * 20) * S, HY + 3 * S, S, true); ++Drawn.HotbarIcons; }

		// Selected item name (Gui.renderSelectedItemName): shown for 2 s after the held item changes.
		const FString HeldId = St.Slots.IsValidIndex(St.Selected) ? St.Slots[St.Selected].Id : FString();
		if (St.Selected != LastSelected || HeldId != LastItemId) { LastSelected = St.Selected; LastItemId = HeldId; NameShownAt = FPlatformTime::Seconds(); }
		const double Age = FPlatformTime::Seconds() - NameShownAt;
		if (!HeldId.IsEmpty() && Age < 2.0 && !H->IsMenuOpen() && !bSpectator && !bAvatar)
		{
			const FString& Name = St.Slots[St.Selected].Name;
			const float Alpha = FMath::Clamp((2.0f - (float)Age) / 0.5f, 0.f, 1.f);
			McText(Name, FMath::FloorToFloat(CX - McWidth(Name) * S / 2), Hh - 59 * S, FLinearColor(1, 1, 1, Alpha), S);
			Drawn.HeldName = Name;
		}
		const bool bSurvival = (St.GameMode == TEXT("survival") || St.GameMode == TEXT("adventure")) && !bAvatar;
		if (bSurvival)
		{
			const float XY = Hh - 29 * S;
			Drawn.XpBar = FVector2D(HX, XY);
			const int32 Fill = (int32)(St.XpProgress * 183.f);
			Drawn.XpFillPixels = Fill;
			if (Icons) { Sprite(Icons, HX, XY, 0, 64, 182, 5, S); if (Fill > 0) Sprite(Icons, HX, XY, 0, 69, Fill, 5, S); }
			if (St.XpLevel > 0)
			{
				const FString L = FString::FromInt(St.XpLevel);
				const float TX = FMath::FloorToFloat(CX - McWidth(L) * S / 2), TY = Hh - 35 * S;
				for (int32 K = 0; K < 4; ++K) McText(L, TX + (K == 0 ? S : K == 1 ? -S : 0), TY + (K == 2 ? S : K == 3 ? -S : 0), FLinearColor::Black, S, false);
				McText(L, TX, TY, FLinearColor(FColor(0x80, 0xFF, 0x20)), S, false);
				Drawn.XpLevel = St.XpLevel;
			}
			const float RowY = Hh - 39 * S;
			Drawn.Hearts = FVector2D(HX, RowY);
			const int32 HeartSlots = FMath::Min(10, FMath::CeilToInt(St.MaxHealth / 2.f));
			const int32 HP = FMath::CeilToInt(St.Health);
			for (int32 I = 0; I < HeartSlots; ++I)
			{
				const float X = HX + I * 8 * S;
				if (Icons) Sprite(Icons, X, RowY, 16, 0, 9, 9, S);
				++Drawn.HeartContainers;
				if (I * 2 + 1 < HP) { if (Icons) Sprite(Icons, X, RowY, 52, 0, 9, 9, S); ++Drawn.FullHearts; }
				else if (I * 2 + 1 == HP) { if (Icons) Sprite(Icons, X, RowY, 61, 0, 9, 9, S); ++Drawn.HalfHearts; }
			}
			if (St.Armor > 0)
			{
				const float AY = RowY - 10 * S;
				Drawn.Armor = FVector2D(HX, AY);
				for (int32 I = 0; I < 10; ++I)
				{
					const float X = HX + I * 8 * S;
					if (I * 2 + 1 < St.Armor) { if (Icons) Sprite(Icons, X, AY, 34, 9, 9, 9, S); ++Drawn.ArmorFull; }
					else if (I * 2 + 1 == St.Armor) { if (Icons) Sprite(Icons, X, AY, 25, 9, 9, 9, S); ++Drawn.ArmorHalf; }
					else { if (Icons) Sprite(Icons, X, AY, 16, 9, 9, 9, S); ++Drawn.ArmorEmpty; }
				}
			}
			const float FR = CX + 91 * S;
			Drawn.Food = FVector2D(FR, RowY);
			for (int32 I = 0; I < 10; ++I)
			{
				const float X = FR - I * 8 * S - 9 * S;
				if (Icons) Sprite(Icons, X, RowY, 16, 27, 9, 9, S);
				if (I * 2 + 1 < St.Food) { if (Icons) Sprite(Icons, X, RowY, 52, 27, 9, 9, S); ++Drawn.FoodFull; }
				else if (I * 2 + 1 == St.Food) { if (Icons) Sprite(Icons, X, RowY, 61, 27, 9, 9, S); ++Drawn.FoodHalf; }
			}
		}
		if (H->Weapon != ECrbWeapon::Hand && !H->IsMenuOpen())
		{
			const FString Wn = St.bGravityHolding ? TEXT("Gravity Gun - wheel: distance, click: place") : TEXT("Gravity Gun - click a block to grab");
			McText(Wn, FMath::FloorToFloat(CX - McWidth(Wn) * S / 2), Hh - 59 * S, FLinearColor(0.6f, 0.95f, 1.f), S);
		}
		if (bAvatar && !H->IsMenuOpen() && H->IsAvatarActive())
		{
			// God of War Unity port UI: the translucent square reticle (Assets/UI white square, 50 % alpha).
			const float CY = FMath::FloorToFloat(Hh / 2);
			DrawRect(FLinearColor(1, 1, 1, 0.5f), CX - 3 * S, CY - 3 * S, 6 * S, 6 * S);
			Drawn.bReticle = true;
		}
		if (H->bInventoryOpen) { if (H->IsCreative()) DrawCreative(S); else DrawInventory(S); }
	}
	Drawn.EnemyBars = 0;
	for (const FCrbPlayerAvatar::FEnemy& E : H->PlayerAvatar.Enemies)
	{
		if (!E.bLive || !E.Comp || !E.Comp->IsVisible() || E.Health <= 0) continue;
		const FVector BarPos = E.Cur + FVector(0, 0, 235.f);
		if (!PlayerOwner || !PlayerOwner->PlayerCameraManager) continue;
		if (FVector::DotProduct(BarPos - PlayerOwner->PlayerCameraManager->GetCameraLocation(), PlayerOwner->PlayerCameraManager->GetCameraRotation().Vector()) <= 10.f) continue; // behind the camera
		const FVector P = Project(BarPos);
		const float BW = 50 * S / 2, BH = 5 * S / 2;
		DrawRect(FLinearColor(0, 0, 0, 0.6f), P.X - BW / 2 - 1, P.Y - 1, BW + 2, BH + 2);
		DrawRect(FLinearColor(0.85f, 0.1f, 0.1f, 0.95f), P.X - BW / 2, P.Y, BW * FMath::Clamp(E.Health / E.MaxHealth, 0.f, 1.f), BH);
		++Drawn.EnemyBars;
	}
	if (!H->StatusLine.IsEmpty() && (H->bShowDiagnostics || !St.bValid || FPlatformTime::Seconds() - H->LastStateTime > 1.0))
		McText(H->StatusLine, 4, 4, FLinearColor(1, 1, 0.6f), FMath::Max(1.f, S / 2));
	if (H->bShowDiagnostics)
	{
		FCrbConnection* L = H->Link();
		TArray<FString> Lines;
		Lines.Add(H->WelcomeText);
		Lines.Add(FString::Printf(TEXT("XYZ %.3f / %.3f / %.3f  yaw %.1f pitch %.1f  ground=%d  fixture=%s"), St.X, St.Y, St.Z, St.Yaw, St.Pitch, St.bOnGround, *St.Fixture));
		Lines.Add(FString::Printf(TEXT("pose %lld (walk %.2f) | avatar applied %d rx %d rejected %d | fallback surfaces %d"), St.PoseSeq, St.PoseWalkPos, H->Avatar.FramesApplied, H->Avatar.FramesReceived, H->Avatar.Rejected, H->Avatar.FallbackSurfaces));
		Lines.Add(FString::Printf(TEXT("sections %d meshed %d tris %d models %d | textures %d (%lld MB) java failures %lld"), H->World.NumSections(), H->World.NumMeshed(), H->World.TotalTriangles(), H->World.NumModels(), H->Textures.NumReady(), H->Textures.DecodedBytes() >> 20, St.TextureFailures));
		Lines.Add(FString::Printf(TEXT("particles %d draws %d | torch lights %d | frames %lld rej %lld | in %d out %d drop %d | %.1f ms"), H->ParticleCount, H->ParticleDraw, H->ActiveTorchLights, H->FramesHandled, H->FramesRejected, L ? L->FramesIn.GetValue() : 0, L ? L->FramesOut.GetValue() : 0, L ? L->DroppedLowPriority.GetValue() : 0, H->LastFrameMs));
		Lines.Add(FString::Printf(TEXT("input: %s | server ops: %s | %s"), *St.InputThread, *St.ServerOpsThread, *H->BuildId));
		if (!St.TextureLastError.IsEmpty()) Lines.Add(TEXT("java texture: ") + St.TextureLastError);
		const float DS = FMath::Max(1.f, S / 2);
		for (int32 I = 0; I < Lines.Num(); ++I)
		{
			DrawRect(FLinearColor(0.31f, 0.31f, 0.31f, 0.5f), 2, 16 + I * 10 * DS, McWidth(Lines[I]) * DS + 2, 9 * DS);
			McText(Lines[I], 3, 17 + I * 10 * DS, FLinearColor(0.88f, 0.88f, 0.88f), DS, false);
		}
	}
}

// Zombies HUD (Black Ops 2 layout, Minecraft font): round tally bottom-left, points bottom-right above the hotbar,
// perk badges, buy prompt under the crosshair, power-up timers, round / power-up / game-over banners.
void ACrbHUD::DrawZombies(float S)
{
	ACrbHost* H = Host();
	if (!H || !Canvas) return;
	const FCrbZmState& Z = H->GetState().Zm;
	const float W = Canvas->ClipX, Hh = Canvas->ClipY, CX = FMath::FloorToFloat(W / 2);
	Drawn.bZombiesHud = true; Drawn.ZmRound = Z.Round; Drawn.ZmPoints = Z.Points; Drawn.ZmPerks = Z.Perks.Num();
	const FLinearColor Blood(0.62f, 0.04f, 0.02f), Gold(1.f, 0.85f, 0.3f), White = FLinearColor::White;
	const double Now = FPlatformTime::Seconds();

	// Round number: BO2's chalk tally for 1-5, numerals after, pulsing white while a new round starts.
	{
		const float RS = S * 3.f, X = 14 * S, Y = Hh - 40 * S;
		const bool bPulse = Z.Phase == TEXT("BREAK") || Z.Phase == TEXT("PREPARE") || (Z.Phase == TEXT("ROUND") && Z.RoundTick < 80);
		const float P = bPulse ? 0.5f + 0.5f * FMath::Sin((float)(Now * 6.0)) : 0.f;
		const FLinearColor C = FMath::Lerp(Blood, White, P);
		if (Z.Round >= 1 && Z.Round <= 5)
			for (int32 I = 0; I < Z.Round; ++I)
			{
				if (I < 4) DrawRect(C, X + I * 6 * S, Y - 18 * S, 2.5f * S, 26 * S);
				else for (int32 K = 0; K < 26; ++K) DrawRect(C, X - 3 * S + K * 1.0f * S, Y - 18 * S + K * S, 2.5f * S, 1.2f * S); // slash
			}
		else if (Z.Round > 5) McText(FString::FromInt(Z.Round), X, Y - 22 * S, C, RS);
	}
	// Points (+ double points tint), kills under them.
	{
		const FString Pts = FString::FromInt(Z.Points);
		const float PS = S * 1.6f, X = W - 16 * S - McWidth(Pts) * PS, Y = Hh - 64 * S;
		DrawRect(FLinearColor(0, 0, 0, 0.35f), X - 4 * S, Y - 3 * S, McWidth(Pts) * PS + 8 * S, 9 * PS + 4 * S);
		McText(Pts, X, Y, Z.DoublePoints > 0 ? Gold : FLinearColor(1, 0.95f, 0.75f), PS);
		const FString K = FString::Printf(TEXT("Kills %d  Headshots %d"), Z.Kills, Z.Headshots);
		McText(K, W - 16 * S - McWidth(K) * S, Y + 12 * PS, FLinearColor(0.85f, 0.85f, 0.85f), S);
		if (Z.Phase == TEXT("ROUND")) { const FString L = FString::Printf(TEXT("Zombies left %d"), Z.Left); McText(L, W - 16 * S - McWidth(L) * S, Y + 12 * PS + 10 * S, FLinearColor(0.85f, 0.5f, 0.5f), S); }
	}
	// Perk badges (colour of the machine, first letters).
	for (int32 I = 0; I < Z.Perks.Num(); ++I)
	{
		const FString& Pk = Z.Perks[I];
		const FLinearColor C = Pk == TEXT("Iron Gut") ? FLinearColor(0.7f, 0.1f, 0.1f) : Pk == TEXT("Second Wind") ? FLinearColor(0.9f, 0.45f, 0.05f)
			: Pk == TEXT("Quick Hands") ? FLinearColor(0.1f, 0.3f, 0.85f) : FLinearColor(0.35f, 0.8f, 0.15f);
		const float X = 14 * S + I * 20 * S, Y = Hh - 70 * S;
		DrawRect(FLinearColor(0, 0, 0, 0.6f), X - S, Y - S, 18 * S, 18 * S);
		DrawRect(C, X, Y, 16 * S, 16 * S);
		TArray<FString> Words; Pk.ParseIntoArray(Words, TEXT(" "));
		FString Ab; for (const FString& Wd : Words) Ab += Wd.Left(1);
		McText(Ab, X + 8 * S - McWidth(Ab) * S / 2, Y + 4 * S, White, S);
	}
	// Buy / rebuild prompt.
	if (!Z.Prompt.IsEmpty() && !H->IsMenuOpen())
	{
		FString T = Z.Prompt; if (Z.Cost > 0) T += FString::Printf(TEXT(" [Cost: %d]"), Z.Cost);
		Drawn.ZmPrompt = T;
		McText(T, FMath::FloorToFloat(CX - McWidth(T) * S / 2), FMath::FloorToFloat(Hh / 2 + 22 * S), Z.Cost > 0 && !Z.bAfford ? FLinearColor(1, 0.4f, 0.4f) : White, S);
	}
	if (!Z.Crate.IsEmpty()) { const FString T = Z.Crate; McText(T, CX - McWidth(T) * S, Hh / 2 - 40 * S, Gold, S * 2); }
	// Active power-ups.
	{
		float X = CX - 60 * S; const float Y = Hh - 58 * S;
		if (Z.InstaKill > 0) { const FString T = FString::Printf(TEXT("INSTA-KILL %d"), Z.InstaKill); McText(T, X, Y, FLinearColor(0.9f, 0.9f, 0.9f), S); X += (McWidth(T) + 10) * S; }
		if (Z.DoublePoints > 0) { const FString T = FString::Printf(TEXT("DOUBLE POINTS %d"), Z.DoublePoints); McText(T, X, Y, Gold, S); }
	}
	// Banners: server messages (round start / survived / perk / power-up / game over) for 3 s of game time.
	const int64 Age = Z.Tick - Z.MessageTick;
	FString Banner = Age >= 0 && Age < 60 ? Z.Message : FString();
	if (Z.Tick - Z.PowerUpTick >= 0 && Z.Tick - Z.PowerUpTick < 50 && !Z.PowerUp.IsEmpty()) Banner = Z.PowerUp + TEXT("!");
	if (Z.Phase == TEXT("GAME_OVER")) Banner = Z.Message.IsEmpty() ? FString(TEXT("Game Over")) : Z.Message;
	if (!Banner.IsEmpty())
	{
		Drawn.ZmBanner = Banner;
		const float BS = S * (Z.Phase == TEXT("GAME_OVER") ? 3.f : 2.f);
		const float A = Z.Phase == TEXT("GAME_OVER") ? 1.f : FMath::Clamp((60 - (float)Age) / 15.f, 0.f, 1.f);
		if (Z.Phase == TEXT("GAME_OVER")) DrawRect(FLinearColor(0.25f, 0, 0, 0.45f), 0, 0, W, Hh);
		McText(Banner, FMath::FloorToFloat(CX - McWidth(Banner) * BS / 2), FMath::FloorToFloat(Hh * 0.28f), FLinearColor(0.85f, 0.08f, 0.05f, A), BS);
	}
}

// Craft 64 HUD (Doom 64 layout, Minecraft art): the weapon sprite held by Steve's blocky arms at the bottom centre with
// Doom view bob, raise/lower on switch and fire frames; health bottom-left and armor bottom-right in big red Minecraft
// font digits; ammo for the held weapon at the bottom centre with its Minecraft item icon; owned weapon slots; pickup
// messages top-left; red damage / gold pickup screen flashes; a small crosshair.
void ACrbHUD::DrawCraft64(float S)
{
	ACrbHost* H = Host();
	if (!H || !Canvas) return;
	const FCrbC64State& C = H->GetState().C64;
	const float W = Canvas->ClipX, Hh = Canvas->ClipY;
	Drawn.bC64Hud = true; Drawn.C64Health = C.Health; Drawn.C64Armor = C.Armor;
	if (H->IsMenuOpen()) return;
	// screen flashes (Doom: red pain palette, gold bonus palette)
	if (H->C64Hurt > 0) DrawRect(FLinearColor(0.75f, 0.02f, 0.0f, FMath::Min(0.5f, H->C64Hurt)), 0, 0, W, Hh);
	if (H->C64Bonus > 0) DrawRect(FLinearColor(0.85f, 0.7f, 0.15f, FMath::Min(0.3f, H->C64Bonus)), 0, 0, W, Hh);
	if (C.bBerserk) DrawRect(FLinearColor(0.5f, 0.0f, 0.0f, 0.06f), 0, 0, W, Hh);

	// weapon sprite: 128x96 art, scaled to 80 % of the screen height, nearest-filtered Minecraft pixels
	const FString Name = FString::Printf(TEXT("crossover_rebuilt:textures/c64/%s_%d.png"), *H->C64Shown, H->C64Frame);
	UTexture2D* T = H->Textures.Get(Name);
	Drawn.C64Sprite = Name;
	if (T)
	{
		const float P = Hh * 0.8f / 96.f;   // Doom proportions: the gun reaches from the bottom edge to just under the crosshair
		const float Bob = H->C64BobAmp * 9.f * P / 2.f;
		const float BX = FMath::Cos(H->C64BobPhase * 0.5f) * Bob * 1.4f, BY = FMath::Abs(FMath::Sin(H->C64BobPhase * 0.5f)) * Bob;
		const float Drop = (1.f - H->C64Raise) * 96.f * P;
		const float SX = FMath::FloorToFloat(W / 2 - 64.f * P + BX), SY = FMath::FloorToFloat(Hh - 96.f * P + 6.f * P + BY + Drop);
		// darken the sprite slightly with the world light so it sits in the scene (Doom 64 sector light on the weapon)
		const FCrbState& St = H->GetState();
		const FLinearColor L = H->LightAt(St.EyeBlock, St.EyeSky);
		const float Lv = FMath::Clamp(0.35f + 0.75f * FMath::Max3(L.R, L.G, L.B), 0.35f, 1.f);
		const bool bFlash = H->C64Frame > 0 && H->C64SinceFire < 0.15f;   // the muzzle flash lights the gun
		if (CrbHudTexOk(T)) DrawTexture(T, SX, SY, 128.f * P, 96.f * P, 0, 0, 1, 1, bFlash ? FLinearColor::White : FLinearColor(Lv, Lv, Lv, 1), BLEND_Translucent);
		Drawn.bC64SpriteDrawn = true;
	}

	// crosshair (Doom 64 has a small one)
	const float CX = FMath::FloorToFloat(W / 2), CY = FMath::FloorToFloat(Hh / 2);
	DrawRect(FLinearColor(1, 0.85f, 0.85f, 0.75f), CX - S, CY - 3 * S, 2 * S, 2 * S);
	DrawRect(FLinearColor(1, 0.85f, 0.85f, 0.75f), CX - S, CY + S, 2 * S, 2 * S);
	DrawRect(FLinearColor(1, 0.85f, 0.85f, 0.75f), CX - 3 * S, CY - S, 2 * S, 2 * S);
	DrawRect(FLinearColor(1, 0.85f, 0.85f, 0.75f), CX + S, CY - S, 2 * S, 2 * S);

	// status: Doom 64 red numbers (Minecraft font, outlined), labels in small caps
	const FLinearColor Red(0.86f, 0.06f, 0.04f), Dim(0.55f, 0.08f, 0.06f);
	const float Big = S * 3.f, Lbl = S;
	auto Outline = [&](const FString& Txt, float X, float Y, float Sc, FLinearColor Col)
	{
		for (int32 K = 0; K < 4; ++K) McText(Txt, X + (K == 0 ? Sc * 0.5f : K == 1 ? -Sc * 0.5f : 0), Y + (K == 2 ? Sc * 0.5f : K == 3 ? -Sc * 0.5f : 0), FLinearColor(0, 0, 0, 0.85f), Sc, false);
		McText(Txt, X, Y, Col, Sc, false);
	};
	const float Base = Hh - 12.f * S - 8.f * Big;
	UTexture2D* Icons = H->Textures.Get(TEXT("minecraft:textures/gui/icons.png"));
	// health (heart icon from Minecraft's GUI sheet)
	if (Icons) Sprite(Icons, 10 * S, Base + 2 * S, 52, 0, 9, 9, Big / 3.f * 2.f);
	Outline(TEXT("HEALTH"), 10 * S, Base - 10 * Lbl, Lbl, Dim);
	Outline(FString::FromInt(C.Health), 10 * S + 22 * S, Base, Big, Red);
	// armor (chestplate icon) bottom-right; blue armor tinted
	const FString Ar = FString::FromInt(C.Armor);
	const float ArX = W - 10 * S - McWidth(Ar) * Big;
	if (Icons) Sprite(Icons, ArX - 22 * S, Base + 2 * S, 34, 9, 9, 9, Big / 3.f * 2.f, C.ArmorType == 2 ? FLinearColor(0.6f, 0.8f, 1.f) : FLinearColor::White);
	Outline(TEXT("ARMOR"), W - 10 * S - McWidth(TEXT("ARMOR")) * Lbl, Base - 10 * Lbl, Lbl, Dim);
	Outline(Ar, ArX, Base, Big, C.ArmorType == 2 ? FLinearColor(0.25f, 0.45f, 1.f) : Red);
	// ammo of the held weapon (bottom centre) with its Minecraft item icon
	if (!C.AmmoType.IsEmpty() && C.AmmoType != TEXT("none"))
	{
		const int32 A = C.AmmoOf(C.AmmoType); Drawn.C64Ammo = A;
		const FString As = FString::FromInt(A);
		const TCHAR* Icon = C.AmmoType == TEXT("bullets") ? TEXT("minecraft:textures/item/iron_nugget.png") : C.AmmoType == TEXT("shells") ? TEXT("minecraft:textures/item/gold_nugget.png")
			: C.AmmoType == TEXT("rockets") ? TEXT("minecraft:textures/item/firework_rocket.png") : TEXT("minecraft:textures/item/glowstone_dust.png");
		const float AW = McWidth(As) * Big;
		if (UTexture2D* It = H->Textures.Get(Icon)) if (CrbHudTexOk(It)) DrawTexture(It, CX - AW / 2 - 22 * S, Base, 16 * S * 1.25f, 16 * S * 1.25f, 0, 0, 1, 1, FLinearColor::White, BLEND_Translucent);
		Outline(As, CX - AW / 2, Base, Big, A > 0 ? FLinearColor(0.95f, 0.75f, 0.1f) : Dim);
	}
	// weapon slots owned (1..8), the held one bright
	{
		static const TCHAR* C64Order[] = { TEXT("fist"), TEXT("chainsaw"), TEXT("pistol"), TEXT("shotgun"), TEXT("super"), TEXT("chaingun"), TEXT("rocket"), TEXT("plasma"), TEXT("bfg"), TEXT("unmaker") };
		static const int32 C64Slot[] = { 1, 1, 2, 3, 3, 4, 5, 6, 7, 8 };
		float X = W - 10 * S - 8 * 9 * S; const float Y = Base - 22 * S;   // above the armor block
		for (int32 Sl = 1; Sl <= 8; ++Sl)
		{
			bool bHave = false, bHeld = false;
			for (int32 I = 0; I < 10; ++I) if (C64Slot[I] == Sl && C.Owned.Contains(C64Order[I])) { bHave = true; bHeld |= C.Weapon == C64Order[I]; }
			McText(FString::FromInt(Sl), X, Y, bHeld ? FLinearColor(1, 0.9f, 0.3f) : bHave ? FLinearColor(0.8f, 0.8f, 0.8f) : FLinearColor(0.3f, 0.3f, 0.3f, 0.7f), S);
			X += 9 * S;
		}
	}
	// kills (top-right) and the pickup message (top-left, Doom style, 4 s)
	const FString K = FString::Printf(TEXT("KILLS %d"), C.Kills);
	McText(K, W - 6 * S - McWidth(K) * S * 1.5f, 6 * S, FLinearColor(0.85f, 0.15f, 0.1f), S * 1.5f);
	const double Age = FPlatformTime::Seconds() - H->C64MsgTime;
	if (!H->C64Msg.IsEmpty() && Age < 4.0) McText(H->C64Msg, 6 * S, 6 * S, FLinearColor(1, 0.85f, 0.2f, FMath::Clamp((4.f - (float)Age) / 0.7f, 0.f, 1.f)), S * 1.5f);
}

// Minecraft x Elden Combat HUD (only while the mod is on): health / stamina bars with a lagging drain (top-left, Elden
// Ring layout), runes (bottom-right), the lock-on point and the target's health + poise bar, damage numbers, and the
// big event banners (YOU DIED, ENEMY FELLED, CRITICAL, PARRY, GUARD BROKEN). Minecraft font throughout.
void ACrbHUD::DrawEldenCombat(float S)
{
	ACrbHost* H = Host();
	if (!H || !Canvas) return;
	const FCrbECState& E = H->GetState().EC;
	const float W = Canvas->ClipX, Hh = Canvas->ClipY;
	const double Now = FPlatformTime::Seconds();
	Drawn.bECHud = true;
	if (H->IsEldenRingStyle()) { DrawEldenRing(S); return; }
	if (H->ECHurtFlash > 0) DrawRect(FLinearColor(0.55f, 0.02f, 0.0f, FMath::Min(0.35f, H->ECHurtFlash)), 0, 0, W, Hh);
	if (H->IsMenuOpen()) return;
	auto Bar = [&](float X, float Y, float Len, float Th, float Frac, float Lag, FLinearColor Fill, FLinearColor LagC)
	{
		DrawRect(FLinearColor(0.72f, 0.6f, 0.36f, 0.9f), X - S, Y - S, Len + 2 * S, Th + 2 * S);            // gold trim
		DrawRect(FLinearColor(0.02f, 0.02f, 0.02f, 0.85f), X, Y, Len, Th);
		DrawRect(LagC, X, Y, Len * FMath::Clamp(Lag, 0.f, 1.f), Th);
		DrawRect(Fill, X, Y, Len * FMath::Clamp(Frac, 0.f, 1.f), Th);
		DrawRect(FLinearColor(1, 1, 1, 0.12f), X, Y, Len * FMath::Clamp(Frac, 0.f, 1.f), FMath::Max(1.f, Th * 0.3f));
	};
	// ---- player bars: length grows with the maximum (Elden Ring), HP from vanilla health
	const float X0 = 14 * S, Y0 = 12 * S;
	const float HpLen = FMath::Clamp(E.MaxHp * 6.f, 80.f, 260.f) * S, StLen = FMath::Clamp(E.MaxStamina * 1.4f, 80.f, 260.f) * S;
	const float HpFrac = E.MaxHp > 0 ? E.Hp / E.MaxHp : 0, StFrac = E.MaxStamina > 0 ? FMath::Max(0.f, E.Stamina) / E.MaxStamina : 0;
	Bar(X0, Y0, HpLen, 5 * S, HpFrac, H->ECHpLag, FLinearColor(0.62f, 0.07f, 0.05f), FLinearColor(0.9f, 0.75f, 0.35f, 0.9f));
	Bar(X0, Y0 + 9 * S, StLen, 4 * S, StFrac, H->ECStaminaLag, E.Stamina <= 0 ? FLinearColor(0.35f, 0.4f, 0.2f) : FLinearColor(0.22f, 0.6f, 0.2f), FLinearColor(0.85f, 0.85f, 0.5f, 0.8f));
	Drawn.ECHpPx = HpLen * HpFrac; Drawn.ECStaminaPx = StLen * StFrac;
	if (E.Charge > 0) Bar(W / 2 - 30 * S, Hh / 2 + 34 * S, 60 * S, 2 * S, E.Charge, 0, FLinearColor(1.f, 0.85f, 0.4f), FLinearColor::Transparent);
	// ---- runes (bottom-right, above the hotbar line)
	{
		const FString R = FString::Printf(TEXT("%lld"), E.Runes);
		const float Sc = S * 1.5f, RW = McWidth(R) * Sc;
		const float RX = W - 14 * S - RW, RY = Hh - 40 * S;
		DrawRect(FLinearColor(0, 0, 0, 0.45f), RX - 18 * S, RY - 3 * S, RW + 24 * S, 14 * S);
		// rune glyph: a small gold diamond
		const FLinearColor Gold(0.95f, 0.8f, 0.4f);
		for (int32 I = 0; I < 5; ++I) { const float Wd = (5 - 2 * FMath::Abs(I - 2)) * 1.4f * S; DrawRect(Gold, RX - 9 * S - Wd / 2, RY + I * 1.8f * S, Wd, 1.8f * S); }
		McText(R, RX, RY, FLinearColor(0.95f, 0.9f, 0.8f), Sc);
	}
	// ---- lock-on: a white point on the target and its health / poise bar above it
	if (E.Lock.bValid)
	{
		const FVector C = H->Coords.ToUE(E.Lock.X, E.Lock.Y + E.Lock.H * 0.6, E.Lock.Z);
		const FVector P = Project(C);
		if (P.Z > 0)
		{
			Drawn.bECLockDrawn = true;
			const float R = 4.f * S;
			for (int32 K = 0; K < 12; ++K)
			{
				const float A0 = K * PI / 6.f, A1 = (K + 1) * PI / 6.f;
				DrawLine(P.X + FMath::Cos(A0) * R, P.Y + FMath::Sin(A0) * R, P.X + FMath::Cos(A1) * R, P.Y + FMath::Sin(A1) * R, FLinearColor(1, 1, 1, 0.9f), 1.5f * S * 0.5f);
			}
			DrawRect(FLinearColor(1, 1, 1, 0.95f), P.X - S, P.Y - S, 2 * S, 2 * S);
			const FVector Top = Project(H->Coords.ToUE(E.Lock.X, E.Lock.Y + E.Lock.H + 0.5, E.Lock.Z));
			if (Top.Z > 0)
			{
				const float L = 50 * S;
				Bar(Top.X - L / 2, Top.Y, L, 2.5f * S, E.Lock.Max > 0 ? E.Lock.Hp / E.Lock.Max : 0, 0, FLinearColor(0.62f, 0.07f, 0.05f), FLinearColor::Transparent);
				Bar(Top.X - L / 2, Top.Y + 4 * S, L, 1.2f * S, E.Lock.PoiseMax > 0 ? E.Lock.Poise / E.Lock.PoiseMax : 0, 0, E.Lock.bStagger ? FLinearColor(1.f, 0.85f, 0.3f) : FLinearColor(0.75f, 0.75f, 0.8f), FLinearColor::Transparent);
				McText(E.Lock.Name, Top.X - McWidth(E.Lock.Name) * S * 0.5f, Top.Y - 10 * S, FLinearColor(0.95f, 0.92f, 0.85f), S);
			}
		}
	}
	// ---- damage number (yellow, rises and fades over 1.2 s)
	if (Now - H->ECDamageTime < 1.2)
	{
		const float Age = (float)(Now - H->ECDamageTime);
		const FVector P = Project(H->ECDamageAt + FVector(0, 0, Age * 40.f));
		if (P.Z > 0)
		{
			const FString D = FString::Printf(TEXT("%d"), FMath::Max(1, FMath::RoundToInt(H->ECDamageShown)));
			McText(D, P.X - McWidth(D) * S * 0.75f, P.Y, FLinearColor(1.f, 0.85f, 0.3f, FMath::Clamp((1.2f - Age) / 0.4f, 0.f, 1.f)), S * 1.5f);
			++Drawn.ECDamageNumbers;
		}
	}
	// ---- event banner (Elden Ring: a dark band across the middle with large spaced serif-like text)
	const bool bDied = H->GetState().bDead || E.Act == TEXT("DEAD");
	const float Dur = H->ECBanner == TEXT("YOU DIED") ? 5.f : (H->ECBanner == TEXT("ENEMY FELLED") ? 3.f : 1.1f);
	const float Age = (float)(Now - H->ECBannerTime);
	if (!H->ECBanner.IsEmpty() && (Age < Dur || (bDied && H->ECBanner == TEXT("YOU DIED"))))
	{
		const bool bBig = H->ECBanner == TEXT("YOU DIED") || H->ECBanner == TEXT("ENEMY FELLED");
		const float A = bDied && H->ECBanner == TEXT("YOU DIED") ? FMath::Clamp(Age / 0.8f, 0.f, 1.f) : FMath::Clamp(FMath::Min(Age / 0.25f, (Dur - Age) / 0.5f), 0.f, 1.f);
		FString Spaced; for (int32 I = 0; I < H->ECBanner.Len(); ++I) { Spaced.AppendChar(H->ECBanner[I]); if (I + 1 < H->ECBanner.Len()) Spaced.AppendChar(TEXT(' ')); }
		const float Sc = S * (bBig ? 4.f : 2.f);
		const float TW = McWidth(Spaced) * Sc, BY = bBig ? Hh * 0.45f : Hh * 0.3f;
		if (bBig) DrawRect(FLinearColor(0, 0, 0, 0.6f * A), 0, BY - 6 * S, W, 8 * Sc + 12 * S);
		FLinearColor C = H->ECBannerColor; C.A = A;
		McText(Spaced, W / 2 - TW / 2, BY, C, Sc);
		Drawn.ECBanner = H->ECBanner;
	}
	// ---- debug overlay (from the mod's debug menu)
	if (H->bECDebugOverlay)
	{
		const FCrbECAnimDebug* D = H->ECSteve.AnimDebug();
		const FString L = FString::Printf(TEXT("EC %s t%d/%d w%d a%d combo%d | stamina %.0f/%.0f poise %.0f/%.0f | %s%s | clip %s %.2f/%.2f | weapon %s (%d voxels) | lock %d | hits %d/%d dodges %d | mobs %d dummies %d"),
			*E.Act, E.T, E.Len, E.W, E.A, E.Combo, E.Stamina, E.MaxStamina, E.Poise, E.MaxPoise, E.bIFrames ? TEXT("IFRAMES ") : TEXT(""), E.bParry ? TEXT("PARRY") : TEXT(""),
			D ? *D->Clip : TEXT("-"), D ? D->Time : 0.f, D ? D->Length : 0.f, *E.Weapon, H->ECSteve.WeaponVoxels, E.Lock.Id, E.Hits, E.Attacks, E.Dodges, E.MobsTracked, E.Dummies);
		McText(L, 6 * S, Y0 + 20 * S, FLinearColor(0.8f, 1.f, 0.8f), S * 0.75f);
	}
	Drawn.bECWeaponDrawn = H->ECSteve.WeaponVisible();
}

// Elden Ring Combat (Steve) HUD: no Minecraft hotbar / hearts / font. Thin HP and stamina bars top-left with the lagging
// drain, the four-slot equipment cross bottom-left (main hand, offhand shield, quick item), runes bottom-right, a small
// white lock-on dot with the target's bar (a boss bar at the bottom for big enemies), damage numbers and the banners.
void ACrbHUD::DrawEldenRing(float S)
{
	ACrbHost* H = Host();
	if (!H || !Canvas) return;
	const FCrbState& St = H->GetState();
	const FCrbECState& E = St.EC;
	const float W = Canvas->ClipX, Hh = Canvas->ClipY;
	const double Now = FPlatformTime::Seconds();
	const float U = Hh / 720.f;                                  // layout unit: 1 px at 720p
	Drawn.bECRingHud = true;
	UFont* Small = GEngine ? GEngine->GetSmallFont() : nullptr;
	UFont* Large = GEngine ? GEngine->GetLargeFont() : nullptr;
	auto Txt = [&](const FString& T, float X, float Y, FLinearColor C, UFont* F, float Sc, bool bCenter)
	{
		if (!F) return;
		float TW = 0, TH = 0; GetTextSize(T, TW, TH, F, Sc);
		const float PX = bCenter ? X - TW / 2 : X;
		DrawText(T, FLinearColor(0, 0, 0, C.A * 0.7f), PX + 1.5f * U, Y + 1.5f * U, F, Sc);
		DrawText(T, C, PX, Y, F, Sc);
	};
	if (H->ECHurtFlash > 0) DrawRect(FLinearColor(0.4f, 0.0f, 0.0f, FMath::Min(0.25f, H->ECHurtFlash * 0.6f)), 0, 0, W, Hh);
	if (H->IsMenuOpen()) return;
	auto Bar = [&](float X, float Y, float Len, float Th, float Frac, float Lag, FLinearColor Fill)
	{
		DrawRect(FLinearColor(0.55f, 0.47f, 0.3f, 0.75f), X - U, Y - U, Len + 2 * U, Th + 2 * U);
		DrawRect(FLinearColor(0.03f, 0.03f, 0.03f, 0.8f), X, Y, Len, Th);
		DrawRect(FLinearColor(0.85f, 0.72f, 0.42f, 0.85f), X, Y, Len * FMath::Clamp(Lag, 0.f, 1.f), Th);
		DrawRect(Fill, X, Y, Len * FMath::Clamp(Frac, 0.f, 1.f), Th);
		DrawRect(FLinearColor(1, 1, 1, 0.1f), X, Y, Len * FMath::Clamp(Frac, 0.f, 1.f), Th * 0.35f);
	};
	// ---- HP / stamina (lengths grow with the maximum, like levelling Vigor / Endurance)
	const float X0 = 42 * U, Y0 = 34 * U;
	const float HpLen = FMath::Clamp(E.MaxHp * 9.f, 120.f, 380.f) * U, StLen = FMath::Clamp(E.MaxStamina * 2.f, 120.f, 380.f) * U;
	Bar(X0, Y0, HpLen, 7 * U, E.MaxHp > 0 ? E.Hp / E.MaxHp : 0, H->ECHpLag, FLinearColor(0.55f, 0.06f, 0.05f));
	Bar(X0, Y0 + 13 * U, StLen, 6 * U, E.MaxStamina > 0 ? FMath::Max(0.f, E.Stamina) / E.MaxStamina : 0, H->ECStaminaLag, E.Stamina <= 0 ? FLinearColor(0.25f, 0.3f, 0.18f) : FLinearColor(0.2f, 0.52f, 0.2f));
	Drawn.ECHpPx = HpLen; Drawn.ECStaminaPx = StLen;
	if (E.Charge > 0) Bar(W / 2 - 50 * U, Hh * 0.62f, 100 * U, 3 * U, E.Charge, 0, FLinearColor(0.95f, 0.82f, 0.5f));
	// ---- equipment cross (bottom-left): up = (spells, empty), left = offhand, right = main hand, down = quick item
	{
		const float C = 58 * U, Box = 40 * U, Gap = 44 * U, CX = 70 * U, CY = Hh - 100 * U;
		auto Slot = [&](float X, float Y, int32 SlotIndex)
		{
			DrawRect(FLinearColor(0, 0, 0, 0.45f), X - Box / 2, Y - Box / 2, Box, Box);
			DrawRect(FLinearColor(0.6f, 0.52f, 0.36f, 0.6f), X - Box / 2, Y - Box / 2, Box, U); DrawRect(FLinearColor(0.6f, 0.52f, 0.36f, 0.6f), X - Box / 2, Y + Box / 2 - U, Box, U);
			if (SlotIndex >= 0) ItemIcon(SlotIndex, X - 16 * U, Y - 16 * U, 2.f * U, false);
		};
		(void)C;
		Slot(CX, CY - Gap, -1);                     // spells (none)
		Slot(CX - Gap, CY, 9);                      // offhand (shield)
		Slot(CX + Gap, CY, St.Selected);            // right hand weapon
		Slot(CX, CY + Gap, (St.Selected + 1) % 9);  // quick item: the next hotbar slot
		if (St.Slots.IsValidIndex(St.Selected) && !St.Slots[St.Selected].Name.IsEmpty()) Txt(St.Slots[St.Selected].Name, CX + Gap + Box * 0.7f, CY - 10 * U, FLinearColor(0.92f, 0.88f, 0.78f, 0.9f), Small, U * 1.6f, false);
	}
	// ---- runes (bottom-right)
	{
		const FString R = FString::Printf(TEXT("%lld"), E.Runes);
		const float RX = W - 60 * U, RY = Hh - 52 * U;
		DrawRect(FLinearColor(0, 0, 0, 0.4f), RX - 120 * U, RY - 4 * U, 150 * U, 26 * U);
		for (int32 I = 0; I < 5; ++I) { const float Wd = (5 - 2 * FMath::Abs(I - 2)) * 2.2f * U; DrawRect(FLinearColor(0.9f, 0.78f, 0.42f), RX - 105 * U - Wd / 2, RY + I * 3.6f * U, Wd, 3.6f * U); }
		Txt(R, RX - 40 * U, RY - 4 * U, FLinearColor(0.95f, 0.92f, 0.84f), Large, U * 1.3f, false);
	}
	// ---- lock-on: a small white dot; the target's bar above it, or a boss bar for big enemies
	if (E.Lock.bValid)
	{
		const FVector P = Project(H->Coords.ToUE(E.Lock.X, E.Lock.Y + E.Lock.H * 0.6, E.Lock.Z));
		if (P.Z > 0)
		{
			Drawn.bECLockDrawn = true;
			DrawRect(FLinearColor(1, 1, 1, 0.95f), P.X - 2.5f * U, P.Y - 2.5f * U, 5 * U, 5 * U);
			DrawRect(FLinearColor(1, 1, 1, 0.35f), P.X - 4 * U, P.Y - 4 * U, 8 * U, 8 * U);
		}
		const float Frac = E.Lock.Max > 0 ? E.Lock.Hp / E.Lock.Max : 0;
		if (E.Lock.Max >= 60.f)
		{
			Drawn.bECBossBar = true;
			const float L = W * 0.55f, BX = W / 2 - L / 2, BY = Hh - 92 * U;
			Txt(E.Lock.Name, BX, BY - 34 * U, FLinearColor(0.93f, 0.9f, 0.82f), Large, U * 1.4f, false);
			Bar(BX, BY, L, 8 * U, Frac, Frac, FLinearColor(0.55f, 0.06f, 0.05f));
		}
		else
		{
			const FVector Top = Project(H->Coords.ToUE(E.Lock.X, E.Lock.Y + E.Lock.H + 0.4, E.Lock.Z));
			if (Top.Z > 0) Bar(Top.X - 30 * U, Top.Y, 60 * U, 3 * U, Frac, Frac, FLinearColor(0.55f, 0.06f, 0.05f));
		}
	}
	// ---- damage number (next to the target, white, 1 s)
	if (Now - H->ECDamageTime < 1.0)
	{
		const float Age = (float)(Now - H->ECDamageTime);
		const FVector P = Project(H->ECDamageAt + FVector(0, 30.f, 10.f));
		if (P.Z > 0) { Txt(FString::FromInt(FMath::Max(1, FMath::RoundToInt(H->ECDamageShown))), P.X + 30 * U, P.Y, FLinearColor(1, 1, 1, FMath::Clamp((1.f - Age) / 0.3f, 0.f, 1.f)), Small, U * 1.9f, false); ++Drawn.ECDamageNumbers; }
	}
	// ---- banners
	const bool bDied = St.bDead || E.Act == TEXT("DEAD");
	const FString& B = H->ECBanner;
	const bool bBig = B == TEXT("YOU DIED") || B == TEXT("ENEMY FELLED");
	const float Dur = B == TEXT("YOU DIED") ? 6.f : (B == TEXT("ENEMY FELLED") ? 3.5f : 1.0f);
	const float Age = (float)(Now - H->ECBannerTime);
	if (!B.IsEmpty() && (Age < Dur || (bDied && B == TEXT("YOU DIED"))))
	{
		const float A = (bDied && B == TEXT("YOU DIED")) ? FMath::Clamp(Age / 1.2f, 0.f, 1.f) : FMath::Clamp(FMath::Min(Age / 0.35f, (Dur - Age) / 0.6f), 0.f, 1.f);
		FString Spaced; for (int32 I = 0; I < B.Len(); ++I) { Spaced.AppendChar(B[I]); if (I + 1 < B.Len()) Spaced.AppendChar(TEXT(' ')); }
		if (bBig)
		{
			const float BY = Hh * 0.44f;
			for (int32 K = 0; K < 12; ++K) DrawRect(FLinearColor(0, 0, 0, 0.62f * A * (1.f - FMath::Abs(K - 5.5f) / 6.f)), 0, BY - 30 * U + K * 9 * U, W, 9 * U);
			FLinearColor C = H->ECBannerColor; C.A = A;
			Txt(Spaced, W / 2, BY, C, Large, U * 3.4f, true);
		}
		else { FLinearColor C = H->ECBannerColor; C.A = A; Txt(Spaced, W / 2, Hh * 0.3f, C, Large, U * 1.7f, true); }
		Drawn.ECBanner = B;
	}
	if (H->bECDebugOverlay)
	{
		const FCrbECAnimDebug* D = H->ECSteve.AnimDebug();
		Txt(FString::Printf(TEXT("ELDEN RING STYLE  %s t%d/%d w%d a%d combo%d | stamina %.0f/%.0f poise %.0f/%.0f | %s%s | clip %s %.2f/%.2f | weapon %s (%d tris) trail %d | lock %d | hits %d/%d dodges %d"),
			*E.Act, E.T, E.Len, E.W, E.A, E.Combo, E.Stamina, E.MaxStamina, E.Poise, E.MaxPoise, E.bIFrames ? TEXT("IFRAMES ") : TEXT(""), E.bParry ? TEXT("PARRY") : TEXT(""),
			D ? *D->Clip : TEXT("-"), D ? D->Time : 0.f, D ? D->Length : 0.f, *E.Weapon, H->ECSteve.WeaponTris, H->ECSteve.TrailQuads, E.Lock.Id, E.Hits, E.Attacks, E.Dodges),
			X0, Y0 + 30 * U, FLinearColor(0.8f, 1.f, 0.8f), Small, U * 1.3f, false);
	}
	Drawn.bECWeaponDrawn = H->ECSteve.WeaponVisible();
}
