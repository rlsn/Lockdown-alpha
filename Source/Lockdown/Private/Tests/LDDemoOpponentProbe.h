#pragma once
#include "CoreMinimal.h"

class ALDPlayerController;

#if WITH_DEV_AUTOMATION_TESTS
namespace LDDemoTests
{
    // DEV-only: a full room of three humans on real owned connections and three opponent AI started through the
    // normal lobby. The humans stay out of play; every AI grows, trades, fights, respawns and wins by the same rules.
    void StartOpponentProbe(ALDPlayerController& Controller);
}
#endif
