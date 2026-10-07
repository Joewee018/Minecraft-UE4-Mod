// Minecraft-layout HUD drawn from Java-owned state with the client's own GUI sheets and bitmap font
// (icons.png, widgets.png, container/inventory.png, font/ascii.png - read from the running Minecraft at runtime,
// never bundled) and Java-rendered item icons. Positions follow vanilla Gui/InventoryScreen in GUI pixels x scale.
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CrbHUD.generated.h"

class ACrbHost;
class UTexture2D;

enum class ECrbCreativeHit : uint8 { None, Tab, Grid, Slot, Destroy, Scroll };
struct FCrbCreativeHit { ECrbCreativeHit Kind = ECrbCreativeHit::None; int32 Index = -1; float ScrollFrac = 0; };

struct FCrbHudDrawn
{
	int32 FullHearts = 0, HalfHearts = 0, HeartContainers = 0;
	int32 ArmorFull = 0, ArmorHalf = 0, ArmorEmpty = 0;
	int32 FoodFull = 0, FoodHalf = 0;
	float XpFillPixels = 0; int32 XpLevel = -1;
	int32 HotbarIcons = 0; int32 Selected = -1; int32 CountLabels = 0;
	bool bUsedMinecraftSheets = false, bUsedMinecraftFont = false, bOutline = false, bInventory = false;
	int32 InventoryIcons = 0; FString HeldName;
	bool bAvatarHud = false, bReticle = false; int32 EnemyBars = 0;
	bool bPPReticle = false; bool bC64Hud = false; FString C64Sprite; int32 C64Health = -1, C64Armor = -1, C64Ammo = -1; bool bC64SpriteDrawn = false;
	bool bECHud = false, bECRingHud = false, bECBossBar = false, bECLockDrawn = false, bECWeaponDrawn = false; float ECStaminaPx = 0, ECHpPx = 0; FString ECBanner; int32 ECDamageNumbers = 0;
	bool bZombiesHud = false; int32 ZmRound = -1, ZmPoints = -1, ZmPerks = 0; FString ZmPrompt, ZmBanner;
	bool bCreative = false, bHotbar = false, bTitle = false; int32 CreativeTabsDrawn = 0, CreativeIcons = 0, ChatLines = 0; FString CreativeTitle;
	float Scale = 1; FVector2D Hotbar, Hearts, Armor, Food, XpBar;
	double Time = 0;
};

UCLASS()
class ACrbHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;
	int32 InventorySlotAt(const FVector2D& Screen) const;
	bool InventorySlotCenter(int32 MenuSlot, FVector2D& Out) const;
	// Creative inventory geometry (vanilla CreativeModeInventoryScreen, 195x136 panel at GUI scale).
	FCrbCreativeHit CreativeHitAt(const FVector2D& Screen) const;
	bool CreativeTabCenter(int32 TabIndex, FVector2D& Out) const;
	bool CreativeCellCenter(int32 Cell, FVector2D& Out) const;
	bool CreativeSlotCenter(int32 MenuSlot, FVector2D& Out) const;
	bool CreativeDestroyCenter(FVector2D& Out) const;
	FCrbHudDrawn Drawn;
private:
	ACrbHost* Host();
	TWeakObjectPtr<ACrbHost> CachedHost;
	void Sprite(UTexture2D* T, float X, float Y, float U, float V, float W, float H, float S, FLinearColor Tint = FLinearColor::White);
	float McText(const FString& S, float X, float Y, FLinearColor C, float Scale, bool bShadow = true);
	float McWidth(const FString& S) const;
	void ItemIcon(int32 IconIndex, float X, float Y, float S, bool bDecorations);
	void DrawOutline();
	void DrawInventory(float S);
	void DrawCreative(float S);
	void DrawChat(float S);
	void DrawTitle();
	void DrawZombies(float S);
	void DrawCraft64(float S);
	void DrawEldenCombat(float S);
	void DrawEldenRing(float S);
	void SheetIcon(UTexture2D* Sheet, int32 Index, float X, float Y, float S);
	bool CreativeLayout(FVector2D& Origin, float& S) const;
	static bool CreativePlayerSlotPos(bool bInventoryTab, int32 Slot, FVector2D& P);
	int32 CreativeTabX(int32 TabIndex) const;
	float LastW = 0, LastH = 0, LastS = 0;
	void BuildFont();
	UTexture2D* Font = nullptr; UTexture2D* Items = nullptr; int32 FontWidths[256]; bool bFontReady = false;
	int32 IconCell = 32, IconCols = 16;
	FVector2D InvOrigin = FVector2D::ZeroVector; float InvScale = 0;
	int32 LastSelected = -1; FString LastItemId; double NameShownAt = 0;
};
