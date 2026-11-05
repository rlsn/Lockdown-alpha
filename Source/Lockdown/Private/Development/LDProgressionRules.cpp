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

int32 LDProgression::SharedTagCount(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
    const TArray<FName>& Equipment, const FLegalOption& Option)
{
    const FLDDemoAbilityDefinition* Candidate = ProgressionIsSkill(Option.Kind) ? ProgressionFindAbilityIn(Content, Option.DefinitionId) : nullptr;
    if (!Candidate)
    {
        return 0;
    }
    TSet<ELDDemoMechanicTag> BuildTags;
    for (const FLDLearnedSkill& Skill : Build.Skills)
    {
        // Approved choice A: an upgrade candidate does not count its own tags.
        if (Skill.Id == Option.DefinitionId)
        {
            continue;
        }
        if (const FLDDemoAbilityDefinition* Learned = ProgressionFindAbilityIn(Content, Skill.Id))
        {
            BuildTags.Append(Learned->Tags);
        }
    }
    for (const FName Item : Equipment)
    {
        if (const FLDDemoEquipmentDefinition* Held = ProgressionFindEquipment(Content, Item))
        {
            BuildTags.Append(Held->Tags);
        }
    }
    int32 Shared = 0;
    for (const ELDDemoMechanicTag Tag : Candidate->Tags)
    {
        Shared += BuildTags.Contains(Tag) ? 1 : 0;
    }
    return Shared;
}

ELDGrowthReason LDProgression::SkillWeight(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
    const TArray<FName>& Equipment, const FLegalOption& Option, int64& OutWeight)
{
    OutWeight = 0;
    const FLDDemoChoiceWeights& Weights = Content.Parameters.Choice;
    int64 Base = 0;
    switch (Option.Kind)
    {
    case ELDChoiceOptionKind::UpgradeActive:
    case ELDChoiceOptionKind::UpgradePassive: Base = Weights.UpgradeWeightBp; break;
    case ELDChoiceOptionKind::LearnActive:
        Base = ProgressionQWFull(Build) ? Weights.NewActiveFullSlotWeightBp : Weights.NewActiveOpenSlotWeightBp; break;
    case ELDChoiceOptionKind::LearnPassive: Base = Weights.NewPassiveWeightBp; break;
    default: return ELDGrowthReason::DataInvalid;
    }
    if (!ProgressionFindAbilityIn(Content, Option.DefinitionId) || Base <= 0 || Weights.AffinityStepBp < 10000
        || Weights.AffinityCapBp < 10000)
    {
        return ELDGrowthReason::DataInvalid;
    }
    int64 Affinity = 10000;
    for (int32 Shared = SharedTagCount(Content, Build, Equipment, Option); Shared > 0 && Affinity < Weights.AffinityCapBp; --Shared)
    {
        if (!ProgressionCheckedMul(Affinity, Weights.AffinityStepBp, Affinity))
        {
            return ELDGrowthReason::ArithmeticOverflow;
        }
        Affinity /= 10000;
    }
    return ProgressionCheckedMul(Base, FMath::Min(Affinity, Weights.AffinityCapBp), OutWeight)
        ? ELDGrowthReason::None : ELDGrowthReason::ArithmeticOverflow;
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
        int32 Actives = 0, Passives = 0;
        for (const FLegalOption& Row : Pool)
        {
            if (ProgressionDraftable(Content, Row))
            {
                Actives += Row.Kind == ELDChoiceOptionKind::LearnActive ? 1 : 0;
                Passives += Row.Kind == ELDChoiceOptionKind::LearnPassive ? 1 : 0;
            }
        }
        return Progression.OpeningActiveCount >= 0 && Progression.OpeningPassiveCount >= 0
            && Progression.OpeningActiveCount + Progression.OpeningPassiveCount == 3
            && Actives >= Progression.OpeningActiveCount && Passives >= Progression.OpeningPassiveCount;
    }
    TArray<FLegalOption> Skills, Attributes;
    ProgressionSkillRows(Content, Pool, Skills, Attributes);
    const int32 NewActives = Skills.FilterByPredicate([](const FLegalOption& Row) { return Row.Kind == ELDChoiceOptionKind::LearnActive; }).Num();
    // With Q/W full only one new active can ever be picked into an offer.
    const int32 Usable = Skills.Num() - NewActives + (ProgressionQWFull(Build) ? FMath::Min(NewActives, 1) : NewActives);
    return Usable >= 2 && Usable + (Attributes.IsEmpty() ? 0 : 1) >= 3;
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
        // The opening build is empty: every new skill of a group has the same weight.
        TArray<FProgressionCandidate> Actives, Passives;
        for (const FLegalOption& Row : Pool)
        {
            if (!ProgressionDraftable(Content, Row)) { continue; }
            if (Row.Kind == ELDChoiceOptionKind::LearnActive) { Actives.Add({Row, 1}); }
            if (Row.Kind == ELDChoiceOptionKind::LearnPassive) { Passives.Add({Row, 1}); }
        }
        for (const auto& Group : {TPair<TArray<FProgressionCandidate>*, int32>(&Actives, Content.Progression.OpeningActiveCount),
            {&Passives, Content.Progression.OpeningPassiveCount}})
        {
            for (int32 Pick = 0; Pick < Group.Value; ++Pick)
            {
                if (!ProgressionPick(*Group.Key, Input.Avoid, State, Out.AddDefaulted_GetRef()))
                {
                    return Fail(ELDGrowthReason::DataInvalid);
                }
            }
        }
        // The card positions are shuffled, so the passive is not always the last card.
        for (int32 Index = Out.Num() - 1; Index > 0; --Index)
        {
            Out.Swap(Index, int32(ProgressionSplitMix64(State) % uint64(Index + 1)));
        }
        return ELDGrowthReason::None;
    }
    TArray<FLegalOption> SkillRows, AttributeRows;
    ProgressionSkillRows(Content, Pool, SkillRows, AttributeRows);
    TArray<FProgressionCandidate> Skills, Attributes;
    for (const FLegalOption& Row : SkillRows)
    {
        FProgressionCandidate& Candidate = Skills.AddDefaulted_GetRef();
        Candidate.Option = Row;
        if (const ELDGrowthReason Reason = SkillWeight(Content, Build, Input.Equipment, Row, Candidate.Weight); Reason != ELDGrowthReason::None)
        {
            return Fail(Reason);
        }
    }
    for (const FLegalOption& Row : AttributeRows)
    {
        Attributes.Add({Row, 1});
    }
    const bool bFull = ProgressionQWFull(Build);
    const auto PickSkill = [&](FLegalOption& Picked)
    {
        if (!ProgressionPick(Skills, Input.Avoid, State, Picked))
        {
            return false;
        }
        if (bFull && Picked.Kind == ELDChoiceOptionKind::LearnActive)
        {
            Skills.RemoveAll([](const FProgressionCandidate& Row) { return Row.Option.Kind == ELDChoiceOptionKind::LearnActive; });
        }
        return true;
    };
    for (int32 Slot = 0; Slot < 2; ++Slot)
    {
        if (!PickSkill(Out.AddDefaulted_GetRef()))
        {
            return Fail(ELDGrowthReason::DataInvalid);
        }
    }
    // The roll is always taken, so the third card's source does not shift the rest of the stream.
    const bool bAttribute = ProgressionSplitMix64(State) % 10000 < uint64(Content.Parameters.Choice.ThirdSlotAttributeBp);
    FLegalOption& Third = Out.AddDefaulted_GetRef();
    const bool bPicked = (bAttribute && !Attributes.IsEmpty()) || Skills.IsEmpty()
        ? ProgressionPick(Attributes, Input.Avoid, State, Third) : PickSkill(Third);
    return bPicked ? ELDGrowthReason::None : Fail(ELDGrowthReason::DataInvalid);
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
    default:
        return false;
    }
}

ELDGrowthReason LDProgression::ApplyOption(const FLDDemoContentSnapshot& Catalog, const FLDGrowthBuild& Current,
    const FLDChoiceOption& Option, const FReplacement* Replacement, FLDGrowthBuild& Out, FAppliedOption& Detail)
{
    Detail = FAppliedOption();
    if (!OptionStillLegal(Catalog, Current, Option))
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
        // 112 §6: the replacement keeps part of the investment; the old skill is still forgotten without refund.
        Detail.NewRank = FMath::Min<uint8>(ReplacementRank(Old.Rank), uint8(Catalog.Get().Progression.MaxSkillRank));
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
