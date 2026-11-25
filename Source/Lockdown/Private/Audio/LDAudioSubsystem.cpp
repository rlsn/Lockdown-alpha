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
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundWave.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    // Body sounds of others: full level within 2000 cm of the listener (the game camera sits about 1500-1900 cm from the
    // ground it looks at), silent 2500 cm beyond, panned by the camera's view.
    constexpr float BodyInnerRadiusCm = 2000.f;
    constexpr float BodyFalloffCm = 2500.f;
    // Simultaneous voices: bodies keep the 16 loudest; the interface keeps its 4 newest. A cue retriggered within 25 ms
    // is one sound (eight hits in one frame never stack into one loud phase-smeared hit).
    constexpr int32 BodyVoices = 16;
    constexpr int32 InterfaceVoices = 4;
    constexpr float RetriggerSeconds = .025f;
}

bool ULDAudioSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    // A dedicated server has no local listener; every other game instance presents its own cues.
    return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void ULDAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    bEventLog = FParse::Param(FCommandLine::Get(), TEXT("LDAudioEventLog"));
    TakeRandom.Initialize(0x4C44);
    // Resolve the whole catalog once; a missing asset is an explicit error, never a silent default.
    FString Error;
    ULDAudioCatalog::FResolved Resolved;
    if (auto* Loaded = LoadObject<ULDAudioCatalog>(nullptr, ULDAudioCatalog::AssetPath, nullptr, LOAD_NoWarn))
    {
        if (Loaded->Resolve(Resolved, Error))
        {
            Catalog = Loaded;
            Takes.Reset();
            const auto Flatten = [this](const TArray<TArray<USoundWave*>>& From, TArray<TArray<int32>>& To)
            {
                To.SetNum(From.Num());
                for (int32 Key = 0; Key < From.Num(); ++Key)
                {
                    for (USoundWave* Take : From[Key]) { To[Key].Add(Takes.Add(Take)); }
                }
            };
            Flatten(Resolved.Cues, CueTakes);
            Flatten(Resolved.Skills, SkillTakes);
            LastCueTake.Init(INDEX_NONE, CueTakes.Num());
            LastSkillTake.Init(INDEX_NONE, SkillTakes.Num());
            MusicSounds.Reset();
            for (USoundWave* Sound : Resolved.Music) { MusicSounds.Add(Sound); }
            for (int32 Bus = 0; Bus < LDAudio::BusCount; ++Bus) { BusClasses[Bus] = Resolved.Buses[Bus]; }
        }
    }
    else { Error = FString::Printf(TEXT("Audio catalog %s is missing."), ULDAudioCatalog::AssetPath); }
    if (Catalog)
    {
        int32 Skills = 0;
        for (const TArray<int32>& Pool : SkillTakes) { Skills += !Pool.IsEmpty(); }
        UE_LOG(LogLDContent, Display, TEXT("LD_AUDIO_CATALOG_READY %d cues (%d takes), %d skill releases and %d music tracks resolved."),
            LDAudio::CueCount, Takes.Num(), Skills, LDAudio::MusicTrackCount);
    }
    else
    {
        CatalogError = Error;
        UE_LOG(LogLDContent, Error, TEXT("LD_AUDIO_CATALOG_INVALID %s Run Scripts\\ImportDemoAudio.ps1 or restore the LFS assets."), *Error);
    }
    BodyAttenuation = NewObject<USoundAttenuation>(this, TEXT("LDBodyAttenuation"));
    FSoundAttenuationSettings& Falloff = BodyAttenuation->Attenuation;
    Falloff.bAttenuate = true;
    Falloff.bSpatialize = true;
    Falloff.AttenuationShape = EAttenuationShape::Sphere;
    Falloff.AttenuationShapeExtents = FVector(BodyInnerRadiusCm, 0.f, 0.f);
    Falloff.FalloffDistance = BodyFalloffCm;
    Falloff.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
    BodyConcurrency = NewObject<USoundConcurrency>(this, TEXT("LDBodyConcurrency"));
    BodyConcurrency->Concurrency.MaxCount = BodyVoices;
    BodyConcurrency->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::StopQuietest;
    BodyConcurrency->Concurrency.RetriggerTime = RetriggerSeconds;
    InterfaceConcurrency = NewObject<USoundConcurrency>(this, TEXT("LDInterfaceConcurrency"));
    InterfaceConcurrency->Concurrency.MaxCount = InterfaceVoices;
    InterfaceConcurrency->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::StopOldest;
    InterfaceConcurrency->Concurrency.RetriggerTime = RetriggerSeconds;
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
                for (USoundClass* Class : BusClasses) { Device->ClearSoundMixClassOverride(Catalog->UserMix, Class, 0.f); }
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
    bool bChanged = bNewDevice;
    for (int32 Bus = 0; Bus < LDAudio::BusCount; ++Bus) { bChanged |= AppliedVolumes[Bus] != BusVolumes[Bus]; }
    if (bChanged)
    {
        // One override per class on the user mix: the master class and its children follow the master volume, and each
        // bus class alone is scaled again by its own volume, so a bus plays at master x bus.
        for (int32 Bus = 0; Bus < LDAudio::BusCount; ++Bus)
        {
            Device->SetSoundMixClassOverride(Catalog->UserMix, BusClasses[Bus], BusVolumes[Bus], 1.f, 0.f, Bus == int32(ELDAudioBus::Master));
            AppliedVolumes[Bus] = BusVolumes[Bus];
        }
        UE_LOG(LogLDContent, Display, TEXT("LD_AUDIO_MIX_APPLIED device=%u master=%.2f music=%.2f sfx=%.2f ui=%.2f"), DeviceId,
            BusVolumes[0], BusVolumes[1], BusVolumes[2], BusVolumes[3]);
    }
    if (bNewDevice)
    {
        Device->PushSoundMixModifier(Catalog->UserMix);
        bMixPushed = true;
        PushedDevice = DeviceId;
    }
    return true;
}

void ULDAudioSubsystem::ApplyBusVolume(ELDAudioBus Bus, float Linear)
{
    const int32 Index = int32(Bus);
    if (Index < 0 || Index >= LDAudio::BusCount) { return; }
    if (!FMath::IsFinite(Linear))
    {
        UE_LOG(LogLDContent, Warning, TEXT("LD_AUDIO_VOLUME_REJECTED Non-finite %s volume; keeping %.2f."), *LDAudio::BusId(Bus), BusVolumes[Index]);
        return;
    }
    BusVolumes[Index] = FMath::Clamp(Linear, 0.f, 1.f);
    if (UGameInstance* Instance = GetGameInstance()) { PushVolume(Instance->GetWorld()); }
}

float ULDAudioSubsystem::GetBusVolume(ELDAudioBus Bus) const
{
    const int32 Index = int32(Bus);
    return Index >= 0 && Index < LDAudio::BusCount ? BusVolumes[Index] : 0.f;
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

int32 ULDAudioSubsystem::PickTake(const TArray<int32>& Pool, int32& Last)
{
    if (Pool.IsEmpty()) { return INDEX_NONE; }
    int32 Pick = Pool[TakeRandom.RandRange(0, Pool.Num() - 1)];
    if (Pool.Num() > 1 && Pick == Last)
    {
        const int32 At = Pool.IndexOfByKey(Pick);
        Pick = Pool[(At + 1 + TakeRandom.RandRange(0, Pool.Num() - 2)) % Pool.Num()];
    }
    Last = Pick;
    return Pick;
}

bool ULDAudioSubsystem::Emit(const FString& What, int32 Take, bool bSpatial, const FVector& Location, float Volume,
    float Pitch, bool bInterface, UAudioComponent** Keep)
{
    if (Keep) { *Keep = nullptr; }
    if (!FMath::IsFinite(Volume) || !FMath::IsFinite(Pitch) || (bSpatial && Location.ContainsNaN()))
    {
        UE_LOG(LogLDContent, Warning, TEXT("LD_AUDIO_CUE_REJECTED %s has a non-finite volume, pitch or location."), *What);
        return false;
    }
    if (!Takes.IsValidIndex(Take)) { ReadyWorld(What); return false; }
    Volume = FMath::Clamp(Volume, 0.f, 2.f);
    Pitch = FMath::Clamp(Pitch, .5f, 2.f);
    USoundWave* Sound = Takes[Take];
    if (bEventLog)
    {
        // The world clock is the capture harness's fixed-step clock; the take, gain and pitch are exactly what plays.
        const UGameInstance* Instance = GetGameInstance();
        const UWorld* Clock = Instance ? Instance->GetWorld() : nullptr;
        UE_LOG(LogLDContent, Display, TEXT("LD_AUDIO_EVENT t=%.4f cue=%s take=%s gain=%.3f pitch=%.3f spatial=%d x=%.0f y=%.0f z=%.0f"),
            Clock ? Clock->GetTimeSeconds() : -1.0, *What, *GetNameSafe(Sound), Volume * Sound->Volume, Pitch, bSpatial ? 1 : 0,
            Location.X, Location.Y, Location.Z);
    }
    UWorld* World = ReadyWorld(What);
    if (!World) { return false; }
    USoundConcurrency* Concurrency = bInterface ? InterfaceConcurrency.Get() : BodyConcurrency.Get();
    if (Keep)
    {
        *Keep = bSpatial
            ? UGameplayStatics::SpawnSoundAtLocation(World, Sound, Location, FRotator::ZeroRotator, Volume, Pitch, 0.f, BodyAttenuation, Concurrency, true)
            : UGameplayStatics::SpawnSound2D(World, Sound, Volume, Pitch, 0.f, Concurrency, false, true);
        return *Keep != nullptr;
    }
    if (bSpatial) { UGameplayStatics::PlaySoundAtLocation(World, Sound, Location, FRotator::ZeroRotator, Volume, Pitch, 0.f, BodyAttenuation, Concurrency); }
    else { UGameplayStatics::PlaySound2D(World, Sound, Volume, Pitch, 0.f, Concurrency, nullptr, bInterface); }
    return true;
}

bool ULDAudioSubsystem::Play(ELDAudioCue Cue, float VolumeMultiplier, float Pitch)
{
    if (!LDAudio::IsPlayableCue(Cue) || !CueTakes.IsValidIndex(int32(Cue))) { ReadyWorld(LDAudio::CueId(Cue)); return false; }
    const int32 Take = PickTake(CueTakes[int32(Cue)], LastCueTake[int32(Cue)]);
    if (Take == INDEX_NONE) { return false; }
    const bool bInterface = ULDAudioCatalog::BusOf(Cue) == ELDAudioBus::UI;
    return Emit(LDAudio::CueId(Cue), Take, false, FVector::ZeroVector, VolumeMultiplier, Pitch, bInterface);
}

bool ULDAudioSubsystem::PlayAt(ELDAudioCue Cue, const FVector& Location, float VolumeMultiplier, float Pitch)
{
    if (!LDAudio::IsPlayableCue(Cue) || !CueTakes.IsValidIndex(int32(Cue))) { ReadyWorld(LDAudio::CueId(Cue)); return false; }
    const int32 Take = PickTake(CueTakes[int32(Cue)], LastCueTake[int32(Cue)]);
    if (Take == INDEX_NONE) { return false; }
    return Emit(LDAudio::CueId(Cue), Take, true, Location, VolumeMultiplier, Pitch, false);
}

bool ULDAudioSubsystem::HasOwnSkillRelease(uint8 Ability) const
{
    return SkillTakes.IsValidIndex(Ability) && !SkillTakes[Ability].IsEmpty();
}

bool ULDAudioSubsystem::PlaySkillRelease(uint8 Ability, bool bSpatial, const FVector& Location, float VolumeMultiplier)
{
    if (!HasOwnSkillRelease(Ability))
    {
        return bSpatial ? PlayAt(ELDAudioCue::CastRelease, Location, VolumeMultiplier) : Play(ELDAudioCue::CastRelease, VolumeMultiplier);
    }
    const int32 Take = PickTake(SkillTakes[Ability], LastSkillTake[Ability]);
    return Emit(FString::Printf(TEXT("Skill%d"), Ability), Take, bSpatial, Location, VolumeMultiplier, 1.f, false);
}

UAudioComponent* ULDAudioSubsystem::Start(ELDAudioCue Cue, bool bSpatial, const FVector& Location, float VolumeMultiplier)
{
    if (!LDAudio::IsPlayableCue(Cue) || !CueTakes.IsValidIndex(int32(Cue))) { ReadyWorld(LDAudio::CueId(Cue)); return nullptr; }
    const int32 Take = PickTake(CueTakes[int32(Cue)], LastCueTake[int32(Cue)]);
    UAudioComponent* Playing = nullptr;
    if (Take != INDEX_NONE) { Emit(LDAudio::CueId(Cue), Take, bSpatial, Location, VolumeMultiplier, 1.f, false, &Playing); }
    return Playing;
}

bool ULDAudioSubsystem::PlayBusPreview(ELDAudioBus Bus)
{
    switch (Bus)
    {
    case ELDAudioBus::Master:
    case ELDAudioBus::Sfx: return Play(ELDAudioCue::HitBasic);
    case ELDAudioBus::UI: return Play(ELDAudioCue::UIClick);
    default: return false;
    }
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
