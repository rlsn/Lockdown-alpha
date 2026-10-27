#include "Vision/LDViewTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/CoreNet.h"
#include "UObject/UnrealType.h"
#include "Framework/LDGameMode.h"
#include "World/LDEnvironmentDirector.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDViewPayloadTest,"Lockdown.Rules.ViewPayload",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDViewPayloadTest::RunTest(const FString& Parameters)
{
    FLDWorldView View; View.Generation=1; View.Revision=23; View.AuthoritySeconds=12;
    View.Current.Init(0,LDVision::MaskBytes); View.Explored.Init(255,LDVision::MaskBytes);
    LDVision::Set(View.Current,2399);
    FLDObservedEntity Entity; Entity.Id=FGuid::NewGuid(); Entity.Position=FVector(-1500,-1000,92.1);Entity.Kind=ELDObservedKind::Seal;
    View.Entities.Init(Entity,128);FNetBitWriter LegacyWriter(nullptr,65536);bool LegacyOk=true;View.NetSerialize(LegacyWriter,nullptr,LegacyOk);
    TestTrue(TEXT("Retained128-record view remains below8KiB"),LegacyOk&&!LegacyWriter.IsError()&&LegacyWriter.GetNumBytes()<8192);
    View.Entities.Init(Entity,LDVision::MaxEntities);
    FNetBitWriter Writer(nullptr,65536); bool bOk=true; View.NetSerialize(Writer,nullptr,bOk);
    TestTrue(TEXT("512-record bounded maximum view fits32KiB"),bOk && !Writer.IsError() && Writer.GetNumBytes()<32768);
    AddInfo(FString::Printf(TEXT("Maximum authorized512-tuple payload: %lld bytes"),int64(Writer.GetNumBytes())));
    FNetBitReader Reader(nullptr,Writer.GetData(),Writer.GetNumBits()); FLDWorldView Read; Read.NetSerialize(Reader,nullptr,bOk);
    TestTrue(TEXT("Wire round trip succeeds"),bOk && !Reader.IsError());
    TestEqual(TEXT("Complete entity count"),Read.Entities.Num(),LDVision::MaxEntities);
    TestEqual(TEXT("Opaque ID preserved"),Read.Entities[0].Id,Entity.Id);
    TestTrue(TEXT("Sanitized dropped Seal kind round trips"),Read.Entities[0].Kind==ELDObservedKind::Seal);
    TestTrue(TEXT("Quantized pose preserved"),FVector(Read.Entities[0].Position).Equals(Entity.Position,.11));
    TestTrue(TEXT("Masks survive full replacement"),Read.Current==View.Current);
    View.Entities[0].Kind=ELDObservedKind::Book;FNetBitWriter BookWriter(nullptr,65536);View.NetSerialize(BookWriter,nullptr,bOk);
    FNetBitReader BookReader(nullptr,BookWriter.GetData(),BookWriter.GetNumBits());FLDWorldView BookRead;BookRead.NetSerialize(BookReader,nullptr,bOk);
    TestTrue(TEXT("Generic book kind round trips without extra fields"),bOk&&BookRead.Entities[0].Kind==ELDObservedKind::Book);
    View.Entities[0].Kind=ELDObservedKind::Charter;View.Entities[0].Presentation=3;FNetBitWriter TaskWriter(nullptr,65536);View.NetSerialize(TaskWriter,nullptr,bOk);FNetBitReader TaskReader(nullptr,TaskWriter.GetData(),TaskWriter.GetNumBits());FLDWorldView TaskRead;TaskRead.NetSerialize(TaskReader,nullptr,bOk);TestTrue(TEXT("Public Charter kind fits the unchanged schema"),bOk&&TaskRead.Entities[0].Kind==ELDObservedKind::Charter&&TaskRead.Entities[0].Presentation==3);
    View.Entities[0].Kind=ELDObservedKind::Production;View.Entities[0].Presentation=8;View.Entities[0].ActionCue=0;FNetBitWriter ProductionWriter(nullptr,65536);View.NetSerialize(ProductionWriter,nullptr,bOk);FNetBitReader ProductionReader(nullptr,ProductionWriter.GetData(),ProductionWriter.GetNumBits());FLDWorldView ProductionRead;ProductionRead.NetSerialize(ProductionReader,nullptr,bOk);TestTrue(TEXT("Ruin service round trips within unchanged eight-field tuple"),bOk&&ProductionRead.Entities[0].Kind==ELDObservedKind::Production&&ProductionRead.Entities[0].Presentation==8);
    View.Entities[0].Presentation=9;FNetBitWriter InvalidSite(nullptr,65536);View.NetSerialize(InvalidSite,nullptr,bOk);TestFalse(TEXT("Unknown production site rejected"),bOk);View.Entities[0].Presentation=0;View.Entities[0].ActionCue=4;FNetBitWriter InvalidCharges(nullptr,65536);View.NetSerialize(InvalidCharges,nullptr,bOk);TestFalse(TEXT("Outcrop cue above finite budget rejected"),bOk);View.Entities[0].ActionCue=3;
    View.Entities[0].Kind=ELDObservedKind::Institution;View.Entities[0].Presentation=4;View.Entities[0].ActionCue=5;FNetBitWriter InstitutionWriter(nullptr,65536);View.NetSerialize(InstitutionWriter,nullptr,bOk);FNetBitReader InstitutionReader(nullptr,InstitutionWriter.GetData(),InstitutionWriter.GetNumBits());FLDWorldView InstitutionRead;InstitutionRead.NetSerialize(InstitutionReader,nullptr,bOk);TestTrue(TEXT("Official institution public state fits unchanged eight-field tuple"),bOk&&InstitutionRead.Entities[0].Kind==ELDObservedKind::Institution&&InstitutionRead.Entities[0].ActionCue==5);View.Entities[0].ActionCue=6;FNetBitWriter InvalidRoutine(nullptr,65536);View.NetSerialize(InvalidRoutine,nullptr,bOk);TestFalse(TEXT("Unknown institution state rejected"),bOk);View.Entities[0].ActionCue=0;
    View.Entities.Add(Entity); FNetBitWriter Oversized(nullptr,65536); View.NetSerialize(Oversized,nullptr,bOk);
    TestFalse(TEXT("Pathological view count rejected"),bOk);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDViewSchemaTest,"Lockdown.Rules.ViewPrivacySchema",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDViewSchemaTest::RunTest(const FString& Parameters)
{
    const TSet<FName> Allowed={TEXT("Id"),TEXT("Position"),TEXT("Facing"),TEXT("Speed"),TEXT("Presentation"),TEXT("Kind"),TEXT("ActionCue"),TEXT("ExposedUntil")};
    int32 Count=0;
    for(TFieldIterator<FProperty> It(FLDObservedEntity::StaticStruct());It;++It)
    { ++Count; TestTrue(TEXT("Only approved observed fields"),Allowed.Contains(It->GetFName())); TestNull(TEXT("No canonical UObject references"),CastField<FObjectPropertyBase>(*It)); }
    TestEqual(TEXT("Approved schema complete"),Count,8);
    const TSet<FName> Public={TEXT("bEnvironmentProfile"),TEXT("bPublicSensorActive"),TEXT("DefensiveFlags"),TEXT("bClaimActive"),TEXT("bContested"),TEXT("ClaimRoute"),TEXT("ClaimProgress"),TEXT("MatchDeadline"),TEXT("EndReason"),TEXT("WinningRoute"),TEXT("MatchEndedAt"),TEXT("ResultReceipt"),TEXT("ResultSerial"),TEXT("SealNoticeSerial"),TEXT("SealNoticeKind"),TEXT("SealNoticeDistrict"),TEXT("bDevelopmentProfile"),TEXT("bUIControlProfile"),TEXT("bMatchStarted"),TEXT("LobbyCount"),TEXT("ReadyCount"),TEXT("PublicSensorOfflineUntil"),TEXT("PublicSensorLockoutUntil"),TEXT("OutcropOne"),TEXT("OutcropTwo"),TEXT("EconomyNextTick"),TEXT("ShortagePhase"),TEXT("ShortageRemaining"),TEXT("CoreOrdersRemaining"),TEXT("SupplyOrdersRemaining"),TEXT("CharterWindow"),TEXT("CharterPhase"),TEXT("CharterWarningAt"),TEXT("CharterOpenAt"),TEXT("CharterCloseAt"),TEXT("Breaches")};
    int32 PublicCount=0;
    for(TFieldIterator<FProperty> It(ALDGameState::StaticClass(),EFieldIteratorFlags::ExcludeSuper);It;++It)
    {++PublicCount;TestTrue(TEXT("Only approved public objective facts"),Public.Contains(It->GetFName()));TestNull(TEXT("No claimant/carrier canonical references"),CastField<FObjectPropertyBase>(*It));}
    TestEqual(TEXT("Public schema complete"),PublicCount,36);
    const TSet<FName> FullMap={TEXT("Handle"),TEXT("PublicDoor"),TEXT("Position"),TEXT("Revision"),TEXT("State"),TEXT("Presentation"),TEXT("Facing"),TEXT("Light")};
    int32 FullCount=0;for(TFieldIterator<FProperty> It(FLDEnvironmentViewEntry::StaticStruct());It;++It)
    {++FullCount;TestTrue(TEXT("Full-map view contains only approved observed fields"),FullMap.Contains(It->GetFName()));TestNull(TEXT("Full-map view has no canonical object reference"),CastField<FObjectPropertyBase>(*It));}
    TestEqual(TEXT("Full-map observed schema complete"),FullCount,8);
    const TSet<FName> Objects={TEXT("Handle"),TEXT("Kind"),TEXT("Position"),TEXT("Revision"),TEXT("State"),TEXT("PhysicalRevision")};
    int32 ObjectCount=0;for(TFieldIterator<FProperty> It(FLDObjectView::StaticStruct());It;++It)
    {++ObjectCount;TestTrue(TEXT("World-object view contains only public observed fields"),Objects.Contains(It->GetFName()));TestNull(TEXT("World-object view has no canonical actor reference"),CastField<FObjectPropertyBase>(*It));}
    TestEqual(TEXT("World-object observed schema complete"),ObjectCount,6);
    FVector_NetQuantize10 Boundary=FVector(-118000,118000,1292.15);FNetBitWriter FullWriter(nullptr,1024);bool FullOk=true;Boundary.NetSerialize(FullWriter,nullptr,FullOk);
    FVector_NetQuantize10 BoundaryRead;FNetBitReader FullReader(nullptr,FullWriter.GetData(),FullWriter.GetNumBits());BoundaryRead.NetSerialize(FullReader,nullptr,FullOk);
    TestTrue(TEXT("Full-map boundary pose fits native quantization"),FullOk&&!FullReader.IsError()&&FVector(BoundaryRead).Equals(Boundary,.11));
    TestEqual(TEXT("Lower world corner"),LDVision::Cell(FVector(-2400,-1600,0)),0);
    TestEqual(TEXT("Upper edge excluded"),LDVision::Cell(FVector(2400,1600,0)),INDEX_NONE);
    TestEqual(TEXT("Last interior cell"),LDVision::Cell(FVector(2399.9,1599.9,0)),2399);
    TestEqual(TEXT("Exact cell boundary"),LDVision::Cell(FVector(-2320,-1600,0)),1);
    return true;
}
#endif
