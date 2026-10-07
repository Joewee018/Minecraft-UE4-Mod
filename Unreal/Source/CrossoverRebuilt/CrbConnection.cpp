#include "CrbConnection.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformProcess.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	constexpr int32 SoftInboundLimit = 512;   // above this, pose/particle/state frames are dropped (newer ones follow)
	constexpr int32 HardInboundLimit = 8192;  // above this the game thread has stalled; disconnect and resync
	constexpr int32 OutboundLimit = 1024;
	bool IsLowPriority(uint16 T)
	{
		return T == (uint16)Crb::EType::Pose || T == (uint16)Crb::EType::Particles || T == (uint16)Crb::EType::State || T == (uint16)Crb::EType::Lightmap;
	}
}

FCrbConnection::FCrbConnection(const FString& InEndpointPath) : EndpointPath(InEndpointPath) {}

FCrbConnection::~FCrbConnection()
{
	Shutdown();
	FQueued* Q; while (Inbound.Dequeue(Q)) delete Q;
	TArray<uint8>* B; while (Outbound.Dequeue(B)) delete B;
}

void FCrbConnection::Start()
{
	if (Thread) return;
	State.Set((int32)ECrbLinkState::WaitingForEndpoint);
	Thread = FRunnableThread::Create(this, TEXT("CrbBridgeSocket"), 0, TPri_AboveNormal);
}

void FCrbConnection::Shutdown()
{
	bStop.Set(1);
	if (Thread) { Thread->WaitForCompletion(); delete Thread; Thread = nullptr; }
	CloseSocket();
	State.Set((int32)ECrbLinkState::Stopped);
}

FString FCrbConnection::GetLastError() const { FScopeLock L(&ErrorLock); return LastError; }
void FCrbConnection::SetError(const FString& E) { FScopeLock L(&ErrorLock); LastError = E; UE_LOG(LogCrb, Warning, TEXT("Bridge: %s"), *E); }

bool FCrbConnection::Pop(Crb::FFrame& Out, int32& OutEpoch)
{
	FQueued* Q = nullptr;
	if (!Inbound.Dequeue(Q)) return false;
	InboundCount.Decrement();
	Out = MoveTemp(Q->Frame); OutEpoch = Q->Epoch; delete Q;
	return true;
}

void FCrbConnection::Send(Crb::EType Type, const FString& Json, const TArray<uint8>* Bin)
{
	if (GetState() != ECrbLinkState::Connected && Type != Crb::EType::Hello) return;
	if (OutboundCount.GetValue() >= OutboundLimit) { DroppedLowPriority.Increment(); return; }
	TArray<uint8>* Buf = new TArray<uint8>();
	Crb::Encode((uint16)Type, SendSeq++, Json, Bin, *Buf);
	OutboundCount.Increment();
	Outbound.Enqueue(Buf);
}

bool FCrbConnection::ReadEndpoint(FString& Host, int32& Port, FString& Token, FString& Error)
{
	FString Text;
	if (!FPaths::FileExists(EndpointPath)) { Error = TEXT("endpoint file not found: ") + EndpointPath; return false; }
	if (IFileManager::Get().FileSize(*EndpointPath) > 4096) { Error = TEXT("endpoint file too large"); return false; }
	if (!FFileHelper::LoadFileToString(Text, *EndpointPath)) { Error = TEXT("endpoint unreadable"); return false; }
	TSharedPtr<FJsonObject> J; TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(R, J) || !J.IsValid()) { Error = TEXT("endpoint json invalid"); return false; }
	int32 Proto = 0;
	if (!J->TryGetNumberField(TEXT("protocol"), Proto) || Proto != Crb::ProtocolVersion) { Error = FString::Printf(TEXT("endpoint protocol %d != %d"), Proto, Crb::ProtocolVersion); return false; }
	if (!J->TryGetNumberField(TEXT("port"), Port) || Port <= 1024 || Port > 65535) { Error = TEXT("endpoint port invalid"); return false; }
	if (!J->TryGetStringField(TEXT("token"), Token) || Token.Len() != 64) { Error = TEXT("endpoint token invalid"); return false; }
	Host = TEXT("127.0.0.1");
	return true;
}

void FCrbConnection::CloseSocket()
{
	if (Socket)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
}

bool FCrbConnection::SendAll(const uint8* Data, int32 Len)
{
	const double Deadline = FPlatformTime::Seconds() + 3.0;
	int32 Done = 0;
	while (Done < Len)
	{
		if (bStop.GetValue()) return false;
		int32 Sent = 0;
		if (!Socket->Send(Data + Done, Len - Done, Sent)) { SetError(TEXT("send failed")); return false; }
		Done += Sent;
		if (Done < Len)
		{
			if (FPlatformTime::Seconds() > Deadline) { SetError(TEXT("send timeout (peer not reading)")); return false; }
			Socket->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromMilliseconds(20));
		}
	}
	return true;
}

bool FCrbConnection::FlushOutbound()
{
	TArray<uint8>* Buf = nullptr;
	while (Outbound.Dequeue(Buf))
	{
		OutboundCount.Decrement();
		const bool bOk = SendAll(Buf->GetData(), Buf->Num());
		delete Buf;
		if (!bOk) return false;
		FramesOut.Increment();
	}
	return true;
}

bool FCrbConnection::ConnectOnce()
{
	FString Host, Token, Error; int32 Port = 0;
	if (!ReadEndpoint(Host, Port, Token, Error)) { State.Set((int32)ECrbLinkState::WaitingForEndpoint); SetError(Error); return false; }
	State.Set((int32)ECrbLinkState::Connecting);
	ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	TSharedRef<FInternetAddr> Addr = SS->CreateInternetAddr();
	bool bValid = false; Addr->SetIp(*Host, bValid); Addr->SetPort(Port);
	if (!bValid) { SetError(TEXT("bad host")); return false; }
	Socket = SS->CreateSocket(NAME_Stream, TEXT("CrbBridge"), false);
	if (!Socket) { SetError(TEXT("socket create failed")); return false; }
	Socket->SetNoDelay(true);
	int32 Actual = 0; Socket->SetReceiveBufferSize(4 * 1024 * 1024, Actual); Socket->SetSendBufferSize(256 * 1024, Actual);
	if (!Socket->Connect(*Addr)) { SetError(FString::Printf(TEXT("connect to 127.0.0.1:%d failed"), Port)); CloseSocket(); return false; }
	Socket->SetNonBlocking(true);
	State.Set((int32)ECrbLinkState::Handshaking);
	// HELLO is written directly by this thread (game thread has not seen the connection yet).
	TSharedRef<FJsonObject> Hello = MakeShared<FJsonObject>();
	Hello->SetStringField(TEXT("token"), Token);
	Hello->SetNumberField(TEXT("protocol"), Crb::ProtocolVersion);
	Hello->SetStringField(TEXT("host"), TEXT("CrossoverRebuilt UE4.27"));
	TArray<uint8> Bytes; Crb::Encode((uint16)Crb::EType::Hello, 0, Crb::ToJson(Hello), nullptr, Bytes);
	if (!SendAll(Bytes.GetData(), Bytes.Num())) { CloseSocket(); return false; }
	return true;
}

void FCrbConnection::ReadLoop()
{
	RecvBuf.Reset();
	TArray<uint8> Chunk; Chunk.SetNumUninitialized(256 * 1024);
	bool bWelcomed = false;
	const double HandshakeDeadline = FPlatformTime::Seconds() + 5.0;
	while (!bStop.GetValue())
	{
		if (ReconnectRequested.GetValue()) { ReconnectRequested.Set(0); SetError(TEXT("reconnect requested")); return; }
		if (bWelcomed && !FlushOutbound()) return;
		if (!bWelcomed && FPlatformTime::Seconds() > HandshakeDeadline) { SetError(TEXT("handshake timeout")); return; }
		if (!Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(5))) continue;
		int32 Read = 0;
		if (!Socket->Recv(Chunk.GetData(), Chunk.Num(), Read)) { SetError(TEXT("connection closed by Minecraft")); return; }
		if (Read == 0)
		{
			// Readable with zero bytes means orderly close.
			uint32 Pending = 0;
			if (!Socket->HasPendingData(Pending)) { SetError(TEXT("connection closed")); return; }
			continue;
		}
		BytesIn.Add(Read);
		RecvBuf.Append(Chunk.GetData(), Read);
		int32 Offset = 0;
		while (RecvBuf.Num() - Offset >= Crb::HeaderBytes)
		{
			uint16 Type, Flags; uint32 Seq, JsonLen, BinLen; FString Error;
			if (!Crb::ParseHeader(RecvBuf.GetData() + Offset, Type, Flags, Seq, JsonLen, BinLen, Error)) { Rejected.Increment(); SetError(TEXT("rejected frame: ") + Error); return; }
			const int64 Total = (int64)Crb::HeaderBytes + JsonLen + BinLen;
			if (RecvBuf.Num() - Offset < Total) break;
			const uint8* P = RecvBuf.GetData() + Offset + Crb::HeaderBytes;
			FQueued* Q = new FQueued();
			Q->Epoch = Epoch.GetValue();
			Q->Frame.Type = Type; Q->Frame.Flags = Flags; Q->Frame.Seq = Seq;
			{ FUTF8ToTCHAR Conv((const ANSICHAR*)P, JsonLen); Q->Frame.JsonText = FString(Conv.Length(), Conv.Get()); }
			Q->Frame.Bin.Append(P + JsonLen, BinLen);
			Offset += (int32)Total;
			FramesIn.Increment();
			if (Type == (uint16)Crb::EType::Welcome && !bWelcomed)
			{
				bWelcomed = true;
				State.Set((int32)ECrbLinkState::Connected);
			}
			const int32 Count = InboundCount.GetValue();
			if (Count > HardInboundLimit) { delete Q; SetError(TEXT("inbound backlog exceeded; resyncing")); return; }
			if (Count > SoftInboundLimit && IsLowPriority(Type)) { delete Q; DroppedLowPriority.Increment(); continue; }
			InboundCount.Increment();
			Inbound.Enqueue(Q);
		}
		if (Offset > 0) RecvBuf.RemoveAt(0, Offset, false);
		if (RecvBuf.Num() > Crb::HeaderBytes + Crb::MaxJsonBytes + Crb::MaxBinBytes) { SetError(TEXT("receive buffer overflow")); return; }
	}
}

uint32 FCrbConnection::Run()
{
	while (!bStop.GetValue())
	{
		if (ConnectOnce())
		{
			Epoch.Increment();
			ReadLoop();
			CloseSocket();
			// Drop anything the game thread did not send yet; the new session resynchronises.
			TArray<uint8>* B; while (Outbound.Dequeue(B)) { OutboundCount.Decrement(); delete B; }
			Reconnects.Increment();
		}
		if (bStop.GetValue()) break;
		State.Set((int32)ECrbLinkState::WaitingForEndpoint);
		for (int32 I = 0; I < 10 && !bStop.GetValue(); ++I) FPlatformProcess::Sleep(0.1f);
	}
	return 0;
}
