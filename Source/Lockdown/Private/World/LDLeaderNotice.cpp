#include "World/LDLeaderNotice.h"
#include "Development/LDDemoContentCatalog.h"
#include "Math/RandomStream.h"

FGuid LDLeaderNotice::PickLeader(uint8 MinLevel, TConstArrayView<FCandidate> Candidates)
{
    const FCandidate* Best = nullptr;
    bool bTied = false;
    for (const FCandidate& Candidate : Candidates)
    {
        if (!Candidate.Contender.IsValid() || Candidate.Level < MinLevel) { continue; }
        if (!Best || Candidate.Level > Best->Level || (Candidate.Level == Best->Level && Candidate.Kills > Best->Kills))
        {
            Best = &Candidate;
            bTied = false;
        }
        else if (Candidate.Level == Best->Level && Candidate.Kills == Best->Kills)
        {
            bTied = true;
        }
    }
    return Best && !bTied ? Best->Contender : FGuid();
}

bool FLDLeaderRules::IsValid() const
{
    return StartedAtMs >= 0 && RefreshMs > 0 && MinLevel >= 1 && FMath::IsFinite(CircleRadiusCm) && CircleRadiusCm > 0
        && FMath::IsFinite(CircleMaxOffsetCm) && CircleMaxOffsetCm > 0 && CircleMaxOffsetCm <= CircleRadiusCm
        && BountyGoldPerRefresh > 0 && BountyGoldMax >= BountyGoldPerRefresh;
}

bool FLDLeaderState::IsRefreshDue(int64 NowMs) const
{
    if (!IsActive() || NowMs < Rules.StartedAtMs) { return false; }
    return uint64((NowMs - Rules.StartedAtMs) / Rules.RefreshMs) > RefreshIndex;
}

bool FLDLeaderState::Refresh(int64 NowMs, TConstArrayView<LDLeaderNotice::FCandidate> Candidates)
{
    if (!IsRefreshDue(NowMs)) { return false; }
    RefreshIndex = uint64((NowMs - Rules.StartedAtMs) / Rules.RefreshMs);
    const FLDLeaderView Before = View();
    const FGuid Picked = LDLeaderNotice::PickLeader(Rules.MinLevel, Candidates);
    const LDLeaderNotice::FCandidate* Body = Picked.IsValid()
        ? Candidates.FindByPredicate([&Picked](const LDLeaderNotice::FCandidate& C) { return C.Contender == Picked; }) : nullptr;
    if (!Body || !Body->bAlive || Body->FootCm.ContainsNaN())
    {
        Leader.Invalidate();
        Streak = 0;
        CircleCentreCm = FVector::ZeroVector;
        RefreshedAtMs = 0;
        return Before.Leader.IsValid();
    }
    Streak = Leader == Picked && Streak < MAX_uint32 ? Streak + 1 : 1;
    Leader = Picked;
    // Uniform in the offset disk; the stream depends only on the match and the refresh index.
    FRandomStream Stream(int32(Rules.Seed ^ uint32(RefreshIndex * 2654435761ull)));
    const double Distance = Rules.CircleMaxOffsetCm * FMath::Sqrt(double(Stream.GetFraction()));
    const double Angle = UE_DOUBLE_TWO_PI * double(Stream.GetFraction());
    CircleCentreCm = Body->FootCm + FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.);
    RefreshedAtMs = NowMs;
    return true;
}

bool FLDLeaderState::LeaderDied(const FGuid& Contender, int64 NowMs)
{
    if (!Leader.IsValid() || Contender != Leader) { return false; }
    LastDownLeader = Leader;
    LastDownAtMs = NowMs;
    LastDownBountyGold = BountyFor(Leader);
    Leader.Invalidate();
    Streak = 0;
    CircleCentreCm = FVector::ZeroVector;
    RefreshedAtMs = 0;
    return true;
}

int64 FLDLeaderState::BountyFor(const FGuid& Victim) const
{
    if (!Leader.IsValid() || Victim != Leader || !Streak) { return 0; }
    return FMath::Min<int64>(Rules.BountyGoldMax, Rules.BountyGoldPerRefresh * int64(FMath::Min<uint32>(Streak, 1u << 20)));
}

int64 FLDLeaderState::NextDueMs(int64 NowMs) const
{
    if (!IsActive()) { return MAX_int64; }
    const uint64 Next = NowMs < Rules.StartedAtMs ? 1 : uint64((NowMs - Rules.StartedAtMs) / Rules.RefreshMs) + 1;
    if (Next > uint64(MAX_int64 - Rules.StartedAtMs) / uint64(Rules.RefreshMs)) { return MAX_int64; }
    return Rules.StartedAtMs + int64(Next) * Rules.RefreshMs;
}

FLDLeaderView FLDLeaderState::View() const
{
    FLDLeaderView Out;
    if (!Leader.IsValid()) { return Out; }
    Out.Leader = Leader;
    Out.CircleCentreCm = CircleCentreCm;
    Out.RefreshedAtMs = RefreshedAtMs;
    Out.BountyGold = BountyFor(Leader);
    return Out;
}

bool LDLeaderNotice::Resolve(const FLDDemoContentSnapshot& Content, int64 StartedAtMs, const FGuid& LedgerMatch,
    FLDLeaderState& Out, FString& Error)
{
    Out = {};
    const auto& P = Content.Get().Parameters;
    auto Cm = [](const FLDDemoRational& Value) { return Value.Denominator > 0 ? double(Value.Numerator) / double(Value.Denominator) : 0.; };
    FLDLeaderRules& R = Out.Rules;
    R.StartedAtMs = StartedAtMs;
    R.RefreshMs = P.LeaderRefreshMs;
    R.MinLevel = P.LeaderMinLevel >= 1 && P.LeaderMinLevel <= MAX_uint8 ? uint8(P.LeaderMinLevel) : 0;
    R.CircleRadiusCm = Cm(P.LeaderCircleRadiusCm);
    R.CircleMaxOffsetCm = Cm(P.LeaderCircleMaxOffsetCm);
    R.BountyGoldPerRefresh = P.LeaderBountyGoldPerRefresh;
    R.BountyGoldMax = P.LeaderBountyGoldMax;
    R.Seed = LedgerMatch.A ^ LedgerMatch.B ^ LedgerMatch.C ^ LedgerMatch.D;
    if (!LedgerMatch.IsValid() || !R.IsValid())
    {
        Out = {};
        Error = TEXT("The approved LEADER parameters or the match identity are invalid.");
        return false;
    }
    Error.Reset();
    return true;
}
