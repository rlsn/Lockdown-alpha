#include "Combat/LDCombatCues.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Development/LDDemoContentIds.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatCueSelectionTest, "Lockdown.Rules.DemoCombatCues.Selection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatCueSelectionTest::RunTest(const FString& Parameters)
{
    using LDCombatCues::Select;
    const auto Is=[](const LDCombatCues::FCue& Cue, ELDObservedActionCue Expected, int64 AtMs)
    { return Cue.Cue==Expected && Cue.AtMs==AtMs; };
    const ELDObservedActionCue None=ELDObservedActionCue::None, Attack=ELDObservedActionCue::Attack,
        Cast=ELDObservedActionCue::Cast, Hit=ELDObservedActionCue::Hit;

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
    TestTrue(TEXT("A landed hit shows Hit for its hold"),
        Is(Select(Struck,4000),Hit,4000) && Is(Select(Struck,4299),Hit,4000) && Is(Select(Struck,4300),None,0)
        && Select(Struck,4100).HitLook==ELDObservedHitLook::Fire);

    FLDCombatCueFacts Busy=Struck;
    Busy.bActing=Busy.bActingBasic=true;
    Busy.ActingStartedAtMs=4100;
    TestTrue(TEXT("Hit outranks Attack, so a swinging body still shows the hit it takes"),
        Is(Select(Busy,4150),Hit,4000) && Is(Select(Busy,4300),Attack,4100));
    Busy.ReleasedStartedAtMs=Busy.ReleasedAtMs=4120;
    TestTrue(TEXT("Cast outranks Hit and Attack"), Is(Select(Busy,4150),Cast,4120));
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
    TestEqual(TEXT("Hit pose with protection and returning markers"),
        ALDCharacter::PresentationCue(Hit,{{Protected,1,9000},{Returning,1,0}}), uint8(4|128|32));

    FLDCombatHit Elemental;
    Elemental.DamageType=ELDDemoDamageType::Magical;
    TestEqual(TEXT("An unmarked magical hit has a magic look"), LDCombatCues::ClassifyHit(Elemental), ELDObservedHitLook::Magic);
    Elemental.BurnStacks=1;
    TestEqual(TEXT("Burn payload marks a fire hit"), LDCombatCues::ClassifyHit(Elemental), ELDObservedHitLook::Fire);
    Elemental.BurnStacks=0;
    Elemental.bApplyWet=true;
    TestEqual(TEXT("Wet payload marks a water hit"), LDCombatCues::ClassifyHit(Elemental), ELDObservedHitLook::Water);
    Elemental.bApplyElectric=true;
    TestEqual(TEXT("Electric payload remains identifiable when paired with wet"), LDCombatCues::ClassifyHit(Elemental), ELDObservedHitLook::Electric);
    Elemental.bApplyWet=false;
    Elemental.bApplyElectric=false;
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
