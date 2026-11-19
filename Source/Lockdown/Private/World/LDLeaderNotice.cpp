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

void LDLeaderNotice::PickLeaders(uint8 MinLevel, int32 Count, TConstArrayView<FCandidate> Candidates, TArray<FGuid>& Out)
{
    Out.Reset();
    TArray<const FCandidate*> Eligible;
    for (const FCandidate& Candidate : Candidates)
    {
        if (Candidate.Contender.IsValid() && Candidate.Level >= MinLevel) { Eligible.Add(&Candidate); }
    }
    Eligible.Sort([](const FCandidate& A, const FCandidate& B) { return A.Level != B.Level ? A.Level > B.Level : A.Kills > B.Kills; });
    for (int32 Index = 0; Index < Eligible.Num() && Out.Num() < Count;)
    {
        int32 End = Index + 1;
        while (End < Eligible.Num() && Eligible[End]->Level == Eligible[Index]->Level && Eligible[End]->Kills == Eligible[Index]->Kills) { ++End; }
        if (Out.Num() + (End - Index) > Count) { break; }
        for (int32 Tied = Index; Tied < End; ++Tied) { Out.Add(Eligible[Tied]->Contender); }
        Index = End;
    }
}

bool FLDLeaderRules::IsValid() const
{
    return StartedAtMs >= 0 && RefreshMs > 0 && MinLevel >= 1 && Leaders >= 1 && Leaders <= 2 && FMath::IsFinite(CircleRadiusCm) && CircleRadiusCm > 0
        && FMath::IsFinite(CircleMaxOffsetCm) && CircleMaxOffsetCm > 0 && CircleMaxOffsetCm <= CircleRadiusCm;
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
    TArray<FGuid> Picked;
    LDLeaderNotice::PickLeaders(Rules.MinLevel, Rules.Leaders, Candidates, Picked);
    // Uniform in the offset disk; the stream depends only on the match and the refresh index.
    FRandomStream Stream(int32(Rules.Seed ^ uint32(RefreshIndex * 2654435761ull)));
    const auto Place = [&](const FGuid& Id, FGuid& OutId, FVector& OutCentre)
    {
        const LDLeaderNotice::FCandidate* Body = Id.IsValid()
            ? Candidates.FindByPredicate([&Id](const LDLeaderNotice::FCandidate& C) { return C.Contender == Id; }) : nullptr;
        const double Distance = Rules.CircleMaxOffsetCm * FMath::Sqrt(double(Stream.GetFraction()));
        const double Angle = UE_DOUBLE_TWO_PI * double(Stream.GetFraction());
        // A trending contender who is dead at the refresh is not shown.
        if (!Body || !Body->bAlive || Body->FootCm.ContainsNaN())
        {
            OutId.Invalidate();
            OutCentre = FVector::ZeroVector;
            return;
        }
        OutId = Id;
        OutCentre = Body->FootCm + FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.);
    };
    Place(Picked.IsValidIndex(0) ? Picked[0] : FGuid(), Leader, CircleCentreCm);
    const LDLeaderNotice::FCandidate* Named = Leader.IsValid()
        ? Candidates.FindByPredicate([this](const LDLeaderNotice::FCandidate& C) { return C.Contender == Leader; }) : nullptr;
    LeaderLevel = Named ? Named->Level : 0;
    Place(Picked.IsValidIndex(1) ? Picked[1] : FGuid(), RunnerUp, RunnerUpCentreCm);
    RefreshedAtMs = Leader.IsValid() || RunnerUp.IsValid() ? NowMs : 0;
    const FLDLeaderView After = View();
    return After.Leader != Before.Leader || After.LeaderLevel != Before.LeaderLevel || After.RunnerUp != Before.RunnerUp || After.RefreshedAtMs != Before.RefreshedAtMs;
}

bool FLDLeaderState::LeaderDied(const FGuid& Contender, int64 NowMs)
{
    if (!IsTrending(Contender)) { return false; }
    LastDownLeader = Contender;
    LastDownAtMs = NowMs;
    if (Contender == RunnerUp)
    {
        RunnerUp.Invalidate();
        RunnerUpCentreCm = FVector::ZeroVector;
    }
    else
    {
        Leader.Invalidate();
        LeaderLevel = 0;
        CircleCentreCm = FVector::ZeroVector;
    }
    if (!Leader.IsValid() && !RunnerUp.IsValid()) { RefreshedAtMs = 0; }
    return true;
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
    if (!Leader.IsValid() && !RunnerUp.IsValid()) { return Out; }
    Out.Leader = Leader;
    Out.LeaderLevel = LeaderLevel;
    Out.CircleCentreCm = CircleCentreCm;
    Out.RefreshedAtMs = RefreshedAtMs;
    Out.RunnerUp = RunnerUp;
    Out.RunnerUpCentreCm = RunnerUpCentreCm;
    return Out;
}

bool LDLeaderNotice::Resolve(const FLDDemoContentSnapshot& Content, int64 StartedAtMs, const FGuid& LedgerMatch,
    bool bRumorDay, FLDLeaderState& Out, FString& Error)
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
    R.Seed = LedgerMatch.A ^ LedgerMatch.B ^ LedgerMatch.C ^ LedgerMatch.D;
    R.Leaders = bRumorDay ? P.Daily.RumorLeaders : 1;
    if (!LedgerMatch.IsValid() || !R.IsValid())
    {
        Out = {};
        Error = TEXT("The approved LEADER parameters or the match identity are invalid.");
        return false;
    }
    Error.Reset();
    return true;
}
