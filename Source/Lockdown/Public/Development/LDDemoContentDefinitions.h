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
    // P6 spec §10.3: an AI contender goes for a live mid-field objective within ObjectiveReachCm (XY) of its centre when its
    // health is at least ObjectiveEntryHPBp of its maximum.
    UPROPERTY() int64 ObjectiveEntryHPBp = 0;
    UPROPERTY() FLDDemoRational ObjectiveReachCm;
    // An AI contender inside a visible telegraph with its D ready dodges out with this chance (AI.DodgeChance).
    UPROPERTY() int64 DodgeChanceBp = 0;
    // Spec §13: a planned walk weighs its length inside a visible enemy ground state by this (AI.EnemyGroundPathCost).
    UPROPERTY() int64 EnemyGroundPathCostBp = 0;
    // Card picks (spec §13): a candidate scores CardVocabScore per vocabulary key it shares with the AI's installed cards
    // and learned skills; with BreakoutChance an AI takes one breakout preset as its goal at match start and its cards
    // score BreakoutCardScore more.
    UPROPERTY() int64 CardVocabScore = 0;
    // An AI walks toward the trending leader only while the leader's public level is at most this far above its own.
    UPROPERTY() int64 LeaderHuntMaxLevelGap = 0;
    UPROPERTY() int64 BreakoutChanceBp = 0;
    UPROPERTY() int64 BreakoutCardScore = 0;
    // An offer sharing no vocabulary key with the AI's build is skipped for vouchers with this chance (AI.CardSkipChance).
    UPROPERTY() int64 CardSkipChanceBp = 0;
};

// Build cards (spec §3.12): capacity CapacityBase + floor((level - 1) / LevelsPerSlot); every offer shows OfferSize cards
// drawn by weight without replacement (VocabWeight for a candidate sharing a vocabulary key with the installed cards or
// learned skills, 1 otherwise); FreeRerolls rerolls at match start and RerollsPerRest more at each mid-match rest.
USTRUCT()
struct FLDDemoCardParameters
{
    GENERATED_BODY()

    UPROPERTY() int32 CapacityBase = 0;
    UPROPERTY() int32 LevelsPerSlot = 0;
    UPROPERTY() int32 OfferSize = 0;
    UPROPERTY() int32 FreeRerolls = 0;
    UPROPERTY() int32 RerollsPerRest = 0;
    UPROPERTY() int64 VocabWeightBp = 0;
    // Stat points for skipping one card offer (CARDS.SkipPoints).
    UPROPERTY() int32 SkipPoints = 0;
    // A level-up queues a card offer only at levels this divides (1: every level).
    UPROPERTY() int32 OfferEveryLevels = 0;
    // #305: the idle time Y22 and K16 may carry into a fight (CARDS.IdleCarrySec); they count nothing else outside one.
    UPROPERTY() int64 IdleCarryMs = 0;
};

// Spec §3.13 rest shop: SkillSlots skills and CardSlots cards per stock (VocabCardSlots of the cards drawn with VocabWeight for a
// candidate sharing the build's vocabulary, the rest uniformly), the prices in stat points, the refresh price (RefreshPrice may
// be 0: the rest's first refresh is free) rising RefreshStep per refresh of one rest and a skill point (at most
// SkillPointMaxPerMatch per match) and a random element medal (MedalPrice, at most MedalMaxPerMatch per match).
USTRUCT()
struct FLDDemoShopParameters
{
    GENERATED_BODY()

    UPROPERTY() int32 CardSlots = 0;
    UPROPERTY() int32 VocabCardSlots = 0;
    UPROPERTY() int64 VocabWeightBp = 0;
    UPROPERTY() int32 SkillSlots = 0;
    UPROPERTY() int32 CardPrice = 0;
    UPROPERTY() int32 SkillPrice = 0;
    UPROPERTY() int32 RefreshPrice = 0;
    UPROPERTY() int32 RefreshStep = 0;
    UPROPERTY() int32 SkillPointPrice = 0;
    UPROPERTY() int32 SkillPointMaxPerMatch = 0;
    UPROPERTY() int32 MedalPrice = 0;
    UPROPERTY() int32 MedalMaxPerMatch = 0;
};

// Spec §10.3 (P6) mid-field objectives: StartMs of segment-local field time after each timed segment starts, announced
// AnnounceLeadMs earlier. Segments 1 and 3 hold a spray mini flag (FlagRadiusCm disk, FlagSprayMs of solo spray inside
// FlagWindowMs); segment 2 holds the arena (ArenaRadiusCm around the flagpole for ArenaMs). The winners take a card offer of
// FlagCardOffer / ArenaCardOffer cards.
USTRUCT()
struct FLDDemoObjectiveParameters
{
    GENERATED_BODY()

    UPROPERTY() int64 StartMs = 0;
    UPROPERTY() int64 AnnounceLeadMs = 0;
    UPROPERTY() int64 FlagWindowMs = 0;
    UPROPERTY() FLDDemoRational FlagRadiusCm;
    UPROPERTY() int64 FlagSprayMs = 0;
    UPROPERTY() int32 FlagCardOffer = 0;
    UPROPERTY() int64 ArenaMs = 0;
    UPROPERTY() FLDDemoRational ArenaRadiusCm;
    UPROPERTY() int32 ArenaCardOffer = 0;
};

// Spec §11.1 (P6) daily modifiers (今日热点): Count of the twelve are drawn each match; these are their numbers.
USTRUCT()
struct FLDDemoDailyParameters
{
    GENERATED_BODY()

    UPROPERTY() int32 Count = 0;
    UPROPERTY() int64 RainIntervalMs = 0;
    UPROPERTY() int32 RainPuddles = 0;
    UPROPERTY() int64 RainPuddleMs = 0;
    UPROPERTY() int64 RainWetBonusMs = 0;
    UPROPERTY() int64 ColdFreezeMs = 0;
    UPROPERTY() int64 ColdChillBonusMs = 0;
    UPROPERTY() int64 BlackoutVisionCutBp = 0;
    UPROPERTY() int64 BlackoutShockScaleBp = 0;
    UPROPERTY() int64 TyphoonIntervalMs = 0;
    UPROPERTY() FLDDemoRational TyphoonPushCm;
    UPROPERTY() int64 HeatBurnTickScaleBp = 0;
    UPROPERTY() int64 SalePriceScaleBp = 0;
    // Delivery day: the airdrop schedule this day uses instead of AirdropLandMs, each drop gone DeliveryExpireMs after landing.
    UPROPERTY() TArray<int64> DeliveryLandMs;
    UPROPERTY() int64 DeliveryExpireMs = 0;
    UPROPERTY() int64 PatrolBossAppearMs = 0;
    UPROPERTY() int64 ExamMonsterXPScaleBp = 0;
    UPROPERTY() int64 ExamRespawnBonusMs = 0;
    UPROPERTY() int64 GymDashCooldownCutBp = 0;
    UPROPERTY() int32 RumorLeaders = 0;
};

// Spec §11.2 (P6) camp affixes: a High, Elite or Ancient camp spawn carries one of six with probability ChanceBp.
USTRUCT()
struct FLDDemoAffixParameters
{
    GENERATED_BODY()

    UPROPERTY() int64 ChanceBp = 0;
    UPROPERTY() int64 OvertimeAttackSpeedBp = 0;
    UPROPERTY() int64 TeambuildXPScaleBp = 0;
    UPROPERTY() int32 FridgeCardOffer = 0;
    UPROPERTY() int64 BonusHPBp = 0;
    UPROPERTY() int32 BonusCardOffer = 0;
    UPROPERTY() int64 LayoffAttackBp = 0;
    UPROPERTY() int64 AuditTelegraphCutBp = 0;
    UPROPERTY() int32 AuditSkillPointsMax = 0;
};

// Element medals drop on their own, never from a card offer or the shop: a level-up that queues no card offer, the last hit
// on a camp monster by its tier, the Boss's last hit, and each event reward (airdrop, locker, mini flag, arena, peddler).
// Each is a chance in basis points (MEDAL.*Chance) to receive one uniformly drawn medal.
USTRUCT()
struct FLDDemoMedalParameters
{
    GENERATED_BODY()

    UPROPERTY() int64 LevelChanceBp = 0;
    UPROPERTY() int64 CampLowChanceBp = 0;
    UPROPERTY() int64 CampHighChanceBp = 0;
    UPROPERTY() int64 CampEliteChanceBp = 0;
    UPROPERTY() int64 CampAncientChanceBp = 0;
    UPROPERTY() int64 BossChanceBp = 0;
    UPROPERTY() int64 EventChanceBp = 0;
    UPROPERTY() int32 MaxTier = 3;
    UPROPERTY() int64 Tier2ReactionBonusBp = 0;
};

// Spec §11.3 (P6) delivery lockers and the second-hand dealer.
USTRUCT()
struct FLDDemoExploreParameters
{
    GENERATED_BODY()

    UPROPERTY() int32 LockerCount = 0;
    UPROPERTY() FLDDemoRational LockerRadiusCm;
    UPROPERTY() int64 LockerChannelMs = 0;
    UPROPERTY() int32 LockerCardOffer = 0;
    UPROPERTY() int64 LockerEarliestMs = 0;
    UPROPERTY() int64 LockerLatestMs = 0;
    UPROPERTY() int64 LockerStayMs = 0;
    UPROPERTY() int64 PeddlerEarliestMs = 0;
    UPROPERTY() int64 PeddlerLatestMs = 0;
    UPROPERTY() FLDDemoRational PeddlerOffsetCm;
    UPROPERTY() int64 PeddlerHPP2 = 0;
    UPROPERTY() FLDDemoRational PeddlerMoveSpeedCmPerSec;
    UPROPERTY() int64 PeddlerStayMs = 0;
    UPROPERTY() int32 PeddlerCardOffer = 0;
};

// Momentum (劲), the one skill resource of a contender (CONTENT_AND_BALANCE §3.3, LDMomentum). Camp members hold none.
USTRUCT()
struct FLDDemoMomentumParameters
{
    GENERATED_BODY()

    UPROPERTY() int64 MaxP2 = 0;
    // The value at match start, every new life and every rest start; the idle drift returns to it.
    UPROPERTY() int64 StartP2 = 0;
    // Gain per second while in combat (LastCombatAt within IdleDelayMs), before the brains bonus.
    UPROPERTY() FLDDemoRational CombatPerSec;
    // A primary basic hit gains BasicPerRefP2 x its attack interval / BasicRefIntervalMs (floored to P2).
    UPROPERTY() int64 BasicPerRefP2 = 0;
    UPROPERTY() int64 BasicRefIntervalMs = 0;
    UPROPERTY() int64 SkillHitP2 = 0;
    UPROPERTY() int64 ReactionP2 = 0;
    // IdleDelayMs after the last combat event the bar drifts toward StartP2 at IdleReturnPerSec.
    UPROPERTY() int64 IdleDelayMs = 0;
    UPROPERTY() FLDDemoRational IdleReturnPerSec;
};

// The basic three-step combo (COMBO.*): StepRatiosBp[step - 1] scales a primary basic's own package; a successful
// AttackStart more than ResetMs after the previous one starts again at step 1.
USTRUCT()
struct FLDDemoComboParameters
{
    GENERATED_BODY()

    UPROPERTY() TArray<int64> StepRatiosBp;
    UPROPERTY() int64 ResetMs = 0;
};

// Issue #287 spec §3.10 and §8.1-§8.2 element and reaction terms beyond the #198 water/fire ones (COMBAT.*): shock (every
// ShockEveryNthHit-th direct hit on the holder strikes ShockRatio x R on it and on the applier's other enemies within
// ShockSplashRadiusCm), chill (attack and move slow), rot (heal and shield cut, damage every RotTickMs), the short shove of a
// fan blow or a typhoon (PushMs) and the reactions' ratios of R, stacks, durations and distances.
USTRUCT()
struct FLDDemoElementParameters
{
    GENERATED_BODY()

    UPROPERTY() int64 ShockMs = 0;
    UPROPERTY() int64 ShockEveryNthHit = 0;
    UPROPERTY() int64 ShockRatioBp = 0;
    UPROPERTY() FLDDemoRational ShockSplashRadiusCm;
    UPROPERTY() int64 ChillMs = 0;
    UPROPERTY() int64 ChillAttackSlowBp = 0;
    UPROPERTY() int64 ChillMoveSlowBp = 0;
    UPROPERTY() int64 RotMs = 0;
    UPROPERTY() int64 RotHealCutBp = 0;
    UPROPERTY() int64 RotPerSecP2 = 0;
    UPROPERTY() int64 RotAPRatioPerSecBp = 0;
    UPROPERTY() int64 RotTickMs = 0;
    UPROPERTY() int64 PushMs = 0;
    UPROPERTY() int64 ConductRatioBp = 0;
    UPROPERTY() int64 OverloadRatioBp = 0;
    UPROPERTY() int64 MeltRatioBp = 0;
    UPROPERTY() int64 MethaneRatioBp = 0;
    UPROPERTY() int64 MethaneBurnStacks = 0;
    UPROPERTY() FLDDemoRational MethaneRadiusCm;
    UPROPERTY() int64 FreezeRootMs = 0;
    UPROPERTY() int64 FermentRotMs = 0;
    UPROPERTY() int64 FermentHealCutBp = 0;
    UPROPERTY() int64 ShatterSkillDamageBp = 0;
    UPROPERTY() int64 ShatterMs = 0;
    UPROPERTY() int64 CorrodeBreachStacks = 0;
    UPROPERTY() int64 CorrodeBreachMs = 0;
    UPROPERTY() int64 FrostbiteAttackSlowBp = 0;
    UPROPERTY() int64 FrostbiteMs = 0;
};

// Spec §8.3 ground states (GROUND.*): radius, life and tick of each kind, ice's inertia and displacement scales, the fire
// ground's layers, the field's damage ratio and the performance caps per owner and per map.
USTRUCT()
struct FLDDemoGroundParameters
{
    GENERATED_BODY()

    UPROPERTY() FLDDemoRational PuddleRadiusCm;
    UPROPERTY() int64 PuddleMs = 0;
    UPROPERTY() int64 PuddleTickMs = 0;
    UPROPERTY() FLDDemoRational IceRadiusCm;
    UPROPERTY() int64 IceMs = 0;
    UPROPERTY() int64 IceAccelScaleBp = 0;
    UPROPERTY() int64 IceDisplacementScaleBp = 0;
    UPROPERTY() FLDDemoRational FireRadiusCm;
    UPROPERTY() int64 FireMs = 0;
    UPROPERTY() int64 FireTickMs = 0;
    UPROPERTY() int64 FireBurnStacks = 0;
    UPROPERTY() FLDDemoRational MiasmaRadiusCm;
    UPROPERTY() int64 MiasmaMs = 0;
    UPROPERTY() int64 MiasmaTickMs = 0;
    UPROPERTY() FLDDemoRational FieldRadiusCm;
    UPROPERTY() int64 FieldMs = 0;
    UPROPERTY() int64 FieldTickMs = 0;
    UPROPERTY() int64 FieldRatioBp = 0;
    UPROPERTY() int64 MaxPerOwner = 0;
    UPROPERTY() int64 MaxTotal = 0;
};

// Skill ranks 1..3 (SKILL.*): rank r scales the grown base amounts by RankBaseScaleBp[r - 1] and the AD/AP/max-HP
// ratios by RankRatioScaleBp[r - 1]; each level of PointLevels grants one skill point.
USTRUCT()
struct FLDDemoSkillRankParameters
{
    GENERATED_BODY()

    UPROPERTY() TArray<int64> RankBaseScaleBp;
    UPROPERTY() TArray<int64> RankRatioScaleBp;
    UPROPERTY() TArray<int64> PointLevels;
    // The technical floor of a Q/W/E cooldown after haste (not a gameplay cap).
    UPROPERTY() int64 CooldownFloorMs = 0;
};

USTRUCT()
struct FLDDemoVisionRangeParameters
{
    GENERATED_BODY()
    UPROPERTY() FLDDemoRational NeutralHeightCm;
    UPROPERTY() FLDDemoRational FullHeightCm;
    UPROPERTY() int64 UphillRadiusBp = 0;
    UPROPERTY() int64 DownhillRadiusBp = 0;
    bool IsValid() const
    {
        if (NeutralHeightCm.Denominator<=0 || FullHeightCm.Denominator<=0) { return false; }
        const double Neutral=double(NeutralHeightCm.Numerator)/NeutralHeightCm.Denominator;
        const double Full=double(FullHeightCm.Numerator)/FullHeightCm.Denominator;
        return FMath::IsFinite(Neutral) && FMath::IsFinite(Full) && Neutral>=0 && Full>Neutral
            && UphillRadiusBp>0 && UphillRadiusBp<=10000 && DownhillRadiusBp>=10000 && DownhillRadiusBp<=MAX_int32;
    }
};

// Auto-empower (自动强化, EMPOWER.*): an isolated experiment, off unless Enabled (or the authority's -LDEmpower switch). While
// on, learned Q/W/E cost no momentum and the combat gain is CombatPerSec; an own action of the owner's host started on a bar
// holding ThresholdP2 is empowered and pays CostP2 at its release. The rest are the five processors' and the element layer's
// terms (docs/systems/COMBAT_AND_GROWTH.md "自动强化实验").
USTRUCT()
struct FLDDemoEmpowerParameters
{
    GENERATED_BODY()

    UPROPERTY() bool bEnabled = false;
    UPROPERTY() int64 ThresholdP2 = 0;
    UPROPERTY() int64 CostP2 = 0;
    UPROPERTY() FLDDemoRational CombatPerSec;
    // MOMENTUM.SkillHit reaches at most this many bodies of one release.
    UPROPERTY() int32 SkillHitTargets = 0;
    // MOMENTUM.Reaction of a reaction a periodic, zone or ground hit set off (a player's own hit keeps MOMENTUM.Reaction).
    UPROPERTY() int64 DerivedReactionP2 = 0;
    // Momentum per percent of max HP lost to a hostile hit (0 = off; an A/B switch).
    UPROPERTY() int64 DamagedPerPercentP2 = 0;
    // Strike: the readable windup an empowered strike adds.
    UPROPERTY() int64 StrikeWindupMs = 0;
    // Fist: the empowered punch strikes a cone ahead (others at FistConeRatioBp of its own package) and pushes it away.
    UPROPERTY() FLDDemoRational FistConeLengthCm;
    UPROPERTY() FLDDemoRational FistConeAngleDegrees;
    UPROPERTY() int64 FistConeRatioBp = 0;
    UPROPERTY() FLDDemoRational FistPushCm;
    // Move: an empowered dash passes through at most DashContactCap bodies and quakes where it ends.
    UPROPERTY() int32 DashContactCap = 0;
    UPROPERTY() FLDDemoRational QuakeRadiusCm;
    UPROPERTY() int64 QuakeSlowBp = 0;
    UPROPERTY() int64 QuakeSlowMs = 0;
    // Strike, guard and ward shapes (areas, lengths, angles, windows, lives) scale by ShapeScaleBp; chain bounces and
    // empowered-basic charges gain ExtraCount.
    UPROPERTY() int64 ShapeScaleBp = 0;
    UPROPERTY() int32 ExtraCount = 0;
    // Guard: an empowered shield bursts GuardBurstRatioBp of its grant over GuardBurstRadiusCm when it breaks or lapses.
    UPROPERTY() int64 GuardBurstRatioBp = 0;
    UPROPERTY() FLDDemoRational GuardBurstRadiusCm;
    // Ward: an empowered trap's fuse.
    UPROPERTY() int64 WardFuseMs = 0;
    // Form changes (AUTO_EMPOWER_SPEC §2.3): a strike's native element leaves its ground state for GroundLifeMs; a push
    // strike stuns on a wall, a parry stuns its attacker and a cordon stuns at the first touch; a cone opens ConeAngleScaleBp
    // wider.
    UPROPERTY() int64 GroundLifeMs = 0;
    UPROPERTY() int64 WallStunMs = 0;
    UPROPERTY() int64 CounterStunMs = 0;
    UPROPERTY() int64 TouchStunMs = 0;
    UPROPERTY() int64 ConeAngleScaleBp = 0;
    // The element layer (fire burn layers, water slow, shock discharge share of R, rot duration scale, chill root).
    UPROPERTY() int64 FireBurnStacks = 0;
    UPROPERTY() int64 WaterSlowBp = 0;
    UPROPERTY() int64 WaterSlowMs = 0;
    UPROPERTY() int64 ShockReactionBp = 0;
    UPROPERTY() int64 RotDurationBp = 0;
    UPROPERTY() int64 ChillRootMs = 0;
};

USTRUCT()
struct FLDDemoContentParameters
{
    GENERATED_BODY()

    UPROPERTY() FLDDemoP2Bounds MaxHP;
    UPROPERTY() FLDDemoP2Bounds AD;
    UPROPERTY() FLDDemoP2Bounds AP;
    UPROPERTY() FLDDemoP2Bounds Armor;
    UPROPERTY() FLDDemoP2Bounds MagicResist;
    UPROPERTY() FLDDemoRateBounds HPRegen;
    UPROPERTY() FLDDemoRationalBounds AttackSpeed;
    UPROPERTY() int64 AttackSpeedPerLevelBp = 0;
    UPROPERTY() int64 CritChanceBaseBp = 0;
    UPROPERTY() int64 CritMultiplierBp = 0;
    UPROPERTY() int64 CooldownHasteBaseP2 = 0;
    // G(L) = 1 + SkillGrowthPerLevelBp x (L - 1).
    UPROPERTY() int64 SkillGrowthPerLevelBp = 0;
    UPROPERTY() FLDDemoSkillRankParameters SkillRanks;
    UPROPERTY() FLDDemoMomentumParameters Momentum;
    UPROPERTY() FLDDemoComboParameters Combo;
    UPROPERTY() int64 CooldownHasteMaxP2 = 0;
    UPROPERTY() FLDDemoRationalBounds MoveSpeedCmPerSec;
    UPROPERTY() FLDDemoRational CollisionRadiusCm;
    UPROPERTY() FLDDemoRationalBounds AttackRangeCm;
    UPROPERTY() int64 BasicAttackWindupMs = 0;
    UPROPERTY() FLDDemoRational VisionRadiusCm;
    UPROPERTY() FLDDemoVisionRangeParameters VisionRange;
    UPROPERTY() FLDDemoRational CaptureRadiusCm;
    UPROPERTY() int64 CaptureSprayMs = 0;
    // VISION.SightGraceSec: a unit an obstacle just hid stays observed this long (not across range, isolation or a teleport).
    UPROPERTY() int64 VisionSightGraceMs = 0;
    UPROPERTY() int64 CaptureGuardMs = 0;
    // CAPTURE.GuardCheckpointSec: an owner whose guard passed this far and is painted over keeps a checkpoint for its next graffiti.
    UPROPERTY() int64 CaptureGuardCheckpointMs = 0;
    // CAPTURE.GuardResumeSec: the guard the next own graffiti of that owner starts from.
    UPROPERTY() int64 CaptureGuardResumeMs = 0;
    UPROPERTY() int64 ShowdownUnlockDelayMs = 0;
    UPROPERTY() int64 ShowdownDurationMs = 0;
    UPROPERTY() int64 ShowdownRespawnLimit = 0;
    UPROPERTY() int64 ShowdownFirstRespawnMs = 0;
    UPROPERTY() int64 ShowdownSecondRespawnMs = 0;
    UPROPERTY() int64 ShowdownRespawnCutoffMs = 0;
    UPROPERTY() int64 ShowdownClosureStartsMs = 0;
    UPROPERTY() TArray<int64> ShowdownSprayRatchetMs;
    UPROPERTY() TArray<int64> ShowdownGuardRatchetMs;
    UPROPERTY() int64 ShowdownKillGuardBonusMs = 0;
    // #198: each of the three field segments; the field progress T only advances in the field.
    UPROPERTY() int64 FieldSegmentMs = 0;
    // #198 rest phases: R0 and R1-R3 build windows whose last RestSignupMs double as PK sign-up, then draw, betting,
    // PK ready lock, PK cap, settlement, payout and the return-to-field countdown.
    UPROPERTY() int64 RestOpeningBuildMs = 0;
    UPROPERTY() int64 RestBuildMs = 0;
    UPROPERTY() int64 RestFinalBuildMs = 0;
    UPROPERTY() int64 RestSignupMs = 0;
    UPROPERTY() int64 RestDrawMs = 0;
    UPROPERTY() int64 RestBettingMs = 0;
    UPROPERTY() int64 RestPKReadyMs = 0;
    UPROPERTY() int64 RestPKMaxMs = 0;
    UPROPERTY() int64 RestSettlementMs = 0;
    UPROPERTY() int64 RestPayoutMs = 0;
    UPROPERTY() int64 RestReturnMs = 0;
    // #198 approved outer parameters §1: a PK start or field return whose bodies cannot all stand waits at most this long
    // after its boundary (then the PK is NoContest and an unready field return is an explicit fault).
    UPROPERTY() int64 RestEntryReadyTimeoutMs = 0;
    // #198: an occupied pre-rest foot point is replaced by a free spot on the same reachable surface within this radius.
    // #198 PK ring centred on the capture anchor: PKArenaRadiusCm until PKRingShrinkStartMs, linear to PKRingMidRadiusCm
    // at PKRingMidMs, linear to PKRingFinalRadiusCm at PKRingFinalMs, then held until RestPKMaxMs. The next ring is
    // shown PKRingPreviewMs ahead. Ring damage is the start-of-PK max HP times the early/mid/final share per second of
    // real outside time ([0, Mid), [Mid, Final), [Final, Max)), settled every PKRingTickMs.
    UPROPERTY() FLDDemoRational PKArenaRadiusCm;
    UPROPERTY() FLDDemoRational PKRingMidRadiusCm;
    UPROPERTY() FLDDemoRational PKRingFinalRadiusCm;
    UPROPERTY() int64 PKRingShrinkStartMs = 0;
    UPROPERTY() int64 PKRingMidMs = 0;
    UPROPERTY() int64 PKRingFinalMs = 0;
    UPROPERTY() int64 PKRingPreviewMs = 0;
    UPROPERTY() int64 PKRingTickMs = 0;
    UPROPERTY() int64 PKRingEarlyDamageBp = 0;
    UPROPERTY() int64 PKRingMidDamageBp = 0;
    UPROPERTY() int64 PKRingFinalDamageBp = 0;
    // #198/#225 PK format and points: the three base award pools are multiplied by PKRewardMultiplierBp, rounded up,
    // then split with serial rotation; participation is based on eligible entrants, wins use ceil(N/2) capped at
    // PKWinnerPointsMax, and betting uses min(PKBettingPoolMaxPoints, eligible spectators).
    UPROPERTY() int64 PKTeamFormatBp = 0;
    UPROPERTY() int64 PKRewardMultiplierBp = 0;
    UPROPERTY() int64 PKParticipationPoints = 0;
    UPROPERTY() int64 PKWinnerPointsMax = 0;
    UPROPERTY() int64 PKBettingPoolMaxPoints = 0;
    // #198 spectator throw from the fixed off-arena throw anchor: ThrowCooldownMs from a successful throw,
    // ThrowTelegraphMs warning, ThrowRadiusCm impact within ThrowRangeCm, ThrowSlowBp slow for ThrowSlowMs and
    // Exposed for ThrowExposeMs.
    UPROPERTY() int64 ThrowCooldownMs = 0;
    UPROPERTY() int64 ThrowTelegraphMs = 0;
    UPROPERTY() FLDDemoRational ThrowRadiusCm;
    UPROPERTY() FLDDemoRational ThrowRangeCm;
    UPROPERTY() int64 ThrowSlowBp = 0;
    UPROPERTY() int64 ThrowSlowMs = 0;
    UPROPERTY() int64 ThrowExposeMs = 0;
    // The final showdown ring: ShowdownRingRadiusCm at the closure start, shrinking linearly to ShowdownRingFinalRadiusCm at
    // the end; outside it a contender loses the early share of its max HP per second in the first half of the closure and
    // the late share in the second, settled every ShowdownRingTickMs.
    UPROPERTY() FLDDemoRational ShowdownRingRadiusCm;
    UPROPERTY() FLDDemoRational ShowdownRingFinalRadiusCm;
    // The shrink is previewed ShowdownRingPreviewMs before the closure starts.
    UPROPERTY() int64 ShowdownRingPreviewMs = 0;
    UPROPERTY() int64 ShowdownRingTickMs = 0;
    UPROPERTY() int64 ShowdownRingEarlyDamageBp = 0;
    UPROPERTY() int64 ShowdownRingLateDamageBp = 0;
    // SHOWDOWN.KillHealMaxHPRatio: a contender kill in the final field heals the killer this share of its maximum HP.
    UPROPERTY() int64 ShowdownKillHealMaxHPBp = 0;
    // SHOWDOWN.KillMomentum: and gives this much momentum.
    UPROPERTY() int64 ShowdownKillMomentumP2 = 0;
    // Spec §10.4 (P6): a field respawn waits RespawnBaseMs + RespawnPerLevelMs x level; the dead contender chooses among
    // SpawnChoiceCount distinct spawn anchors within SpawnChoiceMs (else the first).
    UPROPERTY() int64 RespawnBaseMs = 0;
    UPROPERTY() int64 RespawnPerLevelMs = 0;
    UPROPERTY() int32 SpawnChoiceCount = 0;
    UPROPERTY() int64 SpawnChoiceMs = 0;
    UPROPERTY() int64 ProtectionMs = 0;
    // D21: every contender's recovery, a per-second share of its current maximum HP once this long without combat.
    UPROPERTY() int64 OutOfCombatRegenDelayMs = 0;
    UPROPERTY() int64 OutOfCombatMaxHPRegenBp = 0;
    // D21 (#79): fast recovery for a contender whose foot stands inside a public dorm disk once this long without combat.
    UPROPERTY() FLDDemoRational DormRadiusCm;
    UPROPERTY() int64 DormRegenDelayMs = 0;
    UPROPERTY() int64 DormMaxHPRegenBp = 0;
    UPROPERTY() FLDDemoRational CampResetArrivalRadiusCm;
    // #28: a Returning member with this many consecutive refused path plans, or this long without path progress
    // while free to move, resets where it stands instead of retrying forever.
    UPROPERTY() int64 CampReturnDeadEndPlans = 0;
    UPROPERTY() int64 CampReturnDeadEndMs = 0;
    // #97 camp growth (Development/LDCampGrowth.h): a camp life spawned t after T=0 takes stage
    // Camp growth is fixed for the field segment; each stage adds these shares of the base values.
    UPROPERTY() int64 CampGrowthStagesPerSegment = 0;
    UPROPERTY() int64 CampGrowthMaxStage = 0;
    UPROPERTY() int64 CampGrowthHPPerStageBp = 0;
    UPROPERTY() int64 CampGrowthAttackPerStageBp = 0;
    UPROPERTY() int64 CampGrowthResistPerStageP2 = 0;
    UPROPERTY() int64 CampGrowthRewardXPPerStageBp = 0;
    // Recovery packs share the same maximum-HP ratio, flat heal and pickup radius.
    UPROPERTY() int64 CampClearHealMaxBp = 0;
    UPROPERTY() int64 CampClearHealFlatP2 = 0;
    UPROPERTY() FLDDemoRational CampClearOrbRadiusCm;
    UPROPERTY() int64 FieldSpawnPoolMin = 0;
    UPROPERTY() int64 FieldMapRecoveryCount = 0;
    UPROPERTY() FLDDemoRational FieldSpawnSeparationCm;
    UPROPERTY() FLDDemoRational FieldSpawnCaptureDistanceCm;
    UPROPERTY() FLDDemoRational FieldSpawnPatrolMarginCm;
    UPROPERTY() FLDDemoRational FieldMapRecoverySeparationCm;
    UPROPERTY() FLDDemoRational FieldMapRecoverySightCm;
    UPROPERTY() FLDDemoRational AirdropFieldClearanceCm;
    UPROPERTY() FLDDemoRational AirdropFieldSeparationCm;
    // CAMP.ClearMomentum: and gains this much momentum.
    UPROPERTY() int64 CampClearMomentumP2 = 0;
    // CAMP.ClearOrbSec: how long a camp clear recovery orb lies on the ground before it fades (field progress T).
    UPROPERTY() int64 CampClearOrbMs = 0;
    // D26 (#79) / #198 outer §5 airdrop: the drops land at the explicit field progress instants AirdropLandMs (strictly
    // increasing), each public AnnounceLead earlier, and leave unclaimed AirdropExpireMs after landing.
    UPROPERTY() TArray<int64> AirdropLandMs;
    UPROPERTY() int64 AirdropExpireMs = 0;
    UPROPERTY() int64 AirdropAnnounceLeadMs = 0;
    UPROPERTY() FLDDemoRational AirdropPickupRadiusCm;
    UPROPERTY() int64 AirdropRewardXP = 0;
    UPROPERTY() int64 AirdropHealMaxHPBp = 0;
    // The momentum a claimed airdrop adds (AIRDROP.Momentum).
    UPROPERTY() int64 AirdropMomentumP2 = 0;
    UPROPERTY() int64 AirdropMoveSpeedBp = 0;
    UPROPERTY() int64 AirdropMoveSpeedMs = 0;
    // #198 outer §5 camp markers: in each timed field segment a High and an Elite camp are marked CampMarkRevealMs of
    // field progress after the segment starts (World/LDFieldMarkers.h).
    UPROPERTY() int64 CampMarkRevealMs = 0;
    // D27 (#79) 上热搜: the leader is named every LeaderRefreshMs; see World/LDLeaderNotice.h.
    UPROPERTY() int64 LeaderMinLevel = 0;
    UPROPERTY() int64 LeaderRefreshMs = 0;
    // LEADER.SurvivalPointSec: the trending leader earns one stat point for every this long it stays the leader alive.
    UPROPERTY() int64 LeaderSurvivalPointMs = 0;
    UPROPERTY() FLDDemoRational LeaderCircleRadiusCm;
    UPROPERTY() FLDDemoRational LeaderCircleMaxOffsetCm;
    // #147 build v7 keywords (one shared record per target): breach takes BreachPerStack off armor and magic resist
    // per stack, up to BreachMaxStacks, for BreachMs refreshed whole by every new stack; burn deals BurnPerSec plus
    // BurnAPRatio x the applier's AP per second per layer (snapshotted per layer), every BurnTickMs from the layer's own
    // application for BurnMs, up to BurnMaxStacks layers (#198). Lifesteal has no cap.
    UPROPERTY() int64 BreachPerStackP2 = 0;
    UPROPERTY() int64 BreachMaxStacks = 0;
    UPROPERTY() int64 BreachMs = 0;
    UPROPERTY() int64 BurnPerSecP2 = 0;
    UPROPERTY() int64 BurnAPRatioBp = 0;
    UPROPERTY() int64 BurnTickMs = 0;
    UPROPERTY() int64 BurnMs = 0;
    UPROPERTY() int64 BurnMaxStacks = 0;
    UPROPERTY() int64 KeystonePhysiqueHealToShieldBp = 0;
    UPROPERTY() int64 KeystonePhysiqueOutOfCombatCutBp = 0;
    UPROPERTY() int64 KeystonePhysiqueShieldMaxHPBp = 0;
    UPROPERTY() int64 KeystonePhysiqueShieldDurationMs = 0;
    UPROPERTY() int64 KeystonePhysiqueIntervalMs = 0;
    UPROPERTY() int64 KeystoneAgilityDashCooldownCutBp = 0;
    UPROPERTY() int64 KeystoneThickSkinGuardP2 = 0;
    UPROPERTY() int64 KeystoneThickSkinGuardCapBp = 0;
    UPROPERTY() int64 KeystoneThickSkinIntervalMs = 0;
    // COMBAT.KeystoneThickSkinSteadySec: after the thick-skin guard triggers, slows miss and knockbacks are cut this long.
    UPROPERTY() int64 KeystoneThickSkinSteadyMs = 0;
    // COMBAT.KeystoneThickSkinKnockbackRatio: the share of a knockback distance kept during the thick-skin steady window.
    UPROPERTY() int64 KeystoneThickSkinKnockbackBp = 0;
    UPROPERTY() int64 KeystoneThickSkinBleedThresholdBp = 0;
    UPROPERTY() int64 KeystoneThickSkinBleedMs = 0;
    UPROPERTY() int64 KeystoneThickSkinBleedTickMs = 0;
    UPROPERTY() int64 KeystoneStrengthDamageBp = 0;
    UPROPERTY() int64 KeystoneBrainsMomentumRefundBp = 0;
    UPROPERTY() int64 KeystoneBrainsReactionMomentumP2 = 0;
    UPROPERTY() int64 KeystoneBrainsCooldownCutMs = 0;
    UPROPERTY() int64 KeystoneAgilityDamageBaseP2 = 0;
    UPROPERTY() int64 KeystoneAgilityADRatioBp = 0;
    UPROPERTY() int64 KeystoneAgilityEmpowerDurationMs = 0;
    // COMBAT.KeystoneAgilityMoveSpeedRatio: the move speed bonus the agility keystone grants for its empower window after a dash.
    UPROPERTY() int64 KeystoneAgilityMoveSpeedBp = 0;
    // Elements (CONTENT_AND_BALANCE §4.4, LDCombatElements): one element per target. Burn layers each last BurnMs from their
    // own application and tick every BurnTickMs. Wet lasts WetMs and slows by WetSlowBp. R = ReactionBase + ReactionPerLevel x
    // (L - 1) + ReactionAPRatio x AP of the reaction's trigger. Fog (fire + water): a FogRadiusCm zone for FogMs giving wet
    // every FogIntervalMs to element-free enemies; conduct (water + shock): the primary slowed by ConductSlowBp for
    // ConductSlowMs, the other wet enemies within ConductRadiusCm take R; overload (fire + shock): OverloadBreachStacks of
    // breach, the others within OverloadRadiusCm take R. The other element and reaction terms are Elements, ground states
    // Ground.
    UPROPERTY() int64 WetMs = 0;
    UPROPERTY() int64 WetSlowBp = 0;
    UPROPERTY() int64 ReactionBaseP2 = 0;
    UPROPERTY() int64 ReactionPerLevelP2 = 0;
    UPROPERTY() int64 ReactionAPRatioBp = 0;
    // After a reaction on a body no other reaction happens on it for this long; a different element is not applied meanwhile.
    UPROPERTY() int64 ReactionLockMs = 0;
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
    UPROPERTY() int64 XPSteepFromLevel = 5;
    UPROPERTY() int64 XPSteepExtraPerLevel = 0;
    UPROPERTY() int64 PlayerXPBase = 0;
    // REWARD.BountyXP: extra XP for the killer of a contender shown trending (上热搜) at its death.
    UPROPERTY() int64 BountyXP = 0;
    // REWARD.BountyCardOffer: the size of the card offer the bounty killer picks one from.
    UPROPERTY() int64 BountyCardOffer = 0;
    UPROPERTY() int64 PlayerXPPerLevel = 0;
    // Contender kills only: a contribution within this window before the death earns an assist; the killer and the
    // assisters split the kill XP.
    UPROPERTY() int64 AssistWindowMs = 0;
    // REWARD.AssistXPRatio: each assister of a contender kill takes this share of its XP; the killer keeps the whole.
    UPROPERTY() int64 AssistXPRatioBp = 0;
    // REWARD.LevelGapXP*: contender kill XP scales by 1 - Step per level the killer is above the victim, clamped to [Min, Max].
    UPROPERTY() int64 LevelGapXPStepBp = 0;
    UPROPERTY() int64 LevelGapXPMinBp = 0;
    UPROPERTY() int64 LevelGapXPMaxBp = 0;
    // REWARD.KillMedalChance: the chance a contender kill outside the final field drops an element medal for the killer.
    UPROPERTY() int64 KillMedalChanceBp = 0;
    // #198 points: OpeningPoints at BeginMatch, PointsPerLevel for every level crossed up to the cap, then MaxLevelPoints
    // for every full MaxLevelPointsXP past the top threshold. A build change commits only BuildSafeWindowMs after the
    // contender's last combat action (GROWTH.BuildSafeWindowSec).
    UPROPERTY() int64 OpeningPoints = 0;
    UPROPERTY() int64 PointsPerLevel = 0;
    UPROPERTY() int64 MaxLevelPointsXP = 0;
    UPROPERTY() int64 MaxLevelPoints = 0;
    UPROPERTY() int64 KeystoneThresholdPoints = 0;
    UPROPERTY() int64 BuildSafeWindowMs = 0;
    // Stat points for skipping one active offer (GROWTH.ActiveSkipPoints).
    UPROPERTY() int64 ActiveSkipPoints = 0;
    // Stat points for selling one pending blueprint (GROWTH.BlueprintSellPoints).
    UPROPERTY() int64 BlueprintSellPoints = 0;
    UPROPERTY() FLDDemoAIParameters AI;
    UPROPERTY() FLDDemoCardParameters Cards;
    UPROPERTY() FLDDemoShopParameters Shop;
    UPROPERTY() FLDDemoObjectiveParameters Objective;
    UPROPERTY() FLDDemoDailyParameters Daily;
    UPROPERTY() FLDDemoAffixParameters Affix;
    UPROPERTY() FLDDemoExploreParameters Explore;
    UPROPERTY() FLDDemoMedalParameters Medal;
    UPROPERTY() FLDDemoElementParameters Elements;
    UPROPERTY() FLDDemoGroundParameters Ground;
    // Spec §3.7: the product of a hit's damage factors stops at DamageFactorTechCapBp (DAMAGE.FactorTechCap, a technical
    // guard logged as LD_DAMAGE_FACTOR_CAPPED). Spec §3.8: an effect chain stops deriving past ChainMaxDepth steps and one
    // authority step derives at most ChainMaxEffectsPerStep effects (the rest wait for the next step), logged LD_CHAIN_GUARD.
    UPROPERTY() int64 DamageFactorTechCapBp = 0;
    UPROPERTY() int32 ChainMaxDepth = 0;
    UPROPERTY() int32 ChainMaxEffectsPerStep = 0;
    // Spec §6.5: a chain ends ChainEndMs after its last step (CHAIN.EndSec); its key counts from ChainMinKeySteps kinds.
    UPROPERTY() int64 ChainEndMs = 0;
    UPROPERTY() int32 ChainMinKeySteps = 0;
    // EMPOWER.*: the auto-empower experiment (LDAutoEmpower); inert while it is off.
    UPROPERTY() FLDDemoEmpowerParameters Empower;
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
    CarrySubP2Remainder, StackToLatestExpiry, ConfirmedCombatHit, ReturnThenReset, NotApplicable
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
    UPROPERTY() ELDDemoPolicy CampMomentumPolicy = ELDDemoPolicy::None;
};

// #198: DirectionLine (S09) is a straight band of RangeCm x 2 LineHalfWidthCm in the requested direction; appended last.
UENUM()
enum class ELDDemoTargetMode : uint8 { Self, Unit, DirectionProjectile, DirectionDash, GroundPoint, DirectionCone, DirectionLine };

// When a triggered effect fires: cooldown_refund on a landed primary basic (BasicHit), triggered_shield on an ability hit
// (AbilityHit) or when the holder took positive damage (Damaged).
// #198 B: DashComplete arms an empower_basic of a component when a voluntary dash (D or an active dash) completes with
// a positive displacement (B12).
UENUM()
// AttackSpeedBelow / MoveSpeedAbove: which speed a speed_factor card reads (A10, Y18).
enum class ELDDemoEffectTrigger : uint8 { None, BasicHit, AbilityHit, Damaged, DashComplete, AttackSpeedBelow, MoveSpeedAbove };

// The element a card effect, branch or Y25 choice names (content "fire|water|electric|chill|rot"; electric is the shock
// element). Fire is burn stacks; the others are LDCombatElements marks.
UENUM()
enum class ELDDemoElement : uint8 { None = 0, Fire = 1, Water = 2, Electric = 3, Chill = 4, Rot = 5 };

// The element an active carries (its own or a branch's).
UENUM()
enum class ELDDemoSkillElement : uint8 { None = 0, Fire = 1, Water = 2, Shock = 3, Chill = 4, Rot = 5 };

// How an active reaches its targets (targeted / projectile / area / dash / self).
UENUM()
enum class ELDDemoHitType : uint8 { Targeted = 0, Projectile = 1, Area = 2, Dash = 3, Self = 4 };

// A skill's branch path (issue #287 spec §3.6): rank 2 is A or B, rank 3 is A1/A2 under A or B1/B2 under B; rank 1 has none.
UENUM()
enum class ELDSkillBranch : uint8 { None = 0, A = 1, B = 2, A1 = 3, A2 = 4, B1 = 5, B2 = 6 };

// Which hits a vulnerable target takes more from: every hit, skill hits only or basic hits only.
UENUM()
enum class ELDDemoDamageScope : uint8 { All = 0, Skill = 1, Basic = 2 };

// The control a recast lands: the first release's own control, a root or a disarm.
UENUM()
enum class ELDDemoRecastControl : uint8 { Same = 0, Root = 1, Disarm = 2 };

// Spec §6.1 card forms: where a card installs (one learned Q/W/E skill, the D dash or the body, which also carries the basic
// attack). A medal is an element medal: it sits in the element slot of a learned skill or of the body, outside the workshop's
// capacity, one per slot. 0 (the former basic form) is retired.
UENUM()
enum class ELDCardForm : uint8 { Skill = 1, Dash = 2, Body = 3, Medal = 4, Count UMETA(Hidden) };

// Spec §6.2 card types; the interface draws each as its own shape.
UENUM()
enum class ELDCardType : uint8 { Trigger = 0, Modify = 1, Convert = 2, Grow = 3, Element = 4, Count UMETA(Hidden) };

// Spec §6.3 vocabulary keys, one bit each of a card's or skill's VocabMask (hidden from the interface; offer weights and
// the AI read them).
enum class ELDCardVocab : uint8
{
    BasicHit, Combo3, SkillHit, SkillCast, Reaction, Element, Dash, DashContact, Knockback,
    WallImpact, Shield, ShieldBreak, Controlled, Control, Damaged, LowHP, Kill, Idle, Ground, Heal, Momentum, AttackSpeed,
    MoveSpeed, Crit, Armor, MaxHP, Area, Range, Count
};

UENUM()
enum class ELDDemoDamageType : uint8 { Physical, Magical };

UENUM()
// Boss is the D26 画饼老板's monster row: one roaming body, never placed in a camp. Elite (#97) is the third camp tier,
// appended after Boss so existing values keep their numbers.
enum class ELDDemoCampTier : uint8 { Low, High, Boss, Elite, Ancient };

UENUM()
enum class ELDMonsterPattern : uint8 { Basic, Enrage, Cone, Charge, Projectile, Guard, Circle, SweepLine, EchoCircle, Fan, DoubleCircle, RotatingCone };

USTRUCT()
struct FLDDemoMonsterSkill
{
    GENERATED_BODY()
    UPROPERTY() FString Name;
    UPROPERTY() ELDMonsterPattern Pattern = ELDMonsterPattern::Basic;
    UPROPERTY() int64 Every = 0;
    UPROPERTY() int64 WindupMs = 0;
    UPROPERTY() int64 RecoveryMs = 0;
    UPROPERTY() int64 RatioBp = 0;
    UPROPERTY() FLDDemoRational RangeCm;
    UPROPERTY() FLDDemoRational RadiusCm;
    UPROPERTY() int64 AngleDegrees = 0;
    UPROPERTY() int64 Pulses = 0;
    UPROPERTY() int64 PulseIntervalMs = 0;
    UPROPERTY() int64 PulseAngleDegrees = 0;
    UPROPERTY() FLDDemoRational OffsetCm;
    UPROPERTY() int64 PassiveBp = 0;
    UPROPERTY() int64 EvolvedPassiveBp = 0;
    UPROPERTY() int64 LowHPThresholdBp = 0;
    UPROPERTY() int64 LowHPHasteBp = 0;
    UPROPERTY() int64 GuardDurationMs = 0;
    UPROPERTY() int64 EvolveStage = 0;
    UPROPERTY() int64 EchoStage = 0;
    UPROPERTY() int64 EchoDelayMs = 0;
    UPROPERTY() int64 EchoRatioBp = 0;
    UPROPERTY() int64 EvolvedSlowBp = 0;
    UPROPERTY() int64 EvolvedSlowMs = 0;
    UPROPERTY() FLDDemoRational ProjectileSpeedCmPerSec;
};

UENUM()
enum class ELDDemoBonusStat : uint8
{
    ADFlat, APFlat, MaxHPFlat, ArmorFlat, MagicResistFlat, CooldownHasteFlat,
    AttackRangeCmFlat, AttackSpeedBonusRatio, MoveSpeedBonusRatio, CritChanceBonusRatio,
    // Percent of the projected AD; only conditional and timed bonuses may use it (floored to P2 when applied).
    ADBonusRatio,
    // Always-on HP regeneration in hp/s (an exact rational, stored in FLDDemoStatBonus::Cm like the range).
    HPRegenFlat,
    // Percent of the projected AP; timed bonuses only (the D26 Boss approval buff), floored to P2 when applied.
    APBonusRatio,
    // In-combat momentum gain in momentum/s added to MOMENTUM.CombatPerSec (an exact rational in FLDDemoStatBonus::Cm).
    MomentumRegenFlat
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
    UPROPERTY() uint16 Opcode = 0;
    UPROPERTY() int64 BaseP2 = 0;
    UPROPERTY() int64 ADRatioBp = 0;
    UPROPERTY() int64 APRatioBp = 0;
    UPROPERTY() int64 AdaptiveRatioBp = 0;
    // Damage only: coefficient of the caster's own max HP, snapshotted at release.
    UPROPERTY() int64 MaxHPRatioBp = 0;
    // Coefficient of (Armor + MagicResist) on heavy shields, slams and parries (#345).
    UPROPERTY() int64 ResistRatioBp = 0;
    // Parry missing-HP heal share (#345).
    UPROPERTY() int64 HealMissingBp = 0;
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
    // Generic trigger/entry selectors of triggered effects (cooldown_refund, triggered_shield).
    UPROPERTY() ELDDemoEffectTrigger Trigger = ELDDemoEffectTrigger::None;
    // A cooldown cut (hit_haste_other, reaction_haste, basic_cooldown_cut).
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
    // dash_contact_refund caps actual momentum grants; this cap never grows with level. brace: the record cap (grows with G).
    UPROPERTY() int64 LimitP2 = 0;
    // dash_contact: every body the dash actually touches takes the native hit once (otherwise only the first Count).
    UPROPERTY() bool bEveryContact = false;
    // overcap_extra_hit (A09): every StepRate attacks per second past the attack-speed maximum adds RatioBp extra-hit chance.
    UPROPERTY() FLDDemoRational StepRate;
    // #198 B basic_element: the element a primary basic applies (Water/Electric/Fire); None for every other operation.
    UPROPERTY() ELDDemoElement Element = ELDDemoElement::None;
    // #198 F workshop terms: a cone angle (spread_unit, compress_cone) and a momentum-cost multiplier (cooldown_cost).
    UPROPERTY() FLDDemoRational AngleDegrees;
    UPROPERTY() int64 CostRatioBp = 0;
    // Skill branch terms (issue #287 spec §5.1; field use per op in docs/systems/COMBAT_AND_GROWTH.md): a third duration
    // (wall/center/recast stun, last-charge stun, dash ghost, free cast window), a second distance (trap wire), the scope and
    // caster-only flag of a vulnerability, a recast's control and the remaining branch switches.
    UPROPERTY() int64 ExtraDurationMs = 0;
    UPROPERTY() FLDDemoRational ExtraDistanceCm;
    UPROPERTY() ELDDemoDamageScope Scope = ELDDemoDamageScope::All;
    UPROPERTY() ELDDemoRecastControl RecastControl = ELDDemoRecastControl::Same;
    UPROPERTY() bool bOwnOnly = false;
    UPROPERTY() bool bOnCatch = false;
    UPROPERTY() bool bCenterOnly = false;
    UPROPERTY() bool bAsDash = false;
    UPROPERTY() bool bFreeDirection = false;
    UPROPERTY() bool bAlwaysCrit = false;
    // Issue #287 P4: the ground state a ground_state effect leaves (ELDGroundStateKind; 0 = none) and whether N2's fan carries
    // its owner's last applied element (3A2).
    UPROPERTY() uint8 GroundKind = 0;
    UPROPERTY() bool bCarryElement = false;
    // #198 F workshop field use (content.json key -> field): damage_ratio MultiplierBp (a damage multiplier of the target's
    // package), ratio RatioBp (a geometry/time multiplier or a share), cooldown_ratio SecondaryRatioBp, cost_ratio CostRatioBp,
    // angle AngleDegrees, range_cap and radius RadiusCm, distance DistanceCm, stun_duration/root_duration SecondaryDurationMs,
    // cap_ratio MultiplierBp (C01's max-HP cap), momentum BaseP2 + momentum_cap LimitP2 (C07), base BaseP2 (a damage/shield base,
    // or the grown cap of C08's store and C11's heal), delay DelayMs, interval IntervalMs, duration DurationMs, count Count.
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
    // inside).
    // #198 B component forms (a component's effects; actives never carry them): burn/breach BasicCount only (stacks per
    // primary basic hit); thorns IntervalMs 0 = no per-attacker repeat window; nth_basic_bonus Count (every Count-th landed
    // primary basic) + DistanceCm + DurationMs (a knockback of the primary target away from the holder, no damage);
    // empower_basic Trigger DashComplete + DurationMs + Count (charges per armed package); ledger BaseP2+ADRatioBp+APRatioBp+
    // DamageType per entry, Count entries cash out, DurationMs without a new entry clears the unlocked entries;
    // basic_element Element; basic_damage_scale RatioBp (0 < ratio < 1, the primary basic's own package only);
    // idle_ready_amp DelayMs (ready and idle) + RatioBp (next native direct damage bonus); active_element no fields (the
    // install binds the active and element, FLDDemoComponentDefinition::Binding).
    // Skill rules on native direct hits (IsNativeDirectHit): cast_hit_refund RatioBp (share of the momentum the release
    // paid, once per release); distance_amp DistanceCm (minimum 2D foot distance) + RatioBp (damage bonus); alternate_amp
    // DurationMs (window after the previous Q/W/E release) + RatioBp (bonus of a different active); triggered_shield
    // AbilityHit uses the Damaged fields (BaseP2, MaxHPRatioBp, DurationMs, IntervalMs).
    // Mods M13-M15 (derived hits of the host release, never deriving further): projectile_return MultiplierBp
    // (share of the return flight); projectile_pierce Count (extra bodies) + MultiplierBp (share per extra body);
    // chain_extra Count (extra chain hops); echo_impact DelayMs + MultiplierBp (share of the echo) + SecondaryRatioBp
    // (host cooldown multiplier).
    // Connectors C13-C16 (host native direct hits; derived hits qualify, per-release budgets bound them):
    // hit_haste_other AmountMs (cut of every other learned active's remaining cooldown, once per release; also a hosted
    // dash's first enemy contact); setup_mark DurationMs (mark on the struck enemy) + RatioBp (bonus of a different
    // active's next native direct hit on it); reaction_haste AmountMs (cut of the host's remaining cooldown when it
    // triggers an element reaction, once per release); multi_hit_momentum BaseP2 (momentum per distinct enemy beyond the
    // first) + LimitP2 (momentum cap per release).
    // Mods M16-M20 (forced moves are real knockbacks of the host's source): zone_tether RatioBp (zone radius
    // share; the zone follows the caster); defense_shove DistanceCm + DurationMs (shove) + RadiusCm (shove reach) +
    // RatioBp (host shield share); final_knockback DistanceCm + DurationMs (last empowered package / final wave knockback)
    // + CostRatioBp (S17 attack-speed share; S22 loses its final slow); empower_reach DistanceCm (basic range while a host
    // package is pending) + MultiplierBp (package share); pull DistanceCm + DurationMs (S15 taunt pull) + RadiusCm
    // (closest centre distance a pull leaves).
    // Connectors C17-C21: zone_relocate no fields (a hosted dash completion moves the holder's newest live zone or
    // armed trap to its foot, once per zone life); stored_force RatioBp (share of the absorbed / braced amount stored) +
    // DurationMs (store window; a different learned active's next native direct hit cashes it); empower_spend no fields
    // (another active's native direct hit spends one pending host package as an added hit); control_widen RatioBp (area
    // bonus of the next different active release) + DurationMs (token window after the host's hard control);
    // zone_detonate RatioBp (share of the zone's remaining damage pulses paid at once).
    // Issue #287 P4 (docs/systems/COMBAT_AND_GROWTH.md): rot RatioBp (heal and shield cut) + DurationMs, 0 = COMBAT.Rot*;
    // chill no fields; ground_state GroundKind + DurationMs (0 = GROUND.*Sec)
    // + RatioBp (radius multiplier, 0 = GROUND.*RadiusCm) + SecondaryRatioBp (the owner's damage factor on enemies standing
    // on it) + MultiplierBp (attack slow of enemies inside); fan DurationMs + IntervalMs (blows) + RadiusCm (rectangle
    // length) + ExtraDistanceCm (rectangle width) + DistanceCm (push) + AngleDegrees (sweep per second either way) +
    // ExtraDurationMs (stun when a blow pushes into a wall) + bCarryElement + BaseP2 (momentum per blow that touches an
    // enemy); channel_cone ExtraDurationMs (a root on the last wave); dash_ice RadiusCm (path width) + DurationMs;
    // ground_relocate and double_stall no fields; ground_growth RatioBp (factor per interval) + IntervalMs.

    ELDDemoEffectOp GetOp() const { return static_cast<ELDDemoEffectOp>(Opcode); }

    // A hard control this effect is itself (root, stun, silence, disarm, taunt).
    bool IsControl() const
    {
        const ELDDemoEffectOp Op = GetOp();
        return Op == ELDDemoEffectOp::Root || Op == ELDDemoEffectOp::Stun || Op == ELDDemoEffectOp::Silence
            || Op == ELDDemoEffectOp::Disarm || Op == ELDDemoEffectOp::Taunt;
    }
    // The duration of a control another delivery carries (a wall stun, a final-wave root, a centre stun, a last-hit stun,
    // a blow's stun, a recast's same-target stun or replaced control, a brace answer's stun); null when it carries none.
    // K05/K11 count these.
    int64* ComposedControlMs()
    {
        switch (GetOp())
        {
        case ELDDemoEffectOp::Knockback: case ELDDemoEffectOp::ChannelCone: case ELDDemoEffectOp::GroundImpact:
        case ELDDemoEffectOp::EmpowerBasic: case ELDDemoEffectOp::Fan: case ELDDemoEffectOp::CordonLine:
            return ExtraDurationMs > 0 ? &ExtraDurationMs : nullptr;
        case ELDDemoEffectOp::Recast:
            return ExtraDurationMs > 0 ? &ExtraDurationMs
                : RecastControl != ELDDemoRecastControl::Same && SecondaryDurationMs > 0 ? &SecondaryDurationMs : nullptr;
        // S16 3A1: the brace answer stuns; backstab lunge stuns from rear/controlled; parry stance disarms on block.
        case ELDDemoEffectOp::BraceStun: case ELDDemoEffectOp::BackstabLunge:
            return DurationMs > 0 ? &DurationMs : nullptr;
        case ELDDemoEffectOp::ParryStance:
            return ExtraDurationMs > 0 ? &ExtraDurationMs : SecondaryDurationMs > 0 ? &SecondaryDurationMs : nullptr;
        default: return nullptr;
        }
    }
    bool HasControl() const { return IsControl() || const_cast<FLDDemoEffectDefinition*>(this)->ComposedControlMs() != nullptr; }
};

// #198: the one value set of an active (no ranks); its fixed amounts grow with the owner's level (LDSkillGrowth).
USTRUCT()
struct FLDDemoAbilityValues
{
    GENERATED_BODY()

    UPROPERTY() int64 MomentumCostP2 = 0;
    UPROPERTY() int64 CooldownMs = 0;
    UPROPERTY() TArray<FLDDemoEffectDefinition> Effects;
};

// Branch path helpers: the rank a path belongs to, its rank step key (2a, 2b, 3a1, 3a2, 3b1, 3b2) and its display row id.
namespace LDSkillBranch
{
    inline uint8 RankOf(ELDSkillBranch Path)
    {
        return Path == ELDSkillBranch::None ? 1 : (Path == ELDSkillBranch::A || Path == ELDSkillBranch::B) ? 2 : 3;
    }
    inline const TCHAR* StepKey(ELDSkillBranch Path)
    {
        switch (Path)
        {
        case ELDSkillBranch::A: return TEXT("2a");
        case ELDSkillBranch::B: return TEXT("2b");
        case ELDSkillBranch::A1: return TEXT("3a1");
        case ELDSkillBranch::A2: return TEXT("3a2");
        case ELDSkillBranch::B1: return TEXT("3b1");
        case ELDSkillBranch::B2: return TEXT("3b2");
        default: return TEXT("");
        }
    }
    inline FString DisplayId(const FString& AbilityId, ELDSkillBranch Path) { return AbilityId + TEXT("_") + StepKey(Path); }
    // The rank-2 parent of a rank-3 path (A1/A2 -> A, B1/B2 -> B); None for any other path.
    inline ELDSkillBranch ParentOf(ELDSkillBranch Path)
    {
        return (Path == ELDSkillBranch::A1 || Path == ELDSkillBranch::A2) ? ELDSkillBranch::A
            : (Path == ELDSkillBranch::B1 || Path == ELDSkillBranch::B2) ? ELDSkillBranch::B : ELDSkillBranch::None;
    }
    // Whether Next is a legal rank-up from Current: None -> A|B, A -> A1|A2, B -> B1|B2.
    inline bool IsNext(ELDSkillBranch Current, ELDSkillBranch Next)
    {
        return Current == ELDSkillBranch::None ? (Next == ELDSkillBranch::A || Next == ELDSkillBranch::B) : ParentOf(Next) == Current && Next != ELDSkillBranch::None;
    }
}

// One branch variant of an active: its own target, windup, element, hit type and value set.
USTRUCT()
struct FLDDemoAbilityBranch
{
    GENERATED_BODY()

    UPROPERTY() ELDSkillBranch Path = ELDSkillBranch::None;
    UPROPERTY() FLDDemoTargetDefinition Target;
    UPROPERTY() int64 WindupMs = 0;
    UPROPERTY() ELDDemoSkillElement Element = ELDDemoSkillElement::None;
    UPROPERTY() ELDDemoHitType HitType = ELDDemoHitType::Targeted;
    // Spec §6.3: the vocabulary the compiler derives from element, delivery, control, displacement and shield (ELDCardVocab bits).
    UPROPERTY() uint32 VocabMask = 0;
    UPROPERTY() FLDDemoAbilityValues Values;
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
    UPROPERTY() ELDDemoSkillElement Element = ELDDemoSkillElement::None;
    UPROPERTY() ELDDemoHitType HitType = ELDDemoHitType::Targeted;
    // Spec §6.3: the level-1 vocabulary (ELDCardVocab bits); a learned branch carries its own.
    UPROPERTY() uint32 VocabMask = 0;
    UPROPERTY() FLDDemoAbilityValues Values;
    // The six branch variants in path order A, B, A1, A2, B1, B2: each is the level-1 definition with its rank steps'
    // patches applied (the content compiler resolves them); level growth and the rank scales apply on top.
    UPROPERTY() TArray<FLDDemoAbilityBranch> Branches;

    ELDDemoAbilityId GetNativeId() const { return static_cast<ELDDemoAbilityId>(NativeId); }
    // The variant a branch path names; null for None or a missing path.
    const FLDDemoAbilityBranch* FindBranch(ELDSkillBranch Path) const
    {
        return Branches.FindByPredicate([Path](const FLDDemoAbilityBranch& Row) { return Row.Path == Path && Path != ELDSkillBranch::None; });
    }
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

// Spec §6-§7: one build card. A card has no rarity: every card is one slot of the workshop capacity and may be owned and
// installed more than once (each copy its own slot; copies add their numbers and fire their triggers once each).
USTRUCT()
struct FLDDemoCardDefinition
{
    GENERATED_BODY()

    UPROPERTY() FString Id;
    UPROPERTY() uint16 NativeId = 0;
    UPROPERTY() ELDCardForm Form = ELDCardForm::Body;
    UPROPERTY() ELDCardType Type = ELDCardType::Trigger;
    // One bit per ELDCardVocab key.
    UPROPERTY() uint32 VocabMask = 0;
    // Y25: the elements an install may choose; empty for every other card.
    UPROPERTY() TArray<ELDDemoElement> ChooseElements;
    // Spec §7.2 skill cards that fit only some hosts: one bit per ELDDemoHitType the host must deliver (0 = any), and a host
    // that controls (root, stun, silence, disarm or taunt in its level-1 value set).
    UPROPERTY() uint8 HostHitTypes = 0;
    UPROPERTY() bool bHostControl = false;
    UPROPERTY() int32 DropWeight = 100;
    // Its relative chance in a card draw (basis points of a plain card); the rare jackpot cards draw less often.
    UPROPERTY() int64 DrawWeightBp = 10000;
    // Fixed amounts grow with the holder's level (LDSkillGrowth) like an active's.
    UPROPERTY() TArray<FLDDemoEffectDefinition> Effects;

    ELDDemoCardId GetNativeId() const { return static_cast<ELDDemoCardId>(NativeId); }
};

// One universal final mod: a public rule rewrite of the showdown win or its flagpole (content §4.7). Each card reads only
// its own terms; the others stay zero.
USTRUCT()
struct FLDDemoFinalModDefinition
{
    GENERATED_BODY()

    UPROPERTY() FString Id;
    UPROPERTY() uint8 NativeId = 0;
    // final_hit_and_run: completed sprays that win.
    UPROPERTY() int32 Sprays = 0;
    // final_hit_and_run: how long its graffiti stands before it fades.
    UPROPERTY() int64 FadeMs = 0;
    // final_kill_seconds: guard per kill; final_private_pole: the private guard to win.
    UPROPERTY() int64 GuardMs = 0;
    // final_private_pole: its spray duration.
    UPROPERTY() int64 SprayMs = 0;
    // final_cage: how long the fence stands, and how much damage its holder takes from contenders before it breaks.
    UPROPERTY() int64 DurationMs = 0;
    UPROPERTY() int64 HPP2 = 0;
    // final_private_pole: its spray zone; final_double_yellow: the main zone while the holder owns it.
    UPROPERTY() FLDDemoRational RadiusCm;

    ELDDemoFinalModId GetNativeId() const { return static_cast<ELDDemoFinalModId>(NativeId); }
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
    // no windup, one value set with no momentum cost, the approved cooldown and a single Dash effect.
    UPROPERTY() FLDDemoDashDefinition Dash;
};

// D26 (#79) 画饼老板 Boss: the boss-only rules. Its body stats are the Monsters row with Id and Tier Boss (aggro radius as
// its perception radius), so Combat, vision and rewards find it like any monster.
USTRUCT()
struct FLDDemoBossDefinition
{
    GENERATED_BODY()

    UPROPERTY() FString Id;
    // #198 outer §5: field progress T of its single appearance and of its departure if it is still alive then.
    UPROPERTY() int64 AppearAtMs = 0;
    UPROPERTY() int64 LeaveAtMs = 0;
    UPROPERTY() FLDDemoRational AggroRadiusCm;
    UPROPERTY() int64 AttackerMemoryMs = 0;
};

USTRUCT()
struct FLDDemoMonsterDefinition
{
    GENERATED_BODY()

    UPROPERTY() FString Id;
    UPROPERTY() uint8 NativeId = 0;
    UPROPERTY() ELDDemoCampTier Tier = ELDDemoCampTier::Low;
    UPROPERTY() FLDDemoMonsterSkill Skill;
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
    UPROPERTY() int64 RewardXP = 0;
    UPROPERTY() FLDDemoRational HPRegenPerSec;
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
    // #198 PK on the original map: the eight ring spawns pk_spawn_0..7 in index order and the fixed off-arena
    // spectator throw anchor; both resolve through ULDMapRegistry::Resolve like the other anchors.
    UPROPERTY() TArray<FString> PKSpawnIds;
    UPROPERTY() FString PKThrowId;
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
    // Spec §7: the build cards, in native id order.
    UPROPERTY() TArray<FLDDemoCardDefinition> Cards;
    UPROPERTY() TArray<FLDDemoFinalModDefinition> FinalMods;
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
    // The display row of a content id (the first one, ids compared without case), or null. The interface names things
    // through this table many times per build and per painted label; game thread only.
    const FLDDemoDisplayDefinition* FindDisplay(const FString& Id) const;

private:
    // Row of each display id, built on the first lookup of a table of DisplayIndexedRows rows. Not part of the content.
    mutable TMap<FString, int32> DisplayIndex;
    mutable int32 DisplayIndexedRows = INDEX_NONE;
};
