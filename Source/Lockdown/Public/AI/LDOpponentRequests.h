#pragma once

#include "CoreMinimal.h"
#include "AI/LDOpponentPolicy.h"

struct FLDMoveIdentity;
struct FLDSpatialOrderRequest;
struct FLDOrderReceipt;
struct FLDCombatActionRequest;
struct FLDCombatActionReceipt;
struct FLDCombatOwnerView;
struct FLDBuildCommand;
struct FLDProgressionView;
struct FLDSubmitResult;
struct FLDBuildReceiptQueryResult;
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
    // MoveGround to a registered public anchor and its surface, or AttackUnit on an observer-local handle.
    LOCKDOWN_API bool BuildOrder(const FLDOpponentBodyIntent& Intent, const TArray<FLDOpponentPOI>& POIs,
        const FLDMoveIdentity& Identity, FLDSpatialOrderRequest& Out, FString& Error);
    LOCKDOWN_API bool BuildAction(const FLDOpponentBodyIntent& Intent, uint32 Generation, uint32 Life,
        uint64 DirectSequence, uint64 ExpectedSourceRevision, FLDCombatActionRequest& Out, FString& Error);
    // #198: an active choice of the still-current offer (always asking to learn it now), a learn of a still-pending blueprint
    // into a still-empty slot, an allocation that fits the own stats and unspent points, an opening base pick of a still-open
    // option (always asking to enable it now) or the first install of a still-owned component, with a binding only to a
    // learned active; built from the own latest view.
    LOCKDOWN_API bool BuildCommand(const FLDOpponentBuildIntent& Intent, const FLDProgressionView& Progression,
        uint64 RequestId, FLDBuildCommand& Out, FString& Error);

    // A taunted body takes no own order, stop or activation (Navigation chases its taunter): while this AI's own
    // Combat projection shows an active taunt it holds every body intent instead of submitting refused requests.
    LOCKDOWN_API bool BodyIntentsHeld(const FLDCombatOwnerView& Combat, int64 NowMs);
    LOCKDOWN_API ELDOpponentSettle SettleOrder(const FLDOrderReceipt& Receipt);
    LOCKDOWN_API ELDOpponentSettle SettleAction(const FLDCombatActionReceipt& Receipt);
    LOCKDOWN_API ELDOpponentSettle SettleGrowthRejection(ELDGrowthReason Reason);
    // Admission answer of a build command; the caller consumes the RequestId only when it was reserved.
    LOCKDOWN_API ELDOpponentSettle SettleSubmit(const FLDSubmitResult& Result);
    LOCKDOWN_API ELDOpponentSettle SettleBuildQuery(const FLDBuildReceiptQueryResult& Result);
}
