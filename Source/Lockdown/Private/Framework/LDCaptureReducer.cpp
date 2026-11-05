#include "Framework/LDCaptureReducer.h"

namespace
{
    constexpr int32 MaxPendingCaptureFacts = 4096;
}

bool FLDCaptureReducer::Begin(int64 OpenAtMs, int64 SprayMs, int64 GuardMs, FString& Error)
{
    Reset();
    if (OpenAtMs < 0 || SprayMs <= 0 || GuardMs <= 0)
    {
        Error = TEXT("Spray timing requires a nonnegative opening and positive approved spray and guard durations.");
        return false;
    }
    bActive = true;
    OpenAt = OpenAtMs;
    Spray = SprayMs;
    Guard = GuardMs;
    Error.Reset();
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS
bool FLDCaptureReducer::DevOpenAt(int64 AtMs, FString& Error)
{
    if (!bActive || bOpen)
    {
        Error = bActive ? TEXT("The flagpole is already open.") : TEXT("The spray reducer is not active.");
        return false;
    }
    if (AtMs <= ClosedThrough || AtMs >= OpenAt)
    {
        Error = FString::Printf(TEXT("An early opening at %lld ms must follow the closed boundary %lld and precede the scheduled %lld."),
            AtMs, ClosedThrough, OpenAt);
        return false;
    }
    OpenAt = AtMs;
    Error.Reset();
    return true;
}
#endif

void FLDCaptureReducer::Reset()
{
    *this = FLDCaptureReducer();
}

int64 FLDCaptureReducer::NextDueMs() const
{
    if (!bActive) { return MAX_int64; }
    if (!bOpen) { return OpenAt; }
    if (State == ELDCircleState::Solo) { return DueAt; }
    return IsGuardRunning() ? GuardDue : MAX_int64;
}

int64 FLDCaptureReducer::CompletionInsideMs(int64 AtMs) const
{
    // Only a fact strictly inside the step can resume a guard that then finishes before the step's end.
    if (!bActive || AtMs <= ClosedThrough
        || !Pending.ContainsByPredicate([AtMs](const FLDCaptureEvent& Event) { return Event.Time.BucketMs < AtMs; }))
    { return MAX_int64; }
    FLDCaptureReducer Probe = *this;
    TOptional<FLDCaptureProposal> Proposal;
    FString Ignored;
    return Probe.Reduce(AtMs, true, Proposal, Ignored) && Proposal && Proposal->DueAtMs < AtMs ? Proposal->DueAtMs : MAX_int64;
}

bool FLDCaptureReducer::IsCommittedAliveBody(const FLDCombatBodyRef& Body) const
{
    const FMember* Member = Committed.Find(Body.Victim.EntityId);
    return Member && Member->Phase == ELDLifePhase::Alive && Member->Body == Body;
}

bool FLDCaptureReducer::IsCommittedMember(const FGuid& ContenderId) const
{
    const FMember* Member = Committed.Find(ContenderId);
    return Member && Member->Phase != ELDLifePhase::Left;
}

bool FLDCaptureReducer::Transition(TMap<FGuid, FMember>& Members, const FLDCaptureEvent& Event,
    bool bTimeOrdered, FString& Error)
{
    const FGuid& Id = Event.Body.Victim.EntityId;
    if (Event.Body.Victim.Kind != ELDVictimKind::Contender || !Id.IsValid())
    {
        Error = TEXT("Spray facts belong to contender bodies only.");
        return false;
    }
    FMember* Member = Members.Find(Id);
    switch (Event.Kind)
    {
    case ELDCaptureEventKind::Joined:
        if (Member || !Event.Body.IsValid() || Event.ProtectedUntilMs != 0)
        {
            Error = TEXT("A contender joins the spray roster once, unprotected, with a valid first body.");
            return false;
        }
        Members.Add(Id, {Event.Body, ELDLifePhase::Alive, false, 0, Event.bInside});
        return true;
    case ELDCaptureEventKind::Respawned:
        if (!Member || Member->Phase != ELDLifePhase::Dead || !Event.Body.IsValid()
            || Event.Body.Victim.Life <= Member->Body.Victim.Life || Event.ProtectedUntilMs <= Event.Time.BucketMs)
        {
            Error = TEXT("Respawn must follow a committed death with a newer life and a future protection end.");
            return false;
        }
        *Member = {Event.Body, ELDLifePhase::Alive, true, Event.ProtectedUntilMs, Event.bInside};
        return true;
    case ELDCaptureEventKind::ProtectionEnded:
        if (!Member || Member->Phase != ELDLifePhase::Alive || Member->Body != Event.Body || !Member->bProtected
            || Event.Time.BucketMs > Member->ProtectedUntilMs)
        {
            Error = TEXT("Protection ends once, for the current protected life, no later than its deadline.");
            return false;
        }
        Member->bProtected = false;
        return true;
    case ELDCaptureEventKind::Presence:
    case ELDCaptureEventKind::Interrupted:
        if (!Member || Member->Body != Event.Body || Member->Phase != ELDLifePhase::Alive)
        {
            // In real-time order a life may already be retired at an instant that precedes
            // the end of its last accepted motion; its later samples carry no eligibility.
            if (bTimeOrdered && Member && Member->Body.Victim.Life >= Event.Body.Victim.Life) { return true; }
            Error = TEXT("Motion presence and interruptions must belong to the current committed living body.");
            return false;
        }
        if (Event.Kind == ELDCaptureEventKind::Presence) { Member->bInside = Event.bInside; }
        return true;
    case ELDCaptureEventKind::Died:
        if (!Member || Member->Phase != ELDLifePhase::Alive || Member->Body != Event.Body)
        {
            Error = TEXT("A death ends the current committed living body exactly once.");
            return false;
        }
        Member->Phase = ELDLifePhase::Dead;
        Member->bProtected = false;
        Member->bInside = false;
        return true;
    case ELDCaptureEventKind::Left:
        if (!Member || Member->Phase == ELDLifePhase::Left)
        {
            Error = TEXT("Only a present spray contender can leave.");
            return false;
        }
        Member->Phase = ELDLifePhase::Left;
        Member->bProtected = false;
        Member->bInside = false;
        return true;
    default:
        Error = TEXT("Unknown spray fact.");
        return false;
    }
}

bool FLDCaptureReducer::Enqueue(const FLDCaptureEvent& Event, FString& Error)
{
    if (!bActive || Event.Kind == ELDCaptureEventKind::Open)
    {
        Error = TEXT("Spray facts require an active match and cannot schedule the opening.");
        return false;
    }
    if (!FMath::IsFinite(Event.Time.Seconds) || Event.Time.Seconds < 0 || Event.Time.BucketMs < 0
        || Event.Time.BucketMs <= ClosedThrough)
    {
        Error = TEXT("Spray fact is not after the last closed authority boundary.");
        return false;
    }
    if (Pending.Num() >= MaxPendingCaptureFacts || NextOrdinal == MAX_uint64)
    {
        Error = TEXT("Spray fact backlog exhausted; the boundary cannot be completed.");
        return false;
    }
    if (!Transition(Committed, Event, false, Error)) { return false; }
    FLDCaptureEvent& Stored = Pending.Add_GetRef(Event);
    Stored.Ordinal = ++NextOrdinal;
    Error.Reset();
    return true;
}

bool FLDCaptureReducer::Evaluate(const FLDCaptureEventTime& At, const FGuid& Interrupted, FString& Error)
{
    if (!bOpen) { return true; }
    const int64 Now = At.BucketMs;
    if ((State == ELDCircleState::Solo && Now > DueAt) || (IsGuardRunning() && Now > GuardDue))
    {
        Error = TEXT("The authority driver skipped a spray or guard completion boundary.");
        return false;
    }
    // The owner's death or departure clears his graffiti at once.
    const FMember* OwnerMember = Owner.IsValid() ? Reduced.Find(Owner) : nullptr;
    const bool bOwnerGone = Owner.IsValid() && (!OwnerMember || OwnerMember->Phase != ELDLifePhase::Alive);
    const FGuid NextOwner = bOwnerGone ? FGuid() : Owner;
    FGuid Only;
    int32 Count = 0;
    bool bHolderEffective = false;
    for (const auto& Pair : Reduced)
    {
        const FMember& Member = Pair.Value;
        if (Member.Phase != ELDLifePhase::Alive || Member.bProtected || !Member.bInside) { continue; }
        if (++Count == 1) { Only = Pair.Key; }
        bHolderEffective |= Pair.Key == Occupier;
    }
    const ELDCircleState Next = Count == 0 ? ELDCircleState::Empty
        : Count == 1 ? (Only == NextOwner ? ELDCircleState::Guarded : ELDCircleState::Solo) : ELDCircleState::Contested;
    // Exit, death, departure, protection or an interruption ends the holder's progress; nobody inherits it.
    const bool bInterrupt = Interrupted.IsValid() && Interrupted == Occupier;
    FGuid NextHolder = bHolderEffective && !bInterrupt ? Occupier : FGuid();
    int64 NextPaused = NextHolder.IsValid() ? Paused : 0;
    int64 NextSince = 0;
    int64 NextDue = 0;
    bool bResumed = false;
    if (Next == ELDCircleState::Solo)
    {
        if (State == ELDCircleState::Solo && NextHolder == Only)
        {
            NextSince = SoloSince;
            NextDue = DueAt;
            NextPaused = 0;
        }
        else
        {
            // The paused holder resumes from his frozen progress; any other sole sprayer starts at zero.
            const int64 Resumed = NextHolder == Only ? NextPaused : 0;
            bResumed = Resumed > 0;
            NextHolder = Only;
            NextSince = Now - Resumed;
            NextPaused = 0;
            if (!LDMatchLifecycle::AddDurationMs(NextSince, Spray, NextDue, Error)) { return false; }
        }
    }
    else if (Next == ELDCircleState::Contested)
    {
        if (State == ELDCircleState::Solo && NextHolder.IsValid()) { NextPaused = Now - SoloSince; }
        if (NextPaused <= 0)
        {
            NextHolder.Invalidate();
            NextPaused = 0;
        }
    }
    else
    {
        NextHolder.Invalidate();
        NextPaused = 0;
    }
    // The guard pauses only while a sole sprayer accumulates and continues from the paused value afterwards.
    int64 NextGuardDue = 0;
    int64 NextGuardKept = 0;
    if (NextOwner.IsValid())
    {
        const bool bWasRunning = IsGuardRunning();
        const bool bRuns = Next != ELDCircleState::Solo;
        if (bWasRunning && bRuns) { NextGuardDue = GuardDue; }
        else if (!bWasRunning && !bRuns) { NextGuardKept = GuardKept; }
        else if (bWasRunning) { NextGuardKept = Now - (GuardDue - Guard); }
        else if (!LDMatchLifecycle::AddDurationMs(Now - GuardKept, Guard, NextGuardDue, Error)) { return false; }
    }
    if (Next == State && NextHolder == Occupier && NextSince == SoloSince && NextDue == DueAt && NextPaused == Paused
        && NextOwner == Owner && NextGuardDue == GuardDue && NextGuardKept == GuardKept)
    { return true; }
    if (Epoch == MAX_uint64)
    {
        Error = TEXT("Spray epoch exhausted.");
        return false;
    }
    ++Epoch;
#if WITH_DEV_AUTOMATION_TESTS
    if (Next == ELDCircleState::Solo && (State != ELDCircleState::Solo || NextHolder != Occupier || NextSince != SoloSince))
    {
        FLDCaptureDevTally::FHolder& Holder = DevCounts.Holders.FindOrAdd(NextHolder);
        ++(bResumed ? Holder.Resumes : Holder.Attempts);
    }
    else if (Next == ELDCircleState::Contested && State != ELDCircleState::Contested)
    {
        ++DevCounts.Contests;
        if (NextHolder.IsValid()) { ++DevCounts.Holders.FindOrAdd(NextHolder).Paused; }
    }
    if (Occupier.IsValid() && (NextHolder != Occupier || bInterrupt))
    {
        FLDCaptureDevTally::FHolder& Lost = DevCounts.Holders.FindOrAdd(Occupier);
        ++Lost.Lost;
        Lost.Interrupted += bInterrupt ? 1 : 0;
    }
    if (bOwnerGone)
    {
        ++(OwnerMember && OwnerMember->Phase == ELDLifePhase::Dead ? DevCounts.ClearedByDeath : DevCounts.ClearedByLeave);
        DevCounts.GraffitiSinceMs = -1;
    }
#else
    (void)bResumed;
#endif
    State = Next;
    Occupier = NextHolder;
    SoloSince = NextSince;
    DueAt = NextDue;
    Paused = NextPaused;
    Owner = NextOwner;
    GuardDue = NextGuardDue;
    GuardKept = NextGuardKept;
    return true;
}

bool FLDCaptureReducer::CompleteSpray(FString& Error)
{
    if (Epoch == MAX_uint64)
    {
        Error = TEXT("Spray epoch exhausted.");
        return false;
    }
    // The sole sprayer paints over any graffiti and his own guard starts from zero at the spray instant.
    const int64 At = DueAt;
    int64 NextGuardDue = 0;
    if (!LDMatchLifecycle::AddDurationMs(At, Guard, NextGuardDue, Error)) { return false; }
    ++Epoch;
#if WITH_DEV_AUTOMATION_TESTS
    ++DevCounts.Sprays;
    ++DevCounts.Holders.FindOrAdd(Occupier).Sprays;
    DevCounts.Repaints += Owner.IsValid() ? 1 : 0;
    DevCounts.GraffitiSinceMs = At;
#endif
    Owner = Occupier;
    GuardDue = NextGuardDue;
    GuardKept = 0;
    // He was the only effective contender inside; as the owner he no longer sprays.
    State = ELDCircleState::Guarded;
    Occupier.Invalidate();
    SoloSince = 0;
    DueAt = 0;
    Paused = 0;
    return true;
}

bool FLDCaptureReducer::Advance(int64 Limit, bool bInclusive, bool bCompleteInside, bool& bCompleted, FString& Error)
{
    while (bOpen && State == ELDCircleState::Solo && (DueAt < Limit || (bInclusive && DueAt == Limit)))
    {
        if (!CompleteSpray(Error)) { return false; }
    }
    if (bOpen && IsGuardRunning() && GuardDue < Limit)
    {
        if (!bCompleteInside)
        {
            Error = TEXT("The authority driver skipped the guard completion boundary.");
            return false;
        }
        bCompleted = true;
    }
    return true;
}

bool FLDCaptureReducer::CloseBoundary(int64 AtMs, TOptional<FLDCaptureProposal>& Proposal, FString& Error)
{
    return Reduce(AtMs, false, Proposal, Error);
}

bool FLDCaptureReducer::CloseThroughCompletion(int64 AtMs, TOptional<FLDCaptureProposal>& Proposal, FString& Error)
{
    if (!Reduce(AtMs, true, Proposal, Error)) { return false; }
    if (!Proposal || Proposal->DueAtMs >= AtMs)
    {
        Proposal.Reset();
        Error = TEXT("The guard completion planned inside this step changed after its motion prefix.");
        return false;
    }
    return true;
}

bool FLDCaptureReducer::Reduce(int64 AtMs, bool bCompleteInside, TOptional<FLDCaptureProposal>& Proposal, FString& Error)
{
    Proposal.Reset();
    if (!bActive || AtMs <= ClosedThrough)
    {
        Error = TEXT("Spray boundary is inactive, repeated or regressing.");
        return false;
    }
    TArray<FLDCaptureEvent> Batch;
    for (int32 Index = 0; Index < Pending.Num();)
    {
        if (Pending[Index].Time.BucketMs <= AtMs)
        {
            Batch.Add(MoveTemp(Pending[Index]));
            Pending.RemoveAt(Index, EAllowShrinking::No);
        }
        else { ++Index; }
    }
    if (!bOpen && OpenAt <= AtMs)
    {
        FLDCaptureEvent& Open = Batch.AddDefaulted_GetRef();
        Open.Kind = ELDCaptureEventKind::Open;
        Open.Phase = ELDCommitPhase::Session;
        if (!FLDCaptureEventTime::FromMs(OpenAt, Open.Time, Error)) { return false; }
    }
    Batch.StableSort([](const FLDCaptureEvent& Left, const FLDCaptureEvent& Right)
    {
        if (Left.Time.Seconds != Right.Time.Seconds) { return Left.Time.Seconds < Right.Time.Seconds; }
        if (Left.Phase != Right.Phase) { return uint8(Left.Phase) < uint8(Right.Phase); }
        return Left.Ordinal < Right.Ordinal;
    });
    bool bCompleted = false;
    for (const FLDCaptureEvent& Event : Batch)
    {
        // A spray or guard due before this fact happened at its due instant; facts at that very instant (deaths,
        // departures, controls, exits) are reduced first, so nothing later can rewrite it.
        if (!Advance(Event.Time.BucketMs, false, bCompleteInside, bCompleted, Error)) { return false; }
        if (bCompleted) { break; }
        if (Event.Time.Seconds < LastReducedSeconds)
        {
            Error = TEXT("Spray fact regresses before an instant that was already reduced.");
            return false;
        }
        LastReducedSeconds = Event.Time.Seconds;
        FGuid Interrupted;
        if (Event.Kind == ELDCaptureEventKind::Open) { bOpen = true; }
        else
        {
            if (!Transition(Reduced, Event, true, Error)) { return false; }
            const FMember* Target = Event.Kind == ELDCaptureEventKind::Interrupted ? Reduced.Find(Event.Body.Victim.EntityId) : nullptr;
            // A retired life's late interruption touches nobody's progress.
            if (Target && Target->Body == Event.Body && Target->Phase == ELDLifePhase::Alive) { Interrupted = Event.Body.Victim.EntityId; }
        }
        if (!Evaluate(Event.Time, Interrupted, Error)) { return false; }
    }
    if (!bCompleted && !Advance(AtMs, true, bCompleteInside, bCompleted, Error)) { return false; }
    const int64 ReducedThrough = bCompleted ? GuardDue : AtMs;
    for (const auto& Pair : Reduced)
    {
        if (Pair.Value.Phase == ELDLifePhase::Alive && Pair.Value.bProtected && Pair.Value.ProtectedUntilMs <= ReducedThrough)
        {
            Error = TEXT("Protection reached its deadline without a published ProtectionEnded fact.");
            return false;
        }
    }
    ClosedThrough = AtMs;
    if (bCompleted || (IsGuardRunning() && GuardDue == AtMs))
    {
        Proposal = FLDCaptureProposal{Epoch, Owner, GuardDue - Guard, GuardDue};
    }
    Error.Reset();
    return true;
}
