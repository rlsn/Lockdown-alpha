#include "Audio/LDAudioCatalog.h"
#include "Audio/LDAudioCueTracker.h"
#include "Audio/LDAudioFeel.h"
#include "Character/LDHitFeel.h"
#include "Audio/LDAudioTypes.h"
#include "Audio/LDMusicSelection.h"
#include "Development/LDDemoContentIds.h"
#include "Framework/LDCaptureTypes.h"
#include "Misc/AutomationTest.h"
#include "Vision/LDViewTypes.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundWave.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "LDTestApprovedFixture.h"
namespace
{
TArray<ELDAudioCue> AudioSample(FLDAudioCueTracker& Tracker,const FLDAudioFrame& Frame)
{
    TArray<ELDAudioCue> Out;Tracker.Observe(Frame,Out);return Out;
}
bool AudioIs(const TArray<ELDAudioCue>& Cues,std::initializer_list<ELDAudioCue> Expected)
{
    return Cues==TArray<ELDAudioCue>(Expected);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDAudioCuePublicTest,"Lockdown.Rules.AudioCuePublic",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDAudioCuePublicTest::RunTest(const FString& Parameters)
{
    FLDAudioCueTracker Tracker;
    const FGuid Self=FGuid::NewGuid(),Other=FGuid::NewGuid();
    FLDPublicMatchView Public;Public.Generation=7;Public.Phase=ELDMatchPhase::Development;Public.CircleState=ELDCircleState::NotOpen;Public.CaptureEpoch=1;
    FLDKillFeedView Feed;Feed.Generation=7;
    FLDResultView Result;
    FLDAudioFrame Frame;Frame.Public=&Public;Frame.KillFeed=&Feed;Frame.Result=&Result;Frame.Self=Self;Frame.bClock=true;Frame.NowMs=100000;Frame.LocalSeconds=10;
    TestTrue(TEXT("The first sample of a match only primes"),AudioSample(Tracker,Frame).IsEmpty());

    Public.Phase=ELDMatchPhase::CaptureOpen;Public.CircleState=ELDCircleState::Empty;Public.CaptureEpoch=2;Frame.LocalSeconds=11;
    TestTrue(TEXT("Opening the circle sounds once"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::CaptureOpened}));
    TestTrue(TEXT("The same public state never repeats a cue"),AudioSample(Tracker,Frame).IsEmpty());
    Public.CircleState=ELDCircleState::Solo;Public.Occupier=Other;Public.CaptureEpoch=3;Frame.LocalSeconds=12.5;
    TestTrue(TEXT("A new occupier sounds"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::CaptureStarted}));
    Public.CircleState=ELDCircleState::Contested;Public.Occupier.Invalidate();Public.CaptureEpoch=4;Frame.LocalSeconds=14;
    TestTrue(TEXT("A contest sounds"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::CaptureContested}));
    Public.CircleState=ELDCircleState::Solo;Public.Occupier=Self;Public.CaptureEpoch=5;Frame.LocalSeconds=14.4;
    TestTrue(TEXT("A flap inside the repeat limit stays silent"),AudioSample(Tracker,Frame).IsEmpty());
    Frame.LocalSeconds=20;
    TestTrue(TEXT("A limited epoch is not replayed later"),AudioSample(Tracker,Frame).IsEmpty());
    Public.CircleState=ELDCircleState::Empty;Public.Occupier.Invalidate();Public.CaptureEpoch=6;Frame.LocalSeconds=22;
    TestTrue(TEXT("An emptied circle has no cue"),AudioSample(Tracker,Frame).IsEmpty());
    Public.CircleState=ELDCircleState::Guarded;Public.GraffitiOwner=Other;Public.CaptureEpoch=7;Frame.LocalSeconds=22.2;
    TestTrue(TEXT("A freshly sprayed graffiti sounds the flagpole fanfare even inside the repeat limit"),
        AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::CaptureOpened}));
    Public.CircleState=ELDCircleState::Empty;Public.CaptureEpoch=8;Frame.LocalSeconds=24;
    TestTrue(TEXT("The same graffiti does not sound again"),AudioSample(Tracker,Frame).IsEmpty());
    Public.GraffitiOwner.Invalidate();

    FLDKillNotice Notice;Notice.Serial=1;Notice.Victim=Other;Notice.Killer=Self;Notice.Cause=ELDKillCause::Contender;Notice.OccurredAtMs=Frame.NowMs-500;
    Feed.Notices={Notice};
    TestTrue(TEXT("A fresh public kill notice sounds"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::KillNotice}));
    TestTrue(TEXT("A notice sounds once by serial"),AudioSample(Tracker,Frame).IsEmpty());
    FLDKillNotice Own=Notice;Own.Serial=2;Own.Victim=Self;Own.Killer=Other;Feed.Notices.Add(Own);
    TestTrue(TEXT("The own death is left to the owner projection"),AudioSample(Tracker,Frame).IsEmpty());
    FLDKillNotice Stale=Notice;Stale.Serial=3;Stale.OccurredAtMs=Frame.NowMs-FLDAudioCueTracker::KillNoticeFreshMs-1;Feed.Notices.Add(Stale);
    TestTrue(TEXT("A notice older than its banner stays silent"),AudioSample(Tracker,Frame).IsEmpty());
    FLDKillNotice Camp=Notice;Camp.Serial=4;Camp.Killer.Invalidate();Camp.Cause=ELDKillCause::CampHigh;
    FLDKillNotice Second=Notice;Second.Serial=5;Feed.Notices.Add(Camp);Feed.Notices.Add(Second);
    TestTrue(TEXT("Several fresh notices in one sample are one cue"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::KillNotice}));
    FLDKillFeedView OldFeed=Feed;OldFeed.Generation=6;FLDKillNotice Late=Notice;Late.Serial=9;OldFeed.Notices={Late};Frame.KillFeed=&OldFeed;
    TestTrue(TEXT("A feed of another match never sounds"),AudioSample(Tracker,Frame).IsEmpty());
    Frame.KillFeed=&Feed;

    Public.Phase=ELDMatchPhase::Results;Public.ResultId=FGuid::NewGuid();Public.CaptureEpoch=9;
    Result.Generation=7;Result.ResultId=Public.ResultId;Result.ResultSerial=1;Result.Reason=ELDMatchEndReason::GuardSucceeded;Result.Winner=Self;Result.EndedAtMs=Frame.NowMs;
    TestTrue(TEXT("The own capture win sounds victory once"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::MatchVictory}));
    TestTrue(TEXT("The frozen result does not repeat"),AudioSample(Tracker,Frame).IsEmpty());

    Public.Generation=8;Public.Phase=ELDMatchPhase::Development;Public.ResultId.Invalidate();Public.CircleState=ELDCircleState::NotOpen;Public.CaptureEpoch=1;
    TestTrue(TEXT("A new match primes again"),AudioSample(Tracker,Frame).IsEmpty());
    Public.Phase=ELDMatchPhase::Results;Public.ResultId=FGuid::NewGuid();Result.Generation=8;Result.ResultId=Public.ResultId;Result.Winner=Other;
    Frame.Self.Invalidate();
    TestTrue(TEXT("Victory or defeat waits for the own identity"),AudioSample(Tracker,Frame).IsEmpty());
    Frame.Self=Self;
    TestTrue(TEXT("Another winner sounds defeat"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::MatchDefeat}));
    Public.Generation=9;Public.Phase=ELDMatchPhase::Development;Public.ResultId.Invalidate();AudioSample(Tracker,Frame);
    Public.Phase=ELDMatchPhase::Results;Public.ResultId=FGuid::NewGuid();Result.Generation=9;Result.ResultId=Public.ResultId;
    Result.Reason=ELDMatchEndReason::HostEnded;Result.Winner.Invalidate();
    TestTrue(TEXT("A Host end without winner has its own cue"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::MatchEndedNoWinner}));
    Public.Generation=10;Public.Phase=ELDMatchPhase::Development;Public.ResultId.Invalidate();AudioSample(Tracker,Frame);
    Public.Phase=ELDMatchPhase::Results;Public.ResultId=FGuid::NewGuid();Result.Generation=10;Result.ResultId=Public.ResultId;
    Result.EndedAtMs=Frame.NowMs-FLDAudioCueTracker::ResultFreshMs-1;
    TestTrue(TEXT("A result first seen long after it froze stays silent"),AudioSample(Tracker,Frame).IsEmpty());
    FLDResultView Mismatch=Result;Mismatch.ResultId=FGuid::NewGuid();Frame.Result=&Mismatch;
    TestTrue(TEXT("A result that is not the public match result never sounds"),AudioSample(Tracker,Frame).IsEmpty());
    Frame.KillFeed=nullptr;Frame.Result=nullptr;
    Public.Generation=11;Public.Phase=ELDMatchPhase::CaptureOpen;Public.CircleState=ELDCircleState::Empty;
    Public.Showdown.bStarted=true;Public.Showdown.ClosureAtMs=120000;
    Public.Showdown.RespawnsCloseAtMs=150000;Public.Showdown.EndsAtMs=240000;
    Frame.NowMs=119999;
    TestTrue(TEXT("A first final sample never replays old warnings"),AudioSample(Tracker,Frame).IsEmpty());
    Frame.NowMs=120000;
    TestTrue(TEXT("Street closure warns at its actual authority boundary"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::CaptureContested}));
    TestTrue(TEXT("A closure warning sounds only once"),AudioSample(Tracker,Frame).IsEmpty());
    Frame.NowMs=150000;
    TestTrue(TEXT("The new-respawn cutoff warns once"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::CaptureContested}));
    Frame.NowMs=230000;
    TestTrue(TEXT("The final ten seconds warn independently of replication changes"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::CaptureContested}));
    Public.Phase=ELDMatchPhase::Results;Frame.NowMs=240000;
    TestTrue(TEXT("An ended match never replays objective warnings"),AudioSample(Tracker,Frame).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDAudioCueOwnTest,"Lockdown.Rules.AudioCueOwn",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDAudioCueOwnTest::RunTest(const FString& Parameters)
{
    FLDAudioCueTracker Tracker;
    FLDCombatOwnerView Combat;Combat.Generation=3;Combat.Life=1;Combat.bHasBody=true;Combat.Revision=1;Combat.HPP2=50000;Combat.MaxHPP2=50000;
    Combat.Cues.bReleasedBasic=true;Combat.Cues.ReleasedStartedAtMs=200;Combat.Cues.ReleasedAtMs=500;Combat.AsOfMs=500;
    FLDProgressionView Growth;Growth.bValid=true;Growth.Generation=3;Growth.Life=1;Growth.Level=1;
    FLDAudioFrame Frame;Frame.Self=FGuid::NewGuid();Frame.OwnMatch=3;Frame.OwnLife=1;Frame.OwnLifePhase=ELDLifePhase::Alive;
    Frame.OwnCombat=&Combat;Frame.OwnProgression=&Growth;Frame.LocalSeconds=1;
    TestTrue(TEXT("The first own sample only primes, including a basic attack released before it"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Revision=2;Combat.HPP2=45000;Frame.LocalSeconds=2;
    TestTrue(TEXT("Lost own points sound a hit"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::SelfHit}));
    TestTrue(TEXT("The same revision never repeats"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Revision=3;Combat.HPP2=44000;Frame.LocalSeconds=2.1;
    TestTrue(TEXT("Hits inside the repeat limit stay silent"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Revision=4;Combat.MaxHPP2=40000;Combat.HPP2=40000;Frame.LocalSeconds=3;
    TestTrue(TEXT("A clamp from a smaller maximum is not a hit"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Revision=3;Combat.HPP2=1000;Frame.LocalSeconds=4;
    TestTrue(TEXT("An older combat revision is ignored"),AudioSample(Tracker,Frame).IsEmpty());

    // Shield hit sounds come from the resolved own hit, not from a decrease in shield points.
    FLDCombatStatusView Shield;Shield.StatusId=uint8(ELDDemoStatusId::Shield);Shield.ExpiresAtMs=9000;Shield.AmountP2=3000;
    Combat.Revision=5;Combat.HPP2=40000;Combat.ShieldP2=3000;Combat.Statuses={Shield};Combat.AsOfMs=5000;Frame.LocalSeconds=4.5;
    TestTrue(TEXT("Gaining a shield is silent"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Revision=6;Combat.ShieldP2=1000;Combat.Statuses[0].AmountP2=1000;Combat.AsOfMs=5500;Combat.Cues.LastHitAtMs=5500;
    Combat.Cues.LastHitFlags=LDObservation::ShieldAbsorbed;Frame.LocalSeconds=5;
    TestTrue(TEXT("A hit the shield absorbs whole sounds the shield hit"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::SelfShieldHit}));
    Combat.Revision=7;Combat.ShieldP2=500;Combat.Statuses[0].AmountP2=500;Combat.AsOfMs=5550;Combat.Cues.LastHitAtMs=5550;Frame.LocalSeconds=5.1;
    TestTrue(TEXT("Shield hits share the hit repeat limit"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Revision=8;Combat.ShieldP2=0;Combat.Statuses.Reset();Combat.HPP2=36000;Combat.AsOfMs=6000;
    Combat.Cues.LastHitAtMs=6000;Combat.Cues.LastHitFlags=LDObservation::ShieldAbsorbed|LDObservation::ShieldBroken;Frame.LocalSeconds=5.15;
    TestTrue(TEXT("A shield break sounds distinctly even immediately after a shield hit"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::SelfShieldBreak}));
    ++Combat.Revision;Frame.LocalSeconds=6.5;
    TestTrue(TEXT("A republished shield hit does not sound again"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Revision=10;Combat.ShieldP2=2000;Shield.AmountP2=2000;Shield.ExpiresAtMs=7000;Combat.Statuses={Shield};Combat.AsOfMs=6500;Frame.LocalSeconds=7;
    AudioSample(Tracker,Frame);
    Combat.Revision=11;Combat.ShieldP2=0;Combat.Statuses.Reset();Combat.AsOfMs=7000;Frame.LocalSeconds=8;
    TestTrue(TEXT("A shield that runs out at its end is silent"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Revision=12;Combat.ShieldP2=2000;Shield.ExpiresAtMs=9000;Combat.Statuses={Shield};Combat.AsOfMs=7500;Frame.LocalSeconds=9;
    AudioSample(Tracker,Frame);
    Combat.Revision=13;Combat.ShieldP2=0;Combat.Statuses.Reset();Combat.AsOfMs=7600;Frame.LocalSeconds=9.5;
    TestTrue(TEXT("Shield removal without a new hit stays silent"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Revision=14;Combat.ShieldP2=2000;Combat.Statuses={Shield};Combat.AsOfMs=7700;AudioSample(Tracker,Frame);
    Combat.Revision=15;Combat.ShieldP2=0;Combat.Statuses.Reset();Combat.AsOfMs=7800;Combat.Cues.LastHitAtMs=7800;
    Combat.Cues.LastHitFlags=LDObservation::ShieldAbsorbed|LDObservation::ShieldBroken;Frame.LocalSeconds=10;
    TestTrue(TEXT("A shield broken before its end by a hit sounds the break"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::SelfShieldBreak}));

    {
        FLDAudioCueTracker Rapid;
        FLDCombatOwnerView Hit;Hit.Generation=5;Hit.Life=1;Hit.bHasBody=true;Hit.Revision=1;Hit.HPP2=Hit.MaxHPP2=50000;
        FLDAudioFrame Sample;Sample.OwnMatch=5;Sample.OwnLife=1;Sample.OwnLifePhase=ELDLifePhase::Alive;
        Sample.OwnCombat=&Hit;Sample.LocalSeconds=1;
        AudioSample(Rapid,Sample);
        Hit.Revision=2;Hit.AsOfMs=1100;Hit.Cues.LastHitAtMs=1100;
        Hit.Cues.LastHitFlags=LDObservation::ShieldAbsorbed|LDObservation::ShieldBroken;Sample.LocalSeconds=1.1;
        TestTrue(TEXT("The first break sounds"),AudioIs(AudioSample(Rapid,Sample),{ELDAudioCue::SelfShieldBreak}));
        Hit.Revision=3;Hit.AsOfMs=1150;Hit.Cues.LastHitAtMs=1150;Sample.LocalSeconds=1.15;
        TestTrue(TEXT("Repeated breaks within the break limit stay silent"),AudioSample(Rapid,Sample).IsEmpty());
        Hit.Revision=4;Hit.AsOfMs=1400;Hit.Cues.LastHitAtMs=1400;Sample.LocalSeconds=1.4;
        TestTrue(TEXT("A later break sounds again"),AudioIs(AudioSample(Rapid,Sample),{ELDAudioCue::SelfShieldBreak}));
    }

    // Own releases sound at the body's presented release (LDAudioFeel), never from the projection sample.
    Combat.Revision=16;Combat.Cues.ReleasedStartedAtMs=7700;Combat.Cues.ReleasedAtMs=8000;Combat.AsOfMs=8000;Frame.LocalSeconds=10.5;
    TestTrue(TEXT("A released own basic attack is left to the body presentation"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Revision=19;Combat.Cues.bReleasedBasic=false;Combat.Cues.ReleasedStartedAtMs=8700;Combat.Cues.ReleasedAtMs=9000;Combat.AsOfMs=9000;Frame.LocalSeconds=11.2;
    TestTrue(TEXT("A released own skill is left to the body presentation"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Revision=20;Combat.Cues.bReleasedBasic=true;Frame.LocalSeconds=11.5;

    Growth.Level=2;Growth.ActiveOpportunitiesEarned=1;
    TestTrue(TEXT("A level up sounds once"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::LevelUp}));
    TestTrue(TEXT("The same level stays silent"),AudioSample(Tracker,Frame).IsEmpty());
    Growth.ActiveOpportunitiesEarned=2;
    TestTrue(TEXT("A newly earned active opportunity sounds"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::LevelUp}));
    Growth.ActiveOpportunitiesConsumed=1;Growth.QueuedActiveOpportunities=1;
    TestTrue(TEXT("Picking an active and showing the queued offer stays silent"),AudioSample(Tracker,Frame).IsEmpty());

    Combat.Revision=21;Combat.HPP2=0;Combat.AsOfMs=9100;Combat.Cues.LastHitAtMs=9100;
    Combat.Cues.LastHitFlags=LDObservation::ShieldAbsorbed|LDObservation::ShieldBroken;Frame.LocalSeconds=12;
    TestTrue(TEXT("A lethal shield hit waits for the death cue even if the life phase is delayed"),AudioSample(Tracker,Frame).IsEmpty());
    Frame.OwnLifePhase=ELDLifePhase::Dead;
    TestTrue(TEXT("Death sounds once without a hit"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::SelfDeath}));
    TestTrue(TEXT("Waiting dead stays silent"),AudioSample(Tracker,Frame).IsEmpty());
    Frame.OwnLife=2;Frame.OwnLifePhase=ELDLifePhase::Alive;
    TestTrue(TEXT("The next life sounds a respawn; the old life's view cannot hit"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::SelfRespawn}));
    Combat.Life=2;Combat.Revision=1;Combat.HPP2=50000;Combat.MaxHPP2=50000;Combat.Cues.ReleasedStartedAtMs=9700;Combat.Cues.ReleasedAtMs=10000;Combat.AsOfMs=10000;
    TestTrue(TEXT("A new life's first combat view primes, including its release"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Life=1;Combat.Revision=20;Combat.Cues.ReleasedAtMs=11000;
    TestTrue(TEXT("A release in the old life's view stays silent"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Life=2;Combat.Revision=2;Combat.Cues.ReleasedStartedAtMs=10700;Combat.Cues.ReleasedAtMs=11000;Combat.AsOfMs=11000;
    TestTrue(TEXT("The new life's own basic attack is left to the body presentation"),AudioSample(Tracker,Frame).IsEmpty());
    Frame.OwnMatch=4;Frame.OwnLife=1;Combat.Generation=4;Combat.Life=1;Growth.Generation=4;Growth.Level=1;Growth.ActiveOpportunitiesEarned=0;
    TestTrue(TEXT("A new match never sounds a respawn or level"),AudioSample(Tracker,Frame).IsEmpty());

    TArray<ELDAudioCue> Out;
    FLDCombatActionReceipt Cast;Cast.Generation=4;Cast.Life=1;Cast.Origin=ELDCombatRequestOrigin::Direct;Cast.DirectSequence=10;Cast.Entry=ELDCombatEntry::Q;
    Cast.Phase=ELDCombatPhase::Accepted;Cast.Reject=ELDCombatReject::None;
    Tracker.ObserveCombatReceipt(Cast,4,1,Out);TestTrue(TEXT("An accepted windup is not a cast yet"),Out.IsEmpty());
    Cast.Phase=ELDCombatPhase::Finished;Tracker.ObserveCombatReceipt(Cast,4,1,Out);
    TestTrue(TEXT("The released own active is left to its presented release"),Out.IsEmpty());
    Out.Reset();Tracker.ObserveCombatReceipt(Cast,4,1,Out);TestTrue(TEXT("A repeated receipt stays silent"),Out.IsEmpty());
    FLDCombatActionReceipt Basic=Cast;Basic.DirectSequence=11;Basic.Entry=ELDCombatEntry::Basic;Tracker.ObserveCombatReceipt(Basic,4,1,Out);
    FLDCombatActionReceipt Refused=Cast;Refused.DirectSequence=12;Refused.Entry=ELDCombatEntry::W;Refused.Phase=ELDCombatPhase::Rejected;Refused.Reject=ELDCombatReject::Cooldown;
    Tracker.ObserveCombatReceipt(Refused,4,1,Out);
    FLDCombatActionReceipt Stopped=Cast;Stopped.DirectSequence=13;Stopped.Reject=ELDCombatReject::Cancelled;Tracker.ObserveCombatReceipt(Stopped,4,1,Out);
    FLDCombatActionReceipt OldLife=Cast;OldLife.DirectSequence=14;OldLife.Generation=3;Tracker.ObserveCombatReceipt(OldLife,4,1,Out);
    OldLife.Generation=4;OldLife.Life=2;Tracker.ObserveCombatReceipt(OldLife,4,1,Out);
    TestTrue(TEXT("Basic attacks, refusals, cancels and receipts of another match or life stay silent"),Out.IsEmpty());
    FLDCombatActionReceipt Dash=Cast;Dash.DirectSequence=15;Dash.Entry=ELDCombatEntry::Dash;
    Dash.Phase=ELDCombatPhase::Accepted;Tracker.ObserveCombatReceipt(Dash,4,1,Out);
    TestTrue(TEXT("A dash windup does not sound as a release"),Out.IsEmpty());
    Dash.Phase=ELDCombatPhase::Released;Tracker.ObserveCombatReceipt(Dash,4,1,Out);
    TestTrue(TEXT("The innate dash uses its own release cue"),AudioIs(Out,{ELDAudioCue::SelfDash}));
    Out.Reset();Dash.Phase=ELDCombatPhase::Finished;Tracker.ObserveCombatReceipt(Dash,4,1,Out);
    TestTrue(TEXT("The same dash's finished receipt does not replay the cue"),Out.IsEmpty());
    Dash.DirectSequence=16;Dash.Reject=ELDCombatReject::Cooldown;Dash.Phase=ELDCombatPhase::Rejected;
    Tracker.ObserveCombatReceipt(Dash,4,1,Out);
    Dash.DirectSequence=17;Dash.Reject=ELDCombatReject::None;Dash.Phase=ELDCombatPhase::Finished;Dash.Life=2;
    Tracker.ObserveCombatReceipt(Dash,4,1,Out);
    TestTrue(TEXT("A refused dash and a dash from another life stay silent"),Out.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDAudioFeelSyncTest,"Lockdown.Rules.AudioFeelSync",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDAudioFeelSyncTest::RunTest(const FString& Parameters)
{
    // A swing's whoosh pre-roll peaks on the release frame (at most 40 ms) and never starts before 80 % of the windup, so a
    // predicted swing held at 75 % of its windup stays silent until the authority confirms it.
    for(const float Windup:{.06f,.1f,.18f,.25f,.5f})
    {
        const float PreRoll=LDAudioFeel::SwingPreRollSeconds(Windup);
        TestTrue(FString::Printf(TEXT("Windup %.2f s: pre-roll within 40 ms"),Windup),PreRoll>=0.f&&PreRoll<=.04f+KINDA_SMALL_NUMBER);
        TestTrue(FString::Printf(TEXT("Windup %.2f s: the whoosh starts after the predicted hold"),Windup),
            Windup-PreRoll>LDHitFeel::SwingHoldFraction*Windup);
    }
    TestTrue(TEXT("Only the interface cues use the UI bus"),ULDAudioCatalog::BusOf(ELDAudioCue::UIClick)==ELDAudioBus::UI
        &&ULDAudioCatalog::BusOf(ELDAudioCue::UIFinalLock)==ELDAudioBus::UI&&ULDAudioCatalog::BusOf(ELDAudioCue::HitBasic)==ELDAudioBus::Sfx
        &&ULDAudioCatalog::BusOf(ELDAudioCue::MatchVictory)==ELDAudioBus::Sfx&&ULDAudioCatalog::BusOf(ELDAudioCue::KillNotice)==ELDAudioBus::Sfx);
    for(int32 Bus=0;Bus<LDAudio::BusCount;++Bus)
    {
        ELDAudioBus Parsed=ELDAudioBus::Count;
        TestTrue(TEXT("Every bus id round-trips"),LDAudio::ParseBusId(LDAudio::BusId(ELDAudioBus(Bus)),Parsed)&&Parsed==ELDAudioBus(Bus));
    }
    ELDAudioBus Parsed=ELDAudioBus::Count;
    TestFalse(TEXT("Bus ids are case-sensitive"),LDAudio::ParseBusId(TEXT("sfx"),Parsed));

    // The match start: only the first field segment after the opening rest, never replayed for a late joiner.
    {
        FLDPublicMatchView Public;Public.Generation=3;Public.Phase=ELDMatchPhase::Rest;Public.Timeline.RestIndex=0;
        FLDAudioFrame Frame;Frame.Public=&Public;
        FLDAudioCueTracker Tracker;
        TestTrue(TEXT("The opening rest primes silently"),AudioSample(Tracker,Frame).IsEmpty());
        Public.Phase=ELDMatchPhase::Development;Public.Timeline.FieldSegment=0;
        TestTrue(TEXT("The first field segment starts the match"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::MatchStart}));
        TestTrue(TEXT("The same segment never repeats it"),AudioSample(Tracker,Frame).IsEmpty());
        Public.Phase=ELDMatchPhase::Rest;Public.Timeline.RestIndex=1;AudioSample(Tracker,Frame);
        Public.Phase=ELDMatchPhase::Development;Public.Timeline.FieldSegment=1;
        TestTrue(TEXT("A later field segment is not a match start"),AudioSample(Tracker,Frame).IsEmpty());
        FLDAudioCueTracker Late;Public.Phase=ELDMatchPhase::Rest;Public.Timeline.RestIndex=2;
        TestTrue(TEXT("A late joiner primes in a mid-match rest"),AudioSample(Late,Frame).IsEmpty());
        Public.Phase=ELDMatchPhase::Development;Public.Timeline.FieldSegment=2;
        TestTrue(TEXT("A late joiner never hears a match start"),AudioSample(Late,Frame).IsEmpty());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDAudioCatalogTest,"Lockdown.Rules.AudioCatalog",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDAudioCatalogTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Existing cue numbering stays stable"),uint8(ELDAudioCue::SelfShieldHit),uint8(16));
    TestEqual(TEXT("The body sound cues are appended"),uint8(ELDAudioCue::SwingLight),uint8(40));
    TestEqual(TEXT("The level-up cue keeps its number past the retired shop trade cues"),uint8(ELDAudioCue::LevelUp),uint8(12));
    TestTrue(TEXT("Retired cue numbers are not playable"),!LDAudio::IsPlayableCue(ELDAudioCue(10))&&!LDAudio::IsPlayableCue(ELDAudioCue(11))
        &&!LDAudio::IsPlayableCue(ELDAudioCue(18))&&!LDAudio::IsPlayableCue(ELDAudioCue(19))
        &&!LDAudio::IsPlayableCue(ELDAudioCue(9))&&!LDAudio::IsPlayableCue(ELDAudioCue(17)));
    for(int32 Index=0;Index<LDAudio::CueCount;++Index)
    {
        const ELDAudioCue Cue=LDAudio::PlayableCue(Index);
        ELDAudioCue Parsed=ELDAudioCue::None;
        TestTrue(TEXT("Every cue id round-trips"),LDAudio::IsPlayableCue(Cue)&&LDAudio::ParseCueId(LDAudio::CueId(Cue),Parsed)&&Parsed==Cue);
    }
    TestTrue(TEXT("The playable cues end before Count"),LDAudio::PlayableCue(LDAudio::CueCount)==ELDAudioCue::None&&LDAudio::PlayableCue(-1)==ELDAudioCue::None);
    ELDAudioCue Parsed=ELDAudioCue::None;
    TestFalse(TEXT("Cue ids are case-sensitive and None is not a cue"),LDAudio::ParseCueId(TEXT("killnotice"),Parsed)||LDAudio::ParseCueId(TEXT("None"),Parsed));
    TestTrue(TEXT("Music track numbering is stable"),uint8(ELDMusicTrack::Menu)==1&&uint8(ELDMusicTrack::Farm)==2&&uint8(ELDMusicTrack::Escalation)==3
        &&LDAudio::MusicTrackCount==3);
    for(int32 Index=1;Index<int32(ELDMusicTrack::Count);++Index)
    {
        ELDMusicTrack Track=ELDMusicTrack::None;
        TestTrue(TEXT("Every music track id round-trips"),LDAudio::ParseMusicTrackId(LDAudio::MusicTrackId(ELDMusicTrack(Index)),Track)&&Track==ELDMusicTrack(Index));
    }
    ELDMusicTrack ParsedTrack=ELDMusicTrack::None;
    TestFalse(TEXT("Music ids are case-sensitive and None is not a track"),LDAudio::ParseMusicTrackId(TEXT("menu"),ParsedTrack)||LDAudio::ParseMusicTrackId(TEXT("None"),ParsedTrack));
    TestTrue(TEXT("Menu and Escalation loop, Farm plays once"),LDAudio::MusicTrackLoops(ELDMusicTrack::Menu)&&LDAudio::MusicTrackLoops(ELDMusicTrack::Escalation)
        &&!LDAudio::MusicTrackLoops(ELDMusicTrack::Farm)&&!LDAudio::MusicTrackLoops(ELDMusicTrack::None));
    auto* Master=NewObject<USoundClass>();auto* MusicClass=NewObject<USoundClass>();auto* Mix=NewObject<USoundMix>();auto* Catalog=NewObject<ULDAudioCatalog>();
    auto* SfxClass=NewObject<USoundClass>();auto* UIClass=NewObject<USoundClass>();
    auto Fill=[&]()
    {
        MusicClass->ParentClass=Master;SfxClass->ParentClass=Master;UIClass->ParentClass=Master;Master->ChildClasses={MusicClass,SfxClass,UIClass};
        Catalog->MasterClass=Master;Catalog->MusicClass=MusicClass;Catalog->SfxClass=SfxClass;Catalog->UIClass=UIClass;Catalog->UserMix=Mix;
        Catalog->Cues.Reset();Catalog->Music.Reset();Catalog->SkillReleases.Reset();Catalog->SkillHits.Reset();
        for(int32 Index=0;Index<LDAudio::CueCount;++Index)
        {
            const ELDAudioCue Cue=LDAudio::PlayableCue(Index);
            auto* Wave=NewObject<USoundWave>();Wave->SoundClassObject=ULDAudioCatalog::BusOf(Cue)==ELDAudioBus::UI?UIClass:SfxClass;Wave->Duration=.5f;
            FLDAudioCueEntry& Entry=Catalog->Cues.AddDefaulted_GetRef();Entry.Cue=Cue;Entry.Sound=Wave;
        }
        for(int32 Index=1;Index<int32(ELDMusicTrack::Count);++Index)
        {
            auto* Wave=NewObject<USoundWave>();Wave->SoundClassObject=MusicClass;Wave->Duration=180.f;Wave->bLooping=LDAudio::MusicTrackLoops(ELDMusicTrack(Index));
            FLDMusicTrackEntry& Entry=Catalog->Music.AddDefaulted_GetRef();Entry.Track=ELDMusicTrack(Index);Entry.Sound=Wave;
        }
    };
    ULDAudioCatalog::FResolved Out;FString Error;
    Fill();
    TestTrue(TEXT("A complete catalog resolves every cue"),Catalog->Resolve(Out,Error)&&Out.Cues.Num()==int32(ELDAudioCue::Count)
        &&Out.Cues[int32(ELDAudioCue::Count)-1]==TArray<USoundWave*>({Catalog->Cues.Last().Sound.Get()})&&Out.Cues[0].IsEmpty());
    TestTrue(TEXT("A complete catalog resolves every music track by track value"),Out.Music.Num()==int32(ELDMusicTrack::Count)&&!Out.Music[0]
        &&Out.Music[int32(ELDMusicTrack::Escalation)]==Catalog->Music.Last().Sound);
    TestTrue(TEXT("Each bus resolves to its class"),Out.Buses[0]==Master&&Out.Buses[1]==MusicClass&&Out.Buses[2]==SfxClass&&Out.Buses[3]==UIClass);
    {
        auto* Take=NewObject<USoundWave>();Take->SoundClassObject=SfxClass;Take->Duration=.3f;
        Catalog->Cues[0].Variants.Add(Take);
        FLDAudioKeyedEntry& Skill=Catalog->SkillReleases.AddDefaulted_GetRef();Skill.Key=uint8(ELDDemoAbilityId::ActiveZap);Skill.Sounds.Add(Take);
        TestTrue(TEXT("Takes and skill releases resolve"),Catalog->Resolve(Out,Error)&&Out.Cues[int32(Catalog->Cues[0].Cue)].Num()==2
            &&Out.Skills[uint8(ELDDemoAbilityId::ActiveZap)].Num()==1&&Out.Skills[uint8(ELDDemoAbilityId::ActiveElbow)].IsEmpty());
        FLDAudioKeyedEntry& Hit=Catalog->SkillHits.AddDefaulted_GetRef();Hit.Key=uint8(ELDDemoAbilityId::ActiveElbow);Hit.Sounds.Add(Take);
        TestTrue(TEXT("Skill hits resolve by ability"),Catalog->Resolve(Out,Error)&&Out.SkillHits[uint8(ELDDemoAbilityId::ActiveElbow)].Num()==1
            &&Out.SkillHits[uint8(ELDDemoAbilityId::ActiveZap)].IsEmpty());
        FLDAudioKeyedEntry Again=Catalog->SkillReleases[0];Catalog->SkillReleases.Add(Again);
        TestFalse(TEXT("A repeated skill release is an error"),Catalog->Resolve(Out,Error));
        Fill();auto* Wrong=NewObject<USoundWave>();Wrong->SoundClassObject=UIClass;Wrong->Duration=.3f;Catalog->Cues[0].Variants.Add(Wrong);
        TestFalse(TEXT("A take on another bus is an error"),Catalog->Resolve(Out,Error));
    }
    Catalog->Cues.Pop();TestFalse(TEXT("A missing cue is an error"),Catalog->Resolve(Out,Error));
    Fill();Catalog->Cues.Swap(0,1);TestFalse(TEXT("Entries out of cue order are an error"),Catalog->Resolve(Out,Error));
    Fill();Catalog->Cues[9].Cue=ELDAudioCue(10);TestFalse(TEXT("A retired cue number is an error"),Catalog->Resolve(Out,Error));
    Fill();Catalog->Cues[3].Sound=nullptr;TestFalse(TEXT("A cue without sound is an error"),Catalog->Resolve(Out,Error));
    Fill();Catalog->Cues[0].Sound->SoundClassObject=NewObject<USoundClass>();TestFalse(TEXT("A sound outside its bus class is an error"),Catalog->Resolve(Out,Error));
    Fill();Catalog->Cues[0].Sound->SoundClassObject=Master;TestFalse(TEXT("A cue routed straight to the master class is an error"),Catalog->Resolve(Out,Error));
    Fill();Catalog->SfxClass=UIClass;TestFalse(TEXT("Two buses sharing a class is an error"),Catalog->Resolve(Out,Error));
    Fill();SfxClass->ParentClass=MusicClass;TestFalse(TEXT("A bus outside the master class is an error"),Catalog->Resolve(Out,Error));
    Fill();Catalog->Cues[0].Sound->SoundClassObject=MusicClass;TestFalse(TEXT("A cue routed to the music class is an error"),Catalog->Resolve(Out,Error));
    Fill();Catalog->Cues[1].Sound->bLooping=true;TestFalse(TEXT("A looping cue is an error"),Catalog->Resolve(Out,Error));
    Fill();Catalog->Cues[2].Sound->Duration=0.f;TestFalse(TEXT("A cue without audio is an error"),Catalog->Resolve(Out,Error));
    Fill();Catalog->UserMix=nullptr;TestFalse(TEXT("A catalog without the user mix is an error"),Catalog->Resolve(Out,Error));
    Fill();Catalog->MusicClass=nullptr;TestFalse(TEXT("A catalog without the music class is an error"),Catalog->Resolve(Out,Error));
    Fill();MusicClass->ParentClass=nullptr;TestFalse(TEXT("A music class outside the master class is an error"),Catalog->Resolve(Out,Error));
    Fill();Master->ChildClasses.Reset();TestFalse(TEXT("A master class that does not list the music class is an error"),Catalog->Resolve(Out,Error));
    Fill();Catalog->Music.Pop();TestFalse(TEXT("A missing music track is an error"),Catalog->Resolve(Out,Error));
    Fill();Catalog->Music.Swap(0,1);TestFalse(TEXT("Music out of track order is an error"),Catalog->Resolve(Out,Error));
    Fill();Catalog->Music[1].Sound=nullptr;TestFalse(TEXT("A music track without sound is an error"),Catalog->Resolve(Out,Error));
    Fill();Catalog->Music[0].Sound->SoundClassObject=Master;TestFalse(TEXT("Music routed straight to the master class is an error"),Catalog->Resolve(Out,Error));
    Fill();Catalog->Music[0].Sound->bLooping=false;TestFalse(TEXT("A menu track that does not loop is an error"),Catalog->Resolve(Out,Error));
    Fill();Catalog->Music[1].Sound->bLooping=true;TestFalse(TEXT("A looping farm track is an error"),Catalog->Resolve(Out,Error));
    Fill();Catalog->Music[2].Sound->Duration=0.f;TestFalse(TEXT("A music track without audio is an error"),Catalog->Resolve(Out,Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDAudioMusicSelectionTest,"Lockdown.Rules.AudioMusicSelection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDAudioMusicSelectionTest::RunTest(const FString& Parameters)
{
    FLDPublicMatchView Public;Public.Generation=7;Public.StartedAtMs=50000;Public.OpenAtMs=280000;Public.Timeline.FieldStartedAtMs=100000;
    auto Select=[&](ELDMatchPhase Phase,bool bClock=true,int64 NowMs=100000)
    {
        Public.Phase=Phase;FLDMusicInput Input;Input.Public=&Public;Input.bClock=bClock;Input.NowMs=NowMs;
        return LDAudio::SelectMusic(Input);
    };
    auto Is=[](const FLDMusicRequest& Request,ELDMusicTrack Track,float StartSec=0.f)
    {return Request.bDecided&&Request.Track==Track&&FMath::IsNearlyEqual(Request.StartSec,StartSec,1e-3f);};
    FLDMusicInput Frontend;Frontend.bFrontend=true;
    TestTrue(TEXT("The main menu plays the menu loop"),Is(LDAudio::SelectMusic(Frontend),ELDMusicTrack::Menu));
    Frontend.Public=&Public;Public.Phase=ELDMatchPhase::CaptureOpen;
    TestTrue(TEXT("The frontend world ignores any public match view"),Is(LDAudio::SelectMusic(Frontend),ELDMusicTrack::Menu));
    TestFalse(TEXT("No room state yet keeps whatever plays"),LDAudio::SelectMusic(FLDMusicInput()).bDecided);
    FLDPublicMatchView Unsynced;FLDMusicInput Pending;Pending.Public=&Unsynced;
    TestFalse(TEXT("A generation-0 view keeps whatever plays (the menu carries on into the lobby)"),LDAudio::SelectMusic(Pending).bDecided);
    for(const ELDMatchPhase Phase:{ELDMatchPhase::Lobby,ELDMatchPhase::Starting,ELDMatchPhase::Rest,ELDMatchPhase::Resetting})
    {TestTrue(FString::Printf(TEXT("Phase %d shares the menu loop with the main menu"),int32(Phase)),Is(Select(Phase),ELDMusicTrack::Menu));}
    TestTrue(TEXT("A field segment starts the farm track at its start"),Is(Select(ELDMatchPhase::Development,true,100000),ELDMusicTrack::Farm,0.f));
    TestTrue(TEXT("A late start joins the farm track at the time since the segment started"),Is(Select(ELDMatchPhase::Development,true,172500),ELDMusicTrack::Farm,72.5f));
    TestTrue(TEXT("A clock behind the segment start starts the farm track at its beginning"),Is(Select(ELDMatchPhase::Development,true,99000),ELDMusicTrack::Farm,0.f));
    TestFalse(TEXT("Without the authority clock the farm offset is unknown, so nothing changes"),Select(ELDMatchPhase::Development,false).bDecided);
    Public.Timeline.FieldStartedAtMs=0;
    TestFalse(TEXT("Without a segment start the farm offset is unknown, so nothing changes"),Select(ELDMatchPhase::Development).bDecided);
    Public.Timeline.FieldStartedAtMs=100000;
    TestTrue(TEXT("The flagpole opening switches to the escalation loop from its start"),Is(Select(ELDMatchPhase::CaptureOpen,true,400000),ELDMusicTrack::Escalation));
    TestTrue(TEXT("The escalation loop does not need the clock"),Is(Select(ELDMatchPhase::CaptureOpen,false),ELDMusicTrack::Escalation));
    for(const ELDMatchPhase Phase:{ELDMatchPhase::Results,ELDMatchPhase::Closing,ELDMatchPhase::Faulted})
    {TestTrue(FString::Printf(TEXT("Phase %d stops the music so the result jingle stands alone"),int32(Phase)),Is(Select(Phase),ELDMusicTrack::None));}
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDAudioCatalogAssetTest,"Lockdown.Rules.AudioCatalogAsset",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDAudioCatalogAssetTest::RunTest(const FString& Parameters)
{
    // The shipped catalog: every cue resolves to an imported one-shot routed through the single master chain.
    const auto* Catalog=LoadObject<ULDAudioCatalog>(nullptr,ULDAudioCatalog::AssetPath);
    if(!TestNotNull(TEXT("The Demo audio catalog asset exists"),Catalog)){return false;}
    ULDAudioCatalog::FResolved Out;FString Error;
    const bool bResolved=Catalog->Resolve(Out,Error);
    TestTrue(FString(TEXT("The Demo audio catalog resolves: "))+Error,bResolved);
    TestTrue(TEXT("The catalog uses SC_DemoMaster, SC_DemoMusic and SMix_DemoUser"),Catalog->MasterClass&&Catalog->UserMix&&Catalog->MusicClass
        &&Catalog->MasterClass->GetPathName()==ULDAudioCatalog::MasterClassPath&&Catalog->UserMix->GetPathName()==ULDAudioCatalog::UserMixPath
        &&Catalog->MusicClass->GetPathName()==ULDAudioCatalog::MusicClassPath&&Catalog->SfxClass&&Catalog->UIClass
        &&Catalog->SfxClass->GetPathName()==ULDAudioCatalog::SfxClassPath&&Catalog->UIClass->GetPathName()==ULDAudioCatalog::UIClassPath);
    if(!bResolved){return false;}
    // The farm track restarts with every field segment and the rest that follows fades it out, so a longer track is cut
    // cleanly while a shorter one would leave the end of the segment silent.
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    if(!LDTestApprovedFixture::Read(*this,Content)){return false;}
    const float SegmentSec=float(Content->Get().Parameters.FieldSegmentMs)/1000.f;
    const float FarmSec=Out.Music[int32(ELDMusicTrack::Farm)]->Duration;
    TestTrue(FString::Printf(TEXT("The farm track (%.2f s) covers a field segment (%.0f s)"),FarmSec,SegmentSec),
        FarmSec>=SegmentSec);
    return true;
}
#endif
