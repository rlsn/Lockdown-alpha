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

struct FLDCaptureProposal
{
    uint64 Epoch = 0;
    FGuid Occupier;
    int64 SoloSinceMs = 0;
    int64 DueAtMs = 0;
};

// The unique continuous-exclusive capture reducer. Facts are enqueued in commit order and
// reduced only when their integer boundary closes, in real time order, so intermediate
// enters, exits, deaths and protection changes inside one boundary are never collapsed.
class LOCKDOWN_API FLDCaptureReducer
{
public:
    bool Begin(int64 OpenAtMs, int64 HoldMs, FString& Error);
    void Reset();
    bool IsActive() const { return bActive; }
    bool Enqueue(const FLDCaptureEvent& Event, FString& Error);
    bool CloseBoundary(int64 AtMs, TOptional<FLDCaptureProposal>& Proposal, FString& Error);

    bool IsOpen() const { return bOpen; }
    ELDCircleState GetState() const { return State; }
    const FGuid& GetOccupier() const { return Occupier; }
    uint64 GetEpoch() const { return Epoch; }
    int64 GetSoloSinceMs() const { return State == ELDCircleState::Solo ? SoloSince.BucketMs : 0; }
    int64 GetDueAtMs() const { return State == ELDCircleState::Solo ? DueAt : 0; }
    int64 GetOpenAtMs() const { return OpenAt; }
    int64 GetClosedThroughMs() const { return ClosedThrough; }
    // Next integer boundary the authority driver must stop at: the opening while closed,
    // the completion while one contender holds the circle, otherwise none (MAX_int64).
    int64 NextDueMs() const;
    // Commit-order view used to validate motion evidence before its boundary closes.
    bool IsCommittedAliveBody(const FLDCombatBodyRef& Body) const;
    // A contender that joined the capture roster and has not left it.
    bool IsCommittedMember(const FGuid& ContenderId) const;

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

    TMap<FGuid, FMember> Committed;
    TMap<FGuid, FMember> Reduced;
    TArray<FLDCaptureEvent> Pending;
    bool bActive = false;
    bool bOpen = false;
    int64 OpenAt = 0;
    int64 Hold = 0;
    int64 ClosedThrough = -1;
    int64 DueAt = 0;
    uint64 NextOrdinal = 0;
    uint64 Epoch = 0;
    double LastReducedSeconds = 0;
    ELDCircleState State = ELDCircleState::NotOpen;
    FGuid Occupier;
    FLDCaptureEventTime SoloSince;
};
