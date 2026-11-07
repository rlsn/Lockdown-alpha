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

// 112 §6 draft weights, all basis points of a ratio (10000 = 1.0).
USTRUCT()
struct FLDDemoChoiceWeights
{
    GENERATED_BODY()

    UPROPERTY() int64 UpgradeWeightBp = 0;
    UPROPERTY() int64 NewActiveOpenSlotWeightBp = 0;
    UPROPERTY() int64 NewActiveFullSlotWeightBp = 0;
    UPROPERTY() int64 NewPassiveWeightBp = 0;
    // Each tag a candidate shares with the rest of the build multiplies its weight by AffinityStep, capped at AffinityCap.
    UPROPERTY() int64 AffinityStepBp = 0;
    UPROPERTY() int64 AffinityCapBp = 0;
    // Chance that the third card of a level-up offer comes from the attribute pool.
    UPROPERTY() int64 ThirdSlotAttributeBp = 0;
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
    // STAT.AbilityCritMultiplier: damage multiplier of an ability critical (active_monkey_peach Lv3, passive_precision Lv3).
    UPROPERTY() int64 AbilityCritMultiplierBp = 0;
    UPROPERTY() int64 CooldownHasteBaseP2 = 0;
    UPROPERTY() int64 CooldownHasteMaxP2 = 0;
    UPROPERTY() FLDDemoRationalBounds MoveSpeedCmPerSec;
    UPROPERTY() FLDDemoRational CollisionRadiusCm;
    UPROPERTY() FLDDemoRationalBounds AttackRangeCm;
    UPROPERTY() int64 BasicAttackWindupMs = 0;
    UPROPERTY() FLDDemoRational VisionRadiusCm;
    UPROPERTY() FLDDemoRational ShopInteractRadiusCm;
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
    UPROPERTY() int64 StartingGold = 0;
    UPROPERTY() int64 SellRatioBp = 0;
    // 112 §7: one upgrade per contender and match costs UpgradeCost; fixed stats rise by UpgradeStatBonusBp.
    UPROPERTY() int64 UpgradeCost = 0;
    UPROPERTY() int64 UpgradeStatBonusBp = 0;
    UPROPERTY() FLDDemoRational CampResetArrivalRadiusCm;
    // #28: a Returning member with this many consecutive refused path plans, or this long without path progress
    // while free to move, resets where it stands instead of retrying forever.
    UPROPERTY() int64 CampReturnDeadEndPlans = 0;
    UPROPERTY() int64 CampReturnDeadEndMs = 0;
    // D26 (#79) airdrop: landings at FirstLand + n * Interval from the match start, each public AnnounceLead earlier.
    UPROPERTY() int64 AirdropFirstLandMs = 0;
    UPROPERTY() int64 AirdropIntervalMs = 0;
    UPROPERTY() int64 AirdropAnnounceLeadMs = 0;
    UPROPERTY() FLDDemoRational AirdropPickupRadiusCm;
    UPROPERTY() int64 AirdropRewardXP = 0;
    UPROPERTY() int64 AirdropRewardGold = 0;
    // D27 (#79) 上热搜: the leader is named every LeaderRefreshMs; see World/LDLeaderNotice.h.
    UPROPERTY() int64 LeaderMinLevel = 0;
    UPROPERTY() int64 LeaderRefreshMs = 0;
    UPROPERTY() FLDDemoRational LeaderCircleRadiusCm;
    UPROPERTY() FLDDemoRational LeaderCircleMaxOffsetCm;
    UPROPERTY() int64 LeaderBountyGoldPerRefresh = 0;
    UPROPERTY() int64 LeaderBountyGoldMax = 0;
    // COMBAT.RearArcDeg: full angle of the sector behind a body's authority facing that counts as "from behind".
    UPROPERTY() FLDDemoRational RearArcDeg;
    UPROPERTY() int64 XPBaseToNext = 0;
    UPROPERTY() int64 XPIncreasePerLevel = 0;
    UPROPERTY() int64 PlayerXPBase = 0;
    UPROPERTY() int64 PlayerXPPerLevel = 0;
    UPROPERTY() int64 PlayerGoldBase = 0;
    UPROPERTY() int64 PlayerGoldPerLevel = 0;
    // Contender kills only: a contribution within this window before the death earns an assist; the assisters split
    // this share of the victim's base kill gold (the killer keeps the full kill gold).
    UPROPERTY() int64 AssistWindowMs = 0;
    UPROPERTY() int64 AssistGoldBp = 0;
    UPROPERTY() FLDDemoAIParameters AI;
    UPROPERTY() FLDDemoChoiceWeights Choice;
};

UENUM()
enum class ELDDemoFormula : uint8
{
    None, AddFlatThenAdditivePercent, PointsOverScalePlusPoints, OncePerPrimaryAttack,
    FloorP2ThenShieldThenHP, BaseTimesAdditiveRatio, BasePlusFlat, CeilReciprocalMs,
    CeilBaseWindupTimesBaseOverCurrentAS, CeilBaseTimesScaleOverScalePlusHaste,
    CarrySubP2Remainder, FloorFinalRatioRejectAliveUnderflow, FloorPurchasePriceTimesSellRatio,
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
    UPROPERTY() ELDDemoFormula SellGold = ELDDemoFormula::None;
    UPROPERTY() ELDDemoFormula XPToNext = ELDDemoFormula::None;
    UPROPERTY() int64 ResistanceScaleP2 = 0;
    UPROPERTY() int64 HasteScaleP2 = 0;
};

UENUM()
enum class ELDDemoPolicy : uint8
{
    None, AddFlatThenAdditivePercent, Allowed, OncePerFamily,
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
    UPROPERTY() ELDDemoPolicy DuplicateEquipment = ELDDemoPolicy::None;
    UPROPERTY() ELDDemoPolicy DuplicateEquipmentPassive = ELDDemoPolicy::None;
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
    UPROPERTY() bool ShopRequiresLineOfSight = false;
    UPROPERTY() ELDDemoPolicy ShieldPolicy = ELDDemoPolicy::None;
    UPROPERTY() TArray<ELDDemoRootBlock> RootBlocks;
    UPROPERTY() ELDDemoPolicy CampAggroTrigger = ELDDemoPolicy::None;
    UPROPERTY() bool CampGroupRetaliation = false;
    UPROPERTY() bool CampReturningDamageable = false;
    UPROPERTY() bool CampReturningControllable = false;
    UPROPERTY() ELDDemoPolicy CampResetPolicy = ELDDemoPolicy::None;
    UPROPERTY() ELDDemoPolicy CampManaPolicy = ELDDemoPolicy::None;
};

UENUM()
enum class ELDDemoTargetMode : uint8 { Self, Unit, DirectionProjectile, DirectionDash, GroundPoint };

// When a triggered effect fires (cooldown_refund, triggered_shield, mana_on_hit).
UENUM()
enum class ELDDemoEffectTrigger : uint8
{
    None, Release, EmpowerHit, BasicHit, BasicCrit, Cleanse, LandingHit, LowHP, AbilityHit, Control
};

// Which cooldown tracks a cooldown_refund shortens: the basic cadence, the releasing source's own track,
// the other learned active (Q/W) or both learned actives.
UENUM()
enum class ELDDemoCooldownEntries : uint8 { None, Basic, Self, OtherActives, Actives };

// ability_crit: a guaranteed ability critical against an impaired target, or a share of the crit chance for listed abilities.
UENUM()
enum class ELDDemoCritMode : uint8 { None, Impaired, Chance };

UENUM()
enum class ELDDemoDamageType : uint8 { Physical, Magical };

UENUM()
enum class ELDDemoAbilityKind : uint8 { Active, Passive };

UENUM()
// Boss is the D26 画饼老板's monster row: one roaming body, never placed in a camp.
enum class ELDDemoCampTier : uint8 { Low, High, Boss };

// 112 §1: the only mechanic tags of skills and equipment; draft affinity counts shared tags, the UI never shows them as schools.
UENUM()
enum class ELDDemoMechanicTag : uint8 { BasicAttack, Mobility, Control, Shield, Tank, Breach };

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
    APBonusRatio
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
    // Share of the caster's extra basic-attack range (current minus base) added to this cast range, up to
    // AttackRangeBonusCapCm; 0 = none.
    UPROPERTY() int64 AttackRangeRatioBp = 0;
    UPROPERTY() FLDDemoRational AttackRangeBonusCapCm;
};

USTRUCT()
struct FLDDemoEffectDefinition
{
    GENERATED_BODY()

    // Opcode is checked against ELDDemoEffectOp; reservations are not battle handlers.
    // Field use per op: Slow RatioBp+DurationMs; TimedStatBonus Stats+DurationMs; SelfArea RadiusCm;
    // LowHpStatBonus RatioBp (HP threshold)+Stats; AmbushDamage DelayMs+damage terms; ControlDamageAmp RatioBp+DurationMs.
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
    // DashBoost (D20): the innate dash cooldown is multiplied by CooldownRatioBp/10000 and its distance by
    // (10000+DistanceBonusBp)/10000.
    UPROPERTY() int64 CooldownRatioBp = 0;
    UPROPERTY() int64 DistanceBonusBp = 0;
    UPROPERTY() TArray<FLDDemoStatBonus> Stats;
    // Thorns: armor scaling of the reflection, per-attacker repeat window, critical-hit multiplier,
    // whether ability hits trigger it (basic hits always do) and whether hits from behind are exempt.
    UPROPERTY() int64 ArmorRatioBp = 0;
    UPROPERTY() int64 IntervalMs = 0;
    UPROPERTY() int64 CriticalMultiplierBp = 0;
    UPROPERTY() bool bAbilityTrigger = false;
    UPROPERTY() bool bRearExempt = false;
    // Generic trigger/entry selectors of triggered effects (cooldown_refund, triggered_shield, mana_on_hit).
    UPROPERTY() ELDDemoEffectTrigger Trigger = ELDDemoEffectTrigger::None;
    UPROPERTY() ELDDemoCooldownEntries Entries = ELDDemoCooldownEntries::None;
    UPROPERTY() ELDDemoCritMode CritMode = ELDDemoCritMode::None;
    // cooldown_refund: the remaining-cooldown reduction; ambush_damage: the readiness credit of one dash or jump.
    UPROPERTY() int64 AmountMs = 0;
    // resist_shred: magic resist share removed (ArmorRatioBp is the armor share).
    UPROPERTY() int64 MagicResistRatioBp = 0;
    // slow: the source's vamp against the slowed body while it lasts; low_hp_stat_bonus: omnivamp while active.
    UPROPERTY() int64 VampRatioBp = 0;
    // shield: multiplier when the same release cleansed a root or slow; out_of_combat_regen: regen surge multiplier.
    UPROPERTY() int64 MultiplierBp = 0;
    // nth_basic_bonus: every Count-th landed basic; basic_stacks: maximum stacks.
    UPROPERTY() int32 Count = 0;
    // backstab: impaired targets (rooted, slowed, taunted, knocked back) count as struck from behind.
    UPROPERTY() bool bImpaired = false;
    // dash_boost: cooldown ratio and distance bonus of every learned dash or jump active at release.
    UPROPERTY() int64 SkillCooldownRatioBp = 0;
    UPROPERTY() int64 SkillDistanceBonusBp = 0;
    // ability_crit (Chance): the abilities that may crit with the share of the owner's crit chance.
    UPROPERTY() TArray<FString> AbilityIds;
    // taunt_retaliate: damage per counted attack; momentum: cap of the gathered damage; long_reach: amplification cap.
    UPROPERTY() int64 PerCountP2 = 0;
    UPROPERTY() int64 CapP2 = 0;
    UPROPERTY() int64 CapBp = 0;
    // out_of_combat_regen: HP share of the current max HP regenerated per second (basis points).
    UPROPERTY() int64 MaxHPPerSecBp = 0;
    // #112 handlers (LDCombatMechanics; Count and CapP2 above are shared): armor and MR shred per step, a trigger
    // threshold, a second ratio and duration (an attached slow, heal cut or linger) and a cooldown cut.
    UPROPERTY() int64 ShredP2 = 0;
    UPROPERTY() int64 ThresholdP2 = 0;
    UPROPERTY() int64 SecondaryRatioBp = 0;
    UPROPERTY() int64 SecondaryDurationMs = 0;
    UPROPERTY() int64 CooldownCutMs = 0;

    ELDDemoEffectOp GetOp() const { return static_cast<ELDDemoEffectOp>(Opcode); }
};

USTRUCT()
struct FLDDemoRankDefinition
{
    GENERATED_BODY()

    UPROPERTY() int32 Rank = 0;
    UPROPERTY() int64 ManaCostP2 = 0;
    UPROPERTY() int64 CooldownMs = 0;
    UPROPERTY() TArray<FLDDemoEffectDefinition> Effects;
};

USTRUCT()
struct FLDDemoAbilityDefinition
{
    GENERATED_BODY()

    UPROPERTY() FString Id;
    UPROPERTY() uint8 NativeId = 0;
    UPROPERTY() ELDDemoAbilityKind Kind = ELDDemoAbilityKind::Active;
    UPROPERTY() TArray<ELDDemoMechanicTag> Tags;
    // False keeps the skill out of every three-card offer (opening and level-up); it stays learnable otherwise.
    UPROPERTY() bool Draftable = false;
    UPROPERTY() FLDDemoTargetDefinition Target;
    UPROPERTY() int64 WindupMs = 0;
    UPROPERTY() FString CooldownFamily;
    UPROPERTY() TArray<FLDDemoRankDefinition> Ranks;

    ELDDemoAbilityId GetNativeId() const { return static_cast<ELDDemoAbilityId>(NativeId); }
};

USTRUCT()
struct FLDDemoAttributeDefinition
{
    GENERATED_BODY()

    UPROPERTY() FString Id;
    UPROPERTY() uint8 NativeId = 0;
    UPROPERTY() TArray<FLDDemoStatBonus> Stats;
    // Explicit deductions of a wild card (#112 §4): positive magnitudes the projection subtracts; empty for every other card.
    UPROPERTY() TArray<FLDDemoStatBonus> Penalties;
};

USTRUCT()
struct FLDDemoEquipmentPassive
{
    GENERATED_BODY()

    UPROPERTY() FString Family;
    UPROPERTY() TArray<FLDDemoEffectDefinition> Effects;
    // 112 §7: the upgraded terms of the same handlers in the same order.
    UPROPERTY() bool HasUpgraded = false;
    UPROPERTY() TArray<FLDDemoEffectDefinition> UpgradedEffects;
};

USTRUCT()
struct FLDDemoEquipmentDefinition
{
    GENERATED_BODY()

    UPROPERTY() FString Id;
    UPROPERTY() uint8 NativeId = 0;
    UPROPERTY() int64 BuyPrice = 0;
    UPROPERTY() int64 SellGold = 0;
    UPROPERTY() TArray<ELDDemoMechanicTag> Tags;
    UPROPERTY() TArray<FLDDemoStatBonus> Stats;
    // Stats times 1 + UpgradeStatBonusBp, floored per field precision (P2, R4, whole cm).
    UPROPERTY() TArray<FLDDemoStatBonus> UpgradedStats;
    UPROPERTY() bool HasPassive = false;
    UPROPERTY() FLDDemoEquipmentPassive Passive;

    ELDDemoEquipmentId GetNativeId() const { return static_cast<ELDDemoEquipmentId>(NativeId); }
};

USTRUCT()
struct FLDDemoDashDefinition
{
    GENERATED_BODY()

    UPROPERTY() FLDDemoTargetDefinition Target;
    UPROPERTY() int64 WindupMs = 0;
    UPROPERTY() FString CooldownFamily;
    UPROPERTY() FLDDemoRankDefinition Rank;
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
    // no windup, one rank with the approved mana cost, cooldown and a single Dash effect.
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
    UPROPERTY() int64 RewardGold = 0;
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

USTRUCT()
struct FLDDemoProgressionDefinition
{
    GENERATED_BODY()

    UPROPERTY() int32 MaxLevel = 0;
    UPROPERTY() int32 MaxChoices = 0;
    UPROPERTY() int32 MaxSkillRank = 0;
    UPROPERTY() int32 ActiveSlots = 0;
    UPROPERTY() int32 EquipmentSlots = 0;
    UPROPERTY() int64 XPStart = 0;
    UPROPERTY() TArray<int64> XPThresholds;
    UPROPERTY() TArray<int64> XPToNext;
    // 112 §6: the opening offer holds OpeningActiveCount new draftable actives and OpeningPassiveCount new draftable passives.
    UPROPERTY() int32 OpeningActiveCount = 0;
    UPROPERTY() int32 OpeningPassiveCount = 0;
    // Reaching each of these levels earns one free reroll token.
    UPROPERTY() TArray<int32> RerollLevels;
};

USTRUCT()
struct FLDDemoWorldReferences
{
    GENERATED_BODY()

    UPROPERTY() FString MapId;
    UPROPERTY() TArray<FString> SpawnIds;
    UPROPERTY() TArray<FString> CampIds;
    // #97: every shop anchor, ordinal-sorted; a match may have several shops.
    UPROPERTY() TArray<FString> ShopIds;
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
    UPROPERTY() TArray<FLDDemoAttributeDefinition> Attributes;
    UPROPERTY() TArray<FLDDemoEquipmentDefinition> Equipment;
    UPROPERTY() TArray<FLDDemoMonsterDefinition> Monsters;
    UPROPERTY() TArray<FLDDemoCampPlacement> Camps;
    UPROPERTY() FLDDemoBossDefinition Boss;
    UPROPERTY() FLDDemoProgressionDefinition Progression;
    UPROPERTY() FLDDemoWorldReferences World;
    UPROPERTY() TArray<FLDDemoDisplayDefinition> Display;
};

// 112 §7: the "current effective definition" of an equipped instance. Every reader of equipment stats or passive terms
// goes through these, so an upgrade changes parameters without a second handler branch.
namespace LDDemoEquipment
{
    // Upgradable only when the item's passive, if any, carries its approved upgraded form; never falls back.
    inline bool Upgradable(const FLDDemoEquipmentDefinition& Item)
    {
        return !Item.HasPassive || Item.Passive.HasUpgraded;
    }
    inline const TArray<FLDDemoStatBonus>& EffectiveStats(const FLDDemoEquipmentDefinition& Item, bool bUpgraded)
    {
        return bUpgraded ? Item.UpgradedStats : Item.Stats;
    }
    // Null when the item has no passive, or an upgraded instance lacks its upgraded terms (a refused state).
    inline const TArray<FLDDemoEffectDefinition>* EffectivePassiveEffects(const FLDDemoEquipmentDefinition& Item, bool bUpgraded)
    {
        if (!Item.HasPassive) { return nullptr; }
        if (!bUpgraded) { return &Item.Passive.Effects; }
        return Item.Passive.HasUpgraded ? &Item.Passive.UpgradedEffects : nullptr;
    }
}
