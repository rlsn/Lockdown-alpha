#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inventory/LDInventoryTypes.h"
#include "LDLoadoutProbe.generated.h"
class ALDPlayerController;
UCLASS()
class ALDLoadoutProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDLoadoutProbe();
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
    void AuthorityEdges();
    uint32 Sequence=0;
    double ClaimStarted=0;
    TMap<ELDItemKind,FGuid> Items;
    double HoldSent=0,SavedCooldown=0,SavedFamily=0,SavedMajor=0,ReturnEndpoint=0;
    int64 BankBefore=0;
    uint32 OldLife=0;
};
