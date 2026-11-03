#include "Framework/LDCaptureReducer.h"

namespace
{
    constexpr int32 MaxPendingCaptureFacts = 4096;
}

bool FLDCaptureReducer::Begin(int64 OpenAtMs, int64 HoldMs, FString& Error)
{
    Reset();
    if (OpenAtMs < 0 || HoldMs <= 0)
    {
        Error = TEXT("Capture timing requires a nonnegative opening and a positive approved hold.");
        return false;
    }
    bActive = true;
    OpenAt = OpenAtMs;
    Hold = HoldMs;
    Error.Reset();
    return true;
}

void FLDCaptureReducer::Reset()
{
    *this = FLDCaptureReducer();
}

int64 FLDCaptureReducer::NextDueMs() const
{
    if (!bActive) { return MAX_int64; }
    if (!bOpen) { return OpenAt; }
    if (State == ELDCircleState::Solo) { return DueAt; }
    if (State != ELDCircleState::Contested || !Occupier.IsValid()) { return MAX_int64; }
    // A resume in the next step happens no earlier than ClosedThrough + 1, so stopping here
    // keeps every resumed completion at or after the end of the step that resumes it.
    const int64 Remaining = FMath::Max<int64>(Hold - Paused, 1);
    return Remaining > MAX_int64 - ClosedThrough ? MAX_int64 : ClosedThrough + Remaining;
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
        Error = TEXT("Capture facts belong to contender bodies only.");
        return false;
    }
    FMember* Member = Members.Find(Id);
    switch (Event.Kind)
    {
    case ELDCaptureEventKind::Joined:
        if (Member || !Event.Body.IsValid() || Event.ProtectedUntilMs != 0)
        {
            Error = TEXT("A contender joins the capture roster once, unprotected, with a valid first body.");
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
        if (!Member || Member->Body != Event.Body || Member->Phase != ELDLifePhase::Alive)
        {
            // In real-time order a life may already be retired at an instant that precedes
            // the end of its last accepted motion; its later samples carry no eligibility.
            if (bTimeOrdered && Member && Member->Body.Victim.Life >= Event.Body.Victim.Life) { return true; }
            Error = TEXT("Motion presence must belong to the current committed living body.");
            return false;
        }
        Member->bInside = Event.bInside;
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
            Error = TEXT("Only a present capture contender can leave.");
            return false;
        }
        Member->Phase = ELDLifePhase::Left;
        Member->bProtected = false;
        Member->bInside = false;
        return true;
    default:
        Error = TEXT("Unknown capture fact.");
        return false;
    }
}

bool FLDCaptureReducer::Enqueue(const FLDCaptureEvent& Event, FString& Error)
{
    if (!bActive || Event.Kind == ELDCaptureEventKind::Open)
    {
        Error = TEXT("Capture facts require an active match and cannot schedule the opening.");
        return false;
    }
    if (!FMath::IsFinite(Event.Time.Seconds) || Event.Time.Seconds < 0 || Event.Time.BucketMs < 0
        || Event.Time.BucketMs <= ClosedThrough)
    {
        Error = TEXT("Capture fact is not after the last closed authority boundary.");
        return false;
    }
    if (Pending.Num() >= MaxPendingCaptureFacts || NextOrdinal == MAX_uint64)
    {
        Error = TEXT("Capture fact backlog exhausted; the boundary cannot be completed.");
        return false;
    }
    if (!Transition(Committed, Event, false, Error)) { return false; }
    FLDCaptureEvent& Stored = Pending.Add_GetRef(Event);
    Stored.Ordinal = ++NextOrdinal;
    Error.Reset();
    return true;
}

bool FLDCaptureReducer::Evaluate(const FLDCaptureEventTime& At, FString& Error)
{
    if (!bOpen) { return true; }
    const int64 Now = At.BucketMs;
    if (State == ELDCircleState::Solo && Now > DueAt)
    {
        Error = TEXT("The authority driver skipped the capture completion boundary.");
        return false;
    }
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
        : Count == 1 ? ELDCircleState::Solo : ELDCircleState::Contested;
    // Exit, death, departure or protection ends the holder's progress; nobody inherits it.
    FGuid NextHolder = bHolderEffective ? Occupier : FGuid();
    int64 NextPaused = bHolderEffective ? Paused : 0;
    int64 NextSince = 0;
    int64 NextDue = 0;
    if (Next == ELDCircleState::Solo)
    {
        if (State == ELDCircleState::Solo && NextHolder == Only) { return true; }
        // The paused holder resumes from his frozen progress; any other sole contender starts at zero.
        const int64 Resumed = NextHolder == Only ? NextPaused : 0;
        NextHolder = Only;
        NextSince = Now - Resumed;
        NextPaused = 0;
        if (!LDMatchLifecycle::AddDurationMs(NextSince, Hold, NextDue, Error)) { return false; }
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
    if (Next == State && NextHolder == Occupier) { return true; }
    if (Epoch == MAX_uint64)
    {
        Error = TEXT("Capture epoch exhausted.");
        return false;
    }
    ++Epoch;
    State = Next;
    Occupier = NextHolder;
    SoloSince = NextSince;
    DueAt = NextDue;
    Paused = NextPaused;
    return true;
}

bool FLDCaptureReducer::CloseBoundary(int64 AtMs, TOptional<FLDCaptureProposal>& Proposal, FString& Error)
{
    Proposal.Reset();
    if (!bActive || AtMs <= ClosedThrough)
    {
        Error = TEXT("Capture boundary is inactive, repeated or regressing.");
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
    for (const FLDCaptureEvent& Event : Batch)
    {
        if (Event.Time.Seconds < LastReducedSeconds)
        {
            Error = TEXT("Capture fact regresses before an instant that was already reduced.");
            return false;
        }
        LastReducedSeconds = Event.Time.Seconds;
        if (Event.Kind == ELDCaptureEventKind::Open) { bOpen = true; }
        else if (!Transition(Reduced, Event, true, Error)) { return false; }
        if (!Evaluate(Event.Time, Error)) { return false; }
    }
    for (const auto& Pair : Reduced)
    {
        if (Pair.Value.Phase == ELDLifePhase::Alive && Pair.Value.bProtected && Pair.Value.ProtectedUntilMs <= AtMs)
        {
            Error = TEXT("Protection reached its deadline without a published ProtectionEnded fact.");
            return false;
        }
    }
    if (State == ELDCircleState::Solo && DueAt < AtMs)
    {
        Error = TEXT("The authority driver skipped the capture completion boundary.");
        return false;
    }
    ClosedThrough = AtMs;
    if (State == ELDCircleState::Solo && DueAt == AtMs)
    {
        Proposal = FLDCaptureProposal{Epoch, Occupier, SoloSince, DueAt};
    }
    Error.Reset();
    return true;
}
