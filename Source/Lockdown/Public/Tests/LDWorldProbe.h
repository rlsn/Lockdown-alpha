#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDWorldProbe.generated.h"
class ALDGameMode;
class ALDPlayerController;
class ALDWorldPawn;
enum class ELDItemKind:uint8;
struct FLDProductionPlan;
UCLASS()
class ALDWorldProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDWorldProbe();
    virtual void Tick(float Delta) override;
private:
    struct FStep{FString Name;double Timeout;TFunction<bool()> Poll;};
    TArray<FStep> Steps;
    TArray<FString> Checks;
    TArray<TSharedPtr<class FJsonValue>> Travel;
    UPROPERTY() TObjectPtr<ALDGameMode> Mode;
    UPROPERTY() TObjectPtr<ALDPlayerController> A;
    UPROPERTY() TObjectPtr<ALDPlayerController> B;
    FString Route;
    int32 Cursor=0,Failures=0;
    double StepAt=-1;
    bool Initialized=false,Done=false;
    uint32 Token=40000,Sequence=0;
    void Check(bool Good,const FString& Message);
    void Finish();
    void Build();
    void Add(const FString& Name,double Timeout,TFunction<bool()> Poll);
    void Wait(double Seconds);
    void Move(ALDPlayerController* PC,TFunction<FVector()> Point,const FString& Name);
    void Hold(ALDPlayerController* PC,uint8 Target,double Seconds);
    void Produce(ALDPlayerController* PC,TFunction<FLDProductionPlan()> Plan,double Seconds);
    void Couriers(int32 Count);
    void Buy(ELDItemKind Kind,uint8 Service=0);
    void Toolkit();
    void Core();
    void Cache(uint8 Site,ELDItemKind Kind);
    void Fight(uint8 Kind,uint8 Index);
    void Boundary();
    void Network();
    int32 NetPhase=0;
    double NetStarted=0,NetAt=0,NetPing=0,NetHold=0;
    bool NetSent=false,NetChecked=false;
    FGuid NetKey;
    int64 NetReady=0;
};
