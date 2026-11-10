#include "World/LDFieldMarkers.h"
#include "Development/LDDemoContentCatalog.h"
#include "Region/LDMapGeometry.h"
#include "World/LDAirdrop.h"
#include "World/LDCampLifecycle.h"

namespace
{
    bool MarkerAdd(int64 A, int64 B, int64& Out)
    {
        if ((B > 0 && A > MAX_int64 - B) || (B < 0 && A < MIN_int64 - B)) { return false; }
        Out = A + B;
        return true;
    }

    bool MarkerMul(int64 A, int64 B, int64& Out)
    {
        if (A < 0 || B < 0 || (A != 0 && B > MAX_int64 / A)) { return false; }
        Out = A * B;
        return true;
    }

    // One uniform roll per segment and kind from the server-only seed.
    uint64 MarkerRoll(uint64 Seed, uint8 Segment, ELDFieldMarkerKind Kind)
    {
        return LDAirdrop::Mix(Seed ^ LDAirdrop::Mix(uint64(Segment) << 8 | uint64(Kind)));
    }

    FLDFieldMarker& MarkerOf(FLDFieldMarkerState& State, ELDFieldMarkerKind Kind)
    {
        return Kind == ELDFieldMarkerKind::High ? State.High : State.Elite;
    }
}

int32 LDFieldMarkers::Quadrant(const FVector& PositionCm, const FVector& CentreCm)
{
    return (PositionCm.X < CentreCm.X ? 1 : 0) | (PositionCm.Y < CentreCm.Y ? 2 : 0);
}

int32 LDFieldMarkers::PickHigh(uint64 Seed, uint8 Segment, int32 Previous, int32 Count)
{
    if (Count <= 0) { return INDEX_NONE; }
    const uint64 Roll = MarkerRoll(Seed, Segment, ELDFieldMarkerKind::High);
    if (Count == 1 || Previous < 0 || Previous >= Count) { return int32(Roll % uint64(Count)); }
    return int32((uint64(Previous) + 1 + Roll % uint64(Count - 1)) % uint64(Count));
}

int32 LDFieldMarkers::PickElite(uint64 Seed, uint8 Segment, const FVector& HighCm, const FVector& CentreCm,
    TConstArrayView<FCandidate> Elite, int32 Previous)
{
    if (Elite.IsEmpty()) { return INDEX_NONE; }
    const int32 Opposite = Quadrant(HighCm, CentreCm) ^ 3;
    TArray<int32, TInlineAllocator<8>> Legal;
    for (int32 Index = 0; Index < Elite.Num(); ++Index)
    {
        if (Index != Previous && Quadrant(Elite[Index].PositionCm, CentreCm) == Opposite) { Legal.Add(Index); }
    }
    if (Legal.IsEmpty())
    {
        for (int32 Index = 0; Index < Elite.Num(); ++Index)
        {
            if (Index != Previous || Elite.Num() == 1) { Legal.Add(Index); }
        }
    }
    return Legal[int32(MarkerRoll(Seed, Segment, ELDFieldMarkerKind::Elite) % uint64(Legal.Num()))];
}

const TCHAR* LDFieldMarkers::KindName(ELDFieldMarkerKind Kind)
{
    switch (Kind)
    {
    case ELDFieldMarkerKind::High: return TEXT("high");
    case ELDFieldMarkerKind::Elite: return TEXT("elite");
    default: return TEXT("none");
    }
}

FName LDFieldMarkers::RewardEvent(ELDFieldMarkerKind Kind)
{
    switch (Kind)
    {
    case ELDFieldMarkerKind::High: return FName(TEXT("camp_marker_high"));
    case ELDFieldMarkerKind::Elite: return FName(TEXT("camp_marker_elite"));
    default: return NAME_None;
    }
}

bool FLDFieldMarkerSchedule::IsValid() const
{
    int64 Last = 0;
    if (StartedAtMs < 0 || SegmentMs <= 0 || RevealOffsetMs <= 0 || RevealOffsetMs >= SegmentMs || Segments == 0 || Seed == 0
        || CentreCm.ContainsNaN() || High.Num() < 2 || Elite.Num() < 2 || !EndAtMs(Segments, Last))
    {
        return false;
    }
    for (const TArray<LDFieldMarkers::FCandidate>* Group : {&High, &Elite})
    {
        for (const LDFieldMarkers::FCandidate& Camp : *Group)
        {
            if (Camp.CampId.IsNone() || Camp.PositionCm.ContainsNaN()) { return false; }
        }
    }
    return true;
}

bool FLDFieldMarkerSchedule::RevealAtMs(uint8 Segment, int64& Out) const
{
    int64 Offset = 0, Start = 0;
    return Segment >= 1 && Segment <= Segments && MarkerMul(int64(Segment - 1), SegmentMs, Offset)
        && MarkerAdd(StartedAtMs, Offset, Start) && MarkerAdd(Start, RevealOffsetMs, Out);
}

bool FLDFieldMarkerSchedule::EndAtMs(uint8 Segment, int64& Out) const
{
    int64 Offset = 0;
    return Segment >= 1 && Segment <= Segments && MarkerMul(int64(Segment), SegmentMs, Offset) && MarkerAdd(StartedAtMs, Offset, Out);
}

bool FLDFieldMarkerState::Advance(int64 NowMs, TArray<FLDFieldMarker>& OutRevealed, TArray<FLDFieldMarker>& OutExpired)
{
    OutRevealed.Reset();
    OutExpired.Reset();
    if (!Schedule.IsValid()) { return false; }
    bool bChanged = false;
    int64 RevealAt = 0, EndAt = 0;
    while (Segment < Schedule.Segments && Schedule.RevealAtMs(uint8(Segment + 1), RevealAt) && RevealAt <= NowMs)
    {
        // The previous segment's markers end before the next one is drawn against them.
        EndSegment(OutExpired);
        ++Segment;
        const int32 HighIndex = LDFieldMarkers::PickHigh(Schedule.Seed, Segment, High.Candidate, Schedule.High.Num());
        const int32 EliteIndex = LDFieldMarkers::PickElite(Schedule.Seed, Segment, Schedule.High[HighIndex].PositionCm,
            Schedule.CentreCm, Schedule.Elite, Elite.Candidate);
        High = {ELDFieldMarkerKind::High, HighIndex, ELDFieldMarkerPhase::Revealed};
        Elite = {ELDFieldMarkerKind::Elite, EliteIndex, ELDFieldMarkerPhase::Revealed};
        OutRevealed.Add(High);
        OutRevealed.Add(Elite);
        bChanged = true;
    }
    if (Segment > 0 && Schedule.EndAtMs(Segment, EndAt) && NowMs >= EndAt)
    {
        const int32 Before = OutExpired.Num();
        EndSegment(OutExpired);
        bChanged |= OutExpired.Num() != Before;
    }
    return bChanged || !OutExpired.IsEmpty();
}

int64 FLDFieldMarkerState::NextDueMs(int64 NowMs) const
{
    int64 Due = MAX_int64, At = 0;
    if (!Schedule.IsValid()) { return Due; }
    if (Segment < Schedule.Segments && Schedule.RevealAtMs(uint8(Segment + 1), At) && At > NowMs) { Due = At; }
    const bool bOpen = High.Phase == ELDFieldMarkerPhase::Revealed || Elite.Phase == ELDFieldMarkerPhase::Revealed;
    if (Segment > 0 && bOpen && Schedule.EndAtMs(Segment, At) && At > NowMs) { Due = FMath::Min(Due, At); }
    return Due;
}

const FLDFieldMarker* FLDFieldMarkerState::OpenMarkerFor(FName CampId, int64 NowMs) const
{
    int64 RevealAt = 0, EndAt = 0;
    if (CampId.IsNone() || Segment == 0 || !Schedule.RevealAtMs(Segment, RevealAt) || !Schedule.EndAtMs(Segment, EndAt)
        || NowMs < RevealAt || NowMs >= EndAt)
    {
        return nullptr;
    }
    for (const FLDFieldMarker* Marker : {&High, &Elite})
    {
        const LDFieldMarkers::FCandidate* Camp = CampOf(*Marker);
        if (Marker->Phase == ELDFieldMarkerPhase::Revealed && Camp && Camp->CampId == CampId) { return Marker; }
    }
    return nullptr;
}

bool FLDFieldMarkerState::Claim(ELDFieldMarkerKind Kind, const FGuid& Contender, int64 NowMs)
{
    if (Kind != ELDFieldMarkerKind::High && Kind != ELDFieldMarkerKind::Elite) { return false; }
    FLDFieldMarker& Marker = MarkerOf(*this, Kind);
    if (Marker.Phase != ELDFieldMarkerPhase::Revealed || !Contender.IsValid()) { return false; }
    Marker.Phase = ELDFieldMarkerPhase::Claimed;
    Marker.ClaimedBy = Contender;
    Marker.ClaimedAtMs = NowMs;
    return true;
}

void FLDFieldMarkerState::EndSegment(TArray<FLDFieldMarker>& OutExpired)
{
    for (FLDFieldMarker* Marker : {&High, &Elite})
    {
        if (Marker->Phase != ELDFieldMarkerPhase::Revealed) { continue; }
        Marker->Phase = ELDFieldMarkerPhase::Expired;
        OutExpired.Add(*Marker);
    }
}

const LDFieldMarkers::FCandidate* FLDFieldMarkerState::CampOf(const FLDFieldMarker& Marker) const
{
    const TArray<LDFieldMarkers::FCandidate>& Group = Marker.Kind == ELDFieldMarkerKind::High ? Schedule.High : Schedule.Elite;
    return Marker.Kind != ELDFieldMarkerKind::None && Group.IsValidIndex(Marker.Candidate) ? &Group[Marker.Candidate] : nullptr;
}

void FLDFieldMarkerState::View(TArray<FLDFieldMarkerView>& Out) const
{
    Out.Reset();
    int64 RevealAt = 0, EndAt = 0;
    if (!Schedule.IsValid() || Segment == 0 || !Schedule.RevealAtMs(Segment, RevealAt) || !Schedule.EndAtMs(Segment, EndAt)) { return; }
    for (const FLDFieldMarker* Marker : {&High, &Elite})
    {
        const LDFieldMarkers::FCandidate* Camp = CampOf(*Marker);
        if (!Camp || Marker->Phase == ELDFieldMarkerPhase::None) { continue; }
        FLDFieldMarkerView& Row = Out.AddDefaulted_GetRef();
        Row.Segment = Segment;
        Row.Kind = Marker->Kind;
        Row.CampId = Camp->CampId;
        Row.PositionCm = Camp->PositionCm;
        Row.Phase = Marker->Phase;
        Row.RevealAtMs = RevealAt;
        Row.EndAtMs = EndAt;
    }
}

bool LDFieldMarkers::Resolve(const FLDDemoContentSnapshot& Content, const FLDDemoWorldGeometry& Geometry, int64 StartedAtMs,
    uint64 AirdropSeed, FLDFieldMarkerState& Out, FString& Error)
{
    Out = {};
    const FLDDemoContentDefinitions& D = Content.Get();
    FLDCampRoster Roster;
    if (!LDCamp::BuildRoster(D, Geometry, Roster, Error)) { return false; }
    FLDFieldMarkerState Candidate;
    FLDFieldMarkerSchedule& S = Candidate.Schedule;
    for (const FLDCampSpec& Camp : Roster.Camps)
    {
        if (Camp.Tier == ELDDemoCampTier::High) { S.High.Add({Camp.CampId, Camp.AnchorCm}); }
        else if (Camp.Tier == ELDDemoCampTier::Elite) { S.Elite.Add({Camp.CampId, Camp.AnchorCm}); }
    }
    // Draws index a fixed, id-ordered candidate list, independent of the content row order.
    const auto ById = [](const FCandidate& A, const FCandidate& B) { return A.CampId.LexicalLess(B.CampId); };
    S.High.Sort(ById);
    S.Elite.Sort(ById);
    const FLDDemoWorldAnchor* Centre = Geometry.Anchors.FindByPredicate([&D](const FLDDemoWorldAnchor& Anchor)
        { return Anchor.Kind == ELDDemoWorldAnchorKind::Capture && Anchor.Id == D.World.CaptureId; });
    if (!Centre)
    {
        Error = TEXT("Camp markers need the compiled flagpole anchor as the map centre.");
        return false;
    }
    if (!Centre->PositionCm.ToVector(S.CentreCm, Error)) { return false; }
    S.StartedAtMs = StartedAtMs;
    S.SegmentMs = D.Parameters.FieldSegmentMs;
    S.RevealOffsetMs = D.Parameters.CampMarkRevealMs;
    S.Segments = FLDTimelineParameters::TimedSegments;
    S.Seed = LDAirdrop::Mix(AirdropSeed ^ 0x43414D50'4D41524Bull);
    S.Seed = S.Seed ? S.Seed : 1;
    if (!S.IsValid())
    {
        Error = TEXT("The approved camp marker parameters or the High/Elite camp placements (two of each) are invalid.");
        return false;
    }
    Out = MoveTemp(Candidate);
    Error.Reset();
    return true;
}
