#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDB84Probe.generated.h"
class ALDPlayerController;
class ALDResident;
UCLASS()
class ALDB84Probe:public AActor
{
    GENERATED_BODY()
public:
    ALDB84Probe();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
private:
    FString Role,Path;
    TArray<FString> Checks;
    TWeakObjectPtr<ALDPlayerController> A,B;
    TWeakObjectPtr<ALDResident> NPC;
    double Started=0,At=0,PingAt=0,HoldAt=0,MP=0;
    int64 Ready=0;
    int32 Phase=0,Failures=0;
    uint32 Sequence=0;
    bool Sent=false,Checked=false,Done=false,PlayerWet=false,NPCWet=false,PlayerSlow=false,NPCSlow=false,Stunned=false;
    void Check(bool Value,const FString& Message);
    void Enter(int32 Next);
    void ServerTick();
    void ClientTick();
    void Boundaries();
    void Fields();
    void Catalog();
    FString NavigationSignature() const;
    void Finish();
};
