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

// #147 build v7 combat primitives (docs/systems/COMBAT_AND_GROWTH.md, "构筑 v7 战斗原语") and the #198 build safe window.
// The installed base components that feed the trigger handlers are covered by LDComponentCombatTests.

struct FLDBuildPrimitivesAccess
{
    static TMap<TWeakObjectPtr<ALDCharacter>, FLDBodyCombatState>& Bodies(ALDCombatDirector& Driver) { return Driver.Bodies; }
    static TMap<FGuid, FLDCombatEntityRecord>& Records(ALDCombatDirector& Driver) { return Driver.Records; }
    static TArray<FLDCombatHit>& PendingHits(ALDCombatDirector& Driver) { return Driver.PendingHits; }
    static TArray<FLDCombatZone>& Zones(ALDCombatDirector& Driver) { return Driver.Zones; }
    static bool ApplyHit(ALDCombatDirector& Driver, const FLDCombatHit& Hit, FString& Error) { return Driver.ApplyHit(Hit, Error); }
    static bool AdvanceBurns(ALDCombatDirector& Driver, int64 NowMs, FString& Error) { return Driver.AdvanceBurns(NowMs, Error); }
    static bool LandBasic(ALDCombatDirector& Driver, ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDCombatAction& Action,
        FString& Error)
    { return Driver.LandBasic(Actor, Body, Action, Error); }
    static bool ApplyCleanses(ALDCombatDirector& Driver, FLDBodyCombatState& Body, const FLDDemoAbilityValues& Values, int64 NowMs)
    { return Driver.ApplyCleanses(Body, Values, NowMs); }
    static void ExpireStatuses(ALDCombatDirector& Driver, int64 NowMs) { Driver.ExpireStatuses(NowMs); }
    static void CancelEntityEffects(ALDCombatDirector& Driver, const FGuid& Entity) { Driver.CancelEntityEffects(Entity); }
    static void CancelChannel(ALDCombatDirector& Driver, FLDBodyCombatState& Body) { Driver.CancelChannel(Body); }
    static void KeywordStatuses(const ALDCombatDirector& Driver, const FLDBodyCombatState& Body, int64 NowMs, TArray<FLDObservedStatus>& Out)
    { Driver.AppendKeywordStatuses(Body, NowMs, Out); }
    static void MarkBuildUnsafe(ALDCombatDirector& Driver, const FGuid& Entity, int64 NowMs) { Driver.MarkBuildUnsafe(Entity, NowMs); }
    static bool ReleaseMechanics(ALDCombatDirector& Driver, FLDBodyCombatState& Body, const FLDCombatSourceRef& Source,
        const FLDDemoAbilityValues& Values, const FVector& Origin, const FVector& Point, FString& Error)
    { return Driver.ReleaseMechanics(Body, Source, Values, Origin, Point, FGuid::NewGuid(), MakeShared<FLDCombatCastLedger>(), Error); }
    static void Bind(ALDGameMode& Mode, ALDCombatDirector& Driver, const TSharedPtr<const FLDDemoContentSnapshot>& Catalog)
    {
        Mode.Combat = &Driver;
        Mode.DemoContent = Catalog;
    }
    static void SetNow(ALDGameMode& Mode, int64 NowMs) { Mode.AuthorityNowMs = NowMs; }
    static bool Delivery(ALDCombatDirector& Driver, FLDBodyCombatState& Body, const FLDDemoAbilityValues& Values,
        const FLDDemoTargetDefinition& Target, const FLDCombatAction& Action, FLDCombatHit& Hit, int64 Now, bool& Handled, FString& Error)
    { return Driver.StartSkillDelivery(Body, Values, Target, Action, Hit, Now, Handled, Error); }
    static TArray<FLDCombatGroundEffect>& Ground(ALDCombatDirector& Driver) { return Driver.GroundEffects; }
    // #198 B components.
    static bool ResolveDashCompletions(ALDCombatDirector& Driver, int64 NowMs, FString& Error) { return Driver.ResolveDashCompletions(NowMs, Error); }
    static bool ApplyComponentElement(const ALDCombatDirector& Driver, FLDCombatHit& Hit, ELDDemoElement Element,
        const FLDCombatSourceRef& Component, FString& Error)
    { return Driver.ApplyComponentElement(Hit, Element, Component, Error); }
    static bool Collect(const ALDCombatDirector& Driver, const FLDAttributeProjection& Projection, ELDDemoEffectOp Op,
        TArray<FLDPassiveEffectRef>& Out, FString& Error)
    { return Driver.CollectPassiveEffects(Projection, Op, Out, Error); }
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
    int64 LastUnsafe(const FLDCombatBodyRef& Ref) { return FAccess::Records(*Driver).FindChecked(Ref.Victim.EntityId).LastBuildUnsafeAtMs; }

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

    // D22 lifesteal cap.
    TestEqual(TEXT("Lifesteal adds up to the 35% cap"), CapLifestealBp(6000, T), int64(3500));
    TestEqual(TEXT("below the cap it is kept"), CapLifestealBp(2000, T), int64(2000));

    // Charged empowerment.
    TArray<FLDCombatEmpower> Armed;
    FLDCombatEmpower Charged;
    Charged.Source.Kind = ELDCombatSourceKind::Learned;
    Charged.Source.DefinitionId = TEXT("active_triple_combo");
    Charged.Charges = 3;
    Charged.AmountP2 = 2000;
    Charged.ExpiresAtMs = 5000;
    LDCombatControl::ArmEmpower(Armed, Charged);
    TArray<FLDCombatEmpower> Taken;
    int64 SumP2 = 0;
    TestTrue(TEXT("The first charge"), LDCombatControl::ConsumeEmpowers(Armed, 1000, Taken, SumP2) && SumP2 == 2000
        && Armed.Num() == 1 && Armed[0].Charges == 2);
    TestTrue(TEXT("the second"), LDCombatControl::ConsumeEmpowers(Armed, 1100, Taken, SumP2) && SumP2 == 2000
        && Armed.Num() == 1 && Armed[0].Charges == 1);
    TestTrue(TEXT("and the last end the record"), LDCombatControl::ConsumeEmpowers(Armed, 1200, Taken, SumP2)
        && Taken.Num() == 1 && Taken[0].Charges == 0 && Armed.IsEmpty());
    TestTrue(TEXT("then nothing is left"), LDCombatControl::ConsumeEmpowers(Armed, 1300, Taken, SumP2) && Taken.IsEmpty() && SumP2 == 0);
    Charged.ExpiresAtMs = 1000;
    LDCombatControl::ArmEmpower(Armed, Charged);
    TestTrue(TEXT("An expired empowerment is dropped untaken"), LDCombatControl::ConsumeEmpowers(Armed, 1000, Taken, SumP2)
        && Taken.IsEmpty() && Armed.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildDamagePoolTest, "Lockdown.Rules.DemoBuildPrimitives.DamagePool",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildDamagePoolTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(*this, Catalog)) { return false; }
    const auto Evaluate = [&Catalog](ELDCombatHitClass Class, bool bCritical, int64 PoolBp)
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
        return LDTactical::EvaluateDamage(Input, *Catalog);
    };
    const auto Damage = [](const FLDDamageEvaluationResult& Result) { return Result.IsSuccess() ? Result.Value->DamageP2 : int64(-1); };
    // D21: every "+X%" adds into one pool applied once; a basic crit x2 before it. #198: only a primary basic crits.
    TestEqual(TEXT("A 30% pool"), Damage(Evaluate(ELDCombatHitClass::Ability, false, 3000)), int64(13000));
    TestEqual(TEXT("A basic crit doubles before the pool"), Damage(Evaluate(ELDCombatHitClass::BasicPrimary, true, 3000)), int64(26000));
    const FLDDamageEvaluationResult AbilityCrit = Evaluate(ELDCombatHitClass::Ability, true, 3000);
    TestTrue(TEXT("An ability never crits: a critical ability hit is refused, not scaled"),
        !AbilityCrit.IsSuccess() && AbilityCrit.Error.Code == ELDCombatEvaluationError::Domain);
    TestFalse(TEXT("A negative pool is refused"), Evaluate(ELDCombatHitClass::Ability, false, -1).IsSuccess());
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
    // S14's cleanse ends roots and slows, never a disarm.
    Body.Disarms.Add({FGuid::NewGuid(), Caster.Victim.EntityId, 5000});
    Body.Roots.Add({FGuid::NewGuid(), Caster.Victim.EntityId, 5000});
    Body.Slows.Add({FGuid::NewGuid(), Caster.Victim.EntityId, 3000, 5000});
    FLDDemoAbilityValues Cleanse;
    Cleanse.Effects.AddDefaulted_GetRef().Opcode = uint8(ELDDemoEffectOp::DispelSlow);
    Cleanse.Effects.AddDefaulted_GetRef().Opcode = uint8(ELDDemoEffectOp::DispelRoot);
    TestTrue(TEXT("A slow and root cleanse changes the move-speed term"), FAccess::ApplyCleanses(*F.Driver, Body, Cleanse, 1500));
    TestTrue(TEXT("and removes every root and slow, keeping the disarm"), Body.Roots.IsEmpty() && Body.Slows.IsEmpty()
        && Body.Disarms.Num() == 1);
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
    TestEqual(TEXT("Neither slowed nor controlled: no bonus"), Lost(Hit, 100000, false), int64(10000));
    TestEqual(TEXT("Slowed: +12%"), Lost(Hit, 40000, true), int64(11200));
    TestEqual(TEXT("A low-HP body alone gains nothing (B07 reads controls only)"), Lost(Hit, 40000, false), int64(10000));
    FLDCombatHit Idle = Hit;
    Idle.IdleReadyAmpBp = 2000;
    TestEqual(TEXT("B07 and B17 add into one pool, never multiplied"), Lost(Idle, 100000, true), int64(13200));
    FLDCombatHit Basic = FFixture::Hit(Source, Target, 10000);
    Basic.HitClass = ELDCombatHitClass::BasicPrimary;
    Basic.DamageType = ELDDemoDamageType::Physical;
    Basic.bCritical = true;
    Basic.ControlAmpBp = 1200;
    TestEqual(TEXT("A basic crit doubles before the pool"), Lost(Basic, 100000, true), int64(22400));
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
    FString Error;
    FLDCombatHit Hit = FFixture::Hit(Source, Target, 10000);
    Hit.HitClass = ELDCombatHitClass::BasicPrimary;
    Hit.DamageType = ELDDemoDamageType::Physical;
    Hit.OnHitScaleBp = 10000;
    Hit.bLifesteal = true;
    Hit.LifestealRatioBp = 3000;
    Healer.Resources.HpP2 = 50000;
    TestTrue(TEXT("A primary basic heals its lifesteal share of the HP lost"), F.Apply(Hit, Error) && Healer.Resources.HpP2 == 53000);
    Hit.LifestealRatioBp = 6000;
    TestTrue(TEXT("Lifesteal stops at the 35% cap"), F.Apply(Hit, Error) && Healer.Resources.HpP2 == 56500);
    FLDCombatHit Tick = FFixture::Hit(Source, Target, 1000);
    Tick.bBurnTick = Tick.bPeriodic = true;
    Tick.OnHitScaleBp = 10000;
    Tick.bLifesteal = true;
    Tick.LifestealRatioBp = 3000;
    TestTrue(TEXT("A burn tick never heals its applier"), F.Apply(Tick, Error) && Healer.Resources.HpP2 == 56500);
    return true;
}

// #198: S02 roots, it never turns into a stun on a slowed body (the retired slowed-stun).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildRootOnSlowedTest, "Lockdown.Rules.DemoBuildPrimitives.RootOnSlowed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildRootOnSlowedTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Source = F.AddBody(), Slowed = F.AddBody(), Plain = F.AddBody();
    FString Error;
    F.Body(Slowed).Slows.Add({FGuid::NewGuid(), Source.Victim.EntityId, 3000, 5000});
    FLDCombatHit Hit = FFixture::Hit(Source, Slowed, 0);
    Hit.RootDurationMs = 1000;
    TestTrue(TEXT("A root on a slowed body roots it"), F.Apply(Hit, Error) && F.Body(Slowed).Roots.Num() == 1
        && F.Body(Slowed).Roots[0].ExpiresAtMs == 2000 && F.Body(Slowed).Stuns.IsEmpty() && F.Body(Slowed).Slows.Num() == 1);
    Hit.Target = Plain;
    TestTrue(TEXT("exactly as a body that is not slowed"), F.Apply(Hit, Error) && F.Body(Plain).Roots.Num() == 1
        && F.Body(Plain).Stuns.IsEmpty());
    return true;
}

// #198 build safe window: Combat's per-contender timestamp of the last combat action. Hostile positive damage dealt or
// taken (burn ticks included), a hard control or a knockback received mark it; a slow, healing, a self hit, a zero hit and
// reading or expiring statuses do not. The basic windup and offensive release triggers sit in StartNavAttack and
// ReleaseAbility; IsOffensive below is their classification of every approved active.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildSafeWindowTest, "Lockdown.Rules.DemoBuildPrimitives.BuildSafeWindow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildSafeWindowTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const int64 WindowMs = F.Catalog->Get().Parameters.BuildSafeWindowMs;
    TestEqual(TEXT("The approved window is 4 s"), WindowMs, int64(4000));
    TestEqual(TEXT("Never unsafe is safe at once"), LDCombatSourceRules::BuildSafeAtMs(0, WindowMs), int64(0));
    TestEqual(TEXT("The window runs from the last combat action"), LDCombatSourceRules::BuildSafeAtMs(1500, WindowMs), int64(5500));
    TestEqual(TEXT("A window past the clock saturates instead of wrapping"), LDCombatSourceRules::BuildSafeAtMs(MAX_int64 - 10, WindowMs),
        MAX_int64);

    // Offensive releases: every approved active with a hostile effect; a self buff, a shield or a plain dash is not one.
    const TSet<FString> Peaceful = {TEXT("active_bail_out"), TEXT("active_not_mad"), TEXT("active_triple_combo")};
    for (const FLDDemoAbilityDefinition& Ability : F.Catalog->Get().Abilities)
    {
        TestEqual(FString::Printf(TEXT("%s is %s"), *Ability.Id, Peaceful.Contains(Ability.Id) ? TEXT("not offensive") : TEXT("offensive")),
            LDCombatSourceRules::IsOffensive(Ability.Values), !Peaceful.Contains(Ability.Id));
    }
    TestFalse(TEXT("The innate dash is not offensive"), LDCombatSourceRules::IsOffensive(F.Catalog->Get().Character.Dash.Values));

    const FLDCombatBodyRef Attacker = F.AddBody(), Victim = F.AddBody(), Bystander = F.AddBody(), Camp = F.AddBody(ELDVictimKind::CampMember);
    FString Error;
    TestEqual(TEXT("A new contender has no combat action"), F.Driver->ReadBuildSafeAtMs(Victim.Victim.EntityId), int64(0));
    F.Now(2000);
    TestTrue(TEXT("A zero hit lands"), F.Apply(FFixture::Hit(Attacker, Victim, 0), Error));
    TestTrue(TEXT("A hit without damage marks nobody"), F.LastUnsafe(Attacker) == 0 && F.LastUnsafe(Victim) == 0);
    FLDCombatHit SelfHit = FFixture::Hit(Victim, Victim, 1000);
    TestTrue(TEXT("A self hit lands"), F.Apply(SelfHit, Error));
    TestEqual(TEXT("A self-inflicted hit marks nobody"), F.LastUnsafe(Victim), int64(0));
    F.Now(2500);
    TestTrue(TEXT("A hostile hit lands"), F.Apply(FFixture::Hit(Attacker, Victim, 1000), Error));
    TestTrue(TEXT("Hostile positive damage marks its victim and its credited contender"),
        F.LastUnsafe(Victim) == 2500 && F.LastUnsafe(Attacker) == 2500 && F.LastUnsafe(Bystander) == 0);
    TestEqual(TEXT("Combat reads the window for growth commits"), F.Driver->ReadBuildSafeAtMs(Victim.Victim.EntityId), int64(2500) + WindowMs);
    F.Now(2600);
    TestTrue(TEXT("A camp member's hit lands"), F.Apply(FFixture::Hit(Camp, Bystander, 1000), Error));
    TestEqual(TEXT("A camp's hostile damage marks the contender it hit"), F.LastUnsafe(Bystander), int64(2600));
    F.Now(2700);
    TestTrue(TEXT("A hit on a camp member lands"), F.Apply(FFixture::Hit(Attacker, Camp, 1000), Error));
    TestEqual(TEXT("Damaging a camp member marks the contender credited with it"), F.LastUnsafe(Attacker), int64(2700));
    FAccess::MarkBuildUnsafe(*F.Driver, Attacker.Victim.EntityId, 2650);
    TestEqual(TEXT("An older combat action never moves the window back"), F.LastUnsafe(Attacker), int64(2700));

    // Burn ticks are hostile damage of their applier.
    FLDCombatHit Burning = FFixture::Hit(Bystander, Victim, 0);
    Burning.BurnStacks = 1;
    Burning.BurnAPP2 = 0;
    TestTrue(TEXT("A burn layer without damage lands"), F.Apply(Burning, Error));
    TestEqual(TEXT("A layer alone is no damage yet"), F.LastUnsafe(Bystander), int64(2600));
    F.Now(3200);
    TestTrue(TEXT("The burn ticks"), FAccess::AdvanceBurns(*F.Driver, 3200, Error) && F.Flush(Error));
    TestTrue(TEXT("A burn tick marks its victim and its applier"), F.LastUnsafe(Victim) == 3200 && F.LastUnsafe(Bystander) == 3200);

    // Controls received: hard controls and knockbacks mark the victim; a slow does not.
    const auto Commit = [&](ELDControlKind Kind, int64 AtMs)
    {
        F.Now(AtMs);
        FLDControlApplication App;
        App.Kind = Kind;
        App.EffectId = FGuid::NewGuid();
        App.SourceEntity = App.CreditOwner = Camp.Victim.EntityId;
        App.SourceBody = Camp;
        App.DurationMs = 500;
        App.RatioBp = Kind == ELDControlKind::Slow ? 3000 : 0;
        bool bApplied = false;
        return F.Driver->CommitControl(F.Body(Bystander), App, AtMs, bApplied, Error) && bApplied;
    };
    TestTrue(TEXT("A slow lands"), Commit(ELDControlKind::Slow, 4000));
    TestEqual(TEXT("A slow alone is no combat action"), F.LastUnsafe(Bystander), int64(3200));
    int64 At = 4100;
    for (const ELDControlKind Kind : {ELDControlKind::Root, ELDControlKind::Stun, ELDControlKind::Silence, ELDControlKind::Disarm,
        ELDControlKind::Taunt, ELDControlKind::Knockback})
    {
        TestTrue(FString::Printf(TEXT("Control %u lands"), uint32(Kind)), Commit(Kind, At));
        TestEqual(FString::Printf(TEXT("Control %u received marks its victim"), uint32(Kind)), F.LastUnsafe(Bystander), At);
        At += 100;
    }

    // Healing, reading the body and expiring statuses never refresh the window.
    const int64 Before = F.LastUnsafe(Victim);
    F.Now(9000);
    F.Body(Victim).Resources.HpP2 = 50000;
    FLDCombatHit Heal = FFixture::Hit(Victim, Victim, 0);
    TestTrue(TEXT("A self hit without damage lands"), F.Apply(Heal, Error));
    TArray<FLDObservedStatus> Statuses;
    F.Driver->AppendObservedStatuses(F.Actor(Victim), 9000, Statuses);
    FAccess::ExpireStatuses(*F.Driver, 9000);
    TestTrue(TEXT("Observation and expiry are no combat actions"), F.LastUnsafe(Victim) == Before
        && F.Driver->ReadBuildSafeAtMs(Victim.Victim.EntityId) == Before + WindowMs);
    // The window is the contender's, not the body's: it outlives a removed body.
    FAccess::Bodies(*F.Driver).Remove(&F.Actor(Victim));
    TestEqual(TEXT("A death keeps the window"), F.Driver->ReadBuildSafeAtMs(Victim.Victim.EntityId), Before + WindowMs);
    return true;
}

// #198 S08 脚底开锅: a fixed zone with three pulses at +0/+1/+2 s; each pulse deals its damage, adds one burn layer and slows
// 20% for 0.5 s (zone_slow DurationMs, no linger, no root).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildZonePulsesTest, "Lockdown.Rules.DemoBuildPrimitives.ZonePulses",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildZonePulsesTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDDemoAbilityDefinition* Ability = nullptr;
    FString Error;
    if (!TestTrue(TEXT("S08 is approved"), F.Catalog->FindAbility(TEXT("active_quick_meeting"), Ability, Error) && Ability)) { return false; }
    const FLDDemoAbilityValues* Values = F.Catalog->FindValues(*Ability, 1);
    if (!TestNotNull(TEXT("S08 has its level-1 values"), Values)) { return false; }
    const auto Op = [Values](ELDDemoEffectOp Wanted) { return Values->Effects.FindByPredicate(
        [Wanted](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp() == Wanted; }); };
    const FLDDemoEffectDefinition* Zone = Op(ELDDemoEffectOp::Zone);
    const FLDDemoEffectDefinition* Slow = Op(ELDDemoEffectOp::ZoneSlow);
    const FLDDemoEffectDefinition* Burn = Op(ELDDemoEffectOp::ZoneBurn);
    if (!TestTrue(TEXT("S08 carries a zone, its slow and its burn"), Zone && Slow && Burn && Op(ELDDemoEffectOp::ZoneDamage)
        && !Op(ELDDemoEffectOp::Root)))
    { return false; }
    TestTrue(TEXT("Three pulses one second apart from the release, inside three seconds"), Zone->Count == 3 && Zone->IntervalMs == 1000
        && Zone->DelayMs == 0 && Zone->DurationMs == 3000);
    TestTrue(TEXT("Each check slows 20% for 0.5 s and burns one layer every second"), Slow->RatioBp == 2000 && Slow->IntervalMs == 1000
        && Slow->DurationMs == 500 && Burn->Count == 1 && Burn->IntervalMs == 1000);

    const FLDCombatBodyRef Caster = F.AddBody();
    FLDCombatSourceRef Source;
    Source.Kind = ELDCombatSourceKind::Learned;
    Source.DefinitionId = TEXT("active_quick_meeting");
    F.Now(5000);
    if (!TestTrue(FString::Printf(TEXT("The zone is released: %s"), *Error), FAccess::ReleaseMechanics(*F.Driver, F.Body(Caster), Source,
        *Values, FVector(0, 0, 0), FVector(300, 0, 0), Error) && FAccess::Zones(*F.Driver).Num() == 1))
    { return false; }
    const FLDCombatZone& Placed = FAccess::Zones(*F.Driver)[0];
    TestTrue(TEXT("The zone keeps the approved schedule from its release"), Placed.StartMs == 5000 && Placed.EndMs == 8000
        && Placed.Count == 3 && Placed.IntervalMs == 1000 && Placed.DelayMs == 0 && Placed.bDamage && Placed.CentreCm.X == 300);
    TestTrue(TEXT("Each slow check slows for the check's own duration, starting at the release"), Placed.bSlow && Placed.SlowBp == 2000
        && Placed.SlowIntervalMs == 1000 && Placed.SlowDurationMs == 500 && Placed.NextSlowAtMs == 5000);
    TestTrue(TEXT("The burn checks start at the release"), Placed.BurnStacks == 1 && Placed.BurnIntervalMs == 1000 && Placed.NextBurnAtMs == 5000);
    TArray<int64> Pulses;
    for (int64 At = Placed.StartMs; At <= Placed.EndMs; ++At)
    {
        const int64 Next = LDCombatMechanics::NextPulseAtMs(Placed.StartMs, Placed.DelayMs, Placed.IntervalMs, Placed.Count, At);
        if (Next == At) { Pulses.Add(At); }
    }
    TestTrue(TEXT("Exactly three damage pulses: +0, +1 and +2 s, none at the end"),
        Pulses == TArray<int64>({5000, 6000, 7000}));
    TArray<int64> Checks;
    for (int64 At = Placed.NextSlowAtMs; At < Placed.EndMs; At += Placed.SlowIntervalMs) { Checks.Add(At); }
    TestTrue(TEXT("Exactly three slow and burn checks on the same beats (the end is no check)"),
        Checks == TArray<int64>({5000, 6000, 7000}));
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
    const FLDAttributeProjectionResult Base = LDAttributeProjection::Project(FLDAttributeBuildValues(1, {}), *F.Catalog);
    if (!TestTrue(TEXT("A level-1 projection"), Base.IsSuccess())) { return false; }
    Body.BuildProjection = Body.AttributeProjection = Base.Value.GetValue();
    FLDCombatEntityRecord& Record = FAccess::Records(*F.Driver).FindChecked(Attacker.Victim.EntityId);
    Record.Build.Actives = {{TEXT("active_snark_bolt"), ELDCombatEntry::Q}, {TEXT("active_pants_down"), ELDCombatEntry::W},
        {TEXT("active_elbow"), ELDCombatEntry::E}};
    for (const TCHAR* Id : {TEXT("active_snark_bolt"), TEXT("active_pants_down"), TEXT("active_elbow")})
    {
        FLDCombatTrack& Track = Record.Tracks.AddDefaulted_GetRef();
        Track.IssuedCooldownId = FGuid::NewGuid();
        Track.Kind = ELDCombatSourceKind::Learned;
        Track.DefinitionId = Id;
        Track.ReadyAtMs = 6000;
    }
    // S17 左右开抽: three charges of +16 physical each, attack speed +25% while charges remain.
    const FGuid Release = FGuid::NewGuid();
    FLDCombatEmpower Armed;
    Armed.EffectId = Release;
    Armed.Source.Kind = ELDCombatSourceKind::Learned;
    Armed.Source.DefinitionId = TEXT("active_triple_combo");
    Armed.AmountP2 = 1600;
    Armed.ExpiresAtMs = 6000;
    Armed.Charges = 3;
    Armed.bChargeBonus = true;
    Body.Empowers.Add(Armed);
    FLDCombatTimedBonus Haste;
    Haste.EffectId = Release;
    Haste.SourceDefinition = Armed.Source.DefinitionId;
    Haste.Stats.Add({ELDDemoBonusStat::AttackSpeedBonusRatio, 2500, {}});
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
        TestEqual(TEXT("A basic hits its one target only"), Hits.Num(), 1);
        if (Swing <= 3)
        {
            TestEqual(FString::Printf(TEXT("Charge %d adds its damage"), Swing), Primary->BaseP2, int64(1600));
            TestEqual(FString::Printf(TEXT("No empowered hit adds breach (%d)"), Swing), Primary->BreachStacks, 0);
            TestEqual(TEXT("The attack-speed bonus lasts while charges remain"),
                Body.TimedBonuses.ContainsByPredicate([&Release](const FLDCombatTimedBonus& Row) { return Row.EffectId == Release; }), Swing < 3);
        }
        else
        {
            TestTrue(TEXT("After three charges the basic is plain"), Primary->BaseP2 == 0 && Body.Empowers.IsEmpty());
        }
        TestFalse(TEXT("Without a held trigger a basic carries no lifesteal"), Primary->bLifesteal);
    }
    TestTrue(TEXT("Without a held trigger a landed basic refunds no cooldown"),
        Record.Tracks[0].ReadyAtMs == 6000 && Record.Tracks[1].ReadyAtMs == 6000 && Record.Tracks[2].ReadyAtMs == 6000);
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
    Hit.DeliveryScaleBp=4800;
    Hit.PrimaryGuardP2=1200;
    Hit.PrimaryGuardCapBp=2000;
    const auto Result=LDTactical::EvaluateDamage(Hit,*Catalog);
    if (!TestTrue(TEXT("Exact mitigation evaluates"),Result.IsSuccess())) { return false; }
    TestEqual(TEXT("The primary guard cap follows the delivered amount"),Result.Value->DamageP2,int64(3840));
    TestEqual(TEXT("Shield absorbs only after mitigation"),Result.Value->HPLostP2,int64(1840));
    Hit.HitClass=ELDCombatHitClass::Ability;
    const auto Ability=LDTactical::EvaluateDamage(Hit,*Catalog);
    TestTrue(TEXT("Primary guard does not reduce a separate ability hit"),
        Ability.IsSuccess() && Ability.Value->DamageP2==4800);
    Hit.DeliveryScaleBp=-1;
    TestFalse(TEXT("A negative delivery share is refused"),LDTactical::EvaluateDamage(Hit,*Catalog).IsSuccess());

    // Seventy agility points past level 16 push attack speed past its 2.3 cap.
    FLDAttributeBuildValues Values(16,{FLDStatPointsValue(TEXT("stat_agility"),70)});
    const auto Build=LDAttributeProjection::Project(Values,*Catalog);
    if (!TestTrue(TEXT("The capped build projects"),Build.IsSuccess())) { return false; }
    FLDTemporaryAttributeTerms Terms;
    FLDDemoStatBonus Speed;
    Speed.Stat=ELDDemoBonusStat::AttackSpeedBonusRatio;
    Speed.Amount=3000;
    Terms.Bonuses.Add(Speed);
    Terms.AttackSlowBp=2500;
    const auto Slowed=LDAttributeProjection::ApplyTemporary(Build.Value.GetValue(),Terms,*Catalog);
    TestTrue(TEXT("An attack slow slows the capped attack speed, not the pre-cap sum"),
        Slowed.IsSuccess() && Slowed.Value->Final.AttackSpeed.Numerator==69 && Slowed.Value->Final.AttackSpeed.Denominator==40);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDExpandedDeliveryTest, "Lockdown.Rules.DemoSkillExpansion.PublicEffects",
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
            FAccess::Delivery(*F.Driver,Body,Ability->Values,Ability->Target,Action,Hit,1000,Handled,Error) && Handled);
    }
    TestTrue(TEXT("Channel is an ongoing action cue"),ALDCombatDirector::CueFactsOf(Body).bActing);
    TArray<FLDObservedProjectileFact> Facts;
    TestTrue(TEXT("Telegraphs and channel export public geometry"),F.Driver->ReadObservedProjectiles(Facts,Error));
    TestEqual(TEXT("Three distinct visible effects"),Facts.Num(),3);
    TestTrue(TEXT("The brick telegraph exposes its outer and full-damage radii"),
        Facts.ContainsByPredicate([](const auto& Fact) { return Fact.Look==ELDObservedProjectileLook::BookShadow
            && Fact.RadiusCm==200 && Fact.InnerRadiusCm==80 && Fact.PositionCm.X==650; }));
    FAccess::CancelChannel(*F.Driver,Body);
    TestFalse(TEXT("Cancelling the channel ends it"),Body.ChannelEffectId.IsValid());
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
    for (const auto& Ability : Catalog->Get().Abilities)
    {
        const FLDDemoAbilityValues* First=Catalog->FindValues(Ability,1);
        if (!TestNotNull(*FString::Printf(TEXT("%s has its level-1 values"),*Ability.Id),First)) { continue; }
        for (const uint8 Level : {uint8(5),uint8(10),uint8(13),uint8(16)})
        {
            const FLDDemoAbilityValues* Grown=Catalog->FindValues(Ability,Level);
            bool bBounded=Grown && Grown->ManaCostP2==First->ManaCostP2 && Grown->CooldownMs==First->CooldownMs
                && Grown->Effects.Num()==First->Effects.Num();
            for (int32 Index=0; bBounded && Index<First->Effects.Num(); ++Index)
            {
                bBounded=Grown->Effects[Index].BaseP2>=First->Effects[Index].BaseP2
                    && Grown->Effects[Index].DurationMs==First->Effects[Index].DurationMs
                    && Grown->Effects[Index].RatioBp==First->Effects[Index].RatioBp;
            }
            TestTrue(*FString::Printf(TEXT("%s at level %u grows its amounts only, never mana, cooldown, ratios or durations"),
                *Ability.Id,uint32(Level)),bBounded);
        }
    }
    return true;
}

// #198 B base components (docs/systems/COMBAT_AND_GROWTH.md, "基础构件"): the projection terms, the merged empowered basic,
// B12's dash completion, the B02 ledger and the component element gate, over the approved component catalog.
namespace LDBuildPrimitivesTests
{
struct FInstall
{
    const TCHAR* Id = nullptr;
    FName BoundActive;
    ELDDemoElement Element = ELDDemoElement::None;
};

FLDAttributeProjectionResult ProjectComponents(const FLDDemoContentSnapshot& Catalog, uint8 Level, const TArray<FInstall>& Rows)
{
    FLDAttributeBuildValues Values(Level, {});
    for (const FInstall& Row : Rows)
    {
        FLDInstalledComponentValue& Installed = Values.Components.AddDefaulted_GetRef();
        Installed.ComponentId = FName(Row.Id);
        Installed.BoundActive = Row.BoundActive;
        Installed.Element = Row.Element;
    }
    return LDAttributeProjection::Project(Values, Catalog);
}

// Installs a projection with Rows on a fixture contender at Level (its record's level is what G reads).
bool InstallComponents(FAutomationTestBase& Test, FFixture& F, const FLDCombatBodyRef& Ref, uint8 Level, const TArray<FInstall>& Rows)
{
    const FLDAttributeProjectionResult Result = ProjectComponents(*F.Catalog, Level, Rows);
    if (!Result.IsSuccess())
    {
        Test.AddError(FString::Printf(TEXT("Component projection: %s: %s"), *Result.Error.Field, *Result.Error.Message));
        return false;
    }
    FLDBodyCombatState& Body = F.Body(Ref);
    Body.BuildProjection = Body.AttributeProjection = Result.Value.GetValue();
    FAccess::Records(*F.Driver).FindChecked(Ref.Victim.EntityId).Build.Level = Level;
    return true;
}

bool SameRational(const FLDDemoRational& Value, int64 Numerator, int64 Denominator = 1)
{
    return Value.Denominator > 0 && Value.Numerator * Denominator == Numerator * Value.Denominator;
}

FLDCombatSourceRef SourceOf(ELDCombatSourceKind Kind, const TCHAR* Id)
{
    FLDCombatSourceRef Out;
    Out.Kind = Kind;
    Out.DefinitionId = FName(Id);
    return Out;
}

FLDCombatAction BasicAt(const FLDCombatBodyRef& Target, uint64 Token)
{
    FLDCombatAction Action;
    Action.Kind = ELDCombatActionKind::Basic;
    Action.Key.Origin = ELDCombatRequestOrigin::Nav;
    Action.Token = Token;
    Action.Entry = ELDCombatEntry::Basic;
    Action.Source.Kind = ELDCombatSourceKind::Basic;
    Action.TargetBody = Target;
    return Action;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentProjectionTest, "Lockdown.Rules.Combat.Components.Projection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentProjectionTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    using EAttribute = ELDProjectedAttribute;
    using EError = ELDAttributeProjectionError;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDDemoContentParameters& P = F.Catalog->Get().Parameters;
    // B13/B14 stats, B15's low-HP term and B07's control amplification, listed out of order.
    const FLDAttributeProjectionResult Result = ProjectComponents(*F.Catalog, 10, {{TEXT("component_seeing_red")},
        {TEXT("component_catch_breath")}, {TEXT("component_kick_down")}, {TEXT("component_long_arm")}});
    if (!TestTrue(FString::Printf(TEXT("Installed components project: %s"), *Result.Error.Message), Result.IsSuccess())) { return false; }
    const FLDAttributeProjection& Projected = Result.Value.GetValue();
    const auto Row = [&Projected](EAttribute Attribute) -> const FLDAttributeContributions& { return Projected.Contributions[int32(Attribute)]; };
    TestTrue(TEXT("B13: haste +15 is the Components term, not grown with the level"),
        SameRational(Row(EAttribute::CooldownHaste).Components.Flat, 15) && Projected.Final.CooldownHasteP2 == P.CooldownHasteBaseP2 + 1500);
    TestTrue(TEXT("B13: mana regeneration +1/s"), SameRational(Row(EAttribute::MPRegen).Components.Flat, 1));
    TestTrue(TEXT("B14: basic range +120 cm"), SameRational(Row(EAttribute::AttackRange).Components.Flat, 120)
        && SameRational(Projected.Final.AttackRangeCm, P.AttackRangeCm.Base.Numerator + 120 * P.AttackRangeCm.Base.Denominator,
            P.AttackRangeCm.Base.Denominator));
    TestTrue(TEXT("B15 is a conditional term below 40% HP, never a build term"), Projected.LowHPBonus.IsSet()
        && Projected.LowHPBonus->ThresholdBp == 4000 && Projected.LowHPBonus->Stats.Num() == 2
        && SameRational(Row(EAttribute::AttackSpeed).Components.AdditiveRatio, 0));
    TestTrue(TEXT("B07 is the control amplification term (+12%)"), Projected.ControlAmp.IsSet()
        && Projected.ControlAmp->RatioBp == 1200 && Projected.ControlAmp->ComponentId == FName(TEXT("component_kick_down")));
    TestTrue(TEXT("Every installed component is listed once, sorted by id, at the build level"),
        Projected.InstalledComponents.Num() == 4
        && Projected.InstalledComponents[0].ComponentId == FName(TEXT("component_catch_breath"))
        && Projected.InstalledComponents[1].ComponentId == FName(TEXT("component_kick_down"))
        && Projected.InstalledComponents[2].ComponentId == FName(TEXT("component_long_arm"))
        && Projected.InstalledComponents[3].ComponentId == FName(TEXT("component_seeing_red"))
        && !Projected.InstalledComponents.ContainsByPredicate([](const FLDInstalledComponentTerm& Term) { return Term.Level != 10; }));
    const FLDAttributeProjectionResult Plain = ProjectComponents(*F.Catalog, 10, {});
    TestTrue(TEXT("Without components there is no term"), Plain.IsSuccess() && !Plain.Value->LowHPBonus.IsSet()
        && !Plain.Value->ControlAmp.IsSet() && Plain.Value->InstalledComponents.IsEmpty()
        && SameRational(Plain.Value->Contributions[int32(EAttribute::CooldownHaste)].Components.Flat, 0));

    // Explicit refusals.
    const auto Refused = [&F](const TArray<FInstall>& Rows, EError Code)
    {
        const FLDAttributeProjectionResult Bad = ProjectComponents(*F.Catalog, 1, Rows);
        return !Bad.IsSuccess() && Bad.Error.Code == Code;
    };
    TestTrue(TEXT("A component installed twice is refused"), Refused({{TEXT("component_long_arm")}, {TEXT("component_long_arm")}},
        EError::DuplicateSource));
    TestTrue(TEXT("An unknown component is refused"), Refused({{TEXT("component_nope")}}, EError::UnknownDefinition));
    TestTrue(TEXT("B18 without its binding is refused"), Refused({{TEXT("component_dirty_trick")}}, EError::InputDomain));
    TestTrue(TEXT("A binding on an unbound component is refused"),
        Refused({{TEXT("component_long_arm"), TEXT("active_elbow"), ELDDemoElement::Fire}}, EError::InputDomain));
    TestTrue(TEXT("B18 bound to an active outside its list is refused"),
        Refused({{TEXT("component_dirty_trick"), TEXT("active_bail_out"), ELDDemoElement::Fire}}, EError::InputDomain));
    const FLDAttributeProjectionResult Bound = ProjectComponents(*F.Catalog, 1,
        {{TEXT("component_dirty_trick"), TEXT("active_elbow"), ELDDemoElement::Fire}});
    TestTrue(TEXT("B18 keeps its bound active and element"), Bound.IsSuccess() && Bound.Value->InstalledComponents.Num() == 1
        && Bound.Value->InstalledComponents[0].BoundActive == FName(TEXT("active_elbow"))
        && Bound.Value->InstalledComponents[0].Element == ELDDemoElement::Fire);

    // Trigger effects arrive with their fixed amounts grown for the holder's level; ratios never grow (B16 at G(10)=2.35).
    const FLDAttributeProjectionResult Guard = ProjectComponents(*F.Catalog, 10, {{TEXT("component_thick_hide")}});
    TArray<FLDPassiveEffectRef> Held;
    FString Error;
    if (!TestTrue(TEXT("B16 projects"), Guard.IsSuccess())
        || !TestTrue(FString::Printf(TEXT("B16 resolves: %s"), *Error),
            FAccess::Collect(*F.Driver, Guard.Value.GetValue(), ELDDemoEffectOp::PrimaryGuard, Held, Error)))
    { return false; }
    TestTrue(TEXT("B16 holds 8G = 18.8 at level 10 and its 15% cap"), Held.Num() == 1 && Held[0].Effect.BaseP2 == 1880
        && Held[0].Effect.RatioBp == 1500 && Held[0].Source.Kind == ELDCombatSourceKind::Component
        && Held[0].Source.DefinitionId == FName(TEXT("component_thick_hide")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentEmpowerMergeTest, "Lockdown.Rules.Combat.Components.EmpowerMerge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentEmpowerMergeTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Attacker = F.AddBody(), Camp = F.AddBody(ELDVictimKind::CampMember);
    if (!InstallComponents(*this, F, Attacker, 1, {{TEXT("component_long_arm")}, {TEXT("component_punch_heal")},
        {TEXT("component_third_punch")}}))
    { return false; }
    FLDBodyCombatState& Body = F.Body(Attacker);
    // Spec §10.2: one pending package per source; the same source re-arming replaces its unused value, others coexist.
    FLDCombatEmpower Dash;
    Dash.Source = SourceOf(ELDCombatSourceKind::Learned, TEXT("active_apology_slide"));
    Dash.AmountP2 = 4000;
    Dash.ExpiresAtMs = 4000;
    LDCombatControl::ArmEmpower(Body.Empowers, Dash);
    Dash.AmountP2 = 5000;
    LDCombatControl::ArmEmpower(Body.Empowers, Dash);
    TestTrue(TEXT("The same source re-arming replaces its package"), Body.Empowers.Num() == 1 && Body.Empowers[0].AmountP2 == 5000);
    FLDCombatEmpower Punch;
    Punch.Source = SourceOf(ELDCombatSourceKind::Component, TEXT("component_dash_punch"));
    Punch.AmountP2 = 2500;
    Punch.ExpiresAtMs = 4000;
    LDCombatControl::ArmEmpower(Body.Empowers, Punch);
    TestEqual(TEXT("Another source's package coexists"), Body.Empowers.Num(), 2);
    const int64 BasicRatioBp = F.Catalog->Get().Character.BasicADRatioBp;
    FString Error;
    for (int32 Swing = 1; Swing <= 3; ++Swing)
    {
        FAccess::PendingHits(*F.Driver).Reset();
        if (!TestTrue(FString::Printf(TEXT("Swing %d lands: %s"), Swing, *Error),
            FAccess::LandBasic(*F.Driver, F.Actor(Attacker), Body, BasicAt(Camp, uint64(Swing)), Error)))
        { return false; }
        const TArray<FLDCombatHit>& Hits = FAccess::PendingHits(*F.Driver);
        if (!TestEqual(FString::Printf(TEXT("Swing %d is one primary hit"), Swing), Hits.Num(), 1)) { return false; }
        const FLDCombatHit& Primary = Hits[0];
        TestTrue(FString::Printf(TEXT("Swing %d: B14 scales only the basic's own AD package"), Swing),
            Primary.ADRatioBp == BasicRatioBp * 9000 / 10000);
        TestTrue(FString::Printf(TEXT("Swing %d: B04 lifesteal rides on the one primary hit"), Swing),
            Primary.bLifesteal && Primary.LifestealRatioBp == 1200 && Primary.OnHitScaleBp == 10000);
        TestEqual(FString::Printf(TEXT("Swing %d: B06 counts one primary hit per swing"), Swing), Body.LandedBasics, Swing);
        if (Swing == 1)
        {
            TestTrue(TEXT("Both packages merge into the first primary hit, unscaled, and are consumed once"),
                Primary.BaseP2 == 7500 && Body.Empowers.IsEmpty());
        }
        else
        {
            TestEqual(FString::Printf(TEXT("Swing %d is plain"), Swing), Primary.BaseP2, int64(0));
        }
        TestTrue(FString::Printf(TEXT("Swing %d: B06 pushes 120 cm over 0.12 s on the third basic only"), Swing),
            Swing == 3 ? Primary.KnockbackCm == 120. && Primary.KnockbackMs == 120 && !Primary.bKnockbackFromAnchor
                : Primary.KnockbackCm == 0. && Primary.KnockbackMs == 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentDashEmpowerTest, "Lockdown.Rules.Combat.Components.DashEmpower",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentDashEmpowerTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Runner = F.AddBody(), Plain = F.AddBody();
    if (!InstallComponents(*this, F, Runner, 1, {{TEXT("component_dash_punch")}})) { return false; }
    FLDBodyCombatState& Body = F.Body(Runner);
    ALDCharacter& Actor = F.Actor(Runner);
    Actor.SetActorLocation(FVector(0, 0, 200));
    const auto StartDash = [](FLDBodyCombatState& Into, const ALDCharacter& From, int64 EndMs)
    {
        FLDCombatVoluntaryDash& Dash = Into.VoluntaryDash.Emplace();
        Dash.EffectId = FGuid::NewGuid();
        Dash.Source = SourceOf(ELDCombatSourceKind::Innate, TEXT("innate_dash"));
        Dash.StartFootCm = ALDCombatDirector::FootOf(From);
        Dash.EndMs = EndMs;
    };
    FString Error;
    StartDash(Body, Actor, 1200);
    Actor.SetActorLocation(FVector(300, 0, 200));
    TestTrue(TEXT("Before its end a dash arms nothing"), FAccess::ResolveDashCompletions(*F.Driver, 1100, Error)
        && Body.Empowers.IsEmpty() && Body.VoluntaryDash.IsSet());
    TestTrue(TEXT("A completed voluntary dash that moved arms B12"), FAccess::ResolveDashCompletions(*F.Driver, 1200, Error)
        && Body.Empowers.Num() == 1 && !Body.VoluntaryDash.IsSet());
    const int64 ExpectedP2 = 2500 + Body.AttributeProjection.Final.ADP2 * 2500 / 10000;
    TestTrue(TEXT("B12 snapshots 25G + 0.25 AD at the completion and lasts 3 s"), Body.Empowers.Num() == 1
        && Body.Empowers[0].AmountP2 == ExpectedP2 && Body.Empowers[0].ExpiresAtMs == 4200 && Body.Empowers[0].Charges == 1
        && Body.Empowers[0].Source.Kind == ELDCombatSourceKind::Component
        && Body.Empowers[0].Source.DefinitionId == FName(TEXT("component_dash_punch")));
    StartDash(Body, Actor, 2000);
    TestTrue(TEXT("A dash blocked in place is no completed dash"), FAccess::ResolveDashCompletions(*F.Driver, 2000, Error)
        && Body.Empowers.Num() == 1 && Body.Empowers[0].ExpiresAtMs == 4200 && !Body.VoluntaryDash.IsSet());
    StartDash(Body, Actor, 2500);
    Actor.SetActorLocation(FVector(600, 0, 200));
    // ApplyKnockback drops the voluntary dash it interrupts.
    Body.VoluntaryDash.Reset();
    TestTrue(TEXT("A knocked-back (cancelled) dash arms nothing"), FAccess::ResolveDashCompletions(*F.Driver, 2500, Error)
        && Body.Empowers[0].ExpiresAtMs == 4200);
    StartDash(Body, Actor, 3000);
    Actor.SetActorLocation(FVector(900, 0, 200));
    TestTrue(TEXT("A later completed dash replaces the unused package of the same source"),
        FAccess::ResolveDashCompletions(*F.Driver, 3000, Error) && Body.Empowers.Num() == 1 && Body.Empowers[0].ExpiresAtMs == 6000);
    FLDBodyCombatState& Other = F.Body(Plain);
    StartDash(Other, F.Actor(Plain), 3000);
    F.Actor(Plain).SetActorLocation(FVector(0, 500, 200));
    TestTrue(TEXT("Without B12 a completed dash arms nothing"), FAccess::ResolveDashCompletions(*F.Driver, 3000, Error)
        && Other.Empowers.IsEmpty() && !Other.VoluntaryDash.IsSet());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentLedgerTest, "Lockdown.Rules.Combat.Components.Ledger",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentLedgerTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    using namespace LDCombatKeywords;
    // The pure batch rule.
    FLDCombatLedger Ledger;
    int64 CashP2 = 0;
    bool bRefused = false;
    for (int32 Entry = 1; Entry <= 4; ++Entry)
    {
        TestFalse(FString::Printf(TEXT("Entry %d is booked"), Entry), AddLedgerEntry(Ledger, 1000, 1000 + Entry * 100, 5, 6000, CashP2, bRefused)
            || bRefused);
    }
    TestTrue(TEXT("The fifth entry cashes the batch out at once"), AddLedgerEntry(Ledger, 1300, 1500, 5, 6000, CashP2, bRefused)
        && CashP2 == 5300 && Ledger.EntriesP2.IsEmpty());
    TestFalse(TEXT("A new batch starts"), AddLedgerEntry(Ledger, 1000, 2000, 5, 6000, CashP2, bRefused));
    TestFalse(TEXT("Its window has not passed"), ExpireLedger(Ledger, 7999));
    TestTrue(TEXT("6 s without a new entry clears the unlocked entries"), ExpireLedger(Ledger, 8000) && Ledger.EntriesP2.IsEmpty());
    TestFalse(TEXT("A batch fixes its threshold at its first entry"), AddLedgerEntry(Ledger, 1000, 9000, 3, 6000, CashP2, bRefused));
    TestFalse(TEXT("a later threshold does not move it"), AddLedgerEntry(Ledger, 1000, 9100, 5, 6000, CashP2, bRefused));
    TestTrue(TEXT("so the third entry cashes out"), AddLedgerEntry(Ledger, 1000, 9200, 5, 6000, CashP2, bRefused) && CashP2 == 3000);
    TestFalse(TEXT("An idle batch is cleared before the next entry"), AddLedgerEntry(Ledger, 1000, 10000, 5, 6000, CashP2, bRefused)
        || AddLedgerEntry(Ledger, 1000, 16000, 5, 6000, CashP2, bRefused));
    TestEqual(TEXT("leaving only the new entry"), Ledger.EntriesP2.Num(), 1);
    TestTrue(TEXT("A ledger without a threshold is refused"), !AddLedgerEntry(Ledger, 1000, 16100, 0, 6000, CashP2, bRefused) && bRefused);

    // B02 on Combat: an owner at level 3 books 10G = 13 per effective hit; the fifth cashes out once as magic damage.
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Owner = F.AddBody(), Target = F.AddBody();
    if (!InstallComponents(*this, F, Owner, 3, {{TEXT("component_tab_ledger")}})) { return false; }
    FLDBodyCombatState& Victim = F.Body(Target);
    FString Error;
    for (int32 Hit = 1; Hit <= 4; ++Hit)
    {
        TestTrue(FString::Printf(TEXT("Hit %d lands: %s"), Hit, *Error), F.Apply(FFixture::Hit(Owner, Target, 10000), Error));
    }
    TestTrue(TEXT("Four effective hits book four entries of 13 on the target life"), Victim.Ledgers.Num() == 1
        && Victim.Ledgers[0].EntriesP2 == TArray<int64>({1300, 1300, 1300, 1300}) && FAccess::PendingHits(*F.Driver).IsEmpty());
    FLDCombatHit Tick = FFixture::Hit(Owner, Target, 1000);
    Tick.bBurnTick = Tick.bPeriodic = true;
    TestTrue(TEXT("A burn tick is status damage and books nothing"), F.Apply(Tick, Error) && Victim.Ledgers[0].EntriesP2.Num() == 4);
    TestTrue(TEXT("A zero hit books nothing"), F.Apply(FFixture::Hit(Owner, Target, 0), Error) && Victim.Ledgers[0].EntriesP2.Num() == 4);
    TestTrue(TEXT("The fifth hit lands"), F.Apply(FFixture::Hit(Owner, Target, 10000), Error));
    const TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    TestTrue(TEXT("The batch cashes out as one magic skill-class package of 65"), Pending.Num() == 1 && Pending[0].bLedgerCashOut
        && Pending[0].BaseP2 == 6500 && Pending[0].DamageType == ELDDemoDamageType::Magical
        && Pending[0].HitClass == ELDCombatHitClass::Ability && Pending[0].Source.DefinitionId == FName(TEXT("component_tab_ledger"))
        && Victim.Ledgers.IsEmpty());
    const int64 Before = Victim.Resources.HpP2;
    TestTrue(TEXT("The cash-out lands"), F.Flush(Error) && Before - Victim.Resources.HpP2 == 6500);
    TestTrue(TEXT("and never books an entry itself"), Victim.Ledgers.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentElementGateTest, "Lockdown.Rules.Combat.Components.ElementGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentElementGateTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatSourceRef Trick = SourceOf(ELDCombatSourceKind::Component, TEXT("component_dirty_trick"));
    FString Error;
    FLDCombatHit Probe;
    TestTrue(TEXT("Fire adds one burn layer of the component"), FAccess::ApplyComponentElement(*F.Driver, Probe, ELDDemoElement::Fire,
        Trick, Error) && Probe.ComponentBurnStacks == 1 && Probe.ComponentBurnSource == Trick);
    for (const ELDDemoElement Element : {ELDDemoElement::Water, ELDDemoElement::Electric})
    {
        FLDCombatHit Wet;
        TestTrue(FString::Printf(TEXT("Element %u is an explicit error until #198 A"), uint32(Element)),
            !FAccess::ApplyComponentElement(*F.Driver, Wet, Element, Trick, Error) && Error.Contains(TEXT("#198 A")));
    }
    TestFalse(TEXT("No element is an error"), FAccess::ApplyComponentElement(*F.Driver, Probe, ELDDemoElement::None, Trick, Error));

    // B18 bound to S01 (active_elbow) with fire: each native direct effective hit of S01 adds one layer.
    const FLDCombatBodyRef Owner = F.AddBody(), Target = F.AddBody();
    if (!InstallComponents(*this, F, Owner, 1, {{TEXT("component_dirty_trick"), TEXT("active_elbow"), ELDDemoElement::Fire}}))
    { return false; }
    FAccess::Records(*F.Driver).FindChecked(Owner.Victim.EntityId).Build.Actives = {{TEXT("active_elbow"), ELDCombatEntry::Q},
        {TEXT("active_snark_bolt"), ELDCombatEntry::W}};
    const auto Native = [&](const TCHAR* Active, bool bDirect)
    {
        FLDCombatHit Hit = FFixture::Hit(Owner, Target, 1000);
        Hit.Source = SourceOf(ELDCombatSourceKind::Learned, Active);
        Hit.Token = 1;
        Hit.bDirectAbilityDamage = bDirect;
        return Hit;
    };
    FLDBodyCombatState& Victim = F.Body(Target);
    TestTrue(TEXT("A native hit of the bound active lands"), F.Apply(Native(TEXT("active_elbow"), true), Error));
    TestTrue(TEXT("and adds one fire layer credited to B18"), Victim.Burn.Layers.Num() == 1 && Victim.Burn.Layers[0].Source == Trick);
    TestTrue(TEXT("Another active's hit adds none"), F.Apply(Native(TEXT("active_snark_bolt"), true), Error) && Victim.Burn.Layers.Num() == 1);
    TestTrue(TEXT("A derived hit of the bound active adds none"), F.Apply(Native(TEXT("active_elbow"), false), Error)
        && Victim.Burn.Layers.Num() == 1);
    FLDCombatHit Zero = Native(TEXT("active_elbow"), true);
    Zero.BaseP2 = 0;
    TestTrue(TEXT("An ineffective hit adds none"), F.Apply(Zero, Error) && Victim.Burn.Layers.Num() == 1);
    return true;
}
#endif
