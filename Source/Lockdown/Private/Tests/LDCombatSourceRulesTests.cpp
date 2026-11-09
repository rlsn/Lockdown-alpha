#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDCombatControlRules.h"
#include "Combat/LDControlRules.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDAttributeProjection.h"
#include "Navigation/LDOrderRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/LDTestApprovedFixture.h"

namespace LDCombatSourceRulesTestDetail
{
    const FName Battery(TEXT("item_arcane_battery"));
    const FName Spikes(TEXT("item_phase_anchor"));
    const FName Maul(TEXT("item_echo_maul"));
    const FName Bolt(TEXT("active_snark_bolt"));
    constexpr int64 NowMs = 60000;

    FLDEquippedItemValue Item(uint8 Slot, const FGuid& Instance, FName Definition)
    {
        FLDEquippedItemValue Value;
        Value.Slot = Slot;
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
    if (!LDTestApprovedFixture::Read(*this, Content)) { return false; }
    TArray<FLDCombatBindingPlan> Out;
    ELDCombatPrepareError Code = ELDCombatPrepareError::None;

    FLDCombatEntityRecord Empty;
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

    TestTrue(TEXT("A first learn issues a ready new track"),
        Plan(Empty, Active, *Content, Out, Code) && Out.Num() == 1 && Out[0].bNewTrack && Out[0].ReadyAtMs == 0
        && Out[0].Entry == ELDCombatEntry::W && Out[0].Source.Kind == ELDCombatSourceKind::Learned && Out[0].IssuedCooldownId.IsValid());

    FLDBuildProjectionInput Worn;
    Worn.Equipment.Add(Item(0, FGuid::NewGuid(), Battery));
    Worn.Equipment.Add(Item(1, FGuid::NewGuid(), Maul));
    TestTrue(TEXT("Equipment binds no cooldown track"),
        Plan(Empty, Worn, *Content, Out, Code) && Out.IsEmpty());

    FLDBuildProjectionInput Duplicate = Active;
    Skill.Entry = ELDCombatEntry::Q;
    Duplicate.Actives.Add(Skill);
    TestTrue(TEXT("One definition learned in two entries is refused"),
        !Plan(Relearn, Duplicate, *Content, Out, Code) && Code == ELDCombatPrepareError::InvalidInput);
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
    if (!LDTestApprovedFixture::Read(*this, Content)) { return false; }
    const FLDDemoDashDefinition& Dash = Content->Get().Character.Dash;
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
    TestTrue(TEXT("The spikes hold one dash boost passive"), Boost != nullptr);
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
        Families({}, None) && LDCombatSourceRules::InnateDashTerms(*Content, None, {}, Base, Error) && !Base.bBoosted
        && Base.CooldownMs == Dash.Rank.CooldownMs && Base.DurationMs == Move.DurationMs && Base.ManaCostP2 == Dash.Rank.ManaCostP2
        && Base.DistanceCm.Numerator * Move.DistanceCm.Denominator == Move.DistanceCm.Numerator * Base.DistanceCm.Denominator);
    const int64 Scaled = Dash.Rank.CooldownMs * Boost->CooldownRatioBp;
    const int64 BoostedCooldown = Scaled / 10000 + (Scaled % 10000 != 0 ? 1 : 0);
    TestTrue(TEXT("Worn spikes scale the cooldown and the distance, never the duration"),
        Families({Spikes.ToString()}, One) && LDCombatSourceRules::InnateDashTerms(*Content, One, {}, Single, Error) && Single.bBoosted
        && Single.CooldownMs == BoostedCooldown && Single.DurationMs == Move.DurationMs
        && Single.DistanceCm.Numerator * Move.DistanceCm.Denominator * 10000
            == Move.DistanceCm.Numerator * (10000 + Boost->DistanceBonusBp) * Single.DistanceCm.Denominator);
    TestTrue(TEXT("Two pairs of spikes apply the same-named passive once"),
        Families({Spikes.ToString(), Spikes.ToString()}, Two) && Two.Num() == 1
        && LDCombatSourceRules::InnateDashTerms(*Content, Two, {}, Double, Error)
        && Double.CooldownMs == Single.CooldownMs && Double.DistanceCm.Numerator == Single.DistanceCm.Numerator
        && Double.DistanceCm.Denominator == Single.DistanceCm.Denominator);
    TestTrue(TEXT("Another equipment passive leaves the dash unchanged"),
        Families({Maul.ToString()}, Other) && LDCombatSourceRules::InnateDashTerms(*Content, Other, {}, Unrelated, Error)
        && !Unrelated.bBoosted && Unrelated.CooldownMs == Base.CooldownMs);
    // #147: passive_special_forces advanced ("dash (D) cooldown -30%") boosts the dash like an equipment family; its basic rank does not.
    FLDInnateDashTerms Learned, LearnedBasic;
    const int64 LearnedScaled = Dash.Rank.CooldownMs * 7000;
    TestTrue(TEXT("Advanced special forces: dash cooldown x0.70, distance unchanged"),
        LDCombatSourceRules::InnateDashTerms(*Content, None, {FLDLearnedTriggerTerm{TEXT("passive_special_forces"), 2, 5}}, Learned, Error)
        && Learned.bBoosted && Learned.CooldownMs == LearnedScaled / 10000 + (LearnedScaled % 10000 != 0 ? 1 : 0)
        && Learned.DistanceCm.Numerator * Move.DistanceCm.Denominator == Move.DistanceCm.Numerator * Learned.DistanceCm.Denominator);
    AddInfo(Error);
    TestTrue(TEXT("Basic special forces leaves the dash unchanged"),
        LDCombatSourceRules::InnateDashTerms(*Content, None, {FLDLearnedTriggerTerm{TEXT("passive_special_forces"), 1, 5}}, LearnedBasic, Error)
        && !LearnedBasic.bBoosted && LearnedBasic.CooldownMs == Base.CooldownMs);
    // #112 §5: the same boost lengthens and quickens every character active that displaces its caster
    // (active_bail_out basic: approved 350 -> 437.5 cm, base cooldown x0.80; upgraded +35% / x0.72).
    const FLDDemoAbilityDefinition* LungeSkill = nullptr;
    const FLDDemoAbilityDefinition* BoltSkill = nullptr;
    if (TestTrue(TEXT("Bail out and the bolt are approved"), Content->FindAbility(TEXT("active_bail_out"), LungeSkill, Error) && LungeSkill
        && !LungeSkill->Ranks.IsEmpty() && Content->FindAbility(Bolt.ToString(), BoltSkill, Error) && BoltSkill && !BoltSkill->Ranks.IsEmpty()))
    {
        const FLDDemoEffectDefinition* Lunge = LungeSkill->Ranks[0].Effects.FindByPredicate(
            [](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp() == ELDDemoEffectOp::Dash; });
        TestTrue(TEXT("Bail out displaces its caster; a bolt does not"), Lunge
            && LDCombatSourceRules::DisplacesCaster(LungeSkill->Ranks[0]) && !LDCombatSourceRules::DisplacesCaster(BoltSkill->Ranks[0]));
        int64 CooldownRatioBp = 0, DistanceBonusBp = 0;
        TestTrue(TEXT("Worn spikes: bail out goes 25% farther and its base cooldown is x0.80"),
            Lunge && LDCombatSourceRules::SkillDisplacementBoost(*Content, One, CooldownRatioBp, DistanceBonusBp, Error)
            && CooldownRatioBp == 8000 && DistanceBonusBp == 2500
            && FMath::IsNearlyEqual(double(Lunge->DistanceCm.Numerator) / double(Lunge->DistanceCm.Denominator)
                * double(10000 + DistanceBonusBp) / 10000., 437.5));
        TArray<FLDEquipmentPassiveFamily> Upgraded = One;
        if (Upgraded.Num() == 1) { Upgraded[0].bUpgraded = true; }
        TestTrue(TEXT("Upgraded spikes: +35% and x0.72"), Upgraded.Num() == 1
            && LDCombatSourceRules::SkillDisplacementBoost(*Content, Upgraded, CooldownRatioBp, DistanceBonusBp, Error)
            && CooldownRatioBp == 7200 && DistanceBonusBp == 3500);
    }

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
    // The matrix entries the gate reads: a taunt blocks Cast/Displacement, never Move (forced chase) or Basic
    // (checked against the taunter); a disarm alone leaves the dash usable.
    TOptional<FLDCombatTaunt> Taunt;
    Taunt.Emplace();
    Taunt->ExpiresAtMs = NowMs + 1000;
    TestTrue(TEXT("A taunt blocks the D dash (Cast and Displacement)"),
        LDControl::TauntBlocks(Taunt, NowMs, ELDCombatControlAction::Cast) && LDControl::TauntBlocks(Taunt, NowMs, ELDCombatControlAction::Displacement));
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

    // Buying or selling spikes is a build change, which plans only learned tracks; the innate
    // track keeps the absolute end fixed at its release.
    FLDCombatEntityRecord Record;
    FLDCombatTrack Innate;
    Innate.IssuedCooldownId = FGuid::NewGuid();
    Innate.Kind = ELDCombatSourceKind::Innate;
    Innate.DefinitionId = LDCombatSourceRules::InnateDashId();
    Innate.ReadyAtMs = NowMs + Base.CooldownMs;
    Record.Tracks.Add(Innate);
    FLDBuildProjectionInput Wear;
    Wear.Equipment.Add(Item(0, FGuid::NewGuid(), Spikes));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatCommittedBasicReleaseTest, "Lockdown.Rules.DemoCombatSources.CommittedBasicRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 2026-11-07 user decisions: once a basic windup legally started, its release lands on the committed target life however
// far it ran, as long as that life is still observed and in clear sight; the target life ending, losing it from sight or
// the attacker's own control ends the swing.
bool FLDCombatCommittedBasicReleaseTest::RunTest(const FString& Parameters)
{
    FLDCombatBodyRef Target;
    Target.Victim.Kind = ELDVictimKind::Contender;
    Target.Victim.EntityId = FGuid::NewGuid();
    Target.Victim.Life = 3;
    Target.BodyIdentity = FGuid::NewGuid();
    FLDBasicReleaseInput Gate;
    Gate.Committed = Target;
    Gate.Current = Target;
    Gate.bTargetPresent = true;
    Gate.bObserved = true;
    Gate.bClearSight = true;
    // The gate has no distance input: a target that ran out of attack range during the windup is the same standing life,
    // and the swing lands on it while it is still seen.
    TestTrue(TEXT("A started windup hits a seen target that ran out of range"),
        LDCombatSourceRules::BasicReleaseGate(Gate) == ELDCombatReject::None);
    FLDBasicReleaseInput Unseen = Gate;
    Unseen.bObserved = false;
    TestTrue(TEXT("A target lost from sight during the windup ends the swing"),
        LDCombatSourceRules::BasicReleaseGate(Unseen) == ELDCombatReject::TargetUnavailable);
    FLDBasicReleaseInput Walled = Gate;
    Walled.bClearSight = false;
    TestTrue(TEXT("A target behind a wall at the release ends the swing"),
        LDCombatSourceRules::BasicReleaseGate(Walled) == ELDCombatReject::Blocked);
    FLDBasicReleaseInput Camp = Gate;
    Camp.Committed.Victim.Kind = Camp.Current.Victim.Kind = ELDVictimKind::CampMember;
    TestTrue(TEXT("A camp member target lands the same way"), LDCombatSourceRules::BasicReleaseGate(Camp) == ELDCombatReject::None);

    FLDBasicReleaseInput Dead = Gate;
    Dead.bCurrentPendingDeath = true;
    TestTrue(TEXT("A target that died during the windup cancels the swing"),
        LDCombatSourceRules::BasicReleaseGate(Dead) == ELDCombatReject::TargetUnavailable);
    FLDBasicReleaseInput Gone = Gate;
    Gone.Current = FLDCombatBodyRef();
    TestTrue(TEXT("A retired target body (dead and removed) cancels the swing"),
        LDCombatSourceRules::BasicReleaseGate(Gone) == ELDCombatReject::TargetUnavailable);
    FLDBasicReleaseInput Respawned = Gate;
    Respawned.Current.Victim.Life = 4;
    Respawned.Current.BodyIdentity = FGuid::NewGuid();
    TestTrue(TEXT("A new life of the target cancels the swing"),
        LDCombatSourceRules::BasicReleaseGate(Respawned) == ELDCombatReject::TargetUnavailable);
    FLDBasicReleaseInput Rebodied = Gate;
    Rebodied.Current.BodyIdentity = FGuid::NewGuid();
    TestTrue(TEXT("Another body identity of the same life number cancels the swing"),
        LDCombatSourceRules::BasicReleaseGate(Rebodied) == ELDCombatReject::TargetUnavailable);
    FLDBasicReleaseInput Left = Gate;
    Left.bTargetPresent = false;
    TestTrue(TEXT("A target that left the match cancels the swing"),
        LDCombatSourceRules::BasicReleaseGate(Left) == ELDCombatReject::TargetUnavailable);
    FLDBasicReleaseInput Self = Gate;
    Self.bSelf = true;
    TestTrue(TEXT("The attacker itself is never a release target"),
        LDCombatSourceRules::BasicReleaseGate(Self) == ELDCombatReject::TargetUnavailable);
    FLDBasicReleaseInput Unbound = Gate;
    Unbound.Committed = Unbound.Current = FLDCombatBodyRef();
    TestTrue(TEXT("A swing without a committed life lands on nobody"),
        LDCombatSourceRules::BasicReleaseGate(Unbound) == ELDCombatReject::TargetUnavailable);

    FLDBasicReleaseInput Disarmed = Gate;
    Disarmed.bAttackBlocked = true;
    TestTrue(TEXT("The attacker's own Disarm/Stun/knockback during the windup still ends it"),
        LDCombatSourceRules::BasicReleaseGate(Disarmed) == ELDCombatReject::ControlBlocked);
    Disarmed.bCurrentPendingDeath = true;
    TestTrue(TEXT("The attacker's control is judged before the target"),
        LDCombatSourceRules::BasicReleaseGate(Disarmed) == ELDCombatReject::ControlBlocked);
    FLDBasicReleaseInput Taunted = Gate;
    Taunted.bTauntAllows = false;
    TestTrue(TEXT("A taunt toward another body during the windup still ends it"),
        LDCombatSourceRules::BasicReleaseGate(Taunted) == ELDCombatReject::ControlBlocked);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDOrderChasePressTest, "Lockdown.Rules.DemoOrders.ChasePress",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// A basic-attack chase at a similar speed must not idle: approach, windup in range, certain hit, keep chasing.
bool FLDOrderChasePressTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("A basic still winding up holds the chase"),
        LDOrderRules::ActionHoldsChase(true, ELDCombatEntry::Basic, 1250, 1249));
    TestFalse(TEXT("A basic due now releases in this phase and frees the chase at once"),
        LDOrderRules::ActionHoldsChase(true, ELDCombatEntry::Basic, 1250, 1250));
    TestTrue(TEXT("An ability windup due now still holds the body"),
        LDOrderRules::ActionHoldsChase(true, ELDCombatEntry::Q, 1250, 1250));
    TestFalse(TEXT("No action holds nothing"), LDOrderRules::ActionHoldsChase(false, ELDCombatEntry::None, 0, 1250));

    LDOrderRules::FChasePressInput Press;
    Press.bInRange = true;
    Press.bAttackAllowed = true;
    Press.AttackerFootCm = FVector(0, 0, 0);
    Press.PreviousTargetFootCm = FVector(200, 0, 0);
    Press.TargetFootCm = FVector(210, 0, 0);
    TestTrue(TEXT("Waiting for the beat in range, a target walking away is followed"), LDOrderRules::ChasePress(Press));
    LDOrderRules::FChasePressInput Still = Press;
    Still.PreviousTargetFootCm = Still.TargetFootCm;
    Still.bWasPressing = true;
    TestTrue(TEXT("A still sample keeps pressing"), LDOrderRules::ChasePress(Still));
    Still.bWasPressing = false;
    TestFalse(TEXT("A still sample keeps holding"), LDOrderRules::ChasePress(Still));
    LDOrderRules::FChasePressInput Closer = Press;
    Closer.bWasPressing = true;
    Closer.TargetFootCm = FVector(190, 0, 0);
    TestFalse(TEXT("A target walking closer is waited for"), LDOrderRules::ChasePress(Closer));
    LDOrderRules::FChasePressInput Sideways = Press;
    Sideways.TargetFootCm = FVector(200, 10, 0);
    TestFalse(TEXT("A sideways step does not start a press"), LDOrderRules::ChasePress(Sideways));
    LDOrderRules::FChasePressInput Jump = Press;
    Jump.bWasPressing = true;
    Jump.PreviousTargetFootCm = FVector(-500, 0, 0);
    TestFalse(TEXT("A sample jump (another target, a displacement) is no walk"), LDOrderRules::ChasePress(Jump));
    LDOrderRules::FChasePressInput Acting = Press;
    Acting.bWasPressing = Acting.bActing = true;
    TestFalse(TEXT("An action in progress never presses"), LDOrderRules::ChasePress(Acting));
    LDOrderRules::FChasePressInput Ready = Press;
    Ready.bWasPressing = Ready.bBeatReady = true;
    TestFalse(TEXT("A ready beat never presses (the windup starts instead)"), LDOrderRules::ChasePress(Ready));
    LDOrderRules::FChasePressInput CloseIn = Press;
    CloseIn.bWasPressing = CloseIn.bCloseIn = true;
    TestFalse(TEXT("Closing in after a refused start never presses"), LDOrderRules::ChasePress(CloseIn));
    LDOrderRules::FChasePressInput Out = Press;
    Out.bWasPressing = true;
    Out.bInRange = false;
    TestFalse(TEXT("Out of range is the ordinary chase, not a press"), LDOrderRules::ChasePress(Out));
    LDOrderRules::FChasePressInput Disarmed = Press;
    Disarmed.bWasPressing = true;
    Disarmed.bAttackAllowed = false;
    TestFalse(TEXT("A body that cannot attack holds in range"), LDOrderRules::ChasePress(Disarmed));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatDirectSequenceWindowTest, "Lockdown.Rules.DemoCombatSources.DirectSequenceWindow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatDirectSequenceWindowTest::RunTest(const FString& Parameters)
{
    using LDCombatSourceRules::DirectSequenceVerdict;
    // Queued Q/W at 5 and 6, then a non-Shift D at 7 refused for cooldown before Combat judged it: the refusal is
    // recorded above the high-water mark (4), which does not move.
    TestTrue(TEXT("A replay of the refused request answers the recorded refusal, never runs after the cooldown"),
        DirectSequenceVerdict(true, true, 7, 4, false) == ELDDirectSequenceVerdict::Replay);
    TestTrue(TEXT("The refused sequence with changed terms conflicts"),
        DirectSequenceVerdict(true, false, 7, 4, false) == ELDDirectSequenceVerdict::Conflict);
    TestTrue(TEXT("The queued steps below it stay fresh"), DirectSequenceVerdict(false, false, 5, 4, true) == ELDDirectSequenceVerdict::Fresh);
    // A D at 8 pressed during the own windup went to Combat (the high-water mark is now 8) and was refused as Busy.
    TestTrue(TEXT("A queued step older than a newer judged request still runs once from the queue"),
        DirectSequenceVerdict(false, false, 6, 8, true) == ELDDirectSequenceVerdict::Fresh);
    TestTrue(TEXT("The same old sequence sent as a plain Direct request has expired"),
        DirectSequenceVerdict(false, false, 6, 8, false) == ELDDirectSequenceVerdict::Expired);
    TestTrue(TEXT("A queued step already judged replays instead of running twice"),
        DirectSequenceVerdict(true, true, 6, 8, true) == ELDDirectSequenceVerdict::Replay);
    TestTrue(TEXT("A new sequence above the mark is fresh"), DirectSequenceVerdict(false, false, 9, 8, false) == ELDDirectSequenceVerdict::Fresh);
    return true;
}

#endif
