#include "CrbWorld.h"
#include "CrbProtocol.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Actor.h"
#include "Async/Async.h"

namespace
{
	constexpr int32 MaxJobs = 2;
	constexpr int32 MaxAppliesPerTick = 2;
	FORCEINLINE int32 CellIndex(int32 X, int32 Y, int32 Z) { return (Y * 16 + Z) * 16 + X; }

	struct FNeighbor
	{
		bool bValid = false;
		TArray<FCrbModelPtr> Models; // per palette index
		TArray<uint16> Indices;
		TArray<uint8> Light;
		bool bEmpty = true;
	};
	struct FMeshInput
	{
		FIntVector Pos; int32 Revision = 0; int32 ModelVersion = 0;
		TArray<FCrbModelPtr> Models; TArray<uint16> Indices; TArray<uint8> Light; bool bEmpty = true;
		FNeighbor N[6];
	};

	FCrbMeshOut BuildMesh(const FMeshInput& In)
	{
		FCrbMeshOut Out; Out.Pos = In.Pos; Out.Revision = In.Revision; Out.ModelVersion = In.ModelVersion;
		if (In.bEmpty) return Out;
		auto Lookup = [&](int32 X, int32 Y, int32 Z, const FCrbModel*& M, uint8& L) -> bool
		{
			if (X >= 0 && X < 16 && Y >= 0 && Y < 16 && Z >= 0 && Z < 16)
			{
				const int32 C = CellIndex(X, Y, Z);
				const uint16 P = In.Indices[C];
				M = In.Models.IsValidIndex(P) ? In.Models[P].Get() : nullptr; L = In.Light[C];
				return true;
			}
			int32 D = -1;
			if (Y < 0) D = 0; else if (Y > 15) D = 1; else if (Z < 0) D = 2; else if (Z > 15) D = 3; else if (X < 0) D = 4; else D = 5;
			const FNeighbor& Nb = In.N[D];
			if (!Nb.bValid) { M = nullptr; L = 0xF0; return false; }
			const int32 C = CellIndex((X + 16) & 15, (Y + 16) & 15, (Z + 16) & 15);
			L = Nb.Light.IsValidIndex(C) ? Nb.Light[C] : 0xF0;
			if (Nb.bEmpty) { M = nullptr; return true; }
			const uint16 P = Nb.Indices[C];
			M = Nb.Models.IsValidIndex(P) ? Nb.Models[P].Get() : nullptr;
			return true;
		};
		for (int32 Y = 0; Y < 16; ++Y) for (int32 Z = 0; Z < 16; ++Z) for (int32 X = 0; X < 16; ++X)
		{
			const int32 C = CellIndex(X, Y, Z);
			const uint16 P = In.Indices[C];
			if (!In.Models.IsValidIndex(P)) { ++Out.Missing; continue; }
			const FCrbModel* M = In.Models[P].Get();
			if (!M) { ++Out.Missing; continue; }
			if (M->bAir || M->Quads.Num() == 0) continue;
			const uint8 Own = In.Light[C];
			const int32 L = M->Layer == 2 ? 1 : 0;
			for (const FCrbQuad& Q : M->Quads)
			{
				uint8 QuadLight = Own;
				if (Q.Cull >= 0)
				{
					const FIntVector D = FCrbCoords::DirectionInt(Q.Cull);
					const FCrbModel* NM = nullptr; uint8 NL = 0xF0;
					Lookup(X + D.X, Y + D.Y, Z + D.Z, NM, NL);
					if (NM && NM->bOpaqueCube) continue;
					// Same translucent block on both sides (water/glass runs): hide the shared face.
					if (NM && M->Layer == 2 && NM->Id == M->Id) continue;
					QuadLight = NL;
				}
				else if (M->bOpaqueCube)
				{
					const FIntVector D = FCrbCoords::DirectionInt(Q.Face);
					const FCrbModel* NM = nullptr; uint8 NL = 0xF0;
					Lookup(X + D.X, Y + D.Y, Z + D.Z, NM, NL);
					QuadLight = NL;
				}
				const float Block = ((QuadLight & 15) + 0.5f) / 16.f;
				const float Sky = (((QuadLight >> 4) & 15) + 0.5f) / 16.f;
				static const float ShadeByFace[6] = { 0.5f, 1.0f, 0.8f, 0.8f, 0.6f, 0.6f };
				const float Shade = Q.Shade ? ShadeByFace[FMath::Clamp<int32>(Q.Face, 0, 5)] : 1.f;
				const FLinearColor Color(Q.Tint.R / 255.f, Q.Tint.G / 255.f, Q.Tint.B / 255.f, Shade);
				const FVector E1 = Q.Pos[1] - Q.Pos[0], E2 = Q.Pos[2] - Q.Pos[0];
				FVector McN = FVector::CrossProduct(E1, E2).GetSafeNormal();
				if (McN.IsNearlyZero()) McN = FCrbCoords::DirectionVector(Q.Face);
				const FVector Normal = FCrbCoords::NormalToUE(McN);
				// Vanilla "smooth lighting" (AmbientOcclusionFace) for axis-aligned faces on the block boundary:
				// each vertex averages light from the face cell and its two side / one corner neighbours and darkens
				// by 0.2 per occluding opaque cube.
				const bool bSmooth = Q.Cull >= 0 || M->bOpaqueCube;
				const FIntVector D = FCrbCoords::DirectionInt(Q.Face);
				const int32 Axis = D.X != 0 ? 0 : (D.Y != 0 ? 1 : 2);
				const int32 TA = Axis == 0 ? 1 : 0, TB = Axis == 2 ? 1 : 2; // the two tangent axes
				const int32 Base = Out.V[L].Num();
				for (int32 K = 0; K < 4; ++K)
				{
					const FVector Local(X + Q.Pos[K].X, Y + Q.Pos[K].Y, Z + Q.Pos[K].Z);
					FVector2D UV1(Block, Sky);
					FLinearColor VC = Color;
					if (bSmooth)
					{
						const float PA = Q.Pos[K][TA], PB = Q.Pos[K][TB];
						int32 A3[3] = { 0, 0, 0 }, B3[3] = { 0, 0, 0 };
						A3[TA] = PA > 0.5f ? 1 : -1; B3[TB] = PB > 0.5f ? 1 : -1;
						const FIntVector SA(A3[0], A3[1], A3[2]), SB(B3[0], B3[1], B3[2]);
						const FIntVector F(X + D.X, Y + D.Y, Z + D.Z);
						const FCrbModel* M0 = nullptr; const FCrbModel* M1 = nullptr; const FCrbModel* M2 = nullptr; const FCrbModel* M3 = nullptr;
						uint8 L0 = 0xF0, L1 = 0xF0, L2 = 0xF0, L3 = 0xF0;
						Lookup(F.X, F.Y, F.Z, M0, L0);
						Lookup(F.X + SA.X, F.Y + SA.Y, F.Z + SA.Z, M1, L1);
						Lookup(F.X + SB.X, F.Y + SB.Y, F.Z + SB.Z, M2, L2);
						const bool O1 = M1 && M1->bOpaqueCube, O2 = M2 && M2->bOpaqueCube;
						Lookup(F.X + SA.X + SB.X, F.Y + SA.Y + SB.Y, F.Z + SA.Z + SB.Z, M3, L3);
						const bool O3 = (O1 && O2) || (M3 && M3->bOpaqueCube);
						const bool O0 = M0 && M0->bOpaqueCube;
						const float Ao = ((O0 ? 0.2f : 1.f) + (O1 ? 0.2f : 1.f) + (O2 ? 0.2f : 1.f) + (O3 ? 0.2f : 1.f)) * 0.25f;
						// Occluders contribute the face cell's light (vanilla blend replaces zeros with the centre).
						const uint8 C0 = QuadLight;
						const uint8 S1 = O1 ? C0 : L1, S2 = O2 ? C0 : L2, S3 = O3 ? C0 : L3;
						const float Bl = ((C0 & 15) + (S1 & 15) + (S2 & 15) + (S3 & 15)) * 0.25f;
						const float Sk = ((C0 >> 4) + (S1 >> 4) + (S2 >> 4) + (S3 >> 4)) * 0.25f;
						UV1 = FVector2D((Bl + 0.5f) / 16.f, (Sk + 0.5f) / 16.f);
						VC.A *= Ao;
					}
					Out.V[L].Add(FCrbCoords::DirToUE(Local));
					Out.N[L].Add(Normal);
					{ FVector2D Co, Fi; CrbSplitUV(Q.UV[K], Co, Fi); Out.UV0[L].Add(Co); Out.UV2[L].Add(Fi); }
					Out.UV1[L].Add(UV1);
					Out.C[L].Add(VC);
				}
				Out.I[L].Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
			}
		}
		return Out;
	}
}

FCrbWorld::~FCrbWorld() { for (auto& J : Jobs) if (J.IsValid()) J.Wait(); }

void FCrbWorld::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (UMaterialInstanceDynamic*& M : Mids) if (M) Collector.AddReferencedObject(M);
	for (auto& KV : Sections) if (KV.Value.Mesh) Collector.AddReferencedObject(KV.Value.Mesh);
}

void FCrbWorld::Init(AActor* InOwner, UMaterialInterface* Opaque, UMaterialInterface* Translucent)
{
	Owner = InOwner; BaseOpaque = Opaque; BaseTranslucent = Translucent;
	Mids.SetNum(2);
	Mids[0] = BaseOpaque ? UMaterialInstanceDynamic::Create(BaseOpaque, Owner) : nullptr;
	Mids[1] = BaseTranslucent ? UMaterialInstanceDynamic::Create(BaseTranslucent, Owner) : nullptr;
}

void FCrbWorld::Reset()
{
	for (auto& J : Jobs) if (J.IsValid()) J.Wait();
	Jobs.Reset();
	for (auto& KV : Sections) if (KV.Value.Mesh) KV.Value.Mesh->DestroyComponent();
	Sections.Reset(); Models.Reset(); ++ModelVersion; bHasWindow = false;
}

void FCrbWorld::SetAtlas(UTexture* Atlas, UTexture* Lightmap)
{
	for (UMaterialInstanceDynamic* M : Mids) if (M)
	{
		if (Atlas) M->SetTextureParameterValue(TEXT("Atlas"), Atlas);
		if (Lightmap) M->SetTextureParameterValue(TEXT("Lightmap"), Lightmap);
		M->SetScalarParameterValue(TEXT("HasAtlas"), Atlas ? 1.f : 0.f);
	}
}

void FCrbWorld::SetLightingParams(float VanillaWeight, float SunWeight, bool bLightingEnabled)
{
	for (UMaterialInstanceDynamic* M : Mids) if (M)
	{
		M->SetScalarParameterValue(TEXT("VanillaWeight"), VanillaWeight);
		M->SetScalarParameterValue(TEXT("SunWeight"), SunWeight);
		M->SetScalarParameterValue(TEXT("UseLightmap"), bLightingEnabled ? 1.f : 0.f);
	}
}

bool FCrbWorld::OnModel(const TSharedPtr<FJsonObject>& J, const TArray<uint8>& Bin, FString& Error)
{
	int32 Id = -1, Layer = 0, Count = 0, Emit = 0; bool bOpaque = false, bAir = false; FString State;
	if (!J.IsValid() || !J->TryGetNumberField(TEXT("id"), Id) || Id < 0) { Error = TEXT("model id"); ++Rejected; return false; }
	J->TryGetNumberField(TEXT("layer"), Layer); J->TryGetNumberField(TEXT("quads"), Count); J->TryGetNumberField(TEXT("emit"), Emit);
	J->TryGetBoolField(TEXT("opaque"), bOpaque); J->TryGetBoolField(TEXT("air"), bAir); J->TryGetStringField(TEXT("state"), State);
	constexpr int32 QuadBytes = 88;
	if (Count < 0 || Count > MaxQuadsPerModel || Bin.Num() != Count * QuadBytes || Layer < 0 || Layer > 2) { Error = FString::Printf(TEXT("model %d malformed (quads=%d bin=%d)"), Id, Count, Bin.Num()); ++Rejected; return false; }
	if (!Models.Contains(Id) && Models.Num() >= MaxModels) { Error = TEXT("model cache full"); ++Rejected; return false; }
	TSharedPtr<FCrbModel, ESPMode::ThreadSafe> M = MakeShared<FCrbModel, ESPMode::ThreadSafe>();
	M->Id = Id; M->State = State.Left(200); M->Layer = (uint8)Layer; M->bOpaqueCube = bOpaque; M->Emission = (uint8)FMath::Clamp(Emit, 0, 15); M->bAir = bAir;
	Crb::FReader R(Bin);
	M->Quads.SetNum(Count);
	for (int32 Q = 0; Q < Count; ++Q)
	{
		FCrbQuad& Quad = M->Quads[Q];
		for (int32 K = 0; K < 4; ++K)
		{
			const float X = R.F32(), Y = R.F32(), Z = R.F32(), U = R.F32(), V = R.F32();
			if (!R.Finite(X) || !R.Finite(Y) || !R.Finite(Z) || FMath::Abs(X) > 3 || FMath::Abs(Y) > 3 || FMath::Abs(Z) > 3 || U < -0.01f || U > 1.01f || V < -0.01f || V > 1.01f)
			{ Error = FString::Printf(TEXT("model %d vertex out of range"), Id); ++Rejected; return false; }
			Quad.Pos[K] = FVector(X, Y, Z); Quad.UV[K] = FVector2D(U, V);
		}
		const uint32 Argb = R.U32();
		Quad.Tint = FColor((Argb >> 16) & 255, (Argb >> 8) & 255, Argb & 255, 255);
		Quad.Cull = R.I8(); Quad.Face = R.I8(); Quad.Shade = R.U8(); R.U8();
		if (Quad.Cull < -1 || Quad.Cull > 5 || Quad.Face < 0 || Quad.Face > 5) { Error = TEXT("model direction"); ++Rejected; return false; }
	}
	if (!R.bOk) { Error = TEXT("model truncated"); ++Rejected; return false; }
	Models.Add(Id, M);
	++ModelVersion;
	// Sections waiting for this model will rebuild.
	for (auto& KV : Sections) if (!KV.Value.bEmpty && KV.Value.Palette.Contains(Id)) KV.Value.bDirty = true;
	return true;
}

bool FCrbWorld::OnSection(const TSharedPtr<FJsonObject>& J, const TArray<uint8>& Bin, FString& Error)
{
	int32 SX = 0, SY = 0, SZ = 0, Rev = 0; bool bEmpty = false;
	if (!J.IsValid() || !J->TryGetNumberField(TEXT("sx"), SX) || !J->TryGetNumberField(TEXT("sy"), SY) || !J->TryGetNumberField(TEXT("sz"), SZ)) { Error = TEXT("section coords"); ++Rejected; return false; }
	J->TryGetNumberField(TEXT("rev"), Rev); J->TryGetBoolField(TEXT("empty"), bEmpty);
	if (FMath::Abs(SX) > 2000000 || FMath::Abs(SZ) > 2000000 || SY < -8 || SY > 24) { Error = TEXT("section out of range"); ++Rejected; return false; }
	const FIntVector Pos(SX, SY, SZ);
	if (bHasWindow && (FMath::Abs(SX - WindowCenter.X) > WindowR || FMath::Abs(SZ - WindowCenter.Z) > WindowR || FMath::Abs(SY - WindowCenter.Y) > WindowRY)) return true; // stale, outside window
	TArray<int32> Palette;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!bEmpty)
	{
		if (!J->TryGetArrayField(TEXT("palette"), Arr) || Arr->Num() == 0 || Arr->Num() > 4096) { Error = TEXT("section palette"); ++Rejected; return false; }
		for (auto& V : *Arr) Palette.Add((int32)V->AsNumber());
	}
	const int32 Expected = (bEmpty ? 0 : 8192) + 4096;
	if (Bin.Num() != Expected) { Error = FString::Printf(TEXT("section payload %d != %d"), Bin.Num(), Expected); ++Rejected; return false; }
	FCrbSection* S = Sections.Find(Pos);
	if (!S)
	{
		if (Sections.Num() >= MaxSections) { Error = TEXT("section cache full"); ++Rejected; return false; }
		S = &Sections.Add(Pos); S->Pos = Pos;
	}
	if (Rev < S->Revision) return true; // older than what we hold
	S->Revision = Rev; S->bEmpty = bEmpty; S->Palette = MoveTemp(Palette);
	S->Indices.SetNumUninitialized(bEmpty ? 0 : 4096);
	if (!bEmpty)
	{
		Crb::FReader R(Bin);
		for (int32 I = 0; I < 4096; ++I)
		{
			const uint16 P = R.U16();
			if (P >= S->Palette.Num()) { Error = TEXT("section index outside palette"); ++Rejected; Sections.Remove(Pos); return false; }
			S->Indices[I] = P;
		}
	}
	S->Light.SetNumUninitialized(4096);
	FMemory::Memcpy(S->Light.GetData(), Bin.GetData() + (bEmpty ? 0 : 8192), 4096);
	S->bDirty = true;
	// Neighbour borders change too.
	for (int32 D = 0; D < 6; ++D) if (FCrbSection* N = Sections.Find(Pos + FCrbCoords::DirectionInt(D))) N->bDirty = true;
	return true;
}

void FCrbWorld::OnWindow(const TSharedPtr<FJsonObject>& J)
{
	int32 CX = 0, CY = 0, CZ = 0, R = 4, RY = 3;
	if (!J.IsValid()) return;
	J->TryGetNumberField(TEXT("sx"), CX); J->TryGetNumberField(TEXT("sy"), CY); J->TryGetNumberField(TEXT("sz"), CZ);
	J->TryGetNumberField(TEXT("r"), R); J->TryGetNumberField(TEXT("ry"), RY);
	WindowCenter = FIntVector(CX, CY, CZ); WindowR = FMath::Clamp(R, 1, 8); WindowRY = FMath::Clamp(RY, 1, 6); bHasWindow = true;
	for (auto It = Sections.CreateIterator(); It; ++It)
	{
		const FIntVector P = It.Key();
		if (FMath::Abs(P.X - CX) > WindowR || FMath::Abs(P.Z - CZ) > WindowR || FMath::Abs(P.Y - CY) > WindowRY)
		{
			if (It.Value().Mesh) It.Value().Mesh->DestroyComponent();
			It.RemoveCurrent();
		}
	}
}

int32 FCrbWorld::StateAt(const FIntVector& B) const
{
	const FCrbSection* S = Sections.Find(FIntVector(B.X >> 4, B.Y >> 4, B.Z >> 4));
	if (!S) return -1;
	if (S->bEmpty) return 0;
	const uint16 P = S->Indices[CellIndex(B.X & 15, B.Y & 15, B.Z & 15)];
	return S->Palette.IsValidIndex(P) ? S->Palette[P] : -1;
}

uint8 FCrbWorld::LightAt(const FIntVector& B) const
{
	const FCrbSection* S = Sections.Find(FIntVector(B.X >> 4, B.Y >> 4, B.Z >> 4));
	if (!S || S->Light.Num() != 4096) return 0xF0;
	return S->Light[CellIndex(B.X & 15, B.Y & 15, B.Z & 15)];
}

bool FCrbWorld::IsSolidAt(const FIntVector& B) const
{
	const int32 Id = StateAt(B);
	if (Id <= 0) return false;
	const FCrbModel* M = Model(Id);
	return M && M->bOpaqueCube;
}

void FCrbWorld::GatherEmitters(const FVector& Near, int32 Max, TArray<TPair<FIntVector, uint8>>& Out) const
{
	TArray<TPair<float, TPair<FIntVector, uint8>>> Found;
	for (const auto& KV : Sections)
	{
		const FCrbSection& S = KV.Value;
		if (S.bEmpty) continue;
		const FVector SC(S.Pos.X * 16 + 8, S.Pos.Y * 16 + 8, S.Pos.Z * 16 + 8);
		if (FVector::DistSquared(SC, Near) > 40.f * 40.f) continue;
		TArray<uint8> Emit; Emit.SetNumZeroed(S.Palette.Num()); bool bAny = false;
		for (int32 P = 0; P < S.Palette.Num(); ++P) if (const FCrbModel* M = Model(S.Palette[P])) { Emit[P] = M->Emission; bAny |= M->Emission > 0; }
		if (!bAny) continue;
		for (int32 C = 0; C < 4096; ++C) if (Emit[S.Indices[C]] >= 10)
		{
			const FIntVector B(S.Pos.X * 16 + (C & 15), S.Pos.Y * 16 + (C >> 8), S.Pos.Z * 16 + ((C >> 4) & 15));
			Found.Add(MakeTuple(FVector::DistSquared(FVector(B) + FVector(0.5f), Near), TPair<FIntVector, uint8>(B, Emit[S.Indices[C]])));
		}
	}
	Found.Sort([](const auto& A, const auto& B) { return A.Key < B.Key; });
	for (int32 I = 0; I < Found.Num() && Out.Num() < Max; ++I) Out.Add(Found[I].Value);
}

int32 FCrbWorld::NumMeshed() const { int32 N = 0; for (auto& KV : Sections) if (KV.Value.MeshedRevision == KV.Value.Revision && !KV.Value.bDirty) ++N; return N; }
int32 FCrbWorld::TotalTriangles() const { int32 N = 0; for (auto& KV : Sections) N += KV.Value.Triangles; return N; }

void FCrbWorld::Schedule(const FCrbCoords& Coords)
{
	if (Jobs.Num() >= MaxJobs) return;
	// Nearest dirty section first.
	const FVector Center = LastAnchorMc / 16.f;
	TArray<FCrbSection*> Dirty;
	for (auto& KV : Sections) if (KV.Value.bDirty) Dirty.Add(&KV.Value);
	Dirty.Sort([&](const FCrbSection& A, const FCrbSection& B) { return FVector::DistSquared(FVector(A.Pos), Center) < FVector::DistSquared(FVector(B.Pos), Center); });
	for (FCrbSection* S : Dirty)
	{
		if (Jobs.Num() >= MaxJobs) break;
		S->bDirty = false;
		FMeshInput In;
		In.Pos = S->Pos; In.Revision = S->Revision; In.ModelVersion = ModelVersion; In.bEmpty = S->bEmpty;
		In.Indices = S->Indices; In.Light = S->Light;
		for (int32 Id : S->Palette) In.Models.Add(ModelPtr(Id));
		for (int32 D = 0; D < 6; ++D)
		{
			const FCrbSection* N = Sections.Find(S->Pos + FCrbCoords::DirectionInt(D));
			if (!N) continue;
			FNeighbor& Nb = In.N[D];
			Nb.bValid = true; Nb.bEmpty = N->bEmpty; Nb.Indices = N->Indices; Nb.Light = N->Light;
			for (int32 Id : N->Palette) Nb.Models.Add(ModelPtr(Id));
		}
		Jobs.Add(Async(EAsyncExecution::ThreadPool, [In = MoveTemp(In)]() { return BuildMesh(In); }));
	}
}

void FCrbWorld::Apply(FCrbMeshOut&& Out, const FCrbCoords& Coords)
{
	FCrbSection* S = Sections.Find(Out.Pos);
	if (!S || S->Revision != Out.Revision) return; // superseded or dropped by the window
	const double Start = FPlatformTime::Seconds();
	LastMissing = Out.Missing;
	if (Out.Missing > 0) S->bDirty = true; // retry once the models arrive (OnModel also flags it)
	const bool bHasGeometry = Out.V[0].Num() + Out.V[1].Num() > 0;
	if (!bHasGeometry)
	{
		if (S->Mesh) { S->Mesh->DestroyComponent(); S->Mesh = nullptr; }
	}
	else
	{
		if (!S->Mesh)
		{
			S->Mesh = NewObject<UProceduralMeshComponent>(Owner);
			S->Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			S->Mesh->bUseAsyncCooking = false;
			S->Mesh->SetCastShadow(true);
			S->Mesh->SetupAttachment(Owner->GetRootComponent());
			S->Mesh->RegisterComponent();
		}
		S->Mesh->SetWorldLocation(Coords.ToUE(S->Pos.X * 16.0, S->Pos.Y * 16.0, S->Pos.Z * 16.0));
		S->Mesh->ClearAllMeshSections();
		const TArray<FProcMeshTangent> NoTangents;
		for (int32 L = 0; L < 2; ++L)
		{
			if (Out.V[L].Num() == 0) continue;
			S->Mesh->CreateMeshSection_LinearColor(L, Out.V[L], Out.I[L], Out.N[L], Out.UV0[L], Out.UV1[L], Out.UV2[L], TArray<FVector2D>(), Out.C[L], NoTangents, false);
			S->Mesh->SetMaterial(L, Mids.IsValidIndex(L) ? Mids[L] : nullptr);
		}
	}
	S->Triangles = (Out.I[0].Num() + Out.I[1].Num()) / 3;
	S->MeshedRevision = Out.Revision; S->MeshedModelVersion = Out.ModelVersion;
	MaxApplyMs = FMath::Max(MaxApplyMs, (FPlatformTime::Seconds() - Start) * 1000.0);
}

void FCrbWorld::Tick(const FCrbCoords& Coords)
{
	// Re-anchor existing components if the anchor moved.
	const FVector Anchor((float)Coords.AX, (float)Coords.AY, (float)Coords.AZ);
	if (!Anchor.Equals(LastAnchorMc, 0.01f))
	{
		LastAnchorMc = Anchor;
		for (auto& KV : Sections) if (KV.Value.Mesh) KV.Value.Mesh->SetWorldLocation(Coords.ToUE(KV.Key.X * 16.0, KV.Key.Y * 16.0, KV.Key.Z * 16.0));
	}
	int32 Applied = 0;
	for (int32 I = 0; I < Jobs.Num(); )
	{
		if (Applied < MaxAppliesPerTick && Jobs[I].IsReady())
		{
			FCrbMeshOut Out = Jobs[I].Get(); Jobs.RemoveAtSwap(I); Apply(MoveTemp(Out), Coords); ++Applied;
		}
		else ++I;
	}
	Schedule(Coords);
}
