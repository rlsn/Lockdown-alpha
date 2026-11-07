#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/LDCombatEvaluationTypes.h"
#include "Combat/LDAttributeProjection.h"
#include "Combat/LDAssistRules.h"
#include "Combat/LDCombatTypes.h"
#include "Combat/LDCombatControlRules.h"
#include "Combat/LDCombatMechanics.h"
#include "Combat/LDCombatKeywords.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Vision/LDViewTypes.h"
#include "LDCombatDirector.generated.h"

struct FLDDormZones;
namespace LDAirdrop { struct FCandidate; }

class ALDCharacter;
class ALDGameMode;
class ALDPlayerController;
class FLDDemoContentSnapshot;
enum class ELDGrowthReason : uint16;
struct FLDDemoAbilityDefinition;
struct FLDDemoEquipmentDefinition;
struct FLDDemoRankDefinition;
namespace LDTactical { struct FOutOfCombatRecovery; }
struct FLDDemoTargetDefinition;
struct FLDInnateDashTerms;

enum class ELDCombatActionKind : uint8 { Basic, Ability };

// path_slow (active_bail_out advanced): at the end of the dash every body along the travelled segment is slowed.
struct FLDCombatDashSweep
{
    FLDCombatBodyRef Body;
    FGuid EffectId;
    uint64 Token=0;
    FLDCombatSourceRef Source;
    FVector StartFootCm=FVector::ZeroVector;
    int64 EndAtMs=0;
    int64 SlowBp=0;
    int64 SlowMs=0;
    double HalfWidthCm=0;
};

// #147 damage-triggered shield with a burst: when the shield it granted (ShieldEffectId) is broken, the holder answers
// the bodies within RadiusCm with RatioBp of the granted amount as magic damage.
struct FLDCombatShieldBurst
{
    FGuid ShieldEffectId;
    FLDCombatSourceRef Source;
    int64 GrantedP2=0;
    int64 RatioBp=0;
    double RadiusCm=0;
};

// One passive effect a body holds (learned rank or equipment family), resolved from the approved catalog.
struct FLDPassiveEffectRef
{
    FString Key;                    // ability id or equipment family; the dedup key of per-source marks
    FLDCombatSourceRef Source;
    const FLDDemoEffectDefinition* Effect=nullptr;
};

// One accepted, not yet released action of one body life. Dies with the life.
struct FLDCombatAction
{
    ELDCombatActionKind Kind=ELDCombatActionKind::Basic;
    FLDCombatRequestKey Key;
    uint64 Token=0;
    ELDCombatEntry Entry=ELDCombatEntry::None;
    FLDCombatSourceRef Source;
    uint8 Rank=0;
    FLDCombatTarget Target;
    FLDCombatBodyRef TargetBody;
    FVector Direction=FVector::ZeroVector;
    FVector TargetPoint=FVector::ZeroVector;  // GroundPoint: the requested point (planar use only)
    int64 StartedAtMs=0;
    int64 ReleaseAtMs=0;
    uint64 SourceRevision=0;
};

struct FLDBodyCombatState
{
    FGuid Identity;
    FLDVictimKey Victim;
    struct FResources
    {
        int64 HpP2=0,MaxHpP2=0,MpP2=0,MaxMpP2=0,ShieldP2=0;
        uint64 Revision=0;
    } Resources;
    FLDBasicTiming BasicTiming;
    // Effective attributes: BuildProjection plus the temporary terms below (LDAttributeProjection::ApplyTemporary).
    FLDAttributeProjection AttributeProjection;
    // The installed build (or monster) projection without temporary terms.
    FLDAttributeProjection BuildProjection;
    // The temporary terms AttributeProjection currently includes.
    FLDTemporaryAttributeTerms AppliedTemporary;
    uint64 AttributeRevision=0;
    TOptional<FLDObservedPublicFacts> PublicFacts;
    FName MonsterId;                       // CampMember bodies only
    uint64 StateRevision=1;                // statuses/action changes; resources use Resources.Revision
    TArray<FLDCombatRoot> Roots;
    TArray<FLDCombatSlow> Slows;
    TArray<FLDCombatTimedBonus> TimedBonuses;
    // Hard-control diminishing returns of this life (LDCombatControl::DiminishHardControl).
    FLDHardControlChain HardControl;
    // item_edge_blade's landed-basic count.
    int32 LandedBasics=0;
    // Once-per-release mana grant of item_runic_focus (ability hit).
    FGuid LastAbilityManaEffect;
    // Disarm and Stun instances (LDCombatControl); each keeps its effect, source and half-open expiry.
    TArray<FLDCombatRoot> Disarms;
    TArray<FLDCombatRoot> Stuns;
    // The armed next-basic bonus (one consumption) and this life's thorns repeat windows.
    TOptional<FLDCombatEmpower> Empower;
    TArray<FLDThornsMark> ThornsMarks;
    // Assist contributions other contenders made to this life (LDAssist); read once when the life locks its death.
    TArray<FLDAssistContribution> AssistContributions;
    TOptional<FLDCombatShield> Shield;
    TOptional<FLDCombatTaunt> Taunt;       // contenders only
    TOptional<FLDCombatAction> Action;
    int64 LastCombatAtMs=0;
    int64 RegenThroughMs=0;
    // D21 (#79): this contender's foot stood inside a dorm at the last regeneration settle.
    bool bInDorm=false;
    int64 HpRegenMicroP2=0, MpRegenMicroP2=0; // sub-P2 remainders in 1e-6 P2
    int64 MonsterNextAttackAtMs=0;         // contenders keep their cadence on the persistent record
    int64 DashUntilMs=0;
    // Released action and last landed hit of this life for the visible action cue.
    FLDCombatCueFacts Cues;
    // Source of the last released action (the visible Cast cue's ability once its windup released).
    FLDCombatSourceRef ReleasedSource;
    uint64 ResetThroughEpoch=0;
    bool bPendingDeath=false;
    // #112 C2 mechanics (LDCombatMechanicsDirector.cpp): forced displacement windows (blocking every voluntary action).
    TArray<FLDCombatRoot> Knockbacks;
    // Increments on every knockback that lands; Navigation drops the body's current order when it changes.
    uint64 KnockbackSerial=0;
    // The amount the current shield record was granted with (item_privacy_screen answers a share of it).
    int64 ShieldGrantedP2=0;
    int64 RetortReadyAtMs=0;
    // #147 build v7 keywords (LDCombatKeywords): silence instances (shaped like disarm and stun), the one shared breach
    // and burn of this life, the damage-triggered shield's rest and the burst its current shield carries.
    TArray<FLDCombatRoot> Silences;
    FLDCombatSharedBreach SharedBreach;
    FLDCombatBurn Burn;
    int64 DamagedShieldReadyAtMs=0;
    TOptional<FLDCombatShieldBurst> ShieldBurst;
};

// Persistent cooldown identity; the entry slot is only its current binding.
struct FLDCombatTrack
{
    FGuid IssuedCooldownId;
    ELDCombatSourceKind Kind=ELDCombatSourceKind::None;
    FName DefinitionId;
    int64 ReadyAtMs=0;
    int64 LastDurationMs=0;
};

struct FLDCombatDirectEntry
{
    FLDCombatActionRequest Request;
    FLDCombatActionReceipt Receipt;
};

// Per-entity Combat record that outlives body lives; owns build projection, cooldown tracks,
// the persistent basic cadence and the Direct request window.
struct FLDCombatEntityRecord
{
    ELDVictimKind Kind=ELDVictimKind::Contender;
    FGuid Entity;
    FName MonsterId;          // CampMember only: resolves a camp killer's tier after its body is gone
    FLDBuildProjectionInput Build;
    FLDExpectedBuildVersions Installed;
    FLDAttributeProjection Projection;
    TArray<FLDCombatTrack> Tracks;
    uint64 SourceRevision=1;
    int64 NextAttackAtMs=0;
    uint64 DirectHighWater=0;
    uint64 CancelledThrough=0;
    TArray<FLDCombatDirectEntry> Recent;
    TArray<FLDCombatActionRequest> Pending;
    uint64 NextToken=1;
    uint64 ReceiptRevision=0;
    uint64 ViewRevision=0;
    uint64 PublishedViewRevision=0;
};

// One committed hit waiting for the Effects phase.
struct FLDCombatHit
{
    FGuid HitId;
    FGuid EffectId;
    uint64 Token=0;
    FLDCombatBodyRef Target;
    FLDCombatBodyRef SourceBody;
    FGuid SourceEntity;
    FGuid CreditOwner;
    FLDCombatSourceRef Source;
    ELDCombatHitClass HitClass=ELDCombatHitClass::None;
    int64 BaseP2=0, ADRatioBp=0, APRatioBp=0;
    ELDDemoDamageType DamageType=ELDDemoDamageType::Physical;
    int64 SourceADP2=0, SourceAPP2=0;
    // Coefficient of the source's max HP and that max HP snapshotted at release.
    int64 MaxHPRatioBp=0, SourceMaxHPP2=0;
    bool bCritical=false;
    int64 RootDurationMs=0;
    int64 SlowBp=0, SlowDurationMs=0;
    // Snapshot of the releasing source's control amplification (passive_kick_down), applied when the target is under any
    // control or slow.
    int64 ControlAmpBp=0;
    int64 DisarmDurationMs=0;
    int64 StunDurationMs=0;
    // A reflection never crits, heals, cleaves or triggers another reflection.
    bool bReflect=false;
    // On-hit effects (lifesteal, cleave) trigger at this coefficient; 0 = none. A primary basic is 10000; hits made by
    // on-hit effects, reflections or added damage are always 0. SnapshotOnHit fills the terms.
    int64 OnHitScaleBp=0;
    bool bCleave=false;
    int64 CleaveRatioBp=0;
    double CleaveRadiusCm=0;
    bool bLifesteal=false;
    int64 LifestealRatioBp=0;
    // Ability critical (STAT.AbilityCritMultiplier), decided once per hit at ApplyHit: rolled at this chance
    // (passive_lucky_star advanced).
    int64 AbilityCritChanceBp=0;
    // Added damage (item_edge_blade, path slows): never an on-hit trigger.
    bool bAddedDamage=false;
    // #112 C2 (LDCombatMechanicsDirector.cpp).
    bool bPeriodic=false;            // a zone pulse or burn tick: no crit, no on-hit
    bool bFromShieldTrigger=false;   // a retort or burst hit: never a Q/W hit for passives
    bool bSilent=false;              // a zone's periodic slow refresh: no visible hit cue
    int64 ExtraAmplifyBp=10000;      // source-side multiplier joining the damage bonus pool (item_megaphone on a burn tick)
    double KnockbackCm=0;
    int64 KnockbackMs=0;
    bool bKnockbackFromAnchor=false; // pushed away from KnockbackAnchorCm instead of the source body's foot
    FVector KnockbackAnchorCm=FVector::ZeroVector;
    // #147 build v7 keyword payload (docs/systems/COMBAT_AND_GROWTH.md).
    int64 SilenceDurationMs=0;
    int32 BreachStacks=0;            // shared breach stacks this hit applies
    int32 BurnStacks=0;              // burn layers this hit applies, at the applier level and AP snapshotted at release
    int32 BurnLevel=1;
    int64 BurnAPP2=0;
    bool bBurnTick=false;            // a burn tick: no crit, no on-hit, never a Q/W hit for passives
    int64 ExecuteBp=0;               // pool bonus against a target below ExecuteBelowBp of its max HP before this hit
    int64 ExecuteBelowBp=0;
    int64 ControlAmpBelowBp=0;       // the source's control amplification also counts a target below this HP share
    int64 SlowedStunMs=0;            // a target slowed at this hit is stunned this long instead of rooted
    bool bPierce=false;              // a projectile keeps flying through the bodies it hits (each body life once)
    double WallCheckCm=0;            // a knockback that travels less than this met a wall and stuns WallStunMs
    int64 WallStunMs=0;
    int64 LifestealLowHPBp=0;        // extra on-hit lifesteal against a target below LifestealLowHPBelowBp
    int64 LifestealLowHPBelowBp=0;
    int64 LowHPBasicLifestealBp=0;   // the source's primary-basic lifesteal while it is below SourceLowHPBelowBp
    int64 SourceLowHPBelowBp=0;      // the source's low-HP passive threshold (passive_pressed)
};

// Released straight, non-homing projectile: first legal hostile body or blocking terrain ends it.
struct FLDCombatProjectile
{
    // The combat effect this flight delivers.
    FGuid EffectId;
    // This flight's own identity, fixed when it is scheduled and distinct from every other flight; observers key on it.
    FGuid FlightId;
    uint64 Token=0;
    FLDCombatBodyRef SourceBody;
    FGuid SourceEntity;
    FGuid CreditOwner;
    FLDCombatSourceRef Source;
    int64 BaseP2=0, ADRatioBp=0, APRatioBp=0;
    ELDDemoDamageType DamageType=ELDDemoDamageType::Magical;
    int64 SourceADP2=0, SourceAPP2=0;
    int64 MaxHPRatioBp=0, SourceMaxHPP2=0;
    FVector OriginCm=FVector::ZeroVector;
    FVector Direction=FVector::ZeroVector;
    double SpeedCmPerSec=0, RangeCm=0, RadiusCm=0;
    double TravelledCm=0;
    int64 AdvancedThroughMs=0;
    int64 SlowBp=0, SlowDurationMs=0;
    // #147 pierce: the body lives this flight already struck (each at most once).
    TArray<FLDCombatBodyRef> Struck;
    // The complete hit payload snapshotted at release; the contact copies it and names the struck body.
    FLDCombatHit Payload;
};

// A ground zone (EFFECT zone): Count pulses from StartMs+DelayMs every IntervalMs, a slow kept on enemies inside, and an
// optional root on the enemies still inside when it ends. Its owner's payload terms are snapshotted at release.
struct FLDCombatZone
{
    FGuid EffectId;
    FLDCombatBodyRef SourceBody;
    FGuid SourceEntity;
    FGuid CreditOwner;
    FLDCombatSourceRef Source;
    FVector CentreCm=FVector::ZeroVector;
    double RadiusCm=0;
    int64 StartMs=0, EndMs=0, DelayMs=0, IntervalMs=0;
    int32 Count=0, Pulsed=0;
    bool bSlow=false;
    int64 SlowBp=0, SlowCheckMs=0, SlowLingerMs=0, NextSlowAtMs=0;
    int64 EndRootMs=0;
    bool bDamage=false;
    int64 BaseP2=0, ADRatioBp=0, APRatioBp=0;
    ELDDemoDamageType DamageType=ELDDemoDamageType::Magical;
    int64 SourceADP2=0, SourceAPP2=0;
    // #147 zone_burn: BurnStacks layers (at the owner's level and AP snapshotted at release) on every enemy inside,
    // every BurnIntervalMs from the zone's start while it lasts.
    int32 BurnStacks=0;
    int32 BurnLevel=1;
    int64 BurnIntervalMs=0, NextBurnAtMs=0, BurnAPP2=0;
};

// #147 echo: a self-area release blasts again at AtMs around the same living body's foot with its scaled payload.
struct FLDCombatEcho
{
    FLDCombatBodyRef Body;
    int64 AtMs=0;
    double RadiusCm=0;
    FLDCombatHit Template;
};

// #147 wall stun: when a committed knockback's window ends, a body life that travelled less than ThresholdCm from
// StartFootCm met a wall and is stunned (through the single control path, credited to the push's source).
struct FLDCombatWallCheck
{
    FLDCombatBodyRef Target;
    FGuid EffectId;
    FGuid SourceEntity;
    FLDCombatBodyRef SourceBody;
    FGuid CreditOwner;
    FVector StartFootCm=FVector::ZeroVector;
    int64 AtMs=0;
    double ThresholdCm=0;
    int64 StunMs=0;
};

UCLASS()
class LOCKDOWN_API ALDCombatDirector : public AActor
{
    GENERATED_BODY()
public:
    ALDCombatDirector();
    virtual void Tick(float Delta) override;
    const FLDBodyCombatState* StateFor(const ALDCharacter* Body) const;
    // Authority footpoint: capsule centre minus scaled half-height (same as Vision).
    static FVector FootOf(const ALDCharacter& Body);
    // D26 (#79): the current foot of every registered living contender body (no pending death), for the airdrop pickup.
    void ReadLivingContenderFeet(TArray<LDAirdrop::FCandidate>& Out) const;
    // The sole authority driver: advances native steps and runs every commit phase in order.
    bool AdvanceAuthority(const FLDMatchContext& Match,int64 ThroughMs,FString& Error);

    // ---- Commands shared by human RPCs, the opponent AI and registered camp members.
    bool ResolveControllerCaller(const AController& Controller, FLDCombatCaller& Out, FString& Error) const;
    // Receipt only; Queued intents are admitted in the next Actions phase. bQueuedStep: a shift-queued step Navigation
    // checked as unseen when it was queued; a newer Direct sequence judged since then does not expire it
    // (LDCombatSourceRules::DirectSequenceVerdict).
    FLDCombatActionReceipt SubmitAction(const FLDCombatCaller& Caller, const FLDCombatActionRequest& Request, bool bQueuedStep=false);
    // Navigation refused a fresh Direct request before Combat judged it (2026-11-07: a skill that cannot start keeps the
    // current order and the shift queue). Records the refusal in the receipt window without moving the high-water mark,
    // so a replay answers the same refusal, changed terms conflict, and older queued sequences stay runnable.
    bool RecordDirectRefusal(const FLDCombatCaller& Caller, const FLDCombatActionRequest& Request, ELDCombatReject Reject,
        FLDCombatActionReceipt& Out);
    FLDCombatActionReceipt CancelAction(const FLDCombatCaller& Caller, const FLDCombatCancelRequest& Request);
    FLDCombatActionReceipt QueryActionReceipt(const FLDCombatCaller& Caller, uint64 DirectSequence) const;
    FLDCombatEntryView QueryAvailability(const FLDCombatCaller& Caller, ELDCombatEntry Entry) const;
    bool BuildOwnerView(const FLDCombatCaller& Caller, FLDCombatOwnerView& Out) const;

    // ---- Navigation and camp bodies (server, inside the current Actions permit).
    bool ReadCombatBody(const FLDCombatBodyRef& Body, FLDCombatBodyRead& Out) const;
    FLDCombatActionReceipt StartNavAttack(const FLDCombatCaller& Caller, const FLDCombatRequestKey& Key,
        const FLDCombatBodyRef& Target, const FGuid& Observed, uint32 ObservationEpoch,
        const FLDGameplayWritePermit& Permit);
    // Navigation's approach-and-cast orders (unit, ground-point and projectile Q/W, 2026-11-06 decisions): judges the
    // owner's Direct request now, inside the Actions permit, exactly as a queued Direct request would be judged.
    // OutOfRange and Busy (and Blocked sight to a unit) leave the request unrecorded (bFinal false) so the order keeps
    // closing in or waiting; every other outcome records, reports and returns it like an ordinary Direct receipt
    // (bFinal true). False only on an authority fault. bQueuedStep: the cast order came from the shift queue.
    bool StartNavCast(const FLDCombatCaller& Caller, const FLDCombatActionRequest& Request, const FLDGameplayWritePermit& Permit,
        FLDCombatActionReceipt& Out, bool& bFinal, FString& Error, bool bQueuedStep=false);
    // Cancels the unreleased basic attack of this navigation order; the committed cadence is kept. A swing at Keep
    // (a valid body: the taunter that replaced the order) is left to land, since the taunt still allows it.
    void CancelNavAttack(const FGuid& Entity, const FLDMoveIdentity& Move, const FLDCombatBodyRef& Keep=FLDCombatBodyRef());
    // A newer AttackUnit order of the same life aimed at the same observed target takes over the unreleased basic
    // of the order it replaces (re-clicking the chased target does not throw the committed swing away). The swing is
    // re-keyed to the newer order so that order's own end (Stop, a new order) still cancels it. True when adopted.
    bool AdoptNavAttack(const FGuid& Entity, const FLDMoveIdentity& From, const FLDMoveIdentity& To,
        const FGuid& Observed, uint32 ObservationEpoch);
    // Voluntary movement blocked by Root or Stun (LDCombatControl matrix); a taunt forces movement instead.
    bool IsMoveBlocked(const ALDCharacter& Body) const;
    // The control matrix for one registered body at the current authority time: Root/Disarm/Stun (LDCombatControl)
    // and a taunt's ban on abilities and displacement (LDControl::TauntBlocks).
    bool IsActionBlocked(const FLDBodyCombatState& Body, ELDCombatControlAction Action) const;
    bool ReadMoveSpeed(const ALDCharacter& Body, float& OutCmPerSec) const;
    // The taunter's exact body while Victim is taunted now; false otherwise. Navigation forces its order from this.
    bool ReadTaunter(const FLDCombatBodyRef& Victim, FLDCombatBodyRef& OutTaunter) const;
    // Knockbacks that landed on this body life so far; Navigation drops the current order whenever it changes.
    uint64 ReadKnockbackSerial(const FLDCombatBodyRef& Body) const;
    // Public Root/Shield cues of a registered body at NowMs; protection is derived by the life domain.
    void AppendObservedStatuses(const ALDCharacter& Body, int64 NowMs, TArray<FLDObservedStatus>& Out) const;
    // Action cue of a registered body at NowMs (LDCombatCues); None after a lethal hit (LDCombatObservation.cpp).
    // Fails (with Error) when a visible cast has no approved source, so the publication fails rather than hiding it.
    bool AppendObservedAction(const ALDCharacter& Body, int64 NowMs, FLDObservedPublicFacts& InOut, FString& Error) const;
    // The approved native id of the Q/W ability the body's visible Cast cue shows at NowMs; zero when no Cast cue shows
    // or the cast is the innate dash. Fails only when a cast source has no approved definition.
    bool ObservedCastSource(const ALDCharacter& Body, int64 NowMs, uint8& OutAbility) const;
    // Every projectile still in flight after the last Effects advance, for the observation owner to sample.
    // Fails without output for a projectile whose source has no approved look.
    bool ReadObservedProjectiles(TArray<FLDObservedProjectileFact>& Out, FString& Error) const;
    // A body life's cue facts with its winding-up action and dash filled in.
    static FLDCombatCueFacts CueFactsOf(const FLDBodyCombatState& Body);
    // Public Q/W facts of a build (#112 §10): each held active's native id and rank in its slot. False when a held active
    // has no approved active definition, a rank outside 1..MaxSkillRank or a slot other than Q/W.
    static bool PublicActivesOf(const FLDBuildProjectionInput& Build, const FLDDemoContentSnapshot& Content,
        FLDObservedActivesView& Out);

    // ---- Growth compound commits.
    // A locked death crediting this contender (killer or assister) still awaits its reward (this boundary's set or its one retry).
    bool OwesRewardTo(const FGuid& Recipient) const;
    FLDCombatPrepareResult PrepareBuildChange(const FLDCombatCaller& Caller, const FLDBuildProjectionInput& Input,
        const FLDExpectedBuildVersions& Expected) const;
    void ApplyBuildChange(FLDPreparedCombatProjection&& Prepared, const FLDGameplayWritePermit& Permit);

#if WITH_DEV_AUTOMATION_TESTS
    // ---- DEV test commands (LDDevTestProtocol.h), committed by ALDEconomyDirector inside the Transactions permit.
    // The contender's current Alive body to its committed HP/MP maxima (the shield is untouched). False with Detail when
    // the permit, the contender record or a living body is missing; nothing changes then.
    bool DevRefillResources(const FGuid& Contender, const FLDGameplayWritePermit& Permit, FString& Detail);
    // Every cooldown track of the contender (learned, held or forgotten, and the innate dash) ready at the permit
    // boundary; tracks keep their identity. False with Detail when the permit or the contender record is missing.
    bool DevResetCooldowns(const FGuid& Contender, const FLDGameplayWritePermit& Permit, FString& Detail);
    // Pure parts of the two commands. RefillPool returns whether HP or MP changed; ResetTracks the number of tracks
    // that were cooling down at NowMs.
    static bool DevRefillPool(FLDBodyCombatState::FResources& Pool);
    static int32 DevResetTracks(TArray<FLDCombatTrack>& Tracks, int64 NowMs);
    // The DEV test layer of the installed build (FLDBuildProjectionInput::DevBonuses): bClearAll empties it, otherwise
    // Stat's term becomes Amount in its unit (0 removes it). The installed build is reprojected and installed through
    // PrepareProjected/ApplyBuildChange (resource maxima keep their ratio, a living body keeps its temporary terms)
    // without a build or inventory revision, so Growth's in-flight requests stay valid; a dead contender changes only
    // the persistent build of its next life. NumericDomain with Detail when the projection refuses (a final value
    // outside its catalog bound), nothing changes then.
    ELDGrowthReason DevSetAttribute(const FGuid& Contender, bool bClearAll, ELDDemoBonusStat Stat, int64 Amount,
        const FLDGameplayWritePermit& Permit, FString& Detail);
    // Pure part: whether Bonuses changed. Terms stay sorted by stat.
    static bool DevApplyAttribute(TArray<FLDDemoStatBonus>& Bonuses, bool bClearAll, ELDDemoBonusStat Stat, int64 Amount);
    // DEV test reset to the match start, part 1 (pure, inside the reset composite): the empty level-1 build without a
    // DEV layer at Growth's and the ledger's result revisions, through PrepareProjected like any build change.
    FLDCombatPrepareResult DevPrepareReset(const FGuid& Contender, uint64 ResultBuildRevision, uint64 ResultInventoryRevision) const;
    // Part 2, right after ApplyBuildChange of part 1: every learned cooldown track is dropped (a later learn issues a
    // new one, as at the match start), the innate dash track is ready at NowMs and a living body's HP/MP
    // are full. Returns the number of dropped tracks.
    int32 DevFinishReset(const FGuid& Contender, int64 NowMs);
    // Pure part of DevFinishReset's tracks.
    static int32 DevDropTracks(TArray<FLDCombatTrack>& Tracks, int64 NowMs);
#endif

    // ---- Camps.
    FLDCampResetPrepareResult PrepareCampReset(const FLDCampResetInput& Input, const FLDGameplayWritePermit& Permit) const;
    // D26 (#79): DeathsAndRewards grants a timed stat bonus (the Boss approval buff) to one living contender body; a
    // body already gone or dying gets nothing (bOutApplied false). False only on an authority fault.
    bool GrantTimedBonus(const FLDCombatBodyRef& Body, FName Source, const TArray<FLDDemoStatBonus>& Stats, int64 DurationMs,
        const FLDGameplayWritePermit& Permit, bool& bOutApplied, FString& Error);
    void ApplyCampReset(FLDPreparedCampReset&& Prepared, const FLDGameplayWritePermit& Permit);

    // ---- Shared combat primitives (#112 section 5).
    // The only path that commits a control or slow onto a body life: hard controls (Root, Stun, Disarm, Taunt) pass the
    // body's diminishing-returns chain, the instance is applied with its action interruptions, the spray interrupt and
    // the assist contribution are recorded. bApplied is false when nothing landed (an existing instance, a zero duration).
    bool CommitControl(FLDBodyCombatState& Victim, const FLDControlApplication& App, int64 NowMs, bool& bApplied, FString& Error);
    // Any active root, stun, disarm, taunt, knockback, silence or slow (#147 "被减速或被控制", passive_public_shame).
    bool IsImpaired(const FLDBodyCombatState& Body, int64 NowMs) const;
    // A Q/W/D activation of this source kind blocked now: Cast for every kind, Skill (silence) for learned actives.
    bool IsCastBlocked(const FLDBodyCombatState& Body, ELDCombatSourceKind Kind) const;
    // The remaining-cooldown cut of one entry's bound track (LDCombatSourceRules::ReduceReadyAt); MAX_int64 = ready now.
    // False when the entity holds no track at that entry.
    bool ReduceCooldown(const FGuid& Entity, ELDCombatEntry Entry, int64 AmountMs);
    // The same cut on the learned track of one definition, bound or not.
    bool ReduceLearnedCooldown(const FGuid& Entity, FName DefinitionId, int64 AmountMs);
    // Lifesteal and cleave terms of a source's held equipment passives for a hit triggering on-hit effects at ScaleBp.
    bool SnapshotOnHit(const FLDBodyCombatState& Source, int64 ScaleBp, FLDCombatHit& InOut, FString& Error) const;
    // Source-side terms every hit of this body carries: the control amplification and the low-HP basic lifesteal.
    bool StampSourceTerms(const FLDBodyCombatState& Source, FLDCombatHit& InOut, FString& Error) const;
    // A separate damage instance riding on Primary (added damage) with no damage terms yet: never a crit, control,
    // keyword stack or on-hit trigger.
    static FLDCombatHit AddedHit(const FLDCombatHit& Primary);
    // #147 echo: the second blast's payload, a new effect whose damage terms are RatioBp of the first blast's (every other
    // term, such as its burn stacks, unchanged).
    static FLDCombatHit EchoHit(const FLDCombatHit& Template, int64 RatioBp);

private:
    friend class ALDGameMode;
    friend class FLDDemoCoreTest;
    friend class FLDCaptureCombatInterruptTest;
    friend class FLDPlaytestLogHitVampTest;
    friend class FLDRewardSettlementTest;
    friend class FLDAccountRetireAwaitsRewardTest;
    friend class FLDCombatOrderRulesTest;
    friend class FLDCombatRecoveryTest;
    friend class FLDCombatDormRecoveryTest;
    friend class FLDDemoDevTestCommandsTest;
    friend class FLDBossRewardTest;
    friend class FLDDemoAssistSettlementTest;
    friend class FLDDemoGrowthRerollTest;
    friend struct FLDBuildPrimitivesAccess;

    // ---- Body lifecycle (core-called, RespawnsAndWorld permit).
    bool PrepareContenderBody(ALDCharacter& Body,const FLDVictimKey& Victim,
        const FLDGameplayWritePermit& Permit,FLDBodyCombatState& Out,FString& Error) const;
    bool PrepareCampMemberBody(ALDCharacter& Body,const FLDVictimKey& Victim,FName MonsterId,
        const FLDGameplayWritePermit& Permit,FLDBodyCombatState& Out,FString& Error) const;
    bool InstallCombatBody(ALDCharacter& Body,FLDBodyCombatState&& Prepared,
        const FLDGameplayWritePermit& Permit,FLDCombatBodyRef& Out,FString& Error);
    void ReserveInitialBodyBatch(int32 Count) { Bodies.Reserve(Bodies.Num()+Count); }
    void RemoveRegisteredBody(const FLDCombatBodyRef& Ref);
    // Leave: cancel every unsettled effect owned by the entity; committed facts stay.
    void CancelEntityEffects(const FGuid& Entity);

    // ---- Driver phases (LDCombatDriver.cpp).
    ALDGameMode* Core() const;
    const FLDDemoContentSnapshot* Catalog() const;
    int64 NextDueMs(int64 NowMs) const;
    bool RunActions(const FLDGameplayWritePermit& Permit, FString& Error);
    bool RunEffects(const FLDGameplayWritePermit& Permit, FString& Error);
    bool RunDeaths(const FLDGameplayWritePermit& Permit, FString& Error);
    // The single second attempt of every reward refused as retryable at the previous boundary.
    bool RunRewardRetries(const FLDGameplayWritePermit& Permit, FString& Error);
    // Same-instant boundary of a capture completion that still owed a reward: retries, then Close.
    bool RunSettlementStep(const FLDMatchContext& Match, FString& Error);
    // One growth reward per locked death; a retryable refusal gets exactly one more attempt next boundary.
    bool CommitReward(const FLDCommittedDeath& Fact, bool bRetry, const FLDGameplayWritePermit& Permit, FString& Error);
    bool PublishOwnerViews(FString& Error);

    // ---- Actions (LDCombatActions.cpp).
    FLDCombatEntityRecord* FindRecord(const FGuid& Entity);
    const FLDCombatEntityRecord* FindRecord(const FGuid& Entity) const;
    FLDBodyCombatState* FindBody(const FLDCombatBodyRef& Ref, ALDCharacter** OutActor=nullptr);
    const FLDBodyCombatState* FindBody(const FLDCombatBodyRef& Ref, ALDCharacter** OutActor=nullptr) const;
    FLDBodyCombatState* FindEntityBody(const FGuid& Entity, ALDCharacter** OutActor=nullptr);
    const FLDBodyCombatState* FindEntityBody(const FGuid& Entity, ALDCharacter** OutActor=nullptr) const;
    bool ProcessDirect(FLDCombatEntityRecord& Record, const FLDCombatActionRequest& Request,
        const FLDGameplayWritePermit& Permit, FLDCombatActionReceipt& Receipt, FString& Error);
    bool ReleaseDue(const FLDGameplayWritePermit& Permit, FString& Error);
    bool ReleaseBasic(ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDCombatAction& Action,
        ELDCombatReject& Outcome, FString& Error);
    bool ReleaseAbility(ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDCombatAction& Action,
        ELDCombatReject& Reject, FString& Error);
    // Terminal Finished/Rejected receipt of a released Direct request (abilities and camp basics).
    void FinishDirectReceipt(const FGuid& Entity, const FLDCombatAction& Action, ELDCombatReject Reject);
    bool ResolveUnitTarget(const FLDCombatEntityRecord& Record, const ALDCharacter& Actor, const FLDCombatTarget& Target,
        FLDCombatBodyRef& OutBody, FVector& OutFoot, ELDCombatReject& Reject) const;
    void StoreReceipt(FLDCombatEntityRecord& Record, const FLDCombatActionRequest& Request, FLDCombatActionReceipt Receipt);
    void SendReceipt(const FLDCombatEntityRecord& Record, const FLDCombatActionReceipt& Receipt) const;
    void FinishAction(FLDBodyCombatState& Body, ELDCombatReject Reason, bool bReleased);
    // An accepted action that ends without release (death, cancel, interruption, camp reset):
    // a Direct request also gets its terminal Rejected receipt; nothing is paid or refunded.
    void AbandonAction(FLDBodyCombatState& Body, ELDCombatReject Reason);
    // Close: queued intents and unreleased actions end with MatchClosed receipts; nothing is paid.
    void EndMatchActions();

    // ---- Effects (LDCombatEffects.cpp).
    bool ApplyHit(const FLDCombatHit& Hit, FString& Error);
    bool AdvanceProjectiles(FString& Error);
    // The root, slow and disarm cleanses of one released rank on its caster; true when slows (a temporary term) ended.
    bool ApplyCleanses(FLDBodyCombatState& Body, const FLDDemoRankDefinition& Rank, int64 NowMs);
    // The victim's thorns reflections of one committed hit.
    bool QueueThorns(const FLDCombatHit& Hit, ALDCharacter& Holder, FLDBodyCombatState& Body, int64 DamageP2, bool bCritical, FString& Error);
    // #147: second blasts and knockback wall checks due by now (Effects, before the hits).
    bool ResolveEchoes(FString& Error);
    bool ResolveWallChecks(FString& Error);
    void ExpireStatuses(int64 NowMs);
    // Ends taunts whose taunter died, left or is no longer observed by the victim (Actions, before any intent).
    bool RefreshTaunts(FString& Error);
    // Taunt release (LDCombatControl.cpp): every eligible contender in the radius is taunted by this body; OutTaunted
    // counts the taunts that landed (#147 taunt_shield).
    bool ApplyTauntEffect(ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDDemoEffectDefinition& Effect,
        const FGuid& EffectId, FString& Error, int32* OutTaunted=nullptr);
    // Spray interruption (APP_SPEC 2.2): called once for every root, stun, taunt or knockback committed on a body,
    // inside the current Actions/Effects permit. A contender body's spray progress clears through the capture domain;
    // camp bodies and a world without an active spray reducer are untouched. Disarm, slow and damage never call it.
    bool InterruptSpray(const FLDBodyCombatState& Target, FString& Error);
    // Effective speed of one body: its projection with the current temporary terms.
    bool EffectiveMoveSpeed(const FLDBodyCombatState& Body, double& OutCmPerSec) const;
    bool Regenerate(int64 NowMs, FString& Error);
    // Settles every living body through NowMs; only contender bodies receive the D21 Recovery, and a contender whose
    // foot stands inside one of Dorms at this settle also receives DormRecovery for the interval (#79).
    bool RegenerateBodies(const LDTactical::FOutOfCombatRecovery& Recovery, int64 NowMs, FString& Error,
        const LDTactical::FOutOfCombatRecovery* DormRecovery = nullptr, const FLDDormZones* Dorms = nullptr);

    bool LockDeath(ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDCombatHit& Hit, FString& Error);
    // Every live body other than the source within RadiusCm (planar foot distance) of Anchor with clear sight from it,
    // excluding protected contenders and Excluded; sorted by entity. Shared by cleave and self-origin areas.
    bool CollectAreaTargets(const FVector& Anchor, double RadiusCm, const FLDBodyCombatState* Excluded,
        const FGuid& SourceEntity, TArray<FLDCombatBodyRef>& Out, FString& Error) const;
    // The single assist contribution entry (LDAssist): CreditOwner, another contender, just hurt this contender life with
    // positive damage (shield-absorbed part included) or applied any control or debuff to it. Every damage, control and
    // debuff path calls it once per committed contact; camp bodies and unowned or self contacts are ignored.
    // Returns whether a contribution was recorded or refreshed.
    bool RecordAssistContribution(FLDBodyCombatState& Victim, const FGuid& CreditOwner, int64 NowMs);
    // On-hit effects of one committed hit at its OnHitScaleBp (lifesteal is summed into the hit's vamp; cleave sweeps
    // the bodies Cleaved named before the writes). Hits it creates carry OnHitScaleBp 0.
    bool ApplyOnHitEffects(const FLDCombatHit& Hit, FLDBodyCombatState& Target, const FLDDamageEvaluation& Damage,
        const TArray<FLDCombatBodyRef>& Cleaved, int64 NowMs, FString& Error);
    // The lone resolver of passive effects a projection holds: learned trigger ranks and equipment families (once each).
    bool CollectPassiveEffects(const FLDAttributeProjection& Projection, ELDDemoEffectOp Op, TArray<FLDPassiveEffectRef>& Out,
        FString& Error) const;
    // Mana grant of a triggered effect; marks the owner view.
    void GrantMana(FLDBodyCombatState& Body, int64 AmountP2);
    // Cast range of an active for this body now: the authored range and its share of the extra basic-attack range.
    bool CastRangeCm(const FLDBodyCombatState& Body, const FLDDemoTargetDefinition& Target, double& OutCm, FString& Error) const;
    // End-of-dash path slows (path_slow) due by now.
    bool ResolveDashSweeps(FString& Error);
    // Hooks of one landed basic: refunds and counters.
    bool OnBasicLanded(FLDBodyCombatState& Body, const FLDCombatHit& Primary, FString& Error);
    // Recomputes the effective projection from BuildProjection when the active temporary terms changed.
    bool RefreshTemporary(FLDBodyCombatState& Body, ALDCharacter* Actor, int64 NowMs, FString& Error);
    // Ends lapsed slows and timed bonuses of every body and refreshes their effective projections.
    bool RefreshAllTemporary(int64 NowMs, FString& Error);
    // Installs a new build/monster projection on a body and reapplies its temporary terms.
    bool InstallBodyProjection(FLDBodyCombatState& Body, ALDCharacter* Actor, const FLDAttributeProjection& Projection,
        int64 NowMs, FString& Error);

    // ---- #112 C2 mechanics (LDCombatMechanicsDirector.cpp).
    // A one-time heal of Body (lifesteal); regeneration never passes here. True when HP changed.
    bool HealBody(FLDBodyCombatState& Body, int64 AmountP2);
    // The single shield entry: applied ReplaceIfNotWeaker; a granted shield records its amount (ShieldGrantedP2).
    bool GrantShieldTo(FLDBodyCombatState& Body, int64 AmountP2, int64 DurationMs, const FGuid& EffectId,
        const FGuid& SourceEntity, int64 NowMs, FString& Error);
    // A shield emptied by Hit: item_privacy_screen's retort to the breaker.
    bool OnShieldBroken(FLDBodyCombatState& Owner, const FLDCombatHit& Hit, int64 GrantedP2, int64 NowMs, FString& Error);
    // The forced displacement of one committed knockback (control kind Knockback): server root motion along the ground
    // that stops at the last legal position, every voluntary action blocked while it lasts, the unreleased action
    // abandoned and the current order dropped (KnockbackSerial).
    bool ApplyKnockback(ALDCharacter& Target, FLDBodyCombatState& Body, const FLDCombatHit& Hit, int64 NowMs, bool& bApplied,
        FString& Error);
    // Per landed hit: item_skateboard's haste of the hit's source.
    bool OnHitLanded(FLDBodyCombatState& Target, const FLDCombatHit& Hit, int64 DamageP2, int64 NowMs, FString& Error);
    // Zones due at NowMs (Effects, before the hits).
    bool AdvanceMechanics(int64 NowMs, FString& Error);
    bool ExpireMechanics(FLDBodyCombatState& Body, int64 NowMs);
    int64 NextMechanicsDueMs(int64 NowMs) const;
    // Status cue of the #112 records: knockback, for every observer.
    void AppendMechanicsStatuses(const FLDBodyCombatState& Body, int64 NowMs, TArray<FLDObservedStatus>& Out) const;
    // A trigger effect of the body's learned passives or equipment passive families (first match), or null.
    const FLDDemoEffectDefinition* FindTrigger(const FLDAttributeProjection& Projection, ELDDemoEffectOp Op,
        FLDCombatSourceRef* OutSource=nullptr) const;
    // The registered actor of one body state, or null.
    ALDCharacter* ActorOf(const FLDBodyCombatState& Body) const;
    // ---- #147 build v7 keywords (LDCombatKeywordsDirector.cpp).
    bool KeywordTerms(FLDKeywordTerms& Out, FString& Error) const;
    // The level a body's G(L) reads: a contender's build level; camp bodies use level 1.
    int32 GrowthLevelOf(const FLDBodyCombatState& Body) const;
    // A burn layer of Source with Source's level and AP now snapshotted in it.
    bool BurnLayerOf(const FLDBodyCombatState& Source, const FLDCombatSourceRef& Ref, FLDCombatBurnLayer& Out, FString& Error) const;
    // Shared breach and burn of one committed hit on Target: the hit's own stacks and its living source's passive
    // breach/burn (basic_stacks on a primary basic, stacks on a learned Q/W hit). Assists are recorded per application.
    bool ApplyKeywordStacks(FLDBodyCombatState& Target, const FLDCombatHit& Hit, FLDBodyCombatState* Source, int64 NowMs, FString& Error);
    // Burn ticks due by now: one damage hit per applier and tick; lapsed burns end.
    bool AdvanceBurns(int64 NowMs, FString& Error);
    // The damage-triggered shield of Body after it took positive damage (rest only after a granted shield).
    bool OnDamaged(FLDBodyCombatState& Body, int64 NowMs, FString& Error);
    // The burst of a damage-triggered shield that this hit broke (BrokenShieldId), around the holder.
    bool BurstBrokenShield(FLDBodyCombatState& Body, const FGuid& BrokenShieldId, int64 NowMs, FString& Error);
    // Statuses of the keyword records (silence, breach x stacks, burn x stacks) for every observer.
    void AppendKeywordStatuses(const FLDBodyCombatState& Body, int64 NowMs, TArray<FLDObservedStatus>& Out) const;
    // The release-time zone of a #112 active (EFFECT zone).
    bool ReleaseMechanics(FLDBodyCombatState& Body, const FLDCombatSourceRef& Source, const FLDDemoRankDefinition& Rank,
        const FVector& OriginFoot, const FVector& TargetPoint, FString& Error);
    // Hit extras of a #112 rank (knockback).
    void FillMechanicsTemplate(const FLDDemoRankDefinition& Rank, const FVector& OriginFoot, FLDCombatHit& Template) const;

    // ---- Sources (LDCombatSources.cpp).
    bool ProjectBuild(const FLDBuildProjectionInput& Build, FLDAttributeProjection& Out, FString& Error) const;
    // PrepareBuildChange after its caller, version and shape checks: projection, timing, bindings and the one
    // resource-maximum conversion of a living body. Input is the complete build to install, DEV layer included.
    FLDCombatPrepareResult PrepareProjected(const ALDGameMode& Mode, const FLDDemoContentSnapshot& Content,
        const FLDCombatEntityRecord& Record, const FGuid& Contender, const FLDBuildProjectionInput& Input,
        const FLDExpectedBuildVersions& Expected) const;
    const FLDCombatTrack* TrackFor(const FLDCombatEntityRecord& Record, ELDCombatEntry Entry) const;
    FLDCombatTrack* TrackFor(FLDCombatEntityRecord& Record, ELDCombatEntry Entry);
    bool ResolveEntry(const FLDCombatEntityRecord& Record, ELDCombatEntry Entry, FLDCombatSourceRef& OutSource,
        uint8& OutRank, const FLDDemoAbilityDefinition*& OutAbility) const;
    // The executable active of a resolved entry: a learned rank grown for its owner's Level (#147), or the innate dash;
    // false without one.
    bool ActiveOf(const FLDCombatSourceRef& Source, uint8 Rank, uint8 Level, const FLDDemoAbilityDefinition* Ability,
        const FLDDemoTargetDefinition*& OutTarget, const FLDDemoRankDefinition*& OutRank, int64& OutWindupMs) const;
    // D19/D20 innate dash terms for the body's installed equipment passives (the persistent build without a body).
    bool InnateDashFor(const FLDCombatEntityRecord& Record, const FLDBodyCombatState* Body, FLDInnateDashTerms& Out,
        FString& Error) const;
    bool PlanBindings(const FLDCombatEntityRecord& Record, const FLDBuildProjectionInput& Input, int64 NowMs,
        TArray<FLDCombatBindingPlan>& Out, FString& Error) const;
    FLDCombatEntryView EntryView(const FLDCombatEntityRecord& Record, const FLDBodyCombatState* Body,
        ELDCombatEntry Entry, int64 NowMs) const;
    void MarkViewChanged(const FGuid& Entity);

    TMap<TWeakObjectPtr<ALDCharacter>,FLDBodyCombatState> Bodies;
    TMap<FGuid,FLDCombatEntityRecord> Records;
    TArray<FLDCombatHit> PendingHits;
    TArray<FLDCombatProjectile> Projectiles;
    TArray<FLDCombatZone> Zones;
    TArray<FLDCombatDashSweep> DashSweeps;
    TArray<FLDCombatEcho> Echoes;
    TArray<FLDCombatWallCheck> WallChecks;
    TArray<FLDCommittedDeath> PendingDeaths;
    // Committed positive contacts against camp members, delivered to the camp owner within the same Effects.
    TArray<FLDCombatHostileContact> PendingContacts;
    TArray<FLDCommittedDeath> RetryRewards;
    FRandomStream CritRandom;
    bool bAdvancing=false;
};
