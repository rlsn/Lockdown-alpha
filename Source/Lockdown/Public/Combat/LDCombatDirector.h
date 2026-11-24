#pragma once
#include "CoreMinimal.h"
#include "Combat/LDDashContactRules.h"
#include "GameFramework/Actor.h"
#include "Combat/LDCombatEvaluationTypes.h"
#include "Combat/LDAttributeProjection.h"
#include "Combat/LDAssistRules.h"
#include "Combat/LDCombatTypes.h"
#include "Combat/LDCombatPreInput.h"
#include "Combat/LDDamageRecap.h"
#include "Combat/LDChainRules.h"
#include "Combat/LDCombatControlRules.h"
#include "Combat/LDCombatMechanics.h"
#include "Combat/LDMonsterRules.h"
#include "Combat/LDCombatKeywords.h"
#include "Combat/LDCombatElements.h"
#include "Combat/LDMomentum.h"
#include "Development/LDBuildBlueprint.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Vision/LDViewTypes.h"
#include "LDCombatDirector.generated.h"

struct FLDDormZones;
namespace LDAirdrop { struct FCandidate; }

struct FLDAirdropRewardFact
{
    int64 HpBeforeP2 = 0;
    int64 HpAfterP2 = 0;
    int64 MomentumBeforeP2 = 0;
    int64 MomentumAfterP2 = 0;
    int64 MoveSpeedBonusUntilMs = 0;
};

// Which momentum book a change goes to (the LD_MOMENTUM line of a life or rest, LDMomentum::FLedger).
enum class ELDMomentumBook : uint8 { Basic, Combat, Skill, Reaction, Dodge, Other, Idle, Spent };

// An owner holding a Q/W/E with a full bar (overdrive charge): public while it lasts, ended by the release, a
// let-go or abandon of the key (bCharging false), a bar below the threshold, death or a stun, silence or knockback.
struct FLDCombatOverdriveCharge
{
    ELDCombatEntry Entry=ELDCombatEntry::None;
    int64 StartMs=0;
};

class ALDCharacter;
class ALDGameMode;
class ILDLifeAuthority;
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

// What one release shares across all of its hits: the brains-keystone refund pays once per release; PaidMomentumP2 is the
// momentum this release really paid (after K08; a whole bar for an overdrive). Factors are the damage factors the release
// locked (K09/K12/K16/K17, Y22), carried by every native direct hit of it. MomentumStruck holds the bodies whose first native
// direct hit of this release already gained MOMENTUM.SkillHit; bOverdrive marks an overdrive release; bAutoRecast marks a
// K06 repeat (it never repeats again).
struct FLDCombatCastLedger
{
    bool bOverdrive=false;
    TArray<FLDCombatBodyRef> MomentumStruck;
    bool bBrainsKeystoneRefunded=false;
    int64 PaidMomentumP2=0;
    TArray<FLDDamageFactor> Factors;
    bool bAutoRecast=false;
    // Skill branch riders of this release (issue #287 spec §5.1, LDCombatBranchDirector.cpp): applied by every native
    // direct hit it lands. RiderStruck: the bodies a once-per-target rider already reached; RecastFirst: a recast window's
    // first-release targets (S12 B1 stuns a body both releases hit).
    TArray<FLDDemoEffectDefinition> Riders;
    TArray<FLDCombatBodyRef> RiderStruck;
    TArray<FLDCombatBodyRef> RecastFirst;
    bool bNextBasicGranted=false;
    bool bRecast=false;
};

// A vulnerability a skill branch left on a life: hits of Scope (from OwnerEntity only when it is set) take RatioBp more.
struct FLDCombatVulnerability
{
    FGuid OwnerEntity;
    ELDDemoDamageScope Scope=ELDDemoDamageScope::All;
    int64 RatioBp=0;
    int64 ExpiresAtMs=0;
};

// An open recast window (S02 B, S12 B): the same entry releases once more before UntilMs without cooldown or momentum.
struct FLDCombatRecastWindow
{
    FName Definition;
    int64 UntilMs=0;
    FVector Direction=FVector::ZeroVector;
    FLDDemoEffectDefinition Terms;
    TSharedPtr<FLDCombatCastLedger> FirstLedger;
};

// The skill branch state of one life (LDCombatBranchDirector.cpp).
struct FLDCombatBranchState
{
    TArray<FLDCombatVulnerability> Vulnerabilities;
    // The next primary basic's damage bonus (S01 A1, S06 B2, S16 B) and its window.
    int64 NextBasicAmpBp=0, NextBasicAmpUntilMs=0;
    // A self damage cut (S15 A1) and its window.
    int64 GuardBp=0, GuardUntilMs=0;
    // The next Q/W/E release costs no momentum until then (S05 B2).
    int64 FreeCastUntilMs=0;
    // Primary basics shock or breach until then (S13 A2, S15 A2).
    int64 RiderUntilMs=0;
    bool bRiderElectric=false;
    int32 RiderBreach=0;
    // Every hit on this life discharges until then (S10 B1); the discharge credits OverchargeSource at its snapshot.
    int64 OverchargeUntilMs=0;
    FLDCombatBodyRef OverchargeSource;
    FLDCombatSourceRef OverchargeExit;
    int32 OverchargeLevel=1;
    int64 OverchargeAPP2=0;
    // The shield branch terms (S13 A1, S14 A1/A2) of every standing shield layer, keyed by the layer's effect: each layer
    // reflects, heals when it lapses and bursts when it breaks by its own terms; an entry ends with its layer.
    struct FShieldBranch
    {
        FGuid EffectId;
        int64 ReflectBp=0, EndHealBp=0;
        TOptional<FLDDemoEffectDefinition> BreakBurst;
        FLDCombatSourceRef Exit;
    };
    TArray<FShieldBranch> ShieldBranches;
    FShieldBranch& ShieldBranchFor(const FGuid& EffectId)
    {
        if (FShieldBranch* Known=ShieldBranches.FindByPredicate([&EffectId](const FShieldBranch& Row) { return Row.EffectId==EffectId; }))
        { return *Known; }
        FShieldBranch& Added=ShieldBranches.AddDefaulted_GetRef();
        Added.EffectId=EffectId;
        return Added;
    }
    // The current brace's branch terms (S16), keyed by the brace's effect.
    FGuid BraceEffectId;
    int64 BraceStunMs=0, BraceHealBp=0, BraceStackBp=0, BraceShieldMs=0, BraceShoveMs=0;
    double BraceShoveCm=0;
    // A dash's end burst (S13 B2): placed at the foot where the dash ends, Delay after it.
    TOptional<FLDDemoEffectDefinition> DashEndBurst;
    FLDCombatSourceRef DashEndExit;
    FGuid DashEndEffectId;
    int64 DashEndAtMs=0;
    TSharedPtr<FLDCombatCastLedger> DashEndLedger;
    // The open recast window of this life.
    TOptional<FLDCombatRecastWindow> Recast;
};


enum class ELDCombatActionKind : uint8 { Basic, Ability };

// One card effect a body holds, resolved from the approved catalog (ALDCombatDirector::CollectPassiveEffects): one installed
// copy's effect, its fixed amounts grown for the holder's level. Held by value: the projection it came from may be replaced
// while the caller still reads it. Copies of one card are separate refs (Serial); their numbers add into one damage factor
// (Key) and their triggers fire once each.
struct FLDPassiveEffectRef
{
    FName Key;                      // card id: the damage factor group of its copies
    uint16 Serial=0;                // the installed copy
    FLDCombatSourceRef Source;      // Kind Card, DefinitionId = card id
    FLDDemoEffectDefinition Effect;
    ELDCardTargetKind TargetKind=ELDCardTargetKind::None;
    // Skill: the learned active it sits on; Y25: the chosen element.
    FName Active;
    ELDDemoElement Element=ELDDemoElement::None;
    uint8 Tier=1;

    // Whether this card sits on Exit: a learned skill, the innate D, or the body, which carries the primary basic.
    bool Targets(const FLDCombatSourceRef& Exit) const
    {
        switch (TargetKind)
        {
        case ELDCardTargetKind::Body: return Exit.Kind==ELDCombatSourceKind::Basic;
        case ELDCardTargetKind::Skill: return Exit.Kind==ELDCombatSourceKind::Learned && Exit.DefinitionId==Active;
        case ELDCardTargetKind::Dash: return Exit.Kind==ELDCombatSourceKind::Innate;
        default: return false;
        }
    }
};

// One learned active (or the innate D) as its release compiles it: the authored target and value set with its branch mod terms
// and the skill cards on it (K08/K09 cooldown and cost, K12 health cost, K06 repeats, D05 split dash) applied.
struct FLDCombatActiveTerms
{
    FLDDemoTargetDefinition Target;
    FLDDemoAbilityValues Values;
    int64 WindupMs=0;
    // The learned branch the release snapshot came from (None = rank 1).
    ELDSkillBranch Branch=ELDSkillBranch::None;
    // Native direct damage package multiplier, applied as the hits' PackageScaleBp.
    int64 PackageScaleBp=10000;
    // The innate D's distance and cooldown multipliers; a learned active's are already in Values.
    int64 DashScaleBp=10000;
    int64 CooldownScaleBp=10000;
    // Branch mod terms of this release (pull, return, pierce, echo, tether, finisher, reach) as the shared handlers read them.
    TOptional<FLDPassiveEffectRef> ProjectileReturn, ProjectilePierce, EchoImpact;
    // M16-M20 on this active (their effects already scaled by M03 where they move a body).
    TOptional<FLDPassiveEffectRef> ZoneTether, FinalKnockback, EmpowerReach, Pull;
    TOptional<FLDDemoEffectDefinition> FinalKnockbackTerms, PullTerms;
    // K12: the release pays HPCostBpPerMomentum of max HP per momentum of its cost instead of momentum (copies add).
    int64 HPCostBpPerMomentum=0;
    // Extra burn layers added specifically by Fire Overdrive (merged onto a selected fire medal without copying native skill burn).
    int32 OverdriveBurnStacks=0;
    int32 OverdriveZoneBurnStacks=0;
    // K06: AutoRecasts repeats of this release, each AutoRecastDelayMs after the previous, at AutoRecastScaleBp.
    int32 AutoRecasts=0;
    int64 AutoRecastDelayMs=0, AutoRecastScaleBp=0;
    // D05: the innate dash runs in SplitSegments segments of SplitDistanceCm, the next one within SplitWindowMs.
    int32 SplitSegments=0;
    FLDDemoRational SplitDistanceCm;
    int64 SplitWindowMs=0;
    // D08: the innate dash pays this much momentum (copies add).
    int64 DashMomentumCostP2=0;
    // K07: the multiplier the reshapes put into this release's areas (10000 without one).
    int64 AreaBp=10000;
};

// Y08: a knockback whose source holds Y08 watches for its first real blocking fact (also a branch wall stun).
struct FLDCombatKnockbackImpact
{
    FGuid EffectId;
    FLDCombatBodyRef Holder;
    FGuid CreditOwner;
    FLDPassiveEffectRef Impact;
    bool bCardImpact=false;
    FVector Direction=FVector::ZeroVector;
    FLDCombatSourceRef PushSource;
    int64 HolderADP2=0, HolderAPP2=0;
    int64 EndMs=0;
    // A skill branch's wall stun of this push (static geometry only).
    int64 WallStunMs=0;
};

// What one original action (its EffectId) already took from one installed card copy (K01/K25/D02 once-per-release cuts,
// D01's one refresh).
struct FLDCombatEffectBudget
{
    FGuid EffectId;
    FName Card;
    uint16 Serial=0;
    int64 AtMs=0;
};

// K04: a mark one holder's host skill left on one target life; a different skill's next native direct hit on it takes the
// bonus and consumes it.
struct FLDCombatSetupMark
{
    FLDCombatBodyRef Target;
    FName Card;
    uint16 Serial=0;
    FName Host;
    int64 ExpiresAtMs=0;
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
    bool bMonsterSpecial=false;
    // A contender basic: the combo step its AttackStart took and the attack interval it committed (momentum income).
    uint8 ComboStep=0;
    int64 BasicIntervalMs=0;
    // A Q/W/E admitted with a held overdrive charge; the release decides by the bar (LDMomentum::OverdriveReady).
    bool bOverdrive=false;
    // K06 / A08: a free repeat of a release at AutoRecastScaleBp of its damage (no momentum, no cooldown, no further repeat).
    bool bAutoRecast=false;
    int64 AutoRecastScaleBp=10000;
    // A08: the share scales the whole effect (damage, shields, heals and control durations), not only its damage.
    bool bWholeEffectShare=false;
    // Spec §6.5: the chain a free repeat (K06, A08) continues; unset for a player's own action (it opens one).
    FGuid ChainId;
};

// A K06 repeat of one release, due at DueMs: the same action toward the same target or direction (LDCombatCards.cpp).
struct FLDCombatAutoRecast
{
    FLDCombatBodyRef Body;
    FLDCombatAction Action;
    int64 DueMs=0;
    // The card that queued it (K06 card_encore, A08 card_fist_skill).
    FName Card;
};

// K03: a learned skill's latest native hit on a contender life by one contributor (the assist window decides).
struct FLDSkillContribution
{
    FGuid Contributor;
    FName Skill;
    int64 AtMs=0;
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
        // Momentum lives here beside HP and the shield; its maximum is the fixed MOMENTUM.Max.
        int64 HpP2=0,MaxHpP2=0,MomentumP2=0,ShieldP2=0;
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
    uint8 CampStage=0;                     // #97 CampMember bodies only: growth stage fixed for this life
    ELDCampAffix CampAffix=ELDCampAffix::None; // P6 CampMember bodies only: the affix of this life's camp spawn
    bool bPeddler=false;                   // P6: the second-hand dealer (flees, never attacks)
    int32 LayoffStacks=0;                  // P6 layoff affix: one +LayoffAttack per fallen camp mate
    uint64 StateRevision=1;                // statuses/action changes; resources use Resources.Revision
    TArray<FLDCombatRoot> Roots;
    TArray<FLDCombatSlow> Slows;
    TArray<FLDCombatTimedBonus> TimedBonuses;
    // Hard-control diminishing returns of this life (LDCombatControl::DiminishHardControl).
    FLDHardControlChain HardControl;
    // Disarm and Stun instances (LDCombatControl); each keeps its effect, source and half-open expiry.
    TArray<FLDCombatRoot> Disarms;
    TArray<FLDCombatRoot> Stuns;
    // The pending empowered-basic packages, at most one per source (spec §10.2).
    TArray<FLDCombatEmpower> Empowers;
    // Assist contributions other contenders made to this life (LDAssist); read once when the life locks its death.
    TArray<FLDAssistContribution> AssistContributions;
    // Positive hits this contender life took from other bodies within the recap window (LDDamageRecap); summed into the
    // death fact when the life locks its death.
    TArray<FLDDamageSample> DamageLog;
    TOptional<FLDCombatShield> Shield;
    // Every grant of the stacked shield as its own layer (its amount left and its own deadline). Resources.ShieldP2 is
    // their sum and Shield names the newest grant with the latest deadline; damage spends the earliest-expiring layer first.
    struct FShieldLayer { FGuid EffectId; int64 ExpiresAtMs=0; int64 AmountP2=0; FLDCombatSourceRef Source{}; };
    TArray<FShieldLayer> ShieldLayers;
    TOptional<FLDCombatTaunt> Taunt;       // contenders only
    TOptional<FLDCombatAction> Action;
    int64 LastCombatAtMs=0;
    int64 RegenThroughMs=0;
    // D21 (#79): this contender's foot stood inside a dorm at the last regeneration settle.
    bool bInDorm=false;
    // Sub-P2 remainders in 1e-6 P2; momentum's is signed (positive rising, negative falling, LDMomentum::Settle).
    int64 HpRegenMicroP2=0, MomentumMicroP2=0;
    // The last real combat event of this life for momentum (LastCombatAt without the spawn and rest starts); unset = idle.
    TOptional<int64> MomentumEngagedAtMs;
    // When the current fight began: the first combat event after more than MOMENTUM.IdleDelaySec without one (0 = none yet).
    int64 FightStartedAtMs=0;
    LDMomentum::FLedger MomentumLedger;
    // The combo step the last released basic of this life took (0 = none) and its AttackStart instant.
    uint8 ComboStep=0;
    int64 ComboStartMs=0;
    // The overdrive charge shown to observers while an owner holds a Q/W/E with a full bar (FLDCombatOverdriveCharge).
    TOptional<FLDCombatOverdriveCharge> OverdriveCharge;
    // The last D or dash-counting start of this life (perfect dodge, LDMomentum::IsPerfectDodge).
    TOptional<int64> DashStartedAtMs;
    FVector DashStartFootCm=FVector::ZeroVector;
    // Perfect dodges of this life and the last one's instant (owner notice).
    uint32 PerfectDodges=0;
    int64 LastPerfectDodgeAtMs=0;
    // Innate 0.12s hit-graze perfect dodge: the DashStartedAtMs that already consumed its hit-dodge proc.
    int64 LastDashDodgeStartMs=-1;
    int32 MonsterAttackCount=0;
    bool bMonsterEnraged=false;
    int64 MonsterGuardUntilMs=0;
    int64 MonsterRecoverUntilMs=0;
    int64 MonsterNextAttackAtMs=0;         // contenders keep their cadence on the persistent record
    int64 DashUntilMs=0;
    FLDCombatBodyRef LastAttackTarget;
    // Released action and last landed hit of this life for the visible action cue.
    FLDCombatCueFacts Cues;
    // Source of the last released action (the visible Cast cue's ability once its windup released).
    FLDCombatSourceRef ReleasedSource;
    // The last released action was admitted as an overdrive release (FLDCombatCueFacts::bOverdriveCast).
    bool bReleasedOverdrive=false;
    uint64 ResetThroughEpoch=0;
    bool bPendingDeath=false;
    // #112 C2 mechanics (LDCombatMechanicsDirector.cpp): forced displacement windows (blocking every voluntary action).
    TArray<FLDCombatRoot> Knockbacks;
    // Increments on every knockback that lands; Navigation drops the body's current order when it changes.
    uint64 KnockbackSerial=0;
    // The amount the current shield record was granted with.
    int64 ShieldGrantedP2=0;
    // The amount the newest grant added to the stacked shield (S14 B2 raises only that grant).
    int64 LastShieldGrantP2=0;
    int64 KeystonePhysiqueReadyAtMs=0;
    int64 KeystoneThickSkinReadyAtMs=0;
    // Until then the thick-skin guard keeps the body steady: slows miss and knockbacks keep KeystoneThickSkinKnockbackBp.
    int64 KeystoneSteadyUntilMs=0;
    TMap<FGuid,int64> KeystoneStrengthTargetReadyAtMs;
    FGuid WaveAfterExemptChainId;
    // #147 build v7 keywords (LDCombatKeywords): silence instances (shaped like disarm and stun), the one shared breach
    // and burn of this life and the damage-triggered shield's rest (B09: it starts only on a granted shield).
    TArray<FLDCombatRoot> Silences;
    FLDCombatSharedBreach SharedBreach;
    FLDCombatBurn Burn;
    int64 DamagedShieldReadyAtMs=0;
    TArray<FLDCombatSlow> AttackSlows;
    FGuid ChannelEffectId;
    uint64 ChannelDirectSequence=0;
    int64 ChannelStartedAtMs=0;
    TSharedPtr<FLDCombatDashContact> DashContact;
    // Elements (LDCombatElements): the water, shock, chill or rot mark (fire is Burn), the S16 stance and a shatter (shock +
    // chill): the next skill hit on this life within ShatterUntilMs takes ShatterBp more (its own factor), then it ends.
    FLDCombatElementMark ElementMark;
    // COMBAT.ReactionLockSec: until this authority ms an element that would react with the held one is not applied.
    int64 ReactionLockedUntilMs=0;
    FLDCombatBrace Brace;
    int64 ShatterBp=0, ShatterUntilMs=0;
    // Spec §8.3: the enemy ice ground state this body's foot stood on at the last ground advance (movement inertia and the
    // knockback and dash scale read it); Y23's standing start on its own ground state (0 = not standing on one).
    bool bOnEnemyIce=false;
    int64 TurfSinceMs=0;
    // N2 A2: the last element this life applied to another body; its fans carry it.
    ELDCombatElement LastAppliedElement=ELDCombatElement::None;
    // #198 B12: the voluntary dash in flight (ResolveDashCompletions arms DashComplete empowers when it completes).
    TOptional<FLDCombatVoluntaryDash> VoluntaryDash;
    // Card state of this life (LDCombatCards.cpp): Y08's watched knockback, per-release budgets, K04's marks, A12's focus
    // (target, hits, last hit), Y03's once-per-life stand, Y22's idle start and D05's open second dash segment.
    TOptional<FLDCombatKnockbackImpact> PendingImpact;
    TArray<FLDCombatSetupMark> SetupMarks;
    TArray<FLDCombatEffectBudget> EffectBudgets;
    FLDCombatBodyRef FocusTarget;
    int32 FocusHits=0;
    int64 FocusUntilMs=0;
    bool bLastStandUsed=false;
    int64 LastStandUntilMs=0;
    int64 IdleSinceMs=0;
    int32 SplitDashLeft=0;
    int64 SplitDashUntilMs=0;
    // Issue #292 P5 cards: D04's free next casts, K05's next-skill bonus (its card and window), K18's recent releases (skill and
    // instant), Y24's open tally (marks and the last mark's instant) and K03's skill contributions on this contender life.
    int32 FreeCasts=0;
    FName ControlAmpCard;
    int64 ControlAmpBp=0, ControlAmpUntilMs=0;
    TArray<TPair<FName,int64>> RecentCasts;
    int32 TallyMarks=0;
    int64 TallyAtMs=0;
    TArray<FLDSkillContribution> SkillContributions;
    // Skill branch state (LDCombatBranchDirector.cpp).
    FLDCombatBranchState Branch;
    // #345 keystone_thick_skin stagger bleed: single-hit HP loss past 35% MaxHP settles over 2.0s at 0.5s intervals.
    struct FStaggerBleed
    {
        int64 RemainingP2=0;
        int32 TicksLeft=0;
        int64 NextTickMs=0, IntervalMs=500;
        FLDCombatBodyRef SourceBody;
        FGuid SourceEntity;
        FGuid CreditOwner;
        FLDCombatSourceRef Source;
    };
    TArray<FStaggerBleed> StaggerBleeds;
    // #345 active_unplug parry stance, forced Combo 3 charges, and active_flurry_rush frenzy waves.
    TOptional<FLDDemoEffectDefinition> ActiveParry;
    int64 ParryUntilMs=0;
    FGuid ParryEffectId;
    FLDCombatSourceRef ParrySource;
    bool bParryTriggered=false;
    int32 ForcedCombo3Charges=0;
    TOptional<FLDDemoEffectDefinition> ActiveFrenzy;
    int64 FrenzyUntilMs=0;
    FGuid FrenzyEffectId;
    FLDCombatSourceRef FrenzySource;
    // #345 cross-axis synergy card states.
    int32 SpellbladeCharges=0;
    int64 SpellbladeUntilMs=0;
    ELDCombatElement SpellbladeElement=ELDCombatElement::None;
    FLDCombatSourceRef SpellbladeHost;
    int64 StillSinceMs=0;
    FVector LastStillFootCm=FVector::ZeroVector;
    bool bHasLastStillFoot=false;
    bool bEagleStanceActive=false;
    int64 EagleLingerUntilMs=0;
    bool bOnAnyIce=false;
    FVector LastIceSlideFootCm=FVector::ZeroVector;
    bool bTrackingIceSlide=false;
    double IceSlideAccumCm=0;
    int64 ScholarBasicUntilMs=0;
    int64 ScholarSkillUntilMs=0;
};

// The match counts the grow cards read (spec §7.4), kept on the entity record across lives.
struct FLDCardGrowth
{
    // Y19: HP really healed; Y20: contender kills and assists credited; Y21: hard controls landed on this contender.
    int64 HealedP2=0;
    int32 Kills=0, Assists=0, ControlsTaken=0;
    // K17: hits on contenders of each installed copy's host (by card serial).
    TMap<uint16,int32> SkillHits;
    // A13: primary basic hits on contenders; D09: perfect dodges.
    int32 BasicHits=0, PerfectDodges=0;
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
    FName MonsterId;          // Current camp species.
    TMap<uint32,FName> MonsterLives; // Immutable definitions for released effects of each earlier life.
    FName MonsterAtLife(uint32 Life) const
    {const FName* Found=MonsterLives.Find(Life);return Found?*Found:NAME_None;}
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
    TArray<FPendingCombatIntent> PreInputQueue;
    uint64 NextToken=1;
    uint64 ReceiptRevision=0;
    uint64 ViewRevision=0;
    uint64 PublishedViewRevision=0;
    // Authority ms before which PublishOwnerViews does not rebuild an unchanged owner view only to find its time-driven
    // terms unchanged (ALDCombatDirector::OwnerViewPollMs).
    int64 NextOwnerViewPollMs=0;
    // #198 combat window (contenders): the authority ms of the last combat action (MarkBuildUnsafe); 0 = none yet. Kept
    // across lives; the window is GROWTH.BuildSafeWindowSec. #222: it no longer gates any build command; the owner view
    // publishes it only for the HUD's combat fold of the field summary.
    int64 LastBuildUnsafeAtMs=0;
    // Grow cards (spec §7.4): the match counts their growth reads, kept across lives (Y19 healing, Y20 kills and assists,
    // Y21 controls taken) and K17's contender hits per installed copy (by serial).
    FLDCardGrowth CardGrowth;
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
    int64 BaseP2=0, ADRatioBp=0, APRatioBp=0, AdaptiveRatioBp=0;
    ELDDemoDamageType DamageType=ELDDemoDamageType::Physical;
    int64 SourceADP2=0, SourceAPP2=0, SourceBonusADP2=0;
    // Coefficient of the source's max HP and that max HP snapshotted at release.
    int64 MaxHPRatioBp=0, SourceMaxHPP2=0;
    bool bCritical=false;
    int64 RootDurationMs=0;
    int64 SlowBp=0, SlowDurationMs=0;
    // A contender's primary basic: its combo step (1..3) and committed attack interval (momentum income per hit).
    uint8 ComboStep=0;
    int64 BasicIntervalMs=0;
    // A skill branch's next-basic bonus this primary basic took (joins the damage bonus pool).
    int64 BranchAmpBp=0;
    // A knockback that stuns when a wall stops it (S01 B1, S09 B1, S20 B1).
    int64 KnockbackWallStunMs=0;
    // An S10 B1 discharge: never discharges again.
    bool bOverchargeDischarge=false;
    // Spec §3.7: the damage factors this hit carries from its source (a release's locked factors, a basic's taken ones); the
    // target-side and card factors join them in ApplyHit (LDTactical::DamageFactorProduct).
    TArray<FLDDamageFactor> Factors;
    // Spec §3.8: the steps of the effect chain this hit is (0 for an action's own hit; a hit derived from another is one more).
    uint8 ChainDepth=0;
    // Spec §6.5: the chain this hit belongs to when it was derived from another; an action's own hits find theirs by EffectId.
    FGuid ChainId;
    // A rider of another hit (A11, K13): its own damage type, the parent's crit and factors, never a trigger or a hit count.
    bool bRider=false;
    // A primary basic's extra strike (A06, A09): a basic hit for triggers, no combo advance.
    bool bExtraStrike=false;
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
    // Reaction damage: its reaction, the element it consumed on this target (None: an Overload splash consumed nothing here)
    // and the credit owner of whoever applied that element, named only when exactly one applier held it (the death recap's
    // rule chain, LDDamageRecap). ReactionAssistSetters are every applier of that consumed element: the assist prompt
    // candidates once this hit commits (NotifyReactionAssists).
    ELDCombatReaction ReactionKind=ELDCombatReaction::None;
    ELDCombatElement ReactionConsumed=ELDCombatElement::None;
    FGuid ReactionSetter;
    TArray<FGuid> ReactionAssistSetters;
    // #147 build v7 keyword payload (docs/systems/COMBAT_AND_GROWTH.md).
    int64 SilenceDurationMs=0;
    int32 BreachStacks=0;            // shared breach stacks this hit applies
    int64 BreachMs=0;                // their shared breach lasts this long when positive (corrode); else COMBAT.BreachSec
    int32 BurnStacks=0;              // fire layers this hit applies (LDCombatElements batch: fire, water, electric)
    // #198 elements: wet and electric this hit applies; ElementLevel/ElementAPP2 are the applier snapshot every element of
    // the hit reads (fire damage, reaction damage, discharges). StampElementSnapshot fills them.
    // The hit's own (native) elements other than fire, one bit each (LDCombatElements::Bit); fire is BurnStacks.
    uint8 NativeElements=0;
    int32 ElementLevel=1;
    int64 ElementAPP2=0;
    // Status or settle damage (early fire settle, discharge, reaction): never a skill hit for readers (AI spec §8).
    bool bStatusSettle=false;
    // The rot this hit applies when it differs from COMBAT.Rot* (S21 A1's cut and duration, N3 B1's longer rot); 0 = approved.
    int64 RotCutBp=0, RotMs=0;
    // An overdrive's chill root (OVERDRIVE.ChillRootSec) rides with its chill.
    bool bBurnTick=false;            // a burn tick: no crit, no on-hit, never a Q/W hit for passives
    int64 AttackSlowBp=0, AttackSlowMs=0;
    // Delivery scaling is separate from the additive damage bonus pool (impact outer ring, for example).
    int64 DeliveryScaleBp=10000;
    // #198 F workshop package multiplier (FLDDamageEvaluationInput::PackageScaleBp).
    int64 PackageScaleBp=10000;
    // The exit a knockback of this hit belongs to (an active, A04's basic) for Y08 and the final bowling.
    FLDCombatSourceRef KnockbackSource;
    // Native direct damage of an active (spec §8 主动直接伤害): its damage segment, its zone pulses and the hits copied
    // from its payload (projectile, cone wave, impact, dash contact). B17 and B18 read it; component-added hits never set it.
    // A derived host hit (a later return, pierce or echo of the same release EffectId) sets it and counts as native.
    bool bDirectAbilityDamage=false;
    bool bDashContactHit=false;
    bool bChannelHit=false;
    // Cards: burn layers a card's fire adds to this hit (ApplyCardElement), credited to CardBurnSource; a card's other element
    // on this hit (one per element, the first card naming it), applied as that card's own application after the hit's native
    // element of the same kind.
    struct FCardElementEntry
    {
        ELDCombatElement Element=ELDCombatElement::None;
        FLDCombatSourceRef Source;
        uint8 Tier=1;
    };
    int32 CardBurnStacks=0;
    int32 OverdriveBurnStacks=0;
    FLDCombatSourceRef CardBurnSource;
    uint8 CardBurnTier=1;
    TArray<FCardElementEntry> CardElements;
    // #345: monster damage scale, projectile flight distance, basic+skill dual trigger, stagger bleed, and backstab lunge terms.
    int64 MonsterMultiplierBp=0;
    double FlightDistanceCm=0;
    double SourceDashDistanceCm=0;
    bool bTriggersBasicOnHit=false;
    bool bStaggerBleed=false;
    TOptional<FLDDemoEffectDefinition> BackstabTerms;
    bool CarriesElement(ELDCombatElement Element) const
    {
        return (NativeElements & LDCombatElements::Bit(Element))!=0
            || CardElements.ContainsByPredicate([Element](const FCardElementEntry& Row) { return Row.Element==Element; });
    }
    TSharedPtr<FLDCombatCastLedger> CastLedger;
};

// "native direct hit": an ability hit of the host active's own release (its damage, zone pulses, impacts, traps, chain
// hops, S16's answer and derived host hits sharing the release EffectId). Basics, the innate dash, reactions, burn ticks,
// settles, reflections, riders and card-added hits are none. The caller checks hostility and damage.
inline bool IsNativeDirectHit(const FLDCombatHit& Hit)
{
    return Hit.HitClass==ELDCombatHitClass::Ability && Hit.bDirectAbilityDamage && Hit.Source.Kind==ELDCombatSourceKind::Learned
        && !Hit.bReflect && !Hit.bAddedDamage && !Hit.bBurnTick && !Hit.bStatusSettle && !Hit.bRider
        && !Hit.bFromShieldTrigger;
}

// A delayed or repeated package a branch scheduled on one struck body (follow_up, delayed momentum_steal).
struct FLDCombatFollowUp
{
    FLDCombatHit Payload;
    int64 DueMs=0, IntervalMs=0;
    int32 Remaining=1;
    int64 StealP2=0, StealMultiplierBp=0, FreeCastMs=0;
    // N3 B2: momentum its caster gains when it lands.
    int64 MomentumP2=0;
};

// S07 B: the self area repeats around its living owner's foot.
struct FLDCombatPulse
{
    FLDCombatHit Template;
    double RadiusCm=0;
    int64 NextMs=0, IntervalMs=0;
    int32 Remaining=0;
    int64 MomentumP2=0, ShieldBaseP2=0, ShieldMaxHPBp=0;
};

struct FLDCombatDashContact
{
    FGuid EffectId;
    FLDCombatBodyRef Body;
    FLDCombatSourceRef Source;
    int64 StartMs=0, EndMs=0, PaidMomentumP2=0;
    TArray<FLDCombatBodyRef> Struck;
    TOptional<FLDCombatHit> NativeHit;
    int32 NativeHitsRemaining=0;
    bool bRefunded=false;
    bool bCanDamage=false;
    // The dash direction; D06 phases through bodies (each passed one takes a basic strike) and D01 refreshes the dash once.
    FVector Direction=FVector::ZeroVector;
    bool bPhase=false, bReset=false;
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
    // The final wave knocks every struck enemy: the skill's own knockback (S22, N1) along the cast direction
    // (bFinalKnockbackAlongCast), M18's away from the channel origin.
    double FinalKnockbackCm=0;
    int64 FinalKnockbackMs=0;
    bool bFinalKnockbackAlongCast=false;
    // S22 A2: how many of the last waves carry the slow (1 = the final wave only).
    int32 SlowWaves=1;
    // Spec §5: N1 B1 roots on the last wave.
    int64 FinalRootMs=0;
};

// N2 电风扇 (spec §5): a placed fan that blows every IntervalMs until EndMs over a rectangle LengthCm long and 2 HalfWidthCm
// wide in front of it: every enemy inside is pushed along the blow (its wall stun, the owner's last element when it carries
// one). Its heading swings
// SweepDegrees either way at SweepDegrees per second (B). It ends with its owner's life.
struct FLDCombatFan
{
    FLDCombatHit Payload;
    FGuid ObservedKey;
    FVector OriginCm=FVector::ZeroVector;
    FVector Direction=FVector::ZeroVector;
    double LengthCm=0, HalfWidthCm=0, SweepDegrees=0;
    int64 StartMs=0, EndMs=0, NextBlowMs=0, IntervalMs=0;
    bool bCarryElement=false;
    int64 MomentumP2=0;
    uint64 CreatedSerial=0;
    // The heading of a blow at AtMs (the sweep's triangle wave).
    FVector HeadingAt(int64 AtMs) const;
};

// Spec §5 N4: a placed school bag (LDCombatBagDirector.cpp). It is no body: it blocks bodies through its blocker, stops
// the enemy flights that reach it and takes the raw amount of enemy flights, ground blasts, cones and release areas
// (RawBagDamageP2). At ExpiresAtMs or HpP2 0 it blows Payload over BlastRadiusCm.
struct FLDCombatBag
{
    FLDCombatHit Payload;
    FGuid ObservedKey;
    FVector CentreCm=FVector::ZeroVector;
    double BodyRadiusCm=0, BlastRadiusCm=0;
    // A1: half the width of the wall to the partner bag of the same skill (WallTo); zero without the branch.
    double WallHalfWidthCm=0;
    int64 HpP2=0;
    int64 ExpiresAtMs=0;
    // B2: the fire field it leaves where it blows.
    int64 FireFieldMs=0;
    uint64 CreatedSerial=0;
    TWeakObjectPtr<AActor> Blocker;
    FGuid WallTo, WallKey;
    TWeakObjectPtr<AActor> WallBlocker;
};

// #198 F: what a delayed ground blast is. An S18 landing, an S19 detonation and an M07 compressed zone are natural
// A branch echo is a derived host hit: it never echoes again.
enum class ELDCombatGroundOrigin : uint8 { Impact, Trap, Echo };

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
    // Skill branches: an impact's center stun (S18 B1) and the wet area it leaves (S20 B2); a trap's wire to its owner's
    // next armed trap (S19 A1) and the enemies that wire already struck (each once).
    int64 CenterStunMs=0, LingerMs=0;
    double WireCm=0;
    int64 WireBaseP2=0, WireAPRatioBp=0;
    TArray<FGuid> WireStruck;
    // Observers key a blast on its release EffectId; an echo, which shares it, carries its own key.
    FGuid ObservedKey;
    // C17 (traps only): moved once already; CreatedSerial orders the owner's zones and traps by creation.
    bool bRelocated=false;
    uint64 CreatedSerial=0;
};

// Released straight, non-homing projectile: first legal hostile body (after M14's extra bodies) or blocking terrain ends it.
struct FLDMonsterStrike
{
    FGuid Key;
    FLDMonsterArea Area;
    FLDCombatHit Payload;
    int64 AtMs=0;
    uint64 Token=0;
    bool bReleased=false;
};

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
    int64 BaseP2=0, ADRatioBp=0, APRatioBp=0, AdaptiveRatioBp=0;
    ELDDemoDamageType DamageType=ELDDemoDamageType::Magical;
    int64 SourceADP2=0, SourceAPP2=0, SourceBonusADP2=0;
    int64 MaxHPRatioBp=0, SourceMaxHPP2=0;
    FVector OriginCm=FVector::ZeroVector;
    FVector Direction=FVector::ZeroVector;
    double SpeedCmPerSec=0, RangeCm=0, RadiusCm=0;
    double TravelledCm=0;
    int64 AdvancedThroughMs=0;
    int64 SlowBp=0, SlowDurationMs=0;
    // A pierce: after a body the flight goes on for PierceRemaining more distinct bodies, each at PierceScaleBp of the
    // package with the same slow and controls. M13: where it ends, a return flight starts toward the thrower at
    // ReturnScaleBp; a return (bReturn) never pierces or returns. Struck holds the bodies this flight already hit.
    int32 PierceRemaining=0;
    int64 PierceScaleBp=0, ReturnScaleBp=0;
    // Skill branches: the return's element (S06 B1, S21 B2); the ground state the outbound flight leaves where it ends (N3,
    // S21 A2).
    ELDDemoElement ReturnElement=ELDDemoElement::None;
    TOptional<FLDDemoEffectDefinition> LandingGround;
    // A telegraphed flight (windup of at least 0.15 s, or a camp warning): its end can be perfectly dodged.
    bool bTelegraphed=false;
    bool bReturn=false;
    TArray<FGuid> Struck;
    // A return that starts on the body its outbound flight ended on touches that body at its start (it may strike it again).
    FGuid StartContact;
    // The complete hit payload snapshotted at release; the contact copies it and names the struck body.
    FLDCombatHit Payload;
    // final_cage: this flight already crossed the standing fence (it wears the fence once).
    bool bCrossedFence=false;
    // A05's flying basic: the strikes and riders that land with its primary (A06, A09, A11), retargeted to the struck body.
    TArray<FLDCombatHit> Riders;
    // A08 on a flying third basic: its free Q is queued at the first contact, toward the body struck (bComboCast).
    bool bComboCast=false;
    FLDCombatAction ComboBasic;
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
    int64 PulseSlowBp=0, PulseSlowMs=0;
    int64 BaseP2=0, ADRatioBp=0, APRatioBp=0;
    ELDDemoDamageType DamageType=ELDDemoDamageType::Magical;
    int64 SourceADP2=0, SourceAPP2=0;
    // The release's package multiplier, carried by every damage pulse (its locked factors ride on CastLedger).
    int64 PackageScaleBp=10000;
    // #147 zone_burn: BurnStacks layers (at the owner's level and AP snapshotted at release) on every enemy inside,
    // every BurnIntervalMs from the zone's start while it lasts. #198 S08: a damaging zone's burn beats with its pulses,
    // so every pulse carries PulseBurnStacks in the same hit (one element batch: its fire before a B18 water/electric).
    int32 BurnStacks=0;
    int64 BurnIntervalMs=0, NextBurnAtMs=0;
    int32 PulseBurnStacks=0;
    int32 OverdrivePulseBurnStacks=0;
    // #198: the owner's element snapshot (level, AP) at release; zone_wet makes every pulse apply wet; zone_haste gives
    // the owner and its current-match teammates (OwnerGroup/OwnerPKSide at release, PK side 0: none) HasteBp move speed
    // while their foot stands inside.
    int32 ElementLevel=1;
    int64 ElementAPP2=0;
    bool bWet=false;
    int64 HasteBp=0;
    FGuid OwnerGroup;
    uint8 OwnerPKSide=0;
    // Skill branches: the owner's damage cut and momentum per pulse while its foot stands inside (S08 A2, S20 A2).
    int64 GuardBp=0, MomentumP2=0;
    TSharedPtr<FLDCombatCastLedger> CastLedger;
    // M16: the centre follows the living source body's foot at every mechanics advance.
    bool bTether=false;
    // C17: moved once already (never again); CreatedSerial orders the owner's zones and traps by creation.
    bool bRelocated=false;
    uint64 CreatedSerial=0;
};

// #198 S10: after the first hit, every IntervalMs the nearest not yet struck legal enemy within RadiusCm of the last
// struck body takes the payload at RatioBp of the previous bounce's share, until Remaining runs out; with nobody else near
// (spec §9) the bolt bounces back to its living first target.
struct FLDCombatChain
{
    FLDCombatHit Payload;
    TArray<FGuid> Struck;
    FLDCombatBodyRef First;
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
    // The owner's overdrive charge intent (a Q/W/E pressed or let go with a full bar). Shown to observers while held;
    // refused silently when stale, not castable or the bar is short. A later request with bOverdrive needs it.
    void SubmitCharge(const FLDCombatCaller& Caller, const FLDCombatChargeRequest& Request);
    FLDCombatActionReceipt QueryActionReceipt(const FLDCombatCaller& Caller, uint64 DirectSequence) const;
    FLDCombatEntryView QueryAvailability(const FLDCombatCaller& Caller, ELDCombatEntry Entry) const;
    // The body stands in a dorm and has been out of combat for DORM.RegenDelaySec (the dorm rest that allows a swap).
    bool IsDormRested(const FLDCombatBodyRef& Body) const;
    bool BuildOwnerView(const FLDCombatCaller& Caller, FLDCombatOwnerView& Out) const;
    // Spec §6.5: the chain ends Entity's owner receives (its recent ones) and every contender's 本局最离谱 row.
    void ReadOwnerChains(const FGuid& Entity, TArray<FLDChainEndView>& Out) const;
    void ReadResultHighlights(TArray<FLDResultHighlight>& Out) const;
    // The match closes: every open chain ends now (bookkeeping only, no gameplay effect) so the result counts the last ones.
    bool FinishOpenChains(int64 NowMs, FString& Error) { return AdvanceChains(NowMs,Error,true); }

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
    bool ShouldBufferPreInput(const FGuid& Entity, ELDCombatEntry Entry) const;
    bool HasBufferedDirectSequence(const FGuid& Entity, uint64 DirectSequence) const;
    void BufferPreInputIntent(const FGuid& Entity, FPendingCombatIntent&& Intent);
    void ClearPreInputQueue(const FGuid& Entity, ELDCombatReject DirectReason, ELDOrderFailure OrderFailure);
    bool FlushPreInputQueues(const FLDGameplayWritePermit& Permit, FString& Error);
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
    // Spec §8.3 ice inertia: the authoritative acceleration and braking scale of Body now (GROUND.IceAccelScale on an enemy's
    // ice, 1 elsewhere); the owner view carries the same value to the owning client's movement prediction.
    float ReadMoveAccelScale(const ALDCharacter& Body) const;
    float MoveAccelScaleOf(const FLDBodyCombatState& Body) const;
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
    // Spec §14 public ground-state facts for the observation owner (every live state; its relay keeps the visible ones).
    bool ReadObservedGround(TArray<struct FLDObservedGroundFact>& Out, FString& Error) const;
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
    bool ReadResultCombat(const FGuid& Contender,struct FLDResultCombat& Out) const;
    bool DamageTotalsOf(const FGuid& Contender, TArray<FLDDamageLine>& Out) const;
    // This match's trigger count of each of Contender's cards (training excluded), by card name; false like DamageTotalsOf.
    bool CardTriggersOf(const FGuid& Contender, TArray<FLDCardTriggerRow>& Out) const;
    // Training combos (live updates + finished quiet-gap summaries + resets) ready to send to each contender (#331).
    void TakeFinishedTrainingCombos(int64 NowMs, int64 QuietMs, TMap<FGuid,FLDTrainingComboView>& Out);

#if WITH_DEV_AUTOMATION_TESTS
    // ---- DEV test commands (LDDevTestProtocol.h), committed by ALDGrowthDirector inside the Transactions permit.
    // The contender's current Alive body to its committed HP maximum and a full momentum bar (the shield is untouched). False with Detail when
    // the permit, the contender record or a living body is missing; nothing changes then.
    bool DevRefillResources(const FGuid& Contender, const FLDGameplayWritePermit& Permit, FString& Detail);
    // The living body's momentum to Points whole points (0..MOMENTUM.Max); false with Detail outside that range or without a body.
    bool DevSetMomentum(const FGuid& Contender, int64 Points, const FLDGameplayWritePermit& Permit, FString& Detail);
    // DEV: one ownerless ground state of Kind (ELDGroundStateKind) at the contender's living foot.
    bool DevSpawnGround(const FGuid& Contender, uint8 Kind, const FLDGameplayWritePermit& Permit, FString& Detail);
    // Every cooldown track of the contender (learned, held or forgotten, and the innate dash) ready at the permit
    // boundary; tracks keep their identity. False with Detail when the permit or the contender record is missing.
    bool DevResetCooldowns(const FGuid& Contender, const FLDGameplayWritePermit& Permit, FString& Detail);
    // Pure parts of the two commands. RefillPool returns whether HP or momentum changed; ResetTracks the number of tracks
    // that were cooling down at NowMs.
    static bool DevRefillPool(FLDBodyCombatState::FResources& Pool, int64 MomentumP2);
    // SetMomentum's pool part: false (nothing changed) when Points is outside 0..MaxP2/100; otherwise momentum becomes
    // Points whole points, and a change advances the revision once and clears the sub-P2 remainder. OutChanged says which.
    static bool DevSetMomentumPool(FLDBodyCombatState::FResources& Pool, int64& MicroP2, int64 Points, int64 MaxP2, bool& OutChanged);
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
    // living body's HP is full and its momentum at MOMENTUM.Start. Returns the number of dropped tracks.
    int32 DevFinishReset(const FGuid& Contender, int64 NowMs);
    // Pure part of DevFinishReset's tracks.
    static int32 DevDropTracks(TArray<FLDCombatTrack>& Tracks, int64 NowMs);
#endif

    // ---- Camps.
    bool CampPeerDied(const FLDCombatBodyRef& Peer,const FLDGameplayWritePermit& Permit,FString& Error);
    // A match event's recovery for a living contender body: MissingBp of its missing HP plus MaxBp of its maximum HP, and
    // MomentumP2 momentum (a camp clear, a final-field kill).
    bool CanUseRecoveryPack(const FLDCombatBodyRef& Body) const;
    bool RecoverContender(const FLDCombatBodyRef& Body,int64 MissingBp,int64 MaxBp,int64 MomentumP2,
        const FLDGameplayWritePermit& Permit,FString& Error,int64 FlatHealP2 = 0);
    bool CancelCampWindup(const FLDCombatBodyRef& Body,const FLDGameplayWritePermit& Permit,FString& Error);
    FLDCampResetPrepareResult PrepareCampReset(const FLDCampResetInput& Input, const FLDGameplayWritePermit& Permit) const;
    void ApplyCampReset(FLDPreparedCampReset&& Prepared, const FLDGameplayWritePermit& Permit);

    // ---- Field rewards.
    // Transactions: applies the approved airdrop healing, momentum and timed move-speed rewards to its living claimant.
    // False only on an authority fault; bOutApplied is false when the body is already gone or dying.
    bool ApplyAirdropReward(const FGuid& Contender, const FLDGameplayWritePermit& Permit, bool& bOutApplied,
        FLDAirdropRewardFact& Out, FString& Error);

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
    // Freezes or thaws a world body (camp member, Boss) for a rest; its HP stays as it is. RespawnsAndWorld permit.
    bool SetBodySuspended(const FLDCombatBodyRef& Body, bool bSuspended, const FLDGameplayWritePermit& Permit, FString& Error);
    // Phase boundary cleanup (approved §8): every in-flight effect is cancelled (CancelledByPhase, no damage, break or
    // death is invented) and every control, mark, burn, breach, shield and temporary term on every remaining body ends.
    // Returns the number of cancelled effects. RespawnsAndWorld permit.
    int32 CancelAllForPhase(const FLDGameplayWritePermit& Permit);
    // Phase refill (approved §8): every cooldown track of the contender (skills, dash, held tracks of forgotten actives)
    // and its basic cadence are ready at the boundary. New lives start at full HP by construction.
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
    // Environmental damage (the PK ring and the final showdown's street closure, Combat/LDCombatEnvironment.cpp): straight
    // to HP, no resistance, shield, reduction, delay, reflection, trigger or reward; a lethal amount locks a normal
    // unowned death. Effects permit.
    bool ApplyEnvironmentDamage(const FLDCombatBodyRef& Body, int64 AmountP2, const FLDGameplayWritePermit& Permit, FString& Error);
    // ---- P6 (Combat/LDCombatPacing.cpp) ----
    // Whether the body took a hit after SinceMs (a locker channel breaks on damage).
    bool TookDamageSince(const FLDCombatBodyRef& Body, int64 SinceMs) const;
    // Spec §11.1 cold day: when a puddle turns to ice (DAILY.ColdFreezeSec after it formed, at the latest when it would end).
    int64 ColdFreezeAtMs(const FLDGroundState& Ground) const;
    // Spec §11.2 layoff affix: the one life-long attack bonus of the body's LayoffStacks (none at zero).
    void ApplyLayoffBonus(FLDBodyCombatState& Body) const;
    // Spec §11.2 fridge affix: keeps one ice of the member's own under its feet (Actions).
    bool EnsureAffixIce(const FLDCombatBodyRef& Member, int64 NowMs, FString& Error);
    // Spec §11.1 rain: Count unowned puddles at random field places, each DAILY.RainPuddleSec. RespawnsAndWorld permit.
    bool SpawnDailyRain(uint64 Seed, int32 Count, const FLDGameplayWritePermit& Permit, FString& Error);
    // Spec §11.1 typhoon: every field contender is pushed PushCm along Direction (walls stop it) and every ground state
    // drifts as far. RespawnsAndWorld permit.
    bool ApplyTyphoon(const FVector2D& Direction, double PushCm, const FLDGameplayWritePermit& Permit, FString& Error);
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
    friend class FLDDevSceneToolsTest;
    friend class FLDGroundBossBodyTest;
    friend class FLDDemoCoreTest;
    friend class FLDReconnectProtectionTest;
    friend class FLDControlFeelElevationAimAndClipTest;
    friend bool LDTestRunDashOrderRetention(class FAutomationTestBase&);
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
#if WITH_DEV_AUTOMATION_TESTS
    friend class FLDCombatCancelledRecoveryCueTest;
    friend struct FLDDemoVisionReactionAccess;
    friend struct FLDDemoVisionIceAccess;
#endif

    // ---- Body lifecycle (core-called, RespawnsAndWorld permit).
    bool PrepareContenderBody(ALDCharacter& Body,const FLDVictimKey& Victim,
        const FLDGameplayWritePermit& Permit,FLDBodyCombatState& Out,FString& Error) const;
    // #97: CampStage scales the monster's base values for this whole life (Development/LDCampGrowth.h); 0 = base.
    bool PrepareCampMemberBody(ALDCharacter& Body,const FLDVictimKey& Victim,FName MonsterId,uint8 CampStage,ELDCampAffix Affix,bool bPeddler,
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
    // The same owner seen through its life slice (Framework/LDLifeAuthority.h), for files that need no other GameMode state.
    ILDLifeAuthority* Authority() const;
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
    // How often an owner view whose revision and life are unchanged is rebuilt to catch its time-driven terms (the
    // observation publish period).
    static constexpr int64 OwnerViewPollMs=50;

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
    bool ReleaseMonsterSkill(ALDCharacter& Actor,FLDBodyCombatState& Body,const FLDCombatAction& Action,ELDCombatReject& Reject,FString& Error);
    bool AdvanceMonsterStrikes(int64 NowMs,FString& Error);
    bool LandBasic(ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDCombatAction& Action, FString& Error);
    bool ReleaseAbility(ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDCombatAction& Action,
        ELDCombatReject& Reject, FString& Error);
    // The one hit payload a hostile release snapshots for every target of it (damage terms, source AD/AP, controls,
    // keyword and element payload, amplification and the mods' package multiplier), before its delivery.
    bool BuildReleasePayload(const FLDBodyCombatState& Body, const FLDCombatSourceRef& Source, const FLDCombatActiveTerms& Terms,
        const FVector& ReleaseFoot, const FGuid& EffectId, uint64 Token,
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
    void ClosePending(ELDCombatReject Reason);
    void ClosePending(FLDCombatEntityRecord& Record, ELDCombatReject Reason);
    // Close: queued intents and unreleased actions end with MatchClosed receipts; nothing is paid.
    void EndMatchActions();

    // ---- Effects (LDCombatEffects.cpp).
    bool ApplyHit(const FLDCombatHit& Hit, FString& Error);
    // Every queued hit of this Effects boundary in order (and what those queue), then the C01 windows that closed by NowMs:
    // a window settles only after every fact up to its boundary was committed (spec §13.3).
    bool ApplyPendingHits(int64 NowMs, FString& Error);
    bool AdvanceProjectiles(FString& Error);
    // The root and slow cleanses of one released value set on its caster; true when slows (a temporary term) ended.
    bool ApplyCleanses(FLDBodyCombatState& Body, const FLDDemoAbilityValues& Values, int64 NowMs);
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
    // Momentum (contenders) settles with the same interval when Momentum is given (the match's MOMENTUM.* terms).
    bool RegenerateBodies(const LDTactical::FOutOfCombatRecovery& Recovery, int64 NowMs, FString& Error,
        const LDTactical::FOutOfCombatRecovery* DormRecovery = nullptr, const FLDDormZones* Dorms = nullptr,
        const FLDDemoMomentumParameters* Momentum = nullptr);

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
    // The lone resolver of the card effects a body holds: every installed card copy's effects of Op
    // (FLDAttributeProjection::InstalledCards by serial, catalog order of each card's effects), fixed amounts grown for the
    // holder's level (LDSkillGrowth::GrowEffect). False with Error when a listed card or its growth is missing.
    bool CollectPassiveEffects(const FLDAttributeProjection& Projection, ELDDemoEffectOp Op, TArray<FLDPassiveEffectRef>& Out,
        FString& Error) const;
    // Momentum change of a contender body (a gain, a spend or a steal), clamped to the bar and booked under Kind; marks
    // the owner view. Camp members hold no momentum (CampMomentumPolicy) and ignore it. Returns the applied change.
    int64 GrantMomentum(FLDBodyCombatState& Body, int64 AmountP2, ELDMomentumBook Kind=ELDMomentumBook::Other);
    // The bar over [FromMs, NowMs) of a contender body (LDMomentum::Settle with its brains rate), booked as combat/idle.
    bool SettleMomentum(FLDBodyCombatState& Body, const FLDDemoMomentumParameters& Momentum, int64 FromMs, int64 NowMs,
        int64& InOutP2, FString& Error);
    // Momentum income of one committed hostile hit of Source: a primary basic by its interval, a release's first native
    // direct hit on each body MOMENTUM.SkillHit.
    bool OnMomentumHit(FLDBodyCombatState& Source, const FLDCombatHit& Hit, const FLDBodyCombatState& Victim,
        bool bNativeDirect, FString& Error);
    // Ends the body's overdrive charge (released, let go, controlled, the bar fell); observers stop seeing it.
    void EndOverdriveCharge(FLDBodyCombatState& Body);
    // OVERDRIVE.Scale on the release's terms and the bonus of the active's element (CONTENT_AND_BALANCE §3.5).
    bool ApplyOverdrive(const FLDBodyCombatState& Body, const FLDDemoAbilityDefinition& Ability, FLDCombatActiveTerms& Terms,
        FString& Error) const;
    // One LD_MOMENTUM line of a contender life's momentum books (at the end of the life; a rest starts a new life).
    void LogMomentumLedger(const FLDBodyCombatState& Body) const;
    // Cast range of an active for this body now: the authored range and its share of the extra basic-attack range.
    bool CastRangeCm(const FLDBodyCombatState& Body, const FLDDemoTargetDefinition& Target, double& OutCm, FString& Error) const;
    // Hooks of one landing primary basic before it is queued (LDCombatCards.cpp): A04's push on the third step, A06's double
    // and A09's extra strikes, A11's rider, A12's focus and the factors it takes.
    bool OnBasicLanded(FLDBodyCombatState& Body, FLDCombatHit& Primary, TArray<FLDCombatHit>& OutExtra, FString& Error);
    // One card's element on Hit (A16-A19, K19-K24, D12). Fire adds burn layers credited to Card; the others name Card in the
    // hit's CardElements for ApplyElements.
    bool ApplyCardElement(FLDCombatHit& Hit, ELDDemoElement Element, int32 FireStacks, const FLDCombatSourceRef& Card, FString& Error,
        uint8 MedalTier=1) const;
    // Voluntary dashes that ended by NowMs; one that completed with a real displacement arms every held DashComplete
    // empower_basic (D03, values snapshotted now).
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
    // A one-time heal of Body (lifesteal, K15, charms); regeneration never passes here. Live rot cuts it first; Y14
    // turns the overflow into a shield and Y19 counts what really healed. True when HP changed; OutApplied (optional) is healed.
    bool HealBody(FLDBodyCombatState& Body, int64 AmountP2, const FLDCombatSourceRef& HealSource=FLDCombatSourceRef(),
        int64* OutApplied=nullptr, const FGuid& RootEffectId=FGuid(), FString* OutError=nullptr);
    // The single shield entry: live rot cuts the amount, then it applies StackToLatestExpiry; a granted shield
    // records its amount (ShieldGrantedP2).
    // Spends AmountP2 of Body's shield layers, earliest deadline first; returns how many layers it emptied.
    static int32 SpendShieldLayers(FLDBodyCombatState& Body, int64 AmountP2, TArray<FGuid>* OutEmptied=nullptr);
    // Drops the layers past their deadline and takes their amount off Resources.ShieldP2; true when anything changed.
    static bool ExpireShieldLayers(FLDBodyCombatState& Body, int64 NowMs);
    bool GrantShieldTo(FLDBodyCombatState& Body, int64 AmountP2, int64 DurationMs, const FGuid& EffectId,
        const FGuid& SourceEntity, int64 NowMs, FString& Error, const FLDCombatSourceRef& GrantSource=FLDCombatSourceRef());
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
    // (basic_stacks on a primary basic, A14). Fire (hit layers, card layers) is an element (ApplyElements).
    bool ApplyKeywordStacks(FLDBodyCombatState& Target, const FLDCombatHit& Hit, FLDBodyCombatState* Source, int64 NowMs, FString& Error);
    // Burn ticks due by now: every layer on its own timer, one damage hit per applier, source and tick instant.
    bool AdvanceBurns(int64 NowMs, FString& Error);
    // The damage-triggered shield of Body after it took hostile positive damage (Y07: the rest starts only on a granted shield).
    bool OnDamaged(FLDBodyCombatState& Body, int64 NowMs, FString& Error);
    // Statuses of the keyword records (silence, breach x stacks, burn x stacks) for every observer.
    void AppendKeywordStatuses(const FLDBodyCombatState& Body, int64 NowMs, TArray<FLDObservedStatus>& Out) const;
    // ---- #198 elements, reactions and the element actives (LDCombatElementsDirector.cpp).
    bool ElementTerms(FLDElementTerms& Out, FString& Error) const;
    bool GroundTerms(FLDGroundTerms& Out, FString& Error) const;
    // The applier snapshot of a hit's elements (fire damage, reaction damage, shock strikes, rot ticks): Source's level and AP.
    bool StampElementSnapshot(const FLDBodyCombatState& Source, FLDCombatHit& InOut, FString& Error) const;
    ELDCombatElement ElementOfBody(const FLDBodyCombatState& Body, int64 NowMs) const;
    // The element batch of one committed hit on a controllable Target: fire (the hit's layers and its living source's held
    // burn triggers), then water, shock, chill and rot, each applying, refreshing or reacting with the element held.
    // OutCard: which card element of the hit took effect (none when the hit carried none, or the reaction lock dropped it).
    struct FLDAppliedCardElement { bool bApplied=false; bool bFire=false; FLDCombatSourceRef Source; };
    bool ApplyElements(FLDBodyCombatState& Target, const FLDCombatHit& Hit, FLDBodyCombatState* Source, int64 NowMs, FString& Error,
        FLDAppliedCardElement* OutCard=nullptr);
    // Whether the last ApplyElement took effect (false when the target was dying or the reaction lock dropped it).
    bool bElementApplied=false;
    // One element on Target: installs or refreshes it, or reacts with a different one held (spec §8.2 table), and converts
    // the ground states under Target's foot (spec §8.3).
    bool ApplyElement(FLDBodyCombatState& Target, ELDCombatElement Kind, const FLDElementApplier& Applier, int32 FireStacks,
        const FGuid& HitId, int64 NowMs, FString& Error, const FLDElementExtras& Extras=FLDElementExtras());
    // Ends Body's mark now, with the slows it holds.
    void ClearMark(FLDBodyCombatState& Body);
    // A shocked Target took a direct hit: every COMBAT.ShockEveryNthHit-th (every hit while overcharged) strikes ShockRatio x R
    // on it and on its applier's other enemies within ShockSplashRadius (status damage, never counted again).
    bool CountShock(FLDBodyCombatState& Target, const FVector& TargetFoot, int64 NowMs, FString& Error);
    // One shock strike of Applier on Holder at Foot and on Applier's other enemies around it (status damage, never counted).
    bool StrikeShock(const FLDBodyCombatState& Holder, const FLDElementApplier& Applier, const FVector& Foot, int64 NowMs,
        FString& Error);
    // Fog pulses, chain bounces, S16 ends, rot ticks and lapsed marks due by now.
    bool AdvanceElements(int64 NowMs, FString& Error);
    // ---- Spec §8.3 ground states (LDCombatGroundDirector.cpp).
    // A new ground state of Kind owned by Owner at Centre (circle; a band when HalfLengthCm > 0, a sector when AngleDegrees > 0):
    // the caps evict the oldest first, and one skill source keeps one live instance (Y12 copies add one each).
    bool SpawnGround(ELDGroundStateKind Kind, const FLDElementApplier& Owner, const FVector& Centre, int64 NowMs, FString& Error,
        double RadiusCm=0, int64 LifeMs=0, const FVector2D& Heading=FVector2D(1,0), double HalfLengthCm=0,
        double AngleDegrees=0, int64 VulnerableBp=0, int64 AttackSlowBp=0);
    // Incoming landed at Foot (an element hit's target foot or an element area's landing point): every ground state containing
    // it converts (spec §8.3 "再加元素"), credited to Applier.
    bool ConvertGroundAt(const FVector& Foot, ELDCombatElement Incoming, const FLDElementApplier& Applier, int64 NowMs,
        FString& Error);
    // Ground ticks due by now (standing effects on enemies inside), lapsed states, ice contact and Y23's standing growth.
    bool AdvanceGround(int64 NowMs, FString& Error);
    int64 NextGroundDueMs(int64 NowMs) const;
    // The owner's ground-state factors on a hit of its own on Target (N1 A1's ice vulnerability, Y23's standing growth).
    void GroundFactors(const FLDBodyCombatState& Source, const FLDBodyCombatState& Target, TArray<FLDDamageFactor>& Out) const;
    // D11: Owner's newest live ground state, zone or trap not yet moved moves to Foot (once each).
    bool RelocateNewestGround(const FLDBodyCombatState& Owner, const FVector& Foot, int64 NowMs, FString& Error);

    // One fog pulse now: wet on every element-free enemy of its owner inside it.
    bool PulseFog(const FLDCombatFog& Fog, int64 NowMs, FString& Error);
    int64 NextElementsDueMs(int64 NowMs) const;
    // The single report of a completed reaction (AI spec §8); later connectors read ReactionFacts of this Effects step.
    void OnReactionCompleted(const FLDCombatReactionFact& Fact);
    // A committed reaction hit (positive damage or an applied control on its target) consumed an element its
    // ReactionAssistSetters had applied: each contender setter other than the trigger and the target is offered the assist
    // prompt once per reaction (ALDGameMode::SendReactionAssist gates it by the setter's sight), and ReactionAssists records
    // what was sent.
    bool NotifyReactionAssists(const FLDCombatHit& Hit, FString& Error);
    // S16: Body's new stance (a newer one replaces the old record without its answer). ShareBp: an A08 combo cast's share
    // of what the record turns into (the answer's record part, the heal and the shield).
    bool ReleaseBrace(FLDBodyCombatState& Body, const FLDDemoEffectDefinition& Effect, const FLDCombatSourceRef& Source,
        const FGuid& EffectId, uint64 Token, int64 NowMs, FString& Error, TSharedPtr<FLDCombatCastLedger> CastLedger=nullptr,
        int64 ShareBp=10000);
    // S10: the bounces after the first struck body.
    bool StartChain(const FLDCombatHit& Payload, const FLDCombatBodyRef& First, const FVector& FirstFoot,
        const FLDDemoEffectDefinition& Bounce, int64 NowMs, FString& Error);
    // S09: every legal body whose foot lies in the band LengthCm x 2 HalfWidthCm from Origin along Direction with clear
    // sight from Origin, with its foot.
    bool CollectLineTargets(const FVector& Origin, const FVector& Direction, double LengthCm, double HalfWidthCm,
        const FLDBodyCombatState& Caster, TArray<TPair<FLDCombatBodyRef,FVector>>& Out, FString& Error) const;
    // Wet, shocked, chilled, rotten and braced cues for every observer.
    void AppendElementStatuses(const FLDBodyCombatState& Body, int64 NowMs, TArray<FLDObservedStatus>& Out) const;
    // The release-time zone of a #112 active (EFFECT zone).
    bool ReleaseMechanics(FLDBodyCombatState& Body, const FLDCombatSourceRef& Source, const FLDCombatActiveTerms& Terms,
        const FVector& OriginFoot, const FVector& TargetPoint, const FVector& ReleaseDirection, const FGuid& EffectId, uint64 Token,
        TSharedPtr<FLDCombatCastLedger> CastLedger, FString& Error);
    // Hit extras of a #112 value set (knockback, attack slow).
    void FillMechanicsTemplate(const FLDDemoAbilityValues& Values, const FVector& OriginFoot, FLDCombatHit& Template) const;
    bool AdvanceSkillContent(int64 NowMs, FString& Error);
    // N2 fans: their blows due by now.
    bool AdvanceFans(int64 NowMs, FString& Error);
    // N4 bags (LDCombatBagDirector.cpp).
    bool PlaceBag(FLDBodyCombatState& Body, const FLDCombatHit& Payload, const FLDDemoEffectDefinition& Effect,
        const FLDDemoEffectDefinition* Fire, const FVector& Centre, int64 NowMs, FString& Error);
    bool AdvanceBags(int64 NowMs, FString& Error);
    bool ExplodeBag(const FLDCombatBag& Bag, int64 NowMs, FString& Error);
    void RemoveBag(int32 Index);
    void RemoveBags(TFunctionRef<bool(const FLDCombatBag&)> Which);
    void DamageBag(FLDCombatBag& Bag, const FLDCombatHit& Hit, int64 NowMs);
    // #345 world combat entities (slipper pickups, cordon lines, phantom decoys) and stagger bleeds.
    bool AdvanceStaggerBleeds(int64 NowMs, FString& Error);
    bool AdvanceSlipperDrops(int64 NowMs, FString& Error);
    bool AdvanceCordons(int64 NowMs, FString& Error);
    bool AdvanceDecoys(int64 NowMs, FString& Error);
    bool AdvanceSynergyStates(int64 NowMs, FString& Error);
    bool IsEagleStanceActive(const FLDBodyCombatState& Body, int64 NowMs) const;
    // Max HP above the character's own base and level growth. Damage that scales with max HP reads this, shields read
    // the whole maximum.
    int64 BonusMaxHpP2Of(const FLDBodyCombatState& Body) const;
    // The hulking presence growth of an area. AppliedAreaBp is the multiplier the release's own reshapes already put
    // into the radius and ExtraAreaBp another growth of the same area: all add into one multiplier, none compounds.
    double HulkingAreaScaleOf(const FLDBodyCombatState& Body, int64 AppliedAreaBp = 10000, int64 ExtraAreaBp = 0) const;
    // The warcry ring's growth of its own area: 10% per LimitP2 of the caster's max HP.
    static int64 WarcryAreaBpOf(const FLDBodyCombatState& Body, const FLDDemoAbilityValues& Values);
    int64 HulkingKnockbackKeepBp(const FLDBodyCombatState& Body) const;
    // Enemy bags an area reaches and sees from its anchor (the world sight bodies need, CollectAreaTargets) take its raw amount.
    // final_cage: a hostile hit that crosses the standing fence wears it by its raw amount (RawBagDamageP2); it still
    // passes through. Crosses: one end inside the ring and the other outside. InCircle: an area that touches the ring.
    bool FenceCrosses(const FVector& From, const FVector& To) const;
    void HitFence(const FLDCombatHit& Payload);
    void HitFenceInCircle(const FVector& Centre, double RadiusCm, const FLDCombatHit& Payload);
    void HitFenceInCone(const FVector& Origin, const FVector& Direction, double RangeCm, double AngleDegrees, const FLDCombatHit& Payload);
    bool HitBagsInCircle(const FVector& Centre, double RadiusCm, const FLDCombatHit& Payload, int64 NowMs, FString& Error);
    bool HitBagsInCone(const FVector& Origin, const FVector& Direction, double RangeCm, double AngleDegrees,
        const FLDCombatHit& Payload, int64 NowMs, FString& Error);
    // Whether the world lets an area at Anchor reach Point (false: blocked or out of sight range); a fault fails.
    bool AreaSees(const FVector& Anchor, const FVector& Point, bool& bSees, FString& Error) const;
    // The first live enemy bag a flight of RadiusCm meets on From -> To (its fraction in OutFraction), or INDEX_NONE.
    int32 FirstBagOnSegment(const FGuid& Source, const FVector& From, const FVector& To, double RadiusCm, double& OutFraction) const;
    bool BagIsEnemyOf(const FLDCombatBag& Bag, const FGuid& Source) const;
    static int64 RawBagDamageP2(const FLDCombatHit& Hit);
    bool ResolveDashContacts(int64 NowMs, FString& Error);
    bool OnDashContact(FLDBodyCombatState& Body, FLDCombatDashContact& Dash, ELDVictimKind TargetKind, int64 NowMs, FString& Error);
    TSet<FGuid> WaveAfterExemptedChains;
    // Ends the body's channelled cone (a new action, a death or a source change interrupts it).
    void CancelChannel(FLDBodyCombatState& Body);
    bool StartSkillDelivery(FLDBodyCombatState& Body, const FLDCombatActiveTerms& Terms, const FLDCombatAction& Action,
        FLDCombatHit& Payload, int64 NowMs, bool& bHandled, FString& Error);

    // ---- Cards (LDCombatCards.cpp).
    // The installed card copies of Op that sit on Exit (FLDPassiveEffectRef::Targets).
    bool CardsOn(const FLDAttributeProjection& Projection, ELDDemoEffectOp Op, const FLDCombatSourceRef& Exit,
        TArray<FLDPassiveEffectRef>& Out, FString& Error) const;
    // The summed numbers of the copies of Op the body holds (every copy, any target); 0 without one.
    bool SumCards(const FLDAttributeProjection& Projection, ELDDemoEffectOp Op, int64 FLDDemoEffectDefinition::* Field, int64& Out,
        FString& Error) const;
    // Compiles one learned active's (or the innate D's) terms with its branch mod terms and the cards installed on it.
    bool CompileActive(const FLDAttributeProjection& Projection, const FLDCombatSourceRef& Source, FLDCombatActiveTerms& InOut,
        FString& Error) const;
    // The damage factors a release of Source locks now (K09/K12 skill_damage_factor, K16 idle on Track, K17 growth).
    bool ReleaseFactors(const FLDBodyCombatState& Body, const FLDCombatEntityRecord& Record, const FLDCombatSourceRef& Source,
        const FLDCombatTrack* Track, int64 NowMs, TArray<FLDDamageFactor>& Out, FString& Error) const;
    // The factors a hit takes at resolution: its own, Y22's charge of its source, the target's vulnerability, K04's mark,
    // Y27 on a reaction and the strength keystone. OutTaken lists the K04 marks it consumed.
    bool HitFactors(FLDBodyCombatState* Source, FLDBodyCombatState& Target, const FLDCombatHit& Hit, int64 NowMs,
        TArray<FLDDamageFactor>& Out, TArray<FLDCombatSetupMark>& OutTaken, FString& Error);
    // K06: queue the repeats of a release that compiled AutoRecasts; RunAutoRecasts releases the due ones (Actions).
    bool ScheduleAutoRecasts(const FLDBodyCombatState& Body, const FLDCombatSourceRef& Source, const FLDCombatActiveTerms& Terms,
        const FLDCombatAction& Action, int64 NowMs, FString& Error);
    bool RunAutoRecasts(int64 NowMs, FString& Error);
    // A committed card-relevant hit on Target from Source (LDCombatCards.cpp): skill-hit triggers (K01/K02/K04/K17, Y16, A03),
    // basic triggers (A01/A02), heals (Y17/Y10/K15), Y01's reflection, Y22's reset, Y25's aura and Y03's stand.
    bool OnCardHit(const FLDCombatHit& Hit, FLDBodyCombatState& Target, FLDBodyCombatState* Source, const FLDDamageEvaluation& Damage,
        int64 NowMs, FString& Error);
    // A completed reaction: Y04 momentum, Y05's next element and K25's cut.
    bool OnCardReaction(const FLDCombatReactionFact& Fact, int64 NowMs, FString& Error);
    // A contender kill or assist credited to Entity: Y02's refresh, Y20's growth and K03's host refresh (a host whose native hit
    // reached Victim within WindowMs, the fatal one included).
    bool OnCardTakedown(const FGuid& Entity, bool bKill, const FLDBodyCombatState& Victim, int64 WindowMs, int64 NowMs, FString& Error);
    // Y21: a hard control actually landed on Victim; K05: one from a host skill arms its holder's next-skill bonus.
    bool OnHardControl(FLDBodyCombatState& Victim, const FLDControlApplication& App, int64 NowMs, FString& Error);
    // Issue #292 P5 cards: D04/D09 on a perfect dodge of Body; Y06 on a broken shield of Holder; Y29 a share of the burn damage
    // Source dealt as its shield; Y24's tally of a hit of Source on Target.
    bool OnPerfectDodgeCards(FLDBodyCombatState& Body, int64 NowMs, FString& Error);
    bool OnShieldBrokenCards(FLDBodyCombatState& Holder, int64 NowMs, FString& Error);
    bool OnBurnDealtCards(FLDBodyCombatState& Source, int64 BurnDamageP2, int64 NowMs, FString& Error);
    bool OnTallyHit(FLDBodyCombatState& Source, const FLDCombatHit& Hit, int64 NowMs, FString& Error);
    // Y11: Body's overdrive threshold and scale (OVERDRIVE.Threshold / OVERDRIVE.Scale unless an overdrive_short card replaces them).
    int64 OverdriveThresholdOf(const FLDBodyCombatState& Body) const;
    // Cue facts with the overdrive cast as it will resolve: an acting Q/W/E admitted as overdrive counts only while the
    // bar still holds this body's threshold (the release decides by the same LDMomentum::OverdriveReady); a released one
    // keeps the resolved value ReleaseAbility stored.
    FLDCombatCueFacts PresentedCueFactsOf(const FLDBodyCombatState& Body) const;
    int64 OverdriveScaleOf(const FLDBodyCombatState& Body) const;
    // D07: an innate dash ramming Target deals the card's damage and pushes it away.
    bool DashRamHit(FLDBodyCombatState& Body, const FLDCombatDashContact& Dash, const FLDCombatBodyRef& Target, FString& Error);
    // A08: a landed third basic step queues a free Q release toward its target at the card's share.
    bool QueueComboCast(FLDBodyCombatState& Body, const FLDCombatAction& Basic, const FLDCombatHit& Primary, int64 NowMs, FString& Error);
    // The grow cards' match growth as projection terms (Y19, Y20, Y21): the counters times the grow cards Projection holds,
    // sorted by stat. A projection without grow cards gives none.
    bool CardGrowthTerms(const FLDAttributeProjection& Projection, const FLDCardGrowth& Counters, TArray<FLDDemoStatBonus>& Out,
        FString& Error) const;
    // The grow cards' match growth of Entity as projection terms (Y19, Y20, Y21); reprojects a living body when it changed.
    bool RefreshCardGrowth(const FGuid& Entity, int64 NowMs, FString& Error);
    // Installs Growth as Entity's CardGrowth term: the record reprojects and a living body converts its HP maximum.
    bool InstallCardGrowth(const FGuid& Entity, TArray<FLDDemoStatBonus>&& Growth, int64 NowMs, FString& Error);
    // Y03: Body below its threshold starts its once-per-life stand; while it lasts every hostile hit deals one point.
    bool LastStandActive(FLDBodyCombatState& Body, int64 NowMs, FString& Error);
    // Y14: the overflow of a heal becomes a shield (capped, never weaker than the current one).
    bool OverhealShield(FLDBodyCombatState& Body, int64 OverflowP2, int64 NowMs, FString& Error);
    // D06: one basic hit of Body on Target outside the combo (no step, no empowered package), with its own crit sample.
    bool MakeBasicStrike(const FLDBodyCombatState& Body, const FLDCombatBodyRef& Target, FLDCombatHit& Out, FString& Error);
    // Chain guard (spec §3.8): whether Hit may derive another effect (its depth below CHAIN.MaxDepth); logs LD_CHAIN_GUARD.
    bool ChainAllows(const FLDCombatHit& Hit, const TCHAR* What) const;
    // Skill branches (issue #287 spec §5.1; LDCombatBranchDirector.cpp). The release copies its hit riders onto the cast ledger
    // and arms its self terms; every native direct hit applies the riders; AdvanceBranches runs follow-ups, pulses, dash-end
    // bursts and window expiries.
    bool ArmBranchRelease(ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDCombatSourceRef& Source,
        const FLDCombatActiveTerms& Terms, const FGuid& EffectId, const TSharedPtr<FLDCombatCastLedger>& CastLedger,
        const FLDCombatAction& Action, int64 NowMs, FString& Error);
    bool ArmBranchHostile(FLDBodyCombatState& Body, const FLDCombatActiveTerms& Terms, const FLDCombatHit& Template,
        const TArray<FLDCombatBodyRef>& AreaTargets, int64 NowMs, FString& Error);
    bool ApplyBranchRiders(const FLDCombatHit& Hit, FLDBodyCombatState& Source, FLDBodyCombatState& Target, int64 NowMs,
        FString& Error);
    int64 BranchVulnerableBp(const FLDBodyCombatState& Target, const FLDCombatHit& Hit, int64 NowMs) const;
    int64 BranchGuardBp(const FLDBodyCombatState& Target, int64 NowMs) const;
    // S02 B / S12 B: Source's recast window is open (no cooldown, no momentum); S05 B2: a free next cast.
    bool BranchRecastOpen(const FLDBodyCombatState& Body, const FLDCombatSourceRef& Source, int64 NowMs) const;
    bool BranchFreeCast(const FLDBodyCombatState& Body, const FLDCombatSourceRef& Source, int64 NowMs) const;
    // The recast's own terms over the release snapshot (damage share, control, extra damage, direction).
    void ApplyRecastTerms(const FLDCombatRecastWindow& Window, FLDCombatActiveTerms& Terms, FLDCombatAction& Delivery) const;
    bool AdvanceBranches(int64 NowMs, FString& Error);
    int64 NextBranchDueMs(int64 NowMs) const;
    void ClearBranchState(FLDBodyCombatState& Body);
    // Perfect dodge (issue #287 spec §4.4): at the resolution of a telegraphed enemy effect, a contender whose D started inside
    // InArea within MOMENTUM.PerfectDodgeWindowSec and whose foot is outside now gains MOMENTUM.PerfectDodge, once per effect.
    bool ResolvePerfectDodges(const FGuid& EffectId, const FGuid& SourceEntity, TFunctionRef<bool(const FVector&)> InArea, int64 NowMs,
        FString& Error);
    // Branch reactions of a committed hit on Target: S10 B1 discharges, S14 A1 reflections, S16 B stacks, S13 A1 bursts.
    // BrokenLayers: the shield layers (their EffectIds) this hit emptied; a branch shield settles by its own layer.
    bool OnBranchHitTaken(const FLDCombatHit& Hit, FLDBodyCombatState& Target, int64 DamageP2, const TArray<FGuid>& BrokenLayers,
        int64 NowMs, FString& Error);
    // A flight ended at EndCm: a return that reached its thrower was caught (S06 B2); an outbound flight leaves its landing area.
    bool EndBranchProjectile(const FLDCombatProjectile& Projectile, const FVector& EndCm, bool bReachedRange, FString& Error);
    // S19 A1: Ground's wire to its owner's next newer armed trap within WireCm strikes every enemy body on the segment once.
    bool StrikeTrapWire(FLDCombatGroundEffect& Ground, const FLDBodyCombatState& Holder, FString& Error);
    // S20 B2: a wet area for LingerMs at Centre, owned like the impact's payload.
    bool SpawnLingerZone(const FLDCombatHit& Payload, const FVector& Centre, double RadiusCm, int64 LingerMs, int64 NowMs, FString& Error);
    bool ScheduleEcho(const FLDCombatBodyRef& Owner, const FLDPassiveEffectRef& Echo, const FLDCombatGroundEffect& Blast,
        int64 NowMs, FString& Error);
    // M13: a return flight from where Outbound ended toward its living thrower's foot now.
    bool LaunchReturn(const FLDCombatProjectile& Outbound, const FVector& EndCm, const FGuid& EndBody, FString& Error);
    // K01/D02: a hit of the host skill (or the hosted dash's contact) cuts the other learned skills' remaining cooldowns, once
    // per release EffectId and copy.
    bool HasteOtherActives(FLDBodyCombatState& Holder, const FLDCombatSourceRef& Host, const FGuid& EffectId, int64 NowMs,
        FString& Error);
    // K02/K04/K17 and K01 on one hit of a host skill (A03 counts a third basic step as one hit of every skill).
    bool OnHostDirectHit(FLDBodyCombatState& Holder, const FLDCombatHit& Hit, const FLDCombatSourceRef& Host, int64 NowMs,
        FString& Error);
    // Y08 (any knockback of its holder), or a branch wall stun: arms the knockback watch of a push.
    bool WatchKnockbackImpact(ALDCharacter& Target, FLDBodyCombatState& Body, const FLDCombatHit& Hit,
        const FVector& Direction, int64 NowMs, FString& Error);
    // Y08: one watched push's first block: a static blocker stuns and hurts the pushed body; another legal enemy body takes the
    // same package as the pushed one; anything else (the holder, a protected or other-group body) answers nothing.
    bool ResolveKnockbackImpact(FLDBodyCombatState& Body, const FLDCombatKnockbackImpact& Impact, bool bBody,
        const ALDCharacter* BlockerActor, int64 NowMs, FString& Error);
    // Y08 watches and D05/K06 schedules due by NowMs (Effects).
    bool AdvanceCards(int64 NowMs, FString& Error);
    bool EmitZonePulse(const FLDCombatZone& Zone, FString& Error);
    int64 NextCardsDueMs(int64 NowMs) const;

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
    // innate dash, compiled with the record's installed cards (LDCombatCards.cpp); false without one.
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
    // damage taken, own shield absorption, HP lost and what the victim's own cuts (S16 stance) took off,
    // to the victim contender's current version.
    void LedgerHitCommitted(const FLDCombatHit& Hit, const FLDBodyCombatState& Victim, const FLDDamageEvaluation& Damage, ELDDemoDamageType EffectiveType);
    // HP a one-time heal actually restored to a contender body, and a shield granted to it.
    void LedgerHealed(const FLDBodyCombatState& Body, int64 HealedP2);
    void LedgerShieldGranted(const FLDBodyCombatState& Body, int64 AmountP2,const FLDCombatSourceRef& Source={});
    // HP natural regeneration actually restored.
    void LedgerRegenerated(const FLDBodyCombatState& Body, int64 RestoredP2);
    // One proc of an installed card of Holder that committed a real result (a hit, heal, shield, momentum, status, cut or
    // change of an outcome); never a passive stat term, a candidate or a refused proc.
    void LedgerCardTriggered(const FLDBodyCombatState& Holder, FName Card);
    // A real combat event of Body at NowMs: refreshes the momentum engagement and opens a new fight after an idle gap.
    void MarkEngaged(FLDBodyCombatState& Body, int64 NowMs) const;
    // #305: Y22 and K16 count idle time only inside the current fight plus CARDS.IdleCarrySec carried in from before it; the
    // earliest instant their idle may count from.
    int64 IdleCountFloorMs(const FLDBodyCombatState& Body, int64 NowMs) const;
    // One own combat action request of Entity that Combat refused for a gameplay gate (busy, cooldown, attack cadence,
    // momentum, control, target, direction, range or sight); protocol, staleness and phase refusals are not counted.
    void LedgerActionRefused(const FGuid& Entity, ELDCombatReject Reason);

    TMap<TWeakObjectPtr<ALDCharacter>,FLDBodyCombatState> Bodies;
    TMap<FGuid,FLDCombatEntityRecord> Records;
    TArray<FLDCombatHit> PendingHits;
    TArray<FLDCombatProjectile> Projectiles;
    TArray<FLDCombatZone> Zones;
    TArray<FLDCombatContactMotion> ContactMotion;
    TArray<FLDCombatChannel> Channels;
    TArray<FLDCombatFan> Fans;
    TArray<FLDCombatBag> Bags;
    TArray<FLDCombatGroundEffect> GroundEffects;
    struct FLDCombatSlipperDrop
    {
        FGuid Id;
        FGuid OwnerEntity;
        FLDCombatBodyRef OwnerBody;
        FName SkillId;
        FVector FootCm=FVector::ZeroVector;
        double RadiusCm=110;
        int64 ExpiresAtMs=0;
        FLDDemoEffectDefinition Terms;
    };
    struct FLDCombatCordon
    {
        FGuid Id;
        FLDCombatHit Payload;
        FLDDemoEffectDefinition Terms;
        FVector CentreCm=FVector::ZeroVector;
        FVector Tangent=FVector(0,1,0);
        double HalfLengthCm=250, HalfWidthCm=70;
        int64 ExpiresAtMs=0;
        FGuid OwnerGroup;
        uint8 OwnerPKSide=0;
        TMap<FGuid,int32> EnemyTouches;
        TMap<FGuid,int64> EnemyCooldownUntilMs;
        TSet<FGuid> BoostedFlights;
        uint64 CreatedSerial=0;
    };
    struct FLDCombatDecoy
    {
        FGuid Id;
        FLDCombatBodyRef OwnerBody;
        FGuid OwnerEntity;
        FGuid CreditOwner;
        FLDCombatSourceRef Source;
        FVector FootCm=FVector::ZeroVector;
        double RadiusCm=240;
        int64 ExplodeAtMs=0;
        FLDDemoEffectDefinition Terms;
        int64 SourceADP2=0, SourceAPP2=0, SourceBonusADP2=0;
    };
    struct FLDCombatShardBurst
    {
        FGuid Id;
        FGuid OwnerEntity;
        FVector FootCm=FVector::ZeroVector;
        double RadiusCm=260;
        int64 ExpiresAtMs=0;
    };
    TArray<FLDCombatSlipperDrop> SlipperDrops;
    TArray<FLDCombatCordon> Cordons;
    TArray<FLDCombatDecoy> Decoys;
    TArray<FLDCombatShardBurst> ShardBursts;
    // Skill branch schedules (LDCombatBranchDirector.cpp).
    TArray<FLDCombatFollowUp> FollowUps;
    TArray<FLDCombatPulse> Pulses;
    // (effect, dodger) pairs already judged a perfect dodge, with the instant; pruned after the window.
    TArray<TTuple<FGuid,FGuid,int64>> PerfectDodgeMarks;
    TArray<FLDMonsterStrike> MonsterStrikes;
    // Creation order of zones and traps.
    uint64 NextAreaSerial=0;
    // K06 repeats waiting for their instant.
    TArray<FLDCombatAutoRecast> AutoRecasts;
    // Spec §6.5 chains (LDCombatChains.cpp): the open chains by ChainId, the release effects that map to them, each owner's
    // ended chains and highlights, and the chain current while an action releases or a hit of it resolves.
    struct FChainTrack
    {
        FGuid Owner;
        TArray<FName> Steps;
        int64 DamageP2=0;
        int64 LastStepMs=0;
        TArray<FGuid> Effects;
    };
    struct FChainOwner
    {
        uint32 NextSerial=1;
        TArray<FLDChainEndView> Recent;
        TArray<int32> Logged;
        FLDResultHighlight Best;
    };
    TMap<FGuid,FChainTrack> OpenChains;
    TMap<FGuid,FGuid> EffectChains;
    TMap<FGuid,FChainOwner> ChainOwners;
    FGuid CurrentChain;
    // The chain the last successful ReleaseAbility belonged to, so a card step recorded after it (K06, A08) joins it.
    FGuid LastReleaseChain;
    // A contender's action (EffectId) opens a chain or continues Inherit while it is open; none for camp members and dummies.
    FGuid OpenChain(const FLDBodyCombatState& Body, const FGuid& EffectId, const FGuid& Inherit, int64 NowMs);
    FGuid ChainOfHit(const FLDCombatHit& Hit) const;
    // One step of the current chain when Entity owns it.
    void ChainStep(const FGuid& Entity, FName Kind, int64 NowMs);
    // Damage of a committed hit: its chain's total and its owner's strongest hit on a contender.
    void ChainDamage(const FLDCombatHit& Hit, const FLDBodyCombatState& Victim, int64 DamageP2);
    // Ends every chain CHAIN.EndSec past its last step (every open chain with bAll): the counted ones reach their owner and
    // the longest the log.
    bool AdvanceChains(int64 NowMs, FString& Error, bool bAll=false);
    // Spec §3.8: effects derived in the current authority step (LD_CHAIN_GUARD reason=Step past CHAIN.MaxEffectsPerStep).
    int32 StepDerivedEffects=0;
    // #198 elements: fogs (one per owner), S10 chains and the reactions completed in the current Effects step.
    TArray<FLDCombatFog> Fogs;
    TArray<FLDCombatChain> Chains;
    // Spec §8.3 live ground states (at most GROUND.MaxTotal) and the next public identity.
    TArray<FLDGroundState> GroundStates;
    uint32 NextGroundId=0;
    TArray<FLDCombatReactionFact> ReactionFacts;
    // The reaction assist prompts sent in the current Effects step: the reaction (its hits' EffectId), the setter and the
    // prompt as sent; one per reaction and setter.
    struct FReactionAssistSent
    {
        FGuid ReactionId;
        FGuid Setter;
        FLDReactionAssistView View;
    };
    TArray<FReactionAssistSent> ReactionAssists;
    TArray<FLDCommittedDeath> PendingDeaths;
    // Committed positive contacts against camp members, delivered to the camp owner within the same Effects.
    TArray<FLDCombatHostileContact> PendingContacts;
    TArray<FLDCommittedDeath> RetryRewards;
    FRandomStream CritRandom;
    bool bAdvancing=false;
    FLDBuildLedger BuildLedger;
    // Per contender: the match total of committed damage credited to it, one row per source.
    TMap<FGuid,TArray<FLDDamageLine>> DamageTotals;
    struct FReportCounters
    {
        int64 PhysicalToPlayersP2=0,MagicalToPlayersP2=0,ReactionToPlayersP2=0;
        int64 RecoveryPackP2=0,Reactions=0,PeakReactionP2=0;
        TMap<FName,int64> CardShields;
    };
    TMap<FGuid,FReportCounters> ReportCounters;
    TMap<FGuid,int64> BoundaryReactionDamage;
    // Per contender: how often each card triggered in this match (training excluded).
    TMap<FGuid,TMap<FName,int32>> CardTriggerCounts;
    // Per contender: the running training combo on a dummy, its window and the previous combo's baseline (#331).
    struct FTrainingCombo
    {
        FGuid DummyId;
        TArray<FLDDamageLine> Lines;
        int64 TotalP2=0;
        int64 PreviousTotalP2=0;
        int64 FirstAtMs=0;
        int64 LastAtMs=0;
        bool bLiveDirty=false;
        bool bFinishPending=false;
    };
    TMap<FGuid,FTrainingCombo> TrainingCombos;
};
