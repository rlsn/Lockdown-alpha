#include "Framework/LDGameMode.h"
#include "Core/LDLog.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Framework/LDPlayerController.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDMapRegistry.h"
#include "Engine/World.h"

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

bool ALDGameMode::EnterPKLives(const TArray<FLDPKPlacement>& Placements, const FLDGameplayWritePermit& Permit,
    TArray<FGuid>& OutBlocked, FString& Error)
{
    OutBlocked.Reset();
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None || !IsRestPhase(MatchContext)
        || !Combat || !Registry)
    {
        Error = TEXT("PK lives enter only in a rest, under the current RespawnsAndWorld permit.");
        return false;
    }
    TArray<FLDPKPlacement> Order = Placements;
    Order.Sort([](const FLDPKPlacement& A, const FLDPKPlacement& B) { return A.Contender < B.Contender; });
    // Every rest body of the entrants retires before any PK body is placed.
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
    for (const FLDPKPlacement& Placement : Order)
    {
        const FLDLifeRecord* Life = Lives.Find(Placement.Contender);
        if (!IsMemberPresent(Placement.Contender) || !Life || Life->Phase != ELDLifePhase::Dead || Life->Life == MAX_uint32)
        {
            OutBlocked.Add(Placement.Contender);
            continue;
        }
        const uint32 NewLife = Life->Life + 1;
        FLDPlacementTarget Target;
        Target.Kind = FLDPlacementTarget::EKind::Anchor;
        Target.Anchor = Placement.Anchor;
        FLDPlacedBody Placed;
        FLDBodyCombatState CombatState;
        if (!Life->NativeMotion.IsEmpty()
            || !PlaceContenderBody(Placement.Contender, NewLife, Permit, Placed, Error, Target)
            || !Combat->PrepareContenderBody(*Placed.Body.Get(), {ELDVictimKind::Contender, Placement.Contender, NewLife}, Permit,
                CombatState, Error)
            || !Registry->ValidateSpawnReservation(Placed.Reservation, Permit, Placed.Body.Get(), Error))
        {
            ReleasePlacedBody(Placed);
            if (!Error.IsEmpty() && !Error.StartsWith(TEXT("SpawnBlocked"))) { return false; }
            // A blocked PK anchor is reported, never silently replaced; the contender returns to its rest space.
            UE_LOG(LogLDMatch, Warning, TEXT("LD_PK_SPAWN_BLOCKED contender=%s anchor=%s %s"), *LDLog::Id(Placement.Contender),
                *Placement.Anchor.ToString(), *Error);
            Error.Reset();
            OutBlocked.Add(Placement.Contender);
            PendingPhasePlacement.Add(Placement.Contender);
            continue;
        }
        if (!CommitPlacedLife(Placed, MoveTemp(CombatState), 0, Permit, Error) || !ActivatePlacedBody(Placed, Permit, Error))
        {
            FaultAuthority(Error);
            return false;
        }
        const FLDLifeRecord& Committed = Lives.FindChecked(Placement.Contender);
        PKLives.Add(Placement.Contender);
        if (!Combat->SetPKBody(Committed.Body, Placement.Group, Placement.Side, Permit, Error)
            || !Combat->ReadyAllForPhase(Placement.Contender, Permit, Error))
        {
            return false;
        }
        NotifyLife(Committed.Body, ELDLifeChange::Respawned, Committed.Revision, AuthorityNowMs);
        UE_LOG(LogLDMatch, Display, TEXT("LD_PK_PLACED contender=%s life=%u anchor=%s side=%u"), *LDLog::Id(Placement.Contender),
            NewLife, *Committed.SpawnAnchor.ToString(), uint32(Placement.Side));
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
        if (!PlacePhaseLife(Id, true, 0, Permit, bBlocked, Error)) { return false; }
        if (bBlocked) { PendingPhasePlacement.Add(Id); }
    }
    Error.Reset();
    return true;
}
