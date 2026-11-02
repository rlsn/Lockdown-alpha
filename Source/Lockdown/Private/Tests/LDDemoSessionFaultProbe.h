#pragma once
#include "CoreMinimal.h"

class ALDPlayerController;

#if WITH_DEV_AUTOMATION_TESTS
namespace LDDemoTests
{
    // DEV-only: real mid-match UI leave, Host loss and failed joins through the shipping entry and UI paths.
    void StartSessionFaultProbe(ALDPlayerController& Controller);
}
#endif
