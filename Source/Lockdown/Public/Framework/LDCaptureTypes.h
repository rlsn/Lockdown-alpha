#pragma once

#include "CoreMinimal.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Framework/LDMatchTimeline.h"
#include "Framework/LDShowdownSettings.h"
#include "PK/LDPKTypes.h"
#include "Character/LDNativeMotionTypes.h"
#include "Development/LDDemoContentDefinitions.h"
#include "World/LDFieldPacing.h"
#include "LDCaptureTypes.generated.h"

// Spray zone state. Solo: exactly one effective contender is inside and he is not the graffiti owner, so he sprays.
// Contested: two or more effective contenders are inside. Guarded: the graffiti owner is the only one inside; he
// never sprays his own graffiti.
UENUM()
enum class ELDCircleState : uint8 { NotOpen, Empty, Solo, Contested, Guarded };

// How the current graffiti came to its owner: a completed spray, or final_usurp taking it over with its guard.
UENUM()
enum class ELDGraffitiCause : uint8 { None, Sprayed, Usurped };

UENUM()
// SprayCount (final_hit_and_run) and ClearOut (final_clear_out) are a final mod's alternative win; appended.
enum class ELDMatchEndReason : uint8 { None, GuardSucceeded, HostEnded, ObjectiveTimeout, NoChampion, SprayCount, ClearOut };

// Public cause class of a contender death. Never names the fatal skill, item or build.
UENUM()
// Boss (#79 D26) is appended so earlier causes keep their wire values.
// CampElite (#97) is appended so existing values keep their numbers.
enum class ELDKillCause : uint8 { Contender, CampLow, CampHigh, Unowned, Boss, CampElite, CampAncient };

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

// A recovery orb a whole camp clear leaves on the camp anchor: the first living contender to touch it (AIRDROP.PickupRadiusCm)
// heals CAMP.ClearHealMissingRatio of its missing HP and gains CAMP.ClearMomentum; it fades at ExpireAtMs (field T).
USTRUCT()
struct FLDRecoveryOrbView
{
    GENERATED_BODY()
    UPROPERTY() uint64 Serial = 0;
    UPROPERTY() FVector PositionCm = FVector::ZeroVector;
    UPROPERTY() int64 ExpireAtMs = 0;
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
    // The leader's level at the refresh that named it (0 without a leader).
    UPROPERTY() uint8 LeaderLevel = 0;
    UPROPERTY() FVector CircleCentreCm = FVector::ZeroVector;
    UPROPERTY() int64 RefreshedAtMs = 0;
    // Spec §11.1 rumor day: the second trending contender and its own blurred circle (invalid otherwise).
    UPROPERTY() FGuid RunnerUp;
    UPROPERTY() FVector RunnerUpCentreCm = FVector::ZeroVector;
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

// One contender's final mod, public to every connection once the final field begins (App Spec §2.1).
USTRUCT()
struct FLDFinalModHolderView
{
    GENERATED_BODY()
    UPROPERTY() FGuid Contender;
    UPROPERTY() FName Mod;
};

// A private flagpole (final_private_pole), public like the main one: only Holder sprays it; anyone else inside freezes
// his guard. Same state meaning as the main flagpole fields of FLDPublicMatchView.
USTRUCT()
struct FLDPrivatePoleView
{
    GENERATED_BODY()
    UPROPERTY() FGuid Holder;
    // The holder's public identity slot (his start roster position), the tint of his graffiti on it.
    UPROPERTY() uint8 Presentation = 0;
    UPROPERTY() FVector CentreCm = FVector::ZeroVector;
    UPROPERTY() float RadiusCm = 0;
    UPROPERTY() ELDCircleState CircleState = ELDCircleState::NotOpen;
    // Solo: the holder's spray instant; Contested with his paused progress: that progress.
    UPROPERTY() int64 DueAtMs = 0;
    UPROPERTY() int64 PausedProgressMs = 0;
    UPROPERTY() bool bGraffiti = false;
    UPROPERTY() int64 GuardDueAtMs = 0;
    UPROPERTY() int64 GuardKeptMs = 0;
    UPROPERTY() int64 GuardMs = 0;
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
    // Why the owner holds it (None exactly without an owner); announcements read this fact, never the guard progress.
    UPROPERTY() ELDGraffitiCause GraffitiCause = ELDGraffitiCause::None;
    // With an owner: the real instant the running guard wins (Guarded), or the guard progress kept while it is paused
    // (Empty: the owner is away) or frozen by a challenger in the zone (Solo or Contested). Both zero without an owner.
    UPROPERTY() int64 GuardDueAtMs = 0;
    UPROPERTY() int64 GuardKeptMs = 0;
    UPROPERTY() FGuid ResultId;
    UPROPERTY() FLDAirdropView Airdrop;
    UPROPERTY() FLDLeaderView Leader;
    UPROPERTY() FLDBossView Boss;
    // #198 outer §5: the camp markers of the current (or, until the next reveal, the last) timed field segment.
    UPROPERTY() TArray<FLDFieldMarkerView> Markers;
    // #198: the outer timeline (rest steps, field segment and T).
    UPROPERTY() FLDTimelineView Timeline;
    // #198 session D: the current rest's PK (Serial 0: none); public like the sign-ups.
    UPROPERTY() FLDPKView PK;
    UPROPERTY() FLDShowdownView Showdown;
    UPROPERTY() int64 OwnerBankedGuardMs = 0;
    // Final field only. The main flagpole's current spray zone: final_carry_flag moves it with its owner,
    // final_double_yellow shrinks it while its holder owns the graffiti.
    UPROPERTY() FVector PoleCentreCm = FVector::ZeroVector;
    UPROPERTY() float PoleRadiusCm = 0;
    // final_hit_and_run: the instant the owner's guardless graffiti fades; 0 otherwise.
    UPROPERTY() int64 FadeAtMs = 0;
    // final_cage: the fence on the spray zone boundary stands until CageUntilMs (0: none).
    UPROPERTY() int64 CageUntilMs = 0;
    UPROPERTY() FVector CageCentreCm = FVector::ZeroVector;
    UPROPERTY() float CageRadiusCm = 0;
    // Its integrity: the share of its durability (`hp`) hostile hits across it have not yet worn down (1..10000; 0 = none).
    UPROPERTY() int32 CageIntegrityBp = 0;
    // Every contender's final mod, in roster order (final field only).
    UPROPERTY() TArray<FLDFinalModHolderView> FinalMods;
    UPROPERTY() TArray<FLDPrivatePoleView> PrivatePoles;
    // Spec §11.1/§10.3/§11.3 (P6): the match's daily modifiers, the current (or last) mid-field objective and the delivery
    // lockers out now.
    UPROPERTY() TArray<ELDDailyModifier> DailyModifiers;
    UPROPERTY() FLDObjectiveView Objective;
    UPROPERTY() TArray<FLDLockerView> Lockers;
};

UENUM()
// Where a card sits: the body (which carries the basic attack), a learned skill or the D dash. 2, 3 and 5 are retired.
enum class ELDResultBuildTarget : uint8 { Body = 0, Active = 1, Dash = 4 };

USTRUCT()
struct FLDResultBuildActive
{
   GENERATED_BODY()
   UPROPERTY() FName DefinitionId;
   UPROPERTY() uint8 Slot = 0;
   UPROPERTY() ELDSkillBranch Branch = ELDSkillBranch::None;
};

USTRUCT()
struct FLDResultBuildCard
{
   GENERATED_BODY()
   UPROPERTY() FName DefinitionId;
   UPROPERTY() ELDResultBuildTarget Target = ELDResultBuildTarget::Body;
   // Active: the skill the card sits on.
   UPROPERTY() FName TargetId;
   UPROPERTY() ELDDemoElement Element = ELDDemoElement::None;
};

// Only the installed rule-board identities and their placement, frozen after the result closes.
USTRUCT()
struct FLDResultBuild
{
   GENERATED_BODY()
   UPROPERTY() bool bAvailable = false;
   UPROPERTY() FString CatalogRoot;
   UPROPERTY() TArray<FLDResultBuildActive> Actives;
   // Installed cards by serial; copies of one card are separate rows.
   UPROPERTY() TArray<FLDResultBuildCard> Cards;
   UPROPERTY() FName FinalMod;
   // Allocated 5-stat points in design order (#331).
   UPROPERTY() TArray<int32> StatPoints;
};

USTRUCT()
struct FLDResultAttribute
{
    GENERATED_BODY()
    UPROPERTY() uint8 Attribute = 0;
    UPROPERTY() FLDDemoRational Value;
};

USTRUCT()
struct FLDResultCardContribution
{
    GENERATED_BODY()
    UPROPERTY() FName Card;
    UPROPERTY() int64 DamageP2 = 0;
    UPROPERTY() int64 ShieldP2 = 0;
    UPROPERTY() int64 Triggers = 0;
};

USTRUCT()
struct FLDResultGrowthPhase
{
    GENERATED_BODY()
    UPROPERTY() int64 Allocated = 0;
    UPROPERTY() int64 Refunded = 0;
    UPROPERTY() int64 BoughtCards = 0;
    UPROPERTY() int64 BoughtSkills = 0;
    UPROPERTY() int64 RefreshedShop = 0;
    UPROPERTY() TArray<FLDResultBuildActive> Skills;
    UPROPERTY() TArray<FName> AcquiredCards;
    UPROPERTY() FName FinalMod;
};

USTRUCT()
struct FLDResultCombat
{
    GENERATED_BODY()
    UPROPERTY() bool bAvailable = false;
    // Physical and magical buckets exclude damage directly tagged as a reaction.
    UPROPERTY() int64 PhysicalToPlayersP2 = 0;
    UPROPERTY() int64 MagicalToPlayersP2 = 0;
    UPROPERTY() int64 ReactionToPlayersP2 = 0;
    UPROPERTY() int64 DamageTakenP2 = 0;
    UPROPERTY() int64 ShieldAbsorbedP2 = 0;
    UPROPERTY() int64 HealingP2 = 0;
    UPROPERTY() int64 RecoveryPackP2 = 0;
    UPROPERTY() int64 Reactions = 0;
    UPROPERTY() int64 PeakReactionP2 = 0;
    UPROPERTY() TArray<FLDResultAttribute> Attributes;
    UPROPERTY() uint8 KeystoneMask = 0;
    UPROPERTY() TArray<FLDResultCardContribution> Cards;
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
    UPROPERTY() int64 BankedGuardMs = 0;
    UPROPERTY() uint32 CompletedSprays = 0;
    UPROPERTY() FLDResultBuild Build;
    UPROPERTY() FLDResultCombat Combat;
    // Opening and R1–R3 include the following field segment. The last entry is the final field.
    UPROPERTY() TArray<FLDResultGrowthPhase> Growth;
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
    UPROPERTY() FString CreatureName;
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
    FString CreatureName;
};

namespace LDCapture
{
    inline constexpr int32 MaxKillNotices = 8;
    // Highest ELDKillCause wire value; causes are appended, so range checks compare against this.
    inline constexpr ELDKillCause LastKillCause = ELDKillCause::CampAncient;
    // A match has at most this many contenders, so the scoreboard at most this many rows.
    inline constexpr int32 MaxScoreRows = LDMatchLifecycle::MaxContenders;
    LOCKDOWN_API bool ValidatePublicMatchView(const FLDPublicMatchView& View, FString& Error);
    LOCKDOWN_API bool ValidateResultBuild(const FLDResultBuild& Build, FString& Error);
    LOCKDOWN_API bool ValidateResultView(const FLDResultView& View, FString& Error);
    LOCKDOWN_API bool ValidateKillFeed(const FLDKillFeedView& View, FString& Error);
    LOCKDOWN_API ELDLobbyProjectionResult AcceptPublicMatchView(const FLDPublicMatchView& Candidate,
        FLDPublicMatchView& Latest, FString& Error);
    LOCKDOWN_API ELDLobbyProjectionResult AcceptResultView(const FLDResultView& Candidate,
        FLDResultView& Latest, FString& Error);
    // The previous result belongs to this Lobby only when its frozen identity and generation match.
    LOCKDOWN_API const FLDResultRow* PreviousLobbyResult(const FLDResultView& Result, const FLDLobbyView& Lobby,
        const FLDLobbyMember& Member);
    LOCKDOWN_API ELDLobbyProjectionResult AcceptKillFeed(const FLDKillFeedView& Candidate,
        FLDKillFeedView& Latest, FString& Error);
}
