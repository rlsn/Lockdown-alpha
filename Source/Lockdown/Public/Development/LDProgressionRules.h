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

// #198 B: where an installed component sits and its install parameters. Base components take Kind None; an ActiveElement
// binding (B18) names one learned active and one element. Empty while the instance is in the inventory.
struct FLDComponentTarget
{
    ELDComponentTargetKind Kind = ELDComponentTargetKind::None;
    // Active / EffectSegment: the learned active; Component: the host component (#198 F).
    FName Definition;
    uint8 Segment = 0;
    FName BoundActive;
    ELDDemoElement Element = ELDDemoElement::None;
    // #198 F C12 (extra_base): the installed base component whose effect the exit repeats.
    FName BoundComponent;

    bool operator==(const FLDComponentTarget& Other) const
    {
        return Kind == Other.Kind && Definition == Other.Definition && Segment == Other.Segment && BoundActive == Other.BoundActive
            && Element == Other.Element && BoundComponent == Other.BoundComponent;
    }
    bool IsEmpty() const { return *this == FLDComponentTarget(); }
};

// #198 B: one owned material instance; inventory and installed are one list, at most one instance per definition.
struct FLDComponentInstance
{
    FName Id;
    ELDComponentSource Source = ELDComponentSource::None;
    // OpeningPick: 1; LevelUp: the level whose normal reward granted it.
    uint8 SourceLevel = 0;
    bool bInstalled = false;
    // First-time history (spec §5): once installed, a later re-install is a rest-window move; death, reconnect and
    // uninstalling never clear it.
    bool bEverInstalled = false;
    // #198 G: this mod/connector received its one FillGap.
    bool bGapFilled = false;
    FLDComponentTarget Target;
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
    // #198 session D: points paid by PK settlements (participation, winner share, betting pool); #198 G: also the
    // GROWTH.EmptyComponentPoolPoints of a field reward or elite choice that found its material pool empty. Unspent until
    // allocated.
    int64 AwardedPoints = 0;
    // #198 B: owned components sorted by id, one per definition.
    TArray<FLDComponentInstance> Components;
    uint64 Revision = 1;

    const FLDLearnedActive* FindActive(FName Id) const;
    // Index into Actives of the active in Slot, or INDEX_NONE.
    int32 ActiveInSlot(uint8 Slot) const;
    bool HasBlueprint(FName Id) const;
    const FLDComponentInstance* FindComponent(FName Id) const;
    FLDComponentInstance* FindComponent(FName Id);
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
    // #198 B: normal level-ups whose component roll found the whole common pool owned; each earned
    // GROWTH.EmptyComponentPoolPoints instead (LDProgression::EarnedPoints).
    uint8 EmptyPoolGrants = 0;
    // The head's offer; unset while the queue is empty or no active is eligible for the head.
    TOptional<FLDActiveOfferState> Offer;
    uint64 LastOfferRevision = 0;
    TArray<FLDActiveOpportunityRecord> Records;
    // #198 G: pending elite-camp component choices, oldest first (ChoiceId, event, serial and the fixed options).
    TArray<FLDComponentChoiceView> ComponentChoices;
    // #198 G: high-tier camp blueprints held because no active was eligible, and how many were ever held (seed index).
    uint8 HeldBlueprints = 0;
    uint32 HeldBlueprintsEver = 0;
    // #198 G: every field reward received, oldest first.
    TArray<FLDFieldRewardView> FieldRewards;

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
    // crossed, at the top level GROWTH.MaxLevelPoints for every full GROWTH.MaxLevelPointsXP past its threshold, and
    // GROWTH.EmptyComponentPoolPoints for each of the EmptyPoolGrants level-ups that found the component pool exhausted.
    LOCKDOWN_API ELDGrowthReason EarnedPoints(const FLDDemoContentDefinitions& Content, uint8 Level, int64 XP, uint8 EmptyPoolGrants,
        int64& Out);
    // Earned minus allocated minus permanently spent; AuthorityFault when they exceed what was earned.
    LOCKDOWN_API ELDGrowthReason UnspentPoints(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, int64 XP,
        uint8 EmptyPoolGrants, int64& Out);
    // The level-1 build of a match start: no actives, blueprints, components, allocated or permanently spent points.
    LOCKDOWN_API FLDGrowthBuild StartBuild(const FLDDemoContentDefinitions& Content);
    // Actives by slot, blueprints and components by id, the order ValidateBuild requires.
    LOCKDOWN_API void NormalizeBuild(FLDGrowthBuild& Build);
    // Build has the shape this catalog allows: one point count per stat, no negative permanent spend, at most three distinct
    // known actives in distinct slots 0..2, distinct known blueprints sorted by id that are not learned, and distinct known
    // components sorted by id whose source, history and install target are legal (installed implies ever installed; an
    // inventory instance has no target; only a base component is installed, with the binding its definition takes).
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
    // #198 B: OpeningBase like ActiveChoice; InstallComponent as a first install (InstallGate); UninstallComponent the rest
    // window only (RestOnly; Dead for a dead contender).
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
        int64 XP, uint8 EmptyPoolGrants, const TArray<uint8>& PointCounts, const FBuildContext& Context, FLDGrowthBuild& Out);

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
        uint8 EmptyPoolGrants, const FBuildContext& Context, FLDGrowthBuild& Out, int64& OutRefunded);
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

    // ---- #198 B components ----

    // The component of one normal level-up to Level (2..MaxLevel): a category rolled by the progression
    // component_category_weights band of Level, then a uniform pick among the unowned Common definitions of that kind in
    // catalog order. A category without such a definition is dropped and the others keep their relative weights.
    // OutComponent is None when every category is empty (the caller grants GROWTH.EmptyComponentPoolPoints instead).
    // Deterministic in Seed; DataInvalid for a level outside 2..MaxLevel or a broken band table, never an "empty" pool.
    LOCKDOWN_API ELDGrowthReason DrawLevelComponent(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, uint8 Level,
        uint64 Seed, FName& OutComponent);
    // Private per-level random material of the component roll, distinct from OfferSeed of the same number.
    LOCKDOWN_API uint64 ComponentSeed(const FGuid& MatchSecret, const FGuid& Contender, uint8 Level);
    // Every normal level-up LevelBefore+1..LevelAfter in order draws its component into Out (Source LevelUp, inventory);
    // an exhausted pool adds one to OutEmptyPoolGrants instead. OutGrants lists each level's result (None = points).
    LOCKDOWN_API ELDGrowthReason GrantLevelComponents(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        uint8 LevelBefore, uint8 LevelAfter, const FGuid& MatchSecret, const FGuid& Contender, FLDGrowthBuild& Out,
        uint8& OutEmptyPoolGrants, TArray<FLDComponentGrant>& OutGrants);

    // The active of the consumed opening opportunity (ordinal 1), None before the opening choice.
    LOCKDOWN_API FName OpeningActive(const FLDProgressionState& Progression);
    // Whether the build already took its opening base (an instance with Source OpeningPick).
    LOCKDOWN_API bool OpeningBaseTaken(const FLDGrowthBuild& Build);
    // Spec §4.3 opening base options for OpeningChoice (the opening active), in catalog order: Common base components flagged
    // opening-always plus those listing OpeningChoice, without the owned ones.
    LOCKDOWN_API ELDGrowthReason OpeningBaseOptions(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
        FName OpeningChoice, TArray<FName>& Out);

    // Whether an owned component may be installed now. First install (never installed): the rest window (Dead for a dead
    // contender) or alive inside the build safe window (Dead / NotSafe otherwise). Re-installing a once-installed instance: the rest window
    // only (RestOnly). PhaseClosed or LifePhaseChanged outside the match.
    LOCKDOWN_API ELDGrowthReason InstallGate(const FBuildContext& Context, bool bEverInstalled);
    // Whether Target is a legal install of Component on Build: a base component takes no target; its binding needs a learned
    // active of BindActives and an element of BindElements (InvalidTarget otherwise). Mods and connectors are InvalidTarget
    // until #198 F adds their targets.
    LOCKDOWN_API ELDGrowthReason InstallTargetReason(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
        FName Component, const FLDComponentTarget& Target);

    // What an OpeningBase did with its component.
    struct FInstallOutcome
    {
        bool bInstalled = false;
        // Stored after bInstallIfPossible: why it was not installed (InstallGate or InstallTargetReason); None when not asked.
        ELDGrowthReason StoredReason = ELDGrowthReason::None;
    };

    // The opening base pick: once (OpeningBaseTaken), after the opening active was chosen (OpeningActiveFirst), one of
    // OpeningBaseOptions (OpeningBaseUnavailable). Rest and live stages, alive or dead. With bInstallIfPossible the
    // instance is installed on Target in the same commit when InstallGate and InstallTargetReason allow, otherwise stored.
    LOCKDOWN_API ELDGrowthReason ApplyOpeningBase(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        const FLDProgressionState& Progression, FName Component, bool bInstallIfPossible, const FLDComponentTarget& Target,
        const FBuildContext& Context, FLDGrowthBuild& Out, FInstallOutcome& Outcome);
    // An owned, uninstalled component installed on Target: NotOwned, AlreadyInstalled, an InstallGate or an
    // InstallTargetReason reason when refused.
    LOCKDOWN_API ELDGrowthReason ApplyInstallComponent(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        FName Component, const FLDComponentTarget& Target, const FBuildContext& Context, FLDGrowthBuild& Out);
    // An installed component back to the inventory (its history stays): rest window only (RestOnly), NotOwned, NotInstalled.
    LOCKDOWN_API ELDGrowthReason ApplyUninstallComponent(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        FName Component, const FBuildContext& Context, FLDGrowthBuild& Out);

    // ---- #198 G field rewards (spec §4.2/§4.4, outer parameters §5) and the rest material services (spec §4.5) ----

    // The growth state an FLDProgressionView publishes, as a build (level, allocated stats, actives, blueprints and
    // components with their install targets and history), so a client or the AI evaluates FillGap/Dismantle eligibility
    // with the server's own rules. Revision and permanent spend are copied; nothing else is invented.
    LOCKDOWN_API FLDGrowthBuild BuildFromView(const FLDDemoContentDefinitions& Content, const FLDProgressionView& View);
    // Actives a field blueprint may give, in catalog order: every active neither learned nor kept as a blueprint.
    LOCKDOWN_API void EligibleBlueprints(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, TArray<FName>& Out);
    // Components of Pool the build does not own, in catalog order.
    LOCKDOWN_API void UnownedComponents(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
        ELDDemoComponentPool Pool, TArray<FName>& Out);

    // What one field reward's build part gives (decided before anything installs).
    struct FFieldGrant
    {
        ELDFieldRewardResult Result = ELDFieldRewardResult::None;
        FName Definition;
        // ComponentChoice: one or two distinct known common components.
        TArray<FName> ChoiceOptions;
    };
    // Kind's build part for Build, deterministic in Seed. BuildDraw: a fair class roll between an eligible active blueprint and
    // an unowned scarce component, each uniform; one empty class uses the other; both empty: one common material drawn like a
    // normal level-up of max(Build.Level, 2) (PoolPoints when the common pool is exhausted). Blueprint: one uniform eligible
    // active, BlueprintHeld when none is. ComponentChoice: two distinct unowned common components uniformly without
    // replacement (one when only one is left; PoolPoints when none is). DataInvalid for None.
    LOCKDOWN_API ELDGrowthReason DrawFieldReward(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
        ELDFieldRewardKind Kind, uint64 Seed, FFieldGrant& Out);
    // Private per-event random material of one recipient's field reward.
    LOCKDOWN_API uint64 FieldRewardSeed(const FGuid& MatchSecret, const FGuid& Contender, FName Event, uint64 Serial);
    // Private random material of one held high-tier blueprint (the Index-th held grant of the match, 1-based).
    LOCKDOWN_API uint64 HeldBlueprintSeed(const FGuid& MatchSecret, const FGuid& Contender, uint32 Index);
    // Installs one drawn field grant on Build/Progression (Revision untouched) and records it in Progression.FieldRewards:
    // a blueprint as a pending blueprint, a component as an owned inventory instance (Source BuildReward), PoolPoints as
    // GROWTH.EmptyComponentPoolPoints added to AwardedPoints, a ComponentChoice as a pending choice ChoiceId, BlueprintHeld
    // as one more held blueprint. DataInvalid when the grant does not fit Build.
    LOCKDOWN_API ELDGrowthReason ApplyFieldGrant(const FLDDemoContentDefinitions& Content, const FFieldGrant& Grant, FName Event,
        uint64 Serial, int64 XP, const FGuid& ChoiceId, FLDGrowthBuild& Build, FLDProgressionState& Progression);
    // Grants every held blueprint that an eligible active now exists for (each its own uniform draw); true when any was.
    LOCKDOWN_API bool ReleaseHeldBlueprints(const FLDDemoContentDefinitions& Content, const FGuid& MatchSecret, const FGuid& Contender,
        FLDGrowthBuild& Build, FLDProgressionState& Progression);

    // Spec §4.5 gap targets in catalog order: owned mods/connectors never installed, not yet filled, with no compatible
    // entry among the owned components, learned actives and pending blueprints (the innate D and the holder itself always
    // are one). A reaction connector (reaction_empower) has an entry only with at least two distinct held entries.
    LOCKDOWN_API void GapTargets(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, TArray<FName>& Out);
    // The unowned common base components that would give Target its missing entry (its component targets and extra bases),
    // in catalog order. NotAGap / GapAlreadyFilled when Target is no gap target (Out empty).
    LOCKDOWN_API ELDGrowthReason GapFillers(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, FName Target,
        TArray<FName>& Out);
    // FillGap: GROWTH.FillGapPoints moved to PermanentSpentPoints, Filler owned (Source FillGap, inventory) and Target marked
    // filled, atomically. NotRestWindow outside a rest window, NotAGap/GapAlreadyFilled, NoGapFiller, NotEnoughPoints.
    LOCKDOWN_API ELDGrowthReason ApplyFillGap(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current, int64 XP,
        uint8 EmptyPoolGrants, FName Target, FName Filler, const FBuildContext& Context, FLDGrowthBuild& Out, int64& OutPaid);
    // Whether Surrendered is a legal dismantle input: GROWTH.DismantleInputCount distinct owned common components
    // (DismantleInvalid otherwise).
    LOCKDOWN_API ELDGrowthReason DismantleInputReason(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
        const TArray<FName>& Surrendered);
    struct FDismantleOutcome
    {
        FName Received;
        // Installed mods/connectors whose host was surrendered, back in the inventory (history kept).
        TArray<FName> Uninstalled;
    };
    // Dismantle: the surrendered components removed, every install left without its host component (or C12 base) returned
    // to the inventory, and one uniform unowned common component outside the surrendered ones received (Source Dismantle),
    // fixed by Seed. NotRestWindow outside a rest window, DismantleInvalid, DismantlePoolEmpty (nothing removed).
    LOCKDOWN_API ELDGrowthReason ApplyDismantle(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        const TArray<FName>& Surrendered, uint64 Seed, const FBuildContext& Context, FLDGrowthBuild& Out, FDismantleOutcome& Outcome);
    // Private random material of one dismantle, fixed by its request.
    LOCKDOWN_API uint64 DismantleSeed(const FGuid& MatchSecret, const FGuid& Contender, uint64 RequestId);
    // ComponentChoice: the pending choice ChoiceId resolved with Option (owned, Source EliteChoice) or, when Option is None and
    // every option is already owned, with GROWTH.EmptyComponentPoolPoints (OutEmptyPoolGrant). Gated like ActiveChoice.
    // NoComponentChoice, InvalidOption (not an option), ComponentOwned (the option is owned, or None while one is free).
    LOCKDOWN_API ELDGrowthReason ApplyComponentChoice(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        const FLDProgressionState& Progression, const FGuid& ChoiceId, FName Option, const FBuildContext& Context,
        FLDGrowthBuild& Out, FLDProgressionState& OutProgression, bool& OutEmptyPoolGrant);
}