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
    // #198 B: the component of each normal level-up this change crosses, in level order (None: points instead).
    const TArray<FLDComponentGrant>& ComponentGrants() const { return Grants; }
    // #198 G: the build part of a field reward this change installs (Result None when it carries none).
    const LDProgression::FFieldGrant& FieldGrant() const { return Field; }

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
    TArray<FLDComponentGrant> Grants;
    LDProgression::FFieldGrant Field;
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
        Build,
#if WITH_DEV_AUTOMATION_TESTS
        // DEV test command of ALDGrowthDirector (LDDevTestProtocol.h).
        DevTest,
#endif
    };
    EKind Kind = EKind::Build;
    FGuid Contender;
    uint64 Arrival = 0;
};

// N04 growth owner (#198): level/XP, derived points, their allocation and the permanent rest spend, Q/W/E actives, pending
// blueprints, the active opportunity queue (level and NPC opportunities) with its one current offer, and (#198 B) the owned
// component instances with their install state and first-install history. Contender records persist across deaths and are
// rebuilt only by a new match.
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
    // Admission only: shape, current Generation, membership and the request stream. Context, legality and versions are
    // re-checked by CommitPendingBuild in the Transactions phase.
    FLDSubmitResult QueueBuild(const FGuid& ContenderId, const FLDBuildCommand& Command);
    FLDBuildReceiptQueryResult QueryBuildReceipt(const FGuid& ContenderId, const FLDReceiptQuery& Query) const;

    // ---- Transactions phase (dispatched by ALDGrowthDirector::ProcessTransactions) ----
    void CollectPending(TArray<FLDPendingGrowthIntent>& Out) const;
    // Commits or rejects this contender's pending build command and finalizes its request sequence.
    // An AuthorityFault reason in the receipt means the boundary must fault.
    FLDBuildReceipt CommitPendingBuild(const FGuid& ContenderId, const FLDGameplayWritePermit& Permit);

    // ---- Composite-commit parts for rewards ----
    // Cumulative XP, level, one active opportunity per active_offer_levels entry reached (NPC purchases carry over) and
    // (#198 B) one component per normal level-up crossed (or the exhausted-pool points); BuildRevision advances only with the
    // level. The points follow from the level, XP and exhausted-pool grants.
    FLDPrepareGrowthResult PrepareExperience(const FGuid& ContenderId, int64 GrantXP) const;
    // #198 G: PrepareExperience(GrantXP) followed by the build part Kind of map event Event/Serial, drawn on the post-XP build
    // from private per-event random material (LDProgression::DrawFieldReward / ApplyFieldGrant) and recorded in the field
    // reward history; the build and progression revisions advance once. Kind None is PrepareExperience.
    FLDPrepareGrowthResult PrepareFieldReward(const FGuid& ContenderId, int64 GrantXP, ELDFieldRewardKind Kind, FName Event,
        uint64 Serial) const;
    // No-fail: installs the prepared build/progression and draws the queue head when one is due.
    void ApplyPreparedGrowth(FLDPreparedGrowthChange&& Prepared, const FLDGameplayWritePermit& Permit);

    // Complete post-change projection input for Combat (PEE §4.2 / Combat PrepareBuildChange).
    FLDBuildProjectionInput MakeProjectionInput(const FLDGrowthBuild& Build) const;
    FLDCombatCaller MakeCaller(const FGuid& ContenderId) const;
    bool ReadCombatInstalled(const FGuid& ContenderId, FLDExpectedBuildVersions& Out) const;
    // #198 session D: a PK settlement pays Points (>= 1) unspent attribute points to ContenderId under the business identity
    // AwardId (one per PK and recipient). Transactions permit; spends no regular choice, never touches XP or level.
    // PKAwardDuplicate when AwardId was already paid (nothing changes); AuthorityFault means the boundary must fault.
    ELDGrowthReason GrantPKPoints(const FGuid& ContenderId, int64 Points, const FGuid& AwardId, const FLDGameplayWritePermit& Permit);
    const FLDGrowthBuild* FindBuild(const FGuid& ContenderId) const;
    const FLDProgressionState* FindProgression(const FGuid& ContenderId) const;

    // Build receipts finalized since the last publication, for the owning connection.
    void TakeFinalReceipts(const FGuid& ContenderId, TArray<FLDBuildReceipt>& Out);

#if WITH_DEV_AUTOMATION_TESTS
    // Prepared only; ALDGrowthDirector's evaluation-only entry and reward composite guard the actual commit.
    FLDPrepareGrowthResult DevPrepareEvaluationExperience(const FGuid& ContenderId, int64 GrantXP,
        const TArray<FName>& LevelComponents) const;
    // DEV growth fixture (LDDevGrowthFixture.h): queues one active offer (one to three distinct actives) for this
    // contender's next drawn head offers, oldest first. Every id must be an active the build neither holds nor keeps as a
    // blueprint and an offer must still be left to draw; each head draw re-resolves its preset against that build. The
    // opening offer is drawn in BeginMatch, so it can only be preset in the Lobby through LDDevGrowthFixture::StageOffer.
    bool DevQueueOffer(const FGuid& ContenderId, const TArray<FName>& DefinitionIds, FString& Error);
    // DEV test commands (LDDevTestProtocol.h, Transactions permit), each one revision-bumping build change installed like
    // a build command (Combat projection when it changes Combat's view), Alive or Dead, without the safe window:
    // GrantBlueprint adds an active the build neither holds nor keeps as a pending blueprint;
    // LearnActive learns an active into slot Slot-1 (1..3, replacing and forgetting the active there) or, with Slot 0,
    // into the lowest empty slot (NoEmptySlot when full), consuming its blueprint when it had one;
    // AllocatePoints spends Amount unspent points on one stat.
    // None when installed; otherwise nothing changed, AuthorityFault meaning the boundary must fault.
    ELDGrowthReason DevGrantBlueprint(const FGuid& ContenderId, FName DefinitionId, const FLDGameplayWritePermit& Permit,
        FString& Detail);
    ELDGrowthReason DevLearnActive(const FGuid& ContenderId, FName DefinitionId, uint8 Slot, const FLDGameplayWritePermit& Permit,
        FString& Detail);
    ELDGrowthReason DevAllocatePoints(const FGuid& ContenderId, FName StatId, int64 Amount, const FLDGameplayWritePermit& Permit,
        FString& Detail);
    // DEV test reset to the match start, prepared for ApplyPreparedGrowth inside one composite: level 1, zero XP, no
    // actives, blueprints, components, allocated or permanently spent points, the opening opportunity only (none consumed or
    // bought, no records) and a fresh opening offer drawn at Apply exactly as BeginMatch draws it. Build, progression and offer
    // revisions keep rising, so every in-flight command of the old state is refused as stale; request streams and receipts
    // stay.
    FLDPrepareGrowthResult DevPrepareReset(const FGuid& ContenderId) const;
#endif

private:
    friend class FLDDemoGrowthBuildCommandsTest;

    struct FRecord
    {
        FLDGrowthBuild Build;
        FLDProgressionState Progression;
        TLDGrowthRequestStream<FLDBuildCommand, FLDBuildReceipt> Builds;
        FLDExpectedBuildVersions CombatInstalled;
        TArray<FLDBuildReceipt> Unpublished;
    };

    FLDBuildReceipt RejectPending(FRecord& Record, const FLDBuildCommand& Command, ELDGrowthReason Reason,
        const FLDGameplayWritePermit* Permit);
    // Draws the queue head's offer when the queue has an opportunity without one and an active is eligible for it; with
    // no eligible active the opportunity keeps waiting without an offer.
    void DrawHead(const FGuid& ContenderId, FRecord& Record);
    // Installs NewBuild (and NewProgression when bProgressionChanged) with the next revisions and the Combat projection
    // when the build changes what Combat sees (level, points, learned actives or installed components), all prepared before
    // anything is written,
    // then draws a due head. None when installed; a rejection reason when nothing changed; AuthorityFault when the domain
    // or the permit was lost before the install.
    ELDGrowthReason InstallBuild(const FGuid& ContenderId, FRecord& Record, FLDGrowthBuild&& NewBuild,
        FLDProgressionState&& NewProgression, bool bProgressionChanged, const FLDGameplayWritePermit& Permit);
    // The match stage (ALDGameMode::ReadBuildStage), life phase and build safe window of ContenderId now; false
    // (StaleContext) without a life read.
    bool ReadBuildContext(const FGuid& ContenderId, LDProgression::FBuildContext& Out, uint32& OutLife) const;
    ALDGameMode* Mode() const;

    FLDMatchContext Match;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    FGuid MatchSecret;
    TMap<FGuid, FRecord> Records;
    // #198 session D: PK award identities already paid in this match.
    TSet<FGuid> PaidPKAwards;
#if WITH_DEV_AUTOMATION_TESTS
    // Queued DEV preset offers per contender, oldest first; BeginMatch replaces them with the roster's Lobby-staged offers.
    TMap<FGuid, TArray<TArray<FName>>> DevOfferQueue;
#endif
};