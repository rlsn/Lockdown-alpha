#include "Vision/LDViewTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/CoreNet.h"
#include "UObject/UnrealType.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDViewPayloadTest,"Lockdown.Rules.ViewPayload",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDViewPayloadTest::RunTest(const FString& Parameters)
{
    FLDWorldView View; View.Generation=1; View.Revision=23; View.AuthoritySeconds=12;
    View.Current.Init(0,LDVision::MaskBytes); View.Explored.Init(255,LDVision::MaskBytes);
    LDVision::Set(View.Current,2399);
    FLDObservedEntity Entity; Entity.Id=FGuid::NewGuid(); Entity.Position=FVector(-1500,-1000,92.1);
    View.Entities.Init(Entity,LDVision::MaxEntities);
    FNetBitWriter Writer(nullptr,65536); bool bOk=true; View.NetSerialize(Writer,nullptr,bOk);
    TestTrue(TEXT("Bounded maximum view serializes"),bOk && !Writer.IsError() && Writer.GetNumBytes()<4096);
    FNetBitReader Reader(nullptr,Writer.GetData(),Writer.GetNumBits()); FLDWorldView Read; Read.NetSerialize(Reader,nullptr,bOk);
    TestTrue(TEXT("Wire round trip succeeds"),bOk && !Reader.IsError());
    TestEqual(TEXT("Complete entity count"),Read.Entities.Num(),LDVision::MaxEntities);
    TestEqual(TEXT("Opaque ID preserved"),Read.Entities[0].Id,Entity.Id);
    TestTrue(TEXT("Quantized pose preserved"),FVector(Read.Entities[0].Position).Equals(Entity.Position,.11));
    TestTrue(TEXT("Masks survive full replacement"),Read.Current==View.Current);
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
    TestEqual(TEXT("Lower world corner"),LDVision::Cell(FVector(-2400,-1600,0)),0);
    TestEqual(TEXT("Upper edge excluded"),LDVision::Cell(FVector(2400,1600,0)),INDEX_NONE);
    TestEqual(TEXT("Last interior cell"),LDVision::Cell(FVector(2399.9,1599.9,0)),2399);
    TestEqual(TEXT("Exact cell boundary"),LDVision::Cell(FVector(-2320,-1600,0)),1);
    return true;
}
#endif
