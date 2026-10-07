// Socket worker for the Crossover-Rebuilt bridge. Owns no UObjects; game thread exchanges plain frames via bounded queues.
#pragma once
#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeCounter.h"
#include "Containers/Queue.h"
#include "CrbProtocol.h"

class FSocket;
class FRunnableThread;

enum class ECrbLinkState : int32 { Idle, WaitingForEndpoint, Connecting, Handshaking, Connected, Stopped };

class FCrbConnection : public FRunnable
{
public:
	explicit FCrbConnection(const FString& InEndpointPath);
	virtual ~FCrbConnection();

	void Start();
	void Shutdown();

	// Game thread API.
	bool Pop(Crb::FFrame& Out, int32& OutEpoch);
	void Send(Crb::EType Type, const FString& Json, const TArray<uint8>* Bin = nullptr);
	ECrbLinkState GetState() const { return (ECrbLinkState)State.GetValue(); }
	int32 GetEpoch() const { return Epoch.GetValue(); }
	FString GetLastError() const;
	void RequestReconnect() { ReconnectRequested.Set(1); }

	FThreadSafeCounter FramesIn, FramesOut, Rejected, DroppedLowPriority, Reconnects;
	FThreadSafeCounter64 BytesIn;

	// FRunnable
	virtual uint32 Run() override;
	virtual void Stop() override { bStop.Set(1); }

private:
	struct FQueued { Crb::FFrame Frame; int32 Epoch; };
	bool ReadEndpoint(FString& Host, int32& Port, FString& Token, FString& Error);
	bool ConnectOnce();
	void ReadLoop();
	bool FlushOutbound();
	bool SendAll(const uint8* Data, int32 Len);
	void SetError(const FString& E);
	void CloseSocket();

	FString EndpointPath;
	FSocket* Socket = nullptr;
	FRunnableThread* Thread = nullptr;
	FThreadSafeCounter bStop, State, Epoch, InboundCount, ReconnectRequested;
	TQueue<FQueued*, EQueueMode::Spsc> Inbound;
	TQueue<TArray<uint8>*, EQueueMode::Spsc> Outbound;
	FThreadSafeCounter OutboundCount;
	mutable FCriticalSection ErrorLock;
	FString LastError;
	TArray<uint8> RecvBuf;
	uint32 SendSeq = 1;
};
