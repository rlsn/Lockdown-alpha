#include "Audio/LDAudioCatalog.h"
#include "Audio/LDAudioCueTracker.h"
#include "Audio/LDAudioTypes.h"
#include "Development/LDDemoContentIds.h"
#include "Misc/AutomationTest.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundWave.h"
#if WITH_DEV_AUTOMATION_TESTS
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

    Public.Phase=ELDMatchPhase::Results;Public.ResultId=FGuid::NewGuid();Public.CaptureEpoch=7;
    Result.Generation=7;Result.ResultId=Public.ResultId;Result.ResultSerial=1;Result.Reason=ELDMatchEndReason::CaptureCompleted;Result.Winner=Self;Result.EndedAtMs=Frame.NowMs;
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
    FLDProgressionView Growth;Growth.bValid=true;Growth.Generation=3;Growth.Life=1;Growth.Level=1;
    FLDAudioFrame Frame;Frame.Self=FGuid::NewGuid();Frame.OwnMatch=3;Frame.OwnLife=1;Frame.OwnLifePhase=ELDLifePhase::Alive;
    Frame.OwnCombat=&Combat;Frame.OwnProgression=&Growth;Frame.LocalSeconds=1;
    TestTrue(TEXT("The first own sample only primes"),AudioSample(Tracker,Frame).IsEmpty());
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
    Combat.Life=2;Combat.Revision=1;Combat.HPP2=50000;Combat.MaxHPP2=50000;
    TestTrue(TEXT("A new life's first combat view primes"),AudioSample(Tracker,Frame).IsEmpty());
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
    FLDCombatActionReceipt Refused=Cast;Refused.DirectSequence=12;Refused.Entry=ELDCombatEntry::Item1;Refused.Phase=ELDCombatPhase::Rejected;Refused.Reject=ELDCombatReject::Cooldown;
    Tracker.ObserveCombatReceipt(Refused,4,1,Out);
    FLDCombatActionReceipt Stopped=Cast;Stopped.DirectSequence=13;Stopped.Reject=ELDCombatReject::Cancelled;Tracker.ObserveCombatReceipt(Stopped,4,1,Out);
    FLDCombatActionReceipt OldLife=Cast;OldLife.DirectSequence=14;OldLife.Generation=3;Tracker.ObserveCombatReceipt(OldLife,4,1,Out);
    OldLife.Generation=4;OldLife.Life=2;Tracker.ObserveCombatReceipt(OldLife,4,1,Out);
    TestTrue(TEXT("Basic attacks, refusals, cancels and receipts of another match or life stay silent"),Out.IsEmpty());
    FLDCombatActionReceipt Item=Cast;Item.DirectSequence=15;Item.Entry=ELDCombatEntry::Item2;Tracker.ObserveCombatReceipt(Item,4,1,Out);
    TestTrue(TEXT("An equipment active sounds as a cast"),AudioIs(Out,{ELDAudioCue::SelfCast}));

    Out.Reset();
    FLDSubmitResult Buy;Buy.Generation=4;Buy.Family=ELDGrowthFamily::Shop;Buy.RequestId=1;Buy.Status=ELDAdmissionStatus::Final;
    Buy.bHasShopReceipt=true;Buy.ShopReceipt.Header.Outcome=ELDReceiptOutcome::Applied;
    Tracker.ObserveShopSubmit(Buy,4,Out);TestTrue(TEXT("An applied trade sounds"),AudioIs(Out,{ELDAudioCue::PurchaseSucceeded}));
    Out.Reset();
    FLDShopReceiptQueryResult Query;Query.Query.Generation=4;Query.Query.Family=ELDGrowthFamily::Shop;Query.Query.RequestId=1;
    Query.Lookup=ELDReceiptLookup::Final;Query.bHasReceipt=true;Query.Receipt.Header.Outcome=ELDReceiptOutcome::Applied;
    Tracker.ObserveShopReceipt(Query,4,Out);TestTrue(TEXT("The same request's later receipt stays silent"),Out.IsEmpty());
    FLDSubmitResult Refusal=Buy;Refusal.RequestId=2;Refusal.Status=ELDAdmissionStatus::AdmissionRejected;Refusal.bSequenceReserved=false;Refusal.bHasShopReceipt=false;
    Tracker.ObserveShopSubmit(Refusal,4,Out);TestTrue(TEXT("An unreserved refusal sounds rejected"),AudioIs(Out,{ELDAudioCue::TradeRejected}));
    Out.Reset();
    FLDSubmitResult Reused=Buy;Reused.RequestId=2;Tracker.ObserveShopSubmit(Reused,4,Out);
    TestTrue(TEXT("The reused id of a refused request still sounds its final result"),AudioIs(Out,{ELDAudioCue::PurchaseSucceeded}));
    Out.Reset();
    FLDSubmitResult Reserved=Refusal;Reserved.RequestId=3;Reserved.bSequenceReserved=true;Tracker.ObserveShopSubmit(Reserved,4,Out);
    TestTrue(TEXT("A reserved refusal waits for its final receipt"),Out.IsEmpty());
    Query.Query.RequestId=3;Query.Receipt.Header.Outcome=ELDReceiptOutcome::Rejected;Tracker.ObserveShopReceipt(Query,4,Out);
    TestTrue(TEXT("The final rejected receipt sounds"),AudioIs(Out,{ELDAudioCue::TradeRejected}));
    Out.Reset();
    Query.Query.RequestId=1;Query.Receipt.Header.Outcome=ELDReceiptOutcome::Applied;Tracker.ObserveShopReceipt(Query,4,Out);
    FLDSubmitResult Foreign=Buy;Foreign.RequestId=9;Foreign.Generation=3;Tracker.ObserveShopSubmit(Foreign,4,Out);
    FLDSubmitResult Choice=Buy;Choice.RequestId=10;Choice.Family=ELDGrowthFamily::Choice;Tracker.ObserveShopSubmit(Choice,4,Out);
    TestTrue(TEXT("Late, foreign-match and choice results stay silent"),Out.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDAudioCatalogTest,"Lockdown.Rules.AudioCatalog",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDAudioCatalogTest::RunTest(const FString& Parameters)
{
    for(int32 Index=1;Index<int32(ELDAudioCue::Count);++Index)
    {
        ELDAudioCue Parsed=ELDAudioCue::None;
        TestTrue(TEXT("Every cue id round-trips"),LDAudio::ParseCueId(LDAudio::CueId(ELDAudioCue(Index)),Parsed)&&Parsed==ELDAudioCue(Index));
    }
    ELDAudioCue Parsed=ELDAudioCue::None;
    TestFalse(TEXT("Cue ids are case-sensitive and None is not a cue"),LDAudio::ParseCueId(TEXT("killnotice"),Parsed)||LDAudio::ParseCueId(TEXT("None"),Parsed));
    auto* Master=NewObject<USoundClass>();auto* Mix=NewObject<USoundMix>();auto* Catalog=NewObject<ULDAudioCatalog>();
    auto Fill=[&]()
    {
        Catalog->MasterClass=Master;Catalog->UserMix=Mix;Catalog->Cues.Reset();
        for(int32 Index=1;Index<int32(ELDAudioCue::Count);++Index)
        {
            auto* Wave=NewObject<USoundWave>();Wave->SoundClassObject=Master;Wave->Duration=.5f;
            FLDAudioCueEntry& Entry=Catalog->Cues.AddDefaulted_GetRef();Entry.Cue=ELDAudioCue(Index);Entry.Sound=Wave;
        }
    };
    TArray<USoundWave*> Sounds;FString Error;
    Fill();
    TestTrue(TEXT("A complete catalog resolves every cue"),Catalog->Resolve(Sounds,Error)&&Sounds.Num()==int32(ELDAudioCue::Count)
        &&Sounds[int32(ELDAudioCue::Count)-1]==Catalog->Cues.Last().Sound&&!Sounds[0]);
    Catalog->Cues.Pop();TestFalse(TEXT("A missing cue is an error"),Catalog->Resolve(Sounds,Error));
    Fill();Catalog->Cues.Swap(0,1);TestFalse(TEXT("Entries out of cue order are an error"),Catalog->Resolve(Sounds,Error));
    Fill();Catalog->Cues[3].Sound=nullptr;TestFalse(TEXT("A cue without sound is an error"),Catalog->Resolve(Sounds,Error));
    Fill();Catalog->Cues[0].Sound->SoundClassObject=NewObject<USoundClass>();TestFalse(TEXT("A sound outside the master class is an error"),Catalog->Resolve(Sounds,Error));
    Fill();Catalog->Cues[1].Sound->bLooping=true;TestFalse(TEXT("A looping cue is an error"),Catalog->Resolve(Sounds,Error));
    Fill();Catalog->Cues[2].Sound->Duration=0.f;TestFalse(TEXT("A cue without audio is an error"),Catalog->Resolve(Sounds,Error));
    Fill();Catalog->UserMix=nullptr;TestFalse(TEXT("A catalog without the user mix is an error"),Catalog->Resolve(Sounds,Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDAudioCatalogAssetTest,"Lockdown.Rules.AudioCatalogAsset",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDAudioCatalogAssetTest::RunTest(const FString& Parameters)
{
    // The shipped catalog: every cue resolves to an imported one-shot routed through the single master chain.
    const auto* Catalog=LoadObject<ULDAudioCatalog>(nullptr,ULDAudioCatalog::AssetPath);
    if(!TestNotNull(TEXT("The Demo audio catalog asset exists"),Catalog)){return false;}
    TArray<USoundWave*> Sounds;FString Error;
    const bool bResolved=Catalog->Resolve(Sounds,Error);
    TestTrue(FString(TEXT("The Demo audio catalog resolves: "))+Error,bResolved);
    TestTrue(TEXT("The catalog uses SC_DemoMaster and SMix_DemoUser"),Catalog->MasterClass&&Catalog->UserMix
        &&Catalog->MasterClass->GetPathName()==ULDAudioCatalog::MasterClassPath&&Catalog->UserMix->GetPathName()==ULDAudioCatalog::UserMixPath);
    return true;
}
#endif
