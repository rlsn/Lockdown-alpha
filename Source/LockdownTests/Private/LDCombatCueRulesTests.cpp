#include "Combat/LDCombatCues.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Development/LDDemoContentIds.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatCancelledRecoveryCueTest, "Lockdown.Rules.DemoCombatCues.CancelledRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatCancelledRecoveryCueTest::RunTest(const FString& Parameters)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("An isolated combat world"),World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Director=World->SpawnActor<ALDCombatDirector>();
    auto* Actor=World->SpawnActor<ALDCharacter>();
    if (!TestNotNull(TEXT("Combat director"),Director) || !TestNotNull(TEXT("Attacker"),Actor)) { return false; }

    auto& Body=Director->Bodies.Add(Actor);
    Body.Victim={ELDVictimKind::Contender,FGuid::NewGuid(),1};
    Body.Cues.bReleasedBasic=true;
    Body.Cues.ReleasedStartedAtMs=1000;
    Body.Cues.ReleasedAtMs=1200;
    Body.Cues.ReleasedComboStep=1;
    Body.ReleasedSource.Kind=ELDCombatSourceKind::Basic;

    constexpr int64 NowMs=1225;
    TestEqual(TEXT("The landed basic is showing its recovery cue"),
        LDCombatCues::Select(Director->CueFactsOf(Body),NowMs).Cue,ELDObservedActionCue::Attack);
    Director->CancelNavAttack(Body.Victim.EntityId,FLDMoveIdentity());
    TestEqual(TEXT("Ending the attack order clears the recovery cue"),
        LDCombatCues::Select(Director->CueFactsOf(Body),NowMs).Cue,ELDObservedActionCue::None);
    uint8 ObservedAbility=255;
    TestTrue(TEXT("The observer still accepts the attacker after the order ends"),
        Director->ObservedCastSource(*Actor,NowMs,ObservedAbility));
    TestEqual(TEXT("A cancelled recovery carries no visible ability"),ObservedAbility,uint8(0));

    Body.Cues.ReleasedStartedAtMs=1100;
    Body.Cues.ReleasedAtMs=1200;
    Body.ReleasedSource.Kind=ELDCombatSourceKind::Innate;
    Director->CancelNavAttack(Body.Victim.EntityId,FLDMoveIdentity());
    TestEqual(TEXT("The same order ending leaves a genuine cast cue intact"),
        LDCombatCues::Select(Director->CueFactsOf(Body),NowMs).Cue,ELDObservedActionCue::Cast);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatCueSelectionTest, "Lockdown.Rules.DemoCombatCues.Selection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatCueSelectionTest::RunTest(const FString& Parameters)
{
    using LDCombatCues::Select;
    const auto Is=[](const LDCombatCues::FCue& Cue, ELDObservedActionCue Expected, int64 AtMs)
    { return Cue.Cue==Expected && Cue.AtMs==AtMs; };
    const ELDObservedActionCue None=ELDObservedActionCue::None, Attack=ELDObservedActionCue::Attack,
        Cast=ELDObservedActionCue::Cast;

    TestTrue(TEXT("A body without action or hit shows no cue"), Is(Select(FLDCombatCueFacts(),5000),None,0));

    FLDCombatCueFacts Winding;
    Winding.bActing=Winding.bActingBasic=true;
    Winding.ActingStartedAtMs=1000;
    Winding.ActingReleaseAtMs=1250;
    TestTrue(TEXT("A winding-up basic shows Attack from its start until it releases"),
        Is(Select(Winding,999),None,0) && Is(Select(Winding,1000),Attack,1000) && Is(Select(Winding,9000),Attack,1000));
    TestEqual(TEXT("A winding attack carries its authoritative release time"), Select(Winding,1100).ImpactAtMs, int64(1250));

    FLDCombatCueFacts Released;
    Released.bReleasedBasic=true;
    Released.ReleasedStartedAtMs=1000;
    Released.ReleasedAtMs=1250;
    TestTrue(TEXT("A released basic stays Attack through its release plus the hold"),
        Is(Select(Released,1449),Attack,1000) && Is(Select(Released,1450),None,0));
    TestEqual(TEXT("A released attack carries its actual release time"), Select(Released,1300).ImpactAtMs, int64(1250));

    FLDCombatCueFacts Cancelled=Released;
    TestTrue(TEXT("Movement cancels a released basic's recovery cue"), LDCombatCues::CancelReleasedBasic(Cancelled));
    TestTrue(TEXT("A cancelled basic recovery never reads as a Cast"),
        Is(Select(Cancelled,1300),None,0) && Cancelled.ReleasedAtMs==Released.ReleasedAtMs);
    TestFalse(TEXT("Cancelling twice changes nothing"), LDCombatCues::CancelReleasedBasic(Cancelled));

    FLDCombatCueFacts Instant;
    Instant.ReleasedStartedAtMs=Instant.ReleasedAtMs=2000;
    TestTrue(TEXT("An instant cast is a Cast for the whole hold, longer than the 150 ms sampling minimum"),
        Is(Select(Instant,2000),Cast,2000) && Is(Select(Instant,2199),Cast,2000) && Is(Select(Instant,2200),None,0));

    FLDCombatCueFacts Dash=Instant;
    Dash.DashUntilMs=2400;
    TestTrue(TEXT("A dash stays a Cast until it ends"), Is(Select(Dash,2399),Cast,2000) && Is(Select(Dash,2400),None,0));
    FLDCombatCueFacts BasicAfterDash=Dash;
    BasicAfterDash.bReleasedBasic=true;
    TestTrue(TEXT("A released basic is never extended by a dash"), Is(Select(BasicAfterDash,2300),None,0));

    FLDCombatCueFacts Struck;
    Struck.LastHitAtMs=4000;
    Struck.LastHitLook=uint8(ELDObservedHitLook::Fire);
    TestTrue(TEXT("A landed hit is never an action cue: the struck body keeps its own pose"),
        Is(Select(Struck,4000),None,0) && Is(Select(Struck,4100),None,0));

    FLDCombatCueFacts Busy=Struck;
    Busy.bActing=Busy.bActingBasic=true;
    Busy.ActingStartedAtMs=4100;
    TestTrue(TEXT("A swinging body that is hit keeps showing its swing"), Is(Select(Busy,4150),Attack,4100));
    Busy.ReleasedStartedAtMs=Busy.ReleasedAtMs=4120;
    TestTrue(TEXT("Cast outranks Attack"), Is(Select(Busy,4150),Cast,4120));
    FLDCombatCueFacts Again=Released;
    Again.bActing=Again.bActingBasic=true;
    Again.ActingStartedAtMs=1300;
    TestTrue(TEXT("Within one kind the latest start wins"), Is(Select(Again,1400),Attack,1300));

    TestTrue(TEXT("Facts at the end of the clock cannot overflow a window"),
        [&]() { FLDCombatCueFacts Edge; Edge.ReleasedAtMs=Edge.ReleasedStartedAtMs=Edge.LastHitAtMs=MAX_int64-1;
            return Is(Select(Edge,MAX_int64-1),None,0); }());

    FLDOwnerState Owner;
    Owner.Match=4; Owner.Life=2;
    FLDCombatOwnerView View;
    View.Generation=4; View.Life=2; View.bHasBody=true;
    TestTrue(TEXT("A current owner view with a clock can present hits"),ALDCharacter::PresentationOwnerView(View,Owner,true));
    TestFalse(TEXT("A missing clock cannot present hits"),ALDCharacter::PresentationOwnerView(View,Owner,false));
    View.bHasBody=false;
    TestFalse(TEXT("A bodyless view cannot present hits"),ALDCharacter::PresentationOwnerView(View,Owner,true));
    View.bHasBody=true; View.Generation=3;
    TestFalse(TEXT("An old match cannot present hits"),ALDCharacter::PresentationOwnerView(View,Owner,true));
    View.Generation=4; View.Life=1;
    TestFalse(TEXT("An old life cannot present hits"),ALDCharacter::PresentationOwnerView(View,Owner,true));

    const uint8 Root=uint8(ELDDemoStatusId::Root), Shield=uint8(ELDDemoStatusId::Shield);
    const uint8 Returning=uint8(ELDDemoStatusId::CampReturning), Protected=uint8(ELDDemoStatusId::RespawnProtection);
    TestEqual(TEXT("No cue and no status is the plain pose"), ALDCharacter::PresentationCue(None,{}), uint8(0));
    TestEqual(TEXT("Cast pose with root and shield markers"),
        ALDCharacter::PresentationCue(Cast,{{Root,1,0},{Shield,1,5000}}), uint8(2|16|64));
    TestEqual(TEXT("Attack pose"), ALDCharacter::PresentationCue(Attack,{}), uint8(1));
    TestEqual(TEXT("Attack pose with protection and returning markers"),
        ALDCharacter::PresentationCue(Attack,{{Protected,1,9000},{Returning,1,0}}), uint8(1|128|32));

    FLDCombatHit Direct;
    TestTrue(TEXT("A direct hit with damage confirms to its attacker"), LDCombatCues::ConfirmsDealt(Direct,100));
    TestFalse(TEXT("A hit without damage confirms nothing"), LDCombatCues::ConfirmsDealt(Direct,0));
    const auto Indirect=[](bool FLDCombatHit::*Flag){FLDCombatHit Hit;Hit.*Flag=true;return LDCombatCues::ConfirmsDealt(Hit,100);};
    TestFalse(TEXT("Periodic, burn, settle and reflect damage never confirm a hit"),
        Indirect(&FLDCombatHit::bPeriodic) || Indirect(&FLDCombatHit::bBurnTick) || Indirect(&FLDCombatHit::bStatusSettle)
        || Indirect(&FLDCombatHit::bReflect));

    FLDCombatHit Elemental;
    Elemental.DamageType=ELDDemoDamageType::Magical;
    TestEqual(TEXT("An unmarked magical hit has a magic look"), LDCombatCues::ClassifyHit(Elemental), ELDObservedHitLook::Magic);
    Elemental.BurnStacks=1;
    TestEqual(TEXT("Burn payload marks a fire hit"), LDCombatCues::ClassifyHit(Elemental), ELDObservedHitLook::Fire);
    Elemental.BurnStacks=0;
    Elemental.NativeElements=LDCombatElements::Bit(ELDCombatElement::Water);
    TestEqual(TEXT("Wet payload marks a water hit"), LDCombatCues::ClassifyHit(Elemental), ELDObservedHitLook::Water);
    Elemental.NativeElements|=LDCombatElements::Bit(ELDCombatElement::Shock);
    TestEqual(TEXT("Shock payload remains identifiable when paired with wet"), LDCombatCues::ClassifyHit(Elemental), ELDObservedHitLook::Electric);
    Elemental.NativeElements=0;
    Elemental.DamageType=ELDDemoDamageType::Physical;
    TestEqual(TEXT("An unmarked physical hit remains generic"), LDCombatCues::ClassifyHit(Elemental), ELDObservedHitLook::None);

    FLDDamageEvaluation Damage;
    Damage.DamageP2=100;
    Damage.HPLostP2=100;
    TestEqual(TEXT("A real critical hit is distinguished"), LDCombatCues::ClassifyImpact(true,0,Damage),
        uint8(LDObservation::Critical));
    TestEqual(TEXT("A normal hit has no impact flags"), LDCombatCues::ClassifyImpact(false,0,Damage), uint8(0));
    Damage.ShieldAbsorbedP2=100;
    Damage.HPLostP2=0;
    Damage.RemainingShieldP2=50;
    TestEqual(TEXT("Shield absorption remains distinct from a break"),
        LDCombatCues::ClassifyImpact(false,150,Damage), uint8(LDObservation::ShieldAbsorbed));
    Damage.RemainingShieldP2=0;
    TestEqual(TEXT("A critical breaking the shield carries both results"),
        LDCombatCues::ClassifyImpact(true,100,Damage),
        uint8(LDObservation::Critical|LDObservation::ShieldAbsorbed|LDObservation::ShieldBroken));
    Damage=FLDDamageEvaluation();
    TestEqual(TEXT("A protected critical roll without damage does not display as a critical hit"),
        LDCombatCues::ClassifyImpact(true,0,Damage), uint8(0));
    return true;
}

#endif
