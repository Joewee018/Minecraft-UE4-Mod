#include "CrbMcUi.h"
#include "CrbHost.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/UserInterfaceSettings.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
	const TCHAR* FontName = TEXT("minecraft:textures/font/ascii.png");
	const TCHAR* WidgetsName = TEXT("minecraft:textures/gui/widgets.png");
	const TCHAR* BackgroundName = TEXT("minecraft:textures/gui/options_background.png");
	UTexture2D* FontTex = nullptr; UTexture2D* WidgetsTex = nullptr; UTexture2D* BgTex = nullptr;
	int32 Widths[256]; bool bWidths = false;
	FSlateBrush Glyphs[256];
	FSlateBrush BgBrush;
	float CachedScale = 2.f, CachedUnit = 2.f;

	void BuildGlyphs()
	{
		for (int32 G = 0; G < 256; ++G)
		{
			FSlateBrush& B = Glyphs[G];
			B = FSlateBrush();
			B.SetResourceObject(FontTex);
			B.ImageSize = FVector2D(8, 8);
			B.DrawAs = ESlateBrushDrawType::Image;
			const float U = (G % 16) * 8 / 128.f, V = (G / 16) * 8 / 128.f;
			B.SetUVRegion(FBox2D(FVector2D(U, V), FVector2D(U + 8 / 128.f, V + 8 / 128.f)));
		}
	}
}

namespace CrbMcUi
{
	void Update(ACrbHost* Host)
	{
		if (!Host) return;
		Host->Textures.KeepPixels(FontName);
		UTexture2D* F = Host->Textures.Get(FontName);
		if (F != FontTex)
		{
			FontTex = F; bWidths = false;
			if (FontTex) BuildGlyphs();
		}
		if (FontTex && !bWidths)
		{
			const TArray<uint8>* Px = Host->Textures.Pixels(FontName);
			if (Px && Px->Num() == 128 * 128 * 4 && FontTex->GetSizeX() == 128)
			{
				// Vanilla glyph width: rightmost non-transparent column + 1; space advances 4.
				for (int32 G = 0; G < 256; ++G)
				{
					int32 W = 0;
					for (int32 X = 7; X >= 0 && W == 0; --X)
						for (int32 Y = 0; Y < 8; ++Y)
							if ((*Px)[(((G / 16) * 8 + Y) * 128 + (G % 16) * 8 + X) * 4 + 3] > 0) { W = X + 1; break; }
					Widths[G] = W;
				}
				Widths[32] = 3;
				bWidths = true;
			}
		}
		WidgetsTex = Host->Textures.Get(WidgetsName);
		UTexture2D* Bg = Host->Textures.Get(BackgroundName);
		// GUI scale exactly like the HUD (vanilla "auto": largest integer keeping 320x240 GUI pixels on screen, max 4).
		FVector2D VP(1280, 720);
		if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(VP);
		if (VP.X > 0 && VP.Y > 0)
		{
			CachedScale = FMath::Clamp(FMath::FloorToFloat(FMath::Min(VP.X / 320.f, VP.Y / 240.f)), 1.f, 4.f);
			const float Dpi = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint((int32)VP.X, (int32)VP.Y));
			CachedUnit = CachedScale / FMath::Max(0.1f, Dpi);
		}
		if (Bg != BgTex || (Bg && !FMath::IsNearlyEqual(BgBrush.ImageSize.X, 32.f * CachedUnit)))
		{
			BgTex = Bg;
			BgBrush = FSlateBrush();
			if (BgTex)
			{
				BgBrush.SetResourceObject(BgTex);
				BgBrush.ImageSize = FVector2D(32.f * CachedUnit, 32.f * CachedUnit);
				BgBrush.DrawAs = ESlateBrushDrawType::Image;
				BgBrush.Tiling = ESlateBrushTileType::Both;
				BgBrush.TintColor = FSlateColor(Rgb(0x404040)); // vanilla renderDirtBackground tint
			}
		}
	}

	bool Ready() { return FontTex && bWidths && WidgetsTex; }
	float GuiScale() { return CachedScale; }
	float Unit() { return CachedUnit; }
	UTexture2D* WidgetsTexture() { return WidgetsTex; }
	const FSlateBrush* Background() { return BgTex ? &BgBrush : nullptr; }

	float TextWidth(const FString& S)
	{
		float W = 0;
		for (TCHAR Ch : S) { const int32 Gi = Glyph(Ch); if (Gi >= 0) W += (bWidths ? Widths[Gi] : 5) + 1; }
		return W > 0 ? W - 1 : 0;
	}
}

// ---------------------------------------------------------------- text

void SCrbMcText::Construct(const FArguments& Args)
{
	Text = Args._Text; Color = Args._Color; Scale = Args._Scale; WrapAt = Args._WrapAt; bCentered = Args._bCentered; bShadow = Args._bShadow;
}

void SCrbMcText::Lines(TArray<FString>& Out) const
{
	const FString All = Text.Get();
	if (WrapAt <= 0) { Out.Add(All); return; }
	TArray<FString> Words; All.ParseIntoArray(Words, TEXT(" "), false);
	FString Line;
	for (const FString& W : Words)
	{
		const FString Try = Line.IsEmpty() ? W : Line + TEXT(" ") + W;
		if (!Line.IsEmpty() && CrbMcUi::TextWidth(Try) * Scale > WrapAt) { Out.Add(Line); Line = W; }
		else Line = Try;
	}
	if (!Line.IsEmpty() || Out.Num() == 0) Out.Add(Line);
}

FVector2D SCrbMcText::ComputeDesiredSize(float) const
{
	TArray<FString> L; Lines(L);
	float W = 0; for (const FString& S : L) W = FMath::Max(W, CrbMcUi::TextWidth(S));
	const float U = CrbMcUi::Unit() * Scale;
	return FVector2D((WrapAt > 0 ? FMath::Min(W * Scale, WrapAt) / Scale : W) * U + U, L.Num() * 9.f * U + U);
}

int32 SCrbMcText::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const float U = CrbMcUi::Unit() * Scale;
	TArray<FString> L; Lines(L);
	const FLinearColor C = Color.Get() * Style.GetColorAndOpacityTint();
	const FLinearColor Sh(C.R * 0.25f, C.G * 0.25f, C.B * 0.25f, C.A);
	const bool bReady = CrbMcUi::Ready();
	for (int32 Li = 0; Li < L.Num(); ++Li)
	{
		const FString& S = L[Li];
		float X = bCentered ? FMath::FloorToFloat((G.GetLocalSize().X - CrbMcUi::TextWidth(S) * U) / 2) : 0.f;
		const float Y = Li * 9.f * U;
		if (!bReady)
		{
			FSlateDrawElement::MakeText(Out, Layer, G.ToPaintGeometry(FVector2D(X, Y), FVector2D(G.GetLocalSize().X, 9 * U)), S, FCoreStyle::GetDefaultFontStyle("Bold", FMath::Max(8, (int32)(6 * U))), ESlateDrawEffect::None, C);
			continue;
		}
		for (int32 Pass = bShadow ? 0 : 1; Pass < 2; ++Pass)
		{
			float PX = X + (Pass == 0 ? U : 0.f);
			const float PY = Y + (Pass == 0 ? U : 0.f);
			for (TCHAR Ch0 : S)
			{
				const int32 Ch = CrbMcUi::Glyph(Ch0);
				if (Ch < 0) continue;
				if (Ch != 32) FSlateDrawElement::MakeBox(Out, Layer + Pass, G.ToPaintGeometry(FVector2D(PX, PY), FVector2D(8 * U, 8 * U)), &Glyphs[Ch], ESlateDrawEffect::None, Pass == 0 ? Sh : C);
				PX += (Widths[Ch] + 1) * U;
			}
		}
	}
	return Layer + 2;
}

// ---------------------------------------------------------------- button face

FVector2D SCrbMcButtonFace::ComputeDesiredSize(float) const { return FVector2D(Width, 20.f) * CrbMcUi::Unit(); }

int32 SCrbMcButtonFace::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	UTexture2D* Tex = CrbMcUi::WidgetsTexture();
	const FVector2D Size = G.GetLocalSize();
	if (!Tex)
	{
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(), FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Hovered.Get(false) ? FLinearColor(0.45f, 0.5f, 0.75f) : FLinearColor(0.35f, 0.35f, 0.35f));
		return Layer + 1;
	}
	// AbstractButton: v = 46 disabled, 66 normal, 86 hovered; the face is 200x20 in a 256x256 sheet.
	const float V = !Enabled.Get(true) ? 46.f : (Hovered.Get(false) ? 86.f : 66.f);
	const float U = CrbMcUi::Unit();
	const float W = Size.X / U; // GUI pixels
	struct FPiece { float U0, U1, X0; };
	TArray<FPiece> P;
	if (W <= 400.f)
	{
		const float Half = FMath::FloorToFloat(W / 2);
		P.Add({ 0.f, Half, 0.f });
		P.Add({ 200.f - (W - Half), 200.f, Half });
	}
	else
	{
		// Wider than the sheet: left cap, tiled middle, right cap (same edges as the vanilla halves).
		P.Add({ 0.f, 4.f, 0.f });
		float X = 4.f;
		while (X < W - 4.f) { const float Seg = FMath::Min(192.f, W - 4.f - X); P.Add({ 4.f, 4.f + Seg, X }); X += Seg; }
		P.Add({ 196.f, 200.f, W - 4.f });
	}
	Pieces.SetNum(P.Num());
	for (int32 I = 0; I < P.Num(); ++I)
	{
		FSlateBrush& B = Pieces[I];
		B = FSlateBrush();
		B.SetResourceObject(Tex);
		B.DrawAs = ESlateBrushDrawType::Image;
		B.ImageSize = FVector2D(P[I].U1 - P[I].U0, 20.f);
		B.SetUVRegion(FBox2D(FVector2D(P[I].U0 / 256.f, V / 256.f), FVector2D(P[I].U1 / 256.f, (V + 20.f) / 256.f)));
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2D(P[I].X0 * U, 0), FVector2D((P[I].U1 - P[I].U0) * U, Size.Y)), &B, ESlateDrawEffect::None, Style.GetColorAndOpacityTint());
	}
	return Layer + 1;
}
