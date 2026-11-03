#include "Audio/LDAudioCueTracker.h"
#include "Audio/LDAudioTypes.h"
#include "Framework/LDCombatReceiptLog.h"
#include "Misc/AutomationTest.h"
#include "UI/LDHUDModel.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
FLDCombatActionReceipt BurstReceipt(uint64 Sequence,ELDCombatEntry Entry,ELDCombatPhase Phase,ELDCombatReject Reject)
{
    FLDCombatActionReceipt Receipt;Receipt.Generation=4;Receipt.Life=1;Receipt.Origin=ELDCombatRequestOrigin::Direct;
    Receipt.DirectSequence=Sequence;Receipt.Entry=Entry;Receipt.Phase=Phase;Receipt.Reject=Reject;
    return Receipt;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatReceiptBurstTest,"Lockdown.Rules.CombatReceiptBurst",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDCombatReceiptBurstTest::RunTest(const FString& Parameters)
{
    // Issue #14: two receipts arriving in one frame. Q's release is followed by W's windup before the HUD samples.
    FLDCombatReceiptLog Log;
    uint32 Seen=Log.Serial();
    TestTrue(TEXT("An empty log has no receipt"),Seen==0&&!Log.Latest());
    Log.Push(BurstReceipt(10,ELDCombatEntry::Q,ELDCombatPhase::Finished,ELDCombatReject::None));
    Log.Push(BurstReceipt(11,ELDCombatEntry::W,ELDCombatPhase::Accepted,ELDCombatReject::None));
    TArray<ELDAudioCue> Out;
    {
        FLDAudioCueTracker NewestOnly;
        NewestOnly.ObserveCombatReceipt(*Log.Latest(),4,1,Out);
        TestTrue(TEXT("Reading only the newest receipt misses the earlier release"),Out.IsEmpty());
    }
    FLDAudioCueTracker Tracker;
    TArray<FLDCombatActionReceipt> Arrived;
    TestEqual(TEXT("Nothing left the window"),Log.ReadSince(Seen,Arrived),0u);
    TestTrue(TEXT("Both receipts arrive oldest first and the cursor reaches the newest"),Seen==2&&Arrived.Num()==2
        &&Arrived[0].DirectSequence==10&&Arrived[1].DirectSequence==11);
    for(const FLDCombatActionReceipt& Receipt:Arrived){Tracker.ObserveCombatReceipt(Receipt,4,1,Out);}
    TestTrue(TEXT("The earlier request's release sounds"),Out==TArray<ELDAudioCue>({ELDAudioCue::SelfCast}));
    Arrived.Reset();Out.Reset();
    Log.ReadSince(Seen,Arrived);
    TestTrue(TEXT("A read cursor never replays a receipt"),Arrived.IsEmpty()&&Seen==2);

    // A second presenter keeps its own cursor and still reads everything after it.
    uint32 Late=1;
    Log.ReadSince(Late,Arrived);
    TestTrue(TEXT("Each presenter reads from its own cursor"),Late==2&&Arrived.Num()==1&&Arrived[0].DirectSequence==11);
    Arrived.Reset();

    Log.Push(BurstReceipt(11,ELDCombatEntry::W,ELDCombatPhase::Finished,ELDCombatReject::None));
    Log.Push(BurstReceipt(12,ELDCombatEntry::Item1,ELDCombatPhase::Released,ELDCombatReject::None));
    Log.ReadSince(Seen,Arrived);
    for(const FLDCombatActionReceipt& Receipt:Arrived){Tracker.ObserveCombatReceipt(Receipt,4,1,Out);}
    TestTrue(TEXT("Two releases in one frame are one mixed cue"),Out==TArray<ELDAudioCue>({ELDAudioCue::SelfCast}));
    Out.Reset();
    for(const FLDCombatActionReceipt& Receipt:Arrived){Tracker.ObserveCombatReceipt(Receipt,4,1,Out);}
    TestTrue(TEXT("A release already sounded never sounds again"),Out.IsEmpty());

    const uint32 Mark=Log.Serial();
    TestNull(TEXT("Nothing after the mark matches"),Log.FindLatestSince(Mark,[](const FLDCombatActionReceipt&){return true;}));
    FLDCombatActionReceipt Refused=BurstReceipt(13,ELDCombatEntry::Q,ELDCombatPhase::Rejected,ELDCombatReject::Cooldown);
    Log.Push(Refused);
    Log.Push(BurstReceipt(14,ELDCombatEntry::W,ELDCombatPhase::Accepted,ELDCombatReject::None));
    const FLDCombatActionReceipt* Found=Log.FindLatestSince(Mark,[](const FLDCombatActionReceipt& Receipt){return Receipt.Phase==ELDCombatPhase::Rejected;});
    TestTrue(TEXT("A refusal followed by another receipt is still found after its mark"),Found&&Found->DirectSequence==13);

    // The toast line: every arrived receipt tells its notice once, in order; the lost protection once per life revision.
    TArray<FLDCombatActionReceipt> Frame;
    FLDCombatActionReceipt Broke=BurstReceipt(20,ELDCombatEntry::Item1,ELDCombatPhase::Rejected,ELDCombatReject::Cooldown);
    Broke.bProtectionRemoved=true;Broke.LifeRevision=5;
    FLDCombatActionReceipt Again=Broke;Again.DirectSequence=22;
    FLDCombatActionReceipt Range=BurstReceipt(21,ELDCombatEntry::W,ELDCombatPhase::Rejected,ELDCombatReject::OutOfRange);
    FLDCombatActionReceipt Foreign=Range;Foreign.Generation=3;Foreign.Reject=ELDCombatReject::Dead;
    Frame={Broke,Again,Foreign,Range};
    uint64 Told=0;
    TestEqualSensitive(TEXT("Each distinct notice of the frame is told once in arrival order"),LDHUD::CombatReceiptsNotice(Frame,4,1,Told),
        TEXT("装备 1 未执行：")+LDHUD::CombatRejectText(ELDCombatReject::Cooldown)+TEXT(" · 复活保护已解除 · 角色技能 2 未执行：")
        +LDHUD::CombatRejectText(ELDCombatReject::OutOfRange));
    TestEqual(TEXT("The told protection is remembered"),Told,uint64(5));
    Frame={Broke};
    TestEqualSensitive(TEXT("The same lost protection is not told twice"),LDHUD::CombatReceiptsNotice(Frame,4,1,Told),
        TEXT("装备 1 未执行：")+LDHUD::CombatRejectText(ELDCombatReject::Cooldown));
    Frame={Foreign};
    TestTrue(TEXT("Another match's receipt tells nothing"),LDHUD::CombatReceiptsNotice(Frame,4,1,Told).IsEmpty());

    // A cursor that fell more than the capacity behind learns how many receipts it missed; Clear keeps every cursor valid.
    FLDCombatReceiptLog Full;
    uint32 Behind=0;
    for(int32 Index=0;Index<FLDCombatReceiptLog::Capacity+3;++Index){Full.Push(BurstReceipt(100+Index,ELDCombatEntry::Q,ELDCombatPhase::Accepted,ELDCombatReject::None));}
    Arrived.Reset();
    TestEqual(TEXT("Receipts pushed out of the window are counted"),Full.ReadSince(Behind,Arrived),3u);
    TestTrue(TEXT("The kept window starts after the evicted receipts"),Arrived.Num()==FLDCombatReceiptLog::Capacity
        &&Arrived[0].DirectSequence==103&&Behind==Full.Serial());
    Full.Clear();
    Arrived.Reset();
    TestTrue(TEXT("Clear forgets the receipts but keeps the serial"),Full.Serial()==uint32(FLDCombatReceiptLog::Capacity+3)&&!Full.Latest()
        &&Full.ReadSince(Behind,Arrived)==0&&Arrived.IsEmpty());
    Full.Push(BurstReceipt(200,ELDCombatEntry::Q,ELDCombatPhase::Accepted,ELDCombatReject::None));
    Full.ReadSince(Behind,Arrived);
    TestTrue(TEXT("A receipt after Clear continues the serial"),Arrived.Num()==1&&Arrived[0].DirectSequence==200&&Behind==Full.Serial());
    return true;
}
#endif
