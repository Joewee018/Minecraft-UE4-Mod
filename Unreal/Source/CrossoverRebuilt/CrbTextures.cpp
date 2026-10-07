#include "CrbTextures.h"
#include "CrbProtocol.h"
#include "Engine/Texture2D.h"
#include "Async/Async.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

FCrbTextures::~FCrbTextures()
{
	for (auto& F : Decoding) if (F.IsValid()) F.Wait();
}

void FCrbTextures::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (auto& KV : Ready) if (KV.Value.Texture) Collector.AddReferencedObject(KV.Value.Texture);
}

void FCrbTextures::Reset()
{
	for (auto& F : Decoding) if (F.IsValid()) F.Wait();
	Decoding.Reset(); Assemblies.Reset(); Ready.Reset(); TotalDecoded = 0; ++VersionCounter;
}

bool FCrbTextures::OnFrame(const TSharedPtr<FJsonObject>& J, const TArray<uint8>& Bin, FString& Error)
{
	FString Name; int32 Gen = 0, W = 0, H = 0, Total = 0, Offset = 0; bool bLast = false, bLinear = false;
	if (!J.IsValid() || !J->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty() || Name.Len() > 200) { Error = TEXT("texture name"); ++Rejected; return false; }
	J->TryGetNumberField(TEXT("gen"), Gen); J->TryGetNumberField(TEXT("w"), W); J->TryGetNumberField(TEXT("h"), H);
	J->TryGetNumberField(TEXT("total"), Total); J->TryGetNumberField(TEXT("offset"), Offset); J->TryGetBoolField(TEXT("last"), bLast);
	J->TryGetBoolField(TEXT("linear"), bLinear);
	if (W <= 0 || H <= 0 || W > MaxDimension || H > MaxDimension || Total <= 0 || Total > MaxTextureBytes || Offset < 0 || Offset + Bin.Num() > Total)
	{
		Error = FString::Printf(TEXT("texture %s bounds (w=%d h=%d total=%d off=%d len=%d)"), *Name, W, H, Total, Offset, Bin.Num()); ++Rejected; Assemblies.Remove(Name); return false;
	}
	FAssembly* A = Assemblies.Find(Name);
	if (Offset == 0)
	{
		if (!A && Assemblies.Num() >= MaxAssemblies)
		{
			// Evict the oldest partial transfer instead of blocking other traffic.
			FString Oldest; double T = DBL_MAX;
			for (auto& KV : Assemblies) if (KV.Value.Started < T) { T = KV.Value.Started; Oldest = KV.Key; }
			Assemblies.Remove(Oldest); ++Rejected;
		}
		A = &Assemblies.Add(Name);
		A->Data.SetNumUninitialized(Total); A->Received = 0; A->Generation = Gen; A->W = W; A->H = H; A->bSRGB = !bLinear; A->Started = FPlatformTime::Seconds();
	}
	if (!A || A->Generation != Gen || A->Data.Num() != Total || A->Received != Offset) { Error = TEXT("texture chunk out of order: ") + Name; ++Rejected; Assemblies.Remove(Name); return false; }
	FMemory::Memcpy(A->Data.GetData() + Offset, Bin.GetData(), Bin.Num());
	A->Received += Bin.Num();
	if (!bLast) return true;
	if (A->Received != Total) { Error = TEXT("texture truncated: ") + Name; ++Rejected; Assemblies.Remove(Name); return false; }

	TArray<uint8> Png = MoveTemp(A->Data);
	const bool bSRGB = A->bSRGB;
	Assemblies.Remove(Name);
	IImageWrapperModule* Module = &FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	Decoding.Add(Async(EAsyncExecution::ThreadPool, [Module, Name, Gen, W, H, bSRGB, Png = MoveTemp(Png)]() {
		FDecoded D; D.Name = Name; D.Generation = Gen; D.W = W; D.H = H; D.bSRGB = bSRGB;
		TSharedPtr<IImageWrapper> Wrapper = Module->CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Png.GetData(), Png.Num())) { D.Error = TEXT("png header"); return D; }
		if (Wrapper->GetWidth() != W || Wrapper->GetHeight() != H) { D.Error = TEXT("png dimensions disagree"); return D; }
		TArray<uint8> Raw;
		if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw) || Raw.Num() != W * H * 4) { D.Error = TEXT("png decode"); return D; }
		D.BGRA = MoveTemp(Raw);
		return D;
	}));
	return true;
}

UTexture2D* FCrbTextures::UploadRaw(const FString& Name, int32 W, int32 H, const TArray<uint8>& BGRA, bool bSRGB, int32 Generation)
{
	check(IsInGameThread());
	if (W <= 0 || H <= 0 || W > MaxDimension || H > MaxDimension || BGRA.Num() != W * H * 4) { ++Rejected; return nullptr; }
	FCrbTextureInfo* Existing = Ready.Find(Name);
	UTexture2D* Tex = (Existing && Existing->Texture && Existing->Width == W && Existing->Height == H) ? Existing->Texture : nullptr;
	if (!Tex)
	{
		if (!Existing && Ready.Num() >= MaxTextures) { ++Rejected; return nullptr; }
		if (Existing) TotalDecoded -= Existing->Bytes;
		if (TotalDecoded + BGRA.Num() > MaxDecodedBytes) { ++Rejected; return nullptr; }
		Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
		if (!Tex) { ++Rejected; return nullptr; }
		Tex->SRGB = bSRGB; Tex->Filter = TF_Nearest; Tex->NeverStream = true; Tex->LODGroup = TEXTUREGROUP_Pixels2D;
		Tex->AddressX = TA_Clamp; Tex->AddressY = TA_Clamp;
		FCrbTextureInfo& Info = Ready.Add(Name);
		Info.Texture = Tex; Info.Width = W; Info.Height = H; Info.Bytes = BGRA.Num();
		TotalDecoded += BGRA.Num();
	}
	void* Dst = Tex->PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Dst, BGRA.GetData(), BGRA.Num());
	Tex->PlatformData->Mips[0].BulkData.Unlock();
	Tex->UpdateResource();
	if (Keep.Contains(Name)) CpuPixels.Add(Name, BGRA);
	FCrbTextureInfo& Info = Ready.FindChecked(Name);
	Info.Generation = Generation; Info.ReadyTime = FPlatformTime::Seconds();
	++VersionCounter;
	return Tex;
}

void FCrbTextures::Tick()
{
	// Apply at most two finished decodes per frame to bound game-thread upload cost.
	int32 Applied = 0;
	for (int32 I = 0; I < Decoding.Num() && Applied < 2; )
	{
		if (!Decoding[I].IsReady()) { ++I; continue; }
		FDecoded D = Decoding[I].Get();
		Decoding.RemoveAtSwap(I);
		if (!D.Error.IsEmpty()) { ++Rejected; UE_LOG(LogCrb, Warning, TEXT("Texture %s rejected: %s (other bridge traffic continues)"), *D.Name, *D.Error); continue; }
		const FCrbTextureInfo* Existing = Ready.Find(D.Name);
		if (Existing && Existing->Generation > D.Generation) continue; // stale
		// Recreate when dimensions change.
		if (Existing && (Existing->Width != D.W || Existing->Height != D.H)) { TotalDecoded -= Existing->Bytes; Ready.Remove(D.Name); }
		UploadRaw(D.Name, D.W, D.H, D.BGRA, D.bSRGB, D.Generation);
		++Applied;
	}
}
