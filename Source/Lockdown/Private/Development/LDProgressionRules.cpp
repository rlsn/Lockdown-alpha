#include "Development/LDProgressionRules.h"

#include "Development/LDCampGrowth.h"
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

bool ProgressionValidTable(const FLDDemoProgressionDefinition& P)
{
    if (P.MaxLevel < 2 || P.MaxLevel > MAX_uint8 || P.XPThresholds.Num() != P.MaxLevel || P.XPThresholds[0] != P.XPStart
        || P.XPStart != 0 || P.ActiveOfferLevels.Num() >= MAX_uint8)
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
    // Every extra active opportunity is earned by reaching a later level, each level at most once.
    for (int32 Index = 0; Index < P.ActiveOfferLevels.Num(); ++Index)
    {
        const int32 Level = P.ActiveOfferLevels[Index];
        if (Level < 2 || Level > P.MaxLevel || (Index > 0 && Level <= P.ActiveOfferLevels[Index - 1]))
        {
            return false;
        }
    }
    return true;
}

bool ProgressionIdLess(FName A, FName B)
{
    return A.ToString().Compare(B.ToString(), ESearchCase::CaseSensitive) < 0;
}

const FLDDemoAbilityDefinition* ProgressionFindAbility(const FLDDemoContentDefinitions& Content, FName Id)
{
    return Content.Abilities.FindByPredicate([&](const FLDDemoAbilityDefinition& Row) { return FName(*Row.Id) == Id; });
}

void ProgressionSortBuild(FLDGrowthBuild& Build)
{
    Build.Actives.Sort([](const FLDLearnedActive& A, const FLDLearnedActive& B) { return A.Slot < B.Slot; });
    Build.PendingBlueprints.Sort([](FName A, FName B) { return ProgressionIdLess(A, B); });
}
}

const FLDLearnedActive* FLDGrowthBuild::FindActive(FName Id) const
{
    return Actives.FindByPredicate([&](const FLDLearnedActive& Active) { return Active.Id == Id; });
}

int32 FLDGrowthBuild::ActiveInSlot(uint8 Slot) const
{
    return Actives.IndexOfByPredicate([&](const FLDLearnedActive& Active) { return Active.Slot == Slot; });
}

bool FLDGrowthBuild::HasBlueprint(FName Id) const
{
    return PendingBlueprints.Contains(Id);
}

bool FLDGrowthBuild::SpentPoints(int64& Out) const
{
    Out = 0;
    for (const int32 Count : StatPoints)
    {
        if (Count < 0 || !ProgressionCheckedAdd(Out, Count, Out))
        {
            Out = 0;
            return false;
        }
    }
    return true;
}

ELDGrowthReason LDProgression::LevelForXP(const FLDDemoProgressionDefinition& Progression, int64 XP, uint8& OutLevel)
{
    OutLevel = 0;
    if (!ProgressionValidTable(Progression) || XP < 0)
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

uint8 LDProgression::ActiveOpportunitiesAt(const FLDDemoProgressionDefinition& Progression, uint8 Level)
{
    uint8 Earned = 1;
    for (const int32 OfferLevel : Progression.ActiveOfferLevels)
    {
        Earned += OfferLevel <= Level ? 1 : 0;
    }
    return Earned;
}

ELDGrowthReason LDProgression::PlanExperience(const FLDDemoProgressionDefinition& Progression, int64 CurrentXP,
    uint8 CurrentLevel, uint8 LevelActiveEarned, int64 Grant, FExperienceChange& Out)
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
    if (Before != CurrentLevel || LevelActiveEarned != ActiveOpportunitiesAt(Progression, Before))
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
    Out.ActiveEarnedAfter = ActiveOpportunitiesAt(Progression, After);
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
    if (!ProgressionCheckedMul(Parameters.PlayerXPPerLevel, Steps, XPStep)
        || !ProgressionCheckedAdd(Parameters.PlayerXPBase, XPStep, Out.XP))
    {
        return ELDGrowthReason::ArithmeticOverflow;
    }
    return Out.XP >= 0 ? ELDGrowthReason::None : ELDGrowthReason::DataInvalid;
}

ELDGrowthReason LDProgression::MonsterKillReward(const FLDDemoContentSnapshot& Catalog, FName MonsterId, uint8 Stage,
    FRewardAmounts& Out)
{
    Out = FRewardAmounts();
    const FLDDemoMonsterDefinition* Monster = Catalog.Get().Monsters.FindByPredicate(
        [&](const FLDDemoMonsterDefinition& Row) { return FName(*Row.Id) == MonsterId; });
    int64 XP = 0;
    if (!Monster || Monster->RewardXP < 0 || (Monster->Tier == ELDDemoCampTier::Boss && Stage != 0)
        || !LDCampGrowth::RewardXP(Catalog.Get().Parameters, *Monster, Stage, XP))
    {
        return ELDGrowthReason::DataInvalid;
    }
    Out.XP = XP;
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::EarnedPoints(const FLDDemoContentDefinitions& Content, uint8 Level, int64 XP, int64& Out)
{
    Out = 0;
    const FLDDemoContentParameters& P = Content.Parameters;
    const FLDDemoProgressionDefinition& Progression = Content.Progression;
    uint8 XPLevel = 0;
    if (const ELDGrowthReason Reason = LevelForXP(Progression, XP, XPLevel); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (XPLevel != Level || P.OpeningPoints < 0 || P.PointsPerLevel < 0 || P.MaxLevelPoints < 0 || P.MaxLevelPointsXP <= 0)
    {
        return ELDGrowthReason::DataInvalid;
    }
    int64 Earned = 0, Grants = 0, Extra = 0;
    if (!ProgressionCheckedMul(P.PointsPerLevel, int64(Level) - 1, Earned) || !ProgressionCheckedAdd(Earned, P.OpeningPoints, Earned))
    {
        return ELDGrowthReason::ArithmeticOverflow;
    }
    if (int32(Level) == Progression.MaxLevel)
    {
        // Every full MaxLevelPointsXP past the top threshold grants MaxLevelPoints more; XP keeps accumulating.
        Grants = (XP - Progression.XPThresholds[Progression.MaxLevel - 1]) / P.MaxLevelPointsXP;
        if (!ProgressionCheckedMul(P.MaxLevelPoints, Grants, Extra) || !ProgressionCheckedAdd(Earned, Extra, Earned))
        {
            return ELDGrowthReason::ArithmeticOverflow;
        }
    }
    Out = Earned;
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::UnspentPoints(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, int64 XP,
    int64& Out)
{
    Out = 0;
    int64 Earned = 0, Spent = 0;
    if (const ELDGrowthReason Reason = EarnedPoints(Content, Build.Level, XP, Earned); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (Build.StatPoints.Num() != Content.Stats.Num() || !Build.SpentPoints(Spent) || Build.PermanentSpentPoints < 0)
    {
        return ELDGrowthReason::DataInvalid;
    }
    if (!ProgressionCheckedAdd(Spent, Build.PermanentSpentPoints, Spent))
    {
        return ELDGrowthReason::ArithmeticOverflow;
    }
    // Points are only ever allocated or paid out of the unspent ones, so a deficit is a broken authority record.
    if (Spent > Earned)
    {
        return ELDGrowthReason::AuthorityFault;
    }
    Out = Earned - Spent;
    return ELDGrowthReason::None;
}

FLDGrowthBuild LDProgression::StartBuild(const FLDDemoContentDefinitions& Content)
{
    FLDGrowthBuild Build;
    Build.Level = 1;
    Build.StatPoints.Init(0, Content.Stats.Num());
    Build.Revision = 1;
    return Build;
}

void LDProgression::NormalizeBuild(FLDGrowthBuild& Build)
{
    ProgressionSortBuild(Build);
}

ELDGrowthReason LDProgression::ValidateBuild(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build)
{
    int64 Spent = 0;
    if (Content.Progression.ActiveSlots != 3 || Build.StatPoints.Num() != Content.Stats.Num() || !Build.SpentPoints(Spent)
        || Build.PermanentSpentPoints < 0 || Build.Actives.Num() > Content.Progression.ActiveSlots)
    {
        return ELDGrowthReason::DataInvalid;
    }
    for (int32 Index = 0; Index < Build.Actives.Num(); ++Index)
    {
        const FLDLearnedActive& Active = Build.Actives[Index];
        if (Active.Slot >= Content.Progression.ActiveSlots || !ProgressionFindAbility(Content, Active.Id)
            || (Index > 0 && Build.Actives[Index - 1].Slot >= Active.Slot)
            || Build.Actives.ContainsByPredicate([&Active](const FLDLearnedActive& Other) { return &Other != &Active && Other.Id == Active.Id; }))
        {
            return ELDGrowthReason::DataInvalid;
        }
    }
    for (int32 Index = 0; Index < Build.PendingBlueprints.Num(); ++Index)
    {
        const FName Id = Build.PendingBlueprints[Index];
        if (!ProgressionFindAbility(Content, Id) || Build.FindActive(Id)
            || (Index > 0 && !ProgressionIdLess(Build.PendingBlueprints[Index - 1], Id)))
        {
            return ELDGrowthReason::DataInvalid;
        }
    }
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::EligibleActives(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
    uint8 Ordinal, TArray<FName>& Out)
{
    Out.Reset();
    if (Ordinal == 0)
    {
        return ELDGrowthReason::DataInvalid;
    }
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Build); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    for (const FLDDemoAbilityDefinition& Ability : Content.Abilities)
    {
        const FName Id(*Ability.Id);
        if ((Ordinal == 1 && !Ability.Opening) || Build.FindActive(Id) || Build.HasBlueprint(Id))
        {
            continue;
        }
        Out.Add(Id);
    }
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::DrawActiveOffer(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
    uint8 Ordinal, uint64 Seed, TArray<FName>& Out)
{
    Out.Reset();
    TArray<FName> Remaining;
    if (const ELDGrowthReason Reason = EligibleActives(Content, Build, Ordinal, Remaining); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    uint64 State = Seed;
    while (Out.Num() < 3 && !Remaining.IsEmpty())
    {
        const int32 Pick = int32(ProgressionSplitMix64(State) % uint64(Remaining.Num()));
        Out.Add(Remaining[Pick]);
        Remaining.RemoveAt(Pick);
    }
    return ELDGrowthReason::None;
}

uint64 LDProgression::OfferSeed(const FGuid& MatchSecret, const FGuid& Contender, uint8 Ordinal)
{
    uint32 Words[9] = {MatchSecret.A, MatchSecret.B, MatchSecret.C, MatchSecret.D,
        Contender.A, Contender.B, Contender.C, Contender.D, Ordinal};
    return CityHash64(reinterpret_cast<const char*>(Words), sizeof(Words));
}

bool LDProgression::IsRestWindow(const FBuildContext& Context)
{
    return Context.Stage == EMatchStage::Rest;
}

LDProgression::EMatchStage LDProgression::StageForPublic(ELDMatchPhase Phase, const FLDTimelineView& Timeline)
{
    switch (Phase)
    {
    case ELDMatchPhase::Rest: return LDMatchTimeline::IsRestBuildOpen(Timeline) ? EMatchStage::Rest : EMatchStage::RestLocked;
    case ELDMatchPhase::Development:
    case ELDMatchPhase::CaptureOpen: return EMatchStage::Live;
    default: return EMatchStage::Closed;
    }
}

ELDGrowthReason LDProgression::LearnGate(const FBuildContext& Context)
{
    if (!LDGrowthWire::IsBuildLifePhase(Context.LifePhase))
    {
        return ELDGrowthReason::LifePhaseChanged;
    }
    switch (Context.Stage)
    {
    case EMatchStage::Rest:
        // The rest window has no build safe window; only a dead contender keeps the death-wait rules.
        return Context.LifePhase == ELDLifePhase::Dead ? ELDGrowthReason::Dead : ELDGrowthReason::None;
    case EMatchStage::Live:
        if (Context.LifePhase == ELDLifePhase::Dead)
        {
            return ELDGrowthReason::Dead;
        }
        return Context.bSafe ? ELDGrowthReason::None : ELDGrowthReason::NotSafe;
    case EMatchStage::RestLocked:
        return ELDGrowthReason::BuildLocked;
    case EMatchStage::Closed:
        return ELDGrowthReason::PhaseClosed;
    }
    return ELDGrowthReason::PhaseClosed;
}

ELDGrowthReason LDProgression::CommandGate(ELDBuildCommandKind Kind, const FBuildContext& Context)
{
    if (!LDGrowthWire::IsBuildLifePhase(Context.LifePhase))
    {
        return ELDGrowthReason::LifePhaseChanged;
    }
    switch (Kind)
    {
    case ELDBuildCommandKind::ActiveChoice:
    case ELDBuildCommandKind::LearnBlueprint:
    case ELDBuildCommandKind::AllocatePoints:
    case ELDBuildCommandKind::BuyNpcOffer:
    case ELDBuildCommandKind::Respec:
    case ELDBuildCommandKind::ReplaceActive:
    case ELDBuildCommandKind::ForgetActive:
        break;
    default:
        return ELDGrowthReason::InvalidRequest;
    }
    if (Context.Stage == EMatchStage::RestLocked)
    {
        return ELDGrowthReason::BuildLocked;
    }
    if (Context.Stage != EMatchStage::Live && Context.Stage != EMatchStage::Rest)
    {
        return ELDGrowthReason::PhaseClosed;
    }
    const bool bRest = IsRestWindow(Context);
    const bool bDead = Context.LifePhase == ELDLifePhase::Dead;
    switch (Kind)
    {
    case ELDBuildCommandKind::ActiveChoice:
        return ELDGrowthReason::None;
    case ELDBuildCommandKind::LearnBlueprint:
        return LearnGate(Context);
    case ELDBuildCommandKind::AllocatePoints:
        return bRest || bDead || Context.bSafe ? ELDGrowthReason::None : ELDGrowthReason::NotSafe;
    case ELDBuildCommandKind::BuyNpcOffer:
    case ELDBuildCommandKind::Respec:
        return !bRest ? ELDGrowthReason::NotRestWindow : bDead ? ELDGrowthReason::Dead : ELDGrowthReason::None;
    case ELDBuildCommandKind::ReplaceActive:
        return !bRest ? ELDGrowthReason::ReplacementNotAllowed : LearnGate(Context);
    case ELDBuildCommandKind::ForgetActive:
        return !bRest ? ELDGrowthReason::ReplacementNotAllowed : bDead ? ELDGrowthReason::Dead : ELDGrowthReason::None;
    default:
        return ELDGrowthReason::InvalidRequest;
    }
}

bool LDProgression::PickLearnSlot(const FLDGrowthBuild& Build, uint8 Preferred, uint8& OutSlot)
{
    OutSlot = 0;
    if (Preferred < 3 && Build.ActiveInSlot(Preferred) == INDEX_NONE)
    {
        OutSlot = Preferred;
        return true;
    }
    for (uint8 Slot = 0; Slot < 3; ++Slot)
    {
        if (Build.ActiveInSlot(Slot) == INDEX_NONE)
        {
            OutSlot = Slot;
            return true;
        }
    }
    return false;
}

ELDGrowthReason LDProgression::ApplyActiveChoice(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    FName Option, bool bLearnIfPossible, uint8 PreferredSlot, const FBuildContext& Context, FLDGrowthBuild& Out,
    FChoiceOutcome& Outcome)
{
    Out = Current;
    Outcome = FChoiceOutcome();
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (PreferredSlot != FLDBuildCommand::AnySlot && (PreferredSlot >= 3 || !bLearnIfPossible))
    {
        return ELDGrowthReason::InvalidRequest;
    }
    if (!ProgressionFindAbility(Content, Option) || Current.FindActive(Option) || Current.HasBlueprint(Option))
    {
        return ELDGrowthReason::InvalidOption;
    }
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::ActiveChoice, Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (bLearnIfPossible)
    {
        // The learn half is decided in the same commit: it never fails the choice, it only keeps the active stored.
        Outcome.StoredReason = LearnGate(Context);
        uint8 Slot = 0;
        if (Outcome.StoredReason == ELDGrowthReason::None && !PickLearnSlot(Current, PreferredSlot, Slot))
        {
            Outcome.StoredReason = ELDGrowthReason::NoEmptySlot;
        }
        if (Outcome.StoredReason == ELDGrowthReason::None)
        {
            Out.Actives.Add({Option, Slot});
            ProgressionSortBuild(Out);
            Outcome.Result = ELDBuildChoiceResult::Learned;
            Outcome.Slot = Slot;
            return ELDGrowthReason::None;
        }
    }
    Out.PendingBlueprints.Add(Option);
    ProgressionSortBuild(Out);
    Outcome.Result = ELDBuildChoiceResult::StoredAsBlueprint;
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplyLearnBlueprint(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    FName Blueprint, uint8 Slot, const FBuildContext& Context, FLDGrowthBuild& Out)
{
    Out = Current;
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (Slot >= 3)
    {
        return ELDGrowthReason::InvalidRequest;
    }
    if (!Current.HasBlueprint(Blueprint))
    {
        return ELDGrowthReason::NoBlueprint;
    }
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::LearnBlueprint, Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    // Replacing a learned active is the rest-only ApplyReplaceActive.
    if (Current.ActiveInSlot(Slot) != INDEX_NONE)
    {
        return ELDGrowthReason::SlotOccupied;
    }
    Out.PendingBlueprints.Remove(Blueprint);
    Out.Actives.Add({Blueprint, Slot});
    ProgressionSortBuild(Out);
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplyAllocation(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    int64 XP, const TArray<uint8>& PointCounts, const FBuildContext& Context, FLDGrowthBuild& Out)
{
    Out = Current;
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    int64 Total = 0;
    for (const uint8 Count : PointCounts)
    {
        Total += Count;
    }
    if (PointCounts.Num() != Content.Stats.Num() || Total < 1)
    {
        return ELDGrowthReason::InvalidRequest;
    }
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::AllocatePoints, Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    int64 Unspent = 0;
    if (const ELDGrowthReason Reason = UnspentPoints(Content, Current, XP, Unspent); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (Total > Unspent)
    {
        return ELDGrowthReason::NotEnoughPoints;
    }
    for (int32 Index = 0; Index < PointCounts.Num(); ++Index)
    {
        if (Out.StatPoints[Index] > MAX_int32 - int32(PointCounts[Index]))
        {
            Out = Current;
            return ELDGrowthReason::ArithmeticOverflow;
        }
        Out.StatPoints[Index] += PointCounts[Index];
    }
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplyBuyNpcOffer(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    const FLDProgressionState& Progression, const FGuid& MatchSecret, const FGuid& Contender, const FBuildContext& Context,
    FLDGrowthBuild& Out, FLDProgressionState& OutProgression, FNpcOfferOutcome& Outcome)
{
    Out = Current;
    OutProgression = Progression;
    Outcome = FNpcOfferOutcome();
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    // The opening opportunity always exists, so a bought ordinal is never the opening-pool ordinal 1.
    if (Progression.ActiveEarned == 0 || Progression.ActiveConsumed > Progression.ActiveEarned
        || Progression.NpcPurchased > Progression.ActiveEarned || Content.Parameters.NpcActivePoints < 1)
    {
        return ELDGrowthReason::DataInvalid;
    }
    int64 Unspent = 0;
    if (const ELDGrowthReason Reason = UnspentPoints(Content, Current, Progression.CumulativeXP, Unspent); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::BuyNpcOffer, Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    // Active rewards are handled one at a time: the NPC never charges for an opportunity that would only queue.
    if (Progression.Offer.IsSet() || Progression.QueuedActiveOpportunities() > 0)
    {
        return ELDGrowthReason::OfferOutstanding;
    }
    if (Progression.ActiveEarned == MAX_uint8 || Progression.NpcPurchased == MAX_uint8 || Progression.LastOfferRevision == MAX_uint64)
    {
        return ELDGrowthReason::CapacityExceeded;
    }
    const uint8 Ordinal = uint8(Progression.ActiveEarned + 1);
    TArray<FName> Pool;
    if (const ELDGrowthReason Reason = EligibleActives(Content, Current, Ordinal, Pool); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (Pool.Num() < 3)
    {
        return ELDGrowthReason::NpcPoolTooSmall;
    }
    const int64 Cost = Content.Parameters.NpcActivePoints;
    if (Unspent < Cost)
    {
        return ELDGrowthReason::NotEnoughPoints;
    }
    TArray<FName> Options;
    if (const ELDGrowthReason Reason = DrawActiveOffer(Content, Current, Ordinal, OfferSeed(MatchSecret, Contender, Ordinal), Options);
        Reason != ELDGrowthReason::None || Options.Num() != 3)
    {
        return Reason != ELDGrowthReason::None ? Reason : ELDGrowthReason::DataInvalid;
    }
    if (!ProgressionCheckedAdd(Current.PermanentSpentPoints, Cost, Out.PermanentSpentPoints))
    {
        Out = Current;
        return ELDGrowthReason::ArithmeticOverflow;
    }
    OutProgression.ActiveEarned = Ordinal;
    OutProgression.NpcPurchased = uint8(Progression.NpcPurchased + 1);
    FLDActiveOfferState Offer;
    Offer.OfferId = FGuid::NewGuid();
    Offer.Ordinal = Ordinal;
    Offer.Options = MoveTemp(Options);
    Offer.GeneratedBuildRevision = Current.Revision;
    Offer.Revision = ++OutProgression.LastOfferRevision;
    OutProgression.Offer.Emplace(MoveTemp(Offer));
    Outcome.PointsPaid = Cost;
    Outcome.Ordinal = Ordinal;
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplyRespec(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current, int64 XP,
    const FBuildContext& Context, FLDGrowthBuild& Out, int64& OutRefunded)
{
    Out = Current;
    OutRefunded = 0;
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    int64 Unspent = 0;
    if (const ELDGrowthReason Reason = UnspentPoints(Content, Current, XP, Unspent); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::Respec, Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    int64 Allocated = 0;
    if (!Current.SpentPoints(Allocated))
    {
        return ELDGrowthReason::DataInvalid;
    }
    if (Allocated == 0)
    {
        return ELDGrowthReason::NothingToRespec;
    }
    // Only the allocation returns; PermanentSpentPoints (the NPC) is never refunded.
    for (int32& Count : Out.StatPoints)
    {
        Count = 0;
    }
    OutRefunded = Allocated;
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplyReplaceActive(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    const FLDProgressionState& Progression, FName Blueprint, uint8 Slot, const FBuildContext& Context, FLDGrowthBuild& Out,
    FName& OutForgotten)
{
    Out = Current;
    OutForgotten = NAME_None;
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (Slot >= 3)
    {
        return ELDGrowthReason::InvalidRequest;
    }
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::ReplaceActive, Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    // Another learn or forget never runs while the current offer is undecided.
    if (Progression.Offer.IsSet())
    {
        return ELDGrowthReason::OfferOutstanding;
    }
    const int32 Index = Current.ActiveInSlot(Slot);
    if (Index == INDEX_NONE)
    {
        return ELDGrowthReason::SlotEmpty;
    }
    if (!Current.HasBlueprint(Blueprint))
    {
        return ELDGrowthReason::NoBlueprint;
    }
    // The old active is forgotten, not stored: only a new source gives it back (Combat keeps its cooldown track).
    OutForgotten = Current.Actives[Index].Id;
    Out.Actives.RemoveAt(Index);
    Out.PendingBlueprints.Remove(Blueprint);
    Out.Actives.Add({Blueprint, Slot});
    ProgressionSortBuild(Out);
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplyForgetActive(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    const FLDProgressionState& Progression, uint8 Slot, const FBuildContext& Context, FLDGrowthBuild& Out, FName& OutForgotten)
{
    Out = Current;
    OutForgotten = NAME_None;
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (Slot >= 3)
    {
        return ELDGrowthReason::InvalidRequest;
    }
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::ForgetActive, Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (Progression.Offer.IsSet())
    {
        return ELDGrowthReason::OfferOutstanding;
    }
    const int32 Index = Current.ActiveInSlot(Slot);
    if (Index == INDEX_NONE)
    {
        return ELDGrowthReason::SlotEmpty;
    }
    OutForgotten = Current.Actives[Index].Id;
    Out.Actives.RemoveAt(Index);
    return ELDGrowthReason::None;
}