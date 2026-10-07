// The Unreal side of the bridge: owns the connection and every presentation subsystem. Game thread only.
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CrbCoords.h"
#include "CrbConnection.h"
#include "CrbTextures.h"
#include "CrbWorld.h"
#include "CrbAvatar.h"
#include "CrbTest.h"
#include "CrbPlayerAvatar.h"
#include "CrbShatter.h"
#include "CrbSteve.h"
#include "CrbPPSteve.h"
#include "CrbPortals.h"
#include "CrbECSteve.h"
#include "CrbHost.generated.h"

class ACrbPawn;
class UDirectionalLightComponent;
class USkyLightComponent;
class UExponentialHeightFogComponent;
class UPostProcessComponent;
class UPointLightComponent;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class FCrbTest;

struct FCrbSlot
{
	FString Id; FString Name; int32 Count = 0; int32 Damage = 0; int32 MaxDamage = 0;
};

// Zombies game mode (Java crb.zm.ZombiesGame.VIEW, exported as "zm").
struct FCrbZmZombie { int32 Id = 0; double X = 0, Y = 0, Z = 0; float Yaw = 0, Health = 0, MaxHealth = 1; int32 DeathTime = 0, HurtTime = 0, Anim = 0; bool bInside = false; };
struct FCrbZmState
{
	FString Phase = TEXT("OFF"); int32 Round = 0, Points = 0, Kills = 0, Headshots = 0, Left = 0;
	TArray<FString> Perks; FString Prompt; int32 Cost = 0; bool bAfford = false;
	int32 InstaKill = 0, DoublePoints = 0; FString PowerUp; int64 PowerUpTick = 0; FString Crate; int32 RoundTick = 0;
	FString Message; int64 MessageTick = 0, Tick = 0;
	int32 BulletsFired = 0, HeldMag = -1, ZombieHits = 0; // diagnostics
	int32 Capacity = 0, Reserve = 0;                    // held gun ammo (HUD)
	TArray<FCrbZmZombie> Zombies;
	struct FWallBuy { int32 X = 0, Y = 0, Z = 0, Cell = 0; FString Face; };
	TArray<FWallBuy> WallBuys; FString ChalkSheet, Box; int32 ChalkGen = 0;
	bool IsActive() const { return Phase != TEXT("OFF"); }
};

// SM64 Steve Movement (Java crb.client.sm64.Sm64Controller, exported as "sm64").
struct FCrbSm64State
{
	bool bRequested = false, bActive = false, bGrounded = true, bSlim = false, bServerNoFall = false;
	FString Action = TEXT("IDLE"), Skin, ExitReason, Blocked;
	int32 ActionId = 0, Timer = 0, Chain = 0, JumpSerial = 0, LandSerial = 0, ActionSerial = 0, WallKicks = 0, GroundPounds = 0, Knockbacks = 0, Exits = 0, Enters = 0;
	float FwdVel = 0, SideVel = 0, VelY = 0, FaceYaw = 0, IntendedMag = 0, MaxFwdVel = 0, LastJumpHeight = 0, LastLaunchVel = 0;
	int64 Frames = 0;
	TMap<FString, int32> Counts;
	int32 Count(const TCHAR* A) const { const int32* N = Counts.Find(A); return N ? *N : 0; }
};

// Craft 64 (Java crb.c64.Craft64, exported as "c64"): Doom 64-style shooter mode in the open Minecraft world.
struct FCrbC64State
{
	bool bOn = false, bBerserk = false;
	int32 Health = 0, Armor = 0, ArmorType = 0, Kills = 0, BfgCharge = 0, Refire = 0, PickupsNear = 0;
	FString Weapon, Pending, AmmoType, Message, LastFired;
	TArray<FString> Owned;
	TMap<FString, int32> Ammo, Max;
	int64 FireSeq = 0, MessageSeq = 0, HurtSeq = 0, BonusSeq = 0, Tick = 0, Hits = 0, Shots = 0;
	int32 AmmoOf(const FString& K) const { const int32* N = Ammo.Find(K); return N ? *N : 0; }
};

// Minecraft x Elden Combat (Java crb.ec.EldenCombat, exported as "ec"): the server's combat frames and resources.
struct FCrbECState
{
	bool bOn = false, bShield = false, bIFrames = false, bParry = false, bInfinite = false, bHitboxes = false;
	FString Act = TEXT("IDLE"), Weapon, Item, Event, ItemKey, ItemPx, Style;
	int32 T = 0, Len = 0, W = 0, A = 0, Combo = 0, Attacks = 0, Hits = 0, Dodges = 0, MobsTracked = 0, Dummies = 0;
	float Charge = 0, Stamina = 0, MaxStamina = 100, Poise = 0, MaxPoise = 1, Hp = 0, MaxHp = 20, LastDamage = 0;
	double DX = 0, DY = 0, DZ = 0; int64 Runes = 0, Tick = 0;
	int64 HitSeq = 0, ParrySeq = 0, BlockSeq = 0, GuardBreakSeq = 0, DodgeSeq = 0, RiposteSeq = 0, StaggerSeq = 0, HurtSeq = 0, SwingSeq = 0, KillSeq = 0, DeathSeq = 0, EventSeq = 0, DamageSeq = 0, MobStaggerSeq = 0;
	struct FLock { bool bValid = false; int32 Id = -1; double X = 0, Y = 0, Z = 0; float H = 0, W = 0, Hp = 0, Max = 0, Poise = 0, PoiseMax = 1; bool bStagger = false; FString Name; } Lock;
};

// Physics & Portal mod (Java crb.client.pp.PPController + crb.pp.Portals, exported as "pp").
struct FCrbPPState
{
	bool bRequested = false, bActive = false, bController = true, bGrounded = true, bRolling = false, bRagdoll = false, bLinked = false, bFrozen = false, bForceRoll = false, bForceSlide = false;
	FString State = TEXT("IDLE"), Modifier = TEXT("NORMAL"), Surface, SurfaceBlock, LastPortal, Event;
	float VX = 0, VY = 0, VZ = 0, Speed = 0, Heading = 0, BodyYaw = 0, NX = 0, NY = 1, NZ = 0, Friction = 0, Bounce = 0, YawDelta = 0, IX = 0, IY = 0, IZ = 0, Impact = 0, MaxSpeed = 0, TimeScale = 1, StateTime = 0;
	int64 PortalSeq = 0, RagdollSeq = 0, LandSeq = 0, BounceSeq = 0, LaunchSeq = 0, Frames = 0, EntityTeleports = 0; int32 Surfaces = 0, Objects = 0;
	struct FPortal { bool bValid = false; double X = 0, Y = 0, Z = 0; FVector N = FVector::ZeroVector, U = FVector::ZeroVector; FString Face; };
	FPortal Portals[2];
	TMap<FString, int32> Counts;
	TSharedPtr<FJsonObject> Json;
	int32 Count(const TCHAR* S) const { const int32* N = Counts.Find(S); return N ? *N : 0; }
};

struct FCrbState
{
	bool bValid = false;
	double X = 0, Y = 0, Z = 0; float Yaw = 0, Pitch = 0, Eye = 1.62f;
	bool bOnGround = false, bSneak = false, bSprint = false, bDead = false, bInWater = false, bFlying = false;
	double VX = 0, VY = 0, VZ = 0; int32 HurtTime = 0;
	// Custom Avatar add-on: the thrown axe (Java AxeEntity.VIEW).
	TArray<FCrbMutantState> Mutants; // God of War Unity port enemies
	FCrbZmState Zm;                  // Zombies game mode
	FCrbSm64State Sm64;              // SM64 Steve Movement
	FCrbC64State C64;                // Craft 64
	FCrbPPState PP;                  // Physics & Portal mod
	FCrbECState EC;                  // Minecraft x Elden Combat
	TArray<FCrbShatterEvent> Shatter; int64 ShatterTotal = 0; // projectile-broken glass (last 2 s)
	FString MapCurrent, MapLoading, MapStatus, MapWorld; int32 MapProgress = 0; // Maps menu (crb.client.Maps)
	bool bAxeActive = false; int32 AxePhase = 0; double AxeX = 0, AxeY = 0, AxeZ = 0; float AxeSpin = 0, AxeYaw = 0, AxeTravelled = 0; int32 AxeHits = 0;
	float Health = 20, MaxHealth = 20, Absorb = 0; int32 Armor = 0, Food = 20; float Saturation = 5;
	int32 XpLevel = 0; float XpProgress = 0; int32 Selected = 0;
	TArray<FCrbSlot> Slots; // 0-8 hotbar, 9 offhand, 10-13 armor, 14-40 inventory
	FString Screen, Fixture, GameMode, Dimension;
	int64 Tick = 0, DayTime = 0; float SkyDarken = 0, SunAngle = 0, Rain = 0; int32 EyeSky = 15, EyeBlock = 0;
	bool bHasHit = false; FIntVector Hit; int32 HitFace = 0; int32 HitState = 0;
	TArray<FVector> HitEdges; // pairs of block-local Minecraft points (vanilla outline shape edges)
	bool bGravityHolding = false; int32 GravityState = 0; double GX = 0, GY = 0, GZ = 0; float GravityDistance = 0;
	FLinearColor SkyColor = FLinearColor(0.47f, 0.65f, 1.f), FogColor = FLinearColor(0.7f, 0.8f, 1.f); bool bHasSky = false;
	int64 PoseSeq = 0, InputApplied = 0, PoseErrors = 0, TextureFailures = 0, TexturesSent = 0; float PoseWalkPos = 0;
	FString InputThread, ServerOpsThread, TextureLastError;
	int32 IconGen = 0, IconCell = 32, IconCols = 16; FString IconSheet;
	// Vanilla on-screen text: title (rendered by Java into crb:title), chat history, death screen.
	float TitleAlpha = 0; int32 TitleGen = 0; FString TitleText, SubtitleText;
	struct FChatLine { FString Text; int32 Age = 0; };
	TArray<FChatLine> Chat;
	FString DeathMessage; int32 Score = 0; int64 EntityErrors = 0;
	double ReceivedAt = 0;
};

struct FCrbResult { FString Id, Op, Message, Thread, Active; bool bOk = false; double At = 0; TSharedPtr<FJsonObject> Json; };
struct FCrbEvent { FString Kind, Phase, Id; double X = 0, Y = 0, Z = 0; int32 Removed = 0; double At = 0; };
struct FCrbModInfo { FString Id, Name, Version; bool bAddon = false; };

UENUM()
enum class ECrbWeapon : uint8 { Hand, GravityGun };

UCLASS(config = Game)
class ACrbHost : public AActor
{
	GENERATED_BODY()
public:
	ACrbHost();
	virtual ~ACrbHost();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	// ---- input from the pawn (game thread) ----
	void SetMove(float Forward, float Strafe) { MoveForward = Forward; MoveStrafe = Strafe; }
	void SetButtons(bool bJump, bool bSneak, bool bSprint)
	{
		const bool bChanged = bJump != bJumpHeld || bSneak != bSneakHeld || bSprint != bSprintHeld;
		if (bJump && !bJumpHeld) ++JumpPresses;     // monotonic counters: a tap shorter than a Minecraft tick still counts
		if (bSneak && !bSneakHeld) ++SneakPresses;
		bJumpHeld = bJump; bSneakHeld = bSneak; bSprintHeld = bSprint;
		if (bChanged) SendInput(true); // edges (e.g. vanilla double-tap jump to fly) must not wait for the next input tick
	}
	void Look(float DYaw, float DPitch);
	void PrimaryPressed(bool bDown);
	void SecondaryPressed(bool bDown);
	void SelectSlot(int32 Slot);
	void ScrollWheel(float Delta);
	void CycleView();
	void ToggleModMenu();
	void ToggleDebugMenu();
	void ToggleLighting();
	void ToggleDiagnostics() { bShowDiagnostics = !bShowDiagnostics; }
	// ---- Custom Avatar add-on (CrbAvatarMode.cpp) ----
	UPROPERTY(Config) bool bAvatarEnabled = false;
	FCrbPlayerAvatar PlayerAvatar;
	bool IsAvatarActive() const { return bAvatarEnabled && PlayerAvatar.AssetsReady(); }
	void ToggleAvatar();
	void AvatarRecall();
	bool IsMainHandEmpty() const { return !State.Slots.IsValidIndex(State.Selected) || State.Slots[State.Selected].Id.IsEmpty() || State.Slots[State.Selected].Id == TEXT("minecraft:air"); }
	// God of War Unity port: melee (LMB) and throw (RMB, empty main hand) play their clip and act at the clip's hit /
	// release moment like the repo's animation event; R or the middle mouse button recalls.
	double AttackStartedAt = -100, ThrowStartedAt = -100; bool bMeleePending = false, bThrowPending = false;
	float AvatarFacingYaw = 0; FVector AvatarCameraPivot = FVector::ZeroVector; float AvatarCameraDistance = 0;
	FCrbAvatarAnimInputs AvatarAnim;
	int32 AxeThrowsSent = 0, AxeRecallsSent = 0, AxeCatches = 0, MeleeSent = 0;
	FVector LastThrowTarget = FVector::ZeroVector;
	float AttackDuration() const;
	float ThrowDuration() const;
	bool IsAvatarAttacking() const { return FPlatformTime::Seconds() - AttackStartedAt < AttackDuration(); }
	void AvatarMoveInput(float& Fwd, float& Strafe, float& Yaw) const;
	void TickAvatarMode(float Dt);
	bool AvatarSecondary(bool bDown);
	void AvatarPrimary();
	void SpawnMutant(); void ClearMutants();
	// ---- Physics & Portal mod (CrbPhysicsPortal.cpp): Java runs momentum physics + portals; Unreal draws the 3D Steve,
	//      portal views, UE physics objects, ragdoll, camera and the debug menu ----
	UPROPERTY(Config) bool bPhysicsPortalEnabled = false;
	UPROPERTY(Config) bool bPPDebugMode = true;                  // developer debug menu allowed (F7); never in Shipping
	UPROPERTY(Config) FString PPDebugKey = TEXT("F7");
	UPROPERTY(Config) float PPCameraDistance = 460.f;
	bool IsPhysicsPortalActive() const { return bPhysicsPortalEnabled; }
	void TogglePhysicsPortal();
	void TickPhysicsPortal(float Dt);
	FString PPCommand(const FString& Cmd, const TSharedPtr<FJsonObject>& Args = nullptr);   // "pp.debug" {cmd,...}
	void PPShootPortal(int32 Which) { ++PPPortalPresses[Which == 0 ? 0 : 1]; SendInput(true); }
	void PPClearPortals() { ++PPPortalPresses[2]; SendInput(true); }
	void PPSpawnCube();
	FCrbPPSteve PPSteve; FCrbPortalViews PortalViews; FCrbPPCubes PPCubes;
	UPROPERTY() class UCrbPPDebugComponent* PPDebug = nullptr;
	int64 PPPortalPresses[3] = { 0, 0, 0 }; int64 PPSeenPortal = -1; bool bPPKeysFromTest = false, bPPRollHeld = false;
	int32 PPForceClip = -1; bool bPPAnimPaused = false;
	bool IsPPDebugOpen() const;
	// ---- Craft 64 (CrbCraft64.cpp): Java owns the Doom 64 rules; Unreal draws the weapon sprites, HUD and the look ----
	UPROPERTY(Config) bool bCraft64Enabled = false;
	UPROPERTY(Config) float Craft64ScreenPercentage = 45.f;   // low-res N64 look (HUD stays sharp)
	bool IsCraft64Active() const { return bCraft64Enabled; }
	void ToggleCraft64();
	void TickCraft64(float Dt);
	void Craft64Select(const FString& Want);
	FString C64Want; int32 C64Seq = 0; double C64LastOn = -10;
	// presentation (HUD reads these)
	float C64BobPhase = 0, C64BobAmp = 0, C64Raise = 1, C64SinceFire = 99, C64Hurt = 0, C64Bonus = 0; int32 C64Frame = 0;
	int64 C64SeenFire = -1, C64SeenHurt = -1, C64SeenBonus = -1, C64SeenMsg = -1; FString C64Shown, C64Msg; double C64MsgTime = -100;
	bool bC64LookApplied = false; float C64PrevScreenPct = 100.f; int32 C64PrevUpscale = 3;
	void ApplyCraft64Look(bool bOn);
	// ---- Minecraft x Elden Combat (CrbEldenCombat.cpp; mod id MinecraftEldenCombat). Java owns every combat rule;
	//      Unreal draws the custom 3D Steve + voxel weapon, the combat camera / lock-on, the HUD and the mod's debug
	//      tools. Everything is created on ON (ECStartup) and destroyed on OFF (ECShutdown); OFF = vanilla Minecraft. ----
	UPROPERTY(Config) bool bEldenCombatEnabled = false;
	UPROPERTY(Config) float ECCameraDistance = 430.f;
	UPROPERTY(Config) float ECWeaponPixel = 5.f;
	UPROPERTY(Config) int32 ECStyle = 0;            // 0 = Minecraft x Elden Combat, 1 = Elden Ring Combat (Steve): R_ animation rewrite
	bool IsEldenRingStyle() const { return bEldenCombatEnabled && ECStyle == 1; }
	void SelectEldenStyle(int32 Style);            // Mods menu rows: same style = off, other style = switch, off = on
	void ApplyECLook(bool bOn); bool bECLookApplied = false;
	bool IsEldenCombatActive() const { return bEldenCombatEnabled; }
	void ToggleEldenCombat();
	void TickEldenCombat(float Dt);
	void OpenECMenu();
	FString ECCommand(const FString& Op, const TSharedPtr<FJsonObject>& Args = nullptr);
	bool ECPrimary(bool bDown);       // input hooks: true when the mod consumed the button
	bool ECSecondary(bool bDown);
	bool ECLook(float DYaw);
	bool ECScroll(float Delta);
	FCrbECSteve ECSteve;
	int64 ECLight = 0, ECHeavy = 0, ECParry = 0, ECDodge = 0, ECLock = 0, ECSwitch = 0;
	bool bECHeavyHeld = false, bECBlockHeld = false, bECKeysFromTest = false, bECDebugOverlay = false, bECMenuOpen = false, bECStarted = false;
	double ECLastOn = -10; float ECFlick = 0, ECVisualYaw = 0;
	// presentation (HUD reads these)
	float ECHurtFlash = 0, ECStaminaLag = 0, ECHpLag = 0, ECDamageShown = 0, ECActT = 0; FString ECBanner; FLinearColor ECBannerColor = FLinearColor::White;
	double ECBannerTime = -100, ECDamageTime = -100, ECDeathTime = -100, ECBlockHitTime = -100; FVector ECDamageAt = FVector::ZeroVector;
	int64 ECSeenEvent = -1, ECSeenDamage = -1, ECSeenHurt = -1, ECSeenBlock = -1, ECSeenDeath = -1, ECSeenTick = -1; FString ECActKey;
	int32 ECAnimClip = 0; FString ECAnimClipName;
	void ECStartup(); void ECShutdown();
	// ---- SM64 Steve Movement (CrbSm64.cpp): Java runs the SM64-style physics, Unreal draws Steve and the camera ----
	UPROPERTY(Config) bool bSm64Enabled = false;
	UPROPERTY(Config) float Sm64SpeedMultiplier = 1.f;
	UPROPERTY(Config) float Sm64JumpMultiplier = 1.f;
	UPROPERTY(Config) float Sm64GravityMultiplier = 1.f;
	UPROPERTY(Config) bool bSm64WallKicks = true;
	UPROPERTY(Config) bool bSm64GroundPound = true;
	UPROPERTY(Config) bool bSm64LongJump = true;
	UPROPERTY(Config) bool bSm64VanillaFallDamage = false;
	UPROPERTY(Config) bool bSm64CtrlWalks = true;
	UPROPERTY(Config) float Sm64CameraDistance = 420.f;
	UPROPERTY(Config) bool bSm64SteveSkin = true;    // wear Minecraft's own Steve skin (1:1) instead of the account's skin
	UPROPERTY(Config) float Sm64ModelHeight = 160.f; // Steve's drawn height (UU); the physics scale stays Mario = 1.8 blocks
	FCrbSteve Steve;
	bool IsSm64Active() const { return bSm64Enabled; }
	void ToggleSm64();
	void PushSm64Config(const TSharedPtr<FJsonObject>& Extra = nullptr);
	void OpenSm64Menu();
	void TickSm64(float Dt);
	void UpdateOrbitCamera(const FVector& Feet, float MaxDist, float Dt);
	int32 Sm64ConfigEpoch = -1; int32 Sm64ConfigsSent = 0;
	FCrbSteveAnimInputs SteveAnim;
	int64 JumpPresses = 0, SneakPresses = 0;
	// ---- Zombies game mode (CrbZombies.cpp): Java owns the match; Unreal shows the HUD and sends F-interact ----
	void StartZombies(); void StopZombies();
	void InteractPressed(bool bDown);
	void TickZombies(float Dt);
	void OpenGameModesMenu();
	// ---- Maps menu (CrbMaps.cpp): pick a custom map (Maps folder) or the default superflat; Java swaps the world ----
	struct FMapInfo { FString Id, Name, Source; bool bImported = false; };
	TArray<FMapInfo> MapList; FString MapsFolder; bool bMapListReceived = false; bool bMapsMenuOpen = false;
	void OpenMapsMenu();
	void LoadMap(const FString& Id);
	void OnMapList(const TSharedPtr<FJsonObject>& J);
	bool IsZombiesActive() const { return State.Zm.IsActive(); }
	// BO2-style matchmaking lobby shown before a Zombies match starts (countdown, then zm.start).
	bool bZmLobby = false; double ZmLobbyStart = 0; static constexpr float ZmLobbySeconds = 6.f;
	bool bInteractHeld = false; double LastInteractSent = 0; int32 InteractsSent = 0; FString LastInteractResult;
	FString PendingInteractId;
	bool bPrevAxeActive = false; int32 PrevAxePhase = 0; int32 PrevHurtTime = 0;
	bool IsMenuOpen() const { return bModMenuOpen || bDebugMenuOpen || bInventoryOpen || bPauseOpen || bDeathOpen; }

	// ---- game UI: pause menu, death screen, game mode, creative inventory, Herobrine (CrbGameUI.cpp) ----
	bool bPauseOpen = false, bDeathOpen = false, bQuitting = false;
	void TogglePause();
	void Respawn();
	void QuitGame();
	void SetGameMode(const FString& Mode);
	bool IsCreative() const { return State.GameMode == TEXT("creative"); }
	bool IsSpectator() const { return State.GameMode == TEXT("spectator"); }
	struct FCreativeTab { int32 Index = 0; FString Id, Name, Type, Bg; int32 Row = 0, Col = 0; bool bRight = false, bScroll = true, bShowTitle = true; };
	struct FCreativeItem { FString Id, Name; int32 Count = 0; };
	TArray<FCreativeTab> CreativeTabs;
	TArray<FCreativeItem> CreativeItems;
	int32 CreativeTab = -1, CreativeRow = 0, CreativeMaxRow = 0, CreativeTotal = 0, CreativePageRow = -1, CreativePageTab = -1;
	FString CreativeQuery, CreativePageQuery;
	double CreativeQueryChangedAt = 0;
	bool IsCreativeSearch() const { return bInventoryOpen && IsCreative() && CreativeTabs.IsValidIndex(CreativeTab) && CreativeTabs[CreativeTab].Type == TEXT("search"); }
	bool IsCreativeInventoryTab() const { return CreativeTabs.IsValidIndex(CreativeTab) && CreativeTabs[CreativeTab].Type == TEXT("inventory"); }
	void CreativeSelectTab(int32 Index);
	void CreativeScroll(float Delta);
	void CreativeSetRow(int32 Row);
	void CreativeType(const FString& Chars);
	void CreativeBackspace();
	void CreativeClickAt(const FVector2D& Screen, int32 Button);
	void CreativeRequestPage(bool bForce = false);
	TSharedPtr<FJsonObject> HerobrineStatus;
	bool IsHerobrineInstalled() const;
	void OpenHerobrineMenu();
	void HerobrineCommand(const FString& Op, const TSharedPtr<FJsonObject>& Args = nullptr);
	void TickGameUI(float Dt);
	void OnGameUIResult(const FCrbResult& R);
	void ToggleInventory();
	void InventoryClick(int32 Button);
	void InventoryClickAt(const FVector2D& Screen, int32 Button);
	bool bInventoryOpen = false;
	// Minecraft light map sampled on the CPU (linear RGB) for entities/particles, exactly like vanilla's packed light.
	FLinearColor LightAt(int32 Block, int32 Sky) const;
	TArray<uint8> LightmapPixels;

	// ---- commands ----
	FString SendCommand(const FString& Op, const TSharedPtr<FJsonObject>& Args = nullptr);
	const FCrbResult* FindResult(const FString& Id) const { return Results.Find(Id); }
	void SelectWeapon(ECrbWeapon W);
	void ToggleFixture(const FString& Id);
	void CloseMenus();

	// ---- state for HUD / tests ----
	const FCrbState& GetState() const { return State; }
	FCrbCoords Coords;
	FCrbTextures Textures;
	FCrbWorld World;
	FCrbShatterFx ShatterFx;         // glass shard bursts (CrbShatter.cpp)
	FCrbAvatar Avatar;
	ACrbPawn* Pawn = nullptr;
	FCrbConnection* Link() const { return Connection.Get(); }
	ECrbWeapon Weapon = ECrbWeapon::Hand;
	float WeaponEquip = 1.f; // 0..1 equip animation
	int32 ViewMode = 0; // 0 first person, 1 rear, 2 front
	bool bInputOverride = false; // set by the packaged test driver
	bool bModMenuOpen = false, bDebugMenuOpen = false, bShowDiagnostics = false, bLightingEnabled = true;
	FString StatusLine;
	TArray<FCrbModInfo> Mods;
	TArray<TPair<FString, FString>> Fixtures; // id, label
	FString ActiveFixture;
	double LastFixtureResultAt = 0;
	TArray<FCrbEvent> Events;
	FString WelcomeText, McVersion, LoaderVersion, FabricApiVersion, BridgeVersion, Mappings;
	int32 ParticleCount = 0, ParticleDraw = 0, ActiveTorchLights = 0;
	int64 FramesHandled = 0, FramesRejected = 0;
	double LastStateTime = 0;
	float LastFrameMs = 0;
	FString BuildId;

	UPROPERTY(EditAnywhere, Config, Category = "Lighting") float BaseSunLux = 10000.f;
	UPROPERTY(EditAnywhere, Config, Category = "Lighting") float ExposureBias = -2.2f;
	UPROPERTY(EditAnywhere, Config, Category = "Lighting") float VanillaWeight = 0.85f;
	UPROPERTY(EditAnywhere, Config, Category = "Lighting") float SunWeight = 0.35f;
	UPROPERTY(EditAnywhere, Config, Category = "Lighting") int32 MaxTorchLights = 6;
	UPROPERTY(EditAnywhere, Config, Category = "Lighting") bool bVanillaTonemap = true;

	UPROPERTY() USceneComponent* Root;
	UPROPERTY() UDirectionalLightComponent* Sun;
	UPROPERTY() USkyLightComponent* SkyLight;
	UPROPERTY() UExponentialHeightFogComponent* Fog;
	UPROPERTY() UPostProcessComponent* Post;
	UPROPERTY() TArray<UPointLightComponent*> TorchLights;
	UPROPERTY() UProceduralMeshComponent* Particles;
	UPROPERTY() UProceduralMeshComponent* HeldBlock;
	UPROPERTY() UProceduralMeshComponent* Outline;
	UPROPERTY() UMaterialInterface* MatOutline;
	UPROPERTY() UMaterialInterface* MatTonemap = nullptr;
	bool bOutlineVisible = false;
	void UpdateOutline();
	UPROPERTY() UProceduralMeshComponent* GunMesh;
	UPROPERTY() UStaticMeshComponent* Beam;
	UPROPERTY() UStaticMeshComponent* Orb;
	UPROPERTY() UStaticMeshComponent* SkySphere;
	UPROPERTY() UMaterialInstanceDynamic* SkyMid;
	UPROPERTY() UMaterialInterface* MatOpaque;
	UPROPERTY() UMaterialInterface* MatTranslucent;
	UPROPERTY() UMaterialInterface* MatEntity;
	UPROPERTY() UMaterialInterface* MatEntityTranslucent;
	UPROPERTY() UMaterialInterface* MatParticle;
	UPROPERTY() UMaterialInterface* MatEmissive;
	UPROPERTY() UMaterialInterface* MatVertexColor;
	UPROPERTY() UMaterialInstanceDynamic* ParticleMids[2];
	UPROPERTY() UProceduralMeshComponent* ChalkMesh = nullptr;     // Zombies wall-buy chalk outlines
	UPROPERTY() UMaterialInstanceDynamic* ChalkMid = nullptr;
	FString ChalkSignature; int32 ChalkQuads = 0;
	void TickChalk();
	UPROPERTY() UMaterialInstanceDynamic* BeamMid;
	UPROPERTY() UMaterialInstanceDynamic* OrbMid;
	UPROPERTY() UMaterialInstanceDynamic* HeldMid;
	UPROPERTY() UMaterialInstanceDynamic* GunMid;

	TUniquePtr<FCrbTest> Test;

	// Interpolated presentation of the Java player (feet position in MC units).
	void PresentedFeet(double& X, double& Y, double& Z) const;
	FVector EyeLocationUE() const;
	FRotator ViewRotation() const { return FRotator(LookPitch, LookYaw, 0); }
	float LookYaw = 0, LookPitch = 0;
	FTransform ViewBob = FTransform::Identity;
	bool bLookInitialised = false;

	// Particle snapshots (exposed for tests).
	struct FParticle { uint32 Id; FVector Pos; float Size; FVector2D UV0, UV1; FLinearColor Color; uint8 Sheet; uint8 Block, Sky; };
	TArray<FParticle> ParticleNow;

private:
	void HandleFrame(const Crb::FFrame& F);
	void OnState(const TSharedPtr<FJsonObject>& J);
	void OnWelcome(const TSharedPtr<FJsonObject>& J);
	void OnResult(const TSharedPtr<FJsonObject>& J);
	void OnEvent(const TSharedPtr<FJsonObject>& J);
	void OnParticles(const TSharedPtr<FJsonObject>& J, const TArray<uint8>& Bin);
	void OnLightmap(const TSharedPtr<FJsonObject>& J, const TArray<uint8>& Bin);
	void SendInput(bool bForce);
	void ResetForNewConnection();
	void UpdateAnchor();
	void UpdateLighting(float Dt);
	void UpdateParticles();
	void UpdateWeapons(float Dt);
	void InitPresentation();
	bool bPresentationReady = false;
	void BuildGunMesh();
	int32 GunMeshKind = -1;
	void BuildHeldBlockMesh(int32 StateId);
	UMaterialInterface* LoadMat(const TCHAR* Path);

	TUniquePtr<FCrbConnection> Connection;
	int32 SeenEpoch = 0;
	FCrbState State, PrevState;
	TMap<FString, FCrbResult> Results;
	int32 NextCommand = 1;
	float MoveForward = 0, MoveStrafe = 0;
	bool bJumpHeld = false, bSneakHeld = false, bSprintHeld = false, bAttackHeld = false, bUseHeld = false;
	int64 AttackPresses = 0, UsePresses = 0;
	int32 PendingSlot = -1;
	double LastInputSent = 0;
	FString LastInputJson;
	UTexture2D* Lightmap = nullptr;
	int32 LightmapGen = 0;
	int32 AtlasVersionSeen = -1;
	int32 HeldStateMeshed = -1;
	int64 LastParticleTick = -1;
	TArray<FParticle> ParticlePrev;
	double ParticleTime = 0;
	float BeamPulse = 0;
};
