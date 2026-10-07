#include "CrbProtocol.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"

DEFINE_LOG_CATEGORY(LogCrb);

namespace Crb
{
	TSharedPtr<FJsonObject> FFrame::Json() const
	{
		TSharedPtr<FJsonObject> Obj;
		if (JsonText.IsEmpty()) return MakeShared<FJsonObject>();
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(R, Obj) || !Obj.IsValid()) return nullptr;
		return Obj;
	}

	static uint32 Le32(const uint8* P) { return P[0] | (P[1] << 8) | (P[2] << 16) | ((uint32)P[3] << 24); }
	static uint16 Le16(const uint8* P) { return P[0] | (P[1] << 8); }

	bool ParseHeader(const uint8* H, uint16& Type, uint16& Flags, uint32& Seq, uint32& JsonLen, uint32& BinLen, FString& Error)
	{
		if (Le32(H) != Magic) { Error = TEXT("bad magic"); return false; }
		Type = Le16(H + 4); Flags = Le16(H + 6); Seq = Le32(H + 8); JsonLen = Le32(H + 12); BinLen = Le32(H + 16);
		if (JsonLen > (uint32)MaxJsonBytes) { Error = FString::Printf(TEXT("json too large (%u)"), JsonLen); return false; }
		if (BinLen > (uint32)MaxBinBytes) { Error = FString::Printf(TEXT("binary too large (%u)"), BinLen); return false; }
		if (Type == 0 || Type > 64) { Error = FString::Printf(TEXT("unknown type %u"), Type); return false; }
		return true;
	}

	static void Put32(TArray<uint8>& O, uint32 V) { O.Add(V & 255); O.Add((V >> 8) & 255); O.Add((V >> 16) & 255); O.Add((V >> 24) & 255); }
	static void Put16(TArray<uint8>& O, uint16 V) { O.Add(V & 255); O.Add((V >> 8) & 255); }

	void Encode(uint16 Type, uint32 Seq, const FString& Json, const TArray<uint8>* Bin, TArray<uint8>& Out)
	{
		FTCHARToUTF8 Utf8(*Json);
		const int32 BinLen = Bin ? Bin->Num() : 0;
		Out.Reset(HeaderBytes + Utf8.Length() + BinLen);
		Put32(Out, Magic); Put16(Out, Type); Put16(Out, 0); Put32(Out, Seq); Put32(Out, Utf8.Length()); Put32(Out, BinLen);
		Out.Append((const uint8*)Utf8.Get(), Utf8.Length());
		if (BinLen) Out.Append(*Bin);
	}

	FString ToJson(const TSharedRef<FJsonObject>& Obj)
	{
		FString S;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&S);
		FJsonSerializer::Serialize(Obj, W);
		return S;
	}
}
