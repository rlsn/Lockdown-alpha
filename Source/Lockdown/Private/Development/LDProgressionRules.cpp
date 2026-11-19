#include "Development/LDProgressionRules.h"
#include "Framework/LDPacingTypes.h"

#include "Combat/LDCombatTypes.h"
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

const FLDDemoCardDefinition* ProgressionFindCard(const FLDDemoContentDefinitions& Content, FName Id)
{
    return Content.Cards.FindByPredicate([&](const FLDDemoCardDefinition& Row) { return FName(*Row.Id) == Id; });
}

void ProgressionSortBuild(FLDGrowthBuild& Build)
{
    Build.Actives.Sort([](const FLDLearnedActive& A, const FLDLearnedActive& B) { return A.Slot < B.Slot; });
    Build.PendingBlueprints.Sort([](FName A, FName B) { return ProgressionIdLess(A, B); });
    Build.Cards.Sort([](const FLDCardInstance& A, const FLDCardInstance& B) { return A.Serial < B.Serial; });
}

// The current offer, when OfferId names it.
const FLDCardOfferState* ProgressionCurrentCardOffer(const FLDProgressionState& Progression, const FGuid& OfferId)
{
    return Progression.CardOffer.IsSet() && Progression.CardOffer->OfferId == OfferId && OfferId.IsValid()
        ? &Progression.CardOffer.GetValue() : nullptr;
}

// The current offer resolves: the queue head is consumed.
void ProgressionResolveCardOffer(FLDProgressionState& Progression)
{
    Progression.CardOffer.Reset();
    Progression.CardQueue.RemoveAt(0);
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

const FLDCardInstance* FLDGrowthBuild::FindCard(uint16 Serial) const
{
    return Cards.FindByPredicate([Serial](const FLDCardInstance& Card) { return Card.Serial == Serial; });
}

FLDCardInstance* FLDGrowthBuild::FindCard(uint16 Serial)
{
    return Cards.FindByPredicate([Serial](const FLDCardInstance& Card) { return Card.Serial == Serial; });
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
    if (Build.StatPoints.Num() != Content.Stats.Num() || !Build.SpentPoints(Spent))
    {
        return ELDGrowthReason::DataInvalid;
    }
    // Points are only ever allocated out of the unspent ones, so a deficit is a broken authority record.
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
        || Build.BoughtSkillPoints < 0 || Build.BoughtSkillPoints > Content.Parameters.Shop.SkillPointMaxPerMatch
        || Build.AffixSkillPoints < 0 || Build.AffixSkillPoints > Content.Parameters.Affix.AuditSkillPointsMax
        || Build.Actives.Num() > Content.Progression.ActiveSlots)
    {
        return ELDGrowthReason::DataInvalid;
    }
    for (int32 Index = 0; Index < Build.Actives.Num(); ++Index)
    {
        const FLDLearnedActive& Active = Build.Actives[Index];
        const FLDDemoAbilityDefinition* Ability = ProgressionFindAbility(Content, Active.Id);
        if (Active.Slot >= Content.Progression.ActiveSlots || !Ability
            || (Active.Branch != ELDSkillBranch::None && !Ability->FindBranch(Active.Branch))
            || (Index > 0 && Build.Actives[Index - 1].Slot >= Active.Slot)
            || Build.Actives.ContainsByPredicate([&Active](const FLDLearnedActive& Other) { return &Other != &Active && Other.Id == Active.Id; }))
        {
            return ELDGrowthReason::DataInvalid;
        }
    }
    if (SkillPoints(Content, Build) < 0)
    {
        return ELDGrowthReason::DataInvalid;
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
    // Spec §6.1: distinct serials below NextCardSerial in ascending order, known definitions, legal installs within capacity.
    for (int32 Index = 0; Index < Build.Cards.Num(); ++Index)
    {
        const FLDCardInstance& Card = Build.Cards[Index];
        if (Card.Serial == 0 || Card.Serial >= Build.NextCardSerial || (Index > 0 && Build.Cards[Index - 1].Serial >= Card.Serial)
            || !ProgressionFindCard(Content, Card.Id)
            || (Card.IsInstalled() && CardTargetReason(Content, Build, Card.Id, Card.Target) != ELDGrowthReason::None)
            || (!Card.IsInstalled() && !Card.Target.IsEmpty()))
        {
            return ELDGrowthReason::DataInvalid;
        }
    }
    if (InstalledCards(Build) > CardCapacity(Content.Parameters, Build.Level))
    {
        return ELDGrowthReason::DataInvalid;
    }
    if (!Build.FinalMod.IsEmpty())
    {
        // The binding may name a source the player has since removed: the card is dormant then (FinalModBound), not invalid.
        TArray<FLDFinalModTarget> Legal;
        if (FinalModTargets(Content, Build, Build.FinalMod.DefinitionId, Legal) != ELDGrowthReason::None)
        { return ELDGrowthReason::DataInvalid; }
    }
    return ELDGrowthReason::None;
}

bool LDProgression::FinalModBound(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build)
{
    if (Build.FinalMod.IsEmpty()) { return true; }
    FLDFinalModTarget Binding;
    Binding.Kind = Build.FinalMod.TargetKind;
    Binding.Definition = Build.FinalMod.TargetId;
    TArray<FLDFinalModTarget> Legal;
    return FinalModTargets(Content, Build, Build.FinalMod.DefinitionId, Legal) == ELDGrowthReason::None && Legal.Contains(Binding);
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
    // #222 (2026-11-12, "all the time 可以学新技能和加点 all the time"): learning is open on the field, in every rest step
    // and in the death wait alike; there is no build safe window and no build lock any more.
    return Context.Stage == EMatchStage::Closed ? ELDGrowthReason::PhaseClosed : ELDGrowthReason::None;
}

ELDRestService LDProgression::RestServiceFor(ELDBuildCommandKind Kind)
{
    switch (Kind)
    {
    case ELDBuildCommandKind::Respec:
    case ELDBuildCommandKind::ReplaceActive:
    case ELDBuildCommandKind::ForgetActive: return ELDRestService::Trainer;
    default: return ELDRestService::None;
    }
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
    case ELDBuildCommandKind::SkipActiveOffer:
    case ELDBuildCommandKind::LearnBlueprint:
    case ELDBuildCommandKind::AllocatePoints:
    case ELDBuildCommandKind::Respec:
    case ELDBuildCommandKind::ReplaceActive:
    case ELDBuildCommandKind::ForgetActive:
    case ELDBuildCommandKind::FinalMod:
    case ELDBuildCommandKind::RankUpSkill:
    case ELDBuildCommandKind::PickCard:
    case ELDBuildCommandKind::SkipCards:
    case ELDBuildCommandKind::RerollCards:
    case ELDBuildCommandKind::InstallCard:
    case ELDBuildCommandKind::UninstallCard:
    case ELDBuildCommandKind::ShopBuy:
    case ELDBuildCommandKind::ShopRefresh:
    case ELDBuildCommandKind::ShopSell:
    case ELDBuildCommandKind::UsePickTicket:
        break;
    default:
        return ELDGrowthReason::InvalidRequest;
    }
    if (Context.Stage == EMatchStage::Closed)
    {
        return ELDGrowthReason::PhaseClosed;
    }
    const bool bRest = IsRestWindow(Context);
    const bool bDead = Context.LifePhase == ELDLifePhase::Dead;
    switch (Kind)
    {
    // #222: the own build commands are open in every stage of a running match, alive or dead, in combat and in a rest; the
    // server still checks the resources, slots and structure of each one.
    case ELDBuildCommandKind::ActiveChoice:
    case ELDBuildCommandKind::SkipActiveOffer:
    case ELDBuildCommandKind::LearnBlueprint:
    case ELDBuildCommandKind::AllocatePoints:
    // Spec §6.1/§6.4: the card commands are open whenever the build is.
    case ELDBuildCommandKind::PickCard:
    case ELDBuildCommandKind::SkipCards:
    case ELDBuildCommandKind::RerollCards:
    case ELDBuildCommandKind::InstallCard:
    case ELDBuildCommandKind::UninstallCard:
    // Issue #287 spec §4.5: a rank-up is open any time of a running match, in combat and in the death wait alike.
    case ELDBuildCommandKind::RankUpSkill:
    // Spec §11.3: a pick ticket is spent whenever the build is open.
    case ELDBuildCommandKind::UsePickTicket:
        return ELDGrowthReason::None;
    // The rest shop (spec §3.13) and the trainer keep the rest build window (the build and payout steps) and a living body;
    // the trainer also its NPC (RestServiceFor).
    case ELDBuildCommandKind::ShopBuy:
    case ELDBuildCommandKind::ShopRefresh:
    case ELDBuildCommandKind::ShopSell:
    case ELDBuildCommandKind::Respec:
        return Context.Stage == EMatchStage::RestLocked ? ELDGrowthReason::BuildLocked
            : !bRest ? ELDGrowthReason::NotRestWindow : bDead ? ELDGrowthReason::Dead : ELDGrowthReason::None;
    case ELDBuildCommandKind::ReplaceActive:
    case ELDBuildCommandKind::ForgetActive:
        return Context.Stage == EMatchStage::RestLocked ? ELDGrowthReason::BuildLocked
            : !bRest ? ELDGrowthReason::ReplacementNotAllowed : bDead ? ELDGrowthReason::Dead : ELDGrowthReason::None;
    // Phase 9: the final mod is taken in the final rest's build window (or its payout), with a living body.
    case ELDBuildCommandKind::FinalMod:
        return Context.Stage == EMatchStage::RestLocked ? ELDGrowthReason::BuildLocked
            : !bRest ? ELDGrowthReason::NotRestWindow : bDead ? ELDGrowthReason::Dead : ELDGrowthReason::None;
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
        // The learn half is decided in the same commit: it never fails the choice, it only keeps the active stored when
        // Q/W/E are full (#222: the moment never stores it).
        uint8 Slot = 0;
        if (!PickLearnSlot(Current, PreferredSlot, Slot))
        {
            Outcome.StoredReason = ELDGrowthReason::NoEmptySlot;
        }
        else
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

ELDGrowthReason LDProgression::ApplySkipActiveOffer(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    const FLDProgressionState& Progression, const FBuildContext& Context, FLDProgressionState& OutProgression, int32& OutVouchers)
{
    OutProgression = Progression;
    OutVouchers = 0;
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None) { return Reason; }
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::SkipActiveOffer, Context); Reason != ELDGrowthReason::None)
    { return Reason; }
    // Spec §6.4: skipping a skill offer pays vouchers.
    const int32 Vouchers = Content.Parameters.Vouchers.ActiveSkip;
    if (Vouchers <= 0) { return ELDGrowthReason::DataInvalid; }
    if (const ELDGrowthReason Reason = GrantVouchers(OutProgression, Vouchers); Reason != ELDGrowthReason::None)
    {
        OutProgression = Progression;
        return Reason;
    }
    OutVouchers = Vouchers;
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
    for (int32& Count : Out.StatPoints)
    {
        Count = 0;
    }
    OutRefunded = Allocated;
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplyReplaceActive(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    const FLDProgressionState& Progression, FName Blueprint, uint8 Slot, const FBuildContext& Context, FLDGrowthBuild& Out,
    FName& OutForgotten, TArray<uint16>& OutReclaimed)
{
    Out = Current;
    OutForgotten = NAME_None;
    OutReclaimed.Reset();
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
    // The old active is forgotten, not stored: only a new source gives it back (Combat keeps its cooldown track). Its skill
    // cards return to the library in the same commit; nothing moves to the new active on its own.
    OutForgotten = Current.Actives[Index].Id;
    Out.Actives.RemoveAt(Index);
    Out.PendingBlueprints.Remove(Blueprint);
    Out.Actives.Add({Blueprint, Slot});
    ReclaimAttachments(Out, OutForgotten, OutReclaimed);
    ProgressionSortBuild(Out);
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::FinalModTargets(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
    FName Option, TArray<FLDFinalModTarget>& Out)
{
    Out.Reset();
    const FLDDemoFinalModDefinition* Card = Content.FinalMods.FindByPredicate(
        [Option](const FLDDemoFinalModDefinition& Row) { return FName(*Row.Id) == Option; });
    if (!Card) { return ELDGrowthReason::InvalidOption; }
    if (Card->IsUniversal())
    {
        Out.AddDefaulted();
        return ELDGrowthReason::None;
    }
    const auto Add = [&Out](ELDFinalModTargetKind Kind, FName Id)
    {
        FLDFinalModTarget Target;
        Target.Kind = Kind;
        Target.Definition = Id;
        Out.AddUnique(Target);
    };
    const auto Hard = [](ELDDemoEffectOp Op)
    {
        return Op == ELDDemoEffectOp::Root || Op == ELDDemoEffectOp::Stun || Op == ELDDemoEffectOp::Disarm
            || Op == ELDDemoEffectOp::Taunt || Op == ELDDemoEffectOp::Silence;
    };
    if (Card->Requirement == ELDDemoFinalModRequirement::Dash) { Add(ELDFinalModTargetKind::Dash, NAME_None); }
    for (const FLDLearnedActive& Learned : Build.Actives)
    {
        const FLDDemoAbilityDefinition* Ability = ProgressionFindAbility(Content, Learned.Id);
        if (!Ability) { return ELDGrowthReason::DataInvalid; }
        const TArray<FLDDemoEffectDefinition>& Effects = Ability->Values.Effects;
        const auto Has = [&Effects](ELDDemoEffectOp Op)
        { return Effects.ContainsByPredicate([Op](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp() == Op; }); };
        bool bLegal = false;
        switch (Card->Requirement)
        {
        case ELDDemoFinalModRequirement::ForcedMove: bLegal = Has(ELDDemoEffectOp::Knockback); break;
        case ELDDemoFinalModRequirement::HardControl:
            bLegal = Effects.ContainsByPredicate([&Hard](const FLDDemoEffectDefinition& Effect) { return Hard(Effect.GetOp()); });
            break;
        case ELDDemoFinalModRequirement::Dash: bLegal = Has(ELDDemoEffectOp::Dash); break;
        case ELDDemoFinalModRequirement::PlacedZone:
            bLegal = Ability->Target.Mode == ELDDemoTargetMode::GroundPoint
                && Effects.ContainsByPredicate([](const FLDDemoEffectDefinition& Effect)
                { return Effect.GetOp() == ELDDemoEffectOp::Zone && Effect.DurationMs >= 3000; });
            break;
        default: return ELDGrowthReason::DataInvalid;
        }
        if (bLegal) { Add(ELDFinalModTargetKind::Active, Learned.Id); }
    }
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::DrawFinalModOptions(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
    uint64 Seed, TArray<FName>& Out, int32 Count)
{
    Out.Reset();
    if (Count != 3 && Count != 4) { return ELDGrowthReason::DataInvalid; }
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Build); Reason != ELDGrowthReason::None) { return Reason; }
    TArray<FName> Universal, Mechanic, All;
    for (const FLDDemoFinalModDefinition& Card : Content.FinalMods)
    {
        const FName Id(*Card.Id);
        TArray<FLDFinalModTarget> Targets;
        if (const ELDGrowthReason Reason = FinalModTargets(Content, Build, Id, Targets); Reason != ELDGrowthReason::None)
        { return Reason; }
        if (Targets.IsEmpty()) { continue; }
        All.Add(Id);
        (Card.IsUniversal() ? Universal : Mechanic).Add(Id);
    }
    if (Universal.Num() != 4 || All.Num() < Count) { return ELDGrowthReason::DataInvalid; }
    uint64 State = Seed;
    const auto Pick = [&Out, &State](TArray<FName> Pool)
    {
        Pool.RemoveAll([&Out](FName Id) { return Out.Contains(Id); });
        if (!Pool.IsEmpty()) { Out.Add(Pool[int32(ProgressionSplitMix64(State) % uint64(Pool.Num()))]); }
    };
    Pick(Mechanic.IsEmpty() ? Universal : Mechanic);
    Pick(Universal);
    for (int32 Index = Out.Num(); Index < Count; ++Index) { Pick(All); }
    return Out.Num() == Count ? ELDGrowthReason::None : ELDGrowthReason::DataInvalid;
}

uint64 LDProgression::FinalModSeed(const FGuid& MatchSecret, const FGuid& Contender)
{
    // A tag no opportunity ordinal reaches keeps this draw apart from the offer seeds.
    uint32 Words[10] = {MatchSecret.A, MatchSecret.B, MatchSecret.C, MatchSecret.D,
        Contender.A, Contender.B, Contender.C, Contender.D, 0x46494eu, 1u};
    return CityHash64(reinterpret_cast<const char*>(Words), sizeof(Words));
}

ELDGrowthReason LDProgression::ApplyFinalMod(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    const FLDProgressionState& Progression, const FGuid& OfferId, FName Option, const FLDFinalModTarget& Target,
    const FBuildContext& Context, FLDGrowthBuild& Out, FLDProgressionState& OutProgression)
{
    Out = Current;
    OutProgression = Progression;
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    const FLDFinalModState& FinalMod = Progression.FinalMod;
    if (FinalMod.bLocked && !Current.FinalMod.IsEmpty() && Current.FinalMod.DefinitionId == Option && FinalMod.OfferId == OfferId
        && !FinalModBound(Content, Current))
    {
        // Rebinding a dormant card is part of adjusting the build: open wherever an install is.
        if (const ELDGrowthReason Reason = InstallGate(Context); Reason != ELDGrowthReason::None) { return Reason; }
        TArray<FLDFinalModTarget> Legal;
        if (const ELDGrowthReason Reason = FinalModTargets(Content, Current, Option, Legal); Reason != ELDGrowthReason::None)
        { return Reason; }
        if (!Legal.Contains(Target)) { return ELDGrowthReason::FinalModUnavailable; }
        Out.FinalMod.TargetKind = Target.Kind;
        Out.FinalMod.TargetId = Target.Definition;
        return ELDGrowthReason::None;
    }
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::FinalMod, Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (!FinalMod.IsPending() || FinalMod.OfferId != OfferId)
    {
        return FinalMod.bLocked ? ELDGrowthReason::FinalLocked : ELDGrowthReason::NoFinalMod;
    }
    if (!FinalMod.Options.Contains(Option))
    {
        return ELDGrowthReason::InvalidOption;
    }
    TArray<FLDFinalModTarget> Legal;
    if (const ELDGrowthReason Reason = FinalModTargets(Content, Current, Option, Legal); Reason != ELDGrowthReason::None)
    { return Reason; }
    if (!Legal.Contains(Target)) { return ELDGrowthReason::FinalModUnavailable; }
    Out.FinalMod.DefinitionId = Option;
    Out.FinalMod.TargetKind = Target.Kind;
    Out.FinalMod.TargetId = Target.Definition;
    OutProgression.FinalMod.bLocked = true;
    return ELDGrowthReason::None;
}

int32 LDProgression::SkillPoints(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build)
{
    int32 Points = Build.DevSkillPoints + Build.BoughtSkillPoints + Build.AffixSkillPoints;
    for (const int64 Level : Content.Parameters.SkillRanks.PointLevels)
    {
        Points += Level <= int64(Build.Level) ? 1 : 0;
    }
    for (const FLDLearnedActive& Active : Build.Actives)
    {
        Points -= int32(LDSkillBranch::RankOf(Active.Branch)) - 1;
    }
    return Points;
}

ELDGrowthReason LDProgression::ApplyRankUpSkill(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    uint8 Slot, ELDSkillBranch Branch, const FBuildContext& Context, FLDGrowthBuild& Out)
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
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::RankUpSkill, Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    const int32 Index = Current.ActiveInSlot(Slot);
    if (Index == INDEX_NONE)
    {
        return ELDGrowthReason::SlotEmpty;
    }
    const FLDDemoAbilityDefinition* Ability = ProgressionFindAbility(Content, Current.Actives[Index].Id);
    if (!Ability || !LDSkillBranch::IsNext(Current.Actives[Index].Branch, Branch) || !Ability->FindBranch(Branch))
    {
        return ELDGrowthReason::BranchUnavailable;
    }
    if (SkillPoints(Content, Current) < 1)
    {
        return ELDGrowthReason::NoSkillPoint;
    }
    Out.Actives[Index].Branch = Branch;
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplyForgetActive(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    const FLDProgressionState& Progression, uint8 Slot, const FBuildContext& Context, FLDGrowthBuild& Out, FName& OutForgotten,
    TArray<uint16>& OutReclaimed)
{
    Out = Current;
    OutForgotten = NAME_None;
    OutReclaimed.Reset();
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
    ReclaimAttachments(Out, OutForgotten, OutReclaimed);
    return ELDGrowthReason::None;
}

void LDProgression::ReclaimAttachments(FLDGrowthBuild& Build, FName Forgotten, TArray<uint16>& OutReclaimed)
{
    OutReclaimed.Reset();
    if (Forgotten.IsNone())
    {
        return;
    }
    for (FLDCardInstance& Card : Build.Cards)
    {
        if (Card.Target.Kind == ELDCardTargetKind::Skill && Card.Target.Active == Forgotten)
        {
            Card.Target = FLDCardTarget();
            OutReclaimed.Add(Card.Serial);
        }
    }
}

ELDGrowthReason LDProgression::InstallGate(const FBuildContext& Context)
{
    // #222: installing, moving and uninstalling follow the same open matrix as a learn.
    return LearnGate(Context);
}

// ---- Cards (spec §6) ----

namespace
{
int32 ProgressionPick(uint64& State, int32 Count)
{
    return int32(ProgressionSplitMix64(State) % uint64(Count));
}

ELDCardTargetKind ProgressionFormTarget(ELDCardForm Form)
{
    switch (Form)
    {
    case ELDCardForm::Basic: return ELDCardTargetKind::Basic;
    case ELDCardForm::Skill: return ELDCardTargetKind::Skill;
    case ELDCardForm::Dash: return ELDCardTargetKind::Dash;
    case ELDCardForm::Body: return ELDCardTargetKind::Body;
    default: return ELDCardTargetKind::None;
    }
}
}

int32 LDProgression::CardCapacity(const FLDDemoContentParameters& Parameters, uint8 Level)
{
    const FLDDemoCardParameters& Cards = Parameters.Cards;
    if (Cards.CapacityBase < 1 || Cards.LevelsPerSlot < 1 || Level < 1)
    {
        return 0;
    }
    return Cards.CapacityBase + (int32(Level) - 1) / Cards.LevelsPerSlot;
}

int32 LDProgression::InstalledCards(const FLDGrowthBuild& Build)
{
    int32 Count = 0;
    for (const FLDCardInstance& Card : Build.Cards)
    {
        Count += Card.IsInstalled() ? 1 : 0;
    }
    return Count;
}

uint32 LDProgression::BuildVocab(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build)
{
    uint32 Vocab = 0;
    for (const FLDCardInstance& Card : Build.Cards)
    {
        const FLDDemoCardDefinition* Definition = Card.IsInstalled() ? ProgressionFindCard(Content, Card.Id) : nullptr;
        Vocab |= Definition ? Definition->VocabMask : 0u;
    }
    for (const FLDLearnedActive& Active : Build.Actives)
    {
        const FLDDemoAbilityDefinition* Ability = ProgressionFindAbility(Content, Active.Id);
        Vocab |= Ability ? Ability->VocabMask : 0u;
    }
    return Vocab;
}

ELDGrowthReason LDProgression::CardTargetReason(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, FName Id,
    const FLDCardTarget& Target)
{
    const FLDDemoCardDefinition* Definition = ProgressionFindCard(Content, Id);
    if (!Definition)
    {
        return ELDGrowthReason::DataInvalid;
    }
    const bool bSkill = Target.Kind == ELDCardTargetKind::Skill;
    const bool bElement = Definition->ChooseElements.IsEmpty() ? Target.Element == ELDDemoElement::None
        : Definition->ChooseElements.Contains(Target.Element);
    if (Target.Kind != ProgressionFormTarget(Definition->Form) || bSkill != !Target.Active.IsNone()
        || (bSkill && !Build.FindActive(Target.Active)) || !bElement)
    {
        return ELDGrowthReason::InvalidTarget;
    }
    // Spec §7.2: K07/K10 fit hosts of some deliveries, K05/K11 a host that controls.
    if (bSkill && (Definition->HostHitTypes != 0 || Definition->bHostControl))
    {
        const FLDDemoAbilityDefinition* Host = ProgressionFindAbility(Content, Target.Active);
        if (!Host) { return ELDGrowthReason::DataInvalid; }
        if (Definition->HostHitTypes != 0 && (Definition->HostHitTypes & (1u << uint8(Host->HitType))) == 0)
        {
            return ELDGrowthReason::InvalidTarget;
        }
        const bool bControls = Host->Values.Effects.ContainsByPredicate([](const FLDDemoEffectDefinition& Effect)
        {
            const ELDDemoEffectOp Op = Effect.GetOp();
            return Op == ELDDemoEffectOp::Root || Op == ELDDemoEffectOp::Stun || Op == ELDDemoEffectOp::Silence
                || Op == ELDDemoEffectOp::Disarm || Op == ELDDemoEffectOp::Taunt;
        });
        if (Definition->bHostControl && !bControls) { return ELDGrowthReason::InvalidTarget; }
    }
    return ELDGrowthReason::None;
}

bool LDProgression::DefaultCardTarget(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, FName Id,
    FLDCardTarget& Out)
{
    Out = FLDCardTarget();
    const FLDDemoCardDefinition* Definition = ProgressionFindCard(Content, Id);
    if (!Definition)
    {
        return false;
    }
    Out.Kind = ProgressionFormTarget(Definition->Form);
    Out.Element = Definition->ChooseElements.IsEmpty() ? ELDDemoElement::None : Definition->ChooseElements[0];
    if (Out.Kind == ELDCardTargetKind::Skill)
    {
        // The learned skill sharing the most vocabulary keys with the card; Q/W/E order on ties.
        int32 Best = -1;
        for (const FLDLearnedActive& Active : Build.Actives)
        {
            const FLDDemoAbilityDefinition* Ability = ProgressionFindAbility(Content, Active.Id);
            const int32 Shared = Ability ? FMath::CountBits64(uint64(Ability->VocabMask & Definition->VocabMask)) : 0;
            if (Shared > Best)
            {
                Best = Shared;
                Out.Active = Active.Id;
            }
        }
    }
    if (CardTargetReason(Content, Build, Id, Out) != ELDGrowthReason::None)
    {
        Out = FLDCardTarget();
        return false;
    }
    return true;
}

int32 LDProgression::CardOfferSize(const FLDDemoContentParameters& Parameters, ELDCardOfferSource Source)
{
    switch (Source)
    {
    case ELDCardOfferSource::MiniFlag: return Parameters.Objective.FlagCardOffer;
    case ELDCardOfferSource::Arena: return Parameters.Objective.ArenaCardOffer;
    case ELDCardOfferSource::Locker: return Parameters.Explore.LockerCardOffer;
    case ELDCardOfferSource::Peddler: return Parameters.Explore.PeddlerCardOffer;
    case ELDCardOfferSource::CampFridge: return Parameters.Affix.FridgeCardOffer;
    case ELDCardOfferSource::CampBonus: return Parameters.Affix.BonusCardOffer;
    default: return Parameters.Cards.OfferSize;
    }
}

ELDGrowthReason LDProgression::DrawCardOffer(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, int32 Size,
    uint64 Seed, TArray<FName>& Out)
{
    Out.Reset();
    const FLDDemoCardParameters& Parameters = Content.Parameters.Cards;
    if (Size < 1 || Parameters.VocabWeight < 1)
    {
        return ELDGrowthReason::DataInvalid;
    }
    const uint32 Vocab = BuildVocab(Content, Build);
    TArray<TPair<FName, int64>> Pool;
    for (const FLDDemoCardDefinition& Card : Content.Cards)
    {
        Pool.Emplace(FName(*Card.Id), (Card.VocabMask & Vocab) != 0 ? int64(Parameters.VocabWeight) : 1);
    }
    uint64 State = Seed;
    while (Out.Num() < Size && !Pool.IsEmpty())
    {
        int64 Total = 0;
        for (const TPair<FName, int64>& Entry : Pool) { Total += Entry.Value; }
        int64 Roll = int64(ProgressionSplitMix64(State) % uint64(Total));
        int32 Index = 0;
        while (Roll >= Pool[Index].Value) { Roll -= Pool[Index].Value; ++Index; }
        Out.Add(Pool[Index].Key);
        Pool.RemoveAt(Index);
    }
    return ELDGrowthReason::None;
}

uint64 LDProgression::CardOfferSeed(const FGuid& MatchSecret, const FGuid& Contender, uint16 Ordinal, uint32 Reroll)
{
    // A tag no other draw uses keeps the card offers apart from the active offers of the same number.
    uint32 Words[11] = {MatchSecret.A, MatchSecret.B, MatchSecret.C, MatchSecret.D,
        Contender.A, Contender.B, Contender.C, Contender.D, uint32(Ordinal), Reroll, 0x43415244u};
    return CityHash64(reinterpret_cast<const char*>(Words), sizeof(Words));
}

ELDGrowthReason LDProgression::QueueCardOffers(FLDProgressionState& Progression, ELDCardOfferSource Source, int32 Count,
    uint16& OutFirst, uint16& OutLast)
{
    OutFirst = 1;
    OutLast = 0;
    if (Count < 0 || Source == ELDCardOfferSource::None)
    {
        return ELDGrowthReason::DataInvalid;
    }
    if (int32(Progression.CardOffersEarned) + Count > int32(MAX_uint16))
    {
        return ELDGrowthReason::CapacityExceeded;
    }
    if (Count == 0)
    {
        return ELDGrowthReason::None;
    }
    OutFirst = Progression.CardOffersEarned + 1;
    Progression.CardOffersEarned += uint16(Count);
    OutLast = Progression.CardOffersEarned;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        Progression.CardQueue.Add(Source);
    }
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::RefreshCardOffer(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
    FLDProgressionState& Progression, const FGuid& MatchSecret, const FGuid& Contender, bool& bOutDrawn)
{
    bOutDrawn = false;
    // The opening opportunity resolves first (chosen or skipped), so the opening card offer follows the opening active.
    const bool bOpeningResolved = Progression.Records.ContainsByPredicate(
        [](const FLDActiveOpportunityRecord& Row) { return Row.Ordinal == 1; });
    if (Progression.CardOffer.IsSet() || Progression.CardQueue.IsEmpty() || !bOpeningResolved)
    {
        return ELDGrowthReason::None;
    }
    FLDCardOfferState Offer;
    Offer.Ordinal = uint16(Progression.CardOffersEarned - Progression.CardQueue.Num() + 1);
    Offer.Source = Progression.CardQueue[0];
    if (const ELDGrowthReason Reason = DrawCardOffer(Content, Build, CardOfferSize(Content.Parameters, Offer.Source),
        CardOfferSeed(MatchSecret, Contender, Offer.Ordinal, 0), Offer.Options); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    Offer.OfferId = FGuid::NewGuid();
    Offer.Revision = ++Progression.LastCardOfferRevision;
    MarkSeenCards(Progression, Offer.Options);
    Progression.CardOffer = MoveTemp(Offer);
    bOutDrawn = true;
    return ELDGrowthReason::None;
}

int32 LDProgression::CardRerollsLeft(const FLDProgressionState& Progression)
{
    return FMath::Max(0, Progression.CardRerollsGranted - Progression.CardRerollsUsed);
}

ELDGrowthReason LDProgression::ReceiveCard(const FLDDemoContentDefinitions& Content, FLDGrowthBuild& Build, FName Id,
    uint16 OfferOrdinal, uint16& OutSerial)
{
    OutSerial = 0;
    if (!ProgressionFindCard(Content, Id))
    {
        return ELDGrowthReason::DataInvalid;
    }
    if (Build.NextCardSerial == MAX_uint16)
    {
        return ELDGrowthReason::CapacityExceeded;
    }
    FLDCardInstance& Card = Build.Cards.AddDefaulted_GetRef();
    Card.Serial = Build.NextCardSerial++;
    Card.Id = Id;
    Card.OfferOrdinal = OfferOrdinal;
    OutSerial = Card.Serial;
    ProgressionSortBuild(Build);
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplyPickCard(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    const FLDProgressionState& Progression, const FGuid& OfferId, FName Option, bool bInstallIfPossible, const FLDCardTarget& Target,
    const FBuildContext& Context, FLDGrowthBuild& Out, FLDProgressionState& OutProgression, FCardPickOutcome& Outcome)
{
    Out = Current;
    OutProgression = Progression;
    Outcome = FCardPickOutcome();
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::PickCard, Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    const FLDCardOfferState* Offer = ProgressionCurrentCardOffer(Progression, OfferId);
    if (!Offer)
    {
        return ELDGrowthReason::NoCardOffer;
    }
    if (!Offer->Options.Contains(Option))
    {
        return ELDGrowthReason::NotInOffer;
    }
    if (const ELDGrowthReason Reason = ReceiveCard(Content, Out, Option, Offer->Ordinal, Outcome.Serial); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    ProgressionResolveCardOffer(OutProgression);
    if (bInstallIfPossible)
    {
        FLDGrowthBuild Installed;
        Outcome.StoredReason = ApplyInstallCard(Content, Out, Outcome.Serial, Target, Context, Installed);
        if (Outcome.StoredReason == ELDGrowthReason::None)
        {
            Out = MoveTemp(Installed);
            Outcome.bInstalled = true;
        }
    }
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplySkipCards(const FLDDemoContentDefinitions& Content, const FLDProgressionState& Progression,
    const FGuid& OfferId, const FBuildContext& Context, FLDProgressionState& OutProgression)
{
    OutProgression = Progression;
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::SkipCards, Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (!ProgressionCurrentCardOffer(Progression, OfferId))
    {
        return ELDGrowthReason::NoCardOffer;
    }
    // Spec §6.4: a skipped offer pays CARDS.SkipVouchers.
    if (const ELDGrowthReason Reason = GrantVouchers(OutProgression, Content.Parameters.Cards.SkipVouchers); Reason != ELDGrowthReason::None)
    {
        OutProgression = Progression;
        return Reason;
    }
    ProgressionResolveCardOffer(OutProgression);
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplyRerollCards(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
    const FLDProgressionState& Progression, const FGuid& OfferId, const FGuid& MatchSecret, const FGuid& Contender,
    const FBuildContext& Context, FLDProgressionState& OutProgression)
{
    OutProgression = Progression;
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::RerollCards, Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    const FLDCardOfferState* Offer = ProgressionCurrentCardOffer(Progression, OfferId);
    if (!Offer)
    {
        return ELDGrowthReason::NoCardOffer;
    }
    if (CardRerollsLeft(Progression) < 1)
    {
        return ELDGrowthReason::NoReroll;
    }
    FLDCardOfferState Redrawn = *Offer;
    ++Redrawn.Rerolls;
    if (const ELDGrowthReason Reason = DrawCardOffer(Content, Build, CardOfferSize(Content.Parameters, Offer->Source),
        CardOfferSeed(MatchSecret, Contender, Offer->Ordinal, Redrawn.Rerolls), Redrawn.Options); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    Redrawn.OfferId = FGuid::NewGuid();
    Redrawn.Revision = ++OutProgression.LastCardOfferRevision;
    MarkSeenCards(OutProgression, Redrawn.Options);
    OutProgression.CardOffer = MoveTemp(Redrawn);
    ++OutProgression.CardRerollsUsed;
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplyInstallCard(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    uint16 Serial, const FLDCardTarget& Target, const FBuildContext& Context, FLDGrowthBuild& Out, uint16 Replacing,
    TArray<uint16>* OutUninstalled)
{
    Out = Current;
    if (OutUninstalled) { OutUninstalled->Reset(); }
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (const ELDGrowthReason Reason = InstallGate(Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    const FLDCardInstance* Card = Current.FindCard(Serial);
    if (!Card)
    {
        return ELDGrowthReason::NotOwned;
    }
    if (Card->IsInstalled())
    {
        return ELDGrowthReason::AlreadyInstalled;
    }
    if (Replacing != 0)
    {
        FLDCardInstance* Replaced = Out.FindCard(Replacing);
        if (!Replaced || Replacing == Serial)
        {
            return ELDGrowthReason::NotOwned;
        }
        if (!Replaced->IsInstalled())
        {
            return ELDGrowthReason::NotInstalled;
        }
        Replaced->Target = FLDCardTarget();
        if (OutUninstalled) { OutUninstalled->Add(Replacing); }
    }
    if (const ELDGrowthReason Reason = CardTargetReason(Content, Out, Card->Id, Target); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (InstalledCards(Out) >= CardCapacity(Content.Parameters, Out.Level))
    {
        return ELDGrowthReason::WorkshopFull;
    }
    Out.FindCard(Serial)->Target = Target;
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplyUninstallCard(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    uint16 Serial, const FBuildContext& Context, FLDGrowthBuild& Out)
{
    Out = Current;
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (const ELDGrowthReason Reason = InstallGate(Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    FLDCardInstance* Card = Out.FindCard(Serial);
    if (!Card)
    {
        return ELDGrowthReason::NotOwned;
    }
    if (!Card->IsInstalled())
    {
        return ELDGrowthReason::NotInstalled;
    }
    Card->Target = FLDCardTarget();
    return ELDGrowthReason::None;
}

// ---------------------------------------------------------------- vouchers, the rest shop and pick tickets (spec §3.13, §11.3)

ELDGrowthReason LDProgression::GrantVouchers(FLDProgressionState& Progression, int32 Amount)
{
    if (Amount < 0 || Progression.Vouchers < 0) { return ELDGrowthReason::DataInvalid; }
    if (Progression.Vouchers > MAX_int32 - Amount) { return ELDGrowthReason::ArithmeticOverflow; }
    Progression.Vouchers += Amount;
    return ELDGrowthReason::None;
}

void LDProgression::MarkSeenCards(FLDProgressionState& Progression, const TArray<FName>& Cards)
{
    for (const FName Card : Cards)
    {
        if (!Card.IsNone() && !Progression.SeenCards.Contains(Card)) { Progression.SeenCards.Add(Card); }
    }
    Progression.SeenCards.Sort(ProgressionIdLess);
}

int32 LDProgression::CampClearVouchers(const FLDDemoContentParameters& Parameters, ELDDemoCampTier Tier)
{
    switch (Tier)
    {
    case ELDDemoCampTier::Low: return Parameters.Vouchers.CampLow;
    case ELDDemoCampTier::High: return Parameters.Vouchers.CampHigh;
    case ELDDemoCampTier::Elite: return Parameters.Vouchers.CampElite;
    case ELDDemoCampTier::Ancient: return Parameters.Vouchers.CampAncient;
    default: return 0;
    }
}

uint64 LDProgression::ShopSeed(const FGuid& MatchSecret, const FGuid& Contender, uint32 Stock)
{
    // A tag of its own keeps the shelves apart from the card and active offers.
    uint32 Words[10] = {MatchSecret.A, MatchSecret.B, MatchSecret.C, MatchSecret.D,
        Contender.A, Contender.B, Contender.C, Contender.D, Stock, 0x53484f50u};
    return CityHash64(reinterpret_cast<const char*>(Words), sizeof(Words));
}

ELDGrowthReason LDProgression::DrawShop(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, uint64 Seed,
    TArray<FLDShopSlot>& Out)
{
    Out.Reset();
    const FLDDemoShopParameters& Shop = Content.Parameters.Shop;
    if (Shop.CardSlots < 0 || Shop.SkillSlots < 0 || Shop.VocabCardSlots < 0 || Shop.VocabCardSlots > Shop.CardSlots
        || Shop.VocabWeight < 1 || Shop.CardSlots > Content.Cards.Num())
    {
        return ELDGrowthReason::DataInvalid;
    }
    uint64 State = Seed;
    // Skills: uniform over the actives neither learned nor kept as blueprints (no skill slot without one).
    TArray<FName> Skills;
    EligibleBlueprints(Content, Build, Skills);
    for (int32 Index = 0; Index < Shop.SkillSlots && !Skills.IsEmpty(); ++Index)
    {
        const int32 Pick = ProgressionPick(State, Skills.Num());
        Out.Add({ELDShopItemKind::Skill, Skills[Pick], false});
        Skills.RemoveAt(Pick);
    }
    // Cards: the first VocabCardSlots weighted by the build's vocabulary, the rest uniform, all distinct.
    const uint32 Vocab = BuildVocab(Content, Build);
    TArray<TPair<FName, int64>> Pool;
    for (const FLDDemoCardDefinition& Card : Content.Cards)
    {
        Pool.Emplace(FName(*Card.Id), (Card.VocabMask & Vocab) != 0 ? int64(Shop.VocabWeight) : 1);
    }
    for (int32 Index = 0; Index < Shop.CardSlots && !Pool.IsEmpty(); ++Index)
    {
        const bool bWeighted = Index < Shop.VocabCardSlots;
        int64 Total = 0;
        for (const TPair<FName, int64>& Entry : Pool) { Total += bWeighted ? Entry.Value : 1; }
        int64 Roll = int64(ProgressionSplitMix64(State) % uint64(Total));
        int32 Pick = 0;
        while (Roll >= (bWeighted ? Pool[Pick].Value : 1)) { Roll -= bWeighted ? Pool[Pick].Value : 1; ++Pick; }
        Out.Add({ELDShopItemKind::Card, Pool[Pick].Key, false});
        Pool.RemoveAt(Pick);
    }
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::StockShop(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
    FLDProgressionState& Progression, const FGuid& MatchSecret, const FGuid& Contender, bool bRestStart)
{
    FLDShopState& Shop = Progression.Shop;
    if (Shop.Stocks == MAX_uint32 || Shop.Revision == MAX_uint64) { return ELDGrowthReason::CapacityExceeded; }
    TArray<FLDShopSlot> Slots;
    if (const ELDGrowthReason Reason = DrawShop(Content, Build, ShopSeed(MatchSecret, Contender, Shop.Stocks + 1), Slots);
        Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    ++Shop.Stocks;
    ++Shop.Revision;
    Shop.bStocked = true;
    Shop.Slots = MoveTemp(Slots);
    if (bRestStart) { Shop.Refreshes = 0; }
    TArray<FName> Cards;
    for (const FLDShopSlot& Slot : Shop.Slots) { if (Slot.Kind == ELDShopItemKind::Card) { Cards.Add(Slot.Definition); } }
    MarkSeenCards(Progression, Cards);
    return ELDGrowthReason::None;
}

int32 LDProgression::ShopRefreshPrice(const FLDDemoContentParameters& Parameters, const FLDShopState& Shop)
{
    const int64 Price = int64(Parameters.Shop.RefreshPrice) + int64(Parameters.Shop.RefreshStep) * FMath::Max(Shop.Refreshes, 0);
    return LDPacing::SalePrice(Parameters, int32(FMath::Min<int64>(Price, MAX_int32)), Shop.bSale);
}

int32 LDProgression::ShopPrice(const FLDDemoContentParameters& Parameters, ELDShopItemKind Kind, const FLDShopState& Shop)
{
    const int32 Price = Kind == ELDShopItemKind::Skill ? Parameters.Shop.SkillPrice : Kind == ELDShopItemKind::Card ? Parameters.Shop.CardPrice : 0;
    return LDPacing::SalePrice(Parameters, Price, Shop.bSale);
}

int32 LDProgression::ShopSkillPointPrice(const FLDDemoContentParameters& Parameters, const FLDShopState& Shop)
{
    return LDPacing::SalePrice(Parameters, Parameters.Shop.SkillPointPrice, Shop.bSale);
}

namespace
{
// A skill the shop or a pick ticket gives: learned into the lowest empty slot (LearnGate permitting) or kept as a blueprint.
void ProgressionGiveActive(const LDProgression::FBuildContext& Context, FName Active, FLDGrowthBuild& Build,
    LDProgression::FChoiceOutcome& Outcome)
{
    uint8 Slot = 0;
    if (LDProgression::LearnGate(Context) == ELDGrowthReason::None && LDProgression::PickLearnSlot(Build, FLDBuildCommand::AnySlot, Slot))
    {
        Build.Actives.Add({Active, Slot});
        Outcome.Result = ELDBuildChoiceResult::Learned;
        Outcome.Slot = Slot;
    }
    else
    {
        Build.PendingBlueprints.Add(Active);
        Outcome.Result = ELDBuildChoiceResult::StoredAsBlueprint;
        Outcome.StoredReason = ELDGrowthReason::NoEmptySlot;
    }
    ProgressionSortBuild(Build);
}
}

ELDGrowthReason LDProgression::ApplyShopBuy(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    const FLDProgressionState& Progression, uint8 Slot, uint64 Revision, const FBuildContext& Context, FLDGrowthBuild& Out,
    FLDProgressionState& OutProgression, FShopOutcome& Outcome)
{
    Out = Current;
    OutProgression = Progression;
    Outcome = FShopOutcome();
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None) { return Reason; }
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::ShopBuy, Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    const FLDShopState& Shop = Progression.Shop;
    if (!Shop.bStocked) { return ELDGrowthReason::NoShop; }
    if (Revision != Shop.Revision) { return ELDGrowthReason::ShopStale; }
    const FLDDemoContentParameters& Parameters = Content.Parameters;
    if (Slot == FLDBuildCommand::ShopSkillPointSlot)
    {
        if (Current.BoughtSkillPoints >= Parameters.Shop.SkillPointMaxPerMatch) { return ELDGrowthReason::SkillPointLimit; }
        const int32 PointPrice = ShopSkillPointPrice(Parameters, Shop);
        if (Progression.Vouchers < PointPrice) { return ELDGrowthReason::NotEnoughVouchers; }
        ++Out.BoughtSkillPoints;
        OutProgression.Vouchers -= PointPrice;
        ++OutProgression.Shop.Revision;
        Outcome.VouchersPaid = PointPrice;
        return ELDGrowthReason::None;
    }
    if (Slot >= Shop.Slots.Num()) { return ELDGrowthReason::InvalidRequest; }
    const FLDShopSlot& Item = Shop.Slots[Slot];
    if (Item.bSold || Item.Kind == ELDShopItemKind::None) { return ELDGrowthReason::ShopSoldOut; }
    const int32 Price = ShopPrice(Parameters, Item.Kind, Shop);
    if (Price <= 0) { return ELDGrowthReason::DataInvalid; }
    if (Item.Kind == ELDShopItemKind::Skill
        && (!ProgressionFindAbility(Content, Item.Definition) || Current.FindActive(Item.Definition) || Current.HasBlueprint(Item.Definition)))
    {
        return ELDGrowthReason::InvalidOption;
    }
    if (Progression.Vouchers < Price) { return ELDGrowthReason::NotEnoughVouchers; }
    if (Item.Kind == ELDShopItemKind::Skill)
    {
        ProgressionGiveActive(Context, Item.Definition, Out, Outcome.Choice);
    }
    else if (const ELDGrowthReason Reason = ReceiveCard(Content, Out, Item.Definition, 0, Outcome.CardSerial); Reason != ELDGrowthReason::None)
    {
        Out = Current;
        return Reason;
    }
    OutProgression.Vouchers -= Price;
    OutProgression.Shop.Slots[Slot].bSold = true;
    ++OutProgression.Shop.Revision;
    Outcome.VouchersPaid = Price;
    Outcome.Kind = Item.Kind;
    Outcome.Definition = Item.Definition;
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplyShopRefresh(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
    const FLDProgressionState& Progression, uint64 Revision, const FGuid& MatchSecret, const FGuid& Contender,
    const FBuildContext& Context, FLDProgressionState& OutProgression, int32& OutPaid)
{
    OutProgression = Progression;
    OutPaid = 0;
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::ShopRefresh, Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (!Progression.Shop.bStocked) { return ELDGrowthReason::NoShop; }
    if (Revision != Progression.Shop.Revision) { return ELDGrowthReason::ShopStale; }
    const int32 Price = ShopRefreshPrice(Content.Parameters, Progression.Shop);
    if (Price <= 0) { return ELDGrowthReason::DataInvalid; }
    if (Progression.Vouchers < Price) { return ELDGrowthReason::NotEnoughVouchers; }
    if (const ELDGrowthReason Reason = StockShop(Content, Build, OutProgression, MatchSecret, Contender, false); Reason != ELDGrowthReason::None)
    {
        OutProgression = Progression;
        return Reason;
    }
    OutProgression.Vouchers -= Price;
    ++OutProgression.Shop.Refreshes;
    OutPaid = Price;
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplyShopSell(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    const FLDProgressionState& Progression, uint16 Serial, const FBuildContext& Context, FLDGrowthBuild& Out,
    FLDProgressionState& OutProgression, int32& OutPaid)
{
    Out = Current;
    OutProgression = Progression;
    OutPaid = 0;
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None) { return Reason; }
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::ShopSell, Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    const int32 Index = Current.Cards.IndexOfByPredicate([Serial](const FLDCardInstance& Card) { return Card.Serial == Serial; });
    if (Index == INDEX_NONE) { return ELDGrowthReason::NotOwned; }
    if (Current.Cards[Index].IsInstalled()) { return ELDGrowthReason::CardInstalled; }
    if (const ELDGrowthReason Reason = GrantVouchers(OutProgression, Content.Parameters.Shop.SellPrice); Reason != ELDGrowthReason::None)
    {
        OutProgression = Progression;
        return Reason;
    }
    Out.Cards.RemoveAt(Index);
    OutPaid = Content.Parameters.Shop.SellPrice;
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplyUsePickTicket(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    const FLDProgressionState& Progression, FName Definition, const FBuildContext& Context, FLDGrowthBuild& Out,
    FLDProgressionState& OutProgression, FShopOutcome& Outcome)
{
    Out = Current;
    OutProgression = Progression;
    Outcome = FShopOutcome();
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None) { return Reason; }
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::UsePickTicket, Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (Progression.PickTickets < 1) { return ELDGrowthReason::NoPickTicket; }
    if (ProgressionFindAbility(Content, Definition))
    {
        if (Current.FindActive(Definition) || Current.HasBlueprint(Definition)) { return ELDGrowthReason::InvalidOption; }
        ProgressionGiveActive(Context, Definition, Out, Outcome.Choice);
        Outcome.Kind = ELDShopItemKind::Skill;
    }
    else if (ProgressionFindCard(Content, Definition))
    {
        if (!Progression.SeenCards.Contains(Definition)) { return ELDGrowthReason::NotSeen; }
        if (const ELDGrowthReason Reason = ReceiveCard(Content, Out, Definition, 0, Outcome.CardSerial); Reason != ELDGrowthReason::None)
        {
            Out = Current;
            return Reason;
        }
        Outcome.Kind = ELDShopItemKind::Card;
    }
    else
    {
        return ELDGrowthReason::InvalidOption;
    }
    --OutProgression.PickTickets;
    Outcome.Definition = Definition;
    return ELDGrowthReason::None;
}

FLDGrowthBuild LDProgression::BuildFromView(const FLDDemoContentDefinitions& Content, const FLDProgressionView& View)
{
    FLDGrowthBuild Build = StartBuild(Content);
    Build.Level = FMath::Max<uint8>(View.Level, 1);
    Build.Revision = View.BuildRevision;
    Build.BoughtSkillPoints = View.Shop.SkillPointsBought;
    Build.AffixSkillPoints = View.AffixSkillPoints;
    const TArray<const FLDDemoStatDefinition*> StatOrder = Content.StatsInDesignOrder();
    for (int32 Index = 0; Index < StatOrder.Num() && Index < Build.StatPoints.Num(); ++Index)
    {
        const FLDStatPointsView* Row = View.StatPoints.FindByPredicate(
            [&](const FLDStatPointsView& Stat) { return Stat.StatId == FName(*StatOrder[Index]->Id); });
        Build.StatPoints[Index] = Row ? Row->Points : 0;
    }
    for (const FLDActiveSlotView& Active : View.Actives)
    {
        Build.Actives.Add({Active.DefinitionId, Active.Slot, Active.Branch});
    }
    Build.PendingBlueprints = View.PendingBlueprints;
    for (const FLDCardInstanceView& Row : View.Cards)
    {
        FLDCardInstance& Card = Build.Cards.AddDefaulted_GetRef();
        Card.Serial = Row.Serial;
        Card.Id = Row.DefinitionId;
        Card.OfferOrdinal = Row.OfferOrdinal;
        Card.Target.Kind = Row.Target.Kind;
        Card.Target.Active = Row.Target.Active;
        Card.Target.Element = Row.Target.Element;
        Build.NextCardSerial = FMath::Max<uint16>(Build.NextCardSerial, uint16(FMath::Min<int32>(int32(Row.Serial) + 1, MAX_uint16)));
    }
    ProgressionSortBuild(Build);
    return Build;
}

FLDBuildProjectionInput LDProgression::CombatProjectionInput(const FLDDemoContentDefinitions* Content, const FLDGrowthBuild& Build)
{
    FLDBuildProjectionInput Input;
    Input.Level = Build.Level;
    if (Content && Build.StatPoints.Num() == Content->Stats.Num())
    {
        const TArray<const FLDDemoStatDefinition*> StatOrder = Content->StatsInDesignOrder();
        for (int32 Index = 0; Index < Build.StatPoints.Num(); ++Index)
        {
            if (Build.StatPoints[Index] != 0)
            {
                Input.StatPoints.Emplace(StatOrder[Index]->Id, Build.StatPoints[Index]);
            }
        }
    }
    for (const FLDLearnedActive& Active : Build.Actives)
    {
        FLDLearnedActiveValue& Value = Input.Actives.AddDefaulted_GetRef();
        Value.AbilityId = Active.Id;
        Value.Branch = Active.Branch;
        Value.Entry = Active.Slot == 0 ? ELDCombatEntry::Q
            : Active.Slot == 1 ? ELDCombatEntry::W
            : Active.Slot == 2 ? ELDCombatEntry::E
            : ELDCombatEntry::None;
    }
    for (const FLDCardInstance& Card : Build.Cards)
    {
        if (Card.IsInstalled())
        {
            FLDInstalledCardValue& Value = Input.Cards.AddDefaulted_GetRef();
            Value.CardId = Card.Id;
            Value.Serial = Card.Serial;
            Value.TargetKind = Card.Target.Kind;
            Value.Active = Card.Target.Active;
            Value.Element = Card.Target.Element;
        }
    }
    Input.ResultBuildRevision = Build.Revision;
    if (Content && FinalModBound(*Content, Build)) { Input.FinalMod = Build.FinalMod; }
    return Input;
}

void LDProgression::EligibleBlueprints(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, TArray<FName>& Out)
{
    Out.Reset();
    for (const FLDDemoAbilityDefinition& Ability : Content.Abilities)
    {
        const FName Id(*Ability.Id);
        if (!Build.FindActive(Id) && !Build.HasBlueprint(Id))
        {
            Out.Add(Id);
        }
    }
}

ELDGrowthReason LDProgression::DrawFieldReward(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
    ELDFieldRewardKind Kind, uint64 Seed, FFieldGrant& Out)
{
    Out = FFieldGrant();
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Build); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    uint64 State = Seed;
    TArray<FName> Actives;
    EligibleBlueprints(Content, Build, Actives);
    switch (Kind)
    {
    case ELDFieldRewardKind::BuildDraw:
        // A fair roll first; without an eligible active the reward is the card offer.
        if (!Actives.IsEmpty() && (ProgressionSplitMix64(State) & 1ull) == 0)
        {
            Out.Result = ELDFieldRewardResult::Blueprint;
            Out.Definition = Actives[ProgressionPick(State, Actives.Num())];
            return ELDGrowthReason::None;
        }
        Out.Result = ELDFieldRewardResult::CardOffer;
        return ELDGrowthReason::None;
    case ELDFieldRewardKind::Blueprint:
        if (Actives.IsEmpty())
        {
            Out.Result = ELDFieldRewardResult::BlueprintHeld;
            return ELDGrowthReason::None;
        }
        Out.Result = ELDFieldRewardResult::Blueprint;
        Out.Definition = Actives[ProgressionPick(State, Actives.Num())];
        return ELDGrowthReason::None;
    case ELDFieldRewardKind::CardOffer:
        Out.Result = ELDFieldRewardResult::CardOffer;
        return ELDGrowthReason::None;
    case ELDFieldRewardKind::MiniFlagOffer: case ELDFieldRewardKind::ArenaOffer: case ELDFieldRewardKind::LockerOffer:
    case ELDFieldRewardKind::PeddlerOffer: case ELDFieldRewardKind::FridgeOffer: case ELDFieldRewardKind::BonusOffer:
    {
        static const ELDCardOfferSource Sources[] = {ELDCardOfferSource::MiniFlag, ELDCardOfferSource::Arena, ELDCardOfferSource::Locker,
            ELDCardOfferSource::Peddler, ELDCardOfferSource::CampFridge, ELDCardOfferSource::CampBonus};
        Out.Result = ELDFieldRewardResult::CardOffer;
        Out.OfferSource = Sources[uint8(Kind) - uint8(ELDFieldRewardKind::MiniFlagOffer)];
        return ELDGrowthReason::None;
    }
    case ELDFieldRewardKind::None:
        break;
    }
    return ELDGrowthReason::DataInvalid;
}

uint64 LDProgression::FieldRewardSeed(const FGuid& MatchSecret, const FGuid& Contender, FName Event, uint64 Serial)
{
    const FString EventText = Event.ToString();
    const uint64 EventHash = CityHash64(reinterpret_cast<const char*>(*EventText), EventText.Len() * sizeof(TCHAR));
    uint32 Words[13] = {MatchSecret.A, MatchSecret.B, MatchSecret.C, MatchSecret.D,
        Contender.A, Contender.B, Contender.C, Contender.D, uint32(EventHash), uint32(EventHash >> 32),
        uint32(Serial), uint32(Serial >> 32), 0x4649454Cu};
    return CityHash64(reinterpret_cast<const char*>(Words), sizeof(Words));
}

uint64 LDProgression::HeldBlueprintSeed(const FGuid& MatchSecret, const FGuid& Contender, uint32 Index)
{
    uint32 Words[10] = {MatchSecret.A, MatchSecret.B, MatchSecret.C, MatchSecret.D,
        Contender.A, Contender.B, Contender.C, Contender.D, Index, 0x48454C44u};
    return CityHash64(reinterpret_cast<const char*>(Words), sizeof(Words));
}

ELDGrowthReason LDProgression::ApplyFieldGrant(const FLDDemoContentDefinitions& Content, const FFieldGrant& Grant, FName Event,
    uint64 Serial, int64 XP, FLDGrowthBuild& Build, FLDProgressionState& Progression)
{
    switch (Grant.Result)
    {
    case ELDFieldRewardResult::Blueprint:
        if (!ProgressionFindAbility(Content, Grant.Definition) || Build.FindActive(Grant.Definition) || Build.HasBlueprint(Grant.Definition))
        {
            return ELDGrowthReason::DataInvalid;
        }
        Build.PendingBlueprints.Add(Grant.Definition);
        ProgressionSortBuild(Build);
        break;
    case ELDFieldRewardResult::CardOffer:
    {
        uint16 First = 0, Last = 0;
        if (const ELDGrowthReason Reason = QueueCardOffers(Progression, Grant.OfferSource, 1, First, Last);
            Reason != ELDGrowthReason::None)
        {
            return Reason;
        }
        break;
    }
    case ELDFieldRewardResult::BlueprintHeld:
        if (Progression.HeldBlueprints == MAX_uint8)
        {
            return ELDGrowthReason::CapacityExceeded;
        }
        ++Progression.HeldBlueprints;
        break;
    case ELDFieldRewardResult::None:
        return ELDGrowthReason::DataInvalid;
    }
    FLDFieldRewardView& Record = Progression.FieldRewards.AddDefaulted_GetRef();
    Record.Event = Event;
    Record.Serial = Serial;
    Record.XP = XP;
    Record.Result = Grant.Result;
    Record.Definition = Grant.Definition;
    return ELDGrowthReason::None;
}

bool LDProgression::ReleaseHeldBlueprints(const FLDDemoContentDefinitions& Content, const FGuid& MatchSecret, const FGuid& Contender,
    FLDGrowthBuild& Build, FLDProgressionState& Progression)
{
    bool bReleased = false;
    while (Progression.HeldBlueprints > 0)
    {
        TArray<FName> Actives;
        EligibleBlueprints(Content, Build, Actives);
        if (Actives.IsEmpty())
        {
            break;
        }
        uint64 State = HeldBlueprintSeed(MatchSecret, Contender, ++Progression.HeldBlueprintsEver);
        Build.PendingBlueprints.Add(Actives[ProgressionPick(State, Actives.Num())]);
        ProgressionSortBuild(Build);
        --Progression.HeldBlueprints;
        bReleased = true;
    }
    return bReleased;
}
