#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inventory/LDInventoryTypes.h"
#include "LDInventoryDirector.generated.h"
class ALDPlayerController;
class ALDCharacter;
enum class ELDSightResult : uint8;
enum class ELDWorldQueryFailure : uint8;
namespace LDInventoryDrop
{
    enum class ECheck : uint8 { Accept, Refuse, Fault };
    // The requested drop point is client input, so its own ground failures refuse. A failure without a code, or of the
    // registered world itself (missing/stale binding, body capsule, registered surface), is an authority fault.
    LOCKDOWN_API ECheck FromGround(bool bLegal, ELDWorldQueryFailure Failure);
    // Sight from the actual body to already validated ground follows observation: off-world or stale results fault.
    LOCKDOWN_API ECheck FromSight(ELDSightResult Sight);
}
namespace LDInventoryRequest
{
    enum class EResult : uint8 { Accepted, StaleInventory, StaleItem, ReservedItem, ReservedBag, InvalidSlot, Unsupported };
    // The authoritative physical item behind a request, as the director found it among the caller's holdings.
    struct FItemFacts
    {
        bool bOwned = false;
        uint32 Revision = 0;
        bool bReserved = false;
        uint8 Slot = 0;
    };
    // Admission shared by every executed command (Move, Sort, Drop): the inventory revision, then for an item
    // command its revision and reservation, all decided before any ownership, reference or version change.
    LOCKDOWN_API EResult Admit(const FLDInventory& Inventory, uint8 Command, uint32 ExpectedInventoryRevision,
        uint32 ExpectedItemRevision, const FItemFacts& Item);
    // Move or Sort on the projection: admitted, no reserved entry, slot inside capacity; then every slot and one
    // revision change together. A refusal leaves the projection untouched.
    LOCKDOWN_API EResult Organize(FLDInventory& Inventory, uint8 Command, uint32 ExpectedInventoryRevision,
        const FGuid& ItemId, uint32 ExpectedItemRevision, const FItemFacts& Item, uint8 Slot);
}
UCLASS(Config=Game)
class LOCKDOWN_API ALDInventoryDirector:public AActor
{
    GENERATED_BODY()
public:
    ALDInventoryDirector(){bReplicates=false;}
    UPROPERTY(Config) int32 Capacity=24;
    void Initialize(ALDPlayerController* PC);
    bool HasSpace(ALDPlayerController* PC,ELDItemKind Kind,FString* Reason=nullptr) const;
    ALDItem* Owned(AActor* Container,FGuid Id) const;
    bool Adopt(ALDPlayerController* PC,ALDItem* Item,FName CreationCause=NAME_None,bool Journal=true);
    void Remove(ALDPlayerController* PC,ALDItem* Item,bool Consume,FName Cause=NAME_None);
    void Changed(ALDPlayerController* PC);
    void Queue(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Request,uint32 InventoryRevision,uint8 Command,FGuid Item,uint32 ItemRevision,uint8 Slot,FVector Point);
    void Sample(double Now);
    bool Complete(ALDPlayerController* PC,uint32 Request,const FString& Reason);
private:
    struct FRequest{TWeakObjectPtr<ALDPlayerController> PC;uint32 Match,Life,Id,InventoryRevision;uint8 Command;FGuid Item;uint32 ItemRevision;uint8 Slot;FVector Point;};
    struct FClient{uint32 Last=0;double At=0,Tokens=8;TMap<uint32,FString> Receipts;};
    TMap<TWeakObjectPtr<ALDPlayerController>,FClient> Clients;
    TArray<FRequest> Requests;
    void Request(const FRequest& R);
    void Reply(ALDPlayerController* PC,uint32 Id,const FString& Reason);
    bool Eligible(ALDPlayerController* PC,ALDCharacter*& Body,FString& Why) const;
};
