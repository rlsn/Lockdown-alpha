#include "Development/LDProgressionRules.h"

#include "Development/LDDemoContentCatalog.h"
#include "Hash/CityHash.h"

namespace
{
bool ProgressionCheckedAdd(int64 A, int64 B, int64& Out)
{
    if ((B > 0 && A > MAX_int64 - B) || (B < 0 && A < MIN_int64 - B))
    {
        return false;
    }
    Out = A + B;
    return true;
}

bool ProgressionCheckedMul(int64 A, int64 B, int64& Out)
{
    if (A < 0 || B < 0 || (B != 0 && A > MAX_int64 / B))
    {
        return false;
    }
    Out = A * B;
    return true;
}

uint64 ProgressionSplitMix64(uint64& State)
{
    uint64 Z = (State += 0x9E3779B97F4A7C15ull);
    Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
    Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
    return Z ^ (Z >> 31);
}

bool ProgressionValidThresholds(const FLDDemoProgressionDefinition& P)
{
    // Opportunity 1 is the match's opening choice (BeginMatch); reaching level k earns opportunity k, so the
    // opportunities equal the level and the whole match grants exactly MaxLevel choices.
    if (P.MaxLevel < 2 || P.MaxLevel > MAX_uint8 || P.XPThresholds.Num() != P.MaxLevel || P.XPThresholds[0] != P.XPStart
        || P.XPStart != 0 || P.MaxChoices != P.MaxLevel || P.MaxSkillRank < 1 || P.MaxSkillRank > MAX_uint8)
    {
        return false;
    }
    for (int32 Index = 1; Index < P.XPThresholds.Num(); ++Index)
    {
        if (P.XPThresholds[Index] <= P.XPThresholds[Index - 1])
        {
            return false;
        }
    }
    return true;
}

const FLDDemoAbilityDefinition* ProgressionFindAbility(const FLDDemoContentSnapshot& Catalog, FName Id)
{
    return Catalog.Get().Abilities.FindByPredicate([&](const FLDDemoAbilityDefinition& Row) { return FName(*Row.Id) == Id; });
}

const FLDDemoAttributeDefinition* ProgressionFindAttribute(const FLDDemoContentSnapshot& Catalog, FName Id)
{
    return Catalog.Get().Attributes.FindByPredicate([&](const FLDDemoAttributeDefinition& Row) { return FName(*Row.Id) == Id; });
}

uint8 ProgressionCardCount(const FLDGrowthBuild& Build, FName Id)
{
    const FLDAttributeCardCount* Card = Build.Cards.FindByPredicate([&](const FLDAttributeCardCount& Row) { return Row.Id == Id; });
    return Card ? Card->Count : 0;
}

bool ProgressionIdLess(FName A, FName B)
{
    return A.ToString().Compare(B.ToString(), ESearchCase::CaseSensitive) < 0;
}

void ProgressionSortBuild(FLDGrowthBuild& Build)
{
    Build.Skills.Sort([](const FLDLearnedSkill& A, const FLDLearnedSkill& B) { return ProgressionIdLess(A.Id, B.Id); });
    Build.Cards.Sort([](const FLDAttributeCardCount& A, const FLDAttributeCardCount& B) { return ProgressionIdLess(A.Id, B.Id); });
}

const FLDDemoAbilityDefinition* ProgressionFindAbilityIn(const FLDDemoContentDefinitions& Content, FName Id)
{
    return Content.Abilities.FindByPredicate([&](const FLDDemoAbilityDefinition& Row) { return FName(*Row.Id) == Id; });
}

const FLDDemoEquipmentDefinition* ProgressionFindEquipment(const FLDDemoContentDefinitions& Content, FName Id)
{
    return Content.Equipment.FindByPredicate([&](const FLDDemoEquipmentDefinition& Row) { return FName(*Row.Id) == Id; });
}

bool ProgressionIsSkill(ELDChoiceOptionKind Kind)
{
    return Kind == ELDChoiceOptionKind::LearnActive || Kind == ELDChoiceOptionKind::UpgradeActive
        || Kind == ELDChoiceOptionKind::LearnPassive || Kind == ELDChoiceOptionKind::UpgradePassive;
}

bool ProgressionDraftable(const FLDDemoContentDefinitions& Content, const LDProgression::FLegalOption& Option)
{
    const FLDDemoAbilityDefinition* Ability = ProgressionFindAbilityIn(Content, Option.DefinitionId);
    return Ability && Ability->Draftable;
}

bool ProgressionQWFull(const FLDGrowthBuild& Build)
{
    return Build.ActiveInSlot(0) != INDEX_NONE && Build.ActiveInSlot(1) != INDEX_NONE;
}

bool ProgressionSameCard(const LDProgression::FLegalOption& A, const LDProgression::FLegalOption& B)
{
    return A.Kind == B.Kind && A.DefinitionId == B.DefinitionId;
}

struct FProgressionCandidate
{
    LDProgression::FLegalOption Option;
    int64 Weight = 0;
};

// Weighted pick without replacement from Candidates, preferring rows outside Avoid while any remain.
bool ProgressionPick(TArray<FProgressionCandidate>& Candidates, const TArray<LDProgression::FLegalOption>& Avoid, uint64& State,
    LDProgression::FLegalOption& Out)
{
    TArray<int32, TInlineAllocator<48>> Rows;
    for (int32 Index = 0; Index < Candidates.Num(); ++Index)
    {
        if (!Avoid.ContainsByPredicate([&](const LDProgression::FLegalOption& Row) { return ProgressionSameCard(Row, Candidates[Index].Option); }))
        {
            Rows.Add(Index);
        }
    }
    if (Rows.IsEmpty())
    {
        for (int32 Index = 0; Index < Candidates.Num(); ++Index) { Rows.Add(Index); }
    }
    int64 Total = 0;
    for (const int32 Row : Rows)
    {
        if (Candidates[Row].Weight <= 0 || !ProgressionCheckedAdd(Total, Candidates[Row].Weight, Total))
        {
            return false;
        }
    }
    if (Total <= 0)
    {
        return false;
    }
    uint64 Roll = ProgressionSplitMix64(State) % uint64(Total);
    for (const int32 Row : Rows)
    {
        if (Roll < uint64(Candidates[Row].Weight))
        {
            Out = Candidates[Row].Option;
            Candidates.RemoveAt(Row);
            return true;
        }
        Roll -= uint64(Candidates[Row].Weight);
    }
    return false;
}

// Rows of the skill pool that a level-up draw may ever pick: draftable new skills and upgrades of draftable skills.
void ProgressionSkillRows(const FLDDemoContentDefinitions& Content, const TArray<LDProgression::FLegalOption>& Pool,
    TArray<LDProgression::FLegalOption>& Skills, TArray<LDProgression::FLegalOption>& Attributes)
{
    for (const LDProgression::FLegalOption& Row : Pool)
    {
        if (Row.Kind == ELDChoiceOptionKind::Attribute)
        {
            Attributes.Add(Row);
        }
        else if (ProgressionIsSkill(Row.Kind) && ProgressionDraftable(Content, Row))
        {
            Skills.Add(Row);
        }
    }
}
}

const FLDLearnedSkill* FLDGrowthBuild::FindSkill(FName Id) const
{
    return Skills.FindByPredicate([&](const FLDLearnedSkill& Skill) { return Skill.Id == Id; });
}

int32 FLDGrowthBuild::ActiveInSlot(uint8 Slot) const
{
    return Skills.IndexOfByPredicate([&](const FLDLearnedSkill& Skill) { return Skill.bActive && Skill.Slot == Slot; });
}

int32 FLDGrowthBuild::ChoicesInvested() const
{
    int32 Total = 0;
    for (const FLDLearnedSkill& Skill : Skills)
    {
        Total += Skill.Rank;
    }
    for (const FLDAttributeCardCount& Card : Cards)
    {
        Total += Card.Count;
    }
    return Total;
}

ELDGrowthReason LDProgression::LevelForXP(const FLDDemoProgressionDefinition& Progression, int64 XP, uint8& OutLevel)
{
    OutLevel = 0;
    if (!ProgressionValidThresholds(Progression) || XP < 0)
    {
        return ELDGrowthReason::DataInvalid;
    }
    uint8 Level = 1;
    for (int32 Index = 1; Index < Progression.XPThresholds.Num(); ++Index)
    {
        if (XP < Progression.XPThresholds[Index])
        {
            break;
        }
        Level = uint8(Index + 1);
    }
    OutLevel = Level;
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::PlanExperience(const FLDDemoProgressionDefinition& Progression, int64 CurrentXP,
    uint8 CurrentLevel, uint8 EarnedThrough, int64 Grant, FExperienceChange& Out)
{
    Out = FExperienceChange();
    if (Grant < 0)
    {
        return ELDGrowthReason::DataInvalid;
    }
    uint8 Before = 0;
    if (const ELDGrowthReason Reason = LevelForXP(Progression, CurrentXP, Before); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (Before != CurrentLevel || EarnedThrough != Before)
    {
        return ELDGrowthReason::DataInvalid;
    }
    int64 NewXP = 0;
    if (!ProgressionCheckedAdd(CurrentXP, Grant, NewXP))
    {
        return ELDGrowthReason::ArithmeticOverflow;
    }
    uint8 After = 0;
    if (const ELDGrowthReason Reason = LevelForXP(Progression, NewXP, After); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    Out.NewXP = NewXP;
    Out.LevelBefore = Before;
    Out.LevelAfter = After;
    Out.EarnedThroughAfter = After;
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ContenderKillReward(const FLDDemoContentParameters& Parameters, uint8 MaxLevel,
    uint8 VictimLevel, FRewardAmounts& Out)
{
    Out = FRewardAmounts();
    if (VictimLevel < 1 || VictimLevel > MaxLevel)
    {
        return ELDGrowthReason::DataInvalid;
    }
    const int64 Steps = VictimLevel - 1;
    int64 XPStep = 0;
    int64 GoldStep = 0;
    if (!ProgressionCheckedMul(Parameters.PlayerXPPerLevel, Steps, XPStep)
        || !ProgressionCheckedMul(Parameters.PlayerGoldPerLevel, Steps, GoldStep)
        || !ProgressionCheckedAdd(Parameters.PlayerXPBase, XPStep, Out.XP) || !ProgressionCheckedAdd(Parameters.PlayerGoldBase, GoldStep, Out.Gold))
    {
        return ELDGrowthReason::ArithmeticOverflow;
    }
    return Out.XP >= 0 && Out.Gold >= 0 ? ELDGrowthReason::None : ELDGrowthReason::DataInvalid;
}

ELDGrowthReason LDProgression::MonsterKillReward(const FLDDemoContentSnapshot& Catalog, FName MonsterId, FRewardAmounts& Out)
{
    Out = FRewardAmounts();
    const FLDDemoMonsterDefinition* Monster = Catalog.Get().Monsters.FindByPredicate(
        [&](const FLDDemoMonsterDefinition& Row) { return FName(*Row.Id) == MonsterId; });
    if (!Monster || Monster->RewardXP < 0 || Monster->RewardGold < 0)
    {
        return ELDGrowthReason::DataInvalid;
    }
    Out.XP = Monster->RewardXP;
    Out.Gold = Monster->RewardGold;
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::LegalPool(const FLDDemoContentSnapshot& Catalog, const FLDGrowthBuild& Build,
    TArray<FLegalOption>& Out)
{
    Out.Reset();
    const FLDDemoContentDefinitions& Content = Catalog.Get();
    const int32 MaxRank = Content.Progression.MaxSkillRank;
    if (MaxRank < 1 || MaxRank > MAX_uint8)
    {
        return ELDGrowthReason::DataInvalid;
    }
    for (const FLDDemoAbilityDefinition& Ability : Content.Abilities)
    {
        const FName Id(*Ability.Id);
        const bool bActive = Ability.Kind == ELDDemoAbilityKind::Active;
        const FLDLearnedSkill* Learned = Build.FindSkill(Id);
        if (!Learned)
        {
            Out.Add({bActive ? ELDChoiceOptionKind::LearnActive : ELDChoiceOptionKind::LearnPassive, Id, 0, 1});
        }
        else if (Learned->bActive != bActive || Learned->Rank < 1 || Learned->Rank > MaxRank)
        {
            return ELDGrowthReason::DataInvalid;
        }
        else if (Learned->Rank < MaxRank)
        {
            Out.Add({bActive ? ELDChoiceOptionKind::UpgradeActive : ELDChoiceOptionKind::UpgradePassive, Id,
                Learned->Rank, uint8(Learned->Rank + 1)});
        }
    }
    for (const FLDDemoAttributeDefinition& Attribute : Content.Attributes)
    {
        const FName Id(*Attribute.Id);
        const uint8 Count = ProgressionCardCount(Build, Id);
        if (Count == MAX_uint8)
        {
            return ELDGrowthReason::DataInvalid;
        }
        Out.Add({ELDChoiceOptionKind::Attribute, Id, Count, uint8(Count + 1)});
    }
    Out.Sort([](const FLegalOption& A, const FLegalOption& B)
    {
        return A.DefinitionId != B.DefinitionId ? ProgressionIdLess(A.DefinitionId, B.DefinitionId) : uint8(A.Kind) < uint8(B.Kind);
    });
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::NewSkillWeight(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
    const FLegalOption& Option, int64& OutWeight)
{
    OutWeight = 0;
    const int64 FullSlot = Content.Parameters.Choice.NewActiveFullSlotWeightBp;
    if ((Option.Kind != ELDChoiceOptionKind::LearnActive && Option.Kind != ELDChoiceOptionKind::LearnPassive)
        || !ProgressionFindAbilityIn(Content, Option.DefinitionId) || FullSlot <= 0 || FullSlot > 10000)
    {
        return ELDGrowthReason::DataInvalid;
    }
    OutWeight = Option.Kind == ELDChoiceOptionKind::LearnActive && ProgressionQWFull(Build) ? FullSlot : 10000;
    return ELDGrowthReason::None;
}

uint32 LDProgression::ActiveGrowthStats(const FLDDemoAbilityDefinition& Ability)
{
    const auto Bit = [](ELDDemoBonusStat Stat) { return 1u << uint32(Stat); };
    uint32 Mask = 0;
    for (const FLDDemoRankDefinition& Rank : Ability.Ranks)
    {
        for (const FLDDemoEffectDefinition& Effect : Rank.Effects)
        {
            Mask |= Effect.ADRatioBp > 0 ? Bit(ELDDemoBonusStat::ADFlat) : 0u;
            Mask |= Effect.APRatioBp > 0 ? Bit(ELDDemoBonusStat::APFlat) : 0u;
            Mask |= Effect.MaxHPRatioBp > 0 ? Bit(ELDDemoBonusStat::MaxHPFlat) : 0u;
        }
    }
    return Mask;
}

int64 LDProgression::OpeningActiveWeight(const FLDDemoContentDefinitions& Content, const FLDDemoOpeningCardDefinition& Card,
    const FLDDemoAbilityDefinition& Ability)
{
    uint32 CardStats = 0;
    for (const FLDDemoStatBonus& Bonus : Card.Stats)
    {
        CardStats |= 1u << uint32(Bonus.Stat);
    }
    return (ActiveGrowthStats(Ability) & CardStats) != 0 ? Content.Progression.OpeningMatchedSkillWeight
        : Content.Progression.OpeningOtherSkillWeight;
}

bool LDProgression::DrawFeasible(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
    const TArray<FLegalOption>& Pool, uint8 Ordinal)
{
    const FLDDemoProgressionDefinition& Progression = Content.Progression;
    if (Ordinal == 0)
    {
        return false;
    }
    if (Ordinal == 1)
    {
        // #153: three distinct opening cards, each hiding a distinct new draftable active of the pool.
        int32 Actives = 0;
        for (const FLegalOption& Row : Pool)
        {
            Actives += Row.Kind == ELDChoiceOptionKind::LearnActive && ProgressionDraftable(Content, Row) ? 1 : 0;
        }
        return Build.OpeningCard.IsNone() && Content.OpeningCards.Num() >= 3 && Actives >= 3
            && Progression.OpeningMatchedSkillWeight >= 1 && Progression.OpeningOtherSkillWeight >= 1;
    }
    TArray<FLegalOption> Skills, Attributes;
    ProgressionSkillRows(Content, Pool, Skills, Attributes);
    int32 Upgrades = 0, NewActives = 0, NewPassives = 0;
    for (const FLegalOption& Row : Skills)
    {
        Upgrades += Row.Kind == ELDChoiceOptionKind::UpgradeActive || Row.Kind == ELDChoiceOptionKind::UpgradePassive ? 1 : 0;
        NewActives += Row.Kind == ELDChoiceOptionKind::LearnActive ? 1 : 0;
        NewPassives += Row.Kind == ELDChoiceOptionKind::LearnPassive ? 1 : 0;
    }
    // With Q/W full only one new active can ever be picked into an offer.
    int32 NewSkills = NewPassives + (ProgressionQWFull(Build) ? FMath::Min(NewActives, 1) : NewActives);
    // Worst case: the free slot rolls an attribute card; a skill slot with nothing left falls back to one too.
    int32 SkillCards = 0;
    if (Upgrades > 0) { ++SkillCards; }
    else if (NewSkills > 0) { ++SkillCards; --NewSkills; }
    if (NewSkills > 0) { ++SkillCards; }
    return Attributes.Num() >= 3 - SkillCards;
}

ELDGrowthReason LDProgression::DrawOffer(const FDrawInput& Input, const TArray<FLegalOption>& Pool, TArray<FLegalOption>& Out)
{
    Out.Reset();
    if (!Input.Content || !Input.Build || !DrawFeasible(*Input.Content, *Input.Build, Pool, Input.Ordinal))
    {
        return ELDGrowthReason::DataInvalid;
    }
    const FLDDemoContentDefinitions& Content = *Input.Content;
    const FLDGrowthBuild& Build = *Input.Build;
    uint64 State = Input.Seed;
    const auto Fail = [&Out](ELDGrowthReason Reason) { Out.Reset(); return Reason; };
    if (Input.Ordinal == 1)
    {
        // #153: three distinct cards, each equally likely; then each card in order hides one distinct new draftable active.
        TArray<FProgressionCandidate> Cards;
        for (const FLDDemoOpeningCardDefinition& Card : Content.OpeningCards)
        {
            Cards.Add({{ELDChoiceOptionKind::Opening, FName(*Card.Id), 0, 1}, 1});
        }
        TArray<LDProgression::FLegalOption> Actives;
        for (const FLegalOption& Row : Pool)
        {
            if (Row.Kind == ELDChoiceOptionKind::LearnActive && ProgressionDraftable(Content, Row)) { Actives.Add(Row); }
        }
        for (int32 Pick = 0; Pick < 3; ++Pick)
        {
            FLegalOption& Card = Out.AddDefaulted_GetRef();
            if (!ProgressionPick(Cards, Input.Avoid, State, Card))
            {
                return Fail(ELDGrowthReason::DataInvalid);
            }
        }
        for (FLegalOption& Card : Out)
        {
            const FLDDemoOpeningCardDefinition* Definition = Content.OpeningCards.FindByPredicate(
                [&Card](const FLDDemoOpeningCardDefinition& Row) { return FName(*Row.Id) == Card.DefinitionId; });
            TArray<FProgressionCandidate> Weighted;
            for (const FLegalOption& Active : Actives)
            {
                if (Out.ContainsByPredicate([&Active](const FLegalOption& Row) { return Row.HiddenActive == Active.DefinitionId; }))
                {
                    continue;
                }
                Weighted.Add({Active, OpeningActiveWeight(Content, *Definition, *ProgressionFindAbilityIn(Content, Active.DefinitionId))});
            }
            FLegalOption Hidden;
            if (!ProgressionPick(Weighted, TArray<FLegalOption>(), State, Hidden))
            {
                return Fail(ELDGrowthReason::DataInvalid);
            }
            Card.HiddenActive = Hidden.DefinitionId;
        }
        return ELDGrowthReason::None;
    }
    TArray<FLegalOption> SkillRows, AttributeRows;
    ProgressionSkillRows(Content, Pool, SkillRows, AttributeRows);
    TArray<FProgressionCandidate> Upgrades, NewSkills, Attributes;
    for (const FLegalOption& Row : SkillRows)
    {
        if (Row.Kind == ELDChoiceOptionKind::UpgradeActive || Row.Kind == ELDChoiceOptionKind::UpgradePassive)
        {
            Upgrades.Add({Row, 1});
            continue;
        }
        FProgressionCandidate& Candidate = NewSkills.AddDefaulted_GetRef();
        Candidate.Option = Row;
        if (const ELDGrowthReason Reason = NewSkillWeight(Content, Build, Row, Candidate.Weight); Reason != ELDGrowthReason::None)
        {
            return Fail(Reason);
        }
    }
    for (const FLegalOption& Row : AttributeRows)
    {
        Attributes.Add({Row, 1});
    }
    const bool bFull = ProgressionQWFull(Build);
    const auto PickNew = [&](FLegalOption& Picked)
    {
        if (!ProgressionPick(NewSkills, Input.Avoid, State, Picked))
        {
            return false;
        }
        if (bFull && Picked.Kind == ELDChoiceOptionKind::LearnActive)
        {
            NewSkills.RemoveAll([](const FProgressionCandidate& Row) { return Row.Option.Kind == ELDChoiceOptionKind::LearnActive; });
        }
        return true;
    };
    const auto PickAttribute = [&](FLegalOption& Picked) { return ProgressionPick(Attributes, Input.Avoid, State, Picked); };
    FLegalOption& Upgrade = Out.AddDefaulted_GetRef();
    const bool bUpgrade = !Upgrades.IsEmpty() ? ProgressionPick(Upgrades, Input.Avoid, State, Upgrade)
        : !NewSkills.IsEmpty() ? PickNew(Upgrade) : PickAttribute(Upgrade);
    FLegalOption& Fresh = Out.AddDefaulted_GetRef();
    const bool bFresh = !NewSkills.IsEmpty() ? PickNew(Fresh) : PickAttribute(Fresh);
    // The roll is always taken, so the free slot's source does not shift the rest of the stream.
    const bool bAttribute = ProgressionSplitMix64(State) % 10000 < uint64(Content.Parameters.Choice.FreeSlotAttributeBp);
    FLegalOption& Free = Out.AddDefaulted_GetRef();
    const bool bFree = bAttribute || NewSkills.IsEmpty() ? PickAttribute(Free) : PickNew(Free);
    return bUpgrade && bFresh && bFree ? ELDGrowthReason::None : Fail(ELDGrowthReason::DataInvalid);
}
uint64 LDProgression::OfferSeed(const FGuid& MatchSecret, const FGuid& Contender, uint8 Ordinal)
{
    uint32 Words[9] = {MatchSecret.A, MatchSecret.B, MatchSecret.C, MatchSecret.D,
        Contender.A, Contender.B, Contender.C, Contender.D, Ordinal};
    return CityHash64(reinterpret_cast<const char*>(Words), sizeof(Words));
}

uint64 LDProgression::RerollSeed(const FGuid& MatchSecret, const FGuid& Contender, uint8 Ordinal)
{
    // A distinct domain word keeps the reroll stream independent of the opportunity's first draw.
    uint32 Words[10] = {MatchSecret.A, MatchSecret.B, MatchSecret.C, MatchSecret.D,
        Contender.A, Contender.B, Contender.C, Contender.D, Ordinal, 0x52524F4Cu};
    return CityHash64(reinterpret_cast<const char*>(Words), sizeof(Words));
}

uint8 LDProgression::RerollTokensEarned(const FLDDemoProgressionDefinition& Progression, uint8 Level)
{
    uint8 Earned = 0;
    for (const int32 RerollLevel : Progression.RerollLevels)
    {
        Earned += RerollLevel <= Level ? 1 : 0;
    }
    return Earned;
}

bool LDProgression::OptionStillLegal(const FLDDemoContentSnapshot& Catalog, const FLDGrowthBuild& Build, const FLDChoiceOption& Option)
{
    const int32 MaxRank = Catalog.Get().Progression.MaxSkillRank;
    switch (Option.Kind)
    {
    case ELDChoiceOptionKind::LearnActive:
    case ELDChoiceOptionKind::LearnPassive:
    {
        const FLDDemoAbilityDefinition* Ability = ProgressionFindAbility(Catalog, Option.DefinitionId);
        const bool bActive = Option.Kind == ELDChoiceOptionKind::LearnActive;
        return Ability && (Ability->Kind == ELDDemoAbilityKind::Active) == bActive && !Build.FindSkill(Option.DefinitionId)
            && Option.FromRank == 0 && Option.ToRank == 1;
    }
    case ELDChoiceOptionKind::UpgradeActive:
    case ELDChoiceOptionKind::UpgradePassive:
    {
        const FLDDemoAbilityDefinition* Ability = ProgressionFindAbility(Catalog, Option.DefinitionId);
        const FLDLearnedSkill* Learned = Build.FindSkill(Option.DefinitionId);
        const bool bActive = Option.Kind == ELDChoiceOptionKind::UpgradeActive;
        return Ability && Learned && (Ability->Kind == ELDDemoAbilityKind::Active) == bActive && Learned->bActive == bActive
            && Learned->Rank == Option.FromRank && Option.ToRank == Option.FromRank + 1 && Option.ToRank <= MaxRank;
    }
    case ELDChoiceOptionKind::Attribute:
        return ProgressionFindAttribute(Catalog, Option.DefinitionId) && ProgressionCardCount(Build, Option.DefinitionId) == Option.FromRank
            && Option.ToRank == Option.FromRank + 1;
    case ELDChoiceOptionKind::Opening:
        return Catalog.Get().OpeningCards.ContainsByPredicate([&Option](const FLDDemoOpeningCardDefinition& Row)
            { return FName(*Row.Id) == Option.DefinitionId; })
            && Build.OpeningCard.IsNone() && Option.FromRank == 0 && Option.ToRank == 1;
    default:
        return false;
    }
}

ELDGrowthReason LDProgression::ApplyOption(const FLDDemoContentSnapshot& Catalog, const FLDGrowthBuild& Current,
    const FLDChoiceOption& Option, const FReplacement* Replacement, FLDGrowthBuild& Out, FAppliedOption& Detail, FName HiddenActive)
{
    Detail = FAppliedOption();
    if (!OptionStillLegal(Catalog, Current, Option) || (Option.Kind == ELDChoiceOptionKind::Opening) == HiddenActive.IsNone())
    {
        return ELDGrowthReason::InvalidOption;
    }
    if (Catalog.Get().Progression.ActiveSlots != 2)
    {
        return ELDGrowthReason::DataInvalid;
    }
    if (Replacement && Option.Kind != ELDChoiceOptionKind::LearnActive)
    {
        return ELDGrowthReason::ReplacementNotAllowed;
    }
    Out = Current;
    Detail.Kind = Option.Kind;
    Detail.DefinitionId = Option.DefinitionId;
    Detail.NewRank = Option.ToRank;
    switch (Option.Kind)
    {
    case ELDChoiceOptionKind::Opening:
    {
        // #153: the hidden active is a new draftable active learned into the first free slot (Q on the opening build).
        const FLDDemoAbilityDefinition* Ability = ProgressionFindAbility(Catalog, HiddenActive);
        const int32 Q = Current.ActiveInSlot(0);
        const int32 W = Current.ActiveInSlot(1);
        if (!Ability || Ability->Kind != ELDDemoAbilityKind::Active || !Ability->Draftable || Current.FindSkill(HiddenActive)
            || (Q != INDEX_NONE && W != INDEX_NONE))
        {
            return ELDGrowthReason::DataInvalid;
        }
        const uint8 Slot = Q == INDEX_NONE ? 0 : 1;
        Out.OpeningCard = Option.DefinitionId;
        Out.Skills.Add({HiddenActive, 1, true, Slot});
        Detail.Slot = Slot;
        Detail.OpeningActive = HiddenActive;
        break;
    }
    case ELDChoiceOptionKind::LearnActive:
    {
        const int32 Q = Current.ActiveInSlot(0);
        const int32 W = Current.ActiveInSlot(1);
        if (Q == INDEX_NONE || W == INDEX_NONE)
        {
            if (Replacement)
            {
                return ELDGrowthReason::ReplacementNotAllowed;
            }
            const uint8 Slot = Q == INDEX_NONE ? 0 : 1;
            Out.Skills.Add({Option.DefinitionId, 1, true, Slot});
            Detail.Slot = Slot;
            break;
        }
        if (!Replacement || !Replacement->bConfirmed)
        {
            return ELDGrowthReason::ReplacementRequired;
        }
        if (Replacement->Slot > 1)
        {
            return ELDGrowthReason::ReplacementMismatch;
        }
        const FLDLearnedSkill& Old = Current.Skills[Replacement->Slot == 0 ? Q : W];
        if (Old.Id != Replacement->OldSkillId || Old.Rank != Replacement->OldRank)
        {
            return ELDGrowthReason::ReplacementMismatch;
        }
        Out.Skills.RemoveAll([&](const FLDLearnedSkill& Skill) { return Skill.Id == Old.Id; });
        // #147 D13: the old skill is forgotten without refund and the new active starts at rank 1.
        Detail.NewRank = 1;
        Out.Skills.Add({Option.DefinitionId, Detail.NewRank, true, Replacement->Slot});
        Detail.bReplaced = true;
        Detail.Slot = Replacement->Slot;
        Detail.ReplacedId = Old.Id;
        Detail.ReplacedRank = Old.Rank;
        break;
    }
    case ELDChoiceOptionKind::LearnPassive:
        Out.Skills.Add({Option.DefinitionId, 1, false, 0});
        break;
    case ELDChoiceOptionKind::UpgradeActive:
    case ELDChoiceOptionKind::UpgradePassive:
    {
        FLDLearnedSkill* Learned = Out.Skills.FindByPredicate([&](const FLDLearnedSkill& Skill) { return Skill.Id == Option.DefinitionId; });
        Learned->Rank = Option.ToRank;
        Detail.Slot = Learned->Slot;
        break;
    }
    case ELDChoiceOptionKind::Attribute:
    {
        FLDAttributeCardCount* Card = Out.Cards.FindByPredicate([&](const FLDAttributeCardCount& Row) { return Row.Id == Option.DefinitionId; });
        if (!Card)
        {
            Card = &Out.Cards.Add_GetRef({Option.DefinitionId, 0});
        }
        Card->Count = Option.ToRank;
        break;
    }
    default:
        return ELDGrowthReason::InvalidOption;
    }
    ProgressionSortBuild(Out);
    return ELDGrowthReason::None;
}
