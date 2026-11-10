#include "AI/LDOpponentPolicy.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/LDOpponentFrame.h"
#include "AI/LDOpponentRequests.h"
#include "Combat/LDAttributeProjection.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDGrowthProtocol.h"
#include "Algo/Accumulate.h"
#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"
#include "Navigation/LDOrderTypes.h"
#include "Region/LDMapGeometry.h"
#include "Tests/LDTestApprovedFixture.h"
#include "Vision/LDViewTypes.h"

namespace LDOpponentPolicyTests
{
namespace
{
// Reads the producer-exported approved_base only; there is no live-data fallback.
bool ReadApprovedFixture(FAutomationTestBase& Test, TSharedPtr<const FLDDemoContentSnapshot>& Out)
{
    if (!LDTestApprovedFixture::Read(Test, Out))
    {
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
    const auto Result = LDAttributeProjection::Project(FLDAttributeBuildValues(Level, {}), Catalog);
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
    bGood &= CheckCamp(Test, TEXT("L1 high camp: 11 hits, 7 x 61.01"),
        LDOpponentPolicy::EstimateCampCost(Input, *High, *Catalog), ELDCampCostStatus::Ok, 11370, 11, 7, 42707, 19293);
    FLDCampCostInput Ten = Input;
    Ten.CurrentHpP2 = Ten.MaxHpP2 = L10.MaxHPP2;
    Ten.Attributes = L10;
    bGood &= CheckCamp(Test, TEXT("L10 low camp uses projected attack speed and windup"),
        LDOpponentPolicy::EstimateCampCost(Ten, *Low, *Catalog), ELDCampCostStatus::Ok, 1154, 2, 1, 735, L10.MaxHPP2 - 735);
    bGood &= CheckCamp(Test, TEXT("L10 high camp"),
        LDOpponentPolicy::EstimateCampCost(Ten, *High, *Catalog), ELDCampCostStatus::Ok, 6806, 8, 4, 21176, L10.MaxHPP2 - 21176);

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

bool BaseFrame(FAutomationTestBase& Test, const FLDDemoContentSnapshot& Catalog, FLDOpponentDecisionFrame& Out);

FLDOpponentActive Active(const TCHAR* Definition, uint8 Slot)
{
    FLDOpponentActive Out;
    Out.DefinitionId = Definition;
    Out.Slot = Slot;
    return Out;
}

FLDOpponentOffer ActiveOffer(TArray<FString> Options, uint32 Id = 1, uint64 Revision = 1)
{
    FLDOpponentOffer Out;
    Out.OfferId = FGuid(0xB0, 0, 0, Id);
    Out.OfferRevision = Revision;
    Out.Ordinal = 1;
    Out.Options = MoveTemp(Options);
    return Out;
}

bool Pick(FAutomationTestBase& Test, const FLDDemoContentSnapshot& Catalog, const FString& Label,
    TArray<FLDOpponentActive> Actives, TArray<FString> Blueprints, TArray<FString> Options, const TCHAR* Expected)
{
    FLDOpponentSelf Self;
    Self.Actives = MoveTemp(Actives);
    Self.Blueprints = MoveTemp(Blueprints);
    Self.Offer = ActiveOffer(MoveTemp(Options));
    FString Out, Error;
    const bool bSelected = LDOpponentPolicy::SelectActive(Self, Catalog, Out, Error);
    return Test.TestTrue(Label + TEXT(": ") + Out + TEXT(" ") + Error, bSelected && Out == Expected);
}

// One stat row per stat in the stats' design order, with the given allocated points by stat id.
TArray<FLDOpponentStatPoints> StatRows(const FLDDemoContentSnapshot& Catalog, const TMap<FString, int32>& Points)
{
    TArray<FLDOpponentStatPoints> Out;
    for (const FLDDemoStatDefinition* Stat : Catalog.Get().StatsInDesignOrder())
    {
        FLDOpponentStatPoints Row;
        Row.StatId = Stat->Id;
        Row.Points = Points.FindRef(Stat->Id);
        Out.Add(Row);
    }
    return Out;
}

// An allocation (one count per stat in design order) by stat id.
TMap<FString, int32> ById(const FLDDemoContentSnapshot& Catalog, const TArray<uint8>& Counts)
{
    TMap<FString, int32> Out;
    const TArray<const FLDDemoStatDefinition*> Stats = Catalog.Get().StatsInDesignOrder();
    for (int32 Index = 0; Index < Stats.Num() && Index < Counts.Num(); ++Index)
    {
        if (Counts[Index] > 0) { Out.Add(Stats[Index]->Id, Counts[Index]); }
    }
    return Out;
}

bool SameCounts(const TMap<FString, int32>& Actual, const TMap<FString, int32>& Expected)
{
    if (Actual.Num() != Expected.Num()) { return false; }
    for (const auto& Pair : Expected) { if (Actual.FindRef(Pair.Key) != Pair.Value) { return false; } }
    return true;
}

bool RunChoiceOrder(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(Test, Catalog)) { return false; }
    const auto& C = *Catalog;
    // §6.4: a missing role first, then the target mode Unit > GroundPoint > DirectionCone > DirectionProjectile > Self >
    // DirectionDash, then catalog order.
    bool bGood = Pick(Test, C, TEXT("No actives: an engaging active beats roleless ones"), {}, {},
        {TEXT("active_bail_out"), TEXT("active_seethe"), TEXT("active_snark_bolt")}, TEXT("active_snark_bolt"));
    bGood &= Pick(Test, C, TEXT("Both roles missing: the target mode decides between engage and self-protection"), {}, {},
        {TEXT("active_not_mad"), TEXT("active_elbow")}, TEXT("active_elbow"));
    bGood &= Pick(Test, C, TEXT("Engagement covered by a learned active: self-protection fills the missing role"),
        {Active(TEXT("active_snark_bolt"), 0)}, {}, {TEXT("active_elbow"), TEXT("active_not_mad")}, TEXT("active_not_mad"));
    bGood &= Pick(Test, C, TEXT("A pending blueprint covers its role like a learned active"),
        {}, {TEXT("active_pants_down")}, {TEXT("active_elbow"), TEXT("active_not_mad")}, TEXT("active_not_mad"));
    bGood &= Pick(Test, C, TEXT("Roles covered: GroundPoint beats DirectionCone and DirectionProjectile"),
        {Active(TEXT("active_not_mad"), 0), Active(TEXT("active_snark_bolt"), 1)}, {},
        {TEXT("active_snark_bolt"), TEXT("active_open_mic"), TEXT("active_quick_meeting")}, TEXT("active_quick_meeting"));
    bGood &= Pick(Test, C, TEXT("Roles covered: DirectionCone beats Self and DirectionDash"),
        {Active(TEXT("active_not_mad"), 0), Active(TEXT("active_elbow"), 1)}, {},
        {TEXT("active_bail_out"), TEXT("active_triple_combo"), TEXT("active_open_mic")}, TEXT("active_open_mic"));
    bGood &= Pick(Test, C, TEXT("Roles covered: Self beats DirectionDash"),
        {Active(TEXT("active_not_mad"), 0), Active(TEXT("active_elbow"), 1)}, {},
        {TEXT("active_apology_slide"), TEXT("active_triple_combo")}, TEXT("active_triple_combo"));
    bGood &= Pick(Test, C, TEXT("The same target mode ties on catalog order"),
        {Active(TEXT("active_not_mad"), 0), Active(TEXT("active_snark_bolt"), 1)}, {},
        {TEXT("active_unplug"), TEXT("active_elbow")}, TEXT("active_elbow"));
    FLDOpponentSelf Unknown;
    Unknown.Offer = ActiveOffer({TEXT("active_unknown")});
    FString Ignored, Error;
    bGood &= Test.TestFalse(TEXT("An unknown definition is refused, never defaulted"),
        LDOpponentPolicy::SelectActive(Unknown, C, Ignored, Error));
    FLDOpponentSelf NoOffer;
    bGood &= Test.TestFalse(TEXT("Without an offer nothing is chosen"), LDOpponentPolicy::SelectActive(NoOffer, C, Ignored, Error));

    // Blueprints: the preferred one into the lowest empty slot; none while Q/W/E are full.
    FLDOpponentSelf Learner;
    Learner.Actives = {Active(TEXT("active_not_mad"), 0), Active(TEXT("active_snark_bolt"), 1)};
    Learner.Blueprints = {TEXT("active_elbow"), TEXT("active_seethe")};
    FString Blueprint;
    uint8 Slot = 0xFF;
    bGood &= Test.TestTrue(TEXT("The preferred blueprint goes to the empty E"),
        LDOpponentPolicy::SelectBlueprint(Learner, C, Blueprint, Slot, Error) && Blueprint == TEXT("active_elbow") && Slot == 2);
    Learner.Actives = {Active(TEXT("active_not_mad"), 1)};
    bGood &= Test.TestTrue(TEXT("The lowest empty slot is taken first"),
        LDOpponentPolicy::SelectBlueprint(Learner, C, Blueprint, Slot, Error) && Slot == 0);
    Learner.Actives = {Active(TEXT("active_not_mad"), 0), Active(TEXT("active_snark_bolt"), 1), Active(TEXT("active_unplug"), 2)};
    bGood &= Test.TestFalse(TEXT("Full Q/W/E: the blueprint waits for the rest-only replacement"),
        LDOpponentPolicy::SelectBlueprint(Learner, C, Blueprint, Slot, Error));
    Learner.Actives.Reset();
    Learner.Blueprints.Reset();
    bGood &= Test.TestFalse(TEXT("No blueprint: nothing to learn"), LDOpponentPolicy::SelectBlueprint(Learner, C, Blueprint, Slot, Error));

    // Points: each to the fewest-points stat among physique, thick skin, strength (the basic attack scales with AD) and
    // brains (only with a learned AP active); ties by the stats' design order (physique first); agility never.
    const auto& Stats = C.Get().Stats;
    if (!Test.TestTrue(TEXT("The approved catalog has the five #198 stats"), Stats.Num() == 5)) { return false; }
    const FString Physique(TEXT("stat_physique")), Skin(TEXT("stat_thick_skin")), Strength(TEXT("stat_strength")),
        Brains(TEXT("stat_brains")), Agility(TEXT("stat_agility"));
    FLDOpponentSelf Spender;
    Spender.StatPoints = StatRows(C, {});
    Spender.UnspentPoints = 3;
    TArray<uint8> Counts;
    bGood &= Test.TestTrue(TEXT("Without an AP active: physique, thick skin, strength"),
        LDOpponentPolicy::AllocatePoints(Spender, C, Counts, Error) && Counts.Num() == Stats.Num()
        && SameCounts(ById(C, Counts), {{Physique, 1}, {Skin, 1}, {Strength, 1}}));
    Spender.UnspentPoints = 1;
    bGood &= Test.TestTrue(TEXT("A tie goes to physique, first in the stats' design order, and the counts follow that order"),
        LDOpponentPolicy::AllocatePoints(Spender, C, Counts, Error) && Counts == TArray<uint8>({1, 0, 0, 0, 0}));
    Spender.Actives = {Active(TEXT("active_snark_bolt"), 0)};
    Spender.StatPoints = StatRows(C, {{Physique, 4}, {Skin, 3}, {Strength, 1}, {Agility, 7}});
    Spender.UnspentPoints = 4;
    // Brains (0) and strength (1) are the fewest; thick skin (3) and physique (4) stay out of reach of four points, which
    // level brains and strength to within one of each other.
    bGood &= Test.TestTrue(TEXT("An AP active adds brains; the fewest points go first and agility never"),
        LDOpponentPolicy::AllocatePoints(Spender, C, Counts, Error) && ById(C, Counts).FindRef(Agility) == 0
        && ById(C, Counts).FindRef(Physique) == 0 && ById(C, Counts).FindRef(Skin) == 0
        && ById(C, Counts).FindRef(Brains) + ById(C, Counts).FindRef(Strength) == 4
        && FMath::Abs((0 + ById(C, Counts).FindRef(Brains)) - (1 + ById(C, Counts).FindRef(Strength))) <= 1);
    Spender.UnspentPoints = 0;
    bGood &= Test.TestFalse(TEXT("No unspent points: nothing to allocate"), LDOpponentPolicy::AllocatePoints(Spender, C, Counts, Error));
    Spender.UnspentPoints = 1;
    Swap(Spender.StatPoints[0], Spender.StatPoints[1]);
    bGood &= Test.TestFalse(TEXT("Stat rows out of design order are refused"), LDOpponentPolicy::AllocatePoints(Spender, C, Counts, Error));
    Spender.StatPoints.Pop();
    bGood &= Test.TestFalse(TEXT("Missing stat rows are refused"), LDOpponentPolicy::AllocatePoints(Spender, C, Counts, Error));

    // The learn/allocate step follows the growth domain's gates: learning only in the countdown or alive inside the build
    // safe window, allocating also while dead.
    FLDOpponentDecisionFrame Frame;
    if (!BaseFrame(Test, C, Frame)) { return false; }
    Frame.Self.StatPoints = StatRows(C, {});
    Frame.Self.UnspentPoints = 2;
    Frame.Self.Blueprints = {TEXT("active_elbow")};
    Frame.Self.BuildSafeAtMs = Frame.Public.NowMs + 1;
    FLDOpponentPolicyState State;
    FLDOpponentBuildIntent Intent;
    bGood &= Test.TestFalse(TEXT("Alive inside combat: neither learn nor allocate"),
        LDOpponentPolicy::DecideFollowUpBuild(Frame, C, State, Intent));
    Frame.Self.Phase = ELDLifePhase::Dead;
    bGood &= Test.TestTrue(TEXT("Dead: points are allocated, the blueprint waits"),
        LDOpponentPolicy::DecideFollowUpBuild(Frame, C, State, Intent) && Intent.Kind == ELDOpponentBuildKind::AllocatePoints
        && ById(C, Intent.PointCounts).FindRef(Agility) == 0 && ById(C, Intent.PointCounts).FindRef(Brains) == 0
        && Algo::Accumulate(Intent.PointCounts, 0) == 2);
    Frame.Self.Phase = ELDLifePhase::Alive;
    Frame.Self.BuildSafeAtMs = Frame.Public.NowMs;
    bGood &= Test.TestTrue(TEXT("Safe: the blueprint is learned first"),
        LDOpponentPolicy::DecideFollowUpBuild(Frame, C, State, Intent) && Intent.Kind == ELDOpponentBuildKind::LearnBlueprint
        && Intent.Blueprint == TEXT("active_elbow") && Intent.Slot == 0);
    Frame.Public.Phase = ELDMatchPhase::Rest;
    Frame.Public.Timeline.Stage = ELDMatchStage::Rest;
    Frame.Public.Timeline.Step = ELDRestStep::Build;
    Frame.Self.BuildSafeAtMs = Frame.Public.NowMs + 4000;
    bGood &= Test.TestTrue(TEXT("The rest build window is always safe"),
        LDOpponentPolicy::DecideFollowUpBuild(Frame, C, State, Intent) && Intent.Kind == ELDOpponentBuildKind::LearnBlueprint);
    Frame.bBuildChangeInFlight = true;
    bGood &= Test.TestFalse(TEXT("One build request at a time"), LDOpponentPolicy::DecideFollowUpBuild(Frame, C, State, Intent));
    Frame.bBuildChangeInFlight = false;
    Frame.Public.Phase = ELDMatchPhase::Results;
    bGood &= Test.TestFalse(TEXT("Closed phase: nothing"), LDOpponentPolicy::DecideFollowUpBuild(Frame, C, State, Intent));
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
    Self.BuildRevision = 1;
    Out.Public.Phase = ELDMatchPhase::Development;
    Out.Public.NowMs = 10000;
    Out.POIs = {
        POI(TEXT("capture"), ELDOpponentPOIKind::Capture, 0, 3000),
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
    // #198: in a rest, build steps only, from the adapter's drawn time, while the rest build window is open.
    Frame = Base;
    Frame.Public.Phase = ELDMatchPhase::Rest;
    Frame.Public.Timeline.Stage = ELDMatchStage::Rest;
    Frame.Public.Timeline.Step = ELDRestStep::Build;
    Decision = Decide(Frame);
    bGood &= Expect(Test, TEXT("A rest proposes no body intent"), Decision, EMode::Stopped, EBody::None)
        && Test.TestTrue(TEXT("Nor a build change without an offer"), Decision.Build.Kind == ELDOpponentBuildKind::None);
    const FLDOpponentOffer Opening = ActiveOffer({TEXT("active_seethe"), TEXT("active_snark_bolt"), TEXT("active_bail_out")}, 0x153);
    Frame.Self.Offer = Opening;
    Frame.Self.OpeningPickAtMs = Frame.Public.NowMs + 1;
    Decision = Decide(Frame);
    bGood &= Test.TestTrue(TEXT("Before the drawn time no choice is proposed"), Decision.Build.Kind == ELDOpponentBuildKind::None);
    Frame.Public.NowMs += 1;
    Decision = Decide(Frame);
    bGood &= Expect(Test, TEXT("The opening pick never moves"), Decision, EMode::Choose, EBody::None)
        && Test.TestTrue(TEXT("The opening offer is chosen by the active preference"),
            Decision.Build.Kind == ELDOpponentBuildKind::ChooseActive && Decision.Build.OfferId == Opening.OfferId
            && Decision.Build.Option == TEXT("active_snark_bolt"));
    Frame.bBuildChangeInFlight = true;
    bGood &= Test.TestTrue(TEXT("An in-flight opening choice is not resubmitted"),
        Decide(Frame).Build.Kind == ELDOpponentBuildKind::None);

    Frame = Base;
    Frame.Self.Phase = ELDLifePhase::Dead;
    const FLDOpponentOffer Offer = ActiveOffer({TEXT("active_bail_out"), TEXT("active_snark_bolt")}, 2, 3);
    Frame.Self.Offer = Offer;
    Decision = Decide(Frame);
    bGood &= Expect(Test, TEXT("Dead: no body intent"), Decision, EMode::DeadWaiting, EBody::None)
        && Test.TestTrue(TEXT("Dead: the persistent choice is still confirmed"), Decision.Build.Kind == ELDOpponentBuildKind::ChooseActive
            && Decision.Build.OfferId == Offer.OfferId && Decision.Build.OfferRevision == 3
            && Decision.Build.Option == TEXT("active_snark_bolt"));
    Frame.bBuildChangeInFlight = true;
    bGood &= Test.TestTrue(TEXT("A choice awaiting its receipt is not proposed again"),
        Decide(Frame).Build.Kind == ELDOpponentBuildKind::None);

    Decision = Decide(Base);
    bGood &= Expect(Test, TEXT("A bare start farms the nearest feasible low camp, not a high camp"), Decision,
        EMode::Farm, EBody::MoveGround, TEXT("l1"));
    auto Shuffled = Base;
    Algo::Reverse(Shuffled.POIs);
    bGood &= Expect(Test, TEXT("Public POI order does not change the decision"), Decide(Shuffled), EMode::Farm, EBody::MoveGround,
        TEXT("l1"));

    // D10: the offer is confirmed on the first living decision and does not count as growth for high camps.
    Frame = Base;
    Frame.Self.Offer = Offer;
    Decision = Decide(Frame);
    bGood &= Expect(Test, TEXT("The offer is resolved alongside the first body intent"), Decision, EMode::Farm,
        EBody::MoveGround, TEXT("l1")) && Test.TestTrue(TEXT("The offer is confirmed at once"),
        Decision.Build.Kind == ELDOpponentBuildKind::ChooseActive && Decision.Build.OfferId == Offer.OfferId);
    Frame = Base;
    Frame.Self.OffersConsumed = 1;
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
        && Test.TestTrue(TEXT("The retreat never targets the circle as ground"), Retreat && Retreat->Kind != ELDOpponentPOIKind::Capture);
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
        Frame.Self.Actives = {Active(TEXT("active_not_mad"), 0)};
        FLDOpponentSource Guard;
        Guard.Slot = ELDOpponentSourceSlot::Q;
        Guard.DefinitionId = TEXT("active_not_mad");
        Guard.bReady = true;
        Frame.Self.Sources = {Guard};
        const auto Shielded = LDOpponentPolicy::Decide(Frame, C, Defence);
        Frame.Self.Actives.Reset();
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
        Spray.Self.Actives = {Active(TEXT("active_stare_down"), 0)};
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
    Frame.Public.Phase = ELDMatchPhase::CaptureOpen;
    Frame.Public.Circle = ELDOpponentCircle::Solo;
    Frame.Public.Occupier = FGuid(0xE0, 0, 0, 5);
    Frame.Public.DueAtMs = Frame.Public.NowMs + 5000;
    Frame.CapturePathCm = 3000.;
    Decision = Decide(Frame);
    bGood &= Expect(Test, TEXT("An urgent public solo deadline outranks farming"), Decision, EMode::DefendOrHold,
        EBody::MoveGround, TEXT("capture"))
        && Test.TestTrue(TEXT("Defence reason"), Decision.Reason == ELDOpponentReason::PublicCaptureUrgent);
    Frame.Public.Circle = ELDOpponentCircle::Empty;
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
    Explore.POIs = {POI(TEXT("capture"), ELDOpponentPOIKind::Capture, 0, 3000), POI(TEXT("s00"), ELDOpponentPOIKind::Spawn, 4000, 4000)};
    bGood &= Expect(Test, TEXT("Exploring never walks onto the capture anchor, even when it is the nearest place"), Decide(Explore),
        EMode::Explore, EBody::MoveGround, TEXT("s00"));
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
    Frame.Self.Actives = {Active(TEXT("active_snark_bolt"), 0)};
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

    // The approved ids, at synthetic positions: every camp of the catalog, one spawn, three airdrops, two dorms.
    const auto& Refs = C.Get().World;
    FLDDemoWorldGeometry World;
    World.Anchors = {Anchor(*Refs.CaptureId, ELDDemoWorldAnchorKind::Capture, 0, 3000),
        Anchor(*Refs.SpawnIds[0], ELDDemoWorldAnchorKind::Spawn, 10, 20),
        Anchor(TEXT("airdrop_ne"), ELDDemoWorldAnchorKind::Airdrop, 1940, 2670),
        Anchor(TEXT("airdrop_nw"), ELDDemoWorldAnchorKind::Airdrop, -1940, 2670),
        Anchor(TEXT("airdrop_s"), ELDDemoWorldAnchorKind::Airdrop, 0, -3300),
        // D26: Boss patrol points are not places a body goes; the projection leaves them out.
        Anchor(TEXT("boss_patrol_1"), ELDDemoWorldAnchorKind::BossWaypoint, 2400, 2400),
        Anchor(TEXT("rest_spawn_0"), ELDDemoWorldAnchorKind::RestSpawn, 6500, -500),
        Anchor(TEXT("rest_service_1"), ELDDemoWorldAnchorKind::RestService, 6700, 700),
        Anchor(TEXT("dorm_nw"), ELDDemoWorldAnchorKind::Dorm, -4400, 4400), Anchor(TEXT("dorm_se"), ELDDemoWorldAnchorKind::Dorm, 4400, -4400)};
    FString HighId, LowId, EliteId;
    for (int32 Index = 0; Index < C.Get().Camps.Num(); ++Index)
    {
        const FLDDemoCampPlacement& Camp = C.Get().Camps[Index];
        const bool bLow = Camp.MonsterId == TEXT("monster_scavenger");
        FString& Seen = bLow ? LowId : Camp.MonsterId == TEXT("monster_advisor") ? EliteId : HighId;
        if (Seen.IsEmpty()) { Seen = Camp.AnchorId; }
        World.Anchors.Add(Anchor(*Camp.AnchorId, ELDDemoWorldAnchorKind::Camp, 300 * Index, -2000, bLow ? 2 : 1));
    }
    const int32 Places = World.Anchors.Num() - 3;
    TArray<FLDOpponentPOI> POIs;
    bool bGood = Test.TestTrue(TEXT("Approved anchors and the camp roster form the public places: ") + Error,
        LDOpponentFrame::ReadPublicPOIs(World, C, POIs, Error) && POIs.Num() == Places);
    bGood &= Test.TestFalse(TEXT("Rest-only anchors never become field destinations"), POIs.ContainsByPredicate(
        [](const FLDOpponentPOI& P) { return P.Id == TEXT("rest_spawn_0") || P.Id == TEXT("rest_service_1"); }));
    const auto* DropPOI = POIs.FindByPredicate([](const FLDOpponentPOI& P) { return P.Id == TEXT("airdrop_s"); });
    bGood &= Test.TestTrue(TEXT("D26: each airdrop point is a public place"),
        DropPOI && DropPOI->Kind == ELDOpponentPOIKind::Airdrop && DropPOI->FootCm.Equals(FVector(0, -3300, 0)));
    const auto* DormPOI = POIs.FindByPredicate([](const FLDOpponentPOI& P) { return P.Id == TEXT("dorm_se"); });
    bGood &= Test.TestTrue(TEXT("D21: each dorm is public recovery ground"),
        DormPOI && DormPOI->Kind == ELDOpponentPOIKind::Dorm && DormPOI->FootCm.Equals(FVector(4400, -4400, 0)));
    const auto* High = POIs.FindByPredicate([&HighId](const FLDOpponentPOI& P) { return P.Id == FName(*HighId); });
    const auto* Low = POIs.FindByPredicate([&LowId](const FLDOpponentPOI& P) { return P.Id == FName(*LowId); });
    const auto* Elite = POIs.FindByPredicate([&EliteId](const FLDOpponentPOI& P) { return P.Id == FName(*EliteId); });
    const auto* Spawn = POIs.FindByPredicate([&Refs](const FLDOpponentPOI& P) { return P.Id == FName(*Refs.SpawnIds[0]); });
    bGood &= Test.TestTrue(TEXT("Camp tier and creature come from the public catalog"),
        High && High->Kind == ELDOpponentPOIKind::Camp && High->Tier == ELDDemoCampTier::High && High->MonsterId == TEXT("monster_guardian")
        && Low && Low->Tier == ELDDemoCampTier::Low && Low->MonsterId == TEXT("monster_scavenger")
        && Elite && Elite->Tier == ELDDemoCampTier::Elite && Elite->MonsterId == TEXT("monster_advisor")
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
    bGood &= Test.TestFalse(TEXT("A world without its capture place is refused"),
        LDOpponentFrame::ReadPublicPOIs(Without(ELDDemoWorldAnchorKind::Capture, 1), C, POIs, Error));
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
    Progression.BuildRevision = 9;
    Progression.UnspentPoints = 4;
    Progression.ActiveOpportunitiesConsumed = 2;
    Progression.BuildSafeAtMs = 12345;
    FLDActiveSlotView Bolt;
    Bolt.DefinitionId = TEXT("active_snark_bolt");
    Bolt.Slot = 0;
    Progression.Actives = {Bolt};
    Progression.PendingBlueprints = {TEXT("active_elbow")};
    for (const auto& Stat : C.Get().Stats)
    {
        FLDStatPointsView Row;
        Row.StatId = FName(*Stat.Id);
        Row.Points = Stat.Id == TEXT("stat_strength") ? 2 : 0;
        Progression.StatPoints.Add(Row);
    }
    Progression.bHasActiveOffer = true;
    Progression.ActiveOffer.OfferId = FGuid(0xF2, 0, 0, 1);
    Progression.ActiveOffer.OfferRevision = 4;
    Progression.ActiveOffer.OpportunityOrdinal = 3;
    Progression.ActiveOffer.Options = {TEXT("active_not_mad")};
    FLDOpponentSelf Self;
    const auto Expected = LDAttributeProjection::Project(FLDAttributeBuildValues(3,
        {FLDStatPointsValue(TEXT("stat_strength"), 2)}), C);
    bGood &= Test.TestTrue(TEXT("Own build maps through the shared attribute projection: ") + Error,
        Expected.IsSuccess() && LDOpponentFrame::ReadOwnBuild(Progression, C, Self, Error)
        && Self.Attributes.ADP2 == Expected.Value->Final.ADP2 && Self.Attributes.ArmorP2 == Expected.Value->Final.ArmorP2
        && Self.Attributes.AttackSpeed.Numerator * Expected.Value->Final.AttackSpeed.Denominator
            == Expected.Value->Final.AttackSpeed.Numerator * Self.Attributes.AttackSpeed.Denominator
        && Self.BuildRevision == 9 && Self.Level == 3 && Self.OffersConsumed == 2 && Self.UnspentPoints == 4
        && Self.BuildSafeAtMs == 12345 && Self.Actives.Num() == 1 && Self.Actives[0].Slot == 0
        && Self.Blueprints == TArray<FString>({TEXT("active_elbow")}) && Self.StatPoints.Num() == C.Get().Stats.Num()
        && Self.Offer.IsSet() && Self.Offer->Ordinal == 3 && Self.Offer->Options == TArray<FString>({TEXT("active_not_mad")}));
    Progression.bHasActiveOffer = false;
    bGood &= Test.TestTrue(TEXT("Without a current offer none is proposed"),
        LDOpponentFrame::ReadOwnBuild(Progression, C, Self, Error) && !Self.Offer.IsSet());
    auto Optionless = Progression;
    Optionless.bHasActiveOffer = true;
    Optionless.ActiveOffer.Options.Reset();
    bGood &= Test.TestFalse(TEXT("An offer without an option is refused"), LDOpponentFrame::ReadOwnBuild(Optionless, C, Self, Error));
    auto Unpublished = Progression;
    Unpublished.bValid = false;
    bGood &= Test.TestFalse(TEXT("An unpublished progression view is refused"), LDOpponentFrame::ReadOwnBuild(Unpublished, C, Self, Error));
    auto Levelless = Progression;
    Levelless.Level = 0;
    bGood &= Test.TestFalse(TEXT("A progression view without a level is refused"), LDOpponentFrame::ReadOwnBuild(Levelless, C, Self, Error));
    {
        // A game build prints each id with whichever spelling registered first; the editor keeps these spellings, so
        // this exercises the mapping back to the lowercase content ids that the policy compares as text.
        auto Cased = Progression;
        Cased.bHasActiveOffer = true;
        Cased.Actives[0].DefinitionId = TEXT("Active_Snark_Bolt");
        Cased.PendingBlueprints[0] = TEXT("ACTIVE_ELBOW");
        for (auto& Row : Cased.StatPoints) { if (Row.StatId == TEXT("stat_strength")) { Row.StatId = TEXT("Stat_Strength"); } }
        Cased.ActiveOffer.Options[0] = TEXT("Active_Not_Mad");
        FLDOpponentSelf CasedSelf;
        bGood &= Test.TestTrue(TEXT("Ids printed in another casing map to the lowercase content ids and the same build: ") + Error,
            Expected.IsSuccess() && LDOpponentFrame::ReadOwnBuild(Cased, C, CasedSelf, Error)
            && CasedSelf.Actives.Num() == 1 && CasedSelf.Actives[0].DefinitionId == TEXT("active_snark_bolt")
            && CasedSelf.Blueprints[0] == TEXT("active_elbow")
            && CasedSelf.Offer.IsSet() && CasedSelf.Offer->Options[0] == TEXT("active_not_mad")
            && CasedSelf.Attributes.ADP2 == Expected.Value->Final.ADP2 && CasedSelf.Attributes.ArmorP2 == Expected.Value->Final.ArmorP2);
    }
    {
        // Packaged-casing variants: canonical ids reach the same catalog entries and make exactly the canonical decisions.
        bGood &= Test.TestTrue(TEXT("Canonical ids are the lowercase content ids; an absent id is empty"),
            LDOpponentFrame::CanonicalId(FName(TEXT("Attr_Protein"))) == TEXT("attr_protein")
            && LDOpponentFrame::CanonicalId(FName(TEXT("Monster_Scavenger"))) == TEXT("monster_scavenger")
            && LDOpponentFrame::CanonicalId(NAME_None).IsEmpty());
        FLDOpponentDecisionFrame CaseBase;
        if (!BaseFrame(Test, C, CaseBase)) { return false; }
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
    Frame.Self.Actives = {Active(TEXT("active_snark_bolt"), 0)};
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
    const FLDOpponentOffer Offer = ActiveOffer({TEXT("active_elbow")}, 5, 2);
    Frame.Self.Offer = Offer;
    State = FLDOpponentPolicyState();
    Decision = LDOpponentPolicy::Decide(Frame, C, State);
    bGood &= Test.TestTrue(TEXT("A choice carries its offer key"), Decision.Build.Kind == ELDOpponentBuildKind::ChooseActive
        && KeyIs(Decision.Build.Key, EFailure::Build, NAME_None, Offer.OfferId, uint8(ELDOpponentBuildKind::ChooseActive)));
    bGood &= Test.TestTrue(TEXT("The body keeps farming alongside the choice"), Decision.Body.Kind == EBody::MoveGround
        && Decision.Body.POI == TEXT("l1"));
    const FLDOpponentRetryKey ChoiceKey = Decision.Build.Key;
    for (int64 Attempt = 0; Attempt < AI.RetryMaxAttempts; ++Attempt)
    {
        LDOpponentPolicy::RecordOutcome(State, ChoiceKey, ELDOpponentOutcome::Failed, Frame.Self, Frame.Public.NowMs, AI);
    }
    Decision = LDOpponentPolicy::Decide(Frame, C, State);
    bGood &= Test.TestTrue(TEXT("An exhausted choice is not proposed again for this offer revision; farming goes on"),
        Decision.Build.Kind == ELDOpponentBuildKind::None && Decision.Body.Kind == EBody::MoveGround && Decision.Body.POI == TEXT("l1"));
    auto Revised = Frame;
    Revised.Self.Offer->OfferRevision += 1;
    FLDOpponentPolicyState RevisedState = State;
    bGood &= Test.TestTrue(TEXT("A new offer revision gives the choice a fresh budget"),
        LDOpponentPolicy::Decide(Revised, C, RevisedState).Build.Kind == ELDOpponentBuildKind::ChooseActive);
    LDOpponentPolicy::RecordOutcome(State, ChoiceKey, ELDOpponentOutcome::Succeeded, Frame.Self, Frame.Public.NowMs, AI);
    bGood &= Test.TestTrue(TEXT("A final success clears the choice key"),
        LDOpponentPolicy::Decide(Frame, C, State).Build.Kind == ELDOpponentBuildKind::ChooseActive);
    return bGood;
}

bool RunRequests(FAutomationTestBase& Test)
{
    using EBody = ELDOpponentBodyKind;
    using ESettle = ELDOpponentSettle;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(Test, Catalog)) { return false; }
    const FLDDemoContentSnapshot& C = *Catalog;
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

    const TArray<FLDOpponentPOI> Places = {POI(TEXT("l1"), ELDOpponentPOIKind::Camp, 2000, 0, TEXT("monster_scavenger"))};
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
    Move.POI = TEXT("nowhere");
    bGood &= Test.TestFalse(TEXT("An unknown place is refused"), LDOpponentRequests::BuildOrder(Move, Places, Identity, Order, Error));
    Move.POI = TEXT("l1");
    bGood &= Test.TestFalse(TEXT("Order 0 is never a valid identity"),
        LDOpponentRequests::BuildOrder(Move, Places, FLDMoveIdentity(), Order, Error));
    FLDOpponentBodyIntent Stop;
    Stop.Kind = EBody::Stop;
    bGood &= Test.TestFalse(TEXT("Stop is a cancel, not an order"), LDOpponentRequests::BuildOrder(Stop, Places, Identity, Order, Error));
    FLDOpponentBodyIntent Activation;
    Activation.Kind = EBody::Activate;
    Activation.POI = TEXT("l1");
    bGood &= Test.TestFalse(TEXT("An activation is a combat action, not a spatial order"),
        LDOpponentRequests::BuildOrder(Activation, Places, Identity, Order, Error));

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
    Progression.BuildRevision = 9;
    Progression.CatalogDigest.Bytes[0] = 0xAB;
    Progression.bHasActiveOffer = true;
    Progression.ActiveOffer.OfferId = FGuid(0xF2, 0, 0, 1);
    Progression.ActiveOffer.OfferRevision = 4;
    Progression.ActiveOffer.Options = {TEXT("active_not_mad"), TEXT("active_elbow")};
    FLDActiveSlotView Bolt;
    Bolt.DefinitionId = TEXT("active_snark_bolt");
    Bolt.Slot = 1;
    Progression.Actives = {Bolt};
    Progression.PendingBlueprints = {TEXT("active_seethe")};
    Progression.UnspentPoints = 3;
    for (int32 Index = 0; Index < 5; ++Index)
    {
        FLDStatPointsView Row;
        Row.StatId = FName(*FString::Printf(TEXT("stat_%d"), Index));
        Progression.StatPoints.Add(Row);
    }
    FLDOpponentBuildIntent Choose;
    Choose.Kind = ELDOpponentBuildKind::ChooseActive;
    Choose.OfferId = Progression.ActiveOffer.OfferId;
    Choose.OfferRevision = 4;
    Choose.Option = TEXT("active_elbow");
    FLDBuildCommand Command;
    FName Decoded;
    bGood &= Test.TestTrue(TEXT("A choice echoes the offer, the current life phase and revisions and asks to learn now: ") + Error,
        LDOpponentRequests::BuildCommand(Choose, Progression, C, 3, Command, Error) && Command.Kind == ELDBuildCommandKind::ActiveChoice
        && Command.Generation == 7 && Command.Life == 1 && Command.ExpectedLifePhase == ELDLifePhase::Dead && Command.RequestId == 3
        && Command.OfferId == Choose.OfferId && Command.OfferRevision == 4 && Command.Option.Decode(Decoded)
        && Decoded == FName(TEXT("active_elbow")) && Command.ExpectedBuildRevision == 9 && Command.CatalogDigest == Progression.CatalogDigest
        && Command.bLearnIfPossible && Command.Slot == FLDBuildCommand::AnySlot && Command.PointCounts.IsEmpty());
    Choose.OfferRevision = 5;
    bGood &= Test.TestFalse(TEXT("A stale offer revision is refused"), LDOpponentRequests::BuildCommand(Choose, Progression, C, 3, Command, Error));
    Choose.OfferRevision = 4;
    Choose.Option = TEXT("active_pants_down");
    bGood &= Test.TestFalse(TEXT("An active outside the offer is refused"),
        LDOpponentRequests::BuildCommand(Choose, Progression, C, 3, Command, Error));
    Choose.Option = TEXT("active_elbow");
    bGood &= Test.TestFalse(TEXT("Request id 0 is never sent"), LDOpponentRequests::BuildCommand(Choose, Progression, C, 0, Command, Error));
    auto Offerless = Progression;
    Offerless.bHasActiveOffer = false;
    bGood &= Test.TestFalse(TEXT("A choice without a current offer is refused"),
        LDOpponentRequests::BuildCommand(Choose, Offerless, C, 3, Command, Error));

    FLDOpponentBuildIntent Learn;
    Learn.Kind = ELDOpponentBuildKind::LearnBlueprint;
    Learn.Blueprint = TEXT("active_seethe");
    Learn.Slot = 0;
    bGood &= Test.TestTrue(TEXT("A blueprint learn names the blueprint and its empty slot: ") + Error,
        LDOpponentRequests::BuildCommand(Learn, Progression, C, 4, Command, Error) && Command.Kind == ELDBuildCommandKind::LearnBlueprint
        && Command.Blueprint.Decode(Decoded) && Decoded == FName(TEXT("active_seethe")) && Command.Slot == 0
        && Command.ExpectedBuildRevision == 9);
    Learn.Slot = 1;
    bGood &= Test.TestFalse(TEXT("An occupied slot is refused"), LDOpponentRequests::BuildCommand(Learn, Progression, C, 4, Command, Error));
    Learn.Slot = 3;
    bGood &= Test.TestFalse(TEXT("A slot beyond E is refused"), LDOpponentRequests::BuildCommand(Learn, Progression, C, 4, Command, Error));
    Learn.Slot = 2;
    Learn.Blueprint = TEXT("active_elbow");
    bGood &= Test.TestFalse(TEXT("A blueprint no longer pending is refused"),
        LDOpponentRequests::BuildCommand(Learn, Progression, C, 4, Command, Error));

    FLDOpponentBuildIntent Allocate;
    Allocate.Kind = ELDOpponentBuildKind::AllocatePoints;
    Allocate.PointCounts = {1, 1, 1, 0, 0};
    bGood &= Test.TestTrue(TEXT("An allocation carries one count per stat: ") + Error,
        LDOpponentRequests::BuildCommand(Allocate, Progression, C, 5, Command, Error) && Command.Kind == ELDBuildCommandKind::AllocatePoints
        && Command.PointCounts == Allocate.PointCounts);
    Allocate.PointCounts = {2, 1, 1, 0, 0};
    bGood &= Test.TestFalse(TEXT("More than the unspent points is refused"),
        LDOpponentRequests::BuildCommand(Allocate, Progression, C, 5, Command, Error));
    Allocate.PointCounts = {0, 0, 0, 0, 0};
    bGood &= Test.TestFalse(TEXT("An empty allocation is refused"), LDOpponentRequests::BuildCommand(Allocate, Progression, C, 5, Command, Error));
    Allocate.PointCounts = {1, 1};
    bGood &= Test.TestFalse(TEXT("An allocation missing stats is refused"),
        LDOpponentRequests::BuildCommand(Allocate, Progression, C, 5, Command, Error));

    // #198 rest services, each built from the own latest view with its current guards; a stale subject is refused.
    FLDComponentInstanceView Hide;
    Hide.DefinitionId = TEXT("component_spiky_hide");
    Hide.bInstalled = Hide.bEverInstalled = true;
    FLDComponentInstanceView Crowd;
    Crowd.DefinitionId = TEXT("component_crowd_hit");
    Crowd.bEverInstalled = true;
    FLDComponentInstanceView Ram;
    Ram.DefinitionId = TEXT("component_ram_through");
    FLDComponentInstanceView Owe;
    Owe.DefinitionId = TEXT("component_owe_it");
    Progression.Components = {Crowd, Owe, Ram, Hide};
    FLDOpponentBuildIntent Uninstall;
    Uninstall.Kind = ELDOpponentBuildKind::UninstallComponent;
    Uninstall.Component = TEXT("component_spiky_hide");
    bGood &= Test.TestTrue(TEXT("An uninstall names the installed component with the current guards: ") + Error,
        LDOpponentRequests::BuildCommand(Uninstall, Progression, C, 6, Command, Error)
        && Command.Kind == ELDBuildCommandKind::UninstallComponent && Command.Component.Decode(Decoded)
        && Decoded == FName(TEXT("component_spiky_hide")) && Command.ExpectedBuildRevision == 9 && Command.RequestId == 6);
    Uninstall.Component = TEXT("component_crowd_hit");
    bGood &= Test.TestFalse(TEXT("A component in the inventory is not uninstalled"),
        LDOpponentRequests::BuildCommand(Uninstall, Progression, C, 6, Command, Error));
    Uninstall.Component = TEXT("component_hard_skin");
    bGood &= Test.TestFalse(TEXT("An unowned component is not uninstalled"), LDOpponentRequests::BuildCommand(Uninstall, Progression, C, 6, Command, Error));
    FLDOpponentBuildIntent Reinstall;
    Reinstall.Kind = ELDOpponentBuildKind::InstallComponent;
    Reinstall.Component = TEXT("component_crowd_hit");
    Reinstall.TargetKind = ELDComponentTargetKind::Component;
    Reinstall.TargetId = TEXT("component_spiky_hide");
    bGood &= Test.TestTrue(TEXT("A once-installed inventory component goes back on as a rest move (growth judges the gate): ") + Error,
        LDOpponentRequests::BuildCommand(Reinstall, Progression, C, 7, Command, Error) && Command.Kind == ELDBuildCommandKind::InstallComponent
        && Command.TargetKind == ELDComponentTargetKind::Component && Command.TargetDefinition.Decode(Decoded)
        && Decoded == FName(TEXT("component_spiky_hide")));
    Reinstall.Component = TEXT("component_spiky_hide");
    Reinstall.TargetKind = ELDComponentTargetKind::None;
    Reinstall.TargetId.Reset();
    bGood &= Test.TestFalse(TEXT("An installed component is not installed again"),
        LDOpponentRequests::BuildCommand(Reinstall, Progression, C, 7, Command, Error));
    FLDOpponentBuildIntent Npc;
    Npc.Kind = ELDOpponentBuildKind::BuyNpcOffer;
    bGood &= Test.TestTrue(TEXT("An NPC purchase carries only the guards: ") + Error,
        LDOpponentRequests::BuildCommand(Npc, Progression, C, 8, Command, Error) && Command.Kind == ELDBuildCommandKind::BuyNpcOffer
        && Command.ExpectedBuildRevision == 9 && Command.Life == 1 && Command.ExpectedLifePhase == ELDLifePhase::Dead
        && Command.Component == FLDDefinitionWire() && Command.Blueprint == FLDDefinitionWire() && Command.PointCounts.IsEmpty()
        && Command.Surrendered.IsEmpty());
    FLDOpponentBuildIntent Respec;
    Respec.Kind = ELDOpponentBuildKind::Respec;
    bGood &= Test.TestTrue(TEXT("A respec carries only the guards: ") + Error,
        LDOpponentRequests::BuildCommand(Respec, Progression, C, 9, Command, Error) && Command.Kind == ELDBuildCommandKind::Respec
        && Command.ExpectedBuildRevision == 9 && Command.PointCounts.IsEmpty() && Command.Component == FLDDefinitionWire());
    bGood &= Test.TestFalse(TEXT("Request id 0 is never sent for a rest service either"),
        LDOpponentRequests::BuildCommand(Respec, Progression, C, 0, Command, Error));
    FLDOpponentBuildIntent Replace;
    Replace.Kind = ELDOpponentBuildKind::ReplaceActive;
    Replace.Blueprint = TEXT("active_seethe");
    Replace.Slot = 1;
    bGood &= Test.TestTrue(TEXT("A replacement names the pending blueprint and the learned slot: ") + Error,
        LDOpponentRequests::BuildCommand(Replace, Progression, C, 10, Command, Error) && Command.Kind == ELDBuildCommandKind::ReplaceActive
        && Command.Slot == 1 && Command.Blueprint.Decode(Decoded) && Decoded == FName(TEXT("active_seethe")));
    Replace.Slot = 0;
    bGood &= Test.TestFalse(TEXT("An empty slot is learned into, never replaced"),
        LDOpponentRequests::BuildCommand(Replace, Progression, C, 10, Command, Error));
    Replace.Slot = 3;
    bGood &= Test.TestFalse(TEXT("A replacement beyond E is refused"), LDOpponentRequests::BuildCommand(Replace, Progression, C, 10, Command, Error));
    Replace.Slot = 1;
    Replace.Blueprint = TEXT("active_elbow");
    bGood &= Test.TestFalse(TEXT("A blueprint no longer pending replaces nothing"),
        LDOpponentRequests::BuildCommand(Replace, Progression, C, 10, Command, Error));
    // #198 G Dismantle: exactly GROWTH.DismantleInputCount distinct, still-owned common components.
    if (!Test.TestEqual(TEXT("The approved dismantle takes three components"), C.Get().Parameters.DismantleInputCount, int64(3)))
    {
        return false;
    }
    FLDOpponentBuildIntent Dismantle;
    Dismantle.Kind = ELDOpponentBuildKind::Dismantle;
    Dismantle.Surrendered = {TEXT("component_crowd_hit"), TEXT("component_ram_through"), TEXT("component_spiky_hide")};
    FName Second, Third;
    bGood &= Test.TestTrue(TEXT("A dismantle names the surrendered components in order: ") + Error,
        LDOpponentRequests::BuildCommand(Dismantle, Progression, C, 11, Command, Error) && Command.Kind == ELDBuildCommandKind::Dismantle
        && Command.Surrendered.Num() == 3 && Command.Surrendered[0].Decode(Decoded) && Decoded == FName(TEXT("component_crowd_hit"))
        && Command.Surrendered[1].Decode(Second) && Second == FName(TEXT("component_ram_through"))
        && Command.Surrendered[2].Decode(Third) && Third == FName(TEXT("component_spiky_hide")) && Command.ExpectedBuildRevision == 9);
    Dismantle.Surrendered = {TEXT("component_crowd_hit"), TEXT("component_ram_through")};
    bGood &= Test.TestFalse(TEXT("Fewer than the approved count is refused"), LDOpponentRequests::BuildCommand(Dismantle, Progression, C, 11, Command, Error));
    Dismantle.Surrendered = {TEXT("component_crowd_hit"), TEXT("component_crowd_hit"), TEXT("component_spiky_hide")};
    bGood &= Test.TestFalse(TEXT("A repeated component is refused"), LDOpponentRequests::BuildCommand(Dismantle, Progression, C, 11, Command, Error));
    Dismantle.Surrendered = {TEXT("component_crowd_hit"), TEXT("component_owe_it"), TEXT("component_spiky_hide")};
    bGood &= Test.TestFalse(TEXT("A scarce component is never surrendered"), LDOpponentRequests::BuildCommand(Dismantle, Progression, C, 11, Command, Error));
    Dismantle.Surrendered = {TEXT("component_crowd_hit"), TEXT("component_hard_skin"), TEXT("component_spiky_hide")};
    bGood &= Test.TestFalse(TEXT("A component no longer owned is refused"), LDOpponentRequests::BuildCommand(Dismantle, Progression, C, 11, Command, Error));
    Dismantle.Surrendered = {TEXT("component_crowd_hit"), TEXT("component_unknown"), TEXT("component_spiky_hide")};
    bGood &= Test.TestFalse(TEXT("A component outside the catalog is refused"), LDOpponentRequests::BuildCommand(Dismantle, Progression, C, 11, Command, Error));

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
    Submit.Family = ELDGrowthFamily::Build;
    Submit.Status = ELDAdmissionStatus::AcceptedPending;
    bGood &= Test.TestTrue(TEXT("An accepted pending submit stays in flight"), LDOpponentRequests::SettleSubmit(Submit) == ESettle::InFlight);
    Submit.Status = ELDAdmissionStatus::AdmissionRejected;
    Submit.Reason = LDGrowthWire::Raw(ELDGrowthReason::OutOfOrder);
    bGood &= Test.TestTrue(TEXT("An admission refusal is a bounded failure"), LDOpponentRequests::SettleSubmit(Submit) == ESettle::Failed);
    Submit.Reason = LDGrowthWire::Raw(ELDGrowthReason::OldLife);
    bGood &= Test.TestTrue(TEXT("A request of an older life is dropped"), LDOpponentRequests::SettleSubmit(Submit) == ESettle::Dropped);
    Submit.Reason = LDGrowthWire::Raw(ELDGrowthReason::NotSafe);
    bGood &= Test.TestTrue(TEXT("A closed safe window is re-read, not retried"), LDOpponentRequests::SettleSubmit(Submit) == ESettle::Dropped);
    Submit.Status = ELDAdmissionStatus::Final;
    bGood &= Test.TestTrue(TEXT("A final answer without its receipt is a failure"), LDOpponentRequests::SettleSubmit(Submit) == ESettle::Failed);
    Submit.bHasBuildReceipt = true;
    Submit.BuildReceipt.Header.Outcome = ELDReceiptOutcome::Applied;
    bGood &= Test.TestTrue(TEXT("A replayed applied build command succeeded"), LDOpponentRequests::SettleSubmit(Submit) == ESettle::Succeeded);
    Submit.BuildReceipt.Header.Outcome = ELDReceiptOutcome::Rejected;
    Submit.BuildReceipt.Header.Reason = LDGrowthWire::Raw(ELDGrowthReason::BuildStale);
    bGood &= Test.TestTrue(TEXT("A stale guard is a bounded failure"), LDOpponentRequests::SettleSubmit(Submit) == ESettle::Failed);
    Submit.BuildReceipt.Header.Reason = LDGrowthWire::Raw(ELDGrowthReason::OfferAlreadyCommitted);
    bGood &= Test.TestTrue(TEXT("An already consumed offer is re-read, not retried"), LDOpponentRequests::SettleSubmit(Submit) == ESettle::Dropped);
    FLDBuildReceiptQueryResult BuildQuery;
    BuildQuery.Lookup = ELDReceiptLookup::Pending;
    bGood &= Test.TestTrue(TEXT("A pending lookup stays in flight"), LDOpponentRequests::SettleBuildQuery(BuildQuery) == ESettle::InFlight);
    BuildQuery.Lookup = ELDReceiptLookup::Final;
    BuildQuery.bHasReceipt = true;
    BuildQuery.Receipt.Header.Outcome = ELDReceiptOutcome::Applied;
    bGood &= Test.TestTrue(TEXT("A final applied lookup succeeded"), LDOpponentRequests::SettleBuildQuery(BuildQuery) == ESettle::Succeeded);
    BuildQuery.Lookup = ELDReceiptLookup::ReceiptExpired;
    bGood &= Test.TestTrue(TEXT("An expired record is never read as success"), LDOpponentRequests::SettleBuildQuery(BuildQuery) == ESettle::Dropped);
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
    const auto& DashValues = C.Get().Character.Dash.Values;
    if (!Test.TestTrue(TEXT("The approved character has one dash effect"), DashValues.Effects.Num() == 1)) { return false; }
    const auto& Distance = DashValues.Effects[0].DistanceCm;
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
        const auto* Effect = Ability->Values.Effects.FindByPredicate([Op](const FLDDemoEffectDefinition& E) { return E.GetOp() == Op; });
        if (!Effect) { return false; }
        const FLDDemoRational& Value = Op == ELDDemoEffectOp::Dash ? Effect->DistanceCm : Effect->RadiusCm;
        Out = double(Value.Numerator) / double(Value.Denominator);
        return Out > 0;
    };
    double Area = 0, ZoneRange = 0, BailReach = 0;
    if (!Test.TestTrue(TEXT("The v7 actives are approved"), Read(TEXT("active_seethe"), ELDDemoEffectOp::SelfArea, Area)
        && Read(TEXT("active_quick_meeting"), ELDDemoEffectOp(0), ZoneRange) && Read(TEXT("active_bail_out"), ELDDemoEffectOp::Dash, BailReach)))
    { return false; }
    const auto With = [&Base](const TCHAR* Id)
    {
        auto Frame = Base;
        Frame.Self.Actives = {Active(Id, 0)};
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

    auto Frame = With(TEXT("active_seethe"));
    Frame.Units = {Unit(2, ELDOpponentUnitKind::Contender, FMath::Min(Area, Attack) - 10, 0)};
    FLDOpponentPolicyState Seethe;
    bGood &= Test.TestTrue(TEXT("A rival inside the self area is burned with a self cast"),
        IsCast(LDOpponentPolicy::Decide(Frame, C, Seethe), ELDDemoTargetMode::Self));
    Frame.Units[0].FootCm = FVector(Area + 50, 0, 0);
    bGood &= Test.TestFalse(TEXT("The engaged rival outside the self area is not"),
        IsCast(LDOpponentPolicy::Decide(Frame, C, Seethe), ELDDemoTargetMode::Self));

    Frame = With(TEXT("active_quick_meeting"));
    Frame.Units = {Unit(2, ELDOpponentUnitKind::Contender, Attack - 10, 120)};
    FLDOpponentPolicyState Meeting;
    const auto Zone = LDOpponentPolicy::Decide(Frame, C, Meeting);
    bGood &= Test.TestTrue(TEXT("An engaged rival within range gets the zone on its foot"), IsCast(Zone, ELDDemoTargetMode::GroundPoint)
        && Zone.Body.Point.Equals(Frame.Units[0].FootCm, 1.e-3));
    Frame.Units[0].FootCm = FVector(ZoneRange + 50, 0, 0);
    bGood &= Test.TestFalse(TEXT("and beyond the cast range it does not"),
        IsCast(LDOpponentPolicy::Decide(Frame, C, Meeting), ELDDemoTargetMode::GroundPoint));

    Frame = With(TEXT("active_triple_combo"));
    Frame.Units = {Unit(2, ELDOpponentUnitKind::Contender, Attack - 10, 0)};
    FLDOpponentPolicyState Combo;
    bGood &= Test.TestTrue(TEXT("A rival inside attack range triggers the empowered basics"),
        IsCast(LDOpponentPolicy::Decide(Frame, C, Combo), ELDDemoTargetMode::Self));
    Frame.Units[0].FootCm = FVector(Attack + 60, 0, 0);
    bGood &= Test.TestFalse(TEXT("Out of attack range the empowerment waits"),
        IsCast(LDOpponentPolicy::Decide(Frame, C, Combo), ELDDemoTargetMode::Self));

    Frame = With(TEXT("active_bail_out"));
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

    for (const auto& Case : {
        TPair<const TCHAR*,ELDDemoTargetMode>(TEXT("active_open_mic"),ELDDemoTargetMode::DirectionCone),
        TPair<const TCHAR*,ELDDemoTargetMode>(TEXT("active_snark_bolt"),ELDDemoTargetMode::DirectionProjectile),
        TPair<const TCHAR*,ELDDemoTargetMode>(TEXT("active_sock_shot"),ELDDemoTargetMode::DirectionProjectile),
        TPair<const TCHAR*,ELDDemoTargetMode>(TEXT("active_hose_down"),ELDDemoTargetMode::DirectionLine),
        TPair<const TCHAR*,ELDDemoTargetMode>(TEXT("active_calculus_drop"),ELDDemoTargetMode::GroundPoint),
        TPair<const TCHAR*,ELDDemoTargetMode>(TEXT("active_flyer_trap"),ELDDemoTargetMode::GroundPoint)})
    {
        Frame=With(Case.Key);
        Frame.Units={Unit(2,ELDOpponentUnitKind::Contender,Attack-10,80)};
        FLDOpponentPolicyState Expanded;
        const auto Cast=LDOpponentPolicy::Decide(Frame,C,Expanded);
        bGood &= Test.TestTrue(FString::Printf(TEXT("%s uses its approved target form"),Case.Key),
            IsCast(Cast,Case.Value)
            && (Case.Value!=ELDDemoTargetMode::DirectionCone && Case.Value!=ELDDemoTargetMode::DirectionProjectile
                && Case.Value!=ELDDemoTargetMode::DirectionLine
                || Cast.Body.Direction.Equals(Frame.Units[0].FootCm.GetSafeNormal2D(),1.e-4))
            && (Case.Value!=ELDDemoTargetMode::GroundPoint || Cast.Body.Point.Equals(Frame.Units[0].FootCm,1.e-3)));
    }

    Frame=With(TEXT("active_apology_slide"));
    Frame.Units={Unit(2,ELDOpponentUnitKind::Contender,Attack+20,0)};
    FLDOpponentPolicyState Slide;
    const auto Sliding=LDOpponentPolicy::Decide(Frame,C,Slide);
    bGood &= Test.TestTrue(TEXT("The mixed dash hits a rival just beyond basic range through its contact path"),
        IsCast(Sliding,ELDDemoTargetMode::DirectionDash) && Sliding.Body.Direction.Equals(FVector(1,0,0),1.e-4));

    // Rooted on an urgent capture defence at full health: the single-level not_mad's root cleanse is worth casting.
    Frame = With(TEXT("active_not_mad"));
    Frame.Public.Phase = ELDMatchPhase::CaptureOpen;
    Frame.Public.Circle = ELDOpponentCircle::Solo;
    Frame.Public.Occupier = FGuid(0xE0, 0, 0, 5);
    Frame.Public.DueAtMs = Frame.Public.NowMs + 5000;
    Frame.CapturePathCm = 3000.;
    Frame.Self.MoveBlockedUntilMs = Frame.Public.NowMs + 1000;
    FLDOpponentPolicyState Rooted;
    bGood &= Test.TestTrue(TEXT("not_mad cleanses the root of an urgent defender"),
        IsCast(LDOpponentPolicy::Decide(Frame, C, Rooted), ELDDemoTargetMode::Self));
    return bGood;
}

// #79: §6.1 P2. A recovering body finishes its escape however often the pursuer slips in and out of sight, then waits
// on regeneration; a threat seen again starts a new escape.
bool RunRecoveryEscapeKept(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(Test, Catalog)) { return false; }
    const auto& C = *Catalog;
    using EMode = ELDOpponentMode;
    using EBody = ELDOpponentBodyKind;
    FLDOpponentDecisionFrame Frame;
    if (!BaseFrame(Test, C, Frame)) { return false; }
    Frame.Self.HpP2 = Frame.Self.MaxHpP2 / 5;
    const FLDOpponentUnit Pursuer = Unit(2, ELDOpponentUnitKind::Contender, 300, 0);
    FLDOpponentPolicyState State;
    Frame.Units = {Pursuer};
    const auto Fled = LDOpponentPolicy::Decide(Frame, C, State);
    bool bGood = Expect(Test, TEXT("Below the retreat line a visible threat comes first"),
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
        bKept &= Expect(Test, FString::Printf(TEXT("Glimpse %d: the escape under way keeps walking instead of re-ranking or turning to an errand"),
            Glimpse), Step, EMode::Disengage, EBody::Keep, TEXT("l3"));
    }
    bGood &= bKept;
    Frame.Units.Reset();
    Frame.Order.Status = ELDOpponentOrderStatus::Completed;
    const auto Arrived = LDOpponentPolicy::Decide(Frame, C, State);
    bGood &= Expect(Test, TEXT("Arrived and unseen, the recovering body waits on regeneration"), Arrived, EMode::Disengage,
        EBody::None) && Test.TestTrue(TEXT("Waiting is a recovery decision"), Arrived.Reason == ELDOpponentReason::Recover);
    Frame.Order.Kind = ELDOpponentOrderKind::MoveGround;
    Frame.Order.Status = ELDOpponentOrderStatus::Active;
    Frame.Order.POI = TEXT("l1");
    Frame.Units = {Pursuer};
    bGood &= Expect(Test, TEXT("A threat seen on a later walk starts a new escape"),
        LDOpponentPolicy::Decide(Frame, C, State), EMode::Disengage, EBody::MoveGround);
    return bGood;
}

// D21 (#79): a recovering body uses the public dorms. Outside one it walks to the nearest; inside it waits for the fast
// recovery and stays until full; the dorm is not a safe zone, so a threat still sends it off.
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
    bool bGood = Expect(Test, TEXT("Recovering, the body walks to the nearest dorm"),
        LDOpponentPolicy::Decide(Frame, C, Walk), EMode::Disengage, EBody::MoveGround, TEXT("dorm_se"));
    Frame.Order.Kind = ELDOpponentOrderKind::MoveGround;
    Frame.Order.Status = ELDOpponentOrderStatus::Active;
    Frame.Order.POI = TEXT("dorm_se");
    bGood &= Expect(Test, TEXT("The walk to the dorm keeps its order"), LDOpponentPolicy::Decide(Frame, C, Walk),
        EMode::Disengage, EBody::Keep, TEXT("dorm_se"));

    Frame = Base;
    Frame.Self.FootCm = InDorm;
    FLDOpponentPolicyState Rester;
    const auto Resting = LDOpponentPolicy::Decide(Frame, C, Rester);
    bGood &= Expect(Test, TEXT("Inside a dorm the recovering body rests for the fast recovery"), Resting, EMode::Disengage, EBody::None)
        && Test.TestTrue(TEXT("Resting is a recovery decision"), Resting.Reason == ELDOpponentReason::Recover);

    // Past the resume line a body in a dorm stays until full; elsewhere it resumes at once.
    Frame.Self.HpP2 = Frame.Self.MaxHpP2 * 7 / 10;
    bGood &= Expect(Test, TEXT("Above the resume line a recovering body in a dorm stays"),
        LDOpponentPolicy::Decide(Frame, C, Rester), EMode::Disengage, EBody::None);
    auto Away = Frame;
    Away.Self.FootCm = Base.Self.FootCm;
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

    // #198 outer §5: an open camp marker makes its camp the first farm choice, under the ordinary camp entry rules.
    Frame = Base;
    FLDOpponentPolicyState Plain;
    bGood &= Expect(Test, TEXT("Without a marker the nearest camp is farmed"), LDOpponentPolicy::Decide(Frame, C, Plain),
        EMode::Farm, EBody::MoveGround, TEXT("l1"));
    Frame.Public.MarkedCamps = {TEXT("l3")};
    FLDOpponentPolicyState Marked;
    bGood &= Expect(Test, TEXT("An open camp marker draws the farm to its camp"), LDOpponentPolicy::Decide(Frame, C, Marked),
        EMode::Farm, EBody::MoveGround, TEXT("l3"));
    Frame.Public.MarkedCamps = {TEXT("h2")};
    FLDOpponentPolicyState Weak;
    bGood &= Expect(Test, TEXT("A body below the high-camp entry does not enter a marked High camp"),
        LDOpponentPolicy::Decide(Frame, C, Weak), EMode::Farm, EBody::MoveGround, TEXT("l1"));
    Frame = Base;
    Frame.Public.AirdropPOI = TEXT("airdrop_s");
    Frame.Public.MarkedCamps = {TEXT("l3")};
    FLDOpponentPolicyState Both;
    bGood &= Test.TestTrue(TEXT("A live airdrop outranks a camp marker"),
        LDOpponentPolicy::Decide(Frame, C, Both).Reason == ELDOpponentReason::ContestAirdrop);

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

bool RunCharms(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(Test, Catalog)) { return false; }
    const auto& C = *Catalog;
    using EMode = ELDOpponentMode;
    using EBody = ELDOpponentBodyKind;
    FLDOpponentDecisionFrame Base;
    if (!BaseFrame(Test, C, Base)) { return false; }
    Base.POIs.Add(POI(TEXT("airdrop_s"), ELDOpponentPOIKind::Airdrop, 0, -3300));
    Base.POIs.Add(POI(TEXT("airdrop_w"), ELDOpponentPOIKind::Airdrop, -2300, 400));
    Base.Self.FootCm = FVector(500, -500, 0);
    const auto IsCharm = [](const FLDOpponentDecision& D) { return D.Mode == ELDOpponentMode::Charm && D.Reason == ELDOpponentReason::TakeCharm; };

    auto Frame = Base;
    Frame.Public.Charms = {{TEXT("airdrop_s"), ELDCharmKind::Mana}};
    FLDOpponentPolicyState Full;
    bool bGood = Test.TestFalse(TEXT("#198: a full body ignores a mana charm it cannot use"),
        IsCharm(LDOpponentPolicy::Decide(Frame, C, Full)));
    Frame.Self.MpP2 = Frame.Self.MaxMpP2 / 2;
    FLDOpponentPolicyState Thirsty;
    const auto Mana = LDOpponentPolicy::Decide(Frame, C, Thirsty);
    bGood &= Expect(Test, TEXT("#198: a body short of mana walks onto the mana charm"), Mana, EMode::Charm, EBody::MoveGround,
        TEXT("airdrop_s")) && Test.TestTrue(TEXT("Taking a charm is its own reason"), IsCharm(Mana));

    Frame = Base;
    Frame.Public.Charms = {{TEXT("airdrop_s"), ELDCharmKind::MoveSpeed}, {TEXT("airdrop_w"), ELDCharmKind::MoveSpeed}};
    Frame.Self.FootCm = FVector(-2000, 0, 0);
    FLDOpponentPolicyState Nearest;
    bGood &= Expect(Test, TEXT("#198: of two usable charms the nearer point is taken"), LDOpponentPolicy::Decide(Frame, C, Nearest),
        EMode::Charm, EBody::MoveGround, TEXT("airdrop_w"));

    Frame = Base;
    Frame.Self.HpP2 = Frame.Self.MaxHpP2 / 5;
    Frame.Public.Charms = {{TEXT("airdrop_s"), ELDCharmKind::Heal}};
    FLDOpponentPolicyState Hurt;
    bGood &= Expect(Test, TEXT("#198: a recovering body takes a lying heal charm"), LDOpponentPolicy::Decide(Frame, C, Hurt),
        EMode::Charm, EBody::MoveGround, TEXT("airdrop_s"));
    Frame.Public.Charms = {{TEXT("airdrop_s"), ELDCharmKind::MoveSpeed}};
    FLDOpponentPolicyState HurtNoHeal;
    bGood &= Test.TestFalse(TEXT("A recovering body does not detour for a charm that does not heal"),
        IsCharm(LDOpponentPolicy::Decide(Frame, C, HurtNoHeal)));

    Frame = Base;
    Frame.Public.AirdropPOI = TEXT("airdrop_w");
    Frame.Public.Charms = {{TEXT("airdrop_s"), ELDCharmKind::MoveSpeed}};
    FLDOpponentPolicyState Drop;
    bGood &= Test.TestTrue(TEXT("The airdrop outranks a charm"),
        LDOpponentPolicy::Decide(Frame, C, Drop).Reason == ELDOpponentReason::ContestAirdrop);
    Frame = Base;
    Frame.Public.Charms = {{TEXT("unknown_point"), ELDCharmKind::MoveSpeed}};
    FLDOpponentPolicyState Stray;
    bGood &= Test.TestFalse(TEXT("A charm that names no public airdrop point is ignored"),
        IsCharm(LDOpponentPolicy::Decide(Frame, C, Stray)));
    return bGood;
}

bool RunRecoveryCharmEscape(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(Test, Catalog)) { return false; }
    FLDOpponentDecisionFrame Frame;
    if (!BaseFrame(Test, *Catalog, Frame)) { return false; }
    Frame.POIs.Add(POI(TEXT("retreat"), ELDOpponentPOIKind::Spawn, -2179, -4462));
    Frame.POIs.Add(POI(TEXT("dorm_sw"), ELDOpponentPOIKind::Dorm, -4250, -4250));
    Frame.POIs.Add(POI(TEXT("heal"), ELDOpponentPOIKind::Airdrop, -2384, 469));
    Frame.Public.Charms = {{TEXT("heal"), ELDCharmKind::Heal}};
    Frame.Self.FootCm = FVector(-2179, -4462, 0);
    Frame.Self.HpP2 = Frame.Self.MaxHpP2 / 5;
    Frame.Order.Kind = ELDOpponentOrderKind::MoveGround;
    Frame.Order.Status = ELDOpponentOrderStatus::Completed;
    Frame.Order.POI = TEXT("retreat");
    FLDOpponentPolicyState State;
    State.Generation = Frame.Generation;
    State.Life = Frame.Self.Life;
    State.bRecovering = true;
    State.RetreatPOI = TEXT("retreat");

    bool bGood = Expect(Test, TEXT("After reaching a retreat, a recovering body goes to the dorm rather than turning back for a charm"),
        LDOpponentPolicy::Decide(Frame, *Catalog, State), ELDOpponentMode::Disengage,
        ELDOpponentBodyKind::MoveGround, TEXT("dorm_sw"));
    Frame.Order.Status = ELDOpponentOrderStatus::Active;
    Frame.Order.POI = TEXT("dorm_sw");
    bGood &= Expect(Test, TEXT("The dorm walk remains in progress while the charm is still visible"),
        LDOpponentPolicy::Decide(Frame, *Catalog, State), ELDOpponentMode::Disengage,
        ELDOpponentBodyKind::Keep, TEXT("dorm_sw"));
    Frame.Units = {Unit(2, ELDOpponentUnitKind::Contender, -1879, -4462)};
    bGood &= Test.TestTrue(TEXT("A renewed visible threat still takes priority over recovery"),
        LDOpponentPolicy::Decide(Frame, *Catalog, State).Reason == ELDOpponentReason::Recover);

    Frame.Units.Reset();
    Frame.Order = {};
    Frame.Self.HpP2 = Frame.Self.MaxHpP2 * 7 / 10;
    bGood &= Expect(Test, TEXT("Once recovered, the usable charm is eligible again"),
        LDOpponentPolicy::Decide(Frame, *Catalog, State), ELDOpponentMode::Charm,
        ELDOpponentBodyKind::MoveGround, TEXT("heal"));

    Frame.Self.HpP2 = Frame.Self.MaxHpP2 / 5;
    Frame.Order.Kind = ELDOpponentOrderKind::MoveGround;
    Frame.Order.Status = ELDOpponentOrderStatus::Completed;
    Frame.Order.POI = TEXT("retreat");
    Frame.POIs.RemoveAll([](const FLDOpponentPOI& P) { return P.Kind == ELDOpponentPOIKind::Dorm; });
    State = {};
    State.Generation = Frame.Generation;
    State.Life = Frame.Self.Life;
    State.bRecovering = true;
    State.RetreatPOI = TEXT("retreat");
    bGood &= Expect(Test, TEXT("Without a reachable dorm, the body recovers rather than immediately retracing its escape"),
        LDOpponentPolicy::Decide(Frame, *Catalog, State), ELDOpponentMode::Disengage, ELDOpponentBodyKind::None);
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
    FLDOpponentPolicyState Empty;
    bGood &= Test.TestFalse(TEXT("A circle with no public place inside gives no place to hunt"),
        IsHunt(LDOpponentPolicy::Decide(Frame, C, Empty)));
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
// back to it (Farm against AvoidBoss), a new order each time.
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
        return D.Body.Kind == EBody::MoveGround || D.Body.Kind == EBody::AttackUnit;
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

FLDPKEntrantView PKEntrant(uint32 Handle, uint8 Side, bool bAlive = true)
{
    FLDPKEntrantView Out;
    Out.Contender = FGuid(0xE0, 0, 0, Handle);
    Out.Side = Side;
    Out.bAlive = bAlive;
    return Out;
}

FLDPKBetView PKBet(const FGuid& Spectator, uint8 Side)
{
    FLDPKBetView Out;
    Out.Spectator = Spectator;
    Out.Side = Side;
    return Out;
}

// #198 D (issue #198 comment 6752601175 §2-§6): sign-up, bet, throw, teammate exclusion and the ring, from public reads only.
bool RunPK(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(Test, Catalog)) { return false; }
    const auto& C = *Catalog;
    using EMode = ELDOpponentMode;
    using EReason = ELDOpponentReason;
    using EBody = ELDOpponentBodyKind;
    using EPK = ELDOpponentPKKind;
    FLDOpponentDecisionFrame Base;
    if (!BaseFrame(Test, C, Base)) { return false; }
    const FGuid Me = Base.Self.ContenderId;
    Base.Public.Phase = ELDMatchPhase::Rest;
    Base.Public.Timeline.Stage = ELDMatchStage::Rest;
    Base.Public.Timeline.RestIndex = 1;

    // Sign-up (站圈报名): inside the window of the build step only, the body walks into its own sign-up disk.
    auto Frame = Base;
    FLDOpponentPOI Disk;
    Disk.Id = TEXT("pk_signup_1");
    Disk.Kind = ELDOpponentPOIKind::PKSignup;
    Disk.SurfaceId = TEXT("demo_ground");
    Disk.FootCm = Base.Self.FootCm + FVector(300, 0, 0);
    Frame.SignupPOI = Disk;
    Frame.Public.Timeline.Step = ELDRestStep::Build;
    Frame.Public.Timeline.SignupOpensAtMs = 20000;
    Frame.Public.Timeline.StepEndsAtMs = 30000;
    Frame.Public.NowMs = 19999;
    bool bGood = Test.TestFalse(TEXT("No sign-up before the window opens"), LDOpponentPolicy::WantsPKSignUp(Frame.Public, Me));
    Frame.Public.NowMs = 30000;
    bGood &= Test.TestFalse(TEXT("No sign-up at the step's end"), LDOpponentPolicy::WantsPKSignUp(Frame.Public, Me));
    Frame.Public.NowMs = 20000;
    bGood &= Test.TestTrue(TEXT("Every AI signs up inside the window"), LDOpponentPolicy::WantsPKSignUp(Frame.Public, Me));
    FLDOpponentPolicyState SignUp;
    auto Decision = LDOpponentPolicy::Decide(Frame, C, SignUp);
    bGood &= Expect(Test, TEXT("Signing up walks into the own disk"), Decision, EMode::PK, EBody::MoveGround)
        && Test.TestTrue(TEXT("The walk names the own disk and no request replaces standing in it"),
            Decision.Body.POI == Disk.Id && Decision.PK.Kind == EPK::None && Decision.Reason == EReason::PKSignUp);
    Frame.Order.Status = ELDOpponentOrderStatus::Active;
    Frame.Order.Kind = ELDOpponentOrderKind::MoveGround;
    Frame.Order.POI = Disk.Id;
    Frame.Public.NowMs = 20250;
    bGood &= Test.TestTrue(TEXT("A live walk to the disk is kept, not sent again"),
        LDOpponentPolicy::Decide(Frame, C, SignUp).Body.Kind == EBody::Keep);
    Frame.Order = FLDOpponentOrder();
    Frame.Public.NowMs = 19999;
    bGood &= Test.TestTrue(TEXT("Before the window the body stays in its rest space"),
        LDOpponentPolicy::Decide(Frame, C, SignUp).Body.Kind == EBody::None);
    Frame.Public.NowMs = 20250;
    Frame.Public.Timeline.Signups = {Me};
    Decision = LDOpponentPolicy::Decide(Frame, C, SignUp);
    bGood &= Test.TestTrue(TEXT("A contender standing in its disk stays there until the lock"),
        !LDOpponentPolicy::WantsPKSignUp(Frame.Public, Me) && Decision.Body.Kind == EBody::Keep && Decision.Reason == EReason::PKSignUp);
    Frame.Order.Status = ELDOpponentOrderStatus::Active;
    Frame.Order.Kind = ELDOpponentOrderKind::AttackUnit;
    bGood &= Test.TestTrue(TEXT("Any other order a signed contender still runs stops in the disk"),
        LDOpponentPolicy::Decide(Frame, C, SignUp).Body.Kind == EBody::Stop);
    Frame.Order = FLDOpponentOrder();
    Frame.Public.Timeline.Signups.Reset();
    Frame.Public.Timeline.Step = ELDRestStep::Payout;
    bGood &= Test.TestFalse(TEXT("No sign-up outside the build step"), LDOpponentPolicy::WantsPKSignUp(Frame.Public, Me));

    // Bet: the side of the lowest-seat entrant, once, for an eligible bettor without a public bet.
    FLDPKView PK;
    PK.Generation = Base.Generation;
    PK.Serial = 2;
    PK.Format = ELDPKFormat::FreeForAll;
    PK.SideCount = 4;
    PK.Entrants = {PKEntrant(11, 2), PKEntrant(12, 1), PKEntrant(13, 3), PKEntrant(14, 4)};
    PK.Bettors = {Me};
    Frame = Base;
    Frame.Public.Timeline.Step = ELDRestStep::Betting;
    Frame.Public.PK = PK;
    bGood &= Test.TestTrue(TEXT("The bet goes to the lowest seat's side"), LDOpponentPolicy::PKBetSide(Frame.Public, Me) == 2);
    FLDOpponentPolicyState Bet;
    Decision = LDOpponentPolicy::Decide(Frame, C, Bet);
    bGood &= Expect(Test, TEXT("Betting never moves"), Decision, EMode::PK, EBody::None)
        && Test.TestTrue(TEXT("One bet on that side of this PK"), Decision.PK.Kind == EPK::Bet && Decision.PK.Side == 2
            && Decision.PK.Serial == PK.Serial && Decision.Reason == EReason::PKBet);
    bGood &= Test.TestTrue(TEXT("A bet is sent once per PK"), LDOpponentPolicy::Decide(Frame, C, Bet).PK.Kind == EPK::None);
    Frame.Public.PK.Bets = {PKBet(Me, 3)};
    bGood &= Test.TestTrue(TEXT("A placed bet is not replaced"), LDOpponentPolicy::PKBetSide(Frame.Public, Me) == 0);
    Frame.Public.PK.Bets.Reset();
    Frame.Public.PK.Bettors.Reset();
    bGood &= Test.TestTrue(TEXT("A non-bettor does not bet"), LDOpponentPolicy::PKBetSide(Frame.Public, Me) == 0);

    // Throw: the lowest visible seat not on the bet side, within the throw range of the public anchor.
    PK.Bets = {PKBet(Me, 2)};
    PK.Outcome = ELDPKOutcome::Running;
    PK.FightStartedAtMs = 5000;
    TArray<FLDOpponentUnit> Seen = {
        Unit(11, ELDOpponentUnitKind::Contender, 10, 0), Unit(13, ELDOpponentUnitKind::Contender, 30, 0),
        Unit(14, ELDOpponentUnitKind::Contender, 40, 0)};
    const FLDOpponentUnit* Target = LDOpponentPolicy::PKThrowTarget(PK, 2, Seen, 1000.);
    bGood &= Test.TestTrue(TEXT("The bet side is never thrown at and an unseen entrant is skipped"),
        Target && Target->ContenderId == FGuid(0xE0, 0, 0, 13));
    Seen[1].FootCm = FVector(1500, 0, 0);
    Target = LDOpponentPolicy::PKThrowTarget(PK, 2, Seen, 1000.);
    bGood &= Test.TestTrue(TEXT("An entrant out of throw range is skipped"), Target && Target->ContenderId == FGuid(0xE0, 0, 0, 14));
    bGood &= Test.TestNull(TEXT("Without a bet there is no throw"), LDOpponentPolicy::PKThrowTarget(PK, 0, Seen, 1000.));
    const TArray<FLDOpponentUnit> BetSideOnly = {Unit(11, ELDOpponentUnitKind::Contender, 10, 0)};
    bGood &= Test.TestNull(TEXT("Only the bet side in sight: no throw"), LDOpponentPolicy::PKThrowTarget(PK, 2, BetSideOnly, 1000.));
    Frame = Base;
    Frame.Public.Timeline.Step = ELDRestStep::PK;
    Frame.Public.PK = PK;
    Frame.Public.NowMs = 9000;
    Frame.Units = {Unit(11, ELDOpponentUnitKind::Contender, 10, 0), Unit(13, ELDOpponentUnitKind::Contender, 30, 0)};
    FLDOpponentPolicyState Throw;
    Decision = LDOpponentPolicy::Decide(Frame, C, Throw);
    bGood &= Expect(Test, TEXT("A spectator never moves while the arena runs"), Decision, EMode::PK, EBody::None)
        && Test.TestTrue(TEXT("A ready bettor throws at the current foot of the chosen entrant"), Decision.PK.Kind == EPK::Throw
            && Decision.PK.Serial == PK.Serial && Decision.PK.TargetCm.Equals(FVector(30, 0, 0)) && Decision.Reason == EReason::PKThrow);
    Frame.PKThrowReadyAtMs = Frame.Public.NowMs + 1;
    bGood &= Test.TestTrue(TEXT("No throw before the own cooldown passed"), LDOpponentPolicy::Decide(Frame, C, Throw).PK.Kind == EPK::None);

    // Teammates: only a team format shares a side.
    FLDPKView Teams;
    Teams.Serial = 3;
    Teams.Format = ELDPKFormat::Teams;
    Teams.SideCount = 2;
    Teams.Entrants = {PKEntrant(21, 2), PKEntrant(22, 1), PKEntrant(999, 1), PKEntrant(23, 2)};
    const FGuid Mate = FGuid(0xE0, 0, 0, 22), Rival = FGuid(0xE0, 0, 0, 21);
    bGood &= Test.TestTrue(TEXT("Same side is a teammate"), LDOpponentPolicy::IsPKTeammate(Teams, Me, Mate))
        && Test.TestFalse(TEXT("Another side is not"), LDOpponentPolicy::IsPKTeammate(Teams, Me, Rival))
        && Test.TestFalse(TEXT("Self is not its own teammate"), LDOpponentPolicy::IsPKTeammate(Teams, Me, Me))
        && Test.TestFalse(TEXT("A free-for-all has no teammates"), LDOpponentPolicy::IsPKTeammate(PK, FGuid(0xE0, 0, 0, 11),
            FGuid(0xE0, 0, 0, 12)));

    // Ring: outside the current radius, or outside the previewed next radius while one is shown.
    FLDPKParameters Ring;
    Ring.ArenaRadiusCm = 1000;
    Ring.MidRadiusCm = 600;
    Ring.FinalRadiusCm = 300;
    Ring.ShrinkStartMs = 10000;
    Ring.MidMs = 20000;
    Ring.FinalMs = 30000;
    Ring.PreviewMs = 5000;
    Ring.MaxMs = 40000;
    FLDPKView Arena;
    Arena.FightStartedAtMs = 100000;
    const auto Out = [&Ring, &Arena](double X, int64 ElapsedMs)
    { return LDOpponentPolicy::PKRingRetreat(Ring, Arena, FVector(X, 0, 0), Arena.FightStartedAtMs + ElapsedMs); };
    bGood &= Test.TestFalse(TEXT("No ring before the fight starts"), Out(2000, -1))
        && Test.TestFalse(TEXT("Inside the full arena before any preview"), Out(900, 0))
        && Test.TestTrue(TEXT("Outside the current radius walks back"), Out(1100, 0))
        && Test.TestTrue(TEXT("Outside the previewed mid radius walks back"), Out(900, 6000))
        && Test.TestFalse(TEXT("Inside the previewed radius stays"), Out(500, 6000))
        && Test.TestTrue(TEXT("Outside the previewed final radius during the second shrink"), Out(400, 25000))
        && Test.TestFalse(TEXT("Inside the final radius during the second shrink"), Out(250, 25000))
        && Test.TestTrue(TEXT("Outside the final radius after the shrink"), Out(301, 35000))
        && Test.TestFalse(TEXT("Inside the final radius after the shrink"), Out(299, 35000));

    // Fighting as an entrant: the ring first, then the nearest visible enemy entrant, never a teammate.
    FLDPKParameters Approved;
    FString Error;
    if (!Test.TestTrue(TEXT("The approved ring parameters load"), FLDPKParameters::FromContent(C.Get().Parameters, Approved, Error)))
    {
        Test.AddError(Error);
        return false;
    }
    Teams.Outcome = ELDPKOutcome::Running;
    Teams.ArenaCentreCm = FVector(0, 3000, 0);
    Teams.FightStartedAtMs = 5000;
    Frame = Base;
    Frame.Public.Timeline.Step = ELDRestStep::PK;
    Frame.Public.PK = Teams;
    Frame.Public.NowMs = 5000;
    Frame.Self.FootCm = Teams.ArenaCentreCm + FVector(Approved.ArenaRadiusCm + 100., 0, 0);
    FLDOpponentPolicyState Fight;
    Decision = LDOpponentPolicy::Decide(Frame, C, Fight);
    bGood &= Expect(Test, TEXT("Outside the ring the entrant walks to the arena centre first"), Decision, EMode::PK,
        EBody::MoveGround, TEXT("capture")) && Test.TestTrue(TEXT("The ring is its own reason"), Decision.Reason == EReason::PKRing);
    Frame.Self.FootCm = Teams.ArenaCentreCm;
    Frame.Units = {Unit(22, ELDOpponentUnitKind::Contender, 100, 3000), Unit(21, ELDOpponentUnitKind::Contender, 400, 3000)};
    FLDOpponentPolicyState Engage;
    Decision = LDOpponentPolicy::Decide(Frame, C, Engage);
    bGood &= Test.TestTrue(TEXT("The nearer teammate is passed over for the enemy entrant"),
        Decision.Mode == EMode::PK && Decision.Reason == EReason::PKFight && Decision.Body.Kind == EBody::AttackUnit
        && Decision.Body.Handle == Frame.Units[1].Handle);
    Frame.Units = {Unit(22, ELDOpponentUnitKind::Contender, 100, 3000)};
    FLDOpponentPolicyState MateOnly;
    Decision = LDOpponentPolicy::Decide(Frame, C, MateOnly);
    bGood &= Expect(Test, TEXT("A teammate alone in sight is never a target"), Decision, EMode::PK, EBody::None);
    Frame.Public.Timeline.Step = ELDRestStep::PKReady;
    Frame.Public.PK.Outcome = ELDPKOutcome::Pending;
    Frame.Units = {Unit(21, ELDOpponentUnitKind::Contender, 400, 3000)};
    FLDOpponentPolicyState Ready;
    Decision = LDOpponentPolicy::Decide(Frame, C, Ready);
    bGood &= Expect(Test, TEXT("PKReady: inputs are locked, nothing is sent"), Decision, EMode::Stopped, EBody::None)
        && Test.TestTrue(TEXT("PKReady: no PK request either"), Decision.PK.Kind == EPK::None);
    return bGood;
}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentPKTest, "Lockdown.Rules.DemoOpponentPolicy.PK",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentPKTest::RunTest(const FString& Parameters)
{
    return LDOpponentPolicyTests::RunPK(*this);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentRecoveryEscapeKeptTest, "Lockdown.Rules.DemoOpponentPolicy.RecoveryEscapeKept",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentRecoveryEscapeKeptTest::RunTest(const FString& Parameters)
{
    return LDOpponentPolicyTests::RunRecoveryEscapeKept(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentCharmsTest, "Lockdown.Rules.DemoOpponentPolicy.Charms",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentCharmsTest::RunTest(const FString& Parameters)
{
    return LDOpponentPolicyTests::RunCharms(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentRecoveryCharmEscapeTest, "Lockdown.Rules.DemoOpponentPolicy.RecoveryCharmEscape",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentRecoveryCharmEscapeTest::RunTest(const FString& Parameters)
{
    return LDOpponentPolicyTests::RunRecoveryCharmEscape(*this);
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

namespace LDOpponentComponentTests
{
namespace
{
const FLDDemoComponentDefinition* Component(FAutomationTestBase& Test, const FLDDemoContentSnapshot& Catalog, const TCHAR* Id)
{
    const FLDDemoComponentDefinition* Out = nullptr;
    FString Error;
    if (!Catalog.FindComponent(Id, Out, Error) || !Out) { Test.AddError(FString::Printf(TEXT("%s: %s"), Id, *Error)); return nullptr; }
    return Out;
}

FLDOpponentComponent Owned(const TCHAR* Id, bool bInstalled, bool bEverInstalled)
{
    FLDOpponentComponent Out;
    Out.DefinitionId = Id;
    Out.bInstalled = bInstalled;
    Out.bEverInstalled = bEverInstalled || bInstalled;
    return Out;
}

bool RunPickAndEnable(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(Test, Catalog)) { return false; }
    const FLDDemoContentSnapshot& C = *Catalog;
    const FLDDemoComponentDefinition* Fire = Component(Test, C, TEXT("component_fist_fire"));
    const FLDDemoComponentDefinition* Water = Component(Test, C, TEXT("component_fist_water"));
    const FLDDemoComponentDefinition* Shock = Component(Test, C, TEXT("component_fist_shock"));
    const FLDDemoComponentDefinition* Trick = Component(Test, C, TEXT("component_dirty_trick"));
    if (!Fire || !Water || !Shock || !Trick || !Test.TestTrue(TEXT("B18 binds a listed active"), !Trick->BindActives.IsEmpty())) { return false; }
    // A learned active B18 can bind, and one it cannot (any catalog active its binding does not list).
    const FString Bindable = Trick->BindActives[0];
    const FLDDemoAbilityDefinition* Unbindable = C.Get().Abilities.FindByPredicate([Trick](const FLDDemoAbilityDefinition& Ability)
        { return !Trick->BindActives.Contains(Ability.Id); });

    FLDOpponentSelf Self;
    FString Bound;
    ELDDemoElement Element = ELDDemoElement::Electric;
    bool bGood = Test.TestTrue(TEXT("A plain base component is usable without a binding"),
        LDOpponentPolicy::ComponentUsable(Self, *Fire, Bound, Element) && Bound.IsEmpty() && Element == ELDDemoElement::None);
    bGood &= Test.TestTrue(TEXT("Water and electric base components are usable like any other"),
        LDOpponentPolicy::ComponentUsable(Self, *Water, Bound, Element) && LDOpponentPolicy::ComponentUsable(Self, *Shock, Bound, Element));
    bGood &= Test.TestFalse(TEXT("B18 without a bindable learned active is skipped (InvalidTarget)"),
        LDOpponentPolicy::ComponentUsable(Self, *Trick, Bound, Element));
    if (Unbindable) { Self.Actives.Add({Unbindable->Id, 0}); }
    Self.Actives.Add({Bindable, 1});
    bGood &= Test.TestTrue(TEXT("B18 binds the first bindable learned active (Q/W/E order) with its first listed element (fire)"),
        LDOpponentPolicy::ComponentUsable(Self, *Trick, Bound, Element) && Bound == Bindable && Element == ELDDemoElement::Fire);

    // Opening base: the first option in catalog order the AI can enable now.
    Self.bOpeningBasePending = true;
    Self.OpeningBaseOptions = {TEXT("component_fist_water"), TEXT("component_fist_shock"), TEXT("component_dash_punch")};
    FString Picked;
    FString Error;
    bGood &= Test.TestTrue(TEXT("A water option is picked like any other: ") + Error,
        LDOpponentPolicy::SelectOpeningBase(Self, C, Picked, Bound, Element, Error) && Picked == TEXT("component_fist_water") && Bound.IsEmpty());
    Self.OpeningBaseOptions = {TEXT("component_fist_fire"), TEXT("component_dash_punch")};
    bGood &= Test.TestTrue(TEXT("The first usable option in catalog order wins"),
        LDOpponentPolicy::SelectOpeningBase(Self, C, Picked, Bound, Element, Error) && Picked == TEXT("component_fist_fire"));
    Self.OpeningBaseOptions = {TEXT("component_dirty_trick")};
    const TArray<FLDOpponentActive> HeldActives = Self.Actives;
    Self.Actives.Reset();
    bGood &= Test.TestFalse(TEXT("No usable option picks nothing, never a refused one"),
        LDOpponentPolicy::SelectOpeningBase(Self, C, Picked, Bound, Element, Error));
    Self.Actives = HeldActives;
    Self.OpeningBaseOptions = {TEXT("component_unknown")};
    bGood &= Test.TestFalse(TEXT("An unknown option is refused, never defaulted"),
        LDOpponentPolicy::SelectOpeningBase(Self, C, Picked, Bound, Element, Error));
    Self.bOpeningBasePending = false;
    Self.OpeningBaseOptions.Reset();

    // Enabling: every owned, never-installed component in catalog order; installed and moved ones are skipped.
    Self.Components = {Owned(TEXT("component_dirty_trick"), false, false), Owned(TEXT("component_dash_bump"), false, true),
        Owned(TEXT("component_fist_water"), false, false), Owned(TEXT("component_hard_skin"), false, false),
        Owned(TEXT("component_tab_ledger"), true, true)};
    bGood &= Test.TestTrue(TEXT("The first fresh usable component in catalog order: ") + Error,
        LDOpponentPolicy::SelectComponentInstall(Self, C, Picked, Bound, Element, Error) && Picked == TEXT("component_hard_skin"));
    Self.Components[3].bInstalled = Self.Components[3].bEverInstalled = true;
    bGood &= Test.TestTrue(TEXT("Then B10 water"),
        LDOpponentPolicy::SelectComponentInstall(Self, C, Picked, Bound, Element, Error) && Picked == TEXT("component_fist_water"));
    Self.Components[2].bInstalled = Self.Components[2].bEverInstalled = true;
    bGood &= Test.TestTrue(TEXT("Then B18 with its binding"),
        LDOpponentPolicy::SelectComponentInstall(Self, C, Picked, Bound, Element, Error) && Picked == TEXT("component_dirty_trick")
        && Bound == Bindable && Element == ELDDemoElement::Fire);
    Self.Actives.RemoveAll([&Bindable](const FLDOpponentActive& Active) { return Active.DefinitionId == Bindable; });
    bGood &= Test.TestFalse(TEXT("Nothing usable is left without a bindable active"),
        LDOpponentPolicy::SelectComponentInstall(Self, C, Picked, Bound, Element, Error));
    Self.Actives.Add({Bindable, 1});

    // The decision: the opening pick in the opening rest, installs only inside the safe window, one request at a time.
    FLDOpponentDecisionFrame Frame;
    Frame.Generation = 1;
    Frame.Public.Phase = ELDMatchPhase::Rest;
    Frame.Public.Timeline.Stage = ELDMatchStage::Rest;
    Frame.Public.Timeline.Step = ELDRestStep::Build;
    Frame.Public.NowMs = 10000;
    Frame.Self = Self;
    Frame.Self.ContenderId = FGuid(0xC0, 0, 0, 1);
    Frame.Self.Phase = ELDLifePhase::Alive;
    Frame.Self.Life = 1;
    Frame.Self.Components = {Owned(TEXT("component_hard_skin"), false, false)};
    Frame.Self.bOpeningBasePending = true;
    Frame.Self.OpeningBaseOptions = {TEXT("component_fist_fire"), TEXT("component_dash_punch")};
    FLDOpponentPolicyState State;
    FLDOpponentBuildIntent Intent;
    bGood &= Test.TestTrue(TEXT("The opening pick comes before any install"),
        LDOpponentPolicy::DecideFollowUpBuild(Frame, C, State, Intent) && Intent.Kind == ELDOpponentBuildKind::OpeningBase
        && Intent.Component == TEXT("component_fist_fire") && Intent.Key.POI == FName(TEXT("component_fist_fire")));
    Frame.Self.bOpeningBasePending = false;
    Frame.Self.OpeningBaseOptions.Reset();
    Frame.Public.Phase = ELDMatchPhase::Development;
    Frame.Public.Timeline = FLDTimelineView();
    Frame.Self.BuildSafeAtMs = Frame.Public.NowMs + 1;
    bGood &= Test.TestFalse(TEXT("No install inside combat"), LDOpponentPolicy::DecideFollowUpBuild(Frame, C, State, Intent));
    Frame.Self.BuildSafeAtMs = Frame.Public.NowMs;
    bGood &= Test.TestTrue(TEXT("Out of combat the fresh component is enabled"),
        LDOpponentPolicy::DecideFollowUpBuild(Frame, C, State, Intent) && Intent.Kind == ELDOpponentBuildKind::InstallComponent
        && Intent.Component == TEXT("component_hard_skin"));
    Frame.bBuildChangeInFlight = true;
    bGood &= Test.TestFalse(TEXT("Nothing while a build request is in flight"), LDOpponentPolicy::DecideFollowUpBuild(Frame, C, State, Intent));

    // Commands: built from the own latest view, binding only to a learned active.
    FLDProgressionView View;
    View.bValid = true;
    View.Generation = 1;
    View.Life = 1;
    View.LifePhase = ELDLifePhase::Alive;
    View.BuildRevision = 4;
    View.bOpeningBasePending = true;
    View.OpeningBaseOptions = {FName(TEXT("component_fist_fire"))};
    FLDActiveSlotView& Learned = View.Actives.AddDefaulted_GetRef();
    Learned.DefinitionId = FName(*Bindable);
    Learned.Slot = 1;
    FLDComponentInstanceView& Held = View.Components.AddDefaulted_GetRef();
    Held.DefinitionId = TEXT("component_dirty_trick");
    Held.Source = ELDComponentSource::LevelUp;
    Held.SourceLevel = 2;
    FLDOpponentBuildIntent Pick;
    Pick.Kind = ELDOpponentBuildKind::OpeningBase;
    Pick.Component = TEXT("component_fist_fire");
    FLDBuildCommand Command;
    FName Decoded;
    bGood &= Test.TestTrue(TEXT("An opening pick always asks to enable it now: ") + Error,
        LDOpponentRequests::BuildCommand(Pick, View, C, 7, Command, Error) && Command.Kind == ELDBuildCommandKind::OpeningBase
        && Command.bInstallIfPossible && Command.TargetKind == ELDComponentTargetKind::None && Command.Element == ELDDemoElement::None
        && Command.Component.Decode(Decoded) && Decoded == FName(TEXT("component_fist_fire")) && Command.ExpectedBuildRevision == 4);
    Pick.Component = TEXT("component_dash_punch");
    bGood &= Test.TestFalse(TEXT("An option no longer offered is not sent"), LDOpponentRequests::BuildCommand(Pick, View, C, 8, Command, Error));
    FLDOpponentBuildIntent Enable;
    Enable.Kind = ELDOpponentBuildKind::InstallComponent;
    Enable.Component = TEXT("component_dirty_trick");
    Enable.BoundActive = Bindable;
    Enable.Element = ELDDemoElement::Fire;
    bGood &= Test.TestTrue(TEXT("An install carries its binding: ") + Error,
        LDOpponentRequests::BuildCommand(Enable, View, C, 9, Command, Error) && Command.Kind == ELDBuildCommandKind::InstallComponent
        && !Command.bInstallIfPossible && Command.Element == ELDDemoElement::Fire && Command.BoundActive.Decode(Decoded) && Decoded == FName(*Bindable));
    Enable.BoundActive = TEXT("active_not_learned_anywhere");
    bGood &= Test.TestFalse(TEXT("A binding to an active not learned is not sent"), LDOpponentRequests::BuildCommand(Enable, View, C, 10, Command, Error));
    Held.bInstalled = Held.bEverInstalled = true;
    Enable.BoundActive = Bindable;
    bGood &= Test.TestFalse(TEXT("An installed component is not installed again"), LDOpponentRequests::BuildCommand(Enable, View, C, 11, Command, Error));
    return bGood;
}

FLDOpponentComponent Hosted(const TCHAR* Id, ELDComponentTargetKind Kind = ELDComponentTargetKind::None, const TCHAR* Target = TEXT(""),
    ELDDemoElement Element = ELDDemoElement::None)
{
    FLDOpponentComponent Out = Owned(Id, true, true);
    Out.TargetKind = Kind;
    Out.TargetId = Target;
    Out.Element = Element;
    return Out;
}

// #198 F: the first legal target in catalog list order (learned active, installed host, D, self), C12's first open installed
// base, and the refusals growth would answer (C09, M08/M09 on one target, C01 on a B18 that does not burn).
bool RunWorkshopTargets(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(Test, Catalog)) { return false; }
    const FLDDemoContentSnapshot& C = *Catalog;
    const FLDDemoComponentDefinition* LongKick = Component(Test, C, TEXT("component_long_kick"));
    const FLDDemoComponentDefinition* Ram = Component(Test, C, TEXT("component_ram_through"));
    const FLDDemoComponentDefinition* OweIt = Component(Test, C, TEXT("component_owe_it"));
    const FLDDemoComponentDefinition* FireSkin = Component(Test, C, TEXT("component_fire_skin"));
    const FLDDemoComponentDefinition* BlastChase = Component(Test, C, TEXT("component_blast_chase"));
    const FLDDemoComponentDefinition* DirtyFist = Component(Test, C, TEXT("component_dirty_fist"));
    if (!LongKick || !Ram || !OweIt || !FireSkin || !BlastChase || !DirtyFist) { return false; }

    FLDOpponentSelf Self;
    FLDOpponentComponentTarget Target;
    const auto Is = [&Target](ELDComponentTargetKind Kind, const TCHAR* Id, const TCHAR* Base)
    { return Target.Kind == Kind && Target.TargetId == Id && Target.BoundComponent == Base; };
    bool bGood = Test.TestFalse(TEXT("M03 without its active or host has no target"), LDOpponentPolicy::WorkshopTarget(Self, C, *LongKick, Target));
    Self.Components = {Hosted(TEXT("component_third_punch"))};
    bGood &= Test.TestTrue(TEXT("M03 goes onto its installed host"), LDOpponentPolicy::WorkshopTarget(Self, C, *LongKick, Target)
        && Is(ELDComponentTargetKind::Component, TEXT("component_third_punch"), TEXT("")));
    Self.Actives.Add({TEXT("active_elbow"), 0});
    bGood &= Test.TestTrue(TEXT("A learned listed active comes before a host"), LDOpponentPolicy::WorkshopTarget(Self, C, *LongKick, Target)
        && Is(ELDComponentTargetKind::Active, TEXT("active_elbow"), TEXT("")));
    bGood &= Test.TestTrue(TEXT("M08 without its actives goes onto the innate D"), LDOpponentPolicy::WorkshopTarget(Self, C, *Ram, Target)
        && Is(ELDComponentTargetKind::Dash, TEXT(""), TEXT("")));
    Self.Components.Add(Hosted(TEXT("component_quick_hop"), ELDComponentTargetKind::Dash));
    bGood &= Test.TestFalse(TEXT("M08 never joins M09 on the same D (TargetConflict)"), LDOpponentPolicy::WorkshopTarget(Self, C, *Ram, Target));
    bGood &= Test.TestTrue(TEXT("M12 installs on the holder itself"), LDOpponentPolicy::WorkshopTarget(Self, C, *OweIt, Target)
        && Is(ELDComponentTargetKind::None, TEXT(""), TEXT("")));
    Self.Components.Add(Hosted(TEXT("component_dirty_trick"), ELDComponentTargetKind::None, TEXT(""), ELDDemoElement::Water));
    bGood &= Test.TestFalse(TEXT("C01 skips a B18 that does not burn"), LDOpponentPolicy::WorkshopTarget(Self, C, *FireSkin, Target));
    Self.Components.Last().Element = ELDDemoElement::Fire;
    bGood &= Test.TestTrue(TEXT("C01 goes onto a burning B18"), LDOpponentPolicy::WorkshopTarget(Self, C, *FireSkin, Target)
        && Is(ELDComponentTargetKind::Component, TEXT("component_dirty_trick"), TEXT("")));
    bGood &= Test.TestTrue(TEXT("C09 goes onto the installed element source"), LDOpponentPolicy::WorkshopTarget(Self, C, *BlastChase, Target)
        && Is(ELDComponentTargetKind::Component, TEXT("component_dirty_trick"), TEXT("")));
    Self.Components.Add(Hosted(TEXT("component_fist_water")));
    bGood &= Test.TestTrue(TEXT("C12 repeats an installed water base"), LDOpponentPolicy::WorkshopTarget(Self, C, *DirtyFist, Target)
        && Is(ELDComponentTargetKind::Active, TEXT("active_elbow"), TEXT("component_fist_water")));
    Self.Components.Add(Hosted(TEXT("component_tab_ledger")));
    bGood &= Test.TestTrue(TEXT("C12 repeats the first open installed base"), LDOpponentPolicy::WorkshopTarget(Self, C, *DirtyFist, Target)
        && Is(ELDComponentTargetKind::Active, TEXT("active_elbow"), TEXT("component_tab_ledger")));

    // The decision carries the target and the command names it.
    FLDOpponentDecisionFrame Frame;
    Frame.Generation = 1;
    Frame.Public.Phase = ELDMatchPhase::Development;
    Frame.Public.NowMs = 10000;
    Frame.Self = Self;
    Frame.Self.ContenderId = FGuid(0xC0, 0, 0, 2);
    Frame.Self.Phase = ELDLifePhase::Alive;
    Frame.Self.Life = 1;
    Frame.Self.BuildSafeAtMs = Frame.Public.NowMs;
    Frame.Self.Components.Add(Owned(TEXT("component_dirty_fist"), false, false));
    FLDOpponentPolicyState State;
    FLDOpponentBuildIntent Intent;
    FString Error;
    bGood &= Test.TestTrue(TEXT("The fresh C12 is installed with its target and base"),
        LDOpponentPolicy::DecideFollowUpBuild(Frame, C, State, Intent) && Intent.Kind == ELDOpponentBuildKind::InstallComponent
        && Intent.Component == TEXT("component_dirty_fist") && Intent.TargetKind == ELDComponentTargetKind::Active
        && Intent.TargetId == TEXT("active_elbow") && Intent.BoundComponent == TEXT("component_tab_ledger"));
    FLDProgressionView View;
    View.bValid = true;
    View.Generation = 1;
    View.Life = 1;
    View.LifePhase = ELDLifePhase::Alive;
    FLDActiveSlotView& Learned = View.Actives.AddDefaulted_GetRef();
    Learned.DefinitionId = TEXT("active_elbow");
    FLDComponentInstanceView& Base = View.Components.AddDefaulted_GetRef();
    Base.DefinitionId = TEXT("component_tab_ledger");
    Base.bInstalled = Base.bEverInstalled = true;
    FLDComponentInstanceView& Fresh = View.Components.AddDefaulted_GetRef();
    Fresh.DefinitionId = TEXT("component_dirty_fist");
    FLDBuildCommand Command;
    FName Decoded;
    FName DecodedBase;
    bGood &= Test.TestTrue(TEXT("The install command names the target and the repeated base: ") + Error,
        LDOpponentRequests::BuildCommand(Intent, View, C, 12, Command, Error) && Command.Kind == ELDBuildCommandKind::InstallComponent
        && Command.TargetKind == ELDComponentTargetKind::Active && Command.TargetDefinition.Decode(Decoded) && Decoded == FName(TEXT("active_elbow"))
        && Command.BoundComponent.Decode(DecodedBase) && DecodedBase == FName(TEXT("component_tab_ledger")) && Command.TargetSegment == 0);
    Intent.BoundComponent = TEXT("component_fist_fire");
    bGood &= Test.TestFalse(TEXT("A base not installed is not named"), LDOpponentRequests::BuildCommand(Intent, View, C, 13, Command, Error));
    return bGood;
}

// #198 G: a pending elite camp component choice is always resolved (an unowned option it can enable first, then the first
// unowned one, the empty-pool points once every option is owned) under the active choice's gate, even in combat or dead;
// a rest window spends unspent points on a gap fill before the stats. Commands come from the own latest view only.
bool RunComponentChoice(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(Test, Catalog)) { return false; }
    const FLDDemoContentSnapshot& C = *Catalog;
    if (!Component(Test, C, TEXT("component_long_kick")) || !Component(Test, C, TEXT("component_hard_skin"))
        || !Component(Test, C, TEXT("component_third_punch")))
    {
        return false;
    }
    const FGuid ChoiceId(0xC4, 0, 0, 1);
    FLDOpponentComponentChoice Choice;
    Choice.ChoiceId = ChoiceId;
    // M03 has no learned active or installed host here, so only the base can be enabled right away.
    Choice.Options = {TEXT("component_long_kick"), TEXT("component_hard_skin")};
    FLDOpponentSelf Self;
    FString Option;
    FString Error;
    bool bGood = Test.TestTrue(TEXT("An option it can enable now comes first: ") + Error,
        LDOpponentPolicy::SelectComponentChoice(Self, C, Choice, Option, Error) && Option == TEXT("component_hard_skin"));
    Self.Components = {Owned(TEXT("component_hard_skin"), false, false)};
    bGood &= Test.TestTrue(TEXT("An owned option is skipped for the first free one"),
        LDOpponentPolicy::SelectComponentChoice(Self, C, Choice, Option, Error) && Option == TEXT("component_long_kick"));
    Self.Components.Add(Owned(TEXT("component_long_kick"), false, false));
    bGood &= Test.TestTrue(TEXT("Every option owned takes the empty-pool points"),
        LDOpponentPolicy::SelectComponentChoice(Self, C, Choice, Option, Error) && Option.IsEmpty());
    FLDOpponentComponentChoice Unknown = Choice;
    Unknown.Options = {TEXT("component_unknown")};
    bGood &= Test.TestFalse(TEXT("An option outside the catalog is refused, never defaulted"),
        LDOpponentPolicy::SelectComponentChoice(Self, C, Unknown, Option, Error));

    // The decision: resolved in combat and while dead, keyed by the choice.
    FLDOpponentDecisionFrame Frame;
    Frame.Generation = 1;
    Frame.Public.Phase = ELDMatchPhase::Development;
    Frame.Public.NowMs = 10000;
    Frame.Self.ContenderId = FGuid(0xC0, 0, 0, 3);
    Frame.Self.Phase = ELDLifePhase::Alive;
    Frame.Self.Life = 1;
    Frame.Self.BuildSafeAtMs = Frame.Public.NowMs + 5000;
    Frame.Self.ComponentChoices = {Choice};
    FLDOpponentPolicyState State;
    FLDOpponentBuildIntent Intent;
    bGood &= Test.TestTrue(TEXT("A pending choice is resolved even inside combat"),
        LDOpponentPolicy::DecideFollowUpBuild(Frame, C, State, Intent) && Intent.Kind == ELDOpponentBuildKind::ComponentChoice
        && Intent.ChoiceId == ChoiceId && Intent.Component == TEXT("component_hard_skin") && Intent.Key.Target == ChoiceId);
    Frame.Self.Phase = ELDLifePhase::Dead;
    bGood &= Test.TestTrue(TEXT("And while dead"),
        LDOpponentPolicy::DecideFollowUpBuild(Frame, C, State, Intent) && Intent.Kind == ELDOpponentBuildKind::ComponentChoice);
    Frame.Self.Phase = ELDLifePhase::Alive;
    Frame.bBuildChangeInFlight = true;
    bGood &= Test.TestFalse(TEXT("Nothing while a build request is in flight"), LDOpponentPolicy::DecideFollowUpBuild(Frame, C, State, Intent));
    Frame.bBuildChangeInFlight = false;
    Frame.Self.ComponentChoices.Reset();

    // Gap fills: only in a rest window and only when the unspent points pay the approved price.
    const int64 FillGapPoints = C.Get().Parameters.FillGapPoints;
    if (!Test.TestTrue(TEXT("The approved content prices FillGap"), FillGapPoints > 0)) { return false; }
    Frame.Self.GapFills = {{TEXT("component_long_kick"), TEXT("component_third_punch")}};
    Frame.Self.UnspentPoints = FillGapPoints;
    Frame.Self.BuildSafeAtMs = Frame.Public.NowMs;
    bGood &= Test.TestFalse(TEXT("No gap fill on the field"),
        LDOpponentPolicy::DecideFollowUpBuild(Frame, C, State, Intent) && Intent.Kind == ELDOpponentBuildKind::FillGap);
    Frame.Public.Phase = ELDMatchPhase::Rest;
    Frame.Public.Timeline.Stage = ELDMatchStage::Rest;
    Frame.Public.Timeline.Step = ELDRestStep::Build;
    bGood &= Test.TestTrue(TEXT("A rest window fills the gap before allocating"),
        LDOpponentPolicy::DecideFollowUpBuild(Frame, C, State, Intent) && Intent.Kind == ELDOpponentBuildKind::FillGap
        && Intent.Component == TEXT("component_long_kick") && Intent.Filler == TEXT("component_third_punch")
        && Intent.Key.POI == FName(TEXT("component_long_kick")));
    Frame.Self.UnspentPoints = FillGapPoints - 1;
    bGood &= Test.TestFalse(TEXT("Not without the price in unspent points"),
        LDOpponentPolicy::DecideFollowUpBuild(Frame, C, State, Intent) && Intent.Kind == ELDOpponentBuildKind::FillGap);

    // Commands from the own latest view.
    FLDProgressionView View;
    View.bValid = true;
    View.Generation = 1;
    View.Life = 1;
    View.LifePhase = ELDLifePhase::Alive;
    View.BuildRevision = 6;
    FLDComponentChoiceView& Pending = View.ComponentChoices.AddDefaulted_GetRef();
    Pending.ChoiceId = ChoiceId;
    Pending.Event = TEXT("camp_marker_elite");
    Pending.Serial = 1;
    Pending.Options = {FName(TEXT("component_long_kick")), FName(TEXT("component_hard_skin"))};
    FLDComponentInstanceView& Skin = View.Components.AddDefaulted_GetRef();
    Skin.DefinitionId = TEXT("component_hard_skin");
    FLDOpponentBuildIntent Take;
    Take.Kind = ELDOpponentBuildKind::ComponentChoice;
    Take.ChoiceId = ChoiceId;
    Take.Component = TEXT("component_long_kick");
    FLDBuildCommand Command;
    FName Decoded;
    bGood &= Test.TestTrue(TEXT("A choice names the pending choice and the option: ") + Error,
        LDOpponentRequests::BuildCommand(Take, View, C, 20, Command, Error) && Command.Kind == ELDBuildCommandKind::ComponentChoice
        && Command.OfferId == ChoiceId && Command.Component.Decode(Decoded) && Decoded == FName(TEXT("component_long_kick"))
        && Command.Slot == FLDBuildCommand::AnySlot && Command.ExpectedBuildRevision == 6);
    Take.Component = TEXT("component_hard_skin");
    bGood &= Test.TestFalse(TEXT("An owned option is not sent"), LDOpponentRequests::BuildCommand(Take, View, C, 21, Command, Error));
    Take.Component.Reset();
    bGood &= Test.TestFalse(TEXT("The points are not taken while an option is free"), LDOpponentRequests::BuildCommand(Take, View, C, 22, Command, Error));
    FLDComponentInstanceView& Kick = View.Components.AddDefaulted_GetRef();
    Kick.DefinitionId = TEXT("component_long_kick");
    bGood &= Test.TestTrue(TEXT("Every option owned: the all-zero component takes the points: ") + Error,
        LDOpponentRequests::BuildCommand(Take, View, C, 23, Command, Error) && Command.Kind == ELDBuildCommandKind::ComponentChoice
        && Command.OfferId == ChoiceId && Command.Component == FLDDefinitionWire());
    Take.ChoiceId = FGuid(0xC4, 0, 0, 2);
    bGood &= Test.TestFalse(TEXT("A choice no longer pending is not sent"), LDOpponentRequests::BuildCommand(Take, View, C, 24, Command, Error));

    FLDOpponentBuildIntent Fill;
    Fill.Kind = ELDOpponentBuildKind::FillGap;
    Fill.Component = TEXT("component_long_kick");
    Fill.Filler = TEXT("component_third_punch");
    bGood &= Test.TestTrue(TEXT("A gap fill names the target and the filler: ") + Error,
        LDOpponentRequests::BuildCommand(Fill, View, C, 25, Command, Error) && Command.Kind == ELDBuildCommandKind::FillGap
        && Command.Component.Decode(Decoded) && Decoded == FName(TEXT("component_long_kick"))
        && Command.Filler.Decode(Decoded) && Decoded == FName(TEXT("component_third_punch")));
    Kick.bGapFilled = true;
    bGood &= Test.TestFalse(TEXT("A filled target is not filled again"), LDOpponentRequests::BuildCommand(Fill, View, C, 26, Command, Error));
    Kick.bGapFilled = false;
    FLDComponentInstanceView& Punch = View.Components.AddDefaulted_GetRef();
    Punch.DefinitionId = TEXT("component_third_punch");
    bGood &= Test.TestFalse(TEXT("An owned filler is not sent"), LDOpponentRequests::BuildCommand(Fill, View, C, 27, Command, Error));
    return bGood;
}

// Stands in for growth: commits one rest build decision on the own view (an uninstall with everything installed on it, a
// replacement returning what sat on the forgotten active, a respec, an allocation, a learn). False for anything else.
bool ApplyBuild(const FLDOpponentBuildIntent& Intent, FLDOpponentSelf& Self)
{
    const auto Find = [&Self](const FString& Id)
    { return Self.Components.FindByPredicate([&Id](const FLDOpponentComponent& Held) { return Held.DefinitionId == Id; }); };
    const auto Detach = [&Self](TArray<FString> Removing)
    {
        for (int32 Index = 0; Index < Removing.Num(); ++Index)
        {
            const FString Host = Removing[Index];
            for (const FLDOpponentComponent& Attached : Self.Components)
            {
                const bool bOnIt = Attached.bInstalled && ((Attached.TargetKind == ELDComponentTargetKind::Component
                    && Attached.TargetId == Host) || Attached.BoundComponent == Host);
                if (bOnIt && !Removing.Contains(Attached.DefinitionId)) { Removing.Add(Attached.DefinitionId); }
            }
        }
        for (FLDOpponentComponent& Held : Self.Components)
        {
            if (!Removing.Contains(Held.DefinitionId)) { continue; }
            Held.bInstalled = false;
            Held.TargetKind = ELDComponentTargetKind::None;
            Held.TargetId.Reset();
            Held.BoundActive.Reset();
            Held.Element = ELDDemoElement::None;
            Held.BoundComponent.Reset();
        }
    };
    switch (Intent.Kind)
    {
    case ELDOpponentBuildKind::InstallComponent:
    {
        FLDOpponentComponent* Held = Find(Intent.Component);
        if (!Held || Held->bInstalled) { return false; }
        Held->bInstalled = Held->bEverInstalled = true;
        Held->TargetKind = Intent.TargetKind;
        Held->TargetId = Intent.TargetId;
        Held->BoundActive = Intent.BoundActive;
        Held->Element = Intent.Element;
        Held->BoundComponent = Intent.BoundComponent;
        break;
    }
    case ELDOpponentBuildKind::UninstallComponent:
    {
        const FLDOpponentComponent* Held = Find(Intent.Component);
        if (!Held || !Held->bInstalled) { return false; }
        Detach({Intent.Component});
        break;
    }
    case ELDOpponentBuildKind::ReplaceActive:
    {
        FLDOpponentActive* Old = Self.Actives.FindByPredicate([&Intent](const FLDOpponentActive& Active) { return Active.Slot == Intent.Slot; });
        if (!Old || !Self.Blueprints.Contains(Intent.Blueprint)) { return false; }
        const FString Forgotten = Old->DefinitionId;
        Old->DefinitionId = Intent.Blueprint;
        Self.Blueprints.Remove(Intent.Blueprint);
        TArray<FString> OnIt;
        for (const FLDOpponentComponent& Attached : Self.Components)
        {
            if (Attached.bInstalled && ((Attached.TargetKind == ELDComponentTargetKind::Active && Attached.TargetId == Forgotten)
                || Attached.BoundActive == Forgotten)) { OnIt.Add(Attached.DefinitionId); }
        }
        Detach(OnIt);
        break;
    }
    case ELDOpponentBuildKind::Respec:
    {
        int64 Refunded = 0;
        for (FLDOpponentStatPoints& Row : Self.StatPoints) { Refunded += Row.Points; Row.Points = 0; }
        if (Refunded == 0) { return false; }
        Self.UnspentPoints += Refunded;
        break;
    }
    case ELDOpponentBuildKind::AllocatePoints:
    {
        if (Intent.PointCounts.Num() != Self.StatPoints.Num()) { return false; }
        for (int32 Index = 0; Index < Intent.PointCounts.Num(); ++Index)
        {
            Self.StatPoints[Index].Points += Intent.PointCounts[Index];
            Self.UnspentPoints -= Intent.PointCounts[Index];
        }
        if (Self.UnspentPoints < 0) { return false; }
        break;
    }
    case ELDOpponentBuildKind::LearnBlueprint:
    {
        if (Intent.Slot > 2 || !Self.Blueprints.Contains(Intent.Blueprint)
            || Self.Actives.ContainsByPredicate([&Intent](const FLDOpponentActive& Active) { return Active.Slot == Intent.Slot; }))
        {
            return false;
        }
        Self.Actives.Add({Intent.Blueprint, Intent.Slot});
        Self.Blueprints.Remove(Intent.Blueprint);
        break;
    }
    default:
        return false;
    }
    ++Self.BuildRevision;
    return true;
}

TArray<ELDOpponentBuildKind> KindsOf(const TArray<FLDOpponentBuildIntent>& Intents)
{
    TArray<ELDOpponentBuildKind> Out;
    for (const FLDOpponentBuildIntent& Intent : Intents) { Out.Add(Intent.Kind); }
    return Out;
}

// Runs the follow-up build decisions of Frame to their end, committing each like growth; a decision the stand-in cannot
// commit, or more than twelve decisions (a loop), is an error. OutSelf is the own view at the end.
bool RunRestBuilds(FAutomationTestBase& Test, const FLDDemoContentSnapshot& C, FLDOpponentDecisionFrame Frame,
    TArray<FLDOpponentBuildIntent>& Out, FLDOpponentSelf& OutSelf)
{
    Out.Reset();
    FLDOpponentPolicyState State;
    for (int32 Step = 0; Step < 12; ++Step)
    {
        FLDOpponentBuildIntent Intent;
        if (!LDOpponentPolicy::DecideFollowUpBuild(Frame, C, State, Intent))
        {
            OutSelf = Frame.Self;
            return true;
        }
        Out.Add(Intent);
        if (!ApplyBuild(Intent, Frame.Self))
        {
            Test.AddError(FString::Printf(TEXT("The growth stand-in cannot commit build kind %u"), uint32(Intent.Kind)));
            return false;
        }
    }
    Test.AddError(TEXT("The rest build decisions never reached an end"));
    return false;
}

// A rest build window frame of the shared base frame, with one zero stat row per stat.
bool RestFrame(FAutomationTestBase& Test, const FLDDemoContentSnapshot& C, FLDOpponentDecisionFrame& Out)
{
    if (!LDOpponentPolicyTests::BaseFrame(Test, C, Out)) { return false; }
    Out.Public.Phase = ELDMatchPhase::Rest;
    Out.Public.Timeline.Stage = ELDMatchStage::Rest;
    Out.Public.Timeline.Step = ELDRestStep::Build;
    // A mid rest (R1); the opening rest R0 differs only for the NPC (RunNpcGate).
    Out.Public.Timeline.RestIndex = 1;
    Out.Self.StatPoints = LDOpponentPolicyTests::StatRows(C, {});
    return true;
}
// #198 J2 coordinator decision: the opening rest R0 never buys the NPC (its opening points go to the stats); a mid rest buys
// only for an empty Q/W/E slot and only when the unspent points cover the price.
bool RunNpcGate(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(Test, Catalog)) { return false; }
    const FLDDemoContentSnapshot& C = *Catalog;
    using EKind = ELDOpponentBuildKind;
    FLDOpponentDecisionFrame Mid;
    if (!RestFrame(Test, C, Mid)) { return false; }
    const int64 Price = C.Get().Parameters.NpcActivePoints;
    if (!Test.TestTrue(TEXT("The approved content prices the NPC"), Price > 0)) { return false; }
    Mid.Self.Actives = {{TEXT("active_elbow"), 0}};
    Mid.Self.UnspentPoints = Price;
    Mid.Self.NearbyRestService = ELDRestService::SkillMerchant;
    const auto BuildOf = [&C](const FLDOpponentDecisionFrame& Of)
    {
        FLDOpponentPolicyState Fresh;
        return LDOpponentPolicy::Decide(Of, C, Fresh).Build.Kind;
    };
    auto Opening = Mid;
    Opening.Public.Timeline.RestIndex = 0;
    auto Short = Mid;
    Short.Self.UnspentPoints = Price - 1;
    auto Full = Mid;
    Full.Self.Actives = {{TEXT("active_elbow"), 0}, {TEXT("active_not_mad"), 1}, {TEXT("active_snark_bolt"), 2}};
    bool bGood = Test.TestTrue(TEXT("A mid rest with an empty slot and the price buys the NPC offer"),
        LDOpponentPolicy::WantsNpcOffer(Mid.Self, C, 1) && BuildOf(Mid) == EKind::BuyNpcOffer);
    for (uint8 Index = 2; Index <= 3; ++Index)
    {
        bGood &= Test.TestTrue(FString::Printf(TEXT("R%u buys like R1"), Index), LDOpponentPolicy::WantsNpcOffer(Mid.Self, C, Index));
    }
    bGood &= Test.TestTrue(TEXT("The opening rest R0 spends the opening points on the stats instead"),
        !LDOpponentPolicy::WantsNpcOffer(Opening.Self, C, 0) && BuildOf(Opening) == EKind::AllocatePoints);
    bGood &= Test.TestTrue(TEXT("Points short of the price: no purchase"),
        !LDOpponentPolicy::WantsNpcOffer(Short.Self, C, 1) && BuildOf(Short) == EKind::AllocatePoints);
    bGood &= Test.TestTrue(TEXT("No empty Q/W/E slot: no purchase"),
        !LDOpponentPolicy::WantsNpcOffer(Full.Self, C, 1) && BuildOf(Full) == EKind::AllocatePoints);
    return bGood;
}

bool RunRestServiceTravel(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(Test, Catalog)) { return false; }
    const FLDDemoContentSnapshot& C = *Catalog;
    using EBuild = ELDOpponentBuildKind;
    using EBody = ELDOpponentBodyKind;
    using EReason = ELDOpponentReason;
    FLDOpponentDecisionFrame Frame;
    if (!RestFrame(Test, C, Frame)) { return false; }
    Frame.Self.Actives = {{TEXT("active_elbow"), 0}};
    Frame.Self.UnspentPoints = C.Get().Parameters.NpcActivePoints;
    for (uint8 Index = 1; Index <= 3; ++Index)
    {
        FLDOpponentPOI& Place = Frame.RestServicePOIs.AddDefaulted_GetRef();
        Place.Id = FName(*FString::Printf(TEXT("rest_service_%d"), Index));
        Place.Kind = ELDOpponentPOIKind::RestService;
        Place.RestService = static_cast<ELDRestService>(Index);
        Place.SurfaceId = TEXT("demo_ground");
        Place.FootCm = FVector(1000 * Index, 0, 0);
    }
    const auto Decide = [&C](const FLDOpponentDecisionFrame& Input)
    {
        FLDOpponentPolicyState State;
        return LDOpponentPolicy::Decide(Input, C, State);
    };
    bool bGood = Test.TestTrue(TEXT("Each AI rest intent uses the authority's service mapping"),
        LDOpponentPolicy::RestServiceForBuild(EBuild::BuyNpcOffer) == ELDRestService::SkillMerchant
        && LDOpponentPolicy::RestServiceForBuild(EBuild::FillGap) == ELDRestService::ComponentArtisan
        && LDOpponentPolicy::RestServiceForBuild(EBuild::Dismantle) == ELDRestService::ComponentArtisan
        && LDOpponentPolicy::RestServiceForBuild(EBuild::Respec) == ELDRestService::Trainer
        && LDOpponentPolicy::RestServiceForBuild(EBuild::ReplaceActive) == ELDRestService::Trainer
        && LDOpponentPolicy::RestServiceForBuild(EBuild::AllocatePoints) == ELDRestService::None
        && LDProgression::RestServiceFor(ELDBuildCommandKind::ForgetActive) == ELDRestService::Trainer);
    FLDOpponentDecision Decision = Decide(Frame);
    bGood &= Test.TestTrue(TEXT("A distant purchase walks to the merchant without sending the build"),
        Decision.Reason == EReason::RestService && Decision.Body.Kind == EBody::MoveGround
        && Decision.Body.POI == TEXT("rest_service_1") && Decision.Build.Kind == EBuild::None);
    auto Walking = Frame;
    Walking.Order.Kind = ELDOpponentOrderKind::MoveGround;
    Walking.Order.Status = ELDOpponentOrderStatus::Active;
    Walking.Order.POI = TEXT("rest_service_1");
    Decision = Decide(Walking);
    bGood &= Test.TestTrue(TEXT("An existing merchant walk is kept"),
        Decision.Body.Kind == EBody::Keep && Decision.Build.Kind == EBuild::None);
    auto Acting = Frame;
    Acting.Self.bChanneling = true;
    bGood &= Test.TestTrue(TEXT("A channel finishes before walking"), Decide(Acting).Body.Kind == EBody::Keep);
    auto Nearby = Frame;
    Nearby.Self.NearbyRestService = ELDRestService::SkillMerchant;
    Decision = Decide(Nearby);
    bGood &= Test.TestTrue(TEXT("Only the matching nearby service permits the purchase"),
        Decision.Build.Kind == EBuild::BuyNpcOffer && Decision.Body.Kind == EBody::None);
    Nearby.Self.NearbyRestService = ELDRestService::Trainer;
    bGood &= Test.TestTrue(TEXT("A different nearby service does not permit the purchase"),
        Decide(Nearby).Body.Kind == EBody::MoveGround && Decide(Nearby).Build.Kind == EBuild::None);
    auto Missing = Frame;
    Missing.RestServicePOIs.Reset();
    bGood &= Test.TestTrue(TEXT("A missing service anchor is an explicit provider stop"),
        Decide(Missing).Reason == EReason::ProviderUnavailable && Decide(Missing).Build.Kind == EBuild::None);

    FLDOpponentPolicyState Retry;
    Retry.Generation = Frame.Generation;
    Retry.Life = Frame.Self.Life;
    const FLDOpponentDecision First = LDOpponentPolicy::Decide(Frame, C, Retry);
    LDOpponentPolicy::RecordOutcome(Retry, First.Body.Key, ELDOpponentOutcome::Failed, Frame.Self,
        Frame.Public.NowMs, C.Get().Parameters.AI);
    Decision = LDOpponentPolicy::Decide(Frame, C, Retry);
    bGood &= Test.TestTrue(TEXT("A refused service path backs off rather than sending another order"),
        Decision.Reason == EReason::TargetSuppressed && Decision.Body.Kind == EBody::None);

    auto Signup = Frame;
    Signup.Public.Timeline.SignupOpensAtMs = Signup.Public.NowMs;
    Signup.Public.Timeline.StepEndsAtMs = Signup.Public.NowMs + 10000;
    FLDOpponentPOI Disk;
    Disk.Id = TEXT("pk_signup_0");
    Disk.Kind = ELDOpponentPOIKind::PKSignup;
    Disk.SurfaceId = TEXT("demo_ground");
    Signup.SignupPOI = Disk;
    Decision = Decide(Signup);
    bGood &= Test.TestTrue(TEXT("The sign-up window takes priority over an NPC purchase"),
        Decision.Reason == EReason::PKSignUp && Decision.Body.Kind == EBody::MoveGround
        && Decision.Body.POI == Disk.Id && Decision.Build.Kind == EBuild::None);
    Signup.Self.NearbyRestService = ELDRestService::SkillMerchant;
    bGood &= Test.TestTrue(TEXT("Even a nearby purchase waits during sign-up"),
        Decide(Signup).Build.Kind == EBuild::None);
    return bGood;
}
// #198 rest decisions (OPPONENT_AI.md §6.4): replacement, the components back on after a reclaim, moves to the preferred
// placement, respec, dismantle and the NPC; each only in a rest build window, each run reaching an end.
bool RunRestRebuild(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(Test, Catalog)) { return false; }
    const FLDDemoContentSnapshot& C = *Catalog;
    using EKind = ELDOpponentBuildKind;
    FLDOpponentDecisionFrame Rest;
    if (!RestFrame(Test, C, Rest)) { return false; }
    const auto OnField = [](FLDOpponentDecisionFrame Frame)
    {
        Frame.Public.Phase = ELDMatchPhase::Development;
        Frame.Public.Timeline = FLDTimelineView();
        Frame.Self.BuildSafeAtMs = Frame.Public.NowMs;
        return Frame;
    };
    FLDOpponentPolicyState State;
    FLDOpponentBuildIntent Intent;
    FLDOpponentSelf After;
    TArray<FLDOpponentBuildIntent> Steps;
    FString Blueprint, Error;
    uint8 Slot = 0xFF;

    // Replacement: bail_out brings neither engagement nor self-protection to the other two, so it ranks lowest; the best
    // blueprint against those two (Unit before DirectionCone) takes its slot. Each replacement spends a blueprint.
    auto Frame = Rest;
    Frame.Self.Actives = {{TEXT("active_snark_bolt"), 0}, {TEXT("active_not_mad"), 1}, {TEXT("active_bail_out"), 2}};
    Frame.Self.Blueprints = {TEXT("active_elbow"), TEXT("active_open_mic")};
    bool bGood = Test.TestTrue(TEXT("The best blueprint takes the lowest active's slot: ") + Error,
        LDOpponentPolicy::SelectReplacement(Frame.Self, C, Blueprint, Slot, Error) && Blueprint == TEXT("active_elbow") && Slot == 2);
    bGood &= Test.TestTrue(TEXT("A rest build window sends the replacement"),
        LDOpponentPolicy::DecideFollowUpBuild(Frame, C, State, Intent) && Intent.Kind == EKind::ReplaceActive
        && Intent.Blueprint == TEXT("active_elbow") && Intent.Slot == 2 && Intent.Key.POI == FName(TEXT("active_elbow")));
    bGood &= RunRestBuilds(Test, C, Frame, Steps, After) && Test.TestTrue(TEXT("Then snark_bolt ranks lowest and open_mic replaces it; nothing after"),
        KindsOf(Steps) == TArray<EKind>({EKind::ReplaceActive, EKind::ReplaceActive}) && Steps[1].Blueprint == TEXT("active_open_mic")
        && Steps[1].Slot == 0 && After.Blueprints.IsEmpty());
    bGood &= Test.TestFalse(TEXT("On the field the blueprints wait: no replacement"),
        LDOpponentPolicy::DecideFollowUpBuild(OnField(Frame), C, State, Intent));
    auto Offered = Frame;
    Offered.Self.Offer = LDOpponentPolicyTests::ActiveOffer({TEXT("active_seethe")});
    bGood &= Test.TestFalse(TEXT("An undecided offer holds the replacement (OfferOutstanding)"),
        LDOpponentPolicy::DecideFollowUpBuild(Offered, C, State, Intent));
    auto Dead = Frame;
    Dead.Self.Phase = ELDLifePhase::Dead;
    bGood &= Test.TestFalse(TEXT("Dead in a rest: no replacement"), LDOpponentPolicy::DecideFollowUpBuild(Dead, C, State, Intent));
    auto Kept = Rest;
    Kept.Self.Actives = {{TEXT("active_elbow"), 0}, {TEXT("active_not_mad"), 1}, {TEXT("active_snark_bolt"), 2}};
    Kept.Self.Blueprints = {TEXT("active_apology_slide")};
    bGood &= Test.TestFalse(TEXT("A blueprint ranked below the lowest learned active is kept"),
        LDOpponentPolicy::SelectReplacement(Kept.Self, C, Blueprint, Slot, Error))
        && Test.TestFalse(TEXT("And nothing else is to be built"), LDOpponentPolicy::DecideFollowUpBuild(Kept, C, State, Intent));

    // Respec: the replacement takes the last AP active away, so the brains points come back once and go to the AD stats.
    const FString Physique(TEXT("stat_physique")), Skin(TEXT("stat_thick_skin")), Strength(TEXT("stat_strength")),
        Brains(TEXT("stat_brains"));
    auto Spec = Rest;
    Spec.Self.Actives = {{TEXT("active_elbow"), 0}, {TEXT("active_pants_down"), 1}, {TEXT("active_snark_bolt"), 2}};
    Spec.Self.Blueprints = {TEXT("active_crotch_grab")};
    Spec.Self.StatPoints = LDOpponentPolicyTests::StatRows(C, {{Physique, 1}, {Skin, 1}, {Strength, 1}, {Brains, 2}});
    FString Ignored;
    bGood &= Test.TestFalse(TEXT("Brains serve a learned AP active: no respec"), LDOpponentPolicy::WantsRespec(Spec.Self, C, Ignored));
    bGood &= RunRestBuilds(Test, C, Spec, Steps, After) && Test.TestTrue(TEXT("Replacement, one respec, then the points go to the AD stats only"),
        KindsOf(Steps) == TArray<EKind>({EKind::ReplaceActive, EKind::Respec, EKind::AllocatePoints})
        && Steps[0].Blueprint == TEXT("active_crotch_grab") && Steps[0].Slot == 2
        && LDOpponentPolicyTests::SameCounts(LDOpponentPolicyTests::ById(C, Steps[2].PointCounts), {{Physique, 2}, {Skin, 2}, {Strength, 1}})
        && !LDOpponentPolicy::WantsRespec(After, C, Ignored));
    auto FieldSpec = OnField(Rest);
    FieldSpec.Self.Actives = {{TEXT("active_elbow"), 0}};
    FieldSpec.Self.StatPoints = LDOpponentPolicyTests::StatRows(C, {{Brains, 2}});
    bGood &= Test.TestTrue(TEXT("Brains without an AP active want a respec"), LDOpponentPolicy::WantsRespec(FieldSpec.Self, C, Ignored))
        && Test.TestFalse(TEXT("On the field there is no respec"), LDOpponentPolicy::DecideFollowUpBuild(FieldSpec, C, State, Intent));
    // Moves: M03 on its host while a listed active is learned comes off and goes back onto the active, then stays.
    const auto Is = [](const FLDOpponentBuildIntent& Step, EKind Kind, const TCHAR* Component)
    { return Step.Kind == Kind && Step.Component == Component; };
    auto Move = Rest;
    Move.Self.Actives = {{TEXT("active_elbow"), 0}};
    Move.Self.Components = {Hosted(TEXT("component_long_kick"), ELDComponentTargetKind::Component, TEXT("component_third_punch")),
        Hosted(TEXT("component_third_punch"))};
    FString Moved;
    bGood &= Test.TestTrue(TEXT("M03 on its host is misplaced once its listed active is learned: ") + Error,
        LDOpponentPolicy::SelectComponentMove(Move.Self, C, Moved, Error) && Moved == TEXT("component_long_kick"));
    bGood &= RunRestBuilds(Test, C, Move, Steps, After) && Test.TestTrue(TEXT("Uninstall, then the install onto the active; no move after"),
        Steps.Num() == 2 && Is(Steps[0], EKind::UninstallComponent, TEXT("component_long_kick"))
        && Is(Steps[1], EKind::InstallComponent, TEXT("component_long_kick")) && Steps[1].TargetKind == ELDComponentTargetKind::Active
        && Steps[1].TargetId == TEXT("active_elbow") && !LDOpponentPolicy::SelectComponentMove(After, C, Moved, Error));
    bGood &= Test.TestFalse(TEXT("On the field nothing moves"), LDOpponentPolicy::DecideFollowUpBuild(OnField(Move), C, State, Intent));

    // Reclaim: a replacement returns M08 from the forgotten active; it goes back on in the same rest (here the innate D).
    auto Reclaim = Rest;
    Reclaim.Self.Actives = {{TEXT("active_snark_bolt"), 0}, {TEXT("active_not_mad"), 1}, {TEXT("active_bail_out"), 2}};
    Reclaim.Self.Blueprints = {TEXT("active_elbow")};
    Reclaim.Self.Components = {Hosted(TEXT("component_ram_through"), ELDComponentTargetKind::Active, TEXT("active_bail_out"))};
    bGood &= RunRestBuilds(Test, C, Reclaim, Steps, After) && Test.TestTrue(TEXT("Replacement, then the reclaimed M08 back on the innate D"),
        Steps.Num() == 2 && Steps[0].Kind == EKind::ReplaceActive && Is(Steps[1], EKind::InstallComponent, TEXT("component_ram_through"))
        && Steps[1].TargetKind == ELDComponentTargetKind::Dash && After.Components.Num() == 1 && After.Components[0].bInstalled);
    auto FieldReclaim = OnField(Reclaim);
    FieldReclaim.Self.Blueprints.Reset();
    FieldReclaim.Self.Components = {Owned(TEXT("component_ram_through"), false, true)};
    bGood &= Test.TestFalse(TEXT("On the field a once-installed component stays in the inventory (RestOnly)"),
        LDOpponentPolicy::DecideFollowUpBuild(FieldReclaim, C, State, Intent));

    // Rebinding: B18 bound to W while a bindable active sits in Q rebinds to Q; C01 on it comes off and goes back on.
    auto Bind = Rest;
    Bind.Self.Actives = {{TEXT("active_snark_bolt"), 0}, {TEXT("active_elbow"), 1}};
    FLDOpponentComponent Trick = Hosted(TEXT("component_dirty_trick"), ELDComponentTargetKind::None, TEXT(""), ELDDemoElement::Fire);
    Trick.BoundActive = TEXT("active_elbow");
    Bind.Self.Components = {Trick, Hosted(TEXT("component_fire_skin"), ELDComponentTargetKind::Component, TEXT("component_dirty_trick"))};
    bGood &= RunRestBuilds(Test, C, Bind, Steps, After) && Test.TestTrue(TEXT("B18 off, back on bound to Q with fire, then C01 back onto it"),
        Steps.Num() == 3 && Is(Steps[0], EKind::UninstallComponent, TEXT("component_dirty_trick"))
        && Is(Steps[1], EKind::InstallComponent, TEXT("component_dirty_trick")) && Steps[1].BoundActive == TEXT("active_snark_bolt")
        && Steps[1].Element == ELDDemoElement::Fire && Is(Steps[2], EKind::InstallComponent, TEXT("component_fire_skin"))
        && Steps[2].TargetKind == ELDComponentTargetKind::Component && Steps[2].TargetId == TEXT("component_dirty_trick"));

    // No oscillation: M08 moves onto its listed active, M09 then takes the free D, and neither moves again (TargetConflict).
    auto Dash = Rest;
    Dash.Self.Actives = {{TEXT("active_apology_slide"), 0}};
    Dash.Self.Components = {Owned(TEXT("component_quick_hop"), false, false), Hosted(TEXT("component_ram_through"), ELDComponentTargetKind::Dash)};
    bGood &= RunRestBuilds(Test, C, Dash, Steps, After) && Test.TestTrue(TEXT("M08 to the active, M09 to the free D, then a fixed point"),
        Steps.Num() == 3 && Is(Steps[0], EKind::UninstallComponent, TEXT("component_ram_through"))
        && Is(Steps[1], EKind::InstallComponent, TEXT("component_ram_through")) && Steps[1].TargetKind == ELDComponentTargetKind::Active
        && Steps[1].TargetId == TEXT("active_apology_slide") && Is(Steps[2], EKind::InstallComponent, TEXT("component_quick_hop"))
        && Steps[2].TargetKind == ELDComponentTargetKind::Dash && !LDOpponentPolicy::SelectComponentMove(After, C, Moved, Error));

    // Dismantle: three never-installed common components the build cannot place, in catalog order; a scarce one, a
    // once-installed one and a gap the build may still fill are never given up.
    if (!Test.TestEqual(TEXT("The approved dismantle takes three components"), C.Get().Parameters.DismantleInputCount, int64(3))) { return false; }
    auto Junk = Rest;
    Junk.Self.Actives = {{TEXT("active_elbow"), 0}};
    Junk.Self.Components = {Owned(TEXT("component_after_pin"), false, false), Owned(TEXT("component_hold_down"), false, false),
        Owned(TEXT("component_one_slam"), false, false), Owned(TEXT("component_pick_you"), false, false)};
    TArray<FString> Surrendered;
    const TArray<FString> Expected = {TEXT("component_hold_down"), TEXT("component_one_slam"), TEXT("component_after_pin")};
    bGood &= Test.TestTrue(TEXT("The unusable common components in catalog order, never the scarce one: ") + Error,
        LDOpponentPolicy::SelectDismantle(Junk.Self, C, Surrendered, Error) && Surrendered == Expected);
    bGood &= Test.TestTrue(TEXT("A rest build window dismantles them"),
        LDOpponentPolicy::DecideFollowUpBuild(Junk, C, State, Intent) && Intent.Kind == EKind::Dismantle && Intent.Surrendered == Expected
        && Intent.Key.POI == FName(TEXT("component_hold_down")));
    bGood &= Test.TestFalse(TEXT("On the field nothing is dismantled"), LDOpponentPolicy::DecideFollowUpBuild(OnField(Junk), C, State, Intent));
    auto Once = Junk;
    Once.Self.Components[0].bEverInstalled = true;
    bGood &= Test.TestFalse(TEXT("A once-installed component is never given up, and two are not enough"),
        LDOpponentPolicy::SelectDismantle(Once.Self, C, Surrendered, Error));
    auto Gap = Rest;
    Gap.Self.Actives = {{TEXT("active_elbow"), 0}};
    Gap.Self.Components = {Owned(TEXT("component_big_tab"), false, false), Owned(TEXT("component_hold_down"), false, false),
        Owned(TEXT("component_one_slam"), false, false)};
    bGood &= Test.TestTrue(TEXT("Without a fill the build allows, the unhosted mod counts"),
        LDOpponentPolicy::SelectDismantle(Gap.Self, C, Surrendered, Error) && Surrendered.Num() == 3
        && Surrendered.Last() == TEXT("component_big_tab"));
    Gap.Self.GapFills = {{TEXT("component_big_tab"), TEXT("component_tab_ledger")}};
    bGood &= Test.TestFalse(TEXT("A gap the build may still fill is kept for the fill"),
        LDOpponentPolicy::SelectDismantle(Gap.Self, C, Surrendered, Error))
        && Test.TestFalse(TEXT("And nothing else is to be built"), LDOpponentPolicy::DecideFollowUpBuild(Gap, C, State, Intent));
    // NPC: an empty Q/W/E slot without a pending blueprint buys the NPC's offer before the points, only when growth would charge.
    const int64 NpcPoints = C.Get().Parameters.NpcActivePoints;
    if (!Test.TestTrue(TEXT("The approved content prices the NPC"), NpcPoints > 0)) { return false; }
    const auto BuildOf = [&C](const FLDOpponentDecisionFrame& Of)
    {
        FLDOpponentPolicyState Fresh;
        return LDOpponentPolicy::Decide(Of, C, Fresh).Build.Kind;
    };
    auto Npc = Rest;
    Npc.Self.Actives = {{TEXT("active_elbow"), 0}};
    Npc.Self.UnspentPoints = NpcPoints;
    Npc.Self.NearbyRestService = ELDRestService::SkillMerchant;
    bGood &= Test.TestTrue(TEXT("The NPC rule holds"), LDOpponentPolicy::WantsNpcOffer(Npc.Self, C, 1))
        && Test.TestTrue(TEXT("An empty slot without a blueprint buys the offer before the points"), BuildOf(Npc) == EKind::BuyNpcOffer);
    bGood &= Test.TestTrue(TEXT("DecideFollowUpBuild leaves offers to its caller: it allocates instead"),
        LDOpponentPolicy::DecideFollowUpBuild(Npc, C, State, Intent) && Intent.Kind == EKind::AllocatePoints);
    auto Poor = Npc;
    Poor.Self.UnspentPoints = NpcPoints - 1;
    auto Pending = Npc;
    Pending.Self.Blueprints = {TEXT("active_open_mic")};
    auto Full = Npc;
    Full.Self.Actives = {{TEXT("active_elbow"), 0}, {TEXT("active_not_mad"), 1}, {TEXT("active_snark_bolt"), 2}};
    auto Queued = Npc;
    Queued.Self.QueuedOpportunities = 1;
    auto Offer = Npc;
    Offer.Self.Offer = LDOpponentPolicyTests::ActiveOffer({TEXT("active_open_mic"), TEXT("active_seethe")});
    bGood &= Test.TestTrue(TEXT("Without the price the points are allocated"), BuildOf(Poor) == EKind::AllocatePoints)
        && Test.TestTrue(TEXT("A pending blueprint fills the slot instead"), BuildOf(Pending) == EKind::LearnBlueprint)
        && Test.TestTrue(TEXT("Full Q/W/E: no purchase"), BuildOf(Full) == EKind::AllocatePoints)
        && Test.TestTrue(TEXT("A queued opportunity: no purchase (OfferOutstanding)"), BuildOf(Queued) == EKind::AllocatePoints)
        && Test.TestTrue(TEXT("An offer is chosen first"), BuildOf(Offer) == EKind::ChooseActive)
        && Test.TestTrue(TEXT("On the field there is no NPC"), BuildOf(OnField(Npc)) == EKind::AllocatePoints);

    // The own view feeds these rules: the queued opportunities and B18's bound active.
    FLDProgressionView View;
    View.bValid = true;
    View.Generation = 7;
    View.Level = 3;
    View.BuildRevision = 2;
    View.QueuedActiveOpportunities = 2;
    for (const FLDDemoStatDefinition& Stat : C.Get().Stats)
    {
        FLDStatPointsView Row;
        Row.StatId = FName(*Stat.Id);
        View.StatPoints.Add(Row);
    }
    FLDActiveSlotView& Learned = View.Actives.AddDefaulted_GetRef();
    Learned.DefinitionId = TEXT("active_snark_bolt");
    FLDComponentInstanceView& Bound = View.Components.AddDefaulted_GetRef();
    Bound.DefinitionId = TEXT("component_dirty_trick");
    Bound.Source = ELDComponentSource::LevelUp;
    Bound.SourceLevel = 2;
    Bound.bInstalled = Bound.bEverInstalled = true;
    Bound.Target.BoundActive = TEXT("active_snark_bolt");
    Bound.Target.Element = ELDDemoElement::Fire;
    FLDOpponentSelf Projected;
    bGood &= Test.TestTrue(TEXT("The own view maps the queued opportunities and B18's bound active: ") + Error,
        LDOpponentFrame::ReadOwnBuild(View, C, Projected, Error) && Projected.QueuedOpportunities == 2
        && Projected.Components.Num() == 1 && Projected.Components[0].BoundActive == TEXT("active_snark_bolt")
        && Projected.Components[0].Element == ELDDemoElement::Fire);
    return bGood;
}
// In the rest build window, once nothing is left to build, the body tries its build on a shared dummy (a
// visible contender outside the public roster) with the production engagement; never on anything else, never elsewhere.
bool RunTraining(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(Test, Catalog)) { return false; }
    const FLDDemoContentSnapshot& C = *Catalog;
    using EMode = ELDOpponentMode;
    using EReason = ELDOpponentReason;
    using EBody = ELDOpponentBodyKind;
    FLDOpponentDecisionFrame Base;
    if (!RestFrame(Test, C, Base)) { return false; }
    const FLDOpponentUnit Rival = LDOpponentPolicyTests::Unit(7, ELDOpponentUnitKind::Contender, 60, 0);
    const FLDOpponentUnit Dummy = LDOpponentPolicyTests::Unit(50, ELDOpponentUnitKind::Contender, 100, 0);
    Base.Public.Roster = {Base.Self.ContenderId, Rival.ContenderId};
    Base.Units = {Rival, Dummy};
    const auto Decide = [&C](const FLDOpponentDecisionFrame& Frame)
    {
        FLDOpponentPolicyState State;
        return LDOpponentPolicy::Decide(Frame, C, State);
    };
    const auto Trains = [&Dummy](const FLDOpponentDecision& Decision, EBody Kind)
    { return Decision.Body.Kind == Kind && Decision.Body.Handle == Dummy.Handle; };
    const FLDOpponentUnit* Found = LDOpponentPolicy::TrainingDummy(Base);
    bool bGood = Test.TestTrue(TEXT("The visible contender outside the public roster is a shared dummy"), Found && Found->Handle == Dummy.Handle);
    FLDOpponentDecision Decision = Decide(Base);
    bGood &= Test.TestTrue(TEXT("Nothing to build: the body attacks its dummy, never the roster member in sight"),
        Decision.Mode == EMode::Engage && Decision.Reason == EReason::Train && Trains(Decision, EBody::AttackUnit)
        && Decision.Build.Kind == ELDOpponentBuildKind::None);
    auto Ready = Base;
    FLDOpponentSource Elbow;
    Elbow.Slot = ELDOpponentSourceSlot::Q;
    Elbow.DefinitionId = TEXT("active_elbow");
    Elbow.bReady = true;
    Ready.Self.Actives = {{TEXT("active_elbow"), 0}};
    Ready.Self.Sources = {Elbow};
    Decision = Decide(Ready);
    bGood &= Test.TestTrue(TEXT("A ready active goes first, at the dummy (§6.3 order)"), Decision.Reason == EReason::Train
        && Trains(Decision, EBody::Activate) && Decision.Body.Source == ELDOpponentSourceSlot::Q);
    auto Live = Base;
    Live.Order.Kind = ELDOpponentOrderKind::AttackUnit;
    Live.Order.Status = ELDOpponentOrderStatus::Active;
    Live.Order.Handle = Dummy.Handle;
    Live.Order.Epoch = Dummy.Epoch;
    bGood &= Test.TestTrue(TEXT("A live attack on the dummy is kept, not re-sent"), Trains(Decide(Live), EBody::Keep));
    auto Channel = Base;
    Channel.Self.bChanneling = true;
    bGood &= Test.TestTrue(TEXT("A channel is never cut"), Decide(Channel).Body.Kind == EBody::Keep);
    auto Payout = Base;
    Payout.Public.Timeline.Step = ELDRestStep::Payout;
    bGood &= Test.TestTrue(TEXT("The payout step trains too"), Trains(Decide(Payout), EBody::AttackUnit));
    auto SignUp = Base;
    SignUp.Public.Timeline.SignupOpensAtMs = SignUp.Public.NowMs;
    SignUp.Public.Timeline.StepEndsAtMs = SignUp.Public.NowMs + 1000;
    FLDOpponentPOI Disk;
    Disk.Id = TEXT("pk_signup_0");
    Disk.Kind = ELDOpponentPOIKind::PKSignup;
    Disk.SurfaceId = TEXT("demo_ground");
    Disk.FootCm = Base.Self.FootCm + FVector(300, 0, 0);
    SignUp.SignupPOI = Disk;
    Decision = Decide(SignUp);
    bGood &= Test.TestTrue(TEXT("The open sign-up window walks into the own disk instead of training (站圈报名)"),
        Decision.Mode == EMode::PK && Decision.Reason == EReason::PKSignUp && Decision.Body.Kind == EBody::MoveGround
        && Decision.Body.POI == Disk.Id && Decision.PK.Kind == ELDOpponentPKKind::None);
    SignUp.Public.Timeline.Signups = {Base.Self.ContenderId};
    Decision = Decide(SignUp);
    bGood &= Test.TestTrue(TEXT("Signed up, the body stays in its disk and does not train until the lock"),
        Decision.Reason == EReason::PKSignUp && Decision.Body.Kind == EBody::Keep);
    SignUp.Public.Timeline.Signups.Reset();
    SignUp.Order = Live.Order;
    SignUp.Order.Kind = ELDOpponentOrderKind::Action;
    bGood &= Test.TestTrue(TEXT("An accepted training action finishes before the walk"), Decide(SignUp).Body.Kind == EBody::Keep);

    // Never before or while something is built, never outside the rest build window, never on anything but the dummy.
    auto Building = Base;
    Building.Self.UnspentPoints = 2;
    Decision = Decide(Building);
    bGood &= Test.TestTrue(TEXT("Something to build comes first, without a body intent"),
        Decision.Mode == EMode::Choose && Decision.Build.Kind == ELDOpponentBuildKind::AllocatePoints && Decision.Body.Kind == EBody::None);
    auto InFlight = Base;
    InFlight.bBuildChangeInFlight = true;
    auto Early = Base;
    Early.Self.OpeningPickAtMs = Early.Public.NowMs + 1;
    auto Locked = Base;
    Locked.Public.Timeline.Step = ELDRestStep::Betting;
    auto RosterOnly = Base;
    RosterOnly.Units = {Rival};
    auto Unread = Base;
    Unread.Public.Roster.Reset();
    auto Protected = Base;
    Protected.Units[1].bProtected = true;
    auto DeadBody = Base;
    DeadBody.Self.Phase = ELDLifePhase::Dead;
    bGood &= Test.TestTrue(TEXT("A build in flight holds the training"), Decide(InFlight).Body.Kind == EBody::None)
        && Test.TestTrue(TEXT("Before the drawn moment nothing"), Decide(Early).Body.Kind == EBody::None)
        && Test.TestTrue(TEXT("A locked rest step never trains"), Decide(Locked).Body.Kind == EBody::None)
        && Test.TestTrue(TEXT("Only roster members in sight: nothing to train on"), Decide(RosterOnly).Body.Kind == EBody::None)
        && Test.TestNull(TEXT("An unread roster names no dummy"), LDOpponentPolicy::TrainingDummy(Unread))
        && Test.TestTrue(TEXT("Nor is anything engaged then"), Decide(Unread).Body.Kind == EBody::None)
        && Test.TestTrue(TEXT("A protected unit is not engaged"), Decide(Protected).Body.Kind == EBody::None)
        && Test.TestTrue(TEXT("A dead body never trains"), Decide(DeadBody).Body.Kind == EBody::None);
    Locked.Order = Live.Order;
    bGood &= Test.TestTrue(TEXT("A training order still running when the window closes stops"), Decide(Locked).Body.Kind == EBody::Stop);
    auto Field = Base;
    Field.Public.Phase = ELDMatchPhase::Development;
    Field.Public.Timeline = FLDTimelineView();
    bGood &= Test.TestNull(TEXT("Outside a rest no unit is a dummy"), LDOpponentPolicy::TrainingDummy(Field));
    return bGood;
}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentComponentChoiceTest, "Lockdown.Rules.DemoOpponentPolicy.ComponentChoice",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentComponentChoiceTest::RunTest(const FString& Parameters)
{
    return LDOpponentComponentTests::RunComponentChoice(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentWorkshopTargetsTest, "Lockdown.Rules.DemoOpponentPolicy.WorkshopTargets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentWorkshopTargetsTest::RunTest(const FString& Parameters)
{
    return LDOpponentComponentTests::RunWorkshopTargets(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentComponentsTest, "Lockdown.Rules.DemoOpponentPolicy.Components",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentComponentsTest::RunTest(const FString& Parameters)
{
    return LDOpponentComponentTests::RunPickAndEnable(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentRestRebuildTest, "Lockdown.Rules.DemoOpponentPolicy.RestRebuild",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentRestRebuildTest::RunTest(const FString& Parameters)
{
    return LDOpponentComponentTests::RunRestRebuild(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentNpcGateTest, "Lockdown.Rules.DemoOpponentPolicy.NpcGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentNpcGateTest::RunTest(const FString& Parameters)
{
    return LDOpponentComponentTests::RunNpcGate(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentRestServiceTravelTest, "Lockdown.Rules.DemoOpponentPolicy.RestServiceTravel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentRestServiceTravelTest::RunTest(const FString& Parameters)
{
    return LDOpponentComponentTests::RunRestServiceTravel(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentTrainingTest, "Lockdown.Rules.DemoOpponentPolicy.Training",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentTrainingTest::RunTest(const FString& Parameters)
{
    return LDOpponentComponentTests::RunTraining(*this);
}

#endif
