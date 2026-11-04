#pragma once

#include "CoreMinimal.h"
#include "Development/LDGrowthProtocol.h"
#include "LDDevTestProtocol.generated.h"

// DEV test commands (#78): one owner-only request family through which a DEV test session changes the caller's own
// committed state. The wire types exist in every build so the RPCs keep one signature; only builds with automation
// tests whose authority was started with -LDDevGrowthFixture admit a command (LDDevGrowthFixture::Admit), every other
// authority answers AdmissionRejected / NotAuthority. A command never targets another contender.
//
// Admission reuses the growth request stream (TLDGrowthRequestStream: monotonic RequestId, one pending request,
// 64-receipt replay window). The pending command commits in the Transactions phase in server arrival order together
// with choices and shop commands; XP and gold go through the reward composite with ledger cause DevFixture.

UENUM()
enum class ELDDevTestOp : uint8
{
    None = 0,
    // Amount > 0: cumulative XP with its levels and opportunities, like a reward without a death.
    GrantExperience = 1,
    // Amount > 0: a DevFixture mint into the caller's account.
    GrantGold = 2,
    // Amount == 0: the current Alive body's HP and MP to their committed maxima; the shield is untouched.
    RefillResources = 3,
    // Amount == 0: every cooldown track of the caller (Q/W and gear, bound or debt) ready at the commit boundary.
    ResetCooldowns = 4,
    // Definition = a skill or attribute card id: consumed as the head opportunity's choice, as if it had been offered.
    // It must be a legal option of the current build; Slot 1/2 confirms replacing the active in Q/W, 0 replaces nothing.
    LearnOption = 5,
    // Definition = an item id, Slot 0: one Buy at the catalog price without a shop session or reach.
    BuyItem = 6,
    // Slot 1/2, no Definition: one Sell of the item in that equipment slot at the catalog price.
    SellItem = 7,
    // Stat = an ELDDemoBonusStat other than ADBonusRatio, Amount >= 0 in that stat's unit (P2 points, basis points,
    // whole cm for range): the caller's DEV attribute term for that stat; 0 removes it. Alive, Dead or Unspawned.
    SetAttribute = 8,
    // Amount == 0, Stat 0: removes every DEV attribute term; base, growth, passives and equipment stay.
    ClearAttributes = 9,
    // Nothing named: the caller back to the match start. Level 1, zero XP, no skills, cards, items or DEV attribute
    // terms, the starting gold, the opening opportunity with a fresh opening offer, every cooldown ready and a living
    // body full. Items and gold change through DevReset ledger entries. Alive or Dead.
    ResetToMatchStart = 10,
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
    LOCKDOWN_API const TCHAR* OpName(ELDDevTestOp Op);
    // Request shape only: a known op, a non-zero request id and the op's domain: grants an amount > 0 and nothing else;
    // LearnOption a definition and slot 0..2; BuyItem a definition and slot 0; SellItem slot 1..2 and no definition;
    // SetAttribute a permanent build stat and an amount >= 0; refill, reset and clear nothing.
    // Context (match, life, permit) is checked by admission and the commit.
    LOCKDOWN_API ELDGrowthReason ValidateShape(const FLDDevTestCommand& Command);
    // Admission answer for a command refused before it reached a request stream.
    LOCKDOWN_API FLDDevTestResult Refused(const FLDDevTestCommand& Command, ELDGrowthReason Reason, const FString& Detail);
}
