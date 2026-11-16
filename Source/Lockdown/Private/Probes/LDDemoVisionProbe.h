#pragma once
#include "CoreMinimal.h"

class ALDPlayerController;

#if WITH_DEV_AUTOMATION_TESTS
namespace LDDemoTests
{
    // DEV-only owned-connection observation scenario: a rendered, participating Listen Host and two owning clients
    // started through the normal lobby. Inert unless -LDDemoVisionProbe and its isolated arguments are present.
    void StartVisionProbe(ALDPlayerController& Controller);
}
#endif
