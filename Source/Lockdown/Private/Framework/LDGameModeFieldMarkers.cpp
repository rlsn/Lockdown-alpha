#include "Framework/LDGameMode.h"
#include "Core/LDLog.h"
#include "Development/LDGrowthDirector.h"
#include "Development/LDGrowthProtocol.h"

// #198 outer §5 camp markers and map event reward payment. The reveal schedule, draws and claim identity are the pure
// FLDFieldMarkerState / LDFieldMarkers rules keyed to field progress T; this file only drives them: reveals at the
// RespawnsAndWorld boundary while a field segment runs, the claim from the camp owner's committed clearing death
// (DeathsAndRewards), and the expiry at the segment's rest entry. Every map event reward (airdrop excepted, whose drop
// itself waits on the ground) is paid here, retried at later Transactions boundaries while growth refuses it as retryable.

namespace
{
    void LogMarker(const TCHAR* Event, const FLDFieldMarkerState& State, const FLDFieldMarker& Marker, int64 FieldMs)
    {
        const LDFieldMarkers::FCandidate* Camp = State.CampOf(Marker);
        UE_LOG(LogLDWorld, Display, TEXT("LD_MARKER_%s segment=%u kind=%s camp=%s field_t_ms=%lld"), Event, uint32(State.Segment),
            LDFieldMarkers::KindName(Marker.Kind), Camp ? *Camp->CampId.ToString() : TEXT("none"), FieldMs);
    }
}

int64 ALDGameMode::NextFieldMarkerDueMs() const
{
    if (!FieldMarkers.IsActive() || !IsFieldRunning()) { return MAX_int64; }
    const int64 FieldDue = FieldMarkers.NextDueMs(GetFieldNowMs());
    return FieldDue == MAX_int64 ? MAX_int64 : FieldDueAuthorityMs(FieldDue);
}

bool ALDGameMode::AdvanceFieldMarkers(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None || !FieldMarkers.IsActive())
    {
        Error = TEXT("The camp marker schedule requires the current RespawnsAndWorld permit and an active schedule.");
        return false;
    }
    if (!IsFieldRunning()) { return true; }
    const int64 FieldMs = GetFieldNowMs();
    TArray<FLDFieldMarker> Revealed, Expired;
    if (!FieldMarkers.Advance(FieldMs, Revealed, Expired)) { return true; }
    for (const FLDFieldMarker& Marker : Expired) { LogMarker(TEXT("EXPIRE"), FieldMarkers, Marker, FieldMs); }
    for (const FLDFieldMarker& Marker : Revealed) { LogMarker(TEXT("REVEAL"), FieldMarkers, Marker, FieldMs); }
    bPublicMatchDirty = true;
    return true;
}

bool ALDGameMode::EndFieldMarkersForRest(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None || !FieldMarkers.IsActive())
    {
        Error = TEXT("Ending camp markers at a rest requires the current RespawnsAndWorld permit and an active schedule.");
        return false;
    }
    TArray<FLDFieldMarker> Expired;
    FieldMarkers.EndSegment(Expired);
    for (const FLDFieldMarker& Marker : Expired) { LogMarker(TEXT("EXPIRE"), FieldMarkers, Marker, GetFieldNowMs()); }
    if (!Expired.IsEmpty()) { bPublicMatchDirty = true; }
    return true;
}

bool ALDGameMode::ConsumeCampClear(FName CampId, const FGuid& ClearOwner, int64 FieldMs, const FLDGameplayWritePermit& Permit,
    FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::DeathsAndRewards) != ELDCommitReject::None || !FieldMarkers.IsActive())
    {
        Error = TEXT("A camp clear reaches the camp markers only with the current DeathsAndRewards permit of an active schedule.");
        return false;
    }
    const FLDFieldMarker* Open = FieldMarkers.OpenMarkerFor(CampId, FieldMs);
    if (!Open) { return true; }
    const ELDFieldMarkerKind Kind = Open->Kind;
    FLDContenderLifeRead Life;
    FString LifeError;
    // Only a contender of this match can take a marker; a clear without one leaves it open for the next clear.
    if (!ClearOwner.IsValid() || !IsMemberPresent(ClearOwner) || !ReadContenderLife(MatchContext, ClearOwner, Life, LifeError))
    {
        UE_LOG(LogLDWorld, Display, TEXT("LD_MARKER_CLEAR_UNCLAIMED segment=%u kind=%s camp=%s owner=%s field_t_ms=%lld"),
            uint32(FieldMarkers.Segment), LDFieldMarkers::KindName(Kind), *CampId.ToString(), *LDLog::Id(ClearOwner), FieldMs);
        return true;
    }
    if (!FieldMarkers.Claim(Kind, ClearOwner, FieldMs))
    {
        Error = FString::Printf(TEXT("The open %s camp marker of %s refused its first clear."), LDFieldMarkers::KindName(Kind),
            *CampId.ToString());
        return false;
    }
    bPublicMatchDirty = true;
    UE_LOG(LogLDWorld, Display, TEXT("LD_MARKER_CLAIM segment=%u kind=%s camp=%s contender=%s field_t_ms=%lld"),
        uint32(FieldMarkers.Segment), LDFieldMarkers::KindName(Kind), *CampId.ToString(), *LDLog::Id(ClearOwner), FieldMs);
    return PayFieldReward(ClearOwner, LDFieldMarkers::RewardEvent(Kind), FieldMarkers.Segment, 0,
        Kind == ELDFieldMarkerKind::High ? ELDFieldRewardKind::Blueprint : ELDFieldRewardKind::ComponentChoice, Permit, Error);
}

bool ALDGameMode::PayFieldReward(const FGuid& Recipient, FName Event, uint64 Serial, int64 XP, ELDFieldRewardKind Kind,
    const FLDGameplayWritePermit& Permit, FString& Error)
{
    if ((ValidateWritePermit(Permit, ELDCommitPhase::DeathsAndRewards) != ELDCommitReject::None
            && ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None)
        || !Growth || !Recipient.IsValid() || Event.IsNone() || Serial == 0)
    {
        Error = TEXT("A map event reward needs the current DeathsAndRewards or Transactions permit, the growth domain and its identity.");
        return false;
    }
    return CommitOwedFieldReward({Recipient, Event, Serial, XP, Kind}, Permit, Error);
}

bool ALDGameMode::CommitOwedFieldReward(const FLDOwedFieldReward& Reward, const FLDGameplayWritePermit& Permit, FString& Error)
{
    FLDRewardReceipt Receipt;
    ELDGrowthReason Reason = ELDGrowthReason::None;
    const ELDRewardOutcome Outcome = Growth->CommitFieldReward(Reward.Recipient, Reward.Event, Reward.Serial, Reward.XP, Reward.Kind,
        Permit, Receipt, Reason);
    if (Outcome == ELDRewardOutcome::RetryableNotCommitted)
    {
        // Nothing was installed; the reward stays owed to the same recipient.
        OwedFieldRewards.Add(Reward);
        UE_LOG(LogLDWorld, Warning, TEXT("LD_FIELD_REWARD_DEFERRED event=%s serial=%llu contender=%s reason=%s"),
            *Reward.Event.ToString(), Reward.Serial, *LDLog::Id(Reward.Recipient), LDGrowthWire::ReasonName(Reason));
        return true;
    }
    if (Outcome != ELDRewardOutcome::Granted)
    {
        Error = FString::Printf(TEXT("The %s %llu reward to %s was refused: outcome %u, %s."), *Reward.Event.ToString(),
            Reward.Serial, *Reward.Recipient.ToString(), uint32(Outcome), LDGrowthWire::ReasonName(Reason));
        return false;
    }
    UE_LOG(LogLDWorld, Display, TEXT("LD_FIELD_REWARD event=%s serial=%llu contender=%s xp=%lld build=%u definition=%s field_t_ms=%lld"),
        *Reward.Event.ToString(), Reward.Serial, *LDLog::Id(Reward.Recipient), Receipt.XPDelta, uint32(Receipt.FieldResult),
        *Receipt.FieldDefinition.ToString(), GetFieldNowMs());
    return true;
}

bool ALDGameMode::RetryFieldRewards(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None || !Growth)
    {
        Error = TEXT("Owed map event rewards are retried only with the current Transactions permit and the growth domain.");
        return false;
    }
    const TArray<FLDOwedFieldReward> Owed = MoveTemp(OwedFieldRewards);
    OwedFieldRewards.Reset();
    for (const FLDOwedFieldReward& Reward : Owed)
    {
        if (!CommitOwedFieldReward(Reward, Permit, Error)) { return false; }
    }
    return true;
}
