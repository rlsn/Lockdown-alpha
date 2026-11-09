#include "Audio/LDAudioCatalog.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundWave.h"

namespace
{
    // Literal ids instead of reflected enum names: non-editor builds do not preserve FName casing.
    // Indexed by cue number; a retired number is a null hole.
    constexpr const TCHAR* AudioCueIds[] =
    {
        TEXT("None"), TEXT("KillNotice"), TEXT("CaptureOpened"), TEXT("CaptureStarted"), TEXT("CaptureContested"),
        TEXT("MatchVictory"), TEXT("MatchDefeat"), TEXT("MatchEndedNoWinner"), TEXT("SelfHit"), TEXT("SelfCast"),
        nullptr, nullptr, TEXT("LevelUp"), TEXT("SelfDeath"), TEXT("SelfRespawn"),
        TEXT("UIClick"), TEXT("SelfShieldHit"), TEXT("SelfAttack")
    };
    static_assert(UE_ARRAY_COUNT(AudioCueIds) == int32(ELDAudioCue::Count), "Every cue number needs exactly one stable id or a retired hole.");

    constexpr int32 AudioRetiredIds()
    {
        int32 Holes = 0;
        for (const TCHAR* Id : AudioCueIds) { Holes += Id == nullptr; }
        return Holes;
    }
    static_assert(AudioRetiredIds() == LDAudio::RetiredCueCount, "RetiredCueCount must match the retired holes.");

    const TCHAR* AudioCueIdAt(int32 Index) { return Index >= 0 && Index < int32(ELDAudioCue::Count) ? AudioCueIds[Index] : nullptr; }
}

bool LDAudio::IsPlayableCue(ELDAudioCue Cue)
{
    return Cue > ELDAudioCue::None && AudioCueIdAt(int32(Cue)) != nullptr;
}

ELDAudioCue LDAudio::PlayableCue(int32 Index)
{
    for (int32 Value = 1; Index >= 0 && Value < int32(ELDAudioCue::Count); ++Value)
    {
        if (AudioCueIds[Value] && Index-- == 0) { return ELDAudioCue(Value); }
    }
    return ELDAudioCue::None;
}

FString LDAudio::CueId(ELDAudioCue Cue)
{
    const int32 Index = int32(Cue);
    const TCHAR* Id = AudioCueIdAt(Index);
    return Id ? FString(Id) : FString::Printf(TEXT("Cue%d"), Index);
}

bool LDAudio::ParseCueId(const FString& Id, ELDAudioCue& Out)
{
    for (int32 Index = 1; Index < int32(ELDAudioCue::Count); ++Index)
    {
        if (AudioCueIds[Index] && Id.Equals(AudioCueIds[Index], ESearchCase::CaseSensitive)) { Out = ELDAudioCue(Index); return true; }
    }
    Out = ELDAudioCue::None;
    return false;
}

bool ULDAudioCatalog::Resolve(TArray<USoundWave*>& OutSounds, FString& Error) const
{
    OutSounds.Reset();
    OutSounds.SetNumZeroed(int32(ELDAudioCue::Count));
    if (!MasterClass || !UserMix)
    {
        Error = TEXT("The master SoundClass or the user SoundMix is missing.");
        return false;
    }
    if (Cues.Num() != LDAudio::CueCount)
    {
        Error = FString::Printf(TEXT("Expected %d cue entries, found %d."), LDAudio::CueCount, Cues.Num());
        return false;
    }
    for (int32 Index = 0; Index < Cues.Num(); ++Index)
    {
        const FLDAudioCueEntry& Entry = Cues[Index];
        const ELDAudioCue Expected = LDAudio::PlayableCue(Index);
        const FString Id = LDAudio::CueId(Expected);
        if (Entry.Cue != Expected)
        { Error = FString::Printf(TEXT("Entry %d is %s, expected %s."), Index, *LDAudio::CueId(Entry.Cue), *Id); return false; }
        const USoundWave* Sound = Entry.Sound;
        if (!Sound) { Error = FString::Printf(TEXT("Cue %s has no sound."), *Id); return false; }
        if (Sound->SoundClassObject != MasterClass)
        { Error = FString::Printf(TEXT("Cue %s (%s) is not routed to the master SoundClass."), *Id, *Sound->GetPathName()); return false; }
        if (Sound->bLooping || Sound->AttenuationSettings)
        { Error = FString::Printf(TEXT("Cue %s (%s) must be a one-shot non-spatial sound."), *Id, *Sound->GetPathName()); return false; }
        // Cues are short one-shots; a missing or absurd duration means the import produced no usable audio.
        if (!(Sound->Duration > 0.f) || Sound->Duration > 30.f)
        { Error = FString::Printf(TEXT("Cue %s (%s) has no short finite audio duration (%.3f s)."), *Id, *Sound->GetPathName(), Sound->Duration); return false; }
        OutSounds[int32(Expected)] = const_cast<USoundWave*>(Sound);
    }
    Error.Reset();
    return true;
}
