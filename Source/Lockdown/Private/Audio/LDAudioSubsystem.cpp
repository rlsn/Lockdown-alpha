#include "Audio/LDAudioSubsystem.h"
#include "Core/LDLog.h"
#include "Audio/LDAudioCatalog.h"
#include "Audio/LDMusicSelection.h"
#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundWave.h"
#include "UObject/UObjectGlobals.h"

bool ULDAudioSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    // A dedicated server has no local listener; every other game instance presents its own cues.
    return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void ULDAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    // Resolve the whole catalog once; a missing asset is an explicit error, never a silent default.
    FString Error;
    TArray<USoundWave*> Resolved, ResolvedMusic;
    if (auto* Loaded = LoadObject<ULDAudioCatalog>(nullptr, ULDAudioCatalog::AssetPath, nullptr, LOAD_NoWarn))
    {
        if (Loaded->Resolve(Resolved, ResolvedMusic, Error))
        {
            Catalog = Loaded;
            CueSounds.Reset();
            for (USoundWave* Sound : Resolved) { CueSounds.Add(Sound); }
            MusicSounds.Reset();
            for (USoundWave* Sound : ResolvedMusic) { MusicSounds.Add(Sound); }
        }
    }
    else { Error = FString::Printf(TEXT("Audio catalog %s is missing."), ULDAudioCatalog::AssetPath); }
    if (Catalog)
    { UE_LOG(LogLDContent, Display, TEXT("LD_AUDIO_CATALOG_READY %d cues and %d music tracks resolved."), LDAudio::CueCount, LDAudio::MusicTrackCount); }
    else
    {
        CatalogError = Error;
        UE_LOG(LogLDContent, Error, TEXT("LD_AUDIO_CATALOG_INVALID %s Run Scripts\\ImportDemoAudio.ps1 or restore the LFS assets."), *Error);
    }
    WorldHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ULDAudioSubsystem::HandleWorldReady);
}

void ULDAudioSubsystem::Deinitialize()
{
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(WorldHandle);
    StopMusic();
    if (bMixPushed && Catalog)
    {
        if (FAudioDeviceManager* Manager = FAudioDeviceManager::Get())
        {
            if (FAudioDevice* Device = Manager->GetAudioDeviceRaw(PushedDevice))
            {
                Device->ClearSoundMixClassOverride(Catalog->UserMix, Catalog->MasterClass, 0.f);
                Device->ClearSoundMixClassOverride(Catalog->UserMix, Catalog->MusicClass, 0.f);
                Device->PopSoundMixModifier(Catalog->UserMix);
            }
        }
    }
    bMixPushed = false;
    Super::Deinitialize();
}

void ULDAudioSubsystem::HandleWorldReady(UWorld* World)
{
    if (World && World->GetGameInstance() == GetGameInstance()) { PushVolume(World); }
}

bool ULDAudioSubsystem::PushVolume(UWorld* World)
{
    if (!Catalog || !World || !GEngine || !GEngine->UseSound()) { return false; }
    FAudioDeviceHandle Device = World->GetAudioDevice();
    if (!Device.IsValid()) { return false; }
    const uint32 DeviceId = Device.GetDeviceID();
    const bool bNewDevice = !bMixPushed || PushedDevice != DeviceId;
    if (bNewDevice || AppliedVolume != MasterVolume || AppliedMusicVolume != MusicVolume)
    {
        // Two overrides on the user mix: the master class and its children follow the master volume, and the music class
        // alone is scaled again by the music volume, so music plays at master x music.
        Device->SetSoundMixClassOverride(Catalog->UserMix, Catalog->MasterClass, MasterVolume, 1.f, 0.f, true);
        Device->SetSoundMixClassOverride(Catalog->UserMix, Catalog->MusicClass, MusicVolume, 1.f, 0.f, false);
        AppliedVolume = MasterVolume;
        AppliedMusicVolume = MusicVolume;
        UE_LOG(LogLDContent, Display, TEXT("LD_AUDIO_MIX_APPLIED device=%u volume=%.2f music=%.2f"), DeviceId, MasterVolume, MusicVolume);
    }
    if (bNewDevice)
    {
        Device->PushSoundMixModifier(Catalog->UserMix);
        bMixPushed = true;
        PushedDevice = DeviceId;
    }
    return true;
}

void ULDAudioSubsystem::ApplyMasterVolume(float Linear)
{
    if (!FMath::IsFinite(Linear))
    {
        UE_LOG(LogLDContent, Warning, TEXT("LD_AUDIO_VOLUME_REJECTED Non-finite master volume; keeping %.2f."), MasterVolume);
        return;
    }
    MasterVolume = FMath::Clamp(Linear, 0.f, 1.f);
    if (UGameInstance* Instance = GetGameInstance()) { PushVolume(Instance->GetWorld()); }
}

void ULDAudioSubsystem::ApplyMusicVolume(float Linear)
{
    if (!FMath::IsFinite(Linear))
    {
        UE_LOG(LogLDContent, Warning, TEXT("LD_AUDIO_VOLUME_REJECTED Non-finite music volume; keeping %.2f."), MusicVolume);
        return;
    }
    MusicVolume = FMath::Clamp(Linear, 0.f, 1.f);
    if (UGameInstance* Instance = GetGameInstance()) { PushVolume(Instance->GetWorld()); }
}

UWorld* ULDAudioSubsystem::ReadyWorld(const FString& What)
{
    if (!Catalog)
    {
        if (!bReportedUnavailable)
        {
            bReportedUnavailable = true;
            UE_LOG(LogLDContent, Warning, TEXT("LD_AUDIO_CUE_UNAVAILABLE %s cannot play: %s"), *What, *CatalogError);
        }
        return nullptr;
    }
    UGameInstance* Instance = GetGameInstance();
    UWorld* World = Instance ? Instance->GetWorld() : nullptr;
    if (!PushVolume(World))
    {
        // Automation runs with -nosound; that is an environment fact, not a content error.
        if (!bReportedNoDevice)
        {
            bReportedNoDevice = true;
            UE_LOG(LogLDContent, Display, TEXT("LD_AUDIO_DEVICE_UNAVAILABLE No audio output in this process; %s and later sounds are not audible."), *What);
        }
        return nullptr;
    }
    return World;
}

bool ULDAudioSubsystem::Play(ELDAudioCue Cue)
{
    if (!LDAudio::IsPlayableCue(Cue)) { return false; }
    UWorld* World = ReadyWorld(LDAudio::CueId(Cue));
    if (!World) { return false; }
    UGameplayStatics::PlaySound2D(World, CueSounds[int32(Cue)], 1.f, 1.f, 0.f, nullptr, nullptr, true);
    return true;
}

void ULDAudioSubsystem::StopMusic()
{
    for (UAudioComponent* Component : FadingMusic) { if (Component) { Component->Stop(); } }
    FadingMusic.Reset();
    if (MusicComponent) { MusicComponent->Stop(); }
    MusicComponent = nullptr;
    MusicTrack = ELDMusicTrack::None;
}

bool ULDAudioSubsystem::SetMusic(const FLDMusicRequest& Request)
{
    if (!Request.bDecided || Request.Track == MusicTrack) { return true; }
    const int32 Index = int32(Request.Track);
    if (Index < 0 || Index >= int32(ELDMusicTrack::Count)) { return false; }
    UWorld* World = ReadyWorld(TEXT("Music ") + LDAudio::MusicTrackId(Request.Track));
    if (!World) { return false; }
    // Outgoing components that finished fading are released; the current one starts its own fade.
    FadingMusic.RemoveAll([](const UAudioComponent* Component) { return !Component || !Component->IsPlaying(); });
    if (MusicComponent)
    {
        MusicComponent->FadeOut(LDAudio::MusicFadeOutSec, 0.f);
        FadingMusic.Add(MusicComponent);
        MusicComponent = nullptr;
    }
    const ELDMusicTrack Previous = MusicTrack;
    MusicTrack = Request.Track;
    USoundWave* Sound = Request.Track == ELDMusicTrack::None ? nullptr : MusicSounds[Index].Get();
    // A one-shot track joined past its end has nothing left to play (for example a late entry after the farm track).
    const bool bPastEnd = Sound && !Sound->bLooping && Request.StartSec >= Sound->Duration;
    if (Sound && !bPastEnd)
    {
        // Persisting across level transitions lets the menu loop carry on from the main menu into the lobby and back.
        MusicComponent = UGameplayStatics::CreateSound2D(World, Sound, 1.f, 1.f, Request.StartSec, nullptr, true, false);
        if (!MusicComponent)
        {
            UE_LOG(LogLDContent, Warning, TEXT("LD_AUDIO_MUSIC_UNAVAILABLE %s could not start."), *LDAudio::MusicTrackId(Request.Track));
            return false;
        }
        MusicComponent->Play(Request.StartSec);
    }
    UE_LOG(LogLDContent, Display, TEXT("LD_AUDIO_MUSIC from=%s to=%s start=%.2f%s"), *LDAudio::MusicTrackId(Previous),
        *LDAudio::MusicTrackId(Request.Track), Request.StartSec, bPastEnd ? TEXT(" past-end") : TEXT(""));
    return true;
}
