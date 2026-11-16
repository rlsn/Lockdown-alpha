#pragma once
#include "CoreMinimal.h"

class UWorld;

#if WITH_DEV_AUTOMATION_TESTS
namespace LDDemoTests
{
    // DEV-only (-LDDemoPerfBudget -LDDemoPerfBudgetOutput=<Artifacts dir>): in the Demo map it frames the game camera on
    // open floor, measures frame, game, render, RHI and GPU time with no frame cap, first on the empty field and then
    // with a crowd of real presented bodies (every contender seat and every camp tier), at the low and high quality
    // presets, writes PerfBudget.json against the frame budgets of docs/demo/CONTENT_AND_BALANCE.md and exits.
    void StartPerfBudgetProbe(UWorld& World);
}
#endif
