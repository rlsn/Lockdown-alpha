#pragma once
#include "CoreMinimal.h"
#include "Development/LDCharacterBuild.h"
#include "GameFramework/Actor.h"
#include "LDInventoryTypes.generated.h"

// Append only: existing item and offered-item wire values remain stable.
UENUM() enum class ELDItemKind:uint8 { Book, WardCharge, Seal, Fiber, Ore, Crystal, Toolkit, Core, NullBell, IronHalberd, SimpleStaff, RunicStaff, ConduitStaff, BastionPlate, ArchivistRobe, MerchantHarness, PathfinderBoots, ScholarsSigil, WardingEye, WeightedSatchel, Watchblade, SurveyorBow, OathMace, NullDagger, ShadeMantle, ReflectiveMail, SilentSoles, Cliffwalkers, AnchorGreaves, BrokersSeal, IronOath, SmokeFlask, SeersDust, HealingSalve, ManaDraught, WardTotem, RecallToken, SnareMine, Counterglyph, MirrorSigil, PurityCharm, AegisFlask, GrapplingChain, BlinkCharm, NullStaff, RiftStaff, HarpoonHalberd, TowerRelic, ArcaneCatalyst, HeartFragment, AncientKeyA, AncientKeyB, AncientKeyC, Heartbreaker, IronHelm=54, ScholarsHood=55, ScoutsCowl=56, ReinforcedLeggings=60, RunnersTrousers=61, LaborersGrip=67, IronLongsword=68, ArcaneFocus=75, DuelistsParryingDagger=77, WayfarerBoots=79, ChannelersVest=81, CleansingCharm=82, MirrorAmulet=83, ArcaneLens=86, RiverWaders=63, RiverTreads=78, FrostPrism=89, WaterFlask=90, FrostCapsule=92 };
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
    UPROPERTY() uint16 BindingMask=0;
    UPROPERTY() int64 ReadyAt=0;
    UPROPERTY() uint8 Quality=0;
};
USTRUCT()
struct FLDInventory
{
    GENERATED_BODY()
    UPROPERTY() uint32 Revision=1;
    UPROPERTY() uint8 RulesVersion=6;
    UPROPERTY() TArray<FGuid> Bindings;
    UPROPERTY() uint8 Capacity=24;
    UPROPERTY() bool bInitialized=false;
    UPROPERTY() TArray<FLDInventoryItem> Items;
    UPROPERTY() TArray<FGuid> Quick;
    FLDInventory(){Quick.SetNum(4);Bindings.SetNum(6);}
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
    uint16 BindingMask=0;
    int64 ReadyAt=0;
    uint8 Quality=0;
    // Server-only causal metadata. Never serialize parents or origin to a peer.
    FName Institution; // persistent institution address; server-only
    FGuid OriginReceipt;
    TArray<FGuid> ParentIds;
};
namespace LDInventory
{
    enum ECommand:uint8 { Move=1,Sort,Bind,Unbind,Read,Place,Drop,Pickup,Buy,Use,Equip,Unequip };
    LOCKDOWN_API FString Name(ELDItemKind Kind,ELDSkill Skill);
    LOCKDOWN_API bool QuickUsable(ELDItemKind Kind,uint8 Rules=6);
    LOCKDOWN_API bool Commodity(ELDItemKind Kind);
    LOCKDOWN_API int32 UnitWeight(ELDItemKind Kind); // -1 rejects unknown definitions
    LOCKDOWN_API int32 Weight(const FLDInventory& Inventory);
    LOCKDOWN_API int32 WeightCapacity(const FLDCharacterBuild& Build);
    // Project the complete final bag. Reservations/quick references add no mass.
    LOCKDOWN_API bool Fits(const FLDInventory& Inventory,const FLDCharacterBuild& Build,
        const TArray<FGuid>& Outgoing,const TArray<ELDItemKind>& Incoming,FString* Reason=nullptr);
    LOCKDOWN_API bool BindQuick(FLDInventory& Inventory,FGuid Id,uint8 Slot,FString& Reason);
    LOCKDOWN_API void Remove(FLDInventory& Inventory,FGuid Id);
    LOCKDOWN_API bool Valid(const FLDInventory& Inventory);
}
