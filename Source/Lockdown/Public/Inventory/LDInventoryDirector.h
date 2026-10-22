#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inventory/LDInventoryTypes.h"
#include "LDInventoryDirector.generated.h"
class ALDPlayerController;
UCLASS(Config=Game)
class LOCKDOWN_API ALDInventoryDirector:public AActor
{
    GENERATED_BODY()
public:
    ALDInventoryDirector(){bReplicates=false;}
    UPROPERTY(Config) int32 Capacity=24;
    void Initialize(ALDPlayerController* PC);
    bool HasSpace(ALDPlayerController* PC,ELDItemKind Kind) const;
    ALDItem* Owned(AActor* Container,FGuid Id) const;
    bool Adopt(ALDPlayerController* PC,ALDItem* Item);
    void Remove(ALDPlayerController* PC,ALDItem* Item,bool Consume);
    void Changed(ALDPlayerController* PC);
    void EndLife(ALDPlayerController* PC);
    void QuotePickup(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Token,FGuid Observed);
    void Queue(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Request,uint32 InventoryRevision,uint8 Command,FGuid Item,uint32 ItemRevision,uint8 Slot,FVector Point);
    void Sample(double Now);
    bool Complete(ALDPlayerController* PC,uint32 Request,const FString& Reason);
    void FailNextWardForProbe();
private:
    friend class ALDInventoryProbe;
    struct FRequest{TWeakObjectPtr<ALDPlayerController> PC;uint32 Match,Life,Id,InventoryRevision;uint8 Command;FGuid Item;uint32 ItemRevision;uint8 Slot;FVector Point;};
    struct FClient{uint32 Last=0;double At=0,Tokens=8;TMap<uint32,FString> Receipts;};
    TMap<TWeakObjectPtr<ALDPlayerController>,FClient> Clients;
    TArray<FRequest> Requests;
    bool bFailWard=false;
    void Request(const FRequest& R,double Now);
    void Reply(ALDPlayerController* PC,uint32 Id,const FString& Reason);
    bool Eligible(ALDPlayerController* PC,double Now) const;
};
