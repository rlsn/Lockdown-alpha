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

enum class ELDLocalSettingsLoadStatus : uint8
{
    Loaded, Missing, UnsupportedSchema, Corrupt, ReadFailed, InvalidStorage
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
    static FLDLocalSettingsStorage User(uint8 RequestedProfile=6);
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
    // A rejected binding set can still carry validated, unrelated camera preferences.
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
    static constexpr int32 CurrentSchema = 4;
    UPROPERTY(SaveGame) int32 Schema=CurrentSchema;
    UPROPERTY(SaveGame) uint8 Profile=6;
    UPROPERTY(SaveGame) TMap<FName,FKey> Bindings;
    UPROPERTY(SaveGame) bool bEdgePan=true;
    UPROPERTY(SaveGame) bool bStartFollowing=true;
    UPROPERTY(SaveGame) float TextScale=1;
    UPROPERTY(SaveGame) float Zoom=1400;
    static const TArray<FLDControlBinding>& Defaults(bool Tactical=false);
    const TArray<FLDControlBinding>& Definitions() const {return Profile==8?HybridDefaults():Defaults(Profile==7);}
    static const TArray<FLDControlBinding>& HybridDefaults();
    static FLDLocalSettingsLoadResult LoadLocalSettings(const FLDLocalSettingsStorage& Storage, uint8 RequestedProfile=6);
    static ULDControlSettings* CreateSessionSettings(const FLDLocalSettingsStorage& Storage, uint8 RequestedProfile=6);
    ULDControlSettings* CreateDraft() const;
    bool ApplyLocalSettings(const ULDControlSettings& Draft, FString& Error);
    FLDLocalSettingsSaveResult SaveLocalSettings();
    bool SetAppliedZoom(float Value, FString& Error);
    void SetAppliedFollowing(bool Value);
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
    void ResetLocalDefaults();
private:
    void CopyPreferences(const ULDControlSettings& Other);
    FLDLocalSettingsStorage Storage;
    ELDLocalSettingsPhase Phase = ELDLocalSettingsPhase::EditingDraft;
    bool bRecoveryRequired = false;
};
