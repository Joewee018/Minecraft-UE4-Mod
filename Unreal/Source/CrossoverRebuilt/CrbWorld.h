// Copied, read-only view of nearby Minecraft chunk sections. Java owns all block state; this class only meshes it.
#pragma once
#include "CoreMinimal.h"
#include "Async/Future.h"
#include "UObject/GCObject.h"
#include "CrbCoords.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class AActor;

struct FCrbQuad
{
	FVector Pos[4];      // block-local Minecraft units (0..1 for a cube)
	FVector2D UV[4];     // atlas UVs (Java sprite coordinates, 0..1)
	FColor Tint;         // resolved Java block-color tint (white when untinted)
	int8 Cull = -1;      // Direction index of the neighbour that hides this face, -1 = never culled
	int8 Face = 1;       // facing direction (0 down,1 up,2 north,3 south,4 west,5 east)
	uint8 Shade = 1;     // Java "shade" flag
};

struct FCrbModel
{
	int32 Id = 0;
	FString State;
	uint8 Layer = 0;      // 0 solid, 1 cutout, 2 translucent
	bool bOpaqueCube = false; // occludes neighbour faces (Java: solid render + full cube)
	uint8 Emission = 0;
	bool bAir = false;
	TArray<FCrbQuad> Quads;
};

using FCrbModelPtr = TSharedPtr<const FCrbModel, ESPMode::ThreadSafe>;

struct FCrbSection
{
	FIntVector Pos;          // section coordinates
	int32 Revision = 0;
	bool bEmpty = true;
	TArray<int32> Palette;   // Java block-state ids
	TArray<uint16> Indices;  // 4096 palette indices, (y*16+z)*16+x
	TArray<uint8> Light;     // 4096 entries, sky<<4 | block
	UProceduralMeshComponent* Mesh = nullptr;
	int32 MeshedRevision = -1;
	int32 MeshedModelVersion = -1;
	bool bDirty = true;
	int32 Triangles = 0;
};

struct FCrbMeshOut
{
	FIntVector Pos; int32 Revision = 0; int32 ModelVersion = 0;
	TArray<FVector> V[2]; TArray<int32> I[2]; TArray<FVector> N[2]; TArray<FVector2D> UV0[2]; TArray<FVector2D> UV1[2]; TArray<FVector2D> UV2[2]; TArray<FLinearColor> C[2];
	int32 Missing = 0;
};

class FCrbWorld : public FGCObject
{
public:
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FCrbWorld"); }
	~FCrbWorld();
	static constexpr int32 MaxSections = 1024; // 13x13x5 window (radius 6) = 845
	static constexpr int32 MaxModels = 32768; // vanilla Debug Mode shows ~24k block states
	static constexpr int32 MaxQuadsPerModel = 512;

	void Init(AActor* InOwner, UMaterialInterface* Opaque, UMaterialInterface* Translucent);
	void Reset();
	bool OnModel(const TSharedPtr<class FJsonObject>& J, const TArray<uint8>& Bin, FString& Error);
	bool OnSection(const TSharedPtr<class FJsonObject>& J, const TArray<uint8>& Bin, FString& Error);
	void OnWindow(const TSharedPtr<class FJsonObject>& J);
	void Tick(const FCrbCoords& Coords);
	void SetAtlas(class UTexture* Atlas, class UTexture* Lightmap);
	void SetLightingParams(float VanillaWeight, float SunWeight, bool bLightingEnabled);

	// Queries over copied data (game thread).
	int32 StateAt(const FIntVector& Block) const;          // -1 unknown, 0 air
	uint8 LightAt(const FIntVector& Block) const;
	const FCrbModel* Model(int32 Id) const { const FCrbModelPtr* P = Models.Find(Id); return P ? P->Get() : nullptr; }
	FCrbModelPtr ModelPtr(int32 Id) const { const FCrbModelPtr* P = Models.Find(Id); return P ? *P : nullptr; }
	bool IsSolidAt(const FIntVector& Block) const;
	void GatherEmitters(const FVector& NearMc, int32 Max, TArray<TPair<FIntVector, uint8>>& Out) const;

	int32 NumSections() const { return Sections.Num(); }
	int32 NumMeshed() const;
	int32 NumModels() const { return Models.Num(); }
	int32 TotalTriangles() const;
	int32 MissingModelRefs() const { return LastMissing; }
	int32 Rejected = 0;
	double MaxApplyMs = 0;
	TArray<UMaterialInstanceDynamic*>& GetMaterials() { return Mids; }
	int32 GetWindowRadius() const { return WindowR; }

private:
	void Schedule(const FCrbCoords& Coords);
	void Apply(FCrbMeshOut&& Out, const FCrbCoords& Coords);
	AActor* Owner = nullptr;
	UMaterialInterface* BaseOpaque = nullptr;
	UMaterialInterface* BaseTranslucent = nullptr;
	TArray<UMaterialInstanceDynamic*> Mids; // [0]=opaque [1]=translucent (owned by Owner via UPROPERTY array)
	TMap<FIntVector, FCrbSection> Sections;
	TMap<int32, FCrbModelPtr> Models;
	int32 ModelVersion = 0;
	FIntVector WindowCenter = FIntVector::ZeroValue; int32 WindowR = 4; int32 WindowRY = 3; bool bHasWindow = false;
	TArray<TFuture<FCrbMeshOut>> Jobs;
	int32 LastMissing = 0;
	FVector LastAnchorMc = FVector::ZeroVector;
};
