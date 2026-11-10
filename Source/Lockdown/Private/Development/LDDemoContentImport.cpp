#include "LDDemoContentJson.h"
#include "LDDemoContentMath.h"
#include "Development/LDDemoCardText.h"
#include "Region/LDMapGeometry.h"

namespace LDDemoContent
{
namespace
{
struct FNativeId
{
    const TCHAR* Category;
    const TCHAR* Id;
    uint8 Number;
};

#define DEMO_ID(Category, Name, Enum, Value) {TEXT(Category), TEXT(Name), uint8(Enum::Value)}
const FNativeId NativeIds[] =
{
    DEMO_ID("character", "character_demo", ELDDemoCharacterId, CharacterDemo),
    DEMO_ID("abilities", "active_pants_down", ELDDemoAbilityId, ActivePantsDown),
    DEMO_ID("abilities", "active_crotch_grab", ELDDemoAbilityId, ActiveCrotchGrab),
    DEMO_ID("abilities", "active_snark_bolt", ELDDemoAbilityId, ActiveSnarkBolt),
    DEMO_ID("abilities", "active_seethe", ELDDemoAbilityId, ActiveSeethe),
    DEMO_ID("abilities", "active_bail_out", ELDDemoAbilityId, ActiveBailOut),
    DEMO_ID("abilities", "active_elbow", ELDDemoAbilityId, ActiveElbow),
    DEMO_ID("abilities", "active_not_mad", ELDDemoAbilityId, ActiveNotMad),
    DEMO_ID("abilities", "active_stare_down", ELDDemoAbilityId, ActiveStareDown),
    DEMO_ID("abilities", "active_quick_meeting", ELDDemoAbilityId, ActiveQuickMeeting),
    DEMO_ID("abilities", "active_account_banned", ELDDemoAbilityId, ActiveAccountBanned),
    DEMO_ID("abilities", "active_unplug", ELDDemoAbilityId, ActiveUnplug),
    DEMO_ID("abilities", "active_triple_combo", ELDDemoAbilityId, ActiveTripleCombo),
    DEMO_ID("abilities", "active_open_mic", ELDDemoAbilityId, ActiveOpenMic),
    DEMO_ID("abilities", "active_calculus_drop", ELDDemoAbilityId, ActiveCalculusDrop),
    DEMO_ID("abilities", "active_sock_shot", ELDDemoAbilityId, ActiveSockShot),
    DEMO_ID("abilities", "active_flyer_trap", ELDDemoAbilityId, ActiveFlyerTrap),
    DEMO_ID("abilities", "active_apology_slide", ELDDemoAbilityId, ActiveApologySlide),
    DEMO_ID("abilities", "active_hose_down", ELDDemoAbilityId, ActiveHoseDown),
    DEMO_ID("abilities", "active_zap", ELDDemoAbilityId, ActiveZap),
    DEMO_ID("abilities", "active_charged_ram", ELDDemoAbilityId, ActiveChargedRam),
    DEMO_ID("abilities", "active_brace", ELDDemoAbilityId, ActiveBrace),
    DEMO_ID("abilities", "active_dirty_puddle", ELDDemoAbilityId, ActiveDirtyPuddle),
    DEMO_ID("stats", "stat_physique", ELDDemoStatId, StatPhysique),
    DEMO_ID("stats", "stat_thick_skin", ELDDemoStatId, StatThickSkin),
    DEMO_ID("stats", "stat_strength", ELDDemoStatId, StatStrength),
    DEMO_ID("stats", "stat_brains", ELDDemoStatId, StatBrains),
    DEMO_ID("stats", "stat_agility", ELDDemoStatId, StatAgility),
    DEMO_ID("components", "component_fist_fire", ELDDemoComponentId, ComponentFistFire),
    DEMO_ID("components", "component_tab_ledger", ELDDemoComponentId, ComponentTabLedger),
    DEMO_ID("components", "component_spiky_hide", ELDDemoComponentId, ComponentSpikyHide),
    DEMO_ID("components", "component_punch_heal", ELDDemoComponentId, ComponentPunchHeal),
    DEMO_ID("components", "component_dash_bump", ELDDemoComponentId, ComponentDashBump),
    DEMO_ID("components", "component_third_punch", ELDDemoComponentId, ComponentThirdPunch),
    DEMO_ID("components", "component_kick_down", ELDDemoComponentId, ComponentKickDown),
    DEMO_ID("components", "component_breach_fist", ELDDemoComponentId, ComponentBreachFist),
    DEMO_ID("components", "component_hard_skin", ELDDemoComponentId, ComponentHardSkin),
    DEMO_ID("components", "component_fist_water", ELDDemoComponentId, ComponentFistWater),
    DEMO_ID("components", "component_fist_shock", ELDDemoComponentId, ComponentFistShock),
    DEMO_ID("components", "component_dash_punch", ELDDemoComponentId, ComponentDashPunch),
    DEMO_ID("components", "component_catch_breath", ELDDemoComponentId, ComponentCatchBreath),
    DEMO_ID("components", "component_long_arm", ELDDemoComponentId, ComponentLongArm),
    DEMO_ID("components", "component_seeing_red", ELDDemoComponentId, ComponentSeeingRed),
    DEMO_ID("components", "component_thick_hide", ELDDemoComponentId, ComponentThickHide),
    DEMO_ID("components", "component_held_shot", ELDDemoComponentId, ComponentHeldShot),
    DEMO_ID("components", "component_dirty_trick", ELDDemoComponentId, ComponentDirtyTrick),
    DEMO_ID("monsters", "monster_scavenger", ELDDemoMonsterId, MonsterScavenger),
    DEMO_ID("monsters", "monster_guardian", ELDDemoMonsterId, MonsterGuardian),
    DEMO_ID("monsters", "monster_dean", ELDDemoMonsterId, MonsterDean),
    DEMO_ID("monsters", "monster_advisor", ELDDemoMonsterId, MonsterAdvisor),
    DEMO_ID("statuses", "root", ELDDemoStatusId, Root),
    DEMO_ID("statuses", "shield", ELDDemoStatusId, Shield),
    DEMO_ID("statuses", "camp_returning", ELDDemoStatusId, CampReturning),
    DEMO_ID("statuses", "respawn_protection", ELDDemoStatusId, RespawnProtection),
    DEMO_ID("statuses", "slow", ELDDemoStatusId, Slow),
    DEMO_ID("statuses", "stat_boost", ELDDemoStatusId, StatBoost),
    DEMO_ID("statuses", "exposed", ELDDemoStatusId, Exposed),
    DEMO_ID("statuses", "taunt", ELDDemoStatusId, Taunt),
    DEMO_ID("statuses", "disarm", ELDDemoStatusId, Disarm),
    DEMO_ID("statuses", "stun", ELDDemoStatusId, Stun),
    DEMO_ID("statuses", "empowered", ELDDemoStatusId, Empowered),
    DEMO_ID("statuses", "knockback", ELDDemoStatusId, Knockback),
    DEMO_ID("statuses", "silence", ELDDemoStatusId, Silence),
    DEMO_ID("statuses", "burn", ELDDemoStatusId, Burn),
    DEMO_ID("statuses", "attack_slowed", ELDDemoStatusId, AttackSlowed),
    DEMO_ID("statuses", "wet", ELDDemoStatusId, Wet),
    DEMO_ID("statuses", "electrified", ELDDemoStatusId, Electrified),
    DEMO_ID("statuses", "braced", ELDDemoStatusId, Braced),
    DEMO_ID("statuses", "soured", ELDDemoStatusId, Soured),
    DEMO_ID("effect_ops", "damage", ELDDemoEffectOp, Damage),
    DEMO_ID("effect_ops", "shield", ELDDemoEffectOp, Shield),
    DEMO_ID("effect_ops", "dispel_root", ELDDemoEffectOp, DispelRoot),
    DEMO_ID("effect_ops", "root", ELDDemoEffectOp, Root),
    DEMO_ID("effect_ops", "stat_bonus", ELDDemoEffectOp, StatBonus),
    DEMO_ID("effect_ops", "lifesteal", ELDDemoEffectOp, Lifesteal),
    DEMO_ID("effect_ops", "dash", ELDDemoEffectOp, Dash),
    DEMO_ID("effect_ops", "slow", ELDDemoEffectOp, Slow),
    DEMO_ID("effect_ops", "timed_stat_bonus", ELDDemoEffectOp, TimedStatBonus),
    DEMO_ID("effect_ops", "self_area", ELDDemoEffectOp, SelfArea),
    DEMO_ID("effect_ops", "low_hp_stat_bonus", ELDDemoEffectOp, LowHpStatBonus),
    DEMO_ID("effect_ops", "control_damage_amp", ELDDemoEffectOp, ControlDamageAmp),
    DEMO_ID("effect_ops", "taunt", ELDDemoEffectOp, Taunt),
    DEMO_ID("effect_ops", "disarm", ELDDemoEffectOp, Disarm),
    DEMO_ID("effect_ops", "stun", ELDDemoEffectOp, Stun),
    DEMO_ID("effect_ops", "empower_basic", ELDDemoEffectOp, EmpowerBasic),
    DEMO_ID("effect_ops", "thorns", ELDDemoEffectOp, Thorns),
    DEMO_ID("effect_ops", "cooldown_refund", ELDDemoEffectOp, CooldownRefund),
    DEMO_ID("effect_ops", "triggered_shield", ELDDemoEffectOp, TriggeredShield),
    DEMO_ID("effect_ops", "nth_basic_bonus", ELDDemoEffectOp, NthBasicBonus),
    DEMO_ID("effect_ops", "mana_on_hit", ELDDemoEffectOp, ManaOnHit),
    DEMO_ID("effect_ops", "dispel_slow", ELDDemoEffectOp, DispelSlow),
    DEMO_ID("effect_ops", "knockback", ELDDemoEffectOp, Knockback),
    DEMO_ID("effect_ops", "zone", ELDDemoEffectOp, Zone),
    DEMO_ID("effect_ops", "zone_slow", ELDDemoEffectOp, ZoneSlow),
    DEMO_ID("effect_ops", "zone_damage", ELDDemoEffectOp, ZoneDamage),
    DEMO_ID("effect_ops", "shield_break_retort", ELDDemoEffectOp, ShieldBreakRetort),
    DEMO_ID("effect_ops", "silence", ELDDemoEffectOp, Silence),
    DEMO_ID("effect_ops", "breach", ELDDemoEffectOp, Breach),
    DEMO_ID("effect_ops", "burn", ELDDemoEffectOp, Burn),
    DEMO_ID("effect_ops", "zone_burn", ELDDemoEffectOp, ZoneBurn),
    DEMO_ID("effect_ops", "channel_cone", ELDDemoEffectOp, ChannelCone),
    DEMO_ID("effect_ops", "ground_impact", ELDDemoEffectOp, GroundImpact),
    DEMO_ID("effect_ops", "attack_slow", ELDDemoEffectOp, AttackSlow),
    DEMO_ID("effect_ops", "contact_trap", ELDDemoEffectOp, ContactTrap),
    DEMO_ID("effect_ops", "dash_contact", ELDDemoEffectOp, DashContact),
    DEMO_ID("effect_ops", "primary_guard", ELDDemoEffectOp, PrimaryGuard),
    DEMO_ID("effect_ops", "dash_contact_damage", ELDDemoEffectOp, DashContactDamage),
    DEMO_ID("effect_ops", "dash_contact_refund", ELDDemoEffectOp, DashContactRefund),
    DEMO_ID("effect_ops", "wet", ELDDemoEffectOp, Wet),
    DEMO_ID("effect_ops", "electrify", ELDDemoEffectOp, Electrify),
    DEMO_ID("effect_ops", "chain_bounce", ELDDemoEffectOp, ChainBounce),
    DEMO_ID("effect_ops", "brace", ELDDemoEffectOp, Brace),
    DEMO_ID("effect_ops", "zone_wet", ELDDemoEffectOp, ZoneWet),
    DEMO_ID("effect_ops", "zone_haste", ELDDemoEffectOp, ZoneHaste),
    DEMO_ID("effect_ops", "grievous", ELDDemoEffectOp, Grievous),
    DEMO_ID("effect_ops", "ledger", ELDDemoEffectOp, Ledger),
    DEMO_ID("effect_ops", "basic_element", ELDDemoEffectOp, BasicElement),
    DEMO_ID("effect_ops", "basic_damage_scale", ELDDemoEffectOp, BasicDamageScale),
    DEMO_ID("effect_ops", "idle_ready_amp", ELDDemoEffectOp, IdleReadyAmp),
    DEMO_ID("effect_ops", "active_element", ELDDemoEffectOp, ActiveElement),
};
#undef DEMO_ID

const FNativeId* Native(const TCHAR* Category, const FString& Id)
{
    for (const auto& Entry : NativeIds)
    {
        if (Id == Entry.Id && FCString::Strcmp(Category, Entry.Category) == 0)
        {
            return &Entry;
        }
    }
    return nullptr;
}

bool NativeIdentity(FFields& Fields, const TCHAR* Category, FString& Id, uint8& Number)
{
    int64 RawNumber;
    if (!Fields.Identifier(TEXT("id"), Id) || !Fields.Int(TEXT("native_id"), RawNumber, 1, 255))
    {
        return false;
    }
    const FNativeId* Entry = Native(Category, Id);
    if (!Fields.Check.Require(Entry && RawNumber == Entry->Number, Fields.Path, TEXT("unknown, retired or renumbered native ID")))
    {
        return false;
    }
    Number = static_cast<uint8>(RawNumber);
    return true;
}

bool NativeMap(const FValue& Value, FCheck& Check)
{
    FFields Root(Check, Value, TEXT("native_ids"));
    int64 Version;
    if (!Root.Int(TEXT("schema_version"), Version, 2, 2))
    {
        return false;
    }
    // Inclusive retired identity ranges per section; they must stay exactly these and are never reused (#198 retires the
    // passives 104-113/120-125, cower 119, the attribute cards 15-26, statuses 28-29 and every operation without a consumer).
    struct FRetiredRange { int64 First; int64 Last; };
    static const FRetiredRange RetiredAbilities[] = {{0, 91}, {104, 113}, {119, 125}};
    static const FRetiredRange RetiredStats[] = {{0, 26}};
    static const FRetiredRange RetiredStatuses[] = {{7, 8}, {11, 11}, {15, 19}, {21, 24}, {28, 29}};
    static const FRetiredRange RetiredEffectOps[] = {{6, 6}, {9, 11}, {16, 16}, {19, 19}, {23, 23}, {25, 25}, {27, 30}, {32, 34},
        {38, 39}, {41, 49}, {51, 51}, {53, 53}, {55, 66}, {68, 68}, {72, 76}, {83, 85}, {89, 89}};
    FFields Retired(Check, Root.Take(TEXT("retired_ranges")), TEXT("native_ids.retired_ranges"));
    for (const auto& Pair : {TPair<const TCHAR*, TArrayView<const FRetiredRange>>(TEXT("abilities"), RetiredAbilities),
        {TEXT("stats"), RetiredStats}, {TEXT("statuses"), RetiredStatuses},
        {TEXT("effect_ops"), RetiredEffectOps}})
    {
        const TArray<FValue>* Ranges;
        const int32 Count = Pair.Value.Num();
        if (!Array(Retired.Take(Pair.Key), Ranges, Check, Retired.Path, Count, Count))
        {
            return false;
        }
        for (int32 Index = 0; Index < Count; ++Index)
        {
            const TArray<FValue>* Range;
            int64 First, Last;
            const FRetiredRange& Expected = Pair.Value[Index];
            if (!Array((*Ranges)[Index], Range, Check, Retired.Path, 2, 2)
                || !Integer((*Range)[0], First, Check, Retired.Path, Expected.First, Expected.First)
                || !Integer((*Range)[1], Last, Check, Retired.Path, Expected.Last, Expected.Last))
            {
                return false;
            }
        }
    }
    if (!Retired.Done())
    {
        return false;
    }
    for (const TCHAR* Category : {TEXT("character"), TEXT("abilities"), TEXT("stats"), TEXT("components"),
        TEXT("monsters"), TEXT("statuses"), TEXT("effect_ops")})
    {
        FFields Group(Check, Root.Take(Category), Root.Path + TEXT(".") + Category);
        for (const auto& Entry : NativeIds)
        {
            if (FCString::Strcmp(Category, Entry.Category) == 0)
            {
                int64 Number;
                if (!Group.Int(Entry.Id, Number, Entry.Number, Entry.Number))
                {
                    return false;
                }
            }
        }
        if (!Group.Done())
        {
            return false;
        }
    }
    return Root.Done();
}

bool P2Bounds(FFields& Fields, const TCHAR* Stat, const TCHAR* Unit, FLDDemoP2Bounds& Out)
{
    const FString Prefix = FString(TEXT("STAT.")) + Stat;
    return Fields.Fixed(*(Prefix + TEXT(".Base")), Unit, TEXT("p2"), Out.BaseP2)
        && Fields.Fixed(*(Prefix + TEXT(".PerLevel")), Unit, TEXT("p2"), Out.PerLevelP2)
        && Fields.Fixed(*(Prefix + TEXT(".Min")), Unit, TEXT("p2"), Out.MinP2)
        && Fields.Fixed(*(Prefix + TEXT(".Max")), Unit, TEXT("p2"), Out.MaxP2);
}

bool RateBounds(FFields& Fields, const TCHAR* Stat, const TCHAR* Unit, FLDDemoRateBounds& Out)
{
    const FString Prefix = FString(TEXT("STAT.")) + Stat;
    return Fields.Fraction(*(Prefix + TEXT(".Base")), Unit, Out.Base)
        && Fields.Fraction(*(Prefix + TEXT(".PerLevel")), Unit, Out.PerLevel)
        && Fields.Fraction(*(Prefix + TEXT(".Min")), Unit, Out.Min)
        && Fields.Fraction(*(Prefix + TEXT(".Max")), Unit, Out.Max);
}

bool Parameters(const FValue& Value, FLDDemoContentParameters& P, FCheck& Check)
{
    FFields F(Check, Value, TEXT("content.parameters"));
    if (!P2Bounds(F, TEXT("MaxHP"), TEXT("hp"), P.MaxHP)
        || !P2Bounds(F, TEXT("MaxMP"), TEXT("mp"), P.MaxMP)
        || !P2Bounds(F, TEXT("AD"), TEXT("points"), P.AD)
        || !P2Bounds(F, TEXT("AP"), TEXT("points"), P.AP)
        || !P2Bounds(F, TEXT("Armor"), TEXT("points"), P.Armor)
        || !P2Bounds(F, TEXT("MagicResist"), TEXT("points"), P.MagicResist)
        || !RateBounds(F, TEXT("HPRegen"), TEXT("hp/s"), P.HPRegen)
        || !RateBounds(F, TEXT("MPRegen"), TEXT("mp/s"), P.MPRegen))
    {
        return false;
    }
    struct FRangeField { const TCHAR* Base; const TCHAR* Prefix; const TCHAR* Unit; FLDDemoRationalBounds* Out; };
    for (const auto& Field : {
        FRangeField{TEXT("STAT.BaseAttackSpeed"), TEXT("STAT.AttackSpeed"), TEXT("attacks/s"), &P.AttackSpeed},
        {TEXT("UNIT.MoveSpeedCmPerSec"), TEXT("STAT.MoveSpeed"), TEXT("cm/s"), &P.MoveSpeedCmPerSec},
        {TEXT("UNIT.BasicAttackRangeCm"), TEXT("STAT.AttackRange"), TEXT("cm"), &P.AttackRangeCm}})
    {
        if (!F.Fraction(Field.Base, Field.Unit, Field.Out->Base, true)
            || !F.Fraction(*(FString(Field.Prefix) + TEXT(".Min")), Field.Unit, Field.Out->Min, true)
            || !F.Fraction(*(FString(Field.Prefix) + TEXT(".Max")), Field.Unit, Field.Out->Max, true))
        {
            return false;
        }
    }
    struct FFixedField { const TCHAR* Key; const TCHAR* Unit; const TCHAR* Storage; int64* Out; int64 Min; int64 Max; };
    const FFixedField Fixed[] =
    {
        {TEXT("STAT.AttackSpeedPerLevelRatio"), TEXT("ratio"), TEXT("bp"), &P.AttackSpeedPerLevelBp, 0, 10000},
        {TEXT("STAT.CritChance.Base"), TEXT("ratio"), TEXT("bp"), &P.CritChanceBaseBp, 0, 10000},
        {TEXT("STAT.CritMultiplier"), TEXT("ratio"), TEXT("bp"), &P.CritMultiplierBp, 10000, MAX_int64},
        {TEXT("STAT.CooldownHaste.Base"), TEXT("points"), TEXT("p2"), &P.CooldownHasteBaseP2, 0, MAX_int64},
        {TEXT("STAT.CooldownHaste.Max"), TEXT("points"), TEXT("p2"), &P.CooldownHasteMaxP2, 0, MAX_int64},
        {TEXT("SKILL.GrowthPerLevel"), TEXT("ratio"), TEXT("bp"), &P.SkillGrowthPerLevelBp, 0, 10000},
        {TEXT("SKILL.GrowthPerLevelAfter10"), TEXT("ratio"), TEXT("bp"), &P.SkillGrowthPerLevelAfter10Bp, 0, 10000},
        {TEXT("UNIT.BasicAttackWindupSec"), TEXT("ms"), TEXT("ms"), &P.BasicAttackWindupMs, 1, MAX_int64},
        {TEXT("CAPTURE.SpraySec"), TEXT("ms"), TEXT("ms"), &P.CaptureSprayMs, 1, MAX_int64},
        {TEXT("CAPTURE.GuardSec"), TEXT("ms"), TEXT("ms"), &P.CaptureGuardMs, 1, MAX_int64},
        {TEXT("MATCH.CountdownSec"), TEXT("ms"), TEXT("ms"), &P.CountdownMs, 1, MAX_int64},
        {TEXT("MATCH.DevelopmentSec"), TEXT("ms"), TEXT("ms"), &P.DevelopmentMs, 0, MAX_int64},
        {TEXT("LIFE.RespawnSec"), TEXT("ms"), TEXT("ms"), &P.RespawnMs, 1, MAX_int64},
        {TEXT("LIFE.ProtectionSec"), TEXT("ms"), TEXT("ms"), &P.ProtectionMs, 0, MAX_int64},
        {TEXT("REGEN.OutOfCombatDelaySec"), TEXT("ms"), TEXT("ms"), &P.OutOfCombatRegenDelayMs, 1, MAX_int64},
        {TEXT("REGEN.OutOfCombatMaxHPRatioPerSec"), TEXT("ratio"), TEXT("bp"), &P.OutOfCombatMaxHPRegenBp, 0, 10000},
        {TEXT("REGEN.OutOfCombatMaxMPRatioPerSec"), TEXT("ratio"), TEXT("bp"), &P.OutOfCombatMaxMPRegenBp, 0, 10000},
        {TEXT("DORM.OutOfCombatDelaySec"), TEXT("ms"), TEXT("ms"), &P.DormRegenDelayMs, 1, MAX_int64},
        {TEXT("DORM.MaxHPRatioPerSec"), TEXT("ratio"), TEXT("bp"), &P.DormMaxHPRegenBp, 0, 10000},
        {TEXT("DORM.MaxMPRatioPerSec"), TEXT("ratio"), TEXT("bp"), &P.DormMaxMPRegenBp, 0, 10000},
        {TEXT("CAMP.ReturnDeadEndSec"), TEXT("ms"), TEXT("ms"), &P.CampReturnDeadEndMs, 1, MAX_int64},
        {TEXT("CAMP.GrowthStageSec"), TEXT("ms"), TEXT("ms"), &P.CampGrowthStageMs, 1, MAX_int64},
        {TEXT("CAMP.GrowthHPRatioPerStage"), TEXT("ratio"), TEXT("bp"), &P.CampGrowthHPPerStageBp, 0, MAX_int64},
        {TEXT("CAMP.GrowthAttackRatioPerStage"), TEXT("ratio"), TEXT("bp"), &P.CampGrowthAttackPerStageBp, 0, MAX_int64},
        {TEXT("CAMP.GrowthResistPerStage"), TEXT("points"), TEXT("p2"), &P.CampGrowthResistPerStageP2, 0, MAX_int64},
        {TEXT("CAMP.GrowthRewardXPRatioPerStage"), TEXT("ratio"), TEXT("bp"), &P.CampGrowthRewardXPPerStageBp, 0, MAX_int64},
        {TEXT("AIRDROP.FirstLandSec"), TEXT("ms"), TEXT("ms"), &P.AirdropFirstLandMs, 1, MAX_int64},
        {TEXT("AIRDROP.IntervalSec"), TEXT("ms"), TEXT("ms"), &P.AirdropIntervalMs, 1, MAX_int64},
        {TEXT("AIRDROP.AnnounceLeadSec"), TEXT("ms"), TEXT("ms"), &P.AirdropAnnounceLeadMs, 1, MAX_int64},
        {TEXT("LEADER.RefreshSec"), TEXT("ms"), TEXT("ms"), &P.LeaderRefreshMs, 1, MAX_int64},
        {TEXT("REWARD.AssistWindowSec"), TEXT("ms"), TEXT("ms"), &P.AssistWindowMs, 1, MAX_int64},
        {TEXT("GROWTH.BuildSafeWindowSec"), TEXT("ms"), TEXT("ms"), &P.BuildSafeWindowMs, 1, MAX_int64},
        {TEXT("AI.RetreatHPRatio"), TEXT("ratio"), TEXT("bp"), &P.AI.RetreatHPBp, 0, 10000},
        {TEXT("AI.ResumeFarmHPRatio"), TEXT("ratio"), TEXT("bp"), &P.AI.ResumeFarmHPBp, 0, 10000},
        {TEXT("AI.HighCampEntryHPRatio"), TEXT("ratio"), TEXT("bp"), &P.AI.HighCampEntryHPBp, 0, 10000},
        {TEXT("AI.HighCampReserveHPRatio"), TEXT("ratio"), TEXT("bp"), &P.AI.HighCampReserveHPBp, 0, 10000},
        {TEXT("AI.LowCampReserveHPRatio"), TEXT("ratio"), TEXT("bp"), &P.AI.LowCampReserveHPBp, 0, 10000},
        {TEXT("AI.CaptureReactionBudgetSec"), TEXT("ms"), TEXT("ms"), &P.AI.CaptureReactionBudgetMs, 1, MAX_int64},
        {TEXT("AI.DecisionIntervalSec"), TEXT("ms"), TEXT("ms"), &P.AI.DecisionIntervalMs, 1, MAX_int64},
        {TEXT("AI.RetryPolicy.InitialBackoffSec"), TEXT("ms"), TEXT("ms"), &P.AI.RetryInitialBackoffMs, 1, MAX_int64},
        {TEXT("AI.RetryPolicy.MaxBackoffSec"), TEXT("ms"), TEXT("ms"), &P.AI.RetryMaxBackoffMs, 1, MAX_int64},
        {TEXT("AI.ExploreRevisitIntervalSec"), TEXT("ms"), TEXT("ms"), &P.AI.ExploreRevisitIntervalMs, 1, MAX_int64},
        {TEXT("AI.EngagementBudgetSec"), TEXT("ms"), TEXT("ms"), &P.AI.EngagementBudgetMs, 1, MAX_int64},
        {TEXT("COMBAT.BreachPerStack"), TEXT("points"), TEXT("p2"), &P.BreachPerStackP2, 1, MAX_int64},
        {TEXT("COMBAT.BreachSec"), TEXT("ms"), TEXT("ms"), &P.BreachMs, 1, MAX_int64},
        {TEXT("COMBAT.BurnPerSec"), TEXT("points"), TEXT("p2"), &P.BurnPerSecP2, 0, MAX_int64},
        {TEXT("COMBAT.BurnAPRatioPerSec"), TEXT("ratio"), TEXT("bp"), &P.BurnAPRatioBp, 0, 10000},
        {TEXT("COMBAT.BurnTickSec"), TEXT("ms"), TEXT("ms"), &P.BurnTickMs, 1, MAX_int64},
        {TEXT("COMBAT.BurnSec"), TEXT("ms"), TEXT("ms"), &P.BurnMs, 1, MAX_int64},
        {TEXT("COMBAT.LifestealCap"), TEXT("ratio"), TEXT("bp"), &P.LifestealCapBp, 0, 10000},
        // #198 elements and reactions (CONTENT_AND_BALANCE §4.4).
        {TEXT("COMBAT.WetSec"), TEXT("ms"), TEXT("ms"), &P.WetMs, 1, MAX_int64},
        {TEXT("COMBAT.WetSlow"), TEXT("ratio"), TEXT("bp"), &P.WetSlowBp, 1, 9999},
        {TEXT("COMBAT.ElectrifiedSec"), TEXT("ms"), TEXT("ms"), &P.ElectrifiedMs, 1, MAX_int64},
        {TEXT("COMBAT.DischargeIntervalSec"), TEXT("ms"), TEXT("ms"), &P.DischargeIntervalMs, 1, MAX_int64},
        {TEXT("COMBAT.ReactionBase"), TEXT("points"), TEXT("p2"), &P.ReactionBaseP2, 0, MAX_int64},
        {TEXT("COMBAT.ReactionPerLevel"), TEXT("points"), TEXT("p2"), &P.ReactionPerLevelP2, 0, MAX_int64},
        {TEXT("COMBAT.ReactionAPRatio"), TEXT("ratio"), TEXT("bp"), &P.ReactionAPRatioBp, 0, MAX_int64},
        {TEXT("COMBAT.FogSec"), TEXT("ms"), TEXT("ms"), &P.FogMs, 1, MAX_int64},
        {TEXT("COMBAT.FogIntervalSec"), TEXT("ms"), TEXT("ms"), &P.FogIntervalMs, 1, MAX_int64},
        {TEXT("COMBAT.ConductSlow"), TEXT("ratio"), TEXT("bp"), &P.ConductSlowBp, 1, 9999},
        {TEXT("COMBAT.ConductSlowSec"), TEXT("ms"), TEXT("ms"), &P.ConductSlowMs, 1, MAX_int64},
    };
    for (const auto& Field : Fixed)
    {
        if (!F.Fixed(Field.Key, Field.Unit, Field.Storage, *Field.Out, Field.Min, Field.Max))
        {
            return false;
        }
    }
    struct FCmField { const TCHAR* Key; FLDDemoRational* Out; };
    for (const auto& Field : {
        FCmField{TEXT("UNIT.CollisionRadiusCm"), &P.CollisionRadiusCm},
        {TEXT("VISION.RadiusCm"), &P.VisionRadiusCm},
        {TEXT("CAPTURE.RadiusCm"), &P.CaptureRadiusCm}, {TEXT("CAMP.ResetArrivalRadiusCm"), &P.CampResetArrivalRadiusCm},
        {TEXT("DORM.RadiusCm"), &P.DormRadiusCm}, {TEXT("AIRDROP.PickupRadiusCm"), &P.AirdropPickupRadiusCm},
        {TEXT("LEADER.CircleRadiusCm"), &P.LeaderCircleRadiusCm}, {TEXT("LEADER.CircleMaxOffsetCm"), &P.LeaderCircleMaxOffsetCm},
        {TEXT("COMBAT.DischargeRadiusCm"), &P.DischargeRadiusCm}, {TEXT("COMBAT.FogRadiusCm"), &P.FogRadiusCm},
        {TEXT("COMBAT.ConductRadiusCm"), &P.ConductRadiusCm}, {TEXT("COMBAT.OverloadRadiusCm"), &P.OverloadRadiusCm}})
    {
        if (!F.Fraction(Field.Key, TEXT("cm"), *Field.Out, true))
        {
            return false;
        }
    }
    struct FCountField { const TCHAR* Key; int64* Out; int64 Min; };
    for (const auto& Field : {
        FCountField{TEXT("XP.BaseToNext"), &P.XPBaseToNext, 1}, {TEXT("XP.IncreasePerLevel"), &P.XPIncreasePerLevel, 0},
        {TEXT("REWARD.PlayerXP.Base"), &P.PlayerXPBase, 0}, {TEXT("REWARD.PlayerXP.PerLevel"), &P.PlayerXPPerLevel, 0},
        {TEXT("AI.RetryPolicy.MaxAttempts"), &P.AI.RetryMaxAttempts, 1},
        {TEXT("CAMP.ReturnDeadEndPlans"), &P.CampReturnDeadEndPlans, 1},
        {TEXT("CAMP.GrowthMaxStage"), &P.CampGrowthMaxStage, 1},
        {TEXT("AIRDROP.RewardXP"), &P.AirdropRewardXP, 1}, {TEXT("LEADER.MinLevel"), &P.LeaderMinLevel, 1},
        {TEXT("COMBAT.BreachMaxStacks"), &P.BreachMaxStacks, 1}, {TEXT("COMBAT.BurnMaxStacks"), &P.BurnMaxStacks, 1},
        {TEXT("GROWTH.OpeningPoints"), &P.OpeningPoints, 1}, {TEXT("GROWTH.PointsPerLevel"), &P.PointsPerLevel, 1},
        {TEXT("GROWTH.MaxLevelPointsXP"), &P.MaxLevelPointsXP, 1}, {TEXT("GROWTH.MaxLevelPoints"), &P.MaxLevelPoints, 1},
        {TEXT("COMBAT.OverloadBreachStacks"), &P.OverloadBreachStacks, 1},
        {TEXT("GROWTH.EmptyComponentPoolPoints"), &P.EmptyComponentPoolPoints, 1}})
    {
        if (!F.Int(Field.Key, *Field.Out, Field.Min))
        {
            return false;
        }
    }
    if (!Check.Require(P.BreachMaxStacks <= 10 && P.BurnMaxStacks <= 10 && P.BurnTickMs <= P.BurnMs, F.Path,
        TEXT("keyword stacks are capped at ten and a burn ticks inside its duration"))
        || !Check.Require(P.OverloadBreachStacks <= P.BreachMaxStacks && P.FogIntervalMs <= P.FogMs
            && P.DischargeRadiusCm.Numerator > 0 && P.FogRadiusCm.Numerator > 0 && P.ConductRadiusCm.Numerator > 0
            && P.OverloadRadiusCm.Numerator > 0, F.Path,
            TEXT("element reactions need positive radii, overload stacks within the breach cap and fog pulses inside the fog")))
    {
        return false;
    }
    return F.Done();
}

bool Formulas(const FValue& Value, FLDDemoFormulas& Out, FCheck& Check)
{
    FFields F(Check, Value, TEXT("content.formulas"));
    struct FEntry { const TCHAR* Key; const TCHAR* Name; ELDDemoFormula Expected; ELDDemoFormula* Out; };
#define DEMO_FORMULA(Key, Rule) {TEXT(#Key), TEXT(#Rule), ELDDemoFormula::Rule, &Out.Key}
    const FEntry Entries[] =
    {
        DEMO_FORMULA(StatProjection, AddFlatThenAdditivePercent),
        DEMO_FORMULA(Resistance, PointsOverScalePlusPoints), DEMO_FORMULA(Crit, OncePerPrimaryAttack),
        DEMO_FORMULA(Damage, FloorP2ThenShieldThenHP), DEMO_FORMULA(AttackSpeed, BaseTimesAdditiveRatio),
        DEMO_FORMULA(MoveSpeed, BaseTimesAdditiveRatio), DEMO_FORMULA(AttackRange, BasePlusFlat),
        DEMO_FORMULA(AttackMoveAcquireRadius, MinVisionTwiceAttackRange),
        DEMO_FORMULA(AttackIntervalMs, CeilReciprocalMs), DEMO_FORMULA(AttackWindupMs, CeilBaseWindupTimesBaseOverCurrentAS),
        DEMO_FORMULA(CooldownMs, CeilBaseTimesScaleOverScalePlusHaste), DEMO_FORMULA(Regen, CarrySubP2Remainder),
        DEMO_FORMULA(ResourceMaxChange, FloorFinalRatioRejectAliveUnderflow),
        DEMO_FORMULA(XPToNext, LinearIncrement),
    };
#undef DEMO_FORMULA
    for (const auto& Entry : Entries)
    {
        if (!F.Literal(Entry.Key, Entry.Name))
        {
            return false;
        }
        *Entry.Out = Entry.Expected;
    }
    return F.Fixed(TEXT("ResistanceScale"), TEXT("points"), TEXT("p2"), Out.ResistanceScaleP2, 1)
        && F.Fixed(TEXT("HasteScale"), TEXT("points"), TEXT("p2"), Out.HasteScaleP2, 1) && F.Done();
}

bool Policies(const FValue& Value, FLDDemoPolicies& Out, FCheck& Check)
{
    FFields F(Check, Value, TEXT("content.policies"));
    struct FEntry { const TCHAR* Key; const TCHAR* Name; ELDDemoPolicy Expected; ELDDemoPolicy* Out; };
#define DEMO_POLICY(Key, Rule) {TEXT(#Key), TEXT(#Rule), ELDDemoPolicy::Rule, &Out.Key}
    const FEntry Entries[] =
    {
        DEMO_POLICY(StatStacking, AddFlatThenAdditivePercent),
        DEMO_POLICY(MissingSourceCooldown, ContinueServerTime), DEMO_POLICY(DeadChoiceCommit, Allowed),
        DEMO_POLICY(ResourceMaxChange, PreserveRatio), DEMO_POLICY(ReleasedEffectSourceLoss, ContinueForRemainingContender),
        DEMO_POLICY(SourceLeave, CancelUnsettledNoRefund), DEMO_POLICY(InvalidAggressionProtection, BreakOnAggressiveIntent),
        DEMO_POLICY(CritSampling, OncePerAttackId), DEMO_POLICY(LifestealBasis, PrimaryBasicAttackHPLost),
        DEMO_POLICY(LifestealRecursiveTriggers, Disabled), DEMO_POLICY(BasicSecondaryTriggers, Disabled),
        DEMO_POLICY(RegenIntegration, CarrySubP2Remainder), DEMO_POLICY(ShieldPolicy, ReplaceIfNotWeaker),
        DEMO_POLICY(CampAggroTrigger, ConfirmedCombatHit), DEMO_POLICY(CampResetPolicy, ReturnThenReset),
        DEMO_POLICY(CampManaPolicy, NotApplicable),
    };
#undef DEMO_POLICY
    for (const auto& Entry : Entries)
    {
        if (!F.Literal(Entry.Key, Entry.Name))
        {
            return false;
        }
        *Entry.Out = Entry.Expected;
    }
    if (!F.Flag(TEXT("CampGroupRetaliation"), Out.CampGroupRetaliation)
        || !F.Flag(TEXT("CampReturningDamageable"), Out.CampReturningDamageable)
        || !F.Flag(TEXT("CampReturningControllable"), Out.CampReturningControllable)
        || !Check.Require(Out.CampGroupRetaliation
            && Out.CampReturningDamageable && Out.CampReturningControllable, F.Path, TEXT("unsupported boolean policy")))
    {
        return false;
    }
    const TArray<FValue>* Blocks;
    FString Move, Dash;
    if (!Array(F.Take(TEXT("RootBlocks")), Blocks, Check, F.Path, 2, 2)
        || !String((*Blocks)[0], Move, Check, F.Path) || !String((*Blocks)[1], Dash, Check, F.Path)
        || !Check.Require(Move.Equals(TEXT("Move"), ESearchCase::CaseSensitive)
            && Dash.Equals(TEXT("Dash"), ESearchCase::CaseSensitive), F.Path, TEXT("unsupported root restrictions")))
    {
        return false;
    }
    Out.RootBlocks = {ELDDemoRootBlock::Move, ELDDemoRootBlock::Dash};
    return F.Done();
}

struct FBonusField
{
    const TCHAR* Name;
    ELDDemoBonusStat Stat;
    const TCHAR* Unit;
    const TCHAR* Storage;
};
const FBonusField BonusFields[] =
{
    {TEXT("ADFlat"), ELDDemoBonusStat::ADFlat, TEXT("points"), TEXT("p2")},
    {TEXT("APFlat"), ELDDemoBonusStat::APFlat, TEXT("points"), TEXT("p2")},
    {TEXT("MaxHPFlat"), ELDDemoBonusStat::MaxHPFlat, TEXT("hp"), TEXT("p2")},
    {TEXT("MaxMPFlat"), ELDDemoBonusStat::MaxMPFlat, TEXT("mp"), TEXT("p2")},
    {TEXT("ArmorFlat"), ELDDemoBonusStat::ArmorFlat, TEXT("points"), TEXT("p2")},
    {TEXT("MagicResistFlat"), ELDDemoBonusStat::MagicResistFlat, TEXT("points"), TEXT("p2")},
    {TEXT("CooldownHasteFlat"), ELDDemoBonusStat::CooldownHasteFlat, TEXT("points"), TEXT("p2")},
    {TEXT("AttackRangeCmFlat"), ELDDemoBonusStat::AttackRangeCmFlat, TEXT("cm"), nullptr},
    {TEXT("AttackSpeedBonusRatio"), ELDDemoBonusStat::AttackSpeedBonusRatio, TEXT("ratio"), TEXT("bp")},
    {TEXT("MoveSpeedBonusRatio"), ELDDemoBonusStat::MoveSpeedBonusRatio, TEXT("ratio"), TEXT("bp")},
    {TEXT("CritChanceBonusRatio"), ELDDemoBonusStat::CritChanceBonusRatio, TEXT("ratio"), TEXT("bp")},
    {TEXT("ADBonusRatio"), ELDDemoBonusStat::ADBonusRatio, TEXT("ratio"), TEXT("bp")},
    {TEXT("HPRegenFlat"), ELDDemoBonusStat::HPRegenFlat, TEXT("hp/s"), nullptr},
    {TEXT("APBonusRatio"), ELDDemoBonusStat::APBonusRatio, TEXT("ratio"), TEXT("bp")},
    {TEXT("MPRegenFlat"), ELDDemoBonusStat::MPRegenFlat, TEXT("mp/s"), nullptr},
};

// An authored integer into an int32 definition field.
bool IntInto(FFields& F, const TCHAR* Key, int32& Out, int64 Min, int64 Max)
{
    int64 Value = 0;
    if (!F.Int(Key, Value, Min, Max))
    {
        return false;
    }
    Out = int32(Value);
    return true;
}

constexpr uint32 Bit(ELDDemoBonusStat Stat) { return 1u << uint8(Stat); }
// Effect opcodes reach past 63 (#112), so the per-definition operation set is a 128-bit mask.
struct FOpMask
{
    uint64 Low = 0, High = 0;
    constexpr FOpMask operator|(const FOpMask& Other) const { return {Low | Other.Low, High | Other.High}; }
    constexpr FOpMask operator&(const FOpMask& Other) const { return {Low & Other.Low, High & Other.High}; }
    constexpr FOpMask operator~() const { return {~Low, ~High}; }
    FOpMask& operator|=(const FOpMask& Other) { Low |= Other.Low; High |= Other.High; return *this; }
    constexpr bool operator==(const FOpMask& Other) const { return Low == Other.Low && High == Other.High; }
    constexpr bool operator!=(const FOpMask& Other) const { return !(*this == Other); }
    constexpr bool Any() const { return Low != 0 || High != 0; }
};
constexpr FOpMask OpBit(ELDDemoEffectOp Op)
{
    return uint8(Op) < 64 ? FOpMask{uint64(1) << uint8(Op), 0} : FOpMask{0, uint64(1) << (uint8(Op) - 64)};
}

// Temporary and conditional bonuses never change resource maxima or regeneration (no conversion exists for them).
constexpr uint32 TemporaryStats = ~(Bit(ELDDemoBonusStat::MaxHPFlat) | Bit(ELDDemoBonusStat::MaxMPFlat) | Bit(ELDDemoBonusStat::HPRegenFlat)
    | Bit(ELDDemoBonusStat::MPRegenFlat));

bool DamageTerms(FFields& F, FLDDemoEffectDefinition& Out, FCheck& Check, const FString& Path)
{
    FString Type;
    if (!F.Fixed(TEXT("base"), TEXT("points"), TEXT("p2"), Out.BaseP2)
        || !F.Fixed(TEXT("ad_ratio"), TEXT("ratio"), TEXT("bp"), Out.ADRatioBp)
        || !F.Fixed(TEXT("ap_ratio"), TEXT("ratio"), TEXT("bp"), Out.APRatioBp)
        || !F.Text(TEXT("damage_type"), Type)
        || !Check.Require(Type.Equals(TEXT("Physical"), ESearchCase::CaseSensitive)
            || Type.Equals(TEXT("Magical"), ESearchCase::CaseSensitive), Path, TEXT("unknown damage type")))
    {
        return false;
    }
    Out.DamageType = Type.Equals(TEXT("Physical"), ESearchCase::CaseSensitive) ? ELDDemoDamageType::Physical : ELDDemoDamageType::Magical;
    return Check.Require(Out.BaseP2 > 0 || Out.APRatioBp > 0 || Out.ADRatioBp > 0, Path, TEXT("ineffective effect"));
}

uint32 StatMask(const TArray<FLDDemoStatBonus>& Stats)
{
    uint32 Result = 0;
    for (const auto& Stat : Stats)
    {
        Result |= Bit(Stat.Stat);
    }
    return Result;
}

bool Stats(const FValue& Value, TArray<FLDDemoStatBonus>& Out, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    if (!Check.Good || !Check.Require(!F.Object->Values.IsEmpty(), Path, TEXT("empty stat definition")))
    {
        return false;
    }
    TArray<FString> Keys;
    for (const auto& Pair : F.Object->Values)
    {
        Keys.Emplace(Pair.Key.ToView());
    }
    Keys.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
    for (const FString& Key : Keys)
    {
        const FBonusField* Field = nullptr;
        for (const auto& Candidate : BonusFields)
        {
            if (Key.Equals(Candidate.Name, ESearchCase::CaseSensitive))
            {
                Field = &Candidate;
                break;
            }
        }
        if (!Check.Require(Field != nullptr, Path + TEXT(".") + Key, TEXT("unknown stat")))
        {
            return false;
        }
        FLDDemoStatBonus Bonus;
        Bonus.Stat = Field->Stat;
        if (Field->Storage)
        {
            const int64 Max = FCString::Strcmp(Field->Storage, TEXT("bp")) == 0 ? 10000 : MAX_int64;
            if (!F.Fixed(Field->Name, Field->Unit, Field->Storage, Bonus.Amount, 1, Max))
            {
                return false;
            }
        }
        else if (!F.Fraction(Field->Name, Field->Unit, Bonus.Cm, true))
        {
            return false;
        }
        Out.Add(MoveTemp(Bonus));
    }
    return F.Done();
}

bool Target(const FValue& Value, FLDDemoTargetDefinition& Out, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    FString Mode;
    if (!F.Text(TEXT("mode"), Mode))
    {
        return false;
    }
    if (Mode.Equals(TEXT("Self"), ESearchCase::CaseSensitive))
    {
        Out.Mode = ELDDemoTargetMode::Self;
    }
    else if (Mode.Equals(TEXT("DirectionDash"), ESearchCase::CaseSensitive))
    {
        Out.Mode = ELDDemoTargetMode::DirectionDash;
    }
    else if (Mode.Equals(TEXT("DirectionCone"), ESearchCase::CaseSensitive))
    {
        Out.Mode = ELDDemoTargetMode::DirectionCone;
        FNumbers Numbers(Check);
        if (!F.Fraction(TEXT("range"), TEXT("cm"), Out.RangeCm, true)
            || !F.Fraction(TEXT("angle"), TEXT("degrees"), Out.AngleDegrees, true)
            || !Check.Require(Numbers.LE(Numbers.Value(Out.AngleDegrees), Numbers.Value(180)), Path,
                TEXT("a cone angle must not exceed a half circle"))) { return false; }
    }
    else if (Mode.Equals(TEXT("DirectionLine"), ESearchCase::CaseSensitive))
    {
        Out.Mode = ELDDemoTargetMode::DirectionLine;
        if (!F.Fraction(TEXT("range"), TEXT("cm"), Out.RangeCm, true)
            || !F.Fraction(TEXT("half_width"), TEXT("cm"), Out.LineHalfWidthCm, true)
            || !Check.Require(Out.RangeCm.Numerator > 0 && Out.LineHalfWidthCm.Numerator > 0, Path,
                TEXT("a line needs a positive length and half width"))) { return false; }
    }
    else if (Mode.Equals(TEXT("Unit"), ESearchCase::CaseSensitive) || Mode.Equals(TEXT("DirectionProjectile"), ESearchCase::CaseSensitive)
        || Mode.Equals(TEXT("GroundPoint"), ESearchCase::CaseSensitive))
    {
        Out.Mode = Mode.Equals(TEXT("Unit"), ESearchCase::CaseSensitive) ? ELDDemoTargetMode::Unit
            : Mode.Equals(TEXT("GroundPoint"), ESearchCase::CaseSensitive) ? ELDDemoTargetMode::GroundPoint
            : ELDDemoTargetMode::DirectionProjectile;
        if (!F.Fraction(TEXT("range"), TEXT("cm"), Out.RangeCm, true))
        {
            return false;
        }
        if (Out.Mode == ELDDemoTargetMode::DirectionProjectile
            && (!F.Fraction(TEXT("projectile_radius"), TEXT("cm"), Out.ProjectileRadiusCm, true)
                || !F.Fraction(TEXT("projectile_speed"), TEXT("cm/s"), Out.ProjectileSpeedCmPerSec, true)))
        {
            return false;
        }
        if (F.Has(TEXT("attack_range_ratio"))
            && (!F.Fixed(TEXT("attack_range_ratio"), TEXT("ratio"), TEXT("bp"), Out.AttackRangeRatioBp, 1, 10000)
                || !F.Fraction(TEXT("attack_range_bonus_cap"), TEXT("cm"), Out.AttackRangeBonusCapCm, true)))
        {
            return false;
        }
    }
    else
    {
        return Check.Require(false, Path, TEXT("unsupported target mode"));
    }
    return F.Done();
}

// Optional fields are terms only some definitions of the same operation carry; absence means no such term.
bool OptionalFixed(FFields& F, const TCHAR* Key, const TCHAR* Unit, const TCHAR* Storage, int64& Out,
    int64 Min = 0, int64 Max = MAX_int64)
{
    return !F.Has(Key) || F.Fixed(Key, Unit, Storage, Out, Min, Max);
}

bool TriggerField(FFields& F, ELDDemoEffectTrigger& Out, std::initializer_list<ELDDemoEffectTrigger> Allowed)
{
    static const TCHAR* Names[] = {TEXT("None"), TEXT("BasicHit"), TEXT("AbilityHit"), TEXT("Damaged"), TEXT("DashComplete")};
    FString Name;
    if (!F.Text(TEXT("trigger"), Name))
    {
        return false;
    }
    for (const ELDDemoEffectTrigger Candidate : Allowed)
    {
        if (Name.Equals(Names[uint8(Candidate)], ESearchCase::CaseSensitive))
        {
            Out = Candidate;
            return true;
        }
    }
    return F.Check.Require(false, F.Path + TEXT(".trigger"), TEXT("unsupported trigger for this effect"));
}

// #198 B: the operations Combat reads from an installed component (DemoContent.COMPONENT_OPS). burn, breach and
// empower_basic also have an active form; the others are component-only and no active may carry them.
constexpr FOpMask SharedComponentOps = OpBit(ELDDemoEffectOp::Burn) | OpBit(ELDDemoEffectOp::Breach) | OpBit(ELDDemoEffectOp::EmpowerBasic);
constexpr FOpMask ComponentOnlyOps = OpBit(ELDDemoEffectOp::Ledger) | OpBit(ELDDemoEffectOp::Thorns) | OpBit(ELDDemoEffectOp::Lifesteal)
    | OpBit(ELDDemoEffectOp::DashContactDamage) | OpBit(ELDDemoEffectOp::NthBasicBonus) | OpBit(ELDDemoEffectOp::ControlDamageAmp)
    | OpBit(ELDDemoEffectOp::TriggeredShield) | OpBit(ELDDemoEffectOp::BasicElement) | OpBit(ELDDemoEffectOp::StatBonus)
    | OpBit(ELDDemoEffectOp::BasicDamageScale) | OpBit(ELDDemoEffectOp::LowHpStatBonus) | OpBit(ELDDemoEffectOp::PrimaryGuard)
    | OpBit(ELDDemoEffectOp::IdleReadyAmp) | OpBit(ELDDemoEffectOp::ActiveElement);
constexpr FOpMask ComponentOps = SharedComponentOps | ComponentOnlyOps;

bool ElementField(const FString& Name, ELDDemoElement& Out)
{
    for (const auto& Pair : {TPair<const TCHAR*, ELDDemoElement>(TEXT("fire"), ELDDemoElement::Fire),
        {TEXT("water"), ELDDemoElement::Water}, {TEXT("electric"), ELDDemoElement::Electric}})
    {
        if (Name.Equals(Pair.Key, ESearchCase::CaseSensitive))
        {
            Out = Pair.Value;
            return true;
        }
    }
    return false;
}

// bComponent: the effect belongs to a component (#198 B) and takes the component form of its operation.
bool Effect(const FValue& Value, FLDDemoEffectDefinition& Out, bool bComponent, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    FString Op;
    int64 Opcode;
    if (!F.Identifier(TEXT("op"), Op) || !F.Int(TEXT("opcode"), Opcode, 1, 255))
    {
        return false;
    }
    const FNativeId* Entry = Native(TEXT("effect_ops"), Op);
    if (!Check.Require(Entry && Entry->Number == Opcode, Path, TEXT("unsupported or mismatched effect opcode")))
    {
        return false;
    }
    Out.Opcode = static_cast<uint8>(Opcode);
    const FOpMask OpFlag = OpBit(Out.GetOp());
    if (!Check.Require(bComponent ? (ComponentOps & OpFlag).Any() : !(ComponentOnlyOps & OpFlag).Any(), Path,
        bComponent ? TEXT("not a component operation") : TEXT("an active cannot carry a component operation")))
    {
        return false;
    }
    switch (Out.GetOp())
    {
    case ELDDemoEffectOp::Damage:
    case ELDDemoEffectOp::Shield:
    {
        const bool Damage = Out.GetOp() == ELDDemoEffectOp::Damage;
        if (!F.Fixed(TEXT("base"), Damage ? TEXT("points") : TEXT("hp"), TEXT("p2"), Out.BaseP2)
            || !F.Fixed(TEXT("ap_ratio"), TEXT("ratio"), TEXT("bp"), Out.APRatioBp))
        {
            return false;
        }
        if (Damage)
        {
            FString Type;
            if (!F.Fixed(TEXT("ad_ratio"), TEXT("ratio"), TEXT("bp"), Out.ADRatioBp)
                || !F.Fixed(TEXT("max_hp_ratio"), TEXT("ratio"), TEXT("bp"), Out.MaxHPRatioBp)
                || !F.Text(TEXT("damage_type"), Type)
                || !Check.Require(Type.Equals(TEXT("Physical"), ESearchCase::CaseSensitive)
                    || Type.Equals(TEXT("Magical"), ESearchCase::CaseSensitive), Path, TEXT("unknown damage type")))
            {
                return false;
            }
            Out.DamageType = Type.Equals(TEXT("Physical"), ESearchCase::CaseSensitive) ? ELDDemoDamageType::Physical : ELDDemoDamageType::Magical;
        }
        else if (!F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !OptionalFixed(F, TEXT("max_hp_ratio"), TEXT("ratio"), TEXT("bp"), Out.MaxHPRatioBp, 1, 10000))
        {
            return false;
        }
        if (!Check.Require(Out.BaseP2 > 0 || Out.APRatioBp > 0 || Out.ADRatioBp > 0 || Out.MaxHPRatioBp > 0, Path, TEXT("ineffective effect")))
        {
            return false;
        }
        break;
    }
    case ELDDemoEffectOp::DispelRoot:
    case ELDDemoEffectOp::DispelSlow:
        break;
    case ELDDemoEffectOp::StatBonus:
        if (!Stats(F.Take(TEXT("stats")), Out.Stats, Check, Path + TEXT(".stats")))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::Lifesteal:
        // hp_below + low_hp_multiplier: the lifesteal is multiplied against a target below that HP share (#147).
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 10000)
            || !OptionalFixed(F, TEXT("hp_below"), TEXT("ratio"), TEXT("bp"), Out.SecondaryRatioBp, 1, 9999)
            || !OptionalFixed(F, TEXT("low_hp_multiplier"), TEXT("ratio"), TEXT("bp"), Out.MultiplierBp, 10001)
            || !Check.Require((Out.SecondaryRatioBp > 0) == (Out.MultiplierBp > 0), Path,
                TEXT("a low-HP lifesteal needs its threshold and multiplier")))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::Dash:
        if (!F.Fraction(TEXT("distance"), TEXT("cm"), Out.DistanceCm, true)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::Slow:
        // A slow always leaves some move speed; the projection clamps the rest to STAT.MoveSpeed.Min.
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::TimedStatBonus:
        if (!Stats(F.Take(TEXT("stats")), Out.Stats, Check, Path + TEXT(".stats"))
            || !Check.Require((StatMask(Out.Stats) & ~TemporaryStats) == 0, Path, TEXT("timed bonuses cannot change resource maxima"))
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::SelfArea:
        if (!F.Fraction(TEXT("radius"), TEXT("cm"), Out.RadiusCm, true)
            || !Check.Require(Out.RadiusCm.Numerator > 0, Path, TEXT("area radius must be positive")))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::LowHpStatBonus:
        if (!F.Fixed(TEXT("threshold"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999)
            || !Stats(F.Take(TEXT("stats")), Out.Stats, Check, Path + TEXT(".stats"))
            || !Check.Require((StatMask(Out.Stats) & ~TemporaryStats) == 0, Path, TEXT("conditional bonuses cannot change resource maxima"))
            // lifesteal: primary basic lifesteal while the bonus is active (#147).
            || !OptionalFixed(F, TEXT("lifesteal"), TEXT("ratio"), TEXT("bp"), Out.SecondaryRatioBp, 1, 10000))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::ControlDamageAmp:
        // hp_below: a target below that HP share counts as well as a slowed or controlled one (#147).
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1)
            || !OptionalFixed(F, TEXT("hp_below"), TEXT("ratio"), TEXT("bp"), Out.SecondaryRatioBp, 1, 9999))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::Taunt:
        if (!F.Fraction(TEXT("radius"), TEXT("cm"), Out.RadiusCm, true)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::Root:
    case ELDDemoEffectOp::Disarm:
    case ELDDemoEffectOp::Stun:
    case ELDDemoEffectOp::Silence:
        if (!F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::EmpowerBasic:
    case ELDDemoEffectOp::Thorns:
    {
        const bool Empower = Out.GetOp() == ELDDemoEffectOp::EmpowerBasic;
        FString Type;
        if (!F.Fixed(TEXT("base"), TEXT("points"), TEXT("p2"), Out.BaseP2)
            || !F.Text(TEXT("damage_type"), Type)
            || !Check.Require(Type.Equals(TEXT("Physical"), ESearchCase::CaseSensitive)
                || Type.Equals(TEXT("Magical"), ESearchCase::CaseSensitive), Path, TEXT("unknown damage type")))
        {
            return false;
        }
        Out.DamageType = Type.Equals(TEXT("Physical"), ESearchCase::CaseSensitive) ? ELDDemoDamageType::Physical : ELDDemoDamageType::Magical;
        if (Empower)
        {
            // The bonus joins the empowered basic's own physical primary hit before its critical roll.
            if (!F.Fixed(TEXT("ad_ratio"), TEXT("ratio"), TEXT("bp"), Out.ADRatioBp)
                || !F.Fixed(TEXT("ap_ratio"), TEXT("ratio"), TEXT("bp"), Out.APRatioBp)
                || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
                || !Check.Require(Out.BaseP2 > 0 || Out.ADRatioBp > 0 || Out.APRatioBp > 0, Path, TEXT("ineffective effect"))
                || !Check.Require(Out.DamageType == ELDDemoDamageType::Physical, Path, TEXT("an empowered basic stays physical")))
            {
                return false;
            }
            // #147: charges (landed basics it empowers, one when absent) and stats held while charged.
            // #198 B: a component's package is armed by a completed voluntary dash and names its charges.
            if ((bComponent && (!TriggerField(F, Out.Trigger, {ELDDemoEffectTrigger::DashComplete})
                    || !Check.Require(F.Has(TEXT("charges")), Path, TEXT("a component's empowered basic names its charges"))))
                || (F.Has(TEXT("charges")) && !IntInto(F, TEXT("charges"), Out.Count, 1, 10))
                || (F.Has(TEXT("stats")) && (!Stats(F.Take(TEXT("stats")), Out.Stats, Check, Path + TEXT(".stats"))
                    || !Check.Require((StatMask(Out.Stats) & ~TemporaryStats) == 0, Path, TEXT("timed bonuses cannot change resource maxima")))))
            {
                return false;
            }
            break;
        }
        FString Trigger;
        if (!F.Fixed(TEXT("armor_ratio"), TEXT("ratio"), TEXT("bp"), Out.ArmorRatioBp)
            || !OptionalFixed(F, TEXT("max_hp_ratio"), TEXT("ratio"), TEXT("bp"), Out.MaxHPRatioBp, 1, 10000)
            || !F.Fraction(TEXT("radius"), TEXT("cm"), Out.RadiusCm, true)
            // #198 B: without an interval the reflection answers every qualifying hit.
            || !OptionalFixed(F, TEXT("interval"), TEXT("ms"), TEXT("ms"), Out.IntervalMs, 1)
            || !F.Fixed(TEXT("critical_multiplier"), TEXT("ratio"), TEXT("bp"), Out.CriticalMultiplierBp, 10000)
            // A reflection answers primary basic hits only.
            || !F.Text(TEXT("trigger"), Trigger)
            || !Check.Require(Trigger.Equals(TEXT("Basic"), ESearchCase::CaseSensitive), Path, TEXT("unknown reflection trigger"))
            || !Check.Require(Out.BaseP2 > 0 || Out.ArmorRatioBp > 0 || Out.MaxHPRatioBp > 0, Path, TEXT("ineffective effect"))
            // slow: a move-speed slow on the attacker (#147).
            || !OptionalFixed(F, TEXT("slow"), TEXT("ratio"), TEXT("bp"), Out.SecondaryRatioBp, 1, 9999)
            || !OptionalFixed(F, TEXT("slow_duration"), TEXT("ms"), TEXT("ms"), Out.SecondaryDurationMs, 1)
            || !Check.Require((Out.SecondaryRatioBp > 0) == (Out.SecondaryDurationMs > 0), Path, TEXT("a slow needs its ratio and duration")))
        {
            return false;
        }
        break;
    }
    // #112 handlers (LDCombatMechanics). Field use: see FLDDemoEffectDefinition and docs/systems/COMBAT_AND_GROWTH.md.
    case ELDDemoEffectOp::Knockback:
        // A push along the caster-to-target direction; a block ends it early (#198: no wall stun).
        if (!F.Fraction(TEXT("distance"), TEXT("cm"), Out.DistanceCm, true)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::ZoneDamage:
        if (!DamageTerms(F, Out, Check, Path))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::Zone:
        if (!F.Fraction(TEXT("radius"), TEXT("cm"), Out.RadiusCm, true)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !F.Fixed(TEXT("interval"), TEXT("ms"), TEXT("ms"), Out.IntervalMs, 1)
            || !F.Fixed(TEXT("delay"), TEXT("ms"), TEXT("ms"), Out.DelayMs)
            || !IntInto(F, TEXT("count"), Out.Count, 1, 20)
            || !Check.Require(Out.RadiusCm.Numerator > 0 && Out.DelayMs + Out.IntervalMs * (Out.Count - 1) <= Out.DurationMs,
                Path, TEXT("a zone needs a positive radius and its pulses inside its duration")))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::ZoneSlow:
        // #198: every IntervalMs from the zone's start, the enemies inside are slowed by RatioBp for DurationMs.
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999)
            || !F.Fixed(TEXT("interval"), TEXT("ms"), TEXT("ms"), Out.IntervalMs, 1)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::CooldownRefund:
    {
        // A passive cut of both learned actives' remaining cooldowns on every landed primary basic (#147).
        FString Entries;
        if (!TriggerField(F, Out.Trigger, {ELDDemoEffectTrigger::BasicHit})
            || !F.Text(TEXT("entries"), Entries)
            || !Check.Require(Entries.Equals(TEXT("Actives"), ESearchCase::CaseSensitive), Path, TEXT("unknown cooldown entries"))
            || !F.Fixed(TEXT("cooldown"), TEXT("ms"), TEXT("ms"), Out.AmountMs, 1))
        {
            return false;
        }
        Out.Entries = ELDDemoCooldownEntries::Actives;
        break;
    }
    case ELDDemoEffectOp::TriggeredShield:
        // #147 Damaged: a shield whenever the holder takes damage, then interval of rest (only after a granted shield);
        // burst_ratio + burst_radius: that shield broken answers the bodies around with a share of its amount.
        if (!TriggerField(F, Out.Trigger, {ELDDemoEffectTrigger::Damaged})
            || !F.Fixed(TEXT("base"), TEXT("hp"), TEXT("p2"), Out.BaseP2)
            || !F.Fixed(TEXT("max_hp_ratio"), TEXT("ratio"), TEXT("bp"), Out.MaxHPRatioBp, 0, 10000)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !Check.Require(Out.BaseP2 > 0 || Out.MaxHPRatioBp > 0, Path, TEXT("ineffective shield"))
            || !F.Fixed(TEXT("interval"), TEXT("ms"), TEXT("ms"), Out.IntervalMs, 1)
            || !OptionalFixed(F, TEXT("burst_ratio"), TEXT("ratio"), TEXT("bp"), Out.SecondaryRatioBp, 1)
            || (F.Has(TEXT("burst_radius")) && !F.Fraction(TEXT("burst_radius"), TEXT("cm"), Out.RadiusCm, true))
            || !Check.Require((Out.SecondaryRatioBp > 0) == (Out.RadiusCm.Numerator > 0), Path,
                TEXT("a shield burst needs its ratio and a positive radius")))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::ShieldBreakRetort:
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1)
            || !F.Fixed(TEXT("slow"), TEXT("ratio"), TEXT("bp"), Out.SecondaryRatioBp, 0, 9999)
            || !F.Fixed(TEXT("slow_duration"), TEXT("ms"), TEXT("ms"), Out.SecondaryDurationMs)
            || !F.Fixed(TEXT("internal_cooldown"), TEXT("ms"), TEXT("ms"), Out.IntervalMs, 1)
            || !Check.Require((Out.SecondaryRatioBp > 0) == (Out.SecondaryDurationMs > 0), Path, TEXT("a retort slow needs ratio and duration")))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::NthBasicBonus:
        // #198 B (B06): every Count-th landed primary basic knocks its target away from the holder; no damage.
        if (!IntInto(F, TEXT("count"), Out.Count, 2, 10)
            || !F.Fraction(TEXT("distance"), TEXT("cm"), Out.DistanceCm, true)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::ManaOnHit:
        if (!TriggerField(F, Out.Trigger, {ELDDemoEffectTrigger::AbilityHit})
            || !F.Fixed(TEXT("mana"), TEXT("mp"), TEXT("p2"), Out.BaseP2, 1))
        {
            return false;
        }
        break;
    // #147 build v7 keyword primitives (docs/systems/COMBAT_AND_GROWTH.md).
    case ELDDemoEffectOp::Breach:
    case ELDDemoEffectOp::Burn:
        // stacks: per hit of an active; basic_stacks: per primary basic hit of a component (#198 B), never both.
        if ((F.Has(TEXT("stacks")) && !IntInto(F, TEXT("stacks"), Out.Count, 1, 10))
            || (F.Has(TEXT("basic_stacks")) && !IntInto(F, TEXT("basic_stacks"), Out.BasicCount, 1, 10))
            || !Check.Require(bComponent ? Out.BasicCount > 0 && Out.Count == 0 : Out.Count > 0 && Out.BasicCount == 0, Path,
                bComponent ? TEXT("a component applies its stacks with each primary basic hit")
                    : TEXT("an active applies its stacks to the bodies it hits")))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::ZoneBurn:
        if (!IntInto(F, TEXT("stacks"), Out.Count, 1, 10) || !F.Fixed(TEXT("interval"), TEXT("ms"), TEXT("ms"), Out.IntervalMs, 1))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::ChannelCone:
        if (!F.Fixed(TEXT("interval"), TEXT("ms"), TEXT("ms"), Out.IntervalMs, 1)
            || !IntInto(F, TEXT("count"), Out.Count, 2, 20)
            || !OptionalFixed(F, TEXT("slow"), TEXT("ratio"), TEXT("bp"), Out.SecondaryRatioBp, 1, 9999)
            || !OptionalFixed(F, TEXT("slow_duration"), TEXT("ms"), TEXT("ms"), Out.SecondaryDurationMs, 1)
            || !Check.Require((Out.SecondaryRatioBp > 0) == (Out.SecondaryDurationMs > 0)
                && Out.IntervalMs <= MAX_int64 / (Out.Count - 1), Path, TEXT("invalid channel timing or final slow")))
        { return false; }
        Out.DurationMs = Out.IntervalMs * (Out.Count - 1);
        break;
    case ELDDemoEffectOp::GroundImpact:
    case ELDDemoEffectOp::ContactTrap:
    {
        const bool bImpact = Out.GetOp() == ELDDemoEffectOp::GroundImpact;
        FNumbers Numbers(Check);
        if (!F.Fraction(TEXT("radius"), TEXT("cm"), Out.RadiusCm, true)
            || !F.Fraction(bImpact ? TEXT("inner_radius") : TEXT("trigger_radius"), TEXT("cm"), Out.DistanceCm, true)
            || !F.Fixed(TEXT("delay"), TEXT("ms"), TEXT("ms"), Out.DelayMs, 1)
            || !Check.Require(Numbers.LE(Numbers.Value(Out.DistanceCm), Numbers.Value(Out.RadiusCm)), Path,
                TEXT("inner/trigger radius must fit the effect radius"))) { return false; }
        if (bImpact ? !F.Fixed(TEXT("outer_ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999)
                    : !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)) { return false; }
        break;
    }
    case ELDDemoEffectOp::DashContact:
    {
        // First: only the first body the dash touches takes the native hit; Every: each touched body once (#198 S13).
        FString Targets;
        if (!F.Text(TEXT("targets"), Targets)
            || !Check.Require(Targets.Equals(TEXT("First"), ESearchCase::CaseSensitive)
                || Targets.Equals(TEXT("Every"), ESearchCase::CaseSensitive), Path, TEXT("unknown dash contact targets"))) { return false; }
        Out.bEveryContact = Targets.Equals(TEXT("Every"), ESearchCase::CaseSensitive);
        Out.Count = Out.bEveryContact ? 0 : 1;
        break;
    }
    // #198 session A: elements, reactions and their actives (docs/systems/COMBAT_AND_GROWTH.md, "水火电").
    case ELDDemoEffectOp::Wet:
    case ELDDemoEffectOp::Electrify:
    case ELDDemoEffectOp::ZoneWet:
        break;
    case ELDDemoEffectOp::ChainBounce:
        if (!IntInto(F, TEXT("count"), Out.Count, 1, 5)
            || !F.Fraction(TEXT("radius"), TEXT("cm"), Out.RadiusCm, true)
            || !F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999)
            || !F.Fixed(TEXT("interval"), TEXT("ms"), TEXT("ms"), Out.IntervalMs, 1)
            || !Check.Require(Out.RadiusCm.Numerator > 0, Path, TEXT("a chain needs a positive search radius"))) { return false; }
        break;
    case ELDDemoEffectOp::Brace:
        if (!F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999)
            || !F.Fixed(TEXT("limit"), TEXT("points"), TEXT("p2"), Out.LimitP2, 1)
            || !F.Fraction(TEXT("radius"), TEXT("cm"), Out.RadiusCm, true)
            || !F.Fixed(TEXT("base"), TEXT("points"), TEXT("p2"), Out.BaseP2, 1)
            || !F.Fixed(TEXT("record_ratio"), TEXT("ratio"), TEXT("bp"), Out.SecondaryRatioBp, 1)
            || !Check.Require(Out.RadiusCm.Numerator > 0, Path, TEXT("a brace answer needs a positive radius"))) { return false; }
        break;
    case ELDDemoEffectOp::ZoneHaste:
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999)) { return false; }
        break;
    case ELDDemoEffectOp::Grievous:
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)) { return false; }
        break;
    case ELDDemoEffectOp::AttackSlow:
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)) { return false; }
        break;
    case ELDDemoEffectOp::DashContactDamage:
        if (!DamageTerms(F, Out, Check, Path)
            || !Check.Require(Out.DamageType == ELDDemoDamageType::Physical, Path, TEXT("dash contact damage is physical"))
            || !OptionalFixed(F, TEXT("slow"), TEXT("ratio"), TEXT("bp"), Out.SecondaryRatioBp, 1, 9999)
            || !OptionalFixed(F, TEXT("slow_duration"), TEXT("ms"), TEXT("ms"), Out.SecondaryDurationMs, 1)
            || !Check.Require((Out.SecondaryRatioBp > 0) == (Out.SecondaryDurationMs > 0), Path, TEXT("contact slow needs both terms")))
        { return false; }
        break;
    case ELDDemoEffectOp::PrimaryGuard:
        if (!F.Fixed(TEXT("base"), TEXT("points"), TEXT("p2"), Out.BaseP2, 1)
            || !F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999)
            || !OptionalFixed(F, TEXT("slow_resistance"), TEXT("ratio"), TEXT("bp"), Out.SecondaryRatioBp, 1, 9999)
            || !OptionalFixed(F, TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !Check.Require((Out.SecondaryRatioBp > 0) == (Out.DurationMs > 0), Path, TEXT("slow resistance needs duration")))
        { return false; }
        break;
    case ELDDemoEffectOp::DashContactRefund:
        if (!F.Fixed(TEXT("cooldown"), TEXT("ms"), TEXT("ms"), Out.AmountMs, 1)
            || !F.Fixed(TEXT("interval"), TEXT("ms"), TEXT("ms"), Out.IntervalMs, 1)
            || !OptionalFixed(F, TEXT("mana_ratio"), TEXT("ratio"), TEXT("bp"), Out.SecondaryRatioBp, 1, 9999)
            || !OptionalFixed(F, TEXT("mana_cap"), TEXT("mp"), TEXT("p2"), Out.LimitP2, 1)
            || !Check.Require((Out.SecondaryRatioBp > 0) == (Out.LimitP2 > 0), Path, TEXT("contact mana needs ratio and cap")))
        { return false; }
        break;
    // #198 B component-only operations (field use: FLDDemoEffectDefinition).
    case ELDDemoEffectOp::Ledger:
        if (!DamageTerms(F, Out, Check, Path) || !IntInto(F, TEXT("count"), Out.Count, 2, 10)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1))
        { return false; }
        break;
    case ELDDemoEffectOp::BasicElement:
    {
        FString Element;
        if (!F.Text(TEXT("element"), Element)
            || !Check.Require(ElementField(Element, Out.Element), Path + TEXT(".element"), TEXT("unknown element")))
        { return false; }
        break;
    }
    case ELDDemoEffectOp::BasicDamageScale:
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999)) { return false; }
        break;
    case ELDDemoEffectOp::IdleReadyAmp:
        if (!F.Fixed(TEXT("delay"), TEXT("ms"), TEXT("ms"), Out.DelayMs, 1)
            || !F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1))
        { return false; }
        break;
    case ELDDemoEffectOp::ActiveElement:
        break;
    default:
        return Check.Require(false, Path, TEXT("unimplemented effect projection"));
    }
    return F.Done();
}

bool Effects(const FValue& Value, TArray<FLDDemoEffectDefinition>& Out, bool bComponent, FOpMask& Mask,
    FCheck& Check, const FString& Path)
{
    const TArray<FValue>* Rows;
    if (!Array(Value, Rows, Check, Path, 1, 8))
    {
        return false;
    }
    for (int32 I = 0; I < Rows->Num(); ++I)
    {
        FLDDemoEffectDefinition Definition;
        if (!Effect((*Rows)[I], Definition, bComponent, Check, FString::Printf(TEXT("%s[%d]"), *Path, I)))
        {
            return false;
        }
        const FOpMask B = OpBit(Definition.GetOp());
        if (!Check.Require(!(Mask & B).Any(), Path, TEXT("duplicate effect operation")))
        {
            return false;
        }
        Mask |= B;
        Out.Add(MoveTemp(Definition));
    }
    return true;
}

// #198: the single value set an active row carries beside its identity, target and windup.
bool Values(FFields& F, FLDDemoAbilityValues& Out, FOpMask ExpectedOps, FCheck& Check, const FString& Path)
{
    FOpMask Mask;
    return F.Fixed(TEXT("mana_cost"), TEXT("mp"), TEXT("p2"), Out.ManaCostP2)
        && F.Fixed(TEXT("cooldown"), TEXT("ms"), TEXT("ms"), Out.CooldownMs, 1)
        && Effects(F.Take(TEXT("effects")), Out.Effects, false, Mask, Check, Path + TEXT(".effects"))
        && Check.Require(Mask == ExpectedOps, Path + TEXT(".effects"), TEXT("missing or extra effect; definition/target projection mismatch"));
}

bool Ability(const FValue& Value, FLDDemoAbilityDefinition& Out, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    if (!NativeIdentity(F, TEXT("abilities"), Out.Id, Out.NativeId))
    {
        return false;
    }
    ELDDemoTargetMode Mode = ELDDemoTargetMode::Self;
    FOpMask ExpectedOps;
    uint32 TimedStats = 0;
    using EOp = ELDDemoEffectOp;
    // #198 actives (CONTENT_AND_BALANCE §4.2).
    switch (Out.GetNativeId())
    {
    case ELDDemoAbilityId::ActiveElbow:
        Mode = ELDDemoTargetMode::Unit; ExpectedOps = OpBit(EOp::Damage) | OpBit(EOp::Knockback); break;
    case ELDDemoAbilityId::ActivePantsDown:
        Mode = ELDDemoTargetMode::Unit; ExpectedOps = OpBit(EOp::Damage) | OpBit(EOp::Root); break;
    case ELDDemoAbilityId::ActiveCrotchGrab:
        Mode = ELDDemoTargetMode::Unit; ExpectedOps = OpBit(EOp::Damage) | OpBit(EOp::Stun); break;
    case ELDDemoAbilityId::ActiveAccountBanned:
        Mode = ELDDemoTargetMode::Unit; ExpectedOps = OpBit(EOp::Damage) | OpBit(EOp::Silence); break;
    case ELDDemoAbilityId::ActiveUnplug:
        Mode = ELDDemoTargetMode::Unit; ExpectedOps = OpBit(EOp::Damage) | OpBit(EOp::Disarm); break;
    case ELDDemoAbilityId::ActiveSnarkBolt:
        Mode = ELDDemoTargetMode::DirectionProjectile; ExpectedOps = OpBit(EOp::Damage) | OpBit(EOp::Slow); break;
    case ELDDemoAbilityId::ActiveSeethe:
        ExpectedOps = OpBit(EOp::SelfArea) | OpBit(EOp::Damage) | OpBit(EOp::Burn); break;
    case ELDDemoAbilityId::ActiveQuickMeeting:
        Mode = ELDDemoTargetMode::GroundPoint;
        ExpectedOps = OpBit(EOp::Zone) | OpBit(EOp::ZoneDamage) | OpBit(EOp::ZoneSlow) | OpBit(EOp::ZoneBurn); break;
    case ELDDemoAbilityId::ActiveBailOut:
        Mode = ELDDemoTargetMode::DirectionDash; ExpectedOps = OpBit(EOp::Dash) | OpBit(EOp::EmpowerBasic); break;
    case ELDDemoAbilityId::ActiveApologySlide:
        Mode = ELDDemoTargetMode::DirectionDash;
        ExpectedOps = OpBit(EOp::Dash) | OpBit(EOp::DashContact) | OpBit(EOp::Damage) | OpBit(EOp::Slow); break;
    case ELDDemoAbilityId::ActiveNotMad:
        ExpectedOps = OpBit(EOp::Shield) | OpBit(EOp::DispelSlow) | OpBit(EOp::DispelRoot); break;
    case ELDDemoAbilityId::ActiveStareDown:
        ExpectedOps = OpBit(EOp::Taunt) | OpBit(EOp::TimedStatBonus);
        TimedStats = Bit(ELDDemoBonusStat::ArmorFlat) | Bit(ELDDemoBonusStat::MagicResistFlat); break;
    case ELDDemoAbilityId::ActiveTripleCombo:
        ExpectedOps = OpBit(EOp::EmpowerBasic); break;
    case ELDDemoAbilityId::ActiveCalculusDrop:
        Mode = ELDDemoTargetMode::GroundPoint; ExpectedOps = OpBit(EOp::GroundImpact) | OpBit(EOp::Damage); break;
    case ELDDemoAbilityId::ActiveFlyerTrap:
        Mode = ELDDemoTargetMode::GroundPoint; ExpectedOps = OpBit(EOp::ContactTrap) | OpBit(EOp::Damage) | OpBit(EOp::Slow); break;
    case ELDDemoAbilityId::ActiveOpenMic:
        Mode = ELDDemoTargetMode::DirectionCone; ExpectedOps = OpBit(EOp::ChannelCone) | OpBit(EOp::Damage); break;
    case ELDDemoAbilityId::ActiveHoseDown:
        Mode = ELDDemoTargetMode::DirectionLine; ExpectedOps = OpBit(EOp::Damage) | OpBit(EOp::Knockback) | OpBit(EOp::Wet); break;
    case ELDDemoAbilityId::ActiveZap:
        Mode = ELDDemoTargetMode::Unit; ExpectedOps = OpBit(EOp::Damage) | OpBit(EOp::Electrify) | OpBit(EOp::ChainBounce); break;
    case ELDDemoAbilityId::ActiveChargedRam:
        Mode = ELDDemoTargetMode::DirectionDash;
        ExpectedOps = OpBit(EOp::Dash) | OpBit(EOp::Shield) | OpBit(EOp::DashContact) | OpBit(EOp::Damage) | OpBit(EOp::Electrify); break;
    case ELDDemoAbilityId::ActiveBrace:
        ExpectedOps = OpBit(EOp::Brace); break;
    case ELDDemoAbilityId::ActiveDirtyPuddle:
        Mode = ELDDemoTargetMode::GroundPoint;
        ExpectedOps = OpBit(EOp::Zone) | OpBit(EOp::ZoneDamage) | OpBit(EOp::ZoneWet) | OpBit(EOp::ZoneHaste); break;
    case ELDDemoAbilityId::ActiveSockShot:
        Mode = ELDDemoTargetMode::DirectionProjectile;
        ExpectedOps = OpBit(EOp::Damage) | OpBit(EOp::AttackSlow) | OpBit(EOp::Grievous); break;
    default:
        return Check.Require(false, Path, TEXT("unimplemented ability projection"));
    }
    if (!F.Flag(TEXT("opening"), Out.Opening)
        || !Target(F.Take(TEXT("target")), Out.Target, Check, Path + TEXT(".target"))
        || !Check.Require(Out.Target.Mode == Mode, Path, TEXT("wrong definition target"))
        || !F.Fixed(TEXT("windup"), TEXT("ms"), TEXT("ms"), Out.WindupMs)
        || !F.Identifier(TEXT("cooldown_family"), Out.CooldownFamily)
        || !Check.Require(Out.CooldownFamily == TEXT("cd_") + Out.Id.Mid(7), Path, TEXT("wrong cooldown family"))
        || !Values(F, Out.Values, ExpectedOps, Check, Path))
    {
        return false;
    }
    if (TimedStats && !Check.Require(Out.Values.Effects.ContainsByPredicate([TimedStats](const FLDDemoEffectDefinition& Effect)
        { return Effect.GetOp() == ELDDemoEffectOp::TimedStatBonus && StatMask(Effect.Stats) == TimedStats; }),
        Path, TEXT("wrong timed stat definition")))
    {
        return false;
    }
    return F.Done();
}

// #198: one point of each stat adds exactly these bonuses.
bool StatDefinition(const FValue& Value, FLDDemoStatDefinition& Out, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    if (!NativeIdentity(F, TEXT("stats"), Out.Id, Out.NativeId)
        || !Stats(F.Take(TEXT("per_point")), Out.PerPoint, Check, Path + TEXT(".per_point")) || !F.Done())
    {
        return false;
    }
    using S = ELDDemoBonusStat;
    uint32 Expected = 0;
    switch (Out.GetNativeId())
    {
    case ELDDemoStatId::StatPhysique: Expected = Bit(S::MaxHPFlat) | Bit(S::HPRegenFlat); break;
    case ELDDemoStatId::StatThickSkin: Expected = Bit(S::ArmorFlat) | Bit(S::MagicResistFlat); break;
    case ELDDemoStatId::StatStrength: Expected = Bit(S::ADFlat) | Bit(S::CritChanceBonusRatio); break;
    case ELDDemoStatId::StatBrains: Expected = Bit(S::APFlat) | Bit(S::CooldownHasteFlat) | Bit(S::MPRegenFlat); break;
    case ELDDemoStatId::StatAgility: Expected = Bit(S::AttackSpeedBonusRatio) | Bit(S::MoveSpeedBonusRatio); break;
    default: return Check.Require(false, Path, TEXT("unimplemented stat projection"));
    }
    return Check.Require(StatMask(Out.PerPoint) == Expected, Path, TEXT("wrong stat bonuses"));
}

// #198 B: distinct actives of this catalog; bOpeningPool also requires each to be an opening-pool active.
bool ActiveIds(const FValue& Value, TArray<FString>& Out, const TArray<FLDDemoAbilityDefinition>& Abilities, bool bOpeningPool,
    FCheck& Check, const FString& Path)
{
    const TArray<FValue>* Rows;
    if (!Array(Value, Rows, Check, Path, 1, 32))
    {
        return false;
    }
    for (const auto& Row : *Rows)
    {
        FString Name;
        if (!String(Row, Name, Check, Path, 1, 63) || !Id(Name, Check, Path))
        {
            return false;
        }
        const FLDDemoAbilityDefinition* Ability = Abilities.FindByPredicate([&Name](const FLDDemoAbilityDefinition& Candidate)
            { return Candidate.Id.Equals(Name, ESearchCase::CaseSensitive); });
        if (!Check.Require(Ability != nullptr && !Out.Contains(Name), Path, TEXT("unknown or repeated active reference"))
            || !Check.Require(!bOpeningPool || Ability->Opening, Path, TEXT("an opening condition names actives of the opening pool")))
        {
            return false;
        }
        Out.Add(MoveTemp(Name));
    }
    return true;
}

// #198 B: one workshop material row (DemoContent._validate_components refuses the same shapes).
bool Component(const FValue& Value, FLDDemoComponentDefinition& Out, const TArray<FLDDemoAbilityDefinition>& Abilities,
    FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    FString Kind, Pool;
    if (!NativeIdentity(F, TEXT("components"), Out.Id, Out.NativeId) || !F.Text(TEXT("kind"), Kind) || !F.Text(TEXT("pool"), Pool))
    {
        return false;
    }
    if (Kind.Equals(TEXT("base"), ESearchCase::CaseSensitive)) { Out.Kind = ELDDemoComponentKind::Base; }
    else if (Kind.Equals(TEXT("mod"), ESearchCase::CaseSensitive)) { Out.Kind = ELDDemoComponentKind::Mod; }
    else if (Kind.Equals(TEXT("connector"), ESearchCase::CaseSensitive)) { Out.Kind = ELDDemoComponentKind::Connector; }
    else { return Check.Require(false, Path + TEXT(".kind"), TEXT("unknown component kind")); }
    if (Pool.Equals(TEXT("common"), ESearchCase::CaseSensitive)) { Out.Pool = ELDDemoComponentPool::Common; }
    else if (Pool.Equals(TEXT("scarce"), ESearchCase::CaseSensitive)) { Out.Pool = ELDDemoComponentPool::Scarce; }
    else { return Check.Require(false, Path + TEXT(".pool"), TEXT("unknown component pool")); }
    if (F.Has(TEXT("opening")))
    {
        const FValue Opening = F.Take(TEXT("opening"));
        if (!Check.Require(Out.Kind == ELDDemoComponentKind::Base && Out.Pool == ELDDemoComponentPool::Common, Path,
            TEXT("only common base components are opening options")))
        {
            return false;
        }
        if (Opening && Opening->Type == EJson::String)
        {
            FString Always;
            if (!String(Opening, Always, Check, Path + TEXT(".opening"))
                || !Check.Require(Always.Equals(TEXT("always"), ESearchCase::CaseSensitive), Path + TEXT(".opening"),
                    TEXT("unsupported opening condition")))
            {
                return false;
            }
            Out.bOpeningAlways = true;
        }
        else
        {
            FFields Condition(Check, Opening, Path + TEXT(".opening"));
            if (!ActiveIds(Condition.Take(TEXT("actives")), Out.OpeningActives, Abilities, true, Check, Condition.Path + TEXT(".actives"))
                || !Condition.Done())
            {
                return false;
            }
        }
    }
    FOpMask Mask;
    if (!Effects(F.Take(TEXT("effects")), Out.Effects, true, Mask, Check, Path + TEXT(".effects")))
    {
        return false;
    }
    if (F.Has(TEXT("binding")))
    {
        FFields Binding(Check, F.Take(TEXT("binding")), Path + TEXT(".binding"));
        const TArray<FValue>* Elements = nullptr;
        if (!Binding.Literal(TEXT("kind"), TEXT("active_element"))
            || !ActiveIds(Binding.Take(TEXT("actives")), Out.BindActives, Abilities, false, Check, Binding.Path + TEXT(".actives"))
            || !Array(Binding.Take(TEXT("elements")), Elements, Check, Binding.Path + TEXT(".elements"), 1, 3))
        {
            return false;
        }
        for (const FString& Active : Out.BindActives)
        {
            const FLDDemoAbilityDefinition* Ability = Abilities.FindByPredicate([&Active](const FLDDemoAbilityDefinition& Candidate)
                { return Candidate.Id == Active; });
            if (!Check.Require(Ability->Values.Effects.ContainsByPredicate([](const FLDDemoEffectDefinition& Effect)
                { return Effect.GetOp() == ELDDemoEffectOp::Damage || Effect.GetOp() == ELDDemoEffectOp::ZoneDamage
                    || Effect.GetOp() == ELDDemoEffectOp::Brace; }),
                Binding.Path, TEXT("a bound active needs native direct damage")))
            {
                return false;
            }
        }
        for (const FValue& Row : *Elements)
        {
            FString Name;
            ELDDemoElement Element = ELDDemoElement::None;
            if (!String(Row, Name, Check, Binding.Path + TEXT(".elements"))
                || !Check.Require(ElementField(Name, Element) && !Out.BindElements.Contains(Element), Binding.Path + TEXT(".elements"),
                    TEXT("binding elements are distinct fire/water/electric")))
            {
                return false;
            }
            Out.BindElements.Add(Element);
        }
        if (!Binding.Done())
        {
            return false;
        }
        Out.Binding = ELDDemoComponentBinding::ActiveElement;
    }
    return Check.Require((Out.Binding == ELDDemoComponentBinding::ActiveElement) == (Mask & OpBit(ELDDemoEffectOp::ActiveElement)).Any(),
            Path, TEXT("active_element and its active/element binding come together"))
        && F.Done();
}

// #198 B: the catalog in ascending native ID order, each definition once.
bool Components(const FValue& Value, TArray<FLDDemoComponentDefinition>& Out, const TArray<FLDDemoAbilityDefinition>& Abilities,
    FCheck& Check)
{
    const TArray<FValue>* Rows;
    if (!Array(Value, Rows, Check, TEXT("content.components"), 1, 64))
    {
        return false;
    }
    for (int32 I = 0; I < Rows->Num(); ++I)
    {
        FLDDemoComponentDefinition Row;
        const FString Path = FString::Printf(TEXT("content.components[%d]"), I);
        if (!Component((*Rows)[I], Row, Abilities, Check, Path)
            || !Check.Require(Out.IsEmpty() || Out.Last().NativeId < Row.NativeId, Path,
                TEXT("components are listed once each in ascending native ID order")))
        {
            return false;
        }
        Out.Add(MoveTemp(Row));
    }
    return true;
}

bool Monster(const FValue& Value, FLDDemoMonsterDefinition& Out, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    FString Tier;
    if (!NativeIdentity(F, TEXT("monsters"), Out.Id, Out.NativeId) || !F.Text(TEXT("tier"), Tier)
        || !Check.Require(Tier.Equals(TEXT("Low"), ESearchCase::CaseSensitive)
            || Tier.Equals(TEXT("High"), ESearchCase::CaseSensitive) || Tier.Equals(TEXT("Elite"), ESearchCase::CaseSensitive),
            Path, TEXT("unknown camp tier")))
    {
        return false;
    }
    Out.Tier = Tier.Equals(TEXT("Low"), ESearchCase::CaseSensitive) ? ELDDemoCampTier::Low
        : Tier.Equals(TEXT("High"), ESearchCase::CaseSensitive) ? ELDDemoCampTier::High : ELDDemoCampTier::Elite;
    return F.Int(TEXT("count_per_camp"), Out.CountPerCamp, 1)
        && F.Fixed(TEXT("hp"), TEXT("hp"), TEXT("p2"), Out.HPP2, 1)
        && F.Fixed(TEXT("armor"), TEXT("points"), TEXT("p2"), Out.ArmorP2)
        && F.Fixed(TEXT("magic_resist"), TEXT("points"), TEXT("p2"), Out.MagicResistP2)
        && F.Fixed(TEXT("attack"), TEXT("points"), TEXT("p2"), Out.AttackP2, 1)
        && F.Fraction(TEXT("attack_speed"), TEXT("attacks/s"), Out.AttackSpeed, true)
        && F.Fixed(TEXT("attack_windup"), TEXT("ms"), TEXT("ms"), Out.AttackWindupMs, 1)
        && F.Int(TEXT("attack_interval_ms"), Out.AttackIntervalMs, 1)
        && F.Fraction(TEXT("attack_range"), TEXT("cm"), Out.AttackRangeCm, true)
        && F.Fraction(TEXT("move_speed"), TEXT("cm/s"), Out.MoveSpeedCmPerSec, true)
        && F.Fraction(TEXT("collision_radius"), TEXT("cm"), Out.CollisionRadiusCm, true)
        && F.Fraction(TEXT("leash_radius"), TEXT("cm"), Out.LeashRadiusCm, true)
        && F.Fraction(TEXT("perception_radius"), TEXT("cm"), Out.PerceptionRadiusCm, true)
        && F.Fixed(TEXT("lose_target_grace"), TEXT("ms"), TEXT("ms"), Out.LoseTargetGraceMs, 1)
        && F.Fixed(TEXT("respawn"), TEXT("ms"), TEXT("ms"), Out.RespawnMs, 1)
        && F.Int(TEXT("reward_xp"), Out.RewardXP)
        && F.Fraction(TEXT("hp_regen"), TEXT("hp/s"), Out.HPRegenPerSec)
        && F.Fixed(TEXT("max_mp"), TEXT("mp"), TEXT("p2"), Out.MaxMPP2, 0, 0)
        && F.Fraction(TEXT("mp_regen"), TEXT("mp/s"), Out.MPRegenPerSec)
        && F.Fixed(TEXT("ap"), TEXT("points"), TEXT("p2"), Out.APP2, 0, 0)
        && F.Fixed(TEXT("crit_chance"), TEXT("ratio"), TEXT("bp"), Out.CritChanceBp, 0, 0)
        && Check.Require(Out.HPRegenPerSec.Numerator == 0 && Out.MPRegenPerSec.Numerator == 0,
            Path, TEXT("unsupported monster regeneration")) && F.Done();
}

// Mirrors DemoContent.display_text: invisible, control, bidirectional and astral characters are
// refused, braces are reserved by FText formatting, and description numbers come from typed fields.
bool ValidateDisplayText(const FString& Text, bool bAllowDigits, FCheck& Check, const FString& Path)
{
    for (const TCHAR C : Text)
    {
        const uint32 Code = static_cast<uint32>(C);
        const bool bForbidden = Code < 0x20 || (Code >= 0x7f && Code <= 0x9f) || (Code >= 0x200b && Code <= 0x200f)
            || (Code >= 0x2028 && Code <= 0x202e) || (Code >= 0x2060 && Code <= 0x206f) || Code == 0xfeff
            || Code == TEXT('{') || Code == TEXT('}') || (Code >= 0xd800 && Code <= 0xdfff);
        if (!Check.Require(!bForbidden, Path, TEXT("display text contains a forbidden character")))
        {
            return false;
        }
    }
    const auto IsEdgeSpace = [](TCHAR C) { return C == TEXT(' ') || static_cast<uint32>(C) == 0x3000; };
    if (!Check.Require(!Text.IsEmpty() && !IsEdgeSpace(Text[0]) && !IsEdgeSpace(Text[Text.Len() - 1]),
        Path, TEXT("display text has leading or trailing space")))
    {
        return false;
    }
    for (const TCHAR C : Text)
    {
        const uint32 Code = static_cast<uint32>(C);
        const bool bDigit = (Code >= TEXT('0') && Code <= TEXT('9')) || (Code >= 0xff10 && Code <= 0xff19);
        if (!Check.Require(bAllowDigits || !bDigit, Path,
            TEXT("display description contains digits; numbers come from typed fields")))
        {
            return false;
        }
    }
    return true;
}

template <typename T, typename ParseRow>
bool Definitions(const FValue& Value, TArray<T>& Out, FCheck& Check, const FString& Path,
    int32 Min, int32 Max, ParseRow Parse)
{
    const TArray<FValue>* Rows;
    if (!Array(Value, Rows, Check, Path, Min, Max))
    {
        return false;
    }
    FString Previous;
    for (int32 I = 0; I < Rows->Num(); ++I)
    {
        T Row;
        if (!Parse((*Rows)[I], Row, Check, FString::Printf(TEXT("%s[%d]"), *Path, I))
            || !Check.Require(Previous.IsEmpty() || Previous < Row.Id, Path, TEXT("duplicate or unsorted definition ID")))
        {
            return false;
        }
        Previous = Row.Id;
        Out.Add(MoveTemp(Row));
    }
    return true;
}

bool Ids(const FValue& Value, TArray<FString>& Out, FCheck& Check, const FString& Path, int32 Min, int32 Max)
{
    const TArray<FValue>* Rows;
    if (!Array(Value, Rows, Check, Path, Min, Max))
    {
        return false;
    }
    for (const auto& Row : *Rows)
    {
        FString Name;
        if (!String(Row, Name, Check, Path, 1, 63) || !Id(Name, Check, Path)
            || !Check.Require(Out.IsEmpty() || Out.Last() < Name, Path, TEXT("duplicate or unsorted ID")))
        {
            return false;
        }
        Out.Add(MoveTemp(Name));
    }
    return true;
}

bool Progression(const FValue& Value, FLDDemoProgressionDefinition& Out, FCheck& Check)
{
    FFields F(Check, Value, TEXT("content.progression"));
    if (!F.SmallInt(TEXT("max_level"), Out.MaxLevel, LDDemoProgression::MaxLevel, LDDemoProgression::MaxLevel)
        || !F.SmallInt(TEXT("active_slots"), Out.ActiveSlots, 3, 3)
        || !F.Int(TEXT("xp_start"), Out.XPStart, 0, 0))
    {
        return false;
    }
    for (const auto& Pair : {TPair<const TCHAR*, TArray<int64>*>(TEXT("xp_thresholds"), &Out.XPThresholds),
        {TEXT("xp_to_next"), &Out.XPToNext}})
    {
        const int32 Count = Pair.Value == &Out.XPThresholds ? 16 : 15;
        const TArray<FValue>* Rows;
        if (!Array(F.Take(Pair.Key), Rows, Check, F.Path, Count, Count))
        {
            return false;
        }
        for (const auto& Row : *Rows)
        {
            int64 Number;
            if (!Integer(Row, Number, Check, F.Path))
            {
                return false;
            }
            Pair.Value->Add(Number);
        }
    }
    const TArray<FValue>* Levels;
    if (!Array(F.Take(TEXT("active_offer_levels")), Levels, Check, F.Path, 0, 15))
    {
        return false;
    }
    for (const auto& Row : *Levels)
    {
        int64 Level;
        if (!Integer(Row, Level, Check, F.Path, 2, Out.MaxLevel)
            || !Check.Require(Out.ActiveOfferLevels.IsEmpty() || Out.ActiveOfferLevels.Last() < Level, F.Path,
                TEXT("active offer levels must be distinct and ascending")))
        {
            return false;
        }
        Out.ActiveOfferLevels.Add(int32(Level));
    }
    // #198 B: level-up component category bands cover every level 2..MaxLevel once, ascending; each splits 100 percent.
    const TArray<FValue>* Bands;
    if (!Array(F.Take(TEXT("component_category_weights")), Bands, Check, F.Path + TEXT(".component_category_weights"), 1, 15))
    {
        return false;
    }
    int32 NextLevel = 2;
    for (const auto& Row : *Bands)
    {
        FFields Band(Check, Row, F.Path + TEXT(".component_category_weights"));
        FLDDemoComponentCategoryWeights Weights;
        const TArray<FValue>* BandLevels;
        int64 Low = 0, High = 0;
        if (!Array(Band.Take(TEXT("levels")), BandLevels, Check, Band.Path, 2, 2)
            || !Integer((*BandLevels)[0], Low, Check, Band.Path, 2, Out.MaxLevel) || !Integer((*BandLevels)[1], High, Check, Band.Path, 2, Out.MaxLevel)
            || !Band.SmallInt(TEXT("base"), Weights.BasePercent, 0, 100) || !Band.SmallInt(TEXT("mod"), Weights.ModPercent, 0, 100)
            || !Band.SmallInt(TEXT("connector"), Weights.ConnectorPercent, 0, 100) || !Band.Done()
            || !Check.Require(Low == NextLevel && Low <= High, Band.Path,
                TEXT("component category bands must cover levels 2..max_level contiguously, ascending and exactly once"))
            || !Check.Require(Weights.BasePercent + Weights.ModPercent + Weights.ConnectorPercent == 100, Band.Path,
                TEXT("component category percentages must add up to 100")))
        {
            return false;
        }
        Weights.MinLevel = int32(Low);
        Weights.MaxLevel = int32(High);
        NextLevel = Weights.MaxLevel + 1;
        Out.ComponentCategoryWeights.Add(Weights);
    }
    return Check.Require(NextLevel == Out.MaxLevel + 1, F.Path + TEXT(".component_category_weights"),
            TEXT("component category bands must reach max_level"))
        && F.Done();
}

bool CardTemplates(const FLDDemoContentDefinitions& Out, FCheck& Check);

bool Content(const FValue& Value, FLDDemoContentDefinitions& Out, FCheck& Check)
{
    FFields F(Check, Value, TEXT("content"));
    FFields Identity(Check, F.Take(TEXT("identity")), TEXT("content.identity"));
    int64 Revision;
    if (!Identity.Int(TEXT("schema_version"), Revision, 2, 2)
        || !Identity.Int(TEXT("handler_revision"), Revision, 3, 3)
        || !Identity.Int(TEXT("projection_revision"), Revision, 5, 5))
    {
        return false;
    }
    FFields Approval(Check, Identity.Take(TEXT("approval")), TEXT("content.identity.approval"));
    if (!Approval.Text(TEXT("status"), Out.Identity.ApprovalStatus)
        || !Check.Require(Out.Identity.ApprovalStatus.Equals(TEXT("approved"), ESearchCase::CaseSensitive), Approval.Path, TEXT("approval metadata required"))
        || !Approval.Text(TEXT("decision"), Out.Identity.ApprovalDecision, 1, 1024)
        || !Check.Require(!Out.Identity.ApprovalDecision.TrimStartAndEnd().IsEmpty(), Approval.Path, TEXT("empty decision reference"))
        || !Approval.Done() || !Identity.Done()
        || !Parameters(F.Take(TEXT("parameters")), Out.Parameters, Check)
        || !Formulas(F.Take(TEXT("formulas")), Out.Formulas, Check)
        || !Policies(F.Take(TEXT("policies")), Out.Policies, Check))
    {
        return false;
    }
    FFields Character(Check, F.Take(TEXT("character")), TEXT("content.character"));
    if (!NativeIdentity(Character, TEXT("character"), Out.Character.Id, Out.Character.NativeId))
    {
        return false;
    }
    FFields Basic(Check, Character.Take(TEXT("basic_attack")), TEXT("content.character.basic_attack"));
    if (!Basic.Literal(TEXT("target_mode"), TEXT("Unit")) || !Basic.Literal(TEXT("damage_type"), TEXT("Physical"))
        || !Basic.Fixed(TEXT("ad_ratio"), TEXT("ratio"), TEXT("bp"), Out.Character.BasicADRatioBp, 10000, 10000)
        || !Basic.Int(TEXT("interval_ms"), Out.Character.BasicIntervalMs, 1) || !Basic.Done())
    {
        return false;
    }
    // D19: the innate dash is projected into the single value set Combat executes for every source.
    FFields Dash(Check, Character.Take(TEXT("dash")), TEXT("content.character.dash"));
    FLDDemoDashDefinition& InnateDash = Out.Character.Dash;
    FLDDemoEffectDefinition& DashMove = InnateDash.Values.Effects.AddDefaulted_GetRef();
    InnateDash.Target.Mode = ELDDemoTargetMode::DirectionDash;
    DashMove.Opcode = uint8(ELDDemoEffectOp::Dash);
    if (!Dash.Fixed(TEXT("mana_cost"), TEXT("mp"), TEXT("p2"), InnateDash.Values.ManaCostP2)
        || !Dash.Fixed(TEXT("cooldown"), TEXT("ms"), TEXT("ms"), InnateDash.Values.CooldownMs, 1)
        || !Dash.Fraction(TEXT("distance"), TEXT("cm"), DashMove.DistanceCm, true)
        || !Dash.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), DashMove.DurationMs, 1)
        || !Check.Require(DashMove.DurationMs < InnateDash.Values.CooldownMs, Dash.Path, TEXT("dash outlasts its cooldown"))
        || !Dash.Done() || !Character.Done()
        || !Definitions(F.Take(TEXT("abilities")), Out.Abilities, Check, TEXT("content.abilities"), 1, 22, Ability)
        || !Definitions(F.Take(TEXT("stats")), Out.Stats, Check, TEXT("content.stats"), 5, 5, StatDefinition)
        || !Components(F.Take(TEXT("components")), Out.Components, Out.Abilities, Check)
        || !Progression(F.Take(TEXT("progression")), Out.Progression, Check))
    {
        return false;
    }
    FFields Camps(Check, F.Take(TEXT("camps")), TEXT("content.camps"));
    if (!Definitions(Camps.Take(TEXT("monsters")), Out.Monsters, Check, TEXT("content.camps.monsters"), 3, 3, Monster))
    {
        return false;
    }
    const TArray<FValue>* Placements;
    if (!Array(Camps.Take(TEXT("placements")), Placements, Check, Camps.Path, 1, LDDemoWorld::MaxCamps))
    {
        return false;
    }
    for (const auto& ValueRow : *Placements)
    {
        FFields Row(Check, ValueRow, TEXT("content.camps.placements"));
        FLDDemoCampPlacement Placement;
        if (!Row.Identifier(TEXT("anchor_id"), Placement.AnchorId) || !Row.Identifier(TEXT("monster_id"), Placement.MonsterId)
            || !Row.Done() || !Check.Require(Out.Camps.IsEmpty() || Out.Camps.Last().AnchorId < Placement.AnchorId,
                Row.Path, TEXT("duplicate or unsorted camp placement")))
        {
            return false;
        }
        Out.Camps.Add(MoveTemp(Placement));
    }
    if (!Camps.Done())
    {
        return false;
    }
    // D26 (#79): the Boss block becomes its rules plus one Boss-tier monster row appended after the camp tiers.
    {
        FFields Boss(Check, F.Take(TEXT("boss")), TEXT("content.boss"));
        FLDDemoMonsterDefinition Body;
        FLDDemoBossDefinition& Rules = Out.Boss;
        Body.Tier = ELDDemoCampTier::Boss;
        Body.CountPerCamp = 1;
        if (!NativeIdentity(Boss, TEXT("monsters"), Body.Id, Body.NativeId)
            || !Boss.Fixed(TEXT("hp"), TEXT("hp"), TEXT("p2"), Body.HPP2, 1)
            || !Boss.Fixed(TEXT("armor"), TEXT("points"), TEXT("p2"), Body.ArmorP2)
            || !Boss.Fixed(TEXT("magic_resist"), TEXT("points"), TEXT("p2"), Body.MagicResistP2)
            || !Boss.Fixed(TEXT("attack"), TEXT("points"), TEXT("p2"), Body.AttackP2, 1)
            || !Boss.Fixed(TEXT("attack_interval"), TEXT("ms"), TEXT("ms"), Body.AttackIntervalMs, 1)
            || !Boss.Fixed(TEXT("attack_windup"), TEXT("ms"), TEXT("ms"), Body.AttackWindupMs, 1)
            || !Boss.Fraction(TEXT("attack_range"), TEXT("cm"), Body.AttackRangeCm, true)
            || !Boss.Fraction(TEXT("move_speed"), TEXT("cm/s"), Body.MoveSpeedCmPerSec, true)
            || !Boss.Fraction(TEXT("collision_radius"), TEXT("cm"), Body.CollisionRadiusCm, true)
            || !Boss.Fraction(TEXT("aggro_radius"), TEXT("cm"), Rules.AggroRadiusCm, true)
            || !Boss.Fixed(TEXT("attacker_memory"), TEXT("ms"), TEXT("ms"), Rules.AttackerMemoryMs, 1)
            || !Boss.Fixed(TEXT("appear_at"), TEXT("ms"), TEXT("ms"), Rules.AppearAtMs, 1)
            || !Boss.Int(TEXT("reward_xp"), Body.RewardXP))
        {
            return false;
        }
        FFields Buff(Check, Boss.Take(TEXT("approval")), TEXT("content.boss.approval"));
        if (!Buff.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Rules.ApprovalDurationMs, 1)
            || !Stats(Buff.Take(TEXT("stats")), Rules.ApprovalStats, Check, Buff.Path + TEXT(".stats"))
            || !Check.Require((StatMask(Rules.ApprovalStats) & ~TemporaryStats) == 0, Buff.Path,
                TEXT("the approval buff cannot change resource maxima"))
            || !Buff.Done() || !Boss.Done())
        {
            return false;
        }
        // One attack per interval: attacks/s = 1000 / interval ms, kept exact as a rational.
        const int64 Divisor = FMath::GreatestCommonDivisor(int64(1000), Body.AttackIntervalMs);
        Body.AttackSpeed = {1000 / Divisor, Body.AttackIntervalMs / Divisor};
        Body.LeashRadiusCm = Rules.AggroRadiusCm;
        Body.PerceptionRadiusCm = Rules.AggroRadiusCm;
        Body.LoseTargetGraceMs = Rules.AttackerMemoryMs;
        Body.HPRegenPerSec = {0, 1};
        Body.MPRegenPerSec = {0, 1};
        Rules.Id = Body.Id;
        Out.Monsters.Add(MoveTemp(Body));
    }
    FFields World(Check, F.Take(TEXT("world")), TEXT("content.world"));
    if (!World.Identifier(TEXT("map_id"), Out.World.MapId)
        || !Ids(World.Take(TEXT("spawn_ids")), Out.World.SpawnIds, Check, World.Path + TEXT(".spawn_ids"),
            LDDemoWorld::MinSpawns, LDDemoWorld::MaxSpawns)
        || !Ids(World.Take(TEXT("camp_ids")), Out.World.CampIds, Check, World.Path + TEXT(".camp_ids"), 1, LDDemoWorld::MaxCamps)
        || !World.Identifier(TEXT("capture_id"), Out.World.CaptureId)
        || !World.Done())
    {
        return false;
    }
    TSet<FString> DisplayIds;
    DisplayIds.Add(Out.Character.Id);
    for (const auto& Row : Out.Abilities) { DisplayIds.Add(Row.Id); }
    for (const auto& Row : Out.Stats) { DisplayIds.Add(Row.Id); }
    for (const auto& Row : Out.Components) { DisplayIds.Add(Row.Id); }
    for (const auto& Row : Out.Monsters) { DisplayIds.Add(Row.Id); }
    for (const auto& Entry : NativeIds)
    {
        if (FCString::Strcmp(Entry.Category, TEXT("statuses")) == 0) { DisplayIds.Add(Entry.Id); }
    }
    const FValue Display = F.Take(TEXT("display"));
    TArray<FString> DisplayNames;
    const auto ParseDisplay = [&Out, &DisplayIds, &DisplayNames](const FValue& Row, FLDDemoDisplayDefinition& D, FCheck& C, const FString& Path)
    {
        FFields Fields(C, Row, Path);
        if (!Fields.Identifier(TEXT("id"), D.Id) || !Fields.Text(TEXT("namespace"), D.Namespace)
            || !Fields.Text(TEXT("name_key"), D.NameKey, 1, 128)
            || !Fields.Text(TEXT("name_text"), D.NameText, 1, 32)
            || !ValidateDisplayText(D.NameText, true, C, Path + TEXT(".name_text"))
            || !Fields.Text(TEXT("description_key"), D.DescriptionKey, 1, 128)
            || !Fields.Text(TEXT("description_text"), D.DescriptionText, 1, 160)
            || !ValidateDisplayText(D.DescriptionText, false, C, Path + TEXT(".description_text"))
            || !Fields.Identifier(TEXT("icon_id"), D.IconId)
            || !C.Require(DisplayIds.Contains(D.Id) && D.Namespace.Equals(TEXT("DemoContent"), ESearchCase::CaseSensitive)
                && D.NameKey.Equals(D.Id + TEXT(".name"), ESearchCase::CaseSensitive)
                && D.DescriptionKey.Equals(D.Id + TEXT(".description"), ESearchCase::CaseSensitive)
                && D.IconId == TEXT("icon_") + D.Id, Path, TEXT("unknown or noncanonical display reference"))
            || !C.Require(!DisplayNames.ContainsByPredicate([&D](const FString& Name)
                { return Name.Equals(D.NameText, ESearchCase::CaseSensitive); }), Path, TEXT("duplicate display name")))
        {
            return false;
        }
        DisplayNames.Add(D.NameText);
        if (D.Id == Out.Character.Id
            && (!Fields.Identifier(TEXT("portrait_icon_id"), D.PortraitIconId)
                || !C.Require(D.PortraitIconId == TEXT("portrait_") + D.Id, Path, TEXT("wrong portrait reference"))))
        {
            return false;
        }
        return Fields.Done();
    };
    TArray<uint8> DisplayBytes;
    FLDDemoContentDigest DisplayDigest;
    return Definitions(Display, Out.Display, Check, TEXT("content.display"), DisplayIds.Num(), DisplayIds.Num(), ParseDisplay)
        && CardTemplates(Out, Check)
        && CanonicalBytes(Display, DisplayBytes, Check) && Hash(DisplayBytes, DisplayDigest, Check, TEXT("content.display"))
        && Check.Require(DisplayDigest == Out.Identity.Display, TEXT("content.display"), TEXT("display digest mismatch")) && F.Done();
}

// #147/#198: every placeholder of an active's or a component's description reads one of its effects; of a stat's, a
// per-point bonus.
bool CardTemplates(const FLDDemoContentDefinitions& Out, FCheck& Check)
{
    const auto Tokens = [&Out, &Check](const FString& Id, TArray<LDDemoCardText::FToken>& Parsed)
    {
        const FLDDemoDisplayDefinition* Display = Out.Display.FindByPredicate([&Id](const FLDDemoDisplayDefinition& Row) { return Row.Id == Id; });
        FString Error;
        return Check.Require(Display != nullptr, TEXT("content.display.") + Id, TEXT("missing card display"))
            && Check.Require(LDDemoCardText::Parse(Display->DescriptionText, Parsed, Error), TEXT("content.display.") + Id, Error);
    };
    for (const FLDDemoAbilityDefinition& Ability : Out.Abilities)
    {
        TArray<LDDemoCardText::FToken> Parsed;
        if (!Tokens(Ability.Id, Parsed))
        {
            return false;
        }
        for (const LDDemoCardText::FToken& Token : Parsed)
        {
            if (Token.bValue && !Check.Require(LDDemoCardText::FindEffect(Ability.Values, Token) != nullptr,
                TEXT("content.display.") + Ability.Id, TEXT("a card placeholder names no effect of this active")))
            {
                return false;
            }
        }
    }
    for (const FLDDemoComponentDefinition& Component : Out.Components)
    {
        TArray<LDDemoCardText::FToken> Parsed;
        if (!Tokens(Component.Id, Parsed))
        {
            return false;
        }
        for (const LDDemoCardText::FToken& Token : Parsed)
        {
            if (Token.bValue && !Check.Require(LDDemoCardText::FindEffect(Component.Effects, Token) != nullptr,
                TEXT("content.display.") + Component.Id, TEXT("a card placeholder names no effect of this component")))
            {
                return false;
            }
        }
    }
    for (const FLDDemoStatDefinition& Stat : Out.Stats)
    {
        TArray<LDDemoCardText::FToken> Parsed;
        if (!Tokens(Stat.Id, Parsed))
        {
            return false;
        }
        for (const LDDemoCardText::FToken& Token : Parsed)
        {
            if (Token.bValue && !Check.Require(LDDemoCardText::FindPerPoint(Stat, Token) != nullptr,
                TEXT("content.display.") + Stat.Id, TEXT("a card placeholder names no per-point bonus of this stat")))
            {
                return false;
            }
        }
    }
    return true;
}

bool ContractBytes(const FLDDemoContentBundle& Bundle, const FLDDemoContentIdentity& Identity, FCheck& Check)
{
    FString DigestBytes;
    for (int32 I = 0; I < 32; ++I)
    {
        if (I) { DigestBytes += TEXT(", "); }
        DigestBytes += FString::Printf(TEXT("0x%02x"), Identity.Root.Bytes[I]);
    }
    const FString ExpectedContent = FString::Printf(
        TEXT("#pragma once\n#define LD_DEMO_CONTENT_SCHEMA_VERSION 2\n#define LD_DEMO_CONTENT_HANDLER_REVISION 3\n")
        TEXT("#define LD_DEMO_CONTENT_PROJECTION_REVISION 5\n#define LD_DEMO_CONTENT_PAYLOAD_SHA256 TEXT(\"%s\")\n")
        TEXT("#define LD_DEMO_CATALOG_SHA256 TEXT(\"%s\")\n#define LD_DEMO_CATALOG_DIGEST_BYTES {%s}\n"),
        *Identity.Content.ToHex(), *Identity.Root.ToHex(), *DigestBytes);
    const FTCHARToUTF8 UTF8(*ExpectedContent);
    if (!Check.Require(Bundle.ContentContract.Num() == UTF8.Length()
        && FMemory::Memcmp(Bundle.ContentContract.GetData(), UTF8.Get(), UTF8.Length()) == 0,
        TEXT("content contract"), TEXT("generated content contract does not match exact root/payload bytes")))
    {
        return false;
    }
    for (uint8 Byte : Bundle.WorldContract)
    {
        if (!Check.Require(Byte < 128, TEXT("world contract"), TEXT("expected ASCII generated contract")))
        {
            return false;
        }
    }
    const FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Bundle.WorldContract.GetData()), Bundle.WorldContract.Num());
    const FString WorldText(Decoded.Length(), Decoded.Get());
    const FString ExpectedWorld = FString::Printf(
        TEXT("#pragma once\n#define LD_DEMO_WORLD_SCHEMA_VERSION 3\n#define LD_DEMO_WORLD_PROJECTION_REVISION 3\n")
        TEXT("#define LD_DEMO_WORLD_PAYLOAD_SHA256 TEXT(\"%s\")\n"), *Identity.World.ToHex());
    return Check.Require(WorldText.Replace(TEXT("\r\n"), TEXT("\n")).Equals(ExpectedWorld, ESearchCase::CaseSensitive),
        TEXT("world contract"), TEXT("world schema, projection, byte digest or contract shape differs"));
}

bool Manifest(const FLDDemoContentBundle& Bundle, const FLDDemoContentDigest& ExpectedRoot,
    FLDDemoContentIdentity& Out, FCheck& Check)
{
    if (!Hash(Bundle.Manifest, Out.Root, Check, TEXT("manifest"))
        || !Check.Require(Out.Root == ExpectedRoot, TEXT("manifest"), TEXT("current expected root digest mismatch")))
    {
        return false;
    }
    FValue ManifestValue;
    if (!Parse(Bundle.Manifest, ManifestValue, Check, TEXT("manifest")))
    {
        return false;
    }
    FFields Root(Check, ManifestValue, TEXT("manifest"));
    if (!Root.Literal(TEXT("format"), TEXT("lockdown-demo-content"))
        || !Root.SmallInt(TEXT("schema_version"), Out.SchemaVersion, 2, 2)
        || !Root.SmallInt(TEXT("handler_revision"), Out.HandlerRevision, 3, 3)
        || !Root.SmallInt(TEXT("projection_revision"), Out.ProjectionRevision, 5, 5)
        || !Root.Digest(TEXT("author_schema_sha256"), Out.AuthorSchema)
        || !Root.Digest(TEXT("native_ids_sha256"), Out.NativeIds)
        || !Root.Digest(TEXT("native_contract_sha256"), Out.NativeContract)
        || !Root.Digest(TEXT("display_sha256"), Out.Display))
    {
        return false;
    }
    FFields ContentRef(Check, Root.Take(TEXT("content")), TEXT("manifest.content"));
    FFields WorldRef(Check, Root.Take(TEXT("world")), TEXT("manifest.world"));
    if (!ContentRef.Literal(TEXT("file"), TEXT("demo_content.json")) || !ContentRef.Digest(TEXT("sha256"), Out.Content)
        || !ContentRef.Done() || !WorldRef.Literal(TEXT("file"), TEXT("world.json"))
        || !WorldRef.Digest(TEXT("sha256"), Out.World))
    {
        return false;
    }
    FFields Contract(Check, WorldRef.Take(TEXT("contract")), TEXT("manifest.world.contract"));
    if (!Contract.SmallInt(TEXT("schema_version"), Out.WorldSchemaVersion, 3, 3)
        || !Contract.SmallInt(TEXT("projection_revision"), Out.WorldProjectionRevision, 3, 3)
        || !Contract.Digest(TEXT("sha256"), Out.WorldContract) || !Contract.Done() || !WorldRef.Done() || !Root.Done())
    {
        return false;
    }
    struct FChild { const TCHAR* Name; const TArray<uint8>* Bytes; const FLDDemoContentDigest* Digest; };
    for (const auto& Child : {
        FChild{TEXT("content"), &Bundle.Content, &Out.Content}, {TEXT("world"), &Bundle.World, &Out.World},
        {TEXT("author schema"), &Bundle.AuthorSchema, &Out.AuthorSchema}, {TEXT("native IDs"), &Bundle.NativeIds, &Out.NativeIds},
        {TEXT("native contract"), &Bundle.NativeContract, &Out.NativeContract}, {TEXT("world contract"), &Bundle.WorldContract, &Out.WorldContract}})
    {
        FLDDemoContentDigest Digest;
        if (!Hash(*Child.Bytes, Digest, Check, Child.Name)
            || !Check.Require(Digest == *Child.Digest, Child.Name, TEXT("file bytes differ from root manifest")))
        {
            return false;
        }
    }
    return ContractBytes(Bundle, Out, Check);
}
}

bool Project(const FLDDemoContentBundle& Bundle, const FLDDemoContentDigest& ExpectedRoot,
    FLDDemoContentDefinitions& Out, FString& Error)
{
    Error.Reset();
    FCheck Check(Error);
    FLDDemoContentDefinitions Candidate;
    FValue ContentValue, WorldValue, IdValue;
    if (!Manifest(Bundle, ExpectedRoot, Candidate.Identity, Check)
        || !Parse(Bundle.NativeIds, IdValue, Check, TEXT("native_ids"), false) || !NativeMap(IdValue, Check)
        || !Parse(Bundle.Content, ContentValue, Check, TEXT("content"))
        || !Content(ContentValue, Candidate, Check)
        || !ValidateNumbers(Candidate, Check)
        || !Parse(Bundle.World, WorldValue, Check, TEXT("world"))
        || !ValidateWorld(WorldValue, Candidate, Bundle, Check))
    {
        return false;
    }
    Out = MoveTemp(Candidate);
    return true;
}
}

bool LDDemoCardText::Parse(const FString& Template, TArray<FToken>& Out, FString& Error)
{
    Out.Reset();
    static const TPair<const TCHAR*, EField> Fields[] = {
        {TEXT("amount"), EField::Amount}, {TEXT("duration"), EField::Duration}, {TEXT("ratio"), EField::Ratio},
        {TEXT("count"), EField::Count}, {TEXT("radius"), EField::Radius}, {TEXT("distance"), EField::Distance},
        {TEXT("interval"), EField::Interval}, {TEXT("delay"), EField::Delay}, {TEXT("secondary_ratio"), EField::SecondaryRatio},
        {TEXT("secondary_duration"), EField::SecondaryDuration}, {TEXT("basic_count"), EField::BasicCount},
        {TEXT("refund"), EField::Refund}, {TEXT("burst"), EField::Burst}, {TEXT("limit"), EField::Limit}};
    int32 Index = 0;
    while (Index < Template.Len())
    {
        const int32 Open = Template.Find(TEXT("["), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);
        const int32 Stray = Template.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);
        if (Stray != INDEX_NONE && (Open == INDEX_NONE || Stray < Open))
        {
            Error = TEXT("a closing bracket without its placeholder");
            return false;
        }
        if (Open == INDEX_NONE)
        {
            Out.AddDefaulted_GetRef().Literal = Template.Mid(Index);
            break;
        }
        if (Open > Index)
        {
            Out.AddDefaulted_GetRef().Literal = Template.Mid(Index, Open - Index);
        }
        const int32 Close = Template.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open);
        FString Op, Field;
        if (Close == INDEX_NONE || !Template.Mid(Open + 1, Close - Open - 1).Split(TEXT("."), &Op, &Field)
            || Op.Contains(TEXT("[")) || Field.Contains(TEXT("[")))
        {
            Error = TEXT("a placeholder must read [op.field]");
            return false;
        }
        FToken& Token = Out.AddDefaulted_GetRef();
        Token.bValue = true;
        // #198 stat cards read one bonus of a single point: [per_point.<Stat>].
        Token.bPerPoint = Op.Equals(TEXT("per_point"), ESearchCase::CaseSensitive);
        bool bOp = Token.bPerPoint;
        for (const LDDemoContent::FNativeId& Entry : LDDemoContent::NativeIds)
        {
            if (FCString::Strcmp(Entry.Category, TEXT("effect_ops")) == 0 && Op.Equals(Entry.Id, ESearchCase::CaseSensitive))
            {
                Token.Op = static_cast<ELDDemoEffectOp>(Entry.Number);
                bOp = true;
            }
        }
        bool bField = false;
        for (const TPair<const TCHAR*, EField>& Pair : Fields)
        {
            if (!Token.bPerPoint && Field.Equals(Pair.Key, ESearchCase::CaseSensitive)) { Token.Field = Pair.Value; bField = true; }
        }
        for (const LDDemoContent::FBonusField& Stat : LDDemoContent::BonusFields)
        {
            if (Field.Equals(Stat.Name, ESearchCase::CaseSensitive)) { Token.Field = EField::Stat; Token.Stat = Stat.Stat; bField = true; }
        }
        if (!bOp || !bField)
        {
            Error = FString::Printf(TEXT("unknown placeholder [%s.%s]"), *Op, *Field);
            return false;
        }
        Index = Close + 1;
    }
    return true;
}

const FLDDemoEffectDefinition* LDDemoCardText::FindEffect(const FLDDemoAbilityValues& Values, const FToken& Token)
{
    return FindEffect(Values.Effects, Token);
}

const FLDDemoEffectDefinition* LDDemoCardText::FindEffect(const TArray<FLDDemoEffectDefinition>& Effects, const FToken& Token)
{
    if (!Token.bValue || Token.bPerPoint)
    {
        return nullptr;
    }
    const FLDDemoEffectDefinition* Effect = Effects.FindByPredicate([&Token](const FLDDemoEffectDefinition& Row) { return Row.GetOp() == Token.Op; });
    if (Effect && Token.Field == EField::Stat
        && !Effect->Stats.ContainsByPredicate([&Token](const FLDDemoStatBonus& Stat) { return Stat.Stat == Token.Stat; }))
    {
        return nullptr;
    }
    return Effect;
}

const FLDDemoStatBonus* LDDemoCardText::FindPerPoint(const FLDDemoStatDefinition& Stat, const FToken& Token)
{
    if (!Token.bValue || !Token.bPerPoint || Token.Field != EField::Stat)
    {
        return nullptr;
    }
    return Stat.PerPoint.FindByPredicate([&Token](const FLDDemoStatBonus& Bonus) { return Bonus.Stat == Token.Stat; });
}
