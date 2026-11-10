#pragma once

#include "CoreMinimal.h"
#include "Development/LDDemoContentIds.h"
#include "LDDemoContentDefinitions.generated.h"

USTRUCT()
struct LOCKDOWN_API FLDDemoContentDigest
{
    GENERATED_BODY()

    UPROPERTY() uint8 Bytes[32] = {};

    static bool ParseHex(const FString& Hex, FLDDemoContentDigest& Out, FString& Error);
    FString ToHex() const;
    bool operator==(const FLDDemoContentDigest& Other) const;
    bool operator!=(const FLDDemoContentDigest& Other) const { return !(*this == Other); }
};

USTRUCT()
struct FLDDemoRational
{
    GENERATED_BODY()

    UPROPERTY() int64 Numerator = 0;
    UPROPERTY() int64 Denominator = 1;
};

USTRUCT()
struct FLDDemoP2Bounds
{
    GENERATED_BODY()

    UPROPERTY() int64 BaseP2 = 0;
    UPROPERTY() int64 PerLevelP2 = 0;
    UPROPERTY() int64 MinP2 = 0;
    UPROPERTY() int64 MaxP2 = 0;
};

USTRUCT()
struct FLDDemoRateBounds
{
    GENERATED_BODY()

    UPROPERTY() FLDDemoRational Base;
    UPROPERTY() FLDDemoRational PerLevel;
    UPROPERTY() FLDDemoRational Min;
    UPROPERTY() FLDDemoRational Max;
};

USTRUCT()
struct FLDDemoRationalBounds
{
    GENERATED_BODY()

    UPROPERTY() FLDDemoRational Base;
    UPROPERTY() FLDDemoRational Min;
    UPROPERTY() FLDDemoRational Max;
};

USTRUCT()
struct FLDDemoAIParameters
{
    GENERATED_BODY()

    UPROPERTY() int64 RetreatHPBp = 0;
    UPROPERTY() int64 ResumeFarmHPBp = 0;
    UPROPERTY() int64 HighCampEntryHPBp = 0;
    UPROPERTY() int64 HighCampReserveHPBp = 0;
    UPROPERTY() int64 LowCampReserveHPBp = 0;
    UPROPERTY() int64 CaptureReactionBudgetMs = 0;
    UPROPERTY() int64 DecisionIntervalMs = 0;
    UPROPERTY() int64 RetryMaxAttempts = 0;
    UPROPERTY() int64 RetryInitialBackoffMs = 0;
    UPROPERTY() int64 RetryMaxBackoffMs = 0;
    UPROPERTY() int64 ExploreRevisitIntervalMs = 0;
    UPROPERTY() int64 EngagementBudgetMs = 0;
};

USTRUCT()
struct FLDDemoContentParameters
{
    GENERATED_BODY()

    UPROPERTY() FLDDemoP2Bounds MaxHP;
    UPROPERTY() FLDDemoP2Bounds MaxMP;
    UPROPERTY() FLDDemoP2Bounds AD;
    UPROPERTY() FLDDemoP2Bounds AP;
    UPROPERTY() FLDDemoP2Bounds Armor;
    UPROPERTY() FLDDemoP2Bounds MagicResist;
    UPROPERTY() FLDDemoRateBounds HPRegen;
    UPROPERTY() FLDDemoRateBounds MPRegen;
    UPROPERTY() FLDDemoRationalBounds AttackSpeed;
    UPROPERTY() int64 AttackSpeedPerLevelBp = 0;
    UPROPERTY() int64 CritChanceBaseBp = 0;
    UPROPERTY() int64 CritMultiplierBp = 0;
    UPROPERTY() int64 CooldownHasteBaseP2 = 0;
    // #166/#198: G(L) = 1 + SkillGrowthPerLevelBp x min(L-1, 9) + SkillGrowthPerLevelAfter10Bp x max(L-10, 0).
    UPROPERTY() int64 SkillGrowthPerLevelBp = 0;
    UPROPERTY() int64 SkillGrowthPerLevelAfter10Bp = 0;
    UPROPERTY() int64 CooldownHasteMaxP2 = 0;
    UPROPERTY() FLDDemoRationalBounds MoveSpeedCmPerSec;
    UPROPERTY() FLDDemoRational CollisionRadiusCm;
    UPROPERTY() FLDDemoRationalBounds AttackRangeCm;
    UPROPERTY() int64 BasicAttackWindupMs = 0;
    UPROPERTY() FLDDemoRational VisionRadiusCm;
    UPROPERTY() FLDDemoRational CaptureRadiusCm;
    UPROPERTY() int64 CaptureSprayMs = 0;
    UPROPERTY() int64 CaptureGuardMs = 0;
    // #153: the opening countdown between the initial spawn and the match's T=0; every match timer counts from T=0.
    UPROPERTY() int64 CountdownMs = 0;
    UPROPERTY() int64 DevelopmentMs = 0;
    UPROPERTY() int64 RespawnMs = 0;
    UPROPERTY() int64 ProtectionMs = 0;
    // D21: every contender's recovery, a per-second share of its current maxima once this long without combat.
    UPROPERTY() int64 OutOfCombatRegenDelayMs = 0;
    UPROPERTY() int64 OutOfCombatMaxHPRegenBp = 0;
    UPROPERTY() int64 OutOfCombatMaxMPRegenBp = 0;
    // D21 (#79): fast recovery for a contender whose foot stands inside a public dorm disk once this long without combat.
    UPROPERTY() FLDDemoRational DormRadiusCm;
    UPROPERTY() int64 DormRegenDelayMs = 0;
    UPROPERTY() int64 DormMaxHPRegenBp = 0;
    UPROPERTY() int64 DormMaxMPRegenBp = 0;
    UPROPERTY() FLDDemoRational CampResetArrivalRadiusCm;
    // #28: a Returning member with this many consecutive refused path plans, or this long without path progress
    // while free to move, resets where it stands instead of retrying forever.
    UPROPERTY() int64 CampReturnDeadEndPlans = 0;
    UPROPERTY() int64 CampReturnDeadEndMs = 0;
    // #97 camp growth (Development/LDCampGrowth.h): a camp life spawned t after T=0 takes stage
    // min(CampGrowthMaxStage, floor(t / CampGrowthStageMs)); each stage adds these shares of the base values.
    UPROPERTY() int64 CampGrowthStageMs = 0;
    UPROPERTY() int64 CampGrowthMaxStage = 0;
    UPROPERTY() int64 CampGrowthHPPerStageBp = 0;
    UPROPERTY() int64 CampGrowthAttackPerStageBp = 0;
    UPROPERTY() int64 CampGrowthResistPerStageP2 = 0;
    UPROPERTY() int64 CampGrowthRewardXPPerStageBp = 0;
    // D26 (#79) airdrop: landings at FirstLand + n * Interval from the match start, each public AnnounceLead earlier.
    UPROPERTY() int64 AirdropFirstLandMs = 0;
    UPROPERTY() int64 AirdropIntervalMs = 0;
    UPROPERTY() int64 AirdropAnnounceLeadMs = 0;
    UPROPERTY() FLDDemoRational AirdropPickupRadiusCm;
    UPROPERTY() int64 AirdropRewardXP = 0;
    // D27 (#79) 上热搜: the leader is named every LeaderRefreshMs; see World/LDLeaderNotice.h.
    UPROPERTY() int64 LeaderMinLevel = 0;
    UPROPERTY() int64 LeaderRefreshMs = 0;
    UPROPERTY() FLDDemoRational LeaderCircleRadiusCm;
    UPROPERTY() FLDDemoRational LeaderCircleMaxOffsetCm;
    // #147 build v7 keywords (one shared record per target): breach takes BreachPerStack off armor and magic resist
    // per stack, up to BreachMaxStacks, for BreachMs refreshed whole by every new stack; burn deals BurnPerSec plus
    // BurnAPRatio x the applier's AP per second per layer (snapshotted per layer), every BurnTickMs from the layer's own
    // application for BurnMs, up to BurnMaxStacks layers (#198). Every lifesteal source of one hit adds up to at most LifestealCapBp.
    UPROPERTY() int64 BreachPerStackP2 = 0;
    UPROPERTY() int64 BreachMaxStacks = 0;
    UPROPERTY() int64 BreachMs = 0;
    UPROPERTY() int64 BurnPerSecP2 = 0;
    UPROPERTY() int64 BurnAPRatioBp = 0;
    UPROPERTY() int64 BurnTickMs = 0;
    UPROPERTY() int64 BurnMs = 0;
    UPROPERTY() int64 BurnMaxStacks = 0;
    UPROPERTY() int64 LifestealCapBp = 0;
    // #198 elements (CONTENT_AND_BALANCE §4.4, LDCombatElements): one element per target. Burn layers each last BurnMs
    // from their own application and tick every BurnTickMs. Wet lasts WetMs and slows by WetSlowBp; electrified lasts
    // ElectrifiedMs and, on later positive damage, discharges R to the nearest other enemy within DischargeRadiusCm at
    // most once per DischargeIntervalMs per target. R = ReactionBase + ReactionPerLevel x (L - 1) + ReactionAPRatio x AP
    // of the element's applier. Fog (fire + water): a FogRadiusCm zone for FogMs giving wet every FogIntervalMs to
    // element-free enemies; conduct (water + electric): R within ConductRadiusCm to wet enemies, ConductSlowBp for
    // ConductSlowMs; overload (fire + electric): R within OverloadRadiusCm and OverloadBreachStacks of breach.
    UPROPERTY() int64 WetMs = 0;
    UPROPERTY() int64 WetSlowBp = 0;
    UPROPERTY() int64 ElectrifiedMs = 0;
    UPROPERTY() FLDDemoRational DischargeRadiusCm;
    UPROPERTY() int64 DischargeIntervalMs = 0;
    UPROPERTY() int64 ReactionBaseP2 = 0;
    UPROPERTY() int64 ReactionPerLevelP2 = 0;
    UPROPERTY() int64 ReactionAPRatioBp = 0;
    UPROPERTY() FLDDemoRational FogRadiusCm;
    UPROPERTY() int64 FogMs = 0;
    UPROPERTY() int64 FogIntervalMs = 0;
    UPROPERTY() FLDDemoRational ConductRadiusCm;
    UPROPERTY() int64 ConductSlowBp = 0;
    UPROPERTY() int64 ConductSlowMs = 0;
    UPROPERTY() FLDDemoRational OverloadRadiusCm;
    UPROPERTY() int64 OverloadBreachStacks = 0;
    UPROPERTY() int64 XPBaseToNext = 0;
    UPROPERTY() int64 XPIncreasePerLevel = 0;
    UPROPERTY() int64 PlayerXPBase = 0;
    UPROPERTY() int64 PlayerXPPerLevel = 0;
    // Contender kills only: a contribution within this window before the death earns an assist; the killer and the
    // assisters split the kill XP.
    UPROPERTY() int64 AssistWindowMs = 0;
    // #198 points: OpeningPoints at BeginMatch, PointsPerLevel for every level crossed up to the cap, then MaxLevelPoints
    // for every full MaxLevelPointsXP past the top threshold. A build change commits only BuildSafeWindowMs after the
    // contender's last combat action (GROWTH.BuildSafeWindowSec).
    UPROPERTY() int64 OpeningPoints = 0;
    UPROPERTY() int64 PointsPerLevel = 0;
    UPROPERTY() int64 MaxLevelPointsXP = 0;
    UPROPERTY() int64 MaxLevelPoints = 0;
    UPROPERTY() int64 BuildSafeWindowMs = 0;
    UPROPERTY() FLDDemoAIParameters AI;
};

UENUM()
enum class ELDDemoFormula : uint8
{
    None, AddFlatThenAdditivePercent, PointsOverScalePlusPoints, OncePerPrimaryAttack,
    FloorP2ThenShieldThenHP, BaseTimesAdditiveRatio, BasePlusFlat, CeilReciprocalMs,
    CeilBaseWindupTimesBaseOverCurrentAS, CeilBaseTimesScaleOverScalePlusHaste,
    CarrySubP2Remainder, FloorFinalRatioRejectAliveUnderflow,
    LinearIncrement, MinVisionTwiceAttackRange
};

USTRUCT()
struct FLDDemoFormulas
{
    GENERATED_BODY()

    UPROPERTY() ELDDemoFormula StatProjection = ELDDemoFormula::None;
    UPROPERTY() ELDDemoFormula Resistance = ELDDemoFormula::None;
    UPROPERTY() ELDDemoFormula Crit = ELDDemoFormula::None;
    UPROPERTY() ELDDemoFormula Damage = ELDDemoFormula::None;
    UPROPERTY() ELDDemoFormula AttackSpeed = ELDDemoFormula::None;
    UPROPERTY() ELDDemoFormula MoveSpeed = ELDDemoFormula::None;
    UPROPERTY() ELDDemoFormula AttackRange = ELDDemoFormula::None;
    UPROPERTY() ELDDemoFormula AttackMoveAcquireRadius = ELDDemoFormula::None;
    UPROPERTY() ELDDemoFormula AttackIntervalMs = ELDDemoFormula::None;
    UPROPERTY() ELDDemoFormula AttackWindupMs = ELDDemoFormula::None;
    UPROPERTY() ELDDemoFormula CooldownMs = ELDDemoFormula::None;
    UPROPERTY() ELDDemoFormula Regen = ELDDemoFormula::None;
    UPROPERTY() ELDDemoFormula ResourceMaxChange = ELDDemoFormula::None;
    UPROPERTY() ELDDemoFormula XPToNext = ELDDemoFormula::None;
    UPROPERTY() int64 ResistanceScaleP2 = 0;
    UPROPERTY() int64 HasteScaleP2 = 0;
};

UENUM()
enum class ELDDemoPolicy : uint8
{
    None, AddFlatThenAdditivePercent, Allowed,
    ContinueServerTime, PreserveRatio, ContinueForRemainingContender, CancelUnsettledNoRefund,
    BreakOnAggressiveIntent, OncePerAttackId, PrimaryBasicAttackHPLost, Disabled,
    CarrySubP2Remainder, ReplaceIfNotWeaker, ConfirmedCombatHit, ReturnThenReset, NotApplicable
};

UENUM()
enum class ELDDemoRootBlock : uint8 { Move, Dash };

USTRUCT()
struct FLDDemoPolicies
{
    GENERATED_BODY()

    UPROPERTY() ELDDemoPolicy StatStacking = ELDDemoPolicy::None;
    UPROPERTY() ELDDemoPolicy MissingSourceCooldown = ELDDemoPolicy::None;
    UPROPERTY() ELDDemoPolicy DeadChoiceCommit = ELDDemoPolicy::None;
    UPROPERTY() ELDDemoPolicy ResourceMaxChange = ELDDemoPolicy::None;
    UPROPERTY() ELDDemoPolicy ReleasedEffectSourceLoss = ELDDemoPolicy::None;
    UPROPERTY() ELDDemoPolicy SourceLeave = ELDDemoPolicy::None;
    UPROPERTY() ELDDemoPolicy InvalidAggressionProtection = ELDDemoPolicy::None;
    UPROPERTY() ELDDemoPolicy CritSampling = ELDDemoPolicy::None;
    UPROPERTY() ELDDemoPolicy LifestealBasis = ELDDemoPolicy::None;
    UPROPERTY() ELDDemoPolicy LifestealRecursiveTriggers = ELDDemoPolicy::None;
    UPROPERTY() ELDDemoPolicy BasicSecondaryTriggers = ELDDemoPolicy::None;
    UPROPERTY() ELDDemoPolicy RegenIntegration = ELDDemoPolicy::None;
    UPROPERTY() ELDDemoPolicy ShieldPolicy = ELDDemoPolicy::None;
    UPROPERTY() TArray<ELDDemoRootBlock> RootBlocks;
    UPROPERTY() ELDDemoPolicy CampAggroTrigger = ELDDemoPolicy::None;
    UPROPERTY() bool CampGroupRetaliation = false;
    UPROPERTY() bool CampReturningDamageable = false;
    UPROPERTY() bool CampReturningControllable = false;
    UPROPERTY() ELDDemoPolicy CampResetPolicy = ELDDemoPolicy::None;
    UPROPERTY() ELDDemoPolicy CampManaPolicy = ELDDemoPolicy::None;
};

// #198: DirectionLine (S09) is a straight band of RangeCm x 2 LineHalfWidthCm in the requested direction; appended last.
UENUM()
enum class ELDDemoTargetMode : uint8 { Self, Unit, DirectionProjectile, DirectionDash, GroundPoint, DirectionCone, DirectionLine };

// When a triggered effect fires: cooldown_refund on a landed primary basic (BasicHit), mana_on_hit on an ability hit
// (AbilityHit), triggered_shield when the holder took positive damage (Damaged).
UENUM()
enum class ELDDemoEffectTrigger : uint8 { None, BasicHit, AbilityHit, Damaged };

// Which cooldown tracks a cooldown_refund shortens: all learned actives (Q/W/E).
UENUM()
enum class ELDDemoCooldownEntries : uint8 { None, Actives };

UENUM()
enum class ELDDemoDamageType : uint8 { Physical, Magical };

UENUM()
// Boss is the D26 画饼老板's monster row: one roaming body, never placed in a camp. Elite (#97) is the third camp tier,
// appended after Boss so existing values keep their numbers.
enum class ELDDemoCampTier : uint8 { Low, High, Boss, Elite };

UENUM()
enum class ELDDemoBonusStat : uint8
{
    ADFlat, APFlat, MaxHPFlat, MaxMPFlat, ArmorFlat, MagicResistFlat, CooldownHasteFlat,
    AttackRangeCmFlat, AttackSpeedBonusRatio, MoveSpeedBonusRatio, CritChanceBonusRatio,
    // Percent of the projected AD; only conditional and timed bonuses may use it (floored to P2 when applied).
    ADBonusRatio,
    // Always-on HP regeneration in hp/s (an exact rational, stored in FLDDemoStatBonus::Cm like the range).
    HPRegenFlat,
    // Percent of the projected AP; timed bonuses only (the D26 Boss approval buff), floored to P2 when applied.
    APBonusRatio,
    // Always-on MP regeneration in mp/s (an exact rational in FLDDemoStatBonus::Cm, like HPRegenFlat).
    MPRegenFlat
};

USTRUCT()
struct FLDDemoStatBonus
{
    GENERATED_BODY()

    UPROPERTY() ELDDemoBonusStat Stat = ELDDemoBonusStat::ADFlat;
    // Stat fixes the unit: P2 points/resources or R4 basis points; the rational stats (range in cm, HP regeneration in hp/s)
    // use Cm and leave Amount 0.
    UPROPERTY() int64 Amount = 0;
    UPROPERTY() FLDDemoRational Cm;
};

USTRUCT()
struct FLDDemoTargetDefinition
{
    GENERATED_BODY()

    UPROPERTY() ELDDemoTargetMode Mode = ELDDemoTargetMode::Self;
    UPROPERTY() FLDDemoRational RangeCm;
    UPROPERTY() FLDDemoRational ProjectileRadiusCm;
    UPROPERTY() FLDDemoRational ProjectileSpeedCmPerSec;
    UPROPERTY() FLDDemoRational AngleDegrees;
    // Share of the caster's extra basic-attack range (current minus base) added to this cast range, up to
    // AttackRangeBonusCapCm; 0 = none.
    UPROPERTY() int64 AttackRangeRatioBp = 0;
    UPROPERTY() FLDDemoRational AttackRangeBonusCapCm;
    // DirectionLine only: half the band width.
    UPROPERTY() FLDDemoRational LineHalfWidthCm;
};

USTRUCT()
struct FLDDemoEffectDefinition
{
    GENERATED_BODY()

    // Opcode is checked against ELDDemoEffectOp; reservations are not battle handlers.
    // Field use per op: Slow RatioBp+DurationMs; TimedStatBonus Stats+DurationMs; SelfArea RadiusCm;
    // LowHpStatBonus RatioBp (HP threshold)+Stats; ControlDamageAmp RatioBp.
    UPROPERTY() uint8 Opcode = 0;
    UPROPERTY() int64 BaseP2 = 0;
    UPROPERTY() int64 ADRatioBp = 0;
    UPROPERTY() int64 APRatioBp = 0;
    // Damage only: coefficient of the caster's own max HP, snapshotted at release.
    UPROPERTY() int64 MaxHPRatioBp = 0;
    UPROPERTY() ELDDemoDamageType DamageType = ELDDemoDamageType::Physical;
    UPROPERTY() int64 DurationMs = 0;
    UPROPERTY() int64 DelayMs = 0;
    UPROPERTY() int64 RatioBp = 0;
    UPROPERTY() FLDDemoRational DistanceCm;
    UPROPERTY() FLDDemoRational RadiusCm;
    UPROPERTY() TArray<FLDDemoStatBonus> Stats;
    // Thorns: armor scaling of the reflection, per-attacker repeat window and critical-hit multiplier; it answers
    // primary basic hits from any side.
    UPROPERTY() int64 ArmorRatioBp = 0;
    UPROPERTY() int64 IntervalMs = 0;
    UPROPERTY() int64 CriticalMultiplierBp = 0;
    // Generic trigger/entry selectors of triggered effects (cooldown_refund, triggered_shield, mana_on_hit).
    UPROPERTY() ELDDemoEffectTrigger Trigger = ELDDemoEffectTrigger::None;
    UPROPERTY() ELDDemoCooldownEntries Entries = ELDDemoCooldownEntries::None;
    // cooldown_refund: the remaining-cooldown reduction.
    UPROPERTY() int64 AmountMs = 0;
    // lifesteal: multiplier against a low-HP target.
    UPROPERTY() int64 MultiplierBp = 0;
    // nth_basic_bonus: every Count-th landed basic; zone: its pulse count; empower_basic: its charges.
    UPROPERTY() int32 Count = 0;
    // #147 breach/burn: Count is the stacks one hit of an active applies, or a passive applies per Q/W hit;
    // BasicCount the stacks a passive applies per primary basic hit.
    UPROPERTY() int32 BasicCount = 0;
    // #112 handlers (LDCombatMechanics): a second ratio and duration (an attached slow).
    UPROPERTY() int64 SecondaryRatioBp = 0;
    UPROPERTY() int64 SecondaryDurationMs = 0;
    // dash_contact_refund caps actual mana grants; this cap never grows with level. brace: the record cap (grows with G).
    UPROPERTY() int64 LimitP2 = 0;
    // dash_contact: every body the dash actually touches takes the native hit once (otherwise only the first Count).
    UPROPERTY() bool bEveryContact = false;
    // #147/#198 field use (docs/systems/COMBAT_AND_GROWTH.md): silence DurationMs; breach/burn Count+BasicCount;
    // zone Count pulses at DelayMs + k x IntervalMs inside DurationMs; zone_slow RatioBp every IntervalMs for DurationMs;
    // zone_burn Count+IntervalMs; knockback DistanceCm+DurationMs; channel_cone SecondaryRatioBp+SecondaryDurationMs (a
    // slow on the last wave only); empower_basic Count (charges), Stats (while charged);
    // triggered_shield Damaged IntervalMs, SecondaryRatioBp+RadiusCm (burst on break); thorns SecondaryRatioBp+
    // SecondaryDurationMs (move-only slow); control_damage_amp SecondaryRatioBp (low-HP targets count too); lifesteal
    // SecondaryRatioBp+MultiplierBp (multiplier against low-HP targets); low_hp_stat_bonus SecondaryRatioBp (basic lifesteal).
    // #198 session A: wet/electrify/zone_wet no fields (terms from the COMBAT element parameters); chain_bounce Count
    // (extra targets) RadiusCm RatioBp (kept share per bounce) IntervalMs; brace DurationMs RatioBp (damage cut) LimitP2
    // (record cap) RadiusCm BaseP2 (answer base) SecondaryRatioBp (record share); zone_haste RatioBp (owner move speed
    // inside); grievous RatioBp (cut of new heals and shields) DurationMs.

    ELDDemoEffectOp GetOp() const { return static_cast<ELDDemoEffectOp>(Opcode); }
};

// #198: the one value set of an active (no ranks); its fixed amounts grow with the owner's level (LDSkillGrowth).
USTRUCT()
struct FLDDemoAbilityValues
{
    GENERATED_BODY()

    UPROPERTY() int64 ManaCostP2 = 0;
    UPROPERTY() int64 CooldownMs = 0;
    UPROPERTY() TArray<FLDDemoEffectDefinition> Effects;
};

// #198: every ability is a single-level active learned into Q/W/E.
USTRUCT()
struct FLDDemoAbilityDefinition
{
    GENERATED_BODY()

    UPROPERTY() FString Id;
    UPROPERTY() uint8 NativeId = 0;
    // True puts the active in the opening opportunity's pool; every active may be drawn by a later opportunity.
    UPROPERTY() bool Opening = false;
    UPROPERTY() FLDDemoTargetDefinition Target;
    UPROPERTY() int64 WindupMs = 0;
    UPROPERTY() FString CooldownFamily;
    UPROPERTY() FLDDemoAbilityValues Values;

    ELDDemoAbilityId GetNativeId() const { return static_cast<ELDDemoAbilityId>(NativeId); }
};

// #198: one of the five point-bought stats; each allocated point adds PerPoint (flat and additive percent terms).
USTRUCT()
struct FLDDemoStatDefinition
{
    GENERATED_BODY()

    UPROPERTY() FString Id;
    UPROPERTY() uint8 NativeId = 0;
    UPROPERTY() TArray<FLDDemoStatBonus> PerPoint;

    ELDDemoStatId GetNativeId() const { return static_cast<ELDDemoStatId>(NativeId); }
};

USTRUCT()
struct FLDDemoDashDefinition
{
    GENERATED_BODY()

    UPROPERTY() FLDDemoTargetDefinition Target;
    UPROPERTY() int64 WindupMs = 0;
    UPROPERTY() FString CooldownFamily;
    UPROPERTY() FLDDemoAbilityValues Values;
};

USTRUCT()
struct FLDDemoCharacterDefinition
{
    GENERATED_BODY()

    UPROPERTY() FString Id;
    UPROPERTY() uint8 NativeId = 0;
    UPROPERTY() ELDDemoTargetMode BasicTarget = ELDDemoTargetMode::Unit;
    UPROPERTY() ELDDemoDamageType BasicDamageType = ELDDemoDamageType::Physical;
    UPROPERTY() int64 BasicADRatioBp = 0;
    UPROPERTY() int64 BasicIntervalMs = 0;
    // D19 innate D dash of every contender, projected into the active shape Combat executes: DirectionDash target,
    // no windup, one value set with the approved mana cost, cooldown and a single Dash effect.
    UPROPERTY() FLDDemoDashDefinition Dash;
};

// D26 (#79) 画饼老板 Boss: the boss-only rules. Its body stats are the Monsters row with Id and Tier Boss (aggro radius as
// its perception radius), so Combat, vision and rewards find it like any monster.
USTRUCT()
struct FLDDemoBossDefinition
{
    GENERATED_BODY()

    UPROPERTY() FString Id;
    UPROPERTY() int64 AppearAtMs = 0;
    UPROPERTY() FLDDemoRational AggroRadiusCm;
    UPROPERTY() int64 AttackerMemoryMs = 0;
    UPROPERTY() int64 ApprovalDurationMs = 0;
    UPROPERTY() TArray<FLDDemoStatBonus> ApprovalStats;
};

USTRUCT()
struct FLDDemoMonsterDefinition
{
    GENERATED_BODY()

    UPROPERTY() FString Id;
    UPROPERTY() uint8 NativeId = 0;
    UPROPERTY() ELDDemoCampTier Tier = ELDDemoCampTier::Low;
    UPROPERTY() int64 CountPerCamp = 0;
    UPROPERTY() int64 HPP2 = 0;
    UPROPERTY() int64 ArmorP2 = 0;
    UPROPERTY() int64 MagicResistP2 = 0;
    UPROPERTY() int64 AttackP2 = 0;
    UPROPERTY() FLDDemoRational AttackSpeed;
    UPROPERTY() int64 AttackWindupMs = 0;
    UPROPERTY() int64 AttackIntervalMs = 0;
    UPROPERTY() FLDDemoRational AttackRangeCm;
    UPROPERTY() FLDDemoRational MoveSpeedCmPerSec;
    UPROPERTY() FLDDemoRational CollisionRadiusCm;
    UPROPERTY() FLDDemoRational LeashRadiusCm;
    UPROPERTY() FLDDemoRational PerceptionRadiusCm;
    UPROPERTY() int64 LoseTargetGraceMs = 0;
    UPROPERTY() int64 RespawnMs = 0;
    UPROPERTY() int64 RewardXP = 0;
    UPROPERTY() FLDDemoRational HPRegenPerSec;
    UPROPERTY() int64 MaxMPP2 = 0;
    UPROPERTY() FLDDemoRational MPRegenPerSec;
    UPROPERTY() int64 APP2 = 0;
    UPROPERTY() int64 CritChanceBp = 0;
};

USTRUCT()
struct FLDDemoCampPlacement
{
    GENERATED_BODY()

    UPROPERTY() FString AnchorId;
    UPROPERTY() FString MonsterId;
};

// The approved level cap (#166). The importer accepts exactly this max_level, and every bound on a committed level
// (result rows included) reads it here instead of repeating the number.
namespace LDDemoProgression
{
    inline constexpr int32 MaxLevel = 16;
}

USTRUCT()
struct FLDDemoProgressionDefinition
{
    GENERATED_BODY()

    UPROPERTY() int32 MaxLevel = 0;
    UPROPERTY() int32 ActiveSlots = 0;
    UPROPERTY() int64 XPStart = 0;
    UPROPERTY() TArray<int64> XPThresholds;
    UPROPERTY() TArray<int64> XPToNext;
    // #198: reaching each of these levels enqueues one more active opportunity (after the opening one), distinct and ascending.
    UPROPERTY() TArray<int32> ActiveOfferLevels;
};

USTRUCT()
struct FLDDemoWorldReferences
{
    GENERATED_BODY()

    UPROPERTY() FString MapId;
    UPROPERTY() TArray<FString> SpawnIds;
    UPROPERTY() TArray<FString> CampIds;
    UPROPERTY() FString CaptureId;
};

USTRUCT()
struct FLDDemoDisplayDefinition
{
    GENERATED_BODY()

    UPROPERTY() FString Id;
    UPROPERTY() FString Namespace;
    UPROPERTY() FString NameKey;
    UPROPERTY() FString NameText;
    UPROPERTY() FString DescriptionKey;
    // Active and stat descriptions are card templates (LDDemoCardText).
    UPROPERTY() FString DescriptionText;
    UPROPERTY() FString IconId;
    UPROPERTY() FString PortraitIconId;
};

USTRUCT()
struct FLDDemoContentIdentity
{
    GENERATED_BODY()

    UPROPERTY() FLDDemoContentDigest Root;
    UPROPERTY() FLDDemoContentDigest Content;
    UPROPERTY() FLDDemoContentDigest World;
    UPROPERTY() FLDDemoContentDigest AuthorSchema;
    UPROPERTY() FLDDemoContentDigest NativeIds;
    UPROPERTY() FLDDemoContentDigest NativeContract;
    UPROPERTY() FLDDemoContentDigest Display;
    UPROPERTY() FLDDemoContentDigest WorldContract;
    UPROPERTY() int32 SchemaVersion = 0;
    UPROPERTY() int32 HandlerRevision = 0;
    UPROPERTY() int32 ProjectionRevision = 0;
    UPROPERTY() int32 WorldSchemaVersion = 0;
    UPROPERTY() int32 WorldProjectionRevision = 0;
    UPROPERTY() FString ApprovalStatus;
    UPROPERTY() FString ApprovalDecision;
};

USTRUCT()
struct LOCKDOWN_API FLDDemoContentDefinitions
{
    GENERATED_BODY()

    UPROPERTY() FLDDemoContentIdentity Identity;
    UPROPERTY() FLDDemoContentParameters Parameters;
    UPROPERTY() FLDDemoFormulas Formulas;
    UPROPERTY() FLDDemoPolicies Policies;
    UPROPERTY() FLDDemoCharacterDefinition Character;
    UPROPERTY() TArray<FLDDemoAbilityDefinition> Abilities;
    UPROPERTY() TArray<FLDDemoStatDefinition> Stats;
    UPROPERTY() TArray<FLDDemoMonsterDefinition> Monsters;
    UPROPERTY() TArray<FLDDemoCampPlacement> Camps;
    UPROPERTY() FLDDemoBossDefinition Boss;
    UPROPERTY() FLDDemoProgressionDefinition Progression;
    UPROPERTY() FLDDemoWorldReferences World;
    UPROPERTY() TArray<FLDDemoDisplayDefinition> Display;

    // #198: the point-bought stats in their design order 体格, 脸皮, 力道, 脑力, 身手 (ascending native id). Stats itself is
    // id-sorted like every catalog list; every per-stat array (build points, AllocatePoints counts, received points, owner
    // view rows, UI rows, AI ties) is indexed in this order.
    TArray<const FLDDemoStatDefinition*> StatsInDesignOrder() const;
    // Index of StatId in StatsInDesignOrder, or INDEX_NONE.
    int32 StatDesignIndex(const FString& StatId) const;
};
