#include "Framework/LDGameMode.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDevelopmentDirector.h"

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
    if (LeaderNotice.Leader != Before || LeaderNotice.Leader.IsValid())
    {
        UE_LOG(LogLDWorld, Display, TEXT("LD_LEADER_REFRESH index=%llu leader=%s field_t_ms=%lld"),
            LeaderNotice.RefreshIndex, *LeaderNotice.Leader.ToString(), NowMs);
    }
    return true;
}
