#include "Tests/LDDemoEntryProbe.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Development/LDDemoContentCatalog.h"
#include "Navigation/LDDemoNavigationSystem.h"
#include "Region/LDMapRegistry.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace
{
struct FRoomProbe
{
    TWeakObjectPtr<ALDPlayerController> Controller;
    FString Output;
    FString Role;
    double Deadline = 0;
    bool ReadyWritten = false;
    bool ResultWritten = false;
};

bool Save(const TSharedRef<FJsonObject>& Report, const FString& File)
{
    FString Json;
    const FString Temporary = File + TEXT(".tmp");
    return FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
        && FFileHelper::SaveStringToFile(Json, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        && IFileManager::Get().Move(*File, *Temporary, true, true);
}

bool TickRoom(const TSharedRef<FRoomProbe>& Probe)
{
    if (Probe->ResultWritten && FPaths::FileExists(FPaths::Combine(Probe->Output, TEXT("Finish"))))
    {
        FPlatformMisc::RequestExitWithStatus(false, 0);
        return false;
    }
    auto* Controller = Probe->Controller.Get();
    auto* World = Controller ? Controller->GetWorld() : nullptr;
    const auto* State = World ? World->GetGameState<ALDGameState>() : nullptr;
    auto* Registry = World ? World->GetSubsystem<ULDMapRegistry>() : nullptr;
    const bool TimedOut = FPlatformTime::Seconds() >= Probe->Deadline;
    FString Error;
    bool LocalReady = Controller && World && State && Registry && Registry->Enabled()
        && Controller->IsLocalController() && Controller->GetLocalPlayer()
        && State->DemoLobby.Generation != 0 && State->DemoLobby.Phase == ELDMatchPhase::Lobby
        && Controller->OwnerState.Match == State->DemoLobby.Generation;
    if (LocalReady && Probe->Role == TEXT("Host") && !Probe->ReadyWritten)
    {
        auto Ready = MakeShared<FJsonObject>();
        Ready->SetBoolField(TEXT("ready"), true);
        Ready->SetNumberField(TEXT("generation"), State->DemoLobby.Generation);
        Probe->ReadyWritten = Save(Ready, FPaths::Combine(Probe->Output, TEXT("HostReady.json")));
        if (!Probe->ReadyWritten)
        {
            UE_LOG(LogTemp, Error, TEXT("LD_DEMO_ROOM_PROBE Could not persist Host readiness."));
            FPlatformMisc::RequestExitWithStatus(false, 2);
            return false;
        }
    }
    if (Probe->ResultWritten && !TimedOut) { return true; }
    if ((!LocalReady || State->DemoLobby.Members.Num() != 3) && !TimedOut) { return true; }

    auto Report = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Checks;
    bool Passed = !TimedOut;
    const auto Check = [&](bool Value, const TCHAR* Name)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("name"), Name);
        Row->SetBoolField(TEXT("passed"), Value);
        Checks.Add(MakeShared<FJsonValueObject>(Row));
        Passed &= Value;
    };
    Check(!TimedOut, TEXT("Normal owned connection and world reached readiness before the deadline"));
    Check(LocalReady, TEXT("Local player received the authoritative Lobby generation and valid native world"));
    FString Digest;
    Check(LDDemoEntry::VerifyRuntimeFiles(Error, &Digest), TEXT("The actual process validates the compiled content root and native catalog"));
    if (LocalReady)
    {
        Check(UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) == LDDemoEntry::WorldMap,
            TEXT("The connection is in the sole Demo world"));
        const auto* Nav = Cast<ULDDemoNavigationSystem>(World->GetNavigationSystem());
        Check(Nav && Nav->HasApprovedAgent(), TEXT("The same approved navigation agent is configured on this connection"));
        if (Probe->Role == TEXT("Host"))
        {
            const auto* Driver = World->GetNetDriver();
            Check(World->GetNetMode() == NM_ListenServer && Controller->HasAuthority()
                && Driver && Driver->ClientConnections.Num() == 2, TEXT("Listen Host has two real owned client connections"));
        }
        else
        {
            Check(World->GetNetMode() == NM_Client && !Controller->HasAuthority()
                && Controller->GetNetConnection() != nullptr, TEXT("Remote local player is connected through its owning network connection"));
        }
        int32 Hosts = 0;
        TSet<FGuid> Members;
        TArray<TSharedPtr<FJsonValue>> Ids;
        for (const auto& Member : State->DemoLobby.Members)
        {
            Hosts += Member.Host ? 1 : 0;
            Check(Member.Kind == ELDContenderKind::Human && Member.ContenderId.IsValid(),
                TEXT("Roster rows are real human contenders with valid identities"));
            Members.Add(Member.ContenderId);
            Ids.Add(MakeShared<FJsonValueString>(Member.ContenderId.ToString(EGuidFormats::Digits)));
        }
        Check(Members.Num() == 3 && Hosts == 1 && State->DemoLobby.AICount == 0,
            TEXT("Three-human roster has one Host and no fabricated AI bodies"));
        Report->SetArrayField(TEXT("contenders"), Ids);
        Report->SetNumberField(TEXT("generation"), State->DemoLobby.Generation);
        Check(State->DemoLobby.Phase==ELDMatchPhase::Lobby && !State->DemoLobby.CanStart && !State->DemoLobby.StartBlocker.IsEmpty(),
            TEXT("Incomplete gameplay remains explicitly gated"));
        int32 Pawns = 0;
        for (TActorIterator<APawn> It(World); It; ++It) { ++Pawns; }
        Check(Pawns == 0, TEXT("Loading a Lobby does not spawn unregistered gameplay or spectator bodies"));
        Report->SetNumberField(TEXT("pawns"), Pawns);
        TSharedPtr<const FLDDemoContentSnapshot> Catalog;
        FCollisionShape Capsule;
        const auto* Start = Registry->Resolve(TEXT("s00"), Registry->Generation);
        FLDLegalGround Origin, Approach;
        FLDCompletePath Path;
        ELDWorldQueryFailure Failure;
        const bool NavigationReady = LDDemoEntry::LoadRuntimeContent(Catalog, Error)
            && ULDMapRegistry::ContentCapsule(*Catalog, Capsule, Error) && Start
            && Registry->QueryGround(Registry->Revision(), Start->Position, Capsule, Start->Surface, Origin, Failure, Error)
            && Registry->FindPublicApproach(Registry->Revision(), Origin, TEXT("shop"), Capsule, Approach, Path, Failure, Error);
        Check(NavigationReady, TEXT("This connection resolves real ground and a complete shop-range approach"));
        if (NavigationReady) { Report->SetNumberField(TEXT("shop_path_cm"), Path.LengthCm); }
    }
    Report->SetBoolField(TEXT("passed"), Passed);
    Report->SetStringField(TEXT("role"), Probe->Role);
    Report->SetStringField(TEXT("catalog_sha256"), Digest);
    Report->SetStringField(TEXT("detail"), Error);
    Report->SetStringField(TEXT("scope"), TEXT("Three real human Lobby connections, native saved-world/agent/ground/path binding and no gameplay activation. Not AI, sparse vision, CMC traversal or a playable match."));
    Report->SetArrayField(TEXT("checks"), Checks);
    Probe->ResultWritten = Save(Report, FPaths::Combine(Probe->Output, Probe->Role + TEXT(".json")));
    UE_LOG(LogTemp, Display, TEXT("LD_DEMO_ROOM_PROBE role=%s passed=%d"), *Probe->Role, Passed && Probe->ResultWritten);
    if (!Passed || !Probe->ResultWritten)
    {
        FPlatformMisc::RequestExitWithStatus(false, 2);
        return false;
    }
    return true;
}
}

void LDDemoTests::StartWorldRoomProbe(ALDPlayerController& Controller)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("LDDemoWorldRoomProbe")) || !Controller.IsLocalController()
        || UWorld::RemovePIEPrefix(Controller.GetWorld()->GetOutermost()->GetName()) != LDDemoEntry::WorldMap) { return; }
    auto Probe = MakeShared<FRoomProbe>();
    int32 Timeout = 120;
    FParse::Value(FCommandLine::Get(), TEXT("LDDemoWorldRoomTimeout="), Timeout);
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoWorldRoomOutput="), Probe->Output)
        || !FParse::Value(FCommandLine::Get(), TEXT("LDDemoWorldRoomRole="), Probe->Role)
        || (Probe->Role != TEXT("Host") && Probe->Role != TEXT("Client1") && Probe->Role != TEXT("Client2"))
        || Timeout < 30 || Timeout > 300
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Probe->Output),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts"))))
    {
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_ROOM_PROBE Invalid isolated test arguments."));
        FPlatformMisc::RequestExitWithStatus(false, 2);
        return;
    }
    Probe->Controller = &Controller;
    Probe->Deadline = FPlatformTime::Seconds() + Timeout;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
        [Probe](float) { return TickRoom(Probe); }), .1f);
}
#endif
