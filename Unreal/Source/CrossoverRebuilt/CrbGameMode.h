#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CrbGameMode.generated.h"

UCLASS()
class ACrbGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ACrbGameMode();
	virtual void StartPlay() override;
};
