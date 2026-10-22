#pragma once
#include "CoreMinimal.h"
#include "Development/LDCharacterBuild.h"
#include "GameFramework/Actor.h"
#include "LDInventoryTypes.generated.h"

UENUM() enum class ELDItemKind:uint8 { Book, WardCharge, Seal };
USTRUCT()
struct FLDInventoryItem
{
    GENERATED_BODY()
    UPROPERTY() FGuid Id;
    UPROPERTY() ELDItemKind Kind=ELDItemKind::Book;
    UPROPERTY() ELDSkill Skill=ELDSkill::None;
    UPROPERTY() uint8 Slot=0;
    UPROPERTY() uint32 Revision=1;
    UPROPERTY() bool bReserved=false;
};
USTRUCT()
struct FLDInventory
{
    GENERATED_BODY()
    UPROPERTY() uint32 Revision=1;
    UPROPERTY() uint8 Capacity=24;
    UPROPERTY() bool bInitialized=false;
    UPROPERTY() TArray<FLDInventoryItem> Items;
    UPROPERTY() TArray<FGuid> Quick;
    FLDInventory(){Quick.SetNum(4);}
    const FLDInventoryItem* Find(FGuid Id) const{return Items.FindByPredicate([&](const auto& I){return I.Id==Id;});}
};
// One server-only physical record for all supported carried and dropped items.
// Holder may also be an NPC. No canonical item actor is replicated to clients.
UCLASS()
class LOCKDOWN_API ALDItem:public AActor
{
    GENERATED_BODY()
public:
    ALDItem();
    FGuid Identity=FGuid::NewGuid();
    ELDItemKind Kind=ELDItemKind::WardCharge;
    ELDSkill Skill=ELDSkill::None;
    TWeakObjectPtr<AActor> Holder;
    uint8 State=0,Slot=0; // dropped, carried, consumed
    uint32 Revision=1;
    bool bReserved=false;
};
namespace LDInventory
{
    enum ECommand:uint8 { Move=1,Sort,Bind,Unbind,Read,Place,Drop,Pickup,Buy };
    LOCKDOWN_API FString Name(ELDItemKind Kind,ELDSkill Skill);
    LOCKDOWN_API bool BindQuick(FLDInventory& Inventory,FGuid Id,uint8 Slot,FString& Reason);
    LOCKDOWN_API void Remove(FLDInventory& Inventory,FGuid Id);
    LOCKDOWN_API bool Valid(const FLDInventory& Inventory);
}
