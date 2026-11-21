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
    // The leader's unbroken run pays one stat point every LEADER.SurvivalPointSec (a bigger target, a fatter prize).
    if (!LeaderNotice.Leader.IsValid() || LeaderNotice.Leader != TrendingRunLeader)
    {
        TrendingRunLeader = LeaderNotice.Leader;
        TrendingRunSinceMs = NowMs;
        TrendingRunPaid = 0;
    }
    else if (const int64 EveryMs = DemoContent ? DemoContent->Get().Parameters.LeaderSurvivalPointMs : 0; EveryMs > 0)
    {
        const int64 Due = (NowMs - TrendingRunSinceMs) / EveryMs;
        while (TrendingRunPaid < Due)
        {
            ++TrendingRunPaid;
            const uint32 Words[7] = {TrendingRunLeader.A, TrendingRunLeader.B, TrendingRunLeader.C, TrendingRunLeader.D,
                uint32(TrendingRunSinceMs), uint32(TrendingRunPaid), 0x54524E44u};
            const uint64 Hash = CityHash64(reinterpret_cast<const char*>(Words), sizeof(Words));
            const FGuid AwardId(uint32(Hash >> 32), uint32(Hash), MatchContext.Generation, 0x54524E44u);
            const ELDGrowthReason Reason = Development->GrantPKPoints(TrendingRunLeader, 1, AwardId, false, Permit);
            UE_LOG(LogLDWorld, Display, TEXT("LD_LEADER_SURVIVAL_POINT leader=%s run_ms=%lld paid=%d reason=%d"),
                *LDLog::Id(TrendingRunLeader), NowMs - TrendingRunSinceMs, TrendingRunPaid, int32(Reason));
        }
    }
    if (LeaderNotice.Leader != Before || LeaderNotice.Leader.IsValid())
    {
        UE_LOG(LogLDWorld, Display, TEXT("LD_LEADER_REFRESH index=%llu leader=%s field_t_ms=%lld"),
            LeaderNotice.RefreshIndex, *LeaderNotice.Leader.ToString(), NowMs);
    }
    return true;
}
