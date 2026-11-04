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
    // Request shape only: a known op, a non-zero request id and the op's amount domain (grants > 0, others == 0).
    // Context (match, life, permit) is checked by admission and the commit.
    LOCKDOWN_API ELDGrowthReason ValidateShape(const FLDDevTestCommand& Command);
    // Admission answer for a command refused before it reached a request stream.
    LOCKDOWN_API FLDDevTestResult Refused(const FLDDevTestCommand& Command, ELDGrowthReason Reason, const FString& Detail);
}
