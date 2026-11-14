#pragma once

#include "CoreMinimal.h"
#include "Development/LDGrowthProtocol.h"
#include "LDDevTestProtocol.generated.h"

// DEV test commands (#78): one owner-only request family through which a DEV test session changes the caller's own
// committed state. The wire types exist in every build so the RPCs keep one signature; only builds with automation
// tests whose authority was started with -LDDevGrowthFixture or opted in by its local Host admit a command, every other
// authority answers AdmissionRejected / NotAuthority. A command never targets another contender.
//
// Admission reuses the growth request stream (TLDGrowthRequestStream: monotonic RequestId, one pending request,
// 64-receipt replay window). The pending command commits in the Transactions phase in server arrival order together
// with build commands; XP goes through the reward composite like a reward without a death. Retired op numbers (2 GrantGold,
// 6 BuyItem, 7 SellItem, removed with equipment and gold in #144; 5 LearnOption, removed with the three-card offer in #198)
// are never reused.

UENUM()
enum class ELDDevTestOp : uint8
{
    None = 0,
    // Amount > 0: cumulative XP with its levels and opportunities, like a reward without a death.
    GrantExperience = 1,
    // Amount == 0: the current Alive body's HP and MP to their committed maxima; the shield is untouched.
    RefillResources = 3,
    // Amount == 0: every cooldown track of the caller (Q/W/E and the dash, bound or debt) ready at the commit boundary.
    ResetCooldowns = 4,
    // Stat = an ELDDemoBonusStat other than ADBonusRatio, Amount >= 0 in that stat's unit (P2 points, basis points,
    // whole cm for range): the caller's DEV attribute term for that stat; 0 removes it. Alive, Dead or Unspawned.
    SetAttribute = 8,
    // Amount == 0, Stat 0: removes every DEV attribute term; base, growth and allocated points stay.
    ClearAttributes = 9,
    // Nothing named: the caller back to the match start. Level 1, zero XP, no actives, blueprints, allocated points or DEV
    // attribute terms, the opening opportunity with a fresh opening offer, every cooldown ready, the build safe window
    // passed and a living body full. Alive or Dead.
    ResetToMatchStart = 10,
    // Match-wide, Listen Host connection only, nothing named: every cleared camp waiting for its refresh respawns at the
    // next boundary through the ordinary whole-group refresh; living camps are untouched.
    RefreshCamps = 11,
    // Match-wide, Listen Host connection only, nothing named, Development only: the capture circle opens at this
    // boundary instead of after the full development time; the hold and every capture rule stay.
    OpenCapture = 12,
    // #198, Definition = an active id the caller neither holds nor keeps: a pending blueprint without an opportunity.
    GrantBlueprint = 13,
    // #198, Definition = an active id the caller has not learned, Slot 1..3 = Q/W/E (the active there is forgotten) or 0 =
    // the lowest empty slot: learned now, without an opportunity, a blueprint or the build safe window. Alive or Dead.
    LearnActive = 14,
    // #198, Definition = a stat id, Amount > 0: that many unspent points allocated to it, without the build safe window.
    AllocatePoints = 15,
    SkipPreparation = 16,
    SetPeaceful = 17,
    TeleportToCamera = 18,
};

USTRUCT()
struct LOCKDOWN_API FLDDevTestCommand
{
    GENERATED_BODY()

    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() uint32 Life = 0;
    UPROPERTY() uint64 RequestId = 0;
    UPROPERTY() ELDDevTestOp Op = ELDDevTestOp::None;
    UPROPERTY() int64 Amount = 0;
    UPROPERTY() FLDDefinitionWire Definition;
    UPROPERTY() uint8 Slot = 0;
    // SetAttribute only: the ELDDemoBonusStat value; every other op 0.
    UPROPERTY() uint8 Stat = 0;
    // TeleportToCamera only: requested camera position; authority resolves native support and capsule clearance.
    UPROPERTY() FVector CameraPosition = FVector::ZeroVector;

    bool SameTerms(const FLDDevTestCommand& Other) const;
};

USTRUCT()
struct LOCKDOWN_API FLDDevTestReceiptHeader
{
    GENERATED_BODY()

    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() uint64 RequestId = 0;
    UPROPERTY() uint32 OriginalLife = 0;
    UPROPERTY() ELDReceiptOutcome Outcome = ELDReceiptOutcome::None;
    UPROPERTY() uint16 Reason = 0;
    // Authority boundary batch that committed or rejected the command.
    UPROPERTY() uint64 CommitBatch = 0;
};

USTRUCT()
struct LOCKDOWN_API FLDDevTestReceipt
{
    GENERATED_BODY()

    UPROPERTY() FLDDevTestReceiptHeader Header;
    UPROPERTY() ELDDevTestOp Op = ELDDevTestOp::None;
    UPROPERTY() int64 Amount = 0;
    UPROPERTY() FName Definition;
    UPROPERTY() uint8 Slot = 0;
    UPROPERTY() uint8 Stat = 0;
    // Human-readable committed result or refusal, for the DEV console and log only.
    UPROPERTY() FString Detail;
};

// The single owner answer: the admission (AcceptedPending / AdmissionRejected), a replayed final receipt, or the final
// receipt delivered by the publication after its Transactions phase (Status Final).
USTRUCT()
struct LOCKDOWN_API FLDDevTestResult
{
    GENERATED_BODY()

    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() uint64 RequestId = 0;
    UPROPERTY() ELDAdmissionStatus Status = ELDAdmissionStatus::None;
    UPROPERTY() uint16 Reason = 0;
    UPROPERTY() bool bSequenceReserved = false;
    UPROPERTY() bool bHasReceipt = false;
    UPROPERTY() FLDDevTestReceipt Receipt;
    // Refusal detail for an admission rejection.
    UPROPERTY() FString Detail;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FLDOnDevTestResult, const FLDDevTestResult&);

namespace LDDevTest
{
    LOCKDOWN_API bool IsKnownOp(ELDDevTestOp Op);
    // RefreshCamps, OpenCapture and SkipPreparation change the match; only the Listen Host may send them.
    LOCKDOWN_API bool IsMatchWide(ELDDevTestOp Op);
    LOCKDOWN_API const TCHAR* OpName(ELDDevTestOp Op);
    // The player-facing name of the op in the test-mode notices and panel.
    LOCKDOWN_API const TCHAR* OpLabel(ELDDevTestOp Op);
    // Request shape only: a known op, a non-zero request id and the op's domain: grants an amount > 0 and nothing else;
    // GrantBlueprint a definition; LearnActive a definition and slot 0..3; AllocatePoints a definition and an amount > 0;
    // SetAttribute a permanent build stat and an amount >= 0; refill, reset and clear nothing.
    // Context (match, life, permit) is checked by admission and the commit.
    LOCKDOWN_API ELDGrowthReason ValidateShape(const FLDDevTestCommand& Command);
    // Admission answer for a command refused before it reached a request stream.
    LOCKDOWN_API FLDDevTestResult Refused(const FLDDevTestCommand& Command, ELDGrowthReason Reason, const FString& Detail);

    // One settable permanent build stat of SetAttribute, shared by the console and the in-game test panel. Decimals is the
    // exact entry scale (points and percents take two decimals and travel as P2 / basis points; range takes whole cm);
    // Attribute is the projected row the term shows up in.
    struct FStatField
    {
        const TCHAR* ConsoleName;
        uint8 Stat;       // ELDDemoBonusStat
        int32 Decimals;
        const TCHAR* Unit;
        uint8 Attribute;  // ELDProjectedAttribute
    };
    LOCKDOWN_API TConstArrayView<FStatField> StatFields();
    // A positive decimal integer of at most 18 digits; anything else is refused rather than truncated.
    LOCKDOWN_API bool ParseAmount(const FString& Text, int64& Out);
    // A nonnegative decimal with at most Decimals fraction digits, scaled exactly by 10^Decimals; nothing is rounded.
    LOCKDOWN_API bool ParseScaled(const FString& Text, int32 Decimals, int64& Out);
}
