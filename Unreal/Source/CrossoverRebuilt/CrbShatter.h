// Glass shatter effect. Java reports every glass block / pane destroyed by a projectile (Guns++ bullet or arrow) with
// the block state it had, the shot direction and the light at the block (crb.Shatter). Unreal breaks that block's own
// model faces (same atlas UVs, tint, layer and Minecraft light map as the world mesh) into jittered triangular shards
// that burst along the shot, tumble, fall under gravity, bounce on the copied Java blocks and shrink away.
#pragma once
#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "CrbCoords.h"

class AActor;
class UProceduralMeshComponent;
class FCrbWorld;

struct FCrbShatterEvent { int64 Seq = 0; int32 X = 0, Y = 0, Z = 0, State = 0, Light = 0xF0, Age = 0; FVector Dir = FVector(0, 0, 1); FString Source; };

class FCrbShatterFx : public FGCObject
{
public:
	void Init(AActor* InOwner, FCrbWorld* InWorld) { Owner = InOwner; World = InWorld; }
	void OnEvents(const TArray<FCrbShatterEvent>& Events);
	void Tick(float Dt, const FCrbCoords& Coords);
	void Reset();
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FCrbShatterFx"); }

	// Diagnostics / test evidence.
	int64 LastSeq = 0; int32 BurstsStarted = 0, ShardsSpawned = 0, MissingModel = 0; FString LastSource;
	int32 LiveShards() const;
	int32 LiveBursts() const { return Bursts.Num(); }
	FVector LastBurstCenterMc = FVector::ZeroVector; // MC coords of the latest burst
	float MaxShardTravel = 0;                         // furthest any shard of the latest burst got from its block (blocks)

private:
	struct FShard
	{
		FVector P, V;          // centre (MC blocks, relative to the burst block origin), velocity (blocks/s)
		FQuat R; FVector W;    // orientation, angular velocity (rad/s, MC axes)
		FVector L[3];          // corner offsets from the centre (MC, unrotated)
		FVector2D UV[3]; FVector N; FLinearColor C;
		float Life = 0, MaxLife = 2.5f; bool bResting = false;
	};
	struct FBurst
	{
		FIntVector Block; int32 Layer = 0; FVector2D LightUV; TArray<FShard> Shards; UProceduralMeshComponent* Mesh = nullptr; float Age = 0;
	};
	void Spawn(const FCrbShatterEvent& E);
	AActor* Owner = nullptr; FCrbWorld* World = nullptr;
	TArray<FBurst> Bursts;
	TArray<UProceduralMeshComponent*> Pool;
	FRandomStream Rng = FRandomStream(0x5EA7);
};
