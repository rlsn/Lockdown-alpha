#include "Audio/LDAudioSubsystem.h"
#include "Audio/LDAudioCatalog.h"
#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
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
    TArray<USoundWave*> Resolved;
    if (auto* Loaded = LoadObject<ULDAudioCatalog>(nullptr, ULDAudioCatalog::AssetPath, nullptr, LOAD_NoWarn))
    {
        if (Loaded->Resolve(Resolved, Error))
        {
            Catalog = Loaded;
            CueSounds.Reset();
            for (USoundWave* Sound : Resolved) { CueSounds.Add(Sound); }
        }
    }
    else { Error = FString::Printf(TEXT("Audio catalog %s is missing."), ULDAudioCatalog::AssetPath); }
    if (Catalog) { UE_LOG(LogTemp, Display, TEXT("LD_AUDIO_CATALOG_READY %d cues resolved."), LDAudio::CueCount); }
    else
    {
        CatalogError = Error;
        UE_LOG(LogTemp, Error, TEXT("LD_AUDIO_CATALOG_INVALID %s Run Scripts\\ImportDemoAudio.ps1 or restore the LFS assets."), *Error);
    }
    WorldHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ULDAudioSubsystem::HandleWorldReady);
}

void ULDAudioSubsystem::Deinitialize()
{
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(WorldHandle);
    if (bMixPushed && Catalog)
    {
        if (FAudioDeviceManager* Manager = FAudioDeviceManager::Get())
        {
            if (FAudioDevice* Device = Manager->GetAudioDeviceRaw(PushedDevice))
            {
                Device->ClearSoundMixClassOverride(Catalog->UserMix, Catalog->MasterClass, 0.f);
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
    if (bNewDevice || AppliedVolume != MasterVolume)
    {
        // One override on the user mix: the master class and its children follow the applied local volume.
        Device->SetSoundMixClassOverride(Catalog->UserMix, Catalog->MasterClass, MasterVolume, 1.f, 0.f, true);
        AppliedVolume = MasterVolume;
        UE_LOG(LogTemp, Display, TEXT("LD_AUDIO_MIX_APPLIED device=%u volume=%.2f"), DeviceId, MasterVolume);
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
        UE_LOG(LogTemp, Warning, TEXT("LD_AUDIO_VOLUME_REJECTED Non-finite master volume; keeping %.2f."), MasterVolume);
        return;
    }
    MasterVolume = FMath::Clamp(Linear, 0.f, 1.f);
    if (UGameInstance* Instance = GetGameInstance()) { PushVolume(Instance->GetWorld()); }
}

bool ULDAudioSubsystem::Play(ELDAudioCue Cue)
{
    if (!LDAudio::IsPlayableCue(Cue)) { return false; }
    if (!Catalog)
    {
        if (!bReportedUnavailable)
        {
            bReportedUnavailable = true;
            UE_LOG(LogTemp, Warning, TEXT("LD_AUDIO_CUE_UNAVAILABLE %s cannot play: %s"), *LDAudio::CueId(Cue), *CatalogError);
        }
        return false;
    }
    UGameInstance* Instance = GetGameInstance();
    UWorld* World = Instance ? Instance->GetWorld() : nullptr;
    if (!PushVolume(World))
    {
        // Automation runs with -nosound; that is an environment fact, not a content error.
        if (!bReportedNoDevice)
        {
            bReportedNoDevice = true;
            UE_LOG(LogTemp, Display, TEXT("LD_AUDIO_DEVICE_UNAVAILABLE No audio output in this process; %s and later cues are not audible."), *LDAudio::CueId(Cue));
        }
        return false;
    }
    UGameplayStatics::PlaySound2D(World, CueSounds[int32(Cue)], 1.f, 1.f, 0.f, nullptr, nullptr, true);
    return true;
}
