#pragma once

#include "CoreMinimal.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Character/LDNativeMotionTypes.h"
#include "LDCaptureTypes.generated.h"

// Spray zone state. Solo: exactly one effective contender is inside and he is not the graffiti owner, so he sprays.
// Contested: two or more effective contenders are inside. Guarded: the graffiti owner is the only one inside; he
// never sprays his own graffiti.
UENUM()
enum class ELDCircleState : uint8 { NotOpen, Empty, Solo, Contested, Guarded };

UENUM()
enum class ELDMatchEndReason : uint8 { None, GuardSucceeded, HostEnded };

// Public cause class of a contender death. Never names the fatal skill, item or build.
UENUM()
enum class ELDKillCause : uint8 { Contender, CampLow, CampHigh, Unowned };

// Whole-room spray and guard state. It names at most the spray progress holder and the graffiti owner and never
// lists bodies in the zone, hidden positions, protection or private build data. Live time comes from DemoClock.
USTRUCT()
struct FLDPublicMatchView
{
    GENERATED_BODY()
    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() uint64 PublicRevision = 0;
    UPROPERTY() ELDMatchPhase Phase = ELDMatchPhase::Faulted;
    UPROPERTY() int64 ServerNowMs = 0;
    UPROPERTY() int64 StartedAtMs = 0;
    UPROPERTY() int64 OpenAtMs = 0;
    UPROPERTY() ELDCircleState CircleState = ELDCircleState::NotOpen;
    // The spray progress holder: the sole sprayer while Solo, or the contender whose spray progress a contest has
    // paused while Contested. Invalid otherwise.
    UPROPERTY() FGuid Occupier;
    UPROPERTY() uint64 CaptureEpoch = 0;
    // Solo only. After a resume SoloSinceMs is the resume instant minus the paused progress,
    // so DueAtMs is the real instant the graffiti is sprayed.
    UPROPERTY() int64 SoloSinceMs = 0;
    UPROPERTY() int64 DueAtMs = 0;
    // Contested with a holder only: his spray progress frozen by the contest.
    UPROPERTY() int64 PausedProgressMs = 0;
    // The contender whose graffiti the flagpole shows; invalid while it is bare. GraffitiPresentation is the owner's
    // public identity slot (his start roster position, the body tint key), zero without an owner.
    UPROPERTY() FGuid GraffitiOwner;
    UPROPERTY() uint8 GraffitiPresentation = 0;
    // With an owner: the real instant the running guard wins (every state except Solo), or the guard progress a
    // sole sprayer has paused (Solo only). Both zero without an owner.
    UPROPERTY() int64 GuardDueAtMs = 0;
    UPROPERTY() int64 GuardKeptMs = 0;
    UPROPERTY() FGuid ResultId;
};

USTRUCT()
struct FLDResultRow
{
    GENERATED_BODY()
    UPROPERTY() FGuid ContenderId;
    UPROPERTY() FString Name;
    UPROPERTY() ELDContenderKind Kind = ELDContenderKind::Human;
    UPROPERTY() bool bLeft = false;
    UPROPERTY() uint64 Kills = 0;
    UPROPERTY() uint64 Deaths = 0;
    UPROPERTY() uint8 FinalLevel = 0;
};

// Frozen public result. Rows follow the locked start roster; statistics never choose the winner.
USTRUCT()
struct FLDResultView
{
    GENERATED_BODY()
    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() FGuid ResultId;
    UPROPERTY() uint64 ResultSerial = 0;
    UPROPERTY() ELDMatchEndReason Reason = ELDMatchEndReason::None;
    // Invalid when the Host ended the match without a winner.
    UPROPERTY() FGuid Winner;
    UPROPERTY() int64 StartedAtMs = 0;
    UPROPERTY() int64 EndedAtMs = 0;
    UPROPERTY() int64 DurationMs = 0;
    UPROPERTY() TArray<FLDResultRow> Rows;
};

USTRUCT()
struct FLDKillNotice
{
    GENERATED_BODY()
    UPROPERTY() uint64 Serial = 0;
    UPROPERTY() FGuid Victim;
    // Valid only for a Contender cause.
    UPROPERTY() FGuid Killer;
    UPROPERTY() ELDKillCause Cause = ELDKillCause::Unowned;
    UPROPERTY() int64 OccurredAtMs = 0;
};

// Bounded ring of the most recent contender deaths, oldest first; Serial increases per match.
USTRUCT()
struct FLDKillFeedView
{
    GENERATED_BODY()
    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() TArray<FLDKillNotice> Notices;
};

// One real authority instant. Seconds orders facts across providers; BucketMs is the integer
// boundary that must close before the fact is reduced.
struct LOCKDOWN_API FLDCaptureEventTime
{
    double Seconds = 0;
    int64 BucketMs = 0;
    // Exact integer instants (timers, boundaries) never round-trip through double.
    static bool FromMs(int64 Ms, FLDCaptureEventTime& Out, FString& Error);
    // Sub-millisecond instants use the checked ceiling bucket.
    static bool FromSeconds(double RawSeconds, FLDCaptureEventTime& Out, FString& Error);
};

enum class ELDCaptureEligibilityChange : uint8 { Joined, Respawned, ProtectionEnded, Died, Left };

// Life-provider handoff into the capture reducer. Joined/Respawned carry the committed body's
// native support; Respawned atomically carries its half-open protection end.
struct FLDCaptureEligibilityEvent
{
    FLDCombatBodyRef Body;
    ELDCaptureEligibilityChange Change = ELDCaptureEligibilityChange::Joined;
    FLDCaptureEventTime Time;
    int64 ProtectedUntilMs = 0;
    FVector InitialFootCm = FVector::ZeroVector;
    FLDNativeSupportSample InitialSupport;
    // Died only. CreditContender is another contender that owned the fatal effect, otherwise
    // invalid; KillCause is Contender exactly when CreditContender is valid.
    FLDDeathKey DeathKey;
    FGuid CreditContender;
    ELDKillCause KillCause = ELDKillCause::Unowned;
};

namespace LDCapture
{
    inline constexpr int32 MaxKillNotices = 8;
    LOCKDOWN_API bool ValidatePublicMatchView(const FLDPublicMatchView& View, FString& Error);
    LOCKDOWN_API bool ValidateResultView(const FLDResultView& View, FString& Error);
    LOCKDOWN_API bool ValidateKillFeed(const FLDKillFeedView& View, FString& Error);
    LOCKDOWN_API ELDLobbyProjectionResult AcceptPublicMatchView(const FLDPublicMatchView& Candidate,
        FLDPublicMatchView& Latest, FString& Error);
    LOCKDOWN_API ELDLobbyProjectionResult AcceptResultView(const FLDResultView& Candidate,
        FLDResultView& Latest, FString& Error);
    LOCKDOWN_API ELDLobbyProjectionResult AcceptKillFeed(const FLDKillFeedView& Candidate,
        FLDKillFeedView& Latest, FString& Error);
}
