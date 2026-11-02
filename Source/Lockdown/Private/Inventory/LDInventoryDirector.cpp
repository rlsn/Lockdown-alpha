#include "Inventory/LDInventoryDirector.h"

#include "Development/LDDemoContentCatalog.h"
#include "Economy/LDEconomyDirector.h"
#include "Engine/World.h"
#include "Framework/LDGameMode.h"

namespace
{
const ALDGameMode* InventoryDirectorMode(const AActor& Director)
{
    const UWorld* World = Director.GetWorld();
    return World ? World->GetAuthGameMode<ALDGameMode>() : nullptr;
}

const FLDDemoEquipmentDefinition* InventoryFindEquipment(const FLDDemoContentSnapshot& Catalog, FName Id)
{
    if (Id.IsNone())
    {
        return nullptr;
    }
    return Catalog.Get().Equipment.FindByPredicate([Id](const FLDDemoEquipmentDefinition& Row) { return FName(*Row.Id) == Id; });
}
}

ALDInventoryDirector::ALDInventoryDirector()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
}

void ALDInventoryDirector::BeginMatch(TSharedPtr<const FLDDemoContentSnapshot> InContent)
{
    Content = MoveTemp(InContent);
}

const LDEconomy::FLedger* ALDInventoryDirector::Ledger() const
{
    const ALDGameMode* GameMode = InventoryDirectorMode(*this);
    if (!GameMode || !GameMode->Economy)
    {
        return nullptr;
    }
    // Only the ledger of the current match answers; instance identities are never resolved across matches.
    const ALDEconomyDirector& Economy = *GameMode->Economy;
    const LDEconomy::FLedger& Current = Economy.GetLedger();
    return Current.IsInitialized() && Economy.GetMatch() == GameMode->GetMatchContext() ? &Current : nullptr;
}

FLDEquipmentView ALDInventoryDirector::ReadEquipment(const FGuid& ContenderId) const
{
    FLDEquipmentView View;
    const ALDGameMode* GameMode = InventoryDirectorMode(*this);
    const LDEconomy::FLedger* Book = Ledger();
    const LDEconomy::FAccount* Account = Book ? Book->Account(ContenderId) : nullptr;
    if (!GameMode || !Account)
    {
        return View;
    }
    TArray<FLDEquipmentSlotView> Slots;
    Slots.SetNum(LDEconomy::EquipmentSlots);
    for (int32 Index = 0; Index < Slots.Num(); ++Index)
    {
        Slots[Index].SlotIndex = uint8(Index);
    }
    TArray<TPair<FGuid, LDEconomy::FItem>> Items;
    Book->OwnerItems(ContenderId, Items);
    for (const TPair<FGuid, LDEconomy::FItem>& Pair : Items)
    {
        const LDEconomy::FItem& Item = Pair.Value;
        if (Item.Slot >= LDEconomy::EquipmentSlots || Slots[Item.Slot].bOccupied)
        {
            UE_LOG(LogTemp, Error, TEXT("LD_EQUIPMENT_LEDGER_INVALID contender=%s instance=%s: slot %d is out of range or held twice"),
                *ContenderId.ToString(), *Pair.Key.ToString(), int32(Item.Slot));
            return View;
        }
        FLDEquipmentSlotView& Slot = Slots[Item.Slot];
        Slot.bOccupied = true;
        Slot.Instance = Pair.Key;
        Slot.IssueSerial = Item.IssueSerial;
        Slot.DefinitionId = Item.Definition;
        Slot.ItemRevision = Item.Revision;
    }
    View.bValid = true;
    View.Generation = GameMode->GetMatchContext().Generation;
    View.Gold = Account->Gold;
    View.AccountRevision = Account->Revision;
    View.InventoryRevision = Book->InventoryRevision(ContenderId);
    View.Slots = MoveTemp(Slots);
    return View;
}

bool ALDInventoryDirector::ProjectObservedEquipment(const FGuid& ContenderId, FLDObservedEquipmentView& Out, FString& Error) const
{
    static_assert(sizeof(FLDObservedEquipmentView::Slots) == size_t(LDEconomy::EquipmentSlots),
        "The observed equipment wire carries exactly the ledger's equipment positions.");
    Out = FLDObservedEquipmentView();
    Error.Reset();
    const LDEconomy::FLedger* Book = Ledger();
    if (!Book || !Book->Account(ContenderId))
    {
        Error = TEXT("Observed equipment needs this contender's account in the current match ledger.");
        return false;
    }
    if (!Content.IsValid())
    {
        Error = TEXT("Observed equipment needs the approved catalog of this match.");
        return false;
    }
    TArray<TPair<FGuid, LDEconomy::FItem>> Items;
    Book->OwnerItems(ContenderId, Items);
    FLDObservedEquipmentView View;
    for (const TPair<FGuid, LDEconomy::FItem>& Pair : Items)
    {
        const LDEconomy::FItem& Item = Pair.Value;
        if (Item.Slot >= LDEconomy::EquipmentSlots || View.Slots[Item.Slot] != 0)
        {
            Error = FString::Printf(TEXT("Ledger equipment slot %d is out of range or held twice."), int32(Item.Slot));
            return false;
        }
        // Zero is the empty-slot code on the wire, so a definition without a native identity is a catalog error.
        const FLDDemoEquipmentDefinition* Definition = InventoryFindEquipment(*Content, Item.Definition);
        if (!Definition || Definition->NativeId == 0)
        {
            Error = FString::Printf(TEXT("Equipment definition '%s' is not in the approved catalog."), *Item.Definition.ToString());
            return false;
        }
        View.Slots[Item.Slot] = Definition->NativeId;
    }
    Out = View;
    return true;
}

void ALDInventoryDirector::EquippedItems(const LDEconomy::FAuditState& State, const FGuid& ContenderId,
    TArray<FLDEquippedItemValue>& Out)
{
    Out.Reset();
    TArray<TPair<uint8, FLDEquippedItemValue>> Held;
    for (const TPair<FGuid, LDEconomy::FItem>& Pair : State.LiveItems)
    {
        if (Pair.Value.Owner != ContenderId)
        {
            continue;
        }
        FLDEquippedItemValue Value;
        Value.Entry = Pair.Value.Slot == 0 ? ELDCombatEntry::Item1 : ELDCombatEntry::Item2;
        Value.Instance = Pair.Key;
        Value.DefinitionId = Pair.Value.Definition;
        Value.ItemRevision = Pair.Value.Revision;
        Held.Emplace(Pair.Value.Slot, Value);
    }
    Held.Sort([](const TPair<uint8, FLDEquippedItemValue>& A, const TPair<uint8, FLDEquippedItemValue>& B)
    {
        return A.Key < B.Key;
    });
    Out.Reserve(Held.Num());
    for (const TPair<uint8, FLDEquippedItemValue>& Row : Held)
    {
        Out.Add(Row.Value);
    }
}
