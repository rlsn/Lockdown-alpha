#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDGameInstance.h"
#include "Framework/LDPlayerController.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/SpectatorPawn.h"
#include "EngineUtils.h"
#include "Region/LDMapRegistry.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "Tests/LDDemoEntryProbe.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoEntryTest, "Lockdown.Rules.DemoEntryContract", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoEntryTest::RunTest(const FString& Parameters)
{
    FString Error;
    TestTrue(TEXT("Frontend uses the only menu root"), LDDemoEntry::ValidateSelection(LDDemoEntry::FrontendMap, TEXT(""), Error));
    TestTrue(TEXT("Demo uses the only gameplay root"), LDDemoEntry::ValidateSelection(LDDemoEntry::WorldMap, TEXT("?listen"), Error));
    TestTrue(TEXT("PIE identity is stripped without changing the package root"),
        LDDemoEntry::ValidateSelection(TEXT("/Game/Lockdown/Generated/Maps/UEDPIE_2_L_Demo"), TEXT("?listen"), Error));
    for (const TCHAR* Profile : {TEXT("V03"), TEXT("V04"), TEXT("V05"), TEXT("v06_regression"), TEXT("v07_integration"),
        TEXT("v08_integration"), TEXT("v09_full"), TEXT("v09_environment"), TEXT("v09_population"), TEXT("Demo"), TEXT("")})
    {
        TestFalse(TEXT("No legacy or alternate profile selection"), LDDemoEntry::ValidateSelection(LDDemoEntry::WorldMap,
            TEXT("?Profile=") + FString(Profile), Error));
        TestTrue(TEXT("Rejected profile names the retired option"), Error.Contains(TEXT("Profile")) && Error.Contains(TEXT("已退役")));
    }
    TestFalse(TEXT("Old capacity override is rejected"), LDDemoEntry::ValidateSelection(LDDemoEntry::WorldMap, TEXT("?Humans=12"), Error));
    TestFalse(TEXT("URL-selected GameMode cannot bypass the entry contract"),
        LDDemoEntry::ValidateSelection(LDDemoEntry::FrontendMap, TEXT("?game=/Script/Engine.GameModeBase"), Error));
    TestFalse(TEXT("Frontend cannot become a hidden gameplay room"), LDDemoEntry::ValidateSelection(LDDemoEntry::FrontendMap, TEXT("?listen"), Error));
    for (const TCHAR* Map : {TEXT("/Game/Lockdown/Generated/Maps/L_DevelopmentRegion"),
        TEXT("/Game/Lockdown/Generated/Maps/L_V08Region"), TEXT("/Game/Lockdown/Generated/V09/L_V09Full"),
        TEXT("/Game/Other/L_Demo"), TEXT("L_Demo"), TEXT("/Game/Lockdown/Generated/Maps/L_Demo_Backup")})
    {
        TestFalse(TEXT("Map aliases and legacy roots are rejected"), LDDemoEntry::ValidateSelection(Map, TEXT(""), Error));
    }
    for (const TCHAR* Address : {TEXT("127.0.0.1"), TEXT("127.0.0.1:7777"), TEXT("host.example:65535"), TEXT("localhost:1")})
    {
        TestTrue(TEXT("Reachable address shapes are accepted without promising a connection"), LDDemoEntry::ValidateAddress(Address, Error));
    }
    for (const TCHAR* Address : {TEXT(""), TEXT("host:0"), TEXT("host:65536"), TEXT("host:999999"),
        TEXT("host?Profile=V05"), TEXT("host/L_Demo"), TEXT("https://host"), TEXT("host\n"), TEXT("host name")})
    {
        TestFalse(TEXT("Address cannot inject travel options or an invalid port"), LDDemoEntry::ValidateAddress(Address, Error));
    }
    // A Host's PreLogin refusal travels as "<Code>: <English detail>"; only a known code becomes the shown reason.
    const TPair<const TCHAR*, const TCHAR*> Refusals[] = {
        {LDDemoEntry::JoinRefusal::MatchAlreadyStarted, TEXT("对局已开始")}, {LDDemoEntry::JoinRefusal::RoomFull, TEXT("房间已满")},
        {LDDemoEntry::JoinRefusal::ProtocolMismatch, TEXT("网络协议不同")}, {LDDemoEntry::JoinRefusal::ProductMismatch, TEXT("产品版本不同")},
        {LDDemoEntry::JoinRefusal::ContentMismatch, TEXT("内容数据不同")}, {LDDemoEntry::JoinRefusal::RoomNotReady, TEXT("尚未就绪")},
        {LDDemoEntry::JoinRefusal::InvalidName, TEXT("玩家名称")}};
    TSet<FString> RefusalReasons;
    for (const TPair<const TCHAR*, const TCHAR*>& Refusal : Refusals)
    {
        FString Reason;
        TestTrue(TEXT("A coded refusal maps to its own reason, naming the code but not the English detail"),
            LDDemoEntry::DescribeJoinRefusal(LDDemoEntry::MakeJoinRefusal(Refusal.Key, TEXT("English detail")), Reason)
            && Reason.Contains(Refusal.Value) && Reason.Contains(FString::Printf(TEXT("（%s）"), Refusal.Key))
            && !Reason.Contains(TEXT("English detail")));
        RefusalReasons.Add(Reason);
    }
    TestEqual(TEXT("Every refusal code has a distinct reason"), RefusalReasons.Num(), int32(UE_ARRAY_COUNT(Refusals)));
    FString NotARefusal;
    for (const TCHAR* Text : {TEXT(""), TEXT("Server full."), TEXT("Your connection to the host has been lost."),
        TEXT("Pending Connection Failed."), TEXT("roomfull: another code"), TEXT("UnknownCode: detail"), TEXT("RoomFull")})
    {
        TestFalse(TEXT("Engine texts and unknown codes are never shown as a known refusal"),
            LDDemoEntry::DescribeJoinRefusal(Text, NotARefusal));
    }
    // Title version: only the release commit shows the product version; every other build shows its commit instead.
    const FString Commit = TEXT("1eb0251e9b1f3c5d7a9b1c3d5e7f9a1b3c5d7e9f");
    TestEqual(TEXT("A release build shows its product version"),
        LDDemoEntry::FormatTitleVersion(TEXT("0.10.2"), true, Commit, false), FString(TEXT("版本 0.10.2")));
    TestEqual(TEXT("A development build shows its short commit, not the version line it started from"),
        LDDemoEntry::FormatTitleVersion(TEXT("0.10.0"), false, Commit, false), FString(TEXT("build 1eb0251")));
    TestEqual(TEXT("A development package built from uncommitted changes says so"),
        LDDemoEntry::FormatTitleVersion(TEXT("0.10.0"), false, Commit, true), FString(TEXT("build 1eb0251（含未提交改动）")));
    for (const TCHAR* Unknown : {TEXT("unknown"), TEXT(""), TEXT("1eb0251"), TEXT("1EB0251E9B1F3C5D7A9B1C3D5E7F9A1B3C5D7E9F")})
    {
        TestEqual(TEXT("A development build without a readable commit never falls back to the product version"),
            LDDemoEntry::FormatTitleVersion(TEXT("0.10.0"), false, Unknown, true), FString(TEXT("build unknown")));
    }
    const FString Name = TEXT("UEDPIE_") + LexToString(FGuid::NewGuid().A & MAX_int32) + TEXT("_L_Frontend");
    auto* Package = CreatePackage(*(TEXT("/Game/Lockdown/Generated/Maps/") + Name));
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, FName(*Name), Package, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Isolated native game-mode factory world"), World)) { return false; }
    ON_SCOPE_EXIT { World->DestroyWorld(false); };
    auto* Instance = NewObject<ULDGameInstance>();
    AddExpectedError(TEXT("LD_DEMO_ENTRY_REJECTED"), EAutomationExpectedErrorFlags::Contains, 4);
    TestNull(TEXT("Factory rejects old profile before loading its selected class"), Instance->CreateGameModeForURL(
        FURL(nullptr, *(FString(LDDemoEntry::FrontendMap) + TEXT("?Profile=V05")), TRAVEL_Absolute), World));
    TestNull(TEXT("Factory cannot dispatch an engine GameMode URL override"), Instance->CreateGameModeForURL(
        FURL(nullptr, *(FString(LDDemoEntry::FrontendMap) + TEXT("?game=/Script/Engine.GameModeBase")), TRAVEL_Absolute), World));
    World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
    TestNull(TEXT("Factory rejects a saved non-Demo GameMode instead of silently remapping it"), Instance->CreateGameModeForURL(
        FURL(nullptr, LDDemoEntry::FrontendMap, TRAVEL_Absolute), World));
    World->GetWorldSettings()->DefaultGameMode = nullptr;
    auto* Mode = Cast<ALDGameMode>(Instance->CreateGameModeForURL(FURL(nullptr, LDDemoEntry::FrontendMap, TRAVEL_Absolute), World));
    if (TestNotNull(TEXT("Valid frontend creates the sole native GameMode"), Mode))
    {
        Error.Reset();
        Mode->InitGame(TEXT("L_Frontend"), TEXT(""), Error);
        TestTrue(TEXT("Frontend native initialization succeeds without approved gameplay data"), Error.IsEmpty());
        TestFalse(TEXT("Frontend never opens gameplay writes"), Mode->IsPlaying());
        TestNull(TEXT("Frontend has no default combat Pawn"), Mode->DefaultPawnClass.Get());
        TestNull(TEXT("Frontend cannot create an unregistered spectator body"), Mode->SpectatorClass.Get());
    }
    const FString LegacyName = TEXT("UEDPIE_") + LexToString(FGuid::NewGuid().A & MAX_int32) + TEXT("_L_V09Full");
    auto* LegacyPackage = CreatePackage(*(TEXT("/Game/Lockdown/Generated/V09/") + LegacyName));
    UWorld* LegacyWorld = UWorld::CreateWorld(EWorldType::Game, false, FName(*LegacyName), LegacyPackage,
        true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Native saved-mode rejection world"), LegacyWorld)) { return false; }
    ON_SCOPE_EXIT { LegacyWorld->DestroyWorld(false); };
    LegacyWorld->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
    const FURL LegacyURL(nullptr, TEXT("/Game/Lockdown/Generated/V09/L_V09Full"), TRAVEL_Absolute);
    TestNull(TEXT("A retired world with no Profile is rejected before its selected lifecycle can execute"),
        Instance->CreateGameModeForURL(LegacyURL, LegacyWorld));
    TestEqual(TEXT("Rejection retains the original URL"), Instance->RejectedEntryURL(), LegacyURL.ToString());
    int32 Pawns = 0, GameModes = 0;
    for (TActorIterator<APawn> It(LegacyWorld); It; ++It) { ++Pawns; }
    for (TActorIterator<AGameModeBase> It(LegacyWorld); It; ++It) { ++GameModes; }
    TestEqual(TEXT("Rejected native mode never creates a pawn"), Pawns, 0);
    TestEqual(TEXT("Rejected native mode is not silently replaced by another game"), GameModes, 0);
    auto* Registry = LegacyWorld->GetSubsystem<ULDMapRegistry>();
    TestNull(TEXT("Rejected legacy world never creates its old map registry"), Registry);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoInputBytesTest, "Lockdown.Rules.DemoInputByteIntegrity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoInputBytesTest::RunTest(const FString& Parameters)
{
    const FString Directory = FPaths::ProjectDir() / TEXT("Artifacts") / (TEXT("DemoInputTest-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
    const FString File = Directory / TEXT("payload.json");
    if (!TestTrue(TEXT("Create isolated byte fixture directory"), IFileManager::Get().MakeDirectory(*Directory, true))) { return false; }
    ON_SCOPE_EXIT
    {
        IFileManager::Get().Delete(*File, false, true);
        IFileManager::Get().DeleteDirectory(*Directory);
    };
    FString Error;
    const FString Expected = TEXT("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    TestFalse(TEXT("Missing payload cannot be verified"), LDDemoEntry::VerifyFileHash(File, Expected, Error));
    TestTrue(TEXT("Fixture bytes written without BOM"), FFileHelper::SaveStringToFile(TEXT("abc"), *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    TestTrue(TEXT("Known SHA256 matches exact bytes"), LDDemoEntry::VerifyFileHash(File, Expected, Error));
    TestFalse(TEXT("Digest encoding must be canonical"), LDDemoEntry::VerifyFileHash(File, Expected.ToUpper(), Error));
    TestFalse(TEXT("Truncated digest cannot be accepted"), LDDemoEntry::VerifyFileHash(File, Expected.Left(63), Error));
    TestTrue(TEXT("Mutate only line-ending bytes"), FFileHelper::SaveStringToFile(TEXT("abc\r\n"), *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    TestFalse(TEXT("No newline normalization can hide stale bytes"), LDDemoEntry::VerifyFileHash(File, Expected, Error));
    TestTrue(TEXT("Hash failure is explicit"), !Error.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoEntryObservationTest, "Lockdown.Rules.DemoEntryObservation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoEntryObservationTest::RunTest(const FString& Parameters)
{
    auto* Instance = NewObject<ULDGameInstance>();
    LDDemoTests::StartEntryObservation(*Instance, true);
    ON_SCOPE_EXIT { LDDemoTests::StopEntryObservation(*Instance); };
    LDDemoTests::ObserveEntryBeforeWorld(*Instance);
    TestTrue(TEXT("Pre-world rejection is distinct from observing a world"),
        LDDemoTests::ReadEntryObservation(*Instance).RejectedBeforeWorldCreation
        && !LDDemoTests::ReadEntryObservation(*Instance).Observed);
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Observed isolated world"), World)) { return false; }
    TestFalse(TEXT("A later world creation cannot retain a false pre-world rejection proof"),
        LDDemoTests::ReadEntryObservation(*Instance).RejectedBeforeWorldCreation);
    LDDemoTests::EndEntryBrowse(*Instance);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { if (World) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); } };
    LDDemoTests::ObserveRejectedEntry(*Instance, *World);
    TestTrue(TEXT("Observation begins during native world initialization"), LDDemoTests::ReadEntryObservation(*Instance).Observed);
    APawn* Pawn = World->SpawnActor<APawn>();
    if (!TestNotNull(TEXT("Deliberate invalid activation fixture"), Pawn)) { return false; }
    TestTrue(TEXT("Fixture pawn is destroyed before final census"), Pawn->Destroy());
    LDDemoTests::ObserveMapRegistry(*World, false);
    LDDemoTests::ObserveMapRegistry(*World, true);
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    World = nullptr;
    const auto Observed = LDDemoTests::ReadEntryObservation(*Instance);
    TestTrue(TEXT("Teardown cannot erase initialized-attempt coverage"), Observed.Observed);
    TestEqual(TEXT("Destroyed pawn remains a cumulative activation"), Observed.Pawns, 1);
    TestEqual(TEXT("Registry creation cannot be hidden by teardown"), Observed.RegistryCreations, 1);
    TestEqual(TEXT("Registry activation cannot be hidden by teardown"), Observed.RegistryActivations, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoMatchIdentityTest, "Lockdown.Rules.DemoMatchIdentity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoMatchIdentityTest::RunTest(const FString& Parameters)
{
    uint32 Last = 0, Next = 99;
    TestTrue(TEXT("First valid network generation is allocated"), LDMatchLifecycle::AllocateGeneration(Last, Next));
    TestEqual(TEXT("Zero is never a valid generation"), Next, uint32(1));
    const FLDMatchContext First{Next, FGuid::NewGuid()};
    TestTrue(TEXT("Second generation allocates rather than aliases"), LDMatchLifecycle::AllocateGeneration(Last, Next));
    const FLDMatchContext Second{Next, FGuid::NewGuid()};
    TestTrue(TEXT("Network and ledger identities are both required"), First.IsValid() && Second.IsValid() && First != Second);
    TestFalse(TEXT("Network ID alone is not a ledger context"), (FLDMatchContext{Next, FGuid()}).IsValid());
    TestFalse(TEXT("Ledger ID alone is not a network context"), (FLDMatchContext{0, FGuid::NewGuid()}).IsValid());
    Last = MAX_uint32 - 1;
    TestTrue(TEXT("Last representable generation is valid"), LDMatchLifecycle::AllocateGeneration(Last, Next));
    TestEqual(TEXT("Upper boundary is exact"), Next, MAX_uint32);
    TestFalse(TEXT("Exhaustion never wraps"), LDMatchLifecycle::AllocateGeneration(Last, Next));
    TestEqual(TEXT("Rejected allocation leaves the watermark intact"), Last, MAX_uint32);
    TestEqual(TEXT("Rejected allocation does not return success-shaped identity"), Next, uint32(0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoSessionReceiptsTest, "Lockdown.Rules.DemoSessionReceipts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoSessionReceiptsTest::RunTest(const FString& Parameters)
{
    FLDSessionRequestWindow Window;
    FLDSessionRequest Request;
    Request.Key = {1, 1};
    Request.ExpectedRosterRevision = 1;
    Request.Ready = true;
    FLDSessionReceipt Receipt;
    TestTrue(TEXT("First request begins"), Window.Begin(Request, Receipt) == ELDSessionWindowResult::NewRequest);
    TestTrue(TEXT("Same pending request remains pending"), Window.Begin(Request, Receipt) == ELDSessionWindowResult::ExistingResult
        && Receipt.Outcome == ELDSessionOutcome::Pending);
    auto Conflict = Request;
    Conflict.Ready = false;
    Window.Begin(Conflict, Receipt);
    TestTrue(TEXT("Changing an in-flight request is a conflict"), Receipt.Reason == ELDSessionReject::RequestConflict);
    Conflict.Key.RequestSequence = 2;
    Window.Begin(Conflict, Receipt);
    TestTrue(TEXT("Another request cannot reenter the active mutation"), Receipt.Reason == ELDSessionReject::RequestInFlight);
    Receipt.Key = Request.Key;
    Receipt.Outcome = ELDSessionOutcome::Committed;
    Receipt.Reason = ELDSessionReject::None;
    Receipt.Detail = TEXT("The exact original receipt");
    TestTrue(TEXT("Only terminal receipt commits"), Window.Complete(Receipt));
    TestFalse(TEXT("A receipt cannot commit twice"), Window.Complete(Receipt));
    TestTrue(TEXT("Replay does not begin another mutation"), Window.Begin(Request, Receipt) == ELDSessionWindowResult::ExistingResult);
    TestEqual(TEXT("Exact original outcome is retained"), Receipt.Detail, FString(TEXT("The exact original receipt")));
    Request.Ready = false;
    Window.Begin(Request, Receipt);
    TestTrue(TEXT("Changed terminal terms are rejected"), Receipt.Reason == ELDSessionReject::RequestConflict);
    for (uint64 Sequence = 2; Sequence <= 10000; ++Sequence)
    {
        Request.Key.RequestSequence = Sequence;
        if (Window.Begin(Request, Receipt) != ELDSessionWindowResult::NewRequest) { AddError(TEXT("Monotonic request failed")); break; }
        Receipt.Outcome = ELDSessionOutcome::Rejected;
        Receipt.Reason = ELDSessionReject::StaleRoster;
        if (!Window.Complete(Receipt)) { AddError(TEXT("Rejected request did not consume its sequence")); break; }
    }
    TestEqual(TEXT("Bounded single-receipt window keeps the rejection watermark"), Window.ProcessedThrough(), uint64(10000));
    Request.Key.RequestSequence = 1;
    Window.Begin(Request, Receipt);
    TestTrue(TEXT("Evicted receipt cannot reexecute old request"), Receipt.Reason == ELDSessionReject::ReceiptExpired);
    Request.Key.RequestSequence = 10001;
    Request.Command = static_cast<ELDSessionCommand>(255);
    Window.Begin(Request, Receipt);
    TestTrue(TEXT("Unknown wire command cannot enter a write scope"), Receipt.Reason == ELDSessionReject::InvalidRequest && !Window.IsPending());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoSessionTest, "Lockdown.Rules.DemoLobbyAuthority", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoSessionTest::RunTest(const FString& Parameters)
{
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Isolated authority fixture world"), World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Mode = World->SpawnActor<ALDGameMode>();
    auto* Public = World->SpawnActor<ALDGameState>();
    auto* Host = World->SpawnActor<ALDPlayerController>();
    auto* Guest = World->SpawnActor<ALDPlayerController>();
    auto* Third = World->SpawnActor<ALDPlayerController>();
    auto* Fourth = World->SpawnActor<ALDPlayerController>();
    if (!Mode || !Public || !Host || !Guest || !Third || !Fourth) { AddError(TEXT("Fixture actors failed to spawn")); return false; }
    Host->SetAsLocalPlayerController();
    Mode->GameState = Public;
    Mode->MatchContext = {1, FGuid::NewGuid()};
    Mode->MatchPhase = ELDMatchPhase::Lobby;
    Mode->RosterRevision = 1;
    Mode->bDemoWorld = Mode->bEntryAccepted = true;
    FString Error;
    if (!TestTrue(TEXT("Host counts as one human slot"), Mode->AdmitRoomMember(Host, Error))
        || !TestTrue(TEXT("Second and third human slots fit"), Mode->AdmitRoomMember(Guest, Error) && Mode->AdmitRoomMember(Third, Error)))
    {
        AddError(Error);
        return false;
    }
    const uint64 Roster = Mode->RosterRevision;
    TestFalse(TEXT("Final admission point rejects fourth human"), Mode->AdmitRoomMember(Fourth, Error));
    FString Shown;
    TestTrue(TEXT("The final admission refusal of a full room carries its client-mapped code"),
        LDDemoEntry::DescribeJoinRefusal(Error, Shown) && Shown.Contains(TEXT("（RoomFull）")));
    TestEqual(TEXT("Failed admission does not advance roster"), Mode->RosterRevision, Roster);
    TestEqual(TEXT("No extra participants fill empty AI slots"), Mode->ReadLobby().Members.Num(), 3);
    TestNull(TEXT("Lobby never spawns a combat body"), Host->GetPawn());
    TestFalse(TEXT("Lobby keeps gameplay writes closed"), Mode->IsGameplayWriteOpen(Mode->MatchContext));
    FLDSessionRequest Request;
    Request.Key = {1, 1};
    Request.ExpectedRosterRevision = Roster;
    Request.Ready = true;
    TestTrue(TEXT("Host can ready only its record"), Mode->ProcessSessionCommand(Host, Request).Outcome == ELDSessionOutcome::Committed);
    TestFalse(TEXT("Other human is not implicitly readied"), Mode->StateFor(Guest)->bReady);
    TestTrue(TEXT("Guest can ready independently"), Mode->ProcessSessionCommand(Guest, Request).Outcome == ELDSessionOutcome::Committed);
    TestTrue(TEXT("Last readiness cannot invalidate earlier readiness"), Mode->ProcessSessionCommand(Third, Request).Outcome == ELDSessionOutcome::Committed);
    TestEqual(TEXT("Ready changes do not change structure version"), Mode->RosterRevision, Roster);
    TestTrue(TEXT("All three ready states remain current"), Mode->StateFor(Host)->bReady && Mode->StateFor(Guest)->bReady && Mode->StateFor(Third)->bReady);
    const uint64 AcceptedPublication = Public->DemoLobby.PublicRevision;
    --Public->DemoLobby.PublicRevision;
    Public->OnRep_DemoLobby();
    TestEqual(TEXT("GameState callback restores its accepted view after a stale replication"), Public->DemoLobby.PublicRevision, AcceptedPublication);
    const uint64 Publication = Mode->LobbyPublicRevision;
    TestTrue(TEXT("Duplicate ready replays outcome"), Mode->ProcessSessionCommand(Host, Request).Outcome == ELDSessionOutcome::Committed);
    TestEqual(TEXT("Duplicate cannot republish a mutation"), Mode->LobbyPublicRevision, Publication);
    Request.Key.RequestSequence = 2;
    Request.Ready = false;
    Request.Command = ELDSessionCommand::SetAICount;
    Request.AICount = 3;
    TestTrue(TEXT("Guest cannot alter AI configuration"), Mode->ProcessSessionCommand(Guest, Request).Reason == ELDSessionReject::NotHost);
    TestTrue(TEXT("Host can choose exactly three AI slots"), Mode->ProcessSessionCommand(Host, Request).Outcome == ELDSessionOutcome::Committed);
    TestEqual(TEXT("AI and humans remain independent slots"), Mode->ReadLobby().Members.Num(), 6);
    TestFalse(TEXT("Structural changes clear every human's readiness"), Mode->StateFor(Host)->bReady || Mode->StateFor(Guest)->bReady || Mode->StateFor(Third)->bReady);
    Request.Key.RequestSequence = 3;
    Request.Command = ELDSessionCommand::SetReady;
    Request.AICount = 0;
    Request.Ready = true;
    TestTrue(TEXT("Stale roster cannot ready"), Mode->ProcessSessionCommand(Guest, Request).Reason == ELDSessionReject::StaleRoster);
    Request.Key.Generation = 2;
    Request.ExpectedRosterRevision = Mode->RosterRevision;
    TestTrue(TEXT("Different match cannot write even with current roster"), Mode->ProcessSessionCommand(Host, Request).Reason == ELDSessionReject::StaleMatch);
    Request.Key.Generation = 1;
    Request.Key.RequestSequence = 4;
    for (auto* Controller : {Host, Guest, Third})
    {
        TestTrue(TEXT("Current-roster readiness commits"), Mode->ProcessSessionCommand(Controller, Request).Outcome == ELDSessionOutcome::Committed);
    }
    Request.Key.RequestSequence = 5;
    Request.Command = ELDSessionCommand::StartMatch;
    Request.Ready = false;
    TestTrue(TEXT("Non-Host cannot start"), Mode->ProcessSessionCommand(Guest, Request).Reason == ELDSessionReject::NotHost);
    const auto Blocked = Mode->ProcessSessionCommand(Host, Request);
    TestTrue(TEXT("Missing inputs/providers reject instead of starting legacy game"), Blocked.Reason == ELDSessionReject::MissingDemoDefinition
        && Blocked.Outcome == ELDSessionOutcome::Rejected && Mode->MatchPhase == ELDMatchPhase::Lobby && !Blocked.Detail.IsEmpty());
    TestNull(TEXT("Rejected start creates no old combat service"), Mode->Combat.Get());
    TestFalse(TEXT("Rejected start cannot open gameplay write gate"), Mode->IsGameplayWriteOpen(Mode->MatchContext));
    Mode->MatchPhase = ELDMatchPhase::Starting;
    Mode->LockedAISlots = Mode->LobbyAISlots;
    auto* Opponent = World->SpawnActor<ALDPlayerController>();
    if (!TestNotNull(TEXT("An isolated server opponent controller"), Opponent)) { return false; }
    const auto AI = Mode->RegisterAIContender(*Opponent, 0, Mode->MatchContext);
    if (!TestTrue(TEXT("The opponent registers during preparation"), AI.Binding.IsValid())) { return false; }
    Mode->SpawnRandom.Initialize(153);
    int64 OpeningAfterMs = 0;
    uint8 OpeningIndex = 0;
    int32 OpeningSeed = 0;
    TestTrue(TEXT("The opening plan uses the current server stream"),
        Mode->DrawOpponentOpeningPlan(AI.Binding, OpeningAfterMs, OpeningIndex, OpeningSeed, Error));
    FRandomStream Replay(OpeningSeed);
    TestEqual(TEXT("The recorded stream state reproduces the opening time"), OpeningAfterMs, int64(Replay.RandRange(1000, 4000)));
    TestEqual(TEXT("And reproduces the selected position"), OpeningIndex, uint8(Replay.RandRange(0, 2)));
    TestEqual(TEXT("The same server stream advanced by exactly those draws"), Mode->SpawnRandom.GetCurrentSeed(), Replay.GetCurrentSeed());
    const int32 BeforeInvalid = Mode->SpawnRandom.GetCurrentSeed();
    TestFalse(TEXT("An invalid binding draws no plan"),
        Mode->DrawOpponentOpeningPlan({}, OpeningAfterMs, OpeningIndex, OpeningSeed, Error));
    TestEqual(TEXT("A refused plan consumes no random material"), Mode->SpawnRandom.GetCurrentSeed(), BeforeInvalid);
    TestFalse(TEXT("Admission rechecks phase after PreLogin"), Mode->AdmitRoomMember(Fourth, Error));
    TestTrue(TEXT("The final admission refusal of a started match carries its client-mapped code"),
        LDDemoEntry::DescribeJoinRefusal(Error, Shown) && Shown.Contains(TEXT("（MatchAlreadyStarted）")));
    Request.Key.RequestSequence = 6;
    Request.Command = ELDSessionCommand::SetReady;
    TestTrue(TEXT("Late room mutation is rejected"), Mode->ProcessSessionCommand(Host, Request).Reason == ELDSessionReject::WrongPhase);
    Mode->MatchPhase = ELDMatchPhase::Development;
    TestTrue(TEXT("Only current active context has fast write admission"), Mode->IsGameplayWriteOpen(Mode->MatchContext));
    TestFalse(TEXT("Same network number cannot substitute a different ledger match"),
        Mode->IsGameplayWriteOpen(FLDMatchContext{1, FGuid::NewGuid()}));
    Mode->MatchPhase = ELDMatchPhase::Results;
    TestFalse(TEXT("Terminal phase closes fast write admission"), Mode->IsGameplayWriteOpen(Mode->MatchContext));
    return true;
}

// Issue #29: a Host that quits with clients connected tears its world down while a living owner's body and resource
// records are being released. Owner views stop first; a real mismatch in a playing phase stays an authority fault.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDOwnerViewTeardownTest, "Lockdown.Rules.DemoOwnerViewTeardown", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDOwnerViewTeardownTest::RunTest(const FString& Parameters)
{
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Isolated owner-view fixture world"), World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->bIsTearingDown = false; World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Mode = World->SpawnActor<ALDGameMode>();
    auto* Public = World->SpawnActor<ALDGameState>();
    auto* Host = World->SpawnActor<ALDPlayerController>();
    auto* Guest = World->SpawnActor<ALDPlayerController>();
    if (!Mode || !Public || !Host || !Guest) { AddError(TEXT("Fixture actors failed to spawn")); return false; }
    Host->SetAsLocalPlayerController();
    Mode->GameState = Public;
    Mode->MatchContext = {1, FGuid::NewGuid()};
    Mode->MatchPhase = ELDMatchPhase::Lobby;
    Mode->RosterRevision = 1;
    Mode->bDemoWorld = Mode->bEntryAccepted = true;
    FString Error;
    if (!TestTrue(TEXT("The Host and its client join the room"), Mode->AdmitRoomMember(Host, Error) && Mode->AdmitRoomMember(Guest, Error)))
    {
        AddError(Error);
        return false;
    }
    auto* Life = Mode->Lives.Find(Mode->ContenderId(Guest));
    FLDOwnerState* State = Mode->StateFor(Guest);
    if (!TestNotNull(TEXT("The client has a life record"), Life) || !TestNotNull(TEXT("The client has an owner record"), State)) { return false; }
    // A living owner whose body and resource records are already gone: what a world teardown leaves behind.
    Life->Phase = ELDLifePhase::Alive;
    uint64 Revision = State->Revision;
    const auto Untouched = [&](ELDMatchPhase Phase)
    {
        return Mode->MatchPhase == Phase && Mode->EntryFailure.IsEmpty() && State->Revision == Revision
            && Guest->OwnerState.Revision == Revision;
    };
    for (const ELDMatchPhase Phase : {ELDMatchPhase::Closing, ELDMatchPhase::Resetting})
    {
        Mode->MatchPhase = Phase;
        Mode->SyncOwner(Guest);
        TestTrue(TEXT("A closed room or a retiring match publishes no owner view and reports no projection fault"), Untouched(Phase));
    }
    Mode->MatchPhase = ELDMatchPhase::Development;
    World->bIsTearingDown = true;
    Mode->SyncOwner(Guest);
    TestTrue(TEXT("World teardown mid-match publishes no owner view and reports no projection fault"),
        Untouched(ELDMatchPhase::Development));
    World->bIsTearingDown = false;
    Life->Phase = ELDLifePhase::Unspawned;
    Mode->SyncOwner(Guest);
    TestTrue(TEXT("A playing phase still publishes a consistent owner view"),
        Mode->MatchPhase == ELDMatchPhase::Development && State->Revision == Revision + 1 && Guest->OwnerState.Revision == State->Revision);
    Life->Phase = ELDLifePhase::Alive;
    AddExpectedError(TEXT("LD_DEMO_AUTHORITY_FAULT"), EAutomationExpectedErrorFlags::Contains, 1);
    Mode->SyncOwner(Guest);
    TestTrue(TEXT("A real projection mismatch in a playing phase is still an authority fault"),
        Mode->MatchPhase == ELDMatchPhase::Faulted && Mode->EntryFailure.Contains(TEXT("no matching committed resource/body projection")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoLobbyReadModelTest, "Lockdown.Rules.DemoLobbyReadModel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoLobbyReadModelTest::RunTest(const FString& Parameters)
{
    FLDLobbyView Candidate, Latest;
    Candidate.Generation = 1;
    Candidate.Phase = ELDMatchPhase::Lobby;
    Candidate.RosterRevision = 3;
    Candidate.PublicRevision = 8;
    FLDLobbyMember Host;
    Host.ContenderId = FGuid::NewGuid();
    Host.Name = TEXT("Host");
    Host.Host = true;
    Candidate.Members.Add(Host);
    FString Error;
    TestTrue(TEXT("Initial bounded public roster is accepted"),
        LDMatchLifecycle::AcceptLobbyView(Candidate, Latest, Error) == ELDLobbyProjectionResult::Accepted);
    TestTrue(TEXT("Duplicate publication is not reapplied"),
        LDMatchLifecycle::AcceptLobbyView(Candidate, Latest, Error) == ELDLobbyProjectionResult::Stale);
    Candidate.PublicRevision = 9;
    Candidate.RosterRevision = 2;
    TestTrue(TEXT("Higher publication cannot roll back the structural roster"),
        LDMatchLifecycle::AcceptLobbyView(Candidate, Latest, Error) == ELDLobbyProjectionResult::Stale);
    TestEqual(TEXT("Refusal retains the accepted projection"), Latest.PublicRevision, uint64(8));
    Candidate = Latest;
    Candidate.PublicRevision = 9;
    Candidate.Members[0].Ready = true;
    Candidate.CanStart = true;
    TestTrue(TEXT("Ready-only update retains structure and can describe readiness"),
        LDMatchLifecycle::AcceptLobbyView(Candidate, Latest, Error) == ELDLobbyProjectionResult::Accepted);
    const auto Stable = Latest;
    auto Reject = [&](FLDLobbyView Invalid, const TCHAR* Name)
    {
        Invalid.PublicRevision = Stable.PublicRevision + 1;
        TestTrue(Name, LDMatchLifecycle::AcceptLobbyView(Invalid, Latest, Error) == ELDLobbyProjectionResult::Malformed);
        TestTrue(TEXT("Malformed data does not replace the current view"),
            Latest.PublicRevision == Stable.PublicRevision && Latest.Members.Num() == Stable.Members.Num());
        TestFalse(TEXT("Malformed publication has explicit feedback"), Error.IsEmpty());
    };
    Candidate = Stable;
    const FLDLobbyMember Duplicate = Candidate.Members[0];
    Candidate.Members.Add(Duplicate);
    Reject(Candidate, TEXT("Duplicate contender IDs cannot create additional members"));
    Candidate = Stable;
    Candidate.Members[0].ContenderId.Invalidate();
    Reject(Candidate, TEXT("Invalid contender identity is rejected"));
    Candidate = Stable;
    Candidate.Members[0].Kind = ELDContenderKind::OpponentAI;
    Candidate.AICount = 1;
    Reject(Candidate, TEXT("An AI cannot claim Host identity"));
    Candidate = Stable;
    Candidate.AICount = 1;
    Reject(Candidate, TEXT("Advertised AI count must match the actual public rows"));
    Candidate = Stable;
    Candidate.Members[0].Ready = false;
    Reject(Candidate, TEXT("Start readiness cannot bypass an unready human"));
    Candidate = Stable;
    Candidate.StartBlocker = TEXT("Provider missing");
    Reject(Candidate, TEXT("A blocked room cannot advertise start availability"));
    Candidate = Stable;
    Candidate.Members[0].Name = FString::ChrN(65, TEXT('x'));
    Reject(Candidate, TEXT("Member labels remain bounded"));
    Candidate = Stable;
    Candidate.Phase = static_cast<ELDMatchPhase>(255);
    Reject(Candidate, TEXT("Unknown phase does not enter the UI"));
    Candidate = Stable;
    for (int32 Index = 0; Index < 3; ++Index)
    {
        FLDLobbyMember Member;
        Member.ContenderId = FGuid::NewGuid();
        Member.Name = TEXT("Guest");
        Member.Ready = true;
        Candidate.Members.Add(Member);
    }
    Reject(Candidate, TEXT("Four human rows exceed the Demo structure"));
    Candidate = Stable;
    Candidate.Generation = 2;
    Candidate.RosterRevision = Candidate.PublicRevision = 1;
    Candidate.CanStart = false;
    Candidate.Members[0].Ready = false;
    TestTrue(TEXT("New generation may start fresh independent revisions"),
        LDMatchLifecycle::AcceptLobbyView(Candidate, Latest, Error) == ELDLobbyProjectionResult::Accepted);
    TestTrue(TEXT("Old generation cannot overwrite a new room"),
        LDMatchLifecycle::AcceptLobbyView(Stable, Latest, Error) == ELDLobbyProjectionResult::Stale);
    Candidate = Latest;
    ++Candidate.PublicRevision;
    Candidate.Phase = ELDMatchPhase::Closing;
    Candidate.Members[0].Host = false;
    TestTrue(TEXT("Host loss may publish closing without inventing a replacement Host"),
        LDMatchLifecycle::AcceptLobbyView(Candidate, Latest, Error) == ELDLobbyProjectionResult::Accepted);
    return true;
}
#endif
