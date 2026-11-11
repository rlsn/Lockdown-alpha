#pragma once

#include "CoreMinimal.h"

struct FLDDemoContentParameters;
struct FLDDemoMonsterDefinition;

// #97 camp growth (CONTENT_AND_BALANCE §6.2): a camp life takes stage
// min(CAMP.GrowthMaxStage, floor(time since T=0 / CAMP.GrowthStageSec)) when it spawns and keeps it for that whole life,
// through return resets, until it dies. Each stage adds a fixed share of the base HP, attack and XP reward
// (floor in their stored units) and a flat resist bonus. The Boss is not a camp and stays at stage 0.
namespace LDCampGrowth
{
    // Stage of a life spawned SinceMatchStartMs after T=0; the opening countdown (negative) is stage 0.
    LOCKDOWN_API uint8 Stage(const FLDDemoContentParameters& Parameters, int64 SinceMatchStartMs);

    // Base monster combat values at Stage. False (Error set) for a stage past the cap or an arithmetic overflow.
    LOCKDOWN_API bool ScaleBody(const FLDDemoContentParameters& Parameters, const FLDDemoMonsterDefinition& Base, uint8 Stage,
        FLDDemoMonsterDefinition& Out, FString& Error);

    // Per-life XP reward of Base at Stage.
    LOCKDOWN_API bool RewardXP(const FLDDemoContentParameters& Parameters, const FLDDemoMonsterDefinition& Base, uint8 Stage,
        int64& Out);
}
