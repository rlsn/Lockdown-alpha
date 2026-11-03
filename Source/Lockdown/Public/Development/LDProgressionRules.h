#pragma once

#include "CoreMinimal.h"
#include "Development/LDGrowthProtocol.h"

class FLDDemoContentSnapshot;
struct FLDDemoProgressionDefinition;
struct FLDDemoContentParameters;

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
    // Weighted draw of three distinct options without replacement; deterministic in Seed.
    LOCKDOWN_API ELDGrowthReason DrawThree(const TArray<FLegalOption>& Pool, int64 Weight, uint64 Seed,
        TArray<FLegalOption>& Out);
    // Private per-opportunity random material; a retry of the same ordinal reuses it.
    LOCKDOWN_API uint64 OfferSeed(const FGuid& MatchSecret, const FGuid& Contender, uint8 Ordinal);
    LOCKDOWN_API bool OptionStillLegal(const FLDDemoContentSnapshot& Catalog, const FLDGrowthBuild& Build,
        const FLDChoiceOption& Option);
    // Applies one option. A new active with both Q/W occupied needs a confirmed replacement naming the exact
    // slot, old skill and old rank; the old skill and its rank are forgotten and the new one starts at rank 1.
    // Every other option rejects a replacement. Revision is not changed here.
    LOCKDOWN_API ELDGrowthReason ApplyOption(const FLDDemoContentSnapshot& Catalog, const FLDGrowthBuild& Current,
        const FLDChoiceOption& Option, const FReplacement* Replacement, FLDGrowthBuild& Out, FAppliedOption& Detail);
}
