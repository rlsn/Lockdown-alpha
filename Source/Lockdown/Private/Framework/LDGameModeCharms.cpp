#include "Framework/LDGameMode.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatDirector.h"

// #198 field charms. The waves, expiry and claim key are the pure FLDCharmState / LDCharms rules keyed to field progress
// T; this file only drives them: waves and expiry at the RespawnsAndWorld boundary while a field segment runs (after the
// airdrop, so a newly announced drop's point is excluded), the claim in the same step's Transactions phase (bodies do
// not move between the two), and the effect through Combat. Rest entry removes what is left.

namespace
{
    int32 HeldAirdropPoint(const FLDAirdropState& Airdrop)
    {
        // #198 outer §5: a claimed or expired drop no longer holds its point.
        return Airdrop.HoldsPoint() ? Airdrop.PointIndex : INDEX_NONE;
    }
}

int64 ALDGameMode::NextCharmDueMs() const
{
    if (!Charms.IsActive() || !IsFieldRunning()) { return MAX_int64; }
    const int64 FieldDue = Charms.NextDueFieldMs(GetFieldNowMs());
    return FieldDue == MAX_int64 ? MAX_int64 : FieldDueAuthorityMs(FieldDue);
}

bool ALDGameMode::AdvanceCharms(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None || !Charms.IsActive())
    {
        Error = TEXT("The charm schedule requires the current RespawnsAndWorld permit and an active schedule.");
        return false;
    }
    if (!IsFieldRunning()) { return true; }
    const int64 FieldMs = GetFieldNowMs();
    const int64 NowMs = Permit.GetBoundary().AtMs;
    FLDCharmAdvance Step;
    Charms.Advance(FieldMs, HeldAirdropPoint(Airdrop), Step);
    for (const FLDCharmInstance& Charm : Step.Expired)
    {
        const LDAirdrop::FPoint* Point = Charms.Schedule.Points.IsValidIndex(Charm.PointIndex) ? &Charms.Schedule.Points[Charm.PointIndex] : nullptr;
        UE_LOG(LogLDWorld, Display, TEXT("LD_CHARM_EXPIRE serial=%llu kind=%s point=%s reason=lifetime field_ms=%lld at_ms=%lld"),
            Charm.Serial, LDCharms::KindName(Charm.Kind), Point ? *Point->Id.ToString() : TEXT("none"), FieldMs, NowMs);
    }
    for (const FLDCharmInstance& Charm : Step.Spawned)
    {
        const LDAirdrop::FPoint* Point = Charms.PointOf(Charm);
        UE_LOG(LogLDWorld, Display, TEXT("LD_CHARM_SPAWN serial=%llu wave=%lld kind=%s point=%s expire_field_ms=%lld field_ms=%lld at_ms=%lld"),
            Charm.Serial, Charm.Wave, LDCharms::KindName(Charm.Kind), Point ? *Point->Id.ToString() : TEXT("none"),
            Charm.ExpireFieldMs, FieldMs, NowMs);
    }
    for (const FLDCharmSkip& Skip : Step.Skipped)
    {
        UE_LOG(LogLDWorld, Warning, TEXT("LD_CHARM_SKIPPED wave=%lld missing=%d reason=%s field_ms=%lld at_ms=%lld"),
            Skip.Wave, Skip.Missing, Skip.Reason, FieldMs, NowMs);
    }
    if (Step.Changed()) { bPublicMatchDirty = true; }
    return true;
}

bool ALDGameMode::ClaimCharms(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None || !Charms.IsActive() || !Combat)
    {
        Error = TEXT("The charm claim requires the current Transactions permit, an active schedule and Combat.");
        return false;
    }
    if (!IsFieldRunning() || Charms.Active.IsEmpty()) { return true; }
    const int64 NowMs = Permit.GetBoundary().AtMs;
    TArray<LDAirdrop::FCandidate> Candidates;
    Combat->ReadLivingContenderFeet(Candidates);
    const TArray<FLDCharmInstance> Lying = Charms.Active;
    for (const FLDCharmInstance& Charm : Lying)
    {
        const LDAirdrop::FPoint* Point = Charms.PointOf(Charm);
        FGuid Claimer;
        if (!Point || !LDCharms::PickClaimer(Point->PositionCm, Charms.Schedule.PickupRadiusCm, Candidates,
            Charms.TieSeed(Charm.Serial), Claimer)) { continue; }
        bool bApplied = false;
        FLDCharmEffectFact Fact;
        if (!Combat->ApplyCharm(Claimer, Charm.Kind, Permit, bApplied, Fact, Error)) { return false; }
        // The claimer's body is gone or dying at this boundary: the charm stays for the next eligible toucher.
        if (!bApplied) { continue; }
        FLDCharmInstance Taken;
        if (!Charms.Remove(Charm.Serial, Taken))
        {
            Error = FString::Printf(TEXT("Charm %llu was applied but is no longer lying."), Charm.Serial);
            return false;
        }
        bPublicMatchDirty = true;
        UE_LOG(LogLDWorld, Display, TEXT("LD_CHARM_CLAIM serial=%llu kind=%s point=%s contender=%s hp=%s->%s mp=%s->%s bonus_until_ms=%lld field_ms=%lld at_ms=%lld"),
            Charm.Serial, LDCharms::KindName(Charm.Kind), *Point->Id.ToString(), *Claimer.ToString(),
            *LDLog::P2(Fact.HpBeforeP2), *LDLog::P2(Fact.HpAfterP2), *LDLog::P2(Fact.MpBeforeP2), *LDLog::P2(Fact.MpAfterP2),
            Fact.BonusUntilMs, GetFieldNowMs(), NowMs);
    }
    return true;
}

bool ALDGameMode::EndCharmsForRest(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None || !Charms.IsActive())
    {
        Error = TEXT("Removing charms at a rest requires the current RespawnsAndWorld permit and an active schedule.");
        return false;
    }
    TArray<FLDCharmInstance> Left;
    Charms.Clear(Left);
    for (const FLDCharmInstance& Charm : Left)
    {
        const LDAirdrop::FPoint* Point = Charms.PointOf(Charm);
        UE_LOG(LogLDWorld, Display, TEXT("LD_CHARM_EXPIRE serial=%llu kind=%s point=%s reason=rest field_ms=%lld at_ms=%lld"),
            Charm.Serial, LDCharms::KindName(Charm.Kind), Point ? *Point->Id.ToString() : TEXT("none"), GetFieldNowMs(),
            Permit.GetBoundary().AtMs);
    }
    if (!Left.IsEmpty()) { bPublicMatchDirty = true; }
    return true;
}
