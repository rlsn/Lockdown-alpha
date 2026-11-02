#include "AI/LDOpponentPolicy.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/LDOpponentFrame.h"
#include "Combat/LDAttributeProjection.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDGrowthProtocol.h"
#include "../Development/LDDemoContentJson.h"
#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Navigation/LDOrderTypes.h"
#include "Region/LDMapGeometry.h"
#include "UObject/Package.h"
#include "Vision/LDViewTypes.h"

namespace LDOpponentPolicyTests
{
namespace
{
// Reads the producer-exported approved_base only; there is no live-data fallback.
bool ReadApprovedFixture(FAutomationTestBase& Test, TSharedPtr<const FLDDemoContentSnapshot>& Out)
{
    using namespace LDDemoContent;
    FString Root, Error;
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoContentFixtures="), Root) || Root.IsEmpty())
    {
        Test.AddError(TEXT("Missing -LDDemoContentFixtures=<isolated Artifacts directory>; export approved_base first. No live-data fallback."));
        return false;
    }
    if (!Test.TestTrue(TEXT("Opponent fixtures are isolated under this project's Artifacts"),
        FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Root),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts")))))
    {
        return false;
    }
    FCheck Check(Error);
    TArray<uint8> Bytes;
    FValue IndexValue;
    if (!ReadFile(FPaths::Combine(Root, TEXT("index.json")), Bytes, Error)
        || !Parse(Bytes, IndexValue, Check, TEXT("fixture index")))
    {
        Test.AddError(Error);
        return false;
    }
    FFields Index(Check, IndexValue, TEXT("fixture index"));
    bool bTestOnly = false;
    const TArray<FValue>* Valid = nullptr;
    const TArray<FValue>* Invalid = nullptr;
    if (!Index.Literal(TEXT("format"), TEXT("demo-native-fixtures-1"))
        || !Index.Flag(TEXT("test_only"), bTestOnly) || !Check.Require(bTestOnly, Index.Path, TEXT("test-only marker required"))
        || !Array(Index.Take(TEXT("valid")), Valid, Check, Index.Path, 1, 1024)
        || !Array(Index.Take(TEXT("invalid")), Invalid, Check, Index.Path, 0, 1024) || !Index.Done())
    {
        Test.AddError(Error);
        return false;
    }
    bool bFound = false;
    FLDDemoContentDigest Digest;
    for (const auto& RowValue : *Valid)
    {
        FFields Row(Check, RowValue, TEXT("fixture case"));
        FString Name;
        if (!Row.Identifier(TEXT("name"), Name))
        {
            Test.AddError(Error);
            return false;
        }
        if (Name != TEXT("approved_base")) { continue; }
        const TArray<FValue>* ChangedPaths = nullptr;
        FString MutatedQuantity;
        if (!Check.Require(!bFound, Row.Path, TEXT("duplicate approved_base fixture"))
            || !Row.Digest(TEXT("catalog_sha256"), Digest)
            || !Array(Row.Take(TEXT("changed_paths")), ChangedPaths, Check, Row.Path, 0, MaxFileBytes)
            || !Row.Text(TEXT("mutated_quantity"), MutatedQuantity, 0) || !Row.Done())
        {
            Test.AddError(Error);
            return false;
        }
        bFound = true;
    }
    if (!Test.TestTrue(TEXT("approved_base is explicit; synthetic base is not an approved balance fixture"), bFound))
    {
        return false;
    }
    FLDDemoContentBundle Bundle;
    auto* Asset = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
    TUniquePtr<FLDDemoPreparedContent> Prepared;
    if (!FLDDemoContentBundle::ReadImportFiles(FPaths::Combine(Root, TEXT("approved_base")), Bundle, Error)
        || !ULDDemoContentCatalog::PrepareImport(*Asset, Bundle, Digest, Prepared, Error)
        || !Asset->CommitImport(Prepared, Digest, Error) || !Asset->Read(Digest, Out, Error))
    {
        Test.AddError(Error);
        return false;
    }
    const auto& AI = Out->Get().Parameters.AI;
    return Test.TestTrue(TEXT("Fixture carries the approved first-play AI parameters"),
        AI.RetreatHPBp == 2500 && AI.ResumeFarmHPBp == 6000 && AI.HighCampEntryHPBp == 8000
        && AI.HighCampReserveHPBp == 3500 && AI.LowCampReserveHPBp == 1500 && AI.CaptureReactionBudgetMs == 2000
        && AI.DecisionIntervalMs == 250 && AI.RetryMaxAttempts == 3 && AI.RetryInitialBackoffMs == 250
        && AI.RetryMaxBackoffMs == 500 && AI.ExploreRevisitIntervalMs == 20000 && AI.EngagementBudgetMs == 40000);
}

bool ProjectLevel(FAutomationTestBase& Test, const FLDDemoContentSnapshot& Catalog, uint8 Level, FLDProjectedAttributes& Out)
{
    const auto Result = LDAttributeProjection::Project(FLDAttributeBuildValues(Level, {}, {}, {}), Catalog);
    if (!Test.TestTrue(FString::Printf(TEXT("Level %u bare projection succeeds"), Level), Result.IsSuccess())) { return false; }
    Out = Result.Value->Final;
    return true;
}

const FLDDemoMonsterDefinition* Monster(FAutomationTestBase& Test, const FLDDemoContentSnapshot& Catalog, const TCHAR* Id)
{
    const FLDDemoMonsterDefinition* Out = nullptr;
    FString Error;
    if (!Catalog.FindMonster(Id, Out, Error) || !Out) { Test.AddError(Error); }
    return Out;
}

bool CheckCamp(FAutomationTestBase& Test, const FString& Label, const FLDCampCostEstimate& Actual,
    ELDCampCostStatus Status, int64 ClearMs, int64 PlayerHits, int64 Retaliations, int64 TakenP2, int64 RemainingP2)
{
    return Test.TestTrue(Label + FString::Printf(TEXT(" (status %u clear %lld hits %lld/%lld taken %lld remaining %lld: %s)"),
        uint8(Actual.Status), Actual.ClearMs, Actual.PlayerHits, Actual.RetaliationHits, Actual.DamageTakenP2,
        Actual.RemainingHpP2, *Actual.Error),
        Actual.Status == Status && Actual.ClearMs == ClearMs && Actual.PlayerHits == PlayerHits
        && Actual.RetaliationHits == Retaliations && Actual.DamageTakenP2 == TakenP2 && Actual.RemainingHpP2 == RemainingP2);
}

bool RunCampCost(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(Test, Catalog)) { return false; }
    FLDProjectedAttributes L1, L10;
    const auto* Low = Monster(Test, *Catalog, TEXT("monster_scavenger"));
    const auto* High = Monster(Test, *Catalog, TEXT("monster_guardian"));
    if (!ProjectLevel(Test, *Catalog, 1, L1) || !ProjectLevel(Test, *Catalog, 10, L10) || !Low || !High) { return false; }
    const int64 Budget = Catalog->Get().Parameters.AI.EngagementBudgetMs;
    FLDCampCostInput Input;
    Input.CurrentHpP2 = Input.MaxHpP2 = L1.MaxHPP2;
    Input.Attributes = L1;
    Input.EngagementBudgetMs = Budget;
    bool bGood = Test.TestEqual(TEXT("Level 1 bare MaxHP is the approved 620"), L1.MaxHPP2, int64(62000));
    // CONTENT_AND_BALANCE §7.2 timings; damage is the regen-free conservative sum of its listed retaliations.
    bGood &= CheckCamp(Test, TEXT("L1 low camp: 4 hits, first-hit retaliation, 5 x 8.47"),
        LDOpponentPolicy::EstimateCampCost(Input, *Low, *Catalog), ELDCampCostStatus::Ok, 4537, 4, 5, 4235, 57765);
    bGood &= CheckCamp(Test, TEXT("L1 high camp: 10 hits, 9 x 40.67"),
        LDOpponentPolicy::EstimateCampCost(Input, *High, *Catalog), ELDCampCostStatus::Ok, 13111, 10, 9, 36603, 25397);
    FLDCampCostInput Ten = Input;
    Ten.CurrentHpP2 = Ten.MaxHpP2 = L10.MaxHPP2;
    Ten.Attributes = L10;
    bGood &= CheckCamp(Test, TEXT("L10 low camp uses projected attack speed and windup"),
        LDOpponentPolicy::EstimateCampCost(Ten, *Low, *Catalog), ELDCampCostStatus::Ok, 3845, 4, 4, 2940, L10.MaxHPP2 - 2940);
    bGood &= CheckCamp(Test, TEXT("L10 high camp"),
        LDOpponentPolicy::EstimateCampCost(Ten, *High, *Catalog), ELDCampCostStatus::Ok, 7478, 7, 5, 17645, L10.MaxHPP2 - 17645);

    bGood &= Test.TestEqual(TEXT("A retaliation landing exactly at the death time counts"),
        LDOpponentPolicy::RetaliationHits(0, 350, 1250, 350), int64(1));
    bGood &= Test.TestEqual(TEXT("One millisecond earlier it does not land"), LDOpponentPolicy::RetaliationHits(0, 349, 1250, 350), int64(0));
    bGood &= Test.TestEqual(TEXT("Retaliation cadence"), LDOpponentPolicy::RetaliationHits(0, 1600, 1250, 350), int64(2));
    bGood &= Test.TestEqual(TEXT("A member dead before retaliation starts never strikes"),
        LDOpponentPolicy::RetaliationHits(100, 99, 1250, 350), int64(0));

    FLDCampCostInput Exhausted = Input;
    Exhausted.CurrentHpP2 = 4235;
    bGood &= CheckCamp(Test, TEXT("Predicted HP exhaustion is NotFeasible, never a win"),
        LDOpponentPolicy::EstimateCampCost(Exhausted, *Low, *Catalog), ELDCampCostStatus::NotFeasible, 4537, 4, 5, 4235, 0);
    Exhausted.CurrentHpP2 = 4236;
    bGood &= CheckCamp(Test, TEXT("One P2 more survives with one P2"),
        LDOpponentPolicy::EstimateCampCost(Exhausted, *Low, *Catalog), ELDCampCostStatus::Ok, 4537, 4, 5, 4235, 1);
    FLDCampCostInput Zero = Input;
    Zero.Attributes.ADP2 = 0;
    bGood &= Test.TestTrue(TEXT("No effective damage is NotFeasible; no minimum damage is invented"),
        LDOpponentPolicy::EstimateCampCost(Zero, *Low, *Catalog).Status == ELDCampCostStatus::NotFeasible);
    FLDCampCostInput Slow = Input;
    Slow.Attributes.ADP2 = 100;
    bGood &= Test.TestTrue(TEXT("Clearing beyond the engagement budget is NotFeasible"),
        LDOpponentPolicy::EstimateCampCost(Slow, *Low, *Catalog).Status == ELDCampCostStatus::NotFeasible);
    FLDCampCostInput Waiting = Input;
    Waiting.BasicReadyInMs = 1000;
    bGood &= Test.TestEqual(TEXT("A pending basic readiness delays the whole schedule"),
        LDOpponentPolicy::EstimateCampCost(Waiting, *Low, *Catalog).ClearMs, int64(5537));
    FLDCampCostInput Domain = Input;
    Domain.Attributes.AttackSpeed.Numerator = 1;
    Domain.Attributes.AttackSpeed.Denominator = 10;
    bGood &= Test.TestTrue(TEXT("Attack speed outside the catalog domain is EstimateUnavailable"),
        LDOpponentPolicy::EstimateCampCost(Domain, *Low, *Catalog).Status == ELDCampCostStatus::EstimateUnavailable);
    FLDCampCostInput Overfull = Input;
    Overfull.CurrentHpP2 = Overfull.MaxHpP2 + 1;
    bGood &= Test.TestTrue(TEXT("An impossible HP pool is EstimateUnavailable"),
        LDOpponentPolicy::EstimateCampCost(Overfull, *Low, *Catalog).Status == ELDCampCostStatus::EstimateUnavailable);
    return bGood;
}

FLDOpponentOption Option(uint32 Id, ELDOpponentOptionKind Kind, const TCHAR* Definition, uint8 From = 0, uint8 To = 1)
{
    FLDOpponentOption Out;
    Out.OptionId = FGuid(0xA0, 0, 0, Id);
    Out.Kind = Kind;
    Out.DefinitionId = Definition;
    Out.FromRank = From;
    Out.ToRank = To;
    return Out;
}

FLDOpponentSkill Skill(const TCHAR* Definition, uint8 Rank, ELDOpponentSkillSlot Slot)
{
    FLDOpponentSkill Out;
    Out.DefinitionId = Definition;
    Out.Rank = Rank;
    Out.Slot = Slot;
    return Out;
}

bool Pick(FAutomationTestBase& Test, const FLDDemoContentSnapshot& Catalog, const FString& Label,
    TArray<FLDOpponentSkill> Skills, TArray<FLDOpponentOption> Options, uint32 ExpectedOption,
    TOptional<ELDOpponentSkillSlot> ExpectedReplace = {}, const TCHAR* ExpectedReplaced = TEXT(""), uint8 ExpectedRank = 0)
{
    FLDOpponentSelf Self;
    Self.Skills = MoveTemp(Skills);
    FLDOpponentOffer Offer;
    Offer.OfferId = FGuid(0xB0, 0, 0, 1);
    Offer.OfferRevision = 1;
    Offer.Options = MoveTemp(Options);
    Self.Offer = Offer;
    FLDOpponentChoicePick Out;
    FString Error;
    const bool bSelected = LDOpponentPolicy::SelectChoice(Self, Catalog, Out, Error);
    return Test.TestTrue(Label + TEXT(": ") + Error, bSelected && Out.OptionId == FGuid(0xA0, 0, 0, ExpectedOption)
        && Out.bReplace == ExpectedReplace.IsSet()
        && (!ExpectedReplace.IsSet() || (Out.ReplaceSlot == ExpectedReplace.GetValue()
            && Out.ReplacedSkillId == ExpectedReplaced && Out.ReplacedRank == ExpectedRank)));
}

bool RunChoiceOrder(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(Test, Catalog)) { return false; }
    using EKind = ELDOpponentOptionKind;
    using ESlot = ELDOpponentSkillSlot;
    const auto& C = *Catalog;
    bool bGood = Pick(Test, C, TEXT("Empty W: a self-protection active fills the missing role first"),
        {Skill(TEXT("active_arc_bolt"), 1, ESlot::Q)},
        {Option(1, EKind::Attribute, TEXT("attr_might")), Option(2, EKind::LearnActive, TEXT("active_guard_pulse")),
         Option(3, EKind::LearnPassive, TEXT("passive_precision"))}, 2);
    bGood &= Pick(Test, C, TEXT("Two engage actives: explicit replacement of the least invested one"),
        {Skill(TEXT("active_arc_bolt"), 1, ESlot::Q), Skill(TEXT("active_binding_snare"), 2, ESlot::W)},
        {Option(1, EKind::LearnActive, TEXT("active_guard_pulse")), Option(2, EKind::Attribute, TEXT("attr_vitality")),
         Option(3, EKind::UpgradeActive, TEXT("active_arc_bolt"), 1, 2)}, 1, ESlot::Q, TEXT("active_arc_bolt"), 1);
    bGood &= Pick(Test, C, TEXT("Both roles covered: an upgrade beats a replacing active and an attribute"),
        {Skill(TEXT("active_guard_pulse"), 1, ESlot::Q), Skill(TEXT("active_arc_bolt"), 1, ESlot::W)},
        {Option(1, EKind::LearnActive, TEXT("active_binding_snare")), Option(2, EKind::UpgradeActive, TEXT("active_guard_pulse"), 1, 2),
         Option(3, EKind::Attribute, TEXT("attr_might"))}, 2);
    bGood &= Pick(Test, C, TEXT("Maxed actives: a new passive precedes attributes and replacements"),
        {Skill(TEXT("active_guard_pulse"), 3, ESlot::Q), Skill(TEXT("active_arc_bolt"), 3, ESlot::W)},
        {Option(1, EKind::LearnActive, TEXT("active_binding_snare")), Option(2, EKind::LearnPassive, TEXT("passive_battle_rhythm")),
         Option(3, EKind::Attribute, TEXT("attr_reach"))}, 2);
    bGood &= Pick(Test, C, TEXT("An attribute precedes a replacement that adds no missing role"),
        {Skill(TEXT("active_guard_pulse"), 1, ESlot::Q), Skill(TEXT("active_arc_bolt"), 2, ESlot::W)},
        {Option(1, EKind::LearnActive, TEXT("active_binding_snare")), Option(2, EKind::Attribute, TEXT("attr_might"))}, 2);
    bGood &= Pick(Test, C, TEXT("A forced replacement keeps the unique self-protection"),
        {Skill(TEXT("active_guard_pulse"), 1, ESlot::Q), Skill(TEXT("active_arc_bolt"), 1, ESlot::W)},
        {Option(1, EKind::LearnActive, TEXT("active_binding_snare"))}, 1, ESlot::W, TEXT("active_arc_bolt"), 1);
    bGood &= Pick(Test, C, TEXT("No actives: the first engage role in public catalog order"),
        {}, {Option(1, EKind::LearnActive, TEXT("active_binding_snare")), Option(2, EKind::LearnActive, TEXT("active_guard_pulse")),
             Option(3, EKind::LearnActive, TEXT("active_arc_bolt"))}, 3);
    const auto& Attributes = C.Get().Attributes;
    const FString* First = nullptr;
    uint32 FirstOption = 0;
    const TCHAR* Ids[] = {TEXT("attr_resolve"), TEXT("attr_vitality"), TEXT("attr_focus")};
    for (uint32 Index = 0; Index < 3; ++Index)
    {
        const int32 Order = Attributes.IndexOfByPredicate([&](const auto& A) { return A.Id == Ids[Index]; });
        if (Order != INDEX_NONE && (!First || Order < Attributes.IndexOfByPredicate([&](const auto& A) { return A.Id == *First; })))
        {
            First = &Attributes[Order].Id;
            FirstOption = Index + 1;
        }
    }
    bGood &= Test.TestTrue(TEXT("Approved attributes are present"), First != nullptr)
        && Pick(Test, C, TEXT("Attributes tie on stable public order"), {},
            {Option(1, EKind::Attribute, Ids[0]), Option(2, EKind::Attribute, Ids[1]), Option(3, EKind::Attribute, Ids[2])}, FirstOption);
    FLDOpponentSelf Unknown;
    FLDOpponentOffer Offer;
    Offer.Options = {Option(1, EKind::Attribute, TEXT("attr_unknown"))};
    Unknown.Offer = Offer;
    FLDOpponentChoicePick Ignored;
    FString Error;
    bGood &= Test.TestFalse(TEXT("An unknown definition is refused, never defaulted"),
        LDOpponentPolicy::SelectChoice(Unknown, C, Ignored, Error));
    return bGood;
}

bool Plan(FAutomationTestBase& Test, const FLDDemoContentSnapshot& Catalog, const FString& Label, const TCHAR* First,
    const TCHAR* Second, int64 Gold, int32 ExpectedSell, const TCHAR* ExpectedBuy)
{
    FLDOpponentSelf Self;
    Self.Equipment[0] = First;
    Self.Equipment[1] = Second;
    Self.Gold = Gold;
    TOptional<FLDOpponentEquipmentPlan> Out;
    FString Error;
    const bool bPlanned = LDOpponentPolicy::PlanEquipment(Self, Catalog, Out, Error);
    const bool bNone = FCString::Strlen(ExpectedBuy) == 0;
    return Test.TestTrue(Label + TEXT(": ") + Error, bPlanned && (bNone ? !Out.IsSet()
        : Out.IsSet() && Out->SellSlot == ExpectedSell && Out->BuyItemId == ExpectedBuy));
}

bool RunEquipmentPlan(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(Test, Catalog)) { return false; }
    const auto& C = *Catalog;
    bool bGood = Plan(Test, C, TEXT("Starting gold affords nothing; no plan"), TEXT(""), TEXT(""), 150, INDEX_NONE, TEXT(""));
    bGood &= Plan(Test, C, TEXT("330 gold: basic growth before survival"), TEXT(""), TEXT(""), 330, INDEX_NONE, TEXT("item_edge_blade"));
    bGood &= Plan(Test, C, TEXT("360 gold: one item covering basic growth and survival"), TEXT(""), TEXT(""), 360,
        INDEX_NONE, TEXT("item_siphon_emblem"));
    bGood &= Plan(Test, C, TEXT("Second slot: an active source, ties in public catalog order"), TEXT("item_siphon_emblem"), TEXT(""),
        400, INDEX_NONE, TEXT("item_arcane_battery"));
    bGood &= Plan(Test, C, TEXT("Full pair: sell the survival piece for a strictly better active pair"), TEXT("item_edge_blade"),
        TEXT("item_iron_plate"), 199, 1, TEXT("item_arcane_battery"));
    bGood &= Plan(Test, C, TEXT("One gold short of that: the next strict improvement within real sale value"),
        TEXT("item_edge_blade"), TEXT("item_iron_plate"), 198, 0, TEXT("item_echo_maul"));
    bGood &= Plan(Test, C, TEXT("A top pair never wash-trades for a mere id change"), TEXT("item_echo_maul"),
        TEXT("item_arcane_battery"), 100000, INDEX_NONE, TEXT(""));
    FLDEquipmentPairRank Rank;
    FString Error;
    bGood &= Test.TestTrue(TEXT("Two identical passives count once"),
        LDOpponentPolicy::RankEquipmentPair(TEXT("item_siphon_emblem"), TEXT("item_siphon_emblem"), C, Rank, Error)
        && Rank.Filled == 2 && Rank.BasicGrowth == 1 && Rank.Survival == 1 && Rank.ActiveSources == 0 && Rank.DistinctPassives == 1);
    bGood &= Test.TestTrue(TEXT("Two identical actives are two independent sources"),
        LDOpponentPolicy::RankEquipmentPair(TEXT("item_arcane_battery"), TEXT("item_arcane_battery"), C, Rank, Error)
        && Rank.ActiveSources == 2 && Rank.BasicGrowth == 0 && Rank.Survival == 1);
    FLDOpponentSelf Unknown;
    Unknown.Equipment[0] = TEXT("item_unknown");
    TOptional<FLDOpponentEquipmentPlan> Out;
    bGood &= Test.TestFalse(TEXT("An unknown held item is refused"), LDOpponentPolicy::PlanEquipment(Unknown, C, Out, Error));
    return bGood;
}

bool RunCaptureUrgency(FAutomationTestBase& Test)
{
    FLDCaptureUrgencyInput Input;
    Input.NowMs = 100000;
    Input.DueAtMs = 110000;
    Input.PathCm = 3000.;
    Input.MoveSpeedCmPerSec.Numerator = 600;
    Input.MoveSpeedCmPerSec.Denominator = 1;
    Input.ReactionBudgetMs = 2000;
    bool bGood = Test.TestFalse(TEXT("5 s walk + 2 s budget leaves slack against 10 s"), LDOpponentPolicy::IsCaptureDefenseUrgent(Input));
    Input.DueAtMs = 107000;
    bGood &= Test.TestTrue(TEXT("Exactly the walk plus budget is urgent"), LDOpponentPolicy::IsCaptureDefenseUrgent(Input));
    Input.DueAtMs = 110000;
    Input.MoveBlockedUntilMs = 104000;
    bGood &= Test.TestTrue(TEXT("Remaining control time is part of the estimate"), LDOpponentPolicy::IsCaptureDefenseUrgent(Input));
    Input.MoveBlockedUntilMs = 0;
    Input.MoveSpeedCmPerSec.Numerator = 0;
    bGood &= Test.TestTrue(TEXT("Zero speed is urgent without dividing by zero"), LDOpponentPolicy::IsCaptureDefenseUrgent(Input));
    Input.MoveSpeedCmPerSec.Numerator = 600;
    Input.PathCm.Reset();
    bGood &= Test.TestTrue(TEXT("An unanswered path is conservatively urgent"), LDOpponentPolicy::IsCaptureDefenseUrgent(Input));
    Input.bInside = true;
    Input.DueAtMs = 103000;
    bGood &= Test.TestFalse(TEXT("Inside the circle only the reaction budget counts"), LDOpponentPolicy::IsCaptureDefenseUrgent(Input));
    Input.DueAtMs = Input.NowMs;
    bGood &= Test.TestTrue(TEXT("A reached deadline is urgent"), LDOpponentPolicy::IsCaptureDefenseUrgent(Input));
    return bGood;
}

bool RunRetryBounds(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(Test, Catalog)) { return false; }
    const auto& AI = Catalog->Get().Parameters.AI;
    FLDOpponentRetryLedger Ledger;
    FLDOpponentRetryKey Key;
    Key.Category = ELDOpponentFailure::Path;
    Key.POI = TEXT("l1");
    bool bGood = Test.TestTrue(TEXT("A fresh key may be attempted"), Ledger.CanAttempt(Key, 1, 0));
    Ledger.RecordFailure(Key, 1, 0, AI);
    bGood &= Test.TestFalse(TEXT("First failure waits the initial backoff"), Ledger.CanAttempt(Key, 1, 249));
    bGood &= Test.TestTrue(TEXT("Initial backoff elapses"), Ledger.CanAttempt(Key, 1, 250));
    Ledger.RecordFailure(Key, 1, 250, AI);
    bGood &= Test.TestFalse(TEXT("Second failure doubles to the cap"), Ledger.CanAttempt(Key, 1, 749));
    bGood &= Test.TestTrue(TEXT("Capped backoff elapses"), Ledger.CanAttempt(Key, 1, 750));
    Ledger.RecordFailure(Key, 1, 750, AI);
    bGood &= Test.TestTrue(TEXT("The third attempt, counting the first, exhausts the key"),
        Ledger.IsSuppressed(Key, 1) && !Ledger.CanAttempt(Key, 1, MAX_int64));
    bGood &= Test.TestTrue(TEXT("A changed condition fingerprint lifts the suppression"), Ledger.CanAttempt(Key, 2, 750));
    FLDOpponentRetryKey Other = Key;
    Other.POI = TEXT("l2");
    Ledger.RecordFailure(Other, 1, 0, AI);
    Ledger.RecordSuccess(Other);
    bGood &= Test.TestTrue(TEXT("A final success clears the key"), Ledger.CanAttempt(Other, 1, 0));
    for (int32 Index = 0; Index < 4 * FLDOpponentRetryLedger::Capacity; ++Index)
    {
        FLDOpponentRetryKey Many;
        Many.Category = ELDOpponentFailure::Activation;
        Many.Target = FGuid(0xC0, 0, 0, uint32(Index + 1));
        Ledger.RecordFailure(Many, 1, Index, AI);
    }
    bGood &= Test.TestTrue(TEXT("Failure memory stays bounded in a long match"), Ledger.Num() <= FLDOpponentRetryLedger::Capacity);
    return bGood;
}

FLDOpponentPOI POI(const TCHAR* Id, ELDOpponentPOIKind Kind, double X, double Y, const TCHAR* MonsterId = TEXT(""),
    ELDDemoCampTier Tier = ELDDemoCampTier::Low)
{
    FLDOpponentPOI Out;
    Out.Id = Id;
    Out.Kind = Kind;
    Out.SurfaceId = TEXT("ground");
    Out.FootCm = FVector(X, Y, 0);
    Out.MonsterId = MonsterId;
    Out.Tier = Tier;
    return Out;
}

FLDOpponentUnit Unit(uint32 Handle, ELDOpponentUnitKind Kind, double X, double Y, const TCHAR* MonsterId = TEXT(""))
{
    FLDOpponentUnit Out;
    Out.Handle = FGuid(0xD0, 0, 0, Handle);
    Out.Epoch = 1;
    Out.Kind = Kind;
    Out.ContenderId = Kind == ELDOpponentUnitKind::Contender ? FGuid(0xE0, 0, 0, Handle) : FGuid();
    Out.MonsterId = MonsterId;
    Out.FootCm = FVector(X, Y, 0);
    Out.HpP2 = 50000;
    Out.MaxHpP2 = 62000;
    return Out;
}

bool BaseFrame(FAutomationTestBase& Test, const FLDDemoContentSnapshot& Catalog, FLDOpponentDecisionFrame& Out)
{
    Out = {};
    Out.Generation = 7;
    Out.ViewRevision = 1;
    auto& Self = Out.Self;
    Self.ContenderId = FGuid(0xE0, 0, 0, 999);
    Self.Phase = ELDLifePhase::Alive;
    Self.Life = 1;
    if (!ProjectLevel(Test, Catalog, 1, Self.Attributes)) { return false; }
    Self.HpP2 = Self.MaxHpP2 = Self.Attributes.MaxHPP2;
    Self.MpP2 = Self.MaxMpP2 = Self.Attributes.MaxMPP2;
    Self.Gold = 150;
    Self.BuildRevision = 1;
    Out.Public.Phase = ELDMatchPhase::Development;
    Out.Public.NowMs = 10000;
    Out.POIs = {
        POI(TEXT("capture"), ELDOpponentPOIKind::Capture, 0, 3000),
        POI(TEXT("shop"), ELDOpponentPOIKind::Shop, -3000, 0),
        POI(TEXT("l1"), ELDOpponentPOIKind::Camp, 2000, 0, TEXT("monster_scavenger")),
        POI(TEXT("l2"), ELDOpponentPOIKind::Camp, 0, -2500, TEXT("monster_scavenger")),
        POI(TEXT("l3"), ELDOpponentPOIKind::Camp, -2000, -2000, TEXT("monster_scavenger")),
        POI(TEXT("h1"), ELDOpponentPOIKind::Camp, 1500, 1500, TEXT("monster_guardian"), ELDDemoCampTier::High),
        POI(TEXT("h2"), ELDOpponentPOIKind::Camp, -2500, 2500, TEXT("monster_guardian"), ELDDemoCampTier::High),
        POI(TEXT("s00"), ELDOpponentPOIKind::Spawn, 4000, 4000)};
    return true;
}

bool Expect(FAutomationTestBase& Test, const FString& Label, const FLDOpponentDecision& Decision, ELDOpponentMode Mode,
    ELDOpponentBodyKind Body, FName POIId = NAME_None)
{
    return Test.TestTrue(Label + FString::Printf(TEXT(" (mode %u reason %u body %u poi %s)"), uint8(Decision.Mode),
        uint8(Decision.Reason), uint8(Decision.Body.Kind), *Decision.Body.POI.ToString()),
        Decision.Mode == Mode && Decision.Body.Kind == Body && (POIId.IsNone() || Decision.Body.POI == POIId));
}

bool RunPriorities(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(Test, Catalog)) { return false; }
    const auto& C = *Catalog;
    using EMode = ELDOpponentMode;
    using EBody = ELDOpponentBodyKind;
    FLDOpponentDecisionFrame Base;
    if (!BaseFrame(Test, C, Base)) { return false; }
    const auto Decide = [&C](const FLDOpponentDecisionFrame& Frame)
    {
        FLDOpponentPolicyState State;
        return LDOpponentPolicy::Decide(Frame, C, State);
    };

    auto Frame = Base;
    Frame.Public.Phase = ELDMatchPhase::Lobby;
    auto Decision = Decide(Frame);
    bool bGood = Expect(Test, TEXT("Outside a running match nothing is proposed"), Decision, EMode::Stopped, EBody::None)
        && Test.TestTrue(TEXT("No build change outside a running match"), Decision.Build.Kind == ELDOpponentBuildKind::None);

    Frame = Base;
    Frame.Self.Phase = ELDLifePhase::Dead;
    FLDOpponentOffer Offer;
    Offer.OfferId = FGuid(0xB0, 0, 0, 2);
    Offer.OfferRevision = 3;
    Offer.Options = {Option(1, ELDOpponentOptionKind::Attribute, TEXT("attr_might")),
        Option(2, ELDOpponentOptionKind::LearnActive, TEXT("active_arc_bolt"))};
    Frame.Self.Offer = Offer;
    Decision = Decide(Frame);
    bGood &= Expect(Test, TEXT("Dead: no body intent"), Decision, EMode::DeadWaiting, EBody::None)
        && Test.TestTrue(TEXT("Dead: the persistent choice is still confirmed"), Decision.Build.Kind == ELDOpponentBuildKind::Choose
            && Decision.Build.OfferId == Offer.OfferId && Decision.Build.OfferRevision == 3
            && Decision.Build.Choice.OptionId == FGuid(0xA0, 0, 0, 2));
    Frame.bBuildChangeInFlight = true;
    bGood &= Test.TestTrue(TEXT("A choice awaiting its receipt is not proposed again"),
        Decide(Frame).Build.Kind == ELDOpponentBuildKind::None);

    Decision = Decide(Base);
    bGood &= Expect(Test, TEXT("Bare start farms the nearest feasible low camp, not a high camp"), Decision, EMode::Farm,
        EBody::MoveGround, TEXT("l1"));
    auto Shuffled = Base;
    Algo::Reverse(Shuffled.POIs);
    bGood &= Expect(Test, TEXT("Public POI order does not change the decision"), Decide(Shuffled), EMode::Farm, EBody::MoveGround,
        TEXT("l1"));

    Frame = Base;
    Frame.Units = {Unit(1, ELDOpponentUnitKind::CampMember, 2000, 100, TEXT("monster_scavenger"))};
    Decision = Decide(Frame);
    bGood &= Expect(Test, TEXT("A visible member of the chosen camp is attacked"), Decision, EMode::Farm, EBody::AttackUnit)
        && Test.TestTrue(TEXT("Attack uses the observer-local handle"), Decision.Body.Handle == Frame.Units[0].Handle);

    Frame = Base;
    Frame.Units = {Unit(2, ELDOpponentUnitKind::Contender, 300, 0)};
    Decision = Decide(Frame);
    bGood &= Expect(Test, TEXT("A rival inside the approved acquisition radius is engaged first"), Decision, EMode::Engage,
        EBody::AttackUnit);
    Frame.Units[0].FootCm = FVector(1000, 0, 0);
    bGood &= Expect(Test, TEXT("A rival beyond the acquisition radius does not divert farming"), Decide(Frame), EMode::Farm,
        EBody::MoveGround, TEXT("l1"));
    Frame.Units[0].FootCm = FVector(300, 0, 0);
    Frame.Units[0].bProtected = true;
    bGood &= Expect(Test, TEXT("A protected rival is not targeted"), Decide(Frame), EMode::Farm, EBody::MoveGround, TEXT("l1"));

    Frame = Base;
    Frame.Units = {Unit(2, ELDOpponentUnitKind::Contender, 300, 0)};
    Frame.Self.HpP2 = Frame.Self.MaxHpP2 / 5;
    Decision = Decide(Frame);
    const FLDOpponentPOI* Retreat = Frame.POIs.FindByPredicate([&](const FLDOpponentPOI& P) { return P.Id == Decision.Body.POI; });
    bGood &= Expect(Test, TEXT("Below the retreat line with a visible threat the AI disengages"), Decision, EMode::Disengage,
        EBody::MoveGround) && Test.TestTrue(TEXT("The retreat leads away from the threat"), Retreat && Retreat->FootCm.X < 0);
    Frame.Units.Reset();
    Frame.Order.Kind = ELDOpponentOrderKind::MoveGround;
    Frame.Order.Status = ELDOpponentOrderStatus::Active;
    Frame.Order.POI = TEXT("l1");
    Decision = Decide(Frame);
    bGood &= Expect(Test, TEXT("Recovering without threats stops investing and waits on regeneration"), Decision,
        EMode::Disengage, EBody::Stop) && Test.TestTrue(TEXT("Recovery reason"), Decision.Reason == ELDOpponentReason::Recover);

    Frame = Base;
    Frame.Self.Gold = 330;
    bGood &= Expect(Test, TEXT("An affordable strict improvement goes to the public shop"), Decide(Frame), EMode::Shop,
        EBody::ApproachShop, TEXT("shop"));
    Frame.bShopOpen = true;
    Decision = Decide(Frame);
    bGood &= Expect(Test, TEXT("An opened shop holds position"), Decision, EMode::Shop, EBody::None)
        && Test.TestTrue(TEXT("and buys the planned item"), Decision.Build.Kind == ELDOpponentBuildKind::Buy
            && Decision.Build.BuyItemId == TEXT("item_edge_blade"));

    Frame = Base;
    Frame.Self.Gold = 400;
    Frame.Public.Phase = ELDMatchPhase::CaptureOpen;
    Frame.Public.Circle = ELDOpponentCircle::Solo;
    Frame.Public.Occupier = FGuid(0xE0, 0, 0, 5);
    Frame.Public.DueAtMs = Frame.Public.NowMs + 5000;
    Frame.CapturePathCm = 3000.;
    Decision = Decide(Frame);
    bGood &= Expect(Test, TEXT("An urgent public solo deadline outranks shopping"), Decision, EMode::DefendOrHold,
        EBody::MoveGround, TEXT("capture"))
        && Test.TestTrue(TEXT("Defence reason"), Decision.Reason == ELDOpponentReason::PublicCaptureUrgent);
    Frame.Public.Circle = ELDOpponentCircle::Empty;
    Frame.Self.Gold = 150;
    bGood &= Expect(Test, TEXT("An open, empty circle is contested"), Decide(Frame), EMode::Contest, EBody::MoveGround,
        TEXT("capture"));
    Frame.Self.FootCm = FVector(0, 2500, 0);
    Frame.Units = {Unit(3, ELDOpponentUnitKind::Contender, 0, 3500), Unit(4, ELDOpponentUnitKind::Contender, 5000, 5000)};
    Decision = Decide(Frame);
    bGood &= Expect(Test, TEXT("Inside the circle only rivals inside it are fought"), Decision, EMode::Contest, EBody::AttackUnit)
        && Test.TestTrue(TEXT("The in-circle rival"), Decision.Body.Handle == Frame.Units[0].Handle);
    Frame.Public.Circle = ELDOpponentCircle::Solo;
    Frame.Public.Occupier = Frame.Self.ContenderId;
    // Just outside the circle edge yet inside the acquisition radius of the holder.
    Frame.Units = {Unit(4, ELDOpponentUnitKind::Contender, 0, 2350)};
    Decision = Decide(Frame);
    bGood &= Expect(Test, TEXT("A solo holder does not chase a rival outside the circle"), Decision, EMode::DefendOrHold,
        EBody::None) && Test.TestTrue(TEXT("Hold reason"), Decision.Reason == ELDOpponentReason::HoldCapture);

    Frame = Base;
    Frame.Order.Kind = ELDOpponentOrderKind::MoveGround;
    Frame.Order.Status = ELDOpponentOrderStatus::Active;
    Frame.Order.POI = TEXT("l1");
    bGood &= Expect(Test, TEXT("A still-valid order is kept, not re-sent"), Decide(Frame), EMode::Farm, EBody::Keep, TEXT("l1"));
    Frame.Order.Kind = ELDOpponentOrderKind::Action;
    bGood &= Expect(Test, TEXT("A live accepted action is never cancelled by a new body intent"), Decide(Frame), EMode::Farm,
        EBody::Keep);
    Frame.Order.Kind = ELDOpponentOrderKind::AttackUnit;
    Frame.Order.Handle = FGuid(0xD0, 0, 0, 77);
    Frame.Order.Epoch = 1;
    Frame.Order.POI = NAME_None;
    bGood &= Expect(Test, TEXT("A target missing from the authorized frame is never re-attacked"), Decide(Frame), EMode::Farm,
        EBody::MoveGround, TEXT("l1"));

    Frame = Base;
    Frame.Self.Skills = {Skill(TEXT("active_arc_bolt"), 1, ELDOpponentSkillSlot::Q)};
    FLDOpponentSource Bolt;
    Bolt.Slot = ELDOpponentSourceSlot::Q;
    Bolt.DefinitionId = TEXT("active_arc_bolt");
    Bolt.bReady = true;
    Frame.Self.Sources = {Bolt};
    Frame.Units = {Unit(2, ELDOpponentUnitKind::Contender, 300, 0)};
    Decision = Decide(Frame);
    bGood &= Expect(Test, TEXT("A ready damage active is released at a visible target in range"), Decision, EMode::Engage,
        EBody::Activate) && Test.TestTrue(TEXT("Direction toward the visible target"),
            Decision.Body.Source == ELDOpponentSourceSlot::Q && Decision.Body.TargetMode == ELDDemoTargetMode::DirectionProjectile
            && Decision.Body.Direction.Equals(FVector(1, 0, 0), 1.e-6));
    Frame.Self.Sources[0].bReady = false;
    bGood &= Expect(Test, TEXT("Without authority readiness the basic attack order continues"), Decide(Frame), EMode::Engage,
        EBody::AttackUnit);

    FLDOpponentPolicyState State;
    Frame = Base;
    Frame.Units = {Unit(2, ELDOpponentUnitKind::Contender, 300, 0)};
    LDOpponentPolicy::Decide(Frame, C, State);
    Frame.Public.NowMs += Catalog->Get().Parameters.AI.EngagementBudgetMs + 1;
    bGood &= Expect(Test, TEXT("An engagement past its budget is abandoned, not reset by new frames"),
        LDOpponentPolicy::Decide(Frame, C, State), EMode::Farm, EBody::MoveGround, TEXT("l1"));
    Frame.Generation = 8;
    bGood &= Expect(Test, TEXT("A new match starts from clean strategy state"), LDOpponentPolicy::Decide(Frame, C, State),
        EMode::Engage, EBody::AttackUnit);
    return bGood;
}

FLDDemoWorldAnchor Anchor(const TCHAR* Id, ELDDemoWorldAnchorKind Kind, int64 X, int64 Y, int32 Members = 0)
{
    FLDDemoWorldAnchor Out;
    Out.Id = Id;
    Out.Kind = Kind;
    Out.SurfaceId = TEXT("ground");
    Out.PositionCm.X.Numerator = X;
    Out.PositionCm.Y.Numerator = Y;
    for (int32 Index = 0; Index < Members; ++Index)
    {
        FLDDemoWorldMemberHome Home;
        Home.Id = FString::Printf(TEXT("%s_m%d"), Id, Index);
        Home.PositionCm.X.Numerator = X + 100 * Index;
        Home.PositionCm.Y.Numerator = Y;
        Out.Members.Add(MoveTemp(Home));
    }
    return Out;
}

FLDObservedUnitView ObservedContender(uint32 Handle, double X, uint8 Status)
{
    FLDObservedUnitView Out;
    Out.Handle = FGuid(0xF0, 0, 0, Handle);
    Out.ObservationEpoch = 1;
    Out.StateRevision = 1;
    Out.AuthorizedFieldMask = LDObservation::ContenderFields;
    Out.Kind = ELDObservedUnitKind::Contender;
    Out.FeetCm = FVector(X, 0, 0);
    Out.ContenderId = FGuid(0xF1, 0, 0, Handle);
    Out.Level = 3;
    Out.Health.CurrentP2 = 50000;
    Out.Health.MaxP2 = 62000;
    Out.Mana.CurrentP2 = 10000;
    Out.Mana.MaxP2 = 28000;
    if (Status)
    {
        FLDObservedStatus Protection;
        Protection.StatusId = Status;
        Protection.ExpiresAtServerMs = 5000;
        Out.Statuses.Add(Protection);
    }
    return Out;
}

FLDObservedUnitView ObservedMember(uint32 Handle, uint8 Creature, bool bReturning)
{
    FLDObservedUnitView Out;
    Out.Handle = FGuid(0xF0, 0, 0, Handle);
    Out.ObservationEpoch = 2;
    Out.StateRevision = 1;
    Out.AuthorizedFieldMask = LDObservation::CampFields;
    Out.Kind = ELDObservedUnitKind::CampMember;
    Out.FeetCm = FVector(2000, 0, 0);
    Out.CreatureId = Creature;
    Out.Health.CurrentP2 = Out.Health.MaxP2 = 42000;
    if (bReturning)
    {
        FLDObservedStatus Returning;
        Returning.StatusId = uint8(ELDDemoStatusId::CampReturning);
        Out.Statuses.Add(Returning);
    }
    return Out;
}

bool RunProjections(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(Test, Catalog)) { return false; }
    const auto& C = *Catalog;
    FString Error;

    FLDDemoWorldGeometry World;
    World.Anchors = {Anchor(TEXT("capture"), ELDDemoWorldAnchorKind::Capture, 0, 3000),
        Anchor(TEXT("shop"), ELDDemoWorldAnchorKind::Shop, -3000, 0), Anchor(TEXT("s00"), ELDDemoWorldAnchorKind::Spawn, 10, 20),
        Anchor(TEXT("h1"), ELDDemoWorldAnchorKind::Camp, 1500, 1500, 1), Anchor(TEXT("h2"), ELDDemoWorldAnchorKind::Camp, -2500, 2500, 1),
        Anchor(TEXT("l1"), ELDDemoWorldAnchorKind::Camp, 2000, 0, 2), Anchor(TEXT("l2"), ELDDemoWorldAnchorKind::Camp, 0, -2500, 2),
        Anchor(TEXT("l3"), ELDDemoWorldAnchorKind::Camp, -2000, -2000, 2)};
    TArray<FLDOpponentPOI> POIs;
    bool bGood = Test.TestTrue(TEXT("Approved anchors and the camp roster form the public places: ") + Error,
        LDOpponentFrame::ReadPublicPOIs(World, C, POIs, Error) && POIs.Num() == 8);
    const auto* High = POIs.FindByPredicate([](const FLDOpponentPOI& P) { return P.Id == TEXT("h1"); });
    const auto* Low = POIs.FindByPredicate([](const FLDOpponentPOI& P) { return P.Id == TEXT("l2"); });
    const auto* Spawn = POIs.FindByPredicate([](const FLDOpponentPOI& P) { return P.Id == TEXT("s00"); });
    bGood &= Test.TestTrue(TEXT("Camp tier and creature come from the public catalog"),
        High && High->Kind == ELDOpponentPOIKind::Camp && High->Tier == ELDDemoCampTier::High && High->MonsterId == TEXT("monster_guardian")
        && Low && Low->Tier == ELDDemoCampTier::Low && Low->MonsterId == TEXT("monster_scavenger")
        && Spawn && Spawn->Kind == ELDOpponentPOIKind::Spawn && Spawn->FootCm.Equals(FVector(10, 20, 0)) && Spawn->SurfaceId == TEXT("ground"));
    auto Unplaced = World;
    Unplaced.Anchors.Add(Anchor(TEXT("x9"), ELDDemoWorldAnchorKind::Camp, 0, 0, 2));
    bGood &= Test.TestFalse(TEXT("A camp anchor outside the approved roster is refused"), LDOpponentFrame::ReadPublicPOIs(Unplaced, C, POIs, Error));
    auto MissingCamp = World;
    MissingCamp.Anchors.RemoveAt(MissingCamp.Anchors.Num() - 1);
    bGood &= Test.TestFalse(TEXT("A world missing an approved camp is refused"), LDOpponentFrame::ReadPublicPOIs(MissingCamp, C, POIs, Error));
    auto NoShop = World;
    NoShop.Anchors.RemoveAt(1);
    bGood &= Test.TestFalse(TEXT("A world without its single shop is refused"), LDOpponentFrame::ReadPublicPOIs(NoShop, C, POIs, Error));
    auto Unknown = World;
    Unknown.Anchors.Add(Anchor(TEXT("s99"), ELDDemoWorldAnchorKind::Spawn, 0, 0));
    bGood &= Test.TestFalse(TEXT("An unapproved public anchor is refused"), LDOpponentFrame::ReadPublicPOIs(Unknown, C, POIs, Error));
    auto Duplicate = World;
    Duplicate.Anchors.Add(Anchor(TEXT("s00"), ELDDemoWorldAnchorKind::Spawn, 0, 0));
    bGood &= Test.TestFalse(TEXT("Duplicate public ids are refused"), LDOpponentFrame::ReadPublicPOIs(Duplicate, C, POIs, Error));

    FLDWorldView View;
    View.Schema = LDObservation::Schema;
    View.MatchGeneration = 7;
    View.Generation = 1;
    View.Revision = 1;
    View.WorldMapId = TEXT("demo_world");
    View.WorldLayoutRevision = 1;
    View.WorldGeometryRevision = 1;
    View.SampledAtServerSec = 1.;
    View.Footprint.SourceStatus = ELDObservationSource::ActiveBody;
    View.Footprint.RadiusCm = 1400;
    View.Footprint.CurrentBits.SetNumZeroed(LDObservation::CoverageBytes);
    View.Units = {ObservedContender(1, 100, uint8(ELDDemoStatusId::RespawnProtection)),
        ObservedMember(2, uint8(ELDDemoMonsterId::MonsterGuardian), true)};
    TArray<FLDOpponentUnit> Units;
    bGood &= Test.TestTrue(TEXT("A valid authorized frame maps: ") + Error,
        LDOpponentFrame::ReadObservedUnits(View, 7, C, Units, Error) && Units.Num() == 2);
    if (Units.Num() == 2)
    {
        bGood &= Test.TestTrue(TEXT("Contender identity, health and protection"),
            Units[0].Kind == ELDOpponentUnitKind::Contender && Units[0].ContenderId == View.Units[0].ContenderId
            && Units[0].Handle == View.Units[0].Handle && Units[0].HpP2.Get(0) == 50000 && Units[0].bProtected && !Units[0].bReturning);
        bGood &= Test.TestTrue(TEXT("Camp member creature and the public Returning cue"),
            Units[1].Kind == ELDOpponentUnitKind::CampMember && Units[1].MonsterId == TEXT("monster_guardian")
            && Units[1].Epoch == 2 && Units[1].bReturning && !Units[1].bProtected && Units[1].MaxHpP2.Get(0) == 42000);
    }
    bGood &= Test.TestFalse(TEXT("A frame of another match is refused"), LDOpponentFrame::ReadObservedUnits(View, 8, C, Units, Error));
    auto Malformed = View;
    Malformed.Schema = 0;
    bGood &= Test.TestFalse(TEXT("A malformed frame is refused, never read as empty"),
        LDOpponentFrame::ReadObservedUnits(Malformed, 7, C, Units, Error) || Units.Num() != 0);

    FLDProgressionView Progression;
    Progression.bValid = true;
    Progression.Generation = 7;
    Progression.Level = 3;
    Progression.bCanChoose = true;
    Progression.BuildRevision = 9;
    Progression.ConsumedThrough = 2;
    FLDLearnedSkillView Bolt;
    Bolt.DefinitionId = TEXT("active_arc_bolt");
    Bolt.bActive = true;
    Bolt.Rank = 1;
    Bolt.Slot = 0;
    FLDLearnedSkillView Rhythm;
    Rhythm.DefinitionId = TEXT("passive_battle_rhythm");
    Rhythm.Rank = 1;
    Progression.Skills = {Bolt, Rhythm};
    FLDAttributeCardView Might;
    Might.DefinitionId = TEXT("attr_might");
    Might.Count = 1;
    Progression.AttributeCards = {Might};
    Progression.bHasOffer = true;
    Progression.Offer.OfferId = FGuid(0xF2, 0, 0, 1);
    Progression.Offer.OfferRevision = 4;
    FLDChoiceOption Guard;
    Guard.OptionId = FGuid(0xF2, 0, 0, 2);
    Guard.Kind = ELDChoiceOptionKind::LearnActive;
    Guard.DefinitionId = TEXT("active_guard_pulse");
    Guard.ToRank = 1;
    Progression.Offer.Options = {Guard};
    FLDEquipmentView Equipment;
    Equipment.bValid = true;
    Equipment.Generation = 7;
    Equipment.Gold = 42;
    FLDEquipmentSlotView Blade;
    Blade.SlotIndex = 0;
    Blade.bOccupied = true;
    Blade.DefinitionId = TEXT("item_edge_blade");
    FLDEquipmentSlotView Empty;
    Empty.SlotIndex = 1;
    Equipment.Slots = {Blade, Empty};
    FLDOpponentSelf Self;
    const auto Expected = LDAttributeProjection::Project(FLDAttributeBuildValues(3,
        {FLDLearnedPassiveValue(TEXT("passive_battle_rhythm"), 1)}, {FLDAttributeCardValue(TEXT("attr_might"), 1)},
        {TEXT("item_edge_blade")}), C);
    bGood &= Test.TestTrue(TEXT("Own build maps through the shared attribute projection: ") + Error,
        Expected.IsSuccess() && LDOpponentFrame::ReadOwnBuild(Progression, Equipment, C, Self, Error)
        && Self.Attributes.ADP2 == Expected.Value->Final.ADP2 && Self.Attributes.ArmorP2 == Expected.Value->Final.ArmorP2
        && Self.Attributes.AttackSpeed.Numerator * Expected.Value->Final.AttackSpeed.Denominator
            == Expected.Value->Final.AttackSpeed.Numerator * Self.Attributes.AttackSpeed.Denominator
        && Self.Gold == 42 && Self.Equipment[0] == TEXT("item_edge_blade") && Self.Equipment[1].IsEmpty()
        && Self.BuildRevision == 9 && Self.ChoicesConsumed == 2 && Self.Skills.Num() == 2
        && Self.Skills[0].Slot == ELDOpponentSkillSlot::Q && Self.Skills[1].Slot == ELDOpponentSkillSlot::Passive
        && Self.Offer.IsSet() && Self.Offer->Options.Num() == 1 && Self.Offer->Options[0].Kind == ELDOpponentOptionKind::LearnActive);
    Progression.bCanChoose = false;
    bGood &= Test.TestTrue(TEXT("An offer that cannot be confirmed now is not proposed"),
        LDOpponentFrame::ReadOwnBuild(Progression, Equipment, C, Self, Error) && !Self.Offer.IsSet());
    auto OneSlot = Equipment;
    OneSlot.Slots.SetNum(1);
    bGood &= Test.TestFalse(TEXT("Equipment must expose both ordered slots"), LDOpponentFrame::ReadOwnBuild(Progression, OneSlot, C, Self, Error));
    auto OtherMatch = Equipment;
    OtherMatch.Generation = 8;
    bGood &= Test.TestFalse(TEXT("Views from different publications are refused"),
        LDOpponentFrame::ReadOwnBuild(Progression, OtherMatch, C, Self, Error));

    FLDCombatOwnerView Combat;
    Combat.bHasBody = true;
    Combat.HPP2 = 30000;
    Combat.MaxHPP2 = 62000;
    Combat.MPP2 = 10000;
    Combat.MaxMPP2 = 28000;
    Combat.NextAttackAtMs = 12345;
    Combat.bRooted = true;
    FLDCombatStatusView Root;
    Root.StatusId = uint8(ELDDemoStatusId::Root);
    Root.ExpiresAtMs = 10700;
    Combat.Statuses = {Root};
    const auto Entry = [](ELDCombatEntry Slot, ELDCombatSourceKind Kind, const TCHAR* Id, bool bActive, int64 ReadyAt, ELDCombatReject Why)
    {
        FLDCombatEntryView Out;
        Out.Entry = Slot;
        Out.SourceKind = Kind;
        Out.DefinitionId = Id;
        Out.bHasActive = bActive;
        Out.ReadyAtMs = ReadyAt;
        Out.Unavailable = Why;
        return Out;
    };
    Combat.Entries = {Entry(ELDCombatEntry::Q, ELDCombatSourceKind::Learned, TEXT("active_arc_bolt"), true, 9000, ELDCombatReject::None),
        Entry(ELDCombatEntry::W, ELDCombatSourceKind::Learned, TEXT("active_guard_pulse"), true, 15000, ELDCombatReject::Cooldown),
        Entry(ELDCombatEntry::Item1, ELDCombatSourceKind::Gear, TEXT("item_iron_plate"), false, 0, ELDCombatReject::NoActiveEffect),
        Entry(ELDCombatEntry::Item2, ELDCombatSourceKind::Gear, TEXT("item_phase_anchor"), true, 0, ELDCombatReject::ControlBlocked)};
    bGood &= Test.TestTrue(TEXT("Own combat view maps resources, readiness and Root: ") + Error,
        LDOpponentFrame::ReadOwnCombat(Combat, 10000, Self, Error) && Self.HpP2 == 30000 && Self.MaxMpP2 == 28000
        && Self.BasicReadyAtMs == 12345 && Self.MoveBlockedUntilMs == 10700 && Self.Sources.Num() == 3
        && Self.Sources[0].Slot == ELDOpponentSourceSlot::Q && Self.Sources[0].bReady
        && Self.Sources[1].Slot == ELDOpponentSourceSlot::W && !Self.Sources[1].bReady
        && Self.Sources[2].Slot == ELDOpponentSourceSlot::Gear1 && !Self.Sources[2].bReady);
    Combat.Statuses.Reset();
    bGood &= Test.TestTrue(TEXT("Rooted without a timed status still blocks movement now"),
        LDOpponentFrame::ReadOwnCombat(Combat, 10000, Self, Error) && Self.MoveBlockedUntilMs > 10000);
    Combat.bHasBody = false;
    bGood &= Test.TestFalse(TEXT("No living body is an explicit failure"), LDOpponentFrame::ReadOwnCombat(Combat, 10000, Self, Error));

    bGood &= Test.TestTrue(TEXT("Order receipts map to the policy's order states"),
        LDOpponentFrame::OrderStatus(ELDOrderStatus::Queued) == ELDOpponentOrderStatus::Pending
        && LDOpponentFrame::OrderStatus(ELDOrderStatus::Activated) == ELDOpponentOrderStatus::Active
        && LDOpponentFrame::OrderStatus(ELDOrderStatus::Progress) == ELDOpponentOrderStatus::Active
        && LDOpponentFrame::OrderStatus(ELDOrderStatus::Completed) == ELDOpponentOrderStatus::Completed
        && LDOpponentFrame::OrderStatus(ELDOrderStatus::Cancelled) == ELDOpponentOrderStatus::Cancelled
        && LDOpponentFrame::OrderStatus(ELDOrderStatus::Rejected) == ELDOpponentOrderStatus::Failed);
    return bGood;
}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentCampCostTest, "Lockdown.Rules.DemoOpponentPolicy.CampCost",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentCampCostTest::RunTest(const FString& Parameters)
{
    return LDOpponentPolicyTests::RunCampCost(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentChoiceOrderTest, "Lockdown.Rules.DemoOpponentPolicy.ChoiceOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentChoiceOrderTest::RunTest(const FString& Parameters)
{
    return LDOpponentPolicyTests::RunChoiceOrder(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentEquipmentPlanTest, "Lockdown.Rules.DemoOpponentPolicy.EquipmentPlan",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentEquipmentPlanTest::RunTest(const FString& Parameters)
{
    return LDOpponentPolicyTests::RunEquipmentPlan(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentCaptureUrgencyTest, "Lockdown.Rules.DemoOpponentPolicy.CaptureUrgency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentCaptureUrgencyTest::RunTest(const FString& Parameters)
{
    return LDOpponentPolicyTests::RunCaptureUrgency(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentRetryBoundsTest, "Lockdown.Rules.DemoOpponentPolicy.RetryBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentRetryBoundsTest::RunTest(const FString& Parameters)
{
    return LDOpponentPolicyTests::RunRetryBounds(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentPrioritiesTest, "Lockdown.Rules.DemoOpponentPolicy.Priorities",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentPrioritiesTest::RunTest(const FString& Parameters)
{
    return LDOpponentPolicyTests::RunPriorities(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentProjectionsTest, "Lockdown.Rules.DemoOpponentPolicy.Projections",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentProjectionsTest::RunTest(const FString& Parameters)
{
    return LDOpponentPolicyTests::RunProjections(*this);
}

#endif
