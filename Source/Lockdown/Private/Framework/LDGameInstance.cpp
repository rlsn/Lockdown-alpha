#include "Framework/LDGameInstance.h"
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
        ConnectionMessage = TEXT("Room entry sequence exhausted. Restart the application.");
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
        Error = TEXT("The Demo world has no valid WorldSettings.");
    }
    else if (LDDemoEntry::ValidateSelection(World->GetOutermost()->GetName(), Options, Error)
        && Settings->DefaultGameMode && Settings->DefaultGameMode != ALDGameMode::StaticClass())
    {
        Error = TEXT("Retired saved GameMode. The Demo world must use LDGameMode.");
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
        Error = TEXT("Match generation exhausted. Restart the application before creating another room.");
        UE_LOG(LogTemp, Error, TEXT("LD_MATCH_GENERATION_EXHAUSTED"));
        return false;
    }
    return true;
}

void ULDGameInstance::Init()
{
    Super::Init();
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
void ULDGameInstance::NetworkFailed(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error)
{
    // Pending connections have no world on the driver yet. Resolve their actual
    // owning context rather than losing the message or leaking it to another PIE instance.
    if (!World && Driver)
    {
        if (const FWorldContext* Context = GEngine->GetWorldContextFromPendingNetGameNetDriver(Driver))
        {
            if (Context->OwningGameInstance != this) { return; }
            World = Context->World();
        }
    }
    ReturnAfterFailure(World, FString::Printf(TEXT("%s. Check the address and that the host is running."), ENetworkFailure::ToString(Type)));
}
void ULDGameInstance::TravelFailed(UWorld* World, ETravelFailure::Type Type, const FString& Error) { ReturnAfterFailure(World, Error); }
void ULDGameInstance::ReturnAfterFailure(UWorld* World, const FString& Error)
{
    if (!World || World->GetGameInstance() != this) { return; }
    PrepareLocalSettingsForTravel();
    ConnectionMessage = TEXT("Connection failed: ") + Error;
    if (!World->GetMapName().Contains(TEXT("L_Frontend")))
    {
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
        }
        LocalControlSettings->RequireRecovery();
        LocalSettingsMessage += TEXT(" Current bindings are session-only until you confirm recovery in Settings.");
        UE_LOG(LogTemp, Warning, TEXT("%s"), *LocalSettingsMessage);
    }
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

FLDLocalSettingsSaveResult ULDGameInstance::ApplyLocalSettings(const ULDControlSettings& Draft)
{
    FString Error;
    if (!LocalControlSettings || !LocalControlSettings->ApplyLocalSettings(Draft, Error))
    {
        LocalSettingsMessage = Error.IsEmpty() ? TEXT("Local settings are not available.") : Error;
        return { ELDLocalSettingsSaveStatus::Invalid, LocalSettingsMessage };
    }
    ApplyControlPreferences();
    return FlushLocalSettings();
}

FLDLocalSettingsSaveResult ULDGameInstance::FlushLocalSettings(bool VerifyCurrent)
{
    LocalSettingsFlushAt = 0;
    if (!LocalControlSettings || (!LocalControlSettings->IsDirty() && !VerifyCurrent))
    { return { ELDLocalSettingsSaveStatus::NoChanges, TEXT("No pending local preference changes.") }; }
    const auto Result = LocalControlSettings->SaveLocalSettings();
    LocalSettingsMessage = Result.Message;
    if (!Result.IsSaved()) { UE_LOG(LogTemp, Warning, TEXT("%s"), *Result.Message); }
    return Result;
}

FLDLocalSettingsSaveResult ULDGameInstance::ConfirmLocalSettingsRecovery()
{
    if (!LocalControlSettings)
    { return { ELDLocalSettingsSaveStatus::Invalid, TEXT("No local settings are available to recover.") }; }
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
            Error.IsEmpty() ? TEXT("Display saving remains blocked because the configuration owner is unavailable.") : Error);
        return false;
    }
    return true;
}

FLDDisplaySettingsResult ULDGameInstance::ReadDisplaySettings(FLDDisplaySettings& Out) const
{
    const auto* Settings = DisplaySettingsOwner();
    if (!Settings)
    { return { ELDDisplaySettingsStatus::Unavailable, 0, TEXT("Display controls need an interactive game viewport. No display setting was applied or saved.") }; }
    Out = FLDDisplaySettings::Read(*Settings);
    if (!Out.IsValid())
    { return { ELDDisplaySettingsStatus::Invalid, 0, TEXT("The current display configuration is invalid. Display changes are disabled.") }; }
    return { ELDDisplaySettingsStatus::Loaded, 0, TEXT("Display settings loaded. Edits remain a draft until applied.") };
}

bool ULDGameInstance::GetDisplayResolutions(TArray<FIntPoint>& Out, FString& Error) const
{
    Out.Reset();
    const auto* Settings = DisplaySettingsOwner();
    if (!Settings || !UKismetSystemLibrary::GetSupportedFullscreenResolutions(Out) || Out.IsEmpty())
    { Error = TEXT("The display driver did not provide supported resolutions. Resolution changes are unavailable."); return false; }
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
    if (!Settings) { return SetDisplayResult(ELDDisplaySettingsStatus::Unavailable, TEXT("Display settings need an interactive game viewport.")); }
    if (DisplayTrial.Id() || bDisplayRestorePending || DisplayConfigGuard.IsActive())
    { return SetDisplayResult(ELDDisplaySettingsStatus::Busy, TEXT("Confirm the current display trial or finish restoring it first."), DisplayTrial.Id()); }
    if (!Draft.IsValid()) { return SetDisplayResult(ELDDisplaySettingsStatus::Invalid, TEXT("The selected display values are invalid.")); }
    if (!Draft.SameMode(FLDDisplaySettings::Read(*Settings))) { return BeginDisplayTrial(Draft, TrialOwner); }
    if (!DisplayModeApplied(Draft))
    { return SetDisplayResult(ELDDisplaySettingsStatus::ApplyPending, TEXT("The window is still changing. Wait before applying display settings.")); }
    Settings->ConfirmVideoMode();
    return ApplyNonResolutionAndSave(Draft);
}

FLDDisplaySettingsResult ULDGameInstance::BeginDisplayTrial(const FLDDisplaySettings& Draft, UObject* TrialOwner)
{
    auto* Settings = DisplaySettingsOwner();
    if (!Settings || !IsValid(TrialOwner))
    { return SetDisplayResult(ELDDisplaySettingsStatus::Unavailable, TEXT("A live settings panel and interactive viewport are required for a display trial.")); }
    if (DisplayTrial.Id() || bDisplayRestorePending || DisplayConfigGuard.IsActive())
    { return SetDisplayResult(ELDDisplaySettingsStatus::Busy, TEXT("Another display trial or restore is in progress."), DisplayTrial.Id()); }
    const auto Before = FLDDisplaySettings::Read(*Settings);
    if (!Draft.IsValid() || !Before.IsValid())
    { return SetDisplayResult(ELDDisplaySettingsStatus::Invalid, TEXT("The selected display values are invalid.")); }
    TArray<FIntPoint> Supported;
    FString Error;
    if (!GetDisplayResolutions(Supported, Error)) { return SetDisplayResult(ELDDisplaySettingsStatus::Unavailable, Error); }
    if (!Supported.Contains(Draft.Resolution)
        || (Draft.WindowMode == EWindowMode::WindowedFullscreen && Draft.Resolution != Settings->GetDesktopResolution()))
    { return SetDisplayResult(ELDDisplaySettingsStatus::Invalid, TEXT("Choose a driver-supported resolution; borderless uses the desktop resolution.")); }
    if (!DisplayModeApplied(Before))
    { return SetDisplayResult(ELDDisplaySettingsStatus::ApplyPending, TEXT("The current window mode has not settled. Wait before starting a trial.")); }
    if (!InstallDisplayInputGuard() || !GConfig || !DisplayConfigGuard.Begin(*GConfig, GGameUserSettingsIni,
        GIsEditor ? GEditorSettingsIni : GGameUserSettingsIni, Settings->GetClass()->GetPathName(), Error))
    { return SetDisplayResult(ELDDisplaySettingsStatus::Unavailable, Error.IsEmpty() ? TEXT("The engine display-save paths could not be guarded. No display change was made.") : Error); }
    constexpr double DisplayTrialSeconds = 15.;
    const uint64 Id = DisplayTrial.Begin(Before, Draft, FPlatformTime::Seconds(), DisplayTrialSeconds);
    if (!Id) { ReleaseDisplayConfigGuard(); return SetDisplayResult(ELDDisplaySettingsStatus::Invalid, TEXT("The display trial could not be started.")); }
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
        TEXT("Keep this display mode? It reverts after 15 seconds. VSync and quality are still unapplied; fullscreen shortcuts and the console are locked until this trial ends."), Id);
}

FLDDisplaySettingsResult ULDGameInstance::ConfirmDisplayTrial(uint64 TrialId)
{
    if (!DisplayTrial.IsCurrent(TrialId))
    { return { ELDDisplaySettingsStatus::StaleTrial, TrialId, TEXT("This display confirmation has expired. The current trial was not changed.") }; }
    if (DisplayTrial.IsExpired(FPlatformTime::Seconds()) || !DisplayTrialOwner.IsValid())
    { return CancelDisplayTrial(TrialId); }
    const auto Draft = DisplayTrial.Candidate();
    if (DisplaySettingsOwner() != DisplaySettingsDuringTrial) { return CancelDisplayTrial(TrialId); }
    if (!DisplayModeApplied(Draft))
    { return SetDisplayResult(ELDDisplaySettingsStatus::ApplyPending, TEXT("The requested resolution has not taken effect. Wait, or revert it."), TrialId); }
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
    if (!Settings) { return SetDisplayResult(ELDDisplaySettingsStatus::Unavailable, TEXT("The display settings owner is no longer available.")); }
    Settings->SetVSyncEnabled(Draft.bVSync);
    Settings->ScalabilityQuality = Draft.Quality;
    Settings->ApplyNonResolutionSettings();
    bDisplaySettingsDirty = true;
    if (Settings->IsVSyncDirty() || Scalability::GetQualityLevels() != Draft.Quality)
    { return SetDisplayResult(ELDDisplaySettingsStatus::ApplyFailed, TEXT("The display mode is confirmed, but VSync or quality was not fully applied. Check engine overrides and retry; persistence is not verified.")); }
    return RetryDisplaySave();
}

FLDDisplaySettingsResult ULDGameInstance::CancelDisplayTrial(uint64 TrialId)
{
    if (!TrialId && !DisplayTrial.Id())
    {
        return { bDisplayRestorePending ? ELDDisplaySettingsStatus::RestorePending : ELDDisplaySettingsStatus::NoTrial,
            0, bDisplayRestorePending ? TEXT("The confirmed display mode is still being restored.") : TEXT("No display trial is active.") };
    }
    if (!DisplayTrial.IsCurrent(TrialId))
    { return { ELDDisplaySettingsStatus::StaleTrial, TrialId, TEXT("This display trial has expired. No newer trial was changed.") }; }
    RestoreDisplay = DisplayTrial.Baseline();
    DisplayTrial.Invalidate(TrialId);
    DisplayTrialOwner.Reset();
    bDisplayRestorePending = true;
    return RetryDisplayRestore();
}

FLDDisplaySettingsResult ULDGameInstance::RetryDisplayRestore()
{
    if (!bDisplayRestorePending)
    { return { ELDDisplaySettingsStatus::NoTrial, 0, TEXT("No display restoration is pending.") }; }
    auto* Settings = DisplaySettingsDuringTrial.Get();
    DisplayRestoreStartedAt = FPlatformTime::Seconds();
    if (!Settings)
    { return SetDisplayResult(ELDDisplaySettingsStatus::RestoreFailed, TEXT("The original display settings owner is unavailable. Unconfirmed values were not saved.")); }
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
    { return SetDisplayResult(ELDDisplaySettingsStatus::RestoreFailed, TEXT("The engine fullscreen preference cannot be restored. Display saving remains blocked.")); }
    GConfig->SetInt(*Settings->GetClass()->GetPathName(), TEXT("PreferredFullscreenMode"), int32(RestoreDisplay.PreferredFullscreenMode), GGameUserSettingsIni);
    Settings->ReloadConfig(Settings->GetClass(), *GGameUserSettingsIni, UE::LCPF_None, PreferredProperty);
    FullscreenMode->Set(int32(RestoreDisplay.PreferredFullscreenMode), ECVF_SetByGameSetting);
    if (Settings->GetPreferredFullscreenMode() != RestoreDisplay.PreferredFullscreenMode)
    { return SetDisplayResult(ELDDisplaySettingsStatus::RestoreFailed, TEXT("The engine fullscreen preference did not restore. Display saving remains blocked.")); }
    // Restore the engine object's fields even if travel/exit already removed its viewport.
    if (!CanApply)
    { return SetDisplayResult(ELDDisplaySettingsStatus::RestoreFailed, TEXT("Confirmed display values were restored in memory, but the viewport is unavailable to verify the mode. No display file was saved.")); }
    if (Settings && Settings == DisplaySettingsOwner() && DisplayModeApplied(RestoreDisplay)
        && FLDDisplaySettings::Read(*Settings) == RestoreDisplay)
    {
        if (!ReleaseDisplayConfigGuard()) { return DisplayResult; }
        bDisplayRestorePending = false;
        DisplaySettingsDuringTrial = nullptr;
        return SetDisplayResult(ELDDisplaySettingsStatus::Cancelled, TEXT("The previous display mode is restored. Applied keys and camera preferences were kept."));
    }
    return SetDisplayResult(ELDDisplaySettingsStatus::RestorePending, TEXT("Restoring the confirmed display mode. Applied keys and camera preferences are unchanged."));
}

FLDDisplaySettingsResult ULDGameInstance::RetryDisplaySave()
{
    if (DisplayTrial.Id() || bDisplayRestorePending || DisplayConfigGuard.IsActive())
    { return SetDisplayResult(ELDDisplaySettingsStatus::Busy, TEXT("Unconfirmed or unrestored display values cannot be saved."), DisplayTrial.Id()); }
    auto* Settings = DisplaySettingsOwner();
    if (!Settings) { return SetDisplayResult(ELDDisplaySettingsStatus::Unavailable, TEXT("Display persistence needs an interactive game viewport.")); }
    const auto Expected = FLDDisplaySettings::Read(*Settings);
    if (!Expected.IsValid() || !DisplayModeApplied(Expected) || Settings->IsVSyncDirty() || Scalability::GetQualityLevels() != Expected.Quality)
    { return SetDisplayResult(ELDDisplaySettingsStatus::ApplyFailed, TEXT("The requested display settings are not fully applied. They have not been saved.")); }
    if (Settings->GetLastConfirmedScreenResolution() != Expected.Resolution
        || Settings->GetLastConfirmedFullscreenMode() != Expected.WindowMode)
    { return SetDisplayResult(ELDDisplaySettingsStatus::Invalid, TEXT("Only a confirmed display mode may be saved.")); }
    bDisplaySettingsDirty = true;
    if (!GConfig)
    { return SetDisplayResult(ELDDisplaySettingsStatus::PersistenceFailed, TEXT("Display changes are applied, but the configuration owner is unavailable. Persistence is not verified.")); }
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
            SetDisplayResult(ELDDisplaySettingsStatus::Cancelled, TEXT("The previous display mode is restored. Applied keys and camera preferences were kept."));
        }
        else if (Now - DisplayRestoreStartedAt >= 5. && DisplayResult.Status != ELDDisplaySettingsStatus::RestoreFailed)
        { SetDisplayResult(ELDDisplaySettingsStatus::RestoreFailed, TEXT("The previous display mode could not be verified. Retry restore; unconfirmed values will not be saved.")); }
    }
    if (LocalSettingsFlushAt > 0 && Now >= LocalSettingsFlushAt) { FlushLocalSettings(); }
    return true;
}
