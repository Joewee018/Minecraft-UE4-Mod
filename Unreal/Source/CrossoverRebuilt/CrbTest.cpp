#include "CrbTest.h"
#include "CrbHost.h"
#include "CrbPawn.h"
#include "CrbHUD.h"
#include "CrbMenus.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "HighResScreenshot.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "InputCoreTypes.h"
#include "ImageUtils.h"
#include "Containers/Ticker.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"

namespace { const double SpawnX = 0.5, SpawnY = -60.0, SpawnZ = 0.5; }

FCrbTest::FCrbTest(ACrbHost* InHost, const FString& InSuite) : Host(InHost), Suite(InSuite)
{
	FParse::Value(FCommandLine::Get(), TEXT("-CrbReport="), ReportPath);
	FParse::Value(FCommandLine::Get(), TEXT("-CrbShots="), ShotDir);
	ReportPath = ReportPath.TrimQuotes(); ShotDir = ShotDir.TrimQuotes();
	if (ReportPath.IsEmpty()) ReportPath = FPaths::ProjectSavedDir() / TEXT("CrbTestReport.json");
	if (ShotDir.IsEmpty()) ShotDir = FPaths::GetPath(ReportPath) / TEXT("Shots");
	bQuit = FParse::Param(FCommandLine::Get(), TEXT("CrbQuit"));
	Metrics = MakeShared<FJsonObject>();
	Host->bAvatarEnabled = false; // tests start from the vanilla presentation (not saved; the avatar tests toggle it via the menu)
	StartTime = FPlatformTime::Seconds();
	if (Suite == TEXT("visual")) BuildVisual(); else Build();
}

FCrbTest::~FCrbTest()
{
	if (bDelegateBound && GEngine && GEngine->GameViewport) GEngine->GameViewport->OnScreenshotCaptured().RemoveAll(this);
}

void FCrbTest::Add(const FString& Name, TFunction<void()> Begin, TFunction<bool(float)> Update, float Timeout)
{
	FStep S; S.Name = Name; S.Begin = MoveTemp(Begin); S.Update = MoveTemp(Update); S.Timeout = Timeout;
	Steps.Add(MoveTemp(S));
}

void FCrbTest::Wait(float Seconds) { Add(FString::Printf(TEXT("wait %.1fs"), Seconds), [] {}, [Seconds](float T) { return T >= Seconds; }, Seconds + 5); }
void FCrbTest::Pass(const FString& C) { Checks.Add(C); UE_LOG(LogCrb, Display, TEXT("TEST PASS: %s"), *C); }
void FCrbTest::Fail(const FString& C) { Failures.Add(C); UE_LOG(LogCrb, Error, TEXT("TEST FAIL: %s"), *C); }

void FCrbTest::Shot(const FString& Name)
{
	const FString Path = ShotDir / (Name + TEXT(".png"));
	if (!bDelegateBound && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->OnScreenshotCaptured().AddRaw(this, &FCrbTest::OnShot);
		bDelegateBound = true;
	}
	PendingShot = Path;
	FScreenshotRequest::RequestScreenshot(Path, true, false);
	Shots.Add(Path);
}

void FCrbTest::AimAt(double X, double Y, double Z)
{
	const FCrbState& S = Host->GetState();
	const double DX = X - S.X, DY = Y - (S.Y + S.Eye), DZ = Z - S.Z;
	Host->LookYaw = (float)FMath::RadiansToDegrees(FMath::Atan2(-DX, DZ));
	Host->LookPitch = (float)FMath::RadiansToDegrees(FMath::Atan2(DY, FMath::Sqrt(DX * DX + DZ * DZ)));
}

FString FCrbTest::Diag() const
{
	const FCrbState& S = Host->GetState();
	FString Lane;
	for (int32 Z = 0; Z <= 6; ++Z) for (int32 X = -1; X <= 1; ++X) for (int32 Y = -61; Y <= -59; ++Y)
	{
		const int32 Id = Host->World.StateAt(FIntVector(X, Y, Z));
		const FCrbModel* M = Host->World.Model(Id);
		const FString Name = Id == 0 ? FString(TEXT("air")) : (M ? M->State : FString::FromInt(Id));
		const bool bExpected = Y == -61 ? Name.Contains(TEXT("grass_block")) : Id == 0; // floor below, air above
		if (!bExpected) Lane += FString::Printf(TEXT(" (%d,%d,%d)=%s"), X, Y, Z, *Name);
	}
	return FString::Printf(TEXT(" [menuOpen=%d pause=%d death=%d inv=%d mods=%d debug=%d gamemode=%s] [java pos=(%.2f,%.2f,%.2f) yaw=%.0f ground=%d sneak=%d sprint=%d water=%d screen='%s' fixture='%s' sel=%d; lane:%s]"),
		Host->IsMenuOpen(), Host->bPauseOpen, Host->bDeathOpen, Host->bInventoryOpen, Host->bModMenuOpen, Host->bDebugMenuOpen, *S.GameMode,
		S.X, S.Y, S.Z, S.Yaw, S.bOnGround, S.bSneak, S.bSprint, S.bInWater, *S.Screen, *S.Fixture, S.Selected, Lane.IsEmpty() ? TEXT(" clear") : *Lane);
}

bool FCrbTest::ClickAt(const FVector2D& Pos)
{
	if (!FSlateApplication::IsInitialized()) return false;
	FSlateApplication& S = FSlateApplication::Get();
	S.SetCursorPos(Pos);
	const TSet<FKey> Pressed({ EKeys::LeftMouseButton });
	FPointerEvent Move(0, FSlateApplicationBase::CursorPointerIndex, Pos, Pos, TSet<FKey>(), EKeys::Invalid, 0, FModifierKeysState());
	S.ProcessMouseMoveEvent(Move);
	FPointerEvent Down(0, FSlateApplicationBase::CursorPointerIndex, Pos, Pos, Pressed, EKeys::LeftMouseButton, 0, FModifierKeysState());
	S.ProcessMouseButtonDownEvent(nullptr, Down);
	FPointerEvent Up(0, FSlateApplicationBase::CursorPointerIndex, Pos, Pos, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState());
	S.ProcessMouseButtonUpEvent(Up);
	return true;
}

void FCrbTest::PressKey(const FKey& Key)
{
	if (!FSlateApplication::IsInitialized()) return;
	FSlateApplication& S = FSlateApplication::Get();
	S.SetAllUserFocusToGameViewport();
	FKeyEvent E(Key, FModifierKeysState(), 0, false, 0, 0);
	S.ProcessKeyDownEvent(E);
	S.ProcessKeyUpEvent(E);
}

void FCrbTest::OnShot(int32 W, int32 H, const TArray<FColor>& Px)
{
	// Luma statistics over the 3D view (HUD rows excluded) of exactly the frame saved as evidence.
	FLuma L; double Sum = 0; int64 N = 0, Blown = 0, Dark = 0;
	for (int32 Y = H / 10; Y < H * 7 / 10; Y += 2)
		for (int32 X = 0; X < W; X += 2)
		{
			const FColor C = Px[Y * W + X];
			const float Luma = (0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B) / 255.f;
			Sum += Luma; ++N;
			if (C.R > 250 && C.G > 250 && C.B > 250) ++Blown;
			if (Luma < 0.03f) ++Dark;
		}
	if (N) { L.Mean = Sum / N; L.Blown = (float)Blown / N; L.Dark = (float)Dark / N; L.bOk = true; }
	LastShotLuma = L;
	TArray<FColor> Copy = Px;
	for (FColor& C : Copy) C.A = 255;
	TArray<uint8> Png;
	FImageUtils::CompressImageArray(W, H, Copy, Png);
	if (!PendingShot.IsEmpty()) FFileHelper::SaveArrayToFile(Png, *PendingShot);
	Metrics->SetStringField(TEXT("luma:") + FPaths::GetBaseFilename(PendingShot), FString::Printf(TEXT("mean=%.3f blown=%.4f dark=%.4f"), L.Mean, L.Blown, L.Dark));
	PendingShot.Reset();
}

FCrbTest::FLuma FCrbTest::MeasureViewportUnused()
{
	FLuma L;
	if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport) return L;
	TArray<FColor> Px;
	FViewport* V = GEngine->GameViewport->Viewport;
	const FIntPoint Size = V->GetSizeXY();
	if (!V->ReadPixels(Px) || Px.Num() != Size.X * Size.Y) return L;
	double Sum = 0; int64 N = 0, Blown = 0, Dark = 0;
	for (int32 Y = Size.Y / 10; Y < Size.Y * 7 / 10; Y += 2)
		for (int32 X = 0; X < Size.X; X += 2)
		{
			const FColor C = Px[Y * Size.X + X];
			const float Luma = (0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B) / 255.f;
			Sum += Luma; ++N;
			if (C.R > 250 && C.G > 250 && C.B > 250) ++Blown;
			if (Luma < 0.03f) ++Dark;
		}
	if (N == 0) return L;
	L.Mean = Sum / N; L.Blown = (float)Blown / N; L.Dark = (float)Dark / N; L.bOk = true;
	return L;
}

void FCrbTest::AddCommand(const FString& Op, TSharedPtr<FJsonObject> Args, TFunction<void(bool, const FCrbResult*)> OnDone, float Timeout)
{
	Add(TEXT("command ") + Op, [this, Op, Args] { PendingId = Host->SendCommand(Op, Args); },
		[this, OnDone, Timeout](float T)
		{
			const FCrbResult* R = Host->FindResult(PendingId);
			if (R) { OnDone(R->bOk, R); return true; }
			if (T > Timeout - 0.1f) { OnDone(false, nullptr); return true; }
			return false;
		}, Timeout + 1);
}

void FCrbTest::AddClickRow(const FString& Key, const FString& Label)
{
	Add(TEXT("click ") + Key, [] {}, [this, Key, Label](float T)
	{
		FVector2D P;
		if (T > 0.15f && CrbMenus::RowCenter(Key, P)) { Check(CrbMenus::ClickRow(Key), FString::Printf(TEXT("%s: clicked menu row '%s' (Slate mouse down/up routed to the button at %.0f, %.0f)"), *Label, *Key, P.X, P.Y)); return true; }
		if (T > 5.f) { Fail(Label + TEXT(": menu row not found: ") + Key); return true; }
		return false;
	}, 8);
}

void FCrbTest::AddKey(const FKey& Key, const FString& Label)
{
	Add(TEXT("key ") + Key.ToString(), [this, Key] { PressKey(Key); }, [](float T) { return T > 0.3f; }, 5);
}

void FCrbTest::AddMovementCheck(const FString& Label)
{
	// Reset to the lane start (Java teleports), face +Z, walk 1.6 s (vanilla walk ~6.9 blocks; >5 required so a
	// partly blocked lane fails), then verify Java motion,
	// fresh pose frames, gait progress and that the vertices handed to the renderer kept changing.
	AddCommand(TEXT("player.reset"), nullptr, [this, Label](bool bOk, const FCrbResult*) { if (!bOk) Fail(Label + TEXT(": player.reset refused")); });
	Add(Label + TEXT(" settle"), [this] { Host->bInputOverride = true; Host->SetMove(0, 0); Host->LookYaw = 0; Host->LookPitch = 0; Host->ViewMode = 1; }, [](float T) { return T > 0.7f; }, 5);
	Add(Label + TEXT(" walk"), [this]
	{
		const FCrbState& S = Host->GetState();
		X0 = S.X; Z0 = S.Z; Seq0 = Host->Avatar.LastSeq; Walk0 = Host->Avatar.LastWalkPos; JavaSeq0 = S.PoseSeq; Hashes.Reset();
		Host->SetMove(1, 0);
	}, [this](float T)
	{
		Hashes.Add(Host->Avatar.LastBodyHash);
		return T > 1.6f;
	}, 6);
	Add(Label + TEXT(" verify"), [this] { Host->SetMove(0, 0); }, [this, Label](float T)
	{
		if (T < 0.15f) return false;
		const FCrbState& S = Host->GetState();
		const double DZ = S.Z - Z0;
		const int32 DSeq = Host->Avatar.LastSeq - Seq0;
		const float Gait = FMath::Abs(Host->Avatar.LastWalkPos - Walk0);
		const bool bThread = S.InputThread == TEXT("Render thread");
		const bool bOk = DZ > 5.0 && DSeq >= 20 && Gait > 1.f && Hashes.Num() >= 10 && Host->Avatar.BodyVertexCount > 0 && bThread;
		Check(bOk, FString::Printf(TEXT("%s: Unreal input moved the Java player %.2f blocks; %d new Java pose frames; gait +%.2f; %d distinct rendered body poses (%d vertices); input applied on '%s'%s"),
			*Label, DZ, DSeq, Gait, Hashes.Num(), Host->Avatar.BodyVertexCount, *S.InputThread, bOk ? TEXT("") : *Diag()));
		Metrics->SetStringField(TEXT("move:") + Label, FString::Printf(TEXT("dz=%.2f poses=%d gait=%.2f distinctBody=%d"), DZ, DSeq, Gait, Hashes.Num()));
		Host->ViewMode = 0;
		return true;
	}, 5);
}

void FCrbTest::Build()
{
	// 1. Connection and first world.
	Add(TEXT("connect"), [this] { Host->bInputOverride = true; Host->bSm64Enabled = false; Host->bCraft64Enabled = false; Host->bPhysicsPortalEnabled = false; /* the suites start vanilla; not saved */ }, [this](float T)
	{
		const bool bReady = Host->GetState().bValid && Host->Avatar.FramesReceived > 10 && Host->World.NumMeshed() >= 20 && Host->Textures.Get(TEXT("minecraft:textures/atlas/blocks.png")) != nullptr && !Host->McVersion.IsEmpty();
		if (bReady)
		{
			PoseErrors0 = Host->GetState().PoseErrors;
			Pass(FString::Printf(TEXT("Connected to Minecraft %s (Fabric Loader %s, Fabric API %s, bridge %s, %s); %d sections meshed; block atlas received"), *Host->McVersion, *Host->LoaderVersion, *Host->FabricApiVersion, *Host->BridgeVersion, *Host->Mappings, Host->World.NumMeshed()));
			Metrics->SetStringField(TEXT("minecraft"), Host->McVersion); Metrics->SetStringField(TEXT("loader"), Host->LoaderVersion);
			Metrics->SetStringField(TEXT("fabricApi"), Host->FabricApiVersion); Metrics->SetStringField(TEXT("bridge"), Host->BridgeVersion);
			return true;
		}
		if (T > 280.f) Fail(FString::Printf(TEXT("connect timeout: state=%d avatar=%d meshed=%d atlas=%d status=%s"), Host->GetState().bValid, Host->Avatar.FramesReceived, Host->World.NumMeshed(), Host->Textures.Get(TEXT("minecraft:textures/atlas/blocks.png")) != nullptr, *Host->StatusLine));
		return T > 280.f;
	}, 300);
	AddCommand(TEXT("world.info"), nullptr, [this](bool bOk, const FCrbResult* R)
	{
		bool bFlat = false, bDisp = false;
		if (R && R->Json.IsValid()) { R->Json->TryGetBoolField(TEXT("superflat"), bFlat); R->Json->TryGetBoolField(TEXT("disposable"), bDisp); }
		Check(bOk && bFlat && bDisp && R->Thread == TEXT("Server thread"), FString::Printf(TEXT("Disposable superflat test world confirmed by Java; world commands ran on '%s'"), R ? *R->Thread : TEXT("?")));
	});
	AddCommand(TEXT("fixture.off"), nullptr, [](bool, const FCrbResult*) {});
	TSharedPtr<FJsonObject> Noon = MakeShared<FJsonObject>(); Noon->SetNumberField(TEXT("time"), 6000);
	AddCommand(TEXT("time.set"), Noon, [](bool, const FCrbResult*) {});
	if (Suite == TEXT("zombies"))
	{
		BuildGlass();
		BuildZombies();
		BuildMaps();
		Add(TEXT("finish"), [this] { Finish(); }, [](float) { return true; }, 2);
		return;
	}
	if (Suite == TEXT("boot"))
	{
		// quick load check: the packaged game reaches a meshed, lit world with the player state live
		Add(TEXT("boot world"), [this] { Host->ViewMode = 0; Host->LookYaw = 20; Host->LookPitch = -12; }, [this](float T)
		{
			if (T < 3.f) return false;
			Check(Host->GetState().bValid && Host->World.NumMeshed() > 0, FString::Printf(TEXT("Game loaded: player state live, %d sections meshed, %d textures"), Host->World.NumMeshed(), Host->Textures.NumReady()));
			Shot(TEXT("00_boot_world"));
			return true;
		}, 6);
		Add(TEXT("finish"), [this] { Finish(); }, [](float) { return true; }, 2);
		return;
	}
	if (Suite == TEXT("ec") || Suite == TEXT("ecring"))
	{
		ECRing = Suite == TEXT("ecring");
		BuildEC();
		Add(TEXT("finish"), [this] { Finish(); }, [](float) { return true; }, 2);
		return;
	}
	if (Suite == TEXT("pp"))
	{
		BuildPP();
		Add(TEXT("finish"), [this] { Finish(); }, [](float) { return true; }, 2);
		return;
	}
	if (Suite == TEXT("craft64"))
	{
		BuildCraft64();
		Add(TEXT("finish"), [this] { Finish(); }, [](float) { return true; }, 2);
		return;
	}
	if (Suite == TEXT("sm64"))
	{
		BuildSm64();
		Add(TEXT("finish"), [this] { Finish(); }, [](float) { return true; }, 2);
		return;
	}
	if (Suite == TEXT("avatar"))
	{
		BuildAvatar();
		Add(TEXT("finish"), [this] { Finish(); }, [](float) { return true; }, 2);
		return;
	}
	AddCommand(TEXT("player.reset"), nullptr, [](bool, const FCrbResult*) {});
	Add(TEXT("daylight view"), [this] { Host->LookYaw = 20; Host->LookPitch = -12; Host->ViewMode = 0; }, [](float T) { return T > 2.5f; }, 5);
	Add(TEXT("daylight shot"), [this] { Shot(TEXT("01_daylight_first_person")); }, [this](float T)
	{
		if (T < 0.5f || !PendingShot.IsEmpty()) return false;
		DayLuma = MeasureViewport();
		Check(DayLuma.bOk && DayLuma.Mean > 0.18f && DayLuma.Mean < 0.85f && DayLuma.Blown < 0.03f, FString::Printf(TEXT("Daylight scene is readable: mean luma %.3f, blown-out %.2f%%, near-black %.2f%%"), DayLuma.Mean, DayLuma.Blown * 100, DayLuma.Dark * 100));
		Metrics->SetNumberField(TEXT("dayLuma"), DayLuma.Mean);
		return true;
	}, 5);

	// 2. Baseline movement + animation before any menu.
	AddMovementCheck(TEXT("Before opening the picker"));
	Add(TEXT("rear shot"), [this] { Host->ViewMode = 1; Host->SetMove(1, 0); }, [this](float T) { if (Cross(0.6f)) Shot(TEXT("02_walking_rear_view")); if (T > 1.0f) { Host->SetMove(0, 0); Host->ViewMode = 0; return true; } return false; }, 5);

	// 3. Debug picker: every fixture on, movement, off, movement; then Resume.
	AddKey(EKeys::F4, TEXT("F4"));
	Add(TEXT("picker open"), [] {}, [this](float T)
	{
		APlayerController* PC = Host->GetWorld()->GetFirstPlayerController();
		if (Host->bDebugMenuOpen && CrbMenus::IsOpen() && Host->Fixtures.Num() > 0 && T > 0.5f)
		{
			Check(PC && PC->bShowMouseCursor, FString::Printf(TEXT("F4 opened the mouse-driven picker with %d superflat fixtures and a visible cursor"), Host->Fixtures.Num()));
			Shot(TEXT("03_debug_picker"));
			return true;
		}
		if (T > 6.f) { Fail(TEXT("F4 did not open the debug picker")); return true; }
		return false;
	}, 8);
	AddClickRow(TEXT("resume"), TEXT("Picker"));
	Add(TEXT("resumed"), [] {}, [this](float T)
	{
		if (T < 0.3f) return false;
		APlayerController* PC = Host->GetWorld()->GetFirstPlayerController();
		Check(!Host->IsMenuOpen() && !CrbMenus::IsOpen() && PC && !PC->bShowMouseCursor, TEXT("Resume closed the picker and hid the cursor"));
		return true;
	}, 4);
	const TArray<FString> FixtureIds = { TEXT("shapes"), TEXT("course"), TEXT("items"), TEXT("hud"), TEXT("lighting"), TEXT("particles"), TEXT("all") };
	for (const FString& Id : FixtureIds)
	{
		for (int32 Pass2 = 0; Pass2 < 2; ++Pass2)
		{
			const bool bOn = Pass2 == 0;
			AddKey(EKeys::F4, TEXT("F4"));
			Add(TEXT("picker ") + Id, [] {}, [this](float T) { return (CrbMenus::IsOpen() && T > 0.4f) || T > 5.f; }, 6);
			AddClickRow(TEXT("fixture:") + Id, FString::Printf(TEXT("Fixture '%s' %s"), *Id, bOn ? TEXT("on") : TEXT("off")));
			Add(TEXT("fixture state ") + Id, [] {}, [this, Id, bOn](float T)
			{
				const bool bMatch = bOn ? Host->ActiveFixture == Id : Host->ActiveFixture.IsEmpty();
				APlayerController* PC = Host->GetWorld()->GetFirstPlayerController();
				if (bMatch && T > 0.8f)
				{
					Check(!Host->IsMenuOpen() && PC && !PC->bShowMouseCursor, FString::Printf(TEXT("Fixture '%s' turned %s by Java; menu closed, cursor hidden, game input restored"), *Id, bOn ? TEXT("on") : TEXT("off (selected again)")));
					return true;
				}
				if (T > 8.f) { Fail(FString::Printf(TEXT("Fixture '%s' did not turn %s (active='%s')"), *Id, bOn ? TEXT("on") : TEXT("off"), *Host->ActiveFixture)); return true; }
				return false;
			}, 10);
			if (bOn && (Id == TEXT("all") || Id == TEXT("shapes")))
				Add(TEXT("fixture shot ") + Id, [this, Id] { Host->LookYaw = -50; Host->LookPitch = -14; }, [this, Id](float T) { if (T > 1.2f) { Shot(TEXT("04_fixture_") + Id); return true; } return false; }, 5);
			AddMovementCheck(FString::Printf(TEXT("Fixture '%s' %s"), *Id, bOn ? TEXT("on") : TEXT("off")));
			if (bOn && Id == TEXT("all"))
				Add(TEXT("all fixture texture budget"), [] {}, [this](float)
				{
					const FCrbState& S = Host->GetState();
					Check(S.PoseErrors == PoseErrors0 && Host->Avatar.Rejected == 0, FString::Printf(TEXT("All-features fixture: pose frames kept flowing (java pose errors %lld, rejected %d, java texture failures %lld%s)"), S.PoseErrors, Host->Avatar.Rejected, S.TextureFailures, S.TextureLastError.IsEmpty() ? TEXT("") : *(TEXT(": ") + S.TextureLastError)));
					return true;
				}, 3);
		}
	}
	AddKey(EKeys::F4, TEXT("F4"));
	Add(TEXT("picker for resume"), [] {}, [](float T) { return T > 0.6f; }, 4);
	AddClickRow(TEXT("resume"), TEXT("Picker"));
	AddMovementCheck(TEXT("After closing the picker with Resume"));

	// 4. Held items: actual Java item models, textured from the Java block atlas.
	TSharedPtr<FJsonObject> Items = MakeShared<FJsonObject>(); Items->SetStringField(TEXT("id"), TEXT("items"));
	AddCommand(TEXT("fixture.toggle"), Items, [this](bool bOk, const FCrbResult* R) { Check(bOk && R && R->Active == TEXT("items"), TEXT("Items fixture on")); });
	AddCommand(TEXT("player.reset"), nullptr, [](bool, const FCrbResult*) {});
	const TArray<int32> Slots = { 0, 1, 2, 3, 5, 6, 8 };
	for (int32 Slot : Slots)
	{
		Add(FString::Printf(TEXT("select slot %d"), Slot + 1), [this, Slot] { Host->bInputOverride = true; Host->ViewMode = 0; Host->LookYaw = 0; Host->LookPitch = 0; Host->SelectSlot(Slot); }, [this, Slot](float T)
		{
			const FCrbState& S = Host->GetState();
			const FString Want = S.Slots.IsValidIndex(Slot) ? S.Slots[Slot].Id : FString();
			const bool bJava = S.Selected == Slot && (Want.IsEmpty() ? Host->Avatar.LastMainHand == TEXT("minecraft:air") : Host->Avatar.LastMainHand == Want);
			if (bJava && T > 1.2f)
			{
				bool bAtlas = false, bFallback = false;
				for (const FString& Tx : Host->Avatar.HandTextures) { bAtlas |= Tx.StartsWith(TEXT("minecraft:textures/atlas/blocks.png")) && !Tx.Contains(TEXT("fallback")); bFallback |= Tx.Contains(TEXT("fallback")); }
				const bool bEmpty = Want.IsEmpty();
				Check(Host->Avatar.HandVertexCount > 0 && Host->Avatar.GetHands()->IsVisible() && !bFallback && (bEmpty || bAtlas), FString::Printf(TEXT("Hotbar %d -> Java main hand '%s': first-person model from Java (%d vertices) textured with %s"), Slot + 1, bEmpty ? TEXT("empty hand") : *Want, Host->Avatar.HandVertexCount, *FString::Join(Host->Avatar.HandTextures, TEXT(", "))));
				Shot(FString::Printf(TEXT("05_held_slot%d_%s"), Slot + 1, bEmpty ? TEXT("empty") : *Want.Replace(TEXT("minecraft:"), TEXT(""))));
				return true;
			}
			if (T > 6.f) { Fail(FString::Printf(TEXT("Hotbar %d did not reach Java (selected=%d mainhand=%s want=%s)"), Slot + 1, S.Selected, *Host->Avatar.LastMainHand, *Want)); return true; }
			return false;
		}, 8);
	}
	Add(TEXT("third person shovel"), [this] { Host->SelectSlot(0); Host->ViewMode = 2; Host->LookPitch = -10; }, [this](float T)
	{
		if (T < 1.5f) return false;
		bool bAtlas = false; for (const FString& Tx : Host->Avatar.BodyTextures) bAtlas |= Tx.StartsWith(TEXT("minecraft:textures/atlas/blocks.png")) && !Tx.Contains(TEXT("fallback"));
		Check(bAtlas && Host->Avatar.BodyVertexCount > 0, FString::Printf(TEXT("Third-person body holds the Java item model textured from the block atlas (%s)"), *FString::Join(Host->Avatar.BodyTextures, TEXT(", "))));
		Shot(TEXT("06_third_person_front_shovel"));
		Host->ViewMode = 0;
		return true;
	}, 6);
	AddCommand(TEXT("fixture.toggle"), Items, [this](bool bOk, const FCrbResult* R) { Check(bOk && R && R->Active.IsEmpty(), TEXT("Items fixture off")); });

	// 4b. Survival mechanics through vanilla code paths, under the all-features fixture (items + hunger 11).
	TSharedPtr<FJsonObject> All = MakeShared<FJsonObject>(); All->SetStringField(TEXT("id"), TEXT("all"));
	AddCommand(TEXT("fixture.toggle"), All, [this](bool bOk, const FCrbResult* R) { Check(bOk && R && R->Active == TEXT("all"), TEXT("All-features fixture on for mechanics (") + (R ? R->Message : FString()) + TEXT(")")); });
	AddCommand(TEXT("player.reset"), nullptr, [](bool, const FCrbResult*) {});
	Add(TEXT("break block"), [this] { Host->ViewMode = 0; Host->SelectSlot(0); }, [this](float T)
	{
		AimAt(-1.5, -60.5, 2.5); // grass block beside the walking lane (x = -2)
		if (T > 0.8f && !Host->bInputOverride) {}
		if (Cross(0.8f)) Host->PrimaryPressed(true);
		const int32 Id = Host->World.StateAt(FIntVector(-2, -61, 2));
		if (T > 1.0f && Id == 0)
		{
			Host->PrimaryPressed(false);
			Pass(FString::Printf(TEXT("Holding left click with the wooden shovel broke the grass block through Java's interaction manager (%.1f s)"), T - 0.85f));
			Shot(TEXT("06b_block_broken"));
			return true;
		}
		if (T > 8.f) { Host->PrimaryPressed(false); Fail(FString::Printf(TEXT("Block was not broken (state %d)"), Id)); return true; }
		return false;
	}, 10);
	Add(TEXT("place block"), [this] { Host->SelectSlot(5); }, [this](float T)
	{
		// Top face centre of the off-lane grass block (-2,-61,1); vanilla places the planks above it at (-2,-60,1).
		// (The old target, the hole's floor, was hidden behind grass (-1,-61,1) from the eye, so the planks landed
		// elsewhere.)
		AimAt(-1.5, -60.0, 1.5);
		if (Cross(0.8f)) Host->SecondaryPressed(true);
		if (Cross(1.1f)) Host->SecondaryPressed(false);
		const FCrbModel* M = Host->World.Model(Host->World.StateAt(FIntVector(-2, -60, 1)));
		if (T > 1.0f && M && M->State.Contains(TEXT("oak_planks")))
		{
			Pass(TEXT("Right click placed oak planks from the hotbar via vanilla useItemOn; Java world updated and re-streamed"));
			Shot(TEXT("06c_block_placed"));
			return true;
		}
		if (T > 6.f) { Fail(TEXT("Block was not placed at (-2,-60,1); hit=") + FString::Printf(TEXT("(%d,%d,%d) face %d"), Host->GetState().Hit.X, Host->GetState().Hit.Y, Host->GetState().Hit.Z, Host->GetState().HitFace) + Diag()); return true; }
		return false;
	}, 8);
	Add(TEXT("eat apple"), [this] { Host->SelectSlot(3); Host->LookPitch = 0; X0 = Host->GetState().Food; }, [this](float T)
	{
		if (Cross(0.6f)) Host->SecondaryPressed(true);
		if (T > 0.7f && Host->GetState().Food > X0)
		{
			Host->SecondaryPressed(false);
			Check(true, FString::Printf(TEXT("Holding right click ate an apple through Java item use: hunger %.0f -> %d"), X0, Host->GetState().Food));
			return true;
		}
		if (Cross(1.2f)) Shot(TEXT("06d_eating_first_person"));
		if (T > 6.f) { Host->SecondaryPressed(false); Fail(TEXT("Apple was not eaten")); return true; }
		return false;
	}, 8);
	Add(TEXT("shield"), [this] { Host->SelectSlot(8); }, [this](float T)
	{
		if (Cross(0.6f)) Host->SecondaryPressed(true);
		if (T > 1.4f)
		{
			Shot(TEXT("06e_shield_raised"));
			Host->SecondaryPressed(false);
			Check(Host->Avatar.HandVertexCount > 0, TEXT("Empty main hand + off-hand shield: holding right click raises the shield (vanilla use), first-person pose updated"));
			return true;
		}
		return false;
	}, 4);

	// 4c. Inventory screen (E): vanilla click semantics through Java.
	AddKey(EKeys::E, TEXT("E"));
	Add(TEXT("inventory open"), [] {}, [this](float T)
	{
		if (T < 0.8f) return false;
		ACrbHUD* HUD = Cast<ACrbHUD>(Host->GetWorld()->GetFirstPlayerController()->GetHUD());
		Check(Host->bInventoryOpen && HUD && HUD->Drawn.bInventory && HUD->Drawn.InventoryIcons > 0, FString::Printf(TEXT("E opened the inventory screen (Minecraft inventory.png) with %d Java item icons"), HUD ? HUD->Drawn.InventoryIcons : 0));
		Shot(TEXT("06f_inventory"));
		return true;
	}, 4);
	Add(TEXT("inventory pick"), [] {}, [this](float T)
	{
		ACrbHUD* HUD = Cast<ACrbHUD>(Host->GetWorld()->GetFirstPlayerController()->GetHUD());
		FVector2D P;
		if (Cross(0.5f) && HUD && HUD->InventorySlotCenter(36, P)) Host->InventoryClickAt(P, 0);
		const FCrbState& S = Host->GetState();
		if (T > 0.8f && S.Slots.IsValidIndex(41) && S.Slots[41].Id == TEXT("minecraft:wooden_shovel")) { Pass(TEXT("Clicking hotbar slot 1 picked the shovel onto the Java cursor")); return true; }
		if (T > 5.f) { Fail(TEXT("Inventory pickup did not reach Java")); return true; }
		return false;
	}, 6);
	Add(TEXT("inventory place"), [] {}, [this](float T)
	{
		ACrbHUD* HUD = Cast<ACrbHUD>(Host->GetWorld()->GetFirstPlayerController()->GetHUD());
		FVector2D P;
		if (Cross(0.5f) && HUD && HUD->InventorySlotCenter(9, P)) Host->InventoryClickAt(P, 0);
		const FCrbState& S = Host->GetState();
		if (T > 0.8f && S.Slots.IsValidIndex(14) && S.Slots[14].Id == TEXT("minecraft:wooden_shovel") && S.Slots[0].Id.IsEmpty() && S.Slots[41].Id.IsEmpty())
		{
			Pass(TEXT("Clicking the first main-inventory slot placed it there (Java server confirmed the move)"));
			Shot(TEXT("06g_inventory_moved"));
			return true;
		}
		if (T > 5.f) { Fail(TEXT("Inventory place did not reach Java")); return true; }
		return false;
	}, 6);
	Add(TEXT("inventory close"), [this] { PressKey(EKeys::E); }, [this](float T)
	{
		if (T < 0.6f) return false;
		APlayerController* PC = Host->GetWorld()->GetFirstPlayerController();
		Check(!Host->bInventoryOpen && PC && !PC->bShowMouseCursor, TEXT("E closed the inventory, hid the cursor and returned game input"));
		return true;
	}, 3);
	AddMovementCheck(TEXT("After mechanics and inventory"));
	AddCommand(TEXT("fixture.toggle"), All, [this](bool bOk, const FCrbResult* R) { Check(bOk && R && R->Active.IsEmpty(), FString::Printf(TEXT("All-features fixture off; inventory and vitals restored (java: ok=%d active='%s' '%s')"), bOk, *Host->ActiveFixture, R ? *R->Message : TEXT("no result"))); });

	// 5. HUD from Java state.
	TSharedPtr<FJsonObject> Hud = MakeShared<FJsonObject>(); Hud->SetStringField(TEXT("id"), TEXT("hud"));
	AddCommand(TEXT("fixture.toggle"), Hud, [](bool, const FCrbResult*) {});
	Add(TEXT("hud values"), [this] { Host->ViewMode = 0; Host->LookPitch = 0; }, [this](float T)
	{
		const FCrbState& S = Host->GetState();
		ACrbHUD* HUD = Cast<ACrbHUD>(Host->GetWorld()->GetFirstPlayerController()->GetHUD());
		if (!HUD || T < 1.5f) return false;
		if ((S.Armor != 15 || FMath::RoundToInt(S.Health) != 13 || S.Food != 11 || S.XpLevel != 7) && T < 8.f) return false;
		const FCrbHudDrawn& D = HUD->Drawn;
		const int32 WantFill = (int32)(S.XpProgress * 183.f);
		const bool bOk = S.Armor == 15 && D.ArmorFull == 7 && D.ArmorHalf == 1 && D.ArmorEmpty == 2 && D.FullHearts == 6 && D.HalfHearts == 1 && D.FoodFull == 5 && D.FoodHalf == 1
			&& D.XpLevel == 7 && FMath::Abs(D.XpFillPixels - WantFill) < 1 && WantFill > 0 && D.bUsedMinecraftSheets && D.HotbarIcons >= 0;
		Check(bOk, FString::Printf(TEXT("HUD shows Java state: health %.0f (%d full + %d half hearts), armor %d (%d full, %d half, %d empty above hearts), food %d (%d+%d), XP level %d with %.0f/182 px progress (%.2f), Minecraft GUI sheets=%d"),
			S.Health, D.FullHearts, D.HalfHearts, S.Armor, D.ArmorFull, D.ArmorHalf, D.ArmorEmpty, S.Food, D.FoodFull, D.FoodHalf, D.XpLevel, D.XpFillPixels, S.XpProgress, D.bUsedMinecraftSheets));
		Check(FMath::IsNearlyEqual(D.Armor.Y, D.Hearts.Y - 10 * D.Scale) && FMath::IsNearlyEqual(D.XpBar.Y, Host->GetWorld()->GetGameViewport()->Viewport->GetSizeXY().Y - 29 * D.Scale, 1.f),
			FString::Printf(TEXT("Armor row sits 10 GUI px above hearts and the XP bar 29 GUI px above the bottom (scale %.0f), as in vanilla"), D.Scale));
		Shot(TEXT("07_hud_armor_xp"));
		return true;
	}, 12);
	AddCommand(TEXT("fixture.toggle"), Hud, [](bool, const FCrbResult*) {});

	// 6. Mod menu and gravity gun.
	AddKey(EKeys::M, TEXT("M"));
	Add(TEXT("mod menu open"), [] {}, [this](float T)
	{
		if (Host->bModMenuOpen && T > 0.5f)
		{
			const TArray<FString> K = CrbMenus::RowKeys();
			Check(K.Contains(TEXT("mod:hand")) && K.Contains(TEXT("mod:gravity_gun")) && !K.Contains(TEXT("mod:orbital_cannon")) && K.Contains(TEXT("mod:herobrine")),
				FString::Printf(TEXT("M opened the mod menu: hand, gravity gun and Herobrine (From The Fog) rows, no orbital cannon (%d rows, %d loaded Fabric mods)"), K.Num(), Host->Mods.Num() - 2));
			Shot(TEXT("08_mod_menu"));
			return true;
		}
		if (T > 5.f) { Fail(TEXT("M did not open the mod menu")); return true; }
		return false;
	}, 6);
	AddClickRow(TEXT("mod:gravity_gun"), TEXT("Mod menu"));
	TSharedPtr<FJsonObject> Shapes = MakeShared<FJsonObject>(); Shapes->SetStringField(TEXT("id"), TEXT("shapes"));
	AddCommand(TEXT("fixture.toggle"), Shapes, [](bool, const FCrbResult*) {});
	AddCommand(TEXT("player.reset"), nullptr, [](bool, const FCrbResult*) {});
	Add(TEXT("gravity equip"), [this] { Host->ViewMode = 0; }, [this](float T)
	{
		if (T < 1.0f) return false;
		AimAt(2.5, -59.5, 3.5);
		if (T < 1.6f) return false;
		Check(Host->Weapon == ECrbWeapon::GravityGun && Host->WeaponEquip >= 1.f && !Host->IsMenuOpen() && Host->GunMesh->IsVisible() && !Host->Avatar.GetHands()->IsVisible(), TEXT("Gravity gun equipped (equip animation finished, Minecraft hand hidden, menu closed)"));
		Shot(TEXT("09_gravity_gun_equipped"));
		return true;
	}, 5);
	Add(TEXT("gravity grab"), [this] { AimAt(2.5, -59.5, 3.5); Host->PrimaryPressed(true); Host->PrimaryPressed(false); }, [this](float T)
	{
		const FCrbState& S = Host->GetState();
		if (S.bGravityHolding && T > 1.0f)
		{
			Check(Host->World.StateAt(FIntVector(2, -60, 3)) == 0 && Host->HeldBlock->IsVisible() && Host->Beam->IsVisible(), FString::Printf(TEXT("Gravity gun: Java removed the block and holds state %d; Unreal shows its baked model, beam and orb"), S.GravityState));
			Shot(TEXT("10_gravity_gun_holding"));
			X0 = S.GravityDistance;
			return true;
		}
		if (T > 6.f) { Fail(TEXT("Gravity gun grab failed: ") + Host->StatusLine); return true; }
		return false;
	}, 8);
	Add(TEXT("gravity wheel"), [this] { Host->ScrollWheel(1); Host->ScrollWheel(1); }, [this](float T)
	{
		if (T < 1.0f) return false;
		Check(Host->GetState().GravityDistance > X0 + 0.5, FString::Printf(TEXT("Scroll wheel changed the Java-held distance %.1f -> %.1f"), X0, Host->GetState().GravityDistance));
		return true;
	}, 4);
	Add(TEXT("gravity release"), [this] { Host->LookYaw -= 35; Host->LookPitch = -20; } /* turn east, away from the walking lane */, [this](float T)
	{
		if (Cross(0.6f)) { Host->PrimaryPressed(true); Host->PrimaryPressed(false); }
		if (T > 0.7f && !Host->GetState().bGravityHolding && T > 1.6f)
		{
			Check(!Host->HeldBlock->IsVisible() && !Host->Beam->IsVisible(), TEXT("Release: Java placed the block back into the world; beam and held model cleared: ") + Host->StatusLine);
			return true;
		}
		if (T > 6.f) { Fail(TEXT("Gravity gun release failed: ") + Host->StatusLine + Diag()); return true; }
		return false;
	}, 8);
	AddCommand(TEXT("fixture.toggle"), Shapes, [](bool, const FCrbResult*) {});
	Add(TEXT("back to hand"), [this] { PressKey(EKeys::One); }, [this](float T)
	{
		if (T < 0.8f) return false;
		Check(Host->Weapon == ECrbWeapon::Hand && !Host->GunMesh->IsVisible() && !Host->Orb->IsVisible() && Host->Avatar.GetHands()->IsVisible(), TEXT("Key 1 switched from the gravity gun back to the normal Minecraft hand; add-on visuals cleaned up"));
		return true;
	}, 4);
	AddMovementCheck(TEXT("After weapons"));

	// 7. Lighting, torches, water, particles.
	TSharedPtr<FJsonObject> Light = MakeShared<FJsonObject>(); Light->SetStringField(TEXT("id"), TEXT("lighting"));
	AddCommand(TEXT("fixture.toggle"), Light, [](bool, const FCrbResult*) {});
	AddCommand(TEXT("player.reset"), nullptr, [](bool, const FCrbResult*) {});
	Add(TEXT("night"), [this] { Host->ViewMode = 0; Host->LookYaw = 15; Host->LookPitch = -18; }, [this](float T)
	{
		if (T < 3.0f) return false;
		if (T < 3.1f) { Shot(TEXT("12_night_torches_water")); return false; }
		if (!PendingShot.IsEmpty() && T < 6.f) return false;
		NightLuma = MeasureViewport();
		bool bWater = false;
		for (int32 X = 3; X <= 6; ++X) { const FCrbModel* M = Host->World.Model(Host->World.StateAt(FIntVector(X, -61, 23))); bWater |= M && M->Layer == 2 && M->State.Contains(TEXT("water")); }
		Check(Host->ActiveTorchLights > 0 && Host->ActiveTorchLights <= Host->MaxTorchLights, FString::Printf(TEXT("Night fixture: %d bounded dynamic torch lights (max %d) from Java light-emitting blocks"), Host->ActiveTorchLights, Host->MaxTorchLights));
		Check(bWater, TEXT("Java water rendered as translucent geometry"));
		Check(NightLuma.bOk && NightLuma.Mean < DayLuma.Mean && NightLuma.Blown < 0.03f, FString::Printf(TEXT("Night is darker than day via Java's light map (luma %.3f vs %.3f) without blown-out faces (%.2f%%)"), NightLuma.Mean, DayLuma.Mean, NightLuma.Blown * 100));
		return true;
	}, 8);
	Add(TEXT("lighting toggle"), [this] { PressKey(EKeys::F12); }, [this](float T)
	{
		if (T < 1.5f) return false;
		if (T < 1.6f) { Shot(TEXT("13_lighting_toggle_base")); return false; }
		if (!PendingShot.IsEmpty() && T < 4.f) return false;
		OffLuma = MeasureViewport();
		Check(!Host->bLightingEnabled && OffLuma.Mean > NightLuma.Mean, FString::Printf(TEXT("F12 lighting toggle switches to base 10000 lux lighting (luma %.3f)"), OffLuma.Mean));
		PressKey(EKeys::F12);
		return true;
	}, 5);
	Add(TEXT("lighting back"), [] {}, [this](float T) { if (T < 1.f) return false; Check(Host->bLightingEnabled, TEXT("F12 restores Minecraft light-map lighting")); return true; }, 3);
	AddCommand(TEXT("fixture.toggle"), Light, [](bool, const FCrbResult*) {});
	TSharedPtr<FJsonObject> Part = MakeShared<FJsonObject>(); Part->SetStringField(TEXT("id"), TEXT("particles"));
	AddCommand(TEXT("fixture.toggle"), Part, [](bool, const FCrbResult*) {});
	Add(TEXT("particles"), [this] { Host->LookYaw = 0; Host->LookPitch = -10; }, [this](float T)
	{
		if (T < 2.5f) return false;
		Check(Host->ParticleCount > 0 && Host->ParticleCount <= 1024 && Host->ParticleDraw <= 2, FString::Printf(TEXT("Java particles rendered: %d sprites in %d draw sections (bounded)"), Host->ParticleCount, Host->ParticleDraw));
		Shot(TEXT("14_particles"));
		return true;
	}, 6);
	AddCommand(TEXT("fixture.toggle"), Part, [](bool, const FCrbResult*) {});

	BuildGameUI();

	// 9. Reconnect and final movement.
	Add(TEXT("reconnect"), [this] { X0 = Host->Link() ? Host->Link()->GetEpoch() : 0; Host->Link()->RequestReconnect(); }, [this](float T)
	{
		if (Host->Link()->GetEpoch() > X0 && Host->GetState().bValid && Host->Avatar.FramesReceived > 5 && T > 2.f)
		{
			Pass(TEXT("Disconnected and reconnected; Java resumed state, world and pose streams"));
			return true;
		}
		if (T > 20.f) { Fail(TEXT("Reconnect failed: ") + Host->StatusLine); return true; }
		return false;
	}, 25);
	AddMovementCheck(TEXT("After reconnect"));
	Add(TEXT("finish"), [this] { Finish(); }, [](float) { return true; }, 2);
}

void FCrbTest::BuildGameUI()
{
	// ---- Pause menu (Esc) ----
	AddCommand(TEXT("player.reset"), nullptr, [](bool, const FCrbResult*) {});
	AddKey(EKeys::Escape, TEXT("Esc"));
	Add(TEXT("pause open"), [] {}, [this](float T)
	{
		if (T < 0.5f) return false;
		const TArray<FString> K = CrbMenus::RowKeys();
		APlayerController* PC = Host->GetWorld()->GetFirstPlayerController();
		Check(Host->bPauseOpen && K.Contains(TEXT("pause:back")) && K.Contains(TEXT("pause:quit")) && PC && PC->bShowMouseCursor, TEXT("Esc opened the Game Menu (Back to Game, Mods, Quit Game) instead of quitting; cursor shown"));
		Shot(TEXT("15_pause_menu"));
		return true;
	}, 4);
	AddClickRow(TEXT("pause:back"), TEXT("Pause"));
	Add(TEXT("pause closed"), [] {}, [this](float T)
	{
		if (T < 0.4f) return false;
		APlayerController* PC = Host->GetWorld()->GetFirstPlayerController();
		Check(!Host->bPauseOpen && !Host->IsMenuOpen() && PC && !PC->bShowMouseCursor, TEXT("Back to Game closed the pause menu and returned game input"));
		return true;
	}, 3);

	// ---- Title + chat from Java (vanilla /title and tellraw) ----
	AddCommand(TEXT("ui.test"), nullptr, [this](bool bOk, const FCrbResult* R) { if (!bOk) Fail(TEXT("ui.test refused: ") + (R ? R->Message : FString())); });
	Add(TEXT("title shown"), [] {}, [this](float T)
	{
		const FCrbState& S = Host->GetState();
		ACrbHUD* HUD = Cast<ACrbHUD>(Host->GetWorld()->GetFirstPlayerController()->GetHUD());
		if (T > 1.0f && S.TitleAlpha > 0.5f && Host->Textures.Get(TEXT("crb:title")) && HUD && HUD->Drawn.bTitle && HUD->Drawn.ChatLines > 0)
		{
			Pass(FString::Printf(TEXT("Java title '%s' / '%s' drawn by Minecraft's font into crb:title and shown (alpha %.2f); chat shows %d line(s): '%s'"),
				*S.TitleText, *S.SubtitleText, S.TitleAlpha, HUD->Drawn.ChatLines, S.Chat.Num() ? *S.Chat[0].Text : TEXT("")));
			Shot(TEXT("16_title_and_chat"));
			return true;
		}
		if (T > 8.f) { Fail(FString::Printf(TEXT("Title/chat not shown (alpha %.2f, texture %d, chat %d)"), S.TitleAlpha, Host->Textures.Get(TEXT("crb:title")) != nullptr, S.Chat.Num())); return true; }
		return false;
	}, 10);

	// ---- Death screen + Respawn ----
	AddCommand(TEXT("player.kill"), nullptr, [this](bool bOk, const FCrbResult* R) { if (!bOk) Fail(TEXT("player.kill refused: ") + (R ? R->Message : FString())); });
	Add(TEXT("death screen"), [] {}, [this](float T)
	{
		const TArray<FString> K = CrbMenus::RowKeys();
		if (Host->bDeathOpen && K.Contains(TEXT("death:respawn")) && T > 0.8f)
		{
			Pass(FString::Printf(TEXT("Java death opened the death screen ('%s', score %d) with a Respawn button"), *Host->GetState().DeathMessage, Host->GetState().Score));
			Shot(TEXT("17_death_screen"));
			return true;
		}
		if (T > 8.f) { Fail(TEXT("Death screen did not open") + Diag()); return true; }
		return false;
	}, 10);
	Add(TEXT("esc on death"), [this] { PressKey(EKeys::Escape); }, [this](float T) { if (T < 0.4f) return false; Check(Host->bDeathOpen && !Host->bPauseOpen, TEXT("Esc does not dismiss the death screen (vanilla)")); return true; }, 3);
	AddClickRow(TEXT("death:respawn"), TEXT("Death screen"));
	Add(TEXT("respawned"), [] {}, [this](float T)
	{
		const FCrbState& S = Host->GetState();
		APlayerController* PC = Host->GetWorld()->GetFirstPlayerController();
		if (!S.bDead && !Host->bDeathOpen && S.Health > 0 && T > 0.5f)
		{
			Check(PC && !PC->bShowMouseCursor, FString::Printf(TEXT("Respawn: Java respawned the player (health %.0f) and the death screen closed"), S.Health));
			return true;
		}
		if (T > 8.f) { Fail(TEXT("Respawn did not complete") + Diag()); return true; }
		return false;
	}, 10);
	AddMovementCheck(TEXT("After respawn"));

	// ---- Creative mode via the debug menu ----
	AddKey(EKeys::F4, TEXT("F4"));
	Add(TEXT("debug menu for creative"), [] {}, [](float T) { return (CrbMenus::IsOpen() && T > 0.5f) || T > 5.f; }, 6);
	AddClickRow(TEXT("gamemode"), TEXT("Debug menu"));
	Add(TEXT("creative on"), [] {}, [this](float T)
	{
		ACrbHUD* HUD = Cast<ACrbHUD>(Host->GetWorld()->GetFirstPlayerController()->GetHUD());
		if (Host->IsCreative() && T > 1.f && HUD)
		{
			if (Host->IsMenuOpen()) Host->CloseMenus();
			Check(HUD->Drawn.HeartContainers == 0 && HUD->Drawn.FoodFull == 0 && HUD->Drawn.bHotbar, TEXT("Debug menu switched Java to creative; HUD hides hearts/hunger/armor/XP and keeps the hotbar (vanilla creative HUD)"));
			return true;
		}
		if (T > 6.f) { Fail(TEXT("Game mode did not switch to creative: ") + Host->StatusLine); return true; }
		return false;
	}, 8);
	AddCommand(TEXT("player.reset"), nullptr, [](bool, const FCrbResult*) {});
	Add(TEXT("creative flight"), [this] { Y0 = Host->GetState().Y; Host->SetButtons(false, false, false); }, [this](float T)
	{
		// Vanilla double-tap jump toggles flying: presses 300 ms apart (inside the 7-tick window), then hold to rise.
		// If Java did not start flying (a missed edge), the double tap is repeated every 1.6 s.
		const float C = FMath::Fmod(T, 1.6f);
		const bool bTap = (C > 0.2f && C < 0.35f) || (C > 0.5f && C < 0.65f);
		const bool bHold = T > 0.85f && C > 0.85f;
		Host->SetButtons(bTap || bHold, false, false);
		const FCrbState& S = Host->GetState();
		if (S.Y > Y0 + 1.5 && !S.bOnGround && T > 1.5f)
		{
			Host->SetButtons(false, false, false);
			Pass(FString::Printf(TEXT("Creative flight: double-tap + hold jump flew the Java player up %.2f blocks"), S.Y - Y0));
			Shot(TEXT("18_creative_flying"));
			return true;
		}
		if (T > 6.2f) { Host->SetButtons(false, false, false); Fail(FString::Printf(TEXT("Creative flight: Java player rose %.2f blocks"), S.Y - Y0) + Diag()); return true; }
		return false;
	}, 8);
	Add(TEXT("land"), [this] { Host->SetButtons(false, true, false); }, [this](float T) { if (T < 2.5f) return false; Host->SetButtons(false, false, false); return true; }, 4);
	AddKey(EKeys::E, TEXT("E"));
	Add(TEXT("creative inventory"), [] {}, [this](float T)
	{
		ACrbHUD* HUD = Cast<ACrbHUD>(Host->GetWorld()->GetFirstPlayerController()->GetHUD());
		int32 Items = 0; for (const auto& I : Host->CreativeItems) Items += !I.Id.IsEmpty();
		if (HUD && HUD->Drawn.bCreative && Host->CreativeTabs.Num() >= 10 && Items > 0 && HUD->Drawn.CreativeIcons > 0 && T > 1.0f)
		{
			Pass(FString::Printf(TEXT("E opened the creative inventory: %d vanilla tabs, '%s' page with %d items (%d total, %d rows to scroll)"), Host->CreativeTabs.Num(), *HUD->Drawn.CreativeTitle, Items, Host->CreativeTotal, Host->CreativeMaxRow));
			Shot(TEXT("19_creative_inventory"));
			return true;
		}
		if (T > 10.f) { Fail(FString::Printf(TEXT("Creative inventory incomplete (tabs %d, items %d, icons %d)"), Host->CreativeTabs.Num(), Items, HUD ? HUD->Drawn.CreativeIcons : -1)); return true; }
		return false;
	}, 12);
	Add(TEXT("creative pick"), [this] { PendingId = Host->CreativeItems.Num() ? Host->CreativeItems[0].Id : FString(); }, [this](float T)
	{
		ACrbHUD* HUD = Cast<ACrbHUD>(Host->GetWorld()->GetFirstPlayerController()->GetHUD());
		FVector2D P;
		if (Cross(0.3f) && HUD && HUD->CreativeCellCenter(0, P)) Host->InventoryClickAt(P, 0);
		const FCrbState& S = Host->GetState();
		if (T > 0.6f && S.Slots.IsValidIndex(41) && S.Slots[41].Id == PendingId && !PendingId.IsEmpty())
		{
			Pass(FString::Printf(TEXT("Clicking the first creative item put a full stack of %s (%d) on the cursor"), *PendingId, S.Slots[41].Count));
			return true;
		}
		if (T > 5.f) { Fail(TEXT("Creative pick did not reach Java: ") + Host->StatusLine); return true; }
		return false;
	}, 6);
	Add(TEXT("creative place"), [] {}, [this](float T)
	{
		ACrbHUD* HUD = Cast<ACrbHUD>(Host->GetWorld()->GetFirstPlayerController()->GetHUD());
		FVector2D P;
		if (Cross(0.3f) && HUD && HUD->CreativeSlotCenter(44, P)) Host->InventoryClickAt(P, 0);
		const FCrbState& S = Host->GetState();
		if (T > 0.6f && S.Slots.IsValidIndex(8) && S.Slots[8].Id == PendingId && S.Slots[41].Id.IsEmpty())
		{
			Pass(FString::Printf(TEXT("Placing it on hotbar slot 9 reached the server (vanilla set-creative-slot): slot 9 = %s x%d"), *PendingId, S.Slots[8].Count));
			return true;
		}
		if (T > 5.f) { Fail(TEXT("Creative place did not reach Java") + Diag()); return true; }
		return false;
	}, 6);
	Add(TEXT("creative scroll"), [this] { X0 = Host->CreativeRow; }, [this](float T)
	{
		if (Cross(0.2f)) Host->ScrollWheel(-1.f);
		if (T < 1.0f) return false;
		Check(Host->CreativeMaxRow == 0 || Host->CreativeRow == (int32)X0 + 1, FString::Printf(TEXT("Mouse wheel scrolled the creative page (row %d -> %d of %d)"), (int32)X0, Host->CreativeRow, Host->CreativeMaxRow));
		return true;
	}, 3);
	Add(TEXT("creative search"), [this]
	{
		int32 Search = -1; for (const auto& Tb : Host->CreativeTabs) if (Tb.Type == TEXT("search")) Search = Tb.Index;
		ACrbHUD* HUD = Cast<ACrbHUD>(Host->GetWorld()->GetFirstPlayerController()->GetHUD());
		FVector2D P; if (HUD && Search >= 0 && HUD->CreativeTabCenter(Search, P)) Host->InventoryClickAt(P, 0);
	}, [this](float T)
	{
		if (Cross(0.5f)) Host->CreativeType(TEXT("diamond"));
		bool bAll = Host->CreativeItems.Num() > 0 && !Host->CreativeItems[0].Id.IsEmpty();
		for (const auto& I : Host->CreativeItems) if (!I.Id.IsEmpty() && !I.Id.Contains(TEXT("diamond")) && !I.Name.Contains(TEXT("Diamond"))) bAll = false;
		if (Host->IsCreativeSearch() && Host->CreativeQuery == TEXT("diamond") && bAll && T > 1.2f)
		{
			Pass(FString::Printf(TEXT("Search tab filtered by typed text 'diamond': %d results, first %s"), Host->CreativeTotal, *Host->CreativeItems[0].Id));
			Shot(TEXT("20_creative_search"));
			return true;
		}
		if (T > 6.f) { Fail(FString::Printf(TEXT("Creative search failed (search tab %d, query '%s', results %d)"), Host->IsCreativeSearch(), *Host->CreativeQuery, Host->CreativeTotal)); return true; }
		return false;
	}, 8);
	Add(TEXT("creative inventory tab"), [this]
	{
		int32 Inv = -1; for (const auto& Tb : Host->CreativeTabs) if (Tb.Type == TEXT("inventory")) Inv = Tb.Index;
		ACrbHUD* HUD = Cast<ACrbHUD>(Host->GetWorld()->GetFirstPlayerController()->GetHUD());
		FVector2D P; if (HUD && Inv >= 0 && HUD->CreativeTabCenter(Inv, P)) Host->InventoryClickAt(P, 0);
	}, [this](float T)
	{
		ACrbHUD* HUD = Cast<ACrbHUD>(Host->GetWorld()->GetFirstPlayerController()->GetHUD());
		FVector2D P;
		// Pick the hotbar-9 stack up and drop it in the Destroy Item slot.
		if (Cross(0.4f) && HUD && HUD->CreativeSlotCenter(44, P)) Host->InventoryClickAt(P, 0);
		if (Cross(1.0f) && HUD && HUD->CreativeDestroyCenter(P)) Host->InventoryClickAt(P, 0);
		const FCrbState& S = Host->GetState();
		if (T > 1.4f && Host->IsCreativeInventoryTab() && S.Slots.IsValidIndex(8) && S.Slots[8].Id.IsEmpty() && S.Slots[41].Id.IsEmpty())
		{
			Pass(TEXT("Survival Inventory tab: picked hotbar slot 9 and the Destroy Item slot deleted it (Java confirmed)"));
			Shot(TEXT("21_creative_inventory_tab"));
			return true;
		}
		if (T > 6.f) { Fail(TEXT("Creative inventory tab / destroy failed") + Diag()); return true; }
		return false;
	}, 8);
	Add(TEXT("creative close"), [this] { PressKey(EKeys::Escape); }, [this](float T) { if (T < 0.5f) return false; Check(!Host->bInventoryOpen, TEXT("Esc closed the creative inventory")); return true; }, 3);
	AddKey(EKeys::F4, TEXT("F4"));
	Add(TEXT("debug menu for survival"), [] {}, [](float T) { return (CrbMenus::IsOpen() && T > 0.5f) || T > 5.f; }, 6);
	AddClickRow(TEXT("gamemode"), TEXT("Debug menu"));
	Add(TEXT("survival on"), [] {}, [this](float T)
	{
		ACrbHUD* HUD = Cast<ACrbHUD>(Host->GetWorld()->GetFirstPlayerController()->GetHUD());
		if (!Host->IsCreative() && Host->GetState().GameMode == TEXT("survival") && T > 1.f && HUD)
		{
			if (Host->IsMenuOpen()) Host->CloseMenus();
			Check(HUD->Drawn.HeartContainers > 0, TEXT("Debug menu switched back to survival; hearts and hunger return"));
			return true;
		}
		if (T > 6.f) { Fail(TEXT("Game mode did not switch back to survival")); return true; }
		return false;
	}, 8);

	// ---- Herobrine (From The Fog) ----
	AddKey(EKeys::M, TEXT("M"));
	Add(TEXT("mods for herobrine"), [] {}, [](float T) { return (CrbMenus::IsOpen() && T > 0.5f) || T > 5.f; }, 6);
	AddClickRow(TEXT("mod:herobrine"), TEXT("Mod menu"));
	Add(TEXT("herobrine menu"), [] {}, [this](float T)
	{
		const TArray<FString> K = CrbMenus::RowKeys();
		bool bInstalled = false; if (Host->HerobrineStatus.IsValid()) Host->HerobrineStatus->TryGetBoolField(TEXT("installed"), bInstalled);
		if (bInstalled && K.Contains(TEXT("herobrine:glowing_eyes")) && K.Contains(TEXT("herobrine:summon:fake")) && T > 0.6f)
		{
			Pass(FString::Printf(TEXT("Herobrine menu shows From The Fog's live settings (%d rows) read from its scoreboard"), K.Num()));
			Shot(TEXT("22_herobrine_menu"));
			return true;
		}
		if (T > 8.f) { Fail(TEXT("Herobrine menu not populated (mod installed in Work\\mc\\mods?)")); return true; }
		return false;
	}, 10);
	Add(TEXT("herobrine toggle read"), [this]
	{
		X0 = -999;
		const TArray<TSharedPtr<FJsonValue>>* O = nullptr;
		if (Host->HerobrineStatus.IsValid() && Host->HerobrineStatus->TryGetArrayField(TEXT("options"), O))
			for (auto& V : *O) if (V->AsObject()->GetStringField(TEXT("key")) == TEXT("glowing_eyes")) X0 = V->AsObject()->GetNumberField(TEXT("score"));
	}, [](float) { return true; }, 2);
	AddClickRow(TEXT("herobrine:glowing_eyes"), TEXT("Herobrine menu"));
	Add(TEXT("herobrine toggle"), [] {}, [this](float T)
	{
		double Now = -999;
		const TArray<TSharedPtr<FJsonValue>>* O = nullptr;
		if (Host->HerobrineStatus.IsValid() && Host->HerobrineStatus->TryGetArrayField(TEXT("options"), O))
			for (auto& V : *O) if (V->AsObject()->GetStringField(TEXT("key")) == TEXT("glowing_eyes")) Now = V->AsObject()->GetNumberField(TEXT("score"));
		if (T > 0.5f && Now != X0 && (Now == 0 || Now == 1))
		{
			Pass(FString::Printf(TEXT("Toggling 'Glowing eyes' ran the mod's own config function: score %.0f -> %.0f"), X0, Now));
			return true;
		}
		if (T > 6.f) { Fail(FString::Printf(TEXT("Herobrine setting did not change (%.0f -> %.0f): %s"), X0, Now, *Host->StatusLine)); return true; }
		return false;
	}, 8);
	Add(TEXT("herobrine menu settle"), [] {}, [](float T) { return T > 0.8f; }, 2);
	AddClickRow(TEXT("herobrine:glowing_eyes"), TEXT("Herobrine menu (restore)"));
	Add(TEXT("herobrine menu settle 2"), [] {}, [](float T) { return T > 0.8f; }, 2);
	AddClickRow(TEXT("herobrine:summon:fake"), TEXT("Herobrine menu"));
	Add(TEXT("herobrine summon"), [this] { Host->LookYaw = 0; Host->LookPitch = 0; }, [this](float T)
	{
		int32 Stands = 0; for (const FString& E : Host->Avatar.EntityTypes()) Stands += E == TEXT("minecraft:armor_stand");
		double Rig = 0; if (Host->HerobrineStatus.IsValid()) Host->HerobrineStatus->TryGetNumberField(TEXT("herobrineEntities"), Rig);
		if (T > 2.0f && Rig > 0 && Stands > 0 && Host->Avatar.EntityVertexCount > 0 && !Host->IsMenuOpen())
		{
			Pass(FString::Printf(TEXT("From The Fog spawned Herobrine 5 blocks ahead (%.0f rig entities on the server); %d armor stands rendered in Unreal through the real entity renderers (%d vertices, textures: %s); menu closed"),
				Rig, Stands, Host->Avatar.EntityVertexCount, *FString::Join(Host->Avatar.EntityTextures, TEXT(", "))));
			Shot(TEXT("23_herobrine"));
			return true;
		}
		if (Cross(1.0f) || Cross(3.0f) || Cross(5.0f)) Host->SendCommand(TEXT("herobrine.status"));
		if (T > 10.f) { Fail(FString::Printf(TEXT("Herobrine not visible (server rig %.0f, armor stands %d, entity vertices %d)"), Rig, Stands, Host->Avatar.EntityVertexCount) + Diag()); return true; }
		return false;
	}, 12);
	Add(TEXT("herobrine third person"), [this] { Host->ViewMode = 1; }, [this](float T) { if (T < 1.2f) return false; Shot(TEXT("24_herobrine_behind_player")); Host->ViewMode = 0; return true; }, 4);
	AddCommand(TEXT("herobrine.clear"), nullptr, [this](bool bOk, const FCrbResult* R)
	{
		double Rig = -1; if (R && R->Json.IsValid()) R->Json->TryGetNumberField(TEXT("herobrineEntities"), Rig);
		Check(bOk && Rig == 0, FString::Printf(TEXT("Remove Herobrine cleared the rig (%.0f left)"), Rig));
	});
	AddMovementCheck(TEXT("After creative and Herobrine"));
	BuildAvatar();
	BuildSm64();
	BuildCraft64();
	BuildPP();
	BuildGlass();
	BuildZombies();
	BuildMaps();
}

void FCrbTest::BuildVisual()
{
	// Side-by-side evidence: for each view, the real Minecraft window captures a vanilla screenshot (Work\mc\screenshots)
	// and a JSON dump of the exported surfaces, while Unreal captures the same moment plus its own surface dump.
	Add(TEXT("connect"), [this] { Host->bInputOverride = true; }, [this](float T)
	{
		const bool bReady = Host->GetState().bValid && Host->Avatar.FramesReceived > 10 && Host->World.NumMeshed() >= 20 && Host->Textures.Get(TEXT("minecraft:textures/atlas/blocks.png")) != nullptr;
		if (bReady) { Pass(TEXT("connected")); return true; }
		if (T > 280.f) { Fail(TEXT("connect timeout")); return true; }
		return false;
	}, 300);
	TSharedPtr<FJsonObject> Noon = MakeShared<FJsonObject>(); Noon->SetNumberField(TEXT("time"), 6000);
	AddCommand(TEXT("time.set"), Noon, [](bool, const FCrbResult*) {});
	TSharedPtr<FJsonObject> Items = MakeShared<FJsonObject>(); Items->SetStringField(TEXT("id"), TEXT("items"));
	AddCommand(TEXT("fixture.off"), nullptr, [](bool, const FCrbResult*) {});
	AddCommand(TEXT("fixture.toggle"), Items, [](bool, const FCrbResult*) {});
	AddCommand(TEXT("player.reset"), nullptr, [](bool, const FCrbResult*) {});
	struct FView { int32 Slot; int32 Mode; const TCHAR* Cam; const TCHAR* Name; };
	const TArray<FView> Views = {
		{ 0, 0, TEXT("first"), TEXT("fp_wooden_shovel") }, { 1, 0, TEXT("first"), TEXT("fp_diamond_sword") }, { 2, 0, TEXT("first"), TEXT("fp_bow") },
		{ 3, 0, TEXT("first"), TEXT("fp_apple") }, { 5, 0, TEXT("first"), TEXT("fp_oak_planks") }, { 6, 0, TEXT("first"), TEXT("fp_golden_pickaxe") },
		{ 8, 0, TEXT("first"), TEXT("fp_empty_hand") }, { 0, 1, TEXT("back"), TEXT("tp_back_shovel") }, { 0, 2, TEXT("front"), TEXT("tp_front_shovel") } };
	for (const FView& V : Views)
	{
		const FString Name = V.Name; const int32 Slot = V.Slot, Mode = V.Mode; const FString Cam = V.Cam;
		Add(TEXT("pose ") + Name, [this, Slot, Mode] { Host->SelectSlot(Slot); Host->ViewMode = Mode; Host->LookYaw = 0; Host->LookPitch = 0; Host->SetMove(0, 0); }, [](float T) { return T > 1.6f; }, 4);
		Add(TEXT("capture ") + Name, [this, Name, Cam]
		{
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("name"), Name); A->SetStringField(TEXT("camera"), Cam);
			Host->SendCommand(TEXT("debug.capture"), A);
		}, [this, Name, Mode](float T)
		{
			if (Cross(0.45f))
			{
				Shot(TEXT("ue_") + Name);
				Metrics->SetStringField(TEXT("ue_hands:") + Name, Host->Avatar.DebugSummary(1));
				Metrics->SetStringField(TEXT("ue_body:") + Name, Host->Avatar.DebugSummary(0));
				const FCrbState& St = Host->GetState();
				Metrics->SetStringField(TEXT("ue_env:") + Name, FString::Printf(TEXT("hasSky=%d sky=(%.3f,%.3f,%.3f) fog=(%.3f,%.3f,%.3f) dayTime=%lld windowR=%d meshed=%d lighting=%d"),
					St.bHasSky ? 1 : 0, St.SkyColor.R, St.SkyColor.G, St.SkyColor.B, St.FogColor.R, St.FogColor.G, St.FogColor.B, (long long)St.DayTime,
					Host->World.GetWindowRadius(), Host->World.NumMeshed(), Host->bLightingEnabled ? 1 : 0));
			}
			return T > 1.5f;
		}, 4);
	}
	AddCommand(TEXT("fixture.toggle"), Items, [](bool, const FCrbResult*) {});
	Add(TEXT("finish"), [this] { Host->ViewMode = 0; Finish(); }, [](float) { return true; }, 2);
}

void FCrbTest::Tick(float Dt)
{
	if (bDone) return;
	if (Index < 0 || Index >= Steps.Num()) { Index = 0; Elapsed = 0; if (Steps.Num()) Steps[0].Begin(); }
	FStep& S = Steps[Index];
	PrevElapsed = Elapsed;
	Elapsed += Dt;
	bool bFinished = S.Update(Elapsed);
	if (!bFinished && Elapsed > S.Timeout) { Fail(TEXT("step timed out: ") + S.Name); bFinished = true; }
	if (!bFinished) return;
	if (bDone) return;
	++Index; Elapsed = 0; PrevElapsed = -1.f;
	if (Index < Steps.Num()) { LastStepName = Steps[Index].Name; Steps[Index].Begin(); }
}

void FCrbTest::Finish()
{
	bDone = true;
	Host->bInputOverride = false;
	Host->SetMove(0, 0);
	TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("passed"), Failures.Num() == 0 && Checks.Num() > 0);
	R->SetStringField(TEXT("suite"), Suite);
	R->SetStringField(TEXT("hostBuild"), Host->BuildId);
	R->SetStringField(TEXT("unreal"), TEXT("4.27"));
	R->SetStringField(TEXT("worldGenerator"), TEXT("superflat"));
	R->SetNumberField(TEXT("seconds"), FPlatformTime::Seconds() - StartTime);
	TArray<TSharedPtr<FJsonValue>> C, F, S;
	for (const FString& X : Checks) C.Add(MakeShared<FJsonValueString>(X));
	for (const FString& X : Failures) F.Add(MakeShared<FJsonValueString>(X));
	for (const FString& X : Shots) S.Add(MakeShared<FJsonValueString>(FPaths::GetCleanFilename(X)));
	R->SetArrayField(TEXT("checks"), C); R->SetArrayField(TEXT("failures"), F); R->SetArrayField(TEXT("screenshots"), S);
	Metrics->SetNumberField(TEXT("framesHandled"), Host->FramesHandled); Metrics->SetNumberField(TEXT("framesRejected"), Host->FramesRejected);
	Metrics->SetNumberField(TEXT("avatarFramesApplied"), Host->Avatar.FramesApplied); Metrics->SetNumberField(TEXT("avatarRejected"), Host->Avatar.Rejected);
	Metrics->SetNumberField(TEXT("maxSectionApplyMs"), Host->World.MaxApplyMs); Metrics->SetNumberField(TEXT("textures"), Host->Textures.NumReady());
	Metrics->SetNumberField(TEXT("textureMB"), Host->Textures.DecodedBytes() / 1048576.0);
	R->SetObjectField(TEXT("metrics"), Metrics);
	FString Text; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Text); FJsonSerializer::Serialize(R, W);
	FFileHelper::SaveStringToFile(Text, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogCrb, Display, TEXT("TEST REPORT %s passed=%d checks=%d failures=%d"), *ReportPath, Failures.Num() == 0, Checks.Num(), Failures.Num());
	if (!bQuit) return;
	if (Suite == TEXT("all"))
	{
		// Last check is the real exit path: Esc -> Game Menu -> Quit Game (Java saves and closes Minecraft, then
		// Unreal exits). Tools\TestUnreal.ps1 verifies afterwards that both processes ended and the world was saved.
		UE_LOG(LogCrb, Display, TEXT("TEST quitting through the pause menu"));
		Host->TogglePause();
		TWeakObjectPtr<ACrbHost> WeakHost = Host;
		FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakHost](float)
		{
			if (WeakHost.IsValid() && !CrbMenus::ClickRow(TEXT("pause:quit"))) WeakHost->QuitGame();
			return false;
		}), 0.8f);
		return;
	}
	UKismetSystemLibrary::QuitGame(Host, Host->GetWorld()->GetFirstPlayerController(), EQuitPreference::Quit, false);
}
