#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Framework/LDCaptureTypes.h"

class ULDMapRegistry;
class FLDDemoContentSnapshot;

// D26 (#79) airdrop rules. The n-th drop (0-based) lands at StartedAt + AIRDROP.FirstLandSec + n * AIRDROP.IntervalSec
// on one of the public airdrop points, and is announced AIRDROP.AnnounceLeadSec earlier. There is one current drop: the
// next announcement replaces it whether or not it was claimed. The first living contender whose foot is inside the
// closed AIRDROP.PickupRadiusCm disk of a landed drop claims it once (nearest first, then contender id).
namespace LDAirdrop
{
    struct FPoint
    {
        FName Id;
        FVector PositionCm = FVector::ZeroVector;
    };

    struct FCandidate
    {
        FGuid Contender;
        FVector FootCm = FVector::ZeroVector;
    };

    // The nearest candidate inside the closed pickup disk, ties by contender id; false when none.
    LOCKDOWN_API bool PickClaimer(const FVector& CentreCm, double RadiusCm, TConstArrayView<FCandidate> Candidates, FGuid& Out);
}

struct LOCKDOWN_API FLDAirdropSchedule
{
    int64 StartedAtMs = 0;
    int64 FirstLandMs = 0;
    int64 IntervalMs = 0;
    int64 AnnounceLeadMs = 0;
    // Drop n uses point (PointOffset + n) % Points.Num(); the offset is fixed per match.
    int32 PointOffset = 0;
    TArray<LDAirdrop::FPoint> Points;
    double PickupRadiusCm = 0;

    bool IsValid() const;
    // Overflow-checked instants of drop Index; false when not representable.
    bool LandAtMs(uint64 Index, int64& Out) const;
    bool AnnounceAtMs(uint64 Index, int64& Out) const;
    const LDAirdrop::FPoint& PointOf(uint64 Index) const;
};

// Authority state of the current drop. Serial is the 1-based drop number; zero before the first announcement.
struct LOCKDOWN_API FLDAirdropState
{
    FLDAirdropSchedule Schedule;
    uint64 Serial = 0;
    ELDAirdropPhase Phase = ELDAirdropPhase::None;
    FGuid ClaimedBy;
    int64 ClaimedAtMs = 0;

    bool IsActive() const { return Schedule.IsValid(); }
    // Moves to the drop NowMs falls in. Announcement and landing are inclusive boundaries. True when the public view changed.
    bool Advance(int64 NowMs);
    // The next announcement or landing boundary strictly after NowMs; MAX_int64 when none is representable.
    int64 NextDueMs(int64 NowMs) const;
    // Landed -> Claimed by Contender at NowMs; false in any other phase.
    bool Claim(const FGuid& Contender, int64 NowMs);
    const LDAirdrop::FPoint* CurrentPoint() const;
    FLDAirdropView View() const;
};

namespace LDAirdrop
{
    // Binds every compiled airdrop anchor to its registered ground position and the approved AIRDROP parameters for a
    // match that started at StartedAtMs; PointOffset comes from the match identity. No fallback values.
    LOCKDOWN_API bool Resolve(const ULDMapRegistry& Registry, const FLDDemoContentSnapshot& Content, int64 StartedAtMs,
        const FGuid& LedgerMatch, FLDAirdropState& Out, FString& Error);
}
