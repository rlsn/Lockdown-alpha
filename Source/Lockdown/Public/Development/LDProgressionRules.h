#pragma once

#include "CoreMinimal.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDMatchTimeline.h"

class FLDDemoContentSnapshot;
struct FLDDemoProgressionDefinition;
struct FLDDemoContentParameters;
struct FLDDemoContentDefinitions;

// #198 persistent contender build owned by ALDDevelopmentDirector (PEE §3). Level is stored once here; points are
// derived from the level and XP (LDProgression::EarnedPoints), only their allocation is stored.
struct FLDLearnedActive
{
    FName Id;
    // 0 = Q, 1 = W, 2 = E.
    uint8 Slot = 0;
};

struct LOCKDOWN_API FLDGrowthBuild
{
    uint8 Level = 1;
    // Allocated points, one count per stat in the stats' design order (StatsInDesignOrder; LDProgression::StartBuild sizes it).
    TArray<int32> StatPoints;
    // Learned Q/W/E actives sorted by slot; one per slot and per id.
    TArray<FLDLearnedActive> Actives;
    // Chosen actives waiting to be learned (待学图纸), sorted by id; never a learned id.
    TArray<FName> PendingBlueprints;
    // Points paid for rest services (the NPC active offer); never refunded, a respec only returns StatPoints.
    int64 PermanentSpentPoints = 0;
    uint64 Revision = 1;

    const FLDLearnedActive* FindActive(FName Id) const;
    // Index into Actives of the active in Slot, or INDEX_NONE.
    int32 ActiveInSlot(uint8 Slot) const;
    bool HasBlueprint(FName Id) const;
    // Sum of StatPoints; false on a negative count or an overflow.
    bool SpentPoints(int64& Out) const;
};

// The one current offer of the active opportunity queue: up to three distinct actives drawn for opportunity Ordinal.
struct FLDActiveOfferState
{
    FGuid OfferId;
    uint8 Ordinal = 0;
    TArray<FName> Options;
    uint64 Revision = 0;
    uint64 GeneratedBuildRevision = 0;
};

// One consumed active opportunity, kept for the whole match (dedupe of a consumed offer and telemetry).
struct FLDActiveOpportunityRecord
{
    uint8 Ordinal = 0;
    FGuid OfferId;
    FName Chosen;
    uint64 RequestId = 0;
};

struct FLDProgressionState
{
    int64 CumulativeXP = 0;
    uint64 Revision = 1;
    // Active opportunities are the ordinals 1..ActiveEarned in the order they were earned; the queue head is
    // ActiveConsumed + 1. Ordinal 1 is the opening one granted by BeginMatch; each level of
    // progression.active_offer_levels reached adds one more and so does each NPC purchase, so
    // ActiveEarned == LDProgression::ActiveOpportunitiesAt(level) + NpcPurchased.
    uint8 ActiveEarned = 0;
    uint8 ActiveConsumed = 0;
    // Active opportunities bought from the rest NPC (BuyNpcOffer).
    uint8 NpcPurchased = 0;
    // The head's offer; unset while the queue is empty or no active is eligible for the head.
    TOptional<FLDActiveOfferState> Offer;
    uint64 LastOfferRevision = 0;
    TArray<FLDActiveOpportunityRecord> Records;

    uint8 QueuedActiveOpportunities() const { return ActiveEarned - ActiveConsumed; }
};

namespace LDProgression
{
    struct FRewardAmounts
    {
        int64 XP = 0;
    };

    struct FExperienceChange
    {
        int64 NewXP = 0;
        uint8 LevelBefore = 0;
        uint8 LevelAfter = 0;
        uint8 ActiveEarnedAfter = 0;
    };

    // Where the match is for a build command (ALDGameMode::ReadBuildStage): Live on the field segments; Rest in a rest
    // build window (the build/training windows and the payout); RestLocked in the rest steps whose builds are locked
    // (draw, betting, PK, settlement, return countdown); Closed otherwise (results and every other phase).
    enum class EMatchStage : uint8 { Live, Rest, RestLocked, Closed };

    // The server's view of one contender when a build command commits.
    struct FBuildContext
    {
        EMatchStage Stage = EMatchStage::Closed;
        ELDLifePhase LifePhase = ELDLifePhase::Unspawned;
        // Combat's build safe window has passed (BuildSafeAtMs <= now); only the Live stage reads it.
        bool bSafe = false;
    };

    // Context is inside a rest build window, where the rest-only services and rebuilds are allowed.
    LOCKDOWN_API bool IsRestWindow(const FBuildContext& Context);
    // The stage a client or the AI reads from the public match view: the same rule as ALDGameMode::ReadBuildStage.
    LOCKDOWN_API EMatchStage StageForPublic(ELDMatchPhase Phase, const FLDTimelineView& Timeline);

    // What an ActiveChoice did with its active.
    struct FChoiceOutcome
    {
        ELDBuildChoiceResult Result = ELDBuildChoiceResult::None;
        uint8 Slot = 0;
        // StoredAsBlueprint after asking to learn: NotSafe, Dead or NoEmptySlot; None when not asked.
        ELDGrowthReason StoredReason = ELDGrowthReason::None;
    };

    // Highest level whose cumulative threshold XP has reached; thresholds are strictly increasing from 0.
    LOCKDOWN_API ELDGrowthReason LevelForXP(const FLDDemoProgressionDefinition& Progression, int64 XP, uint8& OutLevel);
    // The opening opportunity plus one per progression.active_offer_levels entry at or below Level.
    LOCKDOWN_API uint8 ActiveOpportunitiesAt(const FLDDemoProgressionDefinition& Progression, uint8 Level);
    // XP is cumulative int64 and keeps accumulating past the top level. LevelActiveEarned is the level part of the
    // earned opportunities (ActiveEarned minus NpcPurchased) and must follow CurrentLevel (ActiveOpportunitiesAt); the
    // change reports the level part at the new level.
    LOCKDOWN_API ELDGrowthReason PlanExperience(const FLDDemoProgressionDefinition& Progression, int64 CurrentXP,
        uint8 CurrentLevel, uint8 LevelActiveEarned, int64 Grant, FExperienceChange& Out);
    // REWARD.PlayerXP at the frozen victim level.
    LOCKDOWN_API ELDGrowthReason ContenderKillReward(const FLDDemoContentParameters& Parameters, uint8 MaxLevel,
        uint8 VictimLevel, FRewardAmounts& Out);
    // CAMP.<tier>.RewardXP of the monster definition, once per member life. #97: the XP grows with the stage that life
    // spawned with (Development/LDCampGrowth.h). The Boss only takes stage 0.
    LOCKDOWN_API ELDGrowthReason MonsterKillReward(const FLDDemoContentSnapshot& Catalog, FName MonsterId, uint8 Stage,
        FRewardAmounts& Out);

    // Points earned by a contender at Level with cumulative XP: GROWTH.OpeningPoints, GROWTH.PointsPerLevel for every level
    // crossed, and at the top level GROWTH.MaxLevelPoints for every full GROWTH.MaxLevelPointsXP past its threshold.
    LOCKDOWN_API ELDGrowthReason EarnedPoints(const FLDDemoContentDefinitions& Content, uint8 Level, int64 XP, int64& Out);
    // Earned minus allocated minus permanently spent; AuthorityFault when they exceed what was earned.
    LOCKDOWN_API ELDGrowthReason UnspentPoints(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, int64 XP,
        int64& Out);
    // The level-1 build of a match start: no actives, blueprints, allocated or permanently spent points.
    LOCKDOWN_API FLDGrowthBuild StartBuild(const FLDDemoContentDefinitions& Content);
    // Actives by slot and blueprints by id, the order ValidateBuild requires.
    LOCKDOWN_API void NormalizeBuild(FLDGrowthBuild& Build);
    // Build has the shape this catalog allows: one point count per stat, no negative permanent spend, at most three
    // distinct known actives in distinct slots 0..2, distinct known blueprints sorted by id that are not learned.
    LOCKDOWN_API ELDGrowthReason ValidateBuild(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build);

    // The actives opportunity Ordinal may draw for Build, in catalog order: the opening pool (abilities flagged opening)
    // for ordinal 1, every active afterwards (level and NPC opportunities alike); learned actives and pending blueprints
    // never. A forgotten active is eligible again.
    LOCKDOWN_API ELDGrowthReason EligibleActives(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
        uint8 Ordinal, TArray<FName>& Out);
    // Up to three distinct eligible actives, uniformly without replacement and deterministic in Seed (fewer when fewer
    // are eligible, none when none is).
    LOCKDOWN_API ELDGrowthReason DrawActiveOffer(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
        uint8 Ordinal, uint64 Seed, TArray<FName>& Out);
    // Private per-opportunity random material; a redraw of the same ordinal reuses it.
    LOCKDOWN_API uint64 OfferSeed(const FGuid& MatchSecret, const FGuid& Contender, uint8 Ordinal);

    // Whether Context may learn an active into a slot now: in a rest window while alive, or on the field alive inside the
    // build safe window. PhaseClosed, BuildLocked, Dead, NotSafe or LifePhaseChanged otherwise.
    LOCKDOWN_API ELDGrowthReason LearnGate(const FBuildContext& Context);
    // The legality matrix of one command kind (GROWTH_TRANSACTIONS). Live: ActiveChoice alive or dead, LearnBlueprint like
    // LearnGate, AllocatePoints while dead or alive inside the safe window, BuyNpcOffer/Respec NotRestWindow,
    // ReplaceActive/ForgetActive ReplacementNotAllowed. Rest: every kind, the safe window ignored (a dead contender keeps
    // the death-wait rules: ActiveChoice and AllocatePoints only, Dead otherwise). RestLocked: BuildLocked. Closed:
    // PhaseClosed.
    LOCKDOWN_API ELDGrowthReason CommandGate(ELDBuildCommandKind Kind, const FBuildContext& Context);
    // The slot a learn takes: Preferred when it is an empty slot 0..2, otherwise the lowest empty one; false when full.
    LOCKDOWN_API bool PickLearnSlot(const FLDGrowthBuild& Build, uint8 Preferred, uint8& OutSlot);

    // An offered active becomes the contender's: learned in the same commit when bLearnIfPossible, LearnGate allows it and
    // a slot is empty, otherwise a pending blueprint. Option must be an active that is neither learned nor pending.
    // Revision is not changed here.
    LOCKDOWN_API ELDGrowthReason ApplyActiveChoice(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        FName Option, bool bLearnIfPossible, uint8 PreferredSlot, const FBuildContext& Context, FLDGrowthBuild& Out,
        FChoiceOutcome& Outcome);
    // A pending blueprint learned into an empty slot: NoBlueprint, SlotOccupied (a full slot is replaced only by
    // ApplyReplaceActive in a rest window) or a LearnGate reason when refused.
    LOCKDOWN_API ELDGrowthReason ApplyLearnBlueprint(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        FName Blueprint, uint8 Slot, const FBuildContext& Context, FLDGrowthBuild& Out);
    // Unspent points allocated across the stats in one step: one count per stat (design order) summing to at least one and at
    // most the unspent points (NotEnoughPoints), gated by CommandGate.
    LOCKDOWN_API ELDGrowthReason ApplyAllocation(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        int64 XP, const TArray<uint8>& PointCounts, const FBuildContext& Context, FLDGrowthBuild& Out);

    // ---- Rest services (#198 §4.4/§5); none changes Revision ----

    // What a BuyNpcOffer paid and opened.
    struct FNpcOfferOutcome
    {
        int64 PointsPaid = 0;
        uint8 Ordinal = 0;
    };
    // The rest NPC: GROWTH.NpcActivePoints moved to PermanentSpentPoints and one new active opportunity whose offer is drawn
    // in the same step with exactly three distinct actives of the whole pool (OfferSeed of the new ordinal), its OfferId
    // new and its revision the next offer revision. Refused without a charge: NotRestWindow outside a rest window,
    // OfferOutstanding while an offer or any opportunity is queued, NpcPoolTooSmall with fewer than three eligible actives,
    // NotEnoughPoints. The offer's GeneratedBuildRevision is Current's; the director sets the installed one.
    LOCKDOWN_API ELDGrowthReason ApplyBuyNpcOffer(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        const FLDProgressionState& Progression, const FGuid& MatchSecret, const FGuid& Contender, const FBuildContext& Context,
        FLDGrowthBuild& Out, FLDProgressionState& OutProgression, FNpcOfferOutcome& Outcome);
    // Every allocated stat point back to the unspent ones; PermanentSpentPoints stays. NotRestWindow outside a rest
    // window, NothingToRespec when nothing is allocated.
    LOCKDOWN_API ELDGrowthReason ApplyRespec(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current, int64 XP,
        const FBuildContext& Context, FLDGrowthBuild& Out, int64& OutRefunded);
    // A pending blueprint learned into the occupied Slot 0..2, the active there forgotten (neither refunded nor kept as a
    // blueprint). ReplacementNotAllowed outside a rest window, OfferOutstanding while an active offer is outstanding,
    // SlotEmpty for an empty slot (LearnBlueprint learns into it), NoBlueprint.
    LOCKDOWN_API ELDGrowthReason ApplyReplaceActive(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        const FLDProgressionState& Progression, FName Blueprint, uint8 Slot, const FBuildContext& Context, FLDGrowthBuild& Out,
        FName& OutForgotten);
    // The active in Slot 0..2 forgotten; it is eligible for later offers again. ReplacementNotAllowed outside a rest window,
    // OfferOutstanding while an active offer is outstanding, SlotEmpty.
    LOCKDOWN_API ELDGrowthReason ApplyForgetActive(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        const FLDProgressionState& Progression, uint8 Slot, const FBuildContext& Context, FLDGrowthBuild& Out, FName& OutForgotten);
}