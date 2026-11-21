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
        TEXT("DueAtMs"),TEXT("PausedProgressMs"),TEXT("GraffitiOwner"),TEXT("GraffitiPresentation"),TEXT("GraffitiCause"),TEXT("GuardDueAtMs"),TEXT("GuardKeptMs"),
        TEXT("ResultId"),TEXT("Airdrop"),TEXT("Leader"),TEXT("Boss"),TEXT("Markers"),TEXT("Timeline"),TEXT("PK"),
        TEXT("Showdown"),TEXT("OwnerBankedGuardMs"),TEXT("PoleCentreCm"),TEXT("PoleRadiusCm"),TEXT("FadeAtMs"),
        TEXT("CageUntilMs"),TEXT("CageCentreCm"),TEXT("CageRadiusCm"),TEXT("CageIntegrityBp"),TEXT("RecoveryOrbs"),TEXT("FinalMods"),TEXT("PrivatePoles"),TEXT("DailyModifiers"),TEXT("Objective"),TEXT("Lockers")},TEXT("The public flagpole carries only public objective and timeline state"));
    ExpectStruct(FLDLockerView::StaticStruct(),{TEXT("AnchorId"),TEXT("PositionCm"),TEXT("ClosesAtT"),TEXT("bChanneling")},
        TEXT("A public locker carries its place, closing time and whether it is held, never who holds it"));
    ExpectStruct(FLDShowdownView::StaticStruct(),{TEXT("bStarted"),TEXT("StartedAtMs"),TEXT("EndsAtMs"),
        TEXT("RespawnsCloseAtMs"),TEXT("ClosureAtMs"),TEXT("SprayMs"),TEXT("GuardMs"),TEXT("CentreCm"),
        TEXT("InitialRadiusCm"),TEXT("FinalRadiusCm")},TEXT("The public showdown exposes clocks and arena geometry, never hidden contenders or builds"));
    // P6: the public objective names its place, window and the mini flag's sole sprayer and winner; never HP or builds.
    ExpectStruct(FLDObjectiveView::StaticStruct(),{TEXT("Serial"),TEXT("Kind"),TEXT("Phase"),TEXT("AnchorId"),TEXT("CentreCm"),
        TEXT("RadiusCm"),TEXT("OpensAtT"),TEXT("ClosesAtT"),TEXT("Sprayer"),TEXT("SprayMs"),TEXT("SprayNeedMs"),TEXT("bContested"),
        TEXT("Winner")},TEXT("The public objective carries no HP, build or hidden state"));
    // D26: the current 天降外卖 names only its public point, landing and expiry instants; never its claimer or claim time (§6.3).
    ExpectStruct(FLDAirdropView::StaticStruct(),{TEXT("Serial"),TEXT("Phase"),TEXT("PointId"),TEXT("PositionCm"),TEXT("LandAtMs"),
        TEXT("ExpireAtMs")},TEXT("The public airdrop names only its public point, landing and expiry"));
    // #198 outer §5: a camp marker names its segment, kind, camp, phase and instants; never who took it.
    ExpectStruct(FLDFieldMarkerView::StaticStruct(),{TEXT("Segment"),TEXT("Kind"),TEXT("CampId"),TEXT("PositionCm"),TEXT("Phase"),
        TEXT("RevealAtMs"),TEXT("EndAtMs")},TEXT("The public camp marker never names its claimer"));
    // D27: the leader notice is deliberately public, but only as a fuzzy circle; never the leader's exact position.
    // Spec §11.1 rumor day: the runner-up is named the same way, with its own fuzzy circle.
    ExpectStruct(FLDLeaderView::StaticStruct(),{TEXT("Leader"),TEXT("LeaderLevel"),TEXT("CircleCentreCm"),TEXT("RefreshedAtMs"),TEXT("RunnerUp"),
        TEXT("RunnerUpCentreCm")},TEXT("The public leader notice names only the trending contenders, the leader's level and their fuzzy circles"));
    // D26: the Boss view names only its phase, its appearance and leave times and the last hit; its body is under fog of war.
    ExpectStruct(FLDBossView::StaticStruct(),{TEXT("Phase"),TEXT("AppearAtMs"),TEXT("LeaveAtMs"),TEXT("KilledBy"),TEXT("DownAtMs")},
        TEXT("The public Boss view never carries the Boss's position or health"));
    ExpectStruct(FLDResultView::StaticStruct(),{TEXT("Generation"),TEXT("ResultId"),TEXT("ResultSerial"),TEXT("Reason"),
        TEXT("Winner"),TEXT("StartedAtMs"),TEXT("EndedAtMs"),TEXT("DurationMs"),TEXT("Rows")},TEXT("The frozen result has no ledger or hidden state"));
    ExpectStruct(FLDResultRow::StaticStruct(),{TEXT("ContenderId"),TEXT("Name"),TEXT("Kind"),TEXT("bLeft"),TEXT("Kills"),
        TEXT("Deaths"),TEXT("Assists"),TEXT("FinalLevel"),TEXT("BankedGuardMs"),TEXT("CompletedSprays"),TEXT("Build")},
        TEXT("Result rows carry settled totals and frozen rule identities, never live resources, cooldowns, attributes or position"));
    ExpectStruct(FLDResultBuild::StaticStruct(),{TEXT("bAvailable"),TEXT("CatalogRoot"),TEXT("Actives"),TEXT("Cards"),
        TEXT("FinalMod"),TEXT("StatPoints")},
        TEXT("Frozen build summaries contain only catalog, installed rule identities and settled stat points"));
    ExpectStruct(FLDResultBuildActive::StaticStruct(),{TEXT("DefinitionId"),TEXT("Slot"),TEXT("Branch")},
        TEXT("Frozen active summaries contain only their identity, slot and learned branch"));
    ExpectStruct(FLDResultBuildCard::StaticStruct(),{TEXT("DefinitionId"),TEXT("Target"),TEXT("TargetId"),TEXT("Element")},
        TEXT("Frozen card summaries contain only their identity, place and element"));
    ExpectStruct(FLDKillFeedView::StaticStruct(),{TEXT("Generation"),TEXT("Notices"),TEXT("Scores")},TEXT("Kill feed is a bounded public ring with the public K/D/A"));
    ExpectStruct(FLDScoreRow::StaticStruct(),{TEXT("ContenderId"),TEXT("Kills"),TEXT("Deaths"),TEXT("Assists")},
        TEXT("Scoreboard rows carry only the public kills, deaths and assists"));
    ExpectStruct(FLDKillNotice::StaticStruct(),{TEXT("Serial"),TEXT("Victim"),TEXT("Killer"),TEXT("Assisters"),TEXT("Cause"),TEXT("OccurredAtMs"),TEXT("CreatureName")},
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
    const TSet<FName> Lobby={TEXT("Generation"),TEXT("Phase"),TEXT("RosterRevision"),TEXT("PublicRevision"),TEXT("PreviousResultId"),TEXT("Members"),TEXT("AICount"),TEXT("CanStart"),TEXT("StartBlocker"),
        // Room facts: the Host runs a DEV test session / DEV content hot reload (#78); every member's screen marks it.
        TEXT("DevTestSession"),TEXT("DevContentReload")};
    int32 LobbyCount=0;
    for(TFieldIterator<FProperty> It(FLDLobbyView::StaticStruct());It;++It)
    {++LobbyCount;TestTrue(TEXT("Demo Lobby contains only public room facts"),Lobby.Contains(It->GetFName()));TestNull(TEXT("Lobby cannot expose canonical objects"),CastField<FObjectPropertyBase>(*It));}
    TestEqual(TEXT("Demo Lobby schema complete"),LobbyCount,11);
    const TSet<FName> Member={TEXT("ContenderId"),TEXT("Name"),TEXT("Kind"),TEXT("Host"),TEXT("Ready"),TEXT("PreviousAIContender")};
    int32 MemberCount=0;
    for(TFieldIterator<FProperty> It(FLDLobbyMember::StaticStruct());It;++It)
    {++MemberCount;TestTrue(TEXT("Roster carries public room and previous-result identities, never private build or body data"),Member.Contains(It->GetFName()));}
    TestEqual(TEXT("Demo member schema complete"),MemberCount,6);
    {
        FLDResultBuild BuildCheck;
        BuildCheck.bAvailable=true;
        BuildCheck.CatalogRoot=FString::ChrN(64,TEXT('a'));
        BuildCheck.StatPoints={260,0,18,0,0};
        FString BuildError;
        TestTrue(TEXT("Result build accepts >255 int32 stat points without clamping"),LDCapture::ValidateResultBuild(BuildCheck,BuildError));
        BuildCheck.StatPoints[1]=-1;
        TestFalse(TEXT("Result build rejects negative stat point counts"),LDCapture::ValidateResultBuild(BuildCheck,BuildError));
    }
    return true;
}
#endif
