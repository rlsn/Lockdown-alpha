#pragma once
#include "CoreMinimal.h"

class ALDPlayerController;

#if WITH_DEV_AUTOMATION_TESTS
namespace LDDemoTests
{
    // DEV-only owned-connection camp scenario: a participating Listen Host and two owning clients started through
    // the normal lobby. Inert unless -LDDemoCampProbe and its isolated arguments are present.
    void StartCampProbe(ALDPlayerController& Controller);
}
#endif
