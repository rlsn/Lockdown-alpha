#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/LDCombatEvaluationTypes.h"
#include "Combat/LDAttributeProjection.h"
#include "Combat/LDCombatTypes.h"
#include "Combat/LDCombatControlRules.h"
#include "Combat/LDCombatMechanics.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Vision/LDViewTypes.h"
#include "LDCombatDirector.generated.h"

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
    // Basic attacks commit to combat when they start, so the ambush window is sampled at acceptance.
    bool bAmbush=false;
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
    // Amplification marks other sources hold on this life (control damage amplification).
    TArray<FLDCombatExposure> Exposures;
    // Last published ambush-ready state, so the status changes the owner view exactly when the window opens.
    bool bAmbushReadyShown=false;
    // Disarm and Stun instances (LDCombatControl); each keeps its effect, source and half-open expiry.
    TArray<FLDCombatRoot> Disarms;
    TArray<FLDCombatRoot> Stuns;
    // The armed next-basic bonus (one consumption) and this life's thorns repeat windows.
    TOptional<FLDCombatEmpower> Empower;
    TArray<FLDThornsMark> ThornsMarks;
    TOptional<FLDCombatShield> Shield;
    TOptional<FLDCombatTaunt> Taunt;       // contenders only
    TOptional<FLDCombatStealth> Stealth;
    TOptional<FLDCombatAction> Action;
    int64 LastCombatAtMs=0;
    int64 RegenThroughMs=0;
    int64 HpRegenMicroP2=0, MpRegenMicroP2=0; // sub-P2 remainders in 1e-6 P2
    int64 MonsterNextAttackAtMs=0;         // contenders keep their cadence on the persistent record
    int64 DashUntilMs=0;
    // Released action and last landed hit of this life for the visible action cue.
    FLDCombatCueFacts Cues;
    // Source of the last released action (the visible Cast cue's ability or gear once its windup released).
    FLDCombatSourceRef ReleasedSource;
    uint64 ResetThroughEpoch=0;
    bool bPendingDeath=false;
    // #112 C2 mechanics (LDCombatMechanicsDirector.cpp): heal/shield reductions, breaches and damage over time, forced
    // displacement windows (blocking every voluntary action), the damage guard and the passive trigger windows.
    TArray<FLDCombatHealCut> HealCuts;
    TArray<FLDCombatBreach> Breaches;
    TArray<FLDCombatDot> Dots;
    TArray<FLDCombatRoot> Knockbacks;
    // Increments on every knockback that lands; Navigation drops the body's current order when it changes.
    uint64 KnockbackSerial=0;
    TArray<FLDCombatVulnerable> Vulnerabilities;
    TOptional<FLDCombatGuard> Guard;
    // The amount the current shield record was granted with (item_privacy_screen answers 40% of it).
    int64 ShieldGrantedP2=0;
    int64 MobilityShieldReadyAtMs=0;
    int64 ShieldPulseReadyAtMs=0;
    int64 RetortReadyAtMs=0;
    // passive_thick_skin Lv3: the next basic or ability hit of this body before ShieldBonusUntilMs adds ShieldBonusP2.
    int64 ShieldBonusP2=0;
    int64 ShieldBonusUntilMs=0;
};

// Persistent cooldown identity; an item instance or entry slot is only its current binding.
struct FLDCombatTrack
{
    FGuid IssuedCooldownId;
    ELDCombatSourceKind Kind=ELDCombatSourceKind::None;
    FName DefinitionId;
    FGuid BoundInstance;      // Gear: the bound item instance
    bool bBound=false;
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
    // Coefficient of the source's max HP and that max HP snapshotted at release (active_body_slam).
    int64 MaxHPRatioBp=0, SourceMaxHPP2=0;
    bool bCritical=false;
    int64 RootDurationMs=0;
    int64 SlowBp=0, SlowDurationMs=0;
    // Snapshot of the releasing source's control damage amplification, applied if this hit controls its target.
    int64 ControlAmpBp=0, ControlAmpDurationMs=0;
    int64 DisarmDurationMs=0;
    int64 StunDurationMs=0;
    // Rear strike: applied when the releasing body stands in the target's rear arc at this hit.
    int64 BackstabRatioBp=0;
    int64 BackstabStunMs=0;
    // A reflection never crits, heals, cleaves or triggers another reflection.
    bool bReflect=false;
    bool bCleave=false;
    int64 CleaveRatioBp=0;
    double CleaveRadiusCm=0;
    bool bLifesteal=false;
    int64 LifestealRatioBp=0;
    // #112 C2 (LDCombatMechanicsDirector.cpp).
    bool bPeriodic=false;            // a damage-over-time tick: no crit, no on-hit, amplified by vulnerability
    bool bAddon=false;               // an added hit (ambush, rider, shield bonus, punishment): stacks and triggers nothing
    bool bFromShieldTrigger=false;   // a pulse/retort hit: its controls never grant shields (#112 §5)
    bool bSilent=false;              // a zone's periodic slow refresh: no visible hit cue
    int64 ExtraAmplifyBp=10000;      // source-side multiplier snapshotted at creation (before resistance)
    double KnockbackCm=0;
    int64 KnockbackMs=0;
    bool bKnockbackFromAnchor=false; // pushed away from KnockbackAnchorCm instead of the source body's foot
    FVector KnockbackAnchorCm=FVector::ZeroVector;
    int64 HealCutBp=0, HealCutMs=0;
    int64 VulnerableBp=0;            // installed for as long as the root this hit applies
    int64 NegativeAmpBp=0;           // per negative kind on the target at this hit (active_cancel_blast)
    int64 ShieldShatterBp=0;         // share of the target's shield removed before the damage (not a break)
    int64 NegativeHealMaxHPBp=0;     // the source heals this share of its max HP per negative kind
    bool bRefreshDebuffs=false;      // restarts this source's slows and damage over time on the target
    int64 OnHitScaleBp=0;            // on-hit effect coefficient (0 = none; basic primary 10000)
    TOptional<FLDCombatDot> Dot;     // a damage over time this hit installs on its target
};

// A released jump: the landing effects resolve at LandAtMs on the same body life, centred on its landing foot.
struct FLDCombatLanding
{
    FLDCombatBodyRef Body;
    FGuid EffectId;
    uint64 Token=0;
    FLDCombatSourceRef Source;
    int64 LandAtMs=0;
    // Self effects (shield, root cleanse, mana) applied to the landed body.
    TArray<FLDDemoEffectDefinition> Effects;
    // Hit payload snapshotted at release; at landing it is delivered to every legal body within AreaRadiusCm of the
    // landing foot (the same CollectAreaTargets rule as a self area), with the release's ambush bonus if it was ready.
    TOptional<FLDCombatHit> HitTemplate;
    double AreaRadiusCm=0;
    TOptional<FLDAmbushDamageTerm> Ambush;
};

// Released straight, non-homing projectile: first legal hostile body or blocking terrain ends it.
struct FLDCombatProjectile
{
    FGuid EffectId;
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
    int64 ControlAmpBp=0, ControlAmpDurationMs=0;
    // Ambush damage snapshotted at release; delivered with the first body this projectile strikes.
    TOptional<FLDAmbushDamageTerm> Ambush;
    bool bRefreshDebuffs=false;
    int64 OnHitScaleBp=0;
};

// A later shot of a released volley (active_barrage_rant): fired from the shooter's foot at AtMs along the release
// direction while the same body life lives.
struct FLDCombatVolleyShot
{
    FLDCombatBodyRef Body;
    int64 AtMs=0;
    FLDCombatProjectile Template;
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
    bool bShield=false;
    int64 ShieldBaseP2=0, ShieldAPRatioBp=0, ShieldMs=0;
    bool bSlow=false;
    int64 SlowBp=0, SlowCheckMs=0, SlowLingerMs=0, NextSlowAtMs=0;
    int64 EndRootMs=0;
    bool bDamage=false;
    int64 BaseP2=0, ADRatioBp=0, APRatioBp=0;
    ELDDemoDamageType DamageType=ELDDemoDamageType::Magical;
    int64 SourceADP2=0, SourceAPP2=0;
    int64 OnHitScaleBp=0;
};

// A decoy left by item_decoy_wig: neither attackable nor selectable; lured victims walk to it until it ends.
struct FLDCombatDecoy
{
    FGuid EffectId;
    FLDCombatBodyRef Holder;
    FVector PointCm=FVector::ZeroVector;
    int64 ExpiresAtMs=0;
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
    // The sole authority driver: advances native steps and runs every commit phase in order.
    bool AdvanceAuthority(const FLDMatchContext& Match,int64 ThroughMs,FString& Error);

    // ---- Commands shared by human RPCs, the opponent AI and registered camp members.
    bool ResolveControllerCaller(const AController& Controller, FLDCombatCaller& Out, FString& Error) const;
    // Receipt only; Queued intents are admitted in the next Actions phase.
    FLDCombatActionReceipt SubmitAction(const FLDCombatCaller& Caller, const FLDCombatActionRequest& Request);
    FLDCombatActionReceipt CancelAction(const FLDCombatCaller& Caller, const FLDCombatCancelRequest& Request);
    FLDCombatActionReceipt QueryActionReceipt(const FLDCombatCaller& Caller, uint64 DirectSequence) const;
    FLDCombatEntryView QueryAvailability(const FLDCombatCaller& Caller, ELDCombatEntry Entry) const;
    bool BuildOwnerView(const FLDCombatCaller& Caller, FLDCombatOwnerView& Out) const;

    // ---- Navigation and camp bodies (server, inside the current Actions permit).
    bool ReadCombatBody(const FLDCombatBodyRef& Body, FLDCombatBodyRead& Out) const;
    FLDCombatActionReceipt StartNavAttack(const FLDCombatCaller& Caller, const FLDCombatRequestKey& Key,
        const FLDCombatBodyRef& Target, const FGuid& Observed, uint32 ObservationEpoch,
        const FLDGameplayWritePermit& Permit);
    // Cancels the unreleased basic attack of this navigation order; the committed cadence is kept. A swing at Keep
    // (a valid body: the taunter that replaced the order) is left to land, since the taunt still allows it.
    void CancelNavAttack(const FGuid& Entity, const FLDMoveIdentity& Move, const FLDCombatBodyRef& Keep=FLDCombatBodyRef());
    // Voluntary movement blocked by Root or Stun (LDCombatControl matrix); a taunt forces movement instead.
    bool IsMoveBlocked(const ALDCharacter& Body) const;
    // The control matrix for one registered body at the current authority time: Root/Disarm/Stun (LDCombatControl)
    // and a taunt's ban on abilities, items and displacement (LDControl::TauntBlocks).
    bool IsActionBlocked(const FLDBodyCombatState& Body, ELDCombatControlAction Action) const;
    bool ReadMoveSpeed(const ALDCharacter& Body, float& OutCmPerSec) const;
    // The taunter's exact body while Victim is taunted now; false otherwise. Navigation forces its order from this.
    bool ReadTaunter(const FLDCombatBodyRef& Victim, FLDCombatBodyRef& OutTaunter) const;
    // A decoy lure on Victim now (item_decoy_wig): the point Navigation walks it to. False when it is not lured.
    bool ReadDecoyLure(const FLDCombatBodyRef& Victim, FVector& OutPoint) const;
    // Knockbacks that landed on this body life so far; Navigation drops the current order whenever it changes.
    uint64 ReadKnockbackSerial(const FLDCombatBodyRef& Body) const;
    // Every decoy standing now, for presentation (owner, point, expiry).
    const TArray<FLDCombatDecoy>& ReadDecoys() const { return Decoys; }
    // Planar distance beyond which enemies cannot observe this body now (stealth); 0 when it is not hidden.
    double ConcealBeyondCm(const ALDCharacter& Body) const;
    // Public Root/Shield cues of a registered body at NowMs; protection is derived by the life domain.
    void AppendObservedStatuses(const ALDCharacter& Body, int64 NowMs, TArray<FLDObservedStatus>& Out) const;
    // Action cue of a registered body at NowMs (LDCombatCues); None after a lethal hit (LDCombatObservation.cpp).
    // Fails (with Error) when a visible cast has no approved source, so the publication fails rather than hiding it.
    bool AppendObservedAction(const ALDCharacter& Body, int64 NowMs, FLDObservedPublicFacts& InOut, FString& Error) const;
    // The approved native ids of the cast the body's visible Cast cue shows at NowMs (Q/W ability or gear active, dash
    // included); both zero when no Cast cue shows. Fails only when a cast source has no approved definition.
    bool ObservedCastSource(const ALDCharacter& Body, int64 NowMs, uint8& OutAbility, uint8& OutEquipment) const;
    // Every projectile still in flight after the last Effects advance, for the observation owner to sample.
    // Fails without output for a projectile whose source has no approved look.
    bool ReadObservedProjectiles(TArray<FLDObservedProjectileFact>& Out, FString& Error) const;
    // A body life's cue facts with its winding-up action and dash filled in.
    static FLDCombatCueFacts CueFactsOf(const FLDBodyCombatState& Body);

    // ---- Growth compound commits.
    // A locked death credited to this contender still awaits its reward (this boundary's set or its one retry).
    bool OwesRewardTo(const FGuid& Recipient) const;
    FLDCombatPrepareResult PrepareBuildChange(const FLDCombatCaller& Caller, const FLDBuildProjectionInput& Input,
        const FLDExpectedBuildVersions& Expected) const;
    void ApplyBuildChange(FLDPreparedCombatProjection&& Prepared, const FLDGameplayWritePermit& Permit);
    FLDGearBindingPreview PreviewGearBinding(const FLDCombatCaller& Caller, FName EquipmentId) const;

#if WITH_DEV_AUTOMATION_TESTS
    // ---- DEV test commands (LDDevTestProtocol.h), committed by ALDEconomyDirector inside the Transactions permit.
    // The contender's current Alive body to its committed HP/MP maxima (the shield is untouched). False with Detail when
    // the permit, the contender record or a living body is missing; nothing changes then.
    bool DevRefillResources(const FGuid& Contender, const FLDGameplayWritePermit& Permit, FString& Detail);
    // Every cooldown track of the contender (learned and gear, bound or unbound debt) ready at the permit boundary;
    // tracks keep their identity and binding. False with Detail when the permit or the contender record is missing.
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
    // Part 2, right after ApplyBuildChange of part 1: every learned and gear cooldown track is dropped (a later learn or
    // buy issues a new one, as at the match start), the innate dash track is ready at NowMs and a living body's HP/MP
    // are full. Returns the number of dropped tracks.
    int32 DevFinishReset(const FGuid& Contender, int64 NowMs);
    // Pure part of DevFinishReset's tracks.
    static int32 DevDropTracks(TArray<FLDCombatTrack>& Tracks, int64 NowMs);
#endif

    // ---- Camps.
    FLDCampResetPrepareResult PrepareCampReset(const FLDCampResetInput& Input, const FLDGameplayWritePermit& Permit) const;
    void ApplyCampReset(FLDPreparedCampReset&& Prepared, const FLDGameplayWritePermit& Permit);

private:
    friend class ALDGameMode;
    friend class FLDDemoCoreTest;
    friend class FLDRewardSettlementTest;
    friend class FLDAccountRetireAwaitsRewardTest;
    friend class FLDCombatOrderRulesTest;
    friend class FLDCombatRecoveryTest;
    friend class FLDDemoDevTestCommandsTest;

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
    // Disarm/Stun of one committed hit, the rear-strike judgement and the victim's thorns reflections.
    bool IsSourceBehind(const FLDCombatBodyRef& Source, const ALDCharacter& Target) const;
    bool QueueThorns(const FLDCombatHit& Hit, ALDCharacter& Holder, FLDBodyCombatState& Body, int64 DamageP2, FString& Error);
    // Self effects shared by a release and a jump landing (shield, root cleanse, mana).
    bool ApplySelfEffect(FLDBodyCombatState& Body, const FLDDemoEffectDefinition& Effect, const FGuid& EffectId,
        int64 NowMs, FString& Error);
    bool ResolveLandings(FString& Error);
    void ExpireStatuses(int64 NowMs);
    // Ends taunts whose taunter died, left or is no longer observed by the victim (Actions, before any intent).
    bool RefreshTaunts(FString& Error);
    // Taunt release (LDCombatControl.cpp): every eligible contender in the radius is taunted by this body.
    bool ApplyTauntEffect(ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDDemoEffectDefinition& Effect,
        const FGuid& EffectId, FString& Error);
    // Effective speed of one body: the projection, less an active stealth penalty.
    bool EffectiveMoveSpeed(const FLDBodyCombatState& Body, int64 NowMs, double& OutCmPerSec) const;
    // Stealth ends at an accepted attack/ability/item or a positive hit.
    void RevealStealth(FLDBodyCombatState& Body);
    bool Regenerate(int64 NowMs, FString& Error);
    // Settles every living body through NowMs; only contender bodies receive the D21 Recovery.
    bool RegenerateBodies(const LDTactical::FOutOfCombatRecovery& Recovery, int64 NowMs, FString& Error);
    bool LockDeath(ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDCombatHit& Hit, FString& Error);
    // Every live body other than the source within RadiusCm (planar foot distance) of Anchor with clear sight from it,
    // excluding protected contenders and Excluded; sorted by entity. Shared by cleave and self-origin areas.
    bool CollectAreaTargets(const FVector& Anchor, double RadiusCm, const FLDBodyCombatState* Excluded,
        const FGuid& SourceEntity, TArray<FLDCombatBodyRef>& Out, FString& Error) const;
    // A control the hit's source just applied to Target (Root now; stun or knock-up later) opens the source's
    // control damage amplification on that life.
    bool MarkControlled(FLDBodyCombatState& Target, const FLDCombatHit& Hit, int64 NowMs, FString& Error);
    // Recomputes the effective projection from BuildProjection when the active temporary terms changed.
    bool RefreshTemporary(FLDBodyCombatState& Body, ALDCharacter* Actor, int64 NowMs, FString& Error);
    // Ends lapsed slows, timed bonuses and exposures of every body and refreshes their effective projections.
    bool RefreshAllTemporary(int64 NowMs, FString& Error);
    // Installs a new build/monster projection on a body and reapplies its temporary terms.
    bool InstallBodyProjection(FLDBodyCombatState& Body, ALDCharacter* Actor, const FLDAttributeProjection& Projection,
        int64 NowMs, FString& Error);

    // ---- #112 C2 mechanics (LDCombatMechanicsDirector.cpp).
    // A one-time heal of Body reduced by its strongest heal cut; regeneration never passes here. True when HP changed.
    bool HealBody(FLDBodyCombatState& Body, int64 AmountP2, int64 NowMs);
    // The single shield entry: the amount is reduced by the body's heal cut, applied ReplaceIfNotWeaker, and a refresh
    // (replacing a shield that still held an amount) fires the owner's shield-event passives unless the shield itself
    // came from such a pulse or another shield-triggered effect.
    bool GrantShieldTo(FLDBodyCombatState& Body, int64 AmountP2, int64 DurationMs, const FGuid& EffectId,
        const FGuid& SourceEntity, bool bFromShieldTrigger, int64 NowMs, FString& Error);
    // Shield-event passives of Owner (passive_mental_gymnastics): pulse damage, Q/W cooldown cut, Lv3 slow.
    bool PulseShieldEvent(FLDBodyCombatState& Owner, int64 NowMs, FString& Error);
    // A shield emptied by Hit: the pulse above and item_privacy_screen's retort to the breaker.
    bool OnShieldBroken(FLDBodyCombatState& Owner, const FLDCombatHit& Hit, int64 GrantedP2, int64 NowMs, FString& Error);
    // The forced displacement of one committed knockback (control kind Knockback): server root motion along the ground
    // that stops at the last legal position, every voluntary action blocked while it lasts, the unreleased action
    // abandoned and the current order dropped (KnockbackSerial).
    bool ApplyKnockback(ALDCharacter& Target, FLDBodyCombatState& Body, const FLDCombatHit& Hit, int64 NowMs, FString& Error);
    // A body displaced itself (dash, jump, leap): expose-post punishments on it and its own thick-skin shield.
    bool OnMobilityUsed(FLDBodyCombatState& Body, int64 NowMs, FString& Error);
    // The source of Hit applied a control or slow: its thick-skin shield (unless the hit came from a shield trigger).
    bool OnControlApplied(const FLDCombatHit& Hit, int64 NowMs, FString& Error);
    // Per landed hit: dox-master stacks, skateboard haste and the thick-skin Lv3 bonus of the hit's source.
    bool OnHitLanded(FLDBodyCombatState& Target, const FLDCombatHit& Hit, int64 DamageP2, int64 NowMs, FString& Error);
    // The guard of Body counts this hit and recoils once past its threshold.
    bool CountGuard(ALDCharacter& Actor, FLDBodyCombatState& Body, int64 PreMitigationP2, int64 NowMs, FString& Error);
    // Dots, zones, volley shots, decoys and lapsed mechanics records, due at NowMs (Effects, before the hits).
    bool AdvanceMechanics(int64 NowMs, FString& Error);
    bool ExpireMechanics(FLDBodyCombatState& Body, int64 NowMs);
    int64 NextMechanicsDueMs(int64 NowMs) const;
    FLDNegativeKinds NegativeKindsOf(const FLDBodyCombatState& Body, int64 NowMs) const;
    // Status cues of the #112 records: knockback, breach (breach or damage over time) and heal cut for every observer;
    // the damage guard and the vulnerability only for the owner.
    void AppendMechanicsStatuses(const FLDBodyCombatState& Body, int64 NowMs, bool bOwner, TArray<FLDObservedStatus>& Out) const;
    // A trigger effect of the body's learned passives or equipment passive families (first match), or null.
    const FLDDemoEffectDefinition* FindTrigger(const FLDAttributeProjection& Projection, ELDDemoEffectOp Op,
        FLDCombatSourceRef* OutSource=nullptr) const;
    // The registered actor of one body state, or null.
    ALDCharacter* ActorOf(const FLDBodyCombatState& Body) const;
    // passive_thick_skin of Body: a shield on mobility or control, behind its internal cooldown.
    bool GrantThickSkin(FLDBodyCombatState& Body, int64 NowMs, FString& Error);
    // The release-time content of the #112 actives (zones, volleys, guard, burst, decoy, cooldown refresh, leap).
    // The release-time self content of the #112 actives: the guard, zones, later volley shots, the decoy and its lure,
    // the backward leap and the cooldown refresh. Hit payloads ride on Template (null when the rank has no hit).
    bool ReleaseMechanics(ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDCombatSourceRef& Source,
        const FLDDemoRankDefinition& Rank, const FLDCombatHit* Template, const FVector& OriginFoot, const FVector& TargetPoint,
        const FGuid& EffectId, uint64 Token, FString& Error);
    // Hit extras of a #112 rank (dot, heal cut, vulnerability, negative-kind terms, knockback, refresh, on-hit scale).
    void FillMechanicsTemplate(const FLDBodyCombatState& Body, const FLDDemoRankDefinition& Rank, const FVector& OriginFoot,
        int64 NowMs, FLDCombatHit& Template) const;
    // C1's remaining-cooldown cut on one entry's persistent track (MAX_int64 = ready now).
    bool ReduceCooldown(const FGuid& Entity, ELDCombatEntry Entry, int64 AmountMs);

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
        uint8& OutRank, const FLDDemoAbilityDefinition*& OutAbility, const FLDDemoEquipmentDefinition*& OutEquipment) const;
    // The executable active of a resolved entry: a learned rank, a gear active or the innate dash; false without one.
    bool ActiveOf(const FLDCombatSourceRef& Source, uint8 Rank, const FLDDemoAbilityDefinition* Ability,
        const FLDDemoEquipmentDefinition* Equipment, const FLDDemoTargetDefinition*& OutTarget,
        const FLDDemoRankDefinition*& OutRank, int64& OutWindupMs) const;
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
    TArray<FLDCombatLanding> Landings;
    TArray<FLDCombatZone> Zones;
    TArray<FLDCombatVolleyShot> VolleyShots;
    TArray<FLDCombatDecoy> Decoys;
    TArray<FLDCommittedDeath> PendingDeaths;
    // Committed positive contacts against camp members, delivered to the camp owner within the same Effects.
    TArray<FLDCombatHostileContact> PendingContacts;
    TArray<FLDCommittedDeath> RetryRewards;
    FRandomStream CritRandom;
    bool bAdvancing=false;
};
