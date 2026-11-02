#pragma once
#include "CoreMinimal.h"

class ALDPlayerController;

#if WITH_DEV_AUTOMATION_TESTS
namespace LDDemoTests
{
    // DEV-only owned-connection growth and shop scenario: a rendered, participating Listen Host plays the victim and
    // one owning client earns kill rewards, makes a choice and trades at the shop through its own connection.
    // Inert unless -LDDemoGrowthProbe and its isolated arguments are present.
    void StartGrowthProbe(ALDPlayerController& Controller);
}
#endif
