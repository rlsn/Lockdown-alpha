#pragma once
#include "CoreMinimal.h"

class UWorld;
class ULDGameInstance;
class ALDPlayerController;

#if WITH_DEV_AUTOMATION_TESTS
namespace LDDemoTests
{
    struct FEntryObservation
    {
        bool Observed = false;
        bool RejectedBeforeWorldCreation = false;
        int32 Pawns = 0;
        int32 GameModes = 0;
        int32 Services = 0;
        int32 RegistryCreations = 0;
        int32 RegistryActivations = 0;
    };
    void StartEntryObservation(ULDGameInstance& Instance, bool IsolatedTest = false);
    void StopEntryObservation(ULDGameInstance& Instance);
    void ObserveRejectedEntry(ULDGameInstance& Instance, UWorld& World);
    void ObserveEntryBeforeWorld(ULDGameInstance& Instance);
    void EndEntryBrowse(ULDGameInstance& Instance);
    void ObserveMapRegistry(UWorld& World, bool Activation);
    FEntryObservation ReadEntryObservation(const ULDGameInstance& Instance);
    void ProbeRejectedEntry(UWorld& World);
    void StartFrontendSettingsProbe(UWorld& World);
    void StartWorldRoomProbe(ALDPlayerController& Controller);
    void StartCoreProbe(ALDPlayerController& Controller);
}
#endif
