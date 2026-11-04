#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDCombatControlRules.h"
#include "Combat/LDControlRules.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDAttributeProjection.h"
#include "Navigation/LDOrderRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "../Development/LDDemoContentJson.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace LDCombatSourceRulesTestDetail
{
    // Same isolated approved_base fixture as the evaluator tests; no live-data fallback.
    bool ReadApprovedContent(FAutomationTestBase& Test, TSharedPtr<const FLDDemoContentSnapshot>& Out)
    {
        using namespace LDDemoContent;
        FString Root, Error;
        if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoContentFixtures="), Root) || Root.IsEmpty())
        {
            Test.AddError(TEXT("Missing -LDDemoContentFixtures=<isolated Artifacts directory>; export approved_base first."));
            return false;
        }
        if (!Test.TestTrue(TEXT("Source-rule fixtures are isolated under this project's Artifacts"),
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
        if (!Test.TestTrue(TEXT("approved_base fixture is present"), bFound)) { return false; }
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
        return true;
    }

    const FName Battery(TEXT("item_arcane_battery"));
    const FName Spikes(TEXT("item_phase_anchor"));
    const FName Maul(TEXT("item_echo_maul"));
    const FName Bolt(TEXT("active_arc_bolt"));
    constexpr int64 NowMs = 60000;

    FLDCombatTrack GearTrack(const FGuid& Id, const FGuid& Bound, bool bBound, int64 ReadyAtMs)
    {
        FLDCombatTrack Track;
        Track.IssuedCooldownId = Id;
        Track.Kind = ELDCombatSourceKind::Gear;
        Track.DefinitionId = Battery;
        Track.BoundInstance = Bound;
        Track.bBound = bBound;
        Track.ReadyAtMs = ReadyAtMs;
        return Track;
    }

    FLDEquippedItemValue Item(ELDCombatEntry Entry, const FGuid& Instance, FName Definition)
    {
        FLDEquippedItemValue Value;
        Value.Entry = Entry;
        Value.Instance = Instance;
        Value.DefinitionId = Definition;
        return Value;
    }

    bool Plan(const FLDCombatEntityRecord& Record, const FLDBuildProjectionInput& Input, const FLDDemoContentSnapshot& Content,
        TArray<FLDCombatBindingPlan>& Out, ELDCombatPrepareError& Code)
    {
        FString Error;
        return LDCombatSourceRules::PlanTrackBindings(Record, Input, NowMs, Content, Out, Code, Error);
    }

    FLDObservedUnitView Unit(const FGuid& Handle, const FVector& Feet, ELDObservedUnitKind Kind, const FGuid& Contender)
    {
        FLDObservedUnitView View;
        View.Handle = Handle;
        View.FeetCm = Feet;
        View.Kind = Kind;
        View.ContenderId = Contender;
        return View;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatSourceTracksTest, "Lockdown.Rules.DemoCombatSources.CooldownTracks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatSourceTracksTest::RunTest(const FString& Parameters)
{
    using namespace LDCombatSourceRulesTestDetail;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    if (!ReadApprovedContent(*this, Content)) { return false; }
    const FGuid A = FGuid::NewGuid(), B = FGuid::NewGuid(), C = FGuid::NewGuid();
    TArray<FLDCombatBindingPlan> Out;
    ELDCombatPrepareError Code = ELDCombatPrepareError::None;

    FLDCombatEntityRecord Empty;
    FLDBuildProjectionInput First;
    First.Equipment.Add(Item(ELDCombatEntry::Item1, A, Battery));
    TestTrue(TEXT("A first active copy issues a ready new track"),
        Plan(Empty, First, *Content, Out, Code) && Out.Num() == 1 && Out[0].bNewTrack && Out[0].ReadyAtMs == 0
        && Out[0].Entry == ELDCombatEntry::Item1 && Out[0].Source.ItemInstance == A && Out[0].IssuedCooldownId.IsValid());

    const FGuid Sold = FGuid::NewGuid();
    FLDCombatEntityRecord Debt;
    Debt.Tracks.Add(GearTrack(Sold, A, false, NowMs + 10000));
    FLDBuildProjectionInput Rebuy;
    Rebuy.Equipment.Add(Item(ELDCombatEntry::Item2, B, Battery));
    TestTrue(TEXT("A rebought copy inherits the sold copy's unexpired debt"),
        Plan(Debt, Rebuy, *Content, Out, Code) && Out.Num() == 1 && !Out[0].bNewTrack
        && Out[0].IssuedCooldownId == Sold && Out[0].ReadyAtMs == NowMs + 10000);

    const FGuid Held = FGuid::NewGuid(), Ready = FGuid::NewGuid();
    FLDCombatEntityRecord Pair;
    Pair.Tracks.Add(GearTrack(Held, A, true, NowMs + 5000));
    Pair.Tracks.Add(GearTrack(Ready, C, false, NowMs - 1));
    FLDBuildProjectionInput Both;
    Both.Equipment.Add(Item(ELDCombatEntry::Item1, A, Battery));
    Both.Equipment.Add(Item(ELDCombatEntry::Item2, B, Battery));
    const bool bBoth = Plan(Pair, Both, *Content, Out, Code) && Out.Num() == 2;
    TestTrue(TEXT("A held copy keeps its own track and a second copy takes the free ready one"),
        bBoth && Out[0].Source.ItemInstance == A && Out[0].IssuedCooldownId == Held && Out[0].ReadyAtMs == NowMs + 5000
        && Out[1].Source.ItemInstance == B && Out[1].IssuedCooldownId == Ready && !Out[1].bNewTrack);

    const FGuid Early = FGuid::NewGuid(), Late = FGuid::NewGuid();
    FLDCombatEntityRecord TwoDebts;
    TwoDebts.Tracks.Add(GearTrack(Early, A, false, NowMs + 3000));
    TwoDebts.Tracks.Add(GearTrack(Late, B, false, NowMs + 8000));
    FLDBuildProjectionInput One;
    One.Equipment.Add(Item(ELDCombatEntry::Item1, C, Battery));
    TestTrue(TEXT("Debt first: the latest unexpired unbound track is taken"),
        Plan(TwoDebts, One, *Content, Out, Code) && Out.Num() == 1 && Out[0].IssuedCooldownId == Late
        && Out[0].ReadyAtMs == NowMs + 8000);

    FLDCombatTrack Learned;
    Learned.IssuedCooldownId = FGuid::NewGuid();
    Learned.Kind = ELDCombatSourceKind::Learned;
    Learned.DefinitionId = Bolt;
    Learned.ReadyAtMs = NowMs + 4000;
    FLDCombatEntityRecord Relearn;
    Relearn.Tracks.Add(Learned);
    FLDBuildProjectionInput Active;
    FLDLearnedActiveValue Skill;
    Skill.AbilityId = Bolt;
    Skill.Rank = 1;
    Skill.Entry = ELDCombatEntry::W;
    Active.Actives.Add(Skill);
    TestTrue(TEXT("A learned definition returns to its single track and end time in any slot"),
        Plan(Relearn, Active, *Content, Out, Code) && Out.Num() == 1 && Out[0].Entry == ELDCombatEntry::W
        && Out[0].IssuedCooldownId == Learned.IssuedCooldownId && Out[0].ReadyAtMs == NowMs + 4000);

    FLDBuildProjectionInput Passive;
    Passive.Equipment.Add(Item(ELDCombatEntry::Item1, A, Maul));
    TestTrue(TEXT("Gear without an active binds no cooldown track"),
        Plan(Empty, Passive, *Content, Out, Code) && Out.IsEmpty());

    FLDBuildProjectionInput Duplicate;
    Duplicate.Equipment.Add(Item(ELDCombatEntry::Item1, A, Battery));
    Duplicate.Equipment.Add(Item(ELDCombatEntry::Item2, A, Battery));
    TestTrue(TEXT("One instance in two slots is refused"),
        !Plan(Empty, Duplicate, *Content, Out, Code) && Code == ELDCombatPrepareError::InvalidInput);
    return true;
}

// D19/D20: the innate dash terms come from the approved character; the spikes passive halves the cooldown and lengthens
// the distance once per family; the shared gate refuses a cooling, unaffordable or rooted dash; build changes never
// plan or rebind the innate track.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatInnateDashTest, "Lockdown.Rules.DemoCombatSources.InnateDash",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatInnateDashTest::RunTest(const FString& Parameters)
{
    using namespace LDCombatSourceRulesTestDetail;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    if (!ReadApprovedContent(*this, Content)) { return false; }
    const FLDDemoEquipmentActive& Dash = Content->Get().Character.Dash;
    if (!TestTrue(TEXT("The approved character projects one executable dash"),
        Dash.Target.Mode == ELDDemoTargetMode::DirectionDash && Dash.WindupMs == 0 && Dash.Rank.Effects.Num() == 1
        && Dash.Rank.Effects[0].GetOp() == ELDDemoEffectOp::Dash && Dash.Rank.CooldownMs > Dash.Rank.Effects[0].DurationMs))
    { return false; }
    const FLDDemoEffectDefinition& Move = Dash.Rank.Effects[0];
    const FLDDemoEquipmentDefinition* SpikesItem = nullptr;
    FString Error;
    if (!TestTrue(TEXT("The spikes are approved equipment"), Content->FindEquipment(Spikes.ToString(), SpikesItem, Error) && SpikesItem))
    { return false; }
    const FLDDemoEffectDefinition* Boost = SpikesItem->HasPassive ? SpikesItem->Passive.Effects.FindByPredicate(
        [](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp() == ELDDemoEffectOp::DashBoost; }) : nullptr;
    TestTrue(TEXT("The spikes have no active and one dash boost passive (1/2 show no active)"), !SpikesItem->HasActive && Boost);
    if (!Boost) { return false; }
    const auto Families = [&](const TArray<FString>& Items, TArray<FLDEquipmentPassiveFamily>& Out)
    {
        const auto Projected = LDAttributeProjection::Project(FLDAttributeBuildValues(1, {}, {}, Items), *Content);
        if (!Projected.IsSuccess()) { return false; }
        Out = Projected.Value->EquipmentPassives;
        return true;
    };
    TArray<FLDEquipmentPassiveFamily> None, One, Two, Other;
    FLDInnateDashTerms Base, Single, Double, Unrelated;
    TestTrue(TEXT("Bare terms are the approved character dash"),
        Families({}, None) && LDCombatSourceRules::InnateDashTerms(*Content, None, Base, Error) && !Base.bBoosted
        && Base.CooldownMs == Dash.Rank.CooldownMs && Base.DurationMs == Move.DurationMs && Base.ManaCostP2 == Dash.Rank.ManaCostP2
        && Base.DistanceCm.Numerator * Move.DistanceCm.Denominator == Move.DistanceCm.Numerator * Base.DistanceCm.Denominator);
    const int64 Scaled = Dash.Rank.CooldownMs * Boost->CooldownRatioBp;
    const int64 BoostedCooldown = Scaled / 10000 + (Scaled % 10000 != 0 ? 1 : 0);
    TestTrue(TEXT("Worn spikes scale the cooldown and the distance, never the duration"),
        Families({Spikes.ToString()}, One) && LDCombatSourceRules::InnateDashTerms(*Content, One, Single, Error) && Single.bBoosted
        && Single.CooldownMs == BoostedCooldown && Single.DurationMs == Move.DurationMs
        && Single.DistanceCm.Numerator * Move.DistanceCm.Denominator * 10000
            == Move.DistanceCm.Numerator * (10000 + Boost->DistanceBonusBp) * Single.DistanceCm.Denominator);
    TestTrue(TEXT("Two pairs of spikes apply the same-named passive once"),
        Families({Spikes.ToString(), Spikes.ToString()}, Two) && Two.Num() == 1
        && LDCombatSourceRules::InnateDashTerms(*Content, Two, Double, Error)
        && Double.CooldownMs == Single.CooldownMs && Double.DistanceCm.Numerator == Single.DistanceCm.Numerator
        && Double.DistanceCm.Denominator == Single.DistanceCm.Denominator);
    TestTrue(TEXT("Another equipment passive leaves the dash unchanged"),
        Families({Maul.ToString()}, Other) && LDCombatSourceRules::InnateDashTerms(*Content, Other, Unrelated, Error)
        && !Unrelated.bBoosted && Unrelated.CooldownMs == Base.CooldownMs);

    const FLDDemoPolicies& Policies = Content->Get().Policies;
    TArray<FLDCombatRoot> Roots;
    FLDActiveGateInput Gate;
    Gate.NowMs = NowMs;
    Gate.ReadyAtMs = NowMs;
    Gate.ManaCostP2 = Base.ManaCostP2;
    Gate.MpP2 = Base.ManaCostP2;
    Gate.bDash = true;
    Gate.Roots = &Roots;
    TestTrue(TEXT("A dash whose track ends now is usable"), LDCombatSourceRules::ActiveGate(Gate, Policies) == ELDCombatReject::None);
    Gate.ReadyAtMs = NowMs + 1;
    TestTrue(TEXT("A cooling dash is refused"), LDCombatSourceRules::ActiveGate(Gate, Policies) == ELDCombatReject::Cooldown);
    Gate.ReadyAtMs = NowMs;
    Roots.Add({FGuid::NewGuid(), FGuid::NewGuid(), NowMs + 500});
    TestTrue(TEXT("A rooted body cannot dash (POLICY.RootBlocks Dash)"),
        LDCombatSourceRules::ActiveGate(Gate, Policies) == ELDCombatReject::ControlBlocked);
    Gate.bDash = false;
    TestTrue(TEXT("Root does not block a non-moving active"), LDCombatSourceRules::ActiveGate(Gate, Policies) == ELDCombatReject::None);
    Gate.bDash = true;
    Gate.NowMs = NowMs + 500;
    Gate.ReadyAtMs = NowMs + 500;
    TestTrue(TEXT("The Root ends exactly at its deadline and the dash is free again"),
        LDCombatSourceRules::ActiveGate(Gate, Policies) == ELDCombatReject::None);
    Gate.ManaCostP2 = 100;
    Gate.MpP2 = 99;
    TestTrue(TEXT("An unaffordable cost is refused before the Root check"),
        LDCombatSourceRules::ActiveGate(Gate, Policies) == ELDCombatReject::InsufficientMana);
    Gate.ManaCostP2 = Gate.MpP2 = 0;
    Gate.bCheckBusy = Gate.bAbilityInProgress = true;
    TestTrue(TEXT("Admission refuses a dash during another ability windup"),
        LDCombatSourceRules::ActiveGate(Gate, Policies) == ELDCombatReject::Busy);
    Gate.bCheckBusy = Gate.bAbilityInProgress = false;
    Gate.bControlBlocked = true;
    TestTrue(TEXT("A stunned or taunted body cannot dash"), LDCombatSourceRules::ActiveGate(Gate, Policies) == ELDCombatReject::ControlBlocked);
    Gate.bDash = false;
    TestTrue(TEXT("A blocked entry refuses any other active too"),
        LDCombatSourceRules::ActiveGate(Gate, Policies) == ELDCombatReject::ControlBlocked);
    Gate.bControlBlocked = false;
    Gate.bDisplacementBlocked = true;
    TestTrue(TEXT("A displacement block (Root/Stun/taunt) leaves a non-moving active usable"),
        LDCombatSourceRules::ActiveGate(Gate, Policies) == ELDCombatReject::None);
    Gate.bDash = true;
    TestTrue(TEXT("A displacement block refuses the dash"), LDCombatSourceRules::ActiveGate(Gate, Policies) == ELDCombatReject::ControlBlocked);
    Gate.bDisplacementBlocked = false;
    // The matrix entries the gate reads: a taunt blocks Cast/Gear/Displacement, never Move (forced chase) or Basic
    // (checked against the taunter); a disarm alone leaves the dash usable.
    TOptional<FLDCombatTaunt> Taunt;
    Taunt.Emplace();
    Taunt->ExpiresAtMs = NowMs + 1000;
    TestTrue(TEXT("A taunt blocks the D dash (Cast and Displacement)"),
        LDControl::TauntBlocks(Taunt, NowMs, ELDCombatControlAction::Cast) && LDControl::TauntBlocks(Taunt, NowMs, ELDCombatControlAction::Displacement));
    TestTrue(TEXT("A taunt blocks items"), LDControl::TauntBlocks(Taunt, NowMs, ELDCombatControlAction::Gear));
    TestFalse(TEXT("A taunt leaves movement and basic attacks to the forced chase"),
        LDControl::TauntBlocks(Taunt, NowMs, ELDCombatControlAction::Move) || LDControl::TauntBlocks(Taunt, NowMs, ELDCombatControlAction::Basic));
    TestFalse(TEXT("An ended taunt blocks nothing"), LDControl::TauntBlocks(Taunt, NowMs + 1000, ELDCombatControlAction::Cast));
    TArray<FLDCombatRoot> NoControl, Disarms;
    Disarms.Add({FGuid::NewGuid(), FGuid::NewGuid(), NowMs + 500});
    TestFalse(TEXT("A disarm leaves the dash usable"),
        LDCombatControl::Blocks(NoControl, Disarms, NoControl, NowMs, Policies, ELDCombatControlAction::Cast)
        || LDCombatControl::Blocks(NoControl, Disarms, NoControl, NowMs, Policies, ELDCombatControlAction::Displacement));
    TestTrue(TEXT("A stun blocks the dash"),
        LDCombatControl::Blocks(NoControl, NoControl, Disarms, NowMs, Policies, ELDCombatControlAction::Displacement));

    // Cooldown debt: buying or selling spikes is a build change, which plans only learned and gear tracks; the innate
    // track keeps the absolute end fixed at its release.
    FLDCombatEntityRecord Record;
    FLDCombatTrack Innate;
    Innate.IssuedCooldownId = FGuid::NewGuid();
    Innate.Kind = ELDCombatSourceKind::Innate;
    Innate.DefinitionId = LDCombatSourceRules::InnateDashId();
    Innate.bBound = true;
    Innate.ReadyAtMs = NowMs + Base.CooldownMs;
    Record.Tracks.Add(Innate);
    FLDBuildProjectionInput Wear;
    Wear.Equipment.Add(Item(ELDCombatEntry::Item1, FGuid::NewGuid(), Spikes));
    TArray<FLDCombatBindingPlan> Out;
    ELDCombatPrepareError Code = ELDCombatPrepareError::None;
    TestTrue(TEXT("Wearing spikes plans no binding and never touches the innate track"),
        Plan(Record, Wear, *Content, Out, Code) && Out.IsEmpty() && Record.Tracks[0].ReadyAtMs == NowMs + Base.CooldownMs);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDOrderAttackMoveCandidateTest, "Lockdown.Rules.DemoOrders.AttackMoveCandidate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDOrderAttackMoveCandidateTest::RunTest(const FString& Parameters)
{
    using LDCombatSourceRulesTestDetail::Unit;
    const FGuid Self = FGuid::NewGuid(), Other = FGuid::NewGuid();
    const FGuid Low(1, 0, 0, 0), High(2, 0, 0, 0);
    TArray<FLDObservedUnitView> Units;
    Units.Add(Unit(FGuid(9, 0, 0, 0), FVector(0, 0, 0), ELDObservedUnitKind::Contender, Self));
    Units.Add(Unit(High, FVector(100, 0, 0), ELDObservedUnitKind::Contender, Other));
    Units.Add(Unit(Low, FVector(0, 100, 0), ELDObservedUnitKind::CampMember, FGuid()));
    Units.Add(Unit(FGuid(3, 0, 0, 0), FVector(500, 0, 0), ELDObservedUnitKind::Contender, FGuid::NewGuid()));
    TestEqual(TEXT("Equal planar distance takes the smaller observer handle, never the attacker"),
        LDOrderRules::NearestCandidate(Units, FVector(0, 0, 50), 440., Self), 2);
    Units.Add(Unit(FGuid(4, 0, 0, 0), FVector(60, 0, 0), ELDObservedUnitKind::Contender, Other));
    TestEqual(TEXT("The nearest visible unit wins"), LDOrderRules::NearestCandidate(Units, FVector::ZeroVector, 440., Self), 4);
    TestEqual(TEXT("Units beyond the acquisition radius are not candidates"),
        LDOrderRules::NearestCandidate(Units, FVector(1000, 0, 0), 440., Self), INDEX_NONE);
    TestEqual(TEXT("A nonpositive radius has no candidate"), LDOrderRules::NearestCandidate(Units, FVector::ZeroVector, 0., Self), INDEX_NONE);
    TestEqual(TEXT("No visible unit, no candidate"),
        LDOrderRules::NearestCandidate(TArray<FLDObservedUnitView>(), FVector::ZeroVector, 440., Self), INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDOrderBookIsolationTest, "Lockdown.Rules.DemoOrders.BookIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// The controller and its book outlive a match while the authority clock restarts at 0, so a throttle armed late
// in one match must never hold the next match's orders in Queued.
bool FLDOrderBookIsolationTest::RunTest(const FString& Parameters)
{
    const FLDMoveIdentity Old{1, 2, 40, 7};
    FLDOrderBook Played;
    Played.Bind(Old);
    Played.Received = Old;
    Played.ArmActivation(410.0, 0.1);
    TestTrue(TEXT("Within one binding the throttle holds the next activation for its interval only"),
        !Played.ActivationDue(410.05) && Played.ActivationDue(410.1));

    FLDOrderBook Reset = Played;
    Reset.Reset();
    TestTrue(TEXT("A match reset drops the throttle: the next match may activate at clock 0"), Reset.ActivationDue(0.0));
    TestTrue(TEXT("A match reset drops the binding and the request watermark"),
        !Reset.IsBoundTo(Old.Match, Old.Life) && Reset.Received == FLDMoveIdentity());

    const FLDMoveIdentity Next{2, 1, 41, 7};
    FLDOrderBook Rebound = Played;
    TestFalse(TEXT("A new match's identity is not the old book's"), Rebound.IsBoundTo(Next.Match, Next.Life));
    Rebound.Bind(Next);
    const FLDMoveIdentity Fresh{2, 1, 0, 0};
    TestTrue(TEXT("Binding a new match starts the throttle and the watermark over"),
        Rebound.IsBoundTo(Next.Match, Next.Life) && Rebound.ActivationDue(0.0) && Rebound.Received == Fresh);

    FLDOrderBook Respawned = Played;
    Respawned.Bind({1, 3, 41, 8});
    TestTrue(TEXT("A new life of the same match starts its own book"),
        Respawned.IsBoundTo(1, 3) && Respawned.Received.Order == 0 && Respawned.ActivationDue(410.05));
    return true;
}

#endif
