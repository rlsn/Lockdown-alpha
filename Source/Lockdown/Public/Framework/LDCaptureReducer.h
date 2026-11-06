#pragma once

#include "CoreMinimal.h"
#include "Framework/LDCaptureTypes.h"

enum class ELDCaptureEventKind : uint8 { Open, Joined, Respawned, ProtectionEnded, Presence, Died, Left, Interrupted };

struct FLDCaptureEvent
{
    ELDCaptureEventKind Kind = ELDCaptureEventKind::Presence;
    FLDCombatBodyRef Body;
    FLDCaptureEventTime Time;
    // Commit phase that produced the fact; it orders only exactly simultaneous instants.
    ELDCommitPhase Phase = ELDCommitPhase::Session;
    // Presence result, or the initial presence of a Joined/Respawned body.
    bool bInside = false;
    int64 ProtectedUntilMs = 0;
    uint64 Ordinal = 0;
};

#if WITH_DEV_AUTOMATION_TESTS
// DEV telemetry only, never read by a rule: every reduced change, including the ones inside one boundary that no
// public view shows. An attempt starts a holder's spray progress from zero, a resume continues his paused progress,
// a pause is a contest that froze it, an interruption is a root, stun, taunt or knockback that cleared it and a loss
// ends it without a graffiti (exit, death, departure or interruption).
struct FLDCaptureDevTally
{
    struct FHolder
    {
        uint32 Attempts = 0;
        uint32 Resumes = 0;
        uint32 Paused = 0;
        uint32 Lost = 0;
        uint32 Interrupted = 0;
        uint32 Sprays = 0;
    };
    // Entries into Contested, with or without a paused holder.
    uint32 Contests = 0;
    // Completed sprays; a repaint is a spray that covered another contender's graffiti.
    uint32 Sprays = 0;
    uint32 Repaints = 0;
    // Why graffiti left the flagpole without a win other than a repaint: its owner died or left the match.
    uint32 ClearedByDeath = 0;
    uint32 ClearedByLeave = 0;
    // Frozen guards reset to zero because their owner left the zone while a challenger was inside.
    uint32 GuardResets = 0;
    // Spray instant of the graffiti currently shown, -1 while the flagpole is bare.
    int64 GraffitiSinceMs = -1;
    TMap<FGuid, FHolder> Holders;
};
#endif

// A running guard that reached its full duration: Winner's graffiti stood since GuardSinceMs (the resume instant
// minus the kept guard progress) and wins at DueAtMs.
struct FLDCaptureProposal
{
    uint64 Epoch = 0;
    FGuid Winner;
    int64 GuardSinceMs = 0;
    int64 DueAtMs = 0;
};

// The unique spray and guard reducer (APP_SPEC 2.2). Facts are enqueued in commit order and reduced only when their
// integer boundary closes, in real time order, so intermediate enters, exits, deaths, interruptions and protection
// changes inside one boundary are never collapsed. A sole effective contender who is not the graffiti owner sprays;
// a contest pauses his spray progress, he resumes it as the sole sprayer again and loses it when he stops being an
// effective contender in the zone (exit, death, departure) or is interrupted (root, stun, taunt, knockback). Nobody
// inherits it. A completed spray paints over any graffiti and starts its owner's guard from zero. The guard freezes
// while any effective contender other than the owner is in the zone (sole sprayer or contest, with or without the
// owner) and continues from the frozen value once none is left; the owner may leave freely otherwise. An owner who
// was in the zone at an instant where the guard was frozen and is out of it when that instant ends, with the guard
// still frozen, resets it to zero (his graffiti stays). The owner's death or departure clears the graffiti.
// A guard that reaches its full duration is the only completion that ends the match.
class LOCKDOWN_API FLDCaptureReducer
{
public:
    bool Begin(int64 OpenAtMs, int64 SprayMs, int64 GuardMs, FString& Error);
    void Reset();
    bool IsActive() const { return bActive; }
    bool Enqueue(const FLDCaptureEvent& Event, FString& Error);
    bool CloseBoundary(int64 AtMs, TOptional<FLDCaptureProposal>& Proposal, FString& Error);
    // Closes a step whose only work after Motion is this capture (see CompletionInsideMs): a guard due strictly
    // inside it completes at its due instant and the facts after that instant are discarded.
    bool CloseThroughCompletion(int64 AtMs, TOptional<FLDCaptureProposal>& Proposal, FString& Error);
    // The instant strictly before AtMs at which the facts enqueued so far complete a guard (a frozen guard resumes
    // when the last challenger stops counting in the zone inside the step); MAX_int64 when they do not. Facts that
    // can still arrive in this step are stamped AtMs, so they can neither cause nor cancel an earlier completion. A
    // spray that completes inside a step does not end the match; CloseBoundary reduces it at its own instant.
    int64 CompletionInsideMs(int64 AtMs) const;

    bool IsOpen() const { return bOpen; }
    ELDCircleState GetState() const { return State; }
    // The spray progress holder: the sole sprayer while Solo, or the paused holder while Contested.
    const FGuid& GetOccupier() const { return Occupier; }
    uint64 GetEpoch() const { return Epoch; }
    // Solo only. A resumed spray starts at the resume instant minus the paused progress.
    int64 GetSoloSinceMs() const { return State == ELDCircleState::Solo ? SoloSince : 0; }
    int64 GetDueAtMs() const { return State == ELDCircleState::Solo ? DueAt : 0; }
    // Contested with a holder only: spray progress frozen at the contest, in (0, spray].
    int64 GetPausedProgressMs() const { return State == ELDCircleState::Contested ? Paused : 0; }
    const FGuid& GetGraffitiOwner() const { return Owner; }
    // Solo and Contested always hold an effective contender other than the owner, which freezes the guard.
    static bool GuardRunsIn(ELDCircleState Circle) { return Circle == ELDCircleState::Empty || Circle == ELDCircleState::Guarded; }
    bool IsGuardRunning() const { return Owner.IsValid() && GuardRunsIn(State); }
    int64 GetGuardDueAtMs() const { return IsGuardRunning() ? GuardDue : 0; }
    // A challenger in the zone froze the owner's guard at this progress, in [0, guard].
    int64 GetGuardKeptMs() const { return Owner.IsValid() && !GuardRunsIn(State) ? GuardKept : 0; }
    int64 GetOpenAtMs() const { return OpenAt; }
    int64 GetClosedThroughMs() const { return ClosedThrough; }
    // Next integer boundary the authority driver must stop at: the opening while closed, the spray while a sole
    // sprayer accumulates and the guard win while the guard runs. Otherwise MAX_int64: a paused contest or a frozen
    // guard adds no stop, because a resume can only come from motion inside a step (CompletionInsideMs, CloseBoundary).
    int64 NextDueMs() const;
    // Commit-order view used to validate motion evidence before its boundary closes.
    bool IsCommittedAliveBody(const FLDCombatBodyRef& Body) const;
    // A contender that joined the capture roster and has not left it.
    bool IsCommittedMember(const FGuid& ContenderId) const;
#if WITH_DEV_AUTOMATION_TESTS
    // DEV test session: moves a closed zone's opening forward to AtMs, a boundary that is not closed yet, so the next
    // CloseBoundary opens it exactly like the scheduled opening. Refused when inactive, already open, or not earlier.
    bool DevOpenAt(int64 AtMs, FString& Error);
    const FLDCaptureDevTally& DevTally() const { return DevCounts; }
#endif

private:
    struct FMember
    {
        FLDCombatBodyRef Body;
        ELDLifePhase Phase = ELDLifePhase::Unspawned;
        bool bProtected = false;
        int64 ProtectedUntilMs = 0;
        bool bInside = false;
    };
    static bool Transition(TMap<FGuid, FMember>& Members, const FLDCaptureEvent& Event, bool bTimeOrdered, FString& Error);
    // ResetOwner names the owner who was in the zone under a frozen guard when this instant began; it is passed with
    // the last fact of the instant only, so an owner and a challenger leaving at the same instant reset nothing.
    bool Evaluate(const FLDCaptureEventTime& At, const FGuid& Interrupted, const FGuid& ResetOwner, FString& Error);
    bool IsOwnerInFrozenGuard() const;
    // Applies the sprays due before Limit (at it too when bInclusive). A running guard due strictly before Limit
    // completes there when bCompleteInside and is a driver contract failure otherwise.
    bool Advance(int64 Limit, bool bInclusive, bool bCompleteInside, bool& bCompleted, FString& Error);
    bool CompleteSpray(FString& Error);
    bool Reduce(int64 AtMs, bool bCompleteInside, TOptional<FLDCaptureProposal>& Proposal, FString& Error);

    TMap<FGuid, FMember> Committed;
    TMap<FGuid, FMember> Reduced;
    TArray<FLDCaptureEvent> Pending;
    bool bActive = false;
    bool bOpen = false;
    int64 OpenAt = 0;
    int64 Spray = 0;
    int64 Guard = 0;
    int64 ClosedThrough = -1;
    int64 SoloSince = 0;
    int64 DueAt = 0;
    int64 Paused = 0;
    int64 GuardDue = 0;
    int64 GuardKept = 0;
    uint64 NextOrdinal = 0;
    uint64 Epoch = 0;
    double LastReducedSeconds = 0;
    ELDCircleState State = ELDCircleState::NotOpen;
    FGuid Occupier;
    FGuid Owner;
#if WITH_DEV_AUTOMATION_TESTS
    FLDCaptureDevTally DevCounts;
#endif
};
