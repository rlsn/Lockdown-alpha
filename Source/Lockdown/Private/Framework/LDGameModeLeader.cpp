#include "Framework/LDGameMode.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDevelopmentDirector.h"

// D27 (#79) 通报批评: the pure FLDLeaderState names the leader at each refresh boundary; this file only gathers the
// committed levels, kill counts and current feet of the contenders still in the match.

int64 ALDGameMode::NextLeaderDueMs() const
{
    return LeaderNotice.IsActive() ? LeaderNotice.NextDueMs(AuthorityNowMs) : MAX_int64;
}

int64 ALDGameMode::LeaderBountyFor(const FGuid& Victim) const
{
    return LeaderNotice.IsActive() ? LeaderNotice.BountyFor(Victim) : 0;
}

bool ALDGameMode::AdvanceLeaderNotice(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None || !LeaderNotice.IsActive()
        || !Combat || !Development)
    {
        Error = TEXT("The leader notice requires the current RespawnsAndWorld permit, active rules, Combat and growth.");
        return false;
    }
    const int64 NowMs = Permit.GetBoundary().AtMs;
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
    if (LeaderNotice.Leader != Before || LeaderNotice.Leader.IsValid())
    {
        UE_LOG(LogTemp, Display, TEXT("LD_LEADER_REFRESH index=%llu leader=%s streak=%u bounty=%lld at_ms=%lld"),
            LeaderNotice.RefreshIndex, *LeaderNotice.Leader.ToString(), LeaderNotice.Streak,
            LeaderNotice.BountyFor(LeaderNotice.Leader), NowMs);
    }
    return true;
}
