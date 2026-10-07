#include "CrbPPDebug.h"
#include "CrbHost.h"
#include "CrbPawn.h"
#include "Components/BoxComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"

#if !UE_BUILD_SHIPPING
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include <initializer_list>
#endif

namespace
{
	const TCHAR* const PPTabNames[] = { TEXT("PHYSICS"), TEXT("PORTALS"), TEXT("STEVE"), TEXT("TESTING") };
	constexpr int32 PPNumTabs = UE_ARRAY_COUNT(PPTabNames);
	FVector PPMcDir(double X, double Y, double Z) { return FVector(Z, -X, Y) * 100.f; }
}

UCrbPPDebugComponent::UCrbPPDebugComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
}

double UCrbPPDebugComponent::ConfigValue(const FString& Key, double Def) const
{
	const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
	if (Config.IsValid() && Config->TryGetArrayField(TEXT("tunables"), A))
		for (const TSharedPtr<FJsonValue>& V : *A) { const TSharedPtr<FJsonObject> O = V->AsObject(); if (O.IsValid() && O->GetStringField(TEXT("key")) == Key) return O->GetNumberField(TEXT("value")); }
	return Def;
}
bool UCrbPPDebugComponent::ConfigFlag(const FString& Key) const { bool B = false; if (Config.IsValid()) Config->TryGetBoolField(Key, B); return B; }

// ============================================================================================ actions
void UCrbPPDebugComponent::Cmd(const FString& Command, const TSharedPtr<FJsonObject>& Args) { if (Host) { Pending.Add(Host->PPCommand(Command, Args)); ++CommandsSent; } }
void UCrbPPDebugComponent::Op(const FString& OpName, const TSharedPtr<FJsonObject>& Args) { if (Host) { Pending.Add(Host->SendCommand(OpName, Args)); ++CommandsSent; } }
void UCrbPPDebugComponent::Tune(const FString& Key, double Value)
{
	TSharedPtr<FJsonObject> V = MakeShared<FJsonObject>(); V->SetNumberField(Key, Value);
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetObjectField(TEXT("values"), V); Cmd(TEXT("tune"), A);
}
void UCrbPPDebugComponent::TuneStep(const FString& Key, int32 Dir)
{
	const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
	if (Config.IsValid() && Config->TryGetArrayField(TEXT("tunables"), A))
		for (const TSharedPtr<FJsonValue>& V : *A)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (!O.IsValid() || O->GetStringField(TEXT("key")) != Key) continue;
			const double Lo = O->GetNumberField(TEXT("min")), Hi = O->GetNumberField(TEXT("max"));
			const double NewV = FMath::Clamp(O->GetNumberField(TEXT("value")) + Dir * (Hi - Lo) / 40.0, Lo, Hi);
			O->SetNumberField(TEXT("value"), NewV);
			Tune(Key, NewV);
		}
}
void UCrbPPDebugComponent::SetFlag(const FString& Key, bool bOn)
{
	TSharedPtr<FJsonObject> V = MakeShared<FJsonObject>(); V->SetBoolField(Key, bOn);
	if (Config.IsValid()) Config->SetBoolField(Key, bOn);
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetObjectField(TEXT("values"), V); Cmd(TEXT("tune"), A);
}
void UCrbPPDebugComponent::SetModifier(const FString& Name)
{
	TSharedPtr<FJsonObject> V = MakeShared<FJsonObject>(); V->SetStringField(TEXT("modifier"), Name);
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetObjectField(TEXT("values"), V); Cmd(TEXT("tune"), A);
}
void UCrbPPDebugComponent::SetTimeScale(float S)
{
	TSharedPtr<FJsonObject> V = MakeShared<FJsonObject>(); V->SetNumberField(TEXT("timeScale"), FMath::Clamp(S, 0.05f, 2.f));
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetObjectField(TEXT("values"), V); Cmd(TEXT("tune"), A);
}
void UCrbPPDebugComponent::SetFrozen(bool bOn)
{
	TSharedPtr<FJsonObject> V = MakeShared<FJsonObject>(); V->SetBoolField(TEXT("frozen"), bOn);
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetObjectField(TEXT("values"), V); Cmd(TEXT("tune"), A);
}
void UCrbPPDebugComponent::TestPortalMomentum()
{
	// Portal A on the course wall at x = 5 (faces the lane), B on the far wall at z = 46; the player is thrown at A
	if (!Host) return;
	const int32 FloorY = -60;
	auto Place = [this, FloorY](int32 Which, int32 X, int32 Z, const TCHAR* Face)
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetNumberField(TEXT("which"), Which);
		A->SetNumberField(TEXT("x"), X); A->SetNumberField(TEXT("y"), FloorY); A->SetNumberField(TEXT("z"), Z); A->SetStringField(TEXT("face"), Face);
		Op(TEXT("pp.portal"), A);
	};
	Place(0, 5, 10, TEXT("west")); Place(1, 0, 46, TEXT("north"));
	TSharedPtr<FJsonObject> T = MakeShared<FJsonObject>(); T->SetNumberField(TEXT("x"), 1.5); T->SetNumberField(TEXT("y"), FloorY); T->SetNumberField(TEXT("z"), 10.5); T->SetNumberField(TEXT("yaw"), -90);
	Op(TEXT("test.tp"), T);
	TSharedPtr<FJsonObject> V = MakeShared<FJsonObject>(); V->SetNumberField(TEXT("x"), 12); V->SetNumberField(TEXT("y"), 2); V->SetNumberField(TEXT("z"), 0);
	TWeakObjectPtr<UCrbPPDebugComponent> W = this;
#if !UE_BUILD_SHIPPING
	FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([W, V](float) { if (W.IsValid()) W->Cmd(TEXT("velocity"), V); return false; }), 0.6f);
#endif
	LastMessage = TEXT("portal momentum test: A on the x=5 wall, B on the z=46 wall, player thrown at A at 12 m/s");
}
void UCrbPPDebugComponent::TestPortalOrientation()
{
	// floor portal under the player, wall portal ahead: falling in comes out horizontally (orientation transform)
	if (!Host) return;
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetNumberField(TEXT("which"), 0);
	A->SetNumberField(TEXT("x"), 0); A->SetNumberField(TEXT("y"), -61); A->SetNumberField(TEXT("z"), 20); A->SetStringField(TEXT("face"), TEXT("up"));
	Op(TEXT("pp.portal"), A);
	TSharedPtr<FJsonObject> B = MakeShared<FJsonObject>(); B->SetNumberField(TEXT("which"), 1);
	B->SetNumberField(TEXT("x"), 0); B->SetNumberField(TEXT("y"), -60); B->SetNumberField(TEXT("z"), 46); B->SetStringField(TEXT("face"), TEXT("north"));
	Op(TEXT("pp.portal"), B);
	TSharedPtr<FJsonObject> T = MakeShared<FJsonObject>(); T->SetNumberField(TEXT("x"), 0.5); T->SetNumberField(TEXT("y"), -52); T->SetNumberField(TEXT("z"), 20.5); T->SetNumberField(TEXT("yaw"), 0);
	Op(TEXT("test.tp"), T);
	LastMessage = TEXT("portal orientation test: drop into a floor portal, come out of a wall portal");
}
void UCrbPPDebugComponent::CycleAnimState()
{
	if (!Host) return;
	Host->PPForceClip = Host->PPForceClip + 1 >= (int32)ECrbPPClip::Count ? -1 : Host->PPForceClip + 1;
	LastMessage = Host->PPForceClip < 0 ? FString(TEXT("animation: auto (physics state)")) : FString(TEXT("animation forced: ")) + CrbPPClipName((ECrbPPClip)Host->PPForceClip);
}
void UCrbPPDebugComponent::SetTab(int32 T) { Tab = FMath::Clamp(T, 0, PPNumTabs - 1); bDirty = true; }

// ============================================================================================ text
FString UCrbPPDebugComponent::MonitorText() const
{
	if (!Host) return FString();
	const FCrbPPState& P = Host->GetState().PP;
	const FCrbPPAnimDebug* AD = Host->PPSteve.AnimDebug();
	return FString::Printf(TEXT("PHYSICS & PORTAL %s  | %s\n  State: %s (%.2f s)  Modifier: %s\n  Speed: %.2f m/s  max %.1f\n  Velocity: %.2f / %.2f / %.2f\n  Surface: %s (friction %.2f, bounce %.2f)\n  Grounded: %s  Normal: %.2f %.2f %.2f\n  Portals: %s  A %s  B %s  through %lld (entities %lld)\n  Last portal: %s\n  Anim: %s %.2f/%.2f x%.2f\n  Time: %.2fx%s  Ragdoll %s  colliders %d  cubes %d\n  Portal views: %d visible, %d captures"),
		P.bActive ? TEXT("ACTIVE") : Host->IsPhysicsPortalActive() ? TEXT("WAITING") : TEXT("OFF"), *P.Event,
		*P.State, P.StateTime, *P.Modifier, P.Speed, P.MaxSpeed, P.VX, P.VY, P.VZ, *P.SurfaceBlock, P.Friction, P.Bounce,
		P.bGrounded ? TEXT("YES") : TEXT("NO"), P.NX, P.NY, P.NZ,
		P.bLinked ? TEXT("LINKED") : TEXT("unlinked"), P.Portals[0].bValid ? *P.Portals[0].Face : TEXT("-"), P.Portals[1].bValid ? *P.Portals[1].Face : TEXT("-"), P.PortalSeq, P.EntityTeleports,
		P.LastPortal.IsEmpty() ? TEXT("-") : *P.LastPortal,
		AD ? *AD->Clip : TEXT("?"), AD ? AD->Time : 0.f, AD ? AD->Length : 0.f, AD ? AD->Rate : 0.f,
		P.TimeScale, P.bFrozen ? TEXT(" FROZEN") : TEXT(""), Host->PPSteve.IsRagdolling() ? TEXT("ON") : TEXT("off"), Host->PPSteve.Colliders.Active(), Host->PPCubes.Num(),
		Host->PortalViews.Visible, Host->PortalViews.Captures);
}

FString UCrbPPDebugComponent::TabText(int32 T) const
{
	if (!Host) return FString();
	const FCrbPPState& P = Host->GetState().PP;
	switch (T)
	{
	case 0: return FString::Printf(TEXT("Rolling %s   Momentum %s   Ragdoll on impact %s\nSurface table entries: %d\nState counts: walk %d run %d roll %d slide %d launch %d land %d portal %d ragdoll %d\nLands %lld  bounces %lld  launches %lld  last impact %.1f m/s"),
		ConfigFlag(TEXT("rolling")) ? TEXT("ON") : TEXT("OFF"), ConfigFlag(TEXT("momentum")) ? TEXT("ON") : TEXT("OFF"), ConfigFlag(TEXT("ragdollOnImpact")) ? TEXT("ON") : TEXT("OFF"), P.Surfaces,
		P.Count(TEXT("WALK")), P.Count(TEXT("RUN")), P.Count(TEXT("ROLL")), P.Count(TEXT("SLIDE")), P.Count(TEXT("LAUNCH")), P.Count(TEXT("LAND")), P.Count(TEXT("PORTAL_EXIT")), P.Count(TEXT("RAGDOLL")),
		P.LandSeq, P.BounceSeq, P.LaunchSeq, P.Impact);
	case 1: return FString::Printf(TEXT("Portal A (blue): %s\nPortal B (orange): %s\nLinked: %s\nPlayer traversals: %lld  last: %s\nEntity traversals: %lld\nUE physics cubes: %d (teleports %d)\nMinecraft physics objects: %d\nPortal views: %d visible, %d captures, material %s"),
		P.Portals[0].bValid ? *FString::Printf(TEXT("%.1f %.1f %.1f facing %s"), P.Portals[0].X, P.Portals[0].Y, P.Portals[0].Z, *P.Portals[0].Face) : TEXT("-"),
		P.Portals[1].bValid ? *FString::Printf(TEXT("%.1f %.1f %.1f facing %s"), P.Portals[1].X, P.Portals[1].Y, P.Portals[1].Z, *P.Portals[1].Face) : TEXT("-"),
		P.bLinked ? TEXT("YES") : TEXT("NO"), P.PortalSeq, P.LastPortal.IsEmpty() ? TEXT("-") : *P.LastPortal, P.EntityTeleports,
		Host->PPCubes.Num(), Host->PPCubes.Teleports, P.Objects, Host->PortalViews.Visible, Host->PortalViews.Captures, Host->PortalViews.bMaterialReady ? TEXT("ok") : TEXT("MISSING"));
	case 2:
	{
		const FCrbPPAnimDebug* AD = Host->PPSteve.AnimDebug();
		return FString::Printf(TEXT("Character: SK_PPSteve %s, %d bones, physics asset %s\nClips loaded: %d / %d\nAnimation: %s (prev %s)  %.2f / %.2f s  rate %.2f  blend %.2f\nForced clip: %s   anim %s\nController: %s   force roll %s   force slide %s\nRagdoll: %s"),
			Host->PPSteve.AssetsReady() ? TEXT("loaded") : *Host->PPSteve.LoadError, Host->PPSteve.NumBones(), Host->PPSteve.HasPhysicsAsset() ? TEXT("yes") : TEXT("NO"),
			Host->PPSteve.ClipsLoaded, (int32)ECrbPPClip::Count, AD ? *AD->Clip : TEXT("?"), AD ? *AD->PrevClip : TEXT("?"), AD ? AD->Time : 0.f, AD ? AD->Length : 0.f, AD ? AD->Rate : 0.f, AD ? AD->Blend : 0.f,
			Host->PPForceClip < 0 ? TEXT("auto") : CrbPPClipName((ECrbPPClip)Host->PPForceClip), Host->bPPAnimPaused ? TEXT("paused") : TEXT("playing"),
			P.bController ? TEXT("ON") : TEXT("OFF (vanilla)"), P.bForceRoll ? TEXT("ON") : TEXT("off"), P.bForceSlide ? TEXT("ON") : TEXT("off"), Host->PPSteve.IsRagdolling() ? TEXT("simulating") : TEXT("off"));
	}
	case 3: return FString::Printf(TEXT("Time scale %.2fx   physics %s\nFrames simulated %lld\nLast event: %s"), P.TimeScale, P.bFrozen ? TEXT("FROZEN") : TEXT("running"), P.Frames, *P.Event);
	default: return FString();
	}
}

#if !UE_BUILD_SHIPPING
// ============================================================================================ Slate panel
namespace
{
	void PPDefer(TFunction<void()> F) { FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([F](float) { F(); return false; })); }
	TSharedRef<SWidget> PPBtn(const FString& Label, TFunction<void()> F, FLinearColor Tint = FLinearColor(0.25f, 0.27f, 0.32f))
	{
		return SNew(SBox).Padding(FMargin(2))[ SNew(SButton).ButtonColorAndOpacity(Tint).OnClicked_Lambda([F]() { PPDefer(F); return FReply::Handled(); })
			[ SNew(STextBlock).Text(FText::FromString(Label)).Font(FCoreStyle::GetDefaultFontStyle("Bold", 9)).ColorAndOpacity(FLinearColor::White) ] ];
	}
	TSharedRef<SWidget> PPCheck(const FString& Label, TFunction<bool()> Get, TFunction<void(bool)> Set)
	{
		return SNew(SBox).Padding(FMargin(2, 1))[ SNew(SCheckBox).IsChecked_Lambda([Get]() { return Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([Set](ECheckBoxState S) { const bool B = S == ECheckBoxState::Checked; PPDefer([Set, B]() { Set(B); }); })
			[ SNew(STextBlock).Text(FText::FromString(Label)).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)).ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f)) ] ];
	}
	TSharedRef<SWidget> PPHead(const FString& Label) { return SNew(SBox).Padding(FMargin(2, 8, 2, 2))[ SNew(STextBlock).Text(FText::FromString(Label)).Font(FCoreStyle::GetDefaultFontStyle("Bold", 10)).ColorAndOpacity(FLinearColor(0.4f, 0.8f, 1.f)) ]; }
}

void UCrbPPDebugComponent::SetPanelOpen(bool bOpen)
{
	if (Host && !Host->bPPDebugMode) bOpen = false;
	if (bOpen == bPanel) return;
	bPanel = bOpen;
	if (bOpen) { BuildPanel(); SetMonitor(true); if (Host) Pending.Add(Host->SendCommand(TEXT("pp.status"))); }
	else if (PanelRoot.IsValid() && GEngine && GEngine->GameViewport) { GEngine->GameViewport->RemoveViewportWidgetContent(PanelRoot.ToSharedRef()); PanelRoot.Reset(); Content.Reset(); }
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		const bool bOtherMenu = Host && Host->IsMenuOpen();
		if (bOpen) { FInputModeGameAndUI M; M.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock); M.SetHideCursorDuringCapture(false); PC->SetInputMode(M); PC->bShowMouseCursor = true; }
		else if (!bOtherMenu) { PC->SetInputMode(FInputModeGameOnly()); PC->bShowMouseCursor = false; FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
	}
}

void UCrbPPDebugComponent::TogglePanel() { SetPanelOpen(!bPanel); }

void UCrbPPDebugComponent::SetMonitor(bool bOn)
{
	if (bOn == bMonitor) return;
	bMonitor = bOn;
	if (!GEngine || !GEngine->GameViewport) return;
	if (bOn)
	{
		TWeakObjectPtr<UCrbPPDebugComponent> W = this;
		MonitorRoot = SNew(SBox).HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(FMargin(0, 8, 8, 0))
			[ SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0, 0, 0, 0.55f)).Padding(FMargin(8, 6))
				[ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Mono", 9)).ColorAndOpacity(FLinearColor(0.8f, 0.95f, 1.f))
					.Text_Lambda([W]() { return FText::FromString(W.IsValid() ? W->MonitorText() : FString()); }) ] ];
		GEngine->GameViewport->AddViewportWidgetContent(MonitorRoot.ToSharedRef(), 140);
	}
	else if (MonitorRoot.IsValid()) { GEngine->GameViewport->RemoveViewportWidgetContent(MonitorRoot.ToSharedRef()); MonitorRoot.Reset(); }
}

void UCrbPPDebugComponent::BuildPanel()
{
	if (!GEngine || !GEngine->GameViewport) return;
	TWeakObjectPtr<UCrbPPDebugComponent> W = this;
	TSharedRef<SWrapBox> Tabs = SNew(SWrapBox).UseAllottedWidth(true);
	for (int32 I = 0; I < PPNumTabs; ++I)
		Tabs->AddSlot()[ SNew(SBox).Padding(FMargin(1))[ SNew(SButton).ButtonColorAndOpacity_Lambda([W, I]() { return W.IsValid() && W->Tab == I ? FLinearColor(0.1f, 0.5f, 1.f) : FLinearColor(0.2f, 0.22f, 0.26f); })
			.OnClicked_Lambda([W, I]() { if (W.IsValid()) W->SetTab(I); return FReply::Handled(); })
			[ SNew(STextBlock).Text(FText::FromString(PPTabNames[I])).Font(FCoreStyle::GetDefaultFontStyle("Bold", 9)).ColorAndOpacity(FLinearColor::White) ] ] ];
	PanelRoot = SNew(SBox).HAlign(HAlign_Left).VAlign(VAlign_Fill).Padding(FMargin(8, 8, 0, 8))
		[ SNew(SBox).WidthOverride(480)
			[ SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.03f, 0.04f, 0.07f, 0.9f)).Padding(FMargin(8))
				[ SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Text(FText::FromString(TEXT("PHYSICS & PORTAL DEBUG  (F7)   developer tool")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11)).ColorAndOpacity(FLinearColor(0.4f, 0.8f, 1.f)) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)[ Tabs ]
					+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Text_Lambda([W]() { return FText::FromString(W.IsValid() ? W->LastMessage : FString()); }).Font(FCoreStyle::GetDefaultFontStyle("Italic", 8)).ColorAndOpacity(FLinearColor(1.f, 0.7f, 0.3f)) ]
					+ SVerticalBox::Slot().FillHeight(1.f)[ SNew(SScrollBox) + SScrollBox::Slot()[ SAssignNew(Content, SVerticalBox) ] ] ] ] ];
	GEngine->GameViewport->AddViewportWidgetContent(PanelRoot.ToSharedRef(), 150);
	RebuildContent();
}

void UCrbPPDebugComponent::RebuildContent()
{
	bDirty = false;
	if (!Content.IsValid()) return;
	Content->ClearChildren();
	TWeakObjectPtr<UCrbPPDebugComponent> W = this;
	const int32 T = Tab;
	Content->AddSlot().AutoHeight()[ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Mono", 9)).ColorAndOpacity(FLinearColor(0.85f, 0.95f, 1.f)).AutoWrapText(true)
		.Text_Lambda([W, T]() { return FText::FromString(W.IsValid() ? W->TabText(T) : FString()); }) ];
	auto Row = [&](TSharedRef<SWidget> X) { Content->AddSlot().AutoHeight()[ X ]; };
	struct FB { FString L; TFunction<void()> F; };
	auto Wrap = [&](std::initializer_list<FB> Buttons)
	{
		TSharedRef<SWrapBox> B = SNew(SWrapBox).UseAllottedWidth(true);
		for (const FB& P : Buttons) B->AddSlot()[ PPBtn(P.L, P.F) ];
		Row(B);
	};
	auto TuneRow = [&](const FString& Key)
	{
		Row(SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)).ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f))
				.Text_Lambda([W, Key]() {
					FString Label = Key; double Def = 0;
					const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
					if (W.IsValid() && W->Config.IsValid() && W->Config->TryGetArrayField(TEXT("tunables"), A))
						for (const TSharedPtr<FJsonValue>& V : *A) { const TSharedPtr<FJsonObject> O = V->AsObject(); if (O.IsValid() && O->GetStringField(TEXT("key")) == Key) { Label = O->GetStringField(TEXT("label")); Def = O->GetNumberField(TEXT("def")); } }
					const double Val = W.IsValid() ? W->ConfigValue(Key, Def) : 0;
					return FText::FromString(FString::Printf(TEXT("%s: %.2f%s"), *Label, Val, FMath::IsNearlyEqual(Val, Def) ? TEXT("") : TEXT("  *"))); }) ]
			+ SHorizontalBox::Slot().AutoWidth()[ PPBtn(TEXT("-"), [W, Key]() { if (W.IsValid()) W->TuneStep(Key, -1); }) ]
			+ SHorizontalBox::Slot().AutoWidth()[ PPBtn(TEXT("+"), [W, Key]() { if (W.IsValid()) W->TuneStep(Key, +1); }) ]);
	};
	switch (T)
	{
	case 0:
		Row(PPHead(TEXT("Physics")));
		Row(PPCheck(TEXT("Enable rolling"), [W]() { return W.IsValid() && W->ConfigFlag(TEXT("rolling")); }, [W](bool B) { if (W.IsValid()) W->SetFlag(TEXT("rolling"), B); }));
		Row(PPCheck(TEXT("Enable momentum"), [W]() { return W.IsValid() && W->ConfigFlag(TEXT("momentum")); }, [W](bool B) { if (W.IsValid()) W->SetFlag(TEXT("momentum"), B); }));
		Row(PPCheck(TEXT("Ragdoll on hard impacts"), [W]() { return W.IsValid() && W->ConfigFlag(TEXT("ragdollOnImpact")); }, [W](bool B) { if (W.IsValid()) W->SetFlag(TEXT("ragdollOnImpact"), B); }));
		for (const TCHAR* K : { TEXT("accel"), TEXT("rollAccel"), TEXT("friction"), TEXT("airControl"), TEXT("gravity"), TEXT("launch"), TEXT("rollSpeed"), TEXT("slope"), TEXT("walkSpeed"), TEXT("runSpeed"), TEXT("ragdollImpact") }) TuneRow(K);
		Row(PPHead(TEXT("Gish-style modifier")));
		Wrap({ { TEXT("Normal"), [W]() { if (W.IsValid()) W->SetModifier(TEXT("NORMAL")); } }, { TEXT("Sticky"), [W]() { if (W.IsValid()) W->SetModifier(TEXT("STICKY")); } },
			{ TEXT("Slippery"), [W]() { if (W.IsValid()) W->SetModifier(TEXT("SLIPPERY")); } }, { TEXT("Squishy"), [W]() { if (W.IsValid()) W->SetModifier(TEXT("SQUISHY")); } },
			{ TEXT("Heavy"), [W]() { if (W.IsValid()) W->SetModifier(TEXT("HEAVY")); } }, { TEXT("Light"), [W]() { if (W.IsValid()) W->SetModifier(TEXT("LIGHT")); } } });
		Wrap({ { TEXT("Reset tuning"), [W]() { if (W.IsValid()) W->Cmd(TEXT("tuneReset")); } } });
		break;
	case 1:
		Row(PPHead(TEXT("Portals")));
		Wrap({ { TEXT("Spawn Portal A"), [W]() { if (W.IsValid() && W->Host) W->Host->PPShootPortal(0); } }, { TEXT("Spawn Portal B"), [W]() { if (W.IsValid() && W->Host) W->Host->PPShootPortal(1); } },
			{ TEXT("Link portals"), [W]() { if (W.IsValid() && W->Host) W->LastMessage = W->Host->GetState().PP.bLinked ? TEXT("A and B are linked (they link as soon as both exist)") : TEXT("place both A and B to link them"); } },
			{ TEXT("Remove portals"), [W]() { if (W.IsValid() && W->Host) W->Host->PPClearPortals(); } },
			{ TEXT("Teleport player (A to B)"), [W]() { if (W.IsValid()) W->Cmd(TEXT("teleport")); } },
			{ TEXT("Test portal momentum"), [W]() { if (W.IsValid()) W->TestPortalMomentum(); } }, { TEXT("Test portal orientation"), [W]() { if (W.IsValid()) W->TestPortalOrientation(); } },
			{ TEXT("Spawn test physics object"), [W]() { if (W.IsValid()) W->Op(TEXT("pp.object")); } }, { TEXT("Spawn UE physics cube"), [W]() { if (W.IsValid() && W->Host) W->Host->PPSpawnCube(); } } });
		break;
	case 2:
		Row(PPHead(TEXT("Steve")));
		Wrap({ { TEXT("Reset player"), [W]() { if (W.IsValid()) W->Cmd(TEXT("resetPlayer")); } }, { TEXT("Reset velocity"), [W]() { if (W.IsValid()) W->Cmd(TEXT("resetVelocity")); } },
			{ TEXT("Launch player"), [W]() { if (W.IsValid()) W->Cmd(TEXT("launch")); } }, { TEXT("Force rolling"), [W]() { if (W.IsValid()) W->Cmd(TEXT("forceRoll")); } },
			{ TEXT("Force sliding"), [W]() { if (W.IsValid()) W->Cmd(TEXT("forceSlide")); } }, { TEXT("Toggle ragdoll"), [W]() { if (W.IsValid()) W->Cmd(TEXT("ragdoll")); } },
			{ TEXT("Toggle physics controller"), [W]() { if (W.IsValid()) W->Cmd(TEXT("controller")); } }, { TEXT("Toggle animation state"), [W]() { if (W.IsValid()) W->CycleAnimState(); } },
			{ TEXT("Pause / play animation"), [W]() { if (W.IsValid() && W->Host) W->Host->bPPAnimPaused = !W->Host->bPPAnimPaused; } } });
		break;
	case 3:
		Row(PPHead(TEXT("Testing")));
		Wrap({ { TEXT("Slow motion 0.25x"), [W]() { if (W.IsValid()) W->SetTimeScale(0.25f); } }, { TEXT("Normal speed"), [W]() { if (W.IsValid()) W->SetTimeScale(1.f); } },
			{ TEXT("Freeze physics"), [W]() { if (W.IsValid()) W->SetFrozen(true); } }, { TEXT("Unfreeze"), [W]() { if (W.IsValid()) W->SetFrozen(false); } },
			{ TEXT("Reset world position"), [W]() { if (W.IsValid()) W->Op(TEXT("pp.home")); } },
			{ TEXT("Spawn test ramp"), [W]() { if (W.IsValid()) { TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("kind"), TEXT("ramp")); W->Op(TEXT("pp.prop"), A); } } },
			{ TEXT("Spawn ice surface"), [W]() { if (W.IsValid()) { TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("kind"), TEXT("ice")); W->Op(TEXT("pp.prop"), A); } } },
			{ TEXT("Spawn slime surface"), [W]() { if (W.IsValid()) { TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("kind"), TEXT("slime")); W->Op(TEXT("pp.prop"), A); } } },
			{ TEXT("Remove test props"), [W]() { if (W.IsValid()) W->Op(TEXT("pp.propClear")); } },
			{ TEXT("Spawn test portal course"), [W]() { if (W.IsValid() && W->Host) W->Host->ToggleFixture(TEXT("pp")); } } });
		break;
	default: break;
	}
}

// ============================================================================================ world drawing
void UCrbPPDebugComponent::DrawViz()
{
	DrawnLastFrame = 0;
	if (!Host || !GetWorld() || !bPanel) return;
	UWorld* Wd = GetWorld();
	const FCrbPPState& P = Host->GetState().PP;
	double X, Y, Z; Host->PresentedFeet(X, Y, Z);
	const FVector Feet = Host->Coords.ToUE(X, Y, Z);
	auto Arrow = [&](const FVector& A, const FVector& B, const FColor& C) { DrawDebugDirectionalArrow(Wd, A, B, 14.f, C, false, -1.f, 0, 2.f); ++DrawnLastFrame; };
	if (Viz[0]) Arrow(Feet + FVector(0, 0, 90), Feet + FVector(0, 0, 90) + PPMcDir(P.VX, P.VY, P.VZ) * 0.3f, FColor::Red);               // velocity
	if (Viz[1]) Arrow(Feet, Feet + PPMcDir(P.NX, P.NY, P.NZ).GetSafeNormal() * 110.f, FColor::Yellow);                                      // ground normal
	for (int32 I = 0; I < 2 && Viz[2]; ++I)
	{
		const FCrbPortalFrame& F = Host->PortalViews.Frames[I];
		if (!F.bValid) continue;
		Arrow(F.C, F.C + F.N * 120.f, I == 0 ? FColor(30, 130, 255) : FColor(255, 140, 20));                                                 // portal normals
		DrawDebugBox(Wd, F.C + F.N * 25.f, FVector(25.f, 50.f, 100.f), FRotationMatrix::MakeFromXZ(F.N, F.U).ToQuat(), FColor::Cyan); ++DrawnLastFrame; // entry zone
	}
	if (Viz[3]) for (UBoxComponent* B : Host->PPSteve.Colliders.Boxes) if (B && B->GetCollisionEnabled() != ECollisionEnabled::NoCollision) { DrawDebugBox(Wd, B->GetComponentLocation(), FVector(50.f), FColor::Green); ++DrawnLastFrame; }
	if (Viz[4]) { const FCrbPPAnimDebug* AD = Host->PPSteve.AnimDebug(); DrawDebugString(Wd, Feet + FVector(0, 0, 220), FString::Printf(TEXT("%s | %s"), *P.State, AD ? *AD->Clip : TEXT("?")), nullptr, FColor::White, 0.f, true); ++DrawnLastFrame; }
}
#else
void UCrbPPDebugComponent::SetPanelOpen(bool) {}
void UCrbPPDebugComponent::TogglePanel() {}
void UCrbPPDebugComponent::SetMonitor(bool) {}
void UCrbPPDebugComponent::BuildPanel() {}
void UCrbPPDebugComponent::RebuildContent() {}
void UCrbPPDebugComponent::DrawViz() {}
#endif

void UCrbPPDebugComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Host) return;
	for (int32 I = Pending.Num() - 1; I >= 0; --I)
		if (const FCrbResult* R = Host->FindResult(Pending[I]))
		{
			LastMessage = R->Message;
			const TSharedPtr<FJsonObject>* C = nullptr;
			if (R->Json.IsValid() && R->Json->TryGetObjectField(TEXT("config"), C)) { const bool bHad = Config.IsValid(); Config = *C; if (!bHad) bDirty = true; }
			Pending.RemoveAt(I);
		}
	if (bPanel && !Host->IsPhysicsPortalActive()) { SetPanelOpen(false); SetMonitor(false); }   // mod off: debug UI goes too
	if (bDirty && bPanel) RebuildContent();
	DrawViz();
}

void UCrbPPDebugComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	SetPanelOpen(false); SetMonitor(false);
	Super::EndPlay(Reason);
}

// ============================================================================================ console commands
#if !UE_BUILD_SHIPPING
namespace
{
	UCrbPPDebugComponent* PPDbg(UWorld* W)
	{
		if (!W) return nullptr;
		for (TActorIterator<ACrbHost> It(W); It; ++It) return It->PPDebug;
		return nullptr;
	}
	bool PPArg01(const TArray<FString>& A, bool Def) { return A.Num() ? A[0] != TEXT("0") && A[0] != TEXT("off") : Def; }
	using FPPArgs = const TArray<FString>&;
#define CRB_PP_CMD(Var, Name, Help, Body) static FAutoConsoleCommandWithWorldAndArgs Var(TEXT(Name), TEXT(Help), FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](FPPArgs A, UWorld* W) { if (UCrbPPDebugComponent* D = PPDbg(W)) { Body; } }));
	CRB_PP_CMD(GPPDebug, "pp.debug", "pp.debug 0|1 - Physics & Portal debug menu", D->SetPanelOpen(PPArg01(A, !D->IsPanelOpen())))
	CRB_PP_CMD(GPPSlowmo, "pp.slowmo", "pp.slowmo <scale> - physics time scale", D->SetTimeScale(A.Num() ? FCString::Atof(*A[0]) : 0.25f))
	CRB_PP_CMD(GPPFreeze, "pp.freeze", "pp.freeze 0|1 - freeze the player physics", D->SetFrozen(PPArg01(A, true)))
	CRB_PP_CMD(GPPPortalA, "pp.portalA", "pp.portalA - shoot the blue portal where you look", if (D->Host) D->Host->PPShootPortal(0))
	CRB_PP_CMD(GPPPortalB, "pp.portalB", "pp.portalB - shoot the orange portal where you look", if (D->Host) D->Host->PPShootPortal(1))
	CRB_PP_CMD(GPPClear, "pp.clearportals", "pp.clearportals - remove both portals", if (D->Host) D->Host->PPClearPortals())
	CRB_PP_CMD(GPPLaunch, "pp.launch", "pp.launch [up] [forward] - launch the player", { TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>(); J->SetNumberField(TEXT("up"), A.Num() ? FCString::Atof(*A[0]) : 14.f); J->SetNumberField(TEXT("forward"), A.Num() > 1 ? FCString::Atof(*A[1]) : 6.f); D->Cmd(TEXT("launch"), J); })
	CRB_PP_CMD(GPPRoll, "pp.roll", "pp.roll - toggle forced rolling", D->Cmd(TEXT("forceRoll")))
	CRB_PP_CMD(GPPSlide, "pp.slide", "pp.slide - toggle forced sliding", D->Cmd(TEXT("forceSlide")))
	CRB_PP_CMD(GPPRagdoll, "pp.ragdoll", "pp.ragdoll - toggle ragdoll", D->Cmd(TEXT("ragdoll")))
	CRB_PP_CMD(GPPMod, "pp.modifier", "pp.modifier <normal|sticky|slippery|squishy|heavy|light>", D->SetModifier(A.Num() ? A[0].ToUpper() : FString(TEXT("NORMAL"))))
	CRB_PP_CMD(GPPTune, "pp.tune", "pp.tune <key> <value> | pp.tune reset", if (A.Num() == 1 && A[0] == TEXT("reset")) D->Cmd(TEXT("tuneReset")); else if (A.Num() >= 2) D->Tune(A[0], FCString::Atod(*A[1])))
	CRB_PP_CMD(GPPCube, "pp.cube", "pp.cube - throw a UE physics cube", if (D->Host) D->Host->PPSpawnCube())
	CRB_PP_CMD(GPPObject, "pp.object", "pp.object - throw a Minecraft physics object", D->Op(TEXT("pp.object")))
	CRB_PP_CMD(GPPCourse, "pp.course", "pp.course - toggle the physics & portal test course", if (D->Host) D->Host->ToggleFixture(TEXT("pp")))
	CRB_PP_CMD(GPPTestMom, "pp.test.momentum", "portal momentum test", D->TestPortalMomentum())
	CRB_PP_CMD(GPPTestOri, "pp.test.orientation", "portal orientation test", D->TestPortalOrientation())
#undef CRB_PP_CMD
}
#endif
