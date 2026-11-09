#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDGrowthProtocol.h"
#include "Development/LDGrowthRequests.h"
#include "Development/LDProgressionRules.h"
#include "LDDevelopmentDirector.generated.h"

class ALDGameMode;
class FLDDemoContentSnapshot;
class FLDGameplayWritePermit;

// Prepared persistent growth change (PEE §4.2). Built by Prepare*, consumed once by ApplyPreparedGrowth
// inside the same uninterrupted composite commit; never stored across phases.
class FLDPreparedGrowthChange
{
public:
    FLDPreparedGrowthChange(FLDPreparedGrowthChange&&) = default;
    FLDPreparedGrowthChange& operator=(FLDPreparedGrowthChange&&) = default;
    FLDPreparedGrowthChange(const FLDPreparedGrowthChange&) = delete;
    FLDPreparedGrowthChange& operator=(const FLDPreparedGrowthChange&) = delete;

    const FGuid& Contender() const { return ContenderId; }
    const FLDGrowthBuild& Build() const { return NewBuild; }
    const FLDProgressionState& Progression() const { return NewProgression; }
    bool ChangesBuild() const { return NewBuild.Revision != ExpectedBuildRevision; }
    uint8 LevelBefore() const { return OldLevel; }

    // Records what Combat will have installed once the caller applies its prepared projection.
    void SetCombatInstall(const FLDExpectedBuildVersions& Installed) { bCombatInstall = true; CombatInstall = Installed; }

private:
    friend class ALDDevelopmentDirector;
    FLDPreparedGrowthChange() = default;
    FGuid ContenderId;
    uint64 ExpectedBuildRevision = 0;
    uint64 ExpectedProgressionRevision = 0;
    uint8 OldLevel = 0;
    FLDGrowthBuild NewBuild;
    FLDProgressionState NewProgression;
    // Legal pool of the queue head on NewBuild, validated (>= 3 options) when a head draw follows Apply.
    bool bDrawHead = false;
    TArray<LDProgression::FLegalOption> HeadPool;
    bool bCombatInstall = false;
    FLDExpectedBuildVersions CombatInstall;
};

struct FLDPrepareGrowthResult
{
    TOptional<FLDPreparedGrowthChange> Value;
    ELDGrowthReason Reason = ELDGrowthReason::None;
    FString Message;
    bool IsSuccess() const { return Value.IsSet() && Reason == ELDGrowthReason::None; }
};

// A queued intent of this module, ordered by server arrival for the Transactions dispatch.
struct FLDPendingGrowthIntent
{
    enum class EKind : uint8
    {
        Choice,
#if WITH_DEV_AUTOMATION_TESTS
        // DEV test command of ALDGrowthDirector (LDDevTestProtocol.h).
        DevTest,
#endif
    };
    EKind Kind = EKind::Choice;
    FGuid Contender;
    uint64 Arrival = 0;
};

// N04 growth owner: level/XP, opportunity queue with one persistent three-card offer, Q/W and passives,
// permanent attribute cards. Contender records persist across deaths and are rebuilt only by a new match.
UCLASS()
class LOCKDOWN_API ALDDevelopmentDirector : public AActor
{
    GENERATED_BODY()

public:
    ALDDevelopmentDirector();

    bool BeginMatch(const FLDMatchContext& Match, const TArray<FGuid>& Roster,
        TSharedPtr<const FLDDemoContentSnapshot> Content, FString& Error);

    // ---- Narrow server entries shared by the owning-connection adapter and server AI ----
    FLDProgressionView ReadProgression(const FGuid& ContenderId) const;
    // Admission only: shape, current Generation, membership and the request stream. Context and versions are
    // re-checked by CommitChoice in the Transactions phase.
    FLDSubmitResult QueueChoice(const FGuid& ContenderId, const FLDChoiceCommand& Command);
    FLDChoiceReceiptQueryResult QueryChoiceReceipt(const FGuid& ContenderId, const FLDReceiptQuery& Query) const;

    // ---- Transactions phase (dispatched by ALDGrowthDirector::ProcessTransactions) ----
    void CollectPending(TArray<FLDPendingGrowthIntent>& Out) const;
    // Commits or rejects this contender's pending choice and finalizes its request sequence.
    // An AuthorityFault reason in the receipt means the boundary must fault.
    FLDChoiceReceipt CommitPendingChoice(const FGuid& ContenderId, const FLDGameplayWritePermit& Permit);

    // ---- Composite-commit parts for rewards ----
    // Cumulative XP, level and one opportunity per level crossed (after the opening one); BuildRevision advances only with the level.
    FLDPrepareGrowthResult PrepareExperience(const FGuid& ContenderId, int64 GrantXP) const;
    // No-fail: installs the prepared build/progression, refreshes the offer guard and draws the queue head.
    void ApplyPreparedGrowth(FLDPreparedGrowthChange&& Prepared, const FLDGameplayWritePermit& Permit);

    // Complete post-change projection input for Combat (PEE §4.2 / Combat PrepareBuildChange).
    FLDBuildProjectionInput MakeProjectionInput(const FLDGrowthBuild& Build) const;
    FLDCombatCaller MakeCaller(const FGuid& ContenderId) const;
    bool ReadCombatInstalled(const FGuid& ContenderId, FLDExpectedBuildVersions& Out) const;
    const FLDGrowthBuild* FindBuild(const FGuid& ContenderId) const;
    const FLDProgressionState* FindProgression(const FGuid& ContenderId) const;

    // Choice receipts finalized since the last publication, for the owning connection.
    void TakeFinalReceipts(const FGuid& ContenderId, TArray<FLDChoiceReceipt>& Out);

#if WITH_DEV_AUTOMATION_TESTS
    // DEV growth fixture (LDDevGrowthFixture.h): queues one three-card offer for this contender's next drawn head offers,
    // oldest first. Every definition must be a legal choice of the current build and an offer must still be left to
    // draw; each head draw re-resolves its preset against that draw's legal pool. The opening offer is drawn in BeginMatch,
    // so it can only be preset in the Lobby through LDDevGrowthFixture::StageOffer.
    bool DevQueueOffer(const FGuid& ContenderId, const TArray<FName>& DefinitionIds, FString& Error);
    // DEV test command (LDDevTestProtocol.h, Transactions permit): consumes the head opportunity with DefinitionId as if it
    // had been one of the offered cards. It must be a legal option of the current build (LegalPool: unlearned at rank 1,
    // learned at its next rank, any attribute card); ReplaceSlot 1/2 confirms replacing the active in Q/W, 0 replaces
    // nothing. Every other rule of a choice applies through InstallOption. None when installed; otherwise nothing changed,
    // AuthorityFault meaning the boundary must fault.
    ELDGrowthReason DevChooseOption(const FGuid& ContenderId, FName DefinitionId, uint8 ReplaceSlot,
        const FLDGameplayWritePermit& Permit, FString& Detail);
    // DEV test reset to the match start, prepared for ApplyPreparedGrowth inside one composite: level 1, zero XP, no
    // skills or cards, the opening opportunity only (EarnedThrough 1, none consumed, no choice records) and a fresh
    // opening offer drawn at Apply exactly as BeginMatch draws it. Build, progression and offer revisions keep rising, so
    // every in-flight choice of the old state is refused as stale; request streams and receipts stay.
    FLDPrepareGrowthResult DevPrepareReset(const FGuid& ContenderId) const;
#endif

private:
    friend class FLDDemoGrowthRerollTest;
    struct FRecord
    {
        FLDGrowthBuild Build;
        FLDProgressionState Progression;
        TLDGrowthRequestStream<FLDChoiceCommand, FLDChoiceReceipt> Choices;
        FLDExpectedBuildVersions CombatInstalled;
        TArray<FLDChoiceReceipt> Unpublished;
    };

    FLDChoiceReceipt RejectPending(FRecord& Record, const FLDChoiceCommand& Command, ELDGrowthReason Reason,
        const FLDGameplayWritePermit* Permit);
    void DrawHead(const FGuid& ContenderId, FRecord& Record, const TArray<LDProgression::FLegalOption>& Pool);
    void InstallHead(const FGuid& ContenderId, FRecord& Record, uint8 Ordinal, bool bRerolled, const TArray<LDProgression::FLegalOption>& Picked);
    // 112 §6: replaces the head offer with one free reroll (same opportunity, previous cards avoided where possible).
    // None when installed; otherwise nothing changed.
    ELDGrowthReason RerollHead(const FGuid& ContenderId, FRecord& Record);
    // Free rerolls left: one per reached reroll level, minus those spent; unused ones carry over.
    uint8 RerollTokens(const FRecord& Record) const;
    // Applies Option to the head opportunity of Record with every choice rule (ApplyOption, revisions, the next head's
    // pool, the Combat projection) and installs it with the next head drawn. HiddenActive is the active an Opening option
    // hides (None otherwise). None when installed; a rejection reason when
    // nothing changed; AuthorityFault when the domain or the permit was lost before the install.
    ELDGrowthReason InstallOption(const FGuid& ContenderId, FRecord& Record, const FLDChoiceOption Option,
        const LDProgression::FReplacement* Replacement, FName HiddenActive, uint64 RequestId, const FLDGameplayWritePermit& Permit,
        LDProgression::FAppliedOption& OutApplied);
    ALDGameMode* Mode() const;

    FLDMatchContext Match;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    FGuid MatchSecret;
    TMap<FGuid, FRecord> Records;
#if WITH_DEV_AUTOMATION_TESTS
    // Queued DEV preset offers per contender, oldest first; BeginMatch replaces them with the roster's Lobby-staged offers.
    TMap<FGuid, TArray<TArray<FName>>> DevOfferQueue;
#endif
};
