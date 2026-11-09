#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Audio/LDAudioTypes.h"
#include "LDAudioSubsystem.generated.h"

class UAudioComponent;
class ULDAudioCatalog;
class USoundBase;
class USoundWave;
class UWorld;
struct FLDMusicRequest;

// Local sound output for one game instance: resolves the whole cue and music catalog once at startup and owns the single
// volume chain (SC_DemoMaster and its child SC_DemoMusic, overridden in SMix_DemoUser, UI_AND_SETTINGS 8). It never reads
// gameplay state; callers pass cues and music requests derived from DTOs they may already read, and every sound plays as
// a non-spatial 2D sound.
UCLASS()
class LOCKDOWN_API ULDAudioSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    // Applied local master volume, linear 0..1. Re-applied to the audio device of every new world.
    void ApplyMasterVolume(float Linear);
    float GetMasterVolume() const { return MasterVolume; }
    // Applied local music volume, linear 0..1, on top of the master volume.
    void ApplyMusicVolume(float Linear);
    float GetMusicVolume() const { return MusicVolume; }
    // Plays the requested track (LDAudio::SelectMusic). An undecided request or the track already playing changes nothing,
    // so the menu loop keeps playing across the travel between the main menu and the lobby; a different track starts at its
    // offset while the previous one fades out over LDAudio::MusicFadeOutSec. A one-shot track requested past its end stays
    // silent. False when the catalog or the audio device is unavailable (reported once, as for cues).
    bool SetMusic(const FLDMusicRequest& Request);
    ELDMusicTrack GetMusicTrack() const { return MusicTrack; }
    // False when the catalog failed to resolve (reported once as an error) or no audio device exists
    // (for example -nosound automation, reported once as a display line).
    bool Play(ELDAudioCue Cue);
    bool IsCatalogReady() const { return Catalog != nullptr; }
    const FString& GetCatalogError() const { return CatalogError; }
private:
    UPROPERTY(Transient) TObjectPtr<ULDAudioCatalog> Catalog;
    UPROPERTY(Transient) TArray<TObjectPtr<USoundBase>> CueSounds;
    UPROPERTY(Transient) TArray<TObjectPtr<USoundWave>> MusicSounds;
    // The current track's component and the outgoing ones still fading; all persist across level transitions.
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> MusicComponent;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> FadingMusic;
    ELDMusicTrack MusicTrack = ELDMusicTrack::None;
    FString CatalogError;
    float MasterVolume = 1.f;
    float MusicVolume = 1.f;
    // Last volumes handed to the device's class overrides; -1 until the first push.
    float AppliedVolume = -1.f;
    float AppliedMusicVolume = -1.f;
    bool bMixPushed = false;
    uint32 PushedDevice = 0;
    bool bReportedUnavailable = false;
    bool bReportedNoDevice = false;
    FDelegateHandle WorldHandle;
    void HandleWorldReady(UWorld* World);
    // Applies the override on the current world's audio device; pushes the user mix once per device.
    bool PushVolume(UWorld* World);
    // The world to play in, with the volume pushed; null (reported once) without a catalog or an audio device.
    UWorld* ReadyWorld(const FString& What);
    void StopMusic();
};
