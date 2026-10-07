#include "CrbMenus.h"
#include "CrbHost.h"
#include "CrbMcUi.h"
#include "Dom/JsonObject.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"
#include "Containers/Ticker.h"

namespace
{
	TSharedPtr<SWidget> MenuRoot;
	TWeakObjectPtr<ACrbHost> MenuOwner;
	TMap<FString, TSharedPtr<SButton>> Rows;
	TSharedPtr<SScrollBox> Scroll;
	enum class EKind { None, Mods, Debug, Herobrine, Pause, Death, Maps, Sm64, EC } MenuKind = EKind::None;

	float RowWidth = 310.f; // GUI pixels (vanilla wide buttons are 200; long mod labels need more)

	// ---- pop-up animation (every menu): the backdrop fades in, the panel springs up from 80% with a small overshoot
	// and the rows slide in one after another. Rebuilding the same open menu (refreshes) does not replay it.
	double OpenedAt = -100.0;
	EKind AnimatedKind = EKind::None; FString AnimatedTitle;
	constexpr float PopSeconds = 0.28f, RowDelay = 0.035f, RowSeconds = 0.22f;
	float SinceOpen() { return (float)(FPlatformTime::Seconds() - OpenedAt); }
	float EaseOutBack(float T) { T = FMath::Clamp(T, 0.f, 1.f); const float C1 = 1.70158f, C3 = C1 + 1.f; return 1.f + C3 * FMath::Pow(T - 1.f, 3.f) + C1 * FMath::Pow(T - 1.f, 2.f); }
	float EaseOutCubic(float T) { T = FMath::Clamp(T, 0.f, 1.f); return 1.f - FMath::Pow(1.f - T, 3.f); }
	float PopScale() { return FMath::Lerp(0.8f, 1.f, EaseOutBack(SinceOpen() / PopSeconds)); }
	float PopAlpha() { return EaseOutCubic(SinceOpen() / (PopSeconds * 0.8f)); }
	float RowProgress(int32 Index) { return EaseOutCubic((SinceOpen() - 0.06f - Index * RowDelay) / RowSeconds); }

	TSharedRef<SWidget> Label(const FString& Text, FLinearColor Color)
	{
		return SNew(SBox).Padding(FMargin(0, 2 * CrbMcUi::Unit()))[ SNew(SCrbMcText).Text(Text).Color(Color).WrapAt(RowWidth) ];
	}

	// A vanilla button (widgets.png face, bitmap label: E0E0E0, hover FFFFA0). Toggle rows that are ON show their label
	// in green. The SButton underneath keeps the click semantics (press-to-fire like vanilla, tests via ClickRow).
	TSharedRef<SWidget> Row(const FString& Key, const FText& Label, TFunction<bool()> IsActive, TFunction<void()> OnClick)
	{
		static FButtonStyle Invisible = FButtonStyle().SetNormal(FSlateNoResource()).SetHovered(FSlateNoResource()).SetPressed(FSlateNoResource()).SetDisabled(FSlateNoResource())
			.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
		TSharedPtr<SButton> B;
		TSharedRef<TWeakPtr<SButton>> Self = MakeShared<TWeakPtr<SButton>>();
		const float U = CrbMcUi::Unit();
		const FString Text = Label.ToString();
		const float Fit = FMath::Min(1.f, (RowWidth - 12.f) / FMath::Max(1.f, CrbMcUi::TextWidth(Text)));
		TSharedRef<SWidget> W = SNew(SBox).WidthOverride(RowWidth * U).HeightOverride(20 * U).Padding(FMargin(0))
		[
			SAssignNew(B, SButton)
			.ButtonStyle(&Invisible)
			.ContentPadding(FMargin(0))
			// Run the action on the next frame, never inside the button's own click: actions close/rebuild the menu,
			// which destroyed the SButton while SButton::ExecuteOnClick was still running (it calls AsShared() after
			// the delegate -> "SharedThis.Get() == this" assertion, the debug-menu crash).
			.OnClicked_Lambda([OnClick]()
			{
				FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OnClick](float) { OnClick(); return false; }));
				return FReply::Handled();
			})
			.ClickMethod(EButtonClickMethod::MouseDown) // vanilla buttons fire on press
			[
				SNew(SOverlay)
				+ SOverlay::Slot()[ SNew(SCrbMcButtonFace).Width(RowWidth).Hovered_Lambda([Self]() { TSharedPtr<SButton> P = Self->Pin(); return P.IsValid() && P->IsHovered(); }) ]
				+ SOverlay::Slot().VAlign(VAlign_Top).Padding(FMargin(0, (6.f + (1.f - Fit) * 4.f) * U, 0, 0)) // vanilla: label at y + (20 - 8) / 2
				[
					SNew(SCrbMcText).Text(Text).Scale(Fit).bCentered(true)
					.Color_Lambda([Self, IsActive]()
					{
						TSharedPtr<SButton> P = Self->Pin();
						if (IsActive()) return CrbMcUi::Rgb(0x80FF80);
						return P.IsValid() && P->IsHovered() ? CrbMcUi::Rgb(0xFFFFA0) : CrbMcUi::Rgb(0xE0E0E0);
					})
				]
			]
		];
		*Self = B;
		const int32 Index = Rows.Num();
		Rows.Add(Key, B);
		return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(FMargin(0, 2 * U))
			.ColorAndOpacity_Lambda([Index]() { return FLinearColor(1, 1, 1, RowProgress(Index)); })
			.RenderTransform_Lambda([Index, U]() { return FSlateRenderTransform(FVector2D((1.f - RowProgress(Index)) * -24.f * U, 0.f)); })
			[ W ];
	}

	// Removes the overlay only (rows are rebuilt by the caller before Show()).
	void RemoveWidget()
	{
		Scroll.Reset();
		if (MenuRoot.IsValid() && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(MenuRoot.ToSharedRef());
		MenuRoot.Reset();
	}

	void Show(ACrbHost* Host, const FText& Title, TSharedRef<SVerticalBox> Body, FLinearColor Backdrop = FLinearColor(0.063f, 0.063f, 0.063f, 0.75f), bool bList = false)
	{
		RemoveWidget();
		MenuOwner = Host;
		if (AnimatedKind != MenuKind || AnimatedTitle != Title.ToString()) { OpenedAt = FPlatformTime::Seconds(); AnimatedKind = MenuKind; AnimatedTitle = Title.ToString(); } // a refresh of the same menu keeps its state
		CrbMcUi::Update(Host);
		const float U = CrbMcUi::Unit();
		const FSlateBrush* Dirt = CrbMcUi::Background();
		// Vanilla in-game screens: dark translucent gradient over the world, white centred title, buttons centred.
		// Long lists (mods, tests, Herobrine) sit on the darkened dirt list background like vanilla selection lists.
		TSharedRef<SWidget> List = SNew(SBox).MaxDesiredHeight(175 * U)
			[ SAssignNew(Scroll, SScrollBox).ScrollBarThickness(FVector2D(3 * U, 3 * U)) + SScrollBox::Slot().HAlign(HAlign_Center)[ Body ] ];
		TSharedRef<SWidget> Panel = (bList && Dirt)
			? StaticCastSharedRef<SWidget>(SNew(SBorder).BorderImage(Dirt).Padding(FMargin(6 * U, 4 * U))[ List ])
			: List;
		MenuRoot = SNew(SOverlay)
			+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
			[ SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor_Lambda([Backdrop]() { FLinearColor C = Backdrop; C.A *= PopAlpha(); return FSlateColor(C); }) ]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
				.ColorAndOpacity_Lambda([]() { return FLinearColor(1, 1, 1, PopAlpha()); })
				.RenderTransformPivot(FVector2D(0.5f, 0.5f))
				.RenderTransform_Lambda([]() { return FSlateRenderTransform(PopScale()); })
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 8 * U)
					[
						SNew(SBox).RenderTransform_Lambda([U]() { return FSlateRenderTransform(FVector2D(0.f, (1.f - EaseOutBack(SinceOpen() / PopSeconds)) * -14.f * U)); })
						[ SNew(SCrbMcText).Text(Title.ToString()).Color(FLinearColor::White).bCentered(true) ]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ Panel ]
				]
			];
		if (GEngine && GEngine->GameViewport) GEngine->GameViewport->AddViewportWidgetContent(MenuRoot.ToSharedRef(), 100);
		if (APlayerController* PC = Host->GetWorld()->GetFirstPlayerController())
		{
			FInputModeGameAndUI Mode; Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock); Mode.SetHideCursorDuringCapture(false);
			PC->SetInputMode(Mode);
			PC->bShowMouseCursor = true;
		}
	}
}

namespace CrbMenus
{
	bool IsOpen() { return MenuRoot.IsValid(); }

	void OpenModMenu(ACrbHost* Host)
	{
		RemoveWidget(); Rows.Reset();
		MenuKind = EKind::Mods;
		TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
		TWeakObjectPtr<ACrbHost> W = Host;
		auto Weapon = [W](ECrbWeapon Wp) { return [W, Wp]() { return W.IsValid() && W->Weapon == Wp; }; };
		auto Pick = [W](ECrbWeapon Wp) { return [W, Wp]() { if (W.IsValid()) { W->SelectWeapon(Wp); W->CloseMenus(); } }; };
		Body->AddSlot().AutoHeight()[ Label(TEXT("Added mods (click to equip)"), FLinearColor(0.7f, 0.9f, 1.f)) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("mod:hand"), FText::FromString(TEXT("Minecraft hand (vanilla held item)")), Weapon(ECrbWeapon::Hand), Pick(ECrbWeapon::Hand)) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("mod:gravity_gun"), FText::FromString(TEXT("Gravity Gun  -  grab, carry, scroll distance, place")), Weapon(ECrbWeapon::GravityGun), Pick(ECrbWeapon::GravityGun)) ];
		{
			const FString AvatarLabel = FString(TEXT("God of War Unity mod  -  play as Kratos: LMB axe melee, RMB throw, R / middle mouse recall"))
				+ (Host->PlayerAvatar.AssetsReady() ? FString() : TEXT("  (assets not cooked)"));
			Body->AddSlot().AutoHeight()[ Row(TEXT("mod:avatar"), FText::FromString(AvatarLabel), [W]() { return W.IsValid() && W->bAvatarEnabled; }, [W]() { if (W.IsValid()) W->ToggleAvatar(); }) ];
			Body->AddSlot().AutoHeight()[ Row(TEXT("gow:spawn"), FText::FromString(TEXT("    Spawn Mutant enemy (100 health)")), []() { return false; }, [W]() { if (W.IsValid()) W->SpawnMutant(); }) ];
			Body->AddSlot().AutoHeight()[ Row(TEXT("gow:clear"), FText::FromString(TEXT("    Remove Mutants")), []() { return false; }, [W]() { if (W.IsValid()) W->ClearMutants(); }) ];
		}
		{
			const FString Sm64Label = FString(TEXT("SM64 Steve Movement  -  Mario 64 moves as Steve: triple jump, backflip, long jump, wall kick, ground pound"))
				+ (Host->Steve.AssetsReady() ? FString() : TEXT("  (Steve rig not cooked: vanilla model)"));
			Body->AddSlot().AutoHeight()[ Row(TEXT("mod:sm64"), FText::FromString(Sm64Label), [W]() { return W.IsValid() && W->bSm64Enabled; }, [W]() { if (W.IsValid()) W->ToggleSm64(); }) ];
			Body->AddSlot().AutoHeight()[ Row(TEXT("mod:physicsportal"), FText::FromString(FString(TEXT("Minecraft Physics & Portal Mod  -  momentum rolling, Minecraft surface physics, Portal A / B, 3D Steve ragdoll"))
					+ (Host->PPSteve.AssetsReady() ? FString() : TEXT("  (3D Steve not cooked)"))),
				[W]() { return W.IsValid() && W->bPhysicsPortalEnabled; }, [W]() { if (W.IsValid()) W->TogglePhysicsPortal(); }) ];
			Body->AddSlot().AutoHeight()[ Row(TEXT("mod:craft64"), FText::FromString(TEXT("Craft 64  -  Doom 64-style shooter in the open Minecraft world: fist to BFG and the Unmaker, all Minecraft-made")),
				[W]() { return W.IsValid() && W->bCraft64Enabled; }, [W]() { if (W.IsValid()) W->ToggleCraft64(); }) ];
			Body->AddSlot().AutoHeight()[ Row(TEXT("mod:eldencombat"), FText::FromString(FString(TEXT("Minecraft \u00d7 Elden Combat  -  stamina, poise, guard / parry, dodge roll i-frames, lock-on, ripostes as the 3D Steve"))
					+ (Host->ECSteve.AssetsAvailable() ? FString() : TEXT("  (3D Steve not cooked)"))),
				[W]() { return W.IsValid() && W->bEldenCombatEnabled && W->ECStyle == 0; }, [W]() { if (W.IsValid()) { W->SelectEldenStyle(0); OpenModMenu(W.Get()); } }) ];
			Body->AddSlot().AutoHeight()[ Row(TEXT("mod:eldenring"), FText::FromString(FString(TEXT("Elden Ring Combat (Steve)  -  Elden Ring-style animation rewrite on the 3D Steve, modelled weapons + shield, Elden HUD and look"))
					+ (Host->ECSteve.AssetsAvailable() ? FString() : TEXT("  (3D Steve not cooked)"))),
				[W]() { return W.IsValid() && W->IsEldenRingStyle(); }, [W]() { if (W.IsValid()) { W->SelectEldenStyle(1); OpenModMenu(W.Get()); } }) ];
			if (Host->IsEldenCombatActive())
				Body->AddSlot().AutoHeight()[ Row(TEXT("ec:menu"), FText::FromString(TEXT("    Elden Combat controls & debug...")), []() { return false; }, [W]() { if (W.IsValid()) W->OpenECMenu(); }) ];
			Body->AddSlot().AutoHeight()[ Row(TEXT("sm64:settings"), FText::FromString(TEXT("    SM64 Steve Movement settings...")), []() { return false; }, [W]() { if (W.IsValid()) W->OpenSm64Menu(); }) ];
		}
		if (Host->IsHerobrineInstalled())
			Body->AddSlot().AutoHeight()[ Row(TEXT("mod:herobrine"), FText::FromString(TEXT("Herobrine (From The Fog)  -  settings and sightings")), []() { return false; }, [W]() { if (W.IsValid()) W->OpenHerobrineMenu(); }) ];
		FString Fabric;
		for (const FCrbModInfo& M : Host->Mods) if (!M.bAddon) Fabric += FString::Printf(TEXT("%s %s,  "), *M.Name, *M.Version);
		Body->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[ Label(TEXT("Loaded Fabric mods: ") + Fabric, FLinearColor(0.75f, 0.75f, 0.75f)) ];
		Body->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[ Row(TEXT("resume"), FText::FromString(TEXT("Resume")), []() { return false; }, [W]() { if (W.IsValid()) W->CloseMenus(); }) ];
		Show(Host, FText::FromString(TEXT("Mods  (M)")), Body, FLinearColor(0.063f, 0.063f, 0.063f, 0.75f), true);
	}

	void OpenDebugMenu(ACrbHost* Host)
	{
		RemoveWidget(); Rows.Reset();
		MenuKind = EKind::Debug;
		TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
		TWeakObjectPtr<ACrbHost> W = Host;
		Body->AddSlot().AutoHeight()[ Label(TEXT("Superflat test fixtures - click to start, click again to turn off"), FLinearColor(0.7f, 0.9f, 1.f)) ];
		for (const TPair<FString, FString>& F : Host->Fixtures)
		{
			const FString Id = F.Key;
			Body->AddSlot().AutoHeight()[ Row(TEXT("fixture:") + Id, FText::FromString(F.Value), [W, Id]() { return W.IsValid() && W->ActiveFixture == Id; }, [W, Id]() { if (W.IsValid()) { W->ToggleFixture(Id); W->CloseMenus(); } }) ];
		}
		if (Host->Fixtures.Num() == 0) Body->AddSlot().AutoHeight()[ Label(TEXT("(waiting for the fixture list from Minecraft)"), FLinearColor(0.88f, 0.88f, 0.88f)) ];
		{
			const FString Mode = Host->GetState().GameMode;
			const bool bCreative = Mode == TEXT("creative");
			const FString Label = FString::Printf(TEXT("Game mode: %s   (click to switch to %s)"), bCreative ? TEXT("Creative") : (Mode.IsEmpty() ? TEXT("?") : *Mode.Left(1).ToUpper().Append(Mode.Mid(1))), bCreative ? TEXT("Survival") : TEXT("Creative"));
			Body->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[ Row(TEXT("gamemode"), FText::FromString(Label), [W]() { return W.IsValid() && W->IsCreative(); }, [W]() { if (W.IsValid()) W->SetGameMode(W->IsCreative() ? TEXT("survival") : TEXT("creative")); }) ];
		}
		if (Host->IsEldenCombatActive())   // the mod's debug section exists only while the mod is on
		{
			Body->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[ Label(TEXT("Minecraft \u00d7 Elden Combat"), FLinearColor(0.93f, 0.78f, 0.38f)) ];
			Body->AddSlot().AutoHeight()[ Row(TEXT("ec:debug"), FText::FromString(TEXT("Combat debug tools...")), []() { return false; }, [W]() { if (W.IsValid()) W->OpenECMenu(); }) ];
		}
		Body->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[ Row(TEXT("diagnostics"), FText::FromString(TEXT("Diagnostics overlay")), [W]() { return W.IsValid() && W->bShowDiagnostics; }, [W]() { if (W.IsValid()) W->ToggleDiagnostics(); }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("resume"), FText::FromString(TEXT("Resume")), []() { return false; }, [W]() { if (W.IsValid()) W->CloseMenus(); }) ];
		Show(Host, FText::FromString(TEXT("Debug tests  (F4)")), Body, FLinearColor(0.063f, 0.063f, 0.063f, 0.75f), true);
	}

	// SM64 Steve Movement settings: every row cycles its value, saves it (Config ini) and pushes it to Java.
	void OpenSm64Menu(ACrbHost* Host)
	{
		RemoveWidget(); Rows.Reset();
		MenuKind = EKind::Sm64;
		TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
		TWeakObjectPtr<ACrbHost> W = Host;
		auto Apply = [W]() { if (!W.IsValid()) return; W->SaveConfig(); W->PushSm64Config(); OpenSm64Menu(W.Get()); };
		auto Cycle = [](float V, std::initializer_list<float> Steps) { TArray<float> A(Steps); for (int32 I = 0; I < A.Num(); ++I) if (V < A[I] - 0.01f) return A[I]; return A[0]; };
		auto Pct = [](float V) { return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(V * 100.f)); };
		auto OnOff = [](bool B) { return B ? FString(TEXT("ON")) : FString(TEXT("OFF")); };
		Body->AddSlot().AutoHeight()[ Label(TEXT("Movement physics run in Minecraft (Java); Steve and the camera are drawn here. Click a row to change it."), FLinearColor(0.7f, 0.9f, 1.f)) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("sm64:enabled"), FText::FromString(FString(TEXT("SM64 Steve Movement: ")) + OnOff(Host->bSm64Enabled)), [W]() { return W.IsValid() && W->bSm64Enabled; }, [W]() { if (W.IsValid()) { W->ToggleSm64(); OpenSm64Menu(W.Get()); } }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("sm64:speed"), FText::FromString(TEXT("Run speed: ") + Pct(Host->Sm64SpeedMultiplier)), []() { return false; }, [W, Apply, Cycle]() { if (W.IsValid()) { W->Sm64SpeedMultiplier = Cycle(W->Sm64SpeedMultiplier, { 0.5f, 0.75f, 1.f, 1.25f, 1.5f }); Apply(); } }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("sm64:jump"), FText::FromString(TEXT("Jump strength: ") + Pct(Host->Sm64JumpMultiplier)), []() { return false; }, [W, Apply, Cycle]() { if (W.IsValid()) { W->Sm64JumpMultiplier = Cycle(W->Sm64JumpMultiplier, { 0.75f, 1.f, 1.25f, 1.5f }); Apply(); } }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("sm64:gravity"), FText::FromString(TEXT("Gravity: ") + Pct(Host->Sm64GravityMultiplier)), []() { return false; }, [W, Apply, Cycle]() { if (W.IsValid()) { W->Sm64GravityMultiplier = Cycle(W->Sm64GravityMultiplier, { 0.5f, 0.75f, 1.f, 1.25f }); Apply(); } }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("sm64:wallkick"), FText::FromString(TEXT("Wall kicks: ") + OnOff(Host->bSm64WallKicks)), [W]() { return W.IsValid() && W->bSm64WallKicks; }, [W, Apply]() { if (W.IsValid()) { W->bSm64WallKicks = !W->bSm64WallKicks; Apply(); } }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("sm64:groundpound"), FText::FromString(TEXT("Ground pound (crouch in the air): ") + OnOff(Host->bSm64GroundPound)), [W]() { return W.IsValid() && W->bSm64GroundPound; }, [W, Apply]() { if (W.IsValid()) { W->bSm64GroundPound = !W->bSm64GroundPound; Apply(); } }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("sm64:longjump"), FText::FromString(TEXT("Long jump (run, crouch, jump): ") + OnOff(Host->bSm64LongJump)), [W]() { return W.IsValid() && W->bSm64LongJump; }, [W, Apply]() { if (W.IsValid()) { W->bSm64LongJump = !W->bSm64LongJump; Apply(); } }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("sm64:fall"), FText::FromString(TEXT("Vanilla fall damage: ") + OnOff(Host->bSm64VanillaFallDamage)), [W]() { return W.IsValid() && W->bSm64VanillaFallDamage; }, [W, Apply]() { if (W.IsValid()) { W->bSm64VanillaFallDamage = !W->bSm64VanillaFallDamage; Apply(); } }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("sm64:walk"), FText::FromString(TEXT("Hold Ctrl to walk: ") + OnOff(Host->bSm64CtrlWalks)), [W]() { return W.IsValid() && W->bSm64CtrlWalks; }, [W, Apply]() { if (W.IsValid()) { W->bSm64CtrlWalks = !W->bSm64CtrlWalks; Apply(); } }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("sm64:skin"), FText::FromString(Host->bSm64SteveSkin ? TEXT("Skin: Steve (Minecraft default, 1:1)") : TEXT("Skin: your Minecraft account skin")), [W]() { return W.IsValid() && W->bSm64SteveSkin; }, [W, Apply]() { if (W.IsValid()) { W->bSm64SteveSkin = !W->bSm64SteveSkin; Apply(); } }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("sm64:height"), FText::FromString(FString::Printf(TEXT("Steve size: %.1f blocks tall"), Host->Sm64ModelHeight / 100.f)), []() { return false; }, [W, Apply, Cycle]() { if (W.IsValid()) { W->Sm64ModelHeight = Cycle(W->Sm64ModelHeight, { 120.f, 140.f, 160.f, 180.f }); Apply(); } }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("sm64:camera"), FText::FromString(FString::Printf(TEXT("Camera distance: %.1f blocks"), Host->Sm64CameraDistance / 100.f)), []() { return false; }, [W, Apply, Cycle]() { if (W.IsValid()) { W->Sm64CameraDistance = Cycle(W->Sm64CameraDistance, { 300.f, 420.f, 600.f, 800.f }); Apply(); } }) ];
		Body->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[ Row(TEXT("sm64:course"), FText::FromString(TEXT("Build / remove the SM64 practice course (test world)")), [W]() { return W.IsValid() && W->ActiveFixture == TEXT("sm64"); }, [W]() { if (W.IsValid()) { W->ToggleFixture(TEXT("sm64")); W->CloseMenus(); } }) ];
		Body->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[ Label(TEXT("Controls: WASD stick (camera relative), Space jump (A), Shift crouch (Z). Jump on landing to chain double and triple jumps; reverse at speed and jump for a side flip; crouch + jump = backflip; run, crouch, jump = long jump; jump into a wall then jump again = wall kick; crouch in the air = ground pound."), FLinearColor(0.75f, 0.75f, 0.75f)) ];
		Body->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[ Row(TEXT("sm64:back"), FText::FromString(TEXT("Back to Mods")), []() { return false; }, [W]() { if (W.IsValid()) { W->CloseMenus(); W->ToggleModMenu(); } }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("resume"), FText::FromString(TEXT("Resume")), []() { return false; }, [W]() { if (W.IsValid()) W->CloseMenus(); }) ];
		Show(Host, FText::FromString(TEXT("SM64 Steve Movement")), Body, FLinearColor(0.063f, 0.063f, 0.063f, 0.75f), true);
	}

	// Minecraft x Elden Combat: controls and the mod's debug tools. Every action goes through ACrbHost::ECCommand, which
	// refuses when the mod is off; turning the mod off closes this menu (ECShutdown).
	void OpenECMenu(ACrbHost* Host)
	{
		RemoveWidget(); Rows.Reset();
		MenuKind = EKind::EC;
		TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
		TWeakObjectPtr<ACrbHost> W = Host;
		auto Flag = [W](const TCHAR* Key, bool bValue) { if (!W.IsValid()) return; TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetBoolField(Key, bValue); W->ECCommand(TEXT("ec.debug"), A); };
		auto Op = [W](const TCHAR* OpName, TSharedPtr<FJsonObject> A) { if (W.IsValid()) W->ECCommand(OpName, A); };
		auto Spawn = [W](const TCHAR* Kind, bool bPassive) { if (!W.IsValid()) return; TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("kind"), Kind); A->SetNumberField(TEXT("dist"), 4); A->SetBoolField(TEXT("passive"), bPassive); W->ECCommand(TEXT("ec.dummy"), A); };
		const FCrbECState& E = Host->GetState().EC;
		Body->AddSlot().AutoHeight()[ Label(TEXT("Controls: LMB light attack (chain for combos), R heavy (hold to charge), RMB guard (shield in the offhand blocks fully), F parry, C or Left Alt dodge roll (no direction = backstep), Q or middle mouse lock-on, flick the mouse / wheel to switch target. Light attack a staggered enemy = riposte."), FLinearColor(0.75f, 0.75f, 0.75f)) ];
		Body->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[ Label(TEXT("Debug tools (this mod only)"), FLinearColor(0.93f, 0.78f, 0.38f)) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("ec:infinite"), FText::FromString(TEXT("Infinite stamina")), [W]() { return W.IsValid() && W->GetState().EC.bInfinite; }, [W, Flag]() { if (W.IsValid()) Flag(TEXT("infinite"), !W->GetState().EC.bInfinite); }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("ec:hitboxes"), FText::FromString(TEXT("Show attack arcs (particles)")), [W]() { return W.IsValid() && W->GetState().EC.bHitboxes; }, [W, Flag]() { if (W.IsValid()) Flag(TEXT("hitboxes"), !W->GetState().EC.bHitboxes); }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("ec:overlay"), FText::FromString(TEXT("Combat state overlay")), [W]() { return W.IsValid() && W->bECDebugOverlay; }, [W]() { if (W.IsValid()) W->bECDebugOverlay = !W->bECDebugOverlay; }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("ec:kit"), FText::FromString(TEXT("Give weapon kit + shield (test worlds)")), []() { return false; }, [Op]() { Op(TEXT("ec.kit"), MakeShared<FJsonObject>()); }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("ec:zombie"), FText::FromString(TEXT("Spawn training zombie")), []() { return false; }, [Spawn]() { Spawn(TEXT("zombie"), false); }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("ec:vindicator"), FText::FromString(TEXT("Spawn vindicator (hits hard)")), []() { return false; }, [Spawn]() { Spawn(TEXT("vindicator"), false); }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("ec:golem"), FText::FromString(TEXT("Spawn iron golem (high poise, felled banner)")), []() { return false; }, [Spawn]() { Spawn(TEXT("golem"), false); }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("ec:dummy"), FText::FromString(TEXT("Spawn passive dummy (no AI)")), []() { return false; }, [Spawn]() { Spawn(TEXT("zombie"), true); }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("ec:clear"), FText::FromString(FString::Printf(TEXT("Remove training enemies (%d)"), E.Dummies)), []() { return false; }, [Op]() { Op(TEXT("ec.clearDummies"), MakeShared<FJsonObject>()); }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("ec:stagger"), FText::FromString(TEXT("Break the target's poise (stagger)")), []() { return false; }, [Op]() { Op(TEXT("ec.breakPoise"), MakeShared<FJsonObject>()); }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("ec:refill"), FText::FromString(TEXT("Refill stamina + heal")), []() { return false; }, [Op]() { Op(TEXT("ec.stamina"), MakeShared<FJsonObject>()); Op(TEXT("ec.heal"), MakeShared<FJsonObject>()); }) ];
		Body->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[ Row(TEXT("ec:off"), FText::FromString(TEXT("Turn the combat mod off")), []() { return false; }, [W]() { if (W.IsValid()) { W->ToggleEldenCombat(); } }) ];
		Body->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[ Row(TEXT("ec:back"), FText::FromString(TEXT("Back to Mods")), []() { return false; }, [W]() { if (W.IsValid()) { W->CloseMenus(); W->ToggleModMenu(); } }) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("resume"), FText::FromString(TEXT("Resume")), []() { return false; }, [W]() { if (W.IsValid()) W->CloseMenus(); }) ];
		Show(Host, FText::FromString(Host->IsEldenRingStyle() ? TEXT("Elden Ring Combat (Steve)") : TEXT("Minecraft \u00d7 Elden Combat")), Body, FLinearColor(0.063f, 0.063f, 0.063f, 0.75f), true);
	}

	void Refresh(ACrbHost* Host) { if (MenuKind == EKind::Debug && MenuRoot.IsValid()) OpenDebugMenu(Host); }

	void OpenPauseMenu(ACrbHost* Host)
	{
		RemoveWidget(); Rows.Reset();
		MenuKind = EKind::Pause;
		TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
		TWeakObjectPtr<ACrbHost> W = Host;
		Body->AddSlot().AutoHeight()[ Row(TEXT("pause:back"), FText::FromString(TEXT("Back to Game")), []() { return false; }, [W]() { if (W.IsValid()) W->CloseMenus(); }) ];
		Body->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[ Row(TEXT("pause:modes"), FText::FromString(Host->IsZombiesActive() ? TEXT("Game Modes...  (Zombies running)") : TEXT("Game Modes...")), [W]() { return W.IsValid() && W->IsZombiesActive(); }, [W]() { if (W.IsValid()) W->OpenGameModesMenu(); }) ];
		Body->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[ Row(TEXT("pause:maps"), FText::FromString(TEXT("Maps...")), []() { return false; }, [W]() { if (W.IsValid()) W->OpenMapsMenu(); }) ];
		Body->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[ Row(TEXT("pause:mods"), FText::FromString(TEXT("Mods...")), []() { return false; }, [W]() { if (W.IsValid()) { W->CloseMenus(); W->ToggleModMenu(); } }) ];
		Body->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[ Row(TEXT("pause:quit"), FText::FromString(TEXT("Quit Game  (saves the world, closes Minecraft)")), []() { return false; }, [W]() { if (W.IsValid()) W->QuitGame(); }) ];
		Show(Host, FText::FromString(TEXT("Game Menu")), Body);
	}

	void OpenGameModesMenu(ACrbHost* Host)
	{
		RemoveWidget(); Rows.Reset();
		MenuKind = EKind::Pause;
		TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
		TWeakObjectPtr<ACrbHost> W = Host;
		const FCrbZmState& Z = Host->GetState().Zm;
		Body->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[ Label(TEXT("New games built from Minecraft's core mechanics. Java runs the match; your world is restored when it ends."), FLinearColor(0.75f, 0.75f, 0.75f)) ];
		if (!Z.IsActive())
			Body->AddSlot().AutoHeight()[ Row(TEXT("zm:start"), FText::FromString(TEXT("Zombies  -  survive the rounds (Guns++ weapons, F to buy / rebuild)")), []() { return false; }, [W]() { if (W.IsValid()) { W->StartZombies(); W->CloseMenus(); } }) ];
		else
		{
			Body->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[ Label(FString::Printf(TEXT("Zombies: round %d, %d points, %d kills"), Z.Round, Z.Points, Z.Kills), FLinearColor(0.9f, 0.2f, 0.15f)) ];
			Body->AddSlot().AutoHeight()[ Row(TEXT("zm:stop"), FText::FromString(TEXT("End Zombies match (restores inventory and game mode)")), []() { return false; }, [W]() { if (W.IsValid()) { W->StopZombies(); W->CloseMenus(); } }) ];
		}
		Body->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[ Label(TEXT("Controls: LMB knife / RMB fire (hold a Guns++ gun), F buy / open / perk / crate, hold F at a window to rebuild, R reload (empty guns reload by themselves), 1-9 or the wheel to switch guns."), FLinearColor(0.7f, 0.9f, 1.f)) ];
		Body->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[ Row(TEXT("modes:back"), FText::FromString(TEXT("Back")), []() { return false; }, [W]() { if (W.IsValid()) { W->CloseMenus(); W->TogglePause(); } }) ];
		Show(Host, FText::FromString(TEXT("Game Modes")), Body);
	}

	void OpenMapsMenu(ACrbHost* Host)
	{
		RemoveWidget(); Rows.Reset();
		MenuKind = EKind::Maps;
		TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
		TWeakObjectPtr<ACrbHost> W = Host;
		const FCrbState& S = Host->GetState();
		Body->AddSlot().AutoHeight()[ Label(FString(TEXT("World generation is off for custom maps: only the map's own chunks exist. Put map saves or modpack zips in the Crossover-Rebuilt\\Maps folder.")), CrbMcUi::Rgb(0xA0A0A0)) ];
		if (!S.MapLoading.IsEmpty() || !S.MapStatus.IsEmpty())
			Body->AddSlot().AutoHeight()[ Label(FString::Printf(TEXT("%s %s"), *S.MapStatus, S.MapProgress > 0 && S.MapProgress < 100 ? *FString::Printf(TEXT("(%d%%)"), S.MapProgress) : TEXT("")), CrbMcUi::Rgb(0xFFFF55)) ];
		if (!Host->bMapListReceived) Body->AddSlot().AutoHeight()[ Label(TEXT("(reading the Maps folder...)"), CrbMcUi::Rgb(0xE0E0E0)) ];
		for (const ACrbHost::FMapInfo& M : Host->MapList)
		{
			const FString Id = M.Id;
			FString Nice = M.Name; if (Nice.Len() > 0) Nice[0] = FChar::ToUpper(Nice[0]);
			const FString Text = Nice + (M.Source == TEXT("generated") ? FString() : (M.bImported ? FString(TEXT("  (imported)")) : FString(TEXT("  (") + M.Source + TEXT(", first load imports it)"))));
			Body->AddSlot().AutoHeight()[ Row(TEXT("map:") + Id, FText::FromString(Text), [W, Id]() { return W.IsValid() && W->GetState().MapCurrent == Id; }, [W, Id]() { if (W.IsValid()) { W->LoadMap(Id); W->CloseMenus(); } }) ];
		}
		Body->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[ Row(TEXT("maps:back"), FText::FromString(TEXT("Back")), []() { return false; }, [W]() { if (W.IsValid()) { W->CloseMenus(); W->TogglePause(); } }) ];
		Show(Host, FText::FromString(TEXT("Select Map")), Body, FLinearColor(0.063f, 0.063f, 0.063f, 0.75f), true);
	}

	void RefreshMaps(ACrbHost* Host) { if (MenuKind == EKind::Maps && MenuRoot.IsValid()) OpenMapsMenu(Host); }

	void OpenDeathMenu(ACrbHost* Host)
	{
		RemoveWidget(); Rows.Reset();
		MenuKind = EKind::Death;
		TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
		TWeakObjectPtr<ACrbHost> W = Host;
		const FCrbState& S = Host->GetState();
		if (!S.DeathMessage.IsEmpty()) Body->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[ Label(S.DeathMessage, FLinearColor::White) ];
		Body->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[ Label(FString::Printf(TEXT("Score: %d"), S.Score), FLinearColor(1, 1, 0.33f)) ];
		Body->AddSlot().AutoHeight()[ Row(TEXT("death:respawn"), FText::FromString(TEXT("Respawn")), []() { return false; }, [W]() { if (W.IsValid()) W->Respawn(); }) ];
		Body->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[ Row(TEXT("death:quit"), FText::FromString(TEXT("Quit Game")), []() { return false; }, [W]() { if (W.IsValid()) W->QuitGame(); }) ];
		Show(Host, FText::FromString(TEXT("You Died!")), Body, FLinearColor(0.31f, 0.0f, 0.0f, 0.55f));
	}

	void OpenHerobrineMenu(ACrbHost* Host)
	{
		RemoveWidget(); Rows.Reset();
		MenuKind = EKind::Herobrine;
		TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
		TWeakObjectPtr<ACrbHost> W = Host;
		const TSharedPtr<FJsonObject> St = Host->HerobrineStatus;
		FString Version; if (St.IsValid()) St->TryGetStringField(TEXT("version"), Version);
		double Rig = 0; if (St.IsValid()) St->TryGetNumberField(TEXT("herobrineEntities"), Rig);
		Body->AddSlot().AutoHeight()[ Label(FString::Printf(TEXT("From The Fog %s by Lunar Eclipse Studios (CC BY-NC-SA 4.0). Settings use the mod's own config. Herobrine entities now: %d"), *Version, (int32)Rig), FLinearColor(0.75f, 0.75f, 0.75f)) ];
		const TArray<TSharedPtr<FJsonValue>>* Opts = nullptr;
		if (!St.IsValid() || !St->TryGetArrayField(TEXT("options"), Opts))
			Body->AddSlot().AutoHeight()[ Label(TEXT("(reading settings from Minecraft...)"), FLinearColor(0.88f, 0.88f, 0.88f)) ];
		else
			for (const TSharedPtr<FJsonValue>& V : *Opts)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				if (!O.IsValid()) continue;
				FString Key, Label; double Score = 0;
				O->TryGetStringField(TEXT("key"), Key); O->TryGetStringField(TEXT("label"), Label); O->TryGetNumberField(TEXT("score"), Score);
				const int32 Sc = (int32)Score;
				FString ValueText, Next;
				if (Key == TEXT("sighting_chance"))
				{
					static const TCHAR* Names[] = { TEXT("?"), TEXT("Common"), TEXT("Uncommon"), TEXT("Rare") };
					static const TCHAR* Ids[] = { TEXT("1_common"), TEXT("2_uncommon"), TEXT("3_rare"), TEXT("1_common") };
					const int32 C = FMath::Clamp(Sc, 0, 3);
					ValueText = Names[C]; Next = Ids[C];
				}
				else if (Key == TEXT("start_delay"))
				{
					ValueText = Sc < 0 ? TEXT("Off (haunting starts now)") : FString::Printf(TEXT("%d days"), Sc);
					Next = Sc < 0 ? TEXT("reset") : (Sc == 0 ? TEXT("off") : TEXT("remove"));
				}
				else { const bool bOn = Sc == 1; ValueText = bOn ? TEXT("ON") : TEXT("OFF"); Next = bOn ? TEXT("false") : TEXT("true"); }
				Body->AddSlot().AutoHeight()[ Row(TEXT("herobrine:") + Key, FText::FromString(FString::Printf(TEXT("%s: %s"), *Label, *ValueText)), [Sc, Key]() { return Key != TEXT("sighting_chance") && Key != TEXT("start_delay") && Sc == 1; },
					[W, Key, Next]() { if (!W.IsValid()) return; TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("key"), Key); A->SetStringField(TEXT("value"), Next); W->HerobrineCommand(TEXT("herobrine.config"), A); }) ];
			}
		Body->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[ Label(TEXT("Start a sighting now (the mod's own admin functions):"), FLinearColor(1.f, 0.6f, 0.6f)) ];
		const TCHAR* Kinds[][2] = { { TEXT("fake"), TEXT("Show Herobrine near you") }, { TEXT("stalking"), TEXT("Stalking sighting") }, { TEXT("creeping"), TEXT("Creeping sighting") }, { TEXT("lurking"), TEXT("Lurking (underground)") }, { TEXT("nightmare"), TEXT("Nightmare") } };
		for (auto& K : Kinds)
		{
			const FString Id = K[0];
			Body->AddSlot().AutoHeight()[ Row(TEXT("herobrine:summon:") + Id, FText::FromString(K[1]), []() { return false; },
				[W, Id]() { if (!W.IsValid()) return; TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("kind"), Id); W->HerobrineCommand(TEXT("herobrine.summon"), A); W->CloseMenus(); }) ];
		}
		Body->AddSlot().AutoHeight()[ Row(TEXT("herobrine:clear"), FText::FromString(TEXT("Remove Herobrine")), []() { return false; }, [W]() { if (W.IsValid()) W->HerobrineCommand(TEXT("herobrine.clear")); }) ];
		Body->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[ Row(TEXT("resume"), FText::FromString(TEXT("Back to game")), []() { return false; }, [W]() { if (W.IsValid()) W->CloseMenus(); }) ];
		Show(Host, FText::FromString(TEXT("Herobrine  -  From The Fog")), Body, FLinearColor(0.063f, 0.063f, 0.063f, 0.75f), true);
	}

	void RefreshHerobrine(ACrbHost* Host) { if (MenuKind == EKind::Herobrine && MenuRoot.IsValid()) OpenHerobrineMenu(Host); }

	void Close(ACrbHost* Host)
	{
		RemoveWidget(); Rows.Reset(); MenuKind = EKind::None; AnimatedKind = EKind::None;
		ACrbHost* H = Host ? Host : MenuOwner.Get();
		if (H && H->GetWorld())
			if (APlayerController* PC = H->GetWorld()->GetFirstPlayerController())
			{
				PC->SetInputMode(FInputModeGameOnly());
				PC->bShowMouseCursor = false;
			}
		if (Host && FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}

	bool IsRowVisible(const TSharedPtr<SButton>& B);

	bool RowCenter(const FString& Key, FVector2D& Out)
	{
		const TSharedPtr<SButton>* B = Rows.Find(Key);
		if (!B || !B->IsValid()) return false;
		const FGeometry& G = (*B)->GetCachedGeometry();
		if (G.GetLocalSize().X <= 0 || !IsRowVisible(*B))
		{
			// Rows below the fold of a long menu have no painted geometry yet: scroll them into view first
			// (what a user does with the wheel); the caller retries next frame.
			if (Scroll.IsValid()) Scroll->ScrollDescendantIntoView(*B, true, EDescendantScrollDestination::IntoView);
			return false;
		}
		Out = G.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f));
		return true;
	}

	bool IsRowVisible(const TSharedPtr<SButton>& B)
	{
		if (!Scroll.IsValid()) return true;
		const FGeometry& SG = Scroll->GetCachedGeometry(); const FGeometry& BG = B->GetCachedGeometry();
		const FVector2D Top = BG.GetAbsolutePosition(), Bottom = BG.GetAbsolutePositionAtCoordinates(FVector2D(1, 1));
		const FVector2D STop = SG.GetAbsolutePosition(), SBottom = SG.GetAbsolutePositionAtCoordinates(FVector2D(1, 1));
		return Top.Y >= STop.Y - 1 && Bottom.Y <= SBottom.Y + 1;
	}

	TArray<FString> RowKeys() { TArray<FString> K; Rows.GetKeys(K); return K; }

	bool ClickRow(const FString& Key)
	{
		// Delivers a real left-button press to the row widget at its on-screen centre through SButton's own mouse
		// handler (the code a physical click runs). Slate's RoutePointerDownEvent asserted on synthesized paths, and
		// OS-level routing fails whenever another window covers the game during unattended tests.
		const TSharedPtr<SButton>* Found = Rows.Find(Key);
		if (!Found || !Found->IsValid() || !FSlateApplication::IsInitialized()) return false;
		const TSharedRef<SButton> Btn = Found->ToSharedRef(); // keep the widget alive for the whole press/release
		const FGeometry G = Btn->GetCachedGeometry();
		if (G.GetLocalSize().X <= 0 || !Btn->IsEnabled() || !IsRowVisible(*Found)) return false;
		const FVector2D Pos = G.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f));
		if (!G.IsUnderLocation(Pos)) return false;
		const TSet<FKey> Pressed({ EKeys::LeftMouseButton });
		FPointerEvent Down(0, FSlateApplicationBase::CursorPointerIndex, Pos, Pos, Pressed, EKeys::LeftMouseButton, 0, FModifierKeysState());
		const FReply R = Btn->OnMouseButtonDown(G, Down);
		FPointerEvent Up(0, FSlateApplicationBase::CursorPointerIndex, Pos, Pos, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState());
		Btn->OnMouseButtonUp(G, Up);
		return R.IsEventHandled();
	}

}
