// Physics & Portal mod developer menu (F7, only when bPPDebugMode is on; compiled out of Shipping builds): tabs for
// Physics, Portals, Steve and Testing, a live monitor, world debug drawing and "pp.*" console commands. It drives the
// mod only through its debug interfaces: the "pp.debug" bridge op (Java crb.client.pp.PPController), the server ops
// (pp.shoot / pp.portal / pp.object / pp.prop / pp.home ...) and the host's UE-side objects.
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CrbPPDebug.generated.h"

class ACrbHost;
class SWidget;
class SVerticalBox;
class FJsonObject;

UCLASS()
class UCrbPPDebugComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UCrbPPDebugComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	UPROPERTY() ACrbHost* Host = nullptr;
	void TogglePanel();
	void SetPanelOpen(bool bOpen);
	bool IsPanelOpen() const { return bPanel; }
	void SetMonitor(bool bOn);
	bool IsMonitorShown() const { return bMonitor; }
	void SetTab(int32 T);

	// actions shared by the panel, the console and the tests
	void Cmd(const FString& Command, const TSharedPtr<FJsonObject>& Args = nullptr);
	void Op(const FString& OpName, const TSharedPtr<FJsonObject>& Args = nullptr);
	void Tune(const FString& Key, double Value);
	void TuneStep(const FString& Key, int32 Dir);
	void SetFlag(const FString& Key, bool bOn);
	void SetModifier(const FString& Name);
	void SetTimeScale(float S);
	void SetFrozen(bool bOn);
	void TestPortalMomentum();
	void TestPortalOrientation();
	void CycleAnimState();

	static constexpr int32 NumViz = 5;
	bool Viz[NumViz] = { true, true, true, true, true };
	int32 Tab = 0, DrawnLastFrame = 0, CommandsSent = 0;
	FString LastMessage;
	TSharedPtr<FJsonObject> Config;
	double ConfigValue(const FString& Key, double Def = 0) const;
	bool ConfigFlag(const FString& Key) const;
	FString MonitorText() const;
	FString TabText(int32 T) const;

private:
	void BuildPanel();
	void RebuildContent();
	void DrawViz();
	bool bPanel = false, bMonitor = false, bDirty = false;
	TSharedPtr<SWidget> PanelRoot, MonitorRoot;
	TSharedPtr<SVerticalBox> Content;
	TArray<FString> Pending;
};
