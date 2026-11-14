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
        TEXT("UIClick"), TEXT("SelfShieldHit"), TEXT("SelfAttack"), nullptr, nullptr, TEXT("SelfShieldBreak"),
        TEXT("SelfDash"), TEXT("UIHover"), TEXT("UIPickup"), TEXT("UIInstall"), TEXT("UIReject"), TEXT("UIWorkshopOpen"),
        TEXT("UIWorkshopClose"), TEXT("UIOfferDeal"), TEXT("UIChoose"), TEXT("UIGain"), TEXT("UIAddPoint"), TEXT("UIHoldCharge"),
        TEXT("UIFinalLock")
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

    constexpr const TCHAR* MusicTrackIds[] = { TEXT("None"), TEXT("Menu"), TEXT("Farm"), TEXT("Escalation") };
    static_assert(UE_ARRAY_COUNT(MusicTrackIds) == int32(ELDMusicTrack::Count), "Every music track needs exactly one stable id.");
}

FString LDAudio::MusicTrackId(ELDMusicTrack Track)
{
    const int32 Index = int32(Track);
    return Index >= 0 && Index < int32(ELDMusicTrack::Count) ? FString(MusicTrackIds[Index]) : FString::Printf(TEXT("Track%d"), Index);
}

bool LDAudio::ParseMusicTrackId(const FString& Id, ELDMusicTrack& Out)
{
    for (int32 Index = 1; Index < int32(ELDMusicTrack::Count); ++Index)
    {
        if (Id.Equals(MusicTrackIds[Index], ESearchCase::CaseSensitive)) { Out = ELDMusicTrack(Index); return true; }
    }
    Out = ELDMusicTrack::None;
    return false;
}

bool LDAudio::MusicTrackLoops(ELDMusicTrack Track)
{
    return Track == ELDMusicTrack::Menu || Track == ELDMusicTrack::Escalation;
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

bool ULDAudioCatalog::Resolve(TArray<USoundWave*>& OutSounds, TArray<USoundWave*>& OutMusic, FString& Error) const
{
    OutSounds.Reset();
    OutSounds.SetNumZeroed(int32(ELDAudioCue::Count));
    OutMusic.Reset();
    OutMusic.SetNumZeroed(int32(ELDMusicTrack::Count));
    if (!MasterClass || !UserMix || !MusicClass)
    {
        Error = TEXT("The master SoundClass, the music SoundClass or the user SoundMix is missing.");
        return false;
    }
    // The music volume multiplies the master volume only while music is a child of the master class.
    if (MusicClass == MasterClass || MusicClass->ParentClass != MasterClass || !MasterClass->ChildClasses.Contains(MusicClass))
    {
        Error = TEXT("The music SoundClass is not a child of the master SoundClass.");
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
    if (Music.Num() != LDAudio::MusicTrackCount)
    {
        Error = FString::Printf(TEXT("Expected %d music tracks, found %d."), LDAudio::MusicTrackCount, Music.Num());
        return false;
    }
    for (int32 Index = 0; Index < Music.Num(); ++Index)
    {
        const FLDMusicTrackEntry& Entry = Music[Index];
        const ELDMusicTrack Expected = ELDMusicTrack(Index + 1);
        const FString Id = LDAudio::MusicTrackId(Expected);
        if (Entry.Track != Expected)
        { Error = FString::Printf(TEXT("Music entry %d is %s, expected %s."), Index, *LDAudio::MusicTrackId(Entry.Track), *Id); return false; }
        const USoundWave* Sound = Entry.Sound;
        if (!Sound) { Error = FString::Printf(TEXT("Music track %s has no sound."), *Id); return false; }
        if (Sound->SoundClassObject != MusicClass)
        { Error = FString::Printf(TEXT("Music track %s (%s) is not routed to the music SoundClass."), *Id, *Sound->GetPathName()); return false; }
        if (Sound->bLooping != LDAudio::MusicTrackLoops(Expected) || Sound->AttenuationSettings)
        {
            Error = FString::Printf(TEXT("Music track %s (%s) must be a non-spatial %s sound."), *Id, *Sound->GetPathName(),
                LDAudio::MusicTrackLoops(Expected) ? TEXT("looping") : TEXT("one-shot"));
            return false;
        }
        // The wave's own length, not the looping play duration; a track is minutes long, never hours.
        if (!(Sound->Duration > 0.f) || Sound->Duration > 3600.f)
        { Error = FString::Printf(TEXT("Music track %s (%s) has no finite audio duration (%.3f s)."), *Id, *Sound->GetPathName(), Sound->Duration); return false; }
        OutMusic[int32(Expected)] = const_cast<USoundWave*>(Sound);
    }
    Error.Reset();
    return true;
}
