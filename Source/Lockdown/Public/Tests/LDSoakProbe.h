#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDSoakProbe.generated.h"
class ALDPlayerController;
UCLASS()
class ALDSoakProbe : public AActor
{
    GENERATED_BODY()
public:
    ALDSoakProbe();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
private:
    TWeakObjectPtr<ALDPlayerController> A,B;
    TWeakObjectPtr<APawn> LastPawn;
    FVector LastPosition=FVector::ZeroVector;
    FString Role;
    double Started=0,MeasureAt=0,LastFrame=0,LastPing=0,LastAction=0,LastContext=0,LastAudit=0;
    double Seconds=600,FullBytes=0,NetInStart=0,NetOutStart=0;
    uint64 LastRule=0,LastView=0,LastReceived=0;
    uint32 Request=1000,Corrections=0;
    double MaxCorrection=0,BodyPathCm=0;
    bool bStarted=false,bMeasured=false,bEnding=false;
    int32 Failures=0,MinActors=MAX_int32,MaxActors=0;
    TArray<double> Frames,Rules,Views,Combined,SnapshotAges,Memory;
    TArray<FString> Checks;
    void ServerTick(double Now);
    void ClientTick(double Now);
    void Check(bool Pass,const FString& Text);
    void Audit(double Now);
    void Finish(double Now);
};
