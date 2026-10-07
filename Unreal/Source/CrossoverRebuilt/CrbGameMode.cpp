#include "CrbGameMode.h"
#include "CrbPawn.h"
#include "CrbHost.h"
#include "CrbHUD.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

ACrbGameMode::ACrbGameMode()
{
	DefaultPawnClass = ACrbPawn::StaticClass();
	HUDClass = ACrbHUD::StaticClass();
}

void ACrbGameMode::StartPlay()
{
	Super::StartPlay();
	FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GetWorld()->SpawnActor<ACrbHost>(ACrbHost::StaticClass(), FTransform::Identity, P);
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
}
