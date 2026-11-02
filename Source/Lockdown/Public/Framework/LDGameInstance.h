#pragma once
#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/EngineBaseTypes.h"
#include "Containers/Ticker.h"
#include "UI/LDControlSettings.h"
#include "LDGameInstance.generated.h"

UCLASS()
class LOCKDOWN_API ULDGameInstance : public UGameInstance
{
    GENERATED_BODY()
public:
    FString ConnectionMessage;
    uint32 AuthorityGeneration = 0;
    bool AllocateMatchGeneration(uint32& Generation, FString& Error);
    virtual AGameModeBase* CreateGameModeForURL(FURL URL, UWorld* World) override;
    virtual void StartGameInstance() override;
    void RecordEntryRejectionBeforeWorld(const FString& URL, const FString& Reason);
    const FString& RejectedEntryURL() const { return LastRejectedEntryURL; }
    TSharedPtr<struct FLDResetEvidence> ResetEvidence;
    TSharedPtr<struct FLDDevelopmentResetEvidence> DevelopmentResetEvidence;
    TSharedPtr<struct FLDB90ResetEvidence> B90ResetEvidence;
    virtual void Init() override;
    virtual void Shutdown() override;
    ULDControlSettings* GetLocalControlSettings(uint8 Registry=6);
    ULDControlSettings* UseIsolatedLocalSettings(const FGuid& Fixture, uint8 Registry);
    FLDLocalSettingsSaveResult ApplyLocalSettings(const ULDControlSettings& Draft);
    FLDLocalSettingsSaveResult FlushLocalSettings(bool VerifyCurrent=false);
    FLDLocalSettingsSaveResult ConfirmLocalSettingsRecovery();
    void RequestLocalSettingsFlush();
    void PrepareLocalSettingsForTravel();
    const FString& GetLocalSettingsMessage() const { return LocalSettingsMessage; }

    FLDDisplaySettingsResult ReadDisplaySettings(FLDDisplaySettings& Out) const;
    bool GetDisplayResolutions(TArray<FIntPoint>& Out, FString& Error) const;
    FLDDisplaySettingsResult ApplyDisplaySettings(const FLDDisplaySettings& Draft, UObject* TrialOwner);
    FLDDisplaySettingsResult BeginDisplayTrial(const FLDDisplaySettings& Draft, UObject* TrialOwner);
    FLDDisplaySettingsResult ConfirmDisplayTrial(uint64 TrialId);
    FLDDisplaySettingsResult CancelDisplayTrial(uint64 TrialId);
    FLDDisplaySettingsResult RetryDisplayRestore();
    FLDDisplaySettingsResult RetryDisplaySave();
    uint64 GetDisplayTrialId() const { return DisplayTrial.Id(); }
    double GetDisplayTrialRemaining() const;
    bool IsDisplayRestorePending() const { return bDisplayRestorePending; }
    bool AreDisplaySettingsDirty() const { return bDisplaySettingsDirty; }
    const FLDDisplaySettingsResult& GetDisplaySettingsResult() const { return DisplayResult; }
private:
    uint64 EntryAttemptSerial = 0;
    FString LastRejectedEntryURL;
    FTSTicker::FDelegateHandle EntryReturnTicker;
    FDelegateHandle NetworkHandle, TravelHandle;
    void NetworkFailed(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error);
    void TravelFailed(UWorld* World, ETravelFailure::Type Type, const FString& Error);
    void ReturnAfterFailure(UWorld* World, const FString& Error);
    UPROPERTY(Transient) TObjectPtr<ULDControlSettings> LocalControlSettings;
    UPROPERTY(Transient) TMap<uint8, TObjectPtr<ULDControlSettings>> LocalSettingsByRegistry;
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

    FLDDisplayTrialState DisplayTrial;
    FLDDisplayConfigWriteGuard DisplayConfigGuard;
    TSharedPtr<class IInputProcessor> DisplayInputGuard;
    UPROPERTY(Transient) TObjectPtr<UGameUserSettings> DisplaySettingsDuringTrial;
    TWeakObjectPtr<UObject> DisplayTrialOwner;
    FLDDisplaySettings RestoreDisplay;
    FLDDisplaySettingsResult DisplayResult;
    bool bDisplaySettingsDirty = false;
    bool bDisplayRestorePending = false;
    double DisplayRestoreStartedAt = 0;
    UGameUserSettings* DisplaySettingsOwner() const;
    bool InstallDisplayInputGuard();
    bool ReleaseDisplayConfigGuard();
    bool DisplayModeApplied(const FLDDisplaySettings& Expected) const;
    FLDDisplaySettingsResult SetDisplayResult(ELDDisplaySettingsStatus Status, const FString& Message, uint64 Id=0);
    FLDDisplaySettingsResult ApplyNonResolutionAndSave(const FLDDisplaySettings& Draft);
};
