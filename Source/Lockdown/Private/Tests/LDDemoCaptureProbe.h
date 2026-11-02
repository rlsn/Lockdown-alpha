#pragma once
#include "CoreMinimal.h"

class ALDPlayerController;

#if WITH_DEV_AUTOMATION_TESTS
namespace LDDemoTests
{
    // DEV-only: normal start, real owned capture/contest/completion, result and return to Lobby.
    void StartCaptureProbe(ALDPlayerController& Controller);
}
#endif
