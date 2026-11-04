#include "World/LDAirdrop.h"
#include "Development/LDDemoContentCatalog.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapRegistry.h"

namespace
{
    bool AirdropMul(uint64 A, int64 B, int64& Out)
    {
        if (B < 0) { return false; }
        if (A == 0 || B == 0) { Out = 0; return true; }
        if (A > uint64(MAX_int64) / uint64(B)) { return false; }
        Out = int64(A) * B;
        return true;
    }

    bool AirdropAdd(int64 A, int64 B, int64& Out)
    {
        if ((B > 0 && A > MAX_int64 - B) || (B < 0 && A < MIN_int64 - B)) { return false; }
        Out = A + B;
        return true;
    }
}

bool LDAirdrop::PickClaimer(const FVector& CentreCm, double RadiusCm, TConstArrayView<FCandidate> Candidates, FGuid& Out)
{
    Out.Invalidate();
    double Best = 0;
    const double Limit = RadiusCm * RadiusCm;
    for (const FCandidate& Candidate : Candidates)
    {
        if (!Candidate.Contender.IsValid() || Candidate.FootCm.ContainsNaN()) { continue; }
        const double Distance = FVector::DistSquared2D(CentreCm, Candidate.FootCm);
        if (!FMath::IsFinite(Distance) || Distance > Limit) { continue; }
        if (!Out.IsValid() || Distance < Best || (Distance == Best && Candidate.Contender < Out))
        {
            Out = Candidate.Contender;
            Best = Distance;
        }
    }
    return Out.IsValid();
}

bool FLDAirdropSchedule::IsValid() const
{
    if (StartedAtMs < 0 || FirstLandMs <= 0 || IntervalMs <= 0 || AnnounceLeadMs <= 0 || AnnounceLeadMs > FirstLandMs
        || AnnounceLeadMs >= IntervalMs || Points.IsEmpty() || PointOffset < 0 || PointOffset >= Points.Num()
        || !FMath::IsFinite(PickupRadiusCm) || PickupRadiusCm <= 0)
    {
        return false;
    }
    for (const LDAirdrop::FPoint& Point : Points)
    {
        if (Point.Id.IsNone() || Point.PositionCm.ContainsNaN()) { return false; }
    }
    return true;
}

bool FLDAirdropSchedule::LandAtMs(uint64 Index, int64& Out) const
{
    int64 Offset = 0, First = 0;
    return AirdropMul(Index, IntervalMs, Offset) && AirdropAdd(StartedAtMs, FirstLandMs, First) && AirdropAdd(First, Offset, Out);
}

bool FLDAirdropSchedule::AnnounceAtMs(uint64 Index, int64& Out) const
{
    int64 Land = 0;
    return LandAtMs(Index, Land) && AirdropAdd(Land, -AnnounceLeadMs, Out);
}

const LDAirdrop::FPoint& FLDAirdropSchedule::PointOf(uint64 Index) const
{
    return Points[int32((uint64(PointOffset) + Index) % uint64(Points.Num()))];
}

bool FLDAirdropState::Advance(int64 NowMs)
{
    int64 FirstAnnounce = 0;
    if (!Schedule.IsValid() || !Schedule.AnnounceAtMs(0, FirstAnnounce) || NowMs < FirstAnnounce) { return false; }
    // Lead < Interval: the latest announcement at or before NowMs names the current drop.
    const uint64 Index = uint64(NowMs - FirstAnnounce) / uint64(Schedule.IntervalMs);
    int64 LandAt = 0;
    if (!Schedule.LandAtMs(Index, LandAt)) { return false; }
    const ELDAirdropPhase Timed = NowMs >= LandAt ? ELDAirdropPhase::Landed : ELDAirdropPhase::Announced;
    if (Serial != Index + 1)
    {
        Serial = Index + 1;
        Phase = Timed;
        ClaimedBy.Invalidate();
        ClaimedAtMs = 0;
        return true;
    }
    if (Phase == ELDAirdropPhase::Announced && Timed == ELDAirdropPhase::Landed)
    {
        Phase = ELDAirdropPhase::Landed;
        return true;
    }
    return false;
}

int64 FLDAirdropState::NextDueMs(int64 NowMs) const
{
    int64 Due = MAX_int64, FirstAnnounce = 0;
    if (!Schedule.IsValid() || !Schedule.AnnounceAtMs(0, FirstAnnounce)) { return Due; }
    const uint64 Next = NowMs < FirstAnnounce ? 0 : uint64(NowMs - FirstAnnounce) / uint64(Schedule.IntervalMs) + 1;
    int64 At = 0;
    if (Schedule.AnnounceAtMs(Next, At) && At > NowMs) { Due = At; }
    if (Serial > 0 && Phase == ELDAirdropPhase::Announced && Schedule.LandAtMs(Serial - 1, At) && At > NowMs)
    {
        Due = FMath::Min(Due, At);
    }
    return Due;
}

bool FLDAirdropState::Claim(const FGuid& Contender, int64 NowMs)
{
    if (Phase != ELDAirdropPhase::Landed || !Contender.IsValid()) { return false; }
    Phase = ELDAirdropPhase::Claimed;
    ClaimedBy = Contender;
    ClaimedAtMs = NowMs;
    return true;
}

const LDAirdrop::FPoint* FLDAirdropState::CurrentPoint() const
{
    return Schedule.IsValid() && Serial > 0 ? &Schedule.PointOf(Serial - 1) : nullptr;
}

FLDAirdropView FLDAirdropState::View() const
{
    FLDAirdropView Out;
    const LDAirdrop::FPoint* Point = CurrentPoint();
    if (!Point || Phase == ELDAirdropPhase::None) { return Out; }
    Out.Serial = Serial;
    Out.Phase = Phase;
    Out.PointId = Point->Id;
    Out.PositionCm = Point->PositionCm;
    Schedule.LandAtMs(Serial - 1, Out.LandAtMs);
    Out.ClaimedBy = ClaimedBy;
    Out.ClaimedAtMs = ClaimedAtMs;
    return Out;
}

bool LDAirdrop::Resolve(const ULDMapRegistry& Registry, const FLDDemoContentSnapshot& Content, int64 StartedAtMs,
    const FGuid& LedgerMatch, FLDAirdropState& Out, FString& Error)
{
    Out = {};
    const auto* Geometry = Registry.GetGeometry();
    if (!Registry.Enabled() || !Geometry || !LedgerMatch.IsValid())
    {
        Error = TEXT("The airdrop has no registered Demo world or match identity.");
        return false;
    }
    const auto& P = Content.Get().Parameters;
    FLDAirdropState Candidate;
    FLDAirdropSchedule& S = Candidate.Schedule;
    S.StartedAtMs = StartedAtMs;
    S.FirstLandMs = P.AirdropFirstLandMs;
    S.IntervalMs = P.AirdropIntervalMs;
    S.AnnounceLeadMs = P.AirdropAnnounceLeadMs;
    S.PickupRadiusCm = P.AirdropPickupRadiusCm.Denominator > 0
        ? double(P.AirdropPickupRadiusCm.Numerator) / double(P.AirdropPickupRadiusCm.Denominator) : 0.;
    for (const FLDDemoWorldAnchor& Anchor : Geometry->Anchors)
    {
        if (Anchor.Kind != ELDDemoWorldAnchorKind::Airdrop) { continue; }
        const FName Id(*Anchor.Id);
        const FLDMapBinding* Binding = Registry.Resolve(Id, Registry.Generation);
        if (!Binding || !Binding->Floor.IsValid())
        {
            Error = FString::Printf(TEXT("Airdrop point '%s' is not bound to registered ground."), *Anchor.Id);
            return false;
        }
        S.Points.Add({Id, Binding->Position});
    }
    // Compiled anchor order is canonical; the per-match offset only chooses where the rotation starts.
    if (!S.Points.IsEmpty())
    {
        S.PointOffset = int32(uint32(LedgerMatch.A ^ LedgerMatch.B ^ LedgerMatch.C ^ LedgerMatch.D) % uint32(S.Points.Num()));
    }
    if (P.AirdropRewardXP <= 0 || P.AirdropRewardGold <= 0 || !S.IsValid())
    {
        Error = TEXT("The approved airdrop parameters or compiled airdrop points are invalid.");
        return false;
    }
    Out = MoveTemp(Candidate);
    Error.Reset();
    return true;
}
