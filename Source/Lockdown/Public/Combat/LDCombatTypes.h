#pragma once

#include "CoreMinimal.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Navigation/LDOrderTypes.h"
#include "Combat/LDAttributeProjection.h"
#include "Development/LDDemoContentIds.h"
#include "Combat/LDChainRules.h"
#include "LDCombatTypes.generated.h"

class AController;

// ---------------------------------------------------------------- sources and entries

// Innate: the D dash every contender has (D19); it is no learned source and is never forgotten.
// 3 was the retired equipment (Gear) source (#144); Innate keeps its number. Card: an installed build card (DefinitionId =
// card id); it is never an entry or a cooldown track. 6 (the retired source-bound final mod) is never reused.
UENUM()
enum class ELDCombatSourceKind : uint8 { None = 0, Basic = 1, Learned = 2, Innate = 4, Card = 5 };

// Activation entry label only; never a source or cooldown identity. Basic comes only from
// Navigation-derived contender attacks and from registered camp members. Dash is the innate D dash.
UENUM()
enum class ELDCombatEntry : uint8 { None = 0, Basic = 1, Q = 2, W = 3, Dash = 4, E = 5 };

USTRUCT()
struct FLDCombatSourceRef
{
    GENERATED_BODY()
    UPROPERTY() ELDCombatSourceKind Kind=ELDCombatSourceKind::None;
    UPROPERTY() FName DefinitionId;   // ability or equipment id; None for contender Basic, creature definition for a monster skill; LDCombatSourceRules::InnateDashId for Innate
    bool operator==(const FLDCombatSourceRef& Other) const
    { return Kind==Other.Kind && DefinitionId==Other.DefinitionId; }
    bool operator!=(const FLDCombatSourceRef& Other) const { return !(*this==Other); }
};

// One attacker's committed positive damage through one source (shield-absorbed part included, as the build blueprint's
// damage facts count it). Contender: the contender credited with the hits; MonsterId: the camp member definition when no
// contender is credited; both empty: an unowned source. A match total of own damage leaves both empty.
// Reaction damage of a death recap also names its rule chain: Reaction (ELDCombatReaction), the consumed element held by
// the victim (Consumed, ELDCombatElement) and Setter, the contender credited with applying it (invalid: not a contender).
USTRUCT()
struct FLDDamageLine
{
    GENERATED_BODY()
    UPROPERTY() FGuid Contender;
    UPROPERTY() FName MonsterId;
    UPROPERTY() FLDCombatSourceRef Source;
    UPROPERTY() int64 DamageP2 = 0;
    UPROPERTY() FGuid Setter;
    UPROPERTY() uint8 Consumed = 0;
    UPROPERTY() uint8 Reaction = 0;
    bool SameAttacker(const FLDDamageLine& Other) const { return Contender == Other.Contender && MonsterId == Other.MonsterId; }
    bool SameKey(const FLDDamageLine& Other) const
    {
        return SameAttacker(Other) && Source == Other.Source && Setter == Other.Setter && Consumed == Other.Consumed
            && Reaction == Other.Reaction;
    }
};

// Server-only, never serialized. Resolved from the owning connection, the registered AI or a registered camp member.
struct FLDCombatCaller
{
    FLDMatchContext Match;
    ELDVictimKind Kind=ELDVictimKind::Contender;
    FGuid EntityId;                          // ContenderId, or camp member entity id
    TWeakObjectPtr<AController> Controller;  // human/AI controller; null for camp members
};

// ---------------------------------------------------------------- action requests

UENUM()
enum class ELDCombatTargetKind : uint8 { Missing, Self, Unit, Direction, Point };

USTRUCT()
struct FLDCombatTarget
{
    GENERATED_BODY()
    UPROPERTY() ELDCombatTargetKind Kind=ELDCombatTargetKind::Missing;
    UPROPERTY() FGuid Observed;                    // Unit, contender callers: observer-local handle
    UPROPERTY() uint32 ObservationEpoch=0;
    UPROPERTY() FVector_NetQuantizeNormal Direction=FVector::ZeroVector; // Direction: planar unit vector
    UPROPERTY() FVector_NetQuantize Point=FVector::ZeroVector;           // Point: requested ground point (GroundPoint skills)
    FLDCombatBodyRef ServerBody;                   // Unit from a registered camp member only; never replicated
};

// Direct request; DirectSequence is monotonic per contender (Q/W/D share it) or per camp member.
USTRUCT()
struct FLDCombatActionRequest
{
    GENERATED_BODY()
    UPROPERTY() uint32 Generation=0;
    UPROPERTY() uint32 Life=0;
    UPROPERTY() uint64 DirectSequence=0;
    UPROPERTY() ELDCombatEntry Entry=ELDCombatEntry::None;
    UPROPERTY() FLDCombatTarget Target;
    // Owner view SourceRevision that the input was armed against; a changed source is not re-interpreted.
    UPROPERTY() uint64 ExpectedSourceRevision=0;
};

USTRUCT()
struct FLDCombatCancelRequest
{
    GENERATED_BODY()
    UPROPERTY() uint32 Generation=0;
    UPROPERTY() uint32 Life=0;
    UPROPERTY() uint64 ThroughDirectSequence=0;
};

UENUM()
enum class ELDCombatRequestOrigin : uint8 { Direct, Nav };

// Server-side request identity; Nav keys are produced only by the server Navigation order.
struct FLDCombatRequestKey
{
    ELDCombatRequestOrigin Origin=ELDCombatRequestOrigin::Direct;
    uint32 Generation=0;
    FGuid Entity;
    uint32 Life=0;
    uint64 DirectSequence=0;   // Direct
    FLDMoveIdentity Move;      // Nav
    uint64 AttackCycle=0;      // Nav
    bool operator==(const FLDCombatRequestKey& Other) const
    {
        return Origin==Other.Origin && Generation==Other.Generation && Entity==Other.Entity && Life==Other.Life
            && DirectSequence==Other.DirectSequence && Move==Other.Move && AttackCycle==Other.AttackCycle;
    }
};

UENUM()
enum class ELDCombatPhase : uint8 { Queued, Accepted, Released, Finished, Rejected };

UENUM()
enum class ELDCombatReject : uint8
{
    None, InvalidRequest, NotOwner, StaleMatch, StaleLife, StaleRequest, RequestConflict, ReceiptExpired,
    Cancelled, StaleSource, SourceUnavailable, Busy, Cooldown, AttackNotReady,
    MomentumShort, ControlBlocked, TargetUnavailable, InvalidDirection, OutOfRange, Blocked, Dead,
    MatchClosed, AuthorityDataUnavailable, CapacityUnavailable,
    // #198: the rest return countdown locks every attack, ability and dash.
    NotStarted,
    // K12: the release costs health the body cannot pay and live.
    HealthShort
};

// Owner-only receipt. A Rejected receipt may still carry bProtectionRemoved: the intent broke protection.
USTRUCT()
struct FLDCombatActionReceipt
{
    GENERATED_BODY()
    UPROPERTY() uint32 Generation=0;
    UPROPERTY() uint32 Life=0;
    UPROPERTY() ELDCombatRequestOrigin Origin=ELDCombatRequestOrigin::Direct;
    UPROPERTY() uint64 DirectSequence=0;
    UPROPERTY() ELDCombatEntry Entry=ELDCombatEntry::None;
    UPROPERTY() uint64 CombatToken=0;      // 0 until accepted
    UPROPERTY() uint64 ReceiptRevision=0;
    UPROPERTY() ELDCombatPhase Phase=ELDCombatPhase::Rejected;
    UPROPERTY() ELDCombatReject Reject=ELDCombatReject::InvalidRequest;
    UPROPERTY() bool bProtectionRemoved=false;
    UPROPERTY() uint64 LifeRevision=0;
};

// ---------------------------------------------------------------- aggressive intent and respawn protection

UENUM()
enum class ELDAggressiveIntentKind : uint8 { AttackUnit, AttackMove, BasicAttack, ActivateCharacter };

UENUM()
enum class ELDAggressionOrigin : uint8 { Navigation, Combat };

// Navigation: original Order/Epoch; Combat: the received DirectSequence with CancelEpoch 0.
// Derived repeat attacks reuse their navigation intent and never mint another removal.
struct FLDAggressionRequestKey
{
    ELDAggressionOrigin Origin=ELDAggressionOrigin::Navigation;
    uint64 Sequence=0;
    uint32 CancelEpoch=0;
};

struct FLDAggressiveIntent
{
    ELDAggressiveIntentKind Kind=ELDAggressiveIntentKind::AttackUnit;
    FLDAggressionRequestKey Request;
};

UENUM()
enum class ELDProtectionOutcome : uint8 { NotProtected, Removed, Rejected };

struct FLDProtectionReceipt
{
    uint32 Generation=0;
    FLDCombatBodyRef Body;
    FLDAggressionRequestKey Request;
    ELDProtectionOutcome Outcome=ELDProtectionOutcome::Rejected;
    bool bProtectionRemoved=false;
    uint64 LifeRevision=0;
    ELDCommitReject Reason=ELDCommitReject::InvalidRequest;
};

// ---------------------------------------------------------------- death facts (Combat -> Life / Growth / Camps / Capture)

UENUM()
// PKArena (#198 session D): the victim was a PK life; PK pays only its own settlement points.
enum class ELDNoRewardReason : uint8 { None, NoOwner, SelfInflicted, OwnerLeft, PKArena };

UENUM()
enum class ELDDeathCause : uint8 { Damage };

struct FLDRewardBasis
{
    FName RewardDefinitionId;                 // contender: character id; camp member: monster id
    uint8 VictimLevelAtDeath=0;               // contender branch only; 0 for camp members
    uint64 VictimBuildRevisionAtDeath=0;      // contender branch only (last build revision Combat installed)
    bool bCreditOwnerEligibleAtDeath=false;   // another contender, still in the match at the fatal time
    ELDNoRewardReason NoRewardReason=ELDNoRewardReason::NoOwner; // None iff eligible
    uint8 CampAffixAtDeath=0;                 // P6 camp member branch only: ELDCampAffix of this life's camp spawn
    bool bPeddlerAtDeath=false;               // P6: the victim was the second-hand dealer
    uint8 CampStageAtDeath=0;                 // #97 camp member branch only: the growth stage this life spawned with
};

// Locked once at the first zero-HP commit; never rebuilt from HP, last attacker or current pawn.
struct FLDCommittedDeath
{
    FLDDeathKey Key;                      // LedgerMatch + VictimKey; the only dedup key
    FLDCombatBodyRef VictimBody;
    FGuid VictimContender;                // invalid for camp members
    FGuid FatalEffectId;                  // fingerprint only
    FGuid CreditOwner;                    // actual contender owner of the fatal effect; invalid = NoOwner
    FLDCombatSourceRef ReleasedSource;    // fingerprint only
    FLDCombatBodyRef SourceBodyAtRelease; // fingerprint only
    FLDRewardBasis RewardBasis;
    // Contender victims with an eligible killer only: the other contenders still in the match whose latest assist
    // contribution fell within REWARD.AssistWindowSec before the fatal hit, sorted. Frozen with the fact.
    TArray<FGuid> Assisters;
    // Contender victims only: the damage this life took within RecapWindowMs (REWARD.AssistWindowSec, the assist window)
    // before the fatal hit (LDDamageRecap::Summarize); the camp member definition of a fatal camp hit names that killer.
    TArray<FLDDamageLine> Recap;
    int64 RecapWindowMs=0;
    FName KillerMonsterId;
    ELDDeathCause Cause=ELDDeathCause::Damage;
    double OccurredAtSeconds=0;           // raw authority time of the fatal hit
    int64 OccurredAtMs=0;                 // checked ceil bucket of OccurredAtSeconds
};

// Committed hostile hit for camp retaliation (World consumes it idempotently by HitId). No hidden positions.
struct FLDCombatHostileContact
{
    FGuid HitId;
    FLDCombatBodyRef VictimBody;
    FLDCombatBodyRef SourceBody;   // releasing body (may have died since)
    FGuid CreditOwner;             // contender owner if any
    double OccurredAtSeconds=0;
};

// ---------------------------------------------------------------- build change (Growth compound commits -> Combat)

// #198: a learned active has one level; its values grow with the owner's level (LDSkillGrowth).
struct FLDLearnedActiveValue
{
    FName AbilityId;
    ELDCombatEntry Entry=ELDCombatEntry::None;   // Q, W or E
    // The skill's branch path; its rank follows from it (None = rank 1).
    ELDSkillBranch Branch=ELDSkillBranch::None;
};

// Complete post-change build. Empty arrays mean "none held"; nothing is merged with the previous build.
struct FLDBuildProjectionInput
{
    uint8 Level=0;
    // #198: the allocated points of each stat that holds any (canonical stat id, positive count).
    TArray<FLDStatPointsValue> StatPoints;
    TArray<FLDLearnedActiveValue> Actives;
    // Installed cards (FLDAttributeBuildValues::Cards), sorted by serial.
    TArray<FLDInstalledCardValue> Cards;
    // The taken final mod, recorded for the build blueprint only: combat reads nothing from it.
    FLDFinalModSelection FinalMod;
    // DEV test layer (FLDAttributeBuildValues::DevBonuses). Combat owns it: a Growth build change never names it and
    // keeps the installed one; only ALDCombatDirector::DevSetAttribute changes it, without a build revision.
    TArray<FLDDemoStatBonus> DevBonuses;
    // The grow cards' match growth (FLDAttributeBuildValues::CardGrowth). Combat owns it like DevBonuses: a Growth build
    // change keeps the installed one; only Combat's growth handlers change it, without a build revision.
    TArray<FLDDemoStatBonus> CardGrowth;
    uint64 ResultBuildRevision=0;      // strictly increasing
};

// The build revision Combat last installed for this contender (match start: 0 = the empty level-1 build).
struct FLDExpectedBuildVersions
{
    uint64 BuildRevision=0;
};

enum class ELDCombatPrepareError : uint8
{
    None, NotAuthority, StaleMatch, UnknownContender, StaleBuild, InvalidInput, Catalog,
    NumericUnderflow, NumericDomain, CapacityExceeded
};

// One row per learned active after the change; a relearned definition keeps its track inside Combat Prepare.
struct FLDCombatBindingPlan
{
    ELDCombatEntry Entry=ELDCombatEntry::None;
    FLDCombatSourceRef Source;
    FGuid IssuedCooldownId;      // persistent cooldown identity, one per learned definition
    int64 ReadyAtMs=0;           // inherited absolute end; <= now means ready
    bool bNewTrack=false;
};

// Opaque and move-only; only ALDCombatDirector::ApplyBuildChange consumes it inside the same permit scope.
class FLDPreparedCombatProjection
{
public:
    FLDPreparedCombatProjection(FLDPreparedCombatProjection&&)=default;
    FLDPreparedCombatProjection& operator=(FLDPreparedCombatProjection&&)=default;
    FLDPreparedCombatProjection(const FLDPreparedCombatProjection&)=delete;
    FLDPreparedCombatProjection& operator=(const FLDPreparedCombatProjection&)=delete;
    const TArray<FLDCombatBindingPlan>& Bindings() const { return BindingPlan; }
    const FLDAttributeProjection& Attributes() const { return Projection; }
    bool AppliesToLiveBody() const { return bAlive; }
private:
    friend class ALDCombatDirector;
    FLDPreparedCombatProjection()=default;
    FLDMatchContext Match;
    FGuid Contender;
    FLDExpectedBuildVersions Expected;
    FLDBuildProjectionInput Input;
    FLDAttributeProjection Projection;
    TArray<FLDCombatBindingPlan> BindingPlan;
    uint64 ExpectedSourceRevision=0;
    bool bAlive=false;
    FLDCombatBodyRef Body;
    uint64 ExpectedResourceRevision=0;
    int64 NewHPP2=0, NewMaxHPP2=0;
};

struct FLDCombatPrepareResult
{
    TOptional<FLDPreparedCombatProjection> Value;
    ELDCombatPrepareError Error=ELDCombatPrepareError::None;
    FString Message;
    bool IsSuccess() const { return Value.IsSet() && Error==ELDCombatPrepareError::None; }
};

// ---------------------------------------------------------------- camp members

struct FLDCampResetInput
{
    FLDCombatBodyRef Body;                 // current live CampMember body
    uint64 ReturnEpoch=0;                  // World-owned; reset key = LedgerMatch + Victim + ReturnEpoch
    FVector HomeFootCm=FVector::ZeroVector;
    // #28: World proved this return a dead end (CAMP.ReturnDeadEnd*); the reset applies where the member stands.
    bool bInPlace=false;
    uint64 ExpectedResourceRevision=0;     // from ReadCombatBody
    uint64 ExpectedStateRevision=0;
};

class FLDPreparedCampReset
{
public:
    FLDPreparedCampReset(FLDPreparedCampReset&&)=default;
    FLDPreparedCampReset& operator=(FLDPreparedCampReset&&)=default;
    FLDPreparedCampReset(const FLDPreparedCampReset&)=delete;
    FLDPreparedCampReset& operator=(const FLDPreparedCampReset&)=delete;
private:
    friend class ALDCombatDirector;
    FLDPreparedCampReset()=default;
    FLDCampResetInput Input;
    FLDMatchContext Match;
    int64 MaxHPP2=0;
};

struct FLDCampResetPrepareResult
{
    TOptional<FLDPreparedCampReset> Value;
    bool bAlreadyApplied=false;            // same key already reset: read-only replay, do not Apply
    ELDCombatPrepareError Error=ELDCombatPrepareError::None;
    FString Message;
};

// Server-only read of one registered body for the camp/AI owner's decisions about *its own* body.
struct FLDCombatBodyRead
{
    FLDCombatBodyRef Body;
    bool bPendingDeath=false;
    int64 HPP2=0, MaxHPP2=0, MomentumP2=0, MaxMomentumP2=0, ShieldP2=0;
    uint64 ResourceRevision=0;
    uint64 StateRevision=0;
    int64 RootedUntilMs=0;               // 0 when not rooted
    bool bActionInProgress=false;
    ELDCombatEntry ActionEntry=ELDCombatEntry::None;  // the winding-up action's entry while bActionInProgress
    int64 ActionReleaseAtMs=0;           // the winding-up action's release instant while bActionInProgress
    int64 NextAttackAtMs=0;
    double AttackRangeCm=0;
    bool bMovementAllowed=false;         // alive, not pending death, neither rooted nor stunned
    bool bAttackAllowed=false;           // alive, not pending death, neither disarmed nor stunned; Root does not block attacks
    bool bBasicAttackReady=false;        // attack allowed, no action in progress and now >= NextAttackAtMs
};

// ---------------------------------------------------------------- owner read model (HUD / AI self)

USTRUCT()
struct FLDCombatEntryView
{
    GENERATED_BODY()
    UPROPERTY() ELDCombatEntry Entry=ELDCombatEntry::None;
    UPROPERTY() ELDCombatSourceKind SourceKind=ELDCombatSourceKind::None;
    UPROPERTY() FName DefinitionId;                  // None = empty slot
    UPROPERTY() uint8 TargetMode=0;                  // ELDDemoTargetMode
    UPROPERTY() int32 RangeCm=0;
    UPROPERTY() int64 MomentumCostP2=0;
    // Skill rank 1..3 and branch path of a learned Q/W/E (ELDSkillBranch); 0/None for the innate dash or an empty slot.
    UPROPERTY() uint8 Rank=0;
    UPROPERTY() uint8 Branch=0;
    // A learned Q/W/E as it would release now: the product of its card damage factors (K09/K12/K16/K17/K18 and an armed
    // K05; 10000 = none) and the cooldown its next release would issue after haste and cards (0 when unknown).
    UPROPERTY() int64 DamageFactorBp=10000;
    UPROPERTY() int64 NextCooldownMs=0;
    UPROPERTY() FGuid IssuedCooldownId;              // persistent cooldown identity; owner view only
    UPROPERTY() int64 ReadyAtMs=0;                   // persistent track end; <= now means ready
    UPROPERTY() int64 CooldownMs=0;                  // duration last issued on this track, for the UI ratio
    // Skill charges (LDCombatSourceRules::SkillCharges); the ready ones follow from ReadyAtMs and CooldownMs at any instant
    // (LDCombatSourceRules::ChargesReady).
    UPROPERTY() uint8 Charges=1;
    UPROPERTY() ELDCombatReject Unavailable=ELDCombatReject::None; // current blocking reason, display only
};

// Control blocks for status display; the authority checks stay in Combat.
UENUM()
enum class ELDCombatControlBlock : uint8
{
    None=0, Move=1, Basic=2, Cast=4, Displacement=8
};

USTRUCT()
struct FLDCombatStatusView
{
    GENERATED_BODY()
    UPROPERTY() uint8 StatusId=0;                    // ELDDemoStatusId
    UPROPERTY() int64 ExpiresAtMs=0;
    UPROPERTY() int64 AmountP2=0;                    // shield amount; 0 otherwise
    UPROPERTY() uint8 ControlBlocks=0;               // ELDCombatControlBlock bits
    UPROPERTY() bool bIsPassive=false;
    UPROPERTY() uint8 Stacks=1;                      // #147 breach and burn layers; 1 otherwise
};

USTRUCT()
struct FLDCombatAttributeRowView
{
    GENERATED_BODY()
    UPROPERTY() uint8 Attribute=0;                   // ELDProjectedAttribute
    UPROPERTY() FLDDemoRational Base;
    UPROPERTY() FLDDemoRational Level;
    UPROPERTY() FLDDemoRational Choices;             // #198: the allocated stat points' flat term
    UPROPERTY() FLDDemoRational Cards;               // the installed cards' flat term (Y13, grow cards)
    UPROPERTY() FLDDemoRational PercentBonus;        // summed additive ratio terms of the build, the DEV layer excluded
    UPROPERTY() FLDDemoRational Dev;                 // DEV test layer flat term; zero outside a DEV test session
    UPROPERTY() FLDDemoRational DevPercent;          // DEV test layer ratio term
    UPROPERTY() FLDDemoRational Temporary;           // timed/conditional flat terms of the current life
    UPROPERTY() FLDDemoRational TemporaryPercent;    // timed/conditional ratio terms minus the strongest slow (signed)
    UPROPERTY() FLDDemoRational Final;
    UPROPERTY() bool bClamped=false;
};

// Action-cue facts of one body life at authority ms: the last released action, the dash end and the last landed hit
// (body state), plus the action winding up (filled when read). LDCombatCues selects the visible cue from them.
USTRUCT()
struct FLDCombatCueFacts
{
    GENERATED_BODY()
    UPROPERTY() bool bActing=false;
    UPROPERTY() bool bActingBasic=false;
    UPROPERTY() int64 ActingStartedAtMs=0;
    UPROPERTY() int64 ActingReleaseAtMs=0;
    UPROPERTY() bool bReleasedBasic=false;
    // The combo step (1..3) of the released basic; 0 for a released ability (hit feel picks its tier from it).
    UPROPERTY() uint8 ReleasedComboStep=0;
    UPROPERTY() int64 ReleasedStartedAtMs=0;
    UPROPERTY() int64 ReleasedAtMs=0;                // 0 = nothing released in this life
    // The learned Q/W/E of the acting non-basic action, else of the last released one (None for a basic or the innate
    // dash): the own body's skill look survives a release snapshot that already cleared the action slot.
    UPROPERTY() FName CastDefinitionId;
    UPROPERTY() int64 DashUntilMs=0;
    UPROPERTY() int64 LastHitAtMs=0;                 // 0 = not hit in this life
    UPROPERTY() FGuid LastHitId;                      // identity of the last resolved hit in this life
    UPROPERTY() uint8 LastHitLook=0;                  // ELDObservedHitLook; recent server-resolved hit presentation only
    UPROPERTY() uint8 LastHitFlags=0;                 // LDObservation::EHitFlags for this hit
    // Spec §12: the last reaction this life suffered (ELDCombatReaction) and when; observers float its name over the body.
    UPROPERTY() uint8 LastReaction=0;
    UPROPERTY() int64 LastReactionAtMs=0;
    // The incoming element that set it off (ELDCombatElement): the reaction's ink (the pair is symmetric).
    UPROPERTY() uint8 LastReactionElement=0;
    // The owner's hit receipt: the last blow this life landed (LDCombatCues::ConfirmsDealt). Owner-only; it never names
    // or locates the target.
    UPROPERTY() int64 LastDealtAtMs=0;               // 0 = nothing landed in this life
    UPROPERTY() uint8 LastDealtLook=0;               // ELDObservedHitLook
    UPROPERTY() uint8 LastDealtFlags=0;              // LDObservation::EHitFlags
    // Owner-only timestamp of the last tactical world pickup (slipper drop or recovery orb) in this life.
    UPROPERTY() int64 LastPickupAtMs=0;
    UPROPERTY() int64 LastRecoveryPackAtMs=0;
};

USTRUCT()
struct FLDCombatPreviewBonus
{
    GENERATED_BODY()
    UPROPERTY() TArray<FLDDemoStatBonus> Stats;
    UPROPERTY() int64 ExpiresAtMs=0;
};

USTRUCT()
struct FLDCombatPreviewSlow
{
    GENERATED_BODY()
    UPROPERTY() int64 RatioBp=0;
    UPROPERTY() int64 ExpiresAtMs=0;
};

// One installed growth card's current count on the owner's build, by card copy (Serial): K16 whole idle seconds of its ready
// skill and Y22 of no damage (the counted ones: in a fight, and the carry before it), K17 contender hits of its skill this
// match, K18 other skills released inside its window, Y23 whole seconds on the own ground state, Y24 open tally marks. The
// card's step times Count is its current bonus. A copy without a row counts 0. Owner-only.
USTRUCT()
struct FLDCombatCardStackView
{
    GENERATED_BODY()
    UPROPERTY() uint16 Serial=0;
    UPROPERTY() FName CardId;
    UPROPERTY() int32 Count=0;
};

// Single owner publication of the Combat self projection; consumers reject an older Revision.
USTRUCT()
struct FLDCombatOwnerView
{
    GENERATED_BODY()
    UPROPERTY() uint32 Generation=0;
    UPROPERTY() uint32 Life=0;
    UPROPERTY() uint64 Revision=0;
    UPROPERTY() int64 AsOfMs=0;
    UPROPERTY() bool bHasBody=false;
    UPROPERTY() int64 HPP2=0;
    UPROPERTY() int64 MaxHPP2=0;
    UPROPERTY() int64 MomentumP2=0;
    UPROPERTY() int64 MaxMomentumP2=0;
    // Spec §6.5: this owner's recent counted chain ends (oldest first; each serial once).
    UPROPERTY() TArray<FLDChainEndView> Chains;
    UPROPERTY() int64 ShieldP2=0;
    // Current effective HP regeneration in points/second; owner-only display, not writable resource state.
    UPROPERTY() double HPRegenPerSec=0;
    // The combo step of the last released basic (0 = none) and its AttackStart; a start after COMBO.ResetSec is step 1.
    UPROPERTY() uint8 ComboStep=0;
    UPROPERTY() int64 ComboStartMs=0;
    // Perfect dodges of this life (MOMENTUM.PerfectDodge) and the authority ms of the last: the owner's one-line notice.
    UPROPERTY() uint32 PerfectDodges=0;
    UPROPERTY() int64 LastPerfectDodgeAtMs=0;
    UPROPERTY() uint64 SourceRevision=0;
    // Inputs needed to reproduce this owner's build and temporary projection without mutating authority state.
    UPROPERTY() uint64 InstalledBuildRevision=0;
    UPROPERTY() bool bPreviewLiving=false;
    UPROPERTY() bool bPreviewFinalField=false;
    UPROPERTY() TArray<FLDDemoStatBonus> PreviewDevBonuses;
    // The grow cards' match growth Combat installed (Y19-Y21, LDAttributeProjection's CardGrowth term), living or dead.
    UPROPERTY() TArray<FLDDemoStatBonus> CardGrowth;
    // A12: the focus stacks of this life while they last (0 when none); the preview adds their haste like the authority.
    UPROPERTY() int32 PreviewFocusHits=0;
    // The growth cards' counts (FLDCombatCardStackView): whole steps and counts, so a row changes at most once a second.
    UPROPERTY() TArray<FLDCombatCardStackView> CardStacks;
    UPROPERTY() TArray<FLDCombatPreviewBonus> PreviewBonuses;
    UPROPERTY() TArray<FLDCombatPreviewSlow> PreviewMoveSlows;
    UPROPERTY() TArray<FLDCombatPreviewSlow> PreviewAttackSlows;
    UPROPERTY() TArray<FLDCombatEntryView> Entries;  // Q, W, Dash
    UPROPERTY() TArray<FLDCombatStatusView> Statuses;
    UPROPERTY() int64 NextAttackAtMs=0;
    UPROPERTY() ELDCombatEntry ActionEntry=ELDCombatEntry::None;
    UPROPERTY() ELDCombatPhase ActionPhase=ELDCombatPhase::Finished; // Accepted while a windup is pending
    UPROPERTY() uint64 ActionToken=0;
    UPROPERTY() int64 ActionReleaseAtMs=0;
    // Voluntary movement is blocked (Root or Stun); the client movement prediction reads it.
    UPROPERTY() bool bRooted=false;
    UPROPERTY() float MoveSpeedCmPerSec=0;
    // Spec §8.3: the own body stands on an enemy's ice: it accelerates and brakes at this scale (1 elsewhere); the client
    // movement prediction reads the same authoritative parameter.
    UPROPERTY() float MoveAccelScale=1.f;
    UPROPERTY() float AttackRangeCm=0;
    UPROPERTY() TArray<FLDCombatAttributeRowView> Attributes;
    UPROPERTY() bool bOutOfCombatRegenActive=false;
    // D21 (#79): the own body stands in a dorm, and the dorm's fast recovery is running.
    UPROPERTY() bool bInDorm=false;
    UPROPERTY() bool bDormRegenActive=false;
    // The own body's action cue facts; the owner selects its cue with LDCombatCues at the public clock.
    UPROPERTY() FLDCombatCueFacts Cues;
    // #198 combat window (absolute authority ms): until here the contender counts as in combat for the HUD's fold of the
    // field summary (#219); <= now means out of combat, 0 means no combat action yet. Persists across lives. #222: no build
    // command reads it.
    UPROPERTY() int64 BuildSafeAtMs=0;
    // The server-fixed duration of this life's respawn wait, for the owning player's countdown only.
    UPROPERTY() int64 RespawnDurationMs=0;
    // Auto-empower (LDAutoEmpower): whether the authority runs the experiment, the host the next empowered action takes and
    // its processor (ELDEmpowerProcessor), whether the owner chose it, the last host request judged, the bar it needs and
    // its price, and this life's empowered actions with the last one's instant (the owner's notice).
    UPROPERTY() bool bEmpowerEnabled=false;
    UPROPERTY() ELDCombatEntry EmpowerHost=ELDCombatEntry::None;
    UPROPERTY() uint8 EmpowerProcessor=0;
    UPROPERTY() bool bEmpowerHostChosen=false;
    UPROPERTY() uint64 EmpowerHostSequence=0;
    UPROPERTY() int64 EmpowerThresholdP2=0;
    UPROPERTY() int64 EmpowerCostP2=0;
    UPROPERTY() int32 Empowerments=0;
    UPROPERTY() int64 LastEmpowerAtMs=0;
};
