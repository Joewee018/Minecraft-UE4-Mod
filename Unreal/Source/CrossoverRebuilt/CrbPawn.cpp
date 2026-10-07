#include "CrbPawn.h"
#include "CrbHost.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"

ACrbPawn::ACrbPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	Head = CreateDefaultSubobject<USceneComponent>(TEXT("Head"));
	RootComponent = Head;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Head);
	Camera->bUsePawnControlRotation = false;
	Camera->SetFieldOfView(100.f);
	AutoPossessPlayer = EAutoReceiveInput::Player0;
}

template<int32 N> void ACrbPawn::Slot() { if (ACrbHost* H = GetHost()) if (!H->IsCreativeSearch()) H->SelectSlot(N); }

ACrbHost* ACrbPawn::GetHost()
{
	if (!CachedHost.IsValid()) for (TActorIterator<ACrbHost> It(GetWorld()); It; ++It) { CachedHost = *It; break; }
	return CachedHost.Get();
}

void ACrbPawn::SetupPlayerInputComponent(UInputComponent* In)
{
	Super::SetupPlayerInputComponent(In);
	In->BindAxis(TEXT("Forward"), this, &ACrbPawn::OnForward);
	In->BindAxis(TEXT("Right"), this, &ACrbPawn::OnRight);
	In->BindAxis(TEXT("LookYaw"), this, &ACrbPawn::OnYaw);
	In->BindAxis(TEXT("LookPitch"), this, &ACrbPawn::OnPitch);
	In->BindAxis(TEXT("MouseWheel"), this, &ACrbPawn::OnWheel);
	In->BindAction(TEXT("Jump"), IE_Pressed, this, &ACrbPawn::JumpDown);
	In->BindAction(TEXT("Jump"), IE_Released, this, &ACrbPawn::JumpUp);
	In->BindAction(TEXT("Sneak"), IE_Pressed, this, &ACrbPawn::SneakDown);
	In->BindAction(TEXT("Sneak"), IE_Released, this, &ACrbPawn::SneakUp);
	In->BindAction(TEXT("Sprint"), IE_Pressed, this, &ACrbPawn::SprintDown);
	In->BindAction(TEXT("Sprint"), IE_Released, this, &ACrbPawn::SprintUp);
	In->BindAction(TEXT("Primary"), IE_Pressed, this, &ACrbPawn::PrimaryDown);
	In->BindAction(TEXT("Primary"), IE_Released, this, &ACrbPawn::PrimaryUp);
	In->BindAction(TEXT("Secondary"), IE_Pressed, this, &ACrbPawn::SecondaryDown);
	In->BindAction(TEXT("Secondary"), IE_Released, this, &ACrbPawn::SecondaryUp);
	In->BindAction(TEXT("Slot1"), IE_Pressed, this, &ACrbPawn::Slot<0>);
	In->BindAction(TEXT("Slot2"), IE_Pressed, this, &ACrbPawn::Slot<1>);
	In->BindAction(TEXT("Slot3"), IE_Pressed, this, &ACrbPawn::Slot<2>);
	In->BindAction(TEXT("Slot4"), IE_Pressed, this, &ACrbPawn::Slot<3>);
	In->BindAction(TEXT("Slot5"), IE_Pressed, this, &ACrbPawn::Slot<4>);
	In->BindAction(TEXT("Slot6"), IE_Pressed, this, &ACrbPawn::Slot<5>);
	In->BindAction(TEXT("Slot7"), IE_Pressed, this, &ACrbPawn::Slot<6>);
	In->BindAction(TEXT("Slot8"), IE_Pressed, this, &ACrbPawn::Slot<7>);
	In->BindAction(TEXT("Slot9"), IE_Pressed, this, &ACrbPawn::Slot<8>);
	In->BindAction(TEXT("View"), IE_Pressed, this, &ACrbPawn::View);
	In->BindAction(TEXT("Inventory"), IE_Pressed, this, &ACrbPawn::Inventory);
	In->BindAction(TEXT("Mods"), IE_Pressed, this, &ACrbPawn::Mods);
	In->BindAction(TEXT("Debug"), IE_Pressed, this, &ACrbPawn::Debug);
	In->BindAction(TEXT("Lighting"), IE_Pressed, this, &ACrbPawn::Lighting);
	In->BindAction(TEXT("Diagnostics"), IE_Pressed, this, &ACrbPawn::Diagnostics);
	In->BindAction(TEXT("Escape"), IE_Pressed, this, &ACrbPawn::Escape);
	In->BindAction(TEXT("Recall"), IE_Pressed, this, &ACrbPawn::Recall);
	In->BindAction(TEXT("Interact"), IE_Pressed, this, &ACrbPawn::InteractDown);
	In->BindAction(TEXT("Interact"), IE_Released, this, &ACrbPawn::InteractUp);
	// Text entry for the creative search tab (letters, digits, space, backspace).
	FInputKeyBinding AnyKey(FInputChord(EKeys::AnyKey), IE_Pressed);
	AnyKey.bConsumeInput = false;
	AnyKey.KeyDelegate.GetDelegateWithKeyForManualSet().BindUObject(this, &ACrbPawn::OnAnyKey);
	In->KeyBindings.Add(AnyKey);
}

void ACrbPawn::OnYaw(float V) { if (ACrbHost* H = GetHost()) H->Look(V * MouseSensitivity, 0); }
void ACrbPawn::OnPitch(float V) { if (ACrbHost* H = GetHost()) H->Look(0, V * MouseSensitivity); }
void ACrbPawn::OnWheel(float V) { if (V != 0) if (ACrbHost* H = GetHost()) H->ScrollWheel(V); }
void ACrbPawn::PrimaryDown() { if (ACrbHost* H = GetHost()) H->PrimaryPressed(true); }
void ACrbPawn::PrimaryUp() { if (ACrbHost* H = GetHost()) H->PrimaryPressed(false); }
void ACrbPawn::SecondaryDown() { if (ACrbHost* H = GetHost()) H->SecondaryPressed(true); }
void ACrbPawn::SecondaryUp() { if (ACrbHost* H = GetHost()) H->SecondaryPressed(false); }
void ACrbPawn::Inventory() { if (ACrbHost* H = GetHost()) { if (H->IsCreativeSearch()) return; /* typed into search */ H->ToggleInventory(); } }

void ACrbPawn::OnAnyKey(FKey Key)
{
	ACrbHost* H = GetHost();
	if (!H || !H->IsCreativeSearch()) return;
	if (Key == EKeys::BackSpace) { H->CreativeBackspace(); return; }
	if (Key == EKeys::SpaceBar) { H->CreativeType(TEXT(" ")); return; }
	const FString Name = Key.GetFName().ToString();
	if (Name.Len() == 1 && FChar::IsAlpha(Name[0])) { H->CreativeType(Name.ToLower()); return; }
	static const TCHAR* Digits[] = { TEXT("Zero"), TEXT("One"), TEXT("Two"), TEXT("Three"), TEXT("Four"), TEXT("Five"), TEXT("Six"), TEXT("Seven"), TEXT("Eight"), TEXT("Nine") };
	for (int32 D = 0; D < 10; ++D) if (Name == Digits[D]) { H->CreativeType(FString::FromInt(D)); return; }
	if (Key == EKeys::Underscore || Key == EKeys::Hyphen) H->CreativeType(TEXT("_"));
}
void ACrbPawn::InteractDown() { if (ACrbHost* H = GetHost()) if (!H->IsCreativeSearch()) H->InteractPressed(true); }
void ACrbPawn::InteractUp() { if (ACrbHost* H = GetHost()) H->InteractPressed(false); }
void ACrbPawn::Recall() { if (ACrbHost* H = GetHost()) if (!H->IsCreativeSearch() && !H->IsMenuOpen() && !H->IsEldenCombatActive()) { if (H->IsZombiesActive()) H->SendCommand(TEXT("zm.reload")); else H->AvatarRecall(); } }
void ACrbPawn::View() { if (ACrbHost* H = GetHost()) H->CycleView(); }
void ACrbPawn::Mods() { if (ACrbHost* H = GetHost()) H->ToggleModMenu(); }
void ACrbPawn::Debug() { if (ACrbHost* H = GetHost()) H->ToggleDebugMenu(); }
void ACrbPawn::Lighting() { if (ACrbHost* H = GetHost()) H->ToggleLighting(); }
void ACrbPawn::Diagnostics() { if (ACrbHost* H = GetHost()) H->ToggleDiagnostics(); }
void ACrbPawn::Escape()
{
	// Vanilla: Esc closes the open screen; otherwise it opens the pause menu (never quits directly).
	ACrbHost* H = GetHost();
	if (!H || H->bDeathOpen) return;
	if (H->IsMenuOpen()) { H->CloseMenus(); return; }
	H->TogglePause();
}

void ACrbPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (ACrbHost* H = GetHost())
	{
		if (H->bInputOverride) { /* scripted test owns movement */ }
		else
		{
		H->SetMove(Forward, Strafe);
		H->SetButtons(bJump, bSneak, bSprint);
		}
	}
	// Minecraft's 70 degree vertical FOV, expressed as UE's horizontal FOV for the current aspect ratio.
	if (GEngine && GEngine->GameViewport)
	{
		FVector2D Size; GEngine->GameViewport->GetViewportSize(Size);
		if (Size.Y > 0) Camera->SetFieldOfView(FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(35.f)) * Size.X / Size.Y)));
	}
}

void ACrbPawn::Present(const FVector& Eye, const FRotator& View, int32 ViewMode, ACrbHost* Host, const FTransform& Bob)
{
	SetActorLocation(Eye);
	FRotator CamRot = View;
	FVector Offset = FVector::ZeroVector;
	if (ViewMode != 0)
	{
		// Third-person camera, pulled in against copied Java block data (a ray march; no Unreal collision).
		const FVector Dir = ViewMode == 1 ? -View.Vector() : View.Vector();
		float Dist = 400.f;
		double EX, EY, EZ; Host->Coords.ToMC(Eye, EX, EY, EZ);
		for (float D = 20.f; D <= 400.f; D += 10.f)
		{
			double X, Y, Z; Host->Coords.ToMC(Eye + Dir * D, X, Y, Z);
			if (Host->World.IsSolidAt(FIntVector(FMath::FloorToInt(X), FMath::FloorToInt(Y), FMath::FloorToInt(Z)))) { Dist = FMath::Max(20.f, D - 25.f); break; }
		}
		Offset = Dir * Dist;
		if (ViewMode == 2) CamRot = FRotator(-View.Pitch, View.Yaw + 180.f, 0);
	}
	CameraDistance = Offset.Size();
	// First person: vanilla view bobbing. The world sees the camera moved by Bob^-1 and the hands (camera children,
	// relative transform = Bob) end up exactly where vanilla's bobbed hand pose puts them on screen.
	const FTransform Base(CamRot, Eye + Offset);
	Camera->SetWorldTransform(ViewMode == 0 ? Bob.Inverse() * Base : Base);
}

void ACrbPawn::PresentOrbit(const FVector& Pivot, const FRotator& View, float Distance)
{
	// Custom Avatar add-on: orbit camera behind the avatar's shoulder; the host already pulled Distance in against
	// copied Java block data.
	SetActorLocation(Pivot);
	const FVector Right = FRotator(0, View.Yaw + 90.f, 0).Vector();
	const FVector Loc = Pivot - View.Vector() * Distance + Right * FMath::Min(45.f, Distance * 0.15f);
	CameraDistance = Distance;
	Camera->SetWorldLocationAndRotation(Loc, View);
}
