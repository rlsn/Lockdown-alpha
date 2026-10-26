#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDB86Probe.generated.h"
class ALDPlayerController;
UCLASS()
class ALDB86Probe:public AActor
{
    GENERATED_BODY()
public:
    ALDB86Probe();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
private:
    FString Role,Directory;
    TArray<FString> Checks;
    TArray<TWeakObjectPtr<ALDPlayerController>> Owners;
    TArray<double> Frames,Rules,Views,Services,BaselineServices,Bytes,Memory,RTT,Ages;
    TArray<TSharedPtr<class FJsonValue>> Samples;
    double Started=0,At=0,MeasuredAt=0,LastFrame=0,LastSample=0,LastFields=0,LastAudit=0,Seconds=600;
    uint64 RuleSerial=0,ViewSerial=0;
    double PendingRules=0,PendingViews=0,PendingWrites=0;
    uint32 Token=700000,Sequence=0;
    int32 Phase=0,Failures=0,PeakActors=0,MinActors=MAX_int32,FullSamples=0,Cycles=0,ReservationCycles=0;
    int64 NetInStart=0,NetOutStart=0;
    bool Done=false,Visual=false,Sent=false,OwnerVerified=false;
    void Check(bool Value,const FString& Text);
    void ServerTick();
    void ClientTick();
    void VisualTick();
    void FillFields(bool Expiry);
    void Key(FKey Input,bool Down);
    void Capture(const TCHAR* Name);
    void Finish();
};
