// Mouse-driven Slate menus: the M mod menu and the debug-test picker. Opening a menu shows the cursor and gives
// Slate focus; closing restores game-only input, hides the cursor and returns focus to the game viewport.
#pragma once
#include "CoreMinimal.h"

class ACrbHost;
class SWidget;

namespace CrbMenus
{
	void OpenModMenu(ACrbHost* Host);
	void OpenDebugMenu(ACrbHost* Host);
	void OpenSm64Menu(ACrbHost* Host);
	void OpenECMenu(ACrbHost* Host);   // Minecraft x Elden Combat controls + debug tools (only while the mod is on)
	void Close(ACrbHost* Host);
	void Refresh(ACrbHost* Host);
	void OpenPauseMenu(ACrbHost* Host);
	void OpenGameModesMenu(ACrbHost* Host);
	void OpenMapsMenu(ACrbHost* Host);
	void RefreshMaps(ACrbHost* Host);
	void OpenDeathMenu(ACrbHost* Host);
	void OpenHerobrineMenu(ACrbHost* Host);
	void RefreshHerobrine(ACrbHost* Host);
	// Test support: screen-space centre of a named row ("fixture:<id>", "mod:<id>", "resume", "diagnostics").
	bool RowCenter(const FString& Key, FVector2D& OutScreen);
	TArray<FString> RowKeys();
	// Test support: deliver a left-button press/release to the row button through SButton's own mouse handlers. Unlike an
	// OS-level synthesized click this works even when another application's window covers the game window.
	bool ClickRow(const FString& Key);
	bool IsOpen();
}
