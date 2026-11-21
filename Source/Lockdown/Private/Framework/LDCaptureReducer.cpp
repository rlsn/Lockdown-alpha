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

bool FLDCaptureReducer::ScheduleOpen(int64 AtMs, FString& Error)
{
    if (!bActive || bOpen)
    {
        Error = bActive ? TEXT("The flagpole is already open.") : TEXT("The spray reducer is not active.");
        return false;
    }
    if (AtMs <= ClosedThrough || AtMs >= OpenAt)
    {
        Error = FString::Printf(TEXT("An opening at %lld ms must follow the closed boundary %lld and precede the current schedule %lld."),
            AtMs, ClosedThrough, OpenAt);
        return false;
    }
    OpenAt = AtMs;
    Error.Reset();
    return true;
}

void FLDCaptureReducer::Reset()
{
    *this = FLDCaptureReducer();
}

bool FLDCaptureReducer::SetFinalRules(const FLDCaptureFinalRules& InRules, FString& Error)
{
    if (!bActive || bOpen)
    {
        Error = TEXT("Final mods are fixed before the flagpole opens.");
        return false;
    }
    for (const auto& Pair : InRules.Mods)
    {
        if (!Pair.Key.IsValid()
            || (Pair.Value == ELDDemoFinalModId::FinalHitAndRun && (InRules.HitAndRunSprays < 2 || InRules.HitAndRunFadeMs <= 0))
            || (Pair.Value == ELDDemoFinalModId::FinalKillSeconds && InRules.KillGuardMs <= 0))
        {
            Error = TEXT("A final mod holder needs a contender and the approved terms of its card.");
            return false;
        }
    }
    Rules = InRules;
    Error.Reset();
    return true;
}

void FLDCaptureReducer::SetShowdownRatchets(const TArray<int64>& InSprayCheckpointsMs,
    const TArray<int64>& InGuardCheckpointsMs, int64 InBaseKillGuardMs)
{
    SprayCheckpointsMs = InSprayCheckpointsMs;
    GuardCheckpointsMs = InGuardCheckpointsMs;
    BaseKillGuardMs = FMath::Max<int64>(0, InBaseKillGuardMs);
}

void FLDCaptureReducer::UpdateSprayLatch(const FGuid& ContenderId, int64 ProgressMs)
{
    if (!ContenderId.IsValid() || ProgressMs <= 0) { return; }
    for (const int64 Checkpoint : SprayCheckpointsMs)
    {
        if (Checkpoint > 0 && Checkpoint < Spray && ProgressMs >= Checkpoint)
        {
            int64& Stored = SprayLatchedMs.FindOrAdd(ContenderId);
            Stored = FMath::Max(Stored, Checkpoint);
        }
    }
}

void FLDCaptureReducer::UpdateGuardLatch(int64 ProgressMs)
{
    if (ProgressMs <= 0) { return; }
    for (const int64 Checkpoint : GuardCheckpointsMs)
    {
        if (Checkpoint > 0 && Checkpoint < Guard && ProgressMs >= Checkpoint)
        {
            GuardLatchedMs = FMath::Max(GuardLatchedMs, Checkpoint);
        }
    }
}

bool FLDCaptureReducer::BeginPrivate(const FLDCaptureReducer& Main, const FGuid& Holder, int64 SprayMs, int64 GuardMs, FString& Error)
{
    if (!Main.bActive || Main.bOpen || !Holder.IsValid() || !Main.Committed.Contains(Holder))
    {
        Error = TEXT("A private flagpole starts from the active, unopened main roster of its holder.");
        return false;
    }
    if (!Begin(Main.OpenAt, SprayMs, GuardMs, Error)) { return false; }
    for (const auto& Pair : Main.Committed)
    {
        FMember Member = Pair.Value;
        Member.bInside = false;
        Committed.Add(Pair.Key, Member);
        Reduced.Add(Pair.Key, Member);
    }
    ClosedThrough = Main.ClosedThrough;
    LastReducedSeconds = Main.LastReducedSeconds;
    OnlySprayer = Holder;
    SprayCheckpointsMs = Main.SprayCheckpointsMs;
    GuardCheckpointsMs = Main.GuardCheckpointsMs;
    BaseKillGuardMs = Main.BaseKillGuardMs;
    Error.Reset();
    return true;
}

ELDDemoFinalModId FLDCaptureReducer::ModOf(const FGuid& ContenderId) const
{
    const ELDDemoFinalModId* Mod = Rules.Mods.Find(ContenderId);
    return Mod ? *Mod : ELDDemoFinalModId(0);
}

ELDLifePhase FLDCaptureReducer::GetReducedPhase(const FGuid& ContenderId) const
{
    const FMember* Member = Reduced.Find(ContenderId);
    return Member ? Member->Phase : ELDLifePhase::Unspawned;
}

bool FLDCaptureReducer::CanSpray(const FGuid& Id) const
{
    return (!OnlySprayer.IsValid() || Id == OnlySprayer) && !MustLeave.Contains(Id);
}

void FLDCaptureReducer::Win(ELDCaptureWin Kind, const FGuid& Winner, int64 AtMs)
{
    FLDCaptureProposal& Row = Wins.AddDefaulted_GetRef();
    Row.Winner = Winner;
    Row.Kind = Kind;
    Row.DueAtMs = AtMs;
}

TOptional<FLDCaptureProposal> FLDCaptureReducer::Settle(const TArray<FLDCaptureProposal>& Candidates)
{
    TOptional<FLDCaptureProposal> Best;
    bool bTied = false;
    for (const FLDCaptureProposal& Row : Candidates)
    {
        const bool bBetter = !Best || Row.DueAtMs < Best->DueAtMs
            || (Row.DueAtMs == Best->DueAtMs && uint8(Row.Kind) < uint8(Best->Kind));
        if (bBetter)
        {
            Best = Row;
            bTied = false;
        }
        else if (Row.DueAtMs == Best->DueAtMs && Row.Kind == Best->Kind && Row.Winner != Best->Winner) { bTied = true; }
    }
    // Two different contenders reaching the same condition at the same instant: nobody wins.
    if (Best && bTied) { Best->Winner.Invalidate(); }
    return Best;
}

int64 FLDCaptureReducer::NextDueMs() const
{
    if (!bActive) { return MAX_int64; }
    if (!bOpen) { return OpenAt; }
    int64 Due = State == ELDCircleState::Solo ? DueAt : MAX_int64;
    if (IsGuardRunning()) { Due = FMath::Min(Due, GuardDue); }
    if (FadeAt > 0) { Due = FMath::Min(Due, FadeAt); }
    return Due;
}

int64 FLDCaptureReducer::CompletionInsideMs(int64 AtMs) const
{
    // Only a fact strictly inside the step can resume a paused or frozen guard that then finishes before the step's end.
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

bool FLDCaptureReducer::IsCommittedInside(const FLDCombatBodyRef& Body) const
{
    const FMember* Member = Committed.Find(Body.Victim.EntityId);
    return Member && Member->Phase == ELDLifePhase::Alive && Member->Body == Body && Member->bInside;
}

bool FLDCaptureReducer::IsCommittedMember(const FGuid& ContenderId) const
{
    const FMember* Member = Committed.Find(ContenderId);
    return Member && Member->Phase != ELDLifePhase::Left;
}

bool FLDCaptureReducer::AccountGuard(int64 ThroughMs, FString& Error)
{
    if (!IsGuardRunning()) { return true; }
    const int64 End = FMath::Min(ThroughMs, GuardDue);
    const int64 Added = FMath::Max<int64>(0, End - GuardAccountedThroughMs);
    int64& Banked = BankedGuard.FindOrAdd(Owner);
    if (Banked > MAX_int64 - Added)
    {
        Error = TEXT("Historical guard contribution overflowed.");
        return false;
    }
    Banked += Added;
    GuardAccountedThroughMs = FMath::Max(GuardAccountedThroughMs, End);
    return true;
}

int64 FLDCaptureReducer::GetEffectiveGuardMs(const FGuid& ContenderId, int64 AtMs) const
{
    if (!ContenderId.IsValid() || Guard <= 0) { return 0; }
    int64 Best = FMath::Clamp<int64>(BankedGuard.FindRef(ContenderId), 0, Guard);
    if (Owner == ContenderId && FadeAt == 0)
    {
        const int64 Current = IsGuardRunning()
            ? FMath::Clamp<int64>(AtMs - (GuardDue - Guard), 0, Guard)
            : FMath::Clamp<int64>(GuardKept, 0, Guard);
        Best = FMath::Max(Best, Current);
    }
    return Best;
}

FGuid FLDCaptureReducer::ResolveShowdownTimeoutWinner(const TArray<FLDShowdownTimeoutCandidate>& Candidates)
{
    const FLDShowdownTimeoutCandidate* Best = nullptr;
    bool bTied = false;
    auto Compare = [](const FLDShowdownTimeoutCandidate& Left, const FLDShowdownTimeoutCandidate& Right) -> int32
    {
        const int64 LeftTarget = FMath::Max<int64>(1, Left.GuardTargetMs);
        const int64 RightTarget = FMath::Max<int64>(1, Right.GuardTargetMs);
        const int64 LeftNum = FMath::Clamp<int64>(Left.GuardProgressMs, 0, LeftTarget) * RightTarget;
        const int64 RightNum = FMath::Clamp<int64>(Right.GuardProgressMs, 0, RightTarget) * LeftTarget;
        if (LeftNum != RightNum) { return LeftNum > RightNum ? 1 : -1; }
        if (Left.ShowdownKills != Right.ShowdownKills) { return Left.ShowdownKills > Right.ShowdownKills ? 1 : -1; }
        if (Left.ShowdownDamageP2 != Right.ShowdownDamageP2) { return Left.ShowdownDamageP2 > Right.ShowdownDamageP2 ? 1 : -1; }
        if (Left.CompletedSprays != Right.CompletedSprays) { return Left.CompletedSprays > Right.CompletedSprays ? 1 : -1; }
        return 0;
    };
    int32 EligibleCount = 0;
    FGuid SoleEligible;
    for (const FLDShowdownTimeoutCandidate& Row : Candidates)
    {
        if (!Row.bEligible || !Row.Contender.IsValid()) { continue; }
        ++EligibleCount;
        SoleEligible = Row.Contender;
        if (Row.GuardProgressMs <= 0 && Row.ShowdownKills <= 0 && Row.ShowdownDamageP2 <= 0 && Row.CompletedSprays == 0)
        {
            continue;
        }
        if (!Best)
        {
            Best = &Row;
            bTied = false;
            continue;
        }
        const int32 Order = Compare(Row, *Best);
        if (Order > 0)
        {
            Best = &Row;
            bTied = false;
        }
        else if (Order == 0)
        {
            bTied = true;
        }
    }
    if (Best) { return bTied ? FGuid() : Best->Contender; }
    return EligibleCount == 1 ? SoleEligible : FGuid();
}

FGuid FLDCaptureReducer::TimeoutWinner() const
{
    FGuid Best;
    int64 BestGuard = 0;
    uint32 BestSprays = 0;
    bool bTied = false;
    for (const auto& Pair : Reduced)
    {
        if (Pair.Value.Phase != ELDLifePhase::Alive || Pair.Value.bProtected) { continue; }
        const int64 Banked = GetBankedGuardMs(Pair.Key);
        const uint32 Sprays = GetCompletedSprays(Pair.Key);
        if (Banked == 0 && Sprays == 0) { continue; }
        if (!Best.IsValid() || Banked > BestGuard || (Banked == BestGuard && Sprays > BestSprays))
        {
            Best = Pair.Key;
            BestGuard = Banked;
            BestSprays = Sprays;
            bTied = false;
        }
        else if (Banked == BestGuard && Sprays == BestSprays) { bTied = true; }
    }
    return bTied ? FGuid() : Best;
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
        // #198: the first field life starts at the R0 return with the shared return protection.
        if (Member || !Event.Body.IsValid() || (Event.ProtectedUntilMs != 0 && Event.ProtectedUntilMs <= Event.Time.BucketMs))
        {
            Error = TEXT("A contender joins the spray roster once, with a valid first body and a future or no protection end.");
            return false;
        }
        Members.Add(Id, {Event.Body, ELDLifePhase::Alive, Event.ProtectedUntilMs != 0, Event.ProtectedUntilMs, Event.bInside});
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
    if (!AccountGuard(Now, Error)) { return false; }
    if (Occupier.IsValid())
    {
        UpdateSprayLatch(Occupier, State == ELDCircleState::Solo ? Now - SoloSince : Paused);
    }
    // A hit-and-run sprayer may spray again only after leaving the zone (or the life).
    for (auto It = MustLeave.CreateIterator(); It; ++It)
    {
        const FMember* Member = Reduced.Find(*It);
        if (!Member || Member->Phase != ELDLifePhase::Alive || !Member->bInside) { It.RemoveCurrent(); }
    }
    const FGuid NextOwner = Owner;
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
        : Count == 1 ? (Only == NextOwner ? ELDCircleState::Guarded : CanSpray(Only) ? ELDCircleState::Solo : ELDCircleState::Contested)
        : ELDCircleState::Contested;
    // Exit, death, departure, protection or an interruption ends the holder's progress above its latched ratchet floor.
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
            // The paused holder resumes from his frozen progress; any sole sprayer keeps his latched 3-step ratchet floor.
            const int64 Floor = SprayLatchedMs.FindRef(Only);
            const int64 Resumed = NextHolder == Only ? FMath::Max(NextPaused, Floor) : Floor;
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
        else if (bInterrupt && bHolderEffective)
        {
            const int64 Floor = SprayLatchedMs.FindRef(Occupier);
            if (Floor > 0)
            {
                NextHolder = Occupier;
                NextPaused = Floor;
            }
        }
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
    int64 NextGuardDue = 0;
    int64 NextGuardKept = 0;
    if (NextOwner.IsValid() && FadeAt == 0)
    {
        const bool bWasRunning = IsGuardRunning();
        const bool bRuns = GuardRunsIn(Next);
        const int64 CurrentGuard = bWasRunning ? Now - (GuardDue - Guard) : GuardKept;
        UpdateGuardLatch(CurrentGuard);
        if (bWasRunning && bRuns) { NextGuardDue = GuardDue; }
        else if (!bWasRunning && !bRuns) { NextGuardKept = FMath::Max(GuardLatchedMs, GuardKept); }
        else if (bWasRunning) { NextGuardKept = FMath::Max(GuardLatchedMs, CurrentGuard); }
        else if (!LDMatchLifecycle::AddDurationMs(Now - FMath::Max(GuardLatchedMs, GuardKept), Guard, NextGuardDue, Error)) { return false; }
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
    GuardAccountedThroughMs = Now;
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
    const FGuid Sprayer = Occupier;
    if (!AccountGuard(At, Error)) { return false; }
    uint32& Sprays = CompletedSprays.FindOrAdd(Occupier);
    if (Sprays == MAX_uint32)
    {
        Error = TEXT("Completed spray contribution overflowed.");
        return false;
    }
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
    ++Sprays;
    SprayLatchedMs.Reset();
    GuardLatchedMs = 0;
    GuardDue = NextGuardDue;
    GuardKept = 0;
    GuardAccountedThroughMs = At;
    FadeAt = 0;
    Cause = ELDGraffitiCause::Sprayed;
    if (ModOf(Sprayer) == ELDDemoFinalModId::FinalHitAndRun)
    {
        // A hit-and-run graffiti has no guard; it fades and its sprayer must leave the zone before spraying again.
        if (int64(Sprays) >= Rules.HitAndRunSprays) { Win(ELDCaptureWin::Sprays, Sprayer, At); }
        GuardDue = 0;
        if (!LDMatchLifecycle::AddDurationMs(At, Rules.HitAndRunFadeMs, FadeAt, Error)) { return false; }
        MustLeave.Add(Sprayer);
    }
    // He was the only effective contender inside; as the owner he no longer sprays.
    State = ELDCircleState::Guarded;
    Occupier.Invalidate();
    SoloSince = 0;
    DueAt = 0;
    Paused = 0;
    return true;
}

bool FLDCaptureReducer::Fade(FString& Error)
{
    if (Epoch == MAX_uint64)
    {
        Error = TEXT("Spray epoch exhausted.");
        return false;
    }
    FLDCaptureEventTime At;
    if (!FLDCaptureEventTime::FromMs(FadeAt, At, Error)) { return false; }
    ++Epoch;
    Owner.Invalidate();
    FadeAt = 0;
    GuardDue = 0;
    GuardKept = 0;
    GuardLatchedMs = 0;
    return Evaluate(At, FGuid(), Error);
}

bool FLDCaptureReducer::Advance(int64 Limit, bool bInclusive, bool bCompleteInside, bool& bCompleted, FString& Error)
{
    const auto Due = [Limit, bInclusive](int64 At) { return At < Limit || (bInclusive && At == Limit); };
    for (;;)
    {
        const int64 SprayDue = bOpen && State == ELDCircleState::Solo ? DueAt : MAX_int64;
        const int64 Faded = bOpen && FadeAt > 0 ? FadeAt : MAX_int64;
        const int64 Next = FMath::Min(SprayDue, Faded);
        // Nothing after a win of this boundary happens.
        if (Next == MAX_int64 || !Due(Next) || (!Wins.IsEmpty() && Next > Wins[0].DueAtMs)) { break; }
        if (!(SprayDue <= Faded ? CompleteSpray(Error) : Fade(Error))) { return false; }
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
    Wins.Reset();
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
    for (int32 Index = 0; Index < Batch.Num(); ++Index)
    {
        const FLDCaptureEvent& Event = Batch[Index];
        // A win decided this boundary at its instant: only facts of that same instant still count.
        if (!Wins.IsEmpty() && Event.Time.BucketMs > Wins[0].DueAtMs) { break; }
        // A spray or guard due before this fact happened at its due instant; facts at that very instant (deaths,
        // departures, controls, exits) are reduced first, so nothing later can rewrite it.
        if (!Advance(Event.Time.BucketMs, false, bCompleteInside, bCompleted, Error)) { return false; }
        if (bCompleted || (!Wins.IsEmpty() && Event.Time.BucketMs > Wins[0].DueAtMs)) { break; }
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
            if (Event.Kind == ELDCaptureEventKind::Died && !ApplyKill(Event, Error)) { return false; }
        }
        if (!Evaluate(Event.Time, Interrupted, Error)) { return false; }
        // Once every fact of this instant is in: a clear-out is judged, and a win whose contender died at this same
        // instant (whatever the order of the deaths) is withdrawn.
        if (bOpen && (Index + 1 == Batch.Num() || Batch[Index + 1].Time.Seconds != Event.Time.Seconds))
        {
            CheckClearOut(Event.Time.BucketMs);
            Wins.RemoveAll([this](const FLDCaptureProposal& Row) { return GetReducedPhase(Row.Winner) != ELDLifePhase::Alive; });
        }
    }
    const int64 Through = Wins.IsEmpty() ? AtMs : FMath::Min(AtMs, Wins[0].DueAtMs);
    if (!bCompleted && !Advance(Through, true, bCompleteInside, bCompleted, Error)) { return false; }
    const int64 ReducedThrough = bCompleted ? GuardDue : Through;
    if (!AccountGuard(ReducedThrough, Error)) { return false; }
    for (const auto& Pair : Reduced)
    {
        if (Pair.Value.Phase == ELDLifePhase::Alive && Pair.Value.bProtected && Pair.Value.ProtectedUntilMs <= ReducedThrough)
        {
            Error = TEXT("Protection reached its deadline without a published ProtectionEnded fact.");
            return false;
        }
    }
    ClosedThrough = AtMs;
    if (bCompleted || (IsGuardRunning() && GuardDue == ReducedThrough))
    {
        Wins.Add(FLDCaptureProposal{Epoch, Owner, ELDCaptureWin::Guard, GuardDue - Guard, GuardDue});
    }
    for (FLDCaptureProposal& Row : Wins) { Row.Epoch = Epoch; }
    Proposal = Settle(Wins);
    Wins.Reset();
    Error.Reset();
    return true;
}

bool FLDCaptureReducer::ApplyKill(const FLDCaptureEvent& Event, FString& Error)
{
    const FGuid& Victim = Event.Body.Victim.EntityId;
    const FGuid& Killer = Event.Killer;
    const FMember* Holder = Killer.IsValid() ? Reduced.Find(Killer) : nullptr;
    if (!bOpen || !Holder || Killer == Victim || Holder->Phase == ELDLifePhase::Left) { return true; }
    const int64 Now = Event.Time.BucketMs;
    if (ModOf(Killer) == ELDDemoFinalModId::FinalUsurp && Owner == Victim && Holder->Phase == ELDLifePhase::Alive)
    {
        if (!AccountGuard(Now, Error)) { return false; }
        if (Epoch == MAX_uint64)
        {
            Error = TEXT("Spray epoch exhausted.");
            return false;
        }
        ++Epoch;
        Owner = Killer;
        Cause = ELDGraffitiCause::Usurped;
        if (FadeAt != 0)
        {
            // A fading graffiti had no guard: the usurper's starts from zero.
            FadeAt = 0;
            GuardKept = 0;
            GuardLatchedMs = 0;
            if (GuardRunsIn(State) && !LDMatchLifecycle::AddDurationMs(Now, Guard, GuardDue, Error)) { return false; }
        }
#if WITH_DEV_AUTOMATION_TESTS
        DevCounts.GraffitiSinceMs = Now;
#endif
    }
    const int64 BonusGuardMs = (ModOf(Killer) == ELDDemoFinalModId::FinalKillSeconds ? Rules.KillGuardMs : 0)
        + (Owner == Killer && FadeAt == 0 ? BaseKillGuardMs : 0);
    if (BonusGuardMs <= 0) { return true; }
    if (!AccountGuard(Now, Error)) { return false; }
    int64& Banked = BankedGuard.FindOrAdd(Killer);
    if (Banked > MAX_int64 - BonusGuardMs)
    {
        Error = TEXT("Historical guard contribution overflowed.");
        return false;
    }
    Banked += BonusGuardMs;
    if (Owner != Killer || FadeAt != 0) { return true; }
    if (Epoch == MAX_uint64)
    {
        Error = TEXT("Spray epoch exhausted.");
        return false;
    }
    ++Epoch;
    if (IsGuardRunning())
    {
        // The kill moves the running guard's due instant earlier; reaching it now wins at this instant.
        GuardDue = FMath::Max(Now, GuardDue - BonusGuardMs);
        UpdateGuardLatch(Now - (GuardDue - Guard));
        if (GuardDue == Now) { Win(ELDCaptureWin::Guard, Killer, Now); }
        return true;
    }
    GuardKept = FMath::Min(Guard, GuardKept + BonusGuardMs);
    UpdateGuardLatch(GuardKept);
    if (GuardKept == Guard && Holder->Phase == ELDLifePhase::Alive) { Win(ELDCaptureWin::Guard, Killer, Now); }
    return true;
}

void FLDCaptureReducer::CheckClearOut(int64 AtMs)
{
    for (const auto& Pair : Rules.Mods)
    {
        const FMember* Holder = Pair.Value == ELDDemoFinalModId::FinalClearOut ? Reduced.Find(Pair.Key) : nullptr;
        if (!Holder || Holder->Phase != ELDLifePhase::Alive) { continue; }
        int32 Others = 0;
        bool bAllDown = true;
        for (const auto& Other : Reduced)
        {
            if (Other.Key == Pair.Key || Other.Value.Phase == ELDLifePhase::Left) { continue; }
            ++Others;
            bAllDown &= Other.Value.Phase == ELDLifePhase::Dead;
        }
        if (Others > 0 && bAllDown && !Wins.ContainsByPredicate([&Pair](const FLDCaptureProposal& Row)
            { return Row.Kind == ELDCaptureWin::ClearOut && Row.Winner == Pair.Key; }))
        { Win(ELDCaptureWin::ClearOut, Pair.Key, AtMs); }
    }
}
