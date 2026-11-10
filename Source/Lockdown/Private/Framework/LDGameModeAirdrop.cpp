#include "Framework/LDGameMode.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDGrowthProtocol.h"
#include "Development/LDGrowthDirector.h"

// D26 (#79): the airdrop. The schedule and claim selection are the pure FLDAirdropState / LDAirdrop rules; this file
// only drives them: the schedule at the RespawnsAndWorld boundary, then the claim in the same step's Transactions phase
// (bodies do not move between the two), paid through the single growth reward composite.

int64 ALDGameMode::NextAirdropDueMs() const
{
    // #198: the schedule is in field progress T; nothing is due while a rest pauses the field.
    return Airdrop.IsActive() && IsFieldRunning() ? FieldDueAuthorityMs(Airdrop.NextDueMs(GetFieldNowMs())) : MAX_int64;
}

bool ALDGameMode::AdvanceAirdrop(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None || !Airdrop.IsActive())
    {
        Error = TEXT("The airdrop schedule requires the current RespawnsAndWorld permit and an active schedule.");
        return false;
    }
    if (!IsFieldRunning()) { return true; }
    const int64 NowMs = Timeline.FieldMsAt(Permit.GetBoundary().AtMs);
    if (Airdrop.Advance(NowMs))
    {
        bPublicMatchDirty = true;
        const LDAirdrop::FPoint* Point = Airdrop.CurrentPoint();
        const TCHAR* Event = Airdrop.Phase == ELDAirdropPhase::Expired ? TEXT("EXPIRE")
            : Airdrop.Phase == ELDAirdropPhase::Landed ? TEXT("LAND") : TEXT("ANNOUNCE");
        UE_LOG(LogLDWorld, Display, TEXT("LD_AIRDROP_%s serial=%llu point=%s field_t_ms=%lld"), Event, Airdrop.Serial,
            Point ? *Point->Id.ToString() : TEXT("none"), NowMs);
    }
    return true;
}

bool ALDGameMode::ClaimAirdrop(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None || !Airdrop.IsActive()
        || !Combat || !Growth || !DemoContent)
    {
        Error = TEXT("The airdrop claim requires the current Transactions permit, an active schedule, Combat and the growth domain.");
        return false;
    }
    if (!IsFieldRunning() || Airdrop.Phase != ELDAirdropPhase::Landed) { return true; }
    const int64 NowMs = Timeline.FieldMsAt(Permit.GetBoundary().AtMs);
    const LDAirdrop::FPoint* Point = Airdrop.CurrentPoint();
    TArray<LDAirdrop::FCandidate> Candidates;
    Combat->ReadLivingContenderFeet(Candidates);
    FGuid Claimer;
    if (!Point || !LDAirdrop::PickClaimer(Point->PositionCm, Airdrop.Schedule.PickupRadiusCm, Candidates, Airdrop.TieSeed(),
        Claimer)) { return true; }
    const auto& Parameters = DemoContent->Get().Parameters;
    FLDRewardReceipt Receipt;
    ELDGrowthReason Reason = ELDGrowthReason::None;
    // #198 outer §5: 200 XP and one build reward draw (50% active blueprint / 50% scarce component) in one commit.
    const ELDRewardOutcome Outcome = Growth->CommitFieldReward(Claimer, TEXT("airdrop"), Airdrop.Serial,
        Parameters.AirdropRewardXP, ELDFieldRewardKind::BuildDraw, Permit, Receipt, Reason);
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
    UE_LOG(LogLDWorld, Display, TEXT("LD_AIRDROP_CLAIM serial=%llu point=%s contender=%s xp=%lld build=%u definition=%s field_t_ms=%lld"),
        Airdrop.Serial, *Point->Id.ToString(), *Claimer.ToString(), Receipt.XPDelta, uint32(Receipt.FieldResult),
        *Receipt.FieldDefinition.ToString(), NowMs);
    return true;
}
