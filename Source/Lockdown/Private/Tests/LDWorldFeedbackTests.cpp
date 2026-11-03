#include "UI/LDWorldFeedback.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentIds.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDCaptureTypes.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Vision/LDViewTypes.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
using FWorldFeedbackTestEvents=TArray<LDWorldFeedback::FEvent>;
struct FWorldFeedbackTestWant
{
    LDWorldFeedback::EKind Kind;
    int64 Amount;
};
FWorldFeedbackTestEvents WorldFeedbackTestObserve(LDWorldFeedback::FTracker& Tracker,const LDWorldFeedback::FSample& Sample)
{
    FWorldFeedbackTestEvents Out;Tracker.Observe(Sample,Out);return Out;
}
// Exactly these kinds and amounts, in this order.
bool WorldFeedbackTestIs(const FWorldFeedbackTestEvents& Events,std::initializer_list<FWorldFeedbackTestWant> Expected)
{
    if(Events.Num()!=int32(Expected.size())){return false;}
    int32 Index=0;
    for(const FWorldFeedbackTestWant& Want:Expected)
    {
        if(Events[Index].Kind!=Want.Kind||Events[Index].Amount!=Want.Amount){return false;}
        ++Index;
    }
    return true;
}
LDWorldFeedback::FSample WorldFeedbackTestSample(uint32 Match,uint32 Life,const FGuid& Self)
{
    LDWorldFeedback::FSample Sample;
    Sample.Match=Match;Sample.Life=Life;Sample.LifePhase=ELDLifePhase::Alive;Sample.Self=Self;
    Sample.PublicGeneration=Match;Sample.bClock=true;Sample.NowMs=100000;
    return Sample;
}
FLDCombatOwnerView WorldFeedbackTestBody(uint32 Match,uint32 Life,uint64 Revision,int64 AsOfMs,int64 HPP2,int64 MaxHPP2=60000)
{
    FLDCombatOwnerView Combat;
    Combat.Generation=Match;Combat.Life=Life;Combat.Revision=Revision;Combat.AsOfMs=AsOfMs;Combat.bHasBody=true;
    Combat.HPP2=HPP2;Combat.MaxHPP2=MaxHPP2;Combat.MPP2=20000;Combat.MaxMPP2=20000;
    return Combat;
}
FLDCombatStatusView WorldFeedbackTestShield(int64 AmountP2,int64 ExpiresAtMs)
{
    FLDCombatStatusView Status;Status.StatusId=uint8(ELDDemoStatusId::Shield);Status.AmountP2=AmountP2;Status.ExpiresAtMs=ExpiresAtMs;
    return Status;
}
FLDObservedUnitView WorldFeedbackTestUnit(ELDObservedUnitKind Kind,int64 HPP2,int64 MaxP2)
{
    FLDObservedUnitView Unit;
    Unit.Handle=FGuid::NewGuid();Unit.ObservationEpoch=2;Unit.StateRevision=1;Unit.Kind=Kind;
    Unit.AuthorizedFieldMask=Kind==ELDObservedUnitKind::Contender?LDObservation::ContenderFields:LDObservation::CampFields;
    if(Kind==ELDObservedUnitKind::Contender){Unit.ContenderId=FGuid::NewGuid();Unit.Presentation=2;}
    else{Unit.CreatureId=uint8(ELDDemoMonsterId::MonsterScavenger);}
    Unit.Health.CurrentP2=HPP2;Unit.Health.MaxP2=MaxP2;
    return Unit;
}
FLDWorldView WorldFeedbackTestFrame(uint32 Match,uint32 Life,uint32 Generation,uint32 Revision,double SampledAtServerSec)
{
    FLDWorldView Frame;
    Frame.Schema=LDObservation::Schema;Frame.MatchGeneration=Match;Frame.ObserverLife=Life;Frame.Generation=Generation;
    Frame.Revision=Revision;Frame.SampledAtServerSec=SampledAtServerSec;
    return Frame;
}
FLDKillNotice WorldFeedbackTestNotice(uint64 Serial,const FGuid& Killer,const FGuid& Victim,int64 OccurredAtMs)
{
    FLDKillNotice Notice;Notice.Serial=Serial;Notice.Killer=Killer;Notice.Victim=Victim;Notice.Cause=ELDKillCause::Contender;Notice.OccurredAtMs=OccurredAtMs;
    return Notice;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorldFeedbackPrimingTest,"Lockdown.Rules.WorldFeedback.Priming",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDWorldFeedbackPrimingTest::RunTest(const FString& Parameters)
{
    using LDWorldFeedback::EKind;
    const FGuid Self=FGuid::NewGuid(),Rival=FGuid::NewGuid();
    LDWorldFeedback::FTracker Tracker;
    // Match 3 is already under way: a wounded and shielded body, gold, XP, a level, a fresh own kill and a hurt unit.
    FLDCombatOwnerView Combat=WorldFeedbackTestBody(3,1,8,20000,30000);
    Combat.ShieldP2=1500;Combat.Statuses.Add(WorldFeedbackTestShield(1500,25000));
    FLDProgressionView Growth;Growth.bValid=true;Growth.Generation=3;Growth.Level=3;Growth.EarnedThrough=2;Growth.CumulativeXP=400;
    FLDEquipmentView Purse;Purse.bValid=true;Purse.Generation=3;Purse.Gold=250;
    FLDKillFeedView Feed;Feed.Generation=3;Feed.Notices.Add(WorldFeedbackTestNotice(4,Self,Rival,99500));
    FLDWorldView Frame=WorldFeedbackTestFrame(3,1,1,7,20.);
    Frame.Units.Add(WorldFeedbackTestUnit(ELDObservedUnitKind::Contender,20000,60000));
    LDWorldFeedback::FSample Sample=WorldFeedbackTestSample(3,1,Self);
    Sample.OwnCombat=&Combat;Sample.Progression=&Growth;Sample.Equipment=&Purse;Sample.KillFeed=&Feed;Sample.Frame=&Frame;
    Sample.RosterName=[Rival](const FGuid& Id){return Id==Rival?FString(TEXT("同学甲")):FString();};
    TestTrue(TEXT("The first sample of a match only primes: nothing that happened before it is replayed"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    TestTrue(TEXT("An unchanged sample stays silent"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Combat.Revision=9;Combat.AsOfMs=20100;Combat.HPP2=29000;Purse.Gold=260;Growth.CumulativeXP=420;
    TestTrue(TEXT("Changes after the first sample are reported against it"),
        WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::DamageTaken,10},{EKind::Gold,10},{EKind::Experience,20}}));

    // Match 4: every projection differs from match 3 (gold resets to 150) and its feed already lists a fresh own kill;
    // it only primes again.
    Sample.Match=4;Sample.PublicGeneration=4;
    Combat=WorldFeedbackTestBody(4,1,1,500,60000);
    Growth.Generation=4;Growth.Level=1;Growth.EarnedThrough=0;Growth.CumulativeXP=0;
    Purse.Generation=4;Purse.Gold=150;
    Feed.Generation=4;Feed.Notices={WorldFeedbackTestNotice(1,Self,Rival,Sample.NowMs-100)};
    Frame=WorldFeedbackTestFrame(4,1,1,2,1.);Frame.Units.Add(WorldFeedbackTestUnit(ELDObservedUnitKind::CampMember,1000,30000));
    TestTrue(TEXT("A new match resets every baseline and primes silently"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());

    // Late projections of match 3, a body view of another life and a feed of another public generation.
    FLDCombatOwnerView OldBody=WorldFeedbackTestBody(3,1,10,20200,1000);
    FLDProgressionView OldGrowth=Growth;OldGrowth.Generation=3;OldGrowth.Level=6;OldGrowth.EarnedThrough=5;OldGrowth.CumulativeXP=5000;
    FLDEquipmentView OldPurse=Purse;OldPurse.Generation=3;OldPurse.Gold=5000;
    FLDKillFeedView OldFeed;OldFeed.Generation=3;OldFeed.Notices.Add(WorldFeedbackTestNotice(9,Self,Rival,Sample.NowMs-100));
    FLDWorldView OldFrame=Frame;OldFrame.MatchGeneration=3;OldFrame.Revision=9;OldFrame.Units[0].Health.CurrentP2=1;
    LDWorldFeedback::FSample Foreign=Sample;
    Foreign.OwnCombat=&OldBody;Foreign.Progression=&OldGrowth;Foreign.Equipment=&OldPurse;Foreign.KillFeed=&OldFeed;Foreign.Frame=&OldFrame;
    TestTrue(TEXT("Projections, notices and frames of another match stay silent"),WorldFeedbackTestObserve(Tracker,Foreign).IsEmpty());
    FLDCombatOwnerView OtherLife=WorldFeedbackTestBody(4,2,5,900,100);
    Feed.Notices.Add(WorldFeedbackTestNotice(2,Self,Rival,Sample.NowMs-50));
    Foreign.OwnCombat=&OtherLife;Foreign.KillFeed=&Feed;Foreign.PublicGeneration=5;
    TestTrue(TEXT("A body view of another life and a feed of another public generation stay silent"),WorldFeedbackTestObserve(Tracker,Foreign).IsEmpty());
    Combat.Revision=2;Combat.AsOfMs=600;Combat.HPP2=59000;Purse.Gold=180;Growth.CumulativeXP=30;
    const FWorldFeedbackTestEvents After=WorldFeedbackTestObserve(Tracker,Sample);
    TestTrue(TEXT("Foreign data moved neither the body nor the kill baseline; growth views of another match only re-prime"),
        WorldFeedbackTestIs(After,{{EKind::DamageTaken,10},{EKind::Kill,1}})&&After[1].Name==TEXT("同学甲"));
    Purse.Gold=210;Growth.CumulativeXP=70;
    TestTrue(TEXT("The re-primed growth then reports the next reward"),
        WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::Gold,30},{EKind::Experience,40}}));

    // A paused observation primes the body and the units again but keeps rewards, levels and kills.
    Tracker.Interrupt();
    Combat.Revision=3;Combat.AsOfMs=700;Combat.HPP2=50000;Purse.Gold=240;Growth.CumulativeXP=110;
    TestTrue(TEXT("After an interruption the body primes again while rewards keep their baseline"),
        WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::Gold,30},{EKind::Experience,40}}));
    Combat.Revision=4;Combat.AsOfMs=800;Combat.HPP2=49000;
    TestTrue(TEXT("The next body change is reported against the new baseline"),
        WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::DamageTaken,10}}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorldFeedbackOwnDamageAndHealTest,"Lockdown.Rules.WorldFeedback.OwnDamageAndHeal",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDWorldFeedbackOwnDamageAndHealTest::RunTest(const FString& Parameters)
{
    using LDWorldFeedback::EKind;
    LDWorldFeedback::FTracker Tracker;
    FLDCombatOwnerView Combat=WorldFeedbackTestBody(3,1,1,1000,50000);
    LDWorldFeedback::FSample Sample=WorldFeedbackTestSample(3,1,FGuid::NewGuid());Sample.OwnCombat=&Combat;
    TestTrue(TEXT("The first body view only primes"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Combat.Revision=2;Combat.AsOfMs=1100;Combat.HPP2=46300;
    const FWorldFeedbackTestEvents Hit=WorldFeedbackTestObserve(Tracker,Sample);
    TestTrue(TEXT("Lost points are one damage-taken number on the own body"),WorldFeedbackTestIs(Hit,{{EKind::DamageTaken,37}})&&Hit[0].bOwn);
    Combat.Revision=3;Combat.AsOfMs=1200;Combat.HPP2=46450;
    TestTrue(TEXT("A regeneration-sized gain stays quiet"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Combat.Revision=4;Combat.AsOfMs=1300;Combat.HPP2=47650;
    TestTrue(TEXT("A gain above the regeneration tolerance is a heal"),WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::Heal,12}}));
    Combat.Revision=5;Combat.AsOfMs=3300;Combat.HPP2=51450;
    TestTrue(TEXT("Two seconds of the fastest regeneration stay quiet"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Combat.HPP2=40000;
    TestTrue(TEXT("A revision is reported once"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Combat.Revision=4;
    TestTrue(TEXT("An older revision is ignored"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Combat.Revision=6;Combat.AsOfMs=3400;Combat.MaxHPP2=66000;Combat.HPP2=56595;
    TestTrue(TEXT("A changed maximum rescales the points silently"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Combat.Revision=7;Combat.AsOfMs=3500;Combat.HPP2=56095;
    TestTrue(TEXT("The next loss counts from the rescaled points"),WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::DamageTaken,5}}));
    Combat.Revision=8;Combat.AsOfMs=3600;Combat.HPP2=56046;
    TestTrue(TEXT("Less than half a point rounds to nothing"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Combat.Revision=9;Combat.AsOfMs=3700;Combat.HPP2=55996;
    TestTrue(TEXT("Half a point rounds up to one"),WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::DamageTaken,1}}));
    Combat.Revision=10;Combat.bHasBody=false;Combat.HPP2=0;
    TestTrue(TEXT("A view without a body is no hit"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Combat.Revision=11;Combat.bHasBody=true;Sample.LifePhase=ELDLifePhase::Dead;
    TestTrue(TEXT("Nothing is reported for a dead owner"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());

    Sample.Life=2;Sample.LifePhase=ELDLifePhase::Alive;Combat=WorldFeedbackTestBody(3,2,1,9000,60000);
    TestTrue(TEXT("The first view of a new life only primes"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Combat.Revision=2;Combat.AsOfMs=9100;Combat.HPP2=59000;
    TestTrue(TEXT("The new body reports its own losses"),WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::DamageTaken,10}}));
    FLDCombatOwnerView OldLife=WorldFeedbackTestBody(3,1,50,9200,100);Sample.OwnCombat=&OldLife;
    TestTrue(TEXT("A late view of the previous life is ignored"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Sample.OwnCombat=&Combat;Combat.Revision=3;Combat.AsOfMs=9300;Combat.HPP2=58000;
    TestTrue(TEXT("...and never became the baseline"),WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::DamageTaken,10}}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorldFeedbackShieldTest,"Lockdown.Rules.WorldFeedback.Shield",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDWorldFeedbackShieldTest::RunTest(const FString& Parameters)
{
    using LDWorldFeedback::EKind;
    LDWorldFeedback::FTracker Tracker;
    FLDCombatOwnerView Combat=WorldFeedbackTestBody(3,1,1,1000,50000);
    LDWorldFeedback::FSample Sample=WorldFeedbackTestSample(3,1,FGuid::NewGuid());Sample.OwnCombat=&Combat;
    TestTrue(TEXT("The first body view only primes"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Combat.Revision=2;Combat.AsOfMs=1100;Combat.ShieldP2=2000;Combat.Statuses={WorldFeedbackTestShield(2000,4000)};
    const FWorldFeedbackTestEvents Gain=WorldFeedbackTestObserve(Tracker,Sample);
    TestTrue(TEXT("A new shield is a 淡定 gain"),WorldFeedbackTestIs(Gain,{{EKind::ShieldGain,20}})&&Gain[0].bOwn);
    Combat.Revision=3;Combat.AsOfMs=1200;Combat.ShieldP2=500;Combat.Statuses={WorldFeedbackTestShield(500,4000)};
    TestTrue(TEXT("Shield points lost while it is still listed were absorbed"),WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::ShieldAbsorbed,15}}));
    Combat.Revision=4;Combat.AsOfMs=1300;Combat.ShieldP2=0;Combat.Statuses.Reset();Combat.HPP2=49000;
    TestTrue(TEXT("A hit through the shield shows the points lost and the shield absorbed"),
        WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::DamageTaken,10},{EKind::ShieldAbsorbed,5}}));
    Combat.Revision=5;Combat.AsOfMs=1400;Combat.ShieldP2=3000;Combat.Statuses={WorldFeedbackTestShield(3000,2000)};
    TestTrue(TEXT("A second shield is another gain"),WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::ShieldGain,30}}));
    Combat.Revision=6;Combat.AsOfMs=2000;Combat.ShieldP2=0;Combat.Statuses.Reset();
    TestTrue(TEXT("A shield that reached its end expires silently"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Combat.Revision=7;Combat.AsOfMs=2100;Combat.ShieldP2=1000;Combat.Statuses={WorldFeedbackTestShield(1000,5000)};
    TestTrue(TEXT("A third shield is a gain"),WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::ShieldGain,10}}));
    Combat.Revision=8;Combat.AsOfMs=2200;Combat.ShieldP2=0;Combat.Statuses.Reset();
    TestTrue(TEXT("A shield used up exactly before its end was absorbed, though no point was lost"),
        WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::ShieldAbsorbed,10}}));
    Combat.Revision=9;Combat.AsOfMs=2300;Combat.ShieldP2=2500;Combat.Statuses={WorldFeedbackTestShield(2500,6000)};
    TestTrue(TEXT("A fresh shield after that is a gain"),WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::ShieldGain,25}}));
    Combat.Revision=10;Combat.AsOfMs=2400;Combat.ShieldP2=4000;Combat.Statuses={WorldFeedbackTestShield(4000,7000)};
    TestTrue(TEXT("A stronger shield replacing a running one gains the difference"),WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::ShieldGain,15}}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorldFeedbackVisibleUnitsTest,"Lockdown.Rules.WorldFeedback.VisibleUnits",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDWorldFeedbackVisibleUnitsTest::RunTest(const FString& Parameters)
{
    using LDWorldFeedback::EKind;
    LDWorldFeedback::FTracker Tracker;
    const FLDObservedUnitView Rival=WorldFeedbackTestUnit(ELDObservedUnitKind::Contender,50000,60000);
    const FLDObservedUnitView Camp=WorldFeedbackTestUnit(ELDObservedUnitKind::CampMember,30000,30000);
    FLDWorldView Frame=WorldFeedbackTestFrame(3,1,1,1,10.);Frame.Units={Rival,Camp};
    LDWorldFeedback::FSample Sample=WorldFeedbackTestSample(3,1,FGuid::NewGuid());Sample.Frame=&Frame;
    auto Advance=[&Frame](uint32 Revision,double SampledAtServerSec){Frame.Revision=Revision;Frame.SampledAtServerSec=SampledAtServerSec;};
    TestTrue(TEXT("The first frame only primes"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Advance(2,10.1);Frame.Units[0].Health.CurrentP2=46300;Frame.Units[1].Health.CurrentP2=29000;
    const FWorldFeedbackTestEvents Hits=WorldFeedbackTestObserve(Tracker,Sample);
    TestTrue(TEXT("Each visible unit's loss between consecutive frames is a number anchored to that unit"),
        WorldFeedbackTestIs(Hits,{{EKind::UnitDamage,37},{EKind::UnitDamage,10}})&&!Hits[0].bOwn&&!Hits[1].bOwn
        &&Hits[0].Handle==Rival.Handle&&Hits[0].Epoch==Rival.ObservationEpoch&&Hits[1].Handle==Camp.Handle&&Hits[1].Epoch==Camp.ObservationEpoch);
    TestTrue(TEXT("A unit number is stamped with the sample time of the frame that showed the change"),
        Hits.Num()==2&&Hits[0].SampledAtServerSec==Frame.SampledAtServerSec&&Hits[1].SampledAtServerSec==Frame.SampledAtServerSec);
    Advance(3,10.2);Frame.Units[0].Health.CurrentP2=46450;Frame.Units[1].Health.CurrentP2=29150;
    const FWorldFeedbackTestEvents Gains=WorldFeedbackTestObserve(Tracker,Sample);
    TestTrue(TEXT("A contender's regeneration-sized gain stays quiet; a camp member, which never regenerates, heals above one point"),
        WorldFeedbackTestIs(Gains,{{EKind::UnitHeal,2}})&&Gains[0].Handle==Camp.Handle);
    Advance(4,10.3);Frame.Units[0].Health.CurrentP2=47650;Frame.Units[1].Health.CurrentP2=29250;
    const FWorldFeedbackTestEvents Heal=WorldFeedbackTestObserve(Tracker,Sample);
    TestTrue(TEXT("A contender gain above the regeneration tolerance heals; exactly one camp point does not"),
        WorldFeedbackTestIs(Heal,{{EKind::UnitHeal,12}})&&Heal[0].Handle==Rival.Handle);
    Advance(5,12.3);Frame.Units[0].Health.CurrentP2=51450;
    TestTrue(TEXT("Two seconds of contender regeneration stay quiet"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Frame.Units[1].Health.CurrentP2=1000;
    TestTrue(TEXT("A frame revision is reported once"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Frame.Units[1].Health.CurrentP2=29250;

    const FLDObservedUnitView CampNow=Frame.Units[1];
    Advance(6,12.4);Frame.Units={CampNow};
    TestTrue(TEXT("A unit leaving sight says nothing"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    FLDObservedUnitView Again=Rival;Again.Handle=FGuid::NewGuid();Again.Health.CurrentP2=20000;
    Advance(7,12.5);Frame.Units.Add(Again);
    TestTrue(TEXT("Back in sight under a new handle, the unit only primes: nothing is compared with its last sighting"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Advance(8,12.6);Frame.Units[1].Health.CurrentP2=19000;
    const FWorldFeedbackTestEvents Seen=WorldFeedbackTestObserve(Tracker,Sample);
    TestTrue(TEXT("The new handle then reports its own losses"),WorldFeedbackTestIs(Seen,{{EKind::UnitDamage,10}})&&Seen[0].Handle==Again.Handle);
    Advance(9,12.7);Frame.Units.RemoveAt(1);
    TestTrue(TEXT("Missing from a frame"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    FLDObservedUnitView Returned=Again;Returned.Health.CurrentP2=5000;
    Advance(10,12.8);Frame.Units.Add(Returned);
    TestTrue(TEXT("A unit missing from one frame is forgotten, even under the same handle"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Advance(11,12.9);Frame.Units[0].Health.MaxP2=36000;Frame.Units[0].Health.CurrentP2=35000;
    TestTrue(TEXT("A changed maximum rescales silently"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());

    Sample.Frame=nullptr;
    TestTrue(TEXT("No current frame says nothing"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Sample.Frame=&Frame;Advance(12,13.5);Frame.Units[0].Health.CurrentP2=34000;
    TestTrue(TEXT("After a frame gap every unit primes again"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Advance(13,13.6);Frame.Units[0].Health.CurrentP2=33000;
    TestTrue(TEXT("...and then reports again"),WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::UnitDamage,10}}));
    Advance(14,13.7);Frame.ObserverLife=2;Frame.Units[0].Health.CurrentP2=32000;
    TestTrue(TEXT("A frame of another observer life is a gap"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Frame.ObserverLife=1;Advance(15,13.8);Frame.Units[0].Health.CurrentP2=31000;
    TestTrue(TEXT("The next current frame primes"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Frame.Generation=2;Advance(1,14.);Frame.Units[0].Health.CurrentP2=20000;
    TestTrue(TEXT("A new observation generation primes, whatever its revision"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Advance(2,14.1);Frame.Units[0].Health.CurrentP2=19000;
    TestTrue(TEXT("...and then reports"),WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::UnitDamage,10}}));
    Sample.LifePhase=ELDLifePhase::Dead;Advance(3,14.2);Frame.Units[0].Health.CurrentP2=18000;
    TestTrue(TEXT("A dead observer sees no unit numbers"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());

    // A unit number shows with the relay's delayed pose of the frame that produced it.
    using LDWorldFeedback::EUnitHold;
    TestTrue(TEXT("A unit number waits while the presentation clock is before its frame"),LDWorldFeedback::UnitHold(true,true,10.05,10.1,.1)==EUnitHold::Hold);
    TestTrue(TEXT("It shows once the presentation clock reaches its frame"),
        LDWorldFeedback::UnitHold(true,true,10.1,10.1,.1)==EUnitHold::Release&&LDWorldFeedback::UnitHold(true,true,10.3,10.1,0.)==EUnitHold::Release);
    TestTrue(TEXT("Without a presentation clock it shows at once"),LDWorldFeedback::UnitHold(true,false,0.,10.1,0.)==EUnitHold::Release);
    TestTrue(TEXT("It never waits longer than the hold limit of local time"),
        LDWorldFeedback::UnitHold(true,true,9.,10.1,LDWorldFeedback::UnitHoldMaxSeconds-.01)==EUnitHold::Hold
        &&LDWorldFeedback::UnitHold(true,true,9.,10.1,LDWorldFeedback::UnitHoldMaxSeconds)==EUnitHold::Release);
    TestTrue(TEXT("A unit no longer presented drops its waiting number, even when it was due"),
        LDWorldFeedback::UnitHold(false,true,11.,10.1,.1)==EUnitHold::Drop&&LDWorldFeedback::UnitHold(false,false,0.,10.1,LDWorldFeedback::UnitHoldMaxSeconds)==EUnitHold::Drop);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorldFeedbackRewardsAndLevelTest,"Lockdown.Rules.WorldFeedback.RewardsAndLevel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDWorldFeedbackRewardsAndLevelTest::RunTest(const FString& Parameters)
{
    using LDWorldFeedback::EKind;
    LDWorldFeedback::FTracker Tracker;
    FLDProgressionView Growth;Growth.bValid=true;Growth.Generation=3;Growth.Level=1;Growth.MaxLevel=10;
    FLDEquipmentView Purse;Purse.bValid=true;Purse.Generation=3;Purse.Gold=300;
    LDWorldFeedback::FSample Sample=WorldFeedbackTestSample(3,1,FGuid::NewGuid());Sample.Progression=&Growth;Sample.Equipment=&Purse;
    TestTrue(TEXT("The first growth sample only primes"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Purse.Gold=330;Growth.CumulativeXP=60;
    const FWorldFeedbackTestEvents Reward=WorldFeedbackTestObserve(Tracker,Sample);
    TestTrue(TEXT("A kill reward raises gold and XP together: a gold line and an experience line on the own body"),
        WorldFeedbackTestIs(Reward,{{EKind::Gold,30},{EKind::Experience,60}})&&Reward[0].bOwn&&Reward[1].bOwn);
    Purse.Gold=360;Growth.CumulativeXP=120;
    TestTrue(TEXT("The next last hit is its own publication and its own +30"),
        WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::Gold,30},{EKind::Experience,60}}));
    Purse.Gold=60;
    TestTrue(TEXT("Buying is silent"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Purse.Gold=160;
    TestTrue(TEXT("Selling raises gold without XP and is silent"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Purse.Gold=200;Growth.CumulativeXP=220;Growth.Level=2;Growth.EarnedThrough=1;Growth.PendingChoiceCount=1;
    const FWorldFeedbackTestEvents Up=WorldFeedbackTestObserve(Tracker,Sample);
    TestTrue(TEXT("A reward reaching a level adds one level-up naming it and the three-choice it earned"),
        WorldFeedbackTestIs(Up,{{EKind::Gold,40},{EKind::Experience,100},{EKind::LevelUp,1}})&&Up[2].Level==2&&Up[2].bChoiceEarned&&Up[2].bOwn);
    Growth.ConsumedThrough=1;Growth.PendingChoiceCount=0;
    TestTrue(TEXT("Picking the card is silent"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Purse.Gold=120;Growth.CumulativeXP=260;
    TestTrue(TEXT("Gold floats only when it grew: a reward landing together with a purchase shows its XP alone"),
        WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::Experience,40}}));
    Purse.Gold=180;Growth.CumulativeXP=760;Growth.Level=4;Growth.EarnedThrough=3;Growth.PendingChoiceCount=2;
    const FWorldFeedbackTestEvents Multi=WorldFeedbackTestObserve(Tracker,Sample);
    TestTrue(TEXT("Several levels at once are one level-up naming the level reached and every choice earned"),
        WorldFeedbackTestIs(Multi,{{EKind::Gold,60},{EKind::Experience,500},{EKind::LevelUp,2}})&&Multi[2].Level==4&&Multi[2].bChoiceEarned);
    Purse.Gold=200;Growth.CumulativeXP=9000;Growth.Level=10;Growth.EarnedThrough=9;
    const FWorldFeedbackTestEvents Last=WorldFeedbackTestObserve(Tracker,Sample);
    TestTrue(TEXT("Reaching the last level is a level-up like any other"),
        WorldFeedbackTestIs(Last,{{EKind::Gold,20},{EKind::Experience,8240},{EKind::LevelUp,6}})&&Last[2].Level==10);
    Purse.Gold=230;Growth.CumulativeXP=9100;
    TestTrue(TEXT("XP keeps accumulating past the last level, so rewards still float there, without a level-up"),
        WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::Gold,30},{EKind::Experience,100}}));

    Purse.bValid=false;Growth.bValid=false;Purse.Gold=260;Growth.CumulativeXP=9200;
    TestTrue(TEXT("An invalid view is never read"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Purse.bValid=true;Growth.bValid=true;Purse.Gold=290;Growth.CumulativeXP=9300;
    TestTrue(TEXT("The first valid view after an invalid one only re-primes"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Sample.Progression=nullptr;Sample.Equipment=nullptr;
    TestTrue(TEXT("A missing view says nothing"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Sample.Progression=&Growth;Sample.Equipment=&Purse;Purse.Gold=320;Growth.CumulativeXP=9400;
    TestTrue(TEXT("The first view after a missing one only re-primes"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Purse.Gold=350;Growth.CumulativeXP=9500;
    TestTrue(TEXT("The next reward floats again"),WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::Gold,30},{EKind::Experience,100}}));

    // The next match: its views reset gold to 150 and XP and level to their start, possibly before the owner follows.
    Growth.Generation=4;Growth.Level=1;Growth.EarnedThrough=0;Growth.ConsumedThrough=0;Growth.PendingChoiceCount=0;Growth.CumulativeXP=0;
    Purse.Generation=4;Purse.Gold=150;
    TestTrue(TEXT("Views of a new generation before the owner's match follows are not read"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Sample.Match=4;Sample.PublicGeneration=4;
    TestTrue(TEXT("The new generation's reset only re-primes: nothing floats"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Purse.Gold=180;Growth.CumulativeXP=40;
    TestTrue(TEXT("Its first kill reward floats against the reset baseline"),
        WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::Gold,30},{EKind::Experience,40}}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorldFeedbackKillTest,"Lockdown.Rules.WorldFeedback.Kill",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDWorldFeedbackKillTest::RunTest(const FString& Parameters)
{
    using LDWorldFeedback::EKind;
    LDWorldFeedback::FTracker Tracker;
    const FGuid Self=FGuid::NewGuid(),Rival=FGuid::NewGuid(),Other=FGuid::NewGuid(),Unlisted=FGuid::NewGuid();
    FLDKillFeedView Feed;Feed.Generation=3;
    LDWorldFeedback::FSample Sample=WorldFeedbackTestSample(3,1,Self);Sample.KillFeed=&Feed;
    Sample.RosterName=[Rival,Other](const FGuid& Id){return Id==Rival?FString(TEXT("同学甲")):Id==Other?FString(TEXT("同学乙")):FString();};
    TestTrue(TEXT("The first feed of a match only primes"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Feed.Notices.Add(WorldFeedbackTestNotice(1,Self,Rival,Sample.NowMs-500));
    const FWorldFeedbackTestEvents Kill=WorldFeedbackTestObserve(Tracker,Sample);
    TestTrue(TEXT("A fresh public notice of the own kill confirms it once"),WorldFeedbackTestIs(Kill,{{EKind::Kill,1}})&&Kill[0].bOwn);
    if(Kill.Num()==1)
    {
        TestEqualSensitive(TEXT("The confirmation carries the victim's public roster name"),Kill[0].Name,FString(TEXT("同学甲")));
        TestEqualSensitive(TEXT("The confirmation reads 击败 <name>！"),LDWorldFeedback::Text(Kill[0]),FString(TEXT("击败 同学甲！")));
    }
    TestTrue(TEXT("A notice confirms only once"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Feed.Notices.Add(WorldFeedbackTestNotice(2,Self,Other,Sample.NowMs-LDWorldFeedback::FTracker::KillNoticeFreshMs-1));
    TestTrue(TEXT("An own kill already old when first seen is marked seen silently"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Feed.Notices.Add(WorldFeedbackTestNotice(3,Other,Rival,Sample.NowMs-100));
    TestTrue(TEXT("Someone else's kill is not confirmed"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Feed.Notices.Add(WorldFeedbackTestNotice(4,Other,Self,Sample.NowMs-100));
    Feed.Notices.Add(WorldFeedbackTestNotice(5,Self,Self,Sample.NowMs-100));
    TestTrue(TEXT("The own death is never a kill"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Feed.Notices.Add(WorldFeedbackTestNotice(7,Self,Unlisted,Sample.NowMs-50));
    Feed.Notices.Add(WorldFeedbackTestNotice(6,Self,Other,Sample.NowMs-50));
    const FWorldFeedbackTestEvents Both=WorldFeedbackTestObserve(Tracker,Sample);
    TestTrue(TEXT("Several own kills confirm in serial order"),WorldFeedbackTestIs(Both,{{EKind::Kill,1},{EKind::Kill,1}}));
    if(Both.Num()==2)
    {
        TestEqualSensitive(TEXT("The earlier serial comes first"),Both[0].Name,FString(TEXT("同学乙")));
        TestTrue(TEXT("A victim the roster does not list keeps an empty name, never an invented one"),Both[1].Name.IsEmpty());
        TestEqualSensitive(TEXT("An unlisted victim reads as 对手"),LDWorldFeedback::Text(Both[1]),FString(TEXT("击败 对手！")));
    }
    Sample.bClock=false;Feed.Notices.Add(WorldFeedbackTestNotice(8,Self,Rival,0));
    TestTrue(TEXT("Without an authority clock a new own kill is confirmed, like the HUD kill feed"),
        WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::Kill,1}}));
    Sample.bClock=true;Sample.Self.Invalidate();Feed.Notices.Add(WorldFeedbackTestNotice(9,Self,Rival,Sample.NowMs-100));
    TestTrue(TEXT("Without the own identity nothing is attributed"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Sample.Self=Self;
    TestTrue(TEXT("The waiting notice is confirmed once the identity is known"),WorldFeedbackTestIs(WorldFeedbackTestObserve(Tracker,Sample),{{EKind::Kill,1}}));
    FLDKillFeedView OldFeed;OldFeed.Generation=2;OldFeed.Notices.Add(WorldFeedbackTestNotice(20,Self,Rival,Sample.NowMs-10));
    Sample.KillFeed=&OldFeed;
    TestTrue(TEXT("A feed of another match never confirms"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    Sample.KillFeed=&Feed;Sample.PublicGeneration=4;Feed.Notices.Add(WorldFeedbackTestNotice(10,Self,Rival,Sample.NowMs-10));
    TestTrue(TEXT("A feed that is not the public match's never confirms"),WorldFeedbackTestObserve(Tracker,Sample).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorldFeedbackTextTest,"Lockdown.Rules.WorldFeedback.Text",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDWorldFeedbackTextTest::RunTest(const FString& Parameters)
{
    using LDWorldFeedback::EKind;
    auto Make=[](EKind Kind,int64 Amount){LDWorldFeedback::FEvent Made;Made.Kind=Kind;Made.Amount=Amount;return Made;};
    TestEqualSensitive(TEXT("Damage taken"),LDWorldFeedback::Text(Make(EKind::DamageTaken,37)),FString(TEXT("-37")));
    TestEqualSensitive(TEXT("Damage on a visible unit"),LDWorldFeedback::Text(Make(EKind::UnitDamage,37)),FString(TEXT("-37")));
    TestEqualSensitive(TEXT("Heal"),LDWorldFeedback::Text(Make(EKind::Heal,12)),FString(TEXT("+12")));
    TestEqualSensitive(TEXT("Heal on a visible unit"),LDWorldFeedback::Text(Make(EKind::UnitHeal,12)),FString(TEXT("+12")));
    TestEqualSensitive(TEXT("Shield gained"),LDWorldFeedback::Text(Make(EKind::ShieldGain,20)),FString(TEXT("+20 淡定")));
    TestEqualSensitive(TEXT("Shield absorbed"),LDWorldFeedback::Text(Make(EKind::ShieldAbsorbed,15)),FString(TEXT("-15 淡定")));
    TestEqualSensitive(TEXT("Gold"),LDWorldFeedback::Text(Make(EKind::Gold,45)),FString(TEXT("+45 零花钱")));
    TestEqualSensitive(TEXT("Experience"),LDWorldFeedback::Text(Make(EKind::Experience,60)),FString(TEXT("+60 经验")));
    LDWorldFeedback::FEvent Up=Make(EKind::LevelUp,1);Up.Level=3;Up.bChoiceEarned=true;
    TestEqualSensitive(TEXT("Level-up banner"),LDWorldFeedback::Text(Up),FString(TEXT("升到 3 级！")));
    TestEqualSensitive(TEXT("Level-up subline for one earned choice"),LDWorldFeedback::Subline(Up),FString(TEXT("获得一次成长三选一")));
    Up.Amount=2;Up.Level=4;
    TestEqualSensitive(TEXT("Level-up subline for several earned choices"),LDWorldFeedback::Subline(Up),FString(TEXT("获得 2 次成长三选一")));
    Up.Amount=0;Up.bChoiceEarned=false;
    TestTrue(TEXT("A level-up without an earned choice has no subline"),LDWorldFeedback::Subline(Up).IsEmpty());
    LDWorldFeedback::FEvent Kill=Make(EKind::Kill,1);Kill.Name=TEXT("同学甲");
    TestEqualSensitive(TEXT("Kill banner"),LDWorldFeedback::Text(Kill),FString(TEXT("击败 同学甲！")));
    Kill.Name.Reset();
    TestEqualSensitive(TEXT("Kill banner for an unlisted victim"),LDWorldFeedback::Text(Kill),FString(TEXT("击败 对手！")));
    TestTrue(TEXT("Only a level-up has a subline"),LDWorldFeedback::Subline(Kill).IsEmpty()&&LDWorldFeedback::Subline(Make(EKind::Gold,45)).IsEmpty());
    LDWorldFeedback::FEvent Number=Make(EKind::UnitDamage,37);Number.Name=TEXT("同学甲");Number.Level=7;Number.bChoiceEarned=true;
    TestEqualSensitive(TEXT("A number shows its amount only, never a name or level"),LDWorldFeedback::Text(Number),FString(TEXT("-37")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorldFeedbackCastsTest,"Lockdown.Rules.WorldFeedback.Casts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDWorldFeedbackCastsTest::RunTest(const FString& Parameters)
{
    using LDWorldFeedback::EKind;
    LDWorldFeedback::FTracker Tracker;
    FLDObservedUnitView Rival=WorldFeedbackTestUnit(ELDObservedUnitKind::Contender,50000,60000);
    FLDWorldView Frame=WorldFeedbackTestFrame(3,1,1,1,10.);Frame.Units={Rival};
    LDWorldFeedback::FSample Sample=WorldFeedbackTestSample(3,1,FGuid::NewGuid());Sample.Frame=&Frame;
    Sample.CastName=[](uint8 Ability,uint8 Equipment){return Ability==7?FString(TEXT("扒裤衩")):Equipment==3?FString(TEXT("跑道钉鞋")):FString();};
    auto Step=[&Frame](uint32 Revision,ELDObservedActionCue Cue,uint16 AgeMs,uint8 Ability,uint8 Equipment)
    {
        Frame.Revision=Revision;Frame.SampledAtServerSec=10.+Revision*.1;
        FLDObservedUnitView& Unit=Frame.Units[0];Unit.ActionCue=Cue;Unit.ActionCueAgeMs=AgeMs;Unit.ActionAbility=Ability;Unit.ActionEquipment=Equipment;
    };
    auto Casts=[&Tracker,&Sample]()
    {
        TArray<LDWorldFeedback::FEvent> Out;
        for(const LDWorldFeedback::FEvent& Event:WorldFeedbackTestObserve(Tracker,Sample)){if(Event.Kind==EKind::UnitCast){Out.Add(Event);}}
        return Out;
    };
    Step(1,ELDObservedActionCue::Cast,40,7,0);
    TestTrue(TEXT("A unit first seen mid-cast only primes"),Casts().IsEmpty());
    Step(2,ELDObservedActionCue::Cast,140,7,0);
    TestTrue(TEXT("The same cast seen again stays quiet"),Casts().IsEmpty());
    Step(3,ELDObservedActionCue::None,0,0,0);Casts();
    Step(4,ELDObservedActionCue::Cast,60,7,0);
    const TArray<LDWorldFeedback::FEvent> Named=Casts();
    TestTrue(TEXT("A new cast shows its source's display name once, on that unit, stamped with its frame"),Named.Num()==1&&!Named[0].bOwn
        &&Named[0].Handle==Rival.Handle&&Named[0].SampledAtServerSec==Frame.SampledAtServerSec&&LDWorldFeedback::Text(Named[0])==TEXT("扒裤衩！"));
    Step(5,ELDObservedActionCue::Cast,20,0,3);
    const TArray<LDWorldFeedback::FEvent> Item=Casts();
    TestTrue(TEXT("A restarted cast with another source shows again: an equipment active reads by its equipment name"),
        Item.Num()==1&&LDWorldFeedback::Text(Item[0])==TEXT("跑道钉鞋！"));
    Step(6,ELDObservedActionCue::Attack,10,0,0);
    TestTrue(TEXT("An attack or a hit never names anything"),Casts().IsEmpty());
    Step(7,ELDObservedActionCue::Cast,10,0,0);
    TestTrue(TEXT("A cast without a public source shows nothing"),Casts().IsEmpty());
    LDWorldFeedback::FEvent Unknown;Unknown.Kind=EKind::UnitCast;
    TestEqualSensitive(TEXT("An unknown source reads as a plain cast"),LDWorldFeedback::Text(Unknown),FString(TEXT("施法！")));
    return true;
}
#endif
