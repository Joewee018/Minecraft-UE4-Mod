// Bounded, chunked texture transfer from Java. PNG decode runs on a worker; UTexture2D creation stays on the game thread.
#pragma once
#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Async/Future.h"

class UTexture2D;

struct FCrbTextureInfo
{
	UTexture2D* Texture = nullptr;
	int32 Width = 0, Height = 0;
	int32 Generation = 0;
	int64 Bytes = 0;
	double ReadyTime = 0;
};

class FCrbTextures : public FGCObject
{
public:
	static constexpr int32 MaxTextureBytes = 64 * 1024 * 1024;   // compressed transfer size (Guns++ makes the block atlas 8192x4096)
	static constexpr int32 MaxDimension = 8192;
	static constexpr int32 MaxTextures = 192; // GUI sheets, atlases, player + mob skins
	static constexpr int64 MaxDecodedBytes = 192ll * 1024 * 1024;
	static constexpr int32 MaxAssemblies = 6;

	~FCrbTextures();
	// Returns false if the frame was malformed (caller counts a rejection; other traffic continues).
	bool OnFrame(const TSharedPtr<class FJsonObject>& J, const TArray<uint8>& Bin, FString& Error);
	void Tick();
	void Reset();
	const FCrbTextureInfo* Find(const FString& Name) const { return Ready.Find(Name); }
	UTexture2D* Get(const FString& Name) const { const FCrbTextureInfo* I = Ready.Find(Name); return I ? I->Texture : nullptr; }
	int32 Version() const { return VersionCounter; }
	int32 NumReady() const { return Ready.Num(); }
	int64 DecodedBytes() const { return TotalDecoded; }
	int32 RejectedCount() const { return Rejected; }
	TArray<FString> Names() const { TArray<FString> K; Ready.GetKeys(K); return K; }
	// Raw BGRA upload (small textures such as the 16x16 light map), game thread only.
	// CPU copy of decoded BGRA for textures registered with KeepPixels() (e.g. the Minecraft font sheet).
	void KeepPixels(const FString& Name) { Keep.Add(Name); }
	const TArray<uint8>* Pixels(const FString& Name) const { return CpuPixels.Find(Name); }
	UTexture2D* UploadRaw(const FString& Name, int32 W, int32 H, const TArray<uint8>& BGRA, bool bSRGB, int32 Generation);

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FCrbTextures"); }

private:
	struct FAssembly { TArray<uint8> Data; int32 Received = 0; int32 Generation = 0; int32 W = 0, H = 0; bool bSRGB = true; double Started = 0; };
	struct FDecoded { FString Name; int32 Generation = 0; int32 W = 0, H = 0; bool bSRGB = true; TArray<uint8> BGRA; FString Error; };
	TSet<FString> Keep;
	TMap<FString, TArray<uint8>> CpuPixels;
	TMap<FString, FAssembly> Assemblies;
	TMap<FString, FCrbTextureInfo> Ready;
	TArray<TFuture<FDecoded>> Decoding;
	int64 TotalDecoded = 0;
	int32 VersionCounter = 0;
	int32 Rejected = 0;
};
