#include "Framework/LDMatchLifecycleTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
#include "Framework/LDGameMode.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDViewSchemaTest,"Lockdown.Rules.ViewPrivacySchema",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDViewSchemaTest::RunTest(const FString& Parameters)
{
    // Final Demo GameState: the public room, the authority clock and the public capture/result/kill
    // projections, nothing else replicates there.
    const TSet<FName> Public={TEXT("DemoLobby"),TEXT("DemoClock"),TEXT("PublicMatch"),TEXT("MatchResult"),TEXT("KillFeed")};
    int32 PublicCount=0;
    for(TFieldIterator<FProperty> It(ALDGameState::StaticClass(),EFieldIteratorFlags::ExcludeSuper);It;++It)
    {++PublicCount;TestTrue(TEXT("Only the public room, clock and capture projections are GameState properties"),Public.Contains(It->GetFName()));TestNull(TEXT("No canonical object references"),CastField<FObjectPropertyBase>(*It));}
    TestEqual(TEXT("Public schema complete"),PublicCount,5);
    auto ExpectStruct=[this](UScriptStruct* Struct,const TSet<FName>& Fields,const TCHAR* What)
    {
        int32 Count=0;
        for(TFieldIterator<FProperty> It(Struct);It;++It)
        {++Count;TestTrue(What,Fields.Contains(It->GetFName()));TestNull(TEXT("Public projections carry no canonical object"),CastField<FObjectPropertyBase>(*It));}
        TestEqual(What,Count,Fields.Num());
    };
    ExpectStruct(FLDPublicMatchView::StaticStruct(),{TEXT("Generation"),TEXT("PublicRevision"),TEXT("Phase"),TEXT("ServerNowMs"),
        TEXT("StartedAtMs"),TEXT("OpenAtMs"),TEXT("CircleState"),TEXT("Occupier"),TEXT("CaptureEpoch"),TEXT("SoloSinceMs"),
        TEXT("DueAtMs"),TEXT("PausedProgressMs"),TEXT("ResultId"),TEXT("Airdrop"),TEXT("Leader")},TEXT("The public circle names only the single progress holder and its progress"));
    // D26: the current airdrop names only its public point, landing instant and claimer; never a contender position.
    ExpectStruct(FLDAirdropView::StaticStruct(),{TEXT("Serial"),TEXT("Phase"),TEXT("PointId"),TEXT("PositionCm"),TEXT("LandAtMs"),
        TEXT("ClaimedBy"),TEXT("ClaimedAtMs")},TEXT("The public airdrop names only its public point, landing and claimer"));
    // D27: the leader notice is deliberately public, but only as a fuzzy circle; never the leader's exact position.
    ExpectStruct(FLDLeaderView::StaticStruct(),{TEXT("Leader"),TEXT("CircleCentreCm"),TEXT("RefreshedAtMs"),TEXT("BountyGold")},
        TEXT("The public leader notice names only the leader, a fuzzy circle and the bounty"));
    ExpectStruct(FLDResultView::StaticStruct(),{TEXT("Generation"),TEXT("ResultId"),TEXT("ResultSerial"),TEXT("Reason"),
        TEXT("Winner"),TEXT("StartedAtMs"),TEXT("EndedAtMs"),TEXT("DurationMs"),TEXT("Rows")},TEXT("The frozen result has no ledger or hidden state"));
    ExpectStruct(FLDResultRow::StaticStruct(),{TEXT("ContenderId"),TEXT("Name"),TEXT("Kind"),TEXT("bLeft"),TEXT("Kills"),
        TEXT("Deaths"),TEXT("FinalLevel")},TEXT("Result rows expose no gold, skills, cooldowns, attributes or position"));
    ExpectStruct(FLDKillFeedView::StaticStruct(),{TEXT("Generation"),TEXT("Notices")},TEXT("Kill feed is a bounded public ring"));
    ExpectStruct(FLDKillNotice::StaticStruct(),{TEXT("Serial"),TEXT("Victim"),TEXT("Killer"),TEXT("Cause"),TEXT("OccurredAtMs")},
        TEXT("Kill notices carry no coordinates or build"));
    const FStructProperty* Clock=CastField<FStructProperty>(ALDGameState::StaticClass()->FindPropertyByName(TEXT("DemoClock")));
    if(TestNotNull(TEXT("The authority clock is one struct value"),Clock))
    {
        const TSet<FName> ClockFields={TEXT("Generation"),TEXT("StepSerial"),TEXT("NowMs")};
        int32 ClockCount=0;
        for(TFieldIterator<FProperty> It(Clock->Struct.Get());It;++It)
        {++ClockCount;TestTrue(TEXT("The authority clock carries only match generation, completed step and time"),ClockFields.Contains(It->GetFName()));TestNull(TEXT("The clock has no canonical object reference"),CastField<FObjectPropertyBase>(*It));}
        TestEqual(TEXT("Authority clock schema complete"),ClockCount,3);
    }
    const TSet<FName> Lobby={TEXT("Generation"),TEXT("Phase"),TEXT("RosterRevision"),TEXT("PublicRevision"),TEXT("Members"),TEXT("AICount"),TEXT("CanStart"),TEXT("StartBlocker"),
        // Room facts: the Host runs a DEV test session / DEV content hot reload (#78); every member's screen marks it.
        TEXT("DevTestSession"),TEXT("DevContentReload")};
    int32 LobbyCount=0;
    for(TFieldIterator<FProperty> It(FLDLobbyView::StaticStruct());It;++It)
    {++LobbyCount;TestTrue(TEXT("Demo Lobby contains only public room facts"),Lobby.Contains(It->GetFName()));TestNull(TEXT("Lobby cannot expose canonical objects"),CastField<FObjectPropertyBase>(*It));}
    TestEqual(TEXT("Demo Lobby schema complete"),LobbyCount,10);
    const TSet<FName> Member={TEXT("ContenderId"),TEXT("Name"),TEXT("Kind"),TEXT("Host"),TEXT("Ready")};
    int32 MemberCount=0;
    for(TFieldIterator<FProperty> It(FLDLobbyMember::StaticStruct());It;++It)
    {++MemberCount;TestTrue(TEXT("Roster never includes private build or body data"),Member.Contains(It->GetFName()));}
    TestEqual(TEXT("Demo member schema complete"),MemberCount,5);
    return true;
}
#endif
