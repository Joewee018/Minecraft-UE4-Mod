#include "CrbAvatar.h"
#include "CrbProtocol.h"
#include "CrbCoords.h"
#include "CrbTextures.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"

void FCrbAvatar::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (Body) Collector.AddReferencedObject(Body);
	if (Hands) Collector.AddReferencedObject(Hands);
	if (Entities) Collector.AddReferencedObject(Entities);
	for (auto& KV : Mids) if (KV.Value) Collector.AddReferencedObject(KV.Value);
	for (auto*& F : Fallback) if (F) Collector.AddReferencedObject(F);
}

void FCrbAvatar::Init(USceneComponent* WorldRoot, USceneComponent* CameraRoot, UMaterialInterface* Cutout, UMaterialInterface* Translucent)
{
	BaseCutout = Cutout; BaseTranslucent = Translucent;
	AActor* Owner = WorldRoot->GetOwner();
	auto Make = [&](USceneComponent* Parent, const TCHAR* Name)
	{
		UProceduralMeshComponent* M = NewObject<UProceduralMeshComponent>(Owner, Name);
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetupAttachment(Parent);
		M->RegisterComponent();
		M->SetCastShadow(true);
		return M;
	};
	Body = Make(WorldRoot, TEXT("CrbAvatarBody"));
	Hands = Make(CameraRoot, TEXT("CrbAvatarHands"));
	// Other entities (mobs, items, armor stands such as From The Fog's Herobrine rig): visible in every view.
	Entities = Make(WorldRoot, TEXT("CrbEntities"));
	Entities->SetBoundsScale(40.f);
	Hands->SetCastShadow(false);
	// Own bounds: the camera component has none, which would frustum-cull the hands.
	Hands->SetBoundsScale(4.f);
	for (int32 L = 0; L < 2; ++L)
	{
		UMaterialInterface* Base = L == 0 ? BaseCutout : BaseTranslucent;
		Fallback[L] = Base ? UMaterialInstanceDynamic::Create(Base, Owner) : nullptr;
		if (Fallback[L]) Fallback[L]->SetScalarParameterValue(TEXT("HasTexture"), 0.f);
	}
}

void FCrbAvatar::Reset()
{
	bHasCur = bHasPrev = false; Cur = FCrbPose(); Prev = FCrbPose();
	if (Body) Body->ClearAllMeshSections();
	if (Hands) Hands->ClearAllMeshSections();
	if (Entities) Entities->ClearAllMeshSections();
	SectionCounts[0].Reset(); SectionCounts[1].Reset(); SectionCounts[2].Reset();
}

bool FCrbAvatar::OnPose(const TSharedPtr<FJsonObject>& J, const TArray<uint8>& Bin, FString& Error)
{
	++FramesReceived;
	FCrbPose P;
	const TArray<TSharedPtr<FJsonValue>>* Groups = nullptr;
	if (!J.IsValid() || !J->TryGetNumberField(TEXT("seq"), P.Seq) || !J->TryGetArrayField(TEXT("groups"), Groups) || Groups->Num() > MaxSurfaces)
	{ Error = TEXT("pose envelope"); ++Rejected; return false; }
	if (bHasCur && P.Seq <= Cur.Seq) return true; // stale/out of order: ignore, never stall
	J->TryGetNumberField(TEXT("tick"), P.Tick); J->TryGetNumberField(TEXT("epoch"), P.Epoch);
	J->TryGetNumberField(TEXT("x"), P.X); J->TryGetNumberField(TEXT("y"), P.Y); J->TryGetNumberField(TEXT("z"), P.Z);
	double D = 0;
	if (J->TryGetNumberField(TEXT("walkPos"), D)) P.WalkPos = (float)D;
	if (J->TryGetNumberField(TEXT("walkSpeed"), D)) P.WalkSpeed = (float)D;
	if (J->TryGetNumberField(TEXT("attack"), D)) P.Attack = (float)D;
	J->TryGetBoolField(TEXT("using"), P.bUsing); J->TryGetStringField(TEXT("mainhand"), P.MainHand);
	const TArray<TSharedPtr<FJsonValue>>* Ents = nullptr;
	if (J->TryGetArrayField(TEXT("entities"), Ents))
		for (int32 E = 0; E < Ents->Num() && E < 64; ++E) { const TSharedPtr<FJsonObject>* EO = nullptr; FString T; if ((*Ents)[E]->TryGetObject(EO) && (*EO)->TryGetStringField(TEXT("type"), T)) P.EntityTypes.Add(T.Left(80)); }
	if (J->TryGetNumberField(TEXT("yaw"), D)) P.Yaw = (float)D;
	if (J->TryGetNumberField(TEXT("pitch"), D)) P.Pitch = (float)D;
	if (J->TryGetNumberField(TEXT("walkDist"), D)) P.WalkDist = (float)D;
	if (J->TryGetNumberField(TEXT("walkDistO"), D)) P.WalkDistO = (float)D;
	if (J->TryGetNumberField(TEXT("bob"), D)) P.Bob = FMath::Clamp((float)D, 0.f, 1.f);
	if (J->TryGetNumberField(TEXT("oBob"), D)) P.OBob = FMath::Clamp((float)D, 0.f, 1.f);
	if (!FMath::IsFinite(P.X) || !FMath::IsFinite(P.Y) || !FMath::IsFinite(P.Z) || FMath::Abs(P.X) > 3.0e7 || FMath::Abs(P.Z) > 3.0e7 || FMath::Abs(P.Y) > 4096)
	{ Error = TEXT("pose position"); ++Rejected; return false; }
	int32 Total = 0;
	for (auto& GV : *Groups)
	{
		const TSharedPtr<FJsonObject>* GO = nullptr;
		if (!GV->TryGetObject(GO)) { Error = TEXT("pose group"); ++Rejected; return false; }
		int32 Count = 0; FCrbSurface S;
		(*GO)->TryGetNumberField(TEXT("g"), S.Group); (*GO)->TryGetNumberField(TEXT("count"), Count);
		(*GO)->TryGetStringField(TEXT("tex"), S.Texture); (*GO)->TryGetNumberField(TEXT("layer"), S.Layer);
		if (Count < 0 || Count % 4 != 0 || S.Group < 0 || S.Group > 2 || S.Layer < 0 || S.Layer > 1 || S.Texture.Len() > 200) { Error = TEXT("pose group fields"); ++Rejected; return false; }
		Total += Count;
		if (Total > MaxVertices) { Error = TEXT("pose too many vertices"); ++Rejected; return false; }
		S.V.SetNumUninitialized(Count);
		P.Surfaces.Add(MoveTemp(S));
	}
	// Vanilla entity diffuse (minecraft_mix_light): two fixed level lights, 0.6 power + 0.4 ambient.
	// Body normals are world-aligned; hand normals are in view space, where vanilla's lights are view-rotated.
	auto RotY = [](const FVector& V, float Deg) { const float R = FMath::DegreesToRadians(Deg), C = FMath::Cos(R), S = FMath::Sin(R); return FVector(C * V.X + S * V.Z, V.Y, -S * V.X + C * V.Z); };
	auto RotX = [](const FVector& V, float Deg) { const float R = FMath::DegreesToRadians(Deg), C = FMath::Cos(R), S = FMath::Sin(R); return FVector(V.X, C * V.Y - S * V.Z, S * V.Y + C * V.Z); };
	const FVector L0 = FVector(0.2f, 1.f, -0.7f).GetSafeNormal(), L1 = FVector(-0.2f, 1.f, 0.7f).GetSafeNormal();
	const FVector V0 = RotX(RotY(L0, P.Yaw + 180.f), P.Pitch), V1 = RotX(RotY(L1, P.Yaw + 180.f), P.Pitch);
	constexpr int32 Stride = 28;
	if (Bin.Num() != Total * Stride) { Error = FString::Printf(TEXT("pose payload %d != %d"), Bin.Num(), Total * Stride); ++Rejected; return false; }
	Crb::FReader R(Bin);
	uint32 Hash = 2166136261u;
	for (FCrbSurface& S : P.Surfaces)
	{
		const int32 Count = S.V.Num();
		S.UV.SetNumUninitialized(Count); S.UV2.SetNumUninitialized(Count); S.C.SetNumUninitialized(Count); S.N.SetNumUninitialized(Count);
		for (int32 K = 0; K < Count; ++K)
		{
			const float X = R.F32(), Y = R.F32(), Z = R.F32(), U = R.F32(), V = R.F32();
			const uint32 Argb = R.U32();
			const float NX = R.I8() / 127.f, NY = R.I8() / 127.f, NZ = R.I8() / 127.f; R.U8();
			const float Lim = S.Group == 2 ? 80.f : 8.f; // entities are up to 64 blocks from the player
			if (!R.Finite(X) || !R.Finite(Y) || !R.Finite(Z) || FMath::Abs(X) > Lim || FMath::Abs(Y) > Lim || FMath::Abs(Z) > Lim || !R.Finite(U) || !R.Finite(V) || FMath::Abs(U) > 4 || FMath::Abs(V) > 4)
			{ Error = TEXT("pose vertex out of range"); ++Rejected; return false; }
			const FVector Mc(X, Y, Z);
			S.V[K] = S.Group != 1 ? FCrbCoords::DirToUE(Mc) : FVector(-Z * 100.f, X * 100.f, Y * 100.f);
			CrbSplitUV(FVector2D(U, V), S.UV[K], S.UV2[K]);
			S.C[K] = FLinearColor(((Argb >> 16) & 255) / 255.f, ((Argb >> 8) & 255) / 255.f, (Argb & 255) / 255.f, ((Argb >> 24) & 255) / 255.f);
			const FVector Nm(NX, NY, NZ);
			const FVector& A0 = S.Group != 1 ? L0 : V0; const FVector& A1 = S.Group != 1 ? L1 : V1;
			const float Diffuse = Nm.IsNearlyZero() ? 1.f : FMath::Min(1.f, (FMath::Max(0.f, FVector::DotProduct(A0, Nm)) + FMath::Max(0.f, FVector::DotProduct(A1, Nm))) * 0.6f + 0.4f);
			S.C[K].R *= Diffuse; S.C[K].G *= Diffuse; S.C[K].B *= Diffuse;
			S.N[K] = S.Group != 1 ? FCrbCoords::NormalToUE(Nm) : FVector(-NZ, NX, NY);
			uint32 Bits; FMemory::Memcpy(&Bits, &X, 4); Hash = (Hash ^ Bits) * 16777619u; FMemory::Memcpy(&Bits, &Y, 4); Hash = (Hash ^ Bits) * 16777619u;
		}
		S.I.Reserve(Count / 4 * 6);
		for (int32 Q = 0; Q < Count; Q += 4) S.I.Append({ Q, Q + 1, Q + 2, Q, Q + 2, Q + 3 });
	}
	if (!R.bOk) { Error = TEXT("pose truncated"); ++Rejected; return false; }
	P.Hash = Hash; P.ReceivedAt = FPlatformTime::Seconds();
	if (bHasCur) { Prev = MoveTemp(Cur); bHasPrev = true; }
	Cur = MoveTemp(P); bHasCur = true;
	LastSeq = Cur.Seq; LastWalkPos = Cur.WalkPos; LastWalkSpeed = Cur.WalkSpeed; LastMainHand = Cur.MainHand; LastPoseTime = Cur.ReceivedAt;
	return true;
}

UMaterialInstanceDynamic* FCrbAvatar::MaterialFor(const FString& Tex, int32 Layer, FCrbTextures& Textures, bool& bFallback)
{
	// Vanilla's entity_translucent / item translucent sheets WRITE DEPTH. UE translucency does not, so the far faces of
	// the skin boxes and of extruded item sprites were drawn over the near ones (third-person front view showed the
	// player's back; held items looked mis-textured). Those textures are binary-alpha, so a depth-writing masked
	// material reproduces vanilla exactly; Layer 1 therefore renders with the cutout material.
	Layer = 0;
	UTexture2D* T = Tex.IsEmpty() ? nullptr : Textures.Get(Tex);
	bFallback = T == nullptr;
	if (!T) return Fallback[Layer];
	const FString Key = Tex + (Layer ? TEXT("#t") : TEXT("#c"));
	UMaterialInstanceDynamic*& M = Mids.FindOrAdd(Key);
	UMaterialInterface* Base = Layer ? BaseTranslucent : BaseCutout;
	if (!M && Base && Mids.Num() < 256)
	{
		M = UMaterialInstanceDynamic::Create(Base, Body->GetOwner());
		M->SetScalarParameterValue(TEXT("HasTexture"), 1.f);
	}
	if (!M) { bFallback = true; return Fallback[Layer]; }
	UTexture2D*& Bound = BoundTextures.FindOrAdd(Key);
	if (Bound != T) { M->SetTextureParameterValue(TEXT("Tex"), T); Bound = T; }
	return M;
}

void FCrbAvatar::Present(UProceduralMeshComponent* Mesh, int32 Group, const FCrbPose& A, const FCrbPose* B, float Alpha, FCrbTextures& Textures, uint32& OutHash, int32& OutVerts, TArray<FString>& OutTex)
{
	TArray<const FCrbSurface*> Surf, SurfB;
	for (const FCrbSurface& S : A.Surfaces) if (S.Group == Group) Surf.Add(&S);
	bool bLerp = B != nullptr && Alpha < 1.f;
	if (bLerp)
	{
		for (const FCrbSurface& S : B->Surfaces) if (S.Group == Group) SurfB.Add(&S);
		if (SurfB.Num() != Surf.Num()) bLerp = false;
		else for (int32 I = 0; I < Surf.Num(); ++I) if (SurfB[I]->V.Num() != Surf[I]->V.Num()) { bLerp = false; break; }
	}
	TArray<int32>& Counts = SectionCounts[Group];
	bool bRecreate = Counts.Num() != Surf.Num();
	if (!bRecreate) for (int32 I = 0; I < Surf.Num(); ++I) if (Counts[I] != Surf[I]->V.Num()) { bRecreate = true; break; }
	if (bRecreate) { Mesh->ClearAllMeshSections(); Counts.Reset(); }
	uint32 Hash = 2166136261u; int32 Verts = 0; OutTex.Reset();
	const TArray<FProcMeshTangent> NoTangents;
	for (int32 I = 0; I < Surf.Num(); ++I)
	{
		const FCrbSurface& S = *Surf[I];
		TArray<FVector> V;
		if (bLerp) { V.SetNumUninitialized(S.V.Num()); for (int32 K = 0; K < S.V.Num(); ++K) V[K] = FMath::Lerp(SurfB[I]->V[K], S.V[K], Alpha); }
		else V = S.V;
		for (const FVector& P : V) { uint32 Bits; FMemory::Memcpy(&Bits, &P.X, 4); Hash = (Hash ^ Bits) * 16777619u; FMemory::Memcpy(&Bits, &P.Z, 4); Hash = (Hash ^ Bits) * 16777619u; }
		if (bRecreate) { Mesh->CreateMeshSection_LinearColor(I, V, S.I, S.N, S.UV, TArray<FVector2D>(), S.UV2, TArray<FVector2D>(), S.C, NoTangents, false); Counts.Add(S.V.Num()); }
		else Mesh->UpdateMeshSection_LinearColor(I, V, S.N, S.UV, TArray<FVector2D>(), S.UV2, TArray<FVector2D>(), S.C, NoTangents);
		bool bFallback = false;
		Mesh->SetMaterial(I, MaterialFor(S.Texture, S.Layer, Textures, bFallback));
		if (bFallback) ++FallbackSurfaces; else ++TexturedSurfaces;
		Verts += V.Num();
		OutTex.Add(S.Texture + (bFallback ? TEXT(" (fallback)") : TEXT("")));
	}
	OutHash = Hash; OutVerts = Verts;
}

void FCrbAvatar::Tick(const FCrbCoords& Coords, FCrbTextures& Textures, bool bFirstPerson, bool bHideHands, bool bVisible, float DeltaSeconds)
{
	if (!bHasCur || !Body) { if (Body) Body->SetVisibility(false); if (Hands) Hands->SetVisibility(false); if (Entities) Entities->SetVisibility(false); return; }
	// Interpolate between the two latest Java frames over one tick interval (50 ms).
	const float Alpha = bHasPrev ? FMath::Clamp(float((FPlatformTime::Seconds() - Cur.ReceivedAt) / 0.05), 0.f, 1.f) : 1.f;
	const FCrbPose* B = bHasPrev ? &Prev : nullptr;
	LastAlpha = Alpha;
	FallbackSurfaces = TexturedSurfaces = 0;
	const bool bShowBody = bVisible && !bFirstPerson && !bSuppressPlayer;
	const bool bShowHands = bVisible && bFirstPerson && !bHideHands && !bSuppressPlayer;
	if (bShowBody)
	{
		const double X = B ? FMath::Lerp(B->X, Cur.X, (double)Alpha) : Cur.X;
		const double Y = B ? FMath::Lerp(B->Y, Cur.Y, (double)Alpha) : Cur.Y;
		const double Z = B ? FMath::Lerp(B->Z, Cur.Z, (double)Alpha) : Cur.Z;
		Body->SetWorldLocationAndRotation(Coords.ToUE(X, Y, Z), FRotator::ZeroRotator);
		Present(Body, 0, Cur, B, Alpha, Textures, LastBodyHash, BodyVertexCount, BodyTextures);
	}
	if (bShowHands) Present(Hands, 1, Cur, B, Alpha, Textures, LastHandsHash, HandVertexCount, HandTextures);
	if (bVisible && Entities)
	{
		// Entity geometry is relative to the same Java player position as the body (no interpolation across
		// frames whose entity sets differ; Present falls back to the newest frame then).
		const double X = B ? FMath::Lerp(B->X, Cur.X, (double)Alpha) : Cur.X;
		const double Y = B ? FMath::Lerp(B->Y, Cur.Y, (double)Alpha) : Cur.Y;
		const double Z = B ? FMath::Lerp(B->Z, Cur.Z, (double)Alpha) : Cur.Z;
		Entities->SetWorldLocationAndRotation(Coords.ToUE(X, Y, Z), FRotator::ZeroRotator);
		Present(Entities, 2, Cur, B, Alpha, Textures, LastEntityHash, EntityVertexCount, EntityTextures);
	}
	if (Entities) Entities->SetVisibility(bVisible && EntityVertexCount > 0);
	Body->SetVisibility(bShowBody);
	Hands->SetVisibility(bShowHands);
	++FramesApplied;
}

void FCrbAvatar::SetLighting(float EmissiveScale, float VanillaWeight, float SunWeight, float Brightness)
{
	auto Apply = [&](UMaterialInstanceDynamic* M)
	{
		if (!M) return;
		M->SetScalarParameterValue(TEXT("EmissiveScale"), EmissiveScale);
		M->SetScalarParameterValue(TEXT("VanillaWeight"), VanillaWeight);
		M->SetScalarParameterValue(TEXT("SunWeight"), SunWeight);
		M->SetScalarParameterValue(TEXT("Bright"), Brightness);
	};
	for (auto& KV : Mids) Apply(KV.Value);
	Apply(Fallback[0]); Apply(Fallback[1]);
}

FTransform FCrbAvatar::ViewBob() const
{
	if (!bHasCur) return FTransform::Identity;
	// GameRenderer.bobView with partial tick = interpolation alpha between Java ticks.
	const float F = LastAlpha;
	const float G = Cur.WalkDist - Cur.WalkDistO;
	const float H = -(Cur.WalkDist + G * F);
	const float I = FMath::Lerp(Cur.OBob, Cur.Bob, F);
	const float Tx = FMath::Sin(H * PI) * I * 0.5f, Ty = -FMath::Abs(FMath::Cos(H * PI) * I);
	const float A = FMath::DegreesToRadians(FMath::Sin(H * PI) * I * 3.f);
	const float B = FMath::DegreesToRadians(FMath::Abs(FMath::Cos(H * PI - 0.2f) * I) * 5.f);
	// M = Rz(A) * Rx(B) in Minecraft view space (x right, y up, -z forward).
	const float CA = FMath::Cos(A), SA = FMath::Sin(A), CB = FMath::Cos(B), SB = FMath::Sin(B);
	const float Rz[3][3] = { { CA, -SA, 0 }, { SA, CA, 0 }, { 0, 0, 1 } };
	const float Rx[3][3] = { { 1, 0, 0 }, { 0, CB, -SB }, { 0, SB, CB } };
	float M[3][3];
	for (int32 R = 0; R < 3; ++R) for (int32 C = 0; C < 3; ++C) { M[R][C] = 0; for (int32 K = 0; K < 3; ++K) M[R][C] += Rz[R][K] * Rx[K][C]; }
	// Change of basis to Unreal camera space: UE = (-z, x, y).
	const float Bm[3][3] = { { 0, 0, -1 }, { 1, 0, 0 }, { 0, 1, 0 } };
	float T[3][3], U[3][3];
	for (int32 R = 0; R < 3; ++R) for (int32 C = 0; C < 3; ++C) { T[R][C] = 0; for (int32 K = 0; K < 3; ++K) T[R][C] += Bm[R][K] * M[K][C]; }
	for (int32 R = 0; R < 3; ++R) for (int32 C = 0; C < 3; ++C) { U[R][C] = 0; for (int32 K = 0; K < 3; ++K) U[R][C] += T[R][K] * Bm[C][K]; }
	// FMatrix uses row vectors (v' = v * M), so rows are the columns of U.
	FMatrix Mat(FPlane(U[0][0], U[1][0], U[2][0], 0), FPlane(U[0][1], U[1][1], U[2][1], 0), FPlane(U[0][2], U[1][2], U[2][2], 0), FPlane(0, Tx * 100.f, Ty * 100.f, 1));
	return FTransform(Mat);
}

FString FCrbAvatar::DebugSummary(int32 Group) const
{
	FString Out = FString::Printf(TEXT("seq=%d yaw=%.1f pitch=%.1f mainhand=%s;"), Cur.Seq, Cur.Yaw, Cur.Pitch, *Cur.MainHand);
	for (const FCrbSurface& S : Cur.Surfaces)
	{
		if (S.Group != Group || S.V.Num() == 0) continue;
		FBox Box(ForceInit); FVector2D UMin(1e9f, 1e9f), UMax(-1e9f, -1e9f);
		for (const FVector& P : S.V) Box += P;
		for (const FVector2D& T : S.UV) { UMin.X = FMath::Min(UMin.X, T.X); UMin.Y = FMath::Min(UMin.Y, T.Y); UMax.X = FMath::Max(UMax.X, T.X); UMax.Y = FMath::Max(UMax.Y, T.Y); }
		Out += FString::Printf(TEXT(" [%s layer=%d n=%d ue_min=(%.1f,%.1f,%.1f) ue_max=(%.1f,%.1f,%.1f) uv=(%.4f,%.4f)-(%.4f,%.4f)]"), *S.Texture, S.Layer, S.V.Num(),
			Box.Min.X, Box.Min.Y, Box.Min.Z, Box.Max.X, Box.Max.Y, Box.Max.Z, UMin.X, UMin.Y, UMax.X, UMax.Y);
	}
	return Out;
}
