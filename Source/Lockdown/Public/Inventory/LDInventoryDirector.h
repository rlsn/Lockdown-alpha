#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDGrowthProtocol.h"
#include "Economy/LDEconomyLedger.h"
#include "Vision/LDViewTypes.h"
#include "LDInventoryDirector.generated.h"

class FLDDemoContentSnapshot;

// N05 equipment reads over the single ledger: two real positions per contender, no bag, no physical holder.
UCLASS()
class LOCKDOWN_API ALDInventoryDirector : public AActor
{
    GENERATED_BODY()

public:
    ALDInventoryDirector();

    // Match start: the approved catalog that resolves the public definitions of observed equipment.
    void BeginMatch(TSharedPtr<const FLDDemoContentSnapshot> InContent);

    // Own gold and both slots for the owner projection and that contender's AI.
    FLDEquipmentView ReadEquipment(const FGuid& ContenderId) const;
    // Visible-opponent equipment for the observation publisher only, after it has verified the observer's
    // current handle: the native ELDDemoEquipmentId of each slot (0 = empty). No instance, revision, cooldown,
    // gold or skill data. An unknown definition is an explicit catalog error, never a fake empty slot.
    bool ProjectObservedEquipment(const FGuid& ContenderId, FLDObservedEquipmentView& Out, FString& Error) const;

    // Equipped instances of one contender in slot order (Item1 = slot 0, Item2 = slot 1) from a ledger state,
    // either the live state or a prepared after-state of a composite commit.
    static void EquippedItems(const LDEconomy::FAuditState& State, const FGuid& ContenderId, TArray<FLDEquippedItemValue>& Out);

private:
    const LDEconomy::FLedger* Ledger() const;

    TSharedPtr<const FLDDemoContentSnapshot> Content;
};
