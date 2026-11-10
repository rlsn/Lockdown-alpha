#pragma once

#include "CoreMinimal.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Framework/LDMatchTimeline.h"
#include "PK/LDPKTypes.h"
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
// Boss (#79 D26) is appended so earlier causes keep their wire values.
// CampElite (#97) is appended so existing values keep their numbers.
enum class ELDKillCause : uint8 { Contender, CampLow, CampHigh, Unowned, Boss, CampElite };

// D26 (#79): the public phase of the current airdrop. #198 outer §5: Expired (appended) = it lay unclaimed for
// AIRDROP.ExpireSec of field progress after landing and is gone.
UENUM()
enum class ELDAirdropPhase : uint8 { None, Announced, Landed, Claimed, Expired };
UENUM()
// D26 (#79) 画饼老板 Boss: None outside a match, Pending until it appears, Alive while it patrols, Down once killed.
// #198 outer §5: Left (appended) = it was still alive (or never could appear) at BOSS leave_at and left the field.
enum class ELDBossPhase : uint8 { None, Pending, Alive, Down, Left };

// D26 (#79): the current airdrop, public to every connection: point, landing instant and claimer. None before the
// first announcement. The next announcement replaces it. LandAtMs and ExpireAtMs are field progress T.
USTRUCT()
struct FLDAirdropView
{
    GENERATED_BODY()
    UPROPERTY() uint64 Serial = 0;
    UPROPERTY() ELDAirdropPhase Phase = ELDAirdropPhase::None;
    UPROPERTY() FName PointId;
    UPROPERTY() FVector PositionCm = FVector::ZeroVector;
    UPROPERTY() int64 LandAtMs = 0;
    // #198 outer §5: the T at which an unclaimed landed drop leaves.
    UPROPERTY() int64 ExpireAtMs = 0;
    // Claimed says only that the drop is gone: neither who took it nor when (App Spec §5.3).
};

// #198 outer §5 camp markers. Wire values are stable.
UENUM()
enum class ELDFieldMarkerKind : uint8 { None = 0, High = 1, Elite = 2 };
UENUM()
enum class ELDFieldMarkerPhase : uint8 { None = 0, Revealed = 1, Claimed = 2, Expired = 3 };

// #198 outer §5: one camp marker of a timed field segment (Segment 1..3), public to every connection. The first contender
// who clears the marked camp while it is Revealed takes it (High: an active blueprint; Elite: a known two-choice of common
// materials); Claimed never says who. Expired: the segment ended first. RevealAtMs and EndAtMs are field progress T.
USTRUCT()
struct FLDFieldMarkerView
{
    GENERATED_BODY()
    UPROPERTY() uint8 Segment = 0;
    UPROPERTY() ELDFieldMarkerKind Kind = ELDFieldMarkerKind::None;
    UPROPERTY() FName CampId;
    UPROPERTY() FVector PositionCm = FVector::ZeroVector;
    UPROPERTY() ELDFieldMarkerPhase Phase = ELDFieldMarkerPhase::None;
    UPROPERTY() int64 RevealAtMs = 0;
    UPROPERTY() int64 EndAtMs = 0;
};

// D27 (#79) 上热搜: the current leader notice, public to every connection: who and a fuzzy circle that contains the
// leader's foot at the refresh. Empty when nobody is named.
USTRUCT()
struct FLDLeaderView
{
    GENERATED_BODY()
    UPROPERTY() FGuid Leader;
    UPROPERTY() FVector CircleCentreCm = FVector::ZeroVector;
    UPROPERTY() int64 RefreshedAtMs = 0;
};

// #198 field charms: heal (max HP share), mana (max MP share) and a timed move-speed bonus. Wire values are stable.
UENUM()
enum class ELDCharmKind : uint8 { None = 0, Heal = 1, Mana = 2, MoveSpeed = 3 };

// #198: one charm lying on a public airdrop point, public to every connection like the airdrop. Serial is its 1-based
// number in this match; ExpireAtFieldMs is the field progress T at which it leaves unclaimed. A claim only removes it:
// the public view never names who took it.
USTRUCT()
struct FLDCharmView
{
    GENERATED_BODY()
    UPROPERTY() uint64 Serial = 0;
    UPROPERTY() ELDCharmKind Kind = ELDCharmKind::None;
    UPROPERTY() FName PointId;
    UPROPERTY() FVector PositionCm = FVector::ZeroVector;
    UPROPERTY() int64 ExpireAtFieldMs = 0;
};

// D26 (#79) 画饼老板 Boss, public to every connection: when it appears, when it leaves if still alive, and who landed the
// last hit. Its body is seen only through the ordinary fog of war, never through this view. Times are field progress T.
USTRUCT()
struct FLDBossView
{
    GENERATED_BODY()
    UPROPERTY() ELDBossPhase Phase = ELDBossPhase::None;
    UPROPERTY() int64 AppearAtMs = 0;
    UPROPERTY() int64 LeaveAtMs = 0;
    UPROPERTY() FGuid KilledBy;
    UPROPERTY() int64 DownAtMs = 0;
};

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
    // With an owner: the real instant the running guard wins (Guarded), or the guard progress kept while it is paused
    // (Empty: the owner is away) or frozen by a challenger in the zone (Solo or Contested). Both zero without an owner.
    UPROPERTY() int64 GuardDueAtMs = 0;
    UPROPERTY() int64 GuardKeptMs = 0;
    UPROPERTY() FGuid ResultId;
    UPROPERTY() FLDAirdropView Airdrop;
    // #198: the charms lying in the field now, in serial order.
    UPROPERTY() TArray<FLDCharmView> Charms;
    UPROPERTY() FLDLeaderView Leader;
    UPROPERTY() FLDBossView Boss;
    // #198 outer §5: the camp markers of the current (or, until the next reveal, the last) timed field segment.
    UPROPERTY() TArray<FLDFieldMarkerView> Markers;
    // #198: the outer timeline (rest steps, field segment and T).
    UPROPERTY() FLDTimelineView Timeline;
    // #198 session D: the current rest's PK (Serial 0: none); public like the sign-ups.
    UPROPERTY() FLDPKView PK;
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
    UPROPERTY() uint64 Assists = 0;
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
    // Contender cause with a rewarded killer only: the assisters of this death, sorted (public like the killer).
    UPROPERTY() TArray<FGuid> Assisters;
};

// One contender's kills, deaths and assists so far, for the hold-Tab scoreboard. Public like the kill notices that move it.
USTRUCT()
struct FLDScoreRow
{
    GENERATED_BODY()
    UPROPERTY() FGuid ContenderId;
    UPROPERTY() uint64 Kills = 0;
    UPROPERTY() uint64 Deaths = 0;
    UPROPERTY() uint64 Assists = 0;
};

// Bounded ring of the most recent contender deaths, oldest first; Serial increases per match. Scores lists every contender
// of the match in roster order; only a death moves it, so it is never newer than the last notice.
USTRUCT()
struct FLDKillFeedView
{
    GENERATED_BODY()
    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() TArray<FLDKillNotice> Notices;
    UPROPERTY() TArray<FLDScoreRow> Scores;
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

// PhaseRetired (#198): a living field life ended by a field -> rest boundary; it leaves the spray roster like a death
// but counts no kill or death.
enum class ELDCaptureEligibilityChange : uint8 { Joined, Respawned, ProtectionEnded, Died, Left, PhaseRetired };

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
    // The locked fact's assisters (FLDCommittedDeath::Assisters); empty without a Contender cause.
    TArray<FGuid> Assisters;
};

namespace LDCapture
{
    inline constexpr int32 MaxKillNotices = 8;
    // Highest ELDKillCause wire value; causes are appended, so range checks compare against this.
    inline constexpr ELDKillCause LastKillCause = ELDKillCause::CampElite;
    // A match has at most this many contenders, so the scoreboard at most this many rows.
    inline constexpr int32 MaxScoreRows = 6;
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
