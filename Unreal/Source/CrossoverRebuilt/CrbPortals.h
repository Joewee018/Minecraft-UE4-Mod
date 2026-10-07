// Physics & Portal mod: portal rendering and Unreal-side physics objects.
// Each linked portal is a 1 x 2 block oval (M_Portal) showing a live view rendered by a SceneCapture2D placed where the
// player's camera would be if it had walked through the portal (the same frame mapping Java uses for momentum), with
// the near clip on the exit surface. Captures run only for portals in front of the camera and within range, at a
// capped rate, into half-resolution render targets. FCrbPPCubes are UE4 physics actors (simulated cubes) that collide
// with pooled Minecraft block colliders and go through the portals with their velocity transformed.
#pragma once
#include "CoreMinimal.h"
#include "UObject/GCObject.h"

class ACrbHost;
class UStaticMeshComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;
class UStaticMesh;

/** Portal frame in UE space (unit vectors; centre in UE units). Local = (along R, along U, along N). */
struct FCrbPortalFrame
{
	bool bValid = false; int32 Id = 0;
	FVector C = FVector::ZeroVector, N = FVector::ForwardVector, U = FVector::UpVector, R = FVector::RightVector;
	FVector Local(const FVector& P) const { const FVector D = P - C; return FVector(FVector::DotProduct(D, R), FVector::DotProduct(D, U), FVector::DotProduct(D, N)); }
	FVector MapPointTo(const FCrbPortalFrame& B, const FVector& P) const { const FVector L = Local(P); return B.C - B.R * L.X + B.U * L.Y - B.N * L.Z; }
	FVector MapDirTo(const FCrbPortalFrame& B, const FVector& V) const
	{
		const float X = FVector::DotProduct(V, R), Y = FVector::DotProduct(V, U), Z = FVector::DotProduct(V, N);
		return -B.R * X + B.U * Y - B.N * Z;
	}
};

class FCrbPortalViews : public FGCObject
{
public:
	void Init(AActor* Owner, USceneComponent* Root);
	/** Frames come from Java's export each tick; the camera is the player's view this frame. */
	void Tick(ACrbHost* Host, bool bOn, const FCrbPortalFrame Frames[2], const FVector& CamPos, const FRotator& CamRot, float Fov, float Dt);
	int32 Captures = 0, Visible = 0; bool bMaterialReady = false;
	FCrbPortalFrame Frames[2];
	UStaticMeshComponent* Planes[2] = {};
	USceneCaptureComponent2D* Caps[2] = {};
	UTextureRenderTarget2D* Targets[2] = {};
	UMaterialInstanceDynamic* Mids[2] = {};
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FCrbPortalViews"); }
private:
	double LastCapture[2] = { 0, 0 };
};

class FCrbPPCubes : public FGCObject
{
public:
	void Init(AActor* Owner, USceneComponent* Root);
	void Spawn(const FVector& At, const FVector& Velocity);
	void Clear();
	void Tick(ACrbHost* Host, const FCrbPortalFrame Frames[2], float Dt);
	int32 Num() const;
	int32 Teleports = 0;
	TArray<UStaticMeshComponent*> Cubes;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FCrbPPCubes"); }
private:
	AActor* Owner = nullptr; USceneComponent* Root = nullptr; UStaticMesh* CubeMesh = nullptr; UMaterialInstanceDynamic* Mat = nullptr;
	TArray<double> Cooldown;
};
