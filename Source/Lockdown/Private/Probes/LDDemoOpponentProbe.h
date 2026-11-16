#pragma once
#include "CoreMinimal.h"

class ALDPlayerController;

#if WITH_DEV_AUTOMATION_TESTS
namespace LDDemoTests
{
    // DEV-only: a full room of three humans on real owned connections and three opponent AI started through the
    // normal lobby. The humans stay out of play while the AI move, grow, fight, respawn and win by the same rules.
    void StartOpponentProbe(ALDPlayerController& Controller);
    // DEV-only telemetry (role Telemetry): the idle Listen Host and three opponent AI play -LDDemoOpponentMatches=N
    // matches in a row; one JSON file per match records how it unfolded (LDDemoOpponentTelemetry.cpp).
    void StartOpponentTelemetry(ALDPlayerController& Controller, const FString& Output, int32 TimeoutSeconds);
}
#endif
