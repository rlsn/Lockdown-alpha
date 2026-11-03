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
    // The single master volume chain: every cue sound routes to MasterClass; the user volume overrides it in UserMix.
    UPROPERTY(VisibleAnywhere) TObjectPtr<USoundClass> MasterClass;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USoundMix> UserMix;
    // Exactly one entry per playable cue, in enum order.
    UPROPERTY(VisibleAnywhere) TArray<FLDAudioCueEntry> Cues;
    // Complete, non-looping, non-spatial table routed to MasterClass. OutSounds is indexed by cue value.
    bool Resolve(TArray<USoundWave*>& OutSounds, FString& Error) const;
};
