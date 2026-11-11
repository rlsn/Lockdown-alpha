#include "Tests/LDDemoSessionFaultProbe.h"
#include "Core/LDLog.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameInstance.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "UI/LDHUD.h"
#include "UI/LDUIRoot.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
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
// Leave: three humans start normally; Client2 checks and cancels the Lobby leave prompt, then leaves through the in-match menu.
// HostLoss: two humans start normally; the runner kills the Host and Client1 must land on an interrupted menu.
// Join: a frontend client presses Join for a closed port and for an unresolvable host.
// Refuse: a live Host refuses real Join presses for another product version, a full room and a started match.
// Race: a real Join passes PreLogin, then the Host starts while that client loads the room; the final admission check
// refuses it with the started-match reason.
// HostLeave: the Listen Host leaves mid-match through its real in-match menu; its client lands on the frontend with
// the Host-left reason, never a lost-connection notice.
// HostQuit: the Listen Host's process quits normally mid-match (as closing its window does) while its client is still
// connected; the Host tears its world down without an authority fault and the client lands on an interrupted menu.
enum class ELDSessionFaultMode : uint8 { Leave, HostLoss, Join, Refuse, Race, HostLeave, HostQuit };
enum class ELDSessionFaultStage : uint8 { Join, Lobby, Starting, Match, Ending, Frontend, Done };

const TCHAR* const LeaveRequested = TEXT("Client2LeaveRequested.json");
const TCHAR* const HostLeaveRequested = TEXT("HostLeaveRequested.json");
const TCHAR* const HostQuitRequested = TEXT("HostQuitRequested.json");
const TCHAR* const HostLeftText = TEXT("房主已离开");
const TCHAR* const HostKilled = TEXT("HostKilled");
const TCHAR* const SawResults = TEXT("Client1SawResults.json");
const TCHAR* const ConnectingPrefix = TEXT("正在连接");
const TCHAR* const JoinFailedPrefix = TEXT("连接失败：");
const TCHAR* const ReturnedPrefix = TEXT("已返回主菜单：");
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

// A client whose PreLogin passed is told to load the room; it has no controller until it joins.
bool HasJoinLoadingRoom(UWorld& World)
{
    const UNetDriver* Driver = World.GetNetDriver();
    return Driver && Driver->ClientConnections.ContainsByPredicate([](const UNetConnection* Connection)
    { return Connection && Connection->ClientLoginState == EClientLoginState::Welcomed && !Connection->PlayerController; });
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
    int32 ClosedPort = 0, HostPort = 0, Attempt = 0;
    double Deadline = 0, StageAt = 0;
    bool Passed = true, Written = false, ReadySent = false, IdentitySaved = false, StartSent = false, EndSent = false;
    bool LeaveSent = false, HandedOff = false, MarkedResults = false, SawConnecting = false, Announced = false;
    bool LobbyLeaveChecked = false;
    FGuid LeaverId;
    FString PreviousNotice;
    TArray<TSharedPtr<FJsonValue>> Checks;
    TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();

    int32 ExpectedHumans() const { return Mode == ELDSessionFaultMode::Leave || Mode == ELDSessionFaultMode::Refuse ? 3 : 2; }
    TArray<FString> Roles() const
    {
        return Mode == ELDSessionFaultMode::Leave || Mode == ELDSessionFaultMode::Refuse
            ? TArray<FString>{TEXT("Host"), TEXT("Client1"), TEXT("Client2")} : TArray<FString>{TEXT("Host"), TEXT("Client1")};
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
            : Mode == ELDSessionFaultMode::HostLoss ? TEXT("HostLoss") : Mode == ELDSessionFaultMode::Refuse ? TEXT("Refuse")
            : Mode == ELDSessionFaultMode::Race ? TEXT("Race") : Mode == ELDSessionFaultMode::HostLeave ? TEXT("HostLeave")
            : Mode == ELDSessionFaultMode::HostQuit ? TEXT("HostQuit") : TEXT("Join"));
        Report->SetStringField(TEXT("detail"), Error);
        Report->SetStringField(TEXT("scope"), TEXT("DEV-only session faults through the real entry, menu and frontend paths on one machine's loopback. Not LAN or public internet."));
        Report->SetObjectField(TEXT("summary"), Summary);
        Report->SetArrayField(TEXT("checks"), Checks);
        Written = Save(Report, Role + TEXT(".json"));
        Stage = ELDSessionFaultStage::Done;
        UE_LOG(LogLDTest, Display, TEXT("LD_DEMO_SESSION_FAULT_PROBE mode=%u role=%s passed=%d detail=%s"), uint8(Mode), *Role, Passed && Written, *Error);
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

    bool CheckLobbyLeaveConfirmation(ALDPlayerController& PC, const FLDLobbyView& Lobby)
    {
        UButton* Leave = FindIn<UButton>(PC.RootUI, TEXT("LeaveRoom"));
        const UTextBlock* Caption = Leave ? Cast<UTextBlock>(Leave->GetContent()) : nullptr;
        if (!Check(Caption && Caption->GetText().ToString() == TEXT("离开房间并返回主菜单"),
            TEXT("The Lobby leave action names its destination"))) { return false; }
        Leave->OnClicked.Broadcast();
        const FString Expected = TEXT("离开后将返回主菜单；房间继续运行，其他成员仍留在 Lobby。");
        const bool bExplainsOutcome = Check(
            FindShownText(PC.RootUI, [&Expected](const FString& Text) { return Text == Expected; }) == Expected,
            TEXT("The Lobby leave confirmation explains the room remains open"));
        UButton* Cancel = FindIn<UButton>(PC.RootUI, TEXT("CancelLeave"));
        if (Cancel) { Cancel->OnClicked.Broadcast(); }
        const bool bStayedInLobby = Check(Cancel && Leave->GetIsEnabled()
            && Lobby.Phase == ELDMatchPhase::Lobby
            && Lobby.Members.ContainsByPredicate([&PC](const FLDLobbyMember& Member)
                { return Member.ContenderId == PC.OwnerState.ContenderId; }),
            TEXT("Cancelling a Lobby leave keeps the member in the room"));
        return bExplainsOutcome && bStayedInLobby;
    }

    // The real in-match menu: open the leave confirmation, then confirm it.
    bool PressLeave(ALDPlayerController& PC)
    {
        if (!Check(!FindIn<UButton>(PC.RootUI, TEXT("EndMatch")),
            TEXT("The in-match menu has no separate end-match action"))) { return false; }
        UButton* Leave = FindIn<UButton>(PC.RootUI, TEXT("LeaveMatch"));
        const UTextBlock* Caption = Leave ? Cast<UTextBlock>(Leave->GetContent()) : nullptr;
        if (!Check(Caption && Caption->GetText().ToString() == TEXT("离开房间并返回主菜单"),
            TEXT("The in-match leave action names its destination"))) { return false; }
        Leave->OnClicked.Broadcast();
        const FString Expected = PC.OwnerState.bLobbyHost
            ? TEXT("你是房主：离开将结束整个房间，所有玩家返回主菜单。")
            : TEXT("离开后将返回主菜单，不能重新加入本局；你的离局记录会保留，其他玩家可以继续本局。");
        if (!Check(FindShownText(PC.RootUI, [&Expected](const FString& Text) { return Text == Expected; }) == Expected,
            TEXT("The leave confirmation explains the role-specific outcome"))) { return false; }
        UButton* Confirm = FindIn<UButton>(PC.RootUI, TEXT("ConfirmLeave"));
        if (Confirm) { Confirm->OnClicked.Broadcast(); }
        return Check(Confirm != nullptr, TEXT("The in-match menu offers the shared leave confirmation"));
    }

    bool Tick();
    bool TickMatch(ALDPlayerController& PC, UWorld& World, ALDGameState& Public);
    bool TickFrontend(ALDPlayerController& PC, UWorld& World);
    bool TickRefused(ALDPlayerController& PC, UWorld& World);
    bool CheckLeaveResult(ALDPlayerController& PC, const ALDGameState& Public);
};

bool FLDDemoSessionFaultProbe::CheckLeaveResult(ALDPlayerController& PC, const ALDGameState& Public)
{
    if (PC.HUDUI) { PC.HUDUI->RefreshReadModel(); }
    const FLDResultView& Result = Public.MatchResult;
    const FLDResultRow* Leaver = Result.Rows.FindByPredicate([this](const FLDResultRow& Row) { return Row.ContenderId == LeaverId; });
    Check(Result.Reason == ELDMatchEndReason::HostEnded && !Result.Winner.IsValid(),
        TEXT("A Host-ended match records no winner"));
    Check(Result.Rows.Num() == 3 && Leaver && Leaver->bLeft && Leaver->Deaths == 0,
        TEXT("The result keeps the leaver as left, not dead"));
    Check(!Result.Rows.ContainsByPredicate([this](const FLDResultRow& Row) { return Row.Kills != 0 || (Row.ContenderId != LeaverId && Row.bLeft); }),
        TEXT("Leaving credits no kill and marks no one else as left"));
    const UButton* Leave = FindIn<UButton>(PC.HUDUI, TEXT("ResultLeaveRoom"));
    const UTextBlock* Caption = Leave ? Cast<UTextBlock>(Leave->GetContent()) : nullptr;
    Check(Caption && Caption->GetText().ToString()
        == (PC.OwnerState.bLobbyHost ? TEXT("关闭房间并返回主菜单") : TEXT("离开房间并返回主菜单")),
        TEXT("The result leave action explains whether it closes the room or only leaves it"));
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
        // Refuse: the full Lobby is announced once, and the Host holds it until the joiner was refused as full.
        if (Mode == ELDSessionFaultMode::Refuse && Role == TEXT("Host") && !Announced)
        {
            if (!Mark(TEXT("HostFull.json"))) { return Finish(); }
            Announced = true;
        }
        if (Mode == ELDSessionFaultMode::Leave && Role == TEXT("Client2") && !LobbyLeaveChecked)
        {
            if (!CheckLobbyLeaveConfirmation(PC, Lobby)) { return Finish(); }
            LobbyLeaveChecked = true;
        }
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
        if (!AllReady || StartSent || !AllMarkers(TEXT("Id.json"))
            || (Mode == ELDSessionFaultMode::Refuse && !Exists(TEXT("JoinerFull.json")))) { return true; }
        // Race: the ready room is announced once, and the Host starts only after the joiner's PreLogin passed.
        if (Mode == ELDSessionFaultMode::Race)
        {
            if (!Announced)
            {
                if (!Mark(TEXT("HostRaceReady.json"))) { return Finish(); }
                Announced = true;
            }
            if (!HasJoinLoadingRoom(World)) { return true; }
            Summary->SetBoolField(TEXT("started_while_join_loads"), true);
        }
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
            || ((Mode == ELDSessionFaultMode::HostLoss || Mode == ELDSessionFaultMode::HostQuit) && Role == TEXT("Client1"))
            || Mode == ELDSessionFaultMode::HostLeave;
        if ((Mode == ELDSessionFaultMode::HostLoss || Mode == ELDSessionFaultMode::HostLeave || Mode == ELDSessionFaultMode::HostQuit)
            && Role == TEXT("Client1") && !SaveMatchPart()) { return Finish(); }
        Stage = ELDSessionFaultStage::Match;
        StageAt = FPlatformTime::Seconds();
        return true;
    case ELDSessionFaultStage::Match:
        if (Mode == ELDSessionFaultMode::Refuse || Mode == ELDSessionFaultMode::Race)
        {
            // The fillers only hold the room; the Host checks that no refused join reached the running match.
            if (Role != TEXT("Host")) { return Finish(); }
            if (!Exists(TEXT("Joiner.json"))) { return true; }
            auto* Authority = World.GetAuthGameMode<ALDGameMode>();
            Check(Authority && Authority->IsGameplayWriteOpen(Authority->GetMatchContext())
                && RemoteControllers(World) == ExpectedHumans() - 1, TEXT("Refused joins never enter the room or its running match"));
            return Finish();
        }
        if (Mode == ELDSessionFaultMode::HostLoss || Mode == ELDSessionFaultMode::HostLeave || Mode == ELDSessionFaultMode::HostQuit)
        {
            if (Role == TEXT("Client1"))
            {
                if (View.Phase == ELDMatchPhase::Results && !MarkedResults) { MarkedResults = Mark(SawResults); }
                return true;
            }
            if (LeaveSent || !Exists(TEXT("Client1InMatch.json")) || !Exists(TEXT("Client1Match.json"))) { return true; }
            auto* Authority = World.GetAuthGameMode<ALDGameMode>();
            const bool bLive = Authority && Authority->IsGameplayWriteOpen(Authority->GetMatchContext())
                && PC.OwnerState.LifePhase == ELDLifePhase::Alive && RemoteControllers(World) == 1;
            Summary->SetNumberField(TEXT("generation"), double(View.Generation));
            if (Mode == ELDSessionFaultMode::HostLoss)
            {
                Check(bLive, TEXT("The Host holds a live match with its owning client before it is lost"));
                return Finish();
            }
            if (Mode == ELDSessionFaultMode::HostQuit)
            {
                // The process quits normally with its client still connected: the engine tears the world down
                // (PreExit) instead of changing map, so no room-close step runs first.
                if (!Check(bLive, TEXT("The Host holds a live match with its owning client before it quits"))
                    || !Mark(HostQuitRequested)) { return Finish(); }
                if (!Finish()) { return false; }
                UE_LOG(LogLDTest, Display, TEXT("LD_DEMO_SESSION_FAULT_HOST_QUIT generation=%u"), View.Generation);
                FPlatformMisc::RequestExitWithStatus(false, 0);
                return false;
            }
            // HostLeave: the Host leaves through its real menu and continues on its own frontend.
            if (!Check(bLive, TEXT("The Host holds a live match with its owning client before it leaves"))
                || !PressLeave(PC) || !SaveMatchPart() || !Mark(HostLeaveRequested)) { return Finish(); }
            LeaveSent = true;
            return true;
        }
        if (Role == TEXT("Client2"))
        {
            if (LeaveSent || !AllMarkers(TEXT("InMatch.json"))) { return true; }
            if (!PressLeave(PC) || !SaveMatchPart() || !Mark(LeaveRequested)) { return Finish(); }
            LeaveSent = true;
            return true;
        }
        if (Role == TEXT("Client1"))
        {
            if (View.Phase != ELDMatchPhase::Results) { return true; }
            if (Public.MatchResult.Generation != View.Generation || Public.MatchResult.ResultId != View.ResultId) { return true; }
            CheckLeaveResult(PC, Public);
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
        CheckLeaveResult(PC, Public);
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
    if (Mode == ELDSessionFaultMode::Leave || (Mode == ELDSessionFaultMode::HostLeave && Role == TEXT("Host")))
    {
        if (FPlatformTime::Seconds() - StageAt < 2.) { return true; }
        Check(Instance->ConnectionMessage.IsEmpty(), TEXT("A voluntary leave is not reported as a failure"));
        Check(FindShownText(Root, [](const FString& Shown) { return Shown.Contains(JoinFailedPrefix) || Shown.Contains(InterruptedText); }).IsEmpty(),
            TEXT("The menu shows no failure or interruption after a voluntary leave"));
        Summary->SetBoolField(TEXT("left_to_menu"), true);
        return Finish();
    }
    if (Mode == ELDSessionFaultMode::HostLeave)
    {
        // The client of a room its Host closed: the Host-left reason, not the connection loss that followed it.
        const FString Shown = FindShownText(Root, [](const FString& Text)
        { return Text.Contains(HostLeftText) || Text.Contains(InterruptedText) || Text.StartsWith(JoinFailedPrefix); });
        if (Instance->ConnectionMessage.IsEmpty() || Shown.IsEmpty()) { return true; }
        Check(Exists(HostLeaveRequested), TEXT("The room closed because the Host left through its menu"));
        Check(Shown.Contains(HostLeftText) && Instance->ConnectionMessage.Contains(HostLeftText) && !Shown.Contains(InterruptedText),
            TEXT("The client shows that the Host left, not a lost connection"));
        Check(!Exists(SawResults), TEXT("A closed room never produced a result table"));
        Check(!Shown.StartsWith(JoinFailedPrefix), TEXT("A closed room is not reported as a failed join"));
        Summary->SetStringField(TEXT("notice"), Shown);
        Summary->SetStringField(TEXT("message"), Instance->ConnectionMessage);
        return Finish();
    }
    if (Mode == ELDSessionFaultMode::HostLoss || Mode == ELDSessionFaultMode::HostQuit)
    {
        const bool bQuit = Mode == ELDSessionFaultMode::HostQuit;
        const FString Shown = FindShownText(Root, [](const FString& Text) { return Text.Contains(InterruptedText); });
        if (!Instance->ConnectionMessage.Contains(InterruptedText) || Shown.IsEmpty()) { return true; }
        Check(Exists(bQuit ? HostQuitRequested : HostKilled), bQuit ? TEXT("The interruption follows the Host quitting mid-match")
            : TEXT("The interruption follows the Host process being killed mid-match"));
        Check(!Exists(SawResults), bQuit ? TEXT("A Host quit never produced a result table") : TEXT("Host loss never produced a result table"));
        Check(!Shown.Contains(JoinFailedPrefix), TEXT("A lost session is not reported as a failed join"));
        Summary->SetStringField(TEXT("notice"), Shown);
        Summary->SetStringField(TEXT("message"), Instance->ConnectionMessage);
        return Finish();
    }
    if (Mode == ELDSessionFaultMode::Refuse || Mode == ELDSessionFaultMode::Race) { return TickRefused(PC, World); }
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

// Refuse: real Join presses against a live Host, each refused with its own coded reason. The joiner announces another
// product version, so a version check would refuse any attempt that reached it; the Host's state marker decides which
// refusal comes first. Race: one Join that passes PreLogin is refused by the final admission check after the Host
// started; that client loaded the room first, so its frontend may show the returned-to-menu context.
bool FLDDemoSessionFaultProbe::TickRefused(ALDPlayerController& PC, UWorld& World)
{
    struct FRefusal { const TCHAR* Waits; const TCHAR* Code; const TCHAR* Reason; const TCHAR* CheckName; const TCHAR* Field; const TCHAR* Marker; };
    static const FRefusal Refused[] = {
        {TEXT("HostJoined.json"), LDDemoEntry::JoinRefusal::ProductMismatch, TEXT("版本与房主不兼容"),
            TEXT("A version mismatch is refused with its own reason"), TEXT("version_mismatch_notice"), TEXT("JoinerMismatch.json")},
        {TEXT("HostFull.json"), LDDemoEntry::JoinRefusal::RoomFull, TEXT("房间已满"),
            TEXT("A full room is refused with its own reason"), TEXT("room_full_notice"), TEXT("JoinerFull.json")},
        {TEXT("HostInMatch.json"), LDDemoEntry::JoinRefusal::MatchAlreadyStarted, TEXT("对局已开始"),
            TEXT("A started match is refused with its own reason"), TEXT("match_started_notice"), nullptr}};
    static const FRefusal Raced[] = {
        {TEXT("HostRaceReady.json"), LDDemoEntry::JoinRefusal::MatchAlreadyStarted, TEXT("对局已开始"),
            TEXT("A join that loses the race to the match start is refused with its own reason"), TEXT("race_notice"), nullptr}};
    const bool bRace = Mode == ELDSessionFaultMode::Race;
    const TArrayView<const FRefusal> Refusals = bRace ? TArrayView<const FRefusal>(Raced) : TArrayView<const FRefusal>(Refused);
    UUserWidget* Root = PC.RootUI;
    if (!Root) { return true; }
    UEditableTextBox* Name = FindIn<UEditableTextBox>(Root, TEXT("PlayerName"));
    UEditableTextBox* Address = FindIn<UEditableTextBox>(Root, TEXT("JoinAddress"));
    UEditableTextBox* Port = FindIn<UEditableTextBox>(Root, TEXT("JoinPort"));
    UButton* Join = FindIn<UButton>(Root, TEXT("Join"));
    if (!Check(Name && Address && Port && Join, TEXT("The frontend offers the name, address, port and Join controls"))) { return Finish(); }
    const FRefusal& Expected = Refusals[Attempt];
    const FString Notice = FindShownText(Root, [](const FString& Text)
    {
        return Text.StartsWith(ConnectingPrefix) || Text.StartsWith(JoinFailedPrefix) || Text.StartsWith(ReturnedPrefix)
            || Text.StartsWith(EndedWithoutReason);
    });
    if (StageAt == 0)
    {
        if (!Exists(Expected.Waits)) { return true; }
        PreviousNotice = Notice;
        Name->SetText(FText::FromString(TEXT("拒绝测试")));
        Address->SetText(FText::FromString(TEXT("127.0.0.1")));
        Port->SetText(FText::FromString(FString::FromInt(HostPort)));
        Join->OnClicked.Broadcast();
        StageAt = FPlatformTime::Seconds();
        return true;
    }
    const double Elapsed = FPlatformTime::Seconds() - StageAt;
    if (!SawConnecting)
    {
        if (Notice.StartsWith(ConnectingPrefix)) { SawConnecting = true; return true; }
        // A loopback refusal can replace the connecting notice before the next probe tick.
        if (Elapsed < 1. || Notice.IsEmpty() || Notice == PreviousNotice)
        {
            if (Elapsed < 10.) { return true; }
            Check(false, TEXT("A Join press starts a connection attempt"));
            return Finish();
        }
        SawConnecting = true;
    }
    if (Notice.IsEmpty() || Notice.StartsWith(ConnectingPrefix)) { return true; }
    Summary->SetStringField(Expected.Field, Notice);
    Check((Notice.StartsWith(JoinFailedPrefix) || (bRace && Notice.StartsWith(ReturnedPrefix))) && Notice.Contains(Expected.Reason)
        && Notice.Contains(FString::Printf(TEXT("（%s）"), Expected.Code)), Expected.CheckName);
    Check(UWorld::RemovePIEPrefix(World.GetOutermost()->GetName()) == LDDemoEntry::FrontendMap, TEXT("A refused join stays on the frontend"));
    Check(Join->GetIsEnabled(), TEXT("The Join control is usable again after a refusal"));
    if (!Passed || (Expected.Marker && !Mark(Expected.Marker))) { return Finish(); }
    if (++Attempt >= Refusals.Num()) { return Finish(); }
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
    FParse::Value(FCommandLine::Get(), TEXT("LDDemoSessionFaultHostPort="), Probe->HostPort);
    const bool bArguments = FParse::Value(FCommandLine::Get(), TEXT("LDDemoSessionFaultOutput="), Probe->Output)
        && FParse::Value(FCommandLine::Get(), TEXT("LDDemoSessionFaultRole="), Probe->Role)
        && FParse::Value(FCommandLine::Get(), TEXT("LDDemoSessionFaultMode="), ModeText);
    Probe->Mode = ModeText == TEXT("HostLoss") ? ELDSessionFaultMode::HostLoss
        : ModeText == TEXT("Join") ? ELDSessionFaultMode::Join
        : ModeText == TEXT("Refuse") ? ELDSessionFaultMode::Refuse
        : ModeText == TEXT("Race") ? ELDSessionFaultMode::Race
        : ModeText == TEXT("HostLeave") ? ELDSessionFaultMode::HostLeave
        : ModeText == TEXT("HostQuit") ? ELDSessionFaultMode::HostQuit : ELDSessionFaultMode::Leave;
    const bool bJoiner = Probe->Role == TEXT("Joiner");
    const bool bRoomRole = Probe->Role == TEXT("Host") || Probe->Role == TEXT("Client1");
    const bool bLiveHostJoiner = Probe->Mode == ELDSessionFaultMode::Refuse || Probe->Mode == ELDSessionFaultMode::Race;
    const bool bRole = Probe->Mode == ELDSessionFaultMode::Join ? bJoiner
        : Probe->Mode == ELDSessionFaultMode::Refuse ? bJoiner || bRoomRole || Probe->Role == TEXT("Client2")
        : Probe->Mode == ELDSessionFaultMode::Race ? bJoiner || bRoomRole
        : bRoomRole || (Probe->Mode == ELDSessionFaultMode::Leave && Probe->Role == TEXT("Client2"));
    // The joiners live on the frontend; one probe per process follows every frontend reload.
    const bool bFrontendJoiner = Probe->Mode == ELDSessionFaultMode::Join || (bLiveHostJoiner && bJoiner);
    if (!bArguments || !bRole || (ModeText != TEXT("Leave") && ModeText != TEXT("HostLoss") && ModeText != TEXT("Join")
            && ModeText != TEXT("Refuse") && ModeText != TEXT("Race") && ModeText != TEXT("HostLeave") && ModeText != TEXT("HostQuit"))
        || Timeout < 120 || Timeout > 900 || (Probe->Mode == ELDSessionFaultMode::Join && (Probe->ClosedPort < 1 || Probe->ClosedPort > 65535))
        || (bLiveHostJoiner && bJoiner && (Probe->HostPort < 1 || Probe->HostPort > 65535))
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Probe->Output),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts"))))
    {
        UE_LOG(LogLDTest, Error, TEXT("LD_DEMO_SESSION_FAULT_PROBE Invalid isolated test arguments."));
        FPlatformMisc::RequestExitWithStatus(false, 2);
        return;
    }
    const FString Map = UWorld::RemovePIEPrefix(Controller.GetWorld()->GetOutermost()->GetName());
    if (Map == LDDemoEntry::FrontendMap)
    {
        // The frontend instance owns a joiner role, or continues a role that left, lost or saw the Host close or quit its match.
        const bool bContinue = bFrontendJoiner
            || (Probe->Mode == ELDSessionFaultMode::Leave && Probe->Role == TEXT("Client2") && Probe->Exists(LeaveRequested))
            || ((Probe->Mode == ELDSessionFaultMode::HostLoss || Probe->Mode == ELDSessionFaultMode::HostQuit)
                && Probe->Role == TEXT("Client1") && Probe->Exists(TEXT("Client1InMatch.json")))
            || (Probe->Mode == ELDSessionFaultMode::HostLeave
                && Probe->Exists(Probe->Role == TEXT("Host") ? FString(HostLeaveRequested) : FString(TEXT("Client1InMatch.json"))));
        if (!bContinue) { return; }
        if (bFrontendJoiner)
        {
            if (const TSharedPtr<FLDDemoSessionFaultProbe> Active = JoinProbe.Pin())
            {
                Active->Controller = &Controller;
                return;
            }
            JoinProbe = Probe;
        }
        Probe->Stage = ELDSessionFaultStage::Frontend;
        Probe->StageAt = bFrontendJoiner ? 0. : FPlatformTime::Seconds();
    }
    else if (Map == LDDemoEntry::WorldMap && bFrontendJoiner)
    {
        // Only the Race joiner briefly loads the room before its refusal; a Refuse joiner that reaches it means a
        // refusal failed, and its one probe reports and exits.
        const TSharedPtr<FLDDemoSessionFaultProbe> Active = JoinProbe.Pin();
        if (Active && Probe->Mode == ELDSessionFaultMode::Refuse)
        {
            Active->Check(false, TEXT("A refused join never enters the room"));
            Active->Finish();
        }
        return;
    }
    else if (Map != LDDemoEntry::WorldMap) { return; }
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
