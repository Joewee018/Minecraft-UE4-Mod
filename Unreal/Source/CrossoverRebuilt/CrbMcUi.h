// Minecraft-textured Slate pieces for the Unreal menus: the vanilla button (widgets.png, normal / hover / disabled,
// drawn as left + right halves like AbstractButton), the bitmap font (font/ascii.png, vanilla glyph widths and drop
// shadow) and the dirt list background (options_background.png). Every texture is read from the running Minecraft
// client at runtime (never bundled). Sizes are in Minecraft GUI pixels times the vanilla "auto" GUI scale.
#pragma once
#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Styling/SlateBrush.h"

class ACrbHost;
class UTexture2D;

namespace CrbMcUi
{
	/** Index of a character in Minecraft's ascii.png glyph sheet (0..255), or -1 when the sheet has no such glyph.
	 *  Rows 0-7 are ASCII; rows 8-10 hold Minecraft's own accented / symbol set (e.g. U+00D7 x-sign at 0x9E). */
	inline int32 Glyph(TCHAR Ch)
	{
		if (Ch < 128) return Ch;
		static const TCHAR* High = TEXT("\u00c7\u00fc\u00e9\u00e2\u00e4\u00e0\u00e5\u00e7\u00ea\u00eb\u00e8\u00ef\u00ee\u00ec\u00c4\u00c5")
			TEXT("\u00c9\u00e6\u00c6\u00f4\u00f6\u00f2\u00fb\u00f9\u00ff\u00d6\u00dc\u00f8\u00a3\u00d8\u00d7\u0192")
			TEXT("\u00e1\u00ed\u00f3\u00fa\u00f1\u00d1\u00aa\u00ba\u00bf\u00ae\u00ac\u00bd\u00bc\u00a1\u00ab\u00bb");
		for (int32 I = 0; High[I]; ++I) if (High[I] == Ch) return 128 + I;
		return -1;
	}
	void Update(ACrbHost* Host);            // pick up the runtime textures (call every frame)
	bool Ready();                           // font + widgets received
	float GuiScale();                       // vanilla auto GUI scale for the current viewport (1..4)
	float Unit();                           // Slate units per GUI pixel (GUI scale / viewport DPI scale)
	float TextWidth(const FString& S);      // GUI pixels
	const FSlateBrush* Background();        // tiled dirt, or null
	UTexture2D* WidgetsTexture();
	// Vanilla colours.
	inline FLinearColor Rgb(uint32 C) { return FLinearColor(FColor((C >> 16) & 255, (C >> 8) & 255, C & 255)); }
}

/** Bitmap-font text with vanilla shadow; optional word wrap at a GUI-pixel width. */
class SCrbMcText : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCrbMcText) : _Color(FLinearColor::White), _Scale(1.f), _WrapAt(0.f), _bCentered(false), _bShadow(true) {}
		SLATE_ATTRIBUTE(FString, Text)
		SLATE_ATTRIBUTE(FLinearColor, Color)
		SLATE_ARGUMENT(float, Scale)       // multiple of the GUI scale (titles use 1, big banners 2)
		SLATE_ARGUMENT(float, WrapAt)      // GUI pixels, 0 = no wrap
		SLATE_ARGUMENT(bool, bCentered)
		SLATE_ARGUMENT(bool, bShadow)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override;
private:
	void Lines(TArray<FString>& Out) const;
	TAttribute<FString> Text; TAttribute<FLinearColor> Color; float Scale = 1, WrapAt = 0; bool bCentered = false, bShadow = true;
};

/** The vanilla 200x20 button face, stretched to any width as two halves (or capped tiles when wider than 400). */
class SCrbMcButtonFace : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCrbMcButtonFace) : _Width(200.f) {}
		SLATE_ATTRIBUTE(bool, Hovered)
		SLATE_ATTRIBUTE(bool, Enabled)
		SLATE_ARGUMENT(float, Width)       // GUI pixels
	SLATE_END_ARGS()
	void Construct(const FArguments& Args) { Hovered = Args._Hovered; Enabled = Args._Enabled; Width = Args._Width; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override;
private:
	TAttribute<bool> Hovered, Enabled; float Width = 200;
	mutable TArray<FSlateBrush> Pieces;
};
