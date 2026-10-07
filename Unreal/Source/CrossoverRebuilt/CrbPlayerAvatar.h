// God of War Unity port: the player (SK_Avatar - the repo's Kratos, private build), the Mutant enemies (SK_Mutant) and
// the axe (SM_LeviathanAxe, procedural fallback), imported by Scripts/CreateAvatar.py into /Game/Crb/Avatar. Pure presentation: Java remains authoritative for position, combat and the thrown axe;
// this only places the mesh at the interpolated Java feet and drives UCrbAvatarAnimInstance from Java state.
#pragma once
#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "CrbAvatarAnim.h"

class USkeletalMeshComponent;
class USkeletalMesh;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UAnimSequence;

// God of War Unity port: a Mutant enemy as exported by Java (StateExporter "mutants").
struct FCrbMutantState { int32 Id = 0; double X = 0, Y = 0, Z = 0; float Yaw = 0, Health = 0, MaxHealth = 100; int32 DeathTime = 0, HurtTime = 0; };

struct FCrbAxeView { bool bActive = false; int32 Phase = 0; FVector Pos = FVector::ZeroVector; float Spin = 0, Yaw = 0, Travelled = 0; int32 Hits = 0; };

class FCrbPlayerAvatar : public FGCObject
{
public:
	static constexpr const TCHAR* MeshPath = TEXT("/Game/Crb/Avatar/SK_Avatar.SK_Avatar");      // player
	static constexpr const TCHAR* EnemyMeshPath = TEXT("/Game/Crb/Avatar/SK_Mutant.SK_Mutant"); // enemy
	static constexpr const TCHAR* AxeMeshPath = TEXT("/Game/Crb/Avatar/SM_LeviathanAxe.SM_LeviathanAxe");
	void Init(AActor* Owner, USceneComponent* Root, UMaterialInterface* VertexColorMat);
	bool AssetsReady() const { return Mesh != nullptr; }
	// Feet in UE space, facing yaw (MC == UE yaw).
	void Tick(bool bEnabled, const FVector& Feet, float FacingYaw, const FCrbAvatarAnimInputs& Inputs, const FCrbAxeView& Axe, float Dt);
	USkeletalMeshComponent* GetMesh() const { return Comp; }
	// Enemies: one pooled skeletal mesh per Mutant near the player, placed at the interpolated Java position.
	struct FEnemy { int32 Id = 0; USkeletalMeshComponent* Comp = nullptr; FVector Prev = FVector::ZeroVector, Cur = FVector::ZeroVector; double At = 0; float Yaw = 0, Health = 0, MaxHealth = 100; int32 DeathTime = 0; FCrbAvatarAnimInputs Anim; bool bLive = false; };
	TArray<FEnemy> Enemies;
	void TickEnemies(const TArray<FCrbMutantState>& Mutants, const struct FCrbCoords& Coords, float Dt);
	int32 VisibleEnemies() const;
	float ClipLength(ECrbClip C) const;
	void SetBrightness(float Emissive, float LitWeight); // world-matched shading: 'Brightness' (emissive radiance) + 'LitWeight'
	float CurrentBrightness() const { return LastBrightness; }
	USceneComponent* GetAxe() const { return AxeComp; }
	bool HasLeviathanAxe() const { return AxeStatic != nullptr && AxeComp == (USceneComponent*)AxeStatic; }
	const FCrbAvatarAnimDebug* AnimDebug() const;
	FVector BoneLocation(const TCHAR* BaseName) const;   // Mixamo base name, e.g. "Head", "RightHand"
	FName BoneName(const TCHAR* BaseName) const;
	uint32 SkeletonHash() const;   // hash of bone names + parents (test evidence that the imported skeleton is the Mixamo one)
	int32 NumBones() const;
	TArray<FString> MaterialReport; // what each mesh slot renders with (diagnostics/tests)
	int32 ClipsLoaded = 0; TArray<FString> MissingClips; FString LoadError;
	bool bAxeInHand = true; FVector AxeWorld = FVector::ZeroVector;
	float MeshYawOffset = -90.f; // Blender -Y forward imports facing UE +Y

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FCrbPlayerAvatar"); }

private:
	void BuildAxe();
	USkeletalMeshComponent* MakeMeshComponent(const TCHAR* Name, USkeletalMesh* ForMesh);
	AActor* OwnerActor = nullptr; USceneComponent* RootComp = nullptr;
	USkeletalMesh* Mesh = nullptr;
	USkeletalMesh* EnemyMesh = nullptr;
	UStaticMeshComponent* AxeStatic = nullptr;
	USceneComponent* AxeComp = nullptr;
	USkeletalMeshComponent* Comp = nullptr;
	UProceduralMeshComponent* AxeMesh = nullptr;
	UMaterialInterface* AxeMat = nullptr;
	UAnimSequence* Clips[(int32)ECrbClip::Count] = {};
	float SmoothedYaw = 0; bool bYawInit = false; float LastBrightness = -1.f, LastLitWeight = 1.f;
	FVector AxePrev = FVector::ZeroVector, AxeCur = FVector::ZeroVector; double AxeAt = 0; float AxeSpin = 0;
};
