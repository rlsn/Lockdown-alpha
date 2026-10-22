#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDVendorProbe.generated.h"
class ALDPlayerController;
UCLASS()
class LOCKDOWN_API ALDVendorProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDVendorProbe();virtual void BeginPlay() override;virtual void Tick(float Delta) override;
private:
    FString Role;int32 Phase=0,Step=0,Failures=0;double Started=0,PhaseAt=0,ActionAt=0,LastPing=0;
    bool bChoice=false,bReady=false;uint32 Replay=0,BeforeInventory=0,OldQuote=0;FGuid Session,Book;
    int32 PriorJobs=0;uint8 DestinationSite=0;TWeakObjectPtr<ALDPlayerController> A,B;
    TArray<FString> Checks;
    void Check(bool Pass,const FString& Text);void Enter(int32 Next);void ServerTick();void ClientTick();void Finish();void EarnedTick();
};
