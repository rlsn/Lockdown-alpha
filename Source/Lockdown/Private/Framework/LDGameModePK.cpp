#include "Framework/LDGameMode.h"
#include "Core/LDLog.h"
#include "Character/LDCharacter.h"
#include "Character/LDNativeMotionTypes.h"
#include "Combat/LDCombatDirector.h"
#include "Framework/LDPlayerController.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDMapRegistry.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "PK/LDPKDirector.h"

// #198 session D: PK lives (approved outer parameters, issue #198 comment 6752601175 §2/§3/§8). The PK director
// (PK/LDPKDirector.cpp) decides who fights where; this file owns the life side. Entering the arena retires the entrant's
// rest life like any phase boundary (no death, K/D or reward) and places a PK life on its drawn PK spawn anchor with the
// same spawn ground and occupancy rules, without protection, at full resources with every cooldown ready, in the PK
// isolation group with its side. Leaving (settlement, NoContest) retires every living PK life back to its rest space.

bool ALDGameMode::ReadLivingBody(const FGuid& Contender, FLDCombatBodyRef& OutBody, FVector& OutFootCm) const
{
    const FLDLifeRecord* Life = Lives.Find(Contender);
    if (!Life || Life->Kind != ELDVictimKind::Contender || Life->Phase != ELDLifePhase::Alive || !Life->Body.IsValid())
    {
        return false;
    }
    OutBody = Life->Body;
    OutFootCm = Life->MotionLastFoot;
    return true;
}

const FLDNativeMotionBatch* ALDGameMode::ReadStepMotion(const FGuid& Contender) const
{
    const FLDLifeRecord* Life = Lives.Find(Contender);
    if (!bAuthorityStepOpen || !Life || Life->Phase != ELDLifePhase::Alive || Life->NativeMotion.IsEmpty()) { return nullptr; }
    const TSharedPtr<const FLDNativeMotionBatch>& Last = Life->NativeMotion.Last();
    return Last.IsValid() && Last->Step == AuthorityStep && Last->Body == Life->Body ? Last.Get() : nullptr;
}

bool ALDGameMode::EnterPKLives(const TArray<FLDPKPlacement>& Placements, const FLDGameplayWritePermit& Permit, bool& bPlaced,
    FString& Error)
{
    bPlaced = false;
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None || !IsRestPhase(MatchContext)
        || !Combat || !PendingPKEntry.IsEmpty())
    {
        Error = TEXT("PK lives enter once per PK, only in a rest, under the current RespawnsAndWorld permit.");
        return false;
    }
    TArray<FLDPKPlacement> Order = Placements;
    Order.Sort([](const FLDPKPlacement& A, const FLDPKPlacement& B) { return A.Contender < B.Contender; });
    // Every rest body of the entrants retires before any PK body is prepared.
    for (const FLDPKPlacement& Placement : Order)
    {
        if (!Placement.Group.IsValid() || Placement.Side == 0 || Placement.Anchor.IsNone())
        {
            Error = TEXT("A PK placement needs its group, side and anchor.");
            return false;
        }
        PendingPhasePlacement.Remove(Placement.Contender);
        const FLDLifeRecord* Life = Lives.Find(Placement.Contender);
        if (Life && Life->Phase == ELDLifePhase::Alive && !RetireLifeForPhase(Placement.Contender, Permit, Error)) { return false; }
    }
    PendingPKEntry = MoveTemp(Order);
    bPKEntryLogged = false;
    return TryPKEntry(Permit, bPlaced, Error);
}

bool ALDGameMode::TryPKEntry(const FLDGameplayWritePermit& Permit, bool& bPlaced, FString& Error)
{
    bPlaced = false;
    // An entrant that left since the lock forfeits: no body stands for it (the PK judges it as forfeited).
    PendingPKEntry.RemoveAll([this](const FLDPKPlacement& Placement) { return !IsMemberPresent(Placement.Contender); });
    TArray<FLDPhaseTarget> Targets;
    for (const FLDPKPlacement& Placement : PendingPKEntry)
    {
        FLDPhaseTarget& Target = Targets.AddDefaulted_GetRef();
        Target.Contender = Placement.Contender;
        Target.Where.Kind = FLDPlacementTarget::EKind::Anchor;
        Target.Where.Anchor = Placement.Anchor;
    }
    TArray<FLDPhasePrepared> Prepared;
    ON_SCOPE_EXIT { ReleasePhasePrepared(Prepared); };
    bool bBlocked = false;
    FString Reason;
    if (!PrepareJointPhase(Targets, Permit, Prepared, bBlocked, Reason, Error)) { return false; }
    if (bBlocked)
    {
        // A blocked approved anchor is reported and never replaced: the entry waits, nobody stands meanwhile.
        if (!bPKEntryLogged)
        {
            UE_LOG(LogLDMatch, Warning, TEXT("LD_PK_ENTRY_WAITING generation=%u at_ms=%lld %s"), MatchContext.Generation,
                Permit.GetBoundary().AtMs, *Reason);
            bPKEntryLogged = true;
        }
        Error.Reset();
        return true;
    }
    for (FLDPhasePrepared& Entry : Prepared)
    {
        if (!CommitPhaseBody(Entry, 0, Permit, Error)) { return false; }
    }
    for (const FLDPKPlacement& Placement : PendingPKEntry)
    {
        const FLDLifeRecord& Committed = Lives.FindChecked(Placement.Contender);
        PKLives.Add(Placement.Contender);
        if (!Combat->SetPKBody(Committed.Body, Placement.Group, Placement.Side, Permit, Error)
            || !Combat->ReadyAllForPhase(Placement.Contender, Permit, Error))
        {
            return false;
        }
        NotifyLife(Committed.Body, ELDLifeChange::Respawned, Committed.Revision, AuthorityNowMs);
        UE_LOG(LogLDMatch, Display, TEXT("LD_PK_PLACED contender=%s life=%u anchor=%s side=%u"), *LDLog::Id(Placement.Contender),
            Committed.Life, *Committed.SpawnAnchor.ToString(), uint32(Placement.Side));
    }
    Prepared.Reset();
    PendingPKEntry.Reset();
    bPlaced = true;
    Error.Reset();
    return true;
}

bool ALDGameMode::AbandonPKEntry(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None || PendingPKEntry.IsEmpty())
    {
        Error = TEXT("Only a waiting PK entry is abandoned, under the current RespawnsAndWorld permit.");
        return false;
    }
    const TArray<FLDPKPlacement> Waiting = MoveTemp(PendingPKEntry);
    PendingPKEntry.Reset();
    // NoContest: nobody fights or is paid (the arena never became legal).
    if (PKDirector && !PKDirector->AbandonArena(Permit, Error)) { return false; }
    // Every waiting entrant goes back to its rest space under a newer spawn identity: its PK request was withdrawn when
    // the joint entry failed, so the rest anchor is never a conflicting replay of the PK anchor.
    for (const FLDPKPlacement& Placement : Waiting)
    {
        if (!IsMemberPresent(Placement.Contender)) { continue; }
        if (!WithdrawPhaseRequest(Placement.Contender, Permit, Error)) { return false; }
        bool bBlocked = false;
        if (!PlaceRestLife(Placement.Contender, Permit, bBlocked, Error)) { return false; }
    }
    Error.Reset();
    return true;
}

bool ALDGameMode::ReturnPKLivesToRest(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None || !IsRestPhase(MatchContext))
    {
        Error = TEXT("PK lives return only in a rest, under the current RespawnsAndWorld permit.");
        return false;
    }
    TArray<FGuid> Order = PKLives.Array();
    Order.Sort();
    for (const FGuid& Id : Order)
    {
        if (Lives.FindChecked(Id).Phase == ELDLifePhase::Alive && !RetireLifeForPhase(Id, Permit, Error)) { return false; }
        PKLives.Remove(Id);
    }
    for (const FGuid& Id : Order)
    {
        if (!IsMemberPresent(Id)) { continue; }
        bool bBlocked = false;
        if (!PlaceRestLife(Id, Permit, bBlocked, Error)) { return false; }
    }
    Error.Reset();
    return true;
}
