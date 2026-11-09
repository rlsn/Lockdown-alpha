#pragma once
#include "CoreMinimal.h"

// One contender's latest assist contribution (positive damage, absorbed part included, or any control or debuff)
// on one victim life. Stored on the victim's body, so a new life starts empty.
struct FLDAssistContribution
{
    FGuid Contributor;
    int64 LastAtMs=0;
};

// Kill XP after the assist split of one contender death.
struct FLDAssistRewardShares
{
    int64 KillerXP=0;
    int64 AssisterXP=0;     // each assister
};

namespace LDAssist
{
    // Records (or refreshes) Contributor's contribution at NowMs. Invalid or self contributions are ignored.
    // Returns true when the record changed.
    LOCKDOWN_API bool RecordContribution(TArray<FLDAssistContribution>& Contributions, const FGuid& Contributor,
        const FGuid& Victim, int64 NowMs);

    // Assisters of one death: every contributor whose latest contribution is at most WindowMs before DeathAtMs,
    // other than the killer and the victim, still in the match (IsPresent). Sorted, unique. An invalid killer has none.
    LOCKDOWN_API void SelectAssisters(const TArray<FLDAssistContribution>& Contributions, const FGuid& Killer,
        const FGuid& Victim, int64 DeathAtMs, int64 WindowMs, TFunctionRef<bool(const FGuid&)> IsPresent, TArray<FGuid>& Out);

    // Splits the victim's base kill XP evenly (floored) among the killer and the assisters. False on a negative input.
    LOCKDOWN_API bool SplitKillReward(int64 BaseXP, int32 AssisterCount, FLDAssistRewardShares& Out);
}
