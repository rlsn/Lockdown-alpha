#include "Core/LDLog.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentIds.h"
#include "Framework/LDCaptureTypes.h"
#include "Misc/AutomationTest.h"
#include "UI/LDHUDModel.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPlaytestLogFormatTest,"Lockdown.Rules.PlaytestLog.Format",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDPlaytestLogFormatTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("A plain token stays bare"),LDLog::Quote(TEXT("RoomFull")),FString(TEXT("RoomFull")));
    TestEqual(TEXT("A name without spaces stays bare, CJK included"),LDLog::Quote(TEXT("\u738b\u5c0f\u660e")),FString(TEXT("\u738b\u5c0f\u660e")));
    TestEqual(TEXT("A space quotes the value"),LDLog::Quote(TEXT("AI 1")),FString(TEXT("\"AI 1\"")));
    TestEqual(TEXT("A full-width or no-break space quotes the value too"),LDLog::Quote(TEXT("\u738b\u3000\u5c0f")),FString(TEXT("\"\u738b\u3000\u5c0f\"")));
    TestEqual(TEXT("A no-break space quotes the value"),LDLog::Quote(TEXT("a\u00a0b")),FString(TEXT("\"a\u00a0b\"")));
    TestEqual(TEXT("Quotes, backslashes and '=' are escaped inside quotes"),LDLog::Quote(TEXT("a\"b\\c=d")),FString(TEXT("\"a\\\"b\\\\c=d\"")));
    TestEqual(TEXT("A line break can never split a log line"),LDLog::Quote(TEXT("x\ny")),FString(TEXT("\"x\\ny\"")));
    TestEqual(TEXT("Empty is an explicit empty value"),LDLog::Quote(FString()),FString(TEXT("\"\"")));
    TestEqual(TEXT("P2 hundredths keep two decimals"),LDLog::P2(12345),FString(TEXT("123.45")));
    TestEqual(TEXT("Negative P2 below one point keeps its sign"),LDLog::P2(-5),FString(TEXT("-0.05")));
    TestEqual(TEXT("Zero P2"),LDLog::P2(0),FString(TEXT("0.00")));
    TestEqual(TEXT("The most negative P2 does not overflow"),LDLog::P2(MIN_int64),FString(TEXT("-92233720368547758.08")));
    TestEqual(TEXT("Positions are whole centimetres"),LDLog::Cm(FVector(10.4,-2.6,0.5)),FString(TEXT("10,-3,1")));
    const FGuid Id(1,2,3,4);
    TestEqual(TEXT("Ids use the 32-digit form of the other LD lines"),LDLog::Id(Id),Id.ToString(EGuidFormats::Digits));
    TestEqual(TEXT("A missing id is '-'"),LDLog::Id(FGuid()),FString(TEXT("-")));
    TestEqual(TEXT("Status ids read as stable words"),LDLog::StatusName(uint8(ELDDemoStatusId::Stun)),FString(TEXT("stun")));
    TestEqual(TEXT("An unknown status keeps its number"),LDLog::StatusName(31),FString(TEXT("31")));
    FLDCombatSourceRef Source;
    TestEqual(TEXT("A sourceless effect"),LDLog::Source(Source),FString(TEXT("none")));
    Source.Kind=ELDCombatSourceKind::Learned;
    Source.DefinitionId=TEXT("active_arc_bolt");
    TestEqual(TEXT("A learned source names its ability"),LDLog::Source(Source),FString(TEXT("learned:active_arc_bolt")));
    Source.Kind=ELDCombatSourceKind::Gear;
    Source.DefinitionId=TEXT("item_slingshot");
    TestEqual(TEXT("A gear source names its item"),LDLog::Source(Source),FString(TEXT("gear:item_slingshot")));
    TestEqual(TEXT("Reflected enums read by name"),LDLog::EnumName(ELDKillCause::CampHigh),FString(TEXT("CampHigh")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPlaytestLogRateLimitTest,"Lockdown.Rules.PlaytestLog.RateLimit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDPlaytestLogRateLimitTest::RunTest(const FString& Parameters)
{
    LDLog::FRateLimit Limit(1.);
    uint32 Suppressed=99;
    TestTrue(TEXT("The first line always passes"),Limit.Allow(10.,Suppressed) && Suppressed==0);
    int32 Passed=0;
    for (int32 Frame=1; Frame<60; ++Frame) { Passed+=Limit.Allow(10.+Frame/60.,Suppressed) ? 1 : 0; }
    TestEqual(TEXT("A burst inside the interval writes nothing more"),Passed,0);
    TestTrue(TEXT("The next line after the interval carries the held-back count"),Limit.Allow(11.,Suppressed) && Suppressed==59);
    TestTrue(TEXT("The count starts again after it was reported"),Limit.Allow(12.5,Suppressed) && Suppressed==0);
    TestTrue(TEXT("A clock that went back never blocks forever"),Limit.Allow(1.,Suppressed));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPlaytestLogPeriodicGateTest,"Lockdown.Rules.PlaytestLog.PeriodicGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDPlaytestLogPeriodicGateTest::RunTest(const FString& Parameters)
{
    LDLog::FPeriodicGate Gate(1000);
    TestTrue(TEXT("The first sample of a match is due"),Gate.Due(250));
    TestFalse(TEXT("Not again inside the same second"),Gate.Due(999));
    TestTrue(TEXT("Due at the next whole second"),Gate.Due(1000));
    TestFalse(TEXT("Once per boundary"),Gate.Due(1001) || Gate.Due(1999));
    TestTrue(TEXT("A long frame that skipped boundaries is one sample, not a burst"),Gate.Due(5300));
    TestFalse(TEXT("The missed boundaries are not replayed"),Gate.Due(5301) || Gate.Due(5999));
    TestTrue(TEXT("Samples realign to whole seconds"),Gate.Due(6000));
    TestTrue(TEXT("A new match restarts its authority clock and the gate with it"),Gate.Due(0));
    TestFalse(TEXT("The restarted gate keeps the period"),Gate.Due(500));
    int32 Samples=0;
    LDLog::FPeriodicGate Summary(10000);
    for (int64 Ms=0; Ms<=60000; Ms+=16) { Samples+=Summary.Due(Ms) ? 1 : 0; }
    TestEqual(TEXT("Ten-second summaries over one minute of 60 Hz frames"),Samples,7);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPlaytestLogPerfWindowTest,"Lockdown.Rules.PlaytestLog.PerfWindow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDPlaytestLogPerfWindowTest::RunTest(const FString& Parameters)
{
    // The ten-second summary is the HUD's frame calculation over a longer window with a hitch threshold.
    LDHUD::FFrameDiagnostics Window(10.,.1);
    int32 Closed=0;
    for (int32 Frame=0; Frame<590; ++Frame) { Closed+=Window.AddFrame(1./60) ? 1 : 0; }
    TestEqual(TEXT("Under ten seconds no summary is written"),Closed,0);
    Closed+=Window.AddFrame(.1) ? 1 : 0;
    Closed+=Window.AddFrame(.25) ? 1 : 0;
    TestEqual(TEXT("The window closes once it reaches ten seconds"),Closed,1);
    TestTrue(TEXT("Frames, average, peak and hitches of the closed window"),Window.Frames==592 && Window.Hitches==2
        && FMath::IsNearlyEqual(Window.PeakMs,250.) && FMath::IsNearlyEqual(Window.FrameMs,1000.*(590./60+.35)/592,.001));
    int32 Next=0;
    bool bClosed=false;
    while (!bClosed && Next<700) { bClosed=Window.AddFrame(1./60); ++Next; }
    TestTrue(TEXT("The next window starts clean"),bClosed && Window.Hitches==0 && Window.Frames==uint32(Next)
        && FMath::IsNearlyEqual(Window.FPS,60.,.01));
    LDHUD::FFrameDiagnostics Hud;
    for (int32 Frame=0; Frame<29; ++Frame) { Hud.AddFrame(1./60); }
    TestTrue(TEXT("The HUD keeps its half-second window and counts no hitches"),Hud.AddFrame(.2) && Hud.Hitches==0 && Hud.Frames==30);
    return true;
}
#endif
