#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDInventoryProbe.generated.h"
class ALDPlayerController;class ALDItem;
UCLASS()
class LOCKDOWN_API ALDInventoryProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDInventoryProbe();virtual void BeginPlay() override;virtual void Tick(float Delta) override;
private:
    FString Role;int32 Phase=0,Step=0,Failures=0;double Started=0,PhaseAt=0,LastPing=0;bool bChoice=false,bReady=false;
    int32 MaxViewBytes=0;
    TWeakObjectPtr<ALDPlayerController> A,B;
    TArray<FGuid> InitialBooks,InitialWards,DuplicateBooks,DeathCharges;
    FGuid Book,Ward;uint32 InitialBuild=0,Replay=0,BeforeInventory=0,BeforeItem=0;
    TArray<FString> Checks;
    void Check(bool Pass,const FString& Text);void Enter(int32 Next);void ServerTick();void ClientTick();void Finish();
    void Key(FKey Input,bool Down);void FullBagCases();
};
