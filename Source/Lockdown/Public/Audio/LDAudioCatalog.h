#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Audio/LDAudioTypes.h"
#include "LDAudioCatalog.generated.h"

class USoundWave;
class USoundClass;
class USoundMix;

USTRUCT()
struct LOCKDOWN_API FLDAudioCueEntry
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere) ELDAudioCue Cue = ELDAudioCue::None;
    // Hard reference: the cooked catalog carries every cue sound.
    UPROPERTY(VisibleAnywhere) TObjectPtr<USoundWave> Sound;
};

USTRUCT()
struct LOCKDOWN_API FLDMusicTrackEntry
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere) ELDMusicTrack Track = ELDMusicTrack::None;
    // Hard reference: the cooked catalog carries every music track.
    UPROPERTY(VisibleAnywhere) TObjectPtr<USoundWave> Sound;
};

// Typed cue -> sound table imported from SourceData\Audio\audio_cues.json by the editor audio import.
// The game resolves it once at startup; any missing or misrouted cue is an explicit error, never silence.
UCLASS()
class LOCKDOWN_API ULDAudioCatalog : public UDataAsset
{
    GENERATED_BODY()
public:
    static constexpr const TCHAR* AssetPath = TEXT("/Game/Lockdown/Audio/DA_DemoAudio.DA_DemoAudio");
    static constexpr const TCHAR* MasterClassPath = TEXT("/Game/Lockdown/Audio/SC_DemoMaster.SC_DemoMaster");
    static constexpr const TCHAR* UserMixPath = TEXT("/Game/Lockdown/Audio/SMix_DemoUser.SMix_DemoUser");
    static constexpr const TCHAR* MusicClassPath = TEXT("/Game/Lockdown/Audio/SC_DemoMusic.SC_DemoMusic");
    // The single master volume chain: every cue sound routes to MasterClass; the user volume overrides it in UserMix.
    UPROPERTY(VisibleAnywhere) TObjectPtr<USoundClass> MasterClass;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USoundMix> UserMix;
    // Music (#94) routes to MusicClass, a child of MasterClass, so it plays at the master volume times the music volume.
    UPROPERTY(VisibleAnywhere) TObjectPtr<USoundClass> MusicClass;
    // Exactly one entry per playable cue, in enum order.
    UPROPERTY(VisibleAnywhere) TArray<FLDAudioCueEntry> Cues;
    // Exactly one entry per music track, in enum order.
    UPROPERTY(VisibleAnywhere) TArray<FLDMusicTrackEntry> Music;
    // Complete, non-looping, non-spatial cue table routed to MasterClass, and a complete non-spatial music table routed to
    // MusicClass with each track's loop flag. OutSounds is indexed by cue value, OutMusic by track value.
    bool Resolve(TArray<USoundWave*>& OutSounds, TArray<USoundWave*>& OutMusic, FString& Error) const;
};
