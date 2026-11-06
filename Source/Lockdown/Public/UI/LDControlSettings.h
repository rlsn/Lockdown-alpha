#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "InputCoreTypes.h"
#include "Scalability.h"
#include "Character/LDCameraSettings.h"
#include "LDControlSettings.generated.h"

class ULDControlSettings;
class UGameUserSettings;
class FConfigCacheIni;
class IConsoleVariable;

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
    // A rejected binding set can still carry validated, unrelated camera/text/volume/minimap preferences and player name.
    ULDControlSettings* RecoverablePreferences = nullptr;
    FString Message;
};

struct FLDLocalSettingsSaveResult
{
    ELDLocalSettingsSaveStatus Status = ELDLocalSettingsSaveStatus::Invalid;
    FString Message;
    bool IsSaved() const { return Status == ELDLocalSettingsSaveStatus::Saved; }
};

// The player-editable display values. The window itself is not a setting: the game always runs borderless at the
// desktop resolution (ULDGameInstance::Init), so VSync and overall quality are applied with a plain Apply.
struct LOCKDOWN_API FLDDisplaySettings
{
    bool bVSync = false;
    Scalability::FQualityLevels Quality;

    static FLDDisplaySettings Read(const UGameUserSettings& Settings);
    // Product defaults for "restore defaults" (UI_AND_SETTINGS 8): the overall quality of SetToDefaults on a transient
    // object of the live settings class, and the engine's r.VSync default because SetToDefaults leaves VSync unchanged.
    // Live is not modified.
    static bool ProductDefaults(const UGameUserSettings& Live, FLDDisplaySettings& Out, FString& Error);
    bool operator==(const FLDDisplaySettings& Other) const;
    bool IsValid() const;
};

enum class ELDDisplaySettingsStatus : uint8
{
    Loaded, PersistenceVerified, Invalid, Unavailable, ApplyFailed, PersistenceFailed
};

struct FLDDisplaySettingsResult
{
    ELDDisplaySettingsStatus Status = ELDDisplaySettingsStatus::Unavailable;
    FString Message;
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
    // Scalability writes sg.ResolutionQuality straight into r.ScreenPercentage, but the engine treats 0 (the default: the
    // project's own screen percentage) as a no-op, so a render scale applied earlier stays until restart. For 0 this
    // drops the scalability-priority value of ScreenPercentage so the project default under it applies at once; other
    // values are applied by the engine. False when a scalability render scale is still in force for 0.
    LOCKDOWN_API bool ApplyRenderScale(IConsoleVariable& ScreenPercentage, float ResolutionQuality);
    // The only window the game uses: borderless at the desktop resolution, held as the confirmed mode. Only the fields
    // change (quality is kept, though the engine setters may adjust it); the caller applies the window.
    LOCKDOWN_API void HoldBorderlessDesktop(UGameUserSettings& Settings);
}

struct FLDControlBinding
{
    FName Action;
    FString Label;
    FKey Key;
    bool Axis=false;
};
// Which ground direction the minimap draws straight up (LDMinimap): the camera's ground forward, the own living body's
// facing (the camera's while there is no body) or world -Y. Saved as this value; any other stored value is rejected.
UENUM() enum class ELDMinimapOrientation : uint8 { Camera=0, Facing=1, North=2 };
// The local-preference groups the settings page restores one page at a time (UI_AND_SETTINGS 8); together they are exactly
// ResetLocalDefaults. Display values belong to UGameUserSettings and are restored through the display apply instead.
enum class ELDPreferenceGroup : uint8
{
    Audio,      // MasterVolume
    Controls,   // Bindings, bQuickCast
    Camera,     // Zoom, bEdgePan, bStartFollowing, CameraPanCmPerSec
    Interface   // TextScale, MinimapOrientation, bShowNetworkDiagnostics, bShowFPS
};
UCLASS()
class LOCKDOWN_API ULDControlSettings : public USaveGame
{
    GENERATED_BODY()
public:
    static constexpr int32 CurrentSchema = 15;
    static constexpr int32 MaxPlayerNameChars = 16;
    // Camera-only pan speed domain in cm/s, defined by the UI settings module (CONTROLS_AND_TARGETING 8).
    static constexpr float MinCameraPanCmPerSec = 900;
    static constexpr float MaxCameraPanCmPerSec = 3600;
    UPROPERTY(SaveGame) int32 Schema=CurrentSchema;
    UPROPERTY(SaveGame) TMap<FName,FKey> Bindings;
    UPROPERTY(SaveGame) bool bEdgePan=true;
    UPROPERTY(SaveGame) bool bStartFollowing=true;
    UPROPERTY(SaveGame) float TextScale=1;
    UPROPERTY(SaveGame) float Zoom=LDCamera::DefaultArmLength;
    // Empty means the player has not chosen a name yet; no default name is ever invented.
    UPROPERTY(SaveGame) FString PlayerName;
    // Linear 0..1 gain of the whole Demo sound output; 1, the default, applies no attenuation.
    UPROPERTY(SaveGame) float MasterVolume=1;
    // Keyboard and edge pan speed of the camera only; it never changes world distances, vision or body speed.
    // The 1800 default is the coordinator's decision, not a balance-tested value.
    UPROPERTY(SaveGame) float CameraPanCmPerSec=1800;
    // Minimap presentation only; it never changes what the minimap may show. The Camera default is the user's decision.
    UPROPERTY(SaveGame) ELDMinimapOrientation MinimapOrientation=ELDMinimapOrientation::Camera;
    // Quick cast (off by default, CONTROLS 3): Combat casts a unit skill at the pointed visible unit and a direction skill
    // toward the pointer on the key press itself, instead of arming it for a left-click confirmation.
    UPROPERTY(SaveGame) bool bQuickCast=false;
    UPROPERTY(SaveGame) bool bShowNetworkDiagnostics=false;
    UPROPERTY(SaveGame) bool bShowFPS=false;
    // The single Demo action registry; every binding, label and saved file uses it.
    static const TArray<FLDControlBinding>& Definitions();
    static FLDLocalSettingsLoadResult LoadLocalSettings(const FLDLocalSettingsStorage& Storage);
    static ULDControlSettings* CreateSessionSettings(const FLDLocalSettingsStorage& Storage);
    ULDControlSettings* CreateDraft() const;
    bool ApplyLocalSettings(const ULDControlSettings& Draft, FString& Error);
    FLDLocalSettingsSaveResult SaveLocalSettings();
    bool SetAppliedZoom(float Value, FString& Error);
    void UseRecommendedCameraZoom() { Zoom = LDCamera::DefaultArmLength; }
    bool SetAppliedMasterVolume(float Value, FString& Error);
    bool SetAppliedCameraPanSpeed(float Value, FString& Error);
    float GetCameraPanCmPerSec() const { return CameraPanCmPerSec; }
    bool SetAppliedMinimapOrientation(ELDMinimapOrientation Value, FString& Error);
    ELDMinimapOrientation GetMinimapOrientation() const { return MinimapOrientation; }
    bool GetQuickCast() const { return bQuickCast; }
    // Player-facing name of an orientation (跟随镜头 / 跟随角色朝向 / 固定正北); empty for any other value.
    static FString MinimapOrientationLabel(ELDMinimapOrientation Value);
    void SetAppliedFollowing(bool Value);
    // Local format check; the name travels as the ?LDName= URL option and the session domain applies this same rule again.
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
    // Restores every preference group (keys, quick cast, camera including the pan speed, text, minimap orientation,
    // diagnostics and master volume); the player name is never cleared by a defaults reset. Display values belong to
    // UGameUserSettings; the settings panel restores them through the display apply.
    void ResetLocalDefaults();
    // Restores one group only; every other preference and the player name keep their values.
    void ResetPreferenceGroup(ELDPreferenceGroup Group);
private:
    void CopyPreferences(const ULDControlSettings& Other);
    FLDLocalSettingsStorage Storage;
    ELDLocalSettingsPhase Phase = ELDLocalSettingsPhase::EditingDraft;
    bool bRecoveryRequired = false;
};
