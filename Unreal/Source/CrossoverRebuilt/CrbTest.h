// Packaged end-to-end test driver (-CrbTest=all -CrbReport=<json> -CrbShots=<dir> [-CrbQuit]).
// Drives the real host paths (bridge input, Slate menus via synthesized mouse events, viewport key events) against a
// running Minecraft 1.20.1 dev client in its disposable superflat world, and records evidence with screenshots.
#pragma once
#include "CoreMinimal.h"

class ACrbHost;

class FCrbTest
{
public:
	FCrbTest(ACrbHost* InHost, const FString& InSuite);
	~FCrbTest();
	void Tick(float Dt);

private:
	struct FStep { FString Name; TFunction<void()> Begin; TFunction<bool(float)> Update; float Timeout = 30.f; };
	void Build();
	void BuildVisual();
	void BuildGameUI();
	void BuildAvatar();
	void BuildZombies();
	void BuildGlass();
	void BuildMaps();
	void BuildSm64();
	void BuildCraft64();
	void BuildPP();
	void AddPPTp(double X, double Y, double Z, float Yaw);
	void AddPPVelocity(double X, double Y, double Z);
	void AddPPPortal(int32 Which, int32 X, int32 Y, int32 Z, const TCHAR* Face);
	float PPIce = 0, PPMax = 0, PPRoll0 = 0; double PPTune0 = 0; FString PPSurf, PPSurf2; bool PPFlag = false; int32 PPLaunch0 = 0, PPCap0 = 0, PPCube0 = 0; int64 PPBounce0 = 0, PPPortal0 = 0, PPEnt0 = 0;
	void BuildEC();
	bool ECRing = false; void ECShotN(const FString& N) { Shot(ECRing ? N.Replace(TEXT("_ec_"), TEXT("_er_")) : N); }
	void AddECOp(const FString& Op, TSharedPtr<class FJsonObject> Args, const FString& Expect);
	FString ECLastReply; TSharedPtr<class FJsonObject> ECLastJson; int32 ECCount0 = 0, ECStep = 0; float ECStam0 = 0, ECHp0 = 0; bool ECFlag = false;
	void AddC64Spawn(const FString& Type, double Dist);
	void AddC64MobCheck(const FString& Label, bool bDiscard);
	FString C64Mob; float C64MobHealth = 0, C64Pct0 = 100; int64 C64Shots0 = 0, C64Hits0 = 0, C64Msg0 = 0; int32 C64Kills0 = 0, C64Ammo0 = 0, C64Hp0 = 0, C64Armor0 = 0; bool C64Flag = false;
	void AddSm64Tp(double X, double Y, double Z, float Yaw);
	void NoteSm64();
	FString SmSeen() const;
	int32 SmStage = 0, SmLand0 = 0; float SmT = 0, SmMax = 0, SmMax2 = 0, SmMax3 = 0; double SmZ0 = 0; bool SmFlag = false;
	FString World0;
	void AddGlassShot(const FString& Block, const FString& ShotPrefix);
	FIntVector GlassPos = FIntVector::ZeroValue; int32 Bursts0 = 0;
	void AddZmLookAt(const FString& Station);
	void AddZmInteract(const FString& Label, TFunction<void(const FString&)> OnResult);
	FVector ZmTarget = FVector::ZeroVector; int32 ZmPoints0 = 0, ZmKills0 = 0; bool bZmTearSeen = false;
	int32 EmptySlot() const;
	void AddDiscard(FString* Id);
	FString PigA, PigB; float PigHealth0 = 0; TSet<FString> AnimSeen;
	void Add(const FString& Name, TFunction<void()> Begin, TFunction<bool(float)> Update, float Timeout = 30.f);
	void Wait(float Seconds);
	void Pass(const FString& C);
	void Fail(const FString& C);
	void Check(bool b, const FString& C) { if (b) Pass(C); else Fail(C); }
	void Shot(const FString& Name);
	void Finish();
	void AddMovementCheck(const FString& Label);
	void AddCommand(const FString& Op, TSharedPtr<class FJsonObject> Args, TFunction<void(bool, const struct FCrbResult*)> OnDone, float Timeout = 8.f);
	void AddClickRow(const FString& Key, const FString& Label);
	void AddKey(const struct FKey& Key, const FString& Label);
	bool ClickAt(const FVector2D& Screen);
	void PressKey(const struct FKey& Key);
	struct FLuma { float Mean = 0, Blown = 0, Dark = 0; bool bOk = false; };
	FLuma MeasureViewport() { return LastShotLuma; }
	FLuma MeasureViewportUnused();
	void OnShot(int32 W, int32 H, const TArray<FColor>& Px);
	FString PendingShot; FLuma LastShotLuma; int64 PoseErrors0 = 0; bool bDelegateBound = false;
	void AimAt(double X, double Y, double Z);
	FString Diag() const; // Java player state + non-air blocks in the walking lane, for failure messages

	ACrbHost* Host;
	FString Suite, ReportPath, ShotDir;
	bool bQuit = false, bDone = false;
	TArray<FStep> Steps;
	int32 Index = -1;
	float Elapsed = 0, PrevElapsed = -1.f;
	// True on the single frame where step time passes At (robust to frame hitches, unlike a fixed time window).
	bool Cross(float At) const { return PrevElapsed <= At && Elapsed > At; }
	double StartTime = 0;
	TArray<FString> Checks, Failures, Shots;
	TSharedPtr<class FJsonObject> Metrics;
	// Scratch shared between step lambdas.
	double X0 = 0, Z0 = 0, Y0 = 0; int32 Seq0 = 0; float Walk0 = 0; int64 JavaSeq0 = 0; TSet<uint32> Hashes; FString PendingId; FString LastStepName;
	FLuma DayLuma, NightLuma, OffLuma;
};
