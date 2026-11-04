#pragma once

#include "CoreMinimal.h"
#include "Development/LDGrowthProtocol.h"

class FLDDemoContentSnapshot;
struct FLDDemoProgressionDefinition;
struct FLDDemoContentParameters;
struct FLDDemoContentDefinitions;

// Persistent contender build owned by ALDDevelopmentDirector (PEE §3). Level is stored once here.
struct FLDLearnedSkill
{
    FName Id;
    uint8 Rank = 0;
    bool bActive = false;
    // Active skills only: 0 = Q, 1 = W.
    uint8 Slot = 0;
};

struct FLDAttributeCardCount
{
    FName Id;
    uint8 Count = 0;
};

struct LOCKDOWN_API FLDGrowthBuild
{
    uint8 Level = 1;
    // Sorted by Id; one row per learned definition.
    TArray<FLDLearnedSkill> Skills;
    // Sorted by Id; one row per attribute card taken at least once.
    TArray<FLDAttributeCardCount> Cards;
    uint64 Revision = 1;

    const FLDLearnedSkill* FindSkill(FName Id) const;
    // Index into Skills of the active in Slot, or INDEX_NONE.
    int32 ActiveInSlot(uint8 Slot) const;
    // Every learned rank plus every attribute card is one consumed choice.
    int32 ChoicesInvested() const;
};

struct FLDChoiceOfferState
{
    FGuid OfferId;
    uint8 Ordinal = 0;
    // 112 §6: this offer already replaced the opportunity's first offer with a free reroll (one per offer).
    bool bRerolled = false;
    TArray<FLDChoiceOption> Options;
    uint64 GeneratedBuildRevision = 0;
    uint64 GuardBuildRevision = 0;
    uint64 Revision = 0;
};

struct FLDChoiceRecord
{
    uint8 Ordinal = 0;
    FGuid OfferId;
    FGuid OptionId;
    uint64 RequestId = 0;
};

struct FLDProgressionState
{
    int64 CumulativeXP = 0;
    uint64 Revision = 1;
    // Opportunities are contiguous ordinals 1..EarnedThrough; the queue head is ConsumedThrough + 1.
    // Ordinal 1 is the opening choice granted by BeginMatch; ordinal k (k >= 2) is earned by reaching level k.
    uint8 EarnedThrough = 0;
    uint8 ConsumedThrough = 0;
    TOptional<FLDChoiceOfferState> Offer;
    uint64 LastOfferRevision = 0;
    // One record per consumed opportunity for the whole match (at most MaxChoices).
    TArray<FLDChoiceRecord> Records;
    // 112 §6: free rerolls spent this match; the earned ones follow the level (LDProgression::RerollTokensEarned).
    uint8 RerollsUsed = 0;

    uint8 PendingCount() const { return EarnedThrough - ConsumedThrough; }
};

namespace LDProgression
{
    struct FRewardAmounts
    {
        int64 Gold = 0;
        int64 XP = 0;
    };

    struct FExperienceChange
    {
        int64 NewXP = 0;
        uint8 LevelBefore = 0;
        uint8 LevelAfter = 0;
        uint8 EarnedThroughAfter = 0;
    };

    struct FLegalOption
    {
        ELDChoiceOptionKind Kind = ELDChoiceOptionKind::None;
        FName DefinitionId;
        uint8 FromRank = 0;
        uint8 ToRank = 0;
    };

    struct FReplacement
    {
        uint8 Slot = 0;
        FName OldSkillId;
        uint8 OldRank = 0;
        bool bConfirmed = false;
    };

    struct FAppliedOption
    {
        ELDChoiceOptionKind Kind = ELDChoiceOptionKind::None;
        FName DefinitionId;
        uint8 NewRank = 0;
        bool bReplaced = false;
        uint8 Slot = 0;
        FName ReplacedId;
        uint8 ReplacedRank = 0;
    };

    // Highest level whose cumulative threshold XP has reached; thresholds are strictly increasing from 0.
    LOCKDOWN_API ELDGrowthReason LevelForXP(const FLDDemoProgressionDefinition& Progression, int64 XP, uint8& OutLevel);
    // XP is cumulative int64. The opening opportunity is already earned (EarnedThrough == level); each level crossed
    // earns exactly one more, so EarnedThrough follows the level and never passes MaxLevel (= MaxChoices).
    LOCKDOWN_API ELDGrowthReason PlanExperience(const FLDDemoProgressionDefinition& Progression, int64 CurrentXP,
        uint8 CurrentLevel, uint8 EarnedThrough, int64 Grant, FExperienceChange& Out);
    // REWARD.PlayerXP/PlayerGold at the frozen victim level.
    LOCKDOWN_API ELDGrowthReason ContenderKillReward(const FLDDemoContentParameters& Parameters, uint8 MaxLevel,
        uint8 VictimLevel, FRewardAmounts& Out);
    // CAMP.<tier>.RewardXP/RewardGold of the monster definition, once per member life.
    LOCKDOWN_API ELDGrowthReason MonsterKillReward(const FLDDemoContentSnapshot& Catalog, FName MonsterId, FRewardAmounts& Out);

    // Every legal semantic option for Build, sorted by (DefinitionId, Kind). Unlearned skills offer rank 1,
    // learned skills below MaxSkillRank offer the next rank, and every attribute card is always legal.
    LOCKDOWN_API ELDGrowthReason LegalPool(const FLDDemoContentSnapshot& Catalog, const FLDGrowthBuild& Build,
        TArray<FLegalOption>& Out);
    // 112 §6 draw input. Pool is LegalPool of Build; the draw itself keeps only draftable skills.
    struct FDrawInput
    {
        // The approved definitions (a snapshot's Get(); tests may pass an edited copy).
        const FLDDemoContentDefinitions* Content = nullptr;
        const FLDGrowthBuild* Build = nullptr;
        // Definitions of the equipped items (one per held instance); their tags join the build for tag affinity.
        TArray<FName> Equipment;
        uint8 Ordinal = 0;
        uint64 Seed = 0;
        // Reroll: the cards of the replaced offer, avoided wherever the remaining pool still allows another card.
        TArray<FLegalOption> Avoid;
    };

    // Tags of Option's definition shared with the rest of the build: every learned skill except Option's own definition
    // and every equipped item. Attribute cards carry no tags.
    LOCKDOWN_API int32 SharedTagCount(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
        const TArray<FName>& Equipment, const FLegalOption& Option);
    // Skill-pool weight in units of 1e-8: kind weight (upgrade, new active by Q/W fullness, new passive) times
    // min(AffinityStep^k, AffinityCap) with k = SharedTagCount. DataInvalid for attribute cards and unknown definitions.
    LOCKDOWN_API ELDGrowthReason SkillWeight(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
        const TArray<FName>& Equipment, const FLegalOption& Option, int64& OutWeight);
    // Whether the draw of opportunity Ordinal can always fill three distinct cards from Pool, whatever the seed.
    LOCKDOWN_API bool DrawFeasible(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
        const TArray<FLegalOption>& Pool, uint8 Ordinal);
    // Opportunity 1 (opening): OpeningActiveCount new draftable actives and OpeningPassiveCount new draftable passives,
    // uniform within each group, in shuffled positions. Later opportunities: the first two cards from the weighted skill
    // pool, the third from the attribute pool with ThirdSlotAttributeChance (equal weights) and from the skill pool
    // otherwise; at most one attribute card; with Q/W full at most one new active. Every pick is without replacement.
    // Deterministic in Seed.
    LOCKDOWN_API ELDGrowthReason DrawOffer(const FDrawInput& Input, const TArray<FLegalOption>& Pool, TArray<FLegalOption>& Out);
    // Private per-opportunity random material; a retry of the same ordinal reuses it.
    LOCKDOWN_API uint64 OfferSeed(const FGuid& MatchSecret, const FGuid& Contender, uint8 Ordinal);
    // The private material of the one reroll of opportunity Ordinal.
    LOCKDOWN_API uint64 RerollSeed(const FGuid& MatchSecret, const FGuid& Contender, uint8 Ordinal);
    // Free rerolls earned by reaching Level (one per reached RerollLevels entry).
    LOCKDOWN_API uint8 RerollTokensEarned(const FLDDemoProgressionDefinition& Progression, uint8 Level);
    // 112 §6: a new active replacing a learned one starts at max(1, old rank - 1).
    inline uint8 ReplacementRank(uint8 OldRank) { return OldRank > 1 ? uint8(OldRank - 1) : uint8(1); }
    LOCKDOWN_API bool OptionStillLegal(const FLDDemoContentSnapshot& Catalog, const FLDGrowthBuild& Build,
        const FLDChoiceOption& Option);
    // Applies one option. A new active with both Q/W occupied needs a confirmed replacement naming the exact
    // slot, old skill and old rank; the old skill is forgotten and the new one starts at ReplacementRank(old rank).
    // Every other option rejects a replacement. Revision is not changed here.
    LOCKDOWN_API ELDGrowthReason ApplyOption(const FLDDemoContentSnapshot& Catalog, const FLDGrowthBuild& Current,
        const FLDChoiceOption& Option, const FReplacement* Replacement, FLDGrowthBuild& Out, FAppliedOption& Detail);
}
