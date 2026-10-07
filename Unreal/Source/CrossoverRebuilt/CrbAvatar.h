// Presents Java-posed player geometry. Geometry is NEVER gated on texture availability: a surface whose
// texture has not arrived (or was rejected) renders with a vertex-colour fallback and is re-bound later.
#pragma once
#include "CoreMinimal.h"
#include "UObject/GCObject.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class FCrbTextures;

struct FCrbSurface
{
	int32 Group = 0;      // 0 body (model space, feet origin), 1 first-person hands (view space), 2 world entities (feet origin)
	FString Texture;
	int32 Layer = 0;      // 0 cutout, 1 translucent
	TArray<FVector> V; TArray<FVector2D> UV, UV2; TArray<FLinearColor> C; TArray<FVector> N; TArray<int32> I;
};

struct FCrbPose
{
	int32 Seq = 0, Tick = 0, Epoch = 0;
	double X = 0, Y = 0, Z = 0;
	float WalkPos = 0, WalkSpeed = 0, Attack = 0;
	bool bUsing = false;
	float Yaw = 0, Pitch = 0, WalkDist = 0, WalkDistO = 0, Bob = 0, OBob = 0;
	FString MainHand;
	TArray<FString> EntityTypes;
	TArray<FCrbSurface> Surfaces;
	double ReceivedAt = 0;
	uint32 Hash = 0;
};

class FCrbAvatar : public FGCObject
{
public:
	static constexpr int32 MaxVertices = 36000; // 36000 x 28 B fits the 1 MiB protocol payload
	static constexpr int32 MaxSurfaces = 48;

	void Init(class USceneComponent* WorldRoot, class USceneComponent* CameraRoot, UMaterialInterface* Cutout, UMaterialInterface* Translucent);
	bool OnPose(const TSharedPtr<class FJsonObject>& J, const TArray<uint8>& Bin, FString& Error);
	void Tick(const struct FCrbCoords& Coords, FCrbTextures& Textures, bool bFirstPerson, bool bHideHands, bool bVisible, float DeltaSeconds);
	void Reset();
	void SetLighting(float EmissiveScale, float VanillaWeight, float SunWeight, float Brightness);

	// Diagnostics / test evidence.
	int32 FramesReceived = 0, FramesApplied = 0, Rejected = 0, FallbackSurfaces = 0, TexturedSurfaces = 0;
	int32 LastSeq = 0; float LastWalkPos = 0, LastWalkSpeed = 0; FString LastMainHand;
	uint32 LastBodyHash = 0, LastHandsHash = 0;  // hash of the vertices actually sent to the renderer
	int32 BodyVertexCount = 0, HandVertexCount = 0, EntityVertexCount = 0;
	uint32 LastEntityHash = 0;
	TArray<FString> EntityTextures;
	const TArray<FString>& EntityTypes() const { return Cur.EntityTypes; }
	UProceduralMeshComponent* GetEntities() const { return Entities; }
	double LastPoseTime = 0;
	TArray<FString> HandTextures, BodyTextures;
	UProceduralMeshComponent* GetBody() const { return Body; }
	// Vanilla GameRenderer.bobView as a camera-local transform (identity until poses arrive).
	FTransform ViewBob() const;
	FString DebugSummary(int32 Group) const;
	float LastAlpha = 1.f;
	bool bSuppressPlayer = false; // Custom Avatar add-on: hide groups 0 and 1
	UProceduralMeshComponent* GetHands() const { return Hands; }

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FCrbAvatar"); }

private:
	void Present(UProceduralMeshComponent* Mesh, int32 Group, const FCrbPose& A, const FCrbPose* B, float Alpha, FCrbTextures& Textures, uint32& OutHash, int32& OutVerts, TArray<FString>& OutTex);
	UMaterialInstanceDynamic* MaterialFor(const FString& Tex, int32 Layer, FCrbTextures& Textures, bool& bFallback);
	UProceduralMeshComponent* Body = nullptr;
	UProceduralMeshComponent* Hands = nullptr;
	UProceduralMeshComponent* Entities = nullptr;
	UMaterialInterface* BaseCutout = nullptr;
	UMaterialInterface* BaseTranslucent = nullptr;
	TMap<FString, UMaterialInstanceDynamic*> Mids;
	TMap<FString, class UTexture2D*> BoundTextures;
	UMaterialInstanceDynamic* Fallback[2] = { nullptr, nullptr };
	FCrbPose Prev, Cur; bool bHasCur = false, bHasPrev = false;
	TArray<int32> SectionCounts[3];
};
