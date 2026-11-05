#pragma once

#include "CoreMinimal.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Navigation/LDOrderTypes.h"
#include "Combat/LDAttributeProjection.h"
#include "Development/LDDemoContentIds.h"
#include "LDCombatTypes.generated.h"

class AController;

// ---------------------------------------------------------------- sources and entries

// Innate: the D dash every contender has (D19); it is no learned or gear source and is never sold or forgotten.
UENUM()
enum class ELDCombatSourceKind : uint8 { None, Basic, Learned, Gear, Innate };

// Activation entry label only; never a source or cooldown identity. Basic comes only from
// Navigation-derived contender attacks and from registered camp members. Dash is the innate D dash.
UENUM()
enum class ELDCombatEntry : uint8 { None, Basic, Q, W, Item1, Item2, Dash };

USTRUCT()
struct FLDCombatSourceRef
{
    GENERATED_BODY()
    UPROPERTY() ELDCombatSourceKind Kind=ELDCombatSourceKind::None;
    UPROPERTY() FName DefinitionId;   // ability or equipment id; None for Basic; LDCombatSourceRules::InnateDashId for Innate
    UPROPERTY() FGuid ItemInstance;   // Gear only
    bool operator==(const FLDCombatSourceRef& Other) const
    { return Kind==Other.Kind && DefinitionId==Other.DefinitionId && ItemInstance==Other.ItemInstance; }
    bool operator!=(const FLDCombatSourceRef& Other) const { return !(*this==Other); }
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

// Direct request; DirectSequence is monotonic per contender (Q/W/1/2/D share it) or per camp member.
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
    Cancelled, StaleSource, SourceUnavailable, NoActiveEffect, Busy, Cooldown, AttackNotReady,
    InsufficientMana, ControlBlocked, TargetUnavailable, InvalidDirection, OutOfRange, Blocked, Dead,
    MatchClosed, AuthorityDataUnavailable, CapacityUnavailable
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
enum class ELDAggressiveIntentKind : uint8 { AttackUnit, AttackMove, BasicAttack, ActivateCharacter, ActivateEquipment };

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
enum class ELDNoRewardReason : uint8 { None, NoOwner, SelfInflicted, OwnerLeft };

UENUM()
enum class ELDDeathCause : uint8 { Damage };

struct FLDRewardBasis
{
    FName RewardDefinitionId;                 // contender: character id; camp member: monster id
    uint8 VictimLevelAtDeath=0;               // contender branch only; 0 for camp members
    uint64 VictimBuildRevisionAtDeath=0;      // contender branch only (last build revision Combat installed)
    bool bCreditOwnerEligibleAtDeath=false;   // another contender, still in the match at the fatal time
    ELDNoRewardReason NoRewardReason=ELDNoRewardReason::NoOwner; // None iff eligible
    int64 LeaderBountyGoldAtDeath=0;          // D27 contender branch only: the shown leader's bounty at the fatal moment
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

struct FLDLearnedActiveValue
{
    FName AbilityId;
    uint8 Rank=0;                                // 1..3
    ELDCombatEntry Entry=ELDCombatEntry::None;   // Q or W
};

struct FLDEquippedItemValue
{
    ELDCombatEntry Entry=ELDCombatEntry::None;   // Item1 or Item2 (slot order)
    FGuid Instance;
    FName DefinitionId;
    uint64 ItemRevision=0;
};

// Complete post-change build. Empty arrays mean "none held"; nothing is merged with the previous build.
struct FLDBuildProjectionInput
{
    uint8 Level=0;
    TArray<FLDLearnedPassiveValue> Passives;
    TArray<FLDAttributeCardValue> AttributeCards;
    TArray<FLDLearnedActiveValue> Actives;
    TArray<FLDEquippedItemValue> Equipment;
    // DEV test layer (FLDAttributeBuildValues::DevBonuses). Combat owns it: a Growth build change never names it and
    // keeps the installed one; only ALDCombatDirector::DevSetAttribute changes it, without a build revision.
    TArray<FLDDemoStatBonus> DevBonuses;
    uint64 ResultBuildRevision=0;      // strictly increasing
    uint64 ResultInventoryRevision=0;  // strictly increasing or unchanged
};

// Revisions Combat last installed for this contender (match start: 0/0 = the empty level-1 build).
struct FLDExpectedBuildVersions
{
    uint64 BuildRevision=0;
    uint64 InventoryRevision=0;
};

enum class ELDCombatPrepareError : uint8
{
    None, NotAuthority, StaleMatch, UnknownContender, StaleBuild, InvalidInput, Catalog,
    NumericUnderflow, NumericDomain, CapacityExceeded
};

// One row per held active source after the change; debt-first rebinding is decided inside Combat Prepare.
struct FLDCombatBindingPlan
{
    ELDCombatEntry Entry=ELDCombatEntry::None;
    FLDCombatSourceRef Source;
    FGuid IssuedCooldownId;      // persistent cooldown identity (gear: <=2 per definition, learned: 1)
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
    int64 NewHPP2=0, NewMaxHPP2=0, NewMPP2=0, NewMaxMPP2=0;
};

struct FLDCombatPrepareResult
{
    TOptional<FLDPreparedCombatProjection> Value;
    ELDCombatPrepareError Error=ELDCombatPrepareError::None;
    FString Message;
    bool IsSuccess() const { return Value.IsSet() && Error==ELDCombatPrepareError::None; }
};

// Read-only shop preview of the cooldown a newly bought copy would inherit (same debt-first rule as Prepare).
struct FLDGearBindingPreview
{
    bool bValid=false;
    bool bHasActive=false;
    bool bInherits=false;
    int64 ReadyAtMs=0;
    FString Error;
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
    int64 MaxHPP2=0, MaxMPP2=0;
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
    int64 HPP2=0, MaxHPP2=0, MPP2=0, MaxMPP2=0, ShieldP2=0;
    uint64 ResourceRevision=0;
    uint64 StateRevision=0;
    int64 RootedUntilMs=0;               // 0 when not rooted
    bool bActionInProgress=false;
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
    UPROPERTY() FGuid ItemInstance;                  // own gear instance; owner view only
    UPROPERTY() uint8 Rank=0;                        // learned only
    UPROPERTY() bool bHasActive=false;               // gear without an active is shown unavailable
    UPROPERTY() uint8 TargetMode=0;                  // ELDDemoTargetMode
    UPROPERTY() int32 RangeCm=0;
    UPROPERTY() int64 ManaCostP2=0;
    UPROPERTY() FGuid IssuedCooldownId;              // persistent cooldown identity; owner view only
    UPROPERTY() int64 ReadyAtMs=0;                   // persistent track end; <= now means ready
    UPROPERTY() int64 CooldownMs=0;                  // duration last issued on this track, for the UI ratio
    UPROPERTY() ELDCombatReject Unavailable=ELDCombatReject::None; // current blocking reason, display only
};

// Control blocks for status display; the authority checks stay in Combat.
UENUM()
enum class ELDCombatControlBlock : uint8
{
    None=0, Move=1, Basic=2, Cast=4, Gear=8, Displacement=16
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
};

USTRUCT()
struct FLDCombatAttributeRowView
{
    GENERATED_BODY()
    UPROPERTY() uint8 Attribute=0;                   // ELDProjectedAttribute
    UPROPERTY() FLDDemoRational Base;
    UPROPERTY() FLDDemoRational Level;
    UPROPERTY() FLDDemoRational Choices;
    UPROPERTY() FLDDemoRational Passives;
    UPROPERTY() FLDDemoRational Equipment;
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
    UPROPERTY() bool bReleasedBasic=false;
    UPROPERTY() int64 ReleasedStartedAtMs=0;
    UPROPERTY() int64 ReleasedAtMs=0;                // 0 = nothing released in this life
    UPROPERTY() int64 DashUntilMs=0;
    UPROPERTY() int64 LastHitAtMs=0;                 // 0 = not hit in this life
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
    UPROPERTY() int64 MPP2=0;
    UPROPERTY() int64 MaxMPP2=0;
    UPROPERTY() int64 ShieldP2=0;
    // Current effective regeneration in points/second; owner-only display, not writable resource state.
    UPROPERTY() double HPRegenPerSec=0;
    UPROPERTY() double MPRegenPerSec=0;
    UPROPERTY() uint64 SourceRevision=0;
    UPROPERTY() TArray<FLDCombatEntryView> Entries;  // Q, W, Item1, Item2, Dash
    UPROPERTY() TArray<FLDCombatStatusView> Statuses;
    UPROPERTY() int64 NextAttackAtMs=0;
    UPROPERTY() ELDCombatEntry ActionEntry=ELDCombatEntry::None;
    UPROPERTY() ELDCombatPhase ActionPhase=ELDCombatPhase::Finished; // Accepted while a windup is pending
    UPROPERTY() uint64 ActionToken=0;
    UPROPERTY() int64 ActionReleaseAtMs=0;
    // Voluntary movement is blocked (Root or Stun); the client movement prediction reads it.
    UPROPERTY() bool bRooted=false;
    UPROPERTY() float MoveSpeedCmPerSec=0;
    UPROPERTY() float AttackRangeCm=0;
    UPROPERTY() TArray<FLDCombatAttributeRowView> Attributes;
    UPROPERTY() TArray<FName> EquipmentPassives;     // once per family
    UPROPERTY() bool bOutOfCombatRegenActive=false;
    // D21 (#79): the own body stands in a dorm, and the dorm's fast recovery is running.
    UPROPERTY() bool bInDorm=false;
    UPROPERTY() bool bDormRegenActive=false;
    // The own body's action cue facts; the owner selects its cue with LDCombatCues at the public clock.
    UPROPERTY() FLDCombatCueFacts Cues;
};
