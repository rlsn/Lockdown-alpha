#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDObjectiveNetworkProbe.generated.h"
class ALDPlayerController;
class ALDSeal;
UCLASS()
class ALDObjectiveNetworkProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDObjectiveNetworkProbe();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
private:
    TWeakObjectPtr<ALDPlayerController> A,B,Carrier,Thief;
    TWeakObjectPtr<ALDSeal> Item;
    FGuid ItemId,Result;
    FString Role,Scenario;
    TArray<FString> Checks;
    int32 Phase=0,Failures=0;
    double Started=0,At=0,WorldAt=0,LastHold=-1000,LastPing=0;
    uint32 Token=0,Sequence=0,OriginalLife=0;
    bool bSent=false,bChecked=false,bExtra=false,bHold=false,bSawChannel=false;
    void Check(bool Pass,const FString& Text);
    void Enter(int32 Next);
    void ServerTick();
    void ClientTick();
    void Send(uint8 Target,FGuid Observed=FGuid());
    void Finish();
};
