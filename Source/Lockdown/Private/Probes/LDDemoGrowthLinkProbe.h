#pragma once
#include "CoreMinimal.h"

class ALDPlayerController;

#if WITH_DEV_AUTOMATION_TESTS
namespace LDDemoTests
{
    // DEV-only short owned-connection growth scenario: a rendered Listen Host and one real owning client, no AI. State is
    // set with DEV test commands; the checks are only what a real connection can prove (a command arrives, its receipt
    // returns, the owner's own view and HUD follow, one real kill pays into the killer's view and the authority's record).
    // Inert unless -LDDemoGrowthLinkProbe and its isolated arguments are present.
    void StartGrowthLinkProbe(ALDPlayerController& Controller);
}
#endif
