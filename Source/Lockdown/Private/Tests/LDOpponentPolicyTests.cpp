#include "AI/LDOpponentPolicy.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/LDOpponentFrame.h"
#include "AI/LDOpponentRequests.h"
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
#include "World/LDWorldQueryTypes.h"

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
    bGood &= CheckCamp(Test, TEXT("L1 low camp: 4 hits, first-hit retaliation, 4 x 8.47"),
        LDOpponentPolicy::EstimateCampCost(Input, *Low, *Catalog), ELDCampCostStatus::Ok, 3586, 4, 4, 3388, 58612);
    bGood &= CheckCamp(Test, TEXT("L1 high camp: 8 hits, 5 x 40.67"),
        LDOpponentPolicy::EstimateCampCost(Input, *High, *Catalog), ELDCampCostStatus::Ok, 8034, 8, 5, 20335, 41665);
    FLDCampCostInput Ten = Input;
    Ten.CurrentHpP2 = Ten.MaxHpP2 = L10.MaxHPP2;
    Ten.Attributes = L10;
    bGood &= CheckCamp(Test, TEXT("L10 low camp uses projected attack speed and windup"),
        LDOpponentPolicy::EstimateCampCost(Ten, *Low, *Catalog), ELDCampCostStatus::Ok, 1154, 2, 1, 735, L10.MaxHPP2 - 735);
    bGood &= CheckCamp(Test, TEXT("L10 high camp"),
        LDOpponentPolicy::EstimateCampCost(Ten, *High, *Catalog), ELDCampCostStatus::Ok, 4922, 6, 3, 10587, L10.MaxHPP2 - 10587);

    bGood &= Test.TestEqual(TEXT("A retaliation landing exactly at the death time counts"),
        LDOpponentPolicy::RetaliationHits(0, 350, 1250, 350), int64(1));
    bGood &= Test.TestEqual(TEXT("One millisecond earlier it does not land"), LDOpponentPolicy::RetaliationHits(0, 349, 1250, 350), int64(0));
    bGood &= Test.TestEqual(TEXT("Retaliation cadence"), LDOpponentPolicy::RetaliationHits(0, 1600, 1250, 350), int64(2));
    bGood &= Test.TestEqual(TEXT("A member dead before retaliation starts never strikes"),
        LDOpponentPolicy::RetaliationHits(100, 99, 1250, 350), int64(0));

    FLDCampCostInput Exhausted = Input;
    Exhausted.CurrentHpP2 = 3388;
    bGood &= CheckCamp(Test, TEXT("Predicted HP exhaustion is NotFeasible, never a win"),
        LDOpponentPolicy::EstimateCampCost(Exhausted, *Low, *Catalog), ELDCampCostStatus::NotFeasible, 3586, 4, 4, 3388, 0);
    Exhausted.CurrentHpP2 = 3389;
    bGood &= CheckCamp(Test, TEXT("One P2 more survives with one P2"),
        LDOpponentPolicy::EstimateCampCost(Exhausted, *Low, *Catalog), ELDCampCostStatus::Ok, 3586, 4, 4, 3388, 1);
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
        LDOpponentPolicy::EstimateCampCost(Waiting, *Low, *Catalog).ClearMs, int64(4586));
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
        {Skill(TEXT("active_snark_bolt"), 1, ESlot::Q)},
        {Option(1, EKind::Attribute, TEXT("attr_protein")), Option(2, EKind::LearnActive, TEXT("active_not_mad")),
         Option(3, EKind::LearnPassive, TEXT("passive_iced_americano"))}, 2);
    bGood &= Pick(Test, C, TEXT("Two engage actives leave E free for self-protection"),
        {Skill(TEXT("active_snark_bolt"), 1, ESlot::Q), Skill(TEXT("active_pants_down"), 2, ESlot::W)},
        {Option(1, EKind::LearnActive, TEXT("active_not_mad")), Option(2, EKind::Attribute, TEXT("attr_canteen")),
         Option(3, EKind::UpgradeActive, TEXT("active_snark_bolt"), 1, 2)}, 1);
    bGood &= Pick(Test, C, TEXT("Both roles covered: an upgrade beats a replacing active and an attribute"),
        {Skill(TEXT("active_not_mad"), 1, ESlot::Q), Skill(TEXT("active_snark_bolt"), 1, ESlot::W)},
        {Option(1, EKind::LearnActive, TEXT("active_pants_down")), Option(2, EKind::UpgradeActive, TEXT("active_not_mad"), 1, 2),
         Option(3, EKind::Attribute, TEXT("attr_protein"))}, 2);
    bGood &= Pick(Test, C, TEXT("Maxed actives: a new passive precedes attributes and replacements"),
        {Skill(TEXT("active_not_mad"), 2, ESlot::Q), Skill(TEXT("active_snark_bolt"), 2, ESlot::W),
         Skill(TEXT("active_pants_down"), 2, ESlot::E)},
        {Option(1, EKind::LearnActive, TEXT("active_pants_down")), Option(2, EKind::LearnPassive, TEXT("passive_pressed")),
         Option(3, EKind::Attribute, TEXT("attr_hand_out"))}, 2);
    bGood &= Pick(Test, C, TEXT("An attribute precedes a replacement that adds no missing role"),
        {Skill(TEXT("active_not_mad"), 1, ESlot::Q), Skill(TEXT("active_snark_bolt"), 2, ESlot::W),
         Skill(TEXT("active_elbow"), 1, ESlot::E)},
        {Option(1, EKind::LearnActive, TEXT("active_pants_down")), Option(2, EKind::Attribute, TEXT("attr_protein"))}, 2);
    bGood &= Pick(Test, C, TEXT("A forced replacement keeps the unique self-protection"),
        {Skill(TEXT("active_not_mad"), 1, ESlot::Q), Skill(TEXT("active_snark_bolt"), 1, ESlot::W),
         Skill(TEXT("active_elbow"), 1, ESlot::E)},
        {Option(1, EKind::LearnActive, TEXT("active_pants_down"))}, 1, ESlot::W, TEXT("active_snark_bolt"), 1);
    bGood &= Pick(Test, C, TEXT("No actives: every role is missing, so the first in public (id) catalog order"),
        {}, {Option(1, EKind::LearnActive, TEXT("active_pants_down")), Option(2, EKind::LearnActive, TEXT("active_not_mad")),
             Option(3, EKind::LearnActive, TEXT("active_snark_bolt"))}, 2);
    bGood &= Pick(Test, C, TEXT("A 112 opening offer (two new actives and a passive): an active fills the empty Q"),
        {}, {Option(1, EKind::LearnActive, TEXT("active_not_mad")), Option(2, EKind::LearnPassive, TEXT("passive_thick_face")),
             Option(3, EKind::LearnActive, TEXT("active_pants_down"))}, 1);
    const auto& Attributes = C.Get().Attributes;
    const FString* First = nullptr;
    uint32 FirstOption = 0;
    const TCHAR* Ids[] = {TEXT("attr_big_talk"), TEXT("attr_canteen"), TEXT("attr_all_nighter")};
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
    // 112 §6: an offer whose best card is only an attribute spends a free reroll when one is held; a rerolled offer is chosen.
    FLDOpponentSelf Rolling;
    FLDOpponentOffer Plain;
    Plain.OfferId = FGuid(0xB0, 0, 0, 2);
    Plain.Options = {Option(1, EKind::Attribute, TEXT("attr_protein")), Option(2, EKind::Attribute, TEXT("attr_all_nighter"))};
    Rolling.Offer = Plain;
    Rolling.RerollTokens = 1;
    FLDOpponentChoicePick Rolled;
    FString RollError;
    bGood &= Test.TestTrue(TEXT("Attributes only and a token: reroll"),
        LDOpponentPolicy::SelectChoice(Rolling, C, Rolled, RollError) && Rolled.bReroll && !Rolled.OptionId.IsValid());
    Rolling.Offer->bRerolled = true;
    bGood &= Test.TestTrue(TEXT("A rerolled offer is chosen from"),
        LDOpponentPolicy::SelectChoice(Rolling, C, Rolled, RollError) && !Rolled.bReroll && Rolled.OptionId.IsValid());
    Rolling.Offer->bRerolled = false;
    Rolling.RerollTokens = 0;
    bGood &= Test.TestTrue(TEXT("Without a token the attribute is taken"),
        LDOpponentPolicy::SelectChoice(Rolling, C, Rolled, RollError) && !Rolled.bReroll);
    Rolling.RerollTokens = 1;
    Rolling.Offer->Options.Add(Option(3, EKind::LearnPassive, TEXT("passive_iced_americano")));
    bGood &= Test.TestTrue(TEXT("A good card is taken even with a token"),
        LDOpponentPolicy::SelectChoice(Rolling, C, Rolled, RollError) && !Rolled.bReroll && Rolled.OptionId == FGuid(0xA0, 0, 0, 3));
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
    // Trade order only; the one upgrade of the match is covered on its own below.
    Self.bUpgradeUsed = true;
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
    bool bGood = Test.TestEqual(TEXT("Approved starting gold is 320"), C.Get().Parameters.StartingGold, int64(320));
    bGood &= Plan(Test, C, TEXT("Starting gold 320 exactly affords the basic-growth edge blade"), TEXT(""), TEXT(""), 320,
        INDEX_NONE, TEXT("item_edge_blade"));
    bGood &= Plan(Test, C, TEXT("319 gold: only the survival iron plate is affordable"), TEXT(""), TEXT(""), 319, INDEX_NONE,
        TEXT("item_iron_plate"));
    bGood &= Plan(Test, C, TEXT("One gold below the cheapest item affords nothing; no plan"), TEXT(""), TEXT(""), 309, INDEX_NONE,
        TEXT(""));
    bGood &= Plan(Test, C, TEXT("330 gold: basic growth before survival"), TEXT(""), TEXT(""), 330, INDEX_NONE, TEXT("item_edge_blade"));
    bGood &= Plan(Test, C, TEXT("360 gold: one item covering basic growth and survival"), TEXT(""), TEXT(""), 360,
        INDEX_NONE, TEXT("item_siphon_emblem"));
    bGood &= Plan(Test, C, TEXT("Second slot: a distinct passive, ties in public catalog order"), TEXT("item_siphon_emblem"), TEXT(""),
        400, INDEX_NONE, TEXT("item_echo_maul"));
    bGood &= Plan(Test, C, TEXT("Full pair: sell the first piece whose real sale value covers a strictly better pair"),
        TEXT("item_edge_blade"), TEXT("item_echo_maul"), 102, 0, TEXT("item_iron_plate"));
    bGood &= Plan(Test, C, TEXT("One gold short of that: the next strict improvement within real sale value"),
        TEXT("item_edge_blade"), TEXT("item_echo_maul"), 101, 1, TEXT("item_iron_plate"));
    bGood &= Plan(Test, C, TEXT("A pair with a passiveless piece trades it for a second distinct passive"), TEXT("item_echo_maul"),
        TEXT("item_arcane_battery"), 100000, 1, TEXT("item_iron_plate"));
    bGood &= Plan(Test, C, TEXT("A top pair never wash-trades for a mere id change"), TEXT("item_edge_blade"),
        TEXT("item_iron_plate"), 100000, INDEX_NONE, TEXT(""));
    FLDEquipmentPairRank Rank;
    FString Error;
    bGood &= Test.TestTrue(TEXT("Two identical passives count once"),
        LDOpponentPolicy::RankEquipmentPair(TEXT("item_siphon_emblem"), TEXT("item_siphon_emblem"), C, Rank, Error)
        && Rank.Filled == 2 && Rank.BasicGrowth == 1 && Rank.Survival == 1 && Rank.DistinctPassives == 1);
    bGood &= Test.TestTrue(TEXT("A stats-only item adds no passive family"),
        LDOpponentPolicy::RankEquipmentPair(TEXT("item_arcane_battery"), TEXT("item_arcane_battery"), C, Rank, Error)
        && Rank.Filled == 2 && Rank.BasicGrowth == 0 && Rank.Survival == 1 && Rank.DistinctPassives == 0);
    // #112 C1: the reworked item passives classify on their own, without the items' attribute stats.
    const auto PassiveOnly = [&C](const TCHAR* Id)
    {
        const FLDDemoEquipmentDefinition* Item = C.Get().Equipment.FindByPredicate(
            [Id](const FLDDemoEquipmentDefinition& Row) { return Row.Id == Id; });
        FLDDemoEquipmentDefinition Copy = Item ? *Item : FLDDemoEquipmentDefinition();
        Copy.Stats.Reset();
        return LDOpponentPolicy::EquipmentUsage(Copy);
    };
    const FLDOpponentUsage Plate = PassiveOnly(TEXT("item_iron_plate"));
    const FLDOpponentUsage Blade = PassiveOnly(TEXT("item_edge_blade"));
    bGood &= Test.TestTrue(TEXT("Critical damage reduction is a survival use"), Plate.bSurvival && !Plate.bBasicGrowth);
    bGood &= Test.TestTrue(TEXT("An every-Nth-basic bonus is a basic-growth use"), Blade.bBasicGrowth && !Blade.bSurvival);
    FLDOpponentSelf Unknown;
    Unknown.Equipment[0] = TEXT("item_unknown");
    TOptional<FLDOpponentEquipmentPlan> Out;
    bGood &= Test.TestFalse(TEXT("An unknown held item is refused"), LDOpponentPolicy::PlanEquipment(Unknown, C, Out, Error));
    // 112 §7: with no affordable trade, the AI spends its one upgrade on a held item that has approved upgraded terms.
    const FLDDemoEquipmentDefinition* Upgradable = C.Get().Equipment.FindByPredicate(
        [](const FLDDemoEquipmentDefinition& Item) { return LDDemoEquipment::Upgradable(Item); });
    if (Test.TestNotNull(TEXT("An approved item can be upgraded"), Upgradable))
    {
        const int64 Cost = C.Get().Parameters.UpgradeCost;
        FLDOpponentSelf Holder;
        Holder.Equipment[0] = Upgradable->Id;
        Holder.Gold = Cost;
        bGood &= Test.TestTrue(TEXT("Exactly the upgrade cost and nothing to buy: upgrade slot 0"),
            LDOpponentPolicy::PlanEquipment(Holder, C, Out, Error) && Out.IsSet() && Out->UpgradeSlot == 0 && Out->BuyItemId.IsEmpty()
            && Out->SellSlot == INDEX_NONE);
        Holder.Gold = Cost - 1;
        bGood &= Test.TestTrue(TEXT("One gold short: no upgrade"), LDOpponentPolicy::PlanEquipment(Holder, C, Out, Error) && !Out.IsSet());
        Holder.Gold = Cost;
        Holder.bUpgradeUsed = true;
        bGood &= Test.TestTrue(TEXT("A spent upgrade is never planned again"), LDOpponentPolicy::PlanEquipment(Holder, C, Out, Error) && !Out.IsSet());
        Holder.bUpgradeUsed = false;
        Holder.EquipmentUpgraded[0] = true;
        bGood &= Test.TestTrue(TEXT("An upgraded item is not upgraded twice"), LDOpponentPolicy::PlanEquipment(Holder, C, Out, Error) && !Out.IsSet());
    }
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
    // Below every approved price, so these frames isolate the non-shopping priorities; the approved start can shop.
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
    // #153: only an opening direction pick at the adapter's drawn time is permitted during the countdown.
    Frame = Base;
    Frame.Public.Phase = ELDMatchPhase::Countdown;
    Decision = Decide(Frame);
    bGood &= Expect(Test, TEXT("The opening countdown proposes no body intent"), Decision, EMode::Stopped, EBody::None)
        && Test.TestTrue(TEXT("Nor a build change before T=0"), Decision.Build.Kind == ELDOpponentBuildKind::None);
    FLDOpponentOffer Opening;
    Opening.OfferId = FGuid(0x153, 0, 0, 1);
    Opening.OfferRevision = 1;
    Opening.Options = {Option(1, ELDOpponentOptionKind::Opening, TEXT("opening_athlete")),
        Option(2, ELDOpponentOptionKind::Opening, TEXT("opening_grinder")),
        Option(3, ELDOpponentOptionKind::Opening, TEXT("opening_keyboard"))};
    Frame.Self.Offer = Opening;
    Frame.Self.OpeningPickAtMs = Frame.Public.NowMs + 1;
    Decision = Decide(Frame);
    bGood &= Test.TestTrue(TEXT("Before the drawn time no choice is proposed"), Decision.Build.Kind == ELDOpponentBuildKind::None);
    Frame.Public.NowMs += 1;
    for (uint8 Index = 0; Index < 3; ++Index)
    {
        Frame.Self.OpeningPickIndex = Index;
        Decision = Decide(Frame);
        bGood &= Expect(Test, TEXT("The opening pick never moves"), Decision, EMode::Choose, EBody::None)
            && Test.TestTrue(TEXT("Every randomly planned position selects its own direction without reading a skill"),
                Decision.Build.Kind == ELDOpponentBuildKind::Choose
                && Decision.Build.Choice.OptionId == Opening.Options[Index].OptionId && !Decision.Build.Choice.bReroll);
    }
    Frame.bBuildChangeInFlight = true;
    bGood &= Test.TestTrue(TEXT("An in-flight opening choice is not resubmitted"),
        Decide(Frame).Build.Kind == ELDOpponentBuildKind::None);

    Frame = Base;
    Frame.Self.Phase = ELDLifePhase::Dead;
    FLDOpponentOffer Offer;
    Offer.OfferId = FGuid(0xB0, 0, 0, 2);
    Offer.OfferRevision = 3;
    Offer.Options = {Option(1, ELDOpponentOptionKind::Attribute, TEXT("attr_protein")),
        Option(2, ELDOpponentOptionKind::LearnActive, TEXT("active_snark_bolt"))};
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
    bGood &= Expect(Test, TEXT("A bare start with nothing affordable farms the nearest feasible low camp, not a high camp"), Decision,
        EMode::Farm, EBody::MoveGround, TEXT("l1"));
    Frame = Base;
    Frame.Self.Gold = C.Get().Parameters.StartingGold;
    Decision = Decide(Frame);
    bGood &= Expect(Test, TEXT("The approved starting gold sends a bare start to the public shop first"), Decision, EMode::Shop,
        EBody::ApproachShop, TEXT("shop"));
    auto Shuffled = Base;
    Algo::Reverse(Shuffled.POIs);
    bGood &= Expect(Test, TEXT("Public POI order does not change the decision"), Decide(Shuffled), EMode::Farm, EBody::MoveGround,
        TEXT("l1"));

    // D10: the opening pick is confirmed on the first living decision and does not count as growth for high camps.
    Frame = Base;
    Frame.Self.Offer = Offer;
    Decision = Decide(Frame);
    bGood &= Expect(Test, TEXT("The opening offer is resolved alongside the first body intent"), Decision, EMode::Farm,
        EBody::MoveGround, TEXT("l1")) && Test.TestTrue(TEXT("The opening offer is confirmed at once"),
        Decision.Build.Kind == ELDOpponentBuildKind::Choose && Decision.Build.OfferId == Offer.OfferId);
    Frame = Base;
    Frame.Self.ChoicesConsumed = 1;
    bGood &= Expect(Test, TEXT("A level-one start with its opening pick still farms a low camp"), Decide(Frame), EMode::Farm,
        EBody::MoveGround, TEXT("l1"));

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
        EBody::MoveGround) && Test.TestTrue(TEXT("The retreat leads away from the threat"), Retreat && Retreat->FootCm.X < 0)
        && Test.TestTrue(TEXT("The retreat never targets the shop building as ground"), Retreat && Retreat->Kind != ELDOpponentPOIKind::Shop);
    auto ShopOnly = Frame;
    ShopOnly.POIs = {POI(TEXT("capture"), ELDOpponentPOIKind::Capture, 0, 3000), POI(TEXT("shop"), ELDOpponentPOIKind::Shop, -4400, 0),
        POI(TEXT("s05"), ELDOpponentPOIKind::Spawn, -3000, 500)};
    bGood &= Expect(Test, TEXT("Even with the shop farthest from the threat, the retreat walks to public ground"), Decide(ShopOnly),
        EMode::Disengage, EBody::MoveGround, TEXT("s05"));
    Frame.Units.Reset();
    Frame.Order.Kind = ELDOpponentOrderKind::MoveGround;
    Frame.Order.Status = ELDOpponentOrderStatus::Active;
    Frame.Order.POI = TEXT("l1");
    Decision = Decide(Frame);
    bGood &= Expect(Test, TEXT("Recovering without threats stops investing and waits on regeneration"), Decision,
        EMode::Disengage, EBody::Stop) && Test.TestTrue(TEXT("Recovery reason"), Decision.Reason == ELDOpponentReason::Recover);

    {
        // A committed escape keeps its place while that still leads away, finishes its walk once the pursuer drops
        // out of sight, waits on arrival and moves on only when seen again.
        FLDOpponentPolicyState Escape;
        Frame = Base;
        Frame.Self.HpP2 = Frame.Self.MaxHpP2 / 5;
        Frame.Units = {Unit(2, ELDOpponentUnitKind::Contender, 300, 0)};
        const auto First = LDOpponentPolicy::Decide(Frame, C, Escape);
        Frame.Units[0].FootCm = FVector(300, -100, 0);
        Frame.Order.Kind = ELDOpponentOrderKind::MoveGround;
        Frame.Order.Status = ELDOpponentOrderStatus::Active;
        Frame.Order.POI = First.Body.POI;
        const auto Fresh = Decide(Frame);
        const auto Kept = LDOpponentPolicy::Decide(Frame, C, Escape);
        Frame.Units.Reset();
        const auto Unseen = LDOpponentPolicy::Decide(Frame, C, Escape);
        Frame.Order.Status = ELDOpponentOrderStatus::Completed;
        const auto Arrived = LDOpponentPolicy::Decide(Frame, C, Escape);
        Frame.Self.FootCm = FVector(-2000, -2000, 0);
        Frame.Units = {Unit(2, ELDOpponentUnitKind::Contender, -1700, -2000)};
        const auto Again = LDOpponentPolicy::Decide(Frame, C, Escape);
        bGood &= Expect(Test, TEXT("The escape takes the widest lead beyond the pursuer's sight"), First, EMode::Disengage,
            EBody::MoveGround, TEXT("l3"));
        bGood &= Expect(Test, TEXT("A fresh ranking after the pursuer moved would prefer another place"), Fresh,
            EMode::Disengage, EBody::MoveGround, TEXT("h2"));
        bGood &= Expect(Test, TEXT("The committed escape keeps walking instead of re-ranking every decision"), Kept,
            EMode::Disengage, EBody::Keep, TEXT("l3"));
        bGood &= Expect(Test, TEXT("A pursuer dropping out of sight does not stop the escape"), Unseen, EMode::Disengage,
            EBody::Keep, TEXT("l3"));
        bGood &= Expect(Test, TEXT("Arrived and unseen, the body waits on regeneration"), Arrived, EMode::Disengage,
            EBody::None);
        bGood &= Expect(Test, TEXT("Seen again at the reached place, the escape moves on"), Again, EMode::Disengage,
            EBody::MoveGround, TEXT("h2"));
    }
    {
        // §6.2: recovering above the stop-loss line still answers an urgent public defence; below it only a ready
        // shield lets the body walk back in, otherwise it escapes instead of charging the holder.
        FLDOpponentPolicyState Defence;
        Frame = Base;
        Frame.Public.Phase = ELDMatchPhase::CaptureOpen;
        Frame.Public.Circle = ELDOpponentCircle::Solo;
        Frame.Public.Occupier = FGuid(0xE0, 0, 0, 5);
        Frame.Public.DueAtMs = Frame.Public.NowMs + 5000;
        Frame.CapturePathCm = 3000.;
        Frame.Units = {Unit(5, ELDOpponentUnitKind::Contender, 0, 2900)};
        Frame.Self.HpP2 = Frame.Self.MaxHpP2 / 5;
        const auto Low = LDOpponentPolicy::Decide(Frame, C, Defence);
        Frame.Self.Skills = {Skill(TEXT("active_not_mad"), 1, ELDOpponentSkillSlot::Q)};
        FLDOpponentSource Guard;
        Guard.Slot = ELDOpponentSourceSlot::Q;
        Guard.DefinitionId = TEXT("active_not_mad");
        Guard.bReady = true;
        Frame.Self.Sources = {Guard};
        const auto Shielded = LDOpponentPolicy::Decide(Frame, C, Defence);
        Frame.Self.Skills.Reset();
        Frame.Self.Sources.Reset();
        Frame.Self.HpP2 = Frame.Self.MaxHpP2 / 2;
        const auto Band = LDOpponentPolicy::Decide(Frame, C, Defence);
        // Arrived: the hold is broken and the body, still recovering, is one of the contestants inside the circle.
        Frame.Public.Circle = ELDOpponentCircle::Contested;
        Frame.Public.Occupier.Invalidate();
        Frame.Public.DueAtMs = 0;
        Frame.Self.FootCm = FVector(0, 2700, 0);
        const auto Inside = LDOpponentPolicy::Decide(Frame, C, Defence);
        Frame.Self.HpP2 = Frame.Self.MaxHpP2 / 5;
        const auto Spent = LDOpponentPolicy::Decide(Frame, C, Defence);
        bGood &= Expect(Test, TEXT("Below the stop-loss line without a ready shield the AI escapes instead of charging"), Low,
            EMode::Disengage, EBody::MoveGround);
        bGood &= Expect(Test, TEXT("A ready shield lets a low body answer the urgent defence, shielding first"), Shielded,
            EMode::DefendOrHold, EBody::Activate) && Test.TestTrue(TEXT("The shield is the activated source"),
                Shielded.Body.Source == ELDOpponentSourceSlot::Q);
        bGood &= Expect(Test, TEXT("Recovering above the stop-loss line still answers an urgent public defence"), Band,
            EMode::DefendOrHold, EBody::MoveGround, TEXT("capture"));
        bGood &= Expect(Test, TEXT("Contesting inside the circle, a recovering body fights the rival there instead of fleeing"),
            Inside, EMode::Contest, EBody::AttackUnit) && Test.TestTrue(TEXT("The rival inside the circle is the target"),
                Inside.Body.Handle == Frame.Units[0].Handle);
        bGood &= Expect(Test, TEXT("Below the stop-loss line without a ready shield it leaves the contest"), Spent,
            EMode::Disengage, EBody::MoveGround);
    }
    {
        // §6.5: each contest that ends in a stop-loss exit is one failed attempt on the capture under this build; after
        // RetryPolicy.MaxAttempts the body stops answering the circle until its build changes.
        FLDOpponentPolicyState Bounded;
        Frame = Base;
        Frame.Public.Phase = ELDMatchPhase::CaptureOpen;
        Frame.Public.Circle = ELDOpponentCircle::Solo;
        Frame.Public.Occupier = FGuid(0xE0, 0, 0, 5);
        Frame.CapturePathCm = 3000.;
        Frame.Units = {Unit(5, ELDOpponentUnitKind::Contender, 0, 2900)};
        bool bAnswered = Catalog->Get().Parameters.AI.RetryMaxAttempts > 0;
        for (int64 Attempt = 0; Attempt < Catalog->Get().Parameters.AI.RetryMaxAttempts; ++Attempt)
        {
            Frame.Public.NowMs += 10000;
            Frame.Public.DueAtMs = Frame.Public.NowMs + 5000;
            Frame.Self.HpP2 = Frame.Self.MaxHpP2 / 2;
            bAnswered &= LDOpponentPolicy::Decide(Frame, C, Bounded).Mode == EMode::DefendOrHold;
            Frame.Self.HpP2 = Frame.Self.MaxHpP2 / 5;
            bAnswered &= LDOpponentPolicy::Decide(Frame, C, Bounded).Mode == EMode::Disengage;
        }
        Frame.Public.NowMs += 10000;
        Frame.Public.DueAtMs = Frame.Public.NowMs + 5000;
        Frame.Self.HpP2 = Frame.Self.MaxHpP2 / 2;
        const auto Exhausted = LDOpponentPolicy::Decide(Frame, C, Bounded);
        auto Paused = Frame;
        Paused.Public.Circle = ELDOpponentCircle::Contested;
        Paused.Public.Occupier = Paused.Self.ContenderId;
        Paused.Public.DueAtMs = 0;
        Paused.Self.FootCm = FVector(0, 2700, 0);
        auto PausedState = Bounded;
        const auto HeldAfterSpent = LDOpponentPolicy::Decide(Paused, C, PausedState);
        Frame.Self.BuildRevision += 1;
        const auto Grown = LDOpponentPolicy::Decide(Frame, C, Bounded);
        bGood &= Test.TestTrue(TEXT("Each allowed contest is answered and ends in a stop-loss exit"), bAnswered);
        bGood &= Expect(Test, TEXT("After the allowed attempts the body stops answering the circle under this build"),
            Exhausted, EMode::Disengage, EBody::MoveGround);
        bGood &= Expect(Test, TEXT("D12: spent contest attempts never make a paused holder give up its progress"),
            HeldAfterSpent, EMode::DefendOrHold, EBody::AttackUnit);
        bGood &= Expect(Test, TEXT("A changed build may contest the circle again"), Grown, EMode::DefendOrHold,
            EBody::MoveGround, TEXT("capture"));
    }
    {
        // D12 contest pause: a contest only pauses the holder's progress; leaving or dying clears it.
        FLDOpponentPolicyState Pause;
        auto Held = Base;
        Held.Public.Phase = ELDMatchPhase::CaptureOpen;
        Held.Public.Circle = ELDOpponentCircle::Contested;
        Held.Public.Occupier = Held.Self.ContenderId;
        Held.Public.DueAtMs = 0;
        Held.Self.FootCm = FVector(0, 2700, 0);
        Held.Units = {Unit(4, ELDOpponentUnitKind::Contender, 0, 3200)};
        const auto Hold = LDOpponentPolicy::Decide(Held, C, Pause);
        Held.Units = {Unit(4, ELDOpponentUnitKind::Contender, 0, 2350)};
        const auto NoChase = LDOpponentPolicy::Decide(Held, C, Pause);
        Held.Units = {Unit(4, ELDOpponentUnitKind::Contender, 0, 3200)};
        Held.Self.HpP2 = Held.Self.MaxHpP2 / 5;
        const auto HolderSpent = LDOpponentPolicy::Decide(Held, C, Pause);
        bGood &= Expect(Test, TEXT("D12: a paused holder stays and fights the challenger inside the circle"), Hold,
            EMode::DefendOrHold, EBody::AttackUnit) && Test.TestTrue(TEXT("D12: hold reason and the in-circle challenger"),
                Hold.Reason == ELDOpponentReason::HoldCapture && Hold.Body.Handle == Held.Units[0].Handle);
        bGood &= Expect(Test, TEXT("D12: a paused holder does not chase a rival outside the circle"), NoChase,
            EMode::DefendOrHold, EBody::None);
        bGood &= Expect(Test, TEXT("D12: only below the stop-loss line without a ready shield does a paused holder leave"),
            HolderSpent, EMode::Disengage, EBody::MoveGround);

        // A challenger inside fights the public holder first, matched through the authorized ContenderId of a visible
        // unit, even over a nearer rival it already attacks.
        FLDOpponentPolicyState Challenge;
        auto Contest = Base;
        Contest.Public.Phase = ELDMatchPhase::CaptureOpen;
        Contest.Public.Circle = ELDOpponentCircle::Contested;
        Contest.Self.FootCm = FVector(0, 2700, 0);
        Contest.Units = {Unit(3, ELDOpponentUnitKind::Contender, 0, 2900), Unit(5, ELDOpponentUnitKind::Contender, 0, 3250)};
        const auto Nearer = LDOpponentPolicy::Decide(Contest, C, Challenge);
        Contest.Public.Occupier = FGuid(0xE0, 0, 0, 5);
        const auto Holder = LDOpponentPolicy::Decide(Contest, C, Challenge);
        Contest.Units = {Unit(3, ELDOpponentUnitKind::Contender, 0, 2900)};
        const auto HolderUnseen = LDOpponentPolicy::Decide(Contest, C, Challenge);
        bGood &= Expect(Test, TEXT("D12: without a held progress the nearer in-circle rival is fought"), Nearer, EMode::Contest,
            EBody::AttackUnit) && Test.TestTrue(TEXT("D12: nearer rival"), Nearer.Body.Handle == Contest.Units[0].Handle);
        bGood &= Expect(Test, TEXT("D12: a challenger switches to the visible paused holder"), Holder, EMode::Contest,
            EBody::AttackUnit) && Test.TestTrue(TEXT("D12: the holder is the target"), Holder.Body.Handle == FGuid(0xD0, 0, 0, 5));
        bGood &= Expect(Test, TEXT("D12: a holder out of sight is never targeted from its public id"), HolderUnseen,
            EMode::Contest, EBody::AttackUnit) && Test.TestTrue(TEXT("D12: the visible rival remains the target"),
                HolderUnseen.Body.Handle == Contest.Units[0].Handle);

        // A rival's paused progress has no deadline; the circle is joined as a contest, never as an urgent defence.
        auto Outside = Contest;
        Outside.Self.FootCm = FVector::ZeroVector;
        Outside.Units.Reset();
        const auto Join = Decide(Outside);
        bGood &= Expect(Test, TEXT("D12: a paused rival hold is contested from outside without urgency"), Join, EMode::Contest,
            EBody::MoveGround, TEXT("capture"));
    }
    {
        // Spray and guard (APP_SPEC 2.2): the owner guards the zone but never dies for it; every other body besieges the
        // visible owner or walks in to repaint; a hard control interrupts the public sole sprayer.
        FLDOpponentPolicyState Guard;
        auto Owned = Base;
        Owned.Public.Phase = ELDMatchPhase::CaptureOpen;
        Owned.Public.Circle = ELDOpponentCircle::Empty;
        Owned.Public.Owner = Owned.Self.ContenderId;
        // 2026-11-07: the owner away from an empty zone pauses his guard; it has no win instant until he is back alone.
        Owned.Public.GuardDueAtMs = 0;
        const auto Guarding = LDOpponentPolicy::Decide(Owned, C, Guard);
        bGood &= Expect(Test, TEXT("The graffiti owner returns to guard the zone"), Guarding, EMode::DefendOrHold,
            EBody::MoveGround, TEXT("capture")) && Test.TestTrue(TEXT("Guard reason"), Guarding.Reason == ELDOpponentReason::GuardGraffiti);
        {
            // A recovering owner above the stop-loss line goes back to the empty zone too: away, his guard stays paused.
            FLDOpponentPolicyState Weary;
            auto Away = Owned;
            Away.Self.HpP2 = Away.Self.MaxHpP2 / 2;
            Weary.bRecovering = true;
            const auto Back = LDOpponentPolicy::Decide(Away, C, Weary);
            bGood &= Expect(Test, TEXT("A recovering owner above the stop-loss line returns to his paused guard"), Back,
                EMode::DefendOrHold, EBody::MoveGround, TEXT("capture"))
                && Test.TestTrue(TEXT("Paused guard reason"), Back.Reason == ELDOpponentReason::GuardGraffiti);
        }
        Owned.Self.HpP2 = Owned.Self.MaxHpP2 / 5;
        Owned.Units = {Unit(5, ELDOpponentUnitKind::Contender, 0, 600)};
        const auto Survive = LDOpponentPolicy::Decide(Owned, C, Guard);
        bGood &= Expect(Test, TEXT("A low owner escapes instead of dying, since his death clears the graffiti"), Survive,
            EMode::Disengage, EBody::MoveGround);
        {
            // 2026-11-07: walking out under a challenger resets the frozen guard and walking out of an empty zone pauses it,
            // so a recovering owner above the stop-loss line keeps fighting in the zone; below it he still escapes.
            FLDOpponentPolicyState Held;
            auto Frozen = Owned;
            Frozen.Public.Circle = ELDOpponentCircle::Contested;
            Frozen.Public.GuardDueAtMs = 0;
            Frozen.Self.FootCm = FVector(0, 3000, 0);
            Frozen.Units = {Unit(5, ELDOpponentUnitKind::Contender, 0, 2900)};
            const auto Escape = LDOpponentPolicy::Decide(Frozen, C, Held);
            bGood &= Test.TestTrue(TEXT("Below the stop-loss line the owner leaves even a contested zone"), Escape.Mode == EMode::Disengage);
            Frozen.Self.HpP2 = Frozen.Self.MaxHpP2 / 2;
            const auto Stay = LDOpponentPolicy::Decide(Frozen, C, Held);
            bGood &= Test.TestTrue(TEXT("A recovering owner above the stop-loss line stays in the contested zone"),
                Stay.Mode == EMode::DefendOrHold && Stay.Reason == ELDOpponentReason::GuardGraffiti);
            Frozen.Public.Circle = ELDOpponentCircle::Guarded;
            Frozen.Public.GuardDueAtMs = Frozen.Public.NowMs + 15000;
            Frozen.Units = {Unit(5, ELDOpponentUnitKind::Contender, 0, 600)};
            const auto Rest = LDOpponentPolicy::Decide(Frozen, C, Held);
            bGood &= Test.TestTrue(TEXT("A recovering owner alone in the zone stays in it, since leaving would pause his guard"),
                Rest.Mode == EMode::DefendOrHold && Rest.Reason == ELDOpponentReason::GuardGraffiti);
            Frozen.Self.HpP2 = Frozen.Self.MaxHpP2 / 5;
            const auto Spent = LDOpponentPolicy::Decide(Frozen, C, Held);
            bGood &= Test.TestTrue(TEXT("Below the stop-loss line the owner leaves even a running guard"), Spent.Mode == EMode::Disengage);
        }

        FLDOpponentPolicyState Siege;
        auto Rival = Base;
        Rival.Public.Phase = ELDMatchPhase::CaptureOpen;
        Rival.Public.Circle = ELDOpponentCircle::Empty;
        Rival.Public.Owner = FGuid(0xE0, 0, 0, 5);
        Rival.Public.GuardDueAtMs = 0;
        Rival.CapturePathCm = 3000.;
        Rival.Units = {Unit(5, ELDOpponentUnitKind::Contender, 0, 300)};
        const auto Besiege = LDOpponentPolicy::Decide(Rival, C, Siege);
        bGood &= Expect(Test, TEXT("A visible rival owner within reach is besieged on the way"), Besiege, EMode::Contest,
            EBody::AttackUnit) && Test.TestTrue(TEXT("The owner is the target"), Besiege.Body.Handle == FGuid(0xD0, 0, 0, 5)
                && Besiege.Reason == ELDOpponentReason::BesiegeOwner);
        Rival.Units.Reset();
        const auto Unhurried = LDOpponentPolicy::Decide(Rival, C, Siege);
        bGood &= Expect(Test, TEXT("2026-11-07: a paused guard has no win instant, so it is answered without urgency"), Unhurried,
            EMode::Contest, EBody::MoveGround, TEXT("capture")) && Test.TestTrue(TEXT("Paused siege reason"),
                Unhurried.Reason == ELDOpponentReason::BesiegeOwner);
        Rival.Public.Circle = ELDOpponentCircle::Guarded;
        Rival.Public.GuardDueAtMs = Rival.Public.NowMs + 15000;
        const auto Repaint = LDOpponentPolicy::Decide(Rival, C, Siege);
        bGood &= Expect(Test, TEXT("An unseen owner's graffiti is answered by walking in to repaint"), Repaint, EMode::Contest,
            EBody::MoveGround, TEXT("capture")) && Test.TestTrue(TEXT("Siege reason"), Repaint.Reason == ELDOpponentReason::BesiegeOwner);
        Rival.Public.GuardDueAtMs = Rival.Public.NowMs + 3000;
        const auto Late = LDOpponentPolicy::Decide(Rival, C, Siege);
        bGood &= Expect(Test, TEXT("A guard about to win makes the answer urgent"), Late, EMode::DefendOrHold, EBody::MoveGround,
            TEXT("capture"));

        FLDOpponentPolicyState Stop;
        auto Spray = Base;
        Spray.Public.Phase = ELDMatchPhase::CaptureOpen;
        Spray.Public.Circle = ELDOpponentCircle::Solo;
        Spray.Public.Occupier = FGuid(0xE0, 0, 0, 5);
        Spray.Public.DueAtMs = Spray.Public.NowMs + 2000;
        Spray.Self.FootCm = FVector(0, 2750, 0);
        Spray.Units = {Unit(5, ELDOpponentUnitKind::Contender, 0, 2900)};
        Spray.Self.Skills = {Skill(TEXT("active_stare_down"), 1, ELDOpponentSkillSlot::Q)};
        FLDOpponentSource Taunt;
        Taunt.Slot = ELDOpponentSourceSlot::Q;
        Taunt.DefinitionId = TEXT("active_stare_down");
        Taunt.bReady = true;
        Spray.Self.Sources = {Taunt};
        const auto Interrupt = LDOpponentPolicy::Decide(Spray, C, Stop);
        bGood &= Expect(Test, TEXT("A ready taunt interrupts the public sole sprayer within its radius"), Interrupt,
            EMode::DefendOrHold, EBody::Activate) && Test.TestTrue(TEXT("The taunt is the activated source"),
                Interrupt.Body.Source == ELDOpponentSourceSlot::Q);
        Spray.Public.Circle = ELDOpponentCircle::Contested;
        Spray.Public.DueAtMs = 0;
        FLDOpponentPolicyState Paused;
        const auto NoInterrupt = LDOpponentPolicy::Decide(Spray, C, Paused);
        bGood &= Expect(Test, TEXT("A paused holder is fought, not taunted, since nobody sprays"), NoInterrupt, EMode::Contest,
            EBody::AttackUnit);
    }

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
    // Both feet inside the 300 cm zone yet farther apart than the acquisition radius, so only the zone draws the fight.
    Frame.Self.FootCm = FVector(0, 2750, 0);
    Frame.Units = {Unit(3, ELDOpponentUnitKind::Contender, 0, 3240), Unit(4, ELDOpponentUnitKind::Contender, 5000, 5000)};
    Decision = Decide(Frame);
    bGood &= Expect(Test, TEXT("Inside the circle only rivals inside it are fought"), Decision, EMode::Contest, EBody::AttackUnit)
        && Test.TestTrue(TEXT("The in-circle rival"), Decision.Body.Handle == Frame.Units[0].Handle);
    Frame.Public.Circle = ELDOpponentCircle::Solo;
    Frame.Public.Occupier = Frame.Self.ContenderId;
    // Just outside the zone edge yet inside the acquisition radius of the holder.
    Frame.Units = {Unit(4, ELDOpponentUnitKind::Contender, 0, 2650)};
    Decision = Decide(Frame);
    bGood &= Expect(Test, TEXT("A solo holder does not chase a rival outside the circle"), Decision, EMode::DefendOrHold,
        EBody::None) && Test.TestTrue(TEXT("Hold reason"), Decision.Reason == ELDOpponentReason::HoldCapture);

    Frame = Base;
    Frame.Order.Kind = ELDOpponentOrderKind::MoveGround;
    Frame.Order.Status = ELDOpponentOrderStatus::Active;
    Frame.Order.POI = TEXT("l1");
    bGood &= Expect(Test, TEXT("A still-valid order is kept, not re-sent"), Decide(Frame), EMode::Farm, EBody::Keep, TEXT("l1"));
    auto Explore = Base;
    Explore.POIs = {POI(TEXT("capture"), ELDOpponentPOIKind::Capture, 0, 3000), POI(TEXT("shop"), ELDOpponentPOIKind::Shop, -300, 0),
        POI(TEXT("s00"), ELDOpponentPOIKind::Spawn, 4000, 4000)};
    bGood &= Expect(Test, TEXT("Exploring never walks onto the shop anchor"), Decide(Explore), EMode::Explore, EBody::MoveGround,
        TEXT("s00"));
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
    Frame.Self.Skills = {Skill(TEXT("active_snark_bolt"), 1, ELDOpponentSkillSlot::Q)};
    FLDOpponentSource Bolt;
    Bolt.Slot = ELDOpponentSourceSlot::Q;
    Bolt.DefinitionId = TEXT("active_snark_bolt");
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

    // The approved ids, at synthetic positions: every shop and camp of the catalog, one spawn, three airdrops, two dorms.
    const auto& Refs = C.Get().World;
    FLDDemoWorldGeometry World;
    World.Anchors = {Anchor(*Refs.CaptureId, ELDDemoWorldAnchorKind::Capture, 0, 3000),
        Anchor(*Refs.SpawnIds[0], ELDDemoWorldAnchorKind::Spawn, 10, 20),
        Anchor(TEXT("airdrop_ne"), ELDDemoWorldAnchorKind::Airdrop, 1940, 2670),
        Anchor(TEXT("airdrop_nw"), ELDDemoWorldAnchorKind::Airdrop, -1940, 2670),
        Anchor(TEXT("airdrop_s"), ELDDemoWorldAnchorKind::Airdrop, 0, -3300),
        // D26: Boss patrol points are not places a body goes; the projection leaves them out.
        Anchor(TEXT("boss_patrol_1"), ELDDemoWorldAnchorKind::BossWaypoint, 2400, 2400),
        Anchor(TEXT("dorm_nw"), ELDDemoWorldAnchorKind::Dorm, -4400, 4400), Anchor(TEXT("dorm_se"), ELDDemoWorldAnchorKind::Dorm, 4400, -4400)};
    for (int32 Index = 0; Index < Refs.ShopIds.Num(); ++Index)
    {
        World.Anchors.Add(Anchor(*Refs.ShopIds[Index], ELDDemoWorldAnchorKind::Shop, -3000, 400 * Index));
    }
    FString HighId, LowId;
    for (int32 Index = 0; Index < C.Get().Camps.Num(); ++Index)
    {
        const FLDDemoCampPlacement& Camp = C.Get().Camps[Index];
        const bool bHigh = Camp.MonsterId == TEXT("monster_guardian");
        (bHigh ? HighId : LowId) = (bHigh ? HighId : LowId).IsEmpty() ? Camp.AnchorId : (bHigh ? HighId : LowId);
        World.Anchors.Add(Anchor(*Camp.AnchorId, ELDDemoWorldAnchorKind::Camp, 300 * Index, -2000, bHigh ? 1 : 2));
    }
    const int32 Places = World.Anchors.Num() - 1;
    TArray<FLDOpponentPOI> POIs;
    bool bGood = Test.TestTrue(TEXT("Approved anchors and the camp roster form the public places: ") + Error,
        LDOpponentFrame::ReadPublicPOIs(World, C, POIs, Error) && POIs.Num() == Places);
    const auto* DropPOI = POIs.FindByPredicate([](const FLDOpponentPOI& P) { return P.Id == TEXT("airdrop_s"); });
    bGood &= Test.TestTrue(TEXT("D26: each airdrop point is a public place"),
        DropPOI && DropPOI->Kind == ELDOpponentPOIKind::Airdrop && DropPOI->FootCm.Equals(FVector(0, -3300, 0)));
    const auto* DormPOI = POIs.FindByPredicate([](const FLDOpponentPOI& P) { return P.Id == TEXT("dorm_se"); });
    bGood &= Test.TestTrue(TEXT("D21: each dorm is public recovery ground"),
        DormPOI && DormPOI->Kind == ELDOpponentPOIKind::Dorm && DormPOI->FootCm.Equals(FVector(4400, -4400, 0)));
    const auto* High = POIs.FindByPredicate([&HighId](const FLDOpponentPOI& P) { return P.Id == FName(*HighId); });
    const auto* Low = POIs.FindByPredicate([&LowId](const FLDOpponentPOI& P) { return P.Id == FName(*LowId); });
    const auto* Spawn = POIs.FindByPredicate([&Refs](const FLDOpponentPOI& P) { return P.Id == FName(*Refs.SpawnIds[0]); });
    bGood &= Test.TestTrue(TEXT("Camp tier and creature come from the public catalog"),
        High && High->Kind == ELDOpponentPOIKind::Camp && High->Tier == ELDDemoCampTier::High && High->MonsterId == TEXT("monster_guardian")
        && Low && Low->Tier == ELDDemoCampTier::Low && Low->MonsterId == TEXT("monster_scavenger")
        && Spawn && Spawn->Kind == ELDOpponentPOIKind::Spawn && Spawn->FootCm.Equals(FVector(10, 20, 0)) && Spawn->SurfaceId == TEXT("ground"));
    const auto Without = [&World](ELDDemoWorldAnchorKind Kind, int32 Count)
    {
        auto Out = World;
        for (int32 Index = Out.Anchors.Num() - 1; Index >= 0 && Count > 0; --Index)
        {
            if (Out.Anchors[Index].Kind == Kind) { Out.Anchors.RemoveAt(Index); --Count; }
        }
        return Out;
    };
    bGood &= Test.TestFalse(TEXT("A world without any dorm is refused"),
        LDOpponentFrame::ReadPublicPOIs(Without(ELDDemoWorldAnchorKind::Dorm, 2), C, POIs, Error));
    bGood &= Test.TestFalse(TEXT("A world without any airdrop point is refused"),
        LDOpponentFrame::ReadPublicPOIs(Without(ELDDemoWorldAnchorKind::Airdrop, 3), C, POIs, Error));
    auto Unplaced = World;
    Unplaced.Anchors.Add(Anchor(TEXT("x9"), ELDDemoWorldAnchorKind::Camp, 0, 0, 2));
    bGood &= Test.TestFalse(TEXT("A camp anchor outside the approved roster is refused"), LDOpponentFrame::ReadPublicPOIs(Unplaced, C, POIs, Error));
    bGood &= Test.TestFalse(TEXT("A world missing an approved camp is refused"),
        LDOpponentFrame::ReadPublicPOIs(Without(ELDDemoWorldAnchorKind::Camp, 1), C, POIs, Error));
    bGood &= Test.TestFalse(TEXT("A world missing an approved shop is refused"),
        LDOpponentFrame::ReadPublicPOIs(Without(ELDDemoWorldAnchorKind::Shop, 1), C, POIs, Error));
    auto Unknown = World;
    Unknown.Anchors.Add(Anchor(TEXT("s99"), ELDDemoWorldAnchorKind::Spawn, 0, 0));
    bGood &= Test.TestFalse(TEXT("An unapproved public anchor is refused"), LDOpponentFrame::ReadPublicPOIs(Unknown, C, POIs, Error));
    auto Duplicate = World;
    Duplicate.Anchors.Add(Anchor(*Refs.SpawnIds[0], ELDDemoWorldAnchorKind::Spawn, 0, 0));
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
    Bolt.DefinitionId = TEXT("active_snark_bolt");
    Bolt.bActive = true;
    Bolt.Rank = 1;
    Bolt.Slot = 0;
    FLDLearnedSkillView Rhythm;
    Rhythm.DefinitionId = TEXT("passive_pressed");
    Rhythm.Rank = 1;
    Progression.Skills = {Bolt, Rhythm};
    FLDAttributeCardView Might;
    Might.DefinitionId = TEXT("attr_protein");
    Might.Count = 1;
    Might.Quality = ELDAttributeQuality::Blue;
    Progression.AttributeCards = {Might};
    Progression.bHasOffer = true;
    Progression.Offer.OfferId = FGuid(0xF2, 0, 0, 1);
    Progression.Offer.OfferRevision = 4;
    FLDChoiceOption Guard;
    Guard.OptionId = FGuid(0xF2, 0, 0, 2);
    Guard.Kind = ELDChoiceOptionKind::LearnActive;
    Guard.DefinitionId = TEXT("active_not_mad");
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
        {FLDLearnedPassiveValue(TEXT("passive_pressed"), 1)}, {FLDAttributeCardValue(TEXT("attr_protein"), 1)},
        {TEXT("item_edge_blade")}), C);
    bGood &= Test.TestTrue(TEXT("Own build maps through the shared attribute projection: ") + Error,
        Expected.IsSuccess() && LDOpponentFrame::ReadOwnBuild(Progression, Equipment, C, Self, Error)
        && Self.Attributes.ADP2 == Expected.Value->Final.ADP2 && Self.Attributes.ArmorP2 == Expected.Value->Final.ArmorP2
        && Self.Attributes.AttackSpeed.Numerator * Expected.Value->Final.AttackSpeed.Denominator
            == Expected.Value->Final.AttackSpeed.Numerator * Self.Attributes.AttackSpeed.Denominator
        && Self.Gold == 42 && Self.Equipment[0] == TEXT("item_edge_blade") && Self.Equipment[1].IsEmpty()
        && Self.BuildRevision == 9 && Self.Level == 3 && Self.ChoicesConsumed == 2 && Self.Skills.Num() == 2
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
    {
        // A game build prints each id with whichever spelling registered first; the editor keeps these spellings, so
        // this exercises the mapping back to the lowercase content ids that the policy compares as text.
        auto Cased = Progression;
        Cased.bCanChoose = true;
        Cased.Skills[0].DefinitionId = TEXT("Active_Snark_Bolt");
        Cased.Skills[1].DefinitionId = TEXT("PASSIVE_PRESSED");
        Cased.AttributeCards[0].DefinitionId = TEXT("Attr_Protein");
        Cased.Offer.Options[0].DefinitionId = TEXT("Active_Not_Mad");
        auto CasedEquipment = Equipment;
        CasedEquipment.Slots[0].DefinitionId = TEXT("Item_Edge_Blade");
        FLDOpponentSelf CasedSelf;
        bGood &= Test.TestTrue(TEXT("Ids printed in another casing map to the lowercase content ids and the same build: ") + Error,
            Expected.IsSuccess() && LDOpponentFrame::ReadOwnBuild(Cased, CasedEquipment, C, CasedSelf, Error)
            && CasedSelf.Skills.Num() == 2 && CasedSelf.Skills[0].DefinitionId == TEXT("active_snark_bolt")
            && CasedSelf.Skills[1].DefinitionId == TEXT("passive_pressed") && CasedSelf.Equipment[0] == TEXT("item_edge_blade")
            && CasedSelf.Offer.IsSet() && CasedSelf.Offer->Options[0].DefinitionId == TEXT("active_not_mad")
            && CasedSelf.Attributes.ADP2 == Expected.Value->Final.ADP2 && CasedSelf.Attributes.ArmorP2 == Expected.Value->Final.ArmorP2);
    }
    {
        // Packaged-casing variants: canonical ids reach the same catalog entries and make exactly the canonical decisions.
        bGood &= Test.TestTrue(TEXT("Canonical ids are the lowercase content ids; an absent id is empty"),
            LDOpponentFrame::CanonicalId(FName(TEXT("Item_Iron_Plate"))) == TEXT("item_iron_plate")
            && LDOpponentFrame::CanonicalId(FName(TEXT("Monster_Scavenger"))) == TEXT("monster_scavenger")
            && LDOpponentFrame::CanonicalId(NAME_None).IsEmpty());
        FLDOpponentDecisionFrame CaseBase;
        if (!BaseFrame(Test, C, CaseBase)) { return false; }
        FLDOpponentSelf CaseCanonSelf = CaseBase.Self;
        CaseCanonSelf.Gold = 400;
        CaseCanonSelf.Equipment[0] = TEXT("item_iron_plate");
        FLDOpponentSelf CaseMappedSelf = CaseCanonSelf;
        CaseMappedSelf.Equipment[0] = LDOpponentFrame::CanonicalId(FName(TEXT("Item_Iron_Plate")));
        TOptional<FLDOpponentEquipmentPlan> CaseCanonPlan, CaseMappedPlan;
        FString CasePlanError;
        bGood &= Test.TestTrue(TEXT("An equipped id printed as Item_Iron_Plate plans exactly like the content id: ") + CasePlanError,
            LDOpponentPolicy::PlanEquipment(CaseCanonSelf, C, CaseCanonPlan, CasePlanError)
            && LDOpponentPolicy::PlanEquipment(CaseMappedSelf, C, CaseMappedPlan, CasePlanError)
            && CaseCanonPlan.IsSet() == CaseMappedPlan.IsSet()
            && (!CaseCanonPlan.IsSet() || (CaseCanonPlan->SellSlot == CaseMappedPlan->SellSlot
                && CaseCanonPlan->BuyItemId == CaseMappedPlan->BuyItemId)));
        auto CaseCanonFrame = CaseBase;
        CaseCanonFrame.Units = {Unit(1, ELDOpponentUnitKind::CampMember, 2000, 100, TEXT("monster_scavenger"))};
        auto CaseMappedFrame = CaseCanonFrame;
        for (auto& Place : CaseMappedFrame.POIs)
        {
            if (Place.MonsterId == TEXT("monster_scavenger"))
            {
                Place.MonsterId = LDOpponentFrame::CanonicalId(FName(TEXT("Monster_Scavenger")));
            }
        }
        FLDOpponentPolicyState CaseCanonState, CaseMappedState;
        const auto CaseCanonDecision = LDOpponentPolicy::Decide(CaseCanonFrame, C, CaseCanonState);
        const auto CaseMappedDecision = LDOpponentPolicy::Decide(CaseMappedFrame, C, CaseMappedState);
        bGood &= Test.TestTrue(TEXT("A camp monster printed as Monster_Scavenger still owns its visible member, which is attacked as for the content id"),
            CaseCanonDecision.Mode == ELDOpponentMode::Farm && CaseCanonDecision.Body.Kind == ELDOpponentBodyKind::AttackUnit
            && CaseCanonDecision.Body.Handle == CaseCanonFrame.Units[0].Handle
            && CaseMappedDecision.Mode == CaseCanonDecision.Mode && CaseMappedDecision.Body.Kind == CaseCanonDecision.Body.Kind
            && CaseMappedDecision.Body.Handle == CaseCanonDecision.Body.Handle && CaseMappedDecision.Body.POI == CaseCanonDecision.Body.POI);
    }

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
    const auto Entry = [](ELDCombatEntry Slot, ELDCombatSourceKind Kind, const TCHAR* Id, int64 ReadyAt, ELDCombatReject Why)
    {
        FLDCombatEntryView Out;
        Out.Entry = Slot;
        Out.SourceKind = Kind;
        Out.DefinitionId = Id;
        Out.ReadyAtMs = ReadyAt;
        Out.Unavailable = Why;
        return Out;
    };
    Combat.Entries = {Entry(ELDCombatEntry::Q, ELDCombatSourceKind::Learned, TEXT("active_snark_bolt"), 9000, ELDCombatReject::None),
        Entry(ELDCombatEntry::W, ELDCombatSourceKind::Learned, TEXT("active_not_mad"), 15000, ELDCombatReject::Cooldown),
        Entry(ELDCombatEntry::E, ELDCombatSourceKind::Learned, TEXT("active_elbow"), 9000, ELDCombatReject::None),
        Entry(ELDCombatEntry::Dash, ELDCombatSourceKind::Innate, TEXT("innate_dash"), 0, ELDCombatReject::ControlBlocked)};
    Combat.Entries.Last().RangeCm = 450;
    bGood &= Test.TestTrue(TEXT("Own combat view maps resources, readiness, the innate dash reach and Root: ") + Error,
        LDOpponentFrame::ReadOwnCombat(Combat, 10000, Self, Error) && Self.HpP2 == 30000 && Self.MaxMpP2 == 28000
        && Self.BasicReadyAtMs == 12345 && Self.MoveBlockedUntilMs == 10700 && Self.Sources.Num() == 4
        && Self.Sources[0].Slot == ELDOpponentSourceSlot::Q && Self.Sources[0].bReady
        && Self.Sources[1].Slot == ELDOpponentSourceSlot::W && !Self.Sources[1].bReady
        && Self.Sources[2].Slot == ELDOpponentSourceSlot::E && Self.Sources[2].bReady
        && Self.Sources[3].Slot == ELDOpponentSourceSlot::Dash && !Self.Sources[3].bReady && Self.Sources[3].RangeCm == 450);
    {
        auto Cased = Combat;
        Cased.Entries[0].DefinitionId = TEXT("Active_Snark_Bolt");
        FLDOpponentSelf CasedSelf;
        bGood &= Test.TestTrue(TEXT("A source id printed in another casing maps to its lowercase content id"),
            LDOpponentFrame::ReadOwnCombat(Cased, 10000, CasedSelf, Error) && CasedSelf.Sources.Num() == 4
            && CasedSelf.Sources[0].DefinitionId == TEXT("active_snark_bolt"));
    }
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

bool KeyIs(const FLDOpponentRetryKey& Key, ELDOpponentFailure Category, FName Place, const FGuid& Target, uint8 Slot = 0)
{
    return Key.Category == Category && Key.POI == Place && Key.Target == Target && Key.Slot == Slot;
}

bool RunOutcomes(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(Test, Catalog)) { return false; }
    const auto& C = *Catalog;
    const auto& AI = C.Get().Parameters.AI;
    using EBody = ELDOpponentBodyKind;
    using EFailure = ELDOpponentFailure;
    FLDOpponentDecisionFrame Base;
    if (!BaseFrame(Test, C, Base)) { return false; }

    FLDOpponentPolicyState State;
    auto Decision = LDOpponentPolicy::Decide(Base, C, State);
    bool bGood = Test.TestTrue(TEXT("A camp move carries its public path key"),
        Decision.Body.Kind == EBody::MoveGround && KeyIs(Decision.Body.Key, EFailure::Path, TEXT("l1"), FGuid()));

    auto Frame = Base;
    Frame.Units = {Unit(1, ELDOpponentUnitKind::CampMember, 2000, 100, TEXT("monster_scavenger")),
        Unit(2, ELDOpponentUnitKind::CampMember, 2100, 0, TEXT("monster_scavenger"))};
    Frame.Units[1].HpP2 = 60000;
    const FGuid First = Frame.Units[0].Handle, Second = Frame.Units[1].Handle;
    State = FLDOpponentPolicyState();
    Decision = LDOpponentPolicy::Decide(Frame, C, State);
    bGood &= Test.TestTrue(TEXT("An attack carries the handle's engagement key"), Decision.Body.Kind == EBody::AttackUnit
        && Decision.Body.Handle == First && KeyIs(Decision.Body.Key, EFailure::Engagement, NAME_None, First));
    LDOpponentPolicy::RecordOutcome(State, Decision.Body.Key, ELDOpponentOutcome::Failed, Frame.Self, Frame.Public.NowMs, AI);
    Decision = LDOpponentPolicy::Decide(Frame, C, State);
    bGood &= Test.TestTrue(TEXT("A refused attack waits out its backoff instead of re-sending each decision"),
        Decision.Body.Kind == EBody::None);
    auto Moving = Frame;
    Moving.Order.Kind = ELDOpponentOrderKind::MoveGround;
    Moving.Order.Status = ELDOpponentOrderStatus::Active;
    Moving.Order.POI = TEXT("l1");
    FLDOpponentPolicyState MovingState = State;
    bGood &= Test.TestTrue(TEXT("During that backoff a live approach keeps moving instead of being stopped"),
        LDOpponentPolicy::Decide(Moving, C, MovingState).Body.Kind == EBody::Keep);
    Frame.Public.NowMs += AI.RetryInitialBackoffMs;
    Decision = LDOpponentPolicy::Decide(Frame, C, State);
    bGood &= Test.TestTrue(TEXT("After the backoff the same visible target is attacked again"),
        Decision.Body.Kind == EBody::AttackUnit && Decision.Body.Handle == First);
    LDOpponentPolicy::RecordOutcome(State, Decision.Body.Key, ELDOpponentOutcome::TargetLost, Frame.Self, Frame.Public.NowMs, AI);
    bGood &= Test.TestTrue(TEXT("A lost target is released at once"), !State.TargetHandle.IsValid());
    Decision = LDOpponentPolicy::Decide(Frame, C, State);
    bGood &= Test.TestTrue(TEXT("A lost handle is not re-picked; another visible member is attacked"),
        Decision.Body.Kind == EBody::AttackUnit && Decision.Body.Handle == Second && State.TargetHandle == Second);

    Frame = Base;
    Frame.Self.Skills = {Skill(TEXT("active_snark_bolt"), 1, ELDOpponentSkillSlot::Q)};
    FLDOpponentSource Bolt;
    Bolt.Slot = ELDOpponentSourceSlot::Q;
    Bolt.DefinitionId = TEXT("active_snark_bolt");
    Bolt.bReady = true;
    Frame.Self.Sources = {Bolt};
    Frame.Units = {Unit(3, ELDOpponentUnitKind::Contender, 300, 0)};
    State = FLDOpponentPolicyState();
    Decision = LDOpponentPolicy::Decide(Frame, C, State);
    bGood &= Test.TestTrue(TEXT("An activation carries its source slot and visible target"), Decision.Body.Kind == EBody::Activate
        && KeyIs(Decision.Body.Key, EFailure::Activation, NAME_None, Frame.Units[0].Handle, uint8(ELDOpponentSourceSlot::Q)));
    LDOpponentPolicy::RecordOutcome(State, Decision.Body.Key, ELDOpponentOutcome::TargetLost, Frame.Self, Frame.Public.NowMs, AI);
    Decision = LDOpponentPolicy::Decide(Frame, C, State);
    bGood &= Test.TestTrue(TEXT("A unit lost to an activation is not chased by a basic attack order either"),
        Decision.Body.Kind == EBody::MoveGround && Decision.Body.POI == TEXT("l1"));

    Frame = Base;
    Frame.Self.Gold = 330;
    FLDOpponentOffer Offer;
    Offer.OfferId = FGuid(0xB0, 0, 0, 5);
    Offer.OfferRevision = 2;
    Offer.Options = {Option(1, ELDOpponentOptionKind::Attribute, TEXT("attr_protein"))};
    Frame.Self.Offer = Offer;
    State = FLDOpponentPolicyState();
    Decision = LDOpponentPolicy::Decide(Frame, C, State);
    bGood &= Test.TestTrue(TEXT("A choice carries its offer key"), Decision.Build.Kind == ELDOpponentBuildKind::Choose
        && KeyIs(Decision.Build.Key, EFailure::Choice, NAME_None, Offer.OfferId));
    bGood &= Test.TestTrue(TEXT("A shop approach carries the shop visit key"), Decision.Body.Kind == EBody::ApproachShop
        && KeyIs(Decision.Body.Key, EFailure::Shop, TEXT("shop"), FGuid()));
    Frame.Self.Offer.Reset();
    Frame.bShopOpen = true;
    Decision = LDOpponentPolicy::Decide(Frame, C, State);
    const FLDOpponentRetryKey ShopKey = Decision.Build.Key;
    bGood &= Test.TestTrue(TEXT("A trade carries the same shop visit key"), Decision.Build.Kind == ELDOpponentBuildKind::Buy
        && KeyIs(ShopKey, EFailure::Shop, TEXT("shop"), FGuid()));
    for (int64 Attempt = 0; Attempt < AI.RetryMaxAttempts; ++Attempt)
    {
        LDOpponentPolicy::RecordOutcome(State, ShopKey, ELDOpponentOutcome::Failed, Frame.Self, Frame.Public.NowMs, AI);
    }
    Decision = LDOpponentPolicy::Decide(Frame, C, State);
    bGood &= Test.TestTrue(TEXT("An exhausted shop visit stops trading and returns to farming"),
        Decision.Build.Kind == ELDOpponentBuildKind::None && Decision.Body.Kind == EBody::MoveGround && Decision.Body.POI == TEXT("l1"));
    LDOpponentPolicy::RecordOutcome(State, ShopKey, ELDOpponentOutcome::Succeeded, Frame.Self, Frame.Public.NowMs, AI);
    bGood &= Test.TestTrue(TEXT("A final success clears the shop visit"),
        LDOpponentPolicy::Decide(Frame, C, State).Build.Kind == ELDOpponentBuildKind::Buy);
    return bGood;
}

bool RunRequests(FAutomationTestBase& Test)
{
    using EBody = ELDOpponentBodyKind;
    using ESettle = ELDOpponentSettle;
    FString Error;
    FLDMoveIdentity Last, Next;
    bool bGood = Test.TestTrue(TEXT("The first order of a life is Order 1 at Epoch 0"),
        LDOpponentRequests::NextMoveIdentity(Last, 7, 1, false, Next) && Next.Match == 7 && Next.Life == 1 && Next.Order == 1
        && Next.Epoch == 0);
    Last = Next;
    bGood &= Test.TestTrue(TEXT("A Stop advances both Order and Epoch"),
        LDOpponentRequests::NextMoveIdentity(Last, 7, 1, true, Next) && Next.Order == 2 && Next.Epoch == 1);
    Last = Next;
    bGood &= Test.TestTrue(TEXT("A later order keeps the Stop epoch"),
        LDOpponentRequests::NextMoveIdentity(Last, 7, 1, false, Next) && Next.Order == 3 && Next.Epoch == 1);
    Last = Next;
    bGood &= Test.TestTrue(TEXT("A new life restarts the per-life sequence"),
        LDOpponentRequests::NextMoveIdentity(Last, 7, 2, false, Next) && Next.Life == 2 && Next.Order == 1 && Next.Epoch == 0);
    Last.Order = MAX_uint32;
    bGood &= Test.TestFalse(TEXT("An exhausted order space is refused, never wrapped"),
        LDOpponentRequests::NextMoveIdentity(Last, 7, 1, false, Next));
    bGood &= Test.TestFalse(TEXT("No identity without a match and a life"),
        LDOpponentRequests::NextMoveIdentity(FLDMoveIdentity(), 0, 1, false, Next));

    const TArray<FLDOpponentPOI> Places = {POI(TEXT("shop"), ELDOpponentPOIKind::Shop, -3000, 0),
        POI(TEXT("l1"), ELDOpponentPOIKind::Camp, 2000, 0, TEXT("monster_scavenger"))};
    FLDMoveIdentity Identity;
    Identity.Match = 7;
    Identity.Life = 1;
    Identity.Order = 4;
    FLDSpatialOrderRequest Order;
    FLDOpponentBodyIntent Move;
    Move.Kind = EBody::MoveGround;
    Move.POI = TEXT("l1");
    bGood &= Test.TestTrue(TEXT("MoveGround targets the public anchor on its registered surface: ") + Error,
        LDOpponentRequests::BuildOrder(Move, Places, Identity, Order, Error) && Order.Kind == ELDSpatialOrderKind::MoveGround
        && Order.Identity == Identity && FVector(Order.GroundCm) == FVector(2000, 0, 0) && Order.SurfaceHint == FName(TEXT("ground")));
    FLDOpponentBodyIntent Attack;
    Attack.Kind = EBody::AttackUnit;
    Attack.Handle = FGuid(0xD0, 0, 0, 1);
    Attack.Epoch = 3;
    bGood &= Test.TestTrue(TEXT("AttackUnit carries only the observer-local handle"),
        LDOpponentRequests::BuildOrder(Attack, Places, Identity, Order, Error) && Order.Kind == ELDSpatialOrderKind::AttackUnit
        && Order.Observed == Attack.Handle && Order.ObservationEpoch == 3 && FVector(Order.GroundCm).IsZero() && Order.SurfaceHint.IsNone());
    Attack.Epoch = 0;
    bGood &= Test.TestFalse(TEXT("A handle without its observation epoch is refused"),
        LDOpponentRequests::BuildOrder(Attack, Places, Identity, Order, Error));
    FLDOpponentBodyIntent Approach;
    Approach.Kind = EBody::ApproachShop;
    Approach.POI = TEXT("shop");
    bGood &= Test.TestTrue(TEXT("ApproachShop names the public shop"),
        LDOpponentRequests::BuildOrder(Approach, Places, Identity, Order, Error) && Order.Kind == ELDSpatialOrderKind::ApproachShop
        && Order.ShopId == FName(TEXT("shop")));
    Approach.POI = TEXT("l1");
    bGood &= Test.TestFalse(TEXT("ApproachShop to a camp is refused"), LDOpponentRequests::BuildOrder(Approach, Places, Identity, Order, Error));
    Move.POI = TEXT("nowhere");
    bGood &= Test.TestFalse(TEXT("An unknown place is refused"), LDOpponentRequests::BuildOrder(Move, Places, Identity, Order, Error));
    Move.POI = TEXT("l1");
    bGood &= Test.TestFalse(TEXT("Order 0 is never a valid identity"),
        LDOpponentRequests::BuildOrder(Move, Places, FLDMoveIdentity(), Order, Error));
    FLDOpponentBodyIntent Stop;
    Stop.Kind = EBody::Stop;
    bGood &= Test.TestFalse(TEXT("Stop is a cancel, not an order"), LDOpponentRequests::BuildOrder(Stop, Places, Identity, Order, Error));

    FLDCombatActionRequest Action;
    FLDOpponentBodyIntent Cast;
    Cast.Kind = EBody::Activate;
    Cast.Source = ELDOpponentSourceSlot::W;
    Cast.TargetMode = ELDDemoTargetMode::Self;
    bGood &= Test.TestTrue(TEXT("A self activation of the W slot: ") + Error,
        LDOpponentRequests::BuildAction(Cast, 7, 1, 5, 11, Action, Error) && Action.Entry == ELDCombatEntry::W
        && Action.Target.Kind == ELDCombatTargetKind::Self && Action.Generation == 7 && Action.Life == 1
        && Action.DirectSequence == 5 && Action.ExpectedSourceRevision == 11);
    Cast.Source = ELDOpponentSourceSlot::Q;
    Cast.TargetMode = ELDDemoTargetMode::Unit;
    Cast.Handle = FGuid(0xD0, 0, 0, 2);
    Cast.Epoch = 4;
    bGood &= Test.TestTrue(TEXT("A unit activation names the observer-local handle"),
        LDOpponentRequests::BuildAction(Cast, 7, 1, 6, 11, Action, Error) && Action.Entry == ELDCombatEntry::Q
        && Action.Target.Kind == ELDCombatTargetKind::Unit && Action.Target.Observed == Cast.Handle
        && Action.Target.ObservationEpoch == 4);
    Cast.Epoch = 0;
    bGood &= Test.TestFalse(TEXT("A unit activation without its observation epoch is refused"),
        LDOpponentRequests::BuildAction(Cast, 7, 1, 6, 11, Action, Error));
    Cast.Source = ELDOpponentSourceSlot::Dash;
    Cast.TargetMode = ELDDemoTargetMode::DirectionDash;
    Cast.Direction = FVector(0, 3, 4);
    bGood &= Test.TestTrue(TEXT("The innate dash goes out on the D entry with a planar normalized direction"),
        LDOpponentRequests::BuildAction(Cast, 7, 1, 7, 11, Action, Error) && Action.Entry == ELDCombatEntry::Dash
        && Action.Target.Kind == ELDCombatTargetKind::Direction && FVector(Action.Target.Direction).Equals(FVector(0, 1, 0), 1.e-4));
    Cast.Direction = FVector(0, 0, 1);
    bGood &= Test.TestFalse(TEXT("A vertical-only direction is refused"), LDOpponentRequests::BuildAction(Cast, 7, 1, 8, 11, Action, Error));
    Cast.Direction = FVector(1, 0, 0);
    bGood &= Test.TestFalse(TEXT("A direct sequence is required"), LDOpponentRequests::BuildAction(Cast, 7, 1, 0, 11, Action, Error));
    // #147 active_quick_meeting: a ground zone names its requested point.
    Cast.Source = ELDOpponentSourceSlot::Q;
    Cast.TargetMode = ELDDemoTargetMode::GroundPoint;
    Cast.Point = FVector(120, -340, 0);
    bGood &= Test.TestTrue(TEXT("A ground activation sends the requested point: ") + Error,
        LDOpponentRequests::BuildAction(Cast, 7, 1, 9, 11, Action, Error) && Action.Entry == ELDCombatEntry::Q
        && Action.Target.Kind == ELDCombatTargetKind::Point && FVector(Action.Target.Point).Equals(Cast.Point, 1.)
        && !Action.Target.Observed.IsValid());

    FLDProgressionView Progression;
    Progression.bValid = true;
    Progression.Generation = 7;
    Progression.Life = 1;
    Progression.LifePhase = ELDLifePhase::Dead;
    Progression.bCanChoose = true;
    Progression.BuildRevision = 9;
    Progression.CatalogDigest.Bytes[0] = 0xAB;
    Progression.bHasOffer = true;
    Progression.Offer.OfferId = FGuid(0xF2, 0, 0, 1);
    Progression.Offer.OfferRevision = 4;
    FLDChoiceOption Learn;
    Learn.OptionId = FGuid(0xF2, 0, 0, 2);
    Learn.Kind = ELDChoiceOptionKind::LearnActive;
    Learn.DefinitionId = TEXT("active_not_mad");
    Learn.ToRank = 1;
    Progression.Offer.Options = {Learn};
    FLDLearnedSkillView Bolt;
    Bolt.DefinitionId = TEXT("active_snark_bolt");
    Bolt.bActive = true;
    Bolt.Rank = 2;
    Bolt.Slot = 1;
    Progression.Skills = {Bolt};
    FLDOpponentBuildIntent Choose;
    Choose.Kind = ELDOpponentBuildKind::Choose;
    Choose.OfferId = Progression.Offer.OfferId;
    Choose.OfferRevision = 4;
    Choose.Choice.OptionId = Learn.OptionId;
    FLDChoiceCommand Command;
    bGood &= Test.TestTrue(TEXT("A choice echoes the head offer, the current life phase and revisions: ") + Error,
        LDOpponentRequests::BuildChoice(Choose, Progression, 3, Command, Error) && Command.Generation == 7 && Command.Life == 1
        && Command.ExpectedLifePhase == ELDLifePhase::Dead && Command.RequestId == 3 && Command.OfferId == Choose.OfferId
        && Command.OfferRevision == 4 && Command.OptionId == Learn.OptionId && Command.ExpectedBuildRevision == 9
        && Command.CatalogDigest == Progression.CatalogDigest && !Command.bReplace && Command.ReplaceSlot == 0
        && Command.ExpectedOldSkillId == FLDDefinitionWire() && !Command.bReplaceConfirmed);
    Choose.Choice.bReplace = true;
    Choose.Choice.ReplaceSlot = ELDOpponentSkillSlot::W;
    Choose.Choice.ReplacedSkillId = TEXT("active_snark_bolt");
    Choose.Choice.ReplacedRank = 2;
    FName Decoded;
    bGood &= Test.TestTrue(TEXT("A full-slot learn names the exact held victim"),
        LDOpponentRequests::BuildChoice(Choose, Progression, 3, Command, Error) && Command.bReplace && Command.ReplaceSlot == 1
        && Command.ExpectedOldSkillId.Decode(Decoded) && Decoded == FName(TEXT("active_snark_bolt")) && Command.ExpectedOldRank == 2
        && Command.bReplaceConfirmed);
    Choose.Choice.ReplacedRank = 1;
    bGood &= Test.TestFalse(TEXT("A victim at another rank is stale"), LDOpponentRequests::BuildChoice(Choose, Progression, 3, Command, Error));
    Choose.Choice = FLDOpponentChoicePick();
    Choose.Choice.OptionId = Learn.OptionId;
    Choose.OfferRevision = 5;
    bGood &= Test.TestFalse(TEXT("A stale offer revision is refused"), LDOpponentRequests::BuildChoice(Choose, Progression, 3, Command, Error));
    Choose.OfferRevision = 4;
    Choose.Choice.OptionId = FGuid(0xF2, 0, 0, 9);
    bGood &= Test.TestFalse(TEXT("An option outside the head offer is refused"),
        LDOpponentRequests::BuildChoice(Choose, Progression, 3, Command, Error));
    Choose.Choice.OptionId = Learn.OptionId;
    bGood &= Test.TestFalse(TEXT("Request id 0 is never sent"), LDOpponentRequests::BuildChoice(Choose, Progression, 0, Command, Error));

    FLDWorldRevision World;
    World.MapId = TEXT("demo_world");
    World.LayoutRevision = 2;
    World.GeometryRevision = 3;
    FLDShopOpenCommand Open;
    bGood &= Test.TestTrue(TEXT("Opening names the public shop and the current world revision"),
        LDOpponentRequests::BuildShopOpen(TEXT("shop"), 7, 1, World, Open, Error) && Open.Generation == 7 && Open.Life == 1
        && Open.ShopId.Decode(Decoded) && Decoded == FName(TEXT("shop")) && Open.WorldMapId.Decode(Decoded)
        && Decoded == FName(TEXT("demo_world")) && Open.WorldLayoutRevision == 2 && Open.WorldGeometryRevision == 3);
    bGood &= Test.TestFalse(TEXT("An unregistered world is refused"),
        LDOpponentRequests::BuildShopOpen(TEXT("shop"), 7, 1, FLDWorldRevision(), Open, Error));
    bGood &= Test.TestFalse(TEXT("An id outside the wire alphabet is refused"),
        LDOpponentRequests::BuildShopOpen(TEXT("bad-id"), 7, 1, World, Open, Error))
        && Test.TestTrue(TEXT("The refusal names the printed id so a build-specific casing is diagnosable"),
            Error.Contains(TEXT("'bad-id'")));

    Progression.LifePhase = ELDLifePhase::Alive;
    FLDEquipmentView Equipment;
    Equipment.bValid = true;
    Equipment.Generation = 7;
    Equipment.Gold = 500;
    Equipment.AccountRevision = 5;
    Equipment.InventoryRevision = 6;
    FLDShopOpenResult Shop;
    Shop.Generation = 7;
    Shop.Life = 1;
    Shop.OpenStatus = ELDShopOpenStatus::Opened;
    Shop.bHasView = true;
    Shop.View.SessionId = FGuid(0xA5, 0, 0, 1);
    Shop.View.Life = 1;
    Shop.View.QuoteRevision = 2;
    Shop.View.CatalogDigest = Progression.CatalogDigest;
    Shop.View.WorldMapId = TEXT("demo_world");
    Shop.View.WorldLayoutRevision = 2;
    Shop.View.WorldGeometryRevision = 3;
    FLDShopItemView Blade;
    Blade.DefinitionId = TEXT("item_edge_blade");
    Blade.BuyPrice = 320;
    Shop.View.Items = {Blade};
    FLDShopSellView Plate;
    Plate.SlotIndex = 1;
    Plate.Instance = FGuid(0xA6, 0, 0, 1);
    Plate.ItemRevision = 4;
    Plate.DefinitionId = TEXT("item_iron_plate");
    Plate.SellPrice = 201;
    Shop.View.Sellable = {Plate};
    FLDOpponentBuildIntent Buy;
    Buy.Kind = ELDOpponentBuildKind::Buy;
    Buy.BuyItemId = TEXT("item_edge_blade");
    FLDShopCommand Trade;
    bGood &= Test.TestTrue(TEXT("Buy takes the shown quote and the current owner revisions: ") + Error,
        LDOpponentRequests::BuildTrade(Buy, Shop, Equipment, Progression, 2, Trade, Error) && Trade.Kind == ELDShopCommandKind::Buy
        && Trade.Generation == 7 && Trade.Life == 1 && Trade.RequestId == 2 && Trade.SessionId == Shop.View.SessionId
        && Trade.QuoteRevision == 2 && Trade.CatalogDigest == Progression.CatalogDigest && Trade.DefinitionId.Decode(Decoded)
        && Decoded == FName(TEXT("item_edge_blade")) && Trade.BuyPrice == 320 && Trade.ExpectedAccountRevision == 5
        && Trade.ExpectedInventoryRevision == 6 && Trade.ExpectedBuildRevision == 9 && Trade.WorldMapId.Decode(Decoded)
        && Decoded == FName(TEXT("demo_world")) && Trade.WorldLayoutRevision == 2 && Trade.WorldGeometryRevision == 3
        && !Trade.Instance.IsValid() && Trade.SellPrice == 0);
    FLDOpponentBuildIntent Sell;
    Sell.Kind = ELDOpponentBuildKind::Sell;
    Sell.SellSlot = 1;
    bGood &= Test.TestTrue(TEXT("Sell names the exact instance revision at the shown price"),
        LDOpponentRequests::BuildTrade(Sell, Shop, Equipment, Progression, 3, Trade, Error) && Trade.Kind == ELDShopCommandKind::Sell
        && Trade.Instance == Plate.Instance && Trade.ExpectedItemRevision == 4 && Trade.SellPrice == 201 && Trade.BuyPrice == 0
        && Trade.DefinitionId == FLDDefinitionWire());
    Sell.SellSlot = 0;
    bGood &= Test.TestFalse(TEXT("A slot without a sellable instance is refused"),
        LDOpponentRequests::BuildTrade(Sell, Shop, Equipment, Progression, 3, Trade, Error));
    Buy.BuyItemId = TEXT("item_echo_maul");
    bGood &= Test.TestFalse(TEXT("An item outside the shown quote is never bought at a catalog price"),
        LDOpponentRequests::BuildTrade(Buy, Shop, Equipment, Progression, 2, Trade, Error));
    Buy.BuyItemId = TEXT("item_edge_blade");
    auto Pending = Shop;
    Pending.OpenStatus = ELDShopOpenStatus::Pending;
    Pending.bHasView = false;
    bGood &= Test.TestFalse(TEXT("No trade while the open is only pending"),
        LDOpponentRequests::BuildTrade(Buy, Pending, Equipment, Progression, 2, Trade, Error));
    auto OtherLife = Shop;
    OtherLife.View.Life = 2;
    bGood &= Test.TestFalse(TEXT("A session of another life never trades"),
        LDOpponentRequests::BuildTrade(Buy, OtherLife, Equipment, Progression, 2, Trade, Error));
    Progression.LifePhase = ELDLifePhase::Dead;
    bGood &= Test.TestFalse(TEXT("No trade while dead"), LDOpponentRequests::BuildTrade(Buy, Shop, Equipment, Progression, 2, Trade, Error));

    const auto OrderAt = [](ELDOrderStatus Status, ELDOrderFailure Failure)
    {
        FLDOrderReceipt Out;
        Out.Status = Status;
        Out.Failure = Failure;
        return LDOpponentRequests::SettleOrder(Out);
    };
    bGood &= Test.TestTrue(TEXT("Order receipts settle by the recovery table"),
        OrderAt(ELDOrderStatus::Queued, ELDOrderFailure::None) == ESettle::InFlight
        && OrderAt(ELDOrderStatus::Progress, ELDOrderFailure::None) == ESettle::InFlight
        && OrderAt(ELDOrderStatus::Completed, ELDOrderFailure::None) == ESettle::Succeeded
        && OrderAt(ELDOrderStatus::Completed, ELDOrderFailure::TargetDied) == ESettle::TargetLost
        && OrderAt(ELDOrderStatus::Cancelled, ELDOrderFailure::TargetUnavailable) == ESettle::TargetLost
        && OrderAt(ELDOrderStatus::Cancelled, ELDOrderFailure::TargetDied) == ESettle::TargetLost
        && OrderAt(ELDOrderStatus::Rejected, ELDOrderFailure::NoCompletePath) == ESettle::Failed
        && OrderAt(ELDOrderStatus::Cancelled, ELDOrderFailure::PathBlocked) == ESettle::Failed
        && OrderAt(ELDOrderStatus::Rejected, ELDOrderFailure::StaleOrder) == ESettle::Failed
        && OrderAt(ELDOrderStatus::Cancelled, ELDOrderFailure::Superseded) == ESettle::Dropped
        && OrderAt(ELDOrderStatus::Cancelled, ELDOrderFailure::Stopped) == ESettle::Dropped
        && OrderAt(ELDOrderStatus::Rejected, ELDOrderFailure::Dead) == ESettle::Dropped
        && OrderAt(ELDOrderStatus::Rejected, ELDOrderFailure::MatchEnded) == ESettle::Dropped);
    const auto ActionAt = [](ELDCombatPhase Phase, ELDCombatReject Reject)
    {
        FLDCombatActionReceipt Out;
        Out.Phase = Phase;
        Out.Reject = Reject;
        return LDOpponentRequests::SettleAction(Out);
    };
    bGood &= Test.TestTrue(TEXT("Action receipts settle by the recovery table"),
        ActionAt(ELDCombatPhase::Queued, ELDCombatReject::None) == ESettle::InFlight
        && ActionAt(ELDCombatPhase::Accepted, ELDCombatReject::None) == ESettle::InFlight
        && ActionAt(ELDCombatPhase::Finished, ELDCombatReject::None) == ESettle::Succeeded
        && ActionAt(ELDCombatPhase::Rejected, ELDCombatReject::TargetUnavailable) == ESettle::TargetLost
        && ActionAt(ELDCombatPhase::Rejected, ELDCombatReject::Cooldown) == ESettle::Failed
        && ActionAt(ELDCombatPhase::Rejected, ELDCombatReject::InsufficientMana) == ESettle::Failed
        && ActionAt(ELDCombatPhase::Rejected, ELDCombatReject::StaleSource) == ESettle::Failed
        && ActionAt(ELDCombatPhase::Rejected, ELDCombatReject::Cancelled) == ESettle::Dropped
        && ActionAt(ELDCombatPhase::Rejected, ELDCombatReject::Dead) == ESettle::Dropped
        && ActionAt(ELDCombatPhase::Rejected, ELDCombatReject::MatchClosed) == ESettle::Dropped);

    FLDSubmitResult Submit;
    Submit.Family = ELDGrowthFamily::Choice;
    Submit.Status = ELDAdmissionStatus::AcceptedPending;
    bGood &= Test.TestTrue(TEXT("An accepted pending submit stays in flight"), LDOpponentRequests::SettleSubmit(Submit) == ESettle::InFlight);
    Submit.Status = ELDAdmissionStatus::AdmissionRejected;
    Submit.Reason = LDGrowthWire::Raw(ELDGrowthReason::OutOfOrder);
    bGood &= Test.TestTrue(TEXT("An admission refusal is a bounded failure"), LDOpponentRequests::SettleSubmit(Submit) == ESettle::Failed);
    Submit.Reason = LDGrowthWire::Raw(ELDGrowthReason::OldLife);
    bGood &= Test.TestTrue(TEXT("A request of an older life is dropped"), LDOpponentRequests::SettleSubmit(Submit) == ESettle::Dropped);
    Submit.Status = ELDAdmissionStatus::Final;
    bGood &= Test.TestTrue(TEXT("A final answer without its receipt is a failure"), LDOpponentRequests::SettleSubmit(Submit) == ESettle::Failed);
    Submit.bHasChoiceReceipt = true;
    Submit.ChoiceReceipt.Header.Outcome = ELDReceiptOutcome::Applied;
    bGood &= Test.TestTrue(TEXT("A replayed applied choice succeeded"), LDOpponentRequests::SettleSubmit(Submit) == ESettle::Succeeded);
    Submit.ChoiceReceipt.Header.Outcome = ELDReceiptOutcome::Rejected;
    Submit.ChoiceReceipt.Header.Reason = LDGrowthWire::Raw(ELDGrowthReason::BuildStale);
    bGood &= Test.TestTrue(TEXT("A stale guard is a bounded failure"), LDOpponentRequests::SettleSubmit(Submit) == ESettle::Failed);
    Submit.ChoiceReceipt.Header.Reason = LDGrowthWire::Raw(ELDGrowthReason::OfferAlreadyCommitted);
    bGood &= Test.TestTrue(TEXT("An already consumed offer is re-read, not retried"), LDOpponentRequests::SettleSubmit(Submit) == ESettle::Dropped);
    FLDChoiceReceiptQueryResult ChoiceQuery;
    ChoiceQuery.Lookup = ELDReceiptLookup::Pending;
    bGood &= Test.TestTrue(TEXT("A pending lookup stays in flight"), LDOpponentRequests::SettleChoiceQuery(ChoiceQuery) == ESettle::InFlight);
    ChoiceQuery.Lookup = ELDReceiptLookup::Final;
    ChoiceQuery.bHasReceipt = true;
    ChoiceQuery.Receipt.Header.Outcome = ELDReceiptOutcome::Applied;
    bGood &= Test.TestTrue(TEXT("A final applied lookup succeeded"), LDOpponentRequests::SettleChoiceQuery(ChoiceQuery) == ESettle::Succeeded);
    ChoiceQuery.Lookup = ELDReceiptLookup::ReceiptExpired;
    bGood &= Test.TestTrue(TEXT("An expired record is never read as success"), LDOpponentRequests::SettleChoiceQuery(ChoiceQuery) == ESettle::Dropped);
    FLDShopReceiptQueryResult ShopQuery;
    ShopQuery.Lookup = ELDReceiptLookup::Final;
    ShopQuery.bHasReceipt = true;
    ShopQuery.Receipt.Header.Outcome = ELDReceiptOutcome::Rejected;
    ShopQuery.Receipt.Header.Reason = LDGrowthWire::Raw(ELDGrowthReason::InsufficientGold);
    bGood &= Test.TestTrue(TEXT("A trade refused for gold is a bounded failure"), LDOpponentRequests::SettleShopQuery(ShopQuery) == ESettle::Failed);
    FLDShopOpenResult OpenAnswer;
    OpenAnswer.OpenStatus = ELDShopOpenStatus::Pending;
    bGood &= Test.TestTrue(TEXT("A pending open stays in flight"), LDOpponentRequests::SettleShopOpen(OpenAnswer) == ESettle::InFlight);
    OpenAnswer.OpenStatus = ELDShopOpenStatus::Opened;
    bGood &= Test.TestTrue(TEXT("Opened without a view is not an open shop"), LDOpponentRequests::SettleShopOpen(OpenAnswer) == ESettle::Failed);
    OpenAnswer.bHasView = true;
    bGood &= Test.TestTrue(TEXT("Opened with its view succeeded"), LDOpponentRequests::SettleShopOpen(OpenAnswer) == ESettle::Succeeded);
    OpenAnswer.OpenStatus = ELDShopOpenStatus::Rejected;
    OpenAnswer.Reason = LDGrowthWire::Raw(ELDGrowthReason::OutsideRange);
    bGood &= Test.TestTrue(TEXT("An open refused out of reach is a bounded failure"), LDOpponentRequests::SettleShopOpen(OpenAnswer) == ESettle::Failed);
    OpenAnswer.Reason = LDGrowthWire::Raw(ELDGrowthReason::Dead);
    bGood &= Test.TestTrue(TEXT("An open refused after death is dropped"), LDOpponentRequests::SettleShopOpen(OpenAnswer) == ESettle::Dropped);

    ELDOpponentOutcome Outcome = ELDOpponentOutcome::Failed;
    bGood &= Test.TestTrue(TEXT("Final settlements teach the failure memory; in-flight and dropped ones do not"),
        LDOpponentRequests::ToOutcome(ESettle::Succeeded, Outcome) && Outcome == ELDOpponentOutcome::Succeeded
        && LDOpponentRequests::ToOutcome(ESettle::TargetLost, Outcome) && Outcome == ELDOpponentOutcome::TargetLost
        && LDOpponentRequests::ToOutcome(ESettle::Failed, Outcome) && Outcome == ELDOpponentOutcome::Failed
        && !LDOpponentRequests::ToOutcome(ESettle::InFlight, Outcome) && !LDOpponentRequests::ToOutcome(ESettle::Dropped, Outcome));
    return bGood;
}

// D19: the AI uses the innate D dash to close on a fleeing rival, to escape and to reach an urgent capture defence; never
// on a camp member, while it cools, or when walking suffices. Reach comes from the authority's read of the D entry.
bool RunInnateDash(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(Test, Catalog)) { return false; }
    const auto& C = *Catalog;
    using EMode = ELDOpponentMode;
    using EBody = ELDOpponentBodyKind;
    FLDOpponentDecisionFrame Base;
    if (!BaseFrame(Test, C, Base)) { return false; }
    const auto& DashRank = C.Get().Character.Dash.Rank;
    if (!Test.TestTrue(TEXT("The approved character has one dash effect"), DashRank.Effects.Num() == 1)) { return false; }
    const auto& Distance = DashRank.Effects[0].DistanceCm;
    const double Reach = double(Distance.Numerator) / double(Distance.Denominator);
    const double Attack = double(Base.Self.Attributes.AttackRangeCm.Numerator) / double(Base.Self.Attributes.AttackRangeCm.Denominator);
    FLDOpponentSource Dash;
    Dash.Slot = ELDOpponentSourceSlot::Dash;
    Dash.DefinitionId = TEXT("innate_dash");
    Dash.bReady = true;
    Dash.RangeCm = FMath::RoundToInt32(Reach);
    Base.Self.Sources = {Dash};
    const auto IsDash = [](const FLDOpponentDecision& Decision)
    { return Decision.Body.Kind == ELDOpponentBodyKind::Activate && Decision.Body.Source == ELDOpponentSourceSlot::Dash
        && Decision.Body.TargetMode == ELDDemoTargetMode::DirectionDash; };

    // Chase: the engaged rival first stands within reach, then keeps beyond half a dash past attack range.
    FLDOpponentPolicyState Chase;
    auto Frame = Base;
    Frame.Units = {Unit(2, ELDOpponentUnitKind::Contender, Attack + 10, 0)};
    const auto Near = LDOpponentPolicy::Decide(Frame, C, Chase);
    bool bGood = Expect(Test, TEXT("A rival within half a dash of attack range is attacked, not dashed at"), Near, EMode::Engage,
        EBody::AttackUnit);
    Frame.Units[0].FootCm = FVector(Attack + .75 * Reach, 0, 0);
    const auto Fleeing = LDOpponentPolicy::Decide(Frame, C, Chase);
    bGood &= Test.TestTrue(TEXT("A fleeing engaged rival one dash away is closed with the dash toward it"),
        Fleeing.Mode == EMode::Engage && IsDash(Fleeing) && Fleeing.Body.Direction.Equals(FVector(1, 0, 0), 1.e-4));
    auto Cooling = Frame;
    Cooling.Self.Sources[0].bReady = false;
    FLDOpponentPolicyState CoolingState = Chase;
    bGood &= Test.TestFalse(TEXT("A cooling dash is never proposed"), IsDash(LDOpponentPolicy::Decide(Cooling, C, CoolingState)));
    auto Far = Frame;
    Far.Units[0].FootCm = FVector(Attack + Reach + 50, 0, 0);
    FLDOpponentPolicyState FarState = Chase;
    bGood &= Test.TestFalse(TEXT("A rival beyond one dash past attack range is walked after"),
        IsDash(LDOpponentPolicy::Decide(Far, C, FarState)));

    // Camp members are farmed on foot even when the dash is ready and would reach.
    Frame = Base;
    Frame.Self.FootCm = FVector(2000 - Attack - .75 * Reach, 100, 0);
    Frame.Units = {Unit(1, ELDOpponentUnitKind::CampMember, 2000, 100, TEXT("monster_scavenger"))};
    FLDOpponentPolicyState Farm;
    const auto Farming = LDOpponentPolicy::Decide(Frame, C, Farm);
    bGood &= Test.TestFalse(TEXT("A camp member is never chased with the dash"), IsDash(Farming));

    // Retreat: a low body seen by a threat dashes toward its escape place, away from the threat.
    Frame = Base;
    Frame.Self.HpP2 = Frame.Self.MaxHpP2 / 5;
    Frame.Units = {Unit(2, ELDOpponentUnitKind::Contender, 300, 0)};
    FLDOpponentPolicyState Escape;
    const auto Escaping = LDOpponentPolicy::Decide(Frame, C, Escape);
    bGood &= Test.TestTrue(TEXT("A low body escaping a visible threat dashes away from it"),
        Escaping.Mode == EMode::Disengage && IsDash(Escaping) && Escaping.Body.Direction.X < 0);

    // Capture: an urgent public deadline spends the dash toward the circle.
    Frame = Base;
    Frame.Public.Phase = ELDMatchPhase::CaptureOpen;
    Frame.Public.Circle = ELDOpponentCircle::Solo;
    Frame.Public.Occupier = FGuid(0xE0, 0, 0, 5);
    Frame.Public.DueAtMs = Frame.Public.NowMs + 5000;
    Frame.CapturePathCm = 3000.;
    FLDOpponentPolicyState Defence;
    const auto Urgent = LDOpponentPolicy::Decide(Frame, C, Defence);
    bGood &= Test.TestTrue(TEXT("An urgent capture defence dashes toward the circle"),
        IsDash(Urgent) && Urgent.Body.Direction.Equals(FVector(0, 1, 0), 1.e-4));
    Frame.Public.DueAtMs = Frame.Public.NowMs + 60000;
    FLDOpponentPolicyState Calm;
    bGood &= Test.TestFalse(TEXT("A capture trip that walking still makes keeps the dash"),
        IsDash(LDOpponentPolicy::Decide(Frame, C, Calm)));
    return bGood;
}

// #147: the v7 actives the cast order did not cover yet. A self area (active_seethe) is cast with the target inside its
// radius, a ground zone (active_quick_meeting) on the target's foot within range, an empowered basic without movement
// (active_triple_combo) with the target inside attack range, and a learned dash (active_bail_out) chases with its own
// reach. A source reads only its learned rank: active_not_mad cleanses a root only at its advanced rank.
bool RunV7Activations(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(Test, Catalog)) { return false; }
    const auto& C = *Catalog;
    using EMode = ELDOpponentMode;
    using EBody = ELDOpponentBodyKind;
    FLDOpponentDecisionFrame Base;
    if (!BaseFrame(Test, C, Base)) { return false; }
    const double Attack = double(Base.Self.Attributes.AttackRangeCm.Numerator) / double(Base.Self.Attributes.AttackRangeCm.Denominator);
    const auto Read = [&C](const TCHAR* Id, ELDDemoEffectOp Op, double& Out)
    {
        const FLDDemoAbilityDefinition* Ability = nullptr;
        FString Error;
        if (!C.FindAbility(Id, Ability, Error) || !Ability) { return false; }
        if (Op == ELDDemoEffectOp(0))
        {
            Out = double(Ability->Target.RangeCm.Numerator) / double(Ability->Target.RangeCm.Denominator);
            return Out > 0;
        }
        const auto* Effect = Ability->Ranks[0].Effects.FindByPredicate([Op](const FLDDemoEffectDefinition& E) { return E.GetOp() == Op; });
        if (!Effect) { return false; }
        const FLDDemoRational& Value = Op == ELDDemoEffectOp::Dash ? Effect->DistanceCm : Effect->RadiusCm;
        Out = double(Value.Numerator) / double(Value.Denominator);
        return Out > 0;
    };
    double Area = 0, ZoneRange = 0, BailReach = 0;
    if (!Test.TestTrue(TEXT("The v7 actives are approved"), Read(TEXT("active_seethe"), ELDDemoEffectOp::SelfArea, Area)
        && Read(TEXT("active_quick_meeting"), ELDDemoEffectOp(0), ZoneRange) && Read(TEXT("active_bail_out"), ELDDemoEffectOp::Dash, BailReach)))
    { return false; }
    const auto With = [&Base](const TCHAR* Id, uint8 Rank)
    {
        auto Frame = Base;
        Frame.Self.Skills = {Skill(Id, Rank, ELDOpponentSkillSlot::Q)};
        FLDOpponentSource Source;
        Source.Slot = ELDOpponentSourceSlot::Q;
        Source.DefinitionId = Id;
        Source.bReady = true;
        Frame.Self.Sources = {Source};
        return Frame;
    };
    const auto IsCast = [](const FLDOpponentDecision& Decision, ELDDemoTargetMode Mode)
    { return Decision.Body.Kind == ELDOpponentBodyKind::Activate && Decision.Body.Source == ELDOpponentSourceSlot::Q
        && Decision.Body.TargetMode == Mode; };
    bool bGood = true;

    auto Frame = With(TEXT("active_seethe"), 1);
    Frame.Units = {Unit(2, ELDOpponentUnitKind::Contender, FMath::Min(Area, Attack) - 10, 0)};
    FLDOpponentPolicyState Seethe;
    bGood &= Test.TestTrue(TEXT("A rival inside the self area is burned with a self cast"),
        IsCast(LDOpponentPolicy::Decide(Frame, C, Seethe), ELDDemoTargetMode::Self));
    Frame.Units[0].FootCm = FVector(Area + 50, 0, 0);
    bGood &= Test.TestFalse(TEXT("The engaged rival outside the self area is not"),
        IsCast(LDOpponentPolicy::Decide(Frame, C, Seethe), ELDDemoTargetMode::Self));

    Frame = With(TEXT("active_quick_meeting"), 1);
    Frame.Units = {Unit(2, ELDOpponentUnitKind::Contender, Attack - 10, 120)};
    FLDOpponentPolicyState Meeting;
    const auto Zone = LDOpponentPolicy::Decide(Frame, C, Meeting);
    bGood &= Test.TestTrue(TEXT("An engaged rival within range gets the zone on its foot"), IsCast(Zone, ELDDemoTargetMode::GroundPoint)
        && Zone.Body.Point.Equals(Frame.Units[0].FootCm, 1.e-3));
    Frame.Units[0].FootCm = FVector(ZoneRange + 50, 0, 0);
    bGood &= Test.TestFalse(TEXT("and beyond the cast range it does not"),
        IsCast(LDOpponentPolicy::Decide(Frame, C, Meeting), ELDDemoTargetMode::GroundPoint));

    Frame = With(TEXT("active_triple_combo"), 1);
    Frame.Units = {Unit(2, ELDOpponentUnitKind::Contender, Attack - 10, 0)};
    FLDOpponentPolicyState Combo;
    bGood &= Test.TestTrue(TEXT("A rival inside attack range triggers the empowered basics"),
        IsCast(LDOpponentPolicy::Decide(Frame, C, Combo), ELDDemoTargetMode::Self));
    Frame.Units[0].FootCm = FVector(Attack + 60, 0, 0);
    bGood &= Test.TestFalse(TEXT("Out of attack range the empowerment waits"),
        IsCast(LDOpponentPolicy::Decide(Frame, C, Combo), ELDDemoTargetMode::Self));

    Frame = With(TEXT("active_bail_out"), 1);
    Frame.Units = {Unit(2, ELDOpponentUnitKind::Contender, Attack + 10, 0)};
    FLDOpponentPolicyState Bail;
    bGood &= Expect(Test, TEXT("A rival within half a dash of attack range is attacked"), LDOpponentPolicy::Decide(Frame, C, Bail),
        EMode::Engage, EBody::AttackUnit);
    Frame.Units[0].FootCm = FVector(Attack + .75 * BailReach, 0, 0);
    const auto Chase = LDOpponentPolicy::Decide(Frame, C, Bail);
    bGood &= Test.TestTrue(TEXT("A learned dash chases with its own reach"), Chase.Mode == EMode::Engage
        && IsCast(Chase, ELDDemoTargetMode::DirectionDash) && Chase.Body.Direction.Equals(FVector(1, 0, 0), 1.e-4));
    Frame.Units[0].FootCm = FVector(Attack + BailReach + 50, 0, 0);
    FLDOpponentPolicyState BailFar = Bail;
    bGood &= Test.TestFalse(TEXT("but not beyond it"), IsCast(LDOpponentPolicy::Decide(Frame, C, BailFar), ELDDemoTargetMode::DirectionDash));

    // Rooted on an urgent capture defence at full health: only the advanced rank's root cleanse is worth casting.
    for (const uint8 Rank : {uint8(1), uint8(2)})
    {
        Frame = With(TEXT("active_not_mad"), Rank);
        Frame.Public.Phase = ELDMatchPhase::CaptureOpen;
        Frame.Public.Circle = ELDOpponentCircle::Solo;
        Frame.Public.Occupier = FGuid(0xE0, 0, 0, 5);
        Frame.Public.DueAtMs = Frame.Public.NowMs + 5000;
        Frame.CapturePathCm = 3000.;
        Frame.Self.MoveBlockedUntilMs = Frame.Public.NowMs + 1000;
        FLDOpponentPolicyState Rooted;
        const bool bCast = IsCast(LDOpponentPolicy::Decide(Frame, C, Rooted), ELDDemoTargetMode::Self);
        bGood &= Test.TestTrue(FString::Printf(TEXT("Rank %d not_mad cleanses the root: %s"), Rank, bCast ? TEXT("yes") : TEXT("no")),
            bCast == (Rank == 2));
    }
    return bGood;
}

// #79: §6.1 P2 outranks P6. A recovering body that can afford an improvement finishes its escape before it turns to
// the shop, however often the pursuer slips in and out of sight, and only then shops.
bool RunEscapeBeforeShop(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(Test, Catalog)) { return false; }
    const auto& C = *Catalog;
    using EMode = ELDOpponentMode;
    using EBody = ELDOpponentBodyKind;
    FLDOpponentDecisionFrame Frame;
    if (!BaseFrame(Test, C, Frame)) { return false; }
    Frame.Self.Gold = C.Get().Parameters.StartingGold;
    Frame.Self.HpP2 = Frame.Self.MaxHpP2 / 5;
    const FLDOpponentUnit Pursuer = Unit(2, ELDOpponentUnitKind::Contender, 300, 0);
    FLDOpponentPolicyState State;
    Frame.Units = {Pursuer};
    const auto Fled = LDOpponentPolicy::Decide(Frame, C, State);
    bool bGood = Expect(Test, TEXT("Below the retreat line with the approved starting gold, a visible threat still comes first"),
        Fled, EMode::Disengage, EBody::MoveGround, TEXT("l3"));
    Frame.Order.Kind = ELDOpponentOrderKind::MoveGround;
    Frame.Order.Status = ELDOpponentOrderStatus::Active;
    Frame.Order.POI = Fled.Body.POI;
    bool bKept = true;
    for (int32 Glimpse = 0; Glimpse < 6; ++Glimpse)
    {
        Frame.Units.Reset();
        if (Glimpse % 2) { Frame.Units.Add(Pursuer); }
        const auto Step = LDOpponentPolicy::Decide(Frame, C, State);
        bKept &= Expect(Test, FString::Printf(TEXT("Glimpse %d: the escape under way keeps walking instead of turning to the shop"),
            Glimpse), Step, EMode::Disengage, EBody::Keep, TEXT("l3"));
    }
    bGood &= bKept;
    Frame.Units.Reset();
    Frame.Order.Status = ELDOpponentOrderStatus::Completed;
    const auto Arrived = LDOpponentPolicy::Decide(Frame, C, State);
    bGood &= Expect(Test, TEXT("Arrived and unseen, a recovering body with an affordable improvement goes shopping"), Arrived,
        EMode::Shop, EBody::ApproachShop, TEXT("shop"));
    Frame.Order.Kind = ELDOpponentOrderKind::ApproachShop;
    Frame.Order.Status = ELDOpponentOrderStatus::Active;
    Frame.Order.POI = TEXT("shop");
    Frame.Units = {Pursuer};
    bGood &= Expect(Test, TEXT("A threat seen on the way to the shop starts a new escape"),
        LDOpponentPolicy::Decide(Frame, C, State), EMode::Disengage, EBody::MoveGround);
    return bGood;
}

// D21 (#79): a recovering body uses the public dorms. With nothing to buy it walks to the nearest one; inside it waits
// for the fast recovery before shopping and stays until full; the dorm is not a safe zone, so a threat still sends it off.
bool RunDormRecovery(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(Test, Catalog)) { return false; }
    const auto& C = *Catalog;
    using EMode = ELDOpponentMode;
    using EBody = ELDOpponentBodyKind;
    FLDOpponentDecisionFrame Base;
    if (!BaseFrame(Test, C, Base)) { return false; }
    Base.POIs.Add(POI(TEXT("dorm_nw"), ELDOpponentPOIKind::Dorm, -4000, 4000));
    Base.POIs.Add(POI(TEXT("dorm_se"), ELDOpponentPOIKind::Dorm, 4000, -4000));
    Base.Self.FootCm = FVector(500, -500, 0);
    Base.Self.HpP2 = Base.Self.MaxHpP2 / 5;
    const FVector InDorm(4100, -4000, 0);

    auto Frame = Base;
    FLDOpponentPolicyState Walk;
    bool bGood = Expect(Test, TEXT("Recovering with nothing to buy, the body walks to the nearest dorm"),
        LDOpponentPolicy::Decide(Frame, C, Walk), EMode::Disengage, EBody::MoveGround, TEXT("dorm_se"));
    Frame.Order.Kind = ELDOpponentOrderKind::MoveGround;
    Frame.Order.Status = ELDOpponentOrderStatus::Active;
    Frame.Order.POI = TEXT("dorm_se");
    bGood &= Expect(Test, TEXT("The walk to the dorm keeps its order"), LDOpponentPolicy::Decide(Frame, C, Walk),
        EMode::Disengage, EBody::Keep, TEXT("dorm_se"));

    Frame = Base;
    Frame.Self.Gold = C.Get().Parameters.StartingGold;
    FLDOpponentPolicyState Buyer;
    bGood &= Expect(Test, TEXT("Outside a dorm an affordable improvement is still bought first"),
        LDOpponentPolicy::Decide(Frame, C, Buyer), EMode::Shop, EBody::ApproachShop, TEXT("shop"));
    Frame.Self.FootCm = InDorm;
    FLDOpponentPolicyState Rester;
    const auto Resting = LDOpponentPolicy::Decide(Frame, C, Rester);
    bGood &= Expect(Test, TEXT("Inside a dorm the fast recovery comes before shopping"), Resting, EMode::Disengage, EBody::None)
        && Test.TestTrue(TEXT("Resting is a recovery decision"), Resting.Reason == ELDOpponentReason::Recover);

    // Past the resume line a body in a dorm stays until full; elsewhere it resumes at once.
    Frame.Self.HpP2 = Frame.Self.MaxHpP2 * 7 / 10;
    bGood &= Expect(Test, TEXT("Above the resume line a recovering body in a dorm stays"),
        LDOpponentPolicy::Decide(Frame, C, Rester), EMode::Disengage, EBody::None);
    auto Away = Frame;
    Away.Self.FootCm = Base.Self.FootCm;
    Away.Self.Gold = 150;
    FLDOpponentPolicyState Resumed = Rester;
    bGood &= Expect(Test, TEXT("Above the resume line outside a dorm the body farms again"),
        LDOpponentPolicy::Decide(Away, C, Resumed), EMode::Farm, EBody::MoveGround);
    Frame.Self.HpP2 = Frame.Self.MaxHpP2;
    bGood &= Test.TestTrue(TEXT("Full in a dorm, recovery ends and the body moves on"),
        LDOpponentPolicy::Decide(Frame, C, Rester).Mode != EMode::Disengage && !Rester.bRecovering);

    // While a rival holds the circle alone, a recovering body does not walk off to a corner dorm.
    Frame = Base;
    Frame.Public.Phase = ELDMatchPhase::CaptureOpen;
    Frame.Public.Circle = ELDOpponentCircle::Solo;
    Frame.Public.Occupier = FGuid(0xE0, 0, 0, 5);
    Frame.Public.DueAtMs = Frame.Public.NowMs + 60000;
    Frame.CapturePathCm = 600.;
    FLDOpponentPolicyState Held;
    const auto Waiting = LDOpponentPolicy::Decide(Frame, C, Held);
    bGood &= Test.TestTrue(FString::Printf(TEXT("A rival's solo hold keeps the recovering body from a corner dorm (mode %u body %u poi %s)"),
        uint8(Waiting.Mode), uint8(Waiting.Body.Kind), *Waiting.Body.POI.ToString()),
        Waiting.Body.POI != TEXT("dorm_se") && Waiting.Body.POI != TEXT("dorm_nw"));
    // The same holds while a rival guards its graffiti: its public guard is just as urgent to answer.
    Frame.Public.Circle = ELDOpponentCircle::Guarded;
    Frame.Public.Occupier = FGuid();
    Frame.Public.DueAtMs = 0;
    Frame.Public.Owner = FGuid(0xE0, 0, 0, 5);
    Frame.Public.GuardDueAtMs = Frame.Public.NowMs + 60000;
    FLDOpponentPolicyState Guarded;
    const auto Guarding = LDOpponentPolicy::Decide(Frame, C, Guarded);
    bGood &= Test.TestTrue(FString::Printf(TEXT("A rival's guard keeps the recovering body from a corner dorm (mode %u body %u poi %s)"),
        uint8(Guarding.Mode), uint8(Guarding.Body.Kind), *Guarding.Body.POI.ToString()),
        Guarding.Body.POI != TEXT("dorm_se") && Guarding.Body.POI != TEXT("dorm_nw"));

    Frame = Base;
    Frame.Self.FootCm = InDorm;
    Frame.Units = {Unit(2, ELDOpponentUnitKind::Contender, 4400, -4000)};
    FLDOpponentPolicyState Threatened;
    bGood &= Expect(Test, TEXT("A visible threat in the dorm sends the body away: it is not a safe zone"),
        LDOpponentPolicy::Decide(Frame, C, Threatened), EMode::Disengage, EBody::MoveGround);
    return bGood;
}

bool RunAirdrop(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(Test, Catalog)) { return false; }
    const auto& C = *Catalog;
    using EMode = ELDOpponentMode;
    using EBody = ELDOpponentBodyKind;
    FLDOpponentDecisionFrame Base;
    if (!BaseFrame(Test, C, Base)) { return false; }
    Base.POIs.Add(POI(TEXT("airdrop_s"), ELDOpponentPOIKind::Airdrop, 0, -3300));
    Base.Self.FootCm = FVector(500, -500, 0);

    auto Frame = Base;
    FLDOpponentPolicyState Farmer;
    bool bGood = Expect(Test, TEXT("Without a public drop the healthy body farms"),
        LDOpponentPolicy::Decide(Frame, C, Farmer), EMode::Farm, EBody::MoveGround);
    Frame.Public.AirdropPOI = TEXT("airdrop_s");
    FLDOpponentPolicyState Runner;
    const auto Run = LDOpponentPolicy::Decide(Frame, C, Runner);
    bGood &= Expect(Test, TEXT("D26: an announced drop draws a healthy body to its public point"), Run, EMode::Airdrop,
        EBody::MoveGround, TEXT("airdrop_s"))
        && Test.TestTrue(TEXT("Going for the drop is its own reason"), Run.Reason == ELDOpponentReason::ContestAirdrop);
    Frame.Order.Kind = ELDOpponentOrderKind::MoveGround;
    Frame.Order.Status = ELDOpponentOrderStatus::Active;
    Frame.Order.POI = TEXT("airdrop_s");
    bGood &= Expect(Test, TEXT("The walk to the drop keeps its order"), LDOpponentPolicy::Decide(Frame, C, Runner),
        EMode::Airdrop, EBody::Keep, TEXT("airdrop_s"));
    Frame = Base;
    Frame.Public.AirdropPOI = TEXT("airdrop_s");
    Frame.Self.FootCm = FVector(20, -3300, 0);
    FLDOpponentPolicyState Waiter;
    bGood &= Expect(Test, TEXT("On the point the body waits for the landing instead of re-ordering the walk"),
        LDOpponentPolicy::Decide(Frame, C, Waiter), EMode::Airdrop, EBody::None);
    Frame.Public.bAirdropLanded = true;
    bGood &= Expect(Test, TEXT("A landed drop under the body is held the same way until the authority grants it"),
        LDOpponentPolicy::Decide(Frame, C, Waiter), EMode::Airdrop, EBody::None);

    Frame = Base;
    Frame.Public.AirdropPOI = TEXT("airdrop_s");
    Frame.Self.Gold = C.Get().Parameters.StartingGold;
    FLDOpponentPolicyState Buyer;
    bGood &= Expect(Test, TEXT("An affordable improvement is still bought first"),
        LDOpponentPolicy::Decide(Frame, C, Buyer), EMode::Shop, EBody::ApproachShop, TEXT("shop"));
    Frame = Base;
    Frame.Public.AirdropPOI = TEXT("airdrop_s");
    Frame.Self.HpP2 = Frame.Self.MaxHpP2 / 5;
    FLDOpponentPolicyState Hurt;
    bGood &= Test.TestTrue(TEXT("A recovering body does not go for the drop"),
        LDOpponentPolicy::Decide(Frame, C, Hurt).Reason != ELDOpponentReason::ContestAirdrop);
    Frame = Base;
    Frame.Public.AirdropPOI = TEXT("airdrop_s");
    Frame.Public.Phase = ELDMatchPhase::CaptureOpen;
    Frame.Public.Circle = ELDOpponentCircle::Solo;
    Frame.Public.Occupier = Frame.Self.ContenderId;
    Frame.Public.DueAtMs = Frame.Public.NowMs + 60000;
    Frame.Self.FootCm = FVector(0, 3000, 0);
    FLDOpponentPolicyState Holder;
    bGood &= Expect(Test, TEXT("Holding the circle alone outranks the drop"),
        LDOpponentPolicy::Decide(Frame, C, Holder), EMode::DefendOrHold, EBody::None);
    Frame = Base;
    Frame.Public.AirdropPOI = TEXT("unknown_point");
    FLDOpponentPolicyState Stray;
    bGood &= Test.TestTrue(TEXT("A drop that names no public airdrop point is ignored"),
        LDOpponentPolicy::Decide(Frame, C, Stray).Reason != ELDOpponentReason::ContestAirdrop);

    // A stop-loss exit from the drop is not a failed contest of the circle: after more such exits than the retry policy
    // allows, an urgent public defence is still answered. The same exits from a contest of the circle do suppress it.
    const auto ExitsThenDefends = [&](EMode Prior)
    {
        FLDOpponentPolicyState S;
        auto Hurt = Base;
        Hurt.Self.HpP2 = 1;
        for (int32 Exit = 0; Exit < 5; ++Exit)
        {
            S.Mode = Prior;
            LDOpponentPolicy::Decide(Hurt, C, S);
        }
        auto Urgent = Base;
        Urgent.Public.Phase = ELDMatchPhase::CaptureOpen;
        Urgent.Public.Circle = ELDOpponentCircle::Solo;
        Urgent.Public.Occupier = FGuid(0xE0, 0, 0, 5);
        Urgent.Public.DueAtMs = Urgent.Public.NowMs + 5000;
        Urgent.CapturePathCm = 3000.;
        return LDOpponentPolicy::Decide(Urgent, C, S).Reason == ELDOpponentReason::PublicCaptureUrgent;
    };
    bGood &= Test.TestTrue(TEXT("Stop-loss exits from a drop never cost the circle: an urgent defence is still answered"),
        ExitsThenDefends(EMode::Airdrop));
    bGood &= Test.TestFalse(TEXT("Control: the same exits from a contest of the circle do stop urgent defences"),
        ExitsThenDefends(EMode::Contest));
    return bGood;
}

bool RunHuntLeader(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(Test, Catalog)) { return false; }
    const auto& C = *Catalog;
    using EMode = ELDOpponentMode;
    using EBody = ELDOpponentBodyKind;
    FLDOpponentDecisionFrame Base;
    if (!BaseFrame(Test, C, Base)) { return false; }
    Base.POIs.Add(POI(TEXT("airdrop_s"), ELDOpponentPOIKind::Airdrop, 0, -3300));
    Base.Self.FootCm = FVector(500, -500, 0);
    Base.Self.Level = uint8(C.Get().Parameters.LeaderMinLevel);
    Base.Public.Leader = FGuid(0xE0, 0, 0, 7);
    Base.Public.LeaderCircleCentreCm = FVector(1400, 1400, 0);
    auto IsHunt = [](const FLDOpponentDecision& D) { return D.Reason == ELDOpponentReason::HuntLeader; };

    auto Frame = Base;
    FLDOpponentPolicyState Hunter;
    const auto Hunt = LDOpponentPolicy::Decide(Frame, C, Hunter);
    bool bGood = Expect(Test, TEXT("D27: a strong healthy body walks to the public place nearest the leader's circle"), Hunt,
        EMode::Explore, EBody::MoveGround, TEXT("h1"))
        && Test.TestTrue(TEXT("Hunting the leader is its own reason"), IsHunt(Hunt));
    Frame = Base;
    Frame.Order.Kind = ELDOpponentOrderKind::MoveGround;
    Frame.Order.Status = ELDOpponentOrderStatus::Completed;
    Frame.Order.POI = TEXT("h1");
    FLDOpponentPolicyState Arrived;
    const auto Again = LDOpponentPolicy::Decide(Frame, C, Arrived);
    bGood &= Test.TestFalse(TEXT("D27: a hunt place just reached is not ordered again at every decision"),
        Again.Body.Kind == EBody::MoveGround && Again.Body.POI == FName(TEXT("h1")));
    // A second public place inside the circle: after h1 the hunt walks on to it, and while that walk is pending or under
    // way the place just reached is not ordered again (no turnback).
    auto Wider = Base;
    Wider.POIs.Add(POI(TEXT("s01"), ELDOpponentPOIKind::Spawn, 1400, 600));
    Frame = Wider;
    Frame.Order.Kind = ELDOpponentOrderKind::MoveGround;
    Frame.Order.Status = ELDOpponentOrderStatus::Completed;
    Frame.Order.POI = TEXT("h1");
    FLDOpponentPolicyState Onward;
    const auto Next = LDOpponentPolicy::Decide(Frame, C, Onward);
    bGood &= Expect(Test, TEXT("D27: after the nearest place the hunt walks on to the next one in the circle"), Next,
        EMode::Explore, EBody::MoveGround, TEXT("s01"));
    for (const ELDOpponentOrderStatus Walking : {ELDOpponentOrderStatus::Pending, ELDOpponentOrderStatus::Active})
    {
        Frame = Wider;
        Frame.Public.NowMs += 250;
        Frame.Order.Kind = ELDOpponentOrderKind::MoveGround;
        Frame.Order.Status = Walking;
        Frame.Order.POI = TEXT("s01");
        const auto During = LDOpponentPolicy::Decide(Frame, C, Onward);
        bGood &= Test.TestFalse(FString::Printf(TEXT("D27: the walk to the next place (status %u) is not turned back to the place just reached"),
            uint8(Walking)), During.Body.Kind == EBody::MoveGround && During.Body.POI == FName(TEXT("h1")));
    }
    Frame = Base;
    Frame.Self.Level = uint8(C.Get().Parameters.LeaderMinLevel - 1);
    FLDOpponentPolicyState Weak;
    bGood &= Test.TestFalse(TEXT("A body below the notice's minimum level does not hunt"), IsHunt(LDOpponentPolicy::Decide(Frame, C, Weak)));
    Frame = Base;
    Frame.Public.Leader = Frame.Self.ContenderId;
    FLDOpponentPolicyState Named;
    bGood &= Test.TestFalse(TEXT("The named leader does not hunt itself"), IsHunt(LDOpponentPolicy::Decide(Frame, C, Named)));
    Frame = Base;
    Frame.Self.HpP2 = Frame.Self.MaxHpP2 / 5;
    FLDOpponentPolicyState Hurt;
    bGood &= Test.TestFalse(TEXT("A hurt body does not hunt"), IsHunt(LDOpponentPolicy::Decide(Frame, C, Hurt)));
    Frame = Base;
    Frame.Public.LeaderCircleCentreCm = FVector(-3000, 0, 0);
    FLDOpponentPolicyState Shopside;
    bGood &= Test.TestFalse(TEXT("A circle with only the shop inside gives no place to hunt"),
        IsHunt(LDOpponentPolicy::Decide(Frame, C, Shopside)));
    Frame = Base;
    Frame.Public.AirdropPOI = TEXT("airdrop_s");
    FLDOpponentPolicyState Both;
    bGood &= Test.TestTrue(TEXT("A public airdrop outranks the hunt"),
        LDOpponentPolicy::Decide(Frame, C, Both).Reason == ELDOpponentReason::ContestAirdrop);
    Frame = Base;
    Frame.Public.Leader.Invalidate();
    FLDOpponentPolicyState Nobody;
    bGood &= Expect(Test, TEXT("Without a named leader the body farms"), LDOpponentPolicy::Decide(Frame, C, Nobody),
        EMode::Farm, EBody::MoveGround);
    return bGood;
}

bool RunAvoidBoss(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(Test, Catalog)) { return false; }
    const auto& C = *Catalog;
    using EMode = ELDOpponentMode;
    using EBody = ELDOpponentBodyKind;
    FLDOpponentDecisionFrame Base;
    if (!BaseFrame(Test, C, Base)) { return false; }
    Base.Self.FootCm = FVector(500, -500, 0);
    const TCHAR* Dean = *C.Get().Boss.Id;
    auto IsAvoid = [](const FLDOpponentDecision& D) { return D.Reason == ELDOpponentReason::AvoidBoss; };

    auto Frame = Base;
    Frame.Units = {Unit(9, ELDOpponentUnitKind::CampMember, 700, -400, Dean)};
    FLDOpponentPolicyState Near;
    const auto Step = LDOpponentPolicy::Decide(Frame, C, Near);
    bool bGood = Expect(Test, TEXT("D26: a healthy body inside the Boss's acquire band steps away from it"), Step,
        EMode::Disengage, EBody::MoveGround)
        && Test.TestTrue(TEXT("Stepping away from the Boss is its own reason"), IsAvoid(Step))
        && Test.TestTrue(TEXT("It walks away from the Boss, not past it"),
            FVector::DotProduct((Frame.POIs.FindByPredicate([&Step](const FLDOpponentPOI& P) { return P.Id == Step.Body.POI; })->FootCm
                - Frame.Self.FootCm).GetSafeNormal2D(), (Frame.Self.FootCm - FVector(700, -400, 0)).GetSafeNormal2D()) >= 0);
    Frame = Base;
    Frame.Units = {Unit(9, ELDOpponentUnitKind::CampMember, 2500, -500, Dean)};
    FLDOpponentPolicyState Far;
    bGood &= Test.TestFalse(TEXT("Out of the band a healthy body carries on"), IsAvoid(LDOpponentPolicy::Decide(Frame, C, Far)));
    Frame = Base;
    Frame.Units = {Unit(9, ELDOpponentUnitKind::CampMember, 700, -400, Dean)};
    Frame.Self.HpP2 = Frame.Self.MaxHpP2 / 5;
    FLDOpponentPolicyState Hurt;
    bGood &= Expect(Test, TEXT("A hurt body near the Boss escapes as recovery"), LDOpponentPolicy::Decide(Frame, C, Hurt),
        EMode::Disengage, EBody::MoveGround);
    Frame = Base;
    Frame.Units = {Unit(9, ELDOpponentUnitKind::CampMember, 200, 3100, Dean)};
    Frame.Public.Phase = ELDMatchPhase::CaptureOpen;
    Frame.Public.Circle = ELDOpponentCircle::Solo;
    Frame.Public.Occupier = Frame.Self.ContenderId;
    Frame.Public.DueAtMs = Frame.Public.NowMs + 60000;
    Frame.Self.FootCm = FVector(0, 3000, 0);
    FLDOpponentPolicyState Holder;
    bGood &= Expect(Test, TEXT("Holding the circle alone outranks stepping away"), LDOpponentPolicy::Decide(Frame, C, Holder),
        EMode::DefendOrHold, EBody::None);
    Frame = Base;
    Frame.Units = {Unit(9, ELDOpponentUnitKind::CampMember, 700, -400, TEXT("monster_scavenger"))};
    FLDOpponentPolicyState Camp;
    bGood &= Test.TestFalse(TEXT("A camp member is not the Boss"), IsAvoid(LDOpponentPolicy::Decide(Frame, C, Camp)));
    return bGood;
}

// #160: the Opponent scenario saw a body flip every decision between stepping away from the Boss and the errand that led
// back to it (Shop/Farm against AvoidBoss), a new order each time.
bool RunBossEscapeKept(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(Test, Catalog)) { return false; }
    const auto& C = *Catalog;
    using EMode = ELDOpponentMode;
    using EBody = ELDOpponentBodyKind;
    FLDOpponentDecisionFrame Base;
    if (!BaseFrame(Test, C, Base)) { return false; }
    Base.Self.FootCm = FVector(500, -500, 0);
    const TCHAR* Dean = *C.Get().Boss.Id;
    auto IsAvoid = [](const FLDOpponentDecision& D) { return D.Reason == ELDOpponentReason::AvoidBoss; };

    auto Frame = Base;
    Frame.Units = {Unit(9, ELDOpponentUnitKind::CampMember, 700, -400, Dean)};
    FLDOpponentPolicyState State;
    const auto Step = LDOpponentPolicy::Decide(Frame, C, State);
    bool bGood = Expect(Test, TEXT("Inside the band the body walks away from the Boss"), Step, EMode::Disengage, EBody::MoveGround)
        && Test.TestTrue(TEXT("The walk away is an escape from the Boss"), State.bEscapingBoss);
    const FName Retreat = Step.Body.POI;
    // The walk is under way, the body has left the band and the Boss is still in sight: without the escape this frame
    // turns back to farming (the Far case of AvoidBoss).
    auto Away = Base;
    Away.Units = {Unit(9, ELDOpponentUnitKind::CampMember, 2500, -500, Dean)};
    Away.Order.Kind = ELDOpponentOrderKind::MoveGround;
    Away.Order.POI = Retreat;
    for (const ELDOpponentOrderStatus Walking : {ELDOpponentOrderStatus::Pending, ELDOpponentOrderStatus::Active})
    {
        Frame = Away;
        Frame.Public.NowMs += 250;
        Frame.Order.Status = Walking;
        const auto During = LDOpponentPolicy::Decide(Frame, C, State);
        bGood &= Expect(Test, FString::Printf(TEXT("Out of the band the escape (status %u) walks on to its place"), uint8(Walking)),
            During, EMode::Disengage, EBody::Keep, Retreat) && Test.TestTrue(TEXT("Still an escape from the Boss"), IsAvoid(During));
    }
    FLDOpponentPolicyState Fresh;
    Frame = Away;
    Frame.Order.Status = ELDOpponentOrderStatus::Active;
    bGood &= Test.TestFalse(TEXT("A body that was not escaping carries on out of the band"), IsAvoid(LDOpponentPolicy::Decide(Frame, C, Fresh)));
    // The walk arrived: the escape is over even with the Boss in sight.
    FLDOpponentPolicyState Arrived = State;
    Frame = Away;
    Frame.Order.Status = ELDOpponentOrderStatus::Completed;
    bGood &= Test.TestFalse(TEXT("An escape ends when its walk has arrived"), IsAvoid(LDOpponentPolicy::Decide(Frame, C, Arrived)))
        && Test.TestFalse(TEXT("The arrived escape is forgotten"), Arrived.bEscapingBoss);
    // The Boss went out of sight: the errand resumes.
    FLDOpponentPolicyState Unseen = State;
    Frame = Away;
    Frame.Order.Status = ELDOpponentOrderStatus::Active;
    Frame.Units.Reset();
    bGood &= Test.TestFalse(TEXT("An escape ends once the Boss is out of sight"), IsAvoid(LDOpponentPolicy::Decide(Frame, C, Unseen)))
        && Test.TestFalse(TEXT("The unseen Boss's escape is forgotten"), Unseen.bEscapingBoss);
    // Back inside the band the body steps away again and keeps its place while that still leads away.
    FLDOpponentPolicyState Again = State;
    Frame = Base;
    Frame.Units = {Unit(9, ELDOpponentUnitKind::CampMember, 700, -400, Dean)};
    Frame.Order.Kind = ELDOpponentOrderKind::MoveGround;
    Frame.Order.Status = ELDOpponentOrderStatus::Active;
    Frame.Order.POI = Retreat;
    bGood &= Expect(Test, TEXT("Inside the band a live escape is kept, not re-ordered"), LDOpponentPolicy::Decide(Frame, C, Again),
        EMode::Disengage, EBody::Keep, Retreat);
    return bGood;
}

// #160: a body standing where navigation refuses every walk (NotGrounded) sent a new walk to another place on every
// decision, 80 body requests in 30 s, and spent every place's retry budget.
bool RunWalkRefusals(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(Test, Catalog)) { return false; }
    const auto& C = *Catalog;
    const auto& AI = C.Get().Parameters.AI;
    using EMode = ELDOpponentMode;
    using EBody = ELDOpponentBodyKind;
    FLDOpponentDecisionFrame Base;
    if (!BaseFrame(Test, C, Base)) { return false; }
    auto Goal = [](const TCHAR* POI)
    {
        FLDOpponentRetryKey Key;
        Key.Category = ELDOpponentFailure::Path;
        Key.POI = POI;
        return Key;
    };
    auto Walks = [](const FLDOpponentDecision& D)
    {
        return D.Body.Kind == EBody::MoveGround || D.Body.Kind == EBody::ApproachShop || D.Body.Kind == EBody::AttackUnit;
    };
    auto Frame = Base;
    FLDOpponentPolicyState State;
    const auto First = LDOpponentPolicy::Decide(Frame, C, State);
    bool bGood = Expect(Test, TEXT("The healthy body walks to farm"), First, EMode::Farm, EBody::MoveGround);
    LDOpponentPolicy::RecordWalkAdmission(State, First.Body.Key, true, Frame.Self, Frame.Public.NowMs, AI);
    Frame.Public.NowMs += 1;
    bGood &= Test.TestFalse(TEXT("Right after a refused walk no other walk is sent"), Walks(LDOpponentPolicy::Decide(Frame, C, State)));
    // Refusals of different places from the same standing point spend the body's walking budget.
    int64 Now = Frame.Public.NowMs;
    for (const TCHAR* Place : {TEXT("l2"), TEXT("l3"), TEXT("h1"), TEXT("s00")})
    {
        Now += AI.RetryMaxBackoffMs + 1;
        LDOpponentPolicy::RecordWalkAdmission(State, Goal(Place), true, Frame.Self, Now, AI);
    }
    Frame.Public.NowMs = Now + 60000;
    bGood &= Test.TestFalse(TEXT("After the retry budget no walk is sent from where the body stands"), Walks(LDOpponentPolicy::Decide(Frame, C, State)));
    const double Reach = double(Frame.Self.Attributes.AttackRangeCm.Numerator) / double(Frame.Self.Attributes.AttackRangeCm.Denominator);
    bGood &= Test.TestFalse(TEXT("No attack chase either"), [&]
    {
        auto Fight = Frame;
        Fight.Units = {Unit(4, ELDOpponentUnitKind::Contender, Reach * 1.5, 0)};
        FLDOpponentPolicyState Copy = State;
        return Walks(LDOpponentPolicy::Decide(Fight, C, Copy));
    }());
    bGood &= Test.TestTrue(TEXT("A target already in reach is still attacked where the body stands"), [&]
    {
        auto Fight = Frame;
        Fight.Units = {Unit(4, ELDOpponentUnitKind::Contender, Reach * .5, 0)};
        FLDOpponentPolicyState Copy = State;
        const auto Decision = LDOpponentPolicy::Decide(Fight, C, Copy);
        return Decision.Body.Kind == EBody::AttackUnit || Decision.Body.Kind == EBody::Activate;
    }());
    FLDOpponentPolicyState Moved = State;
    auto Elsewhere = Frame;
    Elsewhere.Self.FootCm += FVector(40, 0, 0);
    bGood &= Test.TestTrue(TEXT("Once the body has moved it walks again"), Walks(LDOpponentPolicy::Decide(Elsewhere, C, Moved)));
    FLDOpponentPolicyState Respawned = State;
    auto NextLife = Frame;
    NextLife.Self.Life += 1;
    bGood &= Test.TestTrue(TEXT("A new life walks again"), Walks(LDOpponentPolicy::Decide(NextLife, C, Respawned)));
    // The same place refused again is that place's own failure, not the body's.
    FLDOpponentPolicyState Same;
    Frame = Base;
    for (int32 Attempt = 0; Attempt < 4; ++Attempt)
    {
        LDOpponentPolicy::RecordWalkAdmission(Same, Goal(TEXT("l2")), true, Frame.Self, Frame.Public.NowMs, AI);
        Frame.Public.NowMs += AI.RetryMaxBackoffMs + 1;
    }
    bGood &= Test.TestTrue(TEXT("One place refused repeatedly does not stop the body walking elsewhere"), Walks(LDOpponentPolicy::Decide(Frame, C, Same)));
    // An admitted walk clears the count at once.
    FLDOpponentPolicyState Cleared;
    Frame = Base;
    LDOpponentPolicy::RecordWalkAdmission(Cleared, Goal(TEXT("l2")), true, Frame.Self, Frame.Public.NowMs, AI);
    LDOpponentPolicy::RecordWalkAdmission(Cleared, Goal(TEXT("l3")), true, Frame.Self, Frame.Public.NowMs, AI);
    LDOpponentPolicy::RecordWalkAdmission(Cleared, Goal(TEXT("l1")), false, Frame.Self, Frame.Public.NowMs, AI);
    bGood &= Test.TestTrue(TEXT("An admitted walk lets the next walk go at once"), Walks(LDOpponentPolicy::Decide(Frame, C, Cleared)));
    return bGood;
}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentAirdropTest, "Lockdown.Rules.DemoOpponentPolicy.Airdrop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentAirdropTest::RunTest(const FString& Parameters)
{
    return LDOpponentPolicyTests::RunAirdrop(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentHuntLeaderTest, "Lockdown.Rules.DemoOpponentPolicy.HuntLeader",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentHuntLeaderTest::RunTest(const FString& Parameters)
{
    return LDOpponentPolicyTests::RunHuntLeader(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentAvoidBossTest, "Lockdown.Rules.DemoOpponentPolicy.AvoidBoss",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentAvoidBossTest::RunTest(const FString& Parameters)
{
    return LDOpponentPolicyTests::RunAvoidBoss(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentBossEscapeKeptTest, "Lockdown.Rules.DemoOpponentPolicy.BossEscapeKept",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentBossEscapeKeptTest::RunTest(const FString& Parameters)
{
    return LDOpponentPolicyTests::RunBossEscapeKept(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentWalkRefusalsTest, "Lockdown.Rules.DemoOpponentPolicy.WalkRefusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentWalkRefusalsTest::RunTest(const FString& Parameters)
{
    return LDOpponentPolicyTests::RunWalkRefusals(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentDormRecoveryTest, "Lockdown.Rules.DemoOpponentPolicy.DormRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentDormRecoveryTest::RunTest(const FString& Parameters)
{
    return LDOpponentPolicyTests::RunDormRecovery(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentEscapeBeforeShopTest, "Lockdown.Rules.DemoOpponentPolicy.EscapeBeforeShop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentEscapeBeforeShopTest::RunTest(const FString& Parameters)
{
    return LDOpponentPolicyTests::RunEscapeBeforeShop(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentInnateDashTest, "Lockdown.Rules.DemoOpponentPolicy.InnateDash",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentInnateDashTest::RunTest(const FString& Parameters)
{
    return LDOpponentPolicyTests::RunInnateDash(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentV7ActivationsTest, "Lockdown.Rules.DemoOpponentPolicy.V7Activations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoOpponentV7ActivationsTest::RunTest(const FString& Parameters) { return LDOpponentPolicyTests::RunV7Activations(*this); }

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentOutcomesTest, "Lockdown.Rules.DemoOpponentPolicy.Outcomes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentOutcomesTest::RunTest(const FString& Parameters)
{
    return LDOpponentPolicyTests::RunOutcomes(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentRequestsTest, "Lockdown.Rules.DemoOpponentPolicy.Requests",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentRequestsTest::RunTest(const FString& Parameters)
{
    return LDOpponentPolicyTests::RunRequests(*this);
}

#endif
