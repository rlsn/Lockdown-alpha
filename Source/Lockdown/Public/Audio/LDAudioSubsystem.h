#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Audio/LDAudioTypes.h"
#include "LDAudioSubsystem.generated.h"

class UAudioComponent;
class ULDAudioCatalog;
class USoundAttenuation;
class USoundBase;
class USoundClass;
class USoundConcurrency;
class USoundWave;
class UWorld;
struct FLDMusicRequest;

// Local sound output for one game instance: resolves the whole cue, skill and music catalog once at startup and owns the
// volume chain (SC_DemoMaster with its bus children SC_DemoMusic, SC_DemoSfx and SC_DemoUI, overridden in SMix_DemoUser;
// AUDIO_AND_FEEL buses). It never reads gameplay state: callers pass cues derived from what they may already present.
// Own and interface sounds play 2D; a visible other body's sounds play at its location (PlayAt), so a sound never comes
// from a place the local player cannot see.
UCLASS()
class LOCKDOWN_API ULDAudioSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    // Applied local bus volume, linear 0..1 (non-finite values are rejected and keep the previous one). Re-applied to the
    // audio device of every new world. The effective gain of a bus is Master x its own.
    void ApplyBusVolume(ELDAudioBus Bus, float Linear);
    float GetBusVolume(ELDAudioBus Bus) const;
    // One short representative sound on Bus for the settings slider (Master and Sfx: a combo hit; UI: a click). Music has
    // none (music already plays). False without a catalog or an audio device.
    bool PlayBusPreview(ELDAudioBus Bus);
    // Plays the requested track (LDAudio::SelectMusic). An undecided request or the track already playing changes nothing,
    // so the menu loop keeps playing across the travel between the main menu and the lobby; a different track starts at its
    // offset while the previous one fades out over LDAudio::MusicFadeOutSec. A one-shot track requested past its end stays
    // silent. False when the catalog or the audio device is unavailable (reported once, as for cues).
    bool SetMusic(const FLDMusicRequest& Request);
    ELDMusicTrack GetMusicTrack() const { return MusicTrack; }
    // A 2D take of Cue. The multiplier is clamped to 0..2 and the pitch to 0.5..2; false for a non-finite value, a
    // missing catalog or no audio device.
    bool Play(ELDAudioCue Cue, float VolumeMultiplier = 1.f, float Pitch = 1.f);
    // A take of Cue at a world location with the body attenuation (only for bodies the local player sees).
    bool PlayAt(ELDAudioCue Cue, const FVector& Location, float VolumeMultiplier = 1.f, float Pitch = 1.f);
    // The release of an active skill (ELDDemoAbilityId), 2D (bSpatial false) or at Location; a skill without its own
    // sound releases with CastRelease.
    bool PlaySkillRelease(uint8 Ability, bool bSpatial, const FVector& Location, float VolumeMultiplier = 1.f);
    // A cue that its caller may cut short (a cast charge): the playing component, or null when nothing plays.
    UAudioComponent* Start(ELDAudioCue Cue, bool bSpatial, const FVector& Location, float VolumeMultiplier = 1.f);
    bool HasOwnSkillRelease(uint8 Ability) const;
    // A skill's own hit at a visible body's location; false (nothing played) when the skill has no hit of its own.
    bool PlaySkillHit(uint8 Ability, const FVector& Location, float VolumeMultiplier = 1.f);
    bool IsCatalogReady() const { return Catalog != nullptr; }
    const FString& GetCatalogError() const { return CatalogError; }
private:
    UPROPERTY(Transient) TObjectPtr<ULDAudioCatalog> Catalog;
    // Every take, flattened; CueTakes and SkillTakes index into it so the GC keeps them through this array.
    UPROPERTY(Transient) TArray<TObjectPtr<USoundWave>> Takes;
    UPROPERTY(Transient) TArray<TObjectPtr<USoundWave>> MusicSounds;
    UPROPERTY(Transient) TObjectPtr<USoundAttenuation> BodyAttenuation;
    UPROPERTY(Transient) TObjectPtr<USoundConcurrency> BodyConcurrency;
    UPROPERTY(Transient) TObjectPtr<USoundConcurrency> InterfaceConcurrency;
    // The current track's component and the outgoing ones still fading; all persist across level transitions.
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> MusicComponent;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> FadingMusic;
    TArray<TArray<int32>> CueTakes;
    TArray<TArray<int32>> SkillTakes;
    TArray<TArray<int32>> SkillHitTakes;
    TArray<int32> LastCueTake;
    TArray<int32> LastSkillTake;
    TArray<int32> LastSkillHitTake;
    USoundClass* BusClasses[LDAudio::BusCount] = {};
    ELDMusicTrack MusicTrack = ELDMusicTrack::None;
    FString CatalogError;
    float BusVolumes[LDAudio::BusCount] = { 1.f, .6f, 1.f, 1.f };
    // Last volumes handed to the device's class overrides; -1 until the first push.
    float AppliedVolumes[LDAudio::BusCount] = { -1.f, -1.f, -1.f, -1.f };
    bool bMixPushed = false;
    uint32 PushedDevice = 0;
    bool bReportedUnavailable = false;
    bool bReportedNoDevice = false;
    // -LDAudioEventLog: every play is logged with its world time and take (the evidence soundtrack renderer reads it).
    bool bEventLog = false;
    FRandomStream TakeRandom;
    FDelegateHandle WorldHandle;
    void HandleWorldReady(UWorld* World);
    // Applies the overrides on the current world's audio device; pushes the user mix once per device.
    bool PushVolume(UWorld* World);
    // The world to play in, with the volume pushed; null (reported once) without a catalog or an audio device.
    UWorld* ReadyWorld(const FString& What);
    // A take index into Takes that differs from Last when there is a choice; INDEX_NONE for none.
    int32 PickTake(const TArray<int32>& Pool, int32& Last);
    // Plays one take (logged first under -LDAudioEventLog); with Keep, a component the caller may stop is returned there.
    bool Emit(const FString& What, int32 Take, bool bSpatial, const FVector& Location, float Volume, float Pitch, bool bInterface,
        UAudioComponent** Keep = nullptr);
    void StopMusic();
};
