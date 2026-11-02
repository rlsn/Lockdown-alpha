#pragma once

#include "CoreMinimal.h"
#include "AI/LDOpponentPolicy.h"

struct FLDMoveIdentity;
struct FLDSpatialOrderRequest;
struct FLDOrderReceipt;
struct FLDCombatActionRequest;
struct FLDCombatActionReceipt;
struct FLDChoiceCommand;
struct FLDShopOpenCommand;
struct FLDShopOpenResult;
struct FLDShopCommand;
struct FLDProgressionView;
struct FLDEquipmentView;
struct FLDSubmitResult;
struct FLDChoiceReceiptQueryResult;
struct FLDShopReceiptQueryResult;
struct FLDWorldRevision;
enum class ELDGrowthReason : uint16;

// Where one AI request stands after a domain answer (OPPONENT_AI.md §3.3). InFlight keeps the original
// request and queries it again, never resending it under a new identity. Dropped ends it without teaching
// the failure memory anything: a life or match change, this AI's own newer request or an expired record;
// the next decision starts from the current authorized projection.
enum class ELDOpponentSettle : uint8 { InFlight, Succeeded, Failed, TargetLost, Dropped };

// Pure translation between policy intents and the domains' request and receipt contracts. Every command is
// built from this AI's own latest owner projections; the owning domain revalidates it at its commit point.
namespace LDOpponentRequests
{
    // Final settlements map onto the policy's failure memory; InFlight and Dropped teach nothing.
    LOCKDOWN_API bool ToOutcome(ELDOpponentSettle Settle, ELDOpponentOutcome& Out);

    // Next navigation identity of the current life: Order always advances, Stop also advances Epoch, and a
    // new match or life restarts both. False when the per-life space is exhausted.
    LOCKDOWN_API bool NextMoveIdentity(const FLDMoveIdentity& Last, uint32 Generation, uint32 Life, bool bStop,
        FLDMoveIdentity& Out);
    // MoveGround to a registered public anchor and its surface, AttackUnit on an observer-local handle, or
    // ApproachShop to the public shop.
    LOCKDOWN_API bool BuildOrder(const FLDOpponentBodyIntent& Intent, const TArray<FLDOpponentPOI>& POIs,
        const FLDMoveIdentity& Identity, FLDSpatialOrderRequest& Out, FString& Error);
    LOCKDOWN_API bool BuildAction(const FLDOpponentBodyIntent& Intent, uint32 Generation, uint32 Life,
        uint64 DirectSequence, uint64 ExpectedSourceRevision, FLDCombatActionRequest& Out, FString& Error);
    // The head offer must still be the one the policy chose from; a full-slot learn names its exact victim.
    LOCKDOWN_API bool BuildChoice(const FLDOpponentBuildIntent& Intent, const FLDProgressionView& Progression,
        uint64 RequestId, FLDChoiceCommand& Out, FString& Error);
    LOCKDOWN_API bool BuildShopOpen(FName ShopId, uint32 Generation, uint32 Life, const FLDWorldRevision& World,
        FLDShopOpenCommand& Out, FString& Error);
    // Buy and Sell take the shown quote, never the public catalog price, and the current owner revisions.
    LOCKDOWN_API bool BuildTrade(const FLDOpponentBuildIntent& Intent, const FLDShopOpenResult& Shop,
        const FLDEquipmentView& Equipment, const FLDProgressionView& Progression, uint64 RequestId,
        FLDShopCommand& Out, FString& Error);

    LOCKDOWN_API ELDOpponentSettle SettleOrder(const FLDOrderReceipt& Receipt);
    LOCKDOWN_API ELDOpponentSettle SettleAction(const FLDCombatActionReceipt& Receipt);
    LOCKDOWN_API ELDOpponentSettle SettleGrowthRejection(ELDGrowthReason Reason);
    // Admission answer of a choice or trade; the caller consumes the RequestId only when it was reserved.
    LOCKDOWN_API ELDOpponentSettle SettleSubmit(const FLDSubmitResult& Result);
    LOCKDOWN_API ELDOpponentSettle SettleChoiceQuery(const FLDChoiceReceiptQueryResult& Result);
    LOCKDOWN_API ELDOpponentSettle SettleShopQuery(const FLDShopReceiptQueryResult& Result);
    LOCKDOWN_API ELDOpponentSettle SettleShopOpen(const FLDShopOpenResult& Result);
}
