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

uint64 LDAirdrop::Mix(uint64 Value)
{
    Value += 0x9E3779B97F4A7C15ull;
    Value = (Value ^ (Value >> 30)) * 0xBF58476D1CE4E5B9ull;
    Value = (Value ^ (Value >> 27)) * 0x94D049BB133111EBull;
    return Value ^ (Value >> 31);
}

int32 LDAirdrop::NextPoint(uint64 Seed, uint64 Serial, int32 Previous, int32 Count)
{
    if (Count <= 0) { return INDEX_NONE; }
    const uint64 Roll = Mix(Seed ^ Mix(Serial));
    if (Count == 1 || Previous < 0 || Previous >= Count) { return int32(Roll % uint64(Count)); }
    return int32((uint64(Previous) + 1 + Roll % uint64(Count - 1)) % uint64(Count));
}

bool LDAirdrop::PickClaimer(const FVector& CentreCm, double RadiusCm, TConstArrayView<FCandidate> Candidates,
    uint64 TieSeed, FGuid& Out)
{
    Out.Invalidate();
    double Best = 0;
    uint64 BestDraw = 0;
    const double Limit = RadiusCm * RadiusCm;
    for (const FCandidate& Candidate : Candidates)
    {
        if (!Candidate.Contender.IsValid() || Candidate.bProtected || Candidate.FootCm.ContainsNaN()) { continue; }
        const double Distance = FVector::DistSquared2D(CentreCm, Candidate.FootCm);
        if (!FMath::IsFinite(Distance) || Distance > Limit) { continue; }
        const FGuid& Id = Candidate.Contender;
        const uint64 Draw = Mix(TieSeed ^ Mix((uint64(Id.A) << 32 | Id.B) ^ Mix(uint64(Id.C) << 32 | Id.D)));
        if (!Out.IsValid() || Distance < Best || (Distance == Best && Draw < BestDraw))
        {
            Out = Id;
            Best = Distance;
            BestDraw = Draw;
        }
    }
    return Out.IsValid();
}

bool FLDAirdropSchedule::IsValid() const
{
    if (StartedAtMs < 0 || FirstLandMs <= 0 || IntervalMs <= 0 || AnnounceLeadMs <= 0 || AnnounceLeadMs > FirstLandMs
        || AnnounceLeadMs >= IntervalMs || Points.IsEmpty() || Seed == 0
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
        if (Serial > Index) { Serial = 0; PointIndex = INDEX_NONE; }
        // Each point depends on the previous drop's, so every drop up to this one is drawn in order.
        for (uint64 Next = Serial + 1; Next <= Index + 1; ++Next)
        {
            PointIndex = LDAirdrop::NextPoint(Schedule.Seed, Next, PointIndex, Schedule.Points.Num());
        }
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
    return Schedule.IsValid() && Serial > 0 && Schedule.Points.IsValidIndex(PointIndex) ? &Schedule.Points[PointIndex] : nullptr;
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
    // A fresh server-only seed: nothing public (match identity, anchors, clock) predicts the next drop's point.
    const FGuid Random = FGuid::NewGuid();
    S.Seed = LDAirdrop::Mix((uint64(Random.A) << 32 | Random.B) ^ LDAirdrop::Mix(uint64(Random.C) << 32 | Random.D));
    S.Seed = S.Seed ? S.Seed : 1;
    if (P.AirdropRewardXP <= 0 || P.AirdropRewardGold <= 0 || !S.IsValid())
    {
        Error = TEXT("The approved airdrop parameters or compiled airdrop points are invalid.");
        return false;
    }
    Out = MoveTemp(Candidate);
    Error.Reset();
    return true;
}
