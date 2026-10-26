#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDB81Probe.generated.h"
class ALDPlayerController;
UCLASS()
class ALDB81Probe:public AActor
{
    GENERATED_BODY()
public:
    ALDB81Probe();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
private:
    FString Role;
    TArray<FString> Checks;
    TWeakObjectPtr<ALDPlayerController> A,B;
    FGuid Mail,Charm,Salve,Spare,Helm;
    double Started=0,At=0,HoldAt=0,PingAt=0,MP=0;
    int64 MailReady=0;
    int32 Phase=0,Failures=0;
    uint32 Sequence=0,OldLife=0;
    bool Sent=false,Checked=false,Done=false;
    void Check(bool Value,const FString& Message);
    void Enter(int32 Next);
    void ServerTick();
    void ClientTick();
    void Finish();
};
