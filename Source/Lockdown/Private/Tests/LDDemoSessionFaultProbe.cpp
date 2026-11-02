#include "Tests/LDDemoSessionFaultProbe.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameInstance.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "UI/LDUIRoot.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

// Unity builds merge anonymous namespaces across files, so every internal name stays in this namespace.
namespace LDSessionFaultProbeInternal
{
// Leave: three humans start normally; Client2 leaves through the in-match menu, the Host ends the match.
// HostLoss: two humans start normally; the runner kills the Host and Client1 must land on an interrupted menu.
// Join: a frontend client presses Join for a closed port and for an unresolvable host.
enum class ELDSessionFaultMode : uint8 { Leave, HostLoss, Join };
enum class ELDSessionFaultStage : uint8 { Join, Lobby, Starting, Match, Ending, Frontend, Done };

const TCHAR* const LeaveRequested = TEXT("Client2LeaveRequested.json");
const TCHAR* const HostKilled = TEXT("HostKilled");
const TCHAR* const SawResults = TEXT("Client1SawResults.json");
const TCHAR* const ConnectingPrefix = TEXT("正在连接");
const TCHAR* const JoinFailedPrefix = TEXT("连接失败：");
const TCHAR* const EndedWithoutReason = TEXT("连接已结束");
const TCHAR* const InterruptedText = TEXT("会话中断");

template <typename T>
T* FindIn(UUserWidget* Widget, const TCHAR* Name)
{
    return Widget && Widget->WidgetTree ? Widget->WidgetTree->FindWidget<T>(FName(Name)) : nullptr;
}

FString FindShownText(UUserWidget* Widget, TFunctionRef<bool(const FString&)> Match)
{
    FString Found;
    if (Widget && Widget->WidgetTree)
    {
        Widget->WidgetTree->ForEachWidget([&Found, &Match](UWidget* Child)
        {
            const auto* Text = Cast<UTextBlock>(Child);
            if (Found.IsEmpty() && Text)
            {
                const FString Shown = Text->GetText().ToString();
                if (Match(Shown)) { Found = Shown; }
            }
        });
    }
    return Found;
}

int32 RemoteControllers(UWorld& World)
{
    int32 Count = 0;
    for (auto It = World.GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* Remote = It->Get();
        if (Remote && !Remote->IsLocalController()) { ++Count; }
    }
    return Count;
}

class FLDDemoSessionFaultProbe;
// Every failed join reopens the frontend with a new controller; the one Join probe continues across them.
TWeakPtr<FLDDemoSessionFaultProbe> JoinProbe;

class FLDDemoSessionFaultProbe : public TSharedFromThis<FLDDemoSessionFaultProbe>
{
public:
    TWeakObjectPtr<ALDPlayerController> Controller;
    FString Output, Role, Error;
    ELDSessionFaultMode Mode = ELDSessionFaultMode::Leave;
    ELDSessionFaultStage Stage = ELDSessionFaultStage::Join;
    int32 ClosedPort = 0, Attempt = 0;
    double Deadline = 0, StageAt = 0;
    bool Passed = true, Written = false, ReadySent = false, IdentitySaved = false, StartSent = false, EndSent = false;
    bool LeaveSent = false, HandedOff = false, MarkedResults = false, SawConnecting = false;
    FGuid LeaverId;
    FString PreviousNotice;
    TArray<TSharedPtr<FJsonValue>> Checks;
    TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();

    int32 ExpectedHumans() const { return Mode == ELDSessionFaultMode::Leave ? 3 : 2; }
    TArray<FString> Roles() const
    {
        return Mode == ELDSessionFaultMode::Leave ? TArray<FString>{TEXT("Host"), TEXT("Client1"), TEXT("Client2")}
            : TArray<FString>{TEXT("Host"), TEXT("Client1")};
    }

    bool Check(bool Value, const TCHAR* Name)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("name"), Name);
        Row->SetBoolField(TEXT("passed"), Value);
        Checks.Add(MakeShared<FJsonValueObject>(Row));
        Passed &= Value;
        if (!Value && Error.IsEmpty()) { Error = Name; }
        return Value;
    }

    FString Path(const FString& Name) const { return FPaths::Combine(Output, Name); }
    bool Exists(const FString& Name) const { return FPaths::FileExists(Path(Name)); }

    bool Save(const TSharedRef<FJsonObject>& Value, const FString& Name) const
    {
        FString Json;
        const FString File = Path(Name);
        return FJsonSerializer::Serialize(Value, TJsonWriterFactory<>::Create(&Json))
            && FFileHelper::SaveStringToFile(Json, *(File + TEXT(".tmp")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
            && IFileManager::Get().Move(*File, *(File + TEXT(".tmp")), true, true);
    }

    bool Mark(const FString& Name)
    {
        auto Value = MakeShared<FJsonObject>();
        Value->SetBoolField(TEXT("ready"), true);
        if (Save(Value, Name)) { return true; }
        Check(false, TEXT("The probe persists its stage marker"));
        Error = TEXT("Could not persist session fault marker: ") + Name;
        return false;
    }

    bool AllMarkers(const TCHAR* Suffix) const
    {
        for (const FString& Name : Roles())
        {
            if (!Exists(Name + Suffix)) { return false; }
        }
        return true;
    }

    bool SaveIdentity(const FGuid& Contender)
    {
        auto Value = MakeShared<FJsonObject>();
        Value->SetStringField(TEXT("contender"), Contender.ToString(EGuidFormats::Digits));
        return Check(Contender.IsValid() && Save(Value, Role + TEXT("Id.json")), TEXT("The role persists its contender identity"));
    }

    bool LoadIdentity(const TCHAR* Name, FGuid& Out) const
    {
        FString Json, Text;
        TSharedPtr<FJsonObject> Value;
        return FFileHelper::LoadFileToString(Json, *Path(FString(Name) + TEXT("Id.json")))
            && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Value) && Value.IsValid()
            && Value->TryGetStringField(TEXT("contender"), Text) && FGuid::Parse(Text, Out) && Out.IsValid();
    }

    bool Finish()
    {
        auto Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"), Passed);
        Report->SetStringField(TEXT("role"), Role);
        Report->SetStringField(TEXT("mode"), Mode == ELDSessionFaultMode::Leave ? TEXT("Leave")
            : Mode == ELDSessionFaultMode::HostLoss ? TEXT("HostLoss") : TEXT("Join"));
        Report->SetStringField(TEXT("detail"), Error);
        Report->SetStringField(TEXT("scope"), TEXT("DEV-only session faults through the real entry, menu and frontend paths on one machine's loopback. Not LAN or public internet."));
        Report->SetObjectField(TEXT("summary"), Summary);
        Report->SetArrayField(TEXT("checks"), Checks);
        Written = Save(Report, Role + TEXT(".json"));
        Stage = ELDSessionFaultStage::Done;
        UE_LOG(LogTemp, Display, TEXT("LD_DEMO_SESSION_FAULT_PROBE mode=%u role=%s passed=%d detail=%s"), uint8(Mode), *Role, Passed && Written, *Error);
        if (!Passed || !Written) { FPlatformMisc::RequestExitWithStatus(false, 2); return false; }
        return true;
    }

    // A role that continues on the frontend keeps its in-match checks in a separate partial report.
    bool SaveMatchPart()
    {
        auto Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"), Passed);
        Report->SetStringField(TEXT("role"), Role);
        Report->SetStringField(TEXT("detail"), Error);
        Report->SetArrayField(TEXT("checks"), Checks);
        return Check(Save(Report, Role + TEXT("Match.json")), TEXT("The in-match part of the role is persisted"));
    }

    bool Tick();
    bool TickMatch(ALDPlayerController& PC, UWorld& World, ALDGameState& Public);
    bool TickFrontend(ALDPlayerController& PC, UWorld& World);
    bool CheckLeaveResult(const ALDGameState& Public);
};

bool FLDDemoSessionFaultProbe::CheckLeaveResult(const ALDGameState& Public)
{
    const FLDResultView& Result = Public.MatchResult;
    const FLDResultRow* Leaver = Result.Rows.FindByPredicate([this](const FLDResultRow& Row) { return Row.ContenderId == LeaverId; });
    Check(Result.Reason == ELDMatchEndReason::HostEnded && !Result.Winner.IsValid(),
        TEXT("A Host-ended match records no winner"));
    Check(Result.Rows.Num() == 3 && Leaver && Leaver->bLeft && Leaver->Deaths == 0,
        TEXT("The result keeps the leaver as left, not dead"));
    Check(!Result.Rows.ContainsByPredicate([this](const FLDResultRow& Row) { return Row.Kills != 0 || (Row.ContenderId != LeaverId && Row.bLeft); }),
        TEXT("Leaving credits no kill and marks no one else as left"));
    Summary->SetStringField(TEXT("result_id"), Result.ResultId.ToString(EGuidFormats::Digits));
    Summary->SetNumberField(TEXT("generation"), double(Result.Generation));
    return Passed;
}

bool FLDDemoSessionFaultProbe::Tick()
{
    ALDPlayerController* PC = Controller.Get();
    // A match instance that handed its role to the frontend retires with its world.
    if (HandedOff && !PC) { return false; }
    if (Written)
    {
        if (Role != TEXT("Host") && Exists(TEXT("FinishClients"))) { FPlatformMisc::RequestExitWithStatus(false, 0); return false; }
        if (Role == TEXT("Host") && Exists(TEXT("Finish"))) { FPlatformMisc::RequestExitWithStatus(false, 0); return false; }
        return true;
    }
    if (FPlatformTime::Seconds() >= Deadline)
    {
        Check(false, TEXT("The session fault stage completed within its bounded deadline"));
        Error = FString::Printf(TEXT("Session fault probe timed out at stage %u, attempt %d."), uint8(Stage), Attempt);
        return Finish();
    }
    UWorld* World = PC ? PC->GetWorld() : nullptr;
    if (!PC || !World) { return true; }
    if (Stage == ELDSessionFaultStage::Frontend) { return TickFrontend(*PC, *World); }
    auto* Public = World->GetGameState<ALDGameState>();
    return Public ? TickMatch(*PC, *World, *Public) : true;
}

bool FLDDemoSessionFaultProbe::TickMatch(ALDPlayerController& PC, UWorld& World, ALDGameState& Public)
{
    const FLDLobbyView& Lobby = Public.DemoLobby;
    const FLDPublicMatchView& View = Public.PublicMatch;
    switch (Stage)
    {
    case ELDSessionFaultStage::Join:
        if (!PC.IsLocalController() || !Lobby.Generation) { return true; }
        Check(Role == TEXT("Host") ? World.GetNetMode() == NM_ListenServer
            : World.GetNetMode() == NM_Client && PC.GetNetConnection() && !PC.HasAuthority(),
            TEXT("Each role is a real Listen Host or owning client connection"));
        if (!Mark(Role + TEXT("Joined.json"))) { return Finish(); }
        Stage = ELDSessionFaultStage::Lobby;
        return true;
    case ELDSessionFaultStage::Lobby:
    {
        if (!AllMarkers(TEXT("Joined.json")) || Lobby.Members.Num() != ExpectedHumans()) { return true; }
        const FLDLobbyMember* Self = Lobby.Members.FindByPredicate([&PC](const FLDLobbyMember& Row)
        { return Row.ContenderId == PC.OwnerState.ContenderId; });
        if (!Self || !Self->Ready)
        {
            if (!ReadySent || FPlatformTime::Seconds() - StageAt >= 3.)
            {
                PC.SetDemoReady(true);
                ReadySent = true;
                StageAt = FPlatformTime::Seconds();
            }
            return true;
        }
        if (!IdentitySaved)
        {
            if (!SaveIdentity(PC.OwnerState.ContenderId)) { return Finish(); }
            IdentitySaved = true;
        }
        if (Role != TEXT("Host")) { Stage = ELDSessionFaultStage::Starting; return true; }
        const bool AllReady = !Lobby.Members.ContainsByPredicate([](const FLDLobbyMember& Row)
        { return Row.Kind == ELDContenderKind::Human && !Row.Ready; });
        if (!AllReady || StartSent || !AllMarkers(TEXT("Id.json"))) { return true; }
        if (!Check(Lobby.CanStart && Lobby.StartBlocker.IsEmpty(), TEXT("Every provider is wired: the ready room may start normally")))
        {
            Error = TEXT("Normal start is still blocked: ") + Lobby.StartBlocker;
            return Finish();
        }
        PC.StartDemoMatch();
        StartSent = true;
        Stage = ELDSessionFaultStage::Starting;
        return true;
    }
    case ELDSessionFaultStage::Starting:
        if (View.Phase != ELDMatchPhase::Development || View.Generation != Lobby.Generation || !PC.GetPawn()
            || PC.OwnerState.LifePhase != ELDLifePhase::Alive || !PC.CanWorldCommand()) { return true; }
        if (Mode == ELDSessionFaultMode::Leave && !LoadIdentity(TEXT("Client2"), LeaverId))
        {
            Check(false, TEXT("The leaving contender identity is known"));
            return Finish();
        }
        if (!Mark(Role + TEXT("InMatch.json"))) { return Finish(); }
        // The departing or disconnected roles continue on the frontend in a new probe instance.
        HandedOff = (Mode == ELDSessionFaultMode::Leave && Role == TEXT("Client2"))
            || (Mode == ELDSessionFaultMode::HostLoss && Role == TEXT("Client1"));
        if (Mode == ELDSessionFaultMode::HostLoss && HandedOff && !SaveMatchPart()) { return Finish(); }
        Stage = ELDSessionFaultStage::Match;
        StageAt = FPlatformTime::Seconds();
        return true;
    case ELDSessionFaultStage::Match:
        if (Mode == ELDSessionFaultMode::HostLoss)
        {
            if (Role == TEXT("Client1"))
            {
                if (View.Phase == ELDMatchPhase::Results && !MarkedResults) { MarkedResults = Mark(SawResults); }
                return true;
            }
            if (!Exists(TEXT("Client1InMatch.json"))) { return true; }
            auto* Authority = World.GetAuthGameMode<ALDGameMode>();
            Check(Authority && Authority->IsGameplayWriteOpen(Authority->GetMatchContext()) && PC.OwnerState.LifePhase == ELDLifePhase::Alive
                && RemoteControllers(World) == 1, TEXT("The Host holds a live match with its owning client before it is lost"));
            Summary->SetNumberField(TEXT("generation"), double(View.Generation));
            return Finish();
        }
        if (Role == TEXT("Client2"))
        {
            if (LeaveSent || !AllMarkers(TEXT("InMatch.json"))) { return true; }
            // The real in-match menu: open the leave confirmation, then confirm it.
            const bool bOpened = PC.RootUI && FindIn<UButton>(PC.RootUI, TEXT("LeaveMatch"));
            if (bOpened) { FindIn<UButton>(PC.RootUI, TEXT("LeaveMatch"))->OnClicked.Broadcast(); }
            UButton* Confirm = FindIn<UButton>(PC.RootUI, TEXT("ConfirmLeaveMatch"));
            if (Confirm) { Confirm->OnClicked.Broadcast(); }
            if (!Check(bOpened && Confirm, TEXT("The in-match menu offers leave and its confirmation"))
                || !SaveMatchPart() || !Mark(LeaveRequested)) { return Finish(); }
            LeaveSent = true;
            return true;
        }
        if (Role == TEXT("Client1"))
        {
            if (View.Phase != ELDMatchPhase::Results) { return true; }
            if (Public.MatchResult.Generation != View.Generation || Public.MatchResult.ResultId != View.ResultId) { return true; }
            CheckLeaveResult(Public);
            return Finish();
        }
        // Host: the leaver's connection is gone and the room keeps playing before the Host ends it.
        if (!Exists(TEXT("Client2.json")) || RemoteControllers(World) != 1)
        {
            StageAt = FPlatformTime::Seconds();
            return true;
        }
        if (FPlatformTime::Seconds() - StageAt < 2.) { return true; }
        {
            auto* Authority = World.GetAuthGameMode<ALDGameMode>();
            Check((View.Phase == ELDMatchPhase::Development || View.Phase == ELDMatchPhase::CaptureOpen)
                && Authority && Authority->IsGameplayWriteOpen(Authority->GetMatchContext()) && PC.OwnerState.LifePhase == ELDLifePhase::Alive,
                TEXT("The match continues for the remaining contenders after a leave"));
            Check(!Public.KillFeed.Notices.ContainsByPredicate([this](const FLDKillNotice& Notice) { return Notice.Victim == LeaverId; }),
                TEXT("Leaving publishes no death notice"));
            if (!Passed) { return Finish(); }
            PC.EndDemoMatch();
            EndSent = true;
            Stage = ELDSessionFaultStage::Ending;
            return true;
        }
    case ELDSessionFaultStage::Ending:
        if (View.Phase != ELDMatchPhase::Results) { return true; }
        if (Public.MatchResult.Generation != View.Generation || Public.MatchResult.ResultId != View.ResultId) { return true; }
        CheckLeaveResult(Public);
        return Finish();
    default:
        return true;
    }
}

bool FLDDemoSessionFaultProbe::TickFrontend(ALDPlayerController& PC, UWorld& World)
{
    auto* Instance = Cast<ULDGameInstance>(PC.GetGameInstance());
    UUserWidget* Root = PC.RootUI;
    if (!Instance || !Root) { return true; }
    if (Mode == ELDSessionFaultMode::Leave)
    {
        if (FPlatformTime::Seconds() - StageAt < 2.) { return true; }
        Check(Instance->ConnectionMessage.IsEmpty(), TEXT("A voluntary leave is not reported as a failure"));
        Check(FindShownText(Root, [](const FString& Shown) { return Shown.Contains(JoinFailedPrefix) || Shown.Contains(InterruptedText); }).IsEmpty(),
            TEXT("The menu shows no failure or interruption after a voluntary leave"));
        Summary->SetBoolField(TEXT("left_to_menu"), true);
        return Finish();
    }
    if (Mode == ELDSessionFaultMode::HostLoss)
    {
        const FString Shown = FindShownText(Root, [](const FString& Text) { return Text.Contains(InterruptedText); });
        if (!Instance->ConnectionMessage.Contains(InterruptedText) || Shown.IsEmpty()) { return true; }
        Check(Exists(HostKilled), TEXT("The interruption follows the Host process being killed mid-match"));
        Check(!Exists(SawResults), TEXT("Host loss never produced a result table"));
        Check(!Shown.Contains(JoinFailedPrefix), TEXT("A lost session is not reported as a failed join"));
        Summary->SetStringField(TEXT("notice"), Shown);
        Summary->SetStringField(TEXT("message"), Instance->ConnectionMessage);
        return Finish();
    }
    // Join: two real Join presses must each end in an explicit failure with the controls usable again.
    UEditableTextBox* Name = FindIn<UEditableTextBox>(Root, TEXT("PlayerName"));
    UEditableTextBox* Address = FindIn<UEditableTextBox>(Root, TEXT("JoinAddress"));
    UEditableTextBox* Port = FindIn<UEditableTextBox>(Root, TEXT("JoinPort"));
    UButton* Join = FindIn<UButton>(Root, TEXT("Join"));
    if (!Check(Name && Address && Port && Join, TEXT("The frontend offers the name, address, port and Join controls"))) { return Finish(); }
    const FString Notice = FindShownText(Root, [](const FString& Text)
    { return Text.StartsWith(ConnectingPrefix) || Text.StartsWith(JoinFailedPrefix) || Text.StartsWith(EndedWithoutReason); });
    if (StageAt == 0)
    {
        PreviousNotice = Notice;
        Name->SetText(FText::FromString(TEXT("加入测试")));
        Address->SetText(FText::FromString(Attempt == 0 ? TEXT("127.0.0.1") : TEXT("lockdown-demo-unreachable.invalid")));
        Port->SetText(FText::FromString(Attempt == 0 ? FString::FromInt(ClosedPort) : FString(TEXT("7777"))));
        Join->OnClicked.Broadcast();
        StageAt = FPlatformTime::Seconds();
        return true;
    }
    const double Elapsed = FPlatformTime::Seconds() - StageAt;
    if (!SawConnecting)
    {
        if (Notice.StartsWith(ConnectingPrefix)) { SawConnecting = true; return true; }
        // The press replaces the previous outcome; a connecting notice normally shows on the next UI tick.
        if (Elapsed < 1. || Notice.IsEmpty() || Notice == PreviousNotice)
        {
            if (Elapsed < 10.) { return true; }
            Check(false, TEXT("A Join press starts a connection attempt"));
            return Finish();
        }
        SawConnecting = true;
    }
    if (Notice.IsEmpty() || Notice.StartsWith(ConnectingPrefix)) { return true; }
    Summary->SetStringField(Attempt == 0 ? TEXT("closed_port_notice") : TEXT("unresolvable_host_notice"), Notice);
    if (!Check(Notice.StartsWith(JoinFailedPrefix), TEXT("A failed join shows its reason, not a generic ended notice"))) { return Finish(); }
    const FString Reason = Notice.RightChop(FCString::Strlen(JoinFailedPrefix));
    Check(!Reason.IsEmpty() && !Reason.StartsWith(JoinFailedPrefix), TEXT("A failed join names its reason once"));
    Check(!Instance->ConnectionMessage.IsEmpty() && !Instance->ConnectionMessage.StartsWith(JoinFailedPrefix),
        TEXT("The session reason is shown without a duplicated prefix"));
    Check(UWorld::RemovePIEPrefix(World.GetOutermost()->GetName()) == LDDemoEntry::FrontendMap,
        TEXT("A failed join stays on the frontend"));
    Check(Join->GetIsEnabled(), TEXT("The Join control is usable again after a failure"));
    if (!Passed || ++Attempt >= 2) { return Finish(); }
    SawConnecting = false;
    StageAt = 0;
    return true;
}

void Start(ALDPlayerController& Controller)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("LDDemoSessionFaultProbe")) || !Controller.IsLocalController()) { return; }
    auto Probe = MakeShared<FLDDemoSessionFaultProbe>();
    FString ModeText;
    int32 Timeout = 600;
    FParse::Value(FCommandLine::Get(), TEXT("LDDemoSessionFaultTimeout="), Timeout);
    FParse::Value(FCommandLine::Get(), TEXT("LDDemoSessionFaultClosedPort="), Probe->ClosedPort);
    const bool bArguments = FParse::Value(FCommandLine::Get(), TEXT("LDDemoSessionFaultOutput="), Probe->Output)
        && FParse::Value(FCommandLine::Get(), TEXT("LDDemoSessionFaultRole="), Probe->Role)
        && FParse::Value(FCommandLine::Get(), TEXT("LDDemoSessionFaultMode="), ModeText);
    Probe->Mode = ModeText == TEXT("HostLoss") ? ELDSessionFaultMode::HostLoss
        : ModeText == TEXT("Join") ? ELDSessionFaultMode::Join : ELDSessionFaultMode::Leave;
    const bool bRole = Probe->Mode == ELDSessionFaultMode::Join ? Probe->Role == TEXT("Joiner")
        : Probe->Role == TEXT("Host") || Probe->Role == TEXT("Client1") || (Probe->Mode == ELDSessionFaultMode::Leave && Probe->Role == TEXT("Client2"));
    if (!bArguments || !bRole || (ModeText != TEXT("Leave") && ModeText != TEXT("HostLoss") && ModeText != TEXT("Join"))
        || Timeout < 120 || Timeout > 900 || (Probe->Mode == ELDSessionFaultMode::Join && (Probe->ClosedPort < 1 || Probe->ClosedPort > 65535))
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Probe->Output),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts"))))
    {
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_SESSION_FAULT_PROBE Invalid isolated test arguments."));
        FPlatformMisc::RequestExitWithStatus(false, 2);
        return;
    }
    const FString Map = UWorld::RemovePIEPrefix(Controller.GetWorld()->GetOutermost()->GetName());
    if (Map == LDDemoEntry::FrontendMap)
    {
        // The frontend instance owns the Join role, or continues a role that left or lost its match.
        const bool bContinue = Probe->Mode == ELDSessionFaultMode::Join
            || (Probe->Mode == ELDSessionFaultMode::Leave && Probe->Role == TEXT("Client2") && Probe->Exists(LeaveRequested))
            || (Probe->Mode == ELDSessionFaultMode::HostLoss && Probe->Role == TEXT("Client1") && Probe->Exists(TEXT("Client1InMatch.json")));
        if (!bContinue) { return; }
        if (Probe->Mode == ELDSessionFaultMode::Join)
        {
            if (const TSharedPtr<FLDDemoSessionFaultProbe> Active = JoinProbe.Pin())
            {
                Active->Controller = &Controller;
                return;
            }
            JoinProbe = Probe;
        }
        Probe->Stage = ELDSessionFaultStage::Frontend;
        Probe->StageAt = Probe->Mode == ELDSessionFaultMode::Join ? 0. : FPlatformTime::Seconds();
    }
    else if (Map != LDDemoEntry::WorldMap || Probe->Mode == ELDSessionFaultMode::Join) { return; }
    Probe->Controller = &Controller;
    Probe->Deadline = FPlatformTime::Seconds() + Timeout;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float) { return Probe->Tick(); }), .1f);
}
}

void LDDemoTests::StartSessionFaultProbe(ALDPlayerController& Controller)
{
    LDSessionFaultProbeInternal::Start(Controller);
}
#endif
