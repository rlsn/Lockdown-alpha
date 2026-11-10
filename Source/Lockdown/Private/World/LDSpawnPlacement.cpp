#include "World/LDSpawnPlacement.h"
#include "Region/LDMapRegistry.h"
#include "Region/LDMapGeometry.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

bool LDSpawnPlacement::TryUniformSample(uint32 Sample, uint32 Bound, uint32& Index)
{
    Index=0;
    if (!Bound) { return false; }
    const uint32 RejectedPrefix=(uint32(0)-Bound)%Bound;
    if (Sample<RejectedPrefix) { return false; }
    Index=Sample%Bound;
    return true;
}

struct ULDMapRegistry::FSpawnState
{
    struct FMemo
    {
        FLDSpawnRequestKey Request;
        TSharedPtr<const FLDSpawnSelection> Selection;
        TSharedPtr<const FLDSpawnReservation> Held;
        bool Finalized=false;
        bool Withdrawn=false;
    };
    FLDMatchContext Match;
    FLDWorldRevision World;
    TMap<FGuid,FMemo> Requests;
};

LDSpawnPlacement::EAdmission LDSpawnPlacement::Admit(const FPriorRequest* Previous, const FLDSpawnRequestKey& Request)
{
    if (!Previous) { return EAdmission::New; }
    if (Request.Sequence==Previous->Request.Sequence)
    {
        if (!(Request==Previous->Request)) { return EAdmission::Conflict; }
        return Previous->bWithdrawn ? EAdmission::Refused : EAdmission::Replay;
    }
    if (Request.Sequence<Previous->Request.Sequence || Previous->bHeld) { return EAdmission::Refused; }
    if (Previous->bWithdrawn) { return Request.ProposedLife>=Previous->Request.ProposedLife ? EAdmission::New : EAdmission::Refused; }
    return Previous->bFinalized && Request.ProposedLife>Previous->Request.ProposedLife ? EAdmission::New : EAdmission::Refused;
}

namespace
{
    bool SpawnPermission(UWorld& World, const FLDMatchContext& Match,
        const FLDGameplayWritePermit& Permit, FString& Error)
    {
        const auto* Mode=World.GetAuthGameMode<ALDGameMode>();
        if (!Mode || Mode->GetMatchContext()!=Match
            || Mode->ValidateWritePermit(Permit,ELDCommitPhase::RespawnsAndWorld)!=ELDCommitReject::None)
        {
            Error=TEXT("Spawn preparation requires the current core RespawnsAndWorld permit.");
            return false;
        }
        return true;
    }

    bool CapsulesOverlap(const FVector& First, const FVector& Second, const FCollisionShape& Capsule)
    {
        const double Radius=Capsule.GetCapsuleRadius();
        const double StraightHalf=Capsule.GetCapsuleHalfHeight()-Radius;
        const double Vertical=FMath::Max(0.,FMath::Abs(First.Z-Second.Z)-2*StraightHalf);
        return FVector::DistSquared2D(First,Second)+Vertical*Vertical<FMath::Square(2*Radius);
    }
}

bool ULDMapRegistry::BeginSpawnMatch(const FLDMatchContext& Match, FString& Error)
{
    const auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if (!Mode || Mode->GetMatchContext()!=Match || !Match.IsValid() || !Enabled())
    {
        Error=TEXT("Spawn state requires the current core match and registered Demo world.");
        return false;
    }
    if (SpawnState && SpawnState->Match==Match)
    {
        if (SpawnState->World!=WorldRevision)
        { Error=TEXT("Spawn state cannot survive a changed world revision."); return false; }
        return true;
    }
    if (SpawnState)
    {
        for (const auto& Pair : SpawnState->Requests)
        {
            if (Pair.Value.Held)
            { Error=TEXT("A previous match still holds an uncommitted spawn reservation."); return false; }
        }
    }
    SpawnState=MakeShared<FSpawnState>();
    SpawnState->Match=Match;
    SpawnState->World=WorldRevision;
    Error.Reset();
    return true;
}

bool ULDMapRegistry::AdmitSpawnRequest(const FLDSpawnRequestKey& Request, const FLDGameplayWritePermit& Permit,
    TSharedPtr<const FLDSpawnSelection>& Replay, FString& Error)
{
    Replay.Reset();
    if (!SpawnState || SpawnState->Match!=Request.Match || SpawnState->World!=WorldRevision
        || !Request.IsValid() || !SpawnPermission(*GetWorld(),Request.Match,Permit,Error))
    {
        if (Error.IsEmpty()) { Error=TEXT("Spawn request has no current match/world binding."); }
        return false;
    }
    auto* Previous=SpawnState->Requests.Find(Request.ContenderId);
    LDSpawnPlacement::FPriorRequest Prior;
    if (Previous)
    {
        Prior.Request=Previous->Request;
        Prior.bFinalized=Previous->Finalized;
        Prior.bHeld=Previous->Held.IsValid();
        Prior.bWithdrawn=Previous->Withdrawn;
    }
    switch (LDSpawnPlacement::Admit(Previous ? &Prior : nullptr,Request))
    {
    case LDSpawnPlacement::EAdmission::Replay:
        Replay=Previous->Selection;
        Error.Reset();
        return true;
    case LDSpawnPlacement::EAdmission::Conflict:
        Error=TEXT("RequestConflict: spawn request identity has different terms.");
        return false;
    case LDSpawnPlacement::EAdmission::Refused:
        Error=Prior.bWithdrawn && Request.Sequence==Prior.Request.Sequence
            ? TEXT("A withdrawn spawn request never replays; its life is placed under a newer sequence.")
            : TEXT("An expired or still-pending spawn request cannot redraw its anchor.");
        return false;
    default:
        break;
    }
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    FLDContenderLifeRead Life;
    if (!Mode->ReadContenderLife(Request.Match,Request.ContenderId,Life,Error)
        || (Life.Phase!=ELDLifePhase::Unspawned && Life.Phase!=ELDLifePhase::Dead)
        || Life.Life==MAX_uint32 || Request.ProposedLife!=Life.Life+1)
    {
        if (Error.IsEmpty()) { Error=TEXT("Spawn request does not name the next eligible contender life."); }
        return false;
    }
    Error.Reset();
    return true;
}

TSharedPtr<const FLDSpawnSelection> ULDMapRegistry::RecordSpawnSelection(const FLDSpawnRequestKey& Request, FName Anchor)
{
    TSharedPtr<FLDSpawnSelection> Selection(new FLDSpawnSelection);
    Selection->Owner=FObjectKey(this);
    Selection->Key=Request;
    Selection->World=WorldRevision;
    Selection->Anchor=Anchor;
    FSpawnState::FMemo Memo;
    Memo.Request=Request;
    Memo.Selection=Selection;
    SpawnState->Requests.Add(Request.ContenderId,MoveTemp(Memo));
    return Selection;
}

bool ULDMapRegistry::DrawSpawnAnchor(const FLDSpawnRequestKey& Request, FRandomStream& Random,
    const FLDGameplayWritePermit& Permit, TSharedPtr<const FLDSpawnSelection>& Out, FString& Error)
{
    Out.Reset();
    TSharedPtr<const FLDSpawnSelection> Replay;
    if (!AdmitSpawnRequest(Request,Permit,Replay,Error)) { return false; }
    if (Replay) { Out=MoveTemp(Replay); return true; }
    const auto& Anchors=Content->Get().World.SpawnIds;
    if (Anchors.Num()<LDDemoWorld::MinSpawns)
    { Error=TEXT("The approved spawn anchor pool is unavailable."); return false; }
    uint32 Index=0;
    while (!LDSpawnPlacement::TryUniformSample(Random.GetUnsignedInt(),uint32(Anchors.Num()),Index)) {}
    Out=RecordSpawnSelection(Request,FName(*Anchors[int32(Index)]));
    Error.Reset();
    return true;
}

bool ULDMapRegistry::SelectSpawnAnchor(const FLDSpawnRequestKey& Request, FName Anchor,
    const FLDGameplayWritePermit& Permit, TSharedPtr<const FLDSpawnSelection>& Out, FString& Error)
{
    Out.Reset();
    TSharedPtr<const FLDSpawnSelection> Replay;
    if (!AdmitSpawnRequest(Request,Permit,Replay,Error)) { return false; }
    if (Replay)
    {
        if (Replay->AnchorId()!=Anchor) { Error=TEXT("RequestConflict: the replayed spawn request named another anchor."); return false; }
        Out=MoveTemp(Replay);
        return true;
    }
    // #198: a contender's fixed rest anchor is an approved spawn, and a PK entrant's is one of the PK ring spawns.
    const auto& Refs=Content->Get().World;
    if (!Refs.SpawnIds.Contains(Anchor.ToString()) && !Refs.PKSpawnIds.Contains(Anchor.ToString()))
    { Error=FString::Printf(TEXT("%s is not an approved spawn or PK spawn anchor."),*Anchor.ToString()); return false; }
    Out=RecordSpawnSelection(Request,Anchor);
    Error.Reset();
    return true;
}

bool ULDMapRegistry::ReserveSpawnNear(const FLDSpawnRequestKey& Request, const FVector& FootCm, FName SurfaceHint,
    double RadiusCm, const FCollisionShape& Capsule, const FLDGameplayWritePermit& Permit,
    TSharedPtr<const FLDSpawnReservation>& Out, bool& bBlocked, FString& Error)
{
    Out.Reset();
    bBlocked=false;
    TSharedPtr<const FLDSpawnSelection> Replay;
    if (!AdmitSpawnRequest(Request,Permit,Replay,Error)) { return false; }
    if (Replay) { Error=TEXT("A placement near the old foot is never replayed; it names a fresh request."); return false; }
    FCollisionShape Required;
    if (!ContentCapsule(*Content,Required,Error) || !Capsule.IsCapsule()
        || Capsule.GetCapsuleRadius()!=Required.GetCapsuleRadius()
        || Capsule.GetCapsuleHalfHeight()!=Required.GetCapsuleHalfHeight()
        || !FMath::IsFinite(RadiusCm) || RadiusCm<0 || FootCm.ContainsNaN())
    {
        if (Error.IsEmpty()) { Error=TEXT("A near placement needs the approved capsule, a finite foot and a nonnegative radius."); }
        return false;
    }
    ELDWorldQueryFailure Failure;
    FLDLegalGround FootGround;
    if (!QueryGround(WorldRevision,FootCm,Required,SurfaceHint,FootGround,Failure,Error))
    {
        // The old foot is no longer legal support: the caller uses a normal spawn instead.
        bBlocked=true;
        return false;
    }
    // The foot first, then three rings of eight directions out to RadiusCm, in a fixed order.
    TArray<FVector> Candidates;
    Candidates.Add(FootCm);
    for (int32 Ring=1; Ring<=3; ++Ring)
    {
        const double Distance=RadiusCm*Ring/3.;
        for (int32 Direction=0; Direction<8; ++Direction)
        {
            const double Angle=Direction*PI/4.;
            Candidates.Add(FootCm+FVector(FMath::Cos(Angle)*Distance,FMath::Sin(Angle)*Distance,0));
        }
    }
    const double Gap=(UCharacterMovementComponent::MIN_FLOOR_DIST+UCharacterMovementComponent::MAX_FLOOR_DIST)*.5;
    for (const FVector& Candidate : Candidates)
    {
        FLDLegalGround Ground;
        if (!QueryGround(WorldRevision,Candidate,Required,SurfaceHint,Ground,Failure,Error))
        {
            if (Failure==ELDWorldQueryFailure::Blocked || Failure==ELDWorldQueryFailure::NotGrounded || Failure==ELDWorldQueryFailure::WrongSurface) { continue; }
            return false;
        }
        if (FVector::DistSquared2D(Ground.SurfaceCm,FootCm)>FMath::Square(RadiusCm)+1) { continue; }
        const FVector Origin=Ground.SurfaceCm+FVector(0,0,Required.GetCapsuleHalfHeight()+Gap);
        bool Occupied=GetWorld()->OverlapAnyTestByObjectType(Origin,FQuat::Identity,
            FCollisionObjectQueryParams(ECC_Pawn),Required,FCollisionQueryParams(SCENE_QUERY_STAT(DemoSpawnNear)));
        for (const auto& Pair : SpawnState->Requests)
        {
            if (Pair.Value.Held && CapsulesOverlap(Origin,Pair.Value.Held->Origin,Required)) { Occupied=true; break; }
        }
        if (Occupied) { continue; }
        // Same reachable surface: a complete legal path from the old foot to the candidate.
        FLDCompletePath Path;
        if (!QueryCompletePath(WorldRevision,FootGround,Ground,Required,Path,Failure,Error))
        {
            if (Failure==ELDWorldQueryFailure::Blocked || Failure==ELDWorldQueryFailure::IncompletePath || Failure==ELDWorldQueryFailure::Unreachable) { continue; }
            return false;
        }
        TSharedPtr<const FLDSpawnSelection> Selection=RecordSpawnSelection(Request,NAME_None);
        TSharedPtr<FLDSpawnReservation> Reservation(new FLDSpawnReservation);
        Reservation->Owner=FObjectKey(this);
        Reservation->Key=Request;
        Reservation->Token=FGuid::NewGuid();
        Reservation->Anchor=NAME_None;
        Reservation->Offset=INDEX_NONE;
        Reservation->Placement=MoveTemp(Ground);
        Reservation->Origin=Origin;
        SpawnState->Requests.FindChecked(Request.ContenderId).Held=Reservation;
        Out=MoveTemp(Reservation);
        Error.Reset();
        return true;
    }
    bBlocked=true;
    Error=FString::Printf(TEXT("SpawnBlocked: no free legal placement within %.0f cm of the old field foot."),RadiusCm);
    return false;
}

bool ULDMapRegistry::ReserveSpawnPlacement(const TSharedPtr<const FLDSpawnSelection>& Selection,
    const FCollisionShape& Capsule, const FLDGameplayWritePermit& Permit,
    TSharedPtr<const FLDSpawnReservation>& Out, FString& Error)
{
    Out.Reset();
    if (!Selection || Selection->Owner!=FObjectKey(this) || !SpawnState
        || Selection->World!=WorldRevision || SpawnState->Match!=Selection->Key.Match
        || !SpawnPermission(*GetWorld(),Selection->Key.Match,Permit,Error))
    {
        if (Error.IsEmpty()) { Error=TEXT("Spawn selection has expired or belongs to another registry."); }
        return false;
    }
    auto* Memo=SpawnState->Requests.Find(Selection->Key.ContenderId);
    if (!Memo || Memo->Selection!=Selection || Memo->Finalized)
    { Error=TEXT("Spawn selection is not a pending request."); return false; }
    if (Memo->Held) { Out=Memo->Held; Error.Reset(); return true; }
    FCollisionShape Required;
    if (!ContentCapsule(*Content,Required,Error) || !Capsule.IsCapsule()
        || Capsule.GetCapsuleRadius()!=Required.GetCapsuleRadius()
        || Capsule.GetCapsuleHalfHeight()!=Required.GetCapsuleHalfHeight())
    { Error=TEXT("Spawn reservation requires the exact approved native capsule."); return false; }
    const auto* Anchor=Resolve(Selection->Anchor,Generation);
    const auto* Capture=Resolve(FName(*Content->Get().World.CaptureId),Generation);
    if (!Anchor || !Capture || Geometry->SpawnOffsetsCm.Num()!=7)
    { Error=TEXT("Compiled spawn anchor, local offsets or reference path endpoint is missing."); return false; }
    FLDLegalGround CaptureGround;
    ELDWorldQueryFailure Failure;
    if (!QueryGround(WorldRevision,Capture->Position,Required,Capture->Surface,CaptureGround,Failure,Error)) { return false; }
    const double Gap=(UCharacterMovementComponent::MIN_FLOOR_DIST+UCharacterMovementComponent::MAX_FLOOR_DIST)*.5;
    for (int32 Offset=0; Offset<Geometry->SpawnOffsetsCm.Num(); ++Offset)
    {
        FVector Delta;
        if (!Geometry->SpawnOffsetsCm[Offset].ToVector(Delta,Error)) { return false; }
        const FVector Candidate=Anchor->Position+Delta;
        FLDLegalGround Ground;
        if (!QueryGround(WorldRevision,Candidate,Required,Anchor->Surface,Ground,Failure,Error))
        {
            if (Failure==ELDWorldQueryFailure::Blocked) { continue; }
            return false;
        }
        const FVector Origin=Ground.SurfaceCm+FVector(0,0,Required.GetCapsuleHalfHeight()+Gap);
        bool Occupied=GetWorld()->OverlapAnyTestByObjectType(Origin,FQuat::Identity,
            FCollisionObjectQueryParams(ECC_Pawn),Required,FCollisionQueryParams(SCENE_QUERY_STAT(DemoSpawnReservation)));
        for (const auto& Pair : SpawnState->Requests)
        {
            if (Pair.Value.Held && CapsulesOverlap(Origin,Pair.Value.Held->Origin,Required)) { Occupied=true; break; }
        }
        if (Occupied) { continue; }
        FLDCompletePath Path;
        if (!QueryCompletePath(WorldRevision,Ground,CaptureGround,Required,Path,Failure,Error)) { return false; }
        TSharedPtr<FLDSpawnReservation> Reservation(new FLDSpawnReservation);
        Reservation->Owner=FObjectKey(this);
        Reservation->Key=Selection->Key;
        Reservation->Token=FGuid::NewGuid();
        Reservation->Anchor=Selection->Anchor;
        Reservation->Offset=Offset;
        Reservation->Placement=MoveTemp(Ground);
        Reservation->Origin=Origin;
        Memo->Held=Reservation;
        Out=MoveTemp(Reservation);
        Error.Reset();
        return true;
    }
    Error=TEXT("SpawnBlocked: all seven approved offsets at the selected anchor are occupied or blocked.");
    return false;
}

bool ULDMapRegistry::ValidateSpawnReservation(const TSharedPtr<const FLDSpawnReservation>& Reservation,
    const FLDGameplayWritePermit& Permit, const AActor* PreparedBody, FString& Error) const
{
    if (!Reservation || Reservation->Owner!=FObjectKey(this) || !SpawnState
        || SpawnState->Match!=Reservation->Key.Match || Reservation->Placement.World!=WorldRevision
        || !SpawnPermission(*GetWorld(),Reservation->Key.Match,Permit,Error))
    {
        if (Error.IsEmpty()) { Error=TEXT("Spawn reservation has a stale owner, match or world."); }
        return false;
    }
    const auto* Memo=SpawnState->Requests.Find(Reservation->Key.ContenderId);
    if (!Memo || Memo->Finalized || Memo->Held!=Reservation)
    { Error=TEXT("Spawn reservation is no longer held by this request."); return false; }
    FCollisionShape Capsule;
    if (!ContentCapsule(*Content,Capsule,Error)) { return false; }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoSpawnCommit));
    if (PreparedBody) { Query.AddIgnoredActor(PreparedBody); }
    if (GetWorld()->OverlapAnyTestByObjectType(Reservation->Origin,FQuat::Identity,
        FCollisionObjectQueryParams(ECC_Pawn),Capsule,Query))
    { Error=TEXT("SpawnBlocked: a physical body entered the held placement before commit."); return false; }
    Error.Reset();
    return true;
}

bool ULDMapRegistry::ReleaseSpawnReservation(TSharedPtr<const FLDSpawnReservation>& Reservation, FString& Error)
{
    if (!Reservation || Reservation->Owner!=FObjectKey(this) || !SpawnState || SpawnState->Match!=Reservation->Key.Match)
    { Error=TEXT("Cannot release an unknown or stale spawn reservation."); return false; }
    auto* Memo=SpawnState->Requests.Find(Reservation->Key.ContenderId);
    if (!Memo || Memo->Held!=Reservation || Memo->Finalized)
    { Error=TEXT("Spawn reservation was already finalized or released."); return false; }
    Memo->Held.Reset();
    Reservation.Reset();
    Error.Reset();
    return true;
}

bool ULDMapRegistry::WithdrawSpawnRequest(const FLDSpawnRequestKey& Request, const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (!SpawnState || SpawnState->Match!=Request.Match || !Request.IsValid() || !SpawnPermission(*GetWorld(),Request.Match,Permit,Error))
    {
        if (Error.IsEmpty()) { Error=TEXT("A spawn request is withdrawn only in its current match, under the current permit."); }
        return false;
    }
    auto* Memo=SpawnState->Requests.Find(Request.ContenderId);
    // The attempt failed before it recorded a selection under this identity: there is nothing to withdraw.
    if (!Memo || Memo->Request.Sequence<Request.Sequence) { Error.Reset(); return true; }
    if (!(Memo->Request==Request) || Memo->Finalized || Memo->Held)
    {
        Error=TEXT("Only the latest unfinalized, released spawn request of a contender can be withdrawn.");
        return false;
    }
    Memo->Withdrawn=true;
    Error.Reset();
    return true;
}

bool ULDMapRegistry::FinalizeSpawnPlacement(TSharedPtr<const FLDSpawnReservation>& Reservation,
    const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (!Reservation || Reservation->Owner!=FObjectKey(this) || !SpawnState
        || SpawnState->Match!=Reservation->Key.Match || !SpawnPermission(*GetWorld(),Reservation->Key.Match,Permit,Error))
    { if (Error.IsEmpty()) { Error=TEXT("Committed spawn lost its reservation binding."); } return false; }
    auto* Memo=SpawnState->Requests.Find(Reservation->Key.ContenderId);
    if (!Memo || Memo->Finalized || Memo->Held!=Reservation)
    { Error=TEXT("Committed spawn has no uniquely held reservation."); return false; }
    Memo->Finalized=true;
    Memo->Held.Reset();
    Reservation.Reset();
    Error.Reset();
    return true;
}
