#include "Framework/LDGameInstance.h"
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
#include "GameFramework/InputSettings.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/CommandLine.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
    class FLDDisplayTrialInputGuard final : public IInputProcessor
    {
    public:
        explicit FLDDisplayTrialInputGuard(ULDGameInstance* Instance) : Owner(Instance) {}
        virtual void Tick(float Delta, FSlateApplication& App, TSharedRef<ICursor> Cursor) override {}
        virtual bool HandleKeyDownEvent(FSlateApplication& App, const FKeyEvent& Event) override
        {
            const auto* Instance = Owner.Get();
            if (!Instance || (!Instance->GetDisplayTrialId() && !Instance->IsDisplayRestorePending())) { return false; }
            return Event.GetKey() == EKeys::F11 || (Event.GetKey() == EKeys::Enter && Event.IsAltDown())
                || GetDefault<UInputSettings>()->ConsoleKeys.Contains(Event.GetKey());
        }
        virtual const TCHAR* GetDebugName() const override { return TEXT("LockdownDisplayTrial"); }
    private:
        TWeakObjectPtr<ULDGameInstance> Owner;
    };

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
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_ENTRY_REJECTED Missing configured Demo game engine or world context."));
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
            UE_LOG(LogTemp, Error, TEXT("LD_DEMO_FRONTEND_RECOVERY_FAILED %s"), *RecoveryError);
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
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_ENTRY_REJECTED %s"), *ConnectionMessage);
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
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_ENTRY_REJECTED %s"), *Error);
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
        UE_LOG(LogTemp, Error, TEXT("LD_MATCH_GENERATION_EXHAUSTED"));
        return false;
    }
    return true;
}

void ULDGameInstance::Init()
{
    Super::Init();
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
    ReleaseDisplayConfigGuard();
    if (DisplayInputGuard && FSlateApplication::IsInitialized())
    { FSlateApplication::Get().UnregisterInputPreProcessor(DisplayInputGuard); }
    DisplayInputGuard.Reset();
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
            LocalControlSettings->CameraPanCmPerSec = Loaded.RecoverablePreferences->CameraPanCmPerSec;
            LocalControlSettings->MinimapOrientation = Loaded.RecoverablePreferences->MinimapOrientation;
            LocalControlSettings->bQuickCast = Loaded.RecoverablePreferences->bQuickCast;
        }
        LocalControlSettings->RequireRecovery();
        LocalSettingsMessage += TEXT(" 当前按键只在本次运行有效，请到设置中确认恢复。");
        UE_LOG(LogTemp, Warning, TEXT("%s"), *LocalSettingsMessage);
    }
    ApplyAudioPreferences();
    return LocalControlSettings;
}

ULDControlSettings* ULDGameInstance::UseIsolatedLocalSettings(const FGuid& Fixture)
{
    if (DisplayTrial.Id() || bDisplayRestorePending)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot switch settings storage during a display trial or restore."));
        return nullptr;
    }
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
    { AudioOutput->ApplyMasterVolume(LocalControlSettings ? LocalControlSettings->MasterVolume : 1.f); }
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

FLDLocalSettingsSaveResult ULDGameInstance::FlushLocalSettings(bool VerifyCurrent)
{
    LocalSettingsFlushAt = 0;
    if (!LocalControlSettings || (!LocalControlSettings->IsDirty() && !VerifyCurrent))
    { return { ELDLocalSettingsSaveStatus::NoChanges, TEXT("没有待保存的本地偏好修改。") }; }
    const auto Result = LocalControlSettings->SaveLocalSettings();
    LocalSettingsMessage = Result.Message;
    if (!Result.IsSaved()) { UE_LOG(LogTemp, Warning, TEXT("%s"), *Result.Message); }
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
    CancelDisplayTrial(DisplayTrial.Id());
    FlushLocalSettings();
    if (bDisplaySettingsDirty && !bDisplayRestorePending) { RetryDisplaySave(); }
}

UGameUserSettings* ULDGameInstance::DisplaySettingsOwner() const
{
    if (!GEngine || !FApp::CanEverRender() || FApp::IsUnattended() || IsRunningCommandlet()
        || !GEngine->GameViewport || !GEngine->GameViewport->Viewport
        || GEngine->GameViewport->GetGameInstance() != this || bUseIsolatedSettings) { return nullptr; }
    return GEngine->GetGameUserSettings();
}

bool ULDGameInstance::InstallDisplayInputGuard()
{
    if (DisplayInputGuard) { return true; }
    if (!FSlateApplication::IsInitialized()) { return false; }
    auto Guard = MakeShared<FLDDisplayTrialInputGuard>(this);
    if (!FSlateApplication::Get().RegisterInputPreProcessor(Guard, 0)) { return false; }
    DisplayInputGuard = MoveTemp(Guard);
    return true;
}

bool ULDGameInstance::ReleaseDisplayConfigGuard()
{
    if (!DisplayConfigGuard.IsActive()) { return true; }
    FString Error;
    if (!GConfig || !DisplayConfigGuard.Release(*GConfig, Error))
    {
        SetDisplayResult(ELDDisplaySettingsStatus::RestoreFailed,
            Error.IsEmpty() ? TEXT("配置所有者不可用，暂时不能保存显示设置。") : Error);
        return false;
    }
    return true;
}

FLDDisplaySettingsResult ULDGameInstance::ReadDisplaySettings(FLDDisplaySettings& Out) const
{
    const auto* Settings = DisplaySettingsOwner();
    if (!Settings)
    { return { ELDDisplaySettingsStatus::Unavailable, 0, TEXT("显示设置需要可交互的游戏窗口；没有应用或保存任何显示设置。") }; }
    Out = FLDDisplaySettings::Read(*Settings);
    if (!Out.IsValid())
    { return { ELDDisplaySettingsStatus::Invalid, 0, TEXT("当前显示配置无效，已禁用显示修改。") }; }
    return { ELDDisplaySettingsStatus::Loaded, 0, TEXT("已读取显示设置。修改在应用前只是草稿。") };
}

bool ULDGameInstance::GetDisplayResolutions(TArray<FIntPoint>& Out, FString& Error) const
{
    Out.Reset();
    const auto* Settings = DisplaySettingsOwner();
    if (!Settings || !UKismetSystemLibrary::GetSupportedFullscreenResolutions(Out) || Out.IsEmpty())
    { Error = TEXT("显卡驱动没有提供可用分辨率，无法修改分辨率。"); return false; }
    Out.RemoveAll([](FIntPoint Value) { return Value.X <= 0 || Value.Y <= 0; });
    Out.AddUnique(Settings->GetScreenResolution());
    Out.AddUnique(Settings->GetDesktopResolution());
    Out.Sort([](FIntPoint A, FIntPoint B) { return A.X == B.X ? A.Y < B.Y : A.X < B.X; });
    Error.Empty();
    return !Out.IsEmpty();
}

bool ULDGameInstance::DisplayModeApplied(const FLDDisplaySettings& Expected) const
{
    const auto* Settings = DisplaySettingsOwner();
    return Settings && Expected.SameMode(FLDDisplaySettings::Read(*Settings))
        && !Settings->IsScreenResolutionDirty() && !Settings->IsFullscreenModeDirty();
}

FLDDisplaySettingsResult ULDGameInstance::SetDisplayResult(ELDDisplaySettingsStatus Status, const FString& Message, uint64 Id)
{
    DisplayResult = { Status, Id, Message };
    if (Status == ELDDisplaySettingsStatus::ApplyFailed || Status == ELDDisplaySettingsStatus::PersistenceFailed
        || Status == ELDDisplaySettingsStatus::RestoreFailed)
    { UE_LOG(LogTemp, Warning, TEXT("%s"), *Message); }
    return DisplayResult;
}

FLDDisplaySettingsResult ULDGameInstance::ApplyDisplaySettings(const FLDDisplaySettings& Draft, UObject* TrialOwner)
{
    auto* Settings = DisplaySettingsOwner();
    if (!Settings) { return SetDisplayResult(ELDDisplaySettingsStatus::Unavailable, TEXT("显示设置需要可交互的游戏窗口。")); }
    if (DisplayTrial.Id() || bDisplayRestorePending || DisplayConfigGuard.IsActive())
    { return SetDisplayResult(ELDDisplaySettingsStatus::Busy, TEXT("请先确认当前的显示试用，或等待恢复完成。"), DisplayTrial.Id()); }
    if (!Draft.IsValid()) { return SetDisplayResult(ELDDisplaySettingsStatus::Invalid, TEXT("所选的显示参数无效。")); }
    if (!Draft.SameMode(FLDDisplaySettings::Read(*Settings))) { return BeginDisplayTrial(Draft, TrialOwner); }
    if (!DisplayModeApplied(Draft))
    { return SetDisplayResult(ELDDisplaySettingsStatus::ApplyPending, TEXT("窗口仍在变化，请稍后再应用显示设置。")); }
    Settings->ConfirmVideoMode();
    return ApplyNonResolutionAndSave(Draft);
}

FLDDisplaySettingsResult ULDGameInstance::BeginDisplayTrial(const FLDDisplaySettings& Draft, UObject* TrialOwner)
{
    auto* Settings = DisplaySettingsOwner();
    if (!Settings || !IsValid(TrialOwner))
    { return SetDisplayResult(ELDDisplaySettingsStatus::Unavailable, TEXT("显示试用需要打开设置面板和可交互的游戏窗口。")); }
    if (DisplayTrial.Id() || bDisplayRestorePending || DisplayConfigGuard.IsActive())
    { return SetDisplayResult(ELDDisplaySettingsStatus::Busy, TEXT("另一个显示试用或恢复正在进行。"), DisplayTrial.Id()); }
    const auto Before = FLDDisplaySettings::Read(*Settings);
    if (!Draft.IsValid() || !Before.IsValid())
    { return SetDisplayResult(ELDDisplaySettingsStatus::Invalid, TEXT("所选的显示参数无效。")); }
    TArray<FIntPoint> Supported;
    FString Error;
    if (!GetDisplayResolutions(Supported, Error)) { return SetDisplayResult(ELDDisplaySettingsStatus::Unavailable, Error); }
    if (!Supported.Contains(Draft.Resolution)
        || (Draft.WindowMode == EWindowMode::WindowedFullscreen && Draft.Resolution != Settings->GetDesktopResolution()))
    { return SetDisplayResult(ELDDisplaySettingsStatus::Invalid, TEXT("请选择驱动支持的分辨率；无边框窗口使用桌面分辨率。")); }
    if (!DisplayModeApplied(Before))
    { return SetDisplayResult(ELDDisplaySettingsStatus::ApplyPending, TEXT("当前窗口模式尚未稳定，请稍后再开始试用。")); }
    if (!InstallDisplayInputGuard() || !GConfig || !DisplayConfigGuard.Begin(*GConfig, GGameUserSettingsIni,
        GIsEditor ? GEditorSettingsIni : GGameUserSettingsIni, Settings->GetClass()->GetPathName(), Error))
    { return SetDisplayResult(ELDDisplaySettingsStatus::Unavailable, Error.IsEmpty() ? TEXT("无法保护引擎的显示保存路径，没有做任何显示修改。") : Error); }
    constexpr double DisplayTrialSeconds = 15.;
    const uint64 Id = DisplayTrial.Begin(Before, Draft, FPlatformTime::Seconds(), DisplayTrialSeconds);
    if (!Id) { ReleaseDisplayConfigGuard(); return SetDisplayResult(ELDDisplaySettingsStatus::Invalid, TEXT("无法开始显示试用。")); }
    DisplayTrialOwner = TrialOwner;
    DisplaySettingsDuringTrial = Settings;
    Settings->ConfirmVideoMode();
    Settings->SetScreenResolution(Draft.Resolution);
    Settings->SetFullscreenMode(Draft.WindowMode);
    // Both setters can adjust resolution quality in UE. A mode trial must not apply that change.
    Settings->ScalabilityQuality = Before.Quality;
    Settings->ApplyResolutionSettings(false);
    Settings->ScalabilityQuality = Before.Quality;
    return SetDisplayResult(ELDDisplaySettingsStatus::TrialStarted,
        TEXT("保留这个显示模式吗？15 秒后自动还原。垂直同步和画质尚未应用；试用结束前，全屏快捷键和控制台已锁定。"), Id);
}

FLDDisplaySettingsResult ULDGameInstance::ConfirmDisplayTrial(uint64 TrialId)
{
    if (!DisplayTrial.IsCurrent(TrialId))
    { return { ELDDisplaySettingsStatus::StaleTrial, TrialId, TEXT("这次显示确认已过期，当前试用没有改变。") }; }
    if (DisplayTrial.IsExpired(FPlatformTime::Seconds()) || !DisplayTrialOwner.IsValid())
    { return CancelDisplayTrial(TrialId); }
    const auto Draft = DisplayTrial.Candidate();
    if (DisplaySettingsOwner() != DisplaySettingsDuringTrial) { return CancelDisplayTrial(TrialId); }
    if (!DisplayModeApplied(Draft))
    { return SetDisplayResult(ELDDisplaySettingsStatus::ApplyPending, TEXT("请求的分辨率尚未生效。请稍候，或者还原。"), TrialId); }
    if (!ReleaseDisplayConfigGuard()) { return DisplayResult; }
    auto* Settings = DisplaySettingsOwner();
    DisplayTrial.Invalidate(TrialId);
    DisplayTrialOwner.Reset();
    DisplaySettingsDuringTrial = nullptr;
    Settings->ConfirmVideoMode();
    return ApplyNonResolutionAndSave(Draft);
}

FLDDisplaySettingsResult ULDGameInstance::ApplyNonResolutionAndSave(const FLDDisplaySettings& Draft)
{
    auto* Settings = DisplaySettingsOwner();
    if (!Settings) { return SetDisplayResult(ELDDisplaySettingsStatus::Unavailable, TEXT("显示设置的所有者已不可用。")); }
    Settings->SetVSyncEnabled(Draft.bVSync);
    Settings->ScalabilityQuality = Draft.Quality;
    Settings->ApplyNonResolutionSettings();
    bDisplaySettingsDirty = true;
    if (Settings->IsVSyncDirty() || Scalability::GetQualityLevels() != Draft.Quality)
    { return SetDisplayResult(ELDDisplaySettingsStatus::ApplyFailed, TEXT("显示模式已确认，但垂直同步或画质没有完全应用。请检查引擎覆盖设置后重试；尚未确认能够保存。")); }
    // The default render scale (0) is a no-op in the engine; without this, restoring defaults kept the old scale until restart.
    auto* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
    if (!ScreenPercentage || !LDLocalSettings::ApplyRenderScale(*ScreenPercentage, Draft.Quality.ResolutionQuality))
    { return SetDisplayResult(ELDDisplaySettingsStatus::ApplyFailed, TEXT("显示模式已确认，但渲染比例没有回到项目默认值。请重试；尚未确认能够保存。")); }
    return RetryDisplaySave();
}

FLDDisplaySettingsResult ULDGameInstance::CancelDisplayTrial(uint64 TrialId)
{
    if (!TrialId && !DisplayTrial.Id())
    {
        return { bDisplayRestorePending ? ELDDisplaySettingsStatus::RestorePending : ELDDisplaySettingsStatus::NoTrial,
            0, bDisplayRestorePending ? TEXT("已确认的显示模式仍在恢复中。") : TEXT("当前没有显示试用。") };
    }
    if (!DisplayTrial.IsCurrent(TrialId))
    { return { ELDDisplaySettingsStatus::StaleTrial, TrialId, TEXT("这次显示试用已过期，没有改变更新的试用。") }; }
    RestoreDisplay = DisplayTrial.Baseline();
    DisplayTrial.Invalidate(TrialId);
    DisplayTrialOwner.Reset();
    bDisplayRestorePending = true;
    return RetryDisplayRestore();
}

FLDDisplaySettingsResult ULDGameInstance::RetryDisplayRestore()
{
    if (!bDisplayRestorePending)
    { return { ELDDisplaySettingsStatus::NoTrial, 0, TEXT("没有待完成的显示恢复。") }; }
    auto* Settings = DisplaySettingsDuringTrial.Get();
    DisplayRestoreStartedAt = FPlatformTime::Seconds();
    if (!Settings)
    { return SetDisplayResult(ELDDisplaySettingsStatus::RestoreFailed, TEXT("原显示设置的所有者不可用；未确认的值没有保存。")); }
    const bool CanApply = DisplaySettingsOwner() == Settings;
    Settings->RevertVideoMode();
    Settings->SetScreenResolution(RestoreDisplay.Resolution);
    Settings->SetFullscreenMode(RestoreDisplay.WindowMode);
    Settings->ConfirmVideoMode();
    Settings->SetVSyncEnabled(RestoreDisplay.bVSync);
    Settings->ScalabilityQuality = RestoreDisplay.Quality;
    if (CanApply)
    {
        Settings->ApplyResolutionSettings(false);
        Settings->ScalabilityQuality = RestoreDisplay.Quality;
    }
    // ApplyResolutionSettings also changes the preference used by native fullscreen shortcuts.
    // Use the same property-scoped config reload as the engine's ReloadWindowPositions.
    auto* PreferredProperty = FindFProperty<FIntProperty>(Settings->GetClass(), TEXT("PreferredFullscreenMode"));
    auto* FullscreenMode = IConsoleManager::Get().FindConsoleVariable(TEXT("r.FullScreenMode"));
    if (!GConfig || !PreferredProperty || !FullscreenMode)
    { return SetDisplayResult(ELDDisplaySettingsStatus::RestoreFailed, TEXT("无法恢复引擎的全屏偏好，暂时不能保存显示设置。")); }
    GConfig->SetInt(*Settings->GetClass()->GetPathName(), TEXT("PreferredFullscreenMode"), int32(RestoreDisplay.PreferredFullscreenMode), GGameUserSettingsIni);
    Settings->ReloadConfig(Settings->GetClass(), *GGameUserSettingsIni, UE::LCPF_None, PreferredProperty);
    FullscreenMode->Set(int32(RestoreDisplay.PreferredFullscreenMode), ECVF_SetByGameSetting);
    if (Settings->GetPreferredFullscreenMode() != RestoreDisplay.PreferredFullscreenMode)
    { return SetDisplayResult(ELDDisplaySettingsStatus::RestoreFailed, TEXT("引擎的全屏偏好没有恢复，暂时不能保存显示设置。")); }
    // Restore the engine object's fields even if travel/exit already removed its viewport.
    if (!CanApply)
    { return SetDisplayResult(ELDDisplaySettingsStatus::RestoreFailed, TEXT("已确认的显示值已在内存中恢复，但游戏窗口不可用，无法核实模式；没有保存显示文件。")); }
    if (Settings && Settings == DisplaySettingsOwner() && DisplayModeApplied(RestoreDisplay)
        && FLDDisplaySettings::Read(*Settings) == RestoreDisplay)
    {
        if (!ReleaseDisplayConfigGuard()) { return DisplayResult; }
        bDisplayRestorePending = false;
        DisplaySettingsDuringTrial = nullptr;
        return SetDisplayResult(ELDDisplaySettingsStatus::Cancelled, TEXT("已恢复之前的显示模式，已应用的按键和镜头偏好保持不变。"));
    }
    return SetDisplayResult(ELDDisplaySettingsStatus::RestorePending, TEXT("正在恢复已确认的显示模式，已应用的按键和镜头偏好不变。"));
}

FLDDisplaySettingsResult ULDGameInstance::RetryDisplaySave()
{
    if (DisplayTrial.Id() || bDisplayRestorePending || DisplayConfigGuard.IsActive())
    { return SetDisplayResult(ELDDisplaySettingsStatus::Busy, TEXT("未确认或未恢复的显示值不能保存。"), DisplayTrial.Id()); }
    auto* Settings = DisplaySettingsOwner();
    if (!Settings) { return SetDisplayResult(ELDDisplaySettingsStatus::Unavailable, TEXT("保存显示设置需要可交互的游戏窗口。")); }
    const auto Expected = FLDDisplaySettings::Read(*Settings);
    if (!Expected.IsValid() || !DisplayModeApplied(Expected) || Settings->IsVSyncDirty() || Scalability::GetQualityLevels() != Expected.Quality)
    { return SetDisplayResult(ELDDisplaySettingsStatus::ApplyFailed, TEXT("请求的显示设置没有完全应用，尚未保存。")); }
    if (Settings->GetLastConfirmedScreenResolution() != Expected.Resolution
        || Settings->GetLastConfirmedFullscreenMode() != Expected.WindowMode)
    { return SetDisplayResult(ELDDisplaySettingsStatus::Invalid, TEXT("只能保存已确认的显示模式。")); }
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

double ULDGameInstance::GetDisplayTrialRemaining() const
{ return DisplayTrial.Remaining(FPlatformTime::Seconds()); }

bool ULDGameInstance::TickLocalSettings(float Delta)
{
    const double Now = FPlatformTime::Seconds();
    if (DisplayTrial.Id() && (DisplayTrial.IsExpired(Now) || !DisplayTrialOwner.IsValid()))
    { CancelDisplayTrial(DisplayTrial.Id()); }
    if (bDisplayRestorePending)
    {
        if (DisplayModeApplied(RestoreDisplay))
        {
            if (!ReleaseDisplayConfigGuard()) { return true; }
            bDisplayRestorePending = false;
            DisplaySettingsDuringTrial = nullptr;
            SetDisplayResult(ELDDisplaySettingsStatus::Cancelled, TEXT("已恢复之前的显示模式，已应用的按键和镜头偏好保持不变。"));
        }
        else if (Now - DisplayRestoreStartedAt >= 5. && DisplayResult.Status != ELDDisplaySettingsStatus::RestoreFailed)
        { SetDisplayResult(ELDDisplaySettingsStatus::RestoreFailed, TEXT("无法核实之前的显示模式。请重试恢复；未确认的值不会被保存。")); }
    }
    if (LocalSettingsFlushAt > 0 && Now >= LocalSettingsFlushAt) { FlushLocalSettings(); }
    return true;
}
