#pragma once

#include "CoreMinimal.h"
#include "Framework/LDCaptureTypes.h"

enum class ELDCaptureEventKind : uint8 { Open, Joined, Respawned, ProtectionEnded, Presence, Died, Left };

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
// DEV telemetry only, never read by a rule: every reduced segment change, including the ones inside one boundary
// that no public view shows. An attempt starts a holder's progress from zero, a resume continues his paused progress,
// a pause is a contest that froze it and a loss ends it without a win.
struct FLDCaptureDevTally
{
    struct FHolder
    {
        uint32 Attempts = 0;
        uint32 Resumes = 0;
        uint32 Paused = 0;
        uint32 Lost = 0;
    };
    // Entries into Contested, with or without a paused holder.
    uint32 Contests = 0;
    TMap<FGuid, FHolder> Holders;
};
#endif

struct FLDCaptureProposal
{
    uint64 Epoch = 0;
    FGuid Occupier;
    int64 SoloSinceMs = 0;
    int64 DueAtMs = 0;
};

// The unique exclusive capture reducer. Facts are enqueued in commit order and reduced only
// when their integer boundary closes, in real time order, so intermediate enters, exits,
// deaths and protection changes inside one boundary are never collapsed. A contest pauses
// the holder's progress; the holder resumes it as the sole contender again, and loses it
// when he stops being an effective contender (exit, death, departure). Nobody inherits it.
class LOCKDOWN_API FLDCaptureReducer
{
public:
    bool Begin(int64 OpenAtMs, int64 HoldMs, FString& Error);
    void Reset();
    bool IsActive() const { return bActive; }
    bool Enqueue(const FLDCaptureEvent& Event, FString& Error);
    bool CloseBoundary(int64 AtMs, TOptional<FLDCaptureProposal>& Proposal, FString& Error);
    // Closes a step whose only work after Motion is this capture (see CompletionInsideMs): a segment due
    // strictly inside it completes at its due instant and the facts after that instant are discarded.
    bool CloseThroughCompletion(int64 AtMs, TOptional<FLDCaptureProposal>& Proposal, FString& Error);
    // The instant strictly before AtMs at which the facts enqueued so far complete a segment (a paused holder
    // who becomes sole again inside the step); MAX_int64 when they do not. Facts that can still arrive in this
    // step are stamped AtMs, so they can neither cause nor cancel an earlier completion.
    int64 CompletionInsideMs(int64 AtMs) const;

    bool IsOpen() const { return bOpen; }
    ELDCircleState GetState() const { return State; }
    // The progress holder: the sole contender while Solo, or the paused holder while Contested.
    const FGuid& GetOccupier() const { return Occupier; }
    uint64 GetEpoch() const { return Epoch; }
    // Solo only. A resumed segment starts at the resume instant minus the paused progress.
    int64 GetSoloSinceMs() const { return State == ELDCircleState::Solo ? SoloSince : 0; }
    int64 GetDueAtMs() const { return State == ELDCircleState::Solo ? DueAt : 0; }
    // Contested with a holder only: progress frozen at the contest, in (0, hold].
    int64 GetPausedProgressMs() const { return State == ELDCircleState::Contested ? Paused : 0; }
    int64 GetOpenAtMs() const { return OpenAt; }
    int64 GetClosedThroughMs() const { return ClosedThrough; }
    // Next integer boundary the authority driver must stop at: the opening while closed and the
    // completion while one contender holds the circle. Otherwise MAX_int64: a paused contest adds no
    // stop, because a resume can only come from motion inside a step and is closed by CompletionInsideMs.
    int64 NextDueMs() const;
    // Commit-order view used to validate motion evidence before its boundary closes.
    bool IsCommittedAliveBody(const FLDCombatBodyRef& Body) const;
    // A contender that joined the capture roster and has not left it.
    bool IsCommittedMember(const FGuid& ContenderId) const;
#if WITH_DEV_AUTOMATION_TESTS
    // DEV test session: moves a closed circle's opening forward to AtMs, a boundary that is not closed yet, so the next
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
    bool Evaluate(const FLDCaptureEventTime& At, FString& Error);
    bool Reduce(int64 AtMs, bool bCompleteInside, TOptional<FLDCaptureProposal>& Proposal, FString& Error);

    TMap<FGuid, FMember> Committed;
    TMap<FGuid, FMember> Reduced;
    TArray<FLDCaptureEvent> Pending;
    bool bActive = false;
    bool bOpen = false;
    int64 OpenAt = 0;
    int64 Hold = 0;
    int64 ClosedThrough = -1;
    int64 SoloSince = 0;
    int64 DueAt = 0;
    int64 Paused = 0;
    uint64 NextOrdinal = 0;
    uint64 Epoch = 0;
    double LastReducedSeconds = 0;
    ELDCircleState State = ELDCircleState::NotOpen;
    FGuid Occupier;
#if WITH_DEV_AUTOMATION_TESTS
    FLDCaptureDevTally DevCounts;
#endif
};
