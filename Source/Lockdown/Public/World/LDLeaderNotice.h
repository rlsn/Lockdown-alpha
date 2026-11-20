#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Framework/LDCaptureTypes.h"

class FLDDemoContentSnapshot;

// D27 (#79) 上热搜. Every LEADER.RefreshSec from the match start the authority names the leader: among the contenders
// still in the match, the unique highest level of at least LEADER.MinLevel, ties broken by kills, otherwise nobody. A
// leader who is dead at the refresh is not shown. The public view shows a LEADER.CircleRadiusCm circle whose centre is
// the leader's foot moved by a uniform random offset of at most LEADER.CircleMaxOffsetCm (never more than the radius,
// so the leader is always inside). The shown leader's death ends the notice until the next refresh names someone. Spec
// §10.4 (P6): a trending contender's death is recorded as a bounty death (its reward is not set yet). Spec §11.1 rumor day: every refresh names the top two (the runner-up with its own circle).
namespace LDLeaderNotice
{
    struct FCandidate
    {
        FGuid Contender;
        uint8 Level = 0;
        uint64 Kills = 0;
        bool bAlive = false;
        FVector FootCm = FVector::ZeroVector;
    };

    // The unique highest-level candidate at or above MinLevel, ties by kills; invalid when none is unique.
    LOCKDOWN_API FGuid PickLeader(uint8 MinLevel, TConstArrayView<FCandidate> Candidates);
    // Up to Count candidates at or above MinLevel by level, then kills; a tie that would cross the cut admits none of the
    // tied group (Count 1 is PickLeader). Best first.
    LOCKDOWN_API void PickLeaders(uint8 MinLevel, int32 Count, TConstArrayView<FCandidate> Candidates, TArray<FGuid>& Out);
}

struct LOCKDOWN_API FLDLeaderRules
{
    int64 StartedAtMs = 0;
    int64 RefreshMs = 0;
    uint8 MinLevel = 0;
    double CircleRadiusCm = 0;
    double CircleMaxOffsetCm = 0;
    uint32 Seed = 0;
    // How many contenders a refresh names (1; the rumor day's DAILY.RumorLeaders).
    int32 Leaders = 1;
    bool IsValid() const;
};

struct LOCKDOWN_API FLDLeaderState
{
    FLDLeaderRules Rules;
    // The refresh index (1-based) last applied; zero before the first.
    uint64 RefreshIndex = 0;
    FGuid Leader;
    uint8 LeaderLevel = 0;
    FVector CircleCentreCm = FVector::ZeroVector;
    int64 RefreshedAtMs = 0;
    FGuid RunnerUp;
    FVector RunnerUpCentreCm = FVector::ZeroVector;
    // The last shown leader's death: who and when (evidence and logs).
    FGuid LastDownLeader;
    int64 LastDownAtMs = 0;

    bool IsActive() const { return Rules.IsValid(); }
    // The refresh boundary NowMs falls on or after; true when a new refresh is due at NowMs.
    bool IsRefreshDue(int64 NowMs) const;
    // Applies the refresh at NowMs. True when the public view changed.
    bool Refresh(int64 NowMs, TConstArrayView<LDLeaderNotice::FCandidate> Candidates);
    // A shown trending contender died: its notice ends until the next refresh. True when the view changed.
    bool LeaderDied(const FGuid& Contender, int64 NowMs);
    // Whether Contender is shown trending now (the bounty target).
    bool IsTrending(const FGuid& Contender) const { return Contender.IsValid() && (Contender == Leader || Contender == RunnerUp); }
    int64 NextDueMs(int64 NowMs) const;
    FLDLeaderView View() const;
};

namespace LDLeaderNotice
{
    // The approved LEADER parameters for a match that started at StartedAtMs; the random stream is seeded from the
    // match identity. No fallback values.
    LOCKDOWN_API bool Resolve(const FLDDemoContentSnapshot& Content, int64 StartedAtMs, const FGuid& LedgerMatch,
        bool bRumorDay, FLDLeaderState& Out, FString& Error);
}
