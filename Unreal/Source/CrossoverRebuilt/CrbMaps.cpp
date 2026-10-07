// Maps menu: Esc > Maps... lists the default superflat test world and every custom map in the project's Maps folder
// (Java crb.client.Maps scans it). Picking one asks Java to leave the current world and open the map with world
// generation turned off; the bridge connection stays up and Unreal re-streams the new world's sections.
#include "CrbHost.h"
#include "CrbMenus.h"
#include "Dom/JsonObject.h"

void ACrbHost::OpenMapsMenu()
{
	CloseMenus();
	bPauseOpen = true; // a page of the pause menu: Esc closes it
	bMapListReceived = false;
	CrbMenus::OpenMapsMenu(this);
	SendCommand(TEXT("map.list"));
	SendInput(true);
}

void ACrbHost::OnMapList(const TSharedPtr<FJsonObject>& J)
{
	if (!J.IsValid()) return;
	MapList.Reset();
	J->TryGetStringField(TEXT("folder"), MapsFolder);
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (J->TryGetArrayField(TEXT("maps"), Arr))
		for (int32 I = 0; I < Arr->Num() && I < 64; ++I)
		{
			const TSharedPtr<FJsonObject> O = (*Arr)[I]->AsObject();
			if (!O.IsValid()) continue;
			FMapInfo M; O->TryGetStringField(TEXT("id"), M.Id); O->TryGetStringField(TEXT("name"), M.Name); O->TryGetStringField(TEXT("source"), M.Source); O->TryGetBoolField(TEXT("imported"), M.bImported);
			M.Name = M.Name.Left(60);
			MapList.Add(M);
		}
	bMapListReceived = true;
	CrbMenus::RefreshMaps(this);
}

void ACrbHost::LoadMap(const FString& Id)
{
	if (IsZombiesActive()) SendCommand(TEXT("zm.stop")); // the match belongs to the world being left
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("id"), Id);
	SendCommand(TEXT("map.load"), A);
	StatusLine = TEXT("Loading map ") + Id + TEXT("...");
}
