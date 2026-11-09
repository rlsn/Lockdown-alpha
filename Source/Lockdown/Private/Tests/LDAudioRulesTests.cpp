#include "Audio/LDAudioCatalog.h"
#include "Audio/LDAudioCueTracker.h"
#include "Audio/LDAudioTypes.h"
#include "Audio/LDMusicSelection.h"
#include "Development/LDDemoContentIds.h"
#include "Framework/LDCaptureTypes.h"
#include "Misc/AutomationTest.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundWave.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/LDTestApprovedFixture.h"
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

    // The shield: the own projection lists the shield status with its end; nothing else is read.
    FLDCombatStatusView Shield;Shield.StatusId=uint8(ELDDemoStatusId::Shield);Shield.ExpiresAtMs=9000;Shield.AmountP2=3000;
    Combat.Revision=5;Combat.HPP2=40000;Combat.ShieldP2=3000;Combat.Statuses={Shield};Combat.AsOfMs=5000;Frame.LocalSeconds=4.5;
    TestTrue(TEXT("Gaining a shield is silent"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Revision=6;Combat.ShieldP2=1000;Combat.Statuses[0].AmountP2=1000;Combat.AsOfMs=5500;Frame.LocalSeconds=5;
    TestTrue(TEXT("A hit the shield absorbs whole sounds the shield hit"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::SelfShieldHit}));
    Combat.Revision=7;Combat.ShieldP2=500;Combat.Statuses[0].AmountP2=500;Combat.AsOfMs=5550;Frame.LocalSeconds=5.1;
    TestTrue(TEXT("Shield hits share the hit repeat limit"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Revision=8;Combat.ShieldP2=0;Combat.Statuses.Reset();Combat.HPP2=36000;Combat.AsOfMs=6000;Frame.LocalSeconds=6;
    TestTrue(TEXT("A hit that breaks the shield and costs points is the ordinary hit"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::SelfHit}));
    Combat.Revision=9;Combat.ShieldP2=2000;Shield.AmountP2=2000;Shield.ExpiresAtMs=7000;Combat.Statuses={Shield};Combat.AsOfMs=6500;Frame.LocalSeconds=7;
    AudioSample(Tracker,Frame);
    Combat.Revision=10;Combat.ShieldP2=0;Combat.Statuses.Reset();Combat.AsOfMs=7000;Frame.LocalSeconds=8;
    TestTrue(TEXT("A shield that runs out at its end is silent"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Revision=11;Combat.ShieldP2=2000;Shield.ExpiresAtMs=9000;Combat.Statuses={Shield};Combat.AsOfMs=7500;Frame.LocalSeconds=9;
    AudioSample(Tracker,Frame);
    Combat.Revision=12;Combat.ShieldP2=0;Combat.Statuses.Reset();Combat.AsOfMs=7600;Frame.LocalSeconds=10;
    TestTrue(TEXT("A shield broken before its end by a hit sounds the shield hit"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::SelfShieldHit}));

    // The own basic attack: each newer basic release in the owner projection's cue facts sounds once.
    Combat.Revision=13;Combat.Cues.ReleasedStartedAtMs=7700;Combat.Cues.ReleasedAtMs=8000;Combat.AsOfMs=8000;Frame.LocalSeconds=10.5;
    TestTrue(TEXT("A released own basic attack sounds"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::SelfAttack}));
    TestTrue(TEXT("The same revision never repeats the attack"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Revision=14;Combat.Cues.bActing=true;Combat.Cues.bActingBasic=true;Combat.Cues.ActingStartedAtMs=8300;Frame.LocalSeconds=10.6;
    TestTrue(TEXT("A republished release and a new windup stay silent"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Revision=15;Combat.Cues.bActing=false;Combat.Cues.bActingBasic=false;Combat.Cues.ReleasedStartedAtMs=8300;Combat.Cues.ReleasedAtMs=8600;Combat.AsOfMs=8600;Frame.LocalSeconds=10.9;
    TestTrue(TEXT("Consecutive basic attacks each sound"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::SelfAttack}));
    Combat.Revision=16;Combat.Cues.bReleasedBasic=false;Combat.Cues.ReleasedStartedAtMs=8700;Combat.Cues.ReleasedAtMs=9000;Combat.AsOfMs=9000;Frame.LocalSeconds=11.2;
    TestTrue(TEXT("A released skill is left to its receipt"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Revision=17;Combat.Cues.bReleasedBasic=true;Combat.Cues.ReleasedStartedAtMs=200;Combat.Cues.ReleasedAtMs=500;Frame.LocalSeconds=11.5;
    TestTrue(TEXT("An older release time never sounds"),AudioSample(Tracker,Frame).IsEmpty());

    Growth.Level=2;Growth.EarnedThrough=1;
    TestTrue(TEXT("A level up sounds once"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::LevelUp}));
    TestTrue(TEXT("The same level stays silent"),AudioSample(Tracker,Frame).IsEmpty());
    Growth.EarnedThrough=2;
    TestTrue(TEXT("A newly earned three-choice sounds"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::LevelUp}));
    Growth.ConsumedThrough=1;Growth.PendingChoiceCount=1;
    TestTrue(TEXT("Picking a card and showing the queued offer stays silent"),AudioSample(Tracker,Frame).IsEmpty());

    Frame.OwnLifePhase=ELDLifePhase::Dead;Combat.Revision=5;Combat.HPP2=0;Frame.LocalSeconds=6;
    TestTrue(TEXT("Death sounds once without a hit"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::SelfDeath}));
    TestTrue(TEXT("Waiting dead stays silent"),AudioSample(Tracker,Frame).IsEmpty());
    Frame.OwnLife=2;Frame.OwnLifePhase=ELDLifePhase::Alive;
    TestTrue(TEXT("The next life sounds a respawn; the old life's view cannot hit"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::SelfRespawn}));
    Combat.Life=2;Combat.Revision=1;Combat.HPP2=50000;Combat.MaxHPP2=50000;Combat.Cues.ReleasedStartedAtMs=9700;Combat.Cues.ReleasedAtMs=10000;Combat.AsOfMs=10000;
    TestTrue(TEXT("A new life's first combat view primes, including its release"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Life=1;Combat.Revision=20;Combat.Cues.ReleasedAtMs=11000;
    TestTrue(TEXT("A release in the old life's view stays silent"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Life=2;Combat.Revision=2;Combat.Cues.ReleasedStartedAtMs=10700;Combat.Cues.ReleasedAtMs=11000;Combat.AsOfMs=11000;
    TestTrue(TEXT("The new life's own basic attack sounds"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::SelfAttack}));
    Frame.OwnMatch=4;Frame.OwnLife=1;Combat.Generation=4;Combat.Life=1;Growth.Generation=4;Growth.Level=1;Growth.EarnedThrough=0;
    TestTrue(TEXT("A new match never sounds a respawn or level"),AudioSample(Tracker,Frame).IsEmpty());

    TArray<ELDAudioCue> Out;
    FLDCombatActionReceipt Cast;Cast.Generation=4;Cast.Life=1;Cast.Origin=ELDCombatRequestOrigin::Direct;Cast.DirectSequence=10;Cast.Entry=ELDCombatEntry::Q;
    Cast.Phase=ELDCombatPhase::Accepted;Cast.Reject=ELDCombatReject::None;
    Tracker.ObserveCombatReceipt(Cast,4,1,Out);TestTrue(TEXT("An accepted windup is not a cast yet"),Out.IsEmpty());
    Cast.Phase=ELDCombatPhase::Finished;Tracker.ObserveCombatReceipt(Cast,4,1,Out);
    TestTrue(TEXT("The released own active sounds"),AudioIs(Out,{ELDAudioCue::SelfCast}));
    Out.Reset();Tracker.ObserveCombatReceipt(Cast,4,1,Out);TestTrue(TEXT("A repeated receipt stays silent"),Out.IsEmpty());
    FLDCombatActionReceipt Basic=Cast;Basic.DirectSequence=11;Basic.Entry=ELDCombatEntry::Basic;Tracker.ObserveCombatReceipt(Basic,4,1,Out);
    FLDCombatActionReceipt Refused=Cast;Refused.DirectSequence=12;Refused.Entry=ELDCombatEntry::W;Refused.Phase=ELDCombatPhase::Rejected;Refused.Reject=ELDCombatReject::Cooldown;
    Tracker.ObserveCombatReceipt(Refused,4,1,Out);
    FLDCombatActionReceipt Stopped=Cast;Stopped.DirectSequence=13;Stopped.Reject=ELDCombatReject::Cancelled;Tracker.ObserveCombatReceipt(Stopped,4,1,Out);
    FLDCombatActionReceipt OldLife=Cast;OldLife.DirectSequence=14;OldLife.Generation=3;Tracker.ObserveCombatReceipt(OldLife,4,1,Out);
    OldLife.Generation=4;OldLife.Life=2;Tracker.ObserveCombatReceipt(OldLife,4,1,Out);
    TestTrue(TEXT("Basic attacks, refusals, cancels and receipts of another match or life stay silent"),Out.IsEmpty());
    FLDCombatActionReceipt Dash=Cast;Dash.DirectSequence=15;Dash.Entry=ELDCombatEntry::Dash;Tracker.ObserveCombatReceipt(Dash,4,1,Out);
    TestTrue(TEXT("The innate dash sounds as a cast"),AudioIs(Out,{ELDAudioCue::SelfCast}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDAudioCueBasicAttackTest,"Lockdown.Rules.AudioCueBasicAttack",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDAudioCueBasicAttackTest::RunTest(const FString& Parameters)
{
    FLDAudioCueTracker Tracker;
    FLDCombatOwnerView Combat;Combat.Generation=5;Combat.Life=1;Combat.bHasBody=true;
    Combat.Revision=1;Combat.AsOfMs=1000;Combat.HPP2=Combat.MaxHPP2=50000;
    Combat.Cues.bReleasedBasic=true;Combat.Cues.ReleasedAtMs=900;
    FLDAudioFrame Frame;Frame.OwnMatch=5;Frame.OwnLife=1;Frame.OwnLifePhase=ELDLifePhase::Alive;
    Frame.OwnCombat=&Combat;Frame.LocalSeconds=1;
    TestTrue(TEXT("A release already present at the first sample is not replayed"),AudioSample(Tracker,Frame).IsEmpty());

    ++Combat.Revision;Combat.AsOfMs=1100;Combat.Cues.bActing=true;Combat.Cues.bActingBasic=true;
    Combat.Cues.ActingStartedAtMs=1100;
    TestTrue(TEXT("A basic windup is not a released swing"),AudioSample(Tracker,Frame).IsEmpty());
    TArray<ELDAudioCue> Out;
    FLDCombatActionReceipt Refused;Refused.Generation=5;Refused.Life=1;Refused.Entry=ELDCombatEntry::Basic;
    Refused.DirectSequence=1;Refused.Phase=ELDCombatPhase::Rejected;Refused.Reject=ELDCombatReject::OutOfRange;
    Tracker.ObserveCombatReceipt(Refused,5,1,Out);
    ++Combat.Revision;Combat.Cues.bActing=false;
    TestTrue(TEXT("A refused or cancelled basic without a release stays silent"),
        Out.IsEmpty()&&AudioSample(Tracker,Frame).IsEmpty());
    Refused.DirectSequence=2;Refused.Reject=ELDCombatReject::Cancelled;
    Tracker.ObserveCombatReceipt(Refused,5,1,Out);
    TestTrue(TEXT("A cancelled basic receipt cannot manufacture a swing"),Out.IsEmpty());

    ++Combat.Revision;Combat.AsOfMs=1200;Combat.Cues.ReleasedAtMs=1300;
    TestTrue(TEXT("A release later than its projection cannot sound"),AudioSample(Tracker,Frame).IsEmpty());
    ++Combat.Revision;Combat.AsOfMs=1400;Combat.Cues.ReleasedAtMs=1400;
    TestTrue(TEXT("An own successful basic release sounds a swing without any HP change"),
        AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::SelfAttack}));
    TestTrue(TEXT("An identical projection cannot replay the swing"),AudioSample(Tracker,Frame).IsEmpty());
    ++Combat.Revision;
    TestTrue(TEXT("A newer projection with the same release cannot replay it"),AudioSample(Tracker,Frame).IsEmpty());
    --Combat.Revision;Combat.Cues.ReleasedAtMs=1500;Combat.AsOfMs=1500;
    TestTrue(TEXT("An older revision cannot supply a new swing"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Revision+=2;Combat.Cues.ReleasedAtMs=1300;
    TestTrue(TEXT("A release timestamp below the watermark cannot replay"),AudioSample(Tracker,Frame).IsEmpty());
    ++Combat.Revision;Combat.Cues.ReleasedAtMs=1400;
    TestTrue(TEXT("A regressed release cannot lower the watermark"),AudioSample(Tracker,Frame).IsEmpty());

    ++Combat.Revision;Combat.AsOfMs=1600;Combat.Cues.bReleasedBasic=false;Combat.Cues.ReleasedAtMs=1600;
    TestTrue(TEXT("A skill release is not a basic attack"),AudioSample(Tracker,Frame).IsEmpty());
    ++Combat.Revision;Combat.HPP2=49000;Combat.Cues.LastHitAtMs=1700;Combat.AsOfMs=1700;Frame.LocalSeconds=2;
    TestTrue(TEXT("Taking damage is SelfHit, not a basic swing"),
        AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::SelfHit}));
    ++Combat.Revision;Combat.HPP2=50000;
    TestTrue(TEXT("Gaining HP does not manufacture a landed hit or swing"),AudioSample(Tracker,Frame).IsEmpty());
    ++Combat.Revision;Combat.Cues.bReleasedBasic=true;Combat.Cues.ReleasedAtMs=1800;Combat.AsOfMs=1800;
    TestTrue(TEXT("The next actual basic release sounds independently of HP"),
        AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::SelfAttack}));

    ++Combat.Revision;Combat.Cues.ReleasedAtMs=1900;Combat.AsOfMs=1900;Combat.HPP2=0;Frame.OwnLifePhase=ELDLifePhase::Dead;
    TestTrue(TEXT("A release first delivered with own death does not sound a swing"),
        AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::SelfDeath}));
    ++Combat.Revision;
    TestTrue(TEXT("A dead projection never replays its release"),AudioSample(Tracker,Frame).IsEmpty());
    Frame.OwnLife=2;Frame.OwnLifePhase=ELDLifePhase::Alive;
    TestTrue(TEXT("Respawn ignores the previous body's release"),AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::SelfRespawn}));
    Combat.Life=2;Combat.Revision=1;Combat.HPP2=50000;Combat.AsOfMs=2100;Combat.Cues.ReleasedAtMs=2000;
    TestTrue(TEXT("A new life primes its own release watermark"),AudioSample(Tracker,Frame).IsEmpty());
    Frame.OwnLife=1;Combat.Life=1;Combat.Revision=100;Combat.Cues.ReleasedAtMs=2100;
    TestTrue(TEXT("A late old owner life cannot rewind the release tracker"),AudioSample(Tracker,Frame).IsEmpty());
    Frame.OwnLife=2;Combat.Life=2;Combat.Revision=2;Combat.AsOfMs=2200;Combat.Cues.ReleasedAtMs=2200;
    TestTrue(TEXT("An old life cannot swallow the new life's next release"),
        AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::SelfAttack}));
    Frame.OwnMatch=6;Frame.OwnLife=1;Combat.Generation=6;Combat.Life=1;Combat.Revision=1;
    Combat.AsOfMs=100;Combat.Cues.ReleasedAtMs=100;
    TestTrue(TEXT("A new match primes without a respawn or an old swing"),AudioSample(Tracker,Frame).IsEmpty());
    ++Combat.Revision;Combat.AsOfMs=200;Combat.Cues.ReleasedAtMs=200;
    TestTrue(TEXT("The new match can sound its own release at a lower timestamp"),
        AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::SelfAttack}));
    ++Combat.Revision;Combat.AsOfMs=300;Combat.Cues.ReleasedAtMs=300;Combat.Generation=5;
    TestTrue(TEXT("Another match's combat projection cannot supply a swing"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Generation=6;Combat.Life=2;
    TestTrue(TEXT("Another life's combat projection cannot supply a swing"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.Life=1;Combat.bHasBody=false;
    TestTrue(TEXT("A projection without an own body cannot supply a swing"),AudioSample(Tracker,Frame).IsEmpty());
    Combat.bHasBody=true;
    TestTrue(TEXT("A valid current body release is still observed after invalid projections"),
        AudioIs(AudioSample(Tracker,Frame),{ELDAudioCue::SelfAttack}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDAudioCatalogTest,"Lockdown.Rules.AudioCatalog",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDAudioCatalogTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Existing cue numbering stays stable"),uint8(ELDAudioCue::SelfShieldHit),uint8(16));
    TestEqual(TEXT("The basic swing cue is appended"),uint8(ELDAudioCue::SelfAttack),uint8(17));
    TestEqual(TEXT("The level-up cue keeps its number past the retired shop trade cues"),uint8(ELDAudioCue::LevelUp),uint8(12));
    TestTrue(TEXT("Retired cue numbers are not playable"),!LDAudio::IsPlayableCue(ELDAudioCue(10))&&!LDAudio::IsPlayableCue(ELDAudioCue(11)));
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
    auto Fill=[&]()
    {
        MusicClass->ParentClass=Master;Master->ChildClasses={MusicClass};
        Catalog->MasterClass=Master;Catalog->MusicClass=MusicClass;Catalog->UserMix=Mix;Catalog->Cues.Reset();Catalog->Music.Reset();
        for(int32 Index=0;Index<LDAudio::CueCount;++Index)
        {
            auto* Wave=NewObject<USoundWave>();Wave->SoundClassObject=Master;Wave->Duration=.5f;
            FLDAudioCueEntry& Entry=Catalog->Cues.AddDefaulted_GetRef();Entry.Cue=LDAudio::PlayableCue(Index);Entry.Sound=Wave;
        }
        for(int32 Index=1;Index<int32(ELDMusicTrack::Count);++Index)
        {
            auto* Wave=NewObject<USoundWave>();Wave->SoundClassObject=MusicClass;Wave->Duration=180.f;Wave->bLooping=LDAudio::MusicTrackLoops(ELDMusicTrack(Index));
            FLDMusicTrackEntry& Entry=Catalog->Music.AddDefaulted_GetRef();Entry.Track=ELDMusicTrack(Index);Entry.Sound=Wave;
        }
    };
    TArray<USoundWave*> Sounds,Music;FString Error;
    Fill();
    TestTrue(TEXT("A complete catalog resolves every cue"),Catalog->Resolve(Sounds,Music,Error)&&Sounds.Num()==int32(ELDAudioCue::Count)
        &&Sounds[int32(ELDAudioCue::Count)-1]==Catalog->Cues.Last().Sound&&!Sounds[0]);
    TestTrue(TEXT("A complete catalog resolves every music track by track value"),Music.Num()==int32(ELDMusicTrack::Count)&&!Music[0]
        &&Music[int32(ELDMusicTrack::Escalation)]==Catalog->Music.Last().Sound);
    Catalog->Cues.Pop();TestFalse(TEXT("A missing cue is an error"),Catalog->Resolve(Sounds,Music,Error));
    Fill();Catalog->Cues.Swap(0,1);TestFalse(TEXT("Entries out of cue order are an error"),Catalog->Resolve(Sounds,Music,Error));
    Fill();Catalog->Cues[9].Cue=ELDAudioCue(10);TestFalse(TEXT("A retired cue number is an error"),Catalog->Resolve(Sounds,Music,Error));
    Fill();Catalog->Cues[3].Sound=nullptr;TestFalse(TEXT("A cue without sound is an error"),Catalog->Resolve(Sounds,Music,Error));
    Fill();Catalog->Cues[0].Sound->SoundClassObject=NewObject<USoundClass>();TestFalse(TEXT("A sound outside the master class is an error"),Catalog->Resolve(Sounds,Music,Error));
    Fill();Catalog->Cues[0].Sound->SoundClassObject=MusicClass;TestFalse(TEXT("A cue routed to the music class is an error"),Catalog->Resolve(Sounds,Music,Error));
    Fill();Catalog->Cues[1].Sound->bLooping=true;TestFalse(TEXT("A looping cue is an error"),Catalog->Resolve(Sounds,Music,Error));
    Fill();Catalog->Cues[2].Sound->Duration=0.f;TestFalse(TEXT("A cue without audio is an error"),Catalog->Resolve(Sounds,Music,Error));
    Fill();Catalog->UserMix=nullptr;TestFalse(TEXT("A catalog without the user mix is an error"),Catalog->Resolve(Sounds,Music,Error));
    Fill();Catalog->MusicClass=nullptr;TestFalse(TEXT("A catalog without the music class is an error"),Catalog->Resolve(Sounds,Music,Error));
    Fill();MusicClass->ParentClass=nullptr;TestFalse(TEXT("A music class outside the master class is an error"),Catalog->Resolve(Sounds,Music,Error));
    Fill();Master->ChildClasses.Reset();TestFalse(TEXT("A master class that does not list the music class is an error"),Catalog->Resolve(Sounds,Music,Error));
    Fill();Catalog->Music.Pop();TestFalse(TEXT("A missing music track is an error"),Catalog->Resolve(Sounds,Music,Error));
    Fill();Catalog->Music.Swap(0,1);TestFalse(TEXT("Music out of track order is an error"),Catalog->Resolve(Sounds,Music,Error));
    Fill();Catalog->Music[1].Sound=nullptr;TestFalse(TEXT("A music track without sound is an error"),Catalog->Resolve(Sounds,Music,Error));
    Fill();Catalog->Music[0].Sound->SoundClassObject=Master;TestFalse(TEXT("Music routed straight to the master class is an error"),Catalog->Resolve(Sounds,Music,Error));
    Fill();Catalog->Music[0].Sound->bLooping=false;TestFalse(TEXT("A menu track that does not loop is an error"),Catalog->Resolve(Sounds,Music,Error));
    Fill();Catalog->Music[1].Sound->bLooping=true;TestFalse(TEXT("A looping farm track is an error"),Catalog->Resolve(Sounds,Music,Error));
    Fill();Catalog->Music[2].Sound->Duration=0.f;TestFalse(TEXT("A music track without audio is an error"),Catalog->Resolve(Sounds,Music,Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDAudioMusicSelectionTest,"Lockdown.Rules.AudioMusicSelection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDAudioMusicSelectionTest::RunTest(const FString& Parameters)
{
    FLDPublicMatchView Public;Public.Generation=7;Public.StartedAtMs=100000;Public.OpenAtMs=280000;
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
    for(const ELDMatchPhase Phase:{ELDMatchPhase::Lobby,ELDMatchPhase::Starting,ELDMatchPhase::Countdown,ELDMatchPhase::Resetting})
    {TestTrue(FString::Printf(TEXT("Phase %d shares the menu loop with the main menu"),int32(Phase)),Is(Select(Phase),ELDMusicTrack::Menu));}
    TestTrue(TEXT("The development phase starts the farm track at T=0"),Is(Select(ELDMatchPhase::Development,true,100000),ELDMusicTrack::Farm,0.f));
    TestTrue(TEXT("A late start joins the farm track at the time since T=0"),Is(Select(ELDMatchPhase::Development,true,172500),ELDMusicTrack::Farm,72.5f));
    TestTrue(TEXT("A clock behind T=0 starts the farm track at its beginning"),Is(Select(ELDMatchPhase::Development,true,99000),ELDMusicTrack::Farm,0.f));
    TestFalse(TEXT("Without the authority clock the farm offset is unknown, so nothing changes"),Select(ELDMatchPhase::Development,false).bDecided);
    Public.StartedAtMs=0;
    TestFalse(TEXT("Without T=0 the farm offset is unknown, so nothing changes"),Select(ELDMatchPhase::Development).bDecided);
    Public.StartedAtMs=100000;
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
    TArray<USoundWave*> Sounds,Music;FString Error;
    const bool bResolved=Catalog->Resolve(Sounds,Music,Error);
    TestTrue(FString(TEXT("The Demo audio catalog resolves: "))+Error,bResolved);
    TestTrue(TEXT("The catalog uses SC_DemoMaster, SC_DemoMusic and SMix_DemoUser"),Catalog->MasterClass&&Catalog->UserMix&&Catalog->MusicClass
        &&Catalog->MasterClass->GetPathName()==ULDAudioCatalog::MasterClassPath&&Catalog->UserMix->GetPathName()==ULDAudioCatalog::UserMixPath
        &&Catalog->MusicClass->GetPathName()==ULDAudioCatalog::MusicClassPath);
    if(!bResolved){return false;}
    // The farm track spans the development phase (#94: "farm is exactly 3 mins to fit the first 3 mins of the game until the
    // flag opens"); a changed phase length needs a matching track rather than music that stops early or runs into the opening.
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    if(!LDTestApprovedFixture::Read(*this,Content)){return false;}
    const float DevelopmentSec=float(Content->Get().Parameters.DevelopmentMs)/1000.f;
    const float FarmSec=Music[int32(ELDMusicTrack::Farm)]->Duration;
    TestTrue(FString::Printf(TEXT("The farm track (%.2f s) lasts the development phase (%.0f s) within 2 s"),FarmSec,DevelopmentSec),
        FarmSec>=DevelopmentSec&&FarmSec<=DevelopmentSec+2.f);
    return true;
}
#endif
