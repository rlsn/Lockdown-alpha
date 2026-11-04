#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/LDCombatEvaluationTypes.h"
#include "Combat/LDAttributeProjection.h"
#include "Combat/LDCombatTypes.h"
#include "Combat/LDCombatControlRules.h"
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

// active_combo_flurry Lv3: the window in which each landed basic shortens the other learned actives.
struct FLDCombatHitRefundWindow
{
    FName SourceDefinitionId;
    int64 AmountMs=0;
    int64 ExpiresAtMs=0;
};

// active_slick_moves Lv3: planar distance moved until AccumulateUntilMs becomes extra damage of the next landed basic.
struct FLDCombatMomentum
{
    FGuid EffectId;
    FName SourceDefinitionId;
    int64 PerHundredCmP2=0;
    int64 CapP2=0;
    ELDDemoDamageType DamageType=ELDDemoDamageType::Physical;
    int64 AccumulateUntilMs=0;
    // The next landed basic consumes it from AccumulateUntilMs until ConsumeUntilMs.
    int64 ConsumeUntilMs=0;
    FVector LastFootCm=FVector::ZeroVector;
    double DistanceCm=0;
};

// active_come_here Lv3: answers the attacks taken until EndAtMs with one area hit around the caster.
struct FLDCombatRetaliation
{
    FGuid EffectId;
    FLDCombatSourceRef Source;
    int64 EndAtMs=0;
    int32 HitsTaken=0;
    int32 MaxHits=0;
    int64 BaseP2=0, PerHitP2=0, ArmorRatioBp=0;
    ELDDemoDamageType DamageType=ELDDemoDamageType::Magical;
    double RadiusCm=0;
};

// active_lunge Lv3: at the end of the dash every body along the travelled segment is slowed.
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
    // Resistance shreds (breach) and source vamp marks other bodies hold on this life.
    TArray<FLDCombatBreach> Breaches;
    TArray<FLDCombatVampMark> VampMarks;
    // Hard-control diminishing returns of this life (LDCombatControl::DiminishHardControl).
    FLDHardControlChain HardControl;
    // Ambush readiness credit from dashes, jumps and stealth; valid only while LastCombatAtMs equals AmbushCreditFromMs.
    int64 AmbushCreditMs=0;
    int64 AmbushCreditFromMs=0;
    // passive_battle_rhythm stacks (their speed term is a timed bonus keyed by the passive) and item_edge_blade's landed-basic count.
    int32 BasicStacks=0;
    int32 LandedBasics=0;
    // active_combo_flurry Lv3: while it lasts each landed basic cuts the other learned active's remaining cooldown.
    TOptional<FLDCombatHitRefundWindow> BasicHitRefund;
    // active_slick_moves Lv3: movement gathered after the release, paid on the next landed basic.
    TOptional<FLDCombatMomentum> Momentum;
    // active_come_here Lv3: attacks taken while the taunt lasts, answered around the caster when it ends.
    TOptional<FLDCombatRetaliation> Retaliation;
    // passive_cornered Lv3 internal cooldown; passive_second_wind regen surge end.
    int64 LowHPShieldReadyAtMs=0;
    int64 RegenSurgeUntilMs=0;
    // Once-per-release mana grants (passive_public_shame Lv3 control, item_runic_focus ability hit).
    FGuid LastControlManaEffect;
    FGuid LastAbilityManaEffect;
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
    // Snapshot of the releasing source's passive_public_shame amplification, applied when the target is under any control or slow.
    int64 ControlAmpBp=0;
    int64 DisarmDurationMs=0;
    int64 StunDurationMs=0;
    // Rear strike: applied when the releasing body stands in the target's rear arc at this hit.
    int64 BackstabRatioBp=0;
    int64 BackstabStunMs=0;
    // active_thousand_year Lv3: a rooted, slowed or taunted target counts as struck from behind.
    bool bBackstabImpaired=false;
    // A reflection never crits, heals, cleaves or triggers another reflection.
    bool bReflect=false;
    // On-hit effects (lifesteal, cleave, passive_dox_master stacks) trigger at this coefficient; 0 = none. A primary basic is
    // 10000; hits made by on-hit effects, reflections or added damage are always 0. SnapshotOnHit fills the terms.
    int64 OnHitScaleBp=0;
    bool bCleave=false;
    int64 CleaveRatioBp=0;
    double CleaveRadiusCm=0;
    bool bLifesteal=false;
    int64 LifestealRatioBp=0;
    // Ability critical (STAT.AbilityCritMultiplier), decided once per hit at ApplyHit: guaranteed against an impaired or
    // breached target (active_monkey_peach Lv3) or rolled at this chance (passive_precision Lv3).
    bool bCritIfImpaired=false;
    int64 AbilityCritChanceBp=0;
    // passive_eagle_eye: amplification per 100 cm between the source's and the target's feet at the hit, up to the cap.
    int64 DistanceAmpPer100Bp=0, DistanceAmpCapBp=0;
    // Added damage (passive_lurking, item_edge_blade, active_come_here Lv3, sweeps, path slows, damage over time): never an attack active_come_here counts and never
    // an on-hit trigger.
    bool bAddedDamage=false;
    // passive_cornered: omnivamp while the source is below the threshold at the hit.
    int64 OmnivampBp=0, OmnivampThresholdBp=0;
    // Resistance shred this hit applies (active_arc_bolt Lv3, passive_lurking Lv3), keyed by ShredKey.
    FName ShredKey;
    int64 ShredArmorBp=0, ShredMagicResistBp=0, ShredMs=0;
    // active_slipper_throw Lv3: when this hit's slow lands, the source vamps this ratio against the target while it lasts.
    int64 SlowVampBp=0;
    // passive_iron_crotch Lv3: attack-speed cut carried with the slow.
    int64 SlowAttackBp=0;
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
    // active_body_slam Lv3: shield per body struck by the landing (TriggeredShield LandingHit).
    TOptional<FLDDemoEffectDefinition> LandingShield;
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
    // Ambush damage snapshotted at release; delivered with the first body this projectile strikes.
    TOptional<FLDAmbushDamageTerm> Ambush;
    // The complete hit payload snapshotted at release; the contact copies it and names the struck body.
    FLDCombatHit Payload;
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

    // ---- Shared combat primitives (#112 section 5).
    // The only path that commits a control or slow onto a body life: hard controls (Root, Stun, Disarm, Taunt) pass the
    // body's diminishing-returns chain, the instance is applied with its action interruptions, the spray interrupt and
    // the assist contribution are recorded. bApplied is false when nothing landed (an existing instance, a zero duration).
    bool CommitControl(FLDBodyCombatState& Victim, const FLDControlApplication& App, int64 NowMs, bool& bApplied, FString& Error);
    // Any active root, stun, disarm, taunt, knockback or slow (passive_public_shame, active_monkey_peach Lv3).
    bool IsImpaired(const FLDBodyCombatState& Body, int64 NowMs) const;
    // The remaining-cooldown cut of one entry's bound track (LDCombatSourceRules::ReduceReadyAt); MAX_int64 = ready now.
    // False when the entity holds no track at that entry.
    bool ReduceCooldown(const FGuid& Entity, ELDCombatEntry Entry, int64 AmountMs);
    // The same cut on the learned track of one definition, bound or not.
    bool ReduceLearnedCooldown(const FGuid& Entity, FName DefinitionId, int64 AmountMs);
    // Lifesteal and cleave terms of a source's held equipment passives for a hit triggering on-hit effects at ScaleBp.
    bool SnapshotOnHit(const FLDBodyCombatState& Source, int64 ScaleBp, FLDCombatHit& InOut, FString& Error) const;
    // Source-side terms every hit of this body carries: passive_public_shame, passive_eagle_eye and passive_cornered omnivamp.
    bool StampSourceTerms(const FLDBodyCombatState& Source, FLDCombatHit& InOut, FString& Error) const;
    // The passive ambush bonus of one struck body: its own damage instance (added damage), never a crit, control or on-hit.
    static FLDCombatHit MakeAmbushHit(const FLDCombatHit& Primary, const FLDAmbushDamageTerm& Term);

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
    bool QueueThorns(const FLDCombatHit& Hit, ALDCharacter& Holder, FLDBodyCombatState& Body, int64 DamageP2, bool bCritical, FString& Error);
    // Self effects shared by a release and a jump landing (shield, root cleanse, mana).
    // bCleansed: the same release removed a root or slow (active_guard_pulse Lv3 shield multiplier).
    bool ApplySelfEffect(FLDBodyCombatState& Body, const FLDDemoEffectDefinition& Effect, const FGuid& EffectId,
        int64 NowMs, FString& Error, bool bCleansed=false);
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
    // On-hit effects of one committed hit at its OnHitScaleBp (lifesteal is summed into the hit's vamp; cleave sweeps
    // the bodies Cleaved named before the writes). Hits it creates carry OnHitScaleBp 0. C2 adds passive_dox_master stacks here.
    bool ApplyOnHitEffects(const FLDCombatHit& Hit, FLDBodyCombatState& Target, const FLDDamageEvaluation& Damage,
        const TArray<FLDCombatBodyRef>& Cleaved, int64 NowMs, FString& Error);
    // The lone resolver of passive effects a projection holds: learned trigger ranks and equipment families (once each).
    bool CollectPassiveEffects(const FLDAttributeProjection& Projection, ELDDemoEffectOp Op, TArray<FLDPassiveEffectRef>& Out,
        FString& Error) const;
    // Whether the body's ambush passive is ready at NowMs, its dash/stealth credit included.
    bool AmbushReadyNow(const FLDBodyCombatState& Body, int64 NowMs) const;
    // Adds ambush readiness credit for the current out-of-combat window (passive_lurking dashes, item_paper_box stealth).
    void CreditAmbush(FLDBodyCombatState& Body, int64 AmountMs);
    // Mana and shield grants of triggered effects; both mark the owner view.
    void GrantMana(FLDBodyCombatState& Body, int64 AmountP2);
    bool GrantShield(FLDBodyCombatState& Body, int64 AmountP2, int64 DurationMs, const FGuid& EffectId, int64 NowMs, FString& Error);
    // Cast range of an active for this body now: the authored range, its share of the extra basic-attack range,
    // passive_eagle_eye Lv3 for Unit/projectile actives and item_phase_anchor for jumps.
    bool CastRangeCm(const FLDBodyCombatState& Body, const FLDDemoTargetDefinition& Target, double& OutCm, FString& Error) const;
    // End-of-dash path slows (active_lunge Lv3) and taunt retaliations (active_come_here Lv3) due by now.
    bool ResolveDashSweeps(FString& Error);
    bool ResolveRetaliations(FString& Error);
    // Movement gathered by armed momentum since the last step (Effects).
    void AdvanceMomentum(int64 NowMs);
    // Hooks of one landed basic: stacks, refunds, counters and the armed bonuses it consumes.
    bool OnBasicLanded(FLDBodyCombatState& Body, const FLDCombatHit& Primary, FString& Error);
    // Recomputes the effective projection from BuildProjection when the active temporary terms changed.
    bool RefreshTemporary(FLDBodyCombatState& Body, ALDCharacter* Actor, int64 NowMs, FString& Error);
    // Ends lapsed slows, timed bonuses and exposures of every body and refreshes their effective projections.
    bool RefreshAllTemporary(int64 NowMs, FString& Error);
    // Installs a new build/monster projection on a body and reapplies its temporary terms.
    bool InstallBodyProjection(FLDBodyCombatState& Body, ALDCharacter* Actor, const FLDAttributeProjection& Projection,
        int64 NowMs, FString& Error);

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
    TArray<FLDCombatDashSweep> DashSweeps;
    TArray<FLDCommittedDeath> PendingDeaths;
    // Committed positive contacts against camp members, delivered to the camp owner within the same Effects.
    TArray<FLDCombatHostileContact> PendingContacts;
    TArray<FLDCommittedDeath> RetryRewards;
    FRandomStream CritRandom;
    bool bAdvancing=false;
};
