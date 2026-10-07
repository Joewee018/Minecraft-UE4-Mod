#include "CrbHost.h"
#include "CrbMcUi.h"
#include "CrbPawn.h"
#include "CrbTest.h"
#include "CrbMenus.h"
#include "CrbHUD.h"
#include "GameFramework/PlayerController.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	FString Str(const TSharedPtr<FJsonObject>& J, const TCHAR* K) { FString S; if (J.IsValid()) J->TryGetStringField(K, S); return S; }
	double Num(const TSharedPtr<FJsonObject>& J, const TCHAR* K, double D = 0) { double V = D; if (J.IsValid()) J->TryGetNumberField(K, V); return FMath::IsFinite(V) ? V : D; }
	bool Bool(const TSharedPtr<FJsonObject>& J, const TCHAR* K) { bool B = false; if (J.IsValid()) J->TryGetBoolField(K, B); return B; }
	constexpr double InputIntervalSeconds = 1.0 / 30.0;
}

ACrbHost::ACrbHost()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(Root);
	Sun->SetMobility(EComponentMobility::Movable);
	SkyLight = nullptr;
	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(Root);
	Post = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Post"));
	Post->SetupAttachment(Root);
	Post->bUnbound = true;
}

ACrbHost::~ACrbHost() {}

UMaterialInterface* ACrbHost::LoadMat(const TCHAR* Path)
{
	UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, Path);
	if (!M) { UE_LOG(LogCrb, Error, TEXT("Missing material %s (run the asset build step); using engine default"), Path); M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial")); }
	return M;
}

void ACrbHost::BeginPlay()
{
	Super::BeginPlay();
	BuildId = FString::Printf(TEXT("%s %s"), FApp::GetBuildVersion(), ANSI_TO_TCHAR(__DATE__ " " __TIME__));
	MatOpaque = LoadMat(TEXT("/Game/Crb/M_CrbBlock.M_CrbBlock"));
	MatTranslucent = LoadMat(TEXT("/Game/Crb/M_CrbBlockTranslucent.M_CrbBlockTranslucent"));
	MatEntity = LoadMat(TEXT("/Game/Crb/M_CrbEntity.M_CrbEntity"));
	MatEntityTranslucent = LoadMat(TEXT("/Game/Crb/M_CrbEntityTranslucent.M_CrbEntityTranslucent"));
	MatParticle = LoadMat(TEXT("/Game/Crb/M_CrbParticle.M_CrbParticle"));
	MatEmissive = LoadMat(TEXT("/Game/Crb/M_CrbEmissive.M_CrbEmissive"));
	MatVertexColor = LoadMat(TEXT("/Game/Crb/M_CrbVertexColor.M_CrbVertexColor"));
	MatOutline = LoadMat(TEXT("/Game/Crb/M_CrbOutline.M_CrbOutline"));
	MatTonemap = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Crb/M_CrbTonemap.M_CrbTonemap"));

	World.Init(this, MatOpaque, MatTranslucent);
	ShatterFx.Init(this, &World);


	// Lighting: Java light levels drive the materials; the UE sun adds direct light and restrained contact shadows.
	Sun->SetIntensity(BaseSunLux);
	Sun->SetLightColor(FLinearColor(1.f, 0.97f, 0.92f));
	Sun->SetCastShadows(true);
	Sun->ContactShadowLength = 0.04f;
	Sun->DynamicShadowDistanceMovableLight = 6000.f;
	Sun->DynamicShadowCascades = 2;
	Sun->MarkRenderStateDirty();
	Fog->SetFogDensity(0.03f);
	Fog->SetFogHeightFalloff(0.001f);
	Fog->SetStartDistance(2600.f);
	Fog->SetFogMaxOpacity(1.f);
	Fog->FogCutoffDistance = 150000.f; // the sky dome (2 km) keeps Minecraft's sky colour

	FPostProcessSettings& P = Post->Settings;
	P.bOverride_AutoExposureMethod = true; P.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	P.bOverride_AutoExposureApplyPhysicalCameraExposure = true; P.AutoExposureApplyPhysicalCameraExposure = false;
	P.bOverride_AutoExposureBias = true;
	P.bOverride_BloomIntensity = true; P.BloomIntensity = 0.12f;
	P.bOverride_VignetteIntensity = true; P.VignetteIntensity = 0.f;
	P.bOverride_MotionBlurAmount = true; P.MotionBlurAmount = 0.f;
	P.bOverride_AmbientOcclusionIntensity = true; P.AmbientOcclusionIntensity = 0.25f;
	P.bOverride_ScreenSpaceReflectionIntensity = true; P.ScreenSpaceReflectionIntensity = 40.f;
	P.bOverride_LensFlareIntensity = true; P.LensFlareIntensity = 0.f;
	// Vanilla display encoding (replaces ACES filmic, which lifted and desaturated the Minecraft colours).
	if (bVanillaTonemap && MatTonemap) P.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, MatTonemap));
	else if (bVanillaTonemap) UE_LOG(LogCrb, Error, TEXT("Missing M_CrbTonemap (run the asset build step); using the filmic tonemapper"));

	// Torch light pool (bounded).
	for (int32 I = 0; I < FMath::Clamp(MaxTorchLights, 0, 16); ++I)
	{
		UPointLightComponent* L = NewObject<UPointLightComponent>(this);
		L->SetupAttachment(Root); L->RegisterComponent();
		L->SetMobility(EComponentMobility::Movable);
		L->SetCastShadows(false);
		L->bUseInverseSquaredFalloff = false;
		L->SetLightFalloffExponent(2.f);
		L->SetAttenuationRadius(900.f);
		L->SetLightColor(FLinearColor(1.f, 0.72f, 0.4f));
		L->SetIntensity(0.f);
		L->SetVisibility(false);
		TorchLights.Add(L);
	}

	InitPresentation();

	Mods.Add({ TEXT("hand"), TEXT("Minecraft hand (vanilla)"), TEXT("1.20.1"), true });
	Mods.Add({ TEXT("gravity_gun"), TEXT("Gravity Gun"), TEXT("1.0.0"), true });
	Mods.Add({ TEXT("sm64"), TEXT("SM64 Steve Movement"), TEXT("1.0.0"), true });
	Mods.Add({ TEXT("craft64"), TEXT("Craft 64"), TEXT("1.0.0"), true });
	Mods.Add({ TEXT("physicsportal"), TEXT("Minecraft Physics & Portal Mod"), TEXT("1.0.0"), true });
	Mods.Add({ TEXT("eldencombat"), TEXT("Minecraft \u00d7 Elden Combat"), TEXT("1.0.0"), true });   // MinecraftEldenCombat

	FString EndpointPath;
	if (!FParse::Value(FCommandLine::Get(), TEXT("-CrbEndpoint="), EndpointPath))
		EndpointPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Work/bridge-endpoint.json")));
	EndpointPath = EndpointPath.TrimQuotes();
	Connection = MakeUnique<FCrbConnection>(EndpointPath);
	Connection->Start();
	StatusLine = TEXT("Waiting for Minecraft (") + EndpointPath + TEXT(")");

	FString Suite;
	if (FParse::Value(FCommandLine::Get(), TEXT("-CrbTest="), Suite)) Test = MakeUnique<FCrbTest>(this, Suite);
}


void ACrbHost::InitPresentation()
{
	if (bPresentationReady) return;
	Pawn = Cast<ACrbPawn>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!Pawn) return; // retried from Tick until the player pawn exists
	bPresentationReady = true;
	USceneComponent* CameraRoot = Pawn->Camera;
	Avatar.Init(Root, CameraRoot, MatEntity, MatEntityTranslucent);
	PlayerAvatar.Init(this, Root, MatVertexColor);
	Steve.Init(this, Root);
	PPSteve.Init(this, Root); PortalViews.Init(this, Root); PPCubes.Init(this, Root);
	auto MakeMesh = [&](const TCHAR* Name, USceneComponent* Parent)
	{
		UProceduralMeshComponent* M = NewObject<UProceduralMeshComponent>(this, Name);
		M->SetupAttachment(Parent); M->RegisterComponent();
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return M;
	};
	Particles = MakeMesh(TEXT("CrbParticles"), Root);
	Particles->SetCastShadow(false);
	Particles->bUseAttachParentBound = false;
	HeldBlock = MakeMesh(TEXT("CrbHeldBlock"), Root);
	Outline = MakeMesh(TEXT("CrbOutline"), Root);
	Outline->SetCastShadow(false);
	GunMesh = MakeMesh(TEXT("CrbGun"), CameraRoot);
	GunMesh->SetCastShadow(false);
	for (int32 I = 0; I < 2; ++I) { ParticleMids[I] = UMaterialInstanceDynamic::Create(MatParticle, this); ParticleMids[I]->SetScalarParameterValue(TEXT("HasTexture"), 0.f); }
	HeldMid = UMaterialInstanceDynamic::Create(MatOpaque, this);
	GunMid = UMaterialInstanceDynamic::Create(MatVertexColor, this);

	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	auto MakeStatic = [&](const TCHAR* Name, UStaticMesh* Mesh, USceneComponent* Parent, UMaterialInstanceDynamic*& Mid, FLinearColor Color)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, Name);
		C->SetupAttachment(Parent); C->RegisterComponent();
		C->SetStaticMesh(Mesh); C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetCastShadow(false);
		Mid = UMaterialInstanceDynamic::Create(MatEmissive, this);
		Mid->SetVectorParameterValue(TEXT("Color"), Color);
		C->SetMaterial(0, Mid); C->SetVisibility(false);
		return C;
	};
	Beam = MakeStatic(TEXT("CrbBeam"), Cylinder, Root, BeamMid, FLinearColor(0.35f, 0.85f, 1.f));
	Orb = MakeStatic(TEXT("CrbOrb"), Sphere, CameraRoot, OrbMid, FLinearColor(0.5f, 0.95f, 1.f));
	// Sky dome: unlit colour driven by Java time of day. Centred on the camera every frame.
	SkySphere = NewObject<UStaticMeshComponent>(this, TEXT("CrbSky"));
	SkySphere->SetupAttachment(Root); SkySphere->RegisterComponent();
	SkySphere->SetStaticMesh(Sphere); SkySphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkySphere->SetCastShadow(false); SkySphere->SetWorldScale3D(FVector(-4000.f)); SkySphere->bAffectDistanceFieldLighting = false;
	SkyMid = UMaterialInstanceDynamic::Create(LoadMat(TEXT("/Game/Crb/M_CrbSky.M_CrbSky")), this);
	SkySphere->SetMaterial(0, SkyMid);
	BuildGunMesh();
}

void ACrbHost::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Connection)
	{
		if (Connection->GetState() == ECrbLinkState::Connected) Connection->Send(Crb::EType::Bye, TEXT("{}"));
		FPlatformProcess::Sleep(0.05f);
		Connection->Shutdown();
		Connection.Reset();
	}
	CrbMenus::Close(this);
	Super::EndPlay(Reason);
}

void ACrbHost::ResetForNewConnection()
{
	World.Reset();
	ShatterFx.Reset();
	Textures.Reset();
	Avatar.Reset();
	Lightmap = nullptr; AtlasVersionSeen = -1; HeldStateMeshed = -1;
	State = FCrbState(); PrevState = FCrbState();
	Results.Reset(); Events.Reset();
	ParticleNow.Reset(); ParticlePrev.Reset();
	LastInputJson.Reset();
	bLookInitialised = false;
	SendCommand(TEXT("fixture.list"));
}

FString ACrbHost::SendCommand(const FString& Op, const TSharedPtr<FJsonObject>& Args)
{
	const FString Id = FString::Printf(TEXT("ue-%d"), NextCommand++);
	TSharedRef<FJsonObject> C = MakeShared<FJsonObject>();
	C->SetStringField(TEXT("id"), Id); C->SetStringField(TEXT("op"), Op);
	C->SetObjectField(TEXT("args"), Args.IsValid() ? Args : MakeShared<FJsonObject>());
	if (Connection) Connection->Send(Crb::EType::Command, Crb::ToJson(C));
	return Id;
}

void ACrbHost::HandleFrame(const Crb::FFrame& F)
{
	FString Error;
	bool bOk = true;
	switch ((Crb::EType)F.Type)
	{
	case Crb::EType::Welcome: OnWelcome(F.Json()); break;
	case Crb::EType::State: OnState(F.Json()); break;
	case Crb::EType::Result: OnResult(F.Json()); break;
	case Crb::EType::Event: OnEvent(F.Json()); break;
	case Crb::EType::Mods: OnWelcome(F.Json()); break;
	case Crb::EType::Window: World.OnWindow(F.Json()); break;
	case Crb::EType::Section: bOk = World.OnSection(F.Json(), F.Bin, Error); break;
	case Crb::EType::Model: bOk = World.OnModel(F.Json(), F.Bin, Error); break;
	case Crb::EType::Texture: bOk = Textures.OnFrame(F.Json(), F.Bin, Error); break;
	case Crb::EType::Pose: bOk = Avatar.OnPose(F.Json(), F.Bin, Error); break;
	case Crb::EType::Lightmap: OnLightmap(F.Json(), F.Bin); break;
	case Crb::EType::Particles: OnParticles(F.Json(), F.Bin); break;
	default: bOk = false; Error = FString::Printf(TEXT("unexpected frame type %d"), F.Type); break;
	}
	++FramesHandled;
	if (!bOk) { ++FramesRejected; UE_LOG(LogCrb, Warning, TEXT("Rejected bridge frame: %s"), *Error); }
}

void ACrbHost::OnWelcome(const TSharedPtr<FJsonObject>& J)
{
	if (!J.IsValid()) return;
	McVersion = Str(J, TEXT("minecraft")); LoaderVersion = Str(J, TEXT("loader")); FabricApiVersion = Str(J, TEXT("fabricApi"));
	BridgeVersion = Str(J, TEXT("bridge")); Mappings = Str(J, TEXT("mappings"));
	WelcomeText = FString::Printf(TEXT("Minecraft %s | Fabric Loader %s | Fabric API %s | bridge %s"), *McVersion, *LoaderVersion, *FabricApiVersion, *BridgeVersion);
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (J->TryGetArrayField(TEXT("fabricMods"), Arr))
	{
		Mods.RemoveAll([](const FCrbModInfo& M) { return !M.bAddon; });
		for (int32 I = 0; I < Arr->Num() && I < 64; ++I)
		{
			const TSharedPtr<FJsonObject> O = (*Arr)[I]->AsObject();
			Mods.Add({ Str(O, TEXT("id")), Str(O, TEXT("name")), Str(O, TEXT("version")), false });
		}
	}
}

void ACrbHost::OnState(const TSharedPtr<FJsonObject>& J)
{
	if (!J.IsValid()) return;
	FCrbState S;
	S.X = Num(J, TEXT("x")); S.Y = Num(J, TEXT("y")); S.Z = Num(J, TEXT("z"));
	if (FMath::Abs(S.X) > 3.0e7 || FMath::Abs(S.Z) > 3.0e7 || FMath::Abs(S.Y) > 4096) { ++FramesRejected; return; }
	S.bValid = true;
	S.Yaw = Num(J, TEXT("yaw")); S.Pitch = Num(J, TEXT("pitch")); S.Eye = FMath::Clamp((float)Num(J, TEXT("eye"), 1.62), 0.2f, 3.f);
	S.bOnGround = Bool(J, TEXT("onGround")); S.bSneak = Bool(J, TEXT("sneak")); S.bSprint = Bool(J, TEXT("sprint")); S.bDead = Bool(J, TEXT("dead")); S.bInWater = Bool(J, TEXT("inWater"));
	S.bFlying = Bool(J, TEXT("flying")); S.HurtTime = (int32)Num(J, TEXT("hurtTime"));
	S.VX = Num(J, TEXT("vx")); S.VY = Num(J, TEXT("vy")); S.VZ = Num(J, TEXT("vz"));
	const TArray<TSharedPtr<FJsonValue>>* MutA = nullptr;
	if (J->TryGetArrayField(TEXT("mutants"), MutA))
		for (int32 I = 0; I < MutA->Num() && I < 16; ++I)
		{
			const TSharedPtr<FJsonObject> O = (*MutA)[I]->AsObject();
			if (!O.IsValid()) continue;
			FCrbMutantState M; M.Id = (int32)Num(O, TEXT("id")); M.X = Num(O, TEXT("x")); M.Y = Num(O, TEXT("y")); M.Z = Num(O, TEXT("z"));
			M.Yaw = Num(O, TEXT("yaw")); M.Health = Num(O, TEXT("health")); M.MaxHealth = FMath::Max(1.0, Num(O, TEXT("maxHealth"), 100));
			M.DeathTime = (int32)Num(O, TEXT("deathTime")); M.HurtTime = (int32)Num(O, TEXT("hurtTime"));
			S.Mutants.Add(M);
		}
	const TArray<TSharedPtr<FJsonValue>>* ShA = nullptr;
	if (J->TryGetArrayField(TEXT("shatter"), ShA))
		for (int32 I = 0; I < ShA->Num() && I < 64; ++I)
		{
			const TSharedPtr<FJsonObject> O = (*ShA)[I]->AsObject();
			if (!O.IsValid()) continue;
			FCrbShatterEvent E; E.Seq = (int64)Num(O, TEXT("seq")); E.X = (int32)Num(O, TEXT("x")); E.Y = (int32)Num(O, TEXT("y")); E.Z = (int32)Num(O, TEXT("z"));
			E.State = (int32)Num(O, TEXT("state")); E.Light = (int32)Num(O, TEXT("light")); E.Age = (int32)Num(O, TEXT("age"));
			E.Dir = FVector(Num(O, TEXT("dx")), Num(O, TEXT("dy")), Num(O, TEXT("dz"))); E.Source = Str(O, TEXT("source"));
			S.Shatter.Add(E);
		}
	S.ShatterTotal = (int64)Num(J, TEXT("shatterTotal"));
	const TSharedPtr<FJsonObject>* SmO = nullptr;
	if (J->TryGetObjectField(TEXT("sm64"), SmO) && SmO->IsValid())
	{
		const TSharedPtr<FJsonObject>& O = *SmO; FCrbSm64State& M = S.Sm64;
		M.bRequested = Bool(O, TEXT("requested")); M.bActive = Bool(O, TEXT("active")); M.bGrounded = Bool(O, TEXT("grounded"));
		M.bSlim = Bool(O, TEXT("slim")); M.bServerNoFall = Bool(O, TEXT("serverNoFall"));
		M.Action = Str(O, TEXT("action")); M.Skin = Str(O, TEXT("skin")); M.ExitReason = Str(O, TEXT("exitReason")); M.Blocked = Str(O, TEXT("blocked"));
		M.ActionId = (int32)Num(O, TEXT("actionId")); M.Timer = (int32)Num(O, TEXT("timer")); M.Chain = (int32)Num(O, TEXT("chain"));
		M.JumpSerial = (int32)Num(O, TEXT("jumpSerial")); M.LandSerial = (int32)Num(O, TEXT("landSerial")); M.ActionSerial = (int32)Num(O, TEXT("actionSerial"));
		M.WallKicks = (int32)Num(O, TEXT("wallKicks")); M.GroundPounds = (int32)Num(O, TEXT("groundPounds")); M.Knockbacks = (int32)Num(O, TEXT("knockbacks"));
		M.Enters = (int32)Num(O, TEXT("enters")); M.Exits = (int32)Num(O, TEXT("exits"));
		M.FwdVel = Num(O, TEXT("fwdVel")); M.SideVel = Num(O, TEXT("sideVel")); M.VelY = Num(O, TEXT("velY")); M.FaceYaw = Num(O, TEXT("faceYaw"));
		M.IntendedMag = Num(O, TEXT("intendedMag")); M.MaxFwdVel = Num(O, TEXT("maxFwdVel")); M.LastJumpHeight = Num(O, TEXT("lastJumpHeight")); M.LastLaunchVel = Num(O, TEXT("lastLaunchVel"));
		M.Frames = (int64)Num(O, TEXT("frames"));
		const TSharedPtr<FJsonObject>* CO = nullptr;
		if (O->TryGetObjectField(TEXT("counts"), CO) && CO->IsValid())
			for (const auto& KV : (*CO)->Values) M.Counts.Add(KV.Key, (int32)KV.Value->AsNumber());
	}
	const TSharedPtr<FJsonObject>* PPO = nullptr;
	if (J->TryGetObjectField(TEXT("pp"), PPO) && PPO->IsValid())
	{
		const TSharedPtr<FJsonObject>& O = *PPO; FCrbPPState& P = S.PP;
		P.Json = O;
		P.bRequested = Bool(O, TEXT("requested")); P.bActive = Bool(O, TEXT("active")); P.bController = Bool(O, TEXT("controller")); P.bGrounded = Bool(O, TEXT("grounded"));
		P.bRolling = Bool(O, TEXT("rolling")); P.bRagdoll = Bool(O, TEXT("ragdoll")); P.bFrozen = Bool(O, TEXT("frozen")); P.bForceRoll = Bool(O, TEXT("forceRoll")); P.bForceSlide = Bool(O, TEXT("forceSlide"));
		P.State = Str(O, TEXT("state")); P.Modifier = Str(O, TEXT("modifier")); P.Surface = Str(O, TEXT("surface")); P.SurfaceBlock = Str(O, TEXT("surfaceBlock"));
		P.LastPortal = Str(O, TEXT("lastPortal")); P.Event = Str(O, TEXT("event"));
		P.VX = Num(O, TEXT("vx")); P.VY = Num(O, TEXT("vy")); P.VZ = Num(O, TEXT("vz")); P.Speed = Num(O, TEXT("speed")); P.Heading = Num(O, TEXT("heading")); P.BodyYaw = Num(O, TEXT("bodyYaw"));
		P.NX = Num(O, TEXT("nx")); P.NY = Num(O, TEXT("ny"), 1); P.NZ = Num(O, TEXT("nz")); P.Friction = Num(O, TEXT("friction")); P.Bounce = Num(O, TEXT("bounce"));
		P.YawDelta = Num(O, TEXT("yawDelta")); P.IX = Num(O, TEXT("ix")); P.IY = Num(O, TEXT("iy")); P.IZ = Num(O, TEXT("iz")); P.Impact = Num(O, TEXT("impact"));
		P.MaxSpeed = Num(O, TEXT("maxSpeed")); P.TimeScale = Num(O, TEXT("timeScale"), 1); P.StateTime = Num(O, TEXT("stateTime"));
		P.PortalSeq = (int64)Num(O, TEXT("portalSeq")); P.RagdollSeq = (int64)Num(O, TEXT("ragdollSeq")); P.LandSeq = (int64)Num(O, TEXT("landSeq"));
		P.BounceSeq = (int64)Num(O, TEXT("bounceSeq")); P.LaunchSeq = (int64)Num(O, TEXT("launchSeq")); P.Frames = (int64)Num(O, TEXT("frames")); P.Surfaces = (int32)Num(O, TEXT("surfaces"));
		const TSharedPtr<FJsonObject>* CO = nullptr;
		if (O->TryGetObjectField(TEXT("counts"), CO) && CO->IsValid()) for (const auto& KV : (*CO)->Values) P.Counts.Add(KV.Key, (int32)KV.Value->AsNumber());
		const TSharedPtr<FJsonObject>* PO = nullptr;
		if (O->TryGetObjectField(TEXT("portals"), PO) && PO->IsValid())
		{
			P.bLinked = Bool(*PO, TEXT("linked")); P.EntityTeleports = (int64)Num(*PO, TEXT("entityTeleports")); P.Objects = (int32)Num(*PO, TEXT("objects"));
			const TArray<TSharedPtr<FJsonValue>>* PA = nullptr;
			if ((*PO)->TryGetArrayField(TEXT("portals"), PA))
				for (int32 I = 0; I < 2 && I < PA->Num(); ++I)
				{
					const TSharedPtr<FJsonObject> Q = (*PA)[I]->AsObject(); FCrbPPState::FPortal& T = P.Portals[I];
					T.bValid = Q.IsValid() && Q->HasField(TEXT("x"));
					if (!T.bValid) continue;
					T.X = Num(Q, TEXT("x")); T.Y = Num(Q, TEXT("y")); T.Z = Num(Q, TEXT("z"));
					T.N = FVector(Num(Q, TEXT("nx")), Num(Q, TEXT("ny")), Num(Q, TEXT("nz"))); T.U = FVector(Num(Q, TEXT("ux")), Num(Q, TEXT("uy")), Num(Q, TEXT("uz"))); T.Face = Str(Q, TEXT("face"));
				}
		}
	}
	const TSharedPtr<FJsonObject>* EO = nullptr;
	if (J->TryGetObjectField(TEXT("ec"), EO) && EO->IsValid())
	{
		const TSharedPtr<FJsonObject>& O = *EO; FCrbECState& E = S.EC;
		const FString PrevKey = State.EC.ItemKey, PrevPx = State.EC.ItemPx;   // the sprite pixels arrive every 10 ticks; keep them
		E = FCrbECState();
		E.bOn = Bool(O, TEXT("on"));
		if (E.bOn)
		{
			E.Act = Str(O, TEXT("act")); E.T = (int32)Num(O, TEXT("t")); E.Len = (int32)Num(O, TEXT("len")); E.W = (int32)Num(O, TEXT("w")); E.A = (int32)Num(O, TEXT("a"));
			E.Combo = (int32)Num(O, TEXT("combo")); E.Charge = (float)Num(O, TEXT("charge")); E.Weapon = Str(O, TEXT("weapon")); E.Item = Str(O, TEXT("item"));
			E.bShield = Bool(O, TEXT("shield")); E.bIFrames = Bool(O, TEXT("iframes")); E.bParry = Bool(O, TEXT("parry"));
			E.Stamina = (float)Num(O, TEXT("stamina")); E.MaxStamina = (float)Num(O, TEXT("maxStamina")); E.Poise = (float)Num(O, TEXT("poise")); E.MaxPoise = (float)Num(O, TEXT("maxPoise"));
			E.Hp = (float)Num(O, TEXT("hp")); E.MaxHp = (float)Num(O, TEXT("maxHp")); E.Runes = (int64)Num(O, TEXT("runes"));
			E.Attacks = (int32)Num(O, TEXT("attacks")); E.Hits = (int32)Num(O, TEXT("hits")); E.Dodges = (int32)Num(O, TEXT("dodges"));
			E.HitSeq = (int64)Num(O, TEXT("hitSeq")); E.ParrySeq = (int64)Num(O, TEXT("parrySeq")); E.BlockSeq = (int64)Num(O, TEXT("blockSeq")); E.GuardBreakSeq = (int64)Num(O, TEXT("guardBreakSeq"));
			E.DodgeSeq = (int64)Num(O, TEXT("dodgeSeq")); E.RiposteSeq = (int64)Num(O, TEXT("riposteSeq")); E.StaggerSeq = (int64)Num(O, TEXT("staggerSeq")); E.HurtSeq = (int64)Num(O, TEXT("hurtSeq"));
			E.SwingSeq = (int64)Num(O, TEXT("swingSeq")); E.KillSeq = (int64)Num(O, TEXT("killSeq")); E.DeathSeq = (int64)Num(O, TEXT("deathSeq")); E.EventSeq = (int64)Num(O, TEXT("eventSeq"));
			E.DamageSeq = (int64)Num(O, TEXT("damageSeq")); E.MobStaggerSeq = (int64)Num(O, TEXT("mobStaggerSeq")); E.Event = Str(O, TEXT("event")); E.LastDamage = (float)Num(O, TEXT("lastDamage"));
			E.DX = Num(O, TEXT("dx")); E.DY = Num(O, TEXT("dy")); E.DZ = Num(O, TEXT("dz")); E.bInfinite = Bool(O, TEXT("infinite")); E.bHitboxes = Bool(O, TEXT("hitboxes"));
			E.Style = Str(O, TEXT("style"));
			E.Tick = (int64)Num(O, TEXT("tick")); E.MobsTracked = (int32)Num(O, TEXT("mobsTracked")); E.Dummies = (int32)Num(O, TEXT("dummies"));
			E.ItemKey = PrevKey; E.ItemPx = PrevPx;
			if (O->HasField(TEXT("itemPx"))) { E.ItemKey = Str(O, TEXT("itemKey")); E.ItemPx = Str(O, TEXT("itemPx")); }
			const TSharedPtr<FJsonObject>* LO = nullptr;
			if (O->TryGetObjectField(TEXT("lock"), LO) && LO->IsValid())
			{
				const TSharedPtr<FJsonObject>& L = *LO; FCrbECState::FLock& K = E.Lock;
				K.bValid = true; K.Id = (int32)Num(L, TEXT("id")); K.X = Num(L, TEXT("x")); K.Y = Num(L, TEXT("y")); K.Z = Num(L, TEXT("z"));
				K.H = (float)Num(L, TEXT("h")); K.W = (float)Num(L, TEXT("w")); K.Hp = (float)Num(L, TEXT("hp")); K.Max = (float)Num(L, TEXT("max"));
				K.Poise = (float)Num(L, TEXT("poise")); K.PoiseMax = (float)Num(L, TEXT("poiseMax")); K.bStagger = Bool(L, TEXT("stagger")); K.Name = Str(L, TEXT("name"));
			}
		}
	}
	const TSharedPtr<FJsonObject>* C6 = nullptr;
	if (J->TryGetObjectField(TEXT("c64"), C6) && C6->IsValid())
	{
		const TSharedPtr<FJsonObject>& O = *C6; FCrbC64State& C = S.C64;
		C.bOn = Bool(O, TEXT("on"));
		if (C.bOn)
		{
			C.Health = (int32)Num(O, TEXT("health")); C.Armor = (int32)Num(O, TEXT("armor")); C.ArmorType = (int32)Num(O, TEXT("armorType"));
			C.Weapon = Str(O, TEXT("weapon")); C.Pending = Str(O, TEXT("pending")); C.AmmoType = Str(O, TEXT("ammoType"));
			C.Message = Str(O, TEXT("message")); C.LastFired = Str(O, TEXT("lastFired")); C.bBerserk = Bool(O, TEXT("berserk"));
			C.Kills = (int32)Num(O, TEXT("kills")); C.BfgCharge = (int32)Num(O, TEXT("bfgCharge")); C.Refire = (int32)Num(O, TEXT("refire")); C.PickupsNear = (int32)Num(O, TEXT("pickupsNear"));
			C.FireSeq = (int64)Num(O, TEXT("fireSeq")); C.MessageSeq = (int64)Num(O, TEXT("messageSeq")); C.HurtSeq = (int64)Num(O, TEXT("hurtSeq")); C.BonusSeq = (int64)Num(O, TEXT("bonusSeq"));
			C.Tick = (int64)Num(O, TEXT("tick")); C.Hits = (int64)Num(O, TEXT("hits")); C.Shots = (int64)Num(O, TEXT("shots"));
			const TArray<TSharedPtr<FJsonValue>>* OA = nullptr;
			if (O->TryGetArrayField(TEXT("owned"), OA)) for (const TSharedPtr<FJsonValue>& V : *OA) C.Owned.Add(V->AsString());
			const TSharedPtr<FJsonObject>* AO = nullptr; const TSharedPtr<FJsonObject>* MO = nullptr;
			if (O->TryGetObjectField(TEXT("ammo"), AO)) for (const auto& KV : (*AO)->Values) C.Ammo.Add(KV.Key, (int32)KV.Value->AsNumber());
			if (O->TryGetObjectField(TEXT("max"), MO)) for (const auto& KV : (*MO)->Values) C.Max.Add(KV.Key, (int32)KV.Value->AsNumber());
		}
	}
	const TSharedPtr<FJsonObject>* MapO = nullptr;
	if (J->TryGetObjectField(TEXT("maps"), MapO) && MapO->IsValid())
	{
		S.MapCurrent = Str(*MapO, TEXT("current")); S.MapLoading = Str(*MapO, TEXT("loading")); S.MapStatus = Str(*MapO, TEXT("status"));
		S.MapWorld = Str(*MapO, TEXT("world")); S.MapProgress = (int32)Num(*MapO, TEXT("progress"));
	}
	const TSharedPtr<FJsonObject>* ZmO = nullptr;
	if (J->TryGetObjectField(TEXT("zm"), ZmO) && ZmO->IsValid())
	{
		const TSharedPtr<FJsonObject>& Z = *ZmO;
		FCrbZmState& M = S.Zm;
		M.Phase = Str(Z, TEXT("phase")); if (M.Phase.IsEmpty()) M.Phase = TEXT("OFF");
		M.Round = (int32)Num(Z, TEXT("round")); M.Points = (int32)Num(Z, TEXT("points")); M.Kills = (int32)Num(Z, TEXT("kills"));
		M.Headshots = (int32)Num(Z, TEXT("headshots")); M.Left = (int32)Num(Z, TEXT("left"));
		const TArray<TSharedPtr<FJsonValue>>* PA = nullptr;
		if (Z->TryGetArrayField(TEXT("perks"), PA)) for (int32 I = 0; I < PA->Num() && I < 8; ++I) M.Perks.Add((*PA)[I]->AsString());
		M.Prompt = Str(Z, TEXT("prompt")); M.Cost = (int32)Num(Z, TEXT("cost")); M.bAfford = Bool(Z, TEXT("afford"));
		M.InstaKill = (int32)Num(Z, TEXT("instaKill")); M.DoublePoints = (int32)Num(Z, TEXT("doublePoints"));
		M.PowerUp = Str(Z, TEXT("powerUp")); M.PowerUpTick = (int64)Num(Z, TEXT("powerUpTick")); M.Crate = Str(Z, TEXT("crate"));
		M.RoundTick = (int32)Num(Z, TEXT("roundTick")); M.Message = Str(Z, TEXT("message")); M.MessageTick = (int64)Num(Z, TEXT("messageTick"));
		M.Tick = (int64)Num(Z, TEXT("tick"));
		M.ChalkSheet = Str(Z, TEXT("chalkSheet")); M.ChalkGen = (int32)Num(Z, TEXT("chalkGen")); M.Box = Str(Z, TEXT("box"));
		const TArray<TSharedPtr<FJsonValue>>* WA = nullptr;
		if (Z->TryGetArrayField(TEXT("wallbuys"), WA))
			for (int32 I = 0; I < WA->Num() && I < 32; ++I)
			{
				const TSharedPtr<FJsonObject> O = (*WA)[I]->AsObject();
				if (!O.IsValid()) continue;
				FCrbZmState::FWallBuy W; W.X = (int32)Num(O, TEXT("x")); W.Y = (int32)Num(O, TEXT("y")); W.Z = (int32)Num(O, TEXT("z")); W.Cell = (int32)Num(O, TEXT("cell")); W.Face = Str(O, TEXT("face"));
				M.WallBuys.Add(W);
			}
		M.BulletsFired = (int32)Num(Z, TEXT("bulletsFired")); M.HeldMag = (int32)Num(Z, TEXT("heldMag"), -1); M.ZombieHits = (int32)Num(Z, TEXT("zombieHits"));
		M.Capacity = (int32)Num(Z, TEXT("capacity")); M.Reserve = (int32)Num(Z, TEXT("reserve"));
		const TArray<TSharedPtr<FJsonValue>>* ZA = nullptr;
		if (Z->TryGetArrayField(TEXT("zombies"), ZA))
			for (int32 I = 0; I < ZA->Num() && I < 32; ++I)
			{
				const TSharedPtr<FJsonObject> O = (*ZA)[I]->AsObject();
				if (!O.IsValid()) continue;
				FCrbZmZombie E; E.Id = (int32)Num(O, TEXT("id")); E.X = Num(O, TEXT("x")); E.Y = Num(O, TEXT("y")); E.Z = Num(O, TEXT("z"));
				E.Yaw = Num(O, TEXT("yaw")); E.Health = Num(O, TEXT("health")); E.MaxHealth = FMath::Max(1.0, Num(O, TEXT("maxHealth"), 1));
				E.DeathTime = (int32)Num(O, TEXT("deathTime")); E.HurtTime = (int32)Num(O, TEXT("hurtTime")); E.Anim = (int32)Num(O, TEXT("anim")); E.bInside = Bool(O, TEXT("inside"));
				M.Zombies.Add(E);
			}
	}
	const TSharedPtr<FJsonObject>* AxeO = nullptr;
	if (J->TryGetObjectField(TEXT("axe"), AxeO))
	{
		S.bAxeActive = Bool(*AxeO, TEXT("active")); S.AxePhase = (int32)Num(*AxeO, TEXT("phase"));
		S.AxeX = Num(*AxeO, TEXT("x")); S.AxeY = Num(*AxeO, TEXT("y")); S.AxeZ = Num(*AxeO, TEXT("z"));
		S.AxeSpin = Num(*AxeO, TEXT("spin")); S.AxeYaw = Num(*AxeO, TEXT("yaw")); S.AxeTravelled = Num(*AxeO, TEXT("travelled")); S.AxeHits = (int32)Num(*AxeO, TEXT("hits"));
	}
	S.Health = Num(J, TEXT("health")); S.MaxHealth = FMath::Max(1.0, Num(J, TEXT("maxHealth"), 20)); S.Absorb = Num(J, TEXT("absorb"));
	S.Armor = (int32)Num(J, TEXT("armor")); S.Food = (int32)Num(J, TEXT("food")); S.Saturation = Num(J, TEXT("sat"));
	S.XpLevel = (int32)Num(J, TEXT("xpLevel")); S.XpProgress = FMath::Clamp((float)Num(J, TEXT("xpProgress")), 0.f, 1.f);
	S.Selected = FMath::Clamp((int32)Num(J, TEXT("selected")), 0, 8);
	S.Screen = Str(J, TEXT("screen")); S.Fixture = Str(J, TEXT("fixture")); S.GameMode = Str(J, TEXT("gamemode")); S.Dimension = Str(J, TEXT("dim"));
	S.Tick = (int64)Num(J, TEXT("tick")); S.DayTime = (int64)Num(J, TEXT("dayTime")); S.SkyDarken = Num(J, TEXT("skyDarken")); S.SunAngle = Num(J, TEXT("sunAngle")); S.Rain = Num(J, TEXT("rain"));
	auto Rgb = [&](const TCHAR* K, FLinearColor& Out) -> bool
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!J->TryGetArrayField(K, A) || A->Num() != 3) return false;
		Out = FLinearColor(FColor((uint8)FMath::Clamp((*A)[0]->AsNumber() * 255.0, 0.0, 255.0), (uint8)FMath::Clamp((*A)[1]->AsNumber() * 255.0, 0.0, 255.0), (uint8)FMath::Clamp((*A)[2]->AsNumber() * 255.0, 0.0, 255.0)));
		return true;
	};
	S.bHasSky = Rgb(TEXT("sky"), S.SkyColor) && Rgb(TEXT("fog"), S.FogColor);
	S.EyeSky = (int32)Num(J, TEXT("eyeSky"), 15); S.EyeBlock = (int32)Num(J, TEXT("eyeBlock"));
	const TArray<TSharedPtr<FJsonValue>>* Slots = nullptr;
	if (J->TryGetArrayField(TEXT("slots"), Slots))
		for (int32 I = 0; I < Slots->Num() && I < 64; ++I)
		{
			const TSharedPtr<FJsonObject> O = (*Slots)[I]->AsObject();
			FCrbSlot Slot; Slot.Id = Str(O, TEXT("id")); Slot.Name = Str(O, TEXT("name")).Left(64);
			Slot.Count = (int32)Num(O, TEXT("count")); Slot.Damage = (int32)Num(O, TEXT("damage")); Slot.MaxDamage = (int32)Num(O, TEXT("maxDamage"));
			S.Slots.Add(Slot);
		}
	const TSharedPtr<FJsonObject>* Hit = nullptr;
	if (J->TryGetObjectField(TEXT("hit"), Hit))
	{
		const TArray<TSharedPtr<FJsonValue>>* E = nullptr;
		if ((*Hit)->TryGetArrayField(TEXT("edges"), E) && E->Num() % 6 == 0 && E->Num() <= 6 * 96)
			for (int32 I = 0; I + 5 < E->Num(); I += 6)
			{
				const FVector A((*E)[I]->AsNumber(), (*E)[I + 1]->AsNumber(), (*E)[I + 2]->AsNumber()), B((*E)[I + 3]->AsNumber(), (*E)[I + 4]->AsNumber(), (*E)[I + 5]->AsNumber());
				if (A.GetAbsMax() <= 3 && B.GetAbsMax() <= 3) { S.HitEdges.Add(A); S.HitEdges.Add(B); }
			}
	}
	if (J->TryGetObjectField(TEXT("hit"), Hit)) { S.bHasHit = true; S.Hit = FIntVector((int32)Num(*Hit, TEXT("x")), (int32)Num(*Hit, TEXT("y")), (int32)Num(*Hit, TEXT("z"))); S.HitFace = Num(*Hit, TEXT("face")); S.HitState = Num(*Hit, TEXT("state")); }
	const TSharedPtr<FJsonObject>* G = nullptr;
	if (J->TryGetObjectField(TEXT("gravity"), G))
	{
		S.bGravityHolding = Bool(*G, TEXT("holding")); S.GravityState = Num(*G, TEXT("state"));
		S.GX = Num(*G, TEXT("x")); S.GY = Num(*G, TEXT("y")); S.GZ = Num(*G, TEXT("z")); S.GravityDistance = Num(*G, TEXT("distance"));
	}
	S.PoseSeq = Num(J, TEXT("poseSeq")); S.PoseWalkPos = Num(J, TEXT("poseWalkPos")); S.InputApplied = Num(J, TEXT("inputApplied")); S.PoseErrors = Num(J, TEXT("poseErrors"));
	S.TextureFailures = Num(J, TEXT("textureFailures")); S.TexturesSent = Num(J, TEXT("texturesSent")); S.TextureLastError = Str(J, TEXT("textureLastError")).Left(300);
	S.InputThread = Str(J, TEXT("inputThread")); S.ServerOpsThread = Str(J, TEXT("serverOpsThread"));
	S.IconSheet = Str(J, TEXT("iconSheet")); S.IconGen = Num(J, TEXT("iconGen")); S.IconCell = FMath::Clamp((int32)Num(J, TEXT("iconCell"), 32), 8, 64); S.IconCols = FMath::Clamp((int32)Num(J, TEXT("iconCols"), 16), 1, 64);
	const TSharedPtr<FJsonObject>* TitleO = nullptr;
	if (J->TryGetObjectField(TEXT("title"), TitleO))
	{
		S.TitleAlpha = FMath::Clamp((float)Num(*TitleO, TEXT("alpha")), 0.f, 1.f); S.TitleGen = (int32)Num(*TitleO, TEXT("gen"));
		S.TitleText = Str(*TitleO, TEXT("text")).Left(200); S.SubtitleText = Str(*TitleO, TEXT("sub")).Left(200);
	}
	const TArray<TSharedPtr<FJsonValue>>* ChatA = nullptr;
	if (J->TryGetArrayField(TEXT("chat"), ChatA))
		for (int32 I = 0; I < ChatA->Num() && I < 10; ++I)
		{
			const TSharedPtr<FJsonObject> O = (*ChatA)[I]->AsObject();
			if (O.IsValid()) S.Chat.Add({ Str(O, TEXT("text")).Left(200), (int32)Num(O, TEXT("age")) });
		}
	S.DeathMessage = Str(J, TEXT("deathMessage")).Left(200); S.Score = (int32)Num(J, TEXT("score")); S.EntityErrors = (int64)Num(J, TEXT("entityErrors"));
	S.ReceivedAt = FPlatformTime::Seconds();
	if (!bLookInitialised) { LookYaw = S.Yaw; LookPitch = -S.Pitch; bLookInitialised = true; }
	PrevState = State.bValid ? State : S;
	State = MoveTemp(S);
	ShatterFx.OnEvents(State.Shatter);
	// A STATE captured before Java applied a fixture toggle can arrive after its RESULT: trust the result briefly.
	if (FPlatformTime::Seconds() - LastFixtureResultAt > 0.6) ActiveFixture = State.Fixture;
	LastStateTime = State.ReceivedAt;
	UpdateAnchor();
}

void ACrbHost::OnResult(const TSharedPtr<FJsonObject>& J)
{
	if (!J.IsValid()) return;
	FCrbResult R;
	R.Id = Str(J, TEXT("id")); R.Op = Str(J, TEXT("op")); R.Message = Str(J, TEXT("message")).Left(400); R.Thread = Str(J, TEXT("thread"));
	R.bOk = Bool(J, TEXT("ok")); R.At = FPlatformTime::Seconds(); R.Json = J;
	if (J->HasField(TEXT("active"))) { R.Active = Str(J, TEXT("active")); ActiveFixture = R.Active; LastFixtureResultAt = R.At; }
	if (R.Op == TEXT("fixture.list"))
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (J->TryGetArrayField(TEXT("fixtures"), Arr))
		{
			Fixtures.Reset();
			for (auto& V : *Arr) { const TSharedPtr<FJsonObject> O = V->AsObject(); Fixtures.Add(TPair<FString, FString>(Str(O, TEXT("id")), Str(O, TEXT("label")))); }
			if (bDebugMenuOpen) CrbMenus::Refresh(this);
		}
	}
	if (!R.Op.IsEmpty() && R.Op != TEXT("fixture.list")) StatusLine = (R.bOk ? TEXT("") : TEXT("Refused: ")) + R.Message;
	OnGameUIResult(R);
	if (Results.Num() > 256) Results.Reset();
	Results.Add(R.Id, R);
}

void ACrbHost::OnEvent(const TSharedPtr<FJsonObject>& J)
{
	if (!J.IsValid()) return;
	FCrbEvent E; E.Kind = Str(J, TEXT("kind")); E.Phase = Str(J, TEXT("phase")); E.Id = Str(J, TEXT("id"));
	E.X = Num(J, TEXT("x")); E.Y = Num(J, TEXT("y")); E.Z = Num(J, TEXT("z")); E.Removed = Num(J, TEXT("removed")); E.At = FPlatformTime::Seconds();
	if (Events.Num() > 64) Events.RemoveAt(0);
	Events.Add(E);
}

void ACrbHost::OnLightmap(const TSharedPtr<FJsonObject>& J, const TArray<uint8>& Bin)
{
	if (!J.IsValid() || Num(J, TEXT("w")) != 16 || Num(J, TEXT("h")) != 16 || Bin.Num() != 16 * 16 * 4) { ++FramesRejected; return; }
	// Minecraft multiplies atlas * lightmap in display (gamma) space. Decoding both as sRGB and multiplying in
	// linear space is the same product (pow distributes over multiplication), so the re-encoded output matches vanilla.
	Lightmap = Textures.UploadRaw(TEXT("crb:lightmap"), 16, 16, Bin, true, ++LightmapGen);
	LightmapPixels = Bin;
	if (Lightmap) World.SetAtlas(nullptr, Lightmap);
}

void ACrbHost::OnParticles(const TSharedPtr<FJsonObject>& J, const TArray<uint8>& Bin)
{
	if (!J.IsValid()) return;
	const int32 Count = Num(J, TEXT("count")); const int32 Stride = Num(J, TEXT("stride"));
	if (Stride != 48 || Count < 0 || Count > 1024 || Bin.Num() != Count * Stride) { ++FramesRejected; return; }
	const double CX = Num(J, TEXT("cx")), CY = Num(J, TEXT("cy")), CZ = Num(J, TEXT("cz"));
	Crb::FReader R(Bin);
	ParticlePrev = MoveTemp(ParticleNow);
	ParticleNow.Reset(Count);
	for (int32 I = 0; I < Count; ++I)
	{
		FParticle P; P.Id = R.U32();
		const float X = R.F32(), Y = R.F32(), Z = R.F32(); P.Size = R.F32();
		P.UV0.X = R.F32(); P.UV0.Y = R.F32(); P.UV1.X = R.F32(); P.UV1.Y = R.F32();
		const uint32 C = R.U32();
		P.Sheet = R.U8(); P.Block = R.U8(); P.Sky = R.U8(); R.U8(); R.F32();
		if (!R.Finite(X) || !R.Finite(Y) || !R.Finite(Z) || P.Size <= 0 || P.Size > 8 || P.Sheet > 1) continue;
		P.Pos = Coords.ToUE(CX + X, CY + Y, CZ + Z);
		P.Color = FLinearColor(((C >> 16) & 255) / 255.f, ((C >> 8) & 255) / 255.f, (C & 255) / 255.f, ((C >> 24) & 255) / 255.f);
		ParticleNow.Add(P);
	}
	ParticleTime = FPlatformTime::Seconds();
	ParticleCount = ParticleNow.Num();
}

FLinearColor ACrbHost::LightAt(int32 Block, int32 Sky) const
{
	if (!bLightingEnabled || LightmapPixels.Num() != 16 * 16 * 4) return FLinearColor::White;
	const int32 I = (FMath::Clamp(Sky, 0, 15) * 16 + FMath::Clamp(Block, 0, 15)) * 4;
	return FLinearColor(FColor(LightmapPixels[I + 2], LightmapPixels[I + 1], LightmapPixels[I], 255)); // sRGB -> linear
}

void ACrbHost::ToggleInventory()
{
	if (bInventoryOpen) { CloseMenus(); return; }
	if (IsSpectator() || bDeathOpen) return; // vanilla: no inventory in spectator / on the death screen
	CloseMenus();
	bInventoryOpen = true;
	if (IsCreative()) { if (CreativeTabs.Num() == 0) SendCommand(TEXT("creative.tabs")); else CreativeRequestPage(true); }
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		FInputModeGameAndUI Mode; Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock); Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode); PC->bShowMouseCursor = true;
	}
	SendInput(true);
}

void ACrbHost::InventoryClick(int32 Button)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	ACrbHUD* H = PC ? Cast<ACrbHUD>(PC->GetHUD()) : nullptr;
	float MX = 0, MY = 0;
	if (!H || !PC->GetMousePosition(MX, MY)) return;
	InventoryClickAt(FVector2D(MX, MY), Button);
}

void ACrbHost::InventoryClickAt(const FVector2D& Screen, int32 Button)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	ACrbHUD* H = PC ? Cast<ACrbHUD>(PC->GetHUD()) : nullptr;
	if (!H) return;
	if (IsCreative()) { CreativeClickAt(Screen, Button); return; }
	const int32 Slot = H->InventorySlotAt(Screen);
	if (Slot < 0) return;
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
	A->SetNumberField(TEXT("slot"), Slot); A->SetNumberField(TEXT("button"), Button); A->SetBoolField(TEXT("shift"), bSneakHeld);
	SendCommand(TEXT("inv.click"), A);
}

void ACrbHost::UpdateAnchor()
{
	// Section-aligned double-precision anchor keeps UE float coordinates small near the player.
	const double DX = State.X - Coords.AX, DZ = State.Z - Coords.AZ, DY = State.Y - Coords.AY;
	if (Coords.AX == 0 && Coords.AY == 0 && Coords.AZ == 0 ? true : (FMath::Abs(DX) > 512 || FMath::Abs(DZ) > 512 || FMath::Abs(DY) > 512))
	{
		Coords.AX = FMath::FloorToDouble(State.X / 16.0) * 16.0;
		Coords.AY = FMath::FloorToDouble(State.Y / 16.0) * 16.0;
		Coords.AZ = FMath::FloorToDouble(State.Z / 16.0) * 16.0;
	}
}

void ACrbHost::PresentedFeet(double& X, double& Y, double& Z) const
{
	// Render 50 ms behind Java so movement interpolates smoothly between 20 Hz snapshots.
	const double A = FMath::Clamp((FPlatformTime::Seconds() - State.ReceivedAt) / 0.05, 0.0, 1.0);
	X = FMath::Lerp(PrevState.X, State.X, A); Y = FMath::Lerp(PrevState.Y, State.Y, A); Z = FMath::Lerp(PrevState.Z, State.Z, A);
}

FVector ACrbHost::EyeLocationUE() const
{
	double X, Y, Z; PresentedFeet(X, Y, Z);
	return Coords.ToUE(X, Y + State.Eye, Z);
}

void ACrbHost::Look(float DYaw, float DPitch)
{
	if (IsMenuOpen() || IsPPDebugOpen()) return;   // the debug panel owns the mouse
	if (ECLook(DYaw)) return;                       // Elden Combat lock-on owns the camera (flick = switch target)
	LookYaw = FMath::Fmod(LookYaw + DYaw + 360.f, 360.f);
	LookPitch = (IsAvatarActive() || IsSm64Active() || IsPhysicsPortalActive() || IsEldenCombatActive()) ? FMath::Clamp(LookPitch + DPitch, -70.f, 60.f) : FMath::Clamp(LookPitch + DPitch, -89.9f, 89.9f);
}

void ACrbHost::PrimaryPressed(bool bDown)
{
	if (bInventoryOpen) { if (bDown) InventoryClick(0); return; }
	if (IsMenuOpen()) return;
	if (ECPrimary(bDown)) return;                   // Minecraft x Elden Combat: light attack
	if (Weapon == ECrbWeapon::GravityGun)
	{
		if (bDown) SendCommand(State.bGravityHolding ? TEXT("gravity.release") : TEXT("gravity.grab"));
		return;
	}
	if (bDown && !bAttackHeld) { ++AttackPresses; if (IsAvatarActive()) AvatarPrimary(); }
	bAttackHeld = bDown;
	SendInput(true);
}

void ACrbHost::SecondaryPressed(bool bDown)
{
	if (bInventoryOpen) { if (bDown) InventoryClick(1); return; }
	if (ECSecondary(bDown)) return;                 // Minecraft x Elden Combat: guard
	if (IsMenuOpen() || Weapon != ECrbWeapon::Hand) { bUseHeld = false; return; }
	if (AvatarSecondary(bDown)) return; // avatar mode, empty main hand: axe aim / throw
	if (bDown && !bUseHeld) ++UsePresses;
	bUseHeld = bDown;
	SendInput(true);
}

void ACrbHost::SelectSlot(int32 Slot)
{
	if (IsCraft64Active()) { Craft64Select(FString::FromInt(FMath::Clamp(Slot, 0, 8) + 1)); return; } // Doom weapon slots 1..9
	PendingSlot = FMath::Clamp(Slot, 0, 8);
	if (Weapon != ECrbWeapon::Hand) SelectWeapon(ECrbWeapon::Hand); // number keys return to the normal hand
	SendInput(true);
}

void ACrbHost::ScrollWheel(float Delta)
{
	if (bInventoryOpen && IsCreative() && !FMath::IsNearlyZero(Delta)) { CreativeScroll(Delta); return; }
	if (IsMenuOpen() || FMath::IsNearlyZero(Delta)) return;
	if (ECScroll(Delta)) return;                    // Elden Combat: switch lock-on target
	if (Weapon == ECrbWeapon::GravityGun && State.bGravityHolding)
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetNumberField(TEXT("delta"), Delta > 0 ? 0.5 : -0.5);
		SendCommand(TEXT("gravity.distance"), A);
		return;
	}
	if (IsCraft64Active()) { Craft64Select(Delta > 0 ? TEXT("prev") : TEXT("next")); return; }
	if (Weapon == ECrbWeapon::Hand)
	{
		const int32 Base = PendingSlot >= 0 ? PendingSlot : State.Selected;
		PendingSlot = (Base + (Delta > 0 ? 8 : 1)) % 9;
		SendInput(true);
	}
}

void ACrbHost::CycleView() { if (IsAvatarActive() || IsSm64Active() || IsCraft64Active() || IsPhysicsPortalActive() || IsEldenCombatActive()) return; /* avatar mode owns the orbit camera */ ViewMode = (ViewMode + 1) % 3; }

void ACrbHost::SelectWeapon(ECrbWeapon W)
{
	if (W == Weapon) return;
	if (Weapon == ECrbWeapon::GravityGun && State.bGravityHolding) SendCommand(TEXT("gravity.release")); // never strand a held block
	Weapon = W;
	WeaponEquip = 0.f;
	bAttackHeld = false; bUseHeld = false;
	StatusLine = W == ECrbWeapon::Hand ? TEXT("Minecraft hand") : TEXT("Gravity Gun: left click grabs/places, wheel sets distance");
	SendInput(true);
}

void ACrbHost::ToggleFixture(const FString& Id)
{
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("id"), Id);
	SendCommand(TEXT("fixture.toggle"), A);
}

void ACrbHost::ToggleModMenu()
{
	if (bModMenuOpen) { CloseMenus(); return; }
	CloseMenus();
	bModMenuOpen = true;
	CrbMenus::OpenModMenu(this);
	SendInput(true);
}

void ACrbHost::ToggleDebugMenu()
{
	if (bDebugMenuOpen) { CloseMenus(); return; }
	CloseMenus();
	if (Fixtures.Num() == 0) SendCommand(TEXT("fixture.list"));
	bDebugMenuOpen = true;
	CrbMenus::OpenDebugMenu(this);
	SendInput(true);
}

void ACrbHost::CloseMenus()
{
	const bool bWasOpen = IsMenuOpen();
	if (bInventoryOpen) SendCommand(TEXT("inv.close"));
	bModMenuOpen = bDebugMenuOpen = bInventoryOpen = bPauseOpen = bDeathOpen = false; bECMenuOpen = false;
	CrbMenus::Close(this);
	if (bWasOpen) { MoveForward = MoveStrafe = 0; SendInput(true); }
}

void ACrbHost::ToggleLighting()
{
	bLightingEnabled = !bLightingEnabled;
	StatusLine = bLightingEnabled ? TEXT("Minecraft light map lighting") : TEXT("Base lighting (10000 lux neutral)");
}

void ACrbHost::SendInput(bool bForce)
{
	if (!Connection || Connection->GetState() != ECrbLinkState::Connected) return;
	const double Now = FPlatformTime::Seconds();
	if (!bForce && Now - LastInputSent < InputIntervalSeconds) return;
	const bool bMenu = IsMenuOpen();
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	float SendFwd = MoveForward, SendStrafe = MoveStrafe, SendYaw = LookYaw;
	if (IsAvatarActive()) AvatarMoveInput(SendFwd, SendStrafe, SendYaw);
	J->SetNumberField(TEXT("fwd"), bMenu ? 0 : SendFwd);
	J->SetNumberField(TEXT("strafe"), bMenu ? 0 : SendStrafe);
	J->SetBoolField(TEXT("jump"), !bMenu && bJumpHeld);
	J->SetBoolField(TEXT("sneak"), !bMenu && bSneakHeld);
	J->SetBoolField(TEXT("sprint"), !bMenu && bSprintHeld);
	J->SetBoolField(TEXT("attack"), !bMenu && bAttackHeld && Weapon == ECrbWeapon::Hand);
	J->SetBoolField(TEXT("use"), !bMenu && bUseHeld && Weapon == ECrbWeapon::Hand);
	J->SetNumberField(TEXT("attackPresses"), (double)AttackPresses);
	J->SetNumberField(TEXT("usePresses"), (double)UsePresses);
	J->SetNumberField(TEXT("yaw"), SendYaw);
	J->SetNumberField(TEXT("pitch"), -LookPitch);
	J->SetNumberField(TEXT("lease"), 350);
	if (IsCraft64Active())
	{
		// Craft 64: the attack button is the trigger (Java fires the Doom weapon); number keys / wheel pick weapons.
		TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
		C->SetStringField(TEXT("want"), C64Want); C->SetNumberField(TEXT("seq"), C64Seq);
		J->SetObjectField(TEXT("c64"), C);
	}
	if (IsEldenCombatActive())
	{
		// Minecraft x Elden Combat: button counters + held buttons; Java's combat rules consume them (attack / use stay
		// up on the vanilla side while this block is present).
		TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetNumberField(TEXT("light"), (double)ECLight); E->SetNumberField(TEXT("heavy"), (double)ECHeavy);
		E->SetNumberField(TEXT("parry"), (double)ECParry); E->SetNumberField(TEXT("dodge"), (double)ECDodge);
		E->SetNumberField(TEXT("lock"), (double)ECLock); E->SetNumberField(TEXT("switch"), (double)ECSwitch);
		E->SetBoolField(TEXT("heavyHeld"), !bMenu && bECHeavyHeld); E->SetBoolField(TEXT("block"), !bMenu && bECBlockHeld);
		J->SetObjectField(TEXT("ec"), E);
	}
	if (IsPhysicsPortalActive() && !IsAvatarActive())
	{
		// Physics & Portal mod: raw stick + camera yaw (Java drives momentum); roll key, portal gun press counters.
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetNumberField(TEXT("jumpPresses"), (double)JumpPresses);
		P->SetBoolField(TEXT("roll"), !bMenu && (bPPRollHeld || bSneakHeld));
		P->SetNumberField(TEXT("portalA"), (double)PPPortalPresses[0]); P->SetNumberField(TEXT("portalB"), (double)PPPortalPresses[1]); P->SetNumberField(TEXT("portalClear"), (double)PPPortalPresses[2]);
		J->SetObjectField(TEXT("pp"), P);
		J->SetNumberField(TEXT("yaw"), LookYaw);
	}
	if (IsSm64Active())
	{
		// SM64 Steve Movement: raw stick relative to the camera yaw; Java turns Steve and owns his facing.
		J->SetBoolField(TEXT("sm64"), true);
		J->SetNumberField(TEXT("yaw"), LookYaw);
		J->SetNumberField(TEXT("jumpPresses"), (double)JumpPresses);
		J->SetNumberField(TEXT("sneakPresses"), (double)SneakPresses);
	}
	if (PendingSlot >= 0 && !IsCraft64Active()) J->SetNumberField(TEXT("slot"), PendingSlot);
	Connection->Send(Crb::EType::Input, Crb::ToJson(J));
	LastInputSent = Now;
	if (PendingSlot >= 0 && State.Selected == PendingSlot) PendingSlot = -1;
}

void ACrbHost::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bPresentationReady) { InitPresentation(); if (!bPresentationReady) return; }
	const double FrameStart = FPlatformTime::Seconds();
	if (Connection)
	{
		const int32 Epoch = Connection->GetEpoch();
		if (Epoch != SeenEpoch && Connection->GetState() == ECrbLinkState::Connected) { SeenEpoch = Epoch; ResetForNewConnection(); }
		Crb::FFrame F; int32 FrameEpoch = 0; int32 N = 0;
		while (N < 400 && FPlatformTime::Seconds() - FrameStart < 0.006 && Connection->Pop(F, FrameEpoch))
		{
			++N;
			if (FrameEpoch != SeenEpoch) continue; // from a previous connection
			HandleFrame(F);
		}
		switch (Connection->GetState())
		{
		case ECrbLinkState::Connected: if (StatusLine.StartsWith(TEXT("Waiting")) || StatusLine.StartsWith(TEXT("Disconnected"))) StatusLine = WelcomeText; break;
		case ECrbLinkState::WaitingForEndpoint: case ECrbLinkState::Connecting: case ECrbLinkState::Handshaking:
			if (SeenEpoch > 0) StatusLine = TEXT("Disconnected - reconnecting: ") + Connection->GetLastError(); break;
		default: break;
		}
	}
	Textures.Tick();
	if (Textures.Version() != AtlasVersionSeen)
	{
		AtlasVersionSeen = Textures.Version();
		UTexture2D* Atlas = Textures.Get(TEXT("minecraft:textures/atlas/blocks.png"));
		World.SetAtlas(Atlas, Lightmap);
		if (HeldMid) { if (Atlas) HeldMid->SetTextureParameterValue(TEXT("Atlas"), Atlas); if (Lightmap) HeldMid->SetTextureParameterValue(TEXT("Lightmap"), Lightmap); HeldMid->SetScalarParameterValue(TEXT("HasAtlas"), Atlas ? 1.f : 0.f); }
		ParticleMids[0]->SetScalarParameterValue(TEXT("HasTexture"), Atlas ? 1.f : 0.f);
		if (Atlas) ParticleMids[0]->SetTextureParameterValue(TEXT("Tex"), Atlas);
		if (UTexture2D* PA = Textures.Get(TEXT("minecraft:textures/atlas/particles.png"))) { ParticleMids[1]->SetTextureParameterValue(TEXT("Tex"), PA); ParticleMids[1]->SetScalarParameterValue(TEXT("HasTexture"), 1.f); }
	}
	World.Tick(Coords);
	ShatterFx.Tick(DeltaSeconds, Coords);
	CrbMcUi::Update(this);
	UpdateLighting(DeltaSeconds);

	// Pawn/camera follow the interpolated Java eye position; the pawn has no movement of its own.
	const bool bAvatarMode = IsAvatarActive();
	const bool bPPMode = IsPhysicsPortalActive() && !bAvatarMode;
	const bool bSm64Mode = IsSm64Active() && !bAvatarMode && !bPPMode;
	const bool bSteveShown = bSm64Mode && Steve.AssetsReady() && State.bValid;
	const bool bPPShown = bPPMode && PPSteve.AssetsReady() && State.bValid;
	const bool bECMode = IsEldenCombatActive() && !bAvatarMode && !bPPMode && !bSm64Mode;
	const bool bECShown = bECMode && ECSteve.IsAcquired() && State.bValid && State.EC.bOn;
	const bool bFirst = ViewMode == 0 && !bAvatarMode && !bSm64Mode && !bPPMode && !bECMode;
	// pose groups 0 (body) and 1 (hands) hidden while a mod draws the player; entities (group 2) stay. Without the
	// Steve assets SM64 mode still works and shows the vanilla player model from the orbit camera.
	Avatar.bSuppressPlayer = bAvatarMode || bSteveShown || bPPShown || bECShown || (IsCraft64Active() && State.C64.bOn); // Craft 64 draws its own Doom-style weapon
	Avatar.Tick(Coords, Textures, bFirst, Weapon != ECrbWeapon::Hand, State.bValid, DeltaSeconds);
	ViewBob = bFirst && !IsMenuOpen() ? Avatar.ViewBob() : FTransform::Identity;
	TickAvatarMode(DeltaSeconds);
	TickSm64(DeltaSeconds);
	TickCraft64(DeltaSeconds);
	TickPhysicsPortal(DeltaSeconds);
	TickEldenCombat(DeltaSeconds);
	TickZombies(DeltaSeconds);
	if (Pawn && State.bValid)
	{
		if (bAvatarMode || bSm64Mode || bPPMode || bECMode) Pawn->PresentOrbit(AvatarCameraPivot, ViewRotation(), AvatarCameraDistance);
		else Pawn->Present(EyeLocationUE(), ViewRotation(), ViewMode, this, ViewBob);
	}
	if (Avatar.GetHands()) Avatar.GetHands()->SetRelativeTransform(ViewBob);
	UpdateWeapons(DeltaSeconds);
	UpdateOutline();
	UpdateParticles();
	TickGameUI(DeltaSeconds);
	SendInput(false);
	if (Test) Test->Tick(DeltaSeconds);
	LastFrameMs = (FPlatformTime::Seconds() - FrameStart) * 1000.0;
}

void ACrbHost::UpdateLighting(float Dt)
{
	const double T = (double)(State.DayTime % 24000) / 24000.0; // 0 sunrise, .25 noon, .5 sunset, .75 midnight
	const float Theta = (float)(T * 2.0 * PI);
	const float Elev = FMath::Sin(Theta);
	FVector LightDir;
	float SunLux, SunWeightNow;
	FLinearColor Sky;
	if (bLightingEnabled)
	{
		// Light travels from the sun (east at sunrise, overhead at noon) toward the scene.
		LightDir = FVector(0.15f, FMath::Cos(Theta), -FMath::Max(0.12f, FMath::Abs(Elev)) * FMath::Sign(Elev == 0 ? 1.f : Elev)).GetSafeNormal();
		if (Elev < 0) LightDir.Z = -FMath::Max(0.12f, -Elev); // moon light from above at night
		const float Day = FMath::Clamp(Elev * 4.f + 0.2f, 0.f, 1.f);
		SunLux = BaseSunLux * (Elev > 0 ? Day : 0.03f);
		SunWeightNow = SunWeight;
		Sun->SetLightColor(Elev > 0 ? FLinearColor(1.f, 0.96f, 0.9f) : FLinearColor(0.55f, 0.65f, 1.f));
		Sky = State.bHasSky ? State.SkyColor : FMath::Lerp(FLinearColor(0.004f, 0.005f, 0.015f), FLinearColor(0.47f, 0.65f, 1.f), Day);
	}
	else
	{
		LightDir = FVector(0.35f, 0.45f, -0.82f).GetSafeNormal(); // neutral base lighting
		SunLux = BaseSunLux; SunWeightNow = SunWeight; Sky = FLinearColor(0.47f, 0.65f, 1.f);
		Sun->SetLightColor(FLinearColor(1.f, 0.98f, 0.95f));
	}
	Sun->SetWorldRotation(LightDir.Rotation());
	Sun->SetIntensity(SunLux);
	// Manual exposure: BaseSunLux/pi maps to 1.0 (vanilla-brightness block faces), ExposureBias trims.
	Post->Settings.AutoExposureBias = FMath::Log2(PI / FMath::Max(1.f, BaseSunLux)) + ExposureBias;
	// Vanilla-style distance fog in Minecraft's fog colour (radiance scaled like everything else so exposure maps it 1:1);
	// it also hides the edge of the bounded section window the way vanilla fog hides the render-distance edge.
	const FLinearColor FogLin = bLightingEnabled && State.bHasSky ? State.FogColor : Sky;
	Fog->SetFogInscatteringColor(FogLin * (BaseSunLux / PI));
	// Vanilla terrain fog (FogRenderer, FOG_TERRAIN): linear over the last tenth (4..64 blocks) of the render distance,
	// here the radius of the streamed section window, so the window edge fades into the fog colour like vanilla.
	const float FogEndBlocks = World.GetWindowRadius() * 16.f;
	const float FogStartBlocks = FogEndBlocks - FMath::Clamp(FogEndBlocks / 10.f, 4.f, 64.f);
	Fog->SetFogDensity(0.f); // the material fog replaces the height fog
	World.SetLightingParams(VanillaWeight, SunWeightNow, bLightingEnabled);
	for (UMaterialInstanceDynamic* M : World.GetMaterials()) if (M)
	{
		M->SetScalarParameterValue(TEXT("EmissiveScale"), BaseSunLux / PI);
		M->SetScalarParameterValue(TEXT("FogStart"), FogStartBlocks * 100.f);
		M->SetScalarParameterValue(TEXT("FogEnd"), FogEndBlocks * 100.f);
		M->SetVectorParameterValue(TEXT("FogColor"), FogLin);
	}
	if (HeldMid) { HeldMid->SetScalarParameterValue(TEXT("EmissiveScale"), BaseSunLux / PI); HeldMid->SetScalarParameterValue(TEXT("VanillaWeight"), VanillaWeight); HeldMid->SetScalarParameterValue(TEXT("SunWeight"), SunWeightNow); HeldMid->SetScalarParameterValue(TEXT("UseLightmap"), bLightingEnabled ? 1.f : 0.f); }
	// Entity brightness from Java's light at the eyes (through the same light map curve, approximated).
	const FLinearColor EyeLm = LightAt(State.EyeBlock, State.EyeSky);
	const float EyeLight = (EyeLm.R + EyeLm.G + EyeLm.B) / 3.f;
	Avatar.SetLighting(BaseSunLux / PI, VanillaWeight, SunWeightNow * 0.5f, FMath::Max(0.02f, EyeLight));
	for (UMaterialInstanceDynamic* M : ParticleMids) if (M) M->SetScalarParameterValue(TEXT("EmissiveScale"), BaseSunLux / PI);
	if (ChalkMid) ChalkMid->SetScalarParameterValue(TEXT("EmissiveScale"), BaseSunLux / PI);
	if (SkyMid)
	{
		SkyMid->SetVectorParameterValue(TEXT("Color"), Sky); SkyMid->SetScalarParameterValue(TEXT("Intensity"), BaseSunLux / PI);
		SkyMid->SetVectorParameterValue(TEXT("FogColor"), FogLin);
		SkyMid->SetScalarParameterValue(TEXT("SkyFogScale"), 16.f / FMath::Max(16.f, FogEndBlocks));
	}
	if (SkySphere && Pawn) SkySphere->SetWorldLocation(Pawn->Camera->GetComponentLocation());

	// Bounded nearby torch lights from Java light-emitting blocks.
	ActiveTorchLights = 0;
	TArray<TPair<FIntVector, uint8>> Emitters;
	if (bLightingEnabled && State.bValid) World.GatherEmitters(FVector(State.X, State.Y, State.Z), TorchLights.Num(), Emitters);
	for (int32 I = 0; I < TorchLights.Num(); ++I)
	{
		UPointLightComponent* L = TorchLights[I];
		if (I < Emitters.Num())
		{
			const FIntVector B = Emitters[I].Key;
			L->SetWorldLocation(Coords.ToUE(B.X + 0.5, B.Y + 0.7, B.Z + 0.5));
			L->SetIntensity(BaseSunLux * 0.02f * Emitters[I].Value / 15.f);
			L->SetVisibility(true);
			++ActiveTorchLights;
		}
		else L->SetVisibility(false);
	}
}

void ACrbHost::UpdateOutline()
{
	// Vanilla hit outline: the edges of Java's outline shape, depth tested so hidden edges stay hidden.
	// Lines are thin camera-facing ribbons whose width keeps roughly one pixel at any distance.
	bOutlineVisible = false;
	if (!Outline || !Pawn) return;
	Outline->ClearAllMeshSections();
	if (!State.bHasHit || State.HitEdges.Num() == 0 || IsMenuOpen() || Weapon != ECrbWeapon::Hand) return;
	const FVector Cam = Pawn->Camera->GetComponentLocation();
	const float PixelAngle = FMath::Tan(FMath::DegreesToRadians(Pawn->Camera->FieldOfView * 0.5f)) * 2.f / 1280.f;
	TArray<FVector> V, N; TArray<int32> I; TArray<FVector2D> UV; TArray<FLinearColor> C;
	const double E = 0.002; // vanilla draws the outline slightly outside the block
	for (int32 K = 0; K + 1 < State.HitEdges.Num(); K += 2)
	{
		const FVector& A0 = State.HitEdges[K]; const FVector& B0 = State.HitEdges[K + 1];
		auto Grow = [E](const FVector& P) { return FVector(P.X + (P.X >= 0.5f ? E : -E), P.Y + (P.Y >= 0.5f ? E : -E), P.Z + (P.Z >= 0.5f ? E : -E)); };
		const FVector A = Grow(A0), B = Grow(B0);
		const FVector WA = Coords.ToUE(State.Hit.X + A.X, State.Hit.Y + A.Y, State.Hit.Z + A.Z);
		const FVector WB = Coords.ToUE(State.Hit.X + B.X, State.Hit.Y + B.Y, State.Hit.Z + B.Z);
		const FVector Dir = (WB - WA).GetSafeNormal();
		const FVector Mid = (WA + WB) * 0.5f;
		const float Width = FMath::Max(0.2f, FVector::Dist(Cam, Mid) * PixelAngle * 1.6f);
		const FVector Side = FVector::CrossProduct(Dir, (Mid - Cam).GetSafeNormal()).GetSafeNormal() * Width * 0.5f;
		const int32 Base = V.Num();
		V.Append({ WA - Side, WA + Side, WB + Side, WB - Side });
		for (int32 Q = 0; Q < 4; ++Q) { N.Add(FVector::UpVector); UV.Add(FVector2D::ZeroVector); C.Add(FLinearColor::Black); }
		I.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
	}
	Outline->SetWorldLocation(FVector::ZeroVector);
	Outline->CreateMeshSection_LinearColor(0, V, I, N, UV, C, TArray<FProcMeshTangent>(), false);
	Outline->SetMaterial(0, MatOutline);
	bOutlineVisible = true;
}

void ACrbHost::UpdateParticles()
{
	if (!Particles || !Pawn) return;
	const double A = FMath::Clamp((FPlatformTime::Seconds() - ParticleTime) / 0.05, 0.0, 1.0);
	TMap<uint32, const FParticle*> PrevById;
	for (const FParticle& P : ParticlePrev) PrevById.Add(P.Id, &P);
	const FVector CamRight = Pawn->Camera->GetRightVector(), CamUp = Pawn->Camera->GetUpVector();
	TArray<FVector> V[2]; TArray<int32> I[2]; TArray<FVector2D> UV[2], UV2[2]; TArray<FLinearColor> C[2]; TArray<FVector> N[2];
	for (const FParticle& P : ParticleNow)
	{
		FVector Pos = P.Pos;
		if (const FParticle* const* Prev = PrevById.Find(P.Id)) Pos = FMath::Lerp((*Prev)->Pos, P.Pos, (float)A);
		const float H = P.Size * 100.f;
		const int32 S = P.Sheet;
		const int32 B = V[S].Num();
		V[S].Append({ Pos - CamRight * H - CamUp * H, Pos - CamRight * H + CamUp * H, Pos + CamRight * H + CamUp * H, Pos + CamRight * H - CamUp * H });
		for (const FVector2D& Q : { FVector2D(P.UV1.X, P.UV1.Y), FVector2D(P.UV1.X, P.UV0.Y), FVector2D(P.UV0.X, P.UV0.Y), FVector2D(P.UV0.X, P.UV1.Y) })
		{ FVector2D Co, Fi; CrbSplitUV(Q, Co, Fi); UV[S].Add(Co); UV2[S].Add(Fi); }
		const FVector Nrm = -Pawn->Camera->GetForwardVector();
		const FLinearColor Lit = P.Color * LightAt(P.Block, P.Sky); // vanilla: particle colour x light map at its packed light
		for (int32 K = 0; K < 4; ++K) { C[S].Add(FLinearColor(Lit.R, Lit.G, Lit.B, P.Color.A)); N[S].Add(Nrm); }
		I[S].Append({ B, B + 1, B + 2, B, B + 2, B + 3 });
	}
	Particles->SetWorldLocation(FVector::ZeroVector);
	Particles->ClearAllMeshSections();
	ParticleDraw = 0;
	const TArray<FProcMeshTangent> NoT;
	for (int32 S = 0; S < 2; ++S)
	{
		if (V[S].Num() == 0) continue;
		Particles->CreateMeshSection_LinearColor(S, V[S], I[S], N[S], UV[S], TArray<FVector2D>(), UV2[S], TArray<FVector2D>(), C[S], NoT, false);
		Particles->SetMaterial(S, ParticleMids[S]);
		++ParticleDraw;
	}
}
