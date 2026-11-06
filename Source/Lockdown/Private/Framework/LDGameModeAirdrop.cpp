#include "Framework/LDGameMode.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDGrowthProtocol.h"
#include "Economy/LDEconomyDirector.h"

// D26 (#79): the airdrop. The schedule and claim selection are the pure FLDAirdropState / LDAirdrop rules; this file
// only drives them: the schedule at the RespawnsAndWorld boundary, then the claim in the same step's Transactions phase
// (bodies do not move between the two), paid through the single growth reward composite.

int64 ALDGameMode::NextAirdropDueMs() const
{
    return Airdrop.IsActive() ? Airdrop.NextDueMs(AuthorityNowMs) : MAX_int64;
}

bool ALDGameMode::AdvanceAirdrop(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None || !Airdrop.IsActive())
    {
        Error = TEXT("The airdrop schedule requires the current RespawnsAndWorld permit and an active schedule.");
        return false;
    }
    const int64 NowMs = Permit.GetBoundary().AtMs;
    if (Airdrop.Advance(NowMs))
    {
        bPublicMatchDirty = true;
        const LDAirdrop::FPoint* Point = Airdrop.CurrentPoint();
        UE_LOG(LogLDWorld, Display, TEXT("LD_AIRDROP_%s serial=%llu point=%s at_ms=%lld"),
            Airdrop.Phase == ELDAirdropPhase::Landed ? TEXT("LAND") : TEXT("ANNOUNCE"), Airdrop.Serial,
            Point ? *Point->Id.ToString() : TEXT("none"), NowMs);
    }
    return true;
}

bool ALDGameMode::ClaimAirdrop(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None || !Airdrop.IsActive()
        || !Combat || !Economy || !DemoContent)
    {
        Error = TEXT("The airdrop claim requires the current Transactions permit, an active schedule, Combat and the growth domain.");
        return false;
    }
    const int64 NowMs = Permit.GetBoundary().AtMs;
    if (Airdrop.Phase != ELDAirdropPhase::Landed) { return true; }
    const LDAirdrop::FPoint* Point = Airdrop.CurrentPoint();
    TArray<LDAirdrop::FCandidate> Candidates;
    Combat->ReadLivingContenderFeet(Candidates);
    FGuid Claimer;
    if (!Point || !LDAirdrop::PickClaimer(Point->PositionCm, Airdrop.Schedule.PickupRadiusCm, Candidates, Airdrop.TieSeed(),
        Claimer)) { return true; }
    const auto& Parameters = DemoContent->Get().Parameters;
    FLDRewardReceipt Receipt;
    ELDGrowthReason Reason = ELDGrowthReason::None;
    const ELDRewardOutcome Outcome = Economy->CommitEventReward(Claimer, TEXT("airdrop"), Airdrop.Serial,
        Parameters.AirdropRewardXP, Parameters.AirdropRewardGold, Permit, Receipt, Reason);
    if (Outcome == ELDRewardOutcome::RetryableNotCommitted)
    {
        // Nothing was installed; the drop stays on the ground for the next boundary's first toucher.
        UE_LOG(LogLDWorld, Warning, TEXT("LD_AIRDROP_CLAIM_DEFERRED serial=%llu contender=%s reason=%s"), Airdrop.Serial,
            *Claimer.ToString(), LDGrowthWire::ReasonName(Reason));
        return true;
    }
    if (Outcome != ELDRewardOutcome::Granted || !Airdrop.Claim(Claimer, NowMs))
    {
        Error = FString::Printf(TEXT("The airdrop %llu payout to %s was refused: outcome %u, %s."), Airdrop.Serial,
            *Claimer.ToString(), uint32(Outcome), LDGrowthWire::ReasonName(Reason));
        return false;
    }
    bPublicMatchDirty = true;
    UE_LOG(LogLDWorld, Display, TEXT("LD_AIRDROP_CLAIM serial=%llu point=%s contender=%s xp=%lld gold=%lld at_ms=%lld"),
        Airdrop.Serial, *Point->Id.ToString(), *Claimer.ToString(), Receipt.XPDelta, Receipt.GoldDelta, NowMs);
    return true;
}
