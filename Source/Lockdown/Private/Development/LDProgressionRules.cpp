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

using FProgressionRows = TArray<int32, TInlineAllocator<32>>;

// Uniform draw of Count rows without replacement from the Pool indices in Remaining, continuing State.
ELDGrowthReason ProgressionDrawDistinct(const TArray<LDProgression::FLegalOption>& Pool, FProgressionRows Remaining, int32 Count,
    int64 Weight, uint64& State, TArray<LDProgression::FLegalOption>& Out)
{
    for (int32 Draw = 0; Draw < Count; ++Draw)
    {
        int64 Total = 0;
        if (!ProgressionCheckedMul(Weight, Remaining.Num(), Total) || Total <= 0)
        {
            return ELDGrowthReason::ArithmeticOverflow;
        }
        // One approved positive weight for every option: the weighted pick is uniform over remaining rows.
        const uint64 Roll = ProgressionSplitMix64(State) % uint64(Total);
        const int32 Pick = int32(Roll / uint64(Weight));
        Out.Add(Pool[Remaining[Pick]]);
        Remaining.RemoveAt(Pick);
    }
    return ELDGrowthReason::None;
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

ELDGrowthReason LDProgression::DrawThree(const TArray<FLegalOption>& Pool, int64 Weight, uint64 Seed, TArray<FLegalOption>& Out)
{
    Out.Reset();
    if (Weight <= 0 || Pool.Num() < 3)
    {
        return ELDGrowthReason::DataInvalid;
    }
    FProgressionRows Remaining;
    for (int32 Index = 0; Index < Pool.Num(); ++Index)
    {
        Remaining.Add(Index);
    }
    uint64 State = Seed;
    const ELDGrowthReason Reason = ProgressionDrawDistinct(Pool, MoveTemp(Remaining), 3, Weight, State, Out);
    if (Reason != ELDGrowthReason::None)
    {
        Out.Reset();
    }
    return Reason;
}

ELDGrowthReason LDProgression::OpeningCandidates(const FLDDemoProgressionDefinition& Progression, const TArray<FLegalOption>& Pool,
    TArray<int32>& OutAttributes, TArray<int32>& OutPassives)
{
    OutAttributes.Reset();
    OutPassives.Reset();
    if (Progression.OpeningAttributeCount < 0 || Progression.OpeningPassiveCount < 0
        || Progression.OpeningAttributeCount + Progression.OpeningPassiveCount != 3)
    {
        return ELDGrowthReason::DataInvalid;
    }
    for (int32 Index = 0; Index < Pool.Num(); ++Index)
    {
        const FLegalOption& Row = Pool[Index];
        if (Row.Kind == ELDChoiceOptionKind::Attribute && Progression.OpeningAttributeIds.ContainsByPredicate(
            [&Row](const FString& Id) { return FName(*Id) == Row.DefinitionId; }))
        {
            OutAttributes.Add(Index);
        }
        else if (Row.Kind == ELDChoiceOptionKind::LearnPassive)
        {
            OutPassives.Add(Index);
        }
    }
    // A short group is refused explicitly; the opening offer never borrows cards from outside its composition.
    return OutAttributes.Num() >= Progression.OpeningAttributeCount && OutPassives.Num() >= Progression.OpeningPassiveCount
        ? ELDGrowthReason::None : ELDGrowthReason::DataInvalid;
}

ELDGrowthReason LDProgression::DrawOpening(const FLDDemoProgressionDefinition& Progression, const TArray<FLegalOption>& Pool,
    int64 Weight, uint64 Seed, TArray<FLegalOption>& Out)
{
    Out.Reset();
    TArray<int32> Attributes;
    TArray<int32> Passives;
    const ELDGrowthReason PoolReason = OpeningCandidates(Progression, Pool, Attributes, Passives);
    if (Weight <= 0 || PoolReason != ELDGrowthReason::None)
    {
        return ELDGrowthReason::DataInvalid;
    }
    uint64 State = Seed;
    ELDGrowthReason Reason = ProgressionDrawDistinct(Pool, FProgressionRows(Attributes), Progression.OpeningAttributeCount,
        Weight, State, Out);
    if (Reason == ELDGrowthReason::None)
    {
        Reason = ProgressionDrawDistinct(Pool, FProgressionRows(Passives), Progression.OpeningPassiveCount, Weight, State, Out);
    }
    if (Reason != ELDGrowthReason::None)
    {
        Out.Reset();
        return Reason;
    }
    // The card positions are shuffled too, so the passive is not always the last card.
    for (int32 Index = Out.Num() - 1; Index > 0; --Index)
    {
        Out.Swap(Index, int32(ProgressionSplitMix64(State) % uint64(Index + 1)));
    }
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::DrawOffer(const FLDDemoProgressionDefinition& Progression, const TArray<FLegalOption>& Pool,
    uint8 Ordinal, int64 Weight, uint64 Seed, TArray<FLegalOption>& Out)
{
    if (Ordinal == 0)
    {
        Out.Reset();
        return ELDGrowthReason::DataInvalid;
    }
    return Ordinal == 1 ? DrawOpening(Progression, Pool, Weight, Seed, Out) : DrawThree(Pool, Weight, Seed, Out);
}

uint64 LDProgression::OfferSeed(const FGuid& MatchSecret, const FGuid& Contender, uint8 Ordinal)
{
    uint32 Words[9] = {MatchSecret.A, MatchSecret.B, MatchSecret.C, MatchSecret.D,
        Contender.A, Contender.B, Contender.C, Contender.D, Ordinal};
    return CityHash64(reinterpret_cast<const char*>(Words), sizeof(Words));
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
        Out.Skills.Add({Option.DefinitionId, 1, true, Replacement->Slot});
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
