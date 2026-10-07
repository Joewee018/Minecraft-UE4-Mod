// Presentation-only pawn: owns the camera and forwards input to the host. It never simulates movement or collision;
// Minecraft's Java player is authoritative and the pawn is placed at its interpolated eye position.
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "CrbPawn.generated.h"

class UCameraComponent;
class ACrbHost;

UCLASS()
class ACrbPawn : public APawn
{
	GENERATED_BODY()
public:
	ACrbPawn();
	virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
	virtual void Tick(float DeltaSeconds) override;
	void Present(const FVector& Eye, const FRotator& View, int32 ViewMode, ACrbHost* Host, const FTransform& Bob = FTransform::Identity);

	void PresentOrbit(const FVector& Pivot, const FRotator& View, float Distance);
	UPROPERTY(VisibleAnywhere) USceneComponent* Head;
	UPROPERTY(VisibleAnywhere) UCameraComponent* Camera;
	UPROPERTY(EditAnywhere) float MouseSensitivity = 0.6f;
	float CameraDistance = 0;

private:
	ACrbHost* GetHost();
	TWeakObjectPtr<ACrbHost> CachedHost;
	float Forward = 0, Strafe = 0;
	bool bJump = false, bSneak = false, bSprint = false;
	void OnForward(float V) { Forward = V; }
	void OnRight(float V) { Strafe = V; }
	void OnYaw(float V);
	void OnPitch(float V);
	void OnWheel(float V);
	void JumpDown() { bJump = true; } void JumpUp() { bJump = false; }
	void SneakDown() { bSneak = true; } void SneakUp() { bSneak = false; }
	void SprintDown() { bSprint = true; } void SprintUp() { bSprint = false; }
	void PrimaryDown(); void PrimaryUp(); void SecondaryDown(); void SecondaryUp();
	template<int32 N> void Slot();
	void Inventory();
	void Recall();
	void InteractDown(); void InteractUp();
	void View(); void Mods(); void Debug(); void Lighting(); void Diagnostics(); void Escape();
	void OnAnyKey(FKey Key);
};
