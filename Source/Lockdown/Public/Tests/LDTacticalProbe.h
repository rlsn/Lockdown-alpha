#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDTacticalProbe.generated.h"
class ALDPlayerController;
UCLASS()
class ALDTacticalProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDTacticalProbe();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
private:
    FString Role;
    TArray<FString> Checks;
    TWeakObjectPtr<ALDPlayerController> A,B;
    double Started=0,At=0,LastPing=0;
    int32 Phase=0,Failures=0;
    bool Sent=false,Checked=false;
    void Check(bool Value,const FString& Message);
    void Enter(int32 Next);
    void ServerTick();
    void ClientTick();
    void Finish();
};
