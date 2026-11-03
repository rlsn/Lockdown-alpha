#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "GenericPlatform/GenericWindow.h"
#include "InputCoreTypes.h"
#include "Scalability.h"
#include "LDControlSettings.generated.h"

class ULDControlSettings;
class UGameUserSettings;
class FConfigCacheIni;

class LOCKDOWN_API FLDDisplayConfigWriteGuard
{
public:
    FLDDisplayConfigWriteGuard() = default;
    FLDDisplayConfigWriteGuard(const FLDDisplayConfigWriteGuard&) = delete;
    FLDDisplayConfigWriteGuard& operator=(const FLDDisplayConfigWriteGuard&) = delete;
    bool Begin(FConfigCacheIni& Cache, const FString& SettingsFile, const FString& QualityFile,
        const FString& SettingsSection, FString& Error);
    bool Release(FConfigCacheIni& Cache, FString& Error);
    bool IsActive() const { return Data.IsValid(); }
private:
    TSharedPtr<struct FLDDisplayConfigGuardData> Data;
};

// Migrated: an older schema was converted explicitly; its values are applied but not yet saved in the current schema.
enum class ELDLocalSettingsLoadStatus : uint8
{
    Loaded, Migrated, Missing, UnsupportedSchema, Corrupt, ReadFailed, InvalidStorage
};
enum class ELDLocalSettingsSaveStatus : uint8
{
    Saved, NoChanges, Invalid, WriteFailed, ReadbackFailed, RecoveryRequired
};
enum class ELDLocalSettingsPhase : uint8
{
    EditingDraft, ConfirmedApplied, PersistenceVerified
};

struct LOCKDOWN_API FLDLocalSettingsStorage
{
    static FLDLocalSettingsStorage User();
    static FLDLocalSettingsStorage Automation(const FGuid& Fixture);
    const FString& Filename() const { return Path; }
    bool IsValid() const { return !Path.IsEmpty(); }
    bool IsAutomation() const { return bAutomation; }
private:
    FString Path;
    bool bAutomation = false;
};

struct FLDLocalSettingsLoadResult
{
    ELDLocalSettingsLoadStatus Status = ELDLocalSettingsLoadStatus::ReadFailed;
    ULDControlSettings* Settings = nullptr;
    // A rejected binding set can still carry validated, unrelated camera/text/volume preferences and player name.
    ULDControlSettings* RecoverablePreferences = nullptr;
    FString Message;
};

struct FLDLocalSettingsSaveResult
{
    ELDLocalSettingsSaveStatus Status = ELDLocalSettingsSaveStatus::Invalid;
    FString Message;
    bool IsSaved() const { return Status == ELDLocalSettingsSaveStatus::Saved; }
};

struct LOCKDOWN_API FLDDisplaySettings
{
    FIntPoint Resolution = FIntPoint::ZeroValue;
    EWindowMode::Type WindowMode = EWindowMode::Windowed;
    EWindowMode::Type PreferredFullscreenMode = EWindowMode::WindowedFullscreen;
    bool bVSync = false;
    Scalability::FQualityLevels Quality;

    static FLDDisplaySettings Read(const UGameUserSettings& Settings);
    bool SameMode(const FLDDisplaySettings& Other) const;
    bool operator==(const FLDDisplaySettings& Other) const;
    bool IsValid() const;
};

enum class ELDDisplaySettingsStatus : uint8
{
    Loaded, TrialStarted, Applied, PersistenceVerified, Cancelled, NoTrial,
    Invalid, Unavailable, Busy, StaleTrial, ApplyPending, ApplyFailed,
    PersistenceFailed, RestorePending, RestoreFailed
};

struct FLDDisplaySettingsResult
{
    ELDDisplaySettingsStatus Status = ELDDisplaySettingsStatus::Unavailable;
    uint64 TrialId = 0;
    FString Message;
};

struct LOCKDOWN_API FLDDisplayTrialState
{
    uint64 Begin(const FLDDisplaySettings& Confirmed, const FLDDisplaySettings& Draft, double Now, double Duration);
    bool IsCurrent(uint64 Id) const { return Id != 0 && Id == ActiveId; }
    bool IsExpired(double Now) const;
    bool Invalidate(uint64 Id);
    uint64 Id() const { return ActiveId; }
    double Remaining(double Now) const;
    const FLDDisplaySettings& Baseline() const { return Before; }
    const FLDDisplaySettings& Candidate() const { return Pending; }
private:
    uint64 Sequence = 0;
    uint64 ActiveId = 0;
    double Deadline = 0;
    FLDDisplaySettings Before, Pending;
};

namespace LDLocalSettings
{
    LOCKDOWN_API bool ResolveConfigDestination(FConfigCacheIni& Cache, const FString& ConfigId,
        FString& Filename, FString& Error);
    LOCKDOWN_API FLDLocalSettingsSaveResult FlushAndVerifyDisplaySettingsFiles(
        FConfigCacheIni& Cache, const FString& SettingsFile, const FString& QualityFile,
        const FString& SettingsSection, const FLDDisplaySettings& Expected);
    LOCKDOWN_API FLDLocalSettingsSaveResult VerifyDisplaySettingsFile(
        const FString& SettingsFile, const FString& QualityFile, const FString& SettingsSection,
        const FLDDisplaySettings& Expected);
}

struct FLDControlBinding
{
    FName Action;
    FString Label;
    FKey Key;
    bool Axis=false;
};
UCLASS()
class LOCKDOWN_API ULDControlSettings : public USaveGame
{
    GENERATED_BODY()
public:
    static constexpr int32 CurrentSchema = 7;
    static constexpr int32 MaxPlayerNameChars = 16;
    UPROPERTY(SaveGame) int32 Schema=CurrentSchema;
    UPROPERTY(SaveGame) TMap<FName,FKey> Bindings;
    UPROPERTY(SaveGame) bool bEdgePan=true;
    UPROPERTY(SaveGame) bool bStartFollowing=true;
    UPROPERTY(SaveGame) float TextScale=1;
    UPROPERTY(SaveGame) float Zoom=1400;
    // Empty means the player has not chosen a name yet; no default name is ever invented.
    UPROPERTY(SaveGame) FString PlayerName;
    // Linear 0..1 gain of the whole Demo sound output; 1, the default, applies no attenuation.
    UPROPERTY(SaveGame) float MasterVolume=1;
    // The single Demo action registry; every binding, label and saved file uses it.
    static const TArray<FLDControlBinding>& Definitions();
    static FLDLocalSettingsLoadResult LoadLocalSettings(const FLDLocalSettingsStorage& Storage);
    static ULDControlSettings* CreateSessionSettings(const FLDLocalSettingsStorage& Storage);
    ULDControlSettings* CreateDraft() const;
    bool ApplyLocalSettings(const ULDControlSettings& Draft, FString& Error);
    FLDLocalSettingsSaveResult SaveLocalSettings();
    bool SetAppliedZoom(float Value, FString& Error);
    bool SetAppliedMasterVolume(float Value, FString& Error);
    void SetAppliedFollowing(bool Value);
    // Local format check only; the name travels as the ?Name= URL option and the session domain still normalizes or rejects it.
    static bool ValidatePlayerName(const FString& Name, FString& Error);
    bool SetAppliedPlayerName(const FString& Name, FString& Error);
    void AllowRecoverySave() { bRecoveryRequired = false; }
    void RequireRecovery() { bRecoveryRequired = true; }
    bool NeedsRecovery() const { return bRecoveryRequired; }
    bool IsDirty() const { return Phase == ELDLocalSettingsPhase::ConfirmedApplied; }
    ELDLocalSettingsPhase GetPhase() const { return Phase; }
    FKey Key(FName Action) const;
    FString Label(FName Action) const;
    bool Rebind(FName Action,FKey NewKey,FString& Error);
    bool Valid() const;
    bool ValidateLocalSettings(FString& Error) const;
    bool ValidatePreferences(FString& Error) const;
    void ResetBindings();
    // Restores keys, camera, text and master volume only; the player name is never cleared by a defaults reset.
    void ResetLocalDefaults();
private:
    void CopyPreferences(const ULDControlSettings& Other);
    FLDLocalSettingsStorage Storage;
    ELDLocalSettingsPhase Phase = ELDLocalSettingsPhase::EditingDraft;
    bool bRecoveryRequired = false;
};
