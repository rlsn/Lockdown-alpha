#pragma once
#include "CoreMinimal.h"

class ALDPlayerController;

#if WITH_DEV_AUTOMATION_TESTS
namespace LDDemoTests
{
    // DEV-only owned-connection combat and orders scenario: through the ordinary lobby Start, one owning client
    // attack-moves onto a participating Listen Host and stops with S; the Host kills it with a right-click order and
    // audits the random protected respawn, which the client then spends with an empty Q. Inert unless
    // -LDDemoCombatProbe and its isolated arguments are present.
    void StartCombatProbe(ALDPlayerController& Controller);
}
#endif
