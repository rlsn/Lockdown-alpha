#pragma once

#include "CoreMinimal.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDMatchTimeline.h"

class FLDDemoContentSnapshot;
struct FLDBuildProjectionInput;
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
    // The branch path chosen by RankUpSkill; its rank follows from it (None = rank 1). Forgetting or replacing the active
    // drops the entry, so its spent skill points come back (LDProgression::SkillPoints).
    ELDSkillBranch Branch = ELDSkillBranch::None;
};

// Spec §6.1: where an installed card sits (Kind None = in the library): the primary basic, one learned skill (by id), the D
// dash or the body; Element is the chosen element of a card that offers element choices (Y25).
struct FLDCardTarget
{
    ELDCardTargetKind Kind = ELDCardTargetKind::None;
    FName Active;
    ELDDemoElement Element = ELDDemoElement::None;

    bool operator==(const FLDCardTarget& Other) const
    {
        return Kind == Other.Kind && Active == Other.Active && Element == Other.Element;
    }
    bool IsEmpty() const { return *this == FLDCardTarget(); }
};

// One owned card; copies of one definition are separate cards (each its own serial and capacity slot).
struct FLDCardInstance
{
    uint16 Serial = 0;
    FName Id;
    // The offer ordinal that gave it; 0 for a DEV fixture card.
    uint16 OfferOrdinal = 0;
    FLDCardTarget Target;

    bool IsInstalled() const { return Target.Kind != ELDCardTargetKind::None; }
};

// A final mod's source: one learned active or the innate D.
struct FLDFinalModTarget
{
    ELDFinalModTargetKind Kind = ELDFinalModTargetKind::None;
    FName Definition;

    bool operator==(const FLDFinalModTarget& Other) const { return Kind == Other.Kind && Definition == Other.Definition; }
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
    // Owned cards sorted by serial; NextCardSerial is the serial of the next card received (never reused in a match).
    TArray<FLDCardInstance> Cards;
    uint16 NextCardSerial = 1;
    // Independent of the ordinary workshop capacity, actives and pending blueprints.
    FLDFinalModSelection FinalMod;
    // DEV breakout presets only (LDBreakoutPresets): extra skill points for ranks beyond the level's; 0 in play.
    int32 DevSkillPoints = 0;
    // Spec §3.13: skill points bought in the rest shop (at most SHOP.SkillPointMaxPerMatch per match).
    int32 BoughtSkillPoints = 0;
    // Spec §11.2: skill points from clearing an audit-affix camp (at most AFFIX.AuditSkillPointsMax per match).
    int32 AffixSkillPoints = 0;
    uint64 Revision = 1;

    const FLDLearnedActive* FindActive(FName Id) const;
    // Index into Actives of the active in Slot, or INDEX_NONE.
    int32 ActiveInSlot(uint8 Slot) const;
    bool HasBlueprint(FName Id) const;
    const FLDCardInstance* FindCard(uint16 Serial) const;
    FLDCardInstance* FindCard(uint16 Serial);
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

// Spec §6.4: the current card offer. Options are fixed until the offer is taken, skipped or rerolled (a reroll keeps the
// locked card); Rerolls counts this ordinal's rerolls (its seed index).
struct FLDCardOfferState
{
    FGuid OfferId;
    uint16 Ordinal = 0;
    ELDCardOfferSource Source = ELDCardOfferSource::None;
    TArray<FName> Options;
    uint64 Revision = 0;
    uint32 Rerolls = 0;
};

// Spec §3.13: one rest shop slot of the current stock.
struct FLDShopSlot
{
    ELDShopItemKind Kind = ELDShopItemKind::None;
    FName Definition;
    bool bSold = false;
};

// The contender's rest shop: the current stock (SHOP.SkillSlots skills, then SHOP.CardSlots cards), drawn at every mid-match rest
// start and by each refresh; Refreshes counts this rest's refreshes (the price rises with them), Stocks every stock of the
// match (its seed index). Revision advances with every restock and purchase.
struct FLDShopState
{
    bool bStocked = false;
    // Spec §11.1 sale day: every price of this shop is DAILY.SalePriceScale (rounded up).
    bool bSale = false;
    uint64 Revision = 0;
    TArray<FLDShopSlot> Slots;
    int32 Refreshes = 0;
    uint32 Stocks = 0;
};

// One immutable final-rest offer; the selected rule and its binding live in FLDGrowthBuild.
struct FLDFinalModState
{
    FGuid OfferId;
    TArray<FName> Options;
    bool bLocked = false;

    bool IsPending() const { return OfferId.IsValid() && !bLocked; }
};

struct FLDProgressionState
{
    int64 CumulativeXP = 0;
    uint64 Revision = 1;
    // Active opportunities are the ordinals 1..ActiveEarned in the order they were earned; the queue head is
    // ActiveConsumed + 1. Ordinal 1 is the opening one granted by BeginMatch; each level of
    // progression.active_offer_levels reached adds one more, so ActiveEarned == LDProgression::ActiveOpportunitiesAt(level).
    uint8 ActiveEarned = 0;
    uint8 ActiveConsumed = 0;
    // The head's offer; unset while the queue is empty or no active is eligible for the head.
    TOptional<FLDActiveOfferState> Offer;
    uint64 LastOfferRevision = 0;
    TArray<FLDActiveOpportunityRecord> Records;
    // Spec §6.4 card offer queue: the sources of the earned and unresolved offers, head first; CardOffersEarned numbers them
    // (the head's ordinal is CardOffersEarned - CardQueue.Num() + 1). The head's offer is drawn once the opening opportunity is
    // resolved.
    TArray<ELDCardOfferSource> CardQueue;
    uint16 CardOffersEarned = 0;
    TOptional<FLDCardOfferState> CardOffer;
    uint64 LastCardOfferRevision = 0;
    // CARDS.FreeRerolls at match start plus CARDS.RerollsPerRest at each mid-match rest, and the ones used.
    int32 CardRerollsGranted = 0;
    int32 CardRerollsUsed = 0;
    // #198 G: high-tier camp blueprints held because no active was eligible, and how many were ever held (seed index).
    uint8 HeldBlueprints = 0;
    uint32 HeldBlueprintsEver = 0;
    // #198 G: every field reward received, oldest first.
    TArray<FLDFieldRewardView> FieldRewards;
    // The final mod offer and the build lock it ends in.
    FLDFinalModState FinalMod;
    bool bExtraFinalModCard = false;
    // Spec §3.13 vouchers (match-only, 0 at the start) and the rest shop; §11.3 pick tickets, whether the first Ancient clear's
    // ticket was taken (once per match) and every card an offer or a shelf showed (sorted by id; what a ticket may name).
    int32 Vouchers = 0;
    FLDShopState Shop;
    int32 PickTickets = 0;
    bool bAncientTicketTaken = false;
    TArray<FName> SeenCards;

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

    // Where the match is for a build command (ALDGameMode::ReadBuildStage): Live on the field segments; Rest in a rest's
    // build step; RestLocked in its return countdown, which closes only the NPC services (#222); Closed otherwise (results
    // and every other phase).
    enum class EMatchStage : uint8 { Live, Rest, RestLocked, Closed };

    // The server's view of one contender when a build command commits.
    struct FBuildContext
    {
        EMatchStage Stage = EMatchStage::Closed;
        ELDLifePhase LifePhase = ELDLifePhase::Unspawned;
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
        // StoredAsBlueprint after asking to learn: NoEmptySlot; None when not asked.
        ELDGrowthReason StoredReason = ELDGrowthReason::None;
    };

    // Highest level whose cumulative threshold XP has reached; thresholds are strictly increasing from 0.
    LOCKDOWN_API ELDGrowthReason LevelForXP(const FLDDemoProgressionDefinition& Progression, int64 XP, uint8& OutLevel);
    // The opening opportunity plus one per progression.active_offer_levels entry at or below Level.
    LOCKDOWN_API uint8 ActiveOpportunitiesAt(const FLDDemoProgressionDefinition& Progression, uint8 Level);
    // XP is cumulative int64 and keeps accumulating past the top level. LevelActiveEarned is the earned opportunities and
    // must follow CurrentLevel (ActiveOpportunitiesAt); the change reports them at the new level.
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
    // crossed, at the top level GROWTH.MaxLevelPoints for every full GROWTH.MaxLevelPointsXP past its threshold.
    LOCKDOWN_API ELDGrowthReason EarnedPoints(const FLDDemoContentDefinitions& Content, uint8 Level, int64 XP, int64& Out);
    // Earned minus allocated; AuthorityFault when they exceed what was earned.
    LOCKDOWN_API ELDGrowthReason UnspentPoints(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, int64 XP,
        int64& Out);
    // The level-1 build of a match start: no actives, blueprints, cards or allocated points.
    LOCKDOWN_API FLDGrowthBuild StartBuild(const FLDDemoContentDefinitions& Content);
    // Actives by slot, blueprints by id and cards by serial, the order ValidateBuild requires.
    LOCKDOWN_API void NormalizeBuild(FLDGrowthBuild& Build);
    // Build has the shape this catalog allows: one point count per stat, at most three distinct
    // known actives in distinct slots 0..2, distinct known blueprints sorted by id that are not learned, and known cards with
    // distinct serials below NextCardSerial, sorted, each installed on a target of its form (CardTargetReason), no more installed
    // than CardCapacity of its level.
    LOCKDOWN_API ELDGrowthReason ValidateBuild(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build);
    // The actives opportunity Ordinal may draw for Build, in catalog order: the opening pool (abilities flagged opening)
    // for ordinal 1, every active afterwards; learned actives and pending blueprints
    // never. A forgotten active is eligible again.
    LOCKDOWN_API ELDGrowthReason EligibleActives(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
        uint8 Ordinal, TArray<FName>& Out);
    // Up to three distinct eligible actives, uniformly without replacement and deterministic in Seed (fewer when fewer
    // are eligible, none when none is).
    LOCKDOWN_API ELDGrowthReason DrawActiveOffer(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
        uint8 Ordinal, uint64 Seed, TArray<FName>& Out);
    // Private per-opportunity random material; a redraw of the same ordinal reuses it.
    LOCKDOWN_API uint64 OfferSeed(const FGuid& MatchSecret, const FGuid& Contender, uint8 Ordinal);

    // Whether Context may learn an active into a slot now (#222): in every stage of a running match (Live, Rest and
    // RestLocked), alive or dead. PhaseClosed or LifePhaseChanged otherwise.
    LOCKDOWN_API ELDGrowthReason LearnGate(const FBuildContext& Context);
    // The legality matrix of one command kind (GROWTH_TRANSACTIONS). #222: the own build commands (ActiveChoice,
    // LearnBlueprint, AllocatePoints, RankUpSkill, the card commands and UsePickTicket) are open in Live, Rest and RestLocked,
    // alive or dead. The rest shop and the trainer keep the rest window: the shop commands and Respec are NotRestWindow on the
    // field and ReplaceActive/ForgetActive ReplacementNotAllowed, all BuildLocked in RestLocked and Dead for a dead contender.
    // Closed: PhaseClosed.
    LOCKDOWN_API ELDGrowthReason CommandGate(ELDBuildCommandKind Kind, const FBuildContext& Context);
    // The shared-room NPC required by a command, or None when no NPC proximity is needed.
    LOCKDOWN_API ELDRestService RestServiceFor(ELDBuildCommandKind Kind);
    // The slot a learn takes: Preferred when it is an empty slot 0..2, otherwise the lowest empty one; false when full.
    LOCKDOWN_API bool PickLearnSlot(const FLDGrowthBuild& Build, uint8 Preferred, uint8& OutSlot);

    // An offered active becomes the contender's: learned in the same commit when bLearnIfPossible, LearnGate allows it and
    // a slot is empty, otherwise a pending blueprint. Option must be an active that is neither learned nor pending.
    // Revision is not changed here.
    LOCKDOWN_API ELDGrowthReason ApplyActiveChoice(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        FName Option, bool bLearnIfPossible, uint8 PreferredSlot, const FBuildContext& Context, FLDGrowthBuild& Out,
        FChoiceOutcome& Outcome);
    // SkipActiveOffer: VOUCHER.ActiveSkip vouchers (OutVouchers) replace the chosen active.
    LOCKDOWN_API ELDGrowthReason ApplySkipActiveOffer(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        const FLDProgressionState& Progression, const FBuildContext& Context, FLDProgressionState& OutProgression,
        int32& OutVouchers);
    // A pending blueprint learned into an empty slot: NoBlueprint, SlotOccupied (a full slot is replaced only by
    // ApplyReplaceActive in a rest window) or a LearnGate reason when refused.
    LOCKDOWN_API ELDGrowthReason ApplyLearnBlueprint(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        FName Blueprint, uint8 Slot, const FBuildContext& Context, FLDGrowthBuild& Out);
    // Unspent points allocated across the stats in one step: one count per stat (design order) summing to at least one and at
    // most the unspent points (NotEnoughPoints), gated by CommandGate.
    LOCKDOWN_API ELDGrowthReason ApplyAllocation(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        int64 XP, const TArray<uint8>& PointCounts, const FBuildContext& Context, FLDGrowthBuild& Out);

    // ---- Trainer services (#198 §4.4/§5); none changes Revision ----

    // Every allocated stat point back to the unspent ones. NotRestWindow outside a rest window, NothingToRespec when nothing
    // is allocated.
    LOCKDOWN_API ELDGrowthReason ApplyRespec(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current, int64 XP,
        const FBuildContext& Context, FLDGrowthBuild& Out, int64& OutRefunded);
    // A pending blueprint learned into the occupied Slot 0..2, the active there forgotten (neither refunded nor kept as a
    // blueprint) and its skill cards back in the library in the same step (ReclaimAttachments; OutReclaimed, by serial).
    // Nothing moves to the new active on its own. ReplacementNotAllowed outside a rest window, OfferOutstanding while an active offer
    // is outstanding, SlotEmpty for an empty slot (LearnBlueprint learns into it), NoBlueprint.
    LOCKDOWN_API ELDGrowthReason ApplyReplaceActive(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        const FLDProgressionState& Progression, FName Blueprint, uint8 Slot, const FBuildContext& Context, FLDGrowthBuild& Out,
        FName& OutForgotten, TArray<uint16>& OutReclaimed);
    // The active in Slot 0..2 forgotten and its attachments reclaimed in the same step (OutReclaimed); it is eligible for
    // later offers again. ReplacementNotAllowed outside a rest window, OfferOutstanding while an active offer is
    // outstanding, SlotEmpty.
    // Skill points (issue #287 spec §4.5): one per SKILL.PointLevels entry the build's level reached plus the ones bought in the
    // rest shop, minus one per rank above 1 of each learned skill. Forgetting or replacing a skill drops its ranks, so their points come back. Negative is a
    // corrupt build (ValidateBuild refuses it).
    LOCKDOWN_API int32 SkillPoints(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build);
    // RankUpSkill: the skill in Slot takes Branch, its next rank, for one skill point; open like AllocatePoints.
    LOCKDOWN_API ELDGrowthReason ApplyRankUpSkill(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        uint8 Slot, ELDSkillBranch Branch, const FBuildContext& Context, FLDGrowthBuild& Out);
    LOCKDOWN_API ELDGrowthReason ApplyForgetActive(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        const FLDProgressionState& Progression, uint8 Slot, const FBuildContext& Context, FLDGrowthBuild& Out, FName& OutForgotten,
        TArray<uint16>& OutReclaimed);
    // Three or four distinct final mods with legal sources, drawn deterministically from Seed. The first is a mechanic
    // card if available; the second is universal; remaining cards are drawn from all remaining legal options.
    LOCKDOWN_API ELDGrowthReason DrawFinalModOptions(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
        uint64 Seed, TArray<FName>& Out, int32 Count = 3);
    LOCKDOWN_API uint64 FinalModSeed(const FGuid& MatchSecret, const FGuid& Contender);
    // The sources Option may bind now: none for a universal card (one empty target), else the innate D and the learned
    // actives that carry its requirement.
    LOCKDOWN_API ELDGrowthReason FinalModTargets(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
        FName Option, TArray<FLDFinalModTarget>& Out);
    // True when Build has no final mod or its binding is one of the chosen card's legal sources now. The build stays
    // adjustable after the final pick, so removing the bound source leaves the card dormant (not projected into combat)
    // until ApplyFinalMod rebinds it.
    LOCKDOWN_API bool FinalModBound(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build);
    // Takes the final mod option Option with its source Target; OutProgression records the choice and closes the final
    // offer (the card is taken once; the rest of the build stays adjustable). Refused with nothing changed: CommandGate(FinalMod)
    // reasons, NoFinalMod without an open final mod of OfferId, InvalidOption for an option outside it, FinalModUnavailable
    // for a source that is not legal now. A dormant chosen card (FinalModBound false) is rebound with the same OfferId and
    // Option under InstallGate, any stage; FinalLocked otherwise once the card is taken or the offer closed.
    LOCKDOWN_API ELDGrowthReason ApplyFinalMod(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        const FLDProgressionState& Progression, const FGuid& OfferId, FName Option, const FLDFinalModTarget& Target,
        const FBuildContext& Context, FLDGrowthBuild& Out, FLDProgressionState& OutProgression);
    // Spec §6.1: every card installed on the Forgotten skill goes back to the library (never dropped or moved); OutReclaimed
    // lists their serials ascending.
    LOCKDOWN_API void ReclaimAttachments(FLDGrowthBuild& Build, FName Forgotten, TArray<uint16>& OutReclaimed);

    // ---- Cards (spec §6) ----

    // Whether a card may be installed, uninstalled or rebound now: LearnGate (open in every stage of a running match).
    LOCKDOWN_API ELDGrowthReason InstallGate(const FBuildContext& Context);

    // Workshop capacity at Level: CARDS.CapacityBase + floor((Level - 1) / CARDS.LevelsPerSlot); 0 for a broken table.
    LOCKDOWN_API int32 CardCapacity(const FLDDemoContentParameters& Parameters, uint8 Level);
    LOCKDOWN_API int32 InstalledCards(const FLDGrowthBuild& Build);
    // The ELDCardVocab bits of the build: its installed cards and learned skills.
    LOCKDOWN_API uint32 BuildVocab(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build);
    // Whether Target is a legal install of the card Id on Build: its form's target kind, a learned skill for a skill card (of a
    // delivery the card names and one that controls when it asks for that), and one of its element choices exactly when it
    // offers them (InvalidTarget otherwise).
    LOCKDOWN_API ELDGrowthReason CardTargetReason(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, FName Id,
        const FLDCardTarget& Target);
    // The place a picked card goes when the picker names none: its form's only place, or for a skill card the learned skill
    // sharing the most vocabulary keys with it (Q/W/E order on ties); a Y25 card takes its first element. False when no legal
    // place exists now (a skill card without a learned skill).
    LOCKDOWN_API bool DefaultCardTarget(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, FName Id,
        FLDCardTarget& Out);
    // Up to CARDS.OfferSize distinct cards: a weighted draw without replacement over every
    // definition (CARDS.VocabWeight when it shares a vocabulary key with BuildVocab, else 1), deterministic in Seed.
    // Size distinct cards (CardOfferSize of the offer's source).
    LOCKDOWN_API ELDGrowthReason DrawCardOffer(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, int32 Size,
        uint64 Seed, TArray<FName>& Out);
    // Private random material of one card offer ordinal and its Reroll-th reroll.
    LOCKDOWN_API uint64 CardOfferSeed(const FGuid& MatchSecret, const FGuid& Contender, uint16 Ordinal, uint32 Reroll);
    // Count more offers of Source join the queue; OutFirst..OutLast are their ordinals. CapacityExceeded past the uint16 range.
    LOCKDOWN_API ELDGrowthReason QueueCardOffers(FLDProgressionState& Progression, ELDCardOfferSource Source, int32 Count,
        uint16& OutFirst, uint16& OutLast);
    // Draws the head offer when none is current, one is queued and the opening opportunity is resolved (bOutDrawn); the revision is
    // the next card offer revision. The carried lock joins it and is consumed.
    LOCKDOWN_API ELDGrowthReason RefreshCardOffer(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
        FLDProgressionState& Progression, const FGuid& MatchSecret, const FGuid& Contender, bool& bOutDrawn);
    LOCKDOWN_API int32 CardRerollsLeft(const FLDProgressionState& Progression);
    // A new card of definition Id in the library (OfferOrdinal 0 for DEV); OutSerial is its serial.
    LOCKDOWN_API ELDGrowthReason ReceiveCard(const FLDDemoContentDefinitions& Content, FLDGrowthBuild& Build, FName Id,
        uint16 OfferOrdinal, uint16& OutSerial);

    struct FCardPickOutcome
    {
        uint16 Serial = 0;
        bool bInstalled = false;
        // Stored after bInstallIfPossible: why it was not installed; None when not asked.
        ELDGrowthReason StoredReason = ELDGrowthReason::None;
    };
    // PickCard: Option of the current offer OfferId goes to the library (installed on Target in the same commit when asked,
    // legal and a slot is free); the offer resolves (a lock on another card carries over). NoCardOffer, NotInOffer.
    LOCKDOWN_API ELDGrowthReason ApplyPickCard(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        const FLDProgressionState& Progression, const FGuid& OfferId, FName Option, bool bInstallIfPossible, const FLDCardTarget& Target,
        const FBuildContext& Context, FLDGrowthBuild& Out, FLDProgressionState& OutProgression, FCardPickOutcome& Outcome);
    // SkipCards: the current offer resolves without a card (its lock carries over) and pays CARDS.SkipVouchers. NoCardOffer.
    LOCKDOWN_API ELDGrowthReason ApplySkipCards(const FLDDemoContentDefinitions& Content, const FLDProgressionState& Progression,
        const FGuid& OfferId, const FBuildContext& Context, FLDProgressionState& OutProgression);
    // RerollCards: one reroll spent and the offer redrawn (the locked card stays; new OfferId and revision). NoCardOffer,
    // NoReroll.
    LOCKDOWN_API ELDGrowthReason ApplyRerollCards(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
        const FLDProgressionState& Progression, const FGuid& OfferId, const FGuid& MatchSecret, const FGuid& Contender,
        const FBuildContext& Context, FLDProgressionState& OutProgression);
    // InstallCard: the owned card Serial onto Target. NotOwned, AlreadyInstalled, InvalidTarget, WorkshopFull. A non-zero
    // Replacing (another installed card) goes back to the library in the same commit and frees its slot (OutUninstalled).
    LOCKDOWN_API ELDGrowthReason ApplyInstallCard(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        uint16 Serial, const FLDCardTarget& Target, const FBuildContext& Context, FLDGrowthBuild& Out, uint16 Replacing = 0,
        TArray<uint16>* OutUninstalled = nullptr);
    // UninstallCard: the installed card back to the library. NotOwned, NotInstalled.
    LOCKDOWN_API ELDGrowthReason ApplyUninstallCard(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        uint16 Serial, const FBuildContext& Context, FLDGrowthBuild& Out);

    // ---- Vouchers, the rest shop and pick tickets (spec §3.13, §11.3) ----

    // Amount (>= 0) more vouchers; ArithmeticOverflow past int32.
    LOCKDOWN_API ELDGrowthReason GrantVouchers(FLDProgressionState& Progression, int32 Amount);
    // Records Cards as seen this match (a pick ticket may name them).
    LOCKDOWN_API void MarkSeenCards(FLDProgressionState& Progression, const TArray<FName>& Cards);
    // A fresh shelf for Build: SHOP.SkillSlots distinct actives neither learned nor pending (uniform; fewer when fewer are
    // eligible), then SHOP.CardSlots distinct cards, the first SHOP.VocabCardSlots weighted SHOP.VocabWeight for a card sharing
    // the build's vocabulary, the rest uniform; deterministic in Seed.
    LOCKDOWN_API ELDGrowthReason DrawShop(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, uint64 Seed,
        TArray<FLDShopSlot>& Out);
    LOCKDOWN_API uint64 ShopSeed(const FGuid& MatchSecret, const FGuid& Contender, uint32 Stock);
    // Draws the next stock into Progression.Shop (its cards seen); bRestStart resets the rest's refresh count (every
    // mid-match rest start).
    LOCKDOWN_API ELDGrowthReason StockShop(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
        FLDProgressionState& Progression, const FGuid& MatchSecret, const FGuid& Contender, bool bRestStart);
    // SHOP.RefreshPrice + SHOP.RefreshStep per refresh already made this rest.
    LOCKDOWN_API int32 ShopRefreshPrice(const FLDDemoContentParameters& Parameters, const FLDShopState& Shop);
    // The price of a shelf item of Kind (and the skill point, ShopSkillPointPrice) in Shop (sale day halves it, rounded up).
    LOCKDOWN_API int32 ShopPrice(const FLDDemoContentParameters& Parameters, ELDShopItemKind Kind, const FLDShopState& Shop);
    LOCKDOWN_API int32 ShopSkillPointPrice(const FLDDemoContentParameters& Parameters, const FLDShopState& Shop);

    // What a shop purchase or a pick ticket gave.
    struct FShopOutcome
    {
        int32 VouchersPaid = 0;
        ELDShopItemKind Kind = ELDShopItemKind::None;
        FName Definition;
        uint16 CardSerial = 0;
        FChoiceOutcome Choice;
    };
    // ShopBuy: Slot of the current stock (Revision) or the skill point (FLDBuildCommand::ShopSkillPointSlot). A skill learns
    // into the lowest empty Q/W/E slot or becomes a pending blueprint, a card goes to the library, a skill point adds one
    // bought skill point. CommandGate(ShopBuy) reasons; NoShop before the first stock, ShopStale, ShopSoldOut, SkillPointLimit,
    // InvalidOption (an active learned or pending since the stock), NotEnoughVouchers.
    LOCKDOWN_API ELDGrowthReason ApplyShopBuy(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        const FLDProgressionState& Progression, uint8 Slot, uint64 Revision, const FBuildContext& Context, FLDGrowthBuild& Out,
        FLDProgressionState& OutProgression, FShopOutcome& Outcome);
    // ShopRefresh: pays ShopRefreshPrice and draws a new stock. NoShop, ShopStale, NotEnoughVouchers.
    LOCKDOWN_API ELDGrowthReason ApplyShopRefresh(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
        const FLDProgressionState& Progression, uint64 Revision, const FGuid& MatchSecret, const FGuid& Contender,
        const FBuildContext& Context, FLDProgressionState& OutProgression, int32& OutPaid);
    // ShopSell: the library card Serial leaves the build for SHOP.SellPrice vouchers. NotOwned, CardInstalled.
    LOCKDOWN_API ELDGrowthReason ApplyShopSell(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        const FLDProgressionState& Progression, uint16 Serial, const FBuildContext& Context, FLDGrowthBuild& Out,
        FLDProgressionState& OutProgression, int32& OutPaid);
    // UsePickTicket: one ticket for Definition: an active neither learned nor pending (learned into the lowest empty slot or
    // kept as a blueprint) or a card this match showed (to the library). NoPickTicket, InvalidOption, NotSeen.
    LOCKDOWN_API ELDGrowthReason ApplyUsePickTicket(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
        const FLDProgressionState& Progression, FName Definition, const FBuildContext& Context, FLDGrowthBuild& Out,
        FLDProgressionState& OutProgression, FShopOutcome& Outcome);
    // The vouchers a camp tier's clear pays (VOUCHER.Camp*).
    LOCKDOWN_API int32 CampClearVouchers(const FLDDemoContentParameters& Parameters, ELDDemoCampTier Tier);

    // ---- Field rewards (spec §6.4, outer parameters §5) ----

    // The growth state an FLDProgressionView publishes, as a build (level, allocated stats, actives, blueprints and cards with
    // their install targets), so a client or the AI evaluates installs with the server's own rules. Revision and the bought
    // skill points are copied; nothing else is invented.
    LOCKDOWN_API FLDGrowthBuild BuildFromView(const FLDDemoContentDefinitions& Content, const FLDProgressionView& View);
    // The complete Combat input for a growth build, including whether the selected final rule still has its source.
    LOCKDOWN_API FLDBuildProjectionInput CombatProjectionInput(const FLDDemoContentDefinitions* Content, const FLDGrowthBuild& Build);
    // Actives a field blueprint may give, in catalog order: every active neither learned nor kept as a blueprint.
    LOCKDOWN_API void EligibleBlueprints(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, TArray<FName>& Out);
    // What one field reward's build part gives (decided before anything installs).
    struct FFieldGrant
    {
        ELDFieldRewardResult Result = ELDFieldRewardResult::None;
        FName Definition;
        // CardOffer: the source the queued offer carries (its size).
        ELDCardOfferSource OfferSource = ELDCardOfferSource::FieldReward;
    };
    // The number of cards an offer of Source shows: CARDS.OfferSize, or a P6 source's own size.
    LOCKDOWN_API int32 CardOfferSize(const FLDDemoContentParameters& Parameters, ELDCardOfferSource Source);
    // Kind's build part for Build, deterministic in Seed. BuildDraw: a fair roll between a uniform eligible active blueprint
    // and one card offer (the card offer when no active is eligible). Blueprint: one uniform eligible active, BlueprintHeld
    // when none is. CardOffer: one card offer. DataInvalid for None.
    LOCKDOWN_API ELDGrowthReason DrawFieldReward(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
        ELDFieldRewardKind Kind, uint64 Seed, FFieldGrant& Out);
    // Private per-event random material of one recipient's field reward.
    LOCKDOWN_API uint64 FieldRewardSeed(const FGuid& MatchSecret, const FGuid& Contender, FName Event, uint64 Serial);
    // Private random material of one held high-tier blueprint (the Index-th held grant of the match, 1-based).
    LOCKDOWN_API uint64 HeldBlueprintSeed(const FGuid& MatchSecret, const FGuid& Contender, uint32 Index);
    // Installs one drawn field grant on Build/Progression (Revision untouched) and records it in Progression.FieldRewards:
    // a blueprint as a pending blueprint, a card offer as one more queued offer, BlueprintHeld as one more held blueprint.
    // DataInvalid when the grant does not fit Build.
    LOCKDOWN_API ELDGrowthReason ApplyFieldGrant(const FLDDemoContentDefinitions& Content, const FFieldGrant& Grant, FName Event,
        uint64 Serial, int64 XP, FLDGrowthBuild& Build, FLDProgressionState& Progression);
    // Grants every held blueprint that an eligible active now exists for (each its own uniform draw); true when any was.
    LOCKDOWN_API bool ReleaseHeldBlueprints(const FLDDemoContentDefinitions& Content, const FGuid& MatchSecret, const FGuid& Contender,
        FLDGrowthBuild& Build, FLDProgressionState& Progression);
}
