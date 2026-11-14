#pragma once
#include "CoreMinimal.h"
#include "Combat/LDDashContactRules.h"
#include "GameFramework/Actor.h"
#include "Combat/LDCombatEvaluationTypes.h"
#include "Combat/LDAttributeProjection.h"
#include "Combat/LDAssistRules.h"
#include "Combat/LDCombatTypes.h"
#include "Combat/LDDamageRecap.h"
#include "Combat/LDCombatControlRules.h"
#include "Combat/LDCombatMechanics.h"
#include "Combat/LDCombatKeywords.h"
#include "Combat/LDCombatElements.h"
#include "Development/LDBuildBlueprint.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Vision/LDViewTypes.h"
#include "LDCombatDirector.generated.h"

struct FLDDormZones;
namespace LDAirdrop { struct FCandidate; }
enum class ELDCharmKind : uint8;
struct FLDCharmEffectFact;

class ALDCharacter;
class ALDGameMode;
class ALDPlayerController;
class FLDDemoContentSnapshot;
enum class ELDGrowthReason : uint16;
struct FLDDemoAbilityDefinition;
struct FLDDemoAbilityValues;
namespace LDTactical { struct FOutOfCombatRecovery; }
struct FLDDemoTargetDefinition;
struct FLDInnateDashTerms;
struct FLDNativeMotionBatch;
struct FLDCombatDashContact;

// What one release shares across all of its hits: a mana grant pays once per release, and its B17 amplification is one
// §10 trigger (at its first positive hit). PaidManaP2 is the mana this release really paid (after M06), B13's refund
// basis; AlternateAmpBp is B15's amplification locked at release for its native direct damage, one §10 trigger.
struct FLDCombatCastLedger
{
    bool bManaGrantPaid=false;
    bool bIdleAmpTriggered=false;
    int64 PaidManaP2=0;
    int64 AlternateAmpBp=0;
    bool bAlternateAmpTriggered=false;
};

enum class ELDCombatActionKind : uint8 { Basic, Ability };

// One trigger effect a body holds, resolved from the approved catalog (ALDCombatDirector::CollectPassiveEffects): an
// installed component's effect, its fixed amounts grown for the holder's level. Held by value: the projection it came
// from may be replaced while the caller still reads it.
struct FLDPassiveEffectRef
{
    FString Key;                    // component id; the dedup key of per-source marks and ledgers
    FLDCombatSourceRef Source;      // Kind Component, DefinitionId = component id
    FLDDemoEffectDefinition Effect;
    // The install parameters of an ActiveElement binding (B18); empty/None otherwise.
    FName BoundActive;
    ELDDemoElement Element=ELDDemoElement::None;
    // #198 F: a mod/connector's install target and C12's repeated base.
    ELDComponentTargetKind TargetKind=ELDComponentTargetKind::None;
    FName TargetId;
    FName BoundComponent;

    // Whether this mod/connector is installed on Exit: a learned active, a component, or the innate D.
    bool Targets(const FLDCombatSourceRef& Exit) const
    {
        switch (TargetKind)
        {
        case ELDComponentTargetKind::Active: return Exit.Kind==ELDCombatSourceKind::Learned && Exit.DefinitionId==TargetId;
        case ELDComponentTargetKind::Component: return Exit.Kind==ELDCombatSourceKind::Component && Exit.DefinitionId==TargetId;
        case ELDComponentTargetKind::Dash: return Exit.Kind==ELDCombatSourceKind::Innate;
        default: return false;
        }
    }
};

// #198 F: one learned active (or the innate D) as its owner's installed mods compile it (spec §12.3): the authored target
// and value set with M03/M04/M05/M06/M08/M09 applied, the package multipliers of its native damage and contact damage,
// and the delivery rewrites M01 (spread) and M07 (compression) ask for.
struct FLDCombatActiveTerms
{
    FLDDemoTargetDefinition Target;
    FLDDemoAbilityValues Values;
    int64 WindupMs=0;
    // Native direct damage package multiplier (M01 Unit/projectile, M03, M05), applied as the hits' PackageScaleBp.
    int64 PackageScaleBp=10000;
    // Contact damage multiplier of a dash (M08) for its native contact hit and B05.
    int64 ContactScaleBp=10000;
    // The innate D's distance and cooldown multipliers (M08/M09); a learned active's are already in Values.
    int64 DashScaleBp=10000;
    int64 CooldownScaleBp=10000;
    TOptional<FLDDemoEffectDefinition> SpreadUnit, SpreadProjectile, CompressCone, CompressZone, CompressChain;
    // M13-M15: the installed mod that asks for a derived host hit (its component names the ledger trigger).
    TOptional<FLDPassiveEffectRef> ProjectileReturn, ProjectilePierce, ChainExtra, EchoImpact;
    // M16-M20 on this active (their effects already scaled by M03 where they move a body).
    TOptional<FLDPassiveEffectRef> ZoneTether, DefenseShove, FinalKnockback, EmpowerReach, Pull;
    TOptional<FLDDemoEffectDefinition> FinalKnockbackTerms, PullTerms;
};

// #198 F C02: a knockback whose source holds C02 on it watches for its first real blocking fact.
struct FLDCombatKnockbackImpact
{
    FGuid EffectId;
    FLDCombatBodyRef Holder;
    FGuid CreditOwner;
    FLDPassiveEffectRef Impact;
    bool bWorkshopImpact=false;
    bool bFinalBowling=false;
    FName FinalCardId;
    int64 FinalBaseP2=0, FinalAPRatioBp=0;
    double FinalDistanceCm=0;
    FVector Direction=FVector::ZeroVector;
    FLDCombatSourceRef PushSource;
    int64 HolderADP2=0, HolderAPP2=0;
    int64 EndMs=0;
};

// #198 F C05: a 3 s pursuit mark one holder set on one target life with its first hard control kind.
struct FLDCombatControlMark
{
    FLDCombatBodyRef Target;
    ELDControlKind Kind=ELDControlKind::Root;
    FName Component;
    int64 ExpiresAtMs=0;
};

// #198 F: what one original action (its EffectId) already received from a capped connector (C07 mana, C11 heal;
// C13/C15 once-per-release cuts, C16 mana with the distinct enemies its native direct hits already struck).
struct FLDCombatEffectBudget
{
    FGuid EffectId;
    FName Component;
    int64 SpentP2=0;
    int64 AtMs=0;
    TArray<FLDCombatBodyRef> Struck;
};

// C14: a mark one holder's host active left on one target life; a different active's next native direct hit on it
// takes the bonus and consumes it.
struct FLDCombatSetupMark
{
    FLDCombatBodyRef Target;
    FName Component;
    FName Host;
    int64 ExpiresAtMs=0;
};

// C18: the host shield being watched (what it really absorbed so far) and the force stored when it ended.
struct FLDCombatStoredForceWatch
{
    FGuid ShieldId;
    FName Component;
    FName Host;
    int64 AbsorbedP2=0;
};
struct FLDCombatStoredForce
{
    FName Component;
    FName Host;
    int64 AmountP2=0;
    int64 ExpiresAtMs=0;
};

// C20: the "wide" token a host's hard control gave; a different active's next release consumes it.
struct FLDCombatWideToken
{
    FName Component;
    FName Host;
    int64 RatioBp=0;
    int64 ExpiresAtMs=0;
};

// #198 F: one body seen at a time (M11: hit by the holder's active directly; C08: hurt the holder).
struct FLDCombatBodyMark
{
    FLDCombatBodyRef Body;
    int64 AtMs=0;
};

struct FLDFinalModHitMark
{
    FGuid HitId;
    int64 AtMs=0;
    bool bConsumed=false;
};

struct FLDFinalModLifeState
{
    bool bPresenceInitialized=false;
    bool bInside=false;
    bool bBreachUsed=false;
    double OutsideSinceSeconds=0;
    int64 ArmedUntilMs=0;
    int64 VoiceUntilMs=0;
    TArray<FLDFinalModHitMark> Hits;
};

struct FLDFinalModTargetCooldown
{
    FGuid Target;
    int64 ReadyAtMs=0;
};

struct FLDFinalControlExit
{
    FLDCombatBodyRef SourceBody;
    FLDCombatBodyRef Target;
    FGuid EffectId;
    FLDCombatSourceRef Source;
    ELDControlKind Kind=ELDControlKind::Root;
    int64 ExpectedEndMs=0;
    int64 DueAtMs=0;
};

struct FLDFinalTrafficSegment
{
    FGuid Key;
    LDDashContactRules::FGroundBand Ground;
};

struct FLDFinalTrafficTrail
{
    FGuid EffectId;
    FGuid ObservedKey;
    FLDCombatBodyRef SourceBody;
    FLDCombatSourceRef Source;
    int64 EndMs=0;
    TArray<FLDFinalTrafficSegment> Segments;
    TArray<FLDCombatBodyRef> Inside;
};

struct FLDFinalMotionFrame
{
    TSharedPtr<const FLDNativeMotionBatch> Batch;
    TOptional<FLDCombatVoluntaryDash> Dash;
};

// #198 F M12: a deferred share of one enemy hit, paid at its original amount at DueAtMs unless healing repays it first.
struct FLDCombatDebt
{
    int64 AmountP2=0;
    int64 DueAtMs=0;
    FLDCombatBodyRef SourceBody;
    FGuid SourceEntity;
    FGuid CreditOwner;
    FLDCombatSourceRef Source;
};

// #198 F M02: a full ledger batch locked for its delayed cash-out on its target life.
struct FLDCombatLockedLedger
{
    FLDCombatBodyRef Owner;
    FLDCombatBodyRef Target;
    FLDCombatSourceRef Source;
    int64 AmountP2=0;
    int64 DueAtMs=0;
    int64 ScaleBp=10000;
    double MaxDistanceCm=0;
    double SpreadRadiusCm=0;
    ELDDemoDamageType DamageType=ELDDemoDamageType::Magical;
};

// One accepted, not yet released action of one body life. Dies with the life.
struct FLDCombatAction
{
    ELDCombatActionKind Kind=ELDCombatActionKind::Basic;
    FLDCombatRequestKey Key;
    uint64 Token=0;
    ELDCombatEntry Entry=ELDCombatEntry::None;
    FLDCombatSourceRef Source;
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
    // #198 rest phases (Combat/LDCombatRest.cpp): rest lives share one group; field bodies have
    // none. A suspended body (frozen world during a rest) takes and deals nothing.
    FGuid IsolationGroup;
    bool bSuspended=false;
    // A shared training target never dies (refills at 0 HP) and grants nothing.
    bool bTrainingDummy=false;
    // #198 session D PK: the body's side in the PK isolation group (0: none). Same-side bodies never affect each other
    // (friendly fire off). ExposedUntilMs: a spectator throw revealed it to the PK entrants until then (half-open).
    uint8 PKSide=0;
    int64 ExposedUntilMs=0;
    struct FResources
    {
        int64 HpP2=0,MaxHpP2=0,MpP2=0,MaxMpP2=0,ShieldP2=0;
        uint64 Revision=0;
    } Resources;
    FLDBasicTiming BasicTiming;
    FLDFinalModLifeState FinalMod;
    // Effective attributes: BuildProjection plus the temporary terms below (LDAttributeProjection::ApplyTemporary).
    FLDAttributeProjection AttributeProjection;
    // The installed build (or monster) projection without temporary terms.
    FLDAttributeProjection BuildProjection;
    // The temporary terms AttributeProjection currently includes.
    FLDTemporaryAttributeTerms AppliedTemporary;
    uint64 AttributeRevision=0;
    TOptional<FLDObservedPublicFacts> PublicFacts;
    FName MonsterId;                       // CampMember bodies only
    uint8 CampStage=0;                     // #97 CampMember bodies only: growth stage fixed for this life
    uint64 StateRevision=1;                // statuses/action changes; resources use Resources.Revision
    TArray<FLDCombatRoot> Roots;
    TArray<FLDCombatSlow> Slows;
    TArray<FLDCombatTimedBonus> TimedBonuses;
    // Hard-control diminishing returns of this life (LDCombatControl::DiminishHardControl).
    FLDHardControlChain HardControl;
    // B06 (nth_basic_bonus): this life's landed primary basics while the counter is held; it survives target changes and
    // ends with the life.
    int32 LandedBasics=0;
    // Once-per-release mana grant of item_runic_focus (ability hit).
    FGuid LastAbilityManaEffect;
    // Disarm and Stun instances (LDCombatControl); each keeps its effect, source and half-open expiry.
    TArray<FLDCombatRoot> Disarms;
    TArray<FLDCombatRoot> Stuns;
    // The pending empowered-basic packages, at most one per source (spec §10.2), and this life's thorns repeat windows.
    TArray<FLDCombatEmpower> Empowers;
    TArray<FLDThornsMark> ThornsMarks;
    // Assist contributions other contenders made to this life (LDAssist); read once when the life locks its death.
    TArray<FLDAssistContribution> AssistContributions;
    // Positive hits this contender life took from other bodies within the recap window (LDDamageRecap); summed into the
    // death fact when the life locks its death.
    TArray<FLDDamageSample> DamageLog;
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
    // and burn of this life and the damage-triggered shield's rest (B09: it starts only on a granted shield).
    TArray<FLDCombatRoot> Silences;
    FLDCombatSharedBreach SharedBreach;
    FLDCombatBurn Burn;
    int64 DamagedShieldReadyAtMs=0;
    // B16 (triggered_shield AbilityHit): its rest, started only by a granted shield.
    int64 AbilityHitShieldReadyAtMs=0;
    // B15 (alternate_amp): the last Q/W/E release of this life (the innate dash is none).
    FName LastActiveRelease;
    int64 LastActiveReleaseAtMs=0;
    TArray<FLDCombatSlow> AttackSlows;
    FGuid ChannelEffectId;
    uint64 ChannelDirectSequence=0;
    int64 ChannelStartedAtMs=0;
    // dash_contact_refund's rest.
    int64 ContactRefundReadyAtMs=0;
    TSharedPtr<FLDCombatDashContact> DashContact;
    // #198 elements (LDCombatElements): the water or electric mark (fire is Burn), the electric discharge rest of this
    // target, the S16 stance and the S21 cut of new heals and shields.
    FLDCombatElementMark ElementMark;
    int64 DischargeReadyAtMs=0;
    FLDCombatBrace Brace;
    FLDCombatGrievous Grievous;
    // #198 B12: the voluntary dash in flight (ResolveDashCompletions arms DashComplete empowers when it completes).
    TOptional<FLDCombatVoluntaryDash> VoluntaryDash;
    // #198 B02: every owner's ledger on this life, one per owner and component.
    TArray<FLDCombatLedger> Ledgers;
    // #198 F workshop state of this life (LDCombatWorkshop.cpp): C02's watched knockback, the C05 marks this holder set,
    // capped per-action budgets (C07/C11), M11's directly hit bodies, C08's recent attackers, M12's debts, C01's open
    // window, C08's store and the source of the current shield (C10).
    TOptional<FLDCombatKnockbackImpact> PendingImpact;
    TArray<FLDCombatControlMark> ControlMarks;
    // C14: the setup marks this holder set (one per target life, connector and host).
    TArray<FLDCombatSetupMark> SetupMarks;
    TArray<FLDCombatEffectBudget> EffectBudgets;
    TArray<FLDCombatBodyMark> DirectHitMarks;
    TArray<FLDCombatBodyMark> RecentAttackers;
    TArray<FLDCombatDebt> Debts;
    int64 FireWindowSumP2=0, FireWindowEndMs=0;
    int64 CounterStoreP2=0, CounterLastAddMs=0, CounterNextFireMs=0;
    // C08: the latest budgeted root action (a C11 heal's effect) still in the store; unset for unbudgeted heals only.
    FGuid CounterRootEffectId;
    FLDCombatSourceRef ShieldGrantSource;
    // C18 (the watched host shield and the stored force) and C20 (the wide token).
    TOptional<FLDCombatStoredForceWatch> StoredForceWatch;
    TOptional<FLDCombatStoredForce> StoredForce;
    TOptional<FLDCombatWideToken> WideToken;
};

// Persistent cooldown identity; the entry slot is only its current binding.
struct FLDCombatTrack
{
    FGuid IssuedCooldownId;
    ELDCombatSourceKind Kind=ELDCombatSourceKind::None;
    FName DefinitionId;
    int64 ReadyAtMs=0;
    int64 LastDurationMs=0;
    FGuid LastEffectId;
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
    // #198 combat window (contenders): the authority ms of the last combat action (MarkBuildUnsafe); 0 = none yet. Kept
    // across lives; the window is GROWTH.BuildSafeWindowSec. #222: it no longer gates any build command; the owner view
    // publishes it only for the HUD's combat fold of the field summary.
    int64 LastBuildUnsafeAtMs=0;
    // #198 F C01 (spec §13.3): the authority ms of its first install, from which its fixed window boundaries run; kept across
    // lives, uninstalls and reinstalls (a reinstall keeps the original boundaries). Unset until C01 is first installed.
    TOptional<int64> FireWindowAnchorMs;
    int64 FinalModReadyAtMs=0;
    TArray<FLDFinalModTargetCooldown> FinalModTargetCooldowns;
};

// One committed hit waiting for the Effects phase.
struct FLDCombatHit
{
    FGuid HitId;
    FGuid EffectId;
    bool bFinalModDerived=false;
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
    // Snapshot of the source's control amplification (control_damage_amp, B07): joins the damage bonus pool when the target
    // was move-slowed, hard-controlled or knocked back before this hit.
    int64 ControlAmpBp=0;
    // #198 B17 (idle_ready_amp): locked at the release of an active that stood ready and idle long enough; carried only by
    // that release's native direct damage (every pulse of it) and added into the damage bonus pool.
    int64 IdleReadyAmpBp=0;
    int64 DisarmDurationMs=0;
    int64 StunDurationMs=0;
    // A reflection never crits, heals or triggers another reflection.
    bool bReflect=false;
    // On-hit effects (lifesteal) trigger at this coefficient; 0 = none. A primary basic is 10000; hits made by
    // reflections or added damage are always 0. SnapshotOnHit fills the terms.
    int64 OnHitScaleBp=0;
    bool bLifesteal=false;
    int64 LifestealRatioBp=0;
    // B04: the share of this hit's raw package that is its own (not merged empowered packages); lifesteal heals only it.
    int64 LifestealShareBp=10000;
    // Added damage (dash contact damage): never an on-hit trigger.
    bool bAddedDamage=false;
    // #112 C2 (LDCombatMechanicsDirector.cpp).
    bool bPeriodic=false;            // a zone pulse or burn tick: no crit, no on-hit
    bool bFromShieldTrigger=false;   // a retort or burst hit: never a Q/W hit for passives
    bool bSilent=false;              // a zone's periodic slow refresh: no visible hit cue
    double KnockbackCm=0;
    int64 KnockbackMs=0;
    bool bKnockbackFromAnchor=false; // pushed away from KnockbackAnchorCm instead of the source body's foot
    FVector KnockbackAnchorCm=FVector::ZeroVector;
    // M20: the knockback pulls toward its origin instead and stops KnockbackStopCm (centre to centre) short of it.
    bool bKnockbackPull=false;
    double KnockbackStopCm=0;
    // #147 build v7 keyword payload (docs/systems/COMBAT_AND_GROWTH.md).
    int64 SilenceDurationMs=0;
    int32 BreachStacks=0;            // shared breach stacks this hit applies
    int32 BurnStacks=0;              // fire layers this hit applies (LDCombatElements batch: fire, water, electric)
    // #198 elements: wet and electric this hit applies; ElementLevel/ElementAPP2 are the applier snapshot every element of
    // the hit reads (fire damage, reaction damage, discharges). StampElementSnapshot fills them.
    bool bApplyWet=false;
    bool bApplyElectric=false;
    int32 ElementLevel=1;
    int64 ElementAPP2=0;
    // Status or settle damage (early fire settle, discharge, reaction): never a skill hit for readers (AI spec §8).
    bool bStatusSettle=false;
    // S21: new heals and shields of the target are cut by GrievousBp for GrievousMs.
    int64 GrievousBp=0, GrievousMs=0;
    bool bBurnTick=false;            // a burn tick: no crit, no on-hit, never a Q/W hit for passives
    int64 AttackSlowBp=0, AttackSlowMs=0;
    // Delivery scaling is separate from the additive damage bonus pool (impact outer ring, for example).
    int64 DeliveryScaleBp=10000;
    // #198 F workshop package multiplier (FLDDamageEvaluationInput::PackageScaleBp).
    int64 PackageScaleBp=10000;
    // #198 F: the exit a knockback of this hit belongs to (S01, B06, C04) for C02; and an M12 debt payment (no resistance,
    // bonus, deferral or trigger).
    FLDCombatSourceRef KnockbackSource;
    bool bDebtPayment=false;
    // Native direct damage of an active (spec §8 主动直接伤害): its damage segment, its zone pulses and the hits copied
    // from its payload (projectile, cone wave, impact, dash contact). B17 and B18 read it; component-added hits never set it.
    // A derived host hit (a later return, pierce or echo of the same release EffectId) sets it and counts as native.
    bool bDirectAbilityDamage=false;
    bool bDashContactHit=false;
    bool bChannelHit=false;
    // #198 B02: a ledger cash-out, a status/settle hit: it never adds a ledger entry or triggers B18.
    bool bLedgerCashOut=false;
    // #198 B: burn layers a component's fire adds to this hit (ApplyComponentElement), credited to ComponentBurnSource.
    int32 ComponentBurnStacks=0;
    FLDCombatSourceRef ComponentBurnSource;
    // #198 B10/B11/B18: a component's water or electric on this hit, applied as that component's own application (its
    // source, the living holder's level and AP now) after the hit's native element of the same kind; None when absent.
    FLDCombatSourceRef ComponentWetSource;
    FLDCombatSourceRef ComponentElectricSource;
    TSharedPtr<FLDCombatCastLedger> CastLedger;
};

// "native direct hit" (B13-B16): an ability hit of the host active's own release (its damage, zone pulses, impacts,
// traps, chain hops, S16's answer and derived host hits sharing the release EffectId). Basics, the innate dash, reactions,
// burn ticks, settles, debts, reflections and component-added hits are none. The caller checks hostility and damage.
inline bool IsNativeDirectHit(const FLDCombatHit& Hit)
{
    return Hit.HitClass==ELDCombatHitClass::Ability && Hit.bDirectAbilityDamage && Hit.Source.Kind==ELDCombatSourceKind::Learned
        && !Hit.bReflect && !Hit.bAddedDamage && !Hit.bBurnTick && !Hit.bStatusSettle && !Hit.bLedgerCashOut
        && !Hit.bDebtPayment && !Hit.bFromShieldTrigger;
}

struct FLDCombatDashContact
{
    FGuid EffectId;
    FLDCombatBodyRef Body;
    FLDCombatSourceRef Source;
    int64 StartMs=0, EndMs=0, PaidManaP2=0;
    TArray<FLDCombatBodyRef> Struck;
    TOptional<FLDCombatHit> NativeHit;
    int32 NativeHitsRemaining=0;
    bool bRefunded=false;
    bool bCanDamage=false, bCanRefund=false;
    // #198 F: the dash direction (C04 pushes along it), M08's contact multiplier and whether C04 is installed on it.
    FVector Direction=FVector::ZeroVector;
    int64 ContactScaleBp=10000;
    bool bCanKnock=false;
};

struct FLDCombatContactMotion
{
    TSharedPtr<const FLDNativeMotionBatch> Batch;
    TSharedPtr<FLDCombatDashContact> Dash;
};

struct FLDCombatChannel
{
    FLDCombatHit Payload;
    FVector Direction=FVector::ZeroVector;
    double RangeCm=0, AngleDegrees=0;
    int64 NextPulseMs=0, IntervalMs=0;
    int32 Remaining=0;
    int64 FinalSlowBp=0, FinalSlowMs=0;
    // M18: the final wave knocks every struck enemy away from the channel origin (source: the host).
    double FinalKnockbackCm=0;
    int64 FinalKnockbackMs=0;
};

// #198 F: what a delayed ground blast is. An S18 landing, an S19 detonation and an M07 compressed zone are natural
// completions C06 answers; C06's own aftershock and an M02 settle never are.
// M15's echo is a derived host hit: it never echoes again and C06 never answers it.
enum class ELDCombatGroundOrigin : uint8 { Impact, Trap, CompressedZone, Aftershock, Settle, Echo };

// An S19 trap is armed from its placement until ExpiresAtMs (placement + its life); a legal enemy inside InnerRadiusCm
// triggers it (bTriggered) and it blows over RadiusCm at its own point FuseMs later (ReadyAtMs then); an untriggered trap
// that reaches ExpiresAtMs ends without a blast.
struct FLDCombatGroundEffect
{
    FLDCombatHit Payload;
    FVector CentreCm=FVector::ZeroVector;
    double RadiusCm=0, InnerRadiusCm=0;
    int64 ReadyAtMs=0, ExpiresAtMs=0, OuterRatioBp=10000;
    bool bTrap=false;
    bool bTriggered=false;
    int64 FuseMs=0;
    ELDCombatGroundOrigin Origin=ELDCombatGroundOrigin::Impact;
    // M15 on the host release (S18 impact, S19 trap): its echo follows this blast.
    TOptional<FLDPassiveEffectRef> Echo;
    // Observers key a blast on its release EffectId; an echo, which shares it, carries its own key.
    FGuid ObservedKey;
    // C17 (traps only): moved once already; CreatedSerial orders the owner's zones and traps by creation.
    bool bRelocated=false;
    uint64 CreatedSerial=0;
};

// Released straight, non-homing projectile: first legal hostile body (after M14's extra bodies) or blocking terrain ends it.
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
    // #198 F M01: the first contact's damage also lands on the other bodies within this radius of the struck foot.
    double SpreadRadiusCm=0;
    // M14: after a body the flight goes on for PierceRemaining more distinct bodies, each at PierceScaleBp of the
    // package with the same slow and controls. M13: where it ends, a return flight starts toward the thrower at
    // ReturnScaleBp; a return (bReturn) never pierces or returns. Struck holds the bodies this flight already hit.
    int32 PierceRemaining=0;
    int64 PierceScaleBp=0, ReturnScaleBp=0;
    FName PierceComponent, ReturnComponent;
    bool bReturn=false;
    TArray<FGuid> Struck;
    // A return that starts on the body its outbound flight ended on touches that body at its start (it may strike it again).
    FGuid StartContact;
    // The complete hit payload snapshotted at release; the contact copies it and names the struck body.
    FLDCombatHit Payload;
};

// A ground zone (EFFECT zone): Count pulses from StartMs+DelayMs every IntervalMs, and (#198) a slow check and a burn check
// every interval from its start while it lasts. Its owner's payload terms are snapshotted at release.
struct FLDCombatZone
{
    FGuid EffectId;
    // Two split footprints observe independently while retaining the original release ledger.
    FGuid ObservedKey;
    FLDCombatBodyRef SourceBody;
    FGuid SourceEntity;
    FGuid CreditOwner;
    FLDCombatSourceRef Source;
    FVector CentreCm=FVector::ZeroVector;
    double RadiusCm=0;
    int64 StartMs=0, EndMs=0, DelayMs=0, IntervalMs=0;
    int32 Count=0, Pulsed=0;
    // zone_slow: every SlowIntervalMs each enemy inside is slowed by SlowBp for SlowDurationMs.
    bool bSlow=false;
    int64 SlowBp=0, SlowIntervalMs=0, SlowDurationMs=0, NextSlowAtMs=0;
    bool bDamage=false;
    bool bFinalModDerived=false;
    bool bFinalSplit=false;
    int64 PulseSlowBp=0, PulseSlowMs=0;
    int64 BaseP2=0, ADRatioBp=0, APRatioBp=0;
    ELDDemoDamageType DamageType=ELDDemoDamageType::Magical;
    int64 SourceADP2=0, SourceAPP2=0;
    // B07, snapshotted at release and evaluated against each target's state before the pulse.
    int64 ControlAmpBp=0;
    // #198 B17: the release's locked idle amplification, carried by every damage pulse.
    int64 IdleReadyAmpBp=0;
    // #198 F: the release's workshop package multiplier (M05), carried by every damage pulse.
    int64 PackageScaleBp=10000;
    // #147 zone_burn: BurnStacks layers (at the owner's level and AP snapshotted at release) on every enemy inside,
    // every BurnIntervalMs from the zone's start while it lasts. #198 S08: a damaging zone's burn beats with its pulses,
    // so every pulse carries PulseBurnStacks in the same hit (one element batch: its fire before a B18 water/electric).
    int32 BurnStacks=0;
    int64 BurnIntervalMs=0, NextBurnAtMs=0;
    int32 PulseBurnStacks=0;
    // #198: the owner's element snapshot (level, AP) at release; zone_wet makes every pulse apply wet; zone_haste gives
    // the owner and its current-match teammates (OwnerGroup/OwnerPKSide at release, PK side 0: none) HasteBp move speed
    // while their foot stands inside.
    int32 ElementLevel=1;
    int64 ElementAPP2=0;
    bool bWet=false;
    int64 HasteBp=0;
    FGuid OwnerGroup;
    uint8 OwnerPKSide=0;
    TSharedPtr<FLDCombatCastLedger> CastLedger;
    // M16: the centre follows the living source body's foot at every mechanics advance.
    bool bTether=false;
    // C17: moved once already (never again); CreatedSerial orders the owner's zones and traps by creation.
    bool bRelocated=false;
    uint64 CreatedSerial=0;
};

// #198 S10: after the first hit, every IntervalMs the nearest not yet struck legal enemy within RadiusCm of the last
// struck body takes the payload at RatioBp of the previous bounce's share, until Remaining runs out or nobody is near.
struct FLDCombatChain
{
    FLDCombatHit Payload;
    TArray<FGuid> Struck;
    FLDCombatBodyRef Last;
    FVector LastFootCm=FVector::ZeroVector;
    int32 Remaining=0;
    int64 NextAtMs=0, IntervalMs=0, RatioBp=0, ScaleBp=10000;
    double RadiusCm=0;
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
    // Only the core's validated native-motion commit calls this; resolution remains in Effects.
    void CaptureSkillMotion(TSharedPtr<const FLDNativeMotionBatch> Batch);
    bool NotifyFinalPresence(const struct FLDCaptureEvent& Event, FString& Error);
    bool IsAggressiveEntry(const FLDCombatBodyRef& Body, ELDCombatEntry Entry) const;

    // ---- Commands shared by human RPCs, the opponent AI and registered camp members.
    bool ResolveControllerCaller(const AController& Controller, FLDCombatCaller& Out, FString& Error) const;
    bool ReadDirectRequestCursor(const FGuid& Entity, uint64& Through, FString& Error) const;
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
    // Public Q/W/E facts of a build (#112 §10): each held active's native id in its slot. False when a held active has no
    // approved definition or a slot other than Q/W/E.
    static bool PublicActivesOf(const FLDBuildProjectionInput& Build, const FLDDemoContentSnapshot& Content,
        FLDObservedActivesView& Out);
    // #198 combat window of a contender: the absolute authority ms until which it counts as in combat (<= now: out of
    // combat); 0 when it has no combat action yet or no Combat record. #222: no build command reads it.
    int64 ReadBuildSafeAtMs(const FGuid& Contender) const;

    // ---- Growth compound commits.
    FLDCombatPrepareResult PrepareBuildChange(const FLDCombatCaller& Caller, const FLDBuildProjectionInput& Input,
        const FLDExpectedBuildVersions& Expected) const;
    void ApplyBuildChange(FLDPreparedCombatProjection&& Prepared, const FLDGameplayWritePermit& Permit);
    // #198 §10 build blueprint ledger of this match (LDCombatBlueprintLedger.cpp): every installed build version of each
    // contender and the real facts credited to it. Read once by the core when the result is published.
    const FLDBuildLedger& GetBuildLedger() const { return BuildLedger; }
    // This match's committed damage credited to Contender per source, largest first (LDCombatBlueprintLedger.cpp). False for
    // an entity without a contender record; a contender that dealt none has an empty list.
    bool DamageTotalsOf(const FGuid& Contender, TArray<FLDDamageLine>& Out) const;

#if WITH_DEV_AUTOMATION_TESTS
    // ---- DEV test commands (LDDevTestProtocol.h), committed by ALDGrowthDirector inside the Transactions permit.
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
    // without a build revision, so Growth's in-flight requests stay valid; a dead contender changes only
    // the persistent build of its next life. NumericDomain with Detail when the projection refuses (a final value
    // outside its catalog bound), nothing changes then.
    ELDGrowthReason DevSetAttribute(const FGuid& Contender, bool bClearAll, ELDDemoBonusStat Stat, int64 Amount,
        const FLDGameplayWritePermit& Permit, FString& Detail);
    // Pure part: whether Bonuses changed. Terms stay sorted by stat.
    static bool DevApplyAttribute(TArray<FLDDemoStatBonus>& Bonuses, bool bClearAll, ELDDemoBonusStat Stat, int64 Amount);
    // DEV test reset to the match start, part 1 (pure, inside the reset composite): the empty level-1 build without a
    // DEV layer at Growth's result build revision, through PrepareProjected like any build change.
    FLDCombatPrepareResult DevPrepareReset(const FGuid& Contender, uint64 ResultBuildRevision) const;
    // Part 2, right after ApplyBuildChange of part 1: every learned cooldown track is dropped (a later learn issues a
    // new one, as at the match start), the innate dash track is ready at NowMs, the build safe window has passed and a
    // living body's HP/MP are full. Returns the number of dropped tracks.
    int32 DevFinishReset(const FGuid& Contender, int64 NowMs);
    // Pure part of DevFinishReset's tracks.
    static int32 DevDropTracks(TArray<FLDCombatTrack>& Tracks, int64 NowMs);
#endif

    // ---- Camps.
    FLDCampResetPrepareResult PrepareCampReset(const FLDCampResetInput& Input, const FLDGameplayWritePermit& Permit) const;
    // #198 field charm (Transactions): applies Kind's approved effect to the living body of Contender — heal a share of
    // max HP through HealBody, mana a share of max MP through GrantMana (both capped at the maximum), or refresh the
    // charm move-speed timed bonus. A body already gone or dying gets nothing (bOutApplied false). False only on an
    // authority fault.
    bool ApplyCharm(const FGuid& Contender, ELDCharmKind Kind, const FLDGameplayWritePermit& Permit, bool& bOutApplied,
        FLDCharmEffectFact& Out, FString& Error);
    void ApplyCampReset(FLDPreparedCampReset&& Prepared, const FLDGameplayWritePermit& Permit);

    // ---- Shared combat primitives (#112 section 5).
    // The only path that commits a control or slow onto a body life: hard controls (Root, Stun, Disarm, Taunt) pass the
    // body's diminishing-returns chain, the instance is applied with its action interruptions, the spray interrupt and
    // the assist contribution are recorded. bApplied is false when nothing landed (an existing instance, a zero duration).
    bool CommitControl(FLDBodyCombatState& Victim, const FLDControlApplication& App, int64 NowMs, bool& bApplied, FString& Error);
    // Any active root, stun, disarm, taunt, knockback, silence or slow (#147 "被减速或被控制", control_damage_amp).
    bool IsImpaired(const FLDBodyCombatState& Body, int64 NowMs) const;
    // A Q/W/D activation of this source kind blocked now: Cast for every kind, Skill (silence) for learned actives.
    bool IsCastBlocked(const FLDBodyCombatState& Body, ELDCombatSourceKind Kind) const;
    // The remaining-cooldown cut of one entry's bound track (LDCombatSourceRules::ReduceReadyAt); MAX_int64 = ready now.
    // False when the entity holds no track at that entry.
    bool ReduceCooldown(const FGuid& Entity, ELDCombatEntry Entry, int64 AmountMs);
    // The same cut on the learned track of one definition, bound or not.
    bool ReduceLearnedCooldown(const FGuid& Entity, FName DefinitionId, int64 AmountMs);
    // Lifesteal terms of a source's held trigger effects for a hit triggering on-hit effects at ScaleBp.
    bool SnapshotOnHit(const FLDBodyCombatState& Source, int64 ScaleBp, FLDCombatHit& InOut, FString& Error) const;
    // Source-side terms every hit of this body carries: the control amplification (B07).
    bool StampSourceTerms(const FLDBodyCombatState& Source, FLDCombatHit& InOut, FString& Error) const;
    // #198 build safe window: Entity's latest combat action happened at NowMs (an accepted basic windup, a released
    // offensive skill, hostile positive damage dealt or taken, a hard control or knockback received). Contenders only.
    void MarkBuildUnsafe(const FGuid& Entity, int64 NowMs);
    // ---- #198 rest phases (Combat/LDCombatRest.cpp) ----
    // Whether SourceEntity may affect Target: a suspended target is out of reach, and bodies only interact inside the
    // same isolation group. Rest players cannot be hostile targets, but training targets can.
    bool IsIsolated(const FGuid& SourceEntity, const FLDBodyCombatState& Target) const;
    // Rest vision filter: whether Observer must not see Target (another rest group, or the frozen world). False for a
    // body Combat does not hold.
    bool IsHiddenFrom(const FGuid& Observer, const FLDCombatBodyRef& Target) const;
    // A rest life joins the shared rest group (bRest) or returns to the field group. RespawnsAndWorld permit.
    bool SetRestIsolation(const FLDCombatBodyRef& Body, bool bRest, const FLDGameplayWritePermit& Permit, FString& Error);
    // Freezes or thaws a world body (camp member, Boss) for a rest; its HP/MP stay as they are. RespawnsAndWorld permit.
    bool SetBodySuspended(const FLDCombatBodyRef& Body, bool bSuspended, const FLDGameplayWritePermit& Permit, FString& Error);
    // Phase boundary cleanup (approved §8): every in-flight effect is cancelled (CancelledByPhase, no damage, break or
    // death is invented) and every control, mark, burn, breach, shield and temporary term on every remaining body ends.
    // Returns the number of cancelled effects. RespawnsAndWorld permit.
    int32 CancelAllForPhase(const FLDGameplayWritePermit& Permit);
    // Phase refill (approved §8): every cooldown track of the contender (skills, dash, held tracks of forgotten actives)
    // and its basic cadence are ready at the boundary. New lives start at full HP/MP by construction.
    bool ReadyAllForPhase(const FGuid& Contender, const FLDGameplayWritePermit& Permit, FString& Error);
    // Retires a contender's living body for a phase boundary: its own unsettled effects end with it.
    void RetireBodyForPhase(const FLDCombatBodyRef& Body);
    // Shared dummy: a fixed slot-level chassis without a build, full resources, in the rest group.
    // RespawnsAndWorld permit; Victim is the dummy's own first life, never the attacker's identity.
    bool PrepareTrainingDummy(ALDCharacter& Body, const FLDVictimKey& Victim, uint8 Level,
        const FLDGameplayWritePermit& Permit, FLDBodyCombatState& Out, FString& Error) const;
    // The owner's training reset: owner and dummy refilled, statuses and in-flight effects of both cleared, every
    // owner cooldown ready. RespawnsAndWorld permit.
    bool ResetTraining(const FGuid& Owner, const FGuid& Dummy, const FLDGameplayWritePermit& Permit, FString& Error);
    // Retires the dummy's body and drops its Combat record (a dummy entity never returns).
    void RetireTrainingEntity(const FLDCombatBodyRef& Body);
    // ---- #198 session D PK (Combat/LDCombatPK.cpp) ----
    // Whether SourceEntity and Target are in different isolation groups (or either is suspended); vision uses this (team
    // mates still see each other), IsIsolated adds the PK friendly-fire rule.
    bool IsOutOfGroup(const FGuid& SourceEntity, const FLDBodyCombatState& Target) const;
    // The isolation group Entity acts in (its body's, or for a contender between lives the space it is in); false when it
    // is a suspended world body. Vision gives skill effects the permissions of their source's group (#198 R15).
    bool ActingGroup(const FGuid& Entity, FGuid& OutGroup) const;
    // A PK life joins the PK isolation group with its side. RespawnsAndWorld permit.
    bool SetPKBody(const FLDCombatBodyRef& Body, const FGuid& Group, uint8 Side, const FLDGameplayWritePermit& Permit, FString& Error);
    // The PK ring's environmental damage (approved §2): straight to HP, no resistance, shield, reduction, delay,
    // reflection, trigger or reward; a lethal amount locks a normal unowned death. Effects permit.
    bool ApplyEnvironmentDamage(const FLDCombatBodyRef& Body, int64 AmountP2, const FLDGameplayWritePermit& Permit, FString& Error);
    // A spectator throw landing on a PK body (approved §6): a SlowBp slow for SlowMs through the control path (the
    // strongest slow wins, the end refreshes on a real hit) and Exposed until now + ExposeMs. No damage, hard control,
    // growth or credit. Effects permit.
    bool ApplyThrowHit(const FLDCombatBodyRef& Body, const FGuid& EffectId, int64 SlowBp, int64 SlowMs, int64 ExposeMs,
        const FLDGameplayWritePermit& Permit, FString& Error);
    // Current and maximum HP of a living body; false when Combat does not hold it.
    bool ReadBodyHealth(const FLDCombatBodyRef& Body, int64& OutHpP2, int64& OutMaxHpP2) const;
    // Whether the body is Exposed at NowMs.
    bool IsExposed(const FLDCombatBodyRef& Body, int64 NowMs) const;
    // Line of sight between two feet with the area rule (ELDSightResult::Visible), for the throw's landing.
    bool HasAreaSight(const FVector& FromCm, const FVector& ToCm, bool& bOutVisible, FString& Error) const;

private:
    friend class ALDGameMode;
    friend class FLDRestPhaseTest;
    friend class FLDGroundBossBodyTest;
    friend class FLDDemoCoreTest;
    friend class FLDReconnectProtectionTest;
    friend class FLDCaptureCombatInterruptTest;
    friend class FLDPlaytestLogHitVampTest;
    friend class FLDRewardSettlementTest;
    friend class FLDCombatOrderRulesTest;
    friend class FLDCombatRecoveryTest;
    friend class FLDCombatDormRecoveryTest;
    friend class FLDDemoDevTestCommandsTest;
    friend class FLDDemoAssistSettlementTest;
    friend class FLDDemoGrowthBuildCommandsTest;
    friend struct FLDBuildPrimitivesAccess;

    // ---- Body lifecycle (core-called, RespawnsAndWorld permit).
    bool PrepareContenderBody(ALDCharacter& Body,const FLDVictimKey& Victim,
        const FLDGameplayWritePermit& Permit,FLDBodyCombatState& Out,FString& Error) const;
    // #97: CampStage scales the monster's base values for this whole life (Development/LDCampGrowth.h); 0 = base.
    bool PrepareCampMemberBody(ALDCharacter& Body,const FLDVictimKey& Victim,FName MonsterId,uint8 CampStage,
        const FLDGameplayWritePermit& Permit,FLDBodyCombatState& Out,FString& Error) const;
    bool InstallCombatBody(ALDCharacter& Body,FLDBodyCombatState&& Prepared,
        const FLDGameplayWritePermit& Permit,FLDCombatBodyRef& Out,FString& Error);
    void ReserveInitialBodyBatch(int32 Count) { Bodies.Reserve(Bodies.Num()+Count); }
    void RemoveRegisteredBody(const FLDCombatBodyRef& Ref);
    // Leave: cancel every unsettled effect owned by the entity; committed facts stay.
    void CancelEntityEffects(const FGuid& Entity);
    // #198: ends every control, mark, burn, breach, shield, taunt, empower, temporary term and the DR chain of one body.
    bool ClearBodyForPhase(FLDBodyCombatState& Body, ALDCharacter* Actor, int64 NowMs, FString& Error);

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
    // The landing half of ReleaseBasic, once BasicReleaseGate passed: builds and queues the swing's hits on its committed
    // target and finishes the action.
    bool LandBasic(ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDCombatAction& Action, FString& Error);
    bool ReleaseAbility(ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDCombatAction& Action,
        ELDCombatReject& Reject, FString& Error);
    // The one hit payload a hostile release snapshots for every target of it (damage terms, source AD/AP, controls,
    // keyword and element payload, amplification and the mods' package multiplier), before its delivery.
    bool BuildReleasePayload(const FLDBodyCombatState& Body, const FLDCombatSourceRef& Source, const FLDCombatActiveTerms& Terms,
        const FVector& ReleaseFoot, const FGuid& EffectId, uint64 Token, int64 IdleAmpBp,
        const TSharedPtr<FLDCombatCastLedger>& CastLedger, FLDCombatHit& Out, FString& Error) const;
    // The standard deliveries of a released payload that StartSkillDelivery did not take: a projectile, a unit hit (with
    // M01's cone and S10's chain), an S09 line or every body of the self area named at release.
    bool DeliverReleasePayload(const ALDCharacter& Actor, const FLDBodyCombatState& Body, const FLDCombatActiveTerms& Terms,
        const FLDCombatAction& Action, const FLDCombatHit& Template, const FLDCombatBodyRef& UnitTarget, const FVector& TargetFoot,
        const FVector& ReleaseFoot, const TArray<FLDCombatBodyRef>& AreaTargets, int64 NowMs, FString& Error);
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
    // Every queued hit of this Effects boundary in order (and what those queue), then the C01 windows that closed by NowMs:
    // a window settles only after every fact up to its boundary was committed (spec §13.3).
    bool ApplyPendingHits(int64 NowMs, FString& Error);
    // M01's copy of a native single-target hit for another body: its damage only; the primary keeps every control, slow,
    // push, keyword stack, element and grievous of the original (spec §12.1).
    static FLDCombatHit SpreadCopy(const FLDCombatHit& Primary, const FLDCombatBodyRef& Other);
    bool AdvanceProjectiles(FString& Error);
    // The root and slow cleanses of one released value set on its caster; true when slows (a temporary term) ended.
    bool ApplyCleanses(FLDBodyCombatState& Body, const FLDDemoAbilityValues& Values, int64 NowMs);
    // The victim's thorns reflections of one committed hit (B03: an enemy primary basic with positive damage, the holder
    // alive, the attacker within reach and observed with clear sight).
    bool QueueThorns(const FLDCombatHit& Hit, ALDCharacter& Holder, FLDBodyCombatState& Body, int64 DamageP2, bool bCritical, FString& Error);
    void ExpireStatuses(int64 NowMs);
    // Ends taunts whose taunter died, left or is no longer observed by the victim (Actions, before any intent).
    bool RefreshTaunts(FString& Error);
    // Taunt release (LDCombatControl.cpp): every eligible contender in the radius is taunted by this body.
    // Landed (optional) receives every body the taunt actually landed on (M20 pulls them).
    bool ApplyTauntEffect(ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDDemoEffectDefinition& Effect,
        const FGuid& EffectId, const FLDCombatSourceRef& Source, FString& Error, TArray<FLDCombatBodyRef>* Landed=nullptr);
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
    // excluding protected contenders and Excluded; sorted by entity. Shared by every area delivery.
    bool CollectAreaTargets(const FVector& Anchor, double RadiusCm, const FLDBodyCombatState* Excluded,
        const FGuid& SourceEntity, TArray<FLDCombatBodyRef>& Out, FString& Error) const;
    // The single assist contribution entry (LDAssist): CreditOwner, another contender, just hurt this contender life with
    // positive damage (shield-absorbed part included) or applied any control or debuff to it. Every damage, control and
    // debuff path calls it once per committed contact; camp bodies and unowned or self contacts are ignored.
    // Returns whether a contribution was recorded or refreshed.
    bool RecordAssistContribution(FLDBodyCombatState& Victim, const FGuid& CreditOwner, int64 NowMs);
    // The lone resolver of the trigger effects a body holds: every installed component's effects of Op
    // (FLDAttributeProjection::InstalledComponents, catalog order of each component's effects), fixed amounts grown for the
    // holder's level (LDSkillGrowth::GrowEffect). False with Error when a listed component or its growth is missing.
    bool CollectPassiveEffects(const FLDAttributeProjection& Projection, ELDDemoEffectOp Op, TArray<FLDPassiveEffectRef>& Out,
        FString& Error) const;
    // Mana grant of a triggered effect; marks the owner view.
    void GrantMana(FLDBodyCombatState& Body, int64 AmountP2);
    // Cast range of an active for this body now: the authored range and its share of the extra basic-attack range.
    bool CastRangeCm(const FLDBodyCombatState& Body, const FLDDemoTargetDefinition& Target, double& OutCm, FString& Error) const;
    // Hooks of one landing primary basic before it is queued: cooldown refunds and the B06 counter (every Count-th basic
    // carries a knockback of the primary target away from the holder, no damage).
    bool OnBasicLanded(FLDBodyCombatState& Body, FLDCombatHit& Primary, FString& Error);
    // #198 B: one component's element on Hit (B10/B11 basic_element, B18 active_element). Fire adds one burn layer
    // credited to Component; water and electric name Component as the hit's ComponentWetSource / ComponentElectricSource,
    // and ApplyElements applies them as that component's own application with their reactions.
    bool ApplyComponentElement(FLDCombatHit& Hit, ELDDemoElement Element, const FLDCombatSourceRef& Component, FString& Error) const;
    // #198 B02: one entry of every ledger Source holds on Target for an effective basic or skill damage hit; a full batch
    // queues its cash-out (a magic skill-class hit flagged bLedgerCashOut).
    bool RecordLedgerEntries(FLDBodyCombatState& Target, const FLDCombatHit& Hit, const FLDBodyCombatState& Source,
        const TArray<FLDPassiveEffectRef>& Ledgers, int64 NowMs, FString& Error);
    // #198 B17: the idle amplification a release of the learned active on Track takes now (0 when none is held or the active
    // has not stood ready for the effect's DelayMs).
    bool IdleReadyAmpBp(const FLDBodyCombatState& Body, const FLDCombatTrack& Track, int64 NowMs, int64& OutBp, FString& Error) const;
    // B15: the amplification a release of Source takes now (a learned active other than the body's previous Q/W/E
    // release, within the effect's DurationMs of it; 0 otherwise).
    bool AlternateAmpBp(const FLDBodyCombatState& Body, const FLDCombatSourceRef& Source, int64 NowMs, int64& OutBp, FString& Error) const;
    // Records a paid release of Source as the body's previous Q/W/E release (the innate dash is none).
    static void NoteActiveRelease(FLDBodyCombatState& Body, const FLDCombatSourceRef& Source, int64 NowMs);
    // #198 B12: voluntary dashes that ended by NowMs; one that completed with a real displacement arms every held
    // DashComplete empower_basic (values snapshotted now).
    bool ResolveDashCompletions(int64 NowMs, FString& Error);
    // A pending empowered-basic package of Effect from Body now (base + AD/AP shares); physical only.
    bool MakeEmpower(const FLDBodyCombatState& Body, const FLDCombatSourceRef& Source, const FLDDemoEffectDefinition& Effect,
        const FGuid& EffectId, int64 NowMs, FLDCombatEmpower& Out, FString& Error) const;
    // Recomputes the effective projection from BuildProjection when the active temporary terms changed.
    bool RefreshTemporary(FLDBodyCombatState& Body, ALDCharacter* Actor, int64 NowMs, FString& Error);
    // Ends lapsed slows and timed bonuses of every body and refreshes their effective projections.
    bool RefreshAllTemporary(int64 NowMs, FString& Error);
    // Installs a new build/monster projection on a body and reapplies its temporary terms.
    bool InstallBodyProjection(FLDBodyCombatState& Body, ALDCharacter* Actor, const FLDAttributeProjection& Projection,
        int64 NowMs, FString& Error);

    // ---- #112 C2 mechanics (LDCombatMechanicsDirector.cpp).
    // A one-time heal of Body (lifesteal, C11, charms); regeneration never passes here. A live S21 grievous cuts it first. #198 F:
    // M12 debts are repaid first, the rest heals; HealSource (when set) feeds the C08 installed on it with the real heal plus
    // repayment. True when HP or a debt changed; OutApplied (optional) is healed plus repaid. RootEffectId (when set) is the
    // action whose budgeted heal (C11) fed that store: the C08 counter it fires keeps it, so C11 cannot reopen that budget.
    bool HealBody(FLDBodyCombatState& Body, int64 AmountP2, const FLDCombatSourceRef& HealSource=FLDCombatSourceRef(),
        int64* OutApplied=nullptr, const FGuid& RootEffectId=FGuid());
    // The single shield entry: a live S21 grievous cuts the amount, then it applies ReplaceIfNotWeaker; a granted shield
    // records its amount (ShieldGrantedP2) and its granting source (#198 F C10).
    bool GrantShieldTo(FLDBodyCombatState& Body, int64 AmountP2, int64 DurationMs, const FGuid& EffectId,
        const FGuid& SourceEntity, int64 NowMs, FString& Error, const FLDCombatSourceRef& GrantSource=FLDCombatSourceRef());
    // M17: every legal enemy within the shove's radius of HolderFoot is pushed away from it (a knockback of Host).
    bool ShoveAround(const FLDBodyCombatState& Holder, const FVector& HolderFoot, const FLDCombatSourceRef& Host,
        const FLDPassiveEffectRef& Shove, FString& Error);
    // C17: a completed dash of a host moves the body's most recent live untethered zone or armed trap to Foot (once).
    bool RelocateArea(FLDBodyCombatState& Body, const FLDCombatSourceRef& Host, const FVector& Foot, int64 NowMs, FString& Error);
    // C18: settle the watched host shield once it is gone or replaced (StoreForce: a share of what it absorbed, or of
    // an S16 record, replacing the old store).
    bool SettleStoredForce(FLDBodyCombatState& Body, int64 NowMs, FString& Error);
    bool StoreForce(FLDBodyCombatState& Body, const FLDPassiveEffectRef& Ref, int64 RecordedP2, int64 NowMs, FString& Error);
    // C18/C19/C21 on a native direct hit of Hit.Source: the store or another host's package spent on the target, and
    // the host's detonation of the zone the target stands in.
    bool SpendOnNativeHit(FLDBodyCombatState& Holder, const FLDCombatHit& Hit, int64 NowMs, FString& Error);
    bool DetonateZone(FLDBodyCombatState& Holder, const FLDCombatHit& Hit, int64 NowMs, FString& Error);
    // C20: the live wide token of another host scales this release's area terms (consumed by the release).
    bool WidenTerms(FLDBodyCombatState& Body, const FLDCombatSourceRef& Source, FLDCombatActiveTerms& Terms, int64 NowMs,
        bool& bWidened, FString& Error) const;
    // A shield emptied by Hit: item_privacy_screen's retort to the breaker.
    bool OnShieldBroken(FLDBodyCombatState& Owner, const FLDCombatHit& Hit, int64 GrantedP2, int64 NowMs, FString& Error);
    // The forced displacement of one committed knockback (control kind Knockback): server root motion along the ground
    // that stops at the last legal position, every voluntary action blocked while it lasts, the unreleased action
    // abandoned and the current order dropped (KnockbackSerial).
    bool ApplyKnockback(ALDCharacter& Target, FLDBodyCombatState& Body, const FLDCombatHit& Hit, int64 NowMs, bool& bApplied,
        FString& Error);
    // Zones due at NowMs (Effects, before the hits).
    bool AdvanceMechanics(int64 NowMs, FString& Error);
    // S20 zone_haste: the zone owner and its current-match teammates (same isolation group and PK side) hold the zone's
    // move speed while their foot stands inside it; leaving, dying or the zone ending drops it.
    void UpdateZoneHaste(const FLDCombatZone& Zone, int64 NowMs);
    bool ExpireMechanics(FLDBodyCombatState& Body, int64 NowMs);
    int64 NextMechanicsDueMs(int64 NowMs) const;
    // Status cue of the #112 records: knockback, for every observer.
    void AppendMechanicsStatuses(const FLDBodyCombatState& Body, int64 NowMs, TArray<FLDObservedStatus>& Out) const;
    // The first trigger effect Op the body holds (CollectPassiveEffects), unset when none; false with Error on a resolver
    // failure.
    bool FindTrigger(const FLDAttributeProjection& Projection, ELDDemoEffectOp Op, TOptional<FLDPassiveEffectRef>& Out,
        FString& Error) const;
    // The registered actor of one body state, or null.
    ALDCharacter* ActorOf(const FLDBodyCombatState& Body) const;
    // ---- #147 build v7 keywords (LDCombatKeywordsDirector.cpp).
    bool KeywordTerms(FLDKeywordTerms& Out, FString& Error) const;
    // The level a body's G(L) reads: a contender's build level; camp bodies use level 1.
    int32 GrowthLevelOf(const FLDBodyCombatState& Body) const;
    // Shared breach of one committed hit on Target: the hit's own stacks and its living source's held breach triggers
    // (basic_stacks on a primary basic, B08). Fire (hit layers, component layers) is an element (ApplyElements).
    bool ApplyKeywordStacks(FLDBodyCombatState& Target, const FLDCombatHit& Hit, FLDBodyCombatState* Source, int64 NowMs, FString& Error);
    // Burn ticks due by now: every layer on its own timer, one damage hit per applier, source and tick instant.
    bool AdvanceBurns(int64 NowMs, FString& Error);
    // The damage-triggered shield of Body after it took hostile positive damage (B09: the rest starts only on a granted shield).
    bool OnDamaged(FLDBodyCombatState& Body, int64 NowMs, FString& Error);
    // B13 and B16 on a hostile native direct hit of Source that dealt positive damage (IsNativeDirectHit): the release's
    // mana refund (once per release EffectId) and the ability-hit shield (its rest starts only on a granted shield).
    bool OnNativeDirectHit(FLDBodyCombatState& Source, const FLDCombatHit& Hit, int64 NowMs, FString& Error);
    // Statuses of the keyword records (silence, breach x stacks, burn x stacks) for every observer.
    void AppendKeywordStatuses(const FLDBodyCombatState& Body, int64 NowMs, TArray<FLDObservedStatus>& Out) const;
    // ---- #198 elements, reactions and the element actives (LDCombatElementsDirector.cpp).
    bool ElementTerms(FLDElementTerms& Out, FString& Error) const;
    // The applier snapshot of a hit's elements (fire damage, reaction damage, discharges): Source's level and AP now.
    bool StampElementSnapshot(const FLDBodyCombatState& Source, FLDCombatHit& InOut, FString& Error) const;
    ELDCombatElement ElementOfBody(const FLDBodyCombatState& Body, int64 NowMs) const;
    // The element batch of one committed hit on a controllable Target: fire (the hit's layers and its living source's held
    // burn triggers), then water, then electric, each applying, refreshing or reacting with the element held.
    bool ApplyElements(FLDBodyCombatState& Target, const FLDCombatHit& Hit, FLDBodyCombatState* Source, int64 NowMs, FString& Error);
    // One element on Target: installs or refreshes it, or reacts with a different one held (both consumed).
    bool ApplyElement(FLDBodyCombatState& Target, ELDCombatElement Kind, const FLDElementApplier& Applier, int32 FireStacks,
        const FGuid& HitId, int64 NowMs, FString& Error);
    // Ends Body's water or electric mark now, with the wet slow it holds.
    void ClearMark(FLDBodyCombatState& Body);
    // An electrified Target that just took positive damage discharges R to the nearest other enemy of its applier.
    bool TryDischarge(FLDBodyCombatState& Target, const FVector& TargetFoot, int64 NowMs, FString& Error);
    // Fog pulses, chain bounces, S16 ends and lapsed marks due by now.
    bool AdvanceElements(int64 NowMs, FString& Error);
    // One fog pulse now: wet on every element-free enemy of its owner inside it.
    bool PulseFog(const FLDCombatFog& Fog, int64 NowMs, FString& Error);
    int64 NextElementsDueMs(int64 NowMs) const;
    // The single report of a completed reaction (AI spec §8); later connectors read ReactionFacts of this Effects step.
    void OnReactionCompleted(const FLDCombatReactionFact& Fact);
    // S16: Body's new stance (a newer one replaces the old record without its answer).
    bool ReleaseBrace(FLDBodyCombatState& Body, const FLDDemoEffectDefinition& Effect, const FLDCombatSourceRef& Source,
        const FGuid& EffectId, uint64 Token, int64 NowMs, FString& Error, TSharedPtr<FLDCombatCastLedger> CastLedger=nullptr);
    // S10: the bounces after the first struck body.
    bool StartChain(const FLDCombatHit& Payload, const FLDCombatBodyRef& First, const FVector& FirstFoot,
        const FLDDemoEffectDefinition& Bounce, int64 NowMs, FString& Error);
    // S09: every legal body whose foot lies in the band LengthCm x 2 HalfWidthCm from Origin along Direction with clear
    // sight from Origin, with its foot.
    bool CollectLineTargets(const FVector& Origin, const FVector& Direction, double LengthCm, double HalfWidthCm,
        const FLDBodyCombatState& Caster, TArray<TPair<FLDCombatBodyRef,FVector>>& Out, FString& Error) const;
    // Wet, electrified, braced and soured cues for every observer.
    void AppendElementStatuses(const FLDBodyCombatState& Body, int64 NowMs, TArray<FLDObservedStatus>& Out) const;
    // The release-time zone of a #112 active (EFFECT zone); #198 F M07 compresses it into one delayed blast (Terms).
    bool ReleaseMechanics(FLDBodyCombatState& Body, const FLDCombatSourceRef& Source, const FLDCombatActiveTerms& Terms,
        const FVector& OriginFoot, const FVector& TargetPoint, const FVector& ReleaseDirection, const FGuid& EffectId, uint64 Token, int64 IdleAmpBp,
        TSharedPtr<FLDCombatCastLedger> CastLedger, FString& Error);
    // Hit extras of a #112 value set (knockback, attack slow).
    void FillMechanicsTemplate(const FLDDemoAbilityValues& Values, const FVector& OriginFoot, FLDCombatHit& Template) const;
    bool AdvanceSkillContent(int64 NowMs, FString& Error);
    bool ResolveDashContacts(int64 NowMs, FString& Error);
    bool OnDashContact(FLDBodyCombatState& Body, FLDCombatDashContact& Dash, int64 NowMs, FString& Error);
    // Ends the body's channelled cone (a new action, a death or a source change interrupts it).
    void CancelChannel(FLDBodyCombatState& Body);
    bool StartSkillDelivery(FLDBodyCombatState& Body, const FLDCombatActiveTerms& Terms, const FLDCombatAction& Action,
        FLDCombatHit& Payload, int64 NowMs, bool& bHandled, FString& Error);

    // ---- #198 F mods and connectors (LDCombatWorkshop.cpp).
    // The installed mods/connectors of Op that sit on Exit (FLDPassiveEffectRef::Targets).
    bool WorkshopOn(const FLDAttributeProjection& Projection, ELDDemoEffectOp Op, const FLDCombatSourceRef& Exit,
        TArray<FLDPassiveEffectRef>& Out, FString& Error) const;
    // Compiles one learned active's (or the innate D's) terms with the mods installed on it.
    bool CompileActive(const FLDAttributeProjection& Projection, const FLDCombatSourceRef& Source, FLDCombatActiveTerms& InOut,
        FString& Error) const;
    // M01 on a Unit active: the other legal bodies inside the cone toward the primary target take a damage-only copy.
    bool SpreadUnitHits(const FLDBodyCombatState& Body, const FLDCombatActiveTerms& Terms, const FVector& Origin,
        const FVector& PrimaryFoot, const FLDCombatBodyRef& Primary, const FLDCombatHit& Template, FString& Error);
    // C06: a natural completion of Exit (zone end, trap detonation, impact landing, compressed blast) at Centre/Radius.
    bool TriggerAftershock(const FLDCombatBodyRef& Owner, const FLDCombatSourceRef& Exit, const FGuid& EffectId,
        const FVector& Centre, double RadiusCm, int64 NowMs, FString& Error);
    // M15: the release's echo blast (a derived host hit) at Centre after the mod's delay, at its share of Blast's payload;
    // Blast gives the outline. No echo when the holder is gone or about to die.
    bool ScheduleEcho(const FLDCombatBodyRef& Owner, const FLDPassiveEffectRef& Echo, const FLDCombatGroundEffect& Blast,
        int64 NowMs, FString& Error);
    // M13: a return flight from where Outbound ended toward its living thrower's foot now.
    bool LaunchReturn(const FLDCombatProjectile& Outbound, const FVector& EndCm, const FGuid& EndBody, FString& Error);
    // C09: a completed reaction whose trigger source carries the connector arms one empowered-basic package of it.
    bool ArmReactionEmpower(const FLDCombatReactionFact& Fact, int64 NowMs, FString& Error);
    // C15: a completed reaction triggered by a host active cuts that active's remaining cooldown, once per release.
    bool ReactionHaste(const FLDCombatReactionFact& Fact, int64 NowMs, FString& Error);
    // C13: the host release's first native direct hit or the hosted dash's first contact cuts every other learned
    // active's remaining cooldown, once per release EffectId.
    bool HasteOtherActives(FLDBodyCombatState& Holder, const FLDCombatSourceRef& Host, const FGuid& EffectId, int64 NowMs,
        FString& Error);
    // C14/C16 on a host's native direct hit: set the setup mark; count a distinct enemy for the release's mana.
    bool OnHostDirectHit(FLDBodyCombatState& Holder, const FLDCombatHit& Hit, int64 NowMs, FString& Error);
    // C14: the bonus of the holder's setup marks on Target that a different active's native direct hit takes now.
    bool SetupMarkBonus(FLDBodyCombatState& Holder, const FLDCombatHit& Hit, int64 NowMs, int64& OutBp,
        TArray<FLDCombatSetupMark>& OutTaken, FString& Error) const;
    // C05/C07 after a hard control actually landed on Victim (CommitControl).
    bool OnHardControl(FLDBodyCombatState& Victim, const FLDControlApplication& App, int64 NowMs, FString& Error);
    // C02: arms the knockback watch of a committed push when its source holds C02 on the push's exit.
    bool WatchKnockbackImpact(ALDCharacter& Target, FLDBodyCombatState& Body, const FLDCombatHit& Hit,
        const FVector& Direction, int64 NowMs, FString& Error);
    // C02: one watched push's first block: a static blocker stuns and hurts the pushed body; another legal enemy body takes the
    // same package as the pushed one; anything else (the holder, a protected or other-group body) answers nothing.
    bool ResolveKnockbackImpact(FLDBodyCombatState& Body, const FLDCombatKnockbackImpact& Impact, bool bBody,
        const ALDCharacter* BlockerActor, int64 NowMs, FString& Error);
    // The committed-hit hooks of #198 F (after resources and controls of ApplyHit): C01 fire sums, C08 attacker memory,
    // M11 marks, C11 heals, C12 repeats, M12 debts and C10 bursts.
    bool ApplyWorkshopHit(const FLDCombatHit& Hit, FLDBodyCombatState& Target, FLDBodyCombatState* Source,
        const FLDDamageEvaluation& Damage, bool bShieldBroken, int64 ShieldGrantedP2, int64 NowMs, FString& Error);
    // An automatic single counter (B03 thorns, C08) of Holder through its exit's mods (M01 area, M02 delayed blast,
    // M05 reach, M11 marked target): Default is the original target (may be unset for C08, which chooses its own).
    // RootEffectId (may be unset: a new effect) is the effect the counter continues (C08 fed by a C11 heal, spec §13.3).
    bool DeliverCounter(FLDBodyCombatState& Holder, const FLDCombatSourceRef& Exit, const FLDCombatBodyRef& Default,
        int64 BaseP2, ELDDemoDamageType DamageType, double ReachCm, const FGuid& RootEffectId, int64 NowMs, bool& bDelivered,
        FString& Error);
    // A full B02 batch of Owner on Target: M01 spreads it, M02 locks it; otherwise one hit now.
    bool CashOutLedger(const FLDBodyCombatState& Owner, const FLDCombatBodyRef& Target, const FLDCombatSourceRef& Source,
        int64 AmountP2, ELDDemoDamageType DamageType, int64 NowMs, FString& Error);
    // Debts, C08 stores, locked ledgers and C02 watches due by NowMs (Effects).
    bool AdvanceWorkshop(int64 NowMs, FString& Error);
    bool FinalModOf(const FGuid& Entity, const FLDDemoFinalModDefinition*& Out, FString& Error) const;
    bool FinalModTargetsSource(const FGuid& Entity, const FLDCombatSourceRef& Source) const;
    bool RewriteFinalHit(FLDCombatHit& Hit, bool bProtected, int64 NowMs, FString& Error);
    bool OnFinalHit(const FLDCombatHit& Hit, FLDBodyCombatState& Target, FLDBodyCombatState* Source,
        bool bEffective, bool bControlled, int64 NowMs, FString& Error);
    bool FinalControlDuration(const FLDControlApplication& App, int64& DurationMs, FString& Error) const;
    bool WatchFinalControlExit(const FLDBodyCombatState& Target, const FLDControlApplication& App,
        int64 ExpiresAtMs, FString& Error);
    bool FinalCooldownCost(const FLDCombatEntityRecord& Record, const FLDCombatSourceRef& Source,
        int64& InOutCooldownMs, FString& Error) const;
    bool EmitZonePulse(const FLDCombatZone& Zone, FString& Error);
    bool SpawnFinalZone(FLDBodyCombatState& Holder, const FLDDemoFinalModDefinition& Card,
        const FVector& Centre, bool bTether, int64 NowMs, FString& Error);
    bool MakeFinalPush(const FLDCombatBodyRef& Holder, const FLDCombatBodyRef& Target,
        FName CardId, const FVector& Anchor, double DistanceCm, FLDCombatHit& Out, FString& Error) const;
    bool SplitFinalZone(FLDCombatZone& Zone, const FVector& ReleaseDirection, int64 NowMs, bool& bHandled, FString& Error);
    bool ResolveFinalBowlingImpact(const FLDCombatKnockbackImpact& Impact, const ALDCharacter* Blocker,
        const FVector& Direction, int64 NowMs, FString& Error);
    bool AdvanceFinalMods(int64 NowMs, FString& Error);
    int64 NextFinalModDueMs(int64 NowMs) const;
    TArray<FLDFinalControlExit> FinalControlExits;
    TArray<FLDFinalTrafficTrail> FinalTraffic;
    TArray<FLDFinalMotionFrame> FinalMotion;
    // C01 windows whose fixed boundary is at or before NowMs grant their candidate shield (ApplyPendingHits, after the hits).
    bool SettleFireWindows(int64 NowMs, FString& Error);
    // C01 install bookkeeping of one build change from Previous to Record.Projection: its first install registers the fixed
    // window boundaries; an uninstall or a changed binding discards Body's unsettled window.
    bool NoteWorkshopInstall(FLDCombatEntityRecord& Record, const FLDAttributeProjection& Previous, FLDBodyCombatState* Body,
        int64 NowMs, FString& Error);
    int64 NextWorkshopDueMs(int64 NowMs) const;

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
        const FLDDemoAbilityDefinition*& OutAbility) const;
    // The executable active of a resolved entry: a learned active's values grown for its owner's level (#198), or the
    // innate dash, compiled with the record's installed mods (#198 F, LDCombatWorkshop.cpp); false without one.
    bool ActiveOf(const FLDCombatEntityRecord& Record, const FLDCombatSourceRef& Source, const FLDDemoAbilityDefinition* Ability,
        FLDCombatActiveTerms& Out) const;
    // D19 innate dash terms of the approved character.
    bool InnateDashFor(FLDInnateDashTerms& Out, FString& Error) const;
    bool PlanBindings(const FLDCombatEntityRecord& Record, const FLDBuildProjectionInput& Input, int64 NowMs,
        TArray<FLDCombatBindingPlan>& Out, FString& Error) const;
    FLDCombatEntryView EntryView(const FLDCombatEntityRecord& Record, const FLDBodyCombatState* Body,
        ELDCombatEntry Entry, int64 NowMs) const;
    void MarkViewChanged(const FGuid& Entity);

    // ---- #198 §10 build blueprint ledger (LDCombatBlueprintLedger.cpp). Each fact is recorded once where Combat commits it.
    // Private training (a rest life and its own dummy, §16.3) is no match fact and records nothing.
    // The ledger version of Contender's installed build now; INDEX_NONE for an entity without a contender record.
    int32 LedgerVersion(const FGuid& Contender);
    // Death recap and per-source damage totals (LDCombatBlueprintLedger.cpp): one committed positive hit on another body.
    // Private training records nothing, like the ledger.
    void RecapHitCommitted(const FLDCombatHit& Hit, FLDBodyCombatState& Victim, const FLDDamageEvaluation& Damage, int64 NowMs);
    // Right after ApplyBuildChange installed the contender's new build; Previous is the build it replaced.
    void LedgerBuildInstalled(const FGuid& Contender, const FLDBuildProjectionInput& Previous);
    // A contender body released an effect (a landed basic swing or an ability release): its hits belong to this version.
    // Counted is the release fact it adds one to (BasicAttacks or AbilityCasts), Count for none (the innate dash).
    void LedgerEffectReleased(const FLDBodyCombatState& Body, const FGuid& EffectId, ELDBlueprintFact Counted);
    // One committed hit on Victim: damage dealt once to the credited contender (the version that created the effect),
    // damage taken, own shield absorption, HP lost and what the victim's own cuts (S16 stance, primary guard) took off,
    // to the victim contender's current version.
    void LedgerHitCommitted(const FLDCombatHit& Hit, const FLDBodyCombatState& Victim, const FLDDamageEvaluation& Damage);
    // HP a one-time heal actually restored to a contender body, and a shield granted to it.
    void LedgerHealed(const FLDBodyCombatState& Body, int64 HealedP2);
    void LedgerShieldGranted(const FLDBodyCombatState& Body, int64 AmountP2);
    // M12 debt a heal actually repaid (never counted as healing), and HP natural regeneration actually restored.
    void LedgerDebtRepaid(const FLDBodyCombatState& Body, int64 RepaidP2);
    void LedgerRegenerated(const FLDBodyCombatState& Body, int64 RestoredP2);
    // One proc of an installed component of Holder that committed a real result (a hit, heal, shield, mana, status, cut or
    // change of an outcome); never a passive stat term, a candidate or a refused proc.
    void LedgerComponentTriggered(const FLDBodyCombatState& Holder, FName Component);
    // One own combat action request of Entity that Combat refused for a gameplay gate (busy, cooldown, attack cadence,
    // mana, control, target, direction, range or sight); protocol, staleness and phase refusals are not counted.
    void LedgerActionRefused(const FGuid& Entity, ELDCombatReject Reason);

    TMap<TWeakObjectPtr<ALDCharacter>,FLDBodyCombatState> Bodies;
    TMap<FGuid,FLDCombatEntityRecord> Records;
    TArray<FLDCombatHit> PendingHits;
    TArray<FLDCombatProjectile> Projectiles;
    TArray<FLDCombatZone> Zones;
    TArray<FLDCombatContactMotion> ContactMotion;
    TArray<FLDCombatChannel> Channels;
    TArray<FLDCombatGroundEffect> GroundEffects;
    // C17: creation order of zones and traps (most recent first when one is relocated).
    uint64 NextAreaSerial=0;
    // #198 F M02: locked ledger batches waiting for their delayed cash-out.
    TArray<FLDCombatLockedLedger> LockedLedgers;
    // #198 elements: fogs (one per owner), S10 chains and the reactions completed in the current Effects step.
    TArray<FLDCombatFog> Fogs;
    TArray<FLDCombatChain> Chains;
    TArray<FLDCombatReactionFact> ReactionFacts;
    TArray<FLDCommittedDeath> PendingDeaths;
    // Committed positive contacts against camp members, delivered to the camp owner within the same Effects.
    TArray<FLDCombatHostileContact> PendingContacts;
    TArray<FLDCommittedDeath> RetryRewards;
    FRandomStream CritRandom;
    bool bAdvancing=false;
    FLDBuildLedger BuildLedger;
    // Per contender: the match total of committed damage credited to it, one row per source.
    TMap<FGuid,TArray<FLDDamageLine>> DamageTotals;
};
