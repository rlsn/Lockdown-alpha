#include "Framework/LDGameMode.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDevelopmentDirector.h"
#include "Development/LDDemoContentCatalog.h"

// D27 (#79) 上热搜: the pure FLDLeaderState names the leader at each refresh boundary; this file only gathers the
// committed levels, kill counts and current feet of the contenders still in the match.

int64 ALDGameMode::NextLeaderDueMs() const
{
    // #198: refreshes count field progress T; nothing is due while a rest pauses the field.
    return LeaderNotice.IsActive() && IsFieldRunning() ? FieldDueAuthorityMs(LeaderNotice.NextDueMs(GetFieldNowMs())) : MAX_int64;
}

bool ALDGameMode::AdvanceLeaderNotice(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None || !LeaderNotice.IsActive()
        || !Combat || !Development)
    {
        Error = TEXT("The leader notice requires the current RespawnsAndWorld permit, active rules, Combat and growth.");
        return false;
    }
    if (!IsFieldRunning()) { return true; }
    const int64 NowMs = Timeline.FieldMsAt(Permit.GetBoundary().AtMs);
    if (!LeaderNotice.IsRefreshDue(NowMs)) { return true; }
    TArray<LDAirdrop::FCandidate> Feet;
    Combat->ReadLivingContenderFeet(Feet);
    TArray<LDLeaderNotice::FCandidate> Candidates;
    for (const auto& Pair : Contenders)
    {
        const FLDContenderRecord& Record = Pair.Value;
        const FGuid Id = Record.Session;
        if (Record.bLeft || !Id.IsValid()) { continue; }
        const FLDGrowthBuild* Build = Development->FindBuild(Id);
        if (!Build) { continue; }
        LDLeaderNotice::FCandidate& Candidate = Candidates.AddDefaulted_GetRef();
        Candidate.Contender = Id;
        Candidate.Level = Build->Level;
        Candidate.Kills = Record.Kills;
        if (const LDAirdrop::FCandidate* Foot = Feet.FindByPredicate([&Id](const LDAirdrop::FCandidate& F) { return F.Contender == Id; }))
        {
            Candidate.bAlive = true;
            Candidate.FootCm = Foot->FootCm;
        }
    }
    const FGuid Before = LeaderNotice.Leader;
    if (LeaderNotice.Refresh(NowMs, Candidates)) { bPublicMatchDirty = true; }
    // Every trending contender's unbroken run (the leader, and the runner-up on a melon day) earns one stat point every
    // LEADER.SurvivalPointSec; leaving the trending set or dying ends the run. Points are granted in Transactions.
    for (auto It = TrendingRuns.CreateIterator(); It; ++It)
    {
        if (!LeaderNotice.IsTrending(It.Key())) { It.RemoveCurrent(); }
    }
    const int64 EveryMs = DemoContent ? DemoContent->Get().Parameters.LeaderSurvivalPointMs : 0;
    for (const FGuid& Id : {LeaderNotice.Leader, LeaderNotice.RunnerUp})
    {
        if (!Id.IsValid()) { continue; }
        FTrendingRun* Run = TrendingRuns.Find(Id);
        if (!Run) { TrendingRuns.Add(Id, {NowMs, 0}); continue; }
        while (EveryMs > 0 && Run->Counted < (NowMs - Run->SinceMs) / EveryMs)
        {
            ++Run->Counted;
            const uint32 Words[7] = {Id.A, Id.B, Id.C, Id.D, uint32(Run->SinceMs), uint32(Run->Counted), 0x54524E44u};
            const uint64 Hash = CityHash64(reinterpret_cast<const char*>(Words), sizeof(Words));
            TrendingPointsOwed.Add({Id, FGuid(uint32(Hash >> 32), uint32(Hash), MatchContext.Generation, 0x54524E44u)});
        }
    }
    if (LeaderNotice.Leader != Before || LeaderNotice.Leader.IsValid())
    {
        UE_LOG(LogLDWorld, Display, TEXT("LD_LEADER_REFRESH index=%llu leader=%s field_t_ms=%lld"),
            LeaderNotice.RefreshIndex, *LeaderNotice.Leader.ToString(), NowMs);
    }
    return true;
}

bool ALDGameMode::PayTrendingPoints(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None || !Development)
    {
        Error = TEXT("Trending survival points require the current Transactions permit and growth.");
        return false;
    }
    for (int32 Index = 0; Index < TrendingPointsOwed.Num();)
    {
        const FTrendingPoint Point = TrendingPointsOwed[Index];
        const ELDGrowthReason Reason = Development->GrantPKPoints(Point.Contender, 1, Point.AwardId, false, Permit);
        UE_LOG(LogLDWorld, Display, TEXT("LD_LEADER_SURVIVAL_POINT contender=%s award=%s reason=%d"), *LDLog::Id(Point.Contender),
            *Point.AwardId.ToString(), int32(Reason));
        // Granted (or already granted) points are done; a contender who left never gets one; anything else waits for the
        // next Transactions phase.
        if (Reason == ELDGrowthReason::None || Reason == ELDGrowthReason::PKAwardDuplicate || Reason == ELDGrowthReason::LeftMatch
            || Reason == ELDGrowthReason::OldMatch || Reason == ELDGrowthReason::MatchClosed)
        { TrendingPointsOwed.RemoveAt(Index); continue; }
        ++Index;
    }
    return true;
}
