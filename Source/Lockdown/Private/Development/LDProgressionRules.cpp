#include "Development/LDProgressionRules.h"
#include "Development/LDWorkshopRules.h"

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

const FLDDemoComponentDefinition* ProgressionFindComponent(const FLDDemoContentDefinitions& Content, FName Id)
{
    return Content.Components.FindByPredicate([&](const FLDDemoComponentDefinition& Row) { return FName(*Row.Id) == Id; });
}

bool ProgressionListsId(const TArray<FString>& Ids, FName Id)
{
    return !Id.IsNone() && Ids.ContainsByPredicate([Id](const FString& Row) { return FName(*Row) == Id; });
}

// The stored shape of an installed component (ValidateBuild): a base component without a target, carrying exactly the
// binding parameters its definition takes. Whether its active, host or repeated base is still held is checked by
// ValidateBuild itself: forgetting or uninstalling reclaims the install in the same commit (spec §5).
bool ProgressionInstalledShape(const FLDDemoComponentDefinition& Definition, const FLDComponentTarget& Target)
{
    // #198 F: a mod or connector stores its target (no segment form) in the shape its definition accepts.
    if (Definition.Kind != ELDDemoComponentKind::Base)
    {
        return Target.Segment == 0 && LDWorkshop::TargetShape(Definition, Target.Kind, Target.Definition, Target.BoundActive,
            Target.Element, Target.BoundComponent);
    }
    if (Target.Kind != ELDComponentTargetKind::None || !Target.Definition.IsNone() || Target.Segment != 0
        || !Target.BoundComponent.IsNone())
    {
        return false;
    }
    switch (Definition.Binding)
    {
    case ELDDemoComponentBinding::None:
        return Target.BoundActive.IsNone() && Target.Element == ELDDemoElement::None;
    case ELDDemoComponentBinding::ActiveElement:
        return ProgressionListsId(Definition.BindActives, Target.BoundActive) && Target.Element != ELDDemoElement::None
            && Definition.BindElements.Contains(Target.Element);
    }
    return false;
}

// Spec §4.4: whether an installed component other than Definition shares its form on the same target.
bool ProgressionFormConflict(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
    const FLDDemoComponentDefinition& Definition, const FLDComponentTarget& Target)
{
    const FName Self(*Definition.Id);
    return !Definition.Form.IsEmpty() && Build.Components.ContainsByPredicate([&](const FLDComponentInstance& Other)
    {
        const FLDDemoComponentDefinition* OtherDefinition = Other.bInstalled && Other.Id != Self ? ProgressionFindComponent(Content, Other.Id) : nullptr;
        return OtherDefinition && Other.Target.Kind == Target.Kind && Other.Target.Definition == Target.Definition
            && LDWorkshop::Conflicts(Definition, *OtherDefinition);
    });
}

// #198 F: whether Target is a legal install of the mod or connector Definition on Build now (spec §12.1, §13.1): the shape
// its definition accepts (InvalidTarget), a learned active or an installed host component (InvalidTarget), C12's repeated
// base installed (InvalidTarget), C01 on B18 only when B18 burns (InvalidTarget), no other component of its form on the
// same target (TargetConflict).
ELDGrowthReason ProgressionWorkshopTargetReason(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
    const FLDDemoComponentDefinition& Definition, const FLDComponentTarget& Target)
{
    if (Target.Segment != 0 || !LDWorkshop::TargetShape(Definition, Target.Kind, Target.Definition, Target.BoundActive, Target.Element,
        Target.BoundComponent))
    {
        return ELDGrowthReason::InvalidTarget;
    }
    if (Target.Kind == ELDComponentTargetKind::Active && !Build.FindActive(Target.Definition))
    {
        return ELDGrowthReason::InvalidTarget;
    }
    const FName Self(*Definition.Id);
    const auto Installed = [&Build, &Self](FName Id) -> const FLDComponentInstance*
    {
        const FLDComponentInstance* Host = Id == Self ? nullptr : Build.FindComponent(Id);
        return Host && Host->bInstalled ? Host : nullptr;
    };
    const FLDComponentInstance* Host = Target.Kind == ELDComponentTargetKind::Component ? Installed(Target.Definition) : nullptr;
    if (Target.Kind == ELDComponentTargetKind::Component && !Host)
    {
        return ELDGrowthReason::InvalidTarget;
    }
    const FLDComponentInstance* Base = Target.BoundComponent.IsNone() ? nullptr : Installed(Target.BoundComponent);
    if (!Target.BoundComponent.IsNone() && !Base)
    {
        return ELDGrowthReason::InvalidTarget;
    }
    // C01 reads a fire source: B18 counts only while it is bound to fire.
    if (LDWorkshop::FindOp(Definition, ELDDemoEffectOp::FireShield) && Host && Host->Target.BoundActive != NAME_None
        && Host->Target.Element != ELDDemoElement::Fire)
    {
        return ELDGrowthReason::InvalidTarget;
    }
    if (ProgressionFormConflict(Content, Build, Definition, Target))
    {
        return ELDGrowthReason::TargetConflict;
    }
    return ELDGrowthReason::None;
}

void ProgressionSortBuild(FLDGrowthBuild& Build)
{
    Build.Actives.Sort([](const FLDLearnedActive& A, const FLDLearnedActive& B) { return A.Slot < B.Slot; });
    Build.PendingBlueprints.Sort([](FName A, FName B) { return ProgressionIdLess(A, B); });
    Build.Components.Sort([](const FLDComponentInstance& A, const FLDComponentInstance& B) { return ProgressionIdLess(A.Id, B.Id); });
}

// #198 F: Removing grows by every installed instance that sits on one of its entries (as the host component or as C12's
// repeated base), transitively; each id once.
void ProgressionCloseOverAttached(const FLDGrowthBuild& Build, TArray<FName>& Removing)
{
    for (int32 Index = 0; Index < Removing.Num(); ++Index)
    {
        for (const FLDComponentInstance& Attached : Build.Components)
        {
            const bool bOnIt = Attached.bInstalled && ((Attached.Target.Kind == ELDComponentTargetKind::Component
                && Attached.Target.Definition == Removing[Index]) || Attached.Target.BoundComponent == Removing[Index]);
            if (bOnIt && !Removing.Contains(Attached.Id)) { Removing.Add(Attached.Id); }
        }
    }
}

// Back to the inventory with their history: a later install is a rest-window move, never a first install.
void ProgressionReturnToInventory(FLDGrowthBuild& Build, const TArray<FName>& Ids)
{
    for (const FName& Id : Ids)
    {
        FLDComponentInstance* Removed = Build.FindComponent(Id);
        check(Removed);
        Removed->bInstalled = false;
        Removed->Target = FLDComponentTarget();
    }
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

const FLDComponentInstance* FLDGrowthBuild::FindComponent(FName Id) const
{
    return Components.FindByPredicate([&](const FLDComponentInstance& Instance) { return Instance.Id == Id; });
}

FLDComponentInstance* FLDGrowthBuild::FindComponent(FName Id)
{
    return Components.FindByPredicate([&](const FLDComponentInstance& Instance) { return Instance.Id == Id; });
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

ELDGrowthReason LDProgression::EarnedPoints(const FLDDemoContentDefinitions& Content, uint8 Level, int64 XP, uint8 EmptyPoolGrants,
    int64& Out)
{
    Out = 0;
    const FLDDemoContentParameters& P = Content.Parameters;
    const FLDDemoProgressionDefinition& Progression = Content.Progression;
    uint8 XPLevel = 0;
    if (const ELDGrowthReason Reason = LevelForXP(Progression, XP, XPLevel); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    // Each exhausted-pool grant replaced the component of one normal level-up, so there are never more than levels crossed.
    if (XPLevel != Level || P.OpeningPoints < 0 || P.PointsPerLevel < 0 || P.MaxLevelPoints < 0 || P.MaxLevelPointsXP <= 0
        || P.EmptyComponentPoolPoints < 0 || int32(EmptyPoolGrants) > LevelComponentDrawsThrough(Progression, Level))
    {
        return ELDGrowthReason::DataInvalid;
    }
    int64 Earned = 0, Grants = 0, Extra = 0, PoolPoints = 0;
    if (!ProgressionCheckedMul(P.PointsPerLevel, int64(Level) - 1, Earned) || !ProgressionCheckedAdd(Earned, P.OpeningPoints, Earned)
        || !ProgressionCheckedMul(P.EmptyComponentPoolPoints, EmptyPoolGrants, PoolPoints)
        || !ProgressionCheckedAdd(Earned, PoolPoints, Earned))
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
    uint8 EmptyPoolGrants, int64& Out)
{
    Out = 0;
    int64 Earned = 0, Spent = 0;
    if (const ELDGrowthReason Reason = EarnedPoints(Content, Build.Level, XP, EmptyPoolGrants, Earned); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (Build.StatPoints.Num() != Content.Stats.Num() || !Build.SpentPoints(Spent) || Build.PermanentSpentPoints < 0
        || Build.AwardedPoints < 0)
    {
        return ELDGrowthReason::DataInvalid;
    }
    // #198 session D: PK participation, winner and betting points add to the earned ones.
    if (!ProgressionCheckedAdd(Spent, Build.PermanentSpentPoints, Spent) || !ProgressionCheckedAdd(Earned, Build.AwardedPoints, Earned))
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
    // #198 B: one instance per definition sorted by id, at most one opening pick and LevelComponentDraws level-up grants per level.
    int32 OpeningPicks = 0;
    TMap<uint8, int32> GrantLevels;
    for (int32 Index = 0; Index < Build.Components.Num(); ++Index)
    {
        const FLDComponentInstance& Instance = Build.Components[Index];
        const FLDDemoComponentDefinition* Definition = ProgressionFindComponent(Content, Instance.Id);
        bool bDuplicateLevel = false;
        if (Instance.Source == ELDComponentSource::LevelUp)
        {
            int32& Count = GrantLevels.FindOrAdd(Instance.SourceLevel);
            bDuplicateLevel = ++Count > int32(LevelComponentDraws(Content.Progression, Instance.SourceLevel));
        }
        OpeningPicks += Instance.Source == ELDComponentSource::OpeningPick ? 1 : 0;
        // #198 G: a field reward, elite choice, FillGap or Dismantle instance records the level it was received at.
        const bool bService = Instance.Source == ELDComponentSource::BuildReward || Instance.Source == ELDComponentSource::EliteChoice
            || Instance.Source == ELDComponentSource::FillGap || Instance.Source == ELDComponentSource::Dismantle;
        const bool bSource = Instance.Source == ELDComponentSource::OpeningPick ? Instance.SourceLevel == 1
            : Instance.Source == ELDComponentSource::LevelUp
                ? Instance.SourceLevel >= 2 && Instance.SourceLevel <= Build.Level && !bDuplicateLevel
                : bService && Instance.SourceLevel >= 1 && Instance.SourceLevel <= Build.Level;
        if (!Definition || !bSource || (Index > 0 && !ProgressionIdLess(Build.Components[Index - 1].Id, Instance.Id))
            || (Instance.bInstalled && !Instance.bEverInstalled)
            || (Instance.bGapFilled && Definition->Kind == ELDDemoComponentKind::Base)
            || (Instance.bInstalled ? !ProgressionInstalledShape(*Definition, Instance.Target) : !Instance.Target.IsEmpty()))
        {
            return ELDGrowthReason::DataInvalid;
        }
        // Spec §5: an install never outlives what it sits on; forgetting an active or uninstalling a host reclaims it first.
        const FLDComponentTarget& Target = Instance.Target;
        const auto HostInstalled = [&Build](FName Id)
        {
            const FLDComponentInstance* Host = Build.FindComponent(Id);
            return Host && Host->bInstalled;
        };
        if (Instance.bInstalled && ((Target.Kind == ELDComponentTargetKind::Active && !Build.FindActive(Target.Definition))
            || (!Target.BoundActive.IsNone() && !Build.FindActive(Target.BoundActive))
            || (Target.Kind == ELDComponentTargetKind::Component && !HostInstalled(Target.Definition))
            || (!Target.BoundComponent.IsNone() && !HostInstalled(Target.BoundComponent))
            || ProgressionFormConflict(Content, Build, *Definition, Target)))
        {
            return ELDGrowthReason::DataInvalid;
        }
    }
    if (OpeningPicks > 1 || InstalledComponents(Build) > WorkshopCapacity(Content.Progression, Build.Level))
    {
        return ELDGrowthReason::DataInvalid;
    }
    return ELDGrowthReason::None;
}

int32 LDProgression::WorkshopCapacity(const FLDDemoProgressionDefinition& Progression, uint8 Level)
{
    if (Progression.WorkshopCapacityBase < 1 || Progression.WorkshopCapacityLevelsPerSlot < 1 || Level < 1)
    {
        return 0;
    }
    return Progression.WorkshopCapacityBase + (int32(Level) - 1) / Progression.WorkshopCapacityLevelsPerSlot;
}

int32 LDProgression::InstalledComponents(const FLDGrowthBuild& Build)
{
    int32 Count = 0;
    for (const FLDComponentInstance& Instance : Build.Components)
    {
        Count += Instance.bInstalled ? 1 : 0;
    }
    return Count;
}

uint8 LDProgression::LevelComponentDraws(const FLDDemoProgressionDefinition& Progression, uint8 Level)
{
    if (Level < 2 || int32(Level) > Progression.MaxLevel)
    {
        return 0;
    }
    return Progression.BonusComponentLevels.Contains(int32(Level)) ? 2 : 1;
}

int32 LDProgression::LevelComponentDrawsThrough(const FLDDemoProgressionDefinition& Progression, uint8 Level)
{
    int32 Draws = 0;
    for (int32 Reached = 2; Reached <= int32(Level); ++Reached)
    {
        Draws += LevelComponentDraws(Progression, uint8(Reached));
    }
    return Draws;
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
    case ELDMatchPhase::CaptureOpen: return Timeline.Stage == ELDMatchStage::FinalField ? EMatchStage::Closed : EMatchStage::Live;
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
    // (PK included) and in the death wait alike; there is no build safe window and no build lock any more.
    return Context.Stage == EMatchStage::Closed ? ELDGrowthReason::PhaseClosed : ELDGrowthReason::None;
}

ELDRestService LDProgression::RestServiceFor(ELDBuildCommandKind Kind)
{
    switch (Kind)
    {
    case ELDBuildCommandKind::BuyNpcOffer: return ELDRestService::SkillMerchant;
    case ELDBuildCommandKind::FillGap:
    case ELDBuildCommandKind::Dismantle: return ELDRestService::ComponentArtisan;
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
    case ELDBuildCommandKind::BuyNpcOffer:
    case ELDBuildCommandKind::Respec:
    case ELDBuildCommandKind::ReplaceActive:
    case ELDBuildCommandKind::ForgetActive:
    case ELDBuildCommandKind::OpeningBase:
    case ELDBuildCommandKind::InstallComponent:
    case ELDBuildCommandKind::UninstallComponent:
    case ELDBuildCommandKind::ComponentChoice:
    case ELDBuildCommandKind::FillGap:
    case ELDBuildCommandKind::Dismantle:
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
    // #222: the own build commands are open in every stage of a running match, alive or dead, in combat and in a PK; the
    // server still checks the resources, slots and structure of each one.
    case ELDBuildCommandKind::ActiveChoice:
    case ELDBuildCommandKind::SkipActiveOffer:
    case ELDBuildCommandKind::ComponentChoice:
    case ELDBuildCommandKind::OpeningBase:
    case ELDBuildCommandKind::LearnBlueprint:
    case ELDBuildCommandKind::AllocatePoints:
    case ELDBuildCommandKind::InstallComponent:
    case ELDBuildCommandKind::UninstallComponent:
        return ELDGrowthReason::None;
    // The NPC services keep their rest build window (the build and payout steps), their NPC and a living body.
    case ELDBuildCommandKind::BuyNpcOffer:
    case ELDBuildCommandKind::Respec:
    case ELDBuildCommandKind::FillGap:
    case ELDBuildCommandKind::Dismantle:
        return Context.Stage == EMatchStage::RestLocked ? ELDGrowthReason::BuildLocked
            : !bRest ? ELDGrowthReason::NotRestWindow : bDead ? ELDGrowthReason::Dead : ELDGrowthReason::None;
    case ELDBuildCommandKind::ReplaceActive:
    case ELDBuildCommandKind::ForgetActive:
        return Context.Stage == EMatchStage::RestLocked ? ELDGrowthReason::BuildLocked
            : !bRest ? ELDGrowthReason::ReplacementNotAllowed : bDead ? ELDGrowthReason::Dead : ELDGrowthReason::None;
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
    const FLDActiveOfferState& Offer, const FBuildContext& Context, FLDGrowthBuild& Out, int64& OutPoints)
{
    Out = Current;
    OutPoints = 0;
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None) { return Reason; }
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::SkipActiveOffer, Context); Reason != ELDGrowthReason::None)
    { return Reason; }
    const int64 Points = Offer.bNpcPurchased ? Content.Parameters.NpcActiveSkipPoints : Content.Parameters.ActiveSkipPoints;
    if (Points <= 0 || (Offer.bNpcPurchased && Points >= Content.Parameters.NpcActivePoints))
    { return ELDGrowthReason::DataInvalid; }
    if (!ProgressionCheckedAdd(Current.AwardedPoints, Points, Out.AwardedPoints))
    {
        Out = Current;
        return ELDGrowthReason::ArithmeticOverflow;
    }
    OutPoints = Points;
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
    int64 XP, uint8 EmptyPoolGrants, const TArray<uint8>& PointCounts, const FBuildContext& Context, FLDGrowthBuild& Out)
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
    if (const ELDGrowthReason Reason = UnspentPoints(Content, Current, XP, EmptyPoolGrants, Unspent); Reason != ELDGrowthReason::None)
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
    if (const ELDGrowthReason Reason = UnspentPoints(Content, Current, Progression.CumulativeXP, Progression.EmptyPoolGrants, Unspent); Reason != ELDGrowthReason::None)
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
    Offer.bNpcPurchased = true;
    Offer.GeneratedBuildRevision = Current.Revision;
    Offer.Revision = ++OutProgression.LastOfferRevision;
    OutProgression.Offer.Emplace(MoveTemp(Offer));
    Outcome.PointsPaid = Cost;
    Outcome.Ordinal = Ordinal;
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplyRespec(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current, int64 XP,
    uint8 EmptyPoolGrants, const FBuildContext& Context, FLDGrowthBuild& Out, int64& OutRefunded)
{
    Out = Current;
    OutRefunded = 0;
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    int64 Unspent = 0;
    if (const ELDGrowthReason Reason = UnspentPoints(Content, Current, XP, EmptyPoolGrants, Unspent); Reason != ELDGrowthReason::None)
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
    FName& OutForgotten, TArray<FName>& OutReclaimed)
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
    // The old active is forgotten, not stored: only a new source gives it back (Combat keeps its cooldown track). What sat on
    // it returns to the inventory in the same commit; nothing moves to the new active on its own.
    OutForgotten = Current.Actives[Index].Id;
    Out.Actives.RemoveAt(Index);
    Out.PendingBlueprints.Remove(Blueprint);
    Out.Actives.Add({Blueprint, Slot});
    ReclaimAttachments(Out, OutForgotten, OutReclaimed);
    ProgressionSortBuild(Out);
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplyForgetActive(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    const FLDProgressionState& Progression, uint8 Slot, const FBuildContext& Context, FLDGrowthBuild& Out, FName& OutForgotten,
    TArray<FName>& OutReclaimed)
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

void LDProgression::ReclaimAttachments(FLDGrowthBuild& Build, FName Forgotten, TArray<FName>& OutReclaimed)
{
    OutReclaimed.Reset();
    if (Forgotten.IsNone())
    {
        return;
    }
    for (const FLDComponentInstance& Instance : Build.Components)
    {
        const FLDComponentTarget& Target = Instance.Target;
        const bool bOnActive = (Target.Kind == ELDComponentTargetKind::Active || Target.Kind == ELDComponentTargetKind::EffectSegment)
            && Target.Definition == Forgotten;
        if (Instance.bInstalled && (bOnActive || Target.BoundActive == Forgotten))
        {
            OutReclaimed.Add(Instance.Id);
        }
    }
    ProgressionCloseOverAttached(Build, OutReclaimed);
    ProgressionReturnToInventory(Build, OutReclaimed);
    OutReclaimed.Sort([](FName A, FName B) { return ProgressionIdLess(A, B); });
}

// ---- #198 B components ----

namespace
{
ELDGrowthReason ProgressionLevelComponentPools(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, uint8 Level,
    TArray<FName> (&Pools)[3], int64 (&Weights)[3])
{
    const FLDDemoProgressionDefinition& Progression = Content.Progression;
    if (Level < 2 || int32(Level) > Progression.MaxLevel)
    {
        return ELDGrowthReason::DataInvalid;
    }
    const FLDDemoComponentCategoryWeights* Band = nullptr;
    for (const FLDDemoComponentCategoryWeights& Row : Progression.ComponentCategoryWeights)
    {
        if (Row.MinLevel <= int32(Level) && int32(Level) <= Row.MaxLevel)
        {
            if (Band)
            {
                return ELDGrowthReason::DataInvalid;
            }
            Band = &Row;
        }
    }
    if (!Band || Band->BasePercent < 0 || Band->ModPercent < 0 || Band->ConnectorPercent < 0
        || Band->BasePercent + Band->ModPercent + Band->ConnectorPercent != 100)
    {
        return ELDGrowthReason::DataInvalid;
    }
    if (const ELDGrowthReason Reason = LDProgression::ValidateBuild(Content, Build); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    // A definition is drawable only when the build can use it now. A base applies once owned (a B18 binding still
    // needs one learned active of its list); a mod or connector needs a target it could install on: itself, the innate D,
    // a learned active or an owned component it lists, plus an owned base of its ExtraBases when it takes one (C12). A
    // definition with a draw requirement also needs one of its DrawRequiresActives learned (C17, C21).
    const auto Learned = [&Build](const TArray<FString>& Ids)
    {
        return Build.Actives.ContainsByPredicate([&Ids](const FLDLearnedActive& Active) { return LDWorkshop::Lists(Ids, Active.Id); });
    };
    const auto Owned = [&Build](const TArray<FString>& Ids)
    {
        return Build.Components.ContainsByPredicate([&Ids](const FLDComponentInstance& Instance) { return LDWorkshop::Lists(Ids, Instance.Id); });
    };
    const auto Hostable = [&](const FLDDemoComponentDefinition& Definition)
    {
        if (Definition.Kind == ELDDemoComponentKind::Base)
        {
            return Definition.Binding == ELDDemoComponentBinding::None || Learned(Definition.BindActives);
        }
        return (Definition.bTargetSelf || Definition.bTargetDash || Learned(Definition.TargetActives) || Owned(Definition.TargetComponents))
            && (Definition.ExtraBases.IsEmpty() || Owned(Definition.ExtraBases));
    };
    // Indexed by ELDDemoComponentKind: Base, Mod, Connector.
    Weights[0] = Band->BasePercent;
    Weights[1] = Band->ModPercent;
    Weights[2] = Band->ConnectorPercent;
    bool bUnownedRollable = false;
    bool bAnyUnowned = false;
    for (const FLDDemoComponentDefinition& Definition : Content.Components)
    {
        const int32 Kind = int32(Definition.Kind);
        if (Kind < 0 || Kind > 2)
        {
            return ELDGrowthReason::DataInvalid;
        }
        const FName Id(*Definition.Id);
        if (Definition.Pool == ELDDemoComponentPool::Common && !Build.FindComponent(Id))
        {
            bAnyUnowned = true;
            bUnownedRollable |= Weights[Kind] > 0;
            if (Hostable(Definition) && (Definition.DrawRequiresActives.IsEmpty() || Learned(Definition.DrawRequiresActives)))
            {
                Pools[Kind].Add(Id);
            }
        }
    }
    // Definitions are left only in categories this band never rolls: a table that cannot hand them out, not an empty pool.
    if (bAnyUnowned && !bUnownedRollable)
    {
        return ELDGrowthReason::DataInvalid;
    }
    return ELDGrowthReason::None;
}
}

ELDGrowthReason LDProgression::LevelComponentEligible(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
    uint8 Level, FName Component, bool& OutEligible)
{
    OutEligible = false;
    TArray<FName> Pools[3];
    int64 Weights[3] = {};
    const ELDGrowthReason Reason = ProgressionLevelComponentPools(Content, Build, Level, Pools, Weights);
    if (Reason != ELDGrowthReason::None) { return Reason; }
    for (int32 Kind = 0; Kind < 3; ++Kind)
    {
        OutEligible |= Weights[Kind] > 0 && Pools[Kind].Contains(Component);
    }
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::DrawLevelComponent(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, uint8 Level,
    uint64 Seed, FName& OutComponent)
{
    OutComponent = NAME_None;
    TArray<FName> Pools[3];
    int64 Weights[3] = {};
    const ELDGrowthReason Reason = ProgressionLevelComponentPools(Content, Build, Level, Pools, Weights);
    if (Reason != ELDGrowthReason::None) { return Reason; }
    // An empty category (nothing unowned and hostable) is dropped; the others keep their relative weights. Nothing hostable
    // in any rolled category is an empty pool.
    int64 Total = 0;
    for (int32 Kind = 0; Kind < 3; ++Kind)
    {
        Total += Pools[Kind].IsEmpty() ? 0 : Weights[Kind];
    }
    if (Total <= 0)
    {
        return ELDGrowthReason::None;
    }
    uint64 State = Seed;
    int64 Roll = int64(ProgressionSplitMix64(State) % uint64(Total));
    for (int32 Kind = 0; Kind < 3; ++Kind)
    {
        if (Pools[Kind].IsEmpty())
        {
            continue;
        }
        if (Roll < Weights[Kind])
        {
            OutComponent = Pools[Kind][int32(ProgressionSplitMix64(State) % uint64(Pools[Kind].Num()))];
            return ELDGrowthReason::None;
        }
        Roll -= Weights[Kind];
    }
    return ELDGrowthReason::DataInvalid;
}

uint64 LDProgression::ComponentSeed(const FGuid& MatchSecret, const FGuid& Contender, uint8 Level, uint8 Draw)
{
    // The trailing tag keeps the component roll of level N apart from active opportunity N's OfferSeed; a bonus draw
    // takes its own tag so the first draw of every level keeps its material.
    uint32 Words[10] = {MatchSecret.A, MatchSecret.B, MatchSecret.C, MatchSecret.D,
        Contender.A, Contender.B, Contender.C, Contender.D, Level, Draw == 0 ? 0x434F4D50u : 0x434F4D50u + Draw};
    return CityHash64(reinterpret_cast<const char*>(Words), sizeof(Words));
}

ELDGrowthReason LDProgression::GrantLevelComponents(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    uint8 LevelBefore, uint8 LevelAfter, const FGuid& MatchSecret, const FGuid& Contender, FLDGrowthBuild& Out,
    uint8& OutEmptyPoolGrants, TArray<FLDComponentGrant>& OutGrants)
{
    Out = Current;
    OutEmptyPoolGrants = 0;
    OutGrants.Reset();
    if (LevelBefore < 1 || LevelAfter < LevelBefore)
    {
        return ELDGrowthReason::DataInvalid;
    }
    for (int32 Level = int32(LevelBefore) + 1; Level <= int32(LevelAfter); ++Level)
    {
        const uint8 Draws = LevelComponentDraws(Content.Progression, uint8(Level));
        for (uint8 Draw = 0; Draw < Draws; ++Draw)
        {
            FName Picked;
            const ELDGrowthReason Reason = DrawLevelComponent(Content, Out, uint8(Level),
                ComponentSeed(MatchSecret, Contender, uint8(Level), Draw), Picked);
            if (Reason != ELDGrowthReason::None)
            {
                Out = Current;
                OutEmptyPoolGrants = 0;
                OutGrants.Reset();
                return Reason;
            }
            OutGrants.Add({uint8(Level), Picked});
            if (Picked.IsNone())
            {
                ++OutEmptyPoolGrants;
                continue;
            }
            FLDComponentInstance& Instance = Out.Components.AddDefaulted_GetRef();
            Instance.Id = Picked;
            Instance.Source = ELDComponentSource::LevelUp;
            Instance.SourceLevel = uint8(Level);
            ProgressionSortBuild(Out);
        }
    }
    return ELDGrowthReason::None;
}

FName LDProgression::OpeningActive(const FLDProgressionState& Progression)
{
    const FLDActiveOpportunityRecord* Opening = Progression.Records.FindByPredicate(
        [](const FLDActiveOpportunityRecord& Row) { return Row.Ordinal == 1; });
    return Opening ? Opening->Chosen : NAME_None;
}

bool LDProgression::OpeningBaseTaken(const FLDGrowthBuild& Build)
{
    return Build.Components.ContainsByPredicate(
        [](const FLDComponentInstance& Instance) { return Instance.Source == ELDComponentSource::OpeningPick; });
}

ELDGrowthReason LDProgression::OpeningBaseOptions(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
    FName OpeningChoice, TArray<FName>& Out)
{
    Out.Reset();
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Build); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    for (const FLDDemoComponentDefinition& Definition : Content.Components)
    {
        const FName Id(*Definition.Id);
        if (Definition.Kind != ELDDemoComponentKind::Base || Definition.Pool != ELDDemoComponentPool::Common || Build.FindComponent(Id))
        {
            continue;
        }
        if (Definition.bOpeningAlways || ProgressionListsId(Definition.OpeningActives, OpeningChoice))
        {
            Out.Add(Id);
        }
    }
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::InstallGate(const FBuildContext& Context)
{
    // #222: a first install and a move of a once-installed component follow the same open matrix as a learn.
    return LearnGate(Context);
}

ELDGrowthReason LDProgression::InstallTargetReason(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
    FName Component, const FLDComponentTarget& Target)
{
    const FLDDemoComponentDefinition* Definition = ProgressionFindComponent(Content, Component);
    if (Definition && Definition->Kind != ELDDemoComponentKind::Base)
    {
        return ProgressionWorkshopTargetReason(Content, Build, *Definition, Target);
    }
    if (!Definition || Target.Kind != ELDComponentTargetKind::None || !Target.Definition.IsNone() || Target.Segment != 0
        || !Target.BoundComponent.IsNone())
    {
        return ELDGrowthReason::InvalidTarget;
    }
    if (Definition->Binding == ELDDemoComponentBinding::ActiveElement)
    {
        if (!ProgressionListsId(Definition->BindActives, Target.BoundActive) || !Build.FindActive(Target.BoundActive)
            || Target.Element == ELDDemoElement::None || !Definition->BindElements.Contains(Target.Element))
        {
            return ELDGrowthReason::InvalidTarget;
        }
    }
    else if (!Target.BoundActive.IsNone() || Target.Element != ELDDemoElement::None)
    {
        return ELDGrowthReason::InvalidTarget;
    }
    return ProgressionFormConflict(Content, Build, *Definition, Target) ? ELDGrowthReason::TargetConflict : ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplyOpeningBase(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    const FLDProgressionState& Progression, FName Component, bool bInstallIfPossible, const FLDComponentTarget& Target,
    const FBuildContext& Context, FLDGrowthBuild& Out, FInstallOutcome& Outcome)
{
    Out = Current;
    Outcome = FInstallOutcome();
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (!bInstallIfPossible && !Target.IsEmpty())
    {
        return ELDGrowthReason::InvalidRequest;
    }
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::OpeningBase, Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    const FName Opening = OpeningActive(Progression);
    if (!Progression.Records.ContainsByPredicate([](const FLDActiveOpportunityRecord& Row) { return Row.Ordinal == 1; }))
    {
        return ELDGrowthReason::OpeningActiveFirst;
    }
    if (OpeningBaseTaken(Current))
    {
        return ELDGrowthReason::OpeningBaseTaken;
    }
    TArray<FName> Options;
    if (const ELDGrowthReason Reason = OpeningBaseOptions(Content, Current, Opening, Options); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (Component.IsNone() || !Options.Contains(Component))
    {
        return ELDGrowthReason::OpeningBaseUnavailable;
    }
    FLDComponentInstance Instance;
    Instance.Id = Component;
    Instance.Source = ELDComponentSource::OpeningPick;
    Instance.SourceLevel = 1;
    if (bInstallIfPossible)
    {
        // The install half never fails the pick; it only keeps the component in the inventory when the target is not legal.
        Outcome.StoredReason = InstallTargetReason(Content, Current, Component, Target);
        if (Outcome.StoredReason == ELDGrowthReason::None
            && InstalledComponents(Current) >= WorkshopCapacity(Content.Progression, Current.Level))
        {
            Outcome.StoredReason = ELDGrowthReason::WorkshopFull;
        }
        if (Outcome.StoredReason == ELDGrowthReason::None)
        {
            Instance.bInstalled = true;
            Instance.bEverInstalled = true;
            Instance.Target = Target;
            Outcome.bInstalled = true;
        }
    }
    Out.Components.Add(MoveTemp(Instance));
    ProgressionSortBuild(Out);
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplyInstallComponent(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    FName Component, const FLDComponentTarget& Target, const FBuildContext& Context, FLDGrowthBuild& Out, FName Replacing,
    TArray<FName>* OutUninstalled)
{
    Out = Current;
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    const FLDComponentInstance* Instance = Current.FindComponent(Component);
    if (Component.IsNone() || !Instance)
    {
        return ELDGrowthReason::NotOwned;
    }
    if (Instance->bInstalled)
    {
        return ELDGrowthReason::AlreadyInstalled;
    }
    if (const ELDGrowthReason Reason = InstallGate(Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    // Atomic replace: the replaced instance and what sits on it come off first; the install is judged on that build.
    FLDGrowthBuild Reduced = Current;
    TArray<FName> Removing;
    if (!Replacing.IsNone())
    {
        const FLDComponentInstance* Replaced = Current.FindComponent(Replacing);
        if (Replacing == Component || !Replaced)
        {
            return Replacing == Component ? ELDGrowthReason::InvalidRequest : ELDGrowthReason::NotOwned;
        }
        if (!Replaced->bInstalled)
        {
            return ELDGrowthReason::NotInstalled;
        }
        if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::UninstallComponent, Context); Reason != ELDGrowthReason::None)
        {
            return Reason;
        }
        Removing.Add(Replacing);
        ProgressionCloseOverAttached(Reduced, Removing);
        ProgressionReturnToInventory(Reduced, Removing);
    }
    if (const ELDGrowthReason Reason = InstallTargetReason(Content, Reduced, Component, Target); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    // Every installed component takes one workshop slot; replace or uninstall one (its attachments come off with it) to make room.
    if (InstalledComponents(Reduced) >= WorkshopCapacity(Content.Progression, Reduced.Level))
    {
        return ELDGrowthReason::WorkshopFull;
    }
    Out = Reduced;
    FLDComponentInstance* Installed = Out.FindComponent(Component);
    Installed->bInstalled = true;
    Installed->bEverInstalled = true;
    Installed->Target = Target;
    if (OutUninstalled)
    {
        *OutUninstalled = Removing;
    }
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplyUninstallComponent(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    FName Component, const FBuildContext& Context, FLDGrowthBuild& Out)
{
    Out = Current;
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    const FLDComponentInstance* Instance = Current.FindComponent(Component);
    if (Component.IsNone() || !Instance)
    {
        return ELDGrowthReason::NotOwned;
    }
    if (!Instance->bInstalled)
    {
        return ELDGrowthReason::NotInstalled;
    }
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::UninstallComponent, Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    // Back to the inventory with its history (bEverInstalled stays: it was enabled once). #198 F: the mods
    // and connectors installed on it (or repeating it, C12) come off with it, transitively.
    TArray<FName> Removing{Component};
    ProgressionCloseOverAttached(Out, Removing);
    ProgressionReturnToInventory(Out, Removing);
    return ELDGrowthReason::None;
}

// ---- #198 G field rewards and rest material services ----

namespace
{
// Uniform index of [0, Count) from State; Count must be positive.
int32 ProgressionPick(uint64& State, int32 Count)
{
    return int32(ProgressionSplitMix64(State) % uint64(Count));
}

// Adds an owned inventory instance of Id received from Source at the build's level; the caller checked it is unowned.
void ProgressionReceive(FLDGrowthBuild& Build, FName Id, ELDComponentSource Source)
{
    FLDComponentInstance& Instance = Build.Components.AddDefaulted_GetRef();
    Instance.Id = Id;
    Instance.Source = Source;
    Instance.SourceLevel = Build.Level;
    ProgressionSortBuild(Build);
}

// The held entries of a mod/connector (spec §4.5): owned components, learned actives and pending blueprints its targets
// list; -1 when it takes the innate D or its holder, which always exist.
int32 ProgressionHeldEntries(const FLDDemoComponentDefinition& Definition, const FLDGrowthBuild& Build)
{
    if (Definition.bTargetDash || Definition.bTargetSelf)
    {
        return -1;
    }
    int32 Held = 0;
    for (const FString& Row : Definition.TargetActives)
    {
        const FName Id(*Row);
        Held += Build.FindActive(Id) || Build.HasBlueprint(Id) ? 1 : 0;
    }
    for (const FString& Row : Definition.TargetComponents)
    {
        Held += Build.FindComponent(FName(*Row)) ? 1 : 0;
    }
    return Held;
}

// The element kinds one held entry applies natively (spec §4.5, C09): an active's burn/wet/electrify effects, a base's burn or
// basic element, and B18's bound element while it is installed with one. bUnbound adds every element a still-unbound
// binding could take (an unowned filler candidate).
void ProgressionEntryElements(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, FName Id, bool bUnbound,
    TSet<ELDDemoElement>& Out)
{
    const auto FromEffects = [&Out](const TArray<FLDDemoEffectDefinition>& Effects)
    {
        for (const FLDDemoEffectDefinition& Effect : Effects)
        {
            switch (Effect.GetOp())
            {
            case ELDDemoEffectOp::Burn:
            case ELDDemoEffectOp::ZoneBurn: Out.Add(ELDDemoElement::Fire); break;
            case ELDDemoEffectOp::Wet:
            case ELDDemoEffectOp::ZoneWet: Out.Add(ELDDemoElement::Water); break;
            case ELDDemoEffectOp::Electrify: Out.Add(ELDDemoElement::Electric); break;
            case ELDDemoEffectOp::BasicElement:
                if (Effect.Element != ELDDemoElement::None) { Out.Add(Effect.Element); }
                break;
            default: break;
            }
        }
    };
    if (const FLDDemoAbilityDefinition* Ability = ProgressionFindAbility(Content, Id))
    {
        FromEffects(Ability->Values.Effects);
        return;
    }
    const FLDDemoComponentDefinition* Definition = ProgressionFindComponent(Content, Id);
    if (!Definition)
    {
        return;
    }
    FromEffects(Definition->Effects);
    if (Definition->Binding == ELDDemoComponentBinding::ActiveElement)
    {
        const FLDComponentInstance* Instance = Build.FindComponent(Id);
        if (Instance && Instance->bInstalled && Instance->Target.Element != ELDDemoElement::None)
        {
            Out.Add(Instance->Target.Element);
        }
        else if (bUnbound && !Instance)
        {
            Out.Append(Definition->BindElements);
        }
    }
}

// The distinct element kinds C09's held entries (learned actives, pending blueprints, owned components it targets) apply.
TSet<ELDDemoElement> ProgressionHeldElements(const FLDDemoContentDefinitions& Content, const FLDDemoComponentDefinition& Definition,
    const FLDGrowthBuild& Build)
{
    TSet<ELDDemoElement> Kinds;
    for (const FString& Row : Definition.TargetActives)
    {
        const FName Id(*Row);
        if (Build.FindActive(Id) || Build.HasBlueprint(Id)) { ProgressionEntryElements(Content, Build, Id, false, Kinds); }
    }
    for (const FString& Row : Definition.TargetComponents)
    {
        const FName Id(*Row);
        if (Build.FindComponent(Id)) { ProgressionEntryElements(Content, Build, Id, false, Kinds); }
    }
    return Kinds;
}

// Whether the owned Target is a gap now (spec §4.5): a mod/connector never enabled, not yet filled, without any compatible
// held entry. A reaction connector (C09) has an entry only once its held entries apply two distinct element kinds: two fire
// sources can never react.
ELDGrowthReason ProgressionGapReason(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, FName Target)
{
    const FLDComponentInstance* Instance = Build.FindComponent(Target);
    const FLDDemoComponentDefinition* Definition = Instance ? ProgressionFindComponent(Content, Target) : nullptr;
    if (!Definition || Definition->Kind == ELDDemoComponentKind::Base || Instance->bEverInstalled)
    {
        return ELDGrowthReason::NotAGap;
    }
    if (Instance->bGapFilled)
    {
        return ELDGrowthReason::GapAlreadyFilled;
    }
    if (LDWorkshop::FindOp(*Definition, ELDDemoEffectOp::ReactionEmpower))
    {
        return ProgressionHeldElements(Content, *Definition, Build).Num() < 2 ? ELDGrowthReason::None : ELDGrowthReason::NotAGap;
    }
    const int32 Held = ProgressionHeldEntries(*Definition, Build);
    return Held == 0 ? ELDGrowthReason::None : ELDGrowthReason::NotAGap;
}
}

FLDGrowthBuild LDProgression::BuildFromView(const FLDDemoContentDefinitions& Content, const FLDProgressionView& View)
{
    FLDGrowthBuild Build = StartBuild(Content);
    Build.Level = FMath::Max<uint8>(View.Level, 1);
    Build.Revision = View.BuildRevision;
    Build.PermanentSpentPoints = View.PermanentSpentPoints;
    const TArray<const FLDDemoStatDefinition*> StatOrder = Content.StatsInDesignOrder();
    for (int32 Index = 0; Index < StatOrder.Num() && Index < Build.StatPoints.Num(); ++Index)
    {
        const FLDStatPointsView* Row = View.StatPoints.FindByPredicate(
            [&](const FLDStatPointsView& Stat) { return Stat.StatId == FName(*StatOrder[Index]->Id); });
        Build.StatPoints[Index] = Row ? Row->Points : 0;
    }
    for (const FLDActiveSlotView& Active : View.Actives)
    {
        Build.Actives.Add({Active.DefinitionId, Active.Slot});
    }
    Build.PendingBlueprints = View.PendingBlueprints;
    for (const FLDComponentInstanceView& Row : View.Components)
    {
        FLDComponentInstance& Instance = Build.Components.AddDefaulted_GetRef();
        Instance.Id = Row.DefinitionId;
        Instance.Source = Row.Source;
        Instance.SourceLevel = Row.SourceLevel;
        Instance.bInstalled = Row.bInstalled;
        Instance.bEverInstalled = Row.bEverInstalled;
        Instance.bGapFilled = Row.bGapFilled;
        Instance.Target.Kind = Row.Target.Kind;
        Instance.Target.Definition = Row.Target.Definition;
        Instance.Target.Segment = Row.Target.Segment;
        Instance.Target.BoundActive = Row.Target.BoundActive;
        Instance.Target.Element = Row.Target.Element;
        Instance.Target.BoundComponent = Row.Target.BoundComponent;
    }
    ProgressionSortBuild(Build);
    return Build;
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

void LDProgression::UnownedComponents(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
    ELDDemoComponentPool Pool, TArray<FName>& Out)
{
    Out.Reset();
    for (const FLDDemoComponentDefinition& Definition : Content.Components)
    {
        const FName Id(*Definition.Id);
        if (Definition.Pool == Pool && !Build.FindComponent(Id))
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
    TArray<FName> Actives, Scarce, Common;
    EligibleBlueprints(Content, Build, Actives);
    UnownedComponents(Content, Build, ELDDemoComponentPool::Scarce, Scarce);
    UnownedComponents(Content, Build, ELDDemoComponentPool::Common, Common);
    switch (Kind)
    {
    case ELDFieldRewardKind::BuildDraw:
    {
        // A fair class roll first; an empty class hands the reward to the other one.
        const bool bBlueprintRoll = (ProgressionSplitMix64(State) & 1ull) == 0;
        const bool bBlueprint = !Actives.IsEmpty() && (bBlueprintRoll || Scarce.IsEmpty());
        if (bBlueprint)
        {
            Out.Result = ELDFieldRewardResult::Blueprint;
            Out.Definition = Actives[ProgressionPick(State, Actives.Num())];
            return ELDGrowthReason::None;
        }
        if (!Scarce.IsEmpty())
        {
            Out.Result = ELDFieldRewardResult::ScarceComponent;
            Out.Definition = Scarce[ProgressionPick(State, Scarce.Num())];
            return ELDGrowthReason::None;
        }
        // Both classes empty: one ordinary material reward, rolled like a normal level-up of the current level.
        FName Picked;
        const uint8 Level = uint8(FMath::Clamp<int32>(Build.Level, 2, Content.Progression.MaxLevel));
        if (const ELDGrowthReason Reason = DrawLevelComponent(Content, Build, Level, ProgressionSplitMix64(State), Picked);
            Reason != ELDGrowthReason::None)
        {
            return Reason;
        }
        Out.Result = Picked.IsNone() ? ELDFieldRewardResult::PoolPoints : ELDFieldRewardResult::CommonComponent;
        Out.Definition = Picked;
        return ELDGrowthReason::None;
    }
    case ELDFieldRewardKind::Blueprint:
        if (Actives.IsEmpty())
        {
            Out.Result = ELDFieldRewardResult::BlueprintHeld;
            return ELDGrowthReason::None;
        }
        Out.Result = ELDFieldRewardResult::Blueprint;
        Out.Definition = Actives[ProgressionPick(State, Actives.Num())];
        return ELDGrowthReason::None;
    case ELDFieldRewardKind::ComponentChoice:
        if (Common.IsEmpty())
        {
            Out.Result = ELDFieldRewardResult::PoolPoints;
            return ELDGrowthReason::None;
        }
        Out.Result = ELDFieldRewardResult::ComponentChoice;
        while (Out.ChoiceOptions.Num() < 2 && !Common.IsEmpty())
        {
            const int32 Pick = ProgressionPick(State, Common.Num());
            Out.ChoiceOptions.Add(Common[Pick]);
            Common.RemoveAt(Pick);
        }
        return ELDGrowthReason::None;
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
    uint64 Serial, int64 XP, const FGuid& ChoiceId, FLDGrowthBuild& Build, FLDProgressionState& Progression)
{
    const FLDDemoComponentDefinition* Component = Grant.Definition.IsNone() ? nullptr : ProgressionFindComponent(Content, Grant.Definition);
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
    case ELDFieldRewardResult::ScarceComponent:
    case ELDFieldRewardResult::CommonComponent:
        if (!Component || Build.FindComponent(Grant.Definition)
            || (Component->Pool == ELDDemoComponentPool::Scarce) != (Grant.Result == ELDFieldRewardResult::ScarceComponent))
        {
            return ELDGrowthReason::DataInvalid;
        }
        ProgressionReceive(Build, Grant.Definition, ELDComponentSource::BuildReward);
        break;
    case ELDFieldRewardResult::PoolPoints:
        if (Content.Parameters.EmptyComponentPoolPoints < 1
            || !ProgressionCheckedAdd(Build.AwardedPoints, Content.Parameters.EmptyComponentPoolPoints, Build.AwardedPoints))
        {
            return ELDGrowthReason::DataInvalid;
        }
        break;
    case ELDFieldRewardResult::ComponentChoice:
    {
        if (!ChoiceId.IsValid() || Grant.ChoiceOptions.IsEmpty() || Grant.ChoiceOptions.Num() > 2)
        {
            return ELDGrowthReason::DataInvalid;
        }
        FLDComponentChoiceView& Choice = Progression.ComponentChoices.AddDefaulted_GetRef();
        Choice.ChoiceId = ChoiceId;
        Choice.Event = Event;
        Choice.Serial = Serial;
        Choice.Options = Grant.ChoiceOptions;
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

void LDProgression::GapTargets(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, TArray<FName>& Out)
{
    Out.Reset();
    for (const FLDDemoComponentDefinition& Definition : Content.Components)
    {
        const FName Id(*Definition.Id);
        if (Build.FindComponent(Id) && ProgressionGapReason(Content, Build, Id) == ELDGrowthReason::None)
        {
            Out.Add(Id);
        }
    }
}

ELDGrowthReason LDProgression::GapFillers(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, FName Target,
    TArray<FName>& Out)
{
    Out.Reset();
    if (const ELDGrowthReason Reason = ProgressionGapReason(Content, Build, Target); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    const FLDDemoComponentDefinition* Definition = ProgressionFindComponent(Content, Target);
    // C09: a filler must add an element kind its held entries do not apply yet.
    const bool bReaction = LDWorkshop::FindOp(*Definition, ELDDemoEffectOp::ReactionEmpower) != nullptr;
    const TSet<ELDDemoElement> HeldKinds = bReaction ? ProgressionHeldElements(Content, *Definition, Build) : TSet<ELDDemoElement>();
    for (const FLDDemoComponentDefinition& Candidate : Content.Components)
    {
        const FName Id(*Candidate.Id);
        if (Candidate.Kind != ELDDemoComponentKind::Base || Candidate.Pool != ELDDemoComponentPool::Common || Build.FindComponent(Id)
            || !(LDWorkshop::Lists(Definition->TargetComponents, Id) || LDWorkshop::Lists(Definition->ExtraBases, Id)))
        {
            continue;
        }
        if (bReaction)
        {
            TSet<ELDDemoElement> Adds;
            ProgressionEntryElements(Content, Build, Id, true, Adds);
            if (Adds.Difference(HeldKinds).IsEmpty())
            {
                continue;
            }
        }
        Out.Add(Id);
    }
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplyFillGap(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current, int64 XP,
    uint8 EmptyPoolGrants, FName Target, FName Filler, const FBuildContext& Context, FLDGrowthBuild& Out, int64& OutPaid)
{
    Out = Current;
    OutPaid = 0;
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    int64 Unspent = 0;
    if (const ELDGrowthReason Reason = UnspentPoints(Content, Current, XP, EmptyPoolGrants, Unspent); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::FillGap, Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    TArray<FName> Fillers;
    if (const ELDGrowthReason Reason = GapFillers(Content, Current, Target, Fillers); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (!Fillers.Contains(Filler))
    {
        return ELDGrowthReason::NoGapFiller;
    }
    const int64 Cost = Content.Parameters.FillGapPoints;
    if (Cost < 1)
    {
        return ELDGrowthReason::DataInvalid;
    }
    if (Unspent < Cost)
    {
        return ELDGrowthReason::NotEnoughPoints;
    }
    if (!ProgressionCheckedAdd(Current.PermanentSpentPoints, Cost, Out.PermanentSpentPoints))
    {
        Out = Current;
        return ELDGrowthReason::ArithmeticOverflow;
    }
    Out.FindComponent(Target)->bGapFilled = true;
    ProgressionReceive(Out, Filler, ELDComponentSource::FillGap);
    OutPaid = Cost;
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::DismantleInputReason(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
    const TArray<FName>& Surrendered)
{
    if (Content.Parameters.DismantleInputCount < 1 || Surrendered.Num() != Content.Parameters.DismantleInputCount)
    {
        return ELDGrowthReason::DismantleInvalid;
    }
    for (int32 Index = 0; Index < Surrendered.Num(); ++Index)
    {
        const FName Id = Surrendered[Index];
        const FLDDemoComponentDefinition* Definition = ProgressionFindComponent(Content, Id);
        if (Id.IsNone() || !Definition || Definition->Pool != ELDDemoComponentPool::Common || !Build.FindComponent(Id)
            || Surrendered.Find(Id) != Index)
        {
            return ELDGrowthReason::DismantleInvalid;
        }
    }
    return ELDGrowthReason::None;
}

ELDGrowthReason LDProgression::ApplyDismantle(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    const TArray<FName>& Surrendered, uint64 Seed, const FBuildContext& Context, FLDGrowthBuild& Out, FDismantleOutcome& Outcome)
{
    Out = Current;
    Outcome = FDismantleOutcome();
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::Dismantle, Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (const ELDGrowthReason Reason = DismantleInputReason(Content, Current, Surrendered); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    // The surrendered ones are owned, so the unowned common pool already excludes them.
    TArray<FName> Pool;
    UnownedComponents(Content, Current, ELDDemoComponentPool::Common, Pool);
    if (Pool.IsEmpty())
    {
        return ELDGrowthReason::DismantlePoolEmpty;
    }
    uint64 State = Seed;
    Outcome.Received = Pool[ProgressionPick(State, Pool.Num())];
    // Every install left without its host component or repeated base comes off, transitively (like an uninstall).
    TArray<FName> Gone = Surrendered;
    for (int32 Index = 0; Index < Gone.Num(); ++Index)
    {
        for (const FLDComponentInstance& Attached : Out.Components)
        {
            const bool bOnIt = Attached.bInstalled && ((Attached.Target.Kind == ELDComponentTargetKind::Component
                && Attached.Target.Definition == Gone[Index]) || Attached.Target.BoundComponent == Gone[Index]);
            if (bOnIt && !Gone.Contains(Attached.Id))
            {
                Gone.Add(Attached.Id);
                Outcome.Uninstalled.Add(Attached.Id);
            }
        }
    }
    for (const FName& Id : Outcome.Uninstalled)
    {
        FLDComponentInstance* Detached = Out.FindComponent(Id);
        Detached->bInstalled = false;
        Detached->Target = FLDComponentTarget();
    }
    Out.Components.RemoveAll([&Surrendered](const FLDComponentInstance& Instance) { return Surrendered.Contains(Instance.Id); });
    ProgressionReceive(Out, Outcome.Received, ELDComponentSource::Dismantle);
    return ELDGrowthReason::None;
}

uint64 LDProgression::DismantleSeed(const FGuid& MatchSecret, const FGuid& Contender, uint64 RequestId)
{
    uint32 Words[11] = {MatchSecret.A, MatchSecret.B, MatchSecret.C, MatchSecret.D,
        Contender.A, Contender.B, Contender.C, Contender.D, uint32(RequestId), uint32(RequestId >> 32), 0x44534D54u};
    return CityHash64(reinterpret_cast<const char*>(Words), sizeof(Words));
}

ELDGrowthReason LDProgression::ApplyComponentChoice(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    const FLDProgressionState& Progression, const FGuid& ChoiceId, FName Option, const FBuildContext& Context,
    FLDGrowthBuild& Out, FLDProgressionState& OutProgression, bool& OutEmptyPoolGrant)
{
    Out = Current;
    OutProgression = Progression;
    OutEmptyPoolGrant = false;
    if (const ELDGrowthReason Reason = ValidateBuild(Content, Current); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    if (const ELDGrowthReason Reason = CommandGate(ELDBuildCommandKind::ComponentChoice, Context); Reason != ELDGrowthReason::None)
    {
        return Reason;
    }
    const int32 Index = Progression.ComponentChoices.IndexOfByPredicate(
        [&ChoiceId](const FLDComponentChoiceView& Row) { return Row.ChoiceId == ChoiceId; });
    if (!ChoiceId.IsValid() || Index == INDEX_NONE)
    {
        return ELDGrowthReason::NoComponentChoice;
    }
    const FLDComponentChoiceView& Choice = Progression.ComponentChoices[Index];
    if (Option.IsNone())
    {
        // The empty-pool points only once every known option has been obtained elsewhere.
        if (Choice.Options.ContainsByPredicate([&Current](FName Id) { return !Current.FindComponent(Id); }))
        {
            return ELDGrowthReason::ComponentOwned;
        }
        if (!ProgressionCheckedAdd(Current.AwardedPoints, Content.Parameters.EmptyComponentPoolPoints, Out.AwardedPoints))
        {
            Out = Current;
            return ELDGrowthReason::ArithmeticOverflow;
        }
        OutEmptyPoolGrant = true;
    }
    else
    {
        if (!Choice.Options.Contains(Option))
        {
            return ELDGrowthReason::InvalidOption;
        }
        if (Current.FindComponent(Option))
        {
            return ELDGrowthReason::ComponentOwned;
        }
        ProgressionReceive(Out, Option, ELDComponentSource::EliteChoice);
    }
    OutProgression.ComponentChoices.RemoveAt(Index);
    return ELDGrowthReason::None;
}
