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
    LOCKDOWN_API void StartEntryObservation(ULDGameInstance& Instance, bool IsolatedTest = false);
    LOCKDOWN_API void StopEntryObservation(ULDGameInstance& Instance);
    LOCKDOWN_API void ObserveRejectedEntry(ULDGameInstance& Instance, UWorld& World);
    LOCKDOWN_API void ObserveEntryBeforeWorld(ULDGameInstance& Instance);
    LOCKDOWN_API void EndEntryBrowse(ULDGameInstance& Instance);
    LOCKDOWN_API void ObserveMapRegistry(UWorld& World, bool Activation);
    LOCKDOWN_API FEntryObservation ReadEntryObservation(const ULDGameInstance& Instance);
    void ProbeRejectedEntry(UWorld& World);
    void StartFrontendSettingsProbe(UWorld& World);
    void StartWorldRoomProbe(ALDPlayerController& Controller);
    void StartCoreProbe(ALDPlayerController& Controller);
    void StartSoloProbe(ALDPlayerController& Controller);
}
#endif
