#include "Framework/LDGameInstance.h"
#include "Core/LDLog.h"
#include "UI/LDCursor.h"
#include "Audio/LDAudioSubsystem.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDGameEngine.h"
#include "Engine/Engine.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Tests/LDDemoEntryProbe.h"
#include "Character/LDCharacter.h"
#include "Framework/LDPlayerController.h"
#include "Navigation/LDNavigationComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/CommandLine.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    // A pending connection that fails is a failed join; an established game connection that fails interrupts
    // a session. The frontend adds its own context prefix, so these texts carry only the reason. The engine's raw
    // detail (addresses, connection names, timers) stays in its own log line and is not shown to the player.
    FString DescribeNetworkFailure(ENetworkFailure::Type Type, bool bInSession)
    {
        const TCHAR* Reason = TEXT("网络连接失败");
        bool bSessionLoss = bInSession;
        switch (Type)
        {
        case ENetworkFailure::ConnectionLost: Reason = bInSession ? TEXT("与房主的连接已断开") : TEXT("与房间的连接已断开"); break;
        case ENetworkFailure::ConnectionTimeout: Reason = bInSession ? TEXT("与房主的连接超时") : TEXT("连接超时"); break;
        case ENetworkFailure::PendingConnectionFailure: Reason = TEXT("无法与房间建立连接"); bSessionLoss = false; break;
        case ENetworkFailure::FailureReceived: Reason = bInSession ? TEXT("房主结束了连接") : TEXT("房主拒绝了加入"); break;
        case ENetworkFailure::OutdatedClient: Reason = TEXT("本机版本比房主旧"); break;
        case ENetworkFailure::OutdatedServer: Reason = TEXT("房主版本比本机旧"); break;
        case ENetworkFailure::NetGuidMismatch:
        case ENetworkFailure::NetChecksumMismatch: Reason = TEXT("与房主的数据不一致"); break;
        case ENetworkFailure::NetDriverAlreadyExists:
        case ENetworkFailure::NetDriverCreateFailure:
        case ENetworkFailure::NetDriverListenFailure: Reason = TEXT("本机网络初始化失败"); bSessionLoss = false; break;
        default: break;
        }
        const FString Text = FString::Printf(TEXT("%s%s（%s）"), bSessionLoss ? TEXT("会话中断：") : TEXT(""), Reason, ENetworkFailure::ToString(Type));
        const bool bJoinHint = !bInSession && (Type == ENetworkFailure::ConnectionLost || Type == ENetworkFailure::ConnectionTimeout
            || Type == ENetworkFailure::PendingConnectionFailure);
        return Text + (bJoinHint ? TEXT("。请检查地址，并确认房主正在运行。") : TEXT("。"));
    }

    FString DescribeTravelFailure(ETravelFailure::Type Type)
    {
        const TCHAR* Reason = TEXT("进入房间失败");
        switch (Type)
        {
        case ETravelFailure::NoLevel:
        case ETravelFailure::LoadMapFailure:
        case ETravelFailure::PackageMissing: Reason = TEXT("无法加载对局地图"); break;
        case ETravelFailure::PackageVersion: Reason = TEXT("地图版本与房主不一致"); break;
        case ETravelFailure::InvalidURL: Reason = TEXT("房间地址无效"); break;
        case ETravelFailure::PendingNetGameCreateFailure: Reason = TEXT("无法与房间建立连接"); break;
        case ETravelFailure::TravelFailure:
        case ETravelFailure::ServerTravelFailure:
        case ETravelFailure::ClientTravelFailure: Reason = TEXT("切换地图失败"); break;
        default: break;
        }
        return FString::Printf(TEXT("%s（%s）"), Reason, ETravelFailure::ToString(Type))
            + (Type == ETravelFailure::PendingNetGameCreateFailure ? TEXT("。请检查地址，并确认房主正在运行。") : TEXT("。"));
    }
}

void ULDGameInstance::RecordEntryRejectionBeforeWorld(const FString& URL, const FString& Reason)
{
    LastRejectedEntryURL = URL;
    ConnectionMessage = Reason;
}

void ULDGameInstance::StartGameInstance()
{
    FString Requested;
    if (!GetMapOverrideName(FCommandLine::Get(), Requested)) { Requested = LDDemoEntry::FrontendMap; }
    FURL DefaultURL;
    DefaultURL.LoadURLConfig(TEXT("DefaultPlayer"), GGameIni);
    FString Error;
    auto* Engine = GetEngine();
    if (!Engine || !Engine->IsA<ULDGameEngine>() || !WorldContext)
    {
        UE_LOG(LogLDMatch, Error, TEXT("LD_DEMO_ENTRY_REJECTED Missing configured Demo game engine or world context."));
        FPlatformMisc::RequestExitWithStatus(false, 1);
        return;
    }
    const FName ContextHandle = WorldContext->ContextHandle;
    const auto Outcome = Engine->Browse(*WorldContext, FURL(&DefaultURL, *Requested, TRAVEL_Partial), Error);
    if (Outcome == EBrowseReturnVal::Failure)
    {
        if (ConnectionMessage.IsEmpty()) { ConnectionMessage = Error; }
        WorldContext = &Engine->GetWorldContextFromHandleChecked(ContextHandle);
        FString RecoveryError;
        const auto Recovery = Engine->Browse(*WorldContext, FURL(nullptr, LDDemoEntry::FrontendMap, TRAVEL_Absolute), RecoveryError);
        if (Recovery != EBrowseReturnVal::Success)
        {
            UE_LOG(LogLDMatch, Error, TEXT("LD_DEMO_FRONTEND_RECOVERY_FAILED %s"), *RecoveryError);
            FPlatformMisc::RequestExitWithStatus(false, 1);
            return;
        }
    }
    BroadcastOnStart();
}

AGameModeBase* ULDGameInstance::CreateGameModeForURL(FURL URL, UWorld* World)
{
    if (EntryReturnTicker.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(EntryReturnTicker);
        EntryReturnTicker.Reset();
    }
    if (EntryAttemptSerial == MAX_uint64)
    {
        ConnectionMessage = TEXT("房间进入序号已用尽，请重启游戏。");
        UE_LOG(LogLDMatch, Error, TEXT("LD_DEMO_ENTRY_REJECTED %s"), *ConnectionMessage);
        return nullptr;
    }
    const uint64 Attempt = ++EntryAttemptSerial;
    FString Options, Error;
    for (const FString& Option : URL.Op) { Options += TEXT("?") + Option; }
    if (!World) { World = GetWorld(); }
    const auto* Settings = World ? World->GetWorldSettings() : nullptr;
    if (!World || !Settings)
    {
        Error = TEXT("Demo 世界缺少有效的 WorldSettings，无法进入房间。");
    }
    else if (LDDemoEntry::ValidateSelection(World->GetOutermost()->GetName(), Options, Error)
        && Settings->DefaultGameMode && Settings->DefaultGameMode != ALDGameMode::StaticClass())
    {
        Error = TEXT("Demo 世界保存了已退役的 GameMode，必须使用 LDGameMode。");
    }
    if (!Error.IsEmpty())
    {
#if WITH_DEV_AUTOMATION_TESTS
        if (World) { LDDemoTests::ObserveRejectedEntry(*this, *World); }
#endif
        LastRejectedEntryURL = URL.ToString();
        ConnectionMessage = Error;
        UE_LOG(LogLDMatch, Error, TEXT("LD_DEMO_ENTRY_REJECTED %s"), *Error);
        if (World && World->GetGameInstance() == this)
        {
            const TWeakObjectPtr<UWorld> RejectedWorld = World;
            EntryReturnTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,
                [this, Attempt, RejectedWorld](float)
                {
                    EntryReturnTicker.Reset();
                    if (EntryAttemptSerial == Attempt && RejectedWorld.IsValid() && GetWorld() == RejectedWorld.Get())
                    {
                        UGameplayStatics::OpenLevel(this, LDDemoEntry::FrontendMap);
                    }
                    return false;
                }));
        }
        return nullptr;
    }
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Spawn.ObjectFlags |= RF_Transient;
    return World->SpawnActor<ALDGameMode>(ALDGameMode::StaticClass(), Spawn);
}

bool ULDGameInstance::AllocateMatchGeneration(uint32& Generation, FString& Error)
{
    Error.Reset();
    if (!LDMatchLifecycle::AllocateGeneration(AuthorityGeneration, Generation))
    {
        Error = TEXT("对局代次已用尽，请重启游戏后再创建房间。");
        UE_LOG(LogLDMatch, Error, TEXT("LD_MATCH_GENERATION_EXHAUSTED"));
        return false;
    }
    return true;
}

void ULDGameInstance::OnStart()
{
    Super::OnStart();
    // A dedicated server has no viewport and no pointer.
    if (UGameViewportClient* Viewport = GetGameViewportClient()) { LDCursor::Install(*Viewport); }
}

void ULDGameInstance::Init()
{
    Super::Init();
    UseBorderlessWindow();
    // Settings normally load on first use, which applies their volume; only an object that already exists is applied here.
    if (LocalControlSettings) { ApplyAudioPreferences(); }
#if WITH_DEV_AUTOMATION_TESTS
    LDDemoTests::StartEntryObservation(*this);
#endif
    NetworkHandle = GEngine->OnNetworkFailure().AddUObject(this, &ULDGameInstance::NetworkFailed);
    TravelHandle = GEngine->OnTravelFailure().AddUObject(this, &ULDGameInstance::TravelFailed);
    LocalSettingsTickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ULDGameInstance::TickLocalSettings));
    PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this, &ULDGameInstance::LocalSettingsPreLoadMap);
    PreClientTravelHandle = OnNotifyPreClientTravel().AddUObject(this, &ULDGameInstance::LocalSettingsPreClientTravel);
    EngineExitHandle = FCoreDelegates::OnEnginePreExit.AddUObject(this, &ULDGameInstance::PrepareLocalSettingsForTravel);
}
void ULDGameInstance::Shutdown()
{
    PrepareLocalSettingsForTravel();
    FTSTicker::GetCoreTicker().RemoveTicker(LocalSettingsTickHandle);
    FCoreUObjectDelegates::PreLoadMapWithContext.Remove(PreLoadMapHandle);
    OnNotifyPreClientTravel().Remove(PreClientTravelHandle);
    FCoreDelegates::OnEnginePreExit.Remove(EngineExitHandle);
#if WITH_DEV_AUTOMATION_TESTS
    LDDemoTests::StopEntryObservation(*this);
#endif
    if (EntryReturnTicker.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(EntryReturnTicker);
        EntryReturnTicker.Reset();
    }
    GEngine->OnNetworkFailure().Remove(NetworkHandle);
    GEngine->OnTravelFailure().Remove(TravelHandle);
    Super::Shutdown();
}
void ULDGameInstance::NetworkFailed(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& ErrorString)
{
    // Pending connections have no world on the driver yet. Resolve their actual
    // owning context rather than losing the message or leaking it to another PIE instance.
    bool bPending = false;
    if (!World && Driver)
    {
        if (const FWorldContext* Context = GEngine->GetWorldContextFromPendingNetGameNetDriver(Driver))
        {
            if (Context->OwningGameInstance != this) { return; }
            World = Context->World();
            bPending = true;
        }
    }
    const bool bInSession = !bPending && World && !World->GetMapName().Contains(TEXT("L_Frontend"));
    // A Host's PreLogin refusal is the failure text of the pending connection; only its stable code is shown.
    FString Refusal;
    if (!bInSession && LDDemoEntry::DescribeJoinRefusal(ErrorString, Refusal)) { ReturnAfterFailure(World, Refusal); return; }
    ReturnAfterFailure(World, DescribeNetworkFailure(Type, bInSession));
}
void ULDGameInstance::TravelFailed(UWorld* World, ETravelFailure::Type Type, const FString&) { ReturnAfterFailure(World, DescribeTravelFailure(Type)); }
void ULDGameInstance::RememberRoomURL(const FString& URL)
{
    ReconnectURL=URL.Replace(TEXT("?LDReconnect=1"),TEXT(""));
}

void ULDGameInstance::ClearReconnect()
{
    ReconnectTicket.Invalidate();ReconnectURL.Reset();
}

bool ULDGameInstance::ReconnectRoom(ALDPlayerController& Controller)
{
    if(!CanReconnect())
    {ConnectionMessage=TEXT("没有可恢复的本局连接。");return false;}
    Controller.ClientTravel(ReconnectURL+TEXT("?LDReconnect=1"),TRAVEL_Absolute);
    return true;
}
void ULDGameInstance::ReturnAfterRefusal(UWorld* World, const FString& ServerMessage)
{
    FString Reason;
    if (!LDDemoEntry::DescribeJoinRefusal(ServerMessage, Reason)) { Reason = TEXT("房主拒绝了加入。"); }
    ReturnAfterFailure(World, Reason);
}
void ULDGameInstance::ReturnAfterRoomClosed(UWorld* World, const FString& Reason) { ReturnAfterFailure(World, Reason); }
void ULDGameInstance::ReturnAfterFailure(UWorld* World, const FString& Error)
{
    if (!World || World->GetGameInstance() != this) { return; }
    // A refusal or a closed room is followed by its connection closing: the room's first reason stays the shown one.
    if (World == LeavingWorld.Get()) { return; }
    PrepareLocalSettingsForTravel();
    ConnectionMessage = Error;
    if (!World->GetMapName().Contains(TEXT("L_Frontend")))
    {
        LeavingWorld = World;
        World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
        {
            UGameplayStatics::OpenLevel(this, TEXT("/Game/Lockdown/Generated/Maps/L_Frontend"));
        }));
    }
}

ULDControlSettings* ULDGameInstance::GetLocalControlSettings()
{
    if (LocalControlSettings) { return LocalControlSettings; }
    const bool NeedsExplicitStorage = FApp::IsUnattended() || IsRunningCommandlet();
    const auto Storage = bUseIsolatedSettings ? IsolatedSettingsStorage
        : NeedsExplicitStorage ? FLDLocalSettingsStorage() : FLDLocalSettingsStorage::User();
    auto Loaded = ULDControlSettings::LoadLocalSettings(Storage);
    LocalSettingsMessage = Loaded.Message;
    LocalControlSettings = Loaded.Settings;
    if (!LocalControlSettings)
    {
        LocalControlSettings = ULDControlSettings::CreateSessionSettings(Storage);
        if (Loaded.RecoverablePreferences)
        {
            LocalControlSettings->bEdgePan = Loaded.RecoverablePreferences->bEdgePan;
            LocalControlSettings->bStartFollowing = Loaded.RecoverablePreferences->bStartFollowing;
            LocalControlSettings->TextScale = Loaded.RecoverablePreferences->TextScale;
            LocalControlSettings->Zoom = Loaded.RecoverablePreferences->Zoom;
            LocalControlSettings->PlayerName = Loaded.RecoverablePreferences->PlayerName;
            LocalControlSettings->MasterVolume = Loaded.RecoverablePreferences->MasterVolume;
            LocalControlSettings->MusicVolume = Loaded.RecoverablePreferences->MusicVolume;
            LocalControlSettings->CameraPanCmPerSec = Loaded.RecoverablePreferences->CameraPanCmPerSec;
            LocalControlSettings->MinimapOrientation = Loaded.RecoverablePreferences->MinimapOrientation;
            LocalControlSettings->bQuickCast = Loaded.RecoverablePreferences->bQuickCast;
            LocalControlSettings->bAutoAttackAfterCast = Loaded.RecoverablePreferences->bAutoAttackAfterCast;
            LocalControlSettings->bShowNetworkDiagnostics = Loaded.RecoverablePreferences->bShowNetworkDiagnostics;
            LocalControlSettings->bShowFPS = Loaded.RecoverablePreferences->bShowFPS;
            LocalControlSettings->bReduceCombatEffects = Loaded.RecoverablePreferences->bReduceCombatEffects;
            LocalControlSettings->bCameraShake = Loaded.RecoverablePreferences->bCameraShake;
        }
        LocalControlSettings->RequireRecovery();
        LocalSettingsMessage += TEXT(" 当前按键只在本次运行有效，请到设置中确认恢复。");
        UE_LOG(LogLDMatch, Warning, TEXT("%s"), *LocalSettingsMessage);
    }
    ApplyAudioPreferences();
    return LocalControlSettings;
}

ULDControlSettings* ULDGameInstance::UseIsolatedLocalSettings(const FGuid& Fixture)
{
    // Never flush a real user's object while switching a probe to its explicit fixture.
    IsolatedSettingsStorage = FLDLocalSettingsStorage::Automation(Fixture);
    bUseIsolatedSettings = true;
    LocalControlSettings = nullptr;
    LocalSettingsFlushAt = 0;
    return GetLocalControlSettings();
}

void ULDGameInstance::ApplyControlPreferences()
{
    for (const auto* Local : GetLocalPlayers())
    {
        auto* PC = Local ? Cast<ALDPlayerController>(Local->PlayerController) : nullptr;
        if (!PC || PC->Controls != LocalControlSettings) { continue; }
        PC->Navigation->bEdgePan = LocalControlSettings->bEdgePan;
        PC->Navigation->bFollowCamera = LocalControlSettings->bStartFollowing;
        if (auto* Body = Cast<ALDCharacter>(PC->GetPawn())) { Body->CameraArm->TargetArmLength = LocalControlSettings->Zoom; }
        PC->RebuildV05Mappings();
    }
}

void ULDGameInstance::ApplyAudioPreferences()
{
    if (auto* AudioOutput = GetSubsystem<ULDAudioSubsystem>())
    {
        const auto* Defaults = GetDefault<ULDControlSettings>();
        AudioOutput->ApplyMasterVolume(LocalControlSettings ? LocalControlSettings->MasterVolume : Defaults->MasterVolume);
        AudioOutput->ApplyMusicVolume(LocalControlSettings ? LocalControlSettings->MusicVolume : Defaults->MusicVolume);
    }
}

FLDLocalSettingsSaveResult ULDGameInstance::ApplyLocalSettings(const ULDControlSettings& Draft)
{
    FString Error;
    if (!LocalControlSettings || !LocalControlSettings->ApplyLocalSettings(Draft, Error))
    {
        LocalSettingsMessage = Error.IsEmpty() ? TEXT("本地设置不可用。") : Error;
        return { ELDLocalSettingsSaveStatus::Invalid, LocalSettingsMessage };
    }
    ApplyControlPreferences();
    ApplyAudioPreferences();
    return FlushLocalSettings();
}

FLDLocalSettingsSaveResult ULDGameInstance::SetAppliedMasterVolume(float Linear)
{
    FString Error;
    if (!LocalControlSettings || !LocalControlSettings->SetAppliedMasterVolume(Linear, Error))
    {
        LocalSettingsMessage = Error.IsEmpty() ? TEXT("本地设置不可用。") : Error;
        return { ELDLocalSettingsSaveStatus::Invalid, LocalSettingsMessage };
    }
    ApplyAudioPreferences();
    RequestLocalSettingsFlush();
    // Nothing is written yet; a recovery-gated file is not replaced by the coming flush either.
    const int32 Percent = FMath::RoundToInt(100 * LocalControlSettings->MasterVolume);
    LocalSettingsMessage = LocalControlSettings->NeedsRecovery()
        ? FString::Printf(TEXT("主音量 %d%% 已在本次运行中应用；原设置文件保持不变，请先在设置中确认替换再保存。"), Percent)
        : FString::Printf(TEXT("主音量 %d%% 已应用，稍后自动保存。"), Percent);
    return { ELDLocalSettingsSaveStatus::NoChanges, LocalSettingsMessage };
}

FLDLocalSettingsSaveResult ULDGameInstance::SetAppliedMusicVolume(float Linear)
{
    FString Error;
    if (!LocalControlSettings || !LocalControlSettings->SetAppliedMusicVolume(Linear, Error))
    {
        LocalSettingsMessage = Error.IsEmpty() ? TEXT("本地设置不可用。") : Error;
        return { ELDLocalSettingsSaveStatus::Invalid, LocalSettingsMessage };
    }
    ApplyAudioPreferences();
    RequestLocalSettingsFlush();
    // Nothing is written yet; a recovery-gated file is not replaced by the coming flush either.
    const int32 Percent = FMath::RoundToInt(100 * LocalControlSettings->MusicVolume);
    LocalSettingsMessage = LocalControlSettings->NeedsRecovery()
        ? FString::Printf(TEXT("音乐音量 %d%% 已在本次运行中应用；原设置文件保持不变，请先在设置中确认替换再保存。"), Percent)
        : FString::Printf(TEXT("音乐音量 %d%% 已应用，稍后自动保存。"), Percent);
    return { ELDLocalSettingsSaveStatus::NoChanges, LocalSettingsMessage };
}

FLDLocalSettingsSaveResult ULDGameInstance::FlushLocalSettings(bool VerifyCurrent)
{
    LocalSettingsFlushAt = 0;
    if (!LocalControlSettings || (!LocalControlSettings->IsDirty() && !VerifyCurrent))
    { return { ELDLocalSettingsSaveStatus::NoChanges, TEXT("没有待保存的本地偏好修改。") }; }
    const auto Result = LocalControlSettings->SaveLocalSettings();
    LocalSettingsMessage = Result.Message;
    if (!Result.IsSaved()) { UE_LOG(LogLDMatch, Warning, TEXT("%s"), *Result.Message); }
    return Result;
}

FLDLocalSettingsSaveResult ULDGameInstance::ConfirmLocalSettingsRecovery()
{
    if (!LocalControlSettings)
    { return { ELDLocalSettingsSaveStatus::Invalid, TEXT("没有可以恢复的本地设置。") }; }
    LocalControlSettings->AllowRecoverySave();
    return FlushLocalSettings();
}

void ULDGameInstance::RequestLocalSettingsFlush()
{ LocalSettingsFlushAt = FPlatformTime::Seconds() + .5; }

void ULDGameInstance::LocalSettingsPreLoadMap(const FWorldContext& Context, const FString& Map)
{
    if (Context.OwningGameInstance == this) { PrepareLocalSettingsForTravel(); }
}
void ULDGameInstance::LocalSettingsPreClientTravel(const FString& URL, ETravelType Type, bool Seamless)
{ PrepareLocalSettingsForTravel(); }

void ULDGameInstance::PrepareLocalSettingsForTravel()
{
    FlushLocalSettings();
    if (bDisplaySettingsDirty) { RetryDisplaySave(); }
}

void ULDGameInstance::UseBorderlessWindow()
{
    // The window is not a setting: the game always runs borderless at the desktop resolution, whatever an older save
    // holds. Init runs before UGameEngine::Init creates the window, so the first window already uses it. The engine's
    // launch overrides (-windowed, -ResX/-ResY) still apply to the request; automation and several clients on one machine
    // use them. Editor PIE windows belong to the editor.
    if (GIsEditor || !FApp::CanEverRender() || IsRunningCommandlet() || !Cast<UGameEngine>(GEngine)) { return; }
    auto* Settings = GEngine->GetGameUserSettings();
    if (!Settings) { UE_LOG(LogLDMatch, Error, TEXT("No engine display settings; the window keeps its launch mode.")); return; }
    LDLocalSettings::HoldBorderlessDesktop(*Settings);
    // ApplyResolutionSettings can also adjust resolution quality; the window choice must not change quality.
    const auto Quality = Settings->ScalabilityQuality;
    Settings->ApplyResolutionSettings(true);
    Settings->ScalabilityQuality = Quality;
}

UGameUserSettings* ULDGameInstance::DisplaySettingsOwner() const
{
    if (!GEngine || !FApp::CanEverRender() || FApp::IsUnattended() || IsRunningCommandlet()
        || !GEngine->GameViewport || !GEngine->GameViewport->Viewport
        || GEngine->GameViewport->GetGameInstance() != this || bUseIsolatedSettings) { return nullptr; }
    return GEngine->GetGameUserSettings();
}

FLDDisplaySettingsResult ULDGameInstance::ReadDisplaySettings(FLDDisplaySettings& Out) const
{
    const auto* Settings = DisplaySettingsOwner();
    if (!Settings)
    { return { ELDDisplaySettingsStatus::Unavailable, TEXT("显示设置需要可交互的游戏窗口；没有应用或保存任何显示设置。") }; }
    Out = FLDDisplaySettings::Read(*Settings);
    if (!Out.IsValid())
    { return { ELDDisplaySettingsStatus::Invalid, TEXT("当前显示配置无效，已禁用显示修改。") }; }
    return { ELDDisplaySettingsStatus::Loaded, TEXT("已读取显示设置。修改在应用前只是草稿。") };
}

FLDDisplaySettingsResult ULDGameInstance::SetDisplayResult(ELDDisplaySettingsStatus Status, const FString& Message)
{
    DisplayResult = { Status, Message };
    if (Status == ELDDisplaySettingsStatus::ApplyFailed || Status == ELDDisplaySettingsStatus::PersistenceFailed)
    { UE_LOG(LogLDMatch, Warning, TEXT("%s"), *Message); }
    return DisplayResult;
}

FLDDisplaySettingsResult ULDGameInstance::ApplyDisplaySettings(const FLDDisplaySettings& Draft)
{
    auto* Settings = DisplaySettingsOwner();
    if (!Settings) { return SetDisplayResult(ELDDisplaySettingsStatus::Unavailable, TEXT("显示设置需要可交互的游戏窗口，没有应用任何显示修改。")); }
    if (!Draft.IsValid()) { return SetDisplayResult(ELDDisplaySettingsStatus::Invalid, TEXT("所选的显示参数无效，没有应用。请改选后再应用。")); }
    Settings->SetVSyncEnabled(Draft.bVSync);
    Settings->ScalabilityQuality = Draft.Quality;
    Settings->ApplyNonResolutionSettings();
    bDisplaySettingsDirty = true;
    if (Settings->IsVSyncDirty() || Scalability::GetQualityLevels() != Draft.Quality)
    { return SetDisplayResult(ELDDisplaySettingsStatus::ApplyFailed, TEXT("垂直同步或画质没有完全应用，可能被引擎覆盖设置锁定；尚未保存。")); }
    // The default render scale (0) is a no-op in the engine; without this, restoring defaults kept the old scale until restart.
    auto* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
    if (!ScreenPercentage || !LDLocalSettings::ApplyRenderScale(*ScreenPercentage, Draft.Quality.ResolutionQuality))
    { return SetDisplayResult(ELDDisplaySettingsStatus::ApplyFailed, TEXT("画质已应用，但渲染比例没有回到项目默认值；尚未保存。")); }
    return RetryDisplaySave();
}

FLDDisplaySettingsResult ULDGameInstance::RetryDisplaySave()
{
    auto* Settings = DisplaySettingsOwner();
    if (!Settings) { return SetDisplayResult(ELDDisplaySettingsStatus::Unavailable, TEXT("保存显示设置需要可交互的游戏窗口。")); }
    const auto Expected = FLDDisplaySettings::Read(*Settings);
    if (!Expected.IsValid() || Settings->IsVSyncDirty() || Scalability::GetQualityLevels() != Expected.Quality)
    { return SetDisplayResult(ELDDisplaySettingsStatus::ApplyFailed, TEXT("请求的显示设置没有完全应用，尚未保存。")); }
    bDisplaySettingsDirty = true;
    if (!GConfig)
    { return SetDisplayResult(ELDDisplaySettingsStatus::PersistenceFailed, TEXT("显示修改已应用，但配置所有者不可用，尚未确认能够保存。")); }
    Settings->SaveSettings();
    const auto Verified = LDLocalSettings::FlushAndVerifyDisplaySettingsFiles(*GConfig, GGameUserSettingsIni,
        GIsEditor ? GEditorSettingsIni : GGameUserSettingsIni, Settings->GetClass()->GetPathName(), Expected);
    if (!Verified.IsSaved()) { return SetDisplayResult(ELDDisplaySettingsStatus::PersistenceFailed, Verified.Message); }
    bDisplaySettingsDirty = false;
    return SetDisplayResult(ELDDisplaySettingsStatus::PersistenceVerified, Verified.Message);
}

bool ULDGameInstance::TickLocalSettings(float Delta)
{
    const double Now = FPlatformTime::Seconds();
    if (LocalSettingsFlushAt > 0 && Now >= LocalSettingsFlushAt) { FlushLocalSettings(); }
    return true;
}
