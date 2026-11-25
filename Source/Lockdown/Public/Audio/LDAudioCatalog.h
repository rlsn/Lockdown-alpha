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
    // Further takes of the same cue; a play picks one of Sound and these, never the take it played last.
    UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<USoundWave>> Variants;
};

// A sound keyed by a content number instead of a cue: the release of one active skill (ELDDemoAbilityId).
USTRUCT()
struct LOCKDOWN_API FLDAudioKeyedEntry
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere) uint8 Key = 0;
    UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<USoundWave>> Sounds;
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
    static constexpr const TCHAR* SfxClassPath = TEXT("/Game/Lockdown/Audio/SC_DemoSfx.SC_DemoSfx");
    static constexpr const TCHAR* UIClassPath = TEXT("/Game/Lockdown/Audio/SC_DemoUI.SC_DemoUI");
    // The single volume chain: MasterClass has exactly the bus children MusicClass, SfxClass and UIClass, every sound routes
    // to one of them, and the user volumes override the four classes in UserMix (bus = master x its own volume).
    UPROPERTY(VisibleAnywhere) TObjectPtr<USoundClass> MasterClass;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USoundMix> UserMix;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USoundClass> MusicClass;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USoundClass> SfxClass;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USoundClass> UIClass;
    // Exactly one entry per playable cue, in enum order.
    UPROPERTY(VisibleAnywhere) TArray<FLDAudioCueEntry> Cues;
    // Exactly one entry per music track, in enum order.
    UPROPERTY(VisibleAnywhere) TArray<FLDMusicTrackEntry> Music;
    // Skill releases by ability number, ascending; a skill without an entry releases with the CastRelease cue.
    UPROPERTY(VisibleAnywhere) TArray<FLDAudioKeyedEntry> SkillReleases;
    // Skill hits by ability number, ascending: the body sound of a hit the effects matched to that skill's visible strike.
    // A skill without an entry hits with its look's family cue (HitElectric, HitFire, ...).
    UPROPERTY(VisibleAnywhere) TArray<FLDAudioKeyedEntry> SkillHits;

    // What Resolve hands to the player: every take of every cue (indexed by cue value), music by track value, skill takes by
    // ability number (empty = none of its own) and each class by bus.
    struct FResolved
    {
        TArray<TArray<USoundWave*>> Cues;
        TArray<USoundWave*> Music;
        TArray<TArray<USoundWave*>> Skills;
        TArray<TArray<USoundWave*>> SkillHits;
        USoundClass* Buses[LDAudio::BusCount] = {};
    };
    // Complete one-shot non-spatial cue table routed to the SFX or UI class, a complete music table routed to MusicClass
    // with each track's loop flag, and one-shot skill takes on the SFX class.
    bool Resolve(FResolved& Out, FString& Error) const;
    // The bus a cue routes to (UI for the interface cues, SFX for everything else).
    static ELDAudioBus BusOf(ELDAudioCue Cue);
};
