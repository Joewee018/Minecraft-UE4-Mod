// Crossover-Rebuilt bridge protocol (see Docs/PROTOCOL.md). Must match Bridge/src/main/java/crb/Wire.java.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

DECLARE_LOG_CATEGORY_EXTERN(LogCrb, Log, All);

namespace Crb
{
	static constexpr uint32 Magic = 0x31425243; // "CRB1" little-endian
	static constexpr int32 ProtocolVersion = 1;
	static constexpr int32 HeaderBytes = 20;
	static constexpr int32 MaxJsonBytes = 256 * 1024;
	static constexpr int32 MaxBinBytes = 1024 * 1024;

	enum class EType : uint16
	{
		Hello = 1, Welcome = 2, Bye = 3,
		Input = 10, Command = 11, Result = 12,
		State = 20, Section = 21, Window = 22, Model = 23, Texture = 24, Pose = 25,
		Lightmap = 26, Particles = 27, Mods = 28, Event = 30, Icons = 31,
	};

	struct FFrame
	{
		uint16 Type = 0;
		uint16 Flags = 0;
		uint32 Seq = 0;
		FString JsonText;
		TArray<uint8> Bin;
		TSharedPtr<FJsonObject> Json() const;
	};

	// Validates a 20-byte header. Returns false (and a reason) if the frame must be rejected.
	bool ParseHeader(const uint8* H, uint16& Type, uint16& Flags, uint32& Seq, uint32& JsonLen, uint32& BinLen, FString& Error);
	void Encode(uint16 Type, uint32 Seq, const FString& Json, const TArray<uint8>* Bin, TArray<uint8>& Out);
	FString ToJson(const TSharedRef<FJsonObject>& Obj);

	// Little-endian readers with bounds checks for binary payloads.
	struct FReader
	{
		const uint8* Data; int32 Size; int32 Pos = 0; bool bOk = true;
		FReader(const TArray<uint8>& A) : Data(A.GetData()), Size(A.Num()) {}
		FReader(const uint8* D, int32 S) : Data(D), Size(S) {}
		bool Need(int32 N) { if (!bOk || N < 0 || Pos + N > Size) { bOk = false; return false; } return true; }
		uint8 U8() { if (!Need(1)) return 0; return Data[Pos++]; }
		int8 I8() { return (int8)U8(); }
		uint16 U16() { if (!Need(2)) return 0; uint16 V = Data[Pos] | (Data[Pos + 1] << 8); Pos += 2; return V; }
		int16 I16() { return (int16)U16(); }
		uint32 U32() { if (!Need(4)) return 0; uint32 V = Data[Pos] | (Data[Pos + 1] << 8) | (Data[Pos + 2] << 16) | ((uint32)Data[Pos + 3] << 24); Pos += 4; return V; }
		float F32() { uint32 U = U32(); float F; FMemory::Memcpy(&F, &U, 4); return F; }
		bool Finite(float F) const { return FMath::IsFinite(F) && FMath::Abs(F) < 1.0e7f; }
	};
}
