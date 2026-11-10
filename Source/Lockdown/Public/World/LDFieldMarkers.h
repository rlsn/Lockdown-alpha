#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Framework/LDCaptureTypes.h"

class FLDDemoContentSnapshot;
struct FLDDemoWorldGeometry;

// #198 outer §5 camp markers (approved https://github.com/rlsn/Lockdown-alpha/issues/198#issuecomment-6752601175 and
// comment 6752628806: "批准整套方案，按票内参数实施"). In each timed field segment s (1..3), at field progress
// T = (s-1) * MATCH.FieldSegmentSec + CAMPMARK.RevealSec, one High camp and one Elite camp are publicly marked. The High
// camp is a uniform draw among the High camps other than the previous segment's; the Elite camp is a uniform draw among
// the Elite camps in the map quadrant opposite the High camp (both coordinate signs flipped around the flagpole) other
// than the previous segment's Elite camp, or among all Elite camps but the previous one when that quadrant has none. The
// draws come from a server-only seed and are fixed once revealed. A marker binds the real camp: the first contender who
// clears it (its clearing death's credit owner) while it is revealed takes it once; a camp already cleared at the reveal
// waits for its ordinary refresh. Markers that nobody took expire at the segment end (the following rest); the final
// field has none.
namespace LDFieldMarkers
{
    struct FCandidate
    {
        FName CampId;
        FVector PositionCm = FVector::ZeroVector;
    };

    // Public map quadrant of a position around CentreCm: bit 0 set west of it (X < centre), bit 1 set south (Y < centre).
    LOCKDOWN_API int32 Quadrant(const FVector& PositionCm, const FVector& CentreCm);
    // Uniform draw among Count candidates other than Previous (any when Previous is INDEX_NONE or Count is 1); INDEX_NONE
    // when Count is not positive.
    LOCKDOWN_API int32 PickHigh(uint64 Seed, uint8 Segment, int32 Previous, int32 Count);
    // The Elite candidate of a segment whose High camp stands at HighCm (see the rule above); INDEX_NONE when none.
    LOCKDOWN_API int32 PickElite(uint64 Seed, uint8 Segment, const FVector& HighCm, const FVector& CentreCm,
        TConstArrayView<FCandidate> Elite, int32 Previous);
    LOCKDOWN_API const TCHAR* KindName(ELDFieldMarkerKind Kind);
    // The growth business event of a marker kind's reward (camp_marker_high / camp_marker_elite); its serial is the segment.
    LOCKDOWN_API FName RewardEvent(ELDFieldMarkerKind Kind);
}

struct LOCKDOWN_API FLDFieldMarkerSchedule
{
    // Field progress T origin of the first timed segment.
    int64 StartedAtMs = 0;
    int64 SegmentMs = 0;
    int64 RevealOffsetMs = 0;
    uint8 Segments = 0;
    // Server-only, never published.
    uint64 Seed = 0;
    FVector CentreCm = FVector::ZeroVector;
    TArray<LDFieldMarkers::FCandidate> High;
    TArray<LDFieldMarkers::FCandidate> Elite;

    bool IsValid() const;
    // Overflow-checked instants of segment Segment (1-based); false outside 1..Segments.
    bool RevealAtMs(uint8 Segment, int64& Out) const;
    bool EndAtMs(uint8 Segment, int64& Out) const;
};

struct FLDFieldMarker
{
    ELDFieldMarkerKind Kind = ELDFieldMarkerKind::None;
    // Index into the schedule's High or Elite candidates.
    int32 Candidate = INDEX_NONE;
    ELDFieldMarkerPhase Phase = ELDFieldMarkerPhase::None;
    // Authority only; the public view never names the claimer.
    FGuid ClaimedBy;
    int64 ClaimedAtMs = 0;
};

// Authority state: the markers of the current (or last) timed segment, High first. Segment is 0 before the first reveal.
struct LOCKDOWN_API FLDFieldMarkerState
{
    FLDFieldMarkerSchedule Schedule;
    uint8 Segment = 0;
    FLDFieldMarker High;
    FLDFieldMarker Elite;

    bool IsActive() const { return Schedule.IsValid(); }
    // Reveals every segment whose reveal instant NowMs reached (each drawing against the one before it) and expires the
    // unclaimed markers whose segment ended. OutRevealed / OutExpired list the changes (for logs). True when the public view
    // changed.
    bool Advance(int64 NowMs, TArray<FLDFieldMarker>& OutRevealed, TArray<FLDFieldMarker>& OutExpired);
    // The next reveal or segment end strictly after NowMs; MAX_int64 when none.
    int64 NextDueMs(int64 NowMs) const;
    // The revealed, unclaimed marker of CampId whose segment contains NowMs; nullptr otherwise.
    const FLDFieldMarker* OpenMarkerFor(FName CampId, int64 NowMs) const;
    // Revealed -> Claimed by Contender; false in any other phase.
    bool Claim(ELDFieldMarkerKind Kind, const FGuid& Contender, int64 NowMs);
    // Segment end (rest entry): every still revealed marker expires; OutExpired lists them.
    void EndSegment(TArray<FLDFieldMarker>& OutExpired);
    const LDFieldMarkers::FCandidate* CampOf(const FLDFieldMarker& Marker) const;
    // High then Elite of Segment; empty before the first reveal.
    void View(TArray<FLDFieldMarkerView>& Out) const;
};

namespace LDFieldMarkers
{
    // Candidates from the approved camp placements (High and Elite tiers at their compiled anchors), the flagpole as the
    // map centre, MATCH.FieldSegmentSec and CAMPMARK.RevealSec, for the timed segments of a match whose field progress
    // starts at StartedAtMs. The seed derives from the server-only airdrop seed (so a build evaluation replays it). No
    // fallback values.
    LOCKDOWN_API bool Resolve(const FLDDemoContentSnapshot& Content, const FLDDemoWorldGeometry& Geometry, int64 StartedAtMs,
        uint64 AirdropSeed, FLDFieldMarkerState& Out, FString& Error);
}
