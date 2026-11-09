#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatKeywords.h"
#include "Combat/LDCombatControlRules.h"
#include "Combat/LDCombatMechanics.h"
#include "Combat/LDTacticalRules.h"
#include "Combat/LDDashContactRules.h"
#include "Combat/LDCombatSourceRules.h"
#include "Character/LDCharacter.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/LDTestApprovedFixture.h"

#if WITH_DEV_AUTOMATION_TESTS

// #147 build v7 combat primitives (docs/systems/COMBAT_AND_GROWTH.md, "构筑 v7 战斗原语"). No catalog entry uses them yet;
// the passive-driven cases add one test-only passive to their own in-memory copy of the approved fixture, never to SourceData.

struct FLDBuildPrimitivesAccess
{
    static TMap<TWeakObjectPtr<ALDCharacter>, FLDBodyCombatState>& Bodies(ALDCombatDirector& Driver) { return Driver.Bodies; }
    static TMap<FGuid, FLDCombatEntityRecord>& Records(ALDCombatDirector& Driver) { return Driver.Records; }
    static TArray<FLDCombatHit>& PendingHits(ALDCombatDirector& Driver) { return Driver.PendingHits; }
    static TArray<FLDCombatWallCheck>& WallChecks(ALDCombatDirector& Driver) { return Driver.WallChecks; }
    static bool ApplyHit(ALDCombatDirector& Driver, const FLDCombatHit& Hit, FString& Error) { return Driver.ApplyHit(Hit, Error); }
    static bool AdvanceBurns(ALDCombatDirector& Driver, int64 NowMs, FString& Error) { return Driver.AdvanceBurns(NowMs, Error); }
    static bool ResolveWallChecks(ALDCombatDirector& Driver, FString& Error) { return Driver.ResolveWallChecks(Error); }
    static bool LandBasic(ALDCombatDirector& Driver, ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDCombatAction& Action,
        FString& Error)
    { return Driver.LandBasic(Actor, Body, Action, Error); }
    static bool ApplyCleanses(ALDCombatDirector& Driver, FLDBodyCombatState& Body, const FLDDemoRankDefinition& Rank, int64 NowMs)
    { return Driver.ApplyCleanses(Body, Rank, NowMs); }
    static bool BurstBrokenShield(ALDCombatDirector& Driver, FLDBodyCombatState& Body, const FGuid& Broken,
        const FLDCombatBodyRef& Breaker, int64 NowMs, FString& Error)
    { return Driver.BurstBrokenShield(Body, Broken, Breaker, NowMs, Error); }
    static void ExpireStatuses(ALDCombatDirector& Driver, int64 NowMs) { Driver.ExpireStatuses(NowMs); }
    static void CancelEntityEffects(ALDCombatDirector& Driver, const FGuid& Entity) { Driver.CancelEntityEffects(Entity); }
    static void KeywordStatuses(const ALDCombatDirector& Driver, const FLDBodyCombatState& Body, int64 NowMs, TArray<FLDObservedStatus>& Out)
    { Driver.AppendKeywordStatuses(Body, NowMs, Out); }
    static void Bind(ALDGameMode& Mode, ALDCombatDirector& Driver, const TSharedPtr<const FLDDemoContentSnapshot>& Catalog)
    {
        Mode.Combat = &Driver;
        Mode.DemoContent = Catalog;
    }
    static void SetNow(ALDGameMode& Mode, int64 NowMs) { Mode.AuthorityNowMs = NowMs; }
    static bool DashContact(ALDCombatDirector& Driver, FLDBodyCombatState& Body, FLDCombatDashContact& Dash, int64 Now, FString& Error)
    { return Driver.OnDashContact(Body, Dash, Now, Error); }
    static bool Posture(ALDCombatDirector& Driver, FLDBodyCombatState& Body, bool Cower, int64 Now, FString& Error)
    { return Driver.CancelSkillPosture(Body, Cower, Now, Error); }
    static bool Delivery(ALDCombatDirector& Driver, FLDBodyCombatState& Body, const FLDDemoRankDefinition& Rank,
        const FLDDemoTargetDefinition& Target, const FLDCombatAction& Action, FLDCombatHit& Hit, int64 Now, bool& Handled, FString& Error)
    { return Driver.StartSkillDelivery(Body, Rank, Target, Action, Hit, Now, Handled, Error); }
    static TArray<FLDCombatGroundEffect>& Ground(ALDCombatDirector& Driver) { return Driver.GroundEffects; }
};

namespace LDBuildPrimitivesTests
{
using FAccess = FLDBuildPrimitivesAccess;
// One isolated world with a real Combat director over the approved fixture.
struct FFixture
{
    UWorld* World = nullptr;
    ALDGameMode* Mode = nullptr;
    ALDCombatDirector* Driver = nullptr;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    TMap<FGuid, ALDCharacter*> Actors;

    ~FFixture()
    {
        if (World)
        {
            World->DestroyWorld(false);
            GEngine->DestroyWorldContext(World);
        }
    }

    bool Init(FAutomationTestBase& Test)
    {
        if (!LDTestApprovedFixture::Read(Test, Catalog)) { return false; }
        const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
            .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
        if (!World) { Test.AddError(TEXT("Fixture world")); return false; }
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        Mode = World->SpawnActor<ALDGameMode>();
        Driver = World->SpawnActor<ALDCombatDirector>();
        if (!Mode || !Driver) { Test.AddError(TEXT("Fixture actors")); return false; }
        FAccess::Bind(*Mode, *Driver, Catalog);
        World->CopyGameState(Mode, nullptr);
        Now(1000);
        return true;
    }

    void Now(int64 Ms) { FAccess::SetNow(*Mode, Ms); }

    // A registered body (a contender also gets its Combat record, so assists can credit it).
    FLDCombatBodyRef AddBody(ELDVictimKind Kind = ELDVictimKind::Contender, int64 HPP2 = 100000)
    {
        ALDCharacter* Actor = World->SpawnActor<ALDCharacter>();
        const FLDCombatBodyRef Ref{{Kind, FGuid::NewGuid(), 1}, FGuid::NewGuid()};
        FLDBodyCombatState State;
        State.Identity = Ref.BodyIdentity;
        State.Victim = Ref.Victim;
        State.Resources.MaxHpP2 = State.Resources.HpP2 = HPP2;
        FAccess::Bodies(*Driver).Add(Actor, MoveTemp(State));
        Actors.Add(Ref.Victim.EntityId, Actor);
        if (Kind == ELDVictimKind::Contender)
        {
            FLDCombatEntityRecord& Record = FAccess::Records(*Driver).Add(Ref.Victim.EntityId);
            Record.Kind = Kind;
            Record.Entity = Ref.Victim.EntityId;
            Record.Build.Level = 1;
        }
        return Ref;
    }

    FLDBodyCombatState& Body(const FLDCombatBodyRef& Ref) { return FAccess::Bodies(*Driver).FindChecked(Actors.FindChecked(Ref.Victim.EntityId)); }
    ALDCharacter& Actor(const FLDCombatBodyRef& Ref) { return *Actors.FindChecked(Ref.Victim.EntityId); }

    // An approved learned passive as the level-1 projection lists it: a rank-exact combat trigger.
    void LearnPassive(const FLDCombatBodyRef& Ref, const TCHAR* Id, uint8 Rank)
    {
        FLDLearnedTriggerTerm& Term = Body(Ref).AttributeProjection.LearnedTriggers.AddDefaulted_GetRef();
        Term.AbilityId = Id;
        Term.Rank = Rank;
        Term.Level = 1;
    }

    // A magical ability hit of BaseP2 from one body to another.
    static FLDCombatHit Hit(const FLDCombatBodyRef& From, const FLDCombatBodyRef& To, int64 BaseP2)
    {
        FLDCombatHit Out;
        Out.HitId = Out.EffectId = FGuid::NewGuid();
        Out.Target = To;
        Out.SourceBody = From;
        Out.SourceEntity = From.Victim.EntityId;
        Out.CreditOwner = From.Victim.Kind == ELDVictimKind::Contender ? From.Victim.EntityId : FGuid();
        Out.HitClass = ELDCombatHitClass::Ability;
        Out.DamageType = ELDDemoDamageType::Magical;
        Out.BaseP2 = BaseP2;
        return Out;
    }

    bool Apply(const FLDCombatHit& Hit, FString& Error) { return FAccess::ApplyHit(*Driver, Hit, Error); }

    // Applies every queued hit (and what those queue) like one Effects phase.
    bool Flush(FString& Error)
    {
        TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*Driver);
        for (int32 Index = 0; Index < Pending.Num(); ++Index)
        {
            const FLDCombatHit Each = Pending[Index];
            if (!Apply(Each, Error)) { return false; }
        }
        Pending.Reset();
        return true;
    }

    bool HasContribution(const FLDCombatBodyRef& Victim, const FLDCombatBodyRef& From)
    {
        return Body(Victim).AssistContributions.ContainsByPredicate([&From](const FLDAssistContribution& Row)
        { return Row.Contributor == From.Victim.EntityId; });
    }

    static const FLDObservedStatus* FindStatus(const TArray<FLDObservedStatus>& Statuses, ELDDemoStatusId Id)
    {
        return Statuses.FindByPredicate([Id](const FLDObservedStatus& Row) { return Row.StatusId == uint8(Id); });
    }
};

FLDKeywordTerms Terms()
{
    FLDKeywordTerms Out;
    Out.BreachPerStackP2 = 300;
    Out.BreachMaxStacks = 5;
    Out.BreachMs = 4000;
    Out.BurnPerSecP2 = 300;
    Out.BurnAPRatioBp = 300;
    Out.BurnTickMs = 500;
    Out.BurnMs = 3000;
    Out.BurnMaxStacks = 5;
    Out.LifestealCapBp = 3500;
    return Out;
}

// A level-1 layer; its AP sets the per-second damage (3 + 3% AP).
FLDCombatBurnLayer Layer(const FGuid& Source, int64 SourceAPP2)
{
    FLDCombatBurnLayer Out;
    Out.SourceEntity = Out.CreditOwner = Source;
    Out.SourceBody = {{ELDVictimKind::Contender, Source, 1}, FGuid::NewGuid()};
    Out.Level = 1;
    Out.SourceAPP2 = SourceAPP2;
    return Out;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildKeywordRulesTest, "Lockdown.Rules.DemoBuildPrimitives.KeywordRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildKeywordRulesTest::RunTest(const FString& Parameters)
{
    using namespace LDCombatKeywords;
    using namespace LDBuildPrimitivesTests;
    const FLDKeywordTerms T = Terms();
    // D12 breach: one shared group, fixed -3 per stack, five stacks, four seconds refreshed whole.
    FLDCombatSharedBreach Breach;
    TestFalse(TEXT("No stacks is no breach"), ApplyBreach(Breach, 0, 1000, T));
    TestTrue(TEXT("Two stacks land"), ApplyBreach(Breach, 2, 1000, T) && Breach.Stacks == 2 && Breach.ExpiresAtMs == 5000);
    TestTrue(TEXT("Any source adds to the same group, capped at five, and restarts it"),
        ApplyBreach(Breach, 4, 2000, T) && Breach.Stacks == 5 && Breach.ExpiresAtMs == 6000);
    TestTrue(TEXT("A full group only restarts"), ApplyBreach(Breach, 1, 3000, T) && Breach.Stacks == 5 && Breach.ExpiresAtMs == 7000);
    TestEqual(TEXT("Each stack takes 3 armor and magic resist"), BreachFlatP2(Breach, 3000, T), int64(1500));
    TestEqual(TEXT("A lapsed group takes nothing (half-open)"), BreachFlatP2(Breach, 7000, T), int64(0));
    TestTrue(TEXT("and ends"), ExpireBreach(Breach, 7000) && Breach.Stacks == 0);
    TestTrue(TEXT("A new stack after a lapse starts over"), ApplyBreach(Breach, 1, 8000, T) && Breach.Stacks == 1);
    TestEqual(TEXT("A stack takes its flat off the resistance"), BreachedResistanceP2(2000, Breach, 8000, T), int64(1700));
    TestEqual(TEXT("and never drives a resistance below zero"), BreachedResistanceP2(200, Breach, 8000, T), int64(0));

    // D11 burn: shared layers and timer; each layer keeps its applier and snapshot; the oldest gives way at the cap.
    const FGuid A = FGuid::NewGuid(), B = FGuid::NewGuid();
    TestEqual(TEXT("A level-1 layer burns 3 + 3% AP per second"), BurnLayerPerSecondP2(1, 5000, T), int64(300 + 150));
    FLDCombatBurn Burn;
    TestTrue(TEXT("Two layers of A"), ApplyBurn(Burn, Layer(A, 0), 2, 1000, T) && Burn.Layers.Num() == 2
        && Burn.NextTickAtMs == 1500 && Burn.ExpiresAtMs == 4000);
    TestTrue(TEXT("A layer of B joins and restarts the shared timer, keeping the tick cadence"),
        ApplyBurn(Burn, Layer(B, 10000), 1, 1200, T) && Burn.Layers.Num() == 3 && Burn.NextTickAtMs == 1500 && Burn.ExpiresAtMs == 4200);
    TArray<FLDBurnTickShare> Shares;
    BurnTickShares(Burn, T, Shares);
    TestTrue(TEXT("One tick credits each applier its own layers"), Shares.Num() == 2 && Shares[0].SourceEntity == A
        && Shares[0].DamageP2 == 300 && Shares[1].SourceEntity == B && Shares[1].DamageP2 == 300);
    TestTrue(TEXT("At the cap each new layer replaces the oldest"), ApplyBurn(Burn, Layer(B, 10000), 4, 1300, T)
        && Burn.Layers.Num() == 5 && !Burn.Layers.ContainsByPredicate([&A](const FLDCombatBurnLayer& Row) { return Row.SourceEntity == A; }));
    TestEqual(TEXT("A leaving applier takes its layers"), RemoveBurnSource(Burn, B), 5);
    TestTrue(TEXT("and an empty burn ends"), Burn.Layers.IsEmpty() && Burn.ExpiresAtMs == 0);
    TestTrue(TEXT("A new burn"), ApplyBurn(Burn, Layer(A, 0), 1, 0, T));
    TestTrue(TEXT("lasts through its deadline (its last tick falls on it)"), BurnActive(Burn, 3000) && !ExpireBurn(Burn, 3000));
    TestTrue(TEXT("and ends after it"), !BurnActive(Burn, 3001) && ExpireBurn(Burn, 3001) && Burn.Layers.IsEmpty());

    // D22 lifesteal cap, the wall judgement and the pierce order.
    TestEqual(TEXT("Lifesteal adds up to the 35% cap"), CapLifestealBp(6000, T), int64(3500));
    TestEqual(TEXT("below the cap it is kept"), CapLifestealBp(2000, T), int64(2000));
    TestTrue(TEXT("A push that travelled less than 225 cm met a wall"), KnockbackHitWall(FVector::ZeroVector, FVector(224, 0, 0), 225));
    TestFalse(TEXT("one that travelled 225 cm did not"), KnockbackHitWall(FVector::ZeroVector, FVector(0, 225, 0), 225));
    TestFalse(TEXT("No threshold is no wall"), KnockbackHitWall(FVector::ZeroVector, FVector::ZeroVector, 0));
    const FGuid Shooter = FGuid::NewGuid();
    const auto Candidate = [](const FGuid& Entity, double X, double Y)
    { return FLDFlightCandidate{{{ELDVictimKind::Contender, Entity, 1}, FGuid::NewGuid()}, FVector(X, Y, 0), 40}; };
    TArray<FLDFlightCandidate> Candidates = {Candidate(FGuid::NewGuid(), 600, 0), Candidate(FGuid::NewGuid(), 300, 0),
        Candidate(Shooter, 100, 0), Candidate(FGuid::NewGuid(), 500, 300), Candidate(FGuid::NewGuid(), 800, 0)};
    TArray<int32> Order;
    PierceContacts(FVector::ZeroVector, FVector(1000, 0, 0), 10, Shooter, {Candidates[4].Body}, Candidates, Order);
    TestTrue(TEXT("A pierce strikes every body on its path in flight order, never its shooter or a struck life again"),
        Order.Num() == 2 && Order[0] == 1 && Order[1] == 0);

    // Charged empowerment and the second blast's payload.
    TOptional<FLDCombatEmpower> Armed = FLDCombatEmpower();
    Armed->Charges = 3;
    Armed->BaseP2 = 2000;
    Armed->ExpiresAtMs = 5000;
    TestTrue(TEXT("The first charge"), LDCombatControl::ConsumeEmpower(Armed, 1000).IsSet() && Armed.IsSet() && Armed->Charges == 2);
    TestTrue(TEXT("the second"), LDCombatControl::ConsumeEmpower(Armed, 1100).IsSet() && Armed.IsSet() && Armed->Charges == 1);
    const TOptional<FLDCombatEmpower> Last = LDCombatControl::ConsumeEmpower(Armed, 1200);
    TestTrue(TEXT("and the last end the record"), Last.IsSet() && Last->Charges == 0 && !Armed.IsSet());
    TestFalse(TEXT("then nothing is left"), LDCombatControl::ConsumeEmpower(Armed, 1300).IsSet());
    Armed = FLDCombatEmpower();
    Armed->ExpiresAtMs = 1000;
    TestTrue(TEXT("An expired empowerment is dropped untaken"), !LDCombatControl::ConsumeEmpower(Armed, 1000).IsSet() && !Armed.IsSet());
    FLDCombatHit First;
    First.EffectId = FGuid::NewGuid();
    First.BaseP2 = 10000;
    First.ADRatioBp = 6000;
    First.APRatioBp = 7000;
    First.MaxHPRatioBp = 300;
    First.BurnStacks = 2;
    const FLDCombatHit Echo = ALDCombatDirector::EchoHit(First, 5000);
    TestTrue(TEXT("A second blast scales its damage terms only, under its own effect"), Echo.BaseP2 == 5000 && Echo.ADRatioBp == 3000
        && Echo.APRatioBp == 3500 && Echo.MaxHPRatioBp == 150 && Echo.BurnStacks == 2 && Echo.EffectId != First.EffectId);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildDamagePoolTest, "Lockdown.Rules.DemoBuildPrimitives.DamagePool",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildDamagePoolTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(*this, Catalog)) { return false; }
    const auto Evaluate = [&Catalog](ELDCombatHitClass Class, bool bCritical, int64 PoolBp, int64 AmplifyBp)
    {
        FLDDamageEvaluationInput Input;
        const bool bBasic = Class != ELDCombatHitClass::Ability;
        Input.Definition.Emplace(10000, 0, 0, bBasic ? ELDDemoDamageType::Physical : ELDDemoDamageType::Magical);
        Input.HitClass = Class;
        Input.bCritical = bCritical;
        Input.TargetResistanceP2 = 0;
        Input.TargetHPP2 = 1000000;
        Input.TargetShieldP2 = 0;
        Input.bTargetProtected = false;
        Input.DamageBonusBp = PoolBp;
        Input.AmplifyBp = AmplifyBp;
        return LDTactical::EvaluateDamage(Input, *Catalog);
    };
    const auto Damage = [](const FLDDamageEvaluationResult& Result) { return Result.IsSuccess() ? Result.Value->DamageP2 : int64(-1); };
    // D21: every "+X%" adds into one pool applied once; an ability crit is +50% inside it, a basic crit x2 before it.
    TestEqual(TEXT("A 30% pool"), Damage(Evaluate(ELDCombatHitClass::Ability, false, 3000, 10000)), int64(13000));
    TestEqual(TEXT("An ability crit adds 50% to the pool instead of multiplying"),
        Damage(Evaluate(ELDCombatHitClass::Ability, true, 3000, 10000)), int64(18000));
    TestEqual(TEXT("A basic crit doubles before the pool"), Damage(Evaluate(ELDCombatHitClass::BasicPrimary, true, 3000, 10000)), int64(26000));
    TestEqual(TEXT("The target's crit reduction stays a separate multiplier"),
        Damage(Evaluate(ELDCombatHitClass::Ability, true, 2500, 8000)), int64(14000));
    TestFalse(TEXT("A negative pool is refused"), Evaluate(ELDCombatHitClass::Ability, false, -1, 10000).IsSuccess());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildSilenceTest, "Lockdown.Rules.DemoBuildPrimitives.Silence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildSilenceTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Caster = F.AddBody(), Victim = F.AddBody();
    FLDBodyCombatState& Body = F.Body(Victim);
    const auto Commit = [&](ELDControlKind Kind, int64 DurationMs, bool& bApplied)
    {
        FLDControlApplication App;
        App.Kind = Kind;
        App.EffectId = FGuid::NewGuid();
        App.SourceEntity = App.CreditOwner = Caster.Victim.EntityId;
        App.SourceBody = Caster;
        App.DurationMs = DurationMs;
        FString Error;
        const bool bOk = F.Driver->CommitControl(Body, App, F.Mode->GetAuthorityNowMs(), bApplied, Error);
        if (!bOk) { AddError(Error); }
        return bOk;
    };
    // A learned skill winding up ends unpaid; a basic or the innate dash winding up goes on.
    FLDCombatAction Windup;
    Windup.Kind = ELDCombatActionKind::Ability;
    Windup.Key.Origin = ELDCombatRequestOrigin::Nav;
    Windup.Source.Kind = ELDCombatSourceKind::Learned;
    Windup.Source.DefinitionId = TEXT("active_snark_bolt");
    Windup.ReleaseAtMs = 2000;
    Body.Action = Windup;
    bool bApplied = false;
    TestTrue(TEXT("A silence commits through the single control path"), Commit(ELDControlKind::Silence, 1000, bApplied) && bApplied);
    TestTrue(TEXT("It lasts its full time and is a hard control"),
        Body.Silences.Num() == 1 && Body.Silences[0].ExpiresAtMs == 2000 && Body.HardControl.Count == 0 && Body.HardControl.WindowEndMs > 0);
    TestFalse(TEXT("The silenced skill windup ended unpaid"), Body.Action.IsSet());
    TestTrue(TEXT("It forbids Q/W only"), F.Driver->IsActionBlocked(Body, ELDCombatControlAction::Skill)
        && !F.Driver->IsActionBlocked(Body, ELDCombatControlAction::Cast) && !F.Driver->IsActionBlocked(Body, ELDCombatControlAction::Move)
        && !F.Driver->IsActionBlocked(Body, ELDCombatControlAction::Basic)
        && !F.Driver->IsActionBlocked(Body, ELDCombatControlAction::Displacement));
    TestTrue(TEXT("A learned active is blocked, the innate dash is not"),
        F.Driver->IsCastBlocked(Body, ELDCombatSourceKind::Learned) && !F.Driver->IsCastBlocked(Body, ELDCombatSourceKind::Innate));
    TestTrue(TEXT("A silenced body counts as controlled (\"被减速或被控制\")"), F.Driver->IsImpaired(Body, 1500));
    TestTrue(TEXT("The silence locks the cast bar in the display"), LDCombatControl::SilenceBlockBits() == uint8(ELDCombatControlBlock::Cast));
    FLDCombatAction Basic = Windup;
    Basic.Kind = ELDCombatActionKind::Basic;
    Basic.Source.Kind = ELDCombatSourceKind::Basic;
    Body.Action = Basic;
    TestTrue(TEXT("A second silence diminishes with the shared chain"), Commit(ELDControlKind::Silence, 1000, bApplied) && bApplied
        && Body.Silences.Last().ExpiresAtMs == 1600);
    TestTrue(TEXT("and leaves a basic windup alone"), Body.Action.IsSet());
    FLDCombatAction Dash = Windup;
    Dash.Source.Kind = ELDCombatSourceKind::Innate;
    Body.Action = Dash;
    TestTrue(TEXT("A third silence"), Commit(ELDControlKind::Silence, 1000, bApplied) && bApplied);
    TestTrue(TEXT("leaves a dash windup alone"), Body.Action.IsSet());
    Body.Action.Reset();
    TArray<FLDObservedStatus> Statuses;
    FAccess::KeywordStatuses(*F.Driver, Body, 1500, Statuses);
    const FLDObservedStatus* Shown = FFixture::FindStatus(Statuses, ELDDemoStatusId::Silence);
    TestTrue(TEXT("Every observer sees the silence with its latest deadline"), Shown && Shown->ExpiresAtServerMs == 2000);
    FAccess::ExpireStatuses(*F.Driver, 2000);
    TestTrue(TEXT("It ends at its deadline"), Body.Silences.IsEmpty() && !F.Driver->IsActionBlocked(Body, ELDCombatControlAction::Skill));
    TestTrue(TEXT("A new one"), Commit(ELDControlKind::Silence, 1000, bApplied) && bApplied);
    FAccess::CancelEntityEffects(*F.Driver, Caster.Victim.EntityId);
    TestTrue(TEXT("ends with its leaving source"), Body.Silences.IsEmpty());
    // The disarm cleanse.
    Body.Disarms.Add({FGuid::NewGuid(), Caster.Victim.EntityId, 5000});
    FLDDemoRankDefinition Cleanse;
    Cleanse.Effects.AddDefaulted_GetRef().Opcode = uint8(ELDDemoEffectOp::DispelDisarm);
    TestFalse(TEXT("A disarm cleanse changes no temporary term"), FAccess::ApplyCleanses(*F.Driver, Body, Cleanse, 1500));
    TestTrue(TEXT("and removes every disarm"), Body.Disarms.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildSharedBreachBurnTest, "Lockdown.Rules.DemoBuildPrimitives.SharedBreachAndBurn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildSharedBreachBurnTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef A = F.AddBody(), B = F.AddBody(), Target = F.AddBody();
    FLDBodyCombatState& Body = F.Body(Target);
    Body.AttributeProjection.Final.ArmorP2 = 1500;
    FString Error;
    // Breach from two sources lands in one group capped at five stacks of -3.
    FLDCombatHit Hit = FFixture::Hit(A, Target, 0);
    Hit.BreachStacks = 2;
    if (!TestTrue(FString::Printf(TEXT("A breach hit: %s"), *Error), F.Apply(Hit, Error))) { return false; }
    TestTrue(TEXT("Two stacks for four seconds"), Body.SharedBreach.Stacks == 2 && Body.SharedBreach.ExpiresAtMs == 5000);
    TestTrue(TEXT("The breach is its applier's assist contribution"), F.HasContribution(Target, A));
    Hit = FFixture::Hit(B, Target, 0);
    Hit.BreachStacks = 4;
    TestTrue(TEXT("Another source's stacks join the same group up to five"), F.Apply(Hit, Error) && Body.SharedBreach.Stacks == 5);
    FLDCombatHit Strike = FFixture::Hit(A, Target, 10000);
    Strike.DamageType = ELDDemoDamageType::Physical;
    TestTrue(TEXT("Five stacks take 15 armor off: the full 100 lands"), F.Apply(Strike, Error) && Body.Resources.HpP2 == 90000);
    // Burn: layers keep their applier; one tick credits each applier its own damage.
    Hit = FFixture::Hit(A, Target, 0);
    Hit.BurnStacks = 2;
    Hit.BurnAPP2 = 0;
    TestTrue(TEXT("Two burn layers of A"), F.Apply(Hit, Error) && Body.Burn.Layers.Num() == 2 && Body.Burn.NextTickAtMs == 1500);
    Hit = FFixture::Hit(B, Target, 0);
    Hit.BurnStacks = 1;
    Hit.BurnAPP2 = 10000;
    TestTrue(TEXT("One of B"), F.Apply(Hit, Error) && Body.Burn.Layers.Num() == 3 && Body.Burn.ExpiresAtMs == 4000);
    TArray<FLDObservedStatus> Statuses;
    FAccess::KeywordStatuses(*F.Driver, Body, 1000, Statuses);
    const FLDObservedStatus* Exposed = FFixture::FindStatus(Statuses, ELDDemoStatusId::Exposed);
    const FLDObservedStatus* Burning = FFixture::FindStatus(Statuses, ELDDemoStatusId::Burn);
    TestTrue(TEXT("Observers see breach x5 and burn x3"), Exposed && Exposed->Stacks == 5 && Burning && Burning->Stacks == 3
        && Burning->ExpiresAtServerMs == 4000);
    F.Now(1500);
    TestTrue(FString::Printf(TEXT("The first tick: %s"), *Error), FAccess::AdvanceBurns(*F.Driver, 1500, Error));
    const TArray<FLDCombatHit>& Ticks = FAccess::PendingHits(*F.Driver);
    TestTrue(TEXT("One magic tick per applier, never a crit or on-hit"), Ticks.Num() == 2 && Ticks[0].SourceEntity == A.Victim.EntityId
        && Ticks[0].BaseP2 == 300 && Ticks[1].SourceEntity == B.Victim.EntityId && Ticks[1].BaseP2 == 300 && Ticks[0].bBurnTick
        && Ticks[0].bPeriodic && Ticks[0].OnHitScaleBp == 0 && Ticks[0].DamageType == ELDDemoDamageType::Magical);
    TestTrue(TEXT("No burn amplification without an affliction passive"), Ticks[0].ExtraAmplifyBp == 10000 && Ticks[1].ExtraAmplifyBp == 10000);
    TestTrue(TEXT("The ticks land: 3.00 each"), F.Flush(Error) && Body.Resources.HpP2 == 89400 && Body.Burn.NextTickAtMs == 2000);
    FAccess::CancelEntityEffects(*F.Driver, A.Victim.EntityId);
    TestTrue(TEXT("A leaving applier's layers end; the shared breach stays"), Body.Burn.Layers.Num() == 1 && Body.SharedBreach.Stacks == 5);
    Hit.BurnStacks = 6;
    F.Now(1500);
    TestTrue(TEXT("At five layers each new one replaces the oldest"), F.Apply(Hit, Error) && Body.Burn.Layers.Num() == 5);
    TestEqual(TEXT("The new layers restarted the shared timer"), Body.Burn.ExpiresAtMs, int64(4500));
    F.Now(4500);
    FAccess::ExpireStatuses(*F.Driver, 4500);
    TestTrue(TEXT("The burn ticks every half second through its last instant"), FAccess::AdvanceBurns(*F.Driver, 4500, Error)
        && FAccess::PendingHits(*F.Driver).Num() == 6);
    TestTrue(TEXT("and then ends"), F.Flush(Error) && Body.Burn.Layers.IsEmpty());
    F.Now(5000);
    FAccess::ExpireStatuses(*F.Driver, 5000);
    TestFalse(TEXT("The breach ended at its deadline"), Body.SharedBreach.Stacks > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildPoolHitsTest, "Lockdown.Rules.DemoBuildPrimitives.PoolHits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildPoolHitsTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Source = F.AddBody(), Target = F.AddBody();
    FLDBodyCombatState& Body = F.Body(Target);
    FString Error;
    const auto Lost = [&](const FLDCombatHit& Hit, int64 StartHPP2, bool bSlowed)
    {
        Body.Resources.HpP2 = StartHPP2;
        Body.Slows.Reset();
        if (bSlowed) { Body.Slows.Add({FGuid::NewGuid(), Source.Victim.EntityId, 3000, 9000}); }
        if (!F.Apply(Hit, Error)) { AddError(Error); return int64(-1); }
        return StartHPP2 - Body.Resources.HpP2;
    };
    FLDCombatHit Hit = FFixture::Hit(Source, Target, 10000);
    Hit.ControlAmpBp = 1200;
    Hit.ExecuteBp = 3000;
    Hit.ExecuteBelowBp = 5000;
    TestEqual(TEXT("Neither slowed nor low: no bonus"), Lost(Hit, 100000, false), int64(10000));
    TestEqual(TEXT("Slowed and below half: +12% and +30% add into one +42%"), Lost(Hit, 40000, true), int64(14200));
    TestEqual(TEXT("Exactly half is not below half"), Lost(Hit, 50000, false), int64(10000));
    FLDCombatHit LowHP = FFixture::Hit(Source, Target, 10000);
    LowHP.ControlAmpBp = 1200;
    LowHP.ControlAmpBelowBp = 5000;
    TestEqual(TEXT("An amplification that also counts low-HP bodies needs no control"), Lost(LowHP, 40000, false), int64(11200));
    FLDCombatHit Crit = Hit;
    Crit.ControlAmpBp = 0;
    Crit.bCritical = true;
    TestEqual(TEXT("An ability crit with an execute: +50% and +30% add (1.8, not 1.95)"), Lost(Crit, 40000, false), int64(18000));
    FLDCombatHit Basic = FFixture::Hit(Source, Target, 10000);
    Basic.HitClass = ELDCombatHitClass::BasicPrimary;
    Basic.DamageType = ELDDemoDamageType::Physical;
    Basic.bCritical = true;
    Basic.ControlAmpBp = 1200;
    TestEqual(TEXT("A basic crit doubles before the pool"), Lost(Basic, 100000, true), int64(22400));
    FLDCombatHit Amplified = FFixture::Hit(Source, Target, 10000);
    Amplified.ExtraAmplifyBp = 12500;
    Amplified.ControlAmpBp = 1200;
    TestEqual(TEXT("An affliction amplification joins the same pool"), Lost(Amplified, 100000, true), int64(13700));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildLifestealCapTest, "Lockdown.Rules.DemoBuildPrimitives.LifestealCap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildLifestealCapTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Source = F.AddBody(), Target = F.AddBody();
    FLDBodyCombatState& Healer = F.Body(Source);
    FLDBodyCombatState& Body = F.Body(Target);
    FString Error;
    FLDCombatHit Hit = FFixture::Hit(Source, Target, 10000);
    Hit.HitClass = ELDCombatHitClass::BasicPrimary;
    Hit.DamageType = ELDDemoDamageType::Physical;
    Hit.OnHitScaleBp = 10000;
    Hit.bLifesteal = true;
    Hit.LifestealRatioBp = 3000;
    Hit.LifestealLowHPBp = 3000;
    Hit.LifestealLowHPBelowBp = 5000;
    Healer.Resources.HpP2 = 50000;
    TestTrue(TEXT("Against a healthy body only the base lifesteal heals"), F.Apply(Hit, Error) && Healer.Resources.HpP2 == 53000);
    Body.Resources.HpP2 = 40000;
    TestTrue(TEXT("Against a low body the doubled share adds, then the sum stops at 35%"), F.Apply(Hit, Error)
        && Healer.Resources.HpP2 == 56500);
    FLDCombatHit Tick = FFixture::Hit(Source, Target, 1000);
    Tick.bBurnTick = Tick.bPeriodic = true;
    Tick.OnHitScaleBp = 10000;
    Tick.bLifesteal = true;
    Tick.LifestealRatioBp = 3000;
    TestTrue(TEXT("A burn tick never heals its applier"), F.Apply(Tick, Error) && Healer.Resources.HpP2 == 56500);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildControlSwapWallTest, "Lockdown.Rules.DemoBuildPrimitives.ControlSwapAndWall",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildControlSwapWallTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Source = F.AddBody(), Slowed = F.AddBody(), Plain = F.AddBody();
    FString Error;
    F.Body(Slowed).Slows.Add({FGuid::NewGuid(), Source.Victim.EntityId, 3000, 5000});
    FLDCombatHit Hit = FFixture::Hit(Source, Slowed, 0);
    Hit.RootDurationMs = 1000;
    Hit.SlowedStunMs = 800;
    TestTrue(TEXT("A root on a slowed body stuns it instead"), F.Apply(Hit, Error) && F.Body(Slowed).Stuns.Num() == 1
        && F.Body(Slowed).Stuns[0].ExpiresAtMs == 1800 && F.Body(Slowed).Roots.IsEmpty());
    Hit.Target = Plain;
    TestTrue(TEXT("and roots a body that is not slowed"), F.Apply(Hit, Error) && F.Body(Plain).Roots.Num() == 1 && F.Body(Plain).Stuns.IsEmpty());
    // A knockback's wall check: a body that travelled less than the threshold met a wall and is stunned (diminished).
    const FVector Foot = ALDCombatDirector::FootOf(F.Actor(Plain));
    FLDCombatWallCheck Check;
    Check.Target = Plain;
    Check.EffectId = FGuid::NewGuid();
    Check.SourceEntity = Check.CreditOwner = Source.Victim.EntityId;
    Check.SourceBody = Source;
    Check.StartFootCm = Foot;
    Check.AtMs = 1250;
    Check.ThresholdCm = 225;
    Check.StunMs = 1000;
    FLDCombatWallCheck Far = Check;
    Far.Target = Slowed;
    Far.StartFootCm = ALDCombatDirector::FootOf(F.Actor(Slowed)) + FVector(250, 0, 0);
    FAccess::WallChecks(*F.Driver).Append({Check, Far});
    F.Now(1200);
    TestTrue(TEXT("Nothing resolves before the push ends"), FAccess::ResolveWallChecks(*F.Driver, Error) && FAccess::WallChecks(*F.Driver).Num() == 2);
    F.Now(1250);
    TestTrue(FString::Printf(TEXT("The checks resolve: %s"), *Error), FAccess::ResolveWallChecks(*F.Driver, Error)
        && FAccess::WallChecks(*F.Driver).IsEmpty());
    TestTrue(TEXT("The body that met a wall is stunned through the control chain"), F.Body(Plain).Stuns.Num() == 1
        && F.Body(Plain).Stuns[0].ExpiresAtMs == 1250 + 600);
    TestTrue(TEXT("The body that travelled far enough is not"), F.Body(Slowed).Stuns.Num() == 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildPassiveTriggersTest, "Lockdown.Rules.DemoBuildPrimitives.PassiveTriggers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildPassiveTriggersTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(), Target = F.AddBody();
    // Advanced passive_tilted: breach 2 per Q/W hit and 2 per basic; advanced passive_stoke: burn 4 per Q/W hit, 1 per basic.
    F.LearnPassive(Holder, TEXT("passive_tilted"), 2);
    F.LearnPassive(Holder, TEXT("passive_stoke"), 2);
    F.Body(Holder).AttributeProjection.Final.APP2 = 5000;
    FLDBodyCombatState& Body = F.Body(Target);
    FString Error;
    FLDCombatHit Basic = FFixture::Hit(Holder, Target, 100);
    Basic.HitClass = ELDCombatHitClass::BasicPrimary;
    Basic.DamageType = ELDDemoDamageType::Physical;
    if (!TestTrue(TEXT("A primary basic adds its basic stacks of breach and burn: ") + Error, F.Apply(Basic, Error)
        && Body.SharedBreach.Stacks == 2 && Body.Burn.Layers.Num() == 1)) { return false; }
    TestTrue(TEXT("The burn layer snapshots its applier's level and AP: 3 + 3% AP per second"), Body.Burn.Layers[0].Level == 1
        && Body.Burn.Layers[0].SourceAPP2 == 5000 && Body.Burn.Layers[0].SourceEntity == Holder.Victim.EntityId
        && LDCombatKeywords::BurnLayerPerSecondP2(Body.Burn.Layers[0].Level, Body.Burn.Layers[0].SourceAPP2,
            FLDKeywordTerms::From(F.Catalog->Get().Parameters)) == 450);
    FLDCombatHit Skill = FFixture::Hit(Holder, Target, 100);
    Skill.Source.Kind = ELDCombatSourceKind::Learned;
    Skill.Source.DefinitionId = TEXT("active_snark_bolt");
    Skill.Token = 7;
    TestTrue(TEXT("A Q/W hit adds the skill stacks (burn up to its five-layer cap)"), F.Apply(Skill, Error)
        && Body.SharedBreach.Stacks == 4 && Body.Burn.Layers.Num() == 5);
    FLDCombatHit Pulse = Skill;
    Pulse.bPeriodic = true;
    TestTrue(TEXT("A periodic tick adds nothing"), F.Apply(Pulse, Error) && Body.SharedBreach.Stacks == 4 && Body.Burn.Layers.Num() == 5);
    FLDCombatHit Reflect = Skill;
    Reflect.bReflect = true;
    TestTrue(TEXT("Nor does a reflection"), F.Apply(Reflect, Error) && Body.SharedBreach.Stacks == 4);

    // The damage-triggered shield: granted on damage, then rest; never resting after a refused (weaker) shield.
    const FLDCombatBodyRef Guarded = F.AddBody(), Shielded = F.AddBody();
    // Advanced passive_moral_victory at level 1: 60 + 4% max HP for 3 s, 10 s rest, a 50% burst on break.
    F.LearnPassive(Guarded, TEXT("passive_moral_victory"), 2);
    F.LearnPassive(Shielded, TEXT("passive_moral_victory"), 2);
    FLDBodyCombatState& Guard = F.Body(Guarded);
    TestTrue(TEXT("Damage taken grants 60 + 4% max HP for 3 s"), F.Apply(FFixture::Hit(Target, Guarded, 1000), Error)
        && Guard.Shield.IsSet() && Guard.Resources.ShieldP2 == 10000 && Guard.Shield->ExpiresAtMs == 4000);
    TestTrue(TEXT("and starts the 10 s rest"), Guard.DamagedShieldReadyAtMs == 11000);
    TestTrue(TEXT("The burst remembers that shield"), Guard.ShieldBurst.IsSet() && Guard.ShieldBurst->ShieldEffectId == Guard.Shield->EffectId
        && Guard.ShieldBurst->GrantedP2 == 10000 && Guard.ShieldBurst->RatioBp == 5000);
    TestEqual(TEXT("The breaker takes 50% of the granted shield"),
        LDCombatKeywords::ShieldBurstShareP2(10000,5000,false),int64(5000));
    TestEqual(TEXT("Every other nearby enemy takes 25% of the granted shield"),
        LDCombatKeywords::ShieldBurstShareP2(10000,5000,true),int64(2500));
    TestTrue(TEXT("While resting no new shield comes"), F.Apply(FFixture::Hit(Target, Guarded, 1000), Error)
        && Guard.Resources.ShieldP2 == 9000 && Guard.DamagedShieldReadyAtMs == 11000);
    FLDBodyCombatState& Strong = F.Body(Shielded);
    const FGuid Held = FGuid::NewGuid();
    Strong.Shield = FLDCombatShield{Held, Shielded.Victim.EntityId, 9000};
    Strong.Resources.ShieldP2 = 50000;
    TestTrue(TEXT("A shield weaker than the one held is not granted"), F.Apply(FFixture::Hit(Target, Shielded, 1000), Error)
        && Strong.Shield->EffectId == Held);
    TestEqual(TEXT("and then the rest does not start"), Strong.DamagedShieldReadyAtMs, int64(0));
    FAccess::PendingHits(*F.Driver).Reset();
    TestTrue(TEXT("A body without a remembered burst ignores another broken shield"),
        FAccess::BurstBrokenShield(*F.Driver, Guard, FGuid::NewGuid(), Target, 1000, Error)
        && !Guard.ShieldBurst.IsSet() && FAccess::PendingHits(*F.Driver).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildEmpowerChargesTest, "Lockdown.Rules.DemoBuildPrimitives.EmpowerCharges",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildEmpowerChargesTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Attacker = F.AddBody(), Camp = F.AddBody(ELDVictimKind::CampMember);
    FLDBodyCombatState& Body = F.Body(Attacker);
    FLDAttributeBuildValues Empty(1, {}, {});
    const FLDAttributeProjectionResult Base = LDAttributeProjection::Project(Empty, *F.Catalog);
    if (!TestTrue(TEXT("A level-1 projection"), Base.IsSuccess())) { return false; }
    Body.BuildProjection = Body.AttributeProjection = Base.Value.GetValue();
    // Advanced passive_iced_americano refunds 0.15 s of every active per landed basic; advanced passive_freeloader
    // steals 18% and doubles it against a body below half HP.
    F.LearnPassive(Attacker, TEXT("passive_iced_americano"), 2);
    F.LearnPassive(Attacker, TEXT("passive_freeloader"), 2);
    FLDCombatEntityRecord& Record = FAccess::Records(*F.Driver).FindChecked(Attacker.Victim.EntityId);
    Record.Build.Actives = {{TEXT("active_snark_bolt"), 1, ELDCombatEntry::Q}, {TEXT("active_pants_down"), 1, ELDCombatEntry::W},
        {TEXT("active_elbow"), 1, ELDCombatEntry::E}};
    for (const TCHAR* Id : {TEXT("active_snark_bolt"), TEXT("active_pants_down"), TEXT("active_elbow")})
    {
        FLDCombatTrack& Track = Record.Tracks.AddDefaulted_GetRef();
        Track.IssuedCooldownId = FGuid::NewGuid();
        Track.Kind = ELDCombatSourceKind::Learned;
        Track.DefinitionId = Id;
        Track.ReadyAtMs = 6000;
    }
    const FGuid Release = FGuid::NewGuid();
    FLDCombatEmpower Armed;
    Armed.EffectId = Release;
    Armed.SourceDefinitionId = TEXT("active_triple_combo");
    Armed.BaseP2 = 2000;
    Armed.ExpiresAtMs = 6000;
    Armed.Charges = 3;
    Armed.CleaveRatioBp = 3500;
    Armed.CleaveRadiusCm = 180;
    Armed.FinalBreachStacks = 2;
    Armed.bChargeBonus = true;
    Body.Empower = Armed;
    FLDCombatTimedBonus Haste;
    Haste.EffectId = Release;
    Haste.SourceDefinition = Armed.SourceDefinitionId;
    Haste.Stats.Add({ELDDemoBonusStat::AttackSpeedBonusRatio, 3000, {}});
    Haste.ExpiresAtMs = 6000;
    Body.TimedBonuses.Add(Haste);
    FString Error;
    for (int32 Swing = 1; Swing <= 4; ++Swing)
    {
        FAccess::PendingHits(*F.Driver).Reset();
        FLDCombatAction Action;
        Action.Kind = ELDCombatActionKind::Basic;
        Action.Key.Origin = ELDCombatRequestOrigin::Nav;
        Action.Token = uint64(Swing);
        Action.Entry = ELDCombatEntry::Basic;
        Action.Source.Kind = ELDCombatSourceKind::Basic;
        Action.TargetBody = Camp;
        // The landing half only: release sight and observation need a Vision director this fixture does not build.
        if (!TestTrue(FString::Printf(TEXT("Swing %d lands: %s"), Swing, *Error),
            FAccess::LandBasic(*F.Driver, F.Actor(Attacker), Body, Action, Error)))
        { return false; }
        const TArray<FLDCombatHit>& Hits = FAccess::PendingHits(*F.Driver);
        const FLDCombatHit* Primary = Hits.FindByPredicate([](const FLDCombatHit& Row) { return Row.HitClass == ELDCombatHitClass::BasicPrimary; });
        if (!TestNotNull(TEXT("The swing's primary hit"), Primary)) { return false; }
        if (Swing <= 3)
        {
            TestTrue(FString::Printf(TEXT("Charge %d adds its damage and carries the cleave"), Swing), Primary->BaseP2 == 2000 && Primary->bCleave
                && Primary->CleaveRatioBp == 3500 && Primary->CleaveRadiusCm == 180);
            TestEqual(FString::Printf(TEXT("Only the third empowered hit carries two breach stacks (%d)"), Swing),
                Primary->BreachStacks, Swing == 3 ? 2 : 0);
            TestEqual(TEXT("The attack-speed bonus lasts while charges remain"),
                Body.TimedBonuses.ContainsByPredicate([&Release](const FLDCombatTimedBonus& Row) { return Row.EffectId == Release; }), Swing < 3);
        }
        else
        {
            TestTrue(TEXT("After three charges the basic is plain"), Primary->BaseP2 == 0 && !Primary->bCleave && !Body.Empower.IsSet());
        }
        TestTrue(TEXT("Every landed basic carries the passive's lifesteal and its low-HP share"), Primary->bLifesteal
            && Primary->LifestealRatioBp == 1800 && Primary->LifestealLowHPBp == 1800 && Primary->LifestealLowHPBelowBp == 5000);
    }
    TestTrue(TEXT("Each landed basic cut all three actives' remaining cooldown by 0.15 s"),
        Record.Tracks[0].ReadyAtMs == 6000 - 4 * 150 && Record.Tracks[1].ReadyAtMs == 6000 - 4 * 150
        && Record.Tracks[2].ReadyAtMs == 6000 - 4 * 150);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildFullDebuffObservationTest, "Lockdown.Rules.DemoBuildPrimitives.FullDebuffObservation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildFullDebuffObservationTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Source = F.AddBody(), Target = F.AddBody();
    FLDBodyCombatState& Body = F.Body(Target);
    const FGuid From = Source.Victim.EntityId;
    const auto Control = [&From](int64 Until) { return FLDCombatRoot{FGuid::NewGuid(), From, Until}; };
    // Every public combat cue at once.
    Body.Roots.Add(Control(5000));
    Body.Shield = FLDCombatShield{FGuid::NewGuid(), From, 5000};
    Body.Resources.ShieldP2 = 1000;
    Body.Slows.Add({FGuid::NewGuid(), From, 3000, 5000});
    FLDCombatTimedBonus Boost;
    Boost.ExpiresAtMs = 5000;
    Body.TimedBonuses.Add(Boost);
    Body.SharedBreach.Stacks = 3;
    Body.SharedBreach.ExpiresAtMs = 5000;
    Body.Disarms.Add(Control(5000));
    Body.Stuns.Add(Control(5000));
    Body.Knockbacks.Add(Control(5000));
    Body.Silences.Add(Control(5000));
    TestTrue(TEXT("Five burn layers"), LDCombatKeywords::ApplyBurn(Body.Burn, Layer(From, 0), 5, 1000, Terms()));
    TArray<FLDObservedStatus> Statuses;
    F.Driver->AppendObservedStatuses(F.Actor(Target), 1000, Statuses);
    TestEqual(TEXT("Each public combat cue shows once"), Statuses.Num(), 10);
    const FLDObservedStatus* Exposed = FFixture::FindStatus(Statuses, ELDDemoStatusId::Exposed);
    const FLDObservedStatus* Burn = FFixture::FindStatus(Statuses, ELDDemoStatusId::Burn);
    TestTrue(TEXT("with the breach and burn stacks"), Exposed && Exposed->Stacks == 3 && Burn && Burn->Stacks == 5
        && FFixture::FindStatus(Statuses, ELDDemoStatusId::Silence));
    // The observation adds respawn protection to a contender's cues; all of them stay inside the published bound.
    Statuses.Add({uint8(ELDDemoStatusId::RespawnProtection), 1, 2000});
    FLDObservedUnitView Unit;
    Unit.Handle = FGuid::NewGuid();
    Unit.ObservationEpoch = Unit.StateRevision = 1;
    Unit.AuthorizedFieldMask = LDObservation::ContenderFields;
    Unit.Kind = ELDObservedUnitKind::Contender;
    Unit.ContenderId = FGuid::NewGuid();
    Unit.Level = 1;
    Unit.Health.CurrentP2 = Unit.Health.MaxP2 = 100000;
    Unit.Mana.MaxP2 = 10000;
    Unit.Statuses = Statuses;
    FString Error;
    TestTrue(TEXT("A fully debuffed contender is still a valid observation"), LDObservation::ValidateUnit(Unit, Error));
    AddInfo(Error);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDExpandedDashCapsulesTest, "Lockdown.Rules.DemoSkillExpansion.DashCapsuleContact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDExpandedDashCapsulesTest::RunTest(const FString& Parameters)
{
    TOptional<double> Contact;
    FString Error;
    const FVector A(0,0,88), B(300,0,88), Target(150,0,88);
    TestTrue(TEXT("Swept capsules contact before their centres coincide"),
        LDDashContactRules::FirstContact(A,B,Target,Target,80,96,Contact,Error)
        && Contact.IsSet() && FMath::IsNearlyEqual(Contact.GetValue(),70./300.,.000001));
    TestTrue(TEXT("Two moving bodies can cross between step endpoints"),
        LDDashContactRules::FirstContact(A,B,FVector(150,-200,88),FVector(150,200,88),80,96,Contact,Error) && Contact.IsSet());
    TestTrue(TEXT("Vertical separation is not a planar false hit"),
        LDDashContactRules::FirstContact(A,B,Target+FVector(0,0,300),Target+FVector(0,0,300),80,96,Contact,Error) && !Contact.IsSet());
    TestTrue(TEXT("Leaving an initial overlap is not a new collision"),
        LDDashContactRules::FirstContact(A,FVector(-300,0,88),FVector(70,0,88),FVector(70,0,88),80,96,Contact,Error) && !Contact.IsSet());
    TestTrue(TEXT("Entering from initial contact is a collision"),
        LDDashContactRules::FirstContact(A,B,FVector(80,0,88),FVector(80,0,88),80,96,Contact,Error)
        && Contact.IsSet() && Contact.GetValue()==0.);
    TestTrue(TEXT("A dash blocked without displacement cannot farm contacts"),
        LDDashContactRules::FirstContact(A,A,Target,A,80,96,Contact,Error) && !Contact.IsSet());
    TestFalse(TEXT("Invalid geometry is an explicit error"),
        LDDashContactRules::FirstContact(A,B,Target,Target,0,96,Contact,Error));
    TestFalse(TEXT("Invalid geometry has a diagnostic"),Error.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDExpandedMitigationTest, "Lockdown.Rules.DemoSkillExpansion.MitigationAndAttackSlow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDExpandedMitigationTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(*this,Catalog)) { return false; }
    FLDDamageEvaluationInput Hit;
    Hit.Definition.Emplace(10000,0,0,ELDDemoDamageType::Physical);
    Hit.HitClass=ELDCombatHitClass::BasicPrimary;
    Hit.bCritical=false;
    Hit.TargetResistanceP2=0;
    Hit.TargetHPP2=100000;
    Hit.TargetShieldP2=2000;
    Hit.bTargetProtected=false;
    Hit.GuardMultiplierBp=4800;
    Hit.PrimaryGuardP2=1200;
    Hit.PrimaryGuardCapBp=2000;
    const auto Result=LDTactical::EvaluateDamage(Hit,*Catalog);
    if (!TestTrue(TEXT("Exact mitigation evaluates"),Result.IsSuccess())) { return false; }
    TestEqual(TEXT("Primary cap follows global mitigation"),Result.Value->DamageP2,int64(3840));
    TestEqual(TEXT("Shield absorbs only after mitigation"),Result.Value->HPLostP2,int64(1840));
    Hit.HitClass=ELDCombatHitClass::Ability;
    const auto Ability=LDTactical::EvaluateDamage(Hit,*Catalog);
    TestTrue(TEXT("Primary guard does not reduce a separate ability hit"),
        Ability.IsSuccess() && Ability.Value->DamageP2==4800);
    Hit.GuardMultiplierBp=-1;
    TestFalse(TEXT("Negative mitigation is refused"),LDTactical::EvaluateDamage(Hit,*Catalog).IsSuccess());

    FLDAttributeBuildValues Values(16,{}, {FLDAttributeCardValue(TEXT("attr_fast_hands"),15)});
    Values.OpeningCard=TEXT("opening_athlete");
    const auto Build=LDAttributeProjection::Project(Values,*Catalog);
    if (!TestTrue(TEXT("Sixteen legal choices project"),Build.IsSuccess())) { return false; }
    FLDTemporaryAttributeTerms Terms;
    FLDDemoStatBonus Speed;
    Speed.Stat=ELDDemoBonusStat::AttackSpeedBonusRatio;
    Speed.Amount=3000;
    Terms.Bonuses.Add(Speed);
    Terms.AttackSlowBp=2500;
    const auto Slowed=LDAttributeProjection::ApplyTemporary(Build.Value.GetValue(),Terms,*Catalog);
    TestTrue(TEXT("Sock slows the capped attack speed, not the pre-cap sum"),
        Slowed.IsSuccess() && Slowed.Value->Final.AttackSpeed.Numerator==69 && Slowed.Value->Final.AttackSpeed.Denominator==40);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDExpandedSustainTest, "Lockdown.Rules.DemoSkillExpansion.CastSustainBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDExpandedSustainTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const auto Source=F.AddBody(), A=F.AddBody(), B=F.AddBody();
    F.LearnPassive(Source,TEXT("passive_snack_cast"),2);
    F.Body(Source).Resources.HpP2=10000;
    F.Body(Source).Resources.MaxMpP2=10000;
    auto Hit=FFixture::Hit(Source,A,10000);
    Hit.Source.Kind=ELDCombatSourceKind::Learned;
    Hit.Source.DefinitionId=TEXT("active_open_mic");
    Hit.Token=1;
    Hit.bDirectAbilityDamage=true;
    Hit.CastLedger=MakeShared<FLDCombatCastLedger>();
    FString Error;
    TestTrue(TEXT("First direct hit heals"),F.Apply(Hit,Error));
    Hit.HitId=FGuid::NewGuid(); Hit.Target=B;
    TestTrue(TEXT("Second target shares the cast budget"),F.Apply(Hit,Error));
    Hit.HitId=FGuid::NewGuid(); Hit.Target=A;
    TestTrue(TEXT("Third wave shares the same budget"),F.Apply(Hit,Error));
    TestEqual(TEXT("Total healing is capped once per cast"),F.Body(Source).Resources.HpP2,int64(12250));
    TestEqual(TEXT("Healing that was not overflow pays no mana"),F.Body(Source).Resources.MpP2,int64(0));
    F.Body(Source).Resources.HpP2=F.Body(Source).Resources.MaxHpP2;
    TestTrue(TEXT("Exhausted cast cannot turn later waves into mana"),F.Apply(Hit,Error));
    TestEqual(TEXT("No second budget"),F.Body(Source).Resources.MpP2,int64(0));
    Hit.EffectId=FGuid::NewGuid();
    Hit.CastLedger=MakeShared<FLDCombatCastLedger>();
    TestTrue(TEXT("A new full-health cast can convert overflow"),F.Apply(Hit,Error));
    Hit.Target=B;
    TestTrue(TEXT("Another target cannot exceed the cast mana cap"),F.Apply(Hit,Error));
    TestEqual(TEXT("Overflow mana is capped at five"),F.Body(Source).Resources.MpP2,int64(500));
    F.Body(Source).Resources.MpP2=0;
    ++F.Body(Source).Victim.Life;
    Hit.CastLedger=MakeShared<FLDCombatCastLedger>();
    TestTrue(TEXT("An independently released old-life hit still resolves"),F.Apply(Hit,Error));
    TestEqual(TEXT("It cannot heal or refill the new life"),F.Body(Source).Resources.MpP2,int64(0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDExpandedProcTest, "Lockdown.Rules.DemoSkillExpansion.UniqueHitAndArgumentChain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDExpandedProcTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const auto Source=F.AddBody(), Target=F.AddBody();
    F.LearnPassive(Source,TEXT("passive_stoke"),2);
    F.LearnPassive(Source,TEXT("passive_argument_chain"),2);
    auto A=FFixture::Hit(Source,Target,1000);
    A.Source.Kind=ELDCombatSourceKind::Learned;
    A.Source.DefinitionId=TEXT("active_open_mic");
    A.Token=1;
    A.bDirectAbilityDamage=true;
    A.CastLedger=MakeShared<FLDCombatCastLedger>();
    A.CastLedger->bUniqueSkillHits=true;
    FString Error;
    TestTrue(TEXT("First wave lands"),F.Apply(A,Error));
    const int32 FirstLayers=F.Body(Target).Burn.Layers.Num();
    A.HitId=FGuid::NewGuid();
    TestTrue(TEXT("Second wave still damages"),F.Apply(A,Error));
    TestEqual(TEXT("It does not add another batch of burn"),F.Body(Target).Burn.Layers.Num(),FirstLayers);
    auto B=FFixture::Hit(Source,Target,1000);
    B.Source.Kind=ELDCombatSourceKind::Learned;
    B.Source.DefinitionId=TEXT("active_sock_shot");
    B.Token=2;
    B.bDirectAbilityDamage=true;
    B.CastLedger=MakeShared<FLDCombatCastLedger>();
    TestTrue(TEXT("Different active triggers the chain"),F.Apply(B,Error));
    TestEqual(TEXT("Only one bonus is enqueued"),FAccess::PendingHits(*F.Driver).Num(),1);
    TestTrue(TEXT("Chain damage carries its post-damage breach and no recursive trigger"),
        FAccess::PendingHits(*F.Driver)[0].bAddedDamage && FAccess::PendingHits(*F.Driver)[0].BreachStacks==1);
    TestTrue(TEXT("Bonus flushes through ordinary combat"),F.Flush(Error));
    TestTrue(TEXT("A later wave of the first cast cannot re-enter the chain"),F.Apply(A,Error));
    TestTrue(TEXT("No repeated chain bonus"),FAccess::PendingHits(*F.Driver).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDExpandedRefundTest, "Lockdown.Rules.DemoSkillExpansion.DashRefundIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDExpandedRefundTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const auto Source=F.AddBody();
    F.LearnPassive(Source,TEXT("passive_dash_contact_refund"),2);
    auto& Body=F.Body(Source);
    Body.Resources.MaxMpP2=10000;
    FLDCombatDashContact Dash;
    Dash.Body=Source;
    Dash.EffectId=FGuid::NewGuid();
    Dash.Source.Kind=ELDCombatSourceKind::Learned;
    Dash.Source.DefinitionId=TEXT("active_bail_out");
    Dash.PaidManaP2=4000;
    Dash.bCanRefund=true;
    auto& Tracks=FAccess::Records(*F.Driver).FindChecked(Source.Victim.EntityId).Tracks;
    FLDCombatTrack Track;
    Track.Kind=Dash.Source.Kind; Track.DefinitionId=Dash.Source.DefinitionId;
    Track.IssuedCooldownId=FGuid::NewGuid(); Track.LastEffectId=Dash.EffectId; Track.ReadyAtMs=10000;
    Tracks.Add(Track);
    FString Error;
    TestTrue(TEXT("Paid dash contact refunds its own track"),FAccess::DashContact(*F.Driver,Body,Dash,1000,Error));
    TestEqual(TEXT("One remaining cooldown cut"),Tracks[0].ReadyAtMs,int64(8500));
    TestEqual(TEXT("Refund is twenty percent of actual payment"),Body.Resources.MpP2,int64(800));
    TestTrue(TEXT("Another enemy on the same dash does not refund again"),FAccess::DashContact(*F.Driver,Body,Dash,1000,Error));
    TestEqual(TEXT("Multi-target cannot duplicate mana"),Body.Resources.MpP2,int64(800));

    FLDCombatDashContact D=Dash;
    D.EffectId=FGuid::NewGuid(); D.bRefunded=false; D.PaidManaP2=0;
    D.Source.Kind=ELDCombatSourceKind::Innate; D.Source.DefinitionId=LDCombatSourceRules::InnateDashId();
    Track.Kind=D.Source.Kind; Track.DefinitionId=D.Source.DefinitionId; Track.LastEffectId=D.EffectId; Track.ReadyAtMs=19000;
    Tracks.Add(Track);
    TestTrue(TEXT("A second source shares the four-second gate"),FAccess::DashContact(*F.Driver,Body,D,2000,Error));
    TestEqual(TEXT("Shared gate leaves D untouched"),Tracks[1].ReadyAtMs,int64(19000));
    Body.ContactRefundReadyAtMs=0;
    TestTrue(TEXT("An independently ready D refund succeeds"),FAccess::DashContact(*F.Driver,Body,D,2000,Error));
    TestEqual(TEXT("A free dash grants no mana"),Body.Resources.MpP2,int64(800));
    TestEqual(TEXT("D still receives its cooldown refund"),Tracks[1].ReadyAtMs,int64(17500));
    auto Stale=Dash; Stale.bRefunded=false; ++Stale.Body.Victim.Life;
    TestFalse(TEXT("A foreign life cannot use the old receipt"),FAccess::DashContact(*F.Driver,Body,Stale,7000,Error));
    TestEqual(TEXT("Rejected life leaves mana untouched"),Body.Resources.MpP2,int64(800));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDExpandedDeliveryTest, "Lockdown.Rules.DemoSkillExpansion.PostureAndPublicEffects",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDExpandedDeliveryTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const auto Source=F.AddBody();
    auto& Body=F.Body(Source);
    FString Error;
    for (const TCHAR* Id : {TEXT("active_open_mic"),TEXT("active_calculus_drop"),TEXT("active_flyer_trap")})
    {
        const FLDDemoAbilityDefinition* Ability=nullptr;
        if (!TestTrue(TEXT("Approved delivery exists"),F.Catalog->FindAbility(Id,Ability,Error))) { return false; }
        auto Hit=FFixture::Hit(Source,Source,10000);
        Hit.Source.Kind=ELDCombatSourceKind::Learned; Hit.Source.DefinitionId=Id;
        Hit.CastLedger=MakeShared<FLDCombatCastLedger>();
        Hit.bDirectAbilityDamage=true;
        FLDCombatAction Action;
        Action.Direction=FVector(1,0,0); Action.TargetPoint=FVector(2000,0,0); Action.Key.DirectSequence=1;
        bool Handled=false;
        TestTrue(TEXT("Delivery is registered instead of immediate area damage"),
            FAccess::Delivery(*F.Driver,Body,Ability->Ranks[1],Ability->Target,Action,Hit,1000,Handled,Error) && Handled);
    }
    TestTrue(TEXT("Channel is an ongoing action cue"),ALDCombatDirector::CueFactsOf(Body).bActing);
    TArray<FLDObservedProjectileFact> Facts;
    TestTrue(TEXT("Telegraphs and channel export public geometry"),F.Driver->ReadObservedProjectiles(Facts,Error));
    TestEqual(TEXT("Three distinct visible effects"),Facts.Num(),3);
    TestTrue(TEXT("Book telegraph exposes outer and centre radii"),
        Facts.ContainsByPredicate([](const auto& Fact) { return Fact.Look==ELDObservedProjectileLook::BookShadow
            && Fact.RadiusCm==180 && Fact.InnerRadiusCm==80 && Fact.PositionCm.X==650; }));
    Body.CowerUntilMs=3000; Body.CowerReductionBp=4000;
    TestTrue(TEXT("Non-aggressive movement can end the channel without ending cower"),
        FAccess::Posture(*F.Driver,Body,false,1000,Error) && !Body.ChannelEffectId.IsValid() && Body.CowerUntilMs==3000);
    TestTrue(TEXT("Accepted aggression ends cower"),FAccess::Posture(*F.Driver,Body,true,1000,Error) && Body.CowerUntilMs==0);
    Body.bPendingDeath=true;
    Facts.Reset();
    TestTrue(TEXT("Released ground effects survive normal source death"),F.Driver->ReadObservedProjectiles(Facts,Error));
    TestEqual(TEXT("Only the two independent ground effects remain visible"),Facts.Num(),2);
    FAccess::CancelEntityEffects(*F.Driver,Source.Victim.EntityId);
    TestTrue(TEXT("Departure cancels those independent effects"),FAccess::Ground(*F.Driver).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDExpandedCatalogTest, "Lockdown.Rules.DemoSkillExpansion.CatalogGrowthBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDExpandedCatalogTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(*this,Catalog)) { return false; }
    for (const uint8 Level : {uint8(1),uint8(5),uint8(10),uint8(13),uint8(16)})
    {
        for (const auto& Ability : Catalog->Get().Abilities)
        {
            for (const uint8 Rank : {uint8(1),uint8(2)})
            { TestNotNull(TEXT("Every approved skill has a grown rank"),Catalog->FindRank(Ability,Rank,Level)); }
        }
        const FLDDemoAbilityDefinition* Snack=nullptr;
        FString Error;
        if (!Catalog->FindAbility(TEXT("passive_snack_cast"),Snack,Error)) { AddError(Error); return false; }
        const auto* Rank=Catalog->FindRank(*Snack,2,Level);
        TestTrue(TEXT("Mana cap never receives G while healing cap does"),
            Rank && Rank->Effects[0].LimitP2==500 && Rank->Effects[0].BaseP2>=2250);
    }
    return true;
}

#endif
