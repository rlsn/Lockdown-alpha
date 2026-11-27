#pragma once
#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/EngineBaseTypes.h"
#include "Containers/Ticker.h"
#include "UI/LDControlSettings.h"
#include "Audio/LDAudioTypes.h"
#include "LDGameInstance.generated.h"

UCLASS()
class LOCKDOWN_API ULDGameInstance : public UGameInstance
{
    GENERATED_BODY()
public:
    FString ConnectionMessage;
    void RememberRoomURL(const FString& URL);
    void SetReconnectTicket(const FGuid& Ticket) { ReconnectTicket=Ticket; }
    void ClearReconnect();
    bool CanReconnect() const { return ReconnectTicket.IsValid()&&!ReconnectURL.IsEmpty(); }
    FGuid GetReconnectTicket() const { return ReconnectTicket; }
    bool ReconnectRoom(class ALDPlayerController& Controller);
    uint32 AuthorityGeneration = 0;
    bool AllocateMatchGeneration(uint32& Generation, FString& Error);
    virtual AGameModeBase* CreateGameModeForURL(FURL URL, UWorld* World) override;
    virtual void StartGameInstance() override;
    // Installs the game's own pointer (LDCursor) on this instance's viewport, in a packaged game and in PIE alike.
    virtual void OnStart() override;
    void RecordEntryRejectionBeforeWorld(const FString& URL, const FString& Reason);
    const FString& RejectedEntryURL() const { return LastRejectedEntryURL; }
    // A refusal the Host sends after this client loaded the room (its final admission check): shows the coded reason
    // and returns to the frontend.
    void ReturnAfterRefusal(UWorld* World, const FString& ServerMessage);
    // The Host closed the room: its reason is shown on the frontend, not the connection loss that follows it.
    void ReturnAfterRoomClosed(UWorld* World, const FString& Reason);
    // A room world this client already leaves for a refusal or a closed room.
    bool IsLeavingWorld(const UWorld* World) const { return World && World == LeavingWorld.Get(); }
    // Lightweight crash breadcrumbs (#337 §2.3), dumped to Saved/Logs/LastCrashContext.json on system error.
    void NoteCrashMatchState(uint32 Generation, uint8 Phase, uint8 TimelineStep);
    void NoteCrashCommand(const TCHAR* CommandKind);
    virtual void Init() override;
    virtual void Shutdown() override;
    ULDControlSettings* GetLocalControlSettings();
    ULDControlSettings* UseIsolatedLocalSettings(const FGuid& Fixture);
    FLDLocalSettingsSaveResult ApplyLocalSettings(const ULDControlSettings& Draft);
    // Applies a linear 0..1 master volume to the live preferences and the audio output now; only the debounced flush
    // writes it, so a slider drag never saves every intermediate value.
    // The applied volume of one bus (AUDIO_AND_FEEL buses), linear 0..1: heard at once, saved by the coming flush.
    FLDLocalSettingsSaveResult SetAppliedBusVolume(ELDAudioBus Bus, float Linear);
    float GetAppliedBusVolume(ELDAudioBus Bus) const;
    FLDLocalSettingsSaveResult FlushLocalSettings(bool VerifyCurrent=false);
    FLDLocalSettingsSaveResult ConfirmLocalSettingsRecovery();
    void RequestLocalSettingsFlush();
    bool IsLocalSettingsFlushPending() const { return LocalSettingsFlushAt > 0; }
    void PrepareLocalSettingsForTravel();
    const FString& GetLocalSettingsMessage() const { return LocalSettingsMessage; }

    FLDDisplaySettingsResult ReadDisplaySettings(FLDDisplaySettings& Out) const;
    // Applies VSync and overall quality now, then saves and reads the engine settings files back.
    FLDDisplaySettingsResult ApplyDisplaySettings(const FLDDisplaySettings& Draft);
    FLDDisplaySettingsResult RetryDisplaySave();
    bool AreDisplaySettingsDirty() const { return bDisplaySettingsDirty; }
    const FLDDisplaySettingsResult& GetDisplaySettingsResult() const { return DisplayResult; }
private:
    FString ReconnectURL;
    FGuid ReconnectTicket;
    uint64 EntryAttemptSerial = 0;
    FString LastRejectedEntryURL;
    FTSTicker::FDelegateHandle EntryReturnTicker;
    FTSTicker::FDelegateHandle FailureReturnTicker;
    FDelegateHandle NetworkHandle, TravelHandle, SystemErrorHandle;
    uint32 CrashMatchGeneration = 0;
    uint8 CrashMatchPhase = 0;
    uint8 CrashTimelineStep = 0;
    TCHAR CrashLastCommand[64] = {};
    void HandleSystemError();
    void NetworkFailed(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error);
    void TravelFailed(UWorld* World, ETravelFailure::Type Type, const FString& Error);
    void ReturnAfterFailure(UWorld* World, const FString& Error);
    // The room world being left for its first reason; later failures from it are consequences, not new reasons.
    TWeakObjectPtr<UWorld> LeavingWorld;
    UPROPERTY(Transient) TObjectPtr<ULDControlSettings> LocalControlSettings;
    FLDLocalSettingsStorage IsolatedSettingsStorage;
    bool bUseIsolatedSettings = false;
    FString LocalSettingsMessage;
    double LocalSettingsFlushAt = 0;
    FTSTicker::FDelegateHandle LocalSettingsTickHandle;
    FDelegateHandle PreLoadMapHandle, PreClientTravelHandle, EngineExitHandle;
    bool TickLocalSettings(float Delta);
    void LocalSettingsPreLoadMap(const FWorldContext& Context, const FString& Map);
    void LocalSettingsPreClientTravel(const FString& URL, ETravelType Type, bool Seamless);
    void ApplyControlPreferences();
    // Pushes the applied master volume to the audio subsystem, which is absent on a game instance that never ran Init.
    void ApplyAudioPreferences();

    FLDDisplaySettingsResult DisplayResult;
    bool bDisplaySettingsDirty = false;
    // Forces the borderless desktop-resolution window before the engine creates it.
    void UseBorderlessWindow();
    UGameUserSettings* DisplaySettingsOwner() const;
    FLDDisplaySettingsResult SetDisplayResult(ELDDisplaySettingsStatus Status, const FString& Message);
};
