#include "World/LDAirdrop.h"
#include "Development/LDDemoContentCatalog.h"
#include "Evaluation/LDEvaluationSeeds.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapRegistry.h"

namespace
{
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
    if (StartedAtMs < 0 || LandMs.IsEmpty() || AnnounceLeadMs <= 0 || ExpireMs <= 0 || LandMs[0] < AnnounceLeadMs
        || Points.IsEmpty() || Seed == 0 || !FMath::IsFinite(PickupRadiusCm) || PickupRadiusCm <= 0)
    {
        return false;
    }
    // One drop at a time: each leaves (expiry) at or before the next one's announcement.
    for (int32 Index = 0; Index < LandMs.Num(); ++Index)
    {
        int64 Gone = 0, Next = 0;
        if (LandMs[Index] <= 0 || !AirdropAdd(LandMs[Index], ExpireMs, Gone)
            || (Index + 1 < LandMs.Num() && (!AirdropAdd(LandMs[Index + 1], -AnnounceLeadMs, Next) || Gone > Next)))
        {
            return false;
        }
    }
    for (const LDAirdrop::FPoint& Point : Points)
    {
        if (Point.Id.IsNone() || Point.PositionCm.ContainsNaN()) { return false; }
    }
    return true;
}

bool FLDAirdropSchedule::LandAtMs(uint64 Index, int64& Out) const
{
    return Index < uint64(LandMs.Num()) && AirdropAdd(StartedAtMs, LandMs[int32(Index)], Out);
}

bool FLDAirdropSchedule::AnnounceAtMs(uint64 Index, int64& Out) const
{
    int64 Land = 0;
    return LandAtMs(Index, Land) && AirdropAdd(Land, -AnnounceLeadMs, Out);
}

bool FLDAirdropSchedule::ExpireAtMs(uint64 Index, int64& Out) const
{
    int64 Land = 0;
    return LandAtMs(Index, Land) && AirdropAdd(Land, ExpireMs, Out);
}

bool FLDAirdropState::Advance(int64 NowMs)
{
    if (!Schedule.IsValid()) { return false; }
    // The latest announcement at or before NowMs names the current drop.
    uint64 Count = 0;
    for (int64 At = 0; Count < uint64(Schedule.NumDrops()) && Schedule.AnnounceAtMs(Count, At) && At <= NowMs; ++Count) {}
    if (Count == 0) { return false; }
    const uint64 Index = Count - 1;
    int64 LandAt = 0, ExpireAt = 0;
    if (!Schedule.LandAtMs(Index, LandAt) || !Schedule.ExpireAtMs(Index, ExpireAt)) { return false; }
    const ELDAirdropPhase Timed = NowMs >= ExpireAt ? ELDAirdropPhase::Expired
        : NowMs >= LandAt ? ELDAirdropPhase::Landed : ELDAirdropPhase::Announced;
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
    // A claimed or expired drop is gone until the next announcement; otherwise the clock moves it forward only.
    if (!HoldsPoint() || Phase == Timed || (Phase == ELDAirdropPhase::Landed && Timed == ELDAirdropPhase::Announced))
    {
        return false;
    }
    Phase = Timed;
    return true;
}

int64 FLDAirdropState::NextDueMs(int64 NowMs) const
{
    int64 Due = MAX_int64, At = 0;
    if (!Schedule.IsValid()) { return Due; }
    for (uint64 Index = 0; Index < uint64(Schedule.NumDrops()); ++Index)
    {
        if (Schedule.AnnounceAtMs(Index, At) && At > NowMs)
        {
            Due = At;
            break;
        }
    }
    if (Serial > 0 && Phase == ELDAirdropPhase::Announced && Schedule.LandAtMs(Serial - 1, At) && At > NowMs)
    {
        Due = FMath::Min(Due, At);
    }
    if (Serial > 0 && HoldsPoint() && Schedule.ExpireAtMs(Serial - 1, At) && At > NowMs)
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
    Schedule.ExpireAtMs(Serial - 1, Out.ExpireAtMs);
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
    S.LandMs = P.AirdropLandMs;
    S.AnnounceLeadMs = P.AirdropAnnounceLeadMs;
    S.ExpireMs = P.AirdropExpireMs;
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
#if WITH_DEV_AUTOMATION_TESTS
    // Build evaluation (-LDBuildEvaluation only): an armed Case replays its drop points.
    if (const FLDEvaluationSeeds* Seeds = LDEvaluationSeeds::Armed()) { S.Seed = Seeds->AirdropSeed; }
#endif
    if (P.AirdropRewardXP <= 0 || !S.IsValid())
    {
        Error = TEXT("The approved airdrop parameters or compiled airdrop points are invalid.");
        return false;
    }
    Out = MoveTemp(Candidate);
    Error.Reset();
    return true;
}
