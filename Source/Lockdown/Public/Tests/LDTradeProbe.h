#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDTradeProbe.generated.h"
class ALDPlayerController;class ALDCharacter;
UCLASS()
class LOCKDOWN_API ALDTradeProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDTradeProbe();virtual void BeginPlay() override;virtual void Tick(float Delta) override;
private:
    FString Role,Dir;int32 Phase=0,Step=0,Failures=0;double Started=0,PhaseAt=0,LastPing=0,NextCase=0;
    bool Choice=false,Ready=false,bTerminal=false,bDisconnect=false,bCapture=false;
    uint32 FixtureRequest=200000,HoldSequence=0;FGuid CompletedSession,ASeal,BSeal,ABook,BBook,AWard;
    int64 BeforeA=0,BeforeB=0;
    TWeakObjectPtr<ALDPlayerController> A,B;TWeakObjectPtr<ALDCharacter> Thief;
    TArray<FString> Checks;
    void Check(bool Value,const FString& Text);void Enter(int32 Next);void ServerTick();void ClientTick();void Finish();
    FGuid Draft();void Cmd(ALDPlayerController* PC,uint8 Command,FGuid Item=FGuid(),int64 Gold=0);void Cases(int32 Index);void Capture(const FString& Name);
};
