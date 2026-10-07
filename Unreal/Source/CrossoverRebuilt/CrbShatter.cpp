#include "CrbShatter.h"
#include "CrbWorld.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Actor.h"

namespace
{
	constexpr int32 MaxShardsPerBurst = 120;
	constexpr int32 MaxBursts = 24;
	constexpr float Gravity = 18.f;      // blocks/s^2 (vanilla item/particle gravity is ~16-20)
	constexpr float ShrinkTime = 0.6f;   // seconds of shrinking at the end of a shard's life
}

void FCrbShatterFx::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (UProceduralMeshComponent*& M : Pool) if (M) Collector.AddReferencedObject(M);
	for (FBurst& B : Bursts) if (B.Mesh) Collector.AddReferencedObject(B.Mesh);
}

int32 FCrbShatterFx::LiveShards() const
{
	int32 N = 0;
	for (const FBurst& B : Bursts) for (const FShard& S : B.Shards) if (S.Life < S.MaxLife) ++N;
	return N;
}

void FCrbShatterFx::Reset()
{
	for (FBurst& B : Bursts) if (B.Mesh) { B.Mesh->ClearAllMeshSections(); B.Mesh->SetVisibility(false); Pool.Add(B.Mesh); }
	Bursts.Reset();
	LastSeq = 0;
}

void FCrbShatterFx::OnEvents(const TArray<FCrbShatterEvent>& Events)
{
	for (const FCrbShatterEvent& E : Events)
	{
		if (E.Seq <= LastSeq) continue;
		LastSeq = E.Seq;
		if (E.Age > 20) continue; // stale (reconnect replay): Java re-sends the last two seconds every frame
		Spawn(E);
	}
}

void FCrbShatterFx::Spawn(const FCrbShatterEvent& E)
{
	if (!Owner || !World) return;
	if (Bursts.Num() >= MaxBursts) { FBurst& Old = Bursts[0]; if (Old.Mesh) { Old.Mesh->ClearAllMeshSections(); Old.Mesh->SetVisibility(false); Pool.Add(Old.Mesh); } Bursts.RemoveAt(0); }

	// The block's own baked model (already copied for the world mesh): its faces become the shards.
	TArray<FCrbQuad> Quads; int32 Layer = 1;
	if (const FCrbModel* M = World->Model(E.State)) { Quads = M->Quads; Layer = M->Layer; }
	if (Quads.Num() == 0)
	{
		++MissingModel;
		static const FVector C[6][4] = {
			{ {0,0,0},{1,0,0},{1,0,1},{0,0,1} }, { {0,1,0},{0,1,1},{1,1,1},{1,1,0} }, { {0,0,0},{0,1,0},{1,1,0},{1,0,0} },
			{ {0,0,1},{1,0,1},{1,1,1},{0,1,1} }, { {0,0,0},{0,0,1},{0,1,1},{0,1,0} }, { {1,0,0},{1,1,0},{1,1,1},{1,0,1} } };
		for (int32 F = 0; F < 6; ++F) { FCrbQuad Q; for (int32 K = 0; K < 4; ++K) { Q.Pos[K] = C[F][K]; Q.UV[K] = FVector2D::ZeroVector; } Q.Tint = FColor(200, 225, 255); Q.Face = F; Quads.Add(Q); }
	}
	// Faces turned toward the shot break into more pieces.
	const FVector Dir = E.Dir.GetSafeNormal();
	Quads.Sort([](const FCrbQuad& A, const FCrbQuad& B)
	{
		auto Area = [](const FCrbQuad& Q) { return FVector::CrossProduct(Q.Pos[1] - Q.Pos[0], Q.Pos[3] - Q.Pos[0]).Size(); };
		return Area(A) > Area(B);
	});
	if (Quads.Num() > 8) Quads.SetNum(8);

	FBurst B;
	B.Block = FIntVector(E.X, E.Y, E.Z);
	B.Layer = Layer;
	B.LightUV = FVector2D(((E.Light & 15) + 0.5f) / 16.f, (((E.Light >> 4) & 15) + 0.5f) / 16.f);
	const FVector Centre(0.5f, 0.5f, 0.5f);
	for (const FCrbQuad& Q : Quads)
	{
		const float Area = FVector::CrossProduct(Q.Pos[1] - Q.Pos[0], Q.Pos[3] - Q.Pos[0]).Size();
		const int32 N = Area > 0.45f ? 4 : (Area > 0.1f ? 2 : 1);
		FVector McN = FVector::CrossProduct(Q.Pos[1] - Q.Pos[0], Q.Pos[2] - Q.Pos[0]).GetSafeNormal();
		if (McN.IsNearlyZero()) McN = FCrbCoords::DirectionVector(Q.Face);
		static const float ShadeByFace[6] = { 0.5f, 1.0f, 0.8f, 0.8f, 0.6f, 0.6f };
		const FLinearColor Col(Q.Tint.R / 255.f, Q.Tint.G / 255.f, Q.Tint.B / 255.f, Q.Shade ? ShadeByFace[FMath::Clamp<int32>(Q.Face, 0, 5)] : 1.f);
		// Jittered (N+1)^2 lattice over the face (bilinear in position and atlas UV), interior points displaced.
		TArray<FVector2D> ST; ST.SetNum((N + 1) * (N + 1));
		for (int32 J = 0; J <= N; ++J) for (int32 I = 0; I <= N; ++I)
		{
			float S = (float)I / N, T = (float)J / N;
			if (I > 0 && I < N) S += Rng.FRandRange(-0.3f, 0.3f) / N;
			if (J > 0 && J < N) T += Rng.FRandRange(-0.3f, 0.3f) / N;
			ST[J * (N + 1) + I] = FVector2D(S, T);
		}
		auto PosAt = [&Q](const FVector2D& P) { return FMath::Lerp(FMath::Lerp(Q.Pos[0], Q.Pos[1], P.X), FMath::Lerp(Q.Pos[3], Q.Pos[2], P.X), P.Y); };
		auto UVAt = [&Q](const FVector2D& P) { return FMath::Lerp(FMath::Lerp(Q.UV[0], Q.UV[1], P.X), FMath::Lerp(Q.UV[3], Q.UV[2], P.X), P.Y); };
		for (int32 J = 0; J < N; ++J) for (int32 I = 0; I < N; ++I)
		{
			const FVector2D A = ST[J * (N + 1) + I], Bq = ST[J * (N + 1) + I + 1], Cq = ST[(J + 1) * (N + 1) + I + 1], D = ST[(J + 1) * (N + 1) + I];
			const bool bFlip = Rng.FRand() < 0.5f;
			const FVector2D Tris[2][3] = { { A, Bq, bFlip ? D : Cq }, { bFlip ? Bq : A, Cq, D } };
			for (int32 Tr = 0; Tr < 2 && B.Shards.Num() < MaxShardsPerBurst; ++Tr)
			{
				FShard S;
				FVector P[3]; for (int32 K = 0; K < 3; ++K) { P[K] = PosAt(Tris[Tr][K]); S.UV[K] = UVAt(Tris[Tr][K]); }
				S.P = (P[0] + P[1] + P[2]) / 3.f;
				for (int32 K = 0; K < 3; ++K) S.L[K] = P[K] - S.P;
				S.N = McN; S.C = Col;
				// Along the shot, outward from the block centre, a random kick and a little lift; faster near the hit side.
				const FVector Out = (S.P - Centre).GetSafeNormal();
				const float Facing = FMath::Clamp(-FVector::DotProduct(McN, Dir), 0.f, 1.f);
				S.V = Dir * Rng.FRandRange(2.5f, 6.5f) * (0.6f + 0.6f * Facing) + Out * Rng.FRandRange(1.0f, 3.0f) + Rng.GetUnitVector() * 1.2f + FVector(0, Rng.FRandRange(0.f, 1.8f), 0);
				S.R = FQuat::Identity;
				S.W = Rng.GetUnitVector() * Rng.FRandRange(4.f, 16.f);
				S.MaxLife = Rng.FRandRange(2.0f, 3.4f);
				B.Shards.Add(S);
			}
		}
	}
	if (B.Shards.Num() == 0) return;
	B.Mesh = Pool.Num() ? Pool.Pop() : nullptr;
	if (!B.Mesh)
	{
		B.Mesh = NewObject<UProceduralMeshComponent>(Owner);
		B.Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		B.Mesh->bUseAsyncCooking = false;
		B.Mesh->SetCastShadow(false);
		B.Mesh->SetupAttachment(Owner->GetRootComponent());
		B.Mesh->RegisterComponent();
	}
	B.Mesh->SetVisibility(true);
	B.Mesh->ClearAllMeshSections();
	++BurstsStarted; ShardsSpawned += B.Shards.Num(); LastSource = E.Source;
	LastBurstCenterMc = FVector(E.X + 0.5f, E.Y + 0.5f, E.Z + 0.5f); MaxShardTravel = 0;
	Bursts.Add(MoveTemp(B));
}

void FCrbShatterFx::Tick(float Dt, const FCrbCoords& Coords)
{
	Dt = FMath::Min(Dt, 0.05f);
	TArray<UMaterialInstanceDynamic*>* Mats = World ? &World->GetMaterials() : nullptr;
	for (int32 Bi = Bursts.Num() - 1; Bi >= 0; --Bi)
	{
		FBurst& B = Bursts[Bi];
		B.Age += Dt;
		bool bAlive = false;
		TArray<FVector> V; TArray<int32> I; TArray<FVector> N; TArray<FVector2D> UV0, UV1, UV2; TArray<FLinearColor> C;
		V.Reserve(B.Shards.Num() * 3); I.Reserve(B.Shards.Num() * 3);
		auto Solid = [this, &B](const FVector& Local)
		{
			return World->IsSolidAt(FIntVector(B.Block.X + FMath::FloorToInt(Local.X), B.Block.Y + FMath::FloorToInt(Local.Y), B.Block.Z + FMath::FloorToInt(Local.Z)));
		};
		for (FShard& S : B.Shards)
		{
			S.Life += Dt;
			if (S.Life < S.MaxLife) bAlive = true;
			if (!S.bResting && S.Life < S.MaxLife)
			{
				S.V.Y -= Gravity * Dt;
				S.V *= FMath::Max(0.f, 1.f - 0.35f * Dt); // air drag
				// Axis-separated sweep against the copied Java blocks: bounce off floors, glance off walls.
				FVector Np = S.P;
				Np.Y += S.V.Y * Dt;
				if (Solid(Np)) { if (S.V.Y < 0) Np.Y = FMath::FloorToFloat(B.Block.Y + Np.Y) + 1.f - B.Block.Y + 0.02f; else Np.Y = S.P.Y; S.V.Y *= -0.28f; S.V.X *= 0.55f; S.V.Z *= 0.55f; S.W *= 0.45f;
					if (S.V.Size() < 0.6f) { S.bResting = true; S.V = FVector::ZeroVector; } }
				Np.X += S.V.X * Dt; if (Solid(Np)) { Np.X = S.P.X; S.V.X *= -0.3f; }
				Np.Z += S.V.Z * Dt; if (Solid(Np)) { Np.Z = S.P.Z; S.V.Z *= -0.3f; }
				S.P = Np;
				const float Rate = S.W.Size();
				if (Rate > KINDA_SMALL_NUMBER) S.R = (FQuat(S.W / Rate, Rate * Dt) * S.R).GetNormalized();
				MaxShardTravel = FMath::Max(MaxShardTravel, (S.P - FVector(0.5f)).Size());
			}
			const float Scale = FMath::Clamp((S.MaxLife - S.Life) / ShrinkTime, 0.f, 1.f);
			const int32 Base = V.Num();
			const FVector Nrm = FCrbCoords::NormalToUE(S.R.RotateVector(S.N));
			for (int32 K = 0; K < 3; ++K)
			{
				V.Add(FCrbCoords::DirToUE(S.P + S.R.RotateVector(S.L[K] * Scale)));
				N.Add(Nrm); { FVector2D Co, Fi; CrbSplitUV(S.UV[K], Co, Fi); UV0.Add(Co); UV2.Add(Fi); } UV1.Add(B.LightUV); C.Add(S.C);
			}
			I.Append({ Base, Base + 1, Base + 2 });
		}
		if (!bAlive)
		{
			if (B.Mesh) { B.Mesh->ClearAllMeshSections(); B.Mesh->SetVisibility(false); Pool.Add(B.Mesh); }
			Bursts.RemoveAt(Bi);
			continue;
		}
		if (!B.Mesh) continue;
		B.Mesh->SetWorldLocation(Coords.ToUE(B.Block.X, B.Block.Y, B.Block.Z));
		if (B.Mesh->GetNumSections() == 0)
		{
			B.Mesh->CreateMeshSection_LinearColor(0, V, I, N, UV0, UV1, UV2, TArray<FVector2D>(), C, TArray<FProcMeshTangent>(), false);
			const int32 L = B.Layer == 2 ? 1 : 0;
			if (Mats && Mats->IsValidIndex(L)) B.Mesh->SetMaterial(0, (*Mats)[L]);
		}
		else
			B.Mesh->UpdateMeshSection_LinearColor(0, V, N, UV0, UV1, UV2, TArray<FVector2D>(), C, TArray<FProcMeshTangent>());
	}
}
