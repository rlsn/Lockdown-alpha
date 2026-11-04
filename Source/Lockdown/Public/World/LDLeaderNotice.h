#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Framework/LDCaptureTypes.h"

class FLDDemoContentSnapshot;

// D27 (#79) 通报批评. Every LEADER.RefreshSec from the match start the authority names the leader: among the contenders
// still in the match, the unique highest level of at least LEADER.MinLevel, ties broken by kills, otherwise nobody. A
// leader who is dead at the refresh is not shown. The public view shows a LEADER.CircleRadiusCm circle whose centre is
// the leader's foot moved by a uniform random offset of at most LEADER.CircleMaxOffsetCm (never more than the radius,
// so the leader is always inside). Each refresh that names the same contender again adds LEADER.BountyGoldPerRefresh to
// the bounty, up to LEADER.BountyGoldMax; the first refresh already carries one step. Whoever kills the shown leader
// gets the bounty on top of the normal kill reward, locked at the fatal moment; the death ends the notice and the next
// refresh starts a new streak.
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
}

struct LOCKDOWN_API FLDLeaderRules
{
    int64 StartedAtMs = 0;
    int64 RefreshMs = 0;
    uint8 MinLevel = 0;
    double CircleRadiusCm = 0;
    double CircleMaxOffsetCm = 0;
    int64 BountyGoldPerRefresh = 0;
    int64 BountyGoldMax = 0;
    uint32 Seed = 0;
    bool IsValid() const;
};

struct LOCKDOWN_API FLDLeaderState
{
    FLDLeaderRules Rules;
    // The refresh index (1-based) last applied; zero before the first.
    uint64 RefreshIndex = 0;
    FGuid Leader;
    uint32 Streak = 0;
    FVector CircleCentreCm = FVector::ZeroVector;
    int64 RefreshedAtMs = 0;
    // The last shown leader's death: who, when and the bounty it paid on top of the kill reward (evidence and logs).
    FGuid LastDownLeader;
    int64 LastDownAtMs = 0;
    int64 LastDownBountyGold = 0;

    bool IsActive() const { return Rules.IsValid(); }
    // The refresh boundary NowMs falls on or after; true when a new refresh is due at NowMs.
    bool IsRefreshDue(int64 NowMs) const;
    // Applies the refresh at NowMs. True when the public view changed.
    bool Refresh(int64 NowMs, TConstArrayView<LDLeaderNotice::FCandidate> Candidates);
    // The shown leader died: the notice ends and its streak is spent. True when the view changed.
    bool LeaderDied(const FGuid& Contender, int64 NowMs);
    int64 BountyFor(const FGuid& Victim) const;
    int64 NextDueMs(int64 NowMs) const;
    FLDLeaderView View() const;
};

namespace LDLeaderNotice
{
    // The approved LEADER parameters for a match that started at StartedAtMs; the random stream is seeded from the
    // match identity. No fallback values.
    LOCKDOWN_API bool Resolve(const FLDDemoContentSnapshot& Content, int64 StartedAtMs, const FGuid& LedgerMatch,
        FLDLeaderState& Out, FString& Error);
}
