#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDCombatNetworkProbe.generated.h"
class ALDPlayerController;
UCLASS()
class ALDCombatNetworkProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDCombatNetworkProbe();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
private:
    TWeakObjectPtr<ALDPlayerController> ClientA,ClientB;
    TArray<FString> Checks;
    FString Role;
    int32 Phase=0,Failures=0;
    double Started=0,At=0;
    double LastPing=0;
    bool bSent=false,bChecked=false,bSawWarning=false,bExtra=false;
    uint32 OldLife=0;
    FGuid OldObserved;
    FVector DashStart;
    void Enter(int32 Next);
    void ServerTick();
    void ClientTick();
    void Check(bool Pass,const FString& Text);
    void Finish();
};
