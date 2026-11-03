#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Audio/LDAudioTypes.h"
#include "LDAudioSubsystem.generated.h"

class ULDAudioCatalog;
class USoundBase;
class UWorld;

// Local sound output for one game instance: resolves the whole cue catalog once at startup and owns the single
// master-volume chain (SC_DemoMaster overridden in SMix_DemoUser, UI_AND_SETTINGS 8). It never reads gameplay
// state; callers pass cues derived from DTOs they may already read, and every cue plays as a non-spatial 2D sound.
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
    // False when the catalog failed to resolve (reported once as an error) or no audio device exists
    // (for example -nosound automation, reported once as a display line).
    bool Play(ELDAudioCue Cue);
    bool IsCatalogReady() const { return Catalog != nullptr; }
    const FString& GetCatalogError() const { return CatalogError; }
private:
    UPROPERTY(Transient) TObjectPtr<ULDAudioCatalog> Catalog;
    UPROPERTY(Transient) TArray<TObjectPtr<USoundBase>> CueSounds;
    FString CatalogError;
    float MasterVolume = 1.f;
    bool bMixPushed = false;
    uint32 PushedDevice = 0;
    bool bReportedUnavailable = false;
    bool bReportedNoDevice = false;
    FDelegateHandle WorldHandle;
    void HandleWorldReady(UWorld* World);
    // Applies the override on the current world's audio device; pushes the user mix once per device.
    bool PushVolume(UWorld* World);
};
