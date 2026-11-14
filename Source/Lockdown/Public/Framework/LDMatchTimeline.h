#pragma once

#include "CoreMinimal.h"
#include "LDMatchTimeline.generated.h"

struct FLDDemoContentParameters;

// #198 outer match flow (approved outer parameters, issue #198 comment 6752601175 §1/§8/§9): an opening rest R0, then
// three field segments each followed by a mid rest R1..R3, then the final field where the flagpole opens. One monotonic
// authority clock drives everything; the field progress T is a derived accumulator that only advances while a field
// segment runs (it is not a second world tick). Rests and PK pause T.
UENUM()
enum class ELDMatchStage : uint8 { None = 0, Rest = 1, Field = 2, FinalField = 3 };

// The sub-steps of one rest. Build doubles as PK sign-up during its last REST.SignupSec; Payout lets everyone adjust
// again. Draw..Settlement only run when at least two contenders signed up (session D plugs the PK itself in).
UENUM()
enum class ELDRestStep : uint8 { None = 0, Build = 1, Draw = 2, Betting = 3, PKReady = 4, PK = 5, Settlement = 6, Payout = 7, Return = 8 };

UENUM()
enum class ELDRestService : uint8 { None = 0, SkillMerchant = 1, ComponentArtisan = 2, Trainer = 3 };

UENUM()
enum class ELDRestReadyResult : uint8 { None = 0, Accepted = 1, NotContender = 2, StaleMatch = 3, StaleWindow = 4, NotOpen = 5 };

// Public, replicated read model of the timeline. Times are authority ms; T values are field ms.
USTRUCT()
struct LOCKDOWN_API FLDTimelineView
{
    GENERATED_BODY()
    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() uint64 Revision = 0;
    UPROPERTY() ELDMatchStage Stage = ELDMatchStage::None;
    // Rest: 0 = R0 (opening) .. 3 = R3. Field: the rest that preceded it.
    UPROPERTY() uint8 RestIndex = 0;
    // Field: 0..2 for the three timed segments, 3 for the final field. Rest: the segment that follows it.
    UPROPERTY() uint8 FieldSegment = 0;
    UPROPERTY() ELDRestStep Step = ELDRestStep::None;
    UPROPERTY() int64 StepStartedAtMs = 0;
    // Rest steps: the scheduled end (PK: its latest end). Field: the authority ms at which T reaches FieldEndT; final
    // field: MAX_int64.
    UPROPERTY() int64 StepEndsAtMs = 0;
    // Build step: from here until StepEndsAtMs (inclusive) the step doubles as PK sign-up.
    UPROPERTY() int64 SignupOpensAtMs = 0;
    // T at FieldStartedAtMs while a field runs; the frozen T otherwise.
    UPROPERTY() int64 FieldBaseMs = 0;
    UPROPERTY() int64 FieldStartedAtMs = 0;
    // T at which the current/next timed segment ends; -1 for the final field.
    UPROPERTY() int64 FieldEndT = 0;
    // Locked PK entrants of this rest (0 before the lock); fewer than two skip straight to Return.
    UPROPERTY() uint8 LockedEntrants = 0;
    UPROPERTY() bool bPKSkipped = false;
    // Public: standing in the sign-up disk is a visible act. Sorted contender ids; filled by GameMode, not the reducer.
    UPROPERTY() TArray<FGuid> Signups;
    UPROPERTY() TArray<FGuid> ReadyContenders;
};

// Owner-sent training reset result (ALDPlayerController::ServerTrainingReset -> ClientTrainingReset). Queued commits at
// the next authority boundary.
UENUM()
enum class ELDTrainingResetResult : uint8 { None = 0, Queued = 1, NotInRest = 2, NotContender = 3, StaleMatch = 4, NoDummy = 5 };

struct LOCKDOWN_API FLDTimelineParameters
{
    // Number of timed field segments, each followed by a mid rest. The approved flow has exactly three; it is a rule of
    // the outer structure, not a tunable.
    static constexpr uint8 TimedSegments = 3;
    int64 FieldSegmentMs = 0;
    int64 OpeningBuildMs = 0;
    int64 BuildMs = 0;
    int64 SignupMs = 0;
    int64 DrawMs = 0;
    int64 BettingMs = 0;
    int64 PKReadyMs = 0;
    int64 PKMaxMs = 0;
    int64 SettlementMs = 0;
    int64 PayoutMs = 0;
    int64 ReturnMs = 0;
    // Field respawn wait fixed by the T of the death: [0,1,2] for the timed segments, [3] for the final field.
    int64 RespawnMs[TimedSegments] = {};

    static bool FromContent(const FLDDemoContentParameters& Content, FLDTimelineParameters& Out, FString& Error);
    bool Validate(FString& Error) const;
};

// One transition the authority must act on, in order. AtMs is the exact boundary instant.
enum class ELDTimelineEventKind : uint8 { EnterRest, EnterStep, LockBuild, EnterField };

struct FLDTimelineEvent
{
    ELDTimelineEventKind Kind = ELDTimelineEventKind::EnterStep;
    uint8 RestIndex = 0;
    uint8 FieldSegment = 0;
    ELDRestStep Step = ELDRestStep::None;
    int64 AtMs = 0;
    // LockBuild: the locked entrant count; EnterField: the T the segment starts at.
    int64 Value = 0;
};

// Pure, deterministic timeline reducer owned by ALDGameMode. It never reads the world clock: every call passes the
// authority instant, and every transition happens exactly at its scheduled boundary.
class LOCKDOWN_API FLDMatchTimeline
{
public:
    bool Begin(const FLDTimelineParameters& InParameters, int64 StartMs, FString& Error);
    void Reset() { *this = FLDMatchTimeline(); }
    bool IsActive() const { return Stage != ELDMatchStage::None; }

    ELDMatchStage GetStage() const { return Stage; }
    ELDRestStep GetStep() const { return Step; }
    uint8 GetRestIndex() const { return RestIndex; }
    uint8 GetFieldSegment() const { return FieldSegment; }
    bool IsRest() const { return Stage == ELDMatchStage::Rest; }
    bool IsFieldRunning() const { return Stage == ELDMatchStage::Field || Stage == ELDMatchStage::FinalField; }
    // The rest steps in which build commands, the NPC, respec and training are open.
    bool IsRestBuildOpen() const { return Stage == ELDMatchStage::Rest && (Step == ELDRestStep::Build || Step == ELDRestStep::Payout); }
    // Sign-up is open in the last SignupMs of the build step, up to and including its deadline instant: operations
    // committed at the deadline settle before the lock.
    bool IsSignupOpen(int64 AtMs) const;
    int64 GetStepEndsAtMs() const { return StepEndsAtMs; }

    // Field progress T at an authority instant (frozen outside the field; 0 before the first field).
    int64 FieldMsAt(int64 AtMs) const;
    // The authority ms at which T reaches FieldAtMs inside the running segment; MAX_int64 when no field runs, it is not
    // in the future, or it falls after this segment's end.
    int64 FieldDueAuthorityMs(int64 AtMs, int64 FieldAtMs) const;
    // The next exact boundary the driver must stop at; MAX_int64 in the final field.
    int64 NextDueMs() const;
    // Respawn wait of a field death at field progress FieldT.
    int64 RespawnWaitMs(int64 FieldT) const;

    // Applies every transition due at or before AtMs. SignedUp is read once, when the build step locks. Without
    // bAllowLock a due build lock stops the chain (the caller locks later in the same boundary). bBodiesReady says whether
    // every body of a due entry (PKReady -> PK, Return -> field) stands prepared: without it the entry holds (approved
    // outer parameters §1: entry freezes while the scene is not ready), the step stays due and the chain stops; once
    // ready, a held entry happens at the AtMs it became ready, not at its old scheduled instant.
    bool Advance(int64 AtMs, int32 SignedUp, bool bAllowLock, bool bBodiesReady, TArray<FLDTimelineEvent>& Out, FString& Error);
    // Whether the build step's lock is due at or before AtMs.
    bool IsBuildLockDue(int64 AtMs) const { return Stage == ELDMatchStage::Rest && Step == ELDRestStep::Build && StepEndsAtMs <= AtMs; }
    // Whether a body entry (the PK start after PKReady or the field after Return) is due at or before AtMs.
    bool IsEntryDue(int64 AtMs) const
    { return Stage == ELDMatchStage::Rest && (Step == ELDRestStep::PKReady || Step == ELDRestStep::Return) && StepEndsAtMs <= AtMs; }
    // Whether a due entry is currently held for its bodies.
    bool IsEntryHeld() const { return bEntryHeld; }
    // PK ended early (a winner or a draw is decided): the PK step now ends at AtMs; the next Advance settles it.
    bool FinishPK(int64 AtMs, FString& Error);
    // All humans are ready: shorten once, preserving the remaining signup window and never extending the deadline.
    bool FinishBuildEarly(int64 AtMs, bool& bChanged, FString& Error);
#if WITH_DEV_AUTOMATION_TESTS
    // DEV only: queue the real final rest return from a timed field, with T at the last segment end.
    bool DevSkipToFinalRest(int64 AtMs, TArray<FLDTimelineEvent>& Out, FString& Error);
    // Opening preparation only: preserve a real build lock and return, with entry due at the next world boundary.
    bool DevFinishPreparation(int64 AtMs, TArray<FLDTimelineEvent>& Out, FString& Error);
#endif

    FLDTimelineView MakeView(uint32 Generation, uint64 Revision) const;
    const FLDTimelineParameters& GetParameters() const { return Parameters; }

private:
    bool EnterRest(uint8 Index, int64 AtMs, TArray<FLDTimelineEvent>& Out, FString& Error);
    bool EnterStep(ELDRestStep NewStep, int64 AtMs, TArray<FLDTimelineEvent>& Out, FString& Error);
    bool EnterField(int64 AtMs, TArray<FLDTimelineEvent>& Out, FString& Error);
    int64 StepDuration(ELDRestStep InStep) const;

    FLDTimelineParameters Parameters;
    ELDMatchStage Stage = ELDMatchStage::None;
    ELDRestStep Step = ELDRestStep::None;
    uint8 RestIndex = 0;
    uint8 FieldSegment = 0;
    int64 StepStartedAtMs = 0;
    int64 StepEndsAtMs = 0;
    int64 FieldBaseMs = 0;
    int64 FieldStartedAtMs = 0;
    uint8 LockedEntrants = 0;
    bool bPKSkipped = false;
    bool bEntryHeld = false;
};

namespace LDMatchTimeline
{
    LOCKDOWN_API const TCHAR* StepName(ELDRestStep Step);
    LOCKDOWN_API const TCHAR* StageName(ELDMatchStage Stage);
    // Field progress T a client derives from the public timeline at ServerNowMs (the authority's own rule).
    LOCKDOWN_API int64 FieldNowFromView(const FLDTimelineView& View, int64 ServerNowMs);
    // The open rest steps (build/training and payout) of a public timeline.
    inline bool IsRestBuildOpen(const FLDTimelineView& View)
    { return View.Stage == ELDMatchStage::Rest && (View.Step == ELDRestStep::Build || View.Step == ELDRestStep::Payout); }
}
