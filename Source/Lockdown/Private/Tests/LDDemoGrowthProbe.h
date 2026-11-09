#pragma once
#include "CoreMinimal.h"

class ALDPlayerController;

#if WITH_DEV_AUTOMATION_TESTS
namespace LDDemoTests
{
    // DEV-only owned-connection growth scenario: a rendered, participating Listen Host plays the victim and one owning
    // client earns kill rewards and makes its choices through its own connection.
    // Inert unless -LDDemoGrowthProbe and its isolated arguments are present.
    void StartGrowthProbe(ALDPlayerController& Controller);
}
#endif
