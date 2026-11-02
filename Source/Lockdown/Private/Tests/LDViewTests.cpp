#include "Framework/LDMatchLifecycleTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
#include "Framework/LDGameMode.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDViewSchemaTest,"Lockdown.Rules.ViewPrivacySchema",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDViewSchemaTest::RunTest(const FString& Parameters)
{
    // Final Demo GameState: the public room and the authority clock, nothing else replicates there.
    const TSet<FName> Public={TEXT("DemoLobby"),TEXT("DemoClock")};
    int32 PublicCount=0;
    for(TFieldIterator<FProperty> It(ALDGameState::StaticClass(),EFieldIteratorFlags::ExcludeSuper);It;++It)
    {++PublicCount;TestTrue(TEXT("Only the public room and authority clock are GameState properties"),Public.Contains(It->GetFName()));TestNull(TEXT("No canonical object references"),CastField<FObjectPropertyBase>(*It));}
    TestEqual(TEXT("Public schema complete"),PublicCount,2);
    const FStructProperty* Clock=CastField<FStructProperty>(ALDGameState::StaticClass()->FindPropertyByName(TEXT("DemoClock")));
    if(TestNotNull(TEXT("The authority clock is one struct value"),Clock))
    {
        const TSet<FName> ClockFields={TEXT("Generation"),TEXT("StepSerial"),TEXT("NowMs")};
        int32 ClockCount=0;
        for(TFieldIterator<FProperty> It(Clock->Struct.Get());It;++It)
        {++ClockCount;TestTrue(TEXT("The authority clock carries only match generation, completed step and time"),ClockFields.Contains(It->GetFName()));TestNull(TEXT("The clock has no canonical object reference"),CastField<FObjectPropertyBase>(*It));}
        TestEqual(TEXT("Authority clock schema complete"),ClockCount,3);
    }
    const TSet<FName> Lobby={TEXT("Generation"),TEXT("Phase"),TEXT("RosterRevision"),TEXT("PublicRevision"),TEXT("Members"),TEXT("AICount"),TEXT("CanStart"),TEXT("StartBlocker")};
    int32 LobbyCount=0;
    for(TFieldIterator<FProperty> It(FLDLobbyView::StaticStruct());It;++It)
    {++LobbyCount;TestTrue(TEXT("Demo Lobby contains only public room facts"),Lobby.Contains(It->GetFName()));TestNull(TEXT("Lobby cannot expose canonical objects"),CastField<FObjectPropertyBase>(*It));}
    TestEqual(TEXT("Demo Lobby schema complete"),LobbyCount,8);
    const TSet<FName> Member={TEXT("ContenderId"),TEXT("Name"),TEXT("Kind"),TEXT("Host"),TEXT("Ready")};
    int32 MemberCount=0;
    for(TFieldIterator<FProperty> It(FLDLobbyMember::StaticStruct());It;++It)
    {++MemberCount;TestTrue(TEXT("Roster never includes private build or body data"),Member.Contains(It->GetFName()));}
    TestEqual(TEXT("Demo member schema complete"),MemberCount,5);
    return true;
}
#endif
