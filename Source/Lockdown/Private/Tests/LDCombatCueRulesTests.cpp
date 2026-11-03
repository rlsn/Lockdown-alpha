#include "Combat/LDCombatCues.h"
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
    TestTrue(TEXT("A winding-up basic shows Attack from its start until it releases"),
        Is(Select(Winding,999),None,0) && Is(Select(Winding,1000),Attack,1000) && Is(Select(Winding,9000),Attack,1000));

    FLDCombatCueFacts Released;
    Released.bReleasedBasic=true;
    Released.ReleasedStartedAtMs=1000;
    Released.ReleasedAtMs=1250;
    TestTrue(TEXT("A released basic stays Attack through its release plus the hold"),
        Is(Select(Released,1449),Attack,1000) && Is(Select(Released,1450),None,0));

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
    TestTrue(TEXT("A landed hit shows Hit for its hold"),
        Is(Select(Struck,4000),Hit,4000) && Is(Select(Struck,4299),Hit,4000) && Is(Select(Struck,4300),None,0));

    FLDCombatCueFacts Busy=Struck;
    Busy.bActing=Busy.bActingBasic=true;
    Busy.ActingStartedAtMs=4100;
    TestTrue(TEXT("Attack outranks Hit"), Is(Select(Busy,4150),Attack,4100));
    Busy.ReleasedStartedAtMs=Busy.ReleasedAtMs=4120;
    TestTrue(TEXT("Cast outranks Attack and Hit"), Is(Select(Busy,4150),Cast,4120));
    FLDCombatCueFacts Again=Released;
    Again.bActing=Again.bActingBasic=true;
    Again.ActingStartedAtMs=1300;
    TestTrue(TEXT("Within one kind the latest start wins"), Is(Select(Again,1400),Attack,1300));

    TestTrue(TEXT("Facts at the end of the clock cannot overflow a window"),
        [&]() { FLDCombatCueFacts Edge; Edge.ReleasedAtMs=Edge.ReleasedStartedAtMs=Edge.LastHitAtMs=MAX_int64-1;
            return Is(Select(Edge,MAX_int64-1),None,0); }());

    const uint8 Root=uint8(ELDDemoStatusId::Root), Shield=uint8(ELDDemoStatusId::Shield);
    const uint8 Returning=uint8(ELDDemoStatusId::CampReturning), Protected=uint8(ELDDemoStatusId::RespawnProtection);
    TestEqual(TEXT("No cue and no status is the plain pose"), ALDCharacter::PresentationCue(None,{}), uint8(0));
    TestEqual(TEXT("Cast pose with root and shield markers"),
        ALDCharacter::PresentationCue(Cast,{{Root,1,0},{Shield,1,5000}}), uint8(2|16|64));
    TestEqual(TEXT("Attack pose"), ALDCharacter::PresentationCue(Attack,{}), uint8(1));
    TestEqual(TEXT("Hit pose with protection and returning markers"),
        ALDCharacter::PresentationCue(Hit,{{Protected,1,9000},{Returning,1,0}}), uint8(4|128|32));
    return true;
}

#endif
