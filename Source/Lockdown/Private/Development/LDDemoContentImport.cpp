#include "LDDemoContentJson.h"

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
    DEMO_ID("abilities", "active_arc_bolt", ELDDemoAbilityId, ActiveArcBolt),
    DEMO_ID("abilities", "active_guard_pulse", ELDDemoAbilityId, ActiveGuardPulse),
    DEMO_ID("abilities", "active_binding_snare", ELDDemoAbilityId, ActiveBindingSnare),
    DEMO_ID("abilities", "passive_battle_rhythm", ELDDemoAbilityId, PassiveBattleRhythm),
    DEMO_ID("abilities", "passive_second_wind", ELDDemoAbilityId, PassiveSecondWind),
    DEMO_ID("abilities", "passive_precision", ELDDemoAbilityId, PassivePrecision),
    DEMO_ID("abilities", "active_slipper_throw", ELDDemoAbilityId, ActiveSlipperThrow),
    DEMO_ID("abilities", "active_fart_dash", ELDDemoAbilityId, ActiveFartDash),
    DEMO_ID("abilities", "active_combo_flurry", ELDDemoAbilityId, ActiveComboFlurry),
    DEMO_ID("abilities", "active_slick_moves", ELDDemoAbilityId, ActiveSlickMoves),
    DEMO_ID("abilities", "passive_cornered", ELDDemoAbilityId, PassiveCornered),
    DEMO_ID("abilities", "passive_lurking", ELDDemoAbilityId, PassiveLurking),
    DEMO_ID("abilities", "passive_public_shame", ELDDemoAbilityId, PassivePublicShame),
    DEMO_ID("abilities", "passive_eagle_eye", ELDDemoAbilityId, PassiveEagleEye),
    DEMO_ID("abilities", "active_monkey_peach", ELDDemoAbilityId, ActiveMonkeyPeach),
    DEMO_ID("abilities", "active_thousand_year", ELDDemoAbilityId, ActiveThousandYear),
    DEMO_ID("abilities", "active_lunge", ELDDemoAbilityId, ActiveLunge),
    DEMO_ID("abilities", "passive_iron_crotch", ELDDemoAbilityId, PassiveIronCrotch),
    DEMO_ID("abilities", "active_body_slam", ELDDemoAbilityId, ActiveBodySlam),
    DEMO_ID("abilities", "active_come_here", ELDDemoAbilityId, ActiveComeHere),
    DEMO_ID("abilities", "active_porcelain_flop", ELDDemoAbilityId, ActivePorcelainFlop),
    DEMO_ID("abilities", "active_expose_post", ELDDemoAbilityId, ActiveExposePost),
    DEMO_ID("abilities", "active_barrage_rant", ELDDemoAbilityId, ActiveBarrageRant),
    DEMO_ID("abilities", "active_cancel_blast", ELDDemoAbilityId, ActiveCancelBlast),
    DEMO_ID("abilities", "active_pie_chart", ELDDemoAbilityId, ActivePieChart),
    DEMO_ID("abilities", "active_table_flip", ELDDemoAbilityId, ActiveTableFlip),
    DEMO_ID("abilities", "passive_dox_master", ELDDemoAbilityId, PassiveDoxMaster),
    DEMO_ID("abilities", "passive_thick_skin", ELDDemoAbilityId, PassiveThickSkin),
    DEMO_ID("abilities", "passive_mental_gymnastics", ELDDemoAbilityId, PassiveMentalGymnastics),
    DEMO_ID("abilities", "passive_keyboard_warrior", ELDDemoAbilityId, PassiveKeyboardWarrior),
    DEMO_ID("attributes", "attr_might", ELDDemoAttributeId, AttrMight),
    DEMO_ID("attributes", "attr_focus", ELDDemoAttributeId, AttrFocus),
    DEMO_ID("attributes", "attr_vitality", ELDDemoAttributeId, AttrVitality),
    DEMO_ID("attributes", "attr_resolve", ELDDemoAttributeId, AttrResolve),
    DEMO_ID("attributes", "attr_reach", ELDDemoAttributeId, AttrReach),
    DEMO_ID("attributes", "attr_might_hp", ELDDemoAttributeId, AttrMightHp),
    DEMO_ID("attributes", "attr_might_haste", ELDDemoAttributeId, AttrMightHaste),
    DEMO_ID("attributes", "attr_focus_as", ELDDemoAttributeId, AttrFocusAs),
    DEMO_ID("attributes", "attr_focus_defense", ELDDemoAttributeId, AttrFocusDefense),
    DEMO_ID("attributes", "attr_reach_ms", ELDDemoAttributeId, AttrReachMs),
    DEMO_ID("attributes", "attr_roam_hybrid", ELDDemoAttributeId, AttrRoamHybrid),
    DEMO_ID("attributes", "attr_wild_glass_cannon", ELDDemoAttributeId, AttrWildGlassCannon),
    DEMO_ID("attributes", "attr_wild_juggernaut", ELDDemoAttributeId, AttrWildJuggernaut),
    DEMO_ID("attributes", "attr_wild_brawler", ELDDemoAttributeId, AttrWildBrawler),
    DEMO_ID("equipment", "item_iron_plate", ELDDemoEquipmentId, ItemIronPlate),
    DEMO_ID("equipment", "item_edge_blade", ELDDemoEquipmentId, ItemEdgeBlade),
    DEMO_ID("equipment", "item_runic_focus", ELDDemoEquipmentId, ItemRunicFocus),
    DEMO_ID("equipment", "item_siphon_emblem", ELDDemoEquipmentId, ItemSiphonEmblem),
    DEMO_ID("equipment", "item_phase_anchor", ELDDemoEquipmentId, ItemPhaseAnchor),
    DEMO_ID("equipment", "item_arcane_battery", ELDDemoEquipmentId, ItemArcaneBattery),
    DEMO_ID("equipment", "item_echo_maul", ELDDemoEquipmentId, ItemEchoMaul),
    DEMO_ID("equipment", "item_slingshot", ELDDemoEquipmentId, ItemSlingshot),
    DEMO_ID("equipment", "item_paper_box", ELDDemoEquipmentId, ItemPaperBox),
    DEMO_ID("equipment", "item_nail_uniform", ELDDemoEquipmentId, ItemNailUniform),
    DEMO_ID("equipment", "item_megaphone", ELDDemoEquipmentId, ItemMegaphone),
    DEMO_ID("equipment", "item_dossier", ELDDemoEquipmentId, ItemDossier),
    DEMO_ID("equipment", "item_privacy_screen", ELDDemoEquipmentId, ItemPrivacyScreen),
    DEMO_ID("equipment", "item_skateboard", ELDDemoEquipmentId, ItemSkateboard),
    DEMO_ID("equipment", "item_decoy_wig", ELDDemoEquipmentId, ItemDecoyWig),
    DEMO_ID("equipment", "item_energy_drink", ELDDemoEquipmentId, ItemEnergyDrink),
    DEMO_ID("monsters", "monster_scavenger", ELDDemoMonsterId, MonsterScavenger),
    DEMO_ID("monsters", "monster_guardian", ELDDemoMonsterId, MonsterGuardian),
    DEMO_ID("monsters", "monster_dean", ELDDemoMonsterId, MonsterDean),
    DEMO_ID("statuses", "root", ELDDemoStatusId, Root),
    DEMO_ID("statuses", "shield", ELDDemoStatusId, Shield),
    DEMO_ID("statuses", "camp_returning", ELDDemoStatusId, CampReturning),
    DEMO_ID("statuses", "respawn_protection", ELDDemoStatusId, RespawnProtection),
    DEMO_ID("statuses", "slow", ELDDemoStatusId, Slow),
    DEMO_ID("statuses", "stat_boost", ELDDemoStatusId, StatBoost),
    DEMO_ID("statuses", "enraged", ELDDemoStatusId, Enraged),
    DEMO_ID("statuses", "ambush_ready", ELDDemoStatusId, AmbushReady),
    DEMO_ID("statuses", "exposed", ELDDemoStatusId, Exposed),
    DEMO_ID("statuses", "taunt", ELDDemoStatusId, Taunt),
    DEMO_ID("statuses", "stealth", ELDDemoStatusId, Stealth),
    DEMO_ID("statuses", "disarm", ELDDemoStatusId, Disarm),
    DEMO_ID("statuses", "stun", ELDDemoStatusId, Stun),
    DEMO_ID("statuses", "empowered", ELDDemoStatusId, Empowered),
    DEMO_ID("statuses", "knockback", ELDDemoStatusId, Knockback),
    DEMO_ID("statuses", "heal_cut", ELDDemoStatusId, HealCut),
    DEMO_ID("statuses", "damage_guard", ELDDemoStatusId, DamageGuard),
    DEMO_ID("statuses", "vulnerable", ELDDemoStatusId, Vulnerable),
    DEMO_ID("effect_ops", "damage", ELDDemoEffectOp, Damage),
    DEMO_ID("effect_ops", "shield", ELDDemoEffectOp, Shield),
    DEMO_ID("effect_ops", "dispel_root", ELDDemoEffectOp, DispelRoot),
    DEMO_ID("effect_ops", "root", ELDDemoEffectOp, Root),
    DEMO_ID("effect_ops", "stat_bonus", ELDDemoEffectOp, StatBonus),
    DEMO_ID("effect_ops", "out_of_combat_regen", ELDDemoEffectOp, OutOfCombatRegen),
    DEMO_ID("effect_ops", "lifesteal", ELDDemoEffectOp, Lifesteal),
    DEMO_ID("effect_ops", "dash", ELDDemoEffectOp, Dash),
    DEMO_ID("effect_ops", "restore_mana", ELDDemoEffectOp, RestoreMana),
    DEMO_ID("effect_ops", "cleave", ELDDemoEffectOp, Cleave),
    DEMO_ID("effect_ops", "dash_boost", ELDDemoEffectOp, DashBoost),
    DEMO_ID("effect_ops", "slow", ELDDemoEffectOp, Slow),
    DEMO_ID("effect_ops", "timed_stat_bonus", ELDDemoEffectOp, TimedStatBonus),
    DEMO_ID("effect_ops", "self_area", ELDDemoEffectOp, SelfArea),
    DEMO_ID("effect_ops", "low_hp_stat_bonus", ELDDemoEffectOp, LowHpStatBonus),
    DEMO_ID("effect_ops", "ambush_damage", ELDDemoEffectOp, AmbushDamage),
    DEMO_ID("effect_ops", "control_damage_amp", ELDDemoEffectOp, ControlDamageAmp),
    DEMO_ID("effect_ops", "taunt", ELDDemoEffectOp, Taunt),
    DEMO_ID("effect_ops", "stealth", ELDDemoEffectOp, Stealth),
    DEMO_ID("effect_ops", "disarm", ELDDemoEffectOp, Disarm),
    DEMO_ID("effect_ops", "stun", ELDDemoEffectOp, Stun),
    DEMO_ID("effect_ops", "empower_basic", ELDDemoEffectOp, EmpowerBasic),
    DEMO_ID("effect_ops", "jump", ELDDemoEffectOp, Jump),
    DEMO_ID("effect_ops", "thorns", ELDDemoEffectOp, Thorns),
    DEMO_ID("effect_ops", "backstab", ELDDemoEffectOp, Backstab),
    DEMO_ID("effect_ops", "cooldown_refund", ELDDemoEffectOp, CooldownRefund),
    DEMO_ID("effect_ops", "ability_crit", ELDDemoEffectOp, AbilityCrit),
    DEMO_ID("effect_ops", "resist_shred", ELDDemoEffectOp, ResistShred),
    DEMO_ID("effect_ops", "momentum", ELDDemoEffectOp, Momentum),
    DEMO_ID("effect_ops", "taunt_retaliate", ELDDemoEffectOp, TauntRetaliate),
    DEMO_ID("effect_ops", "triggered_shield", ELDDemoEffectOp, TriggeredShield),
    DEMO_ID("effect_ops", "basic_stacks", ELDDemoEffectOp, BasicStacks),
    DEMO_ID("effect_ops", "long_reach", ELDDemoEffectOp, LongReach),
    DEMO_ID("effect_ops", "crit_damage_reduction", ELDDemoEffectOp, CritDamageReduction),
    DEMO_ID("effect_ops", "nth_basic_bonus", ELDDemoEffectOp, NthBasicBonus),
    DEMO_ID("effect_ops", "mana_on_hit", ELDDemoEffectOp, ManaOnHit),
    DEMO_ID("effect_ops", "dispel_slow", ELDDemoEffectOp, DispelSlow),
    DEMO_ID("effect_ops", "path_slow", ELDDemoEffectOp, PathSlow),
    DEMO_ID("effect_ops", "knockback", ELDDemoEffectOp, Knockback),
    DEMO_ID("effect_ops", "heal_cut", ELDDemoEffectOp, HealCut),
    DEMO_ID("effect_ops", "dot", ELDDemoEffectOp, Dot),
    DEMO_ID("effect_ops", "mobility_punish", ELDDemoEffectOp, MobilityPunish),
    DEMO_ID("effect_ops", "volley", ELDDemoEffectOp, Volley),
    DEMO_ID("effect_ops", "refresh_debuffs", ELDDemoEffectOp, RefreshDebuffs),
    DEMO_ID("effect_ops", "on_hit", ELDDemoEffectOp, OnHit),
    DEMO_ID("effect_ops", "debuff_amp", ELDDemoEffectOp, DebuffAmp),
    DEMO_ID("effect_ops", "shield_shatter", ELDDemoEffectOp, ShieldShatter),
    DEMO_ID("effect_ops", "debuff_heal", ELDDemoEffectOp, DebuffHeal),
    DEMO_ID("effect_ops", "zone", ELDDemoEffectOp, Zone),
    DEMO_ID("effect_ops", "zone_shield", ELDDemoEffectOp, ZoneShield),
    DEMO_ID("effect_ops", "zone_slow", ELDDemoEffectOp, ZoneSlow),
    DEMO_ID("effect_ops", "zone_root", ELDDemoEffectOp, ZoneRoot),
    DEMO_ID("effect_ops", "zone_damage", ELDDemoEffectOp, ZoneDamage),
    DEMO_ID("effect_ops", "damage_guard", ELDDemoEffectOp, DamageGuard),
    DEMO_ID("effect_ops", "shield_burst", ELDDemoEffectOp, ShieldBurst),
    DEMO_ID("effect_ops", "vulnerable", ELDDemoEffectOp, Vulnerable),
    DEMO_ID("effect_ops", "decoy", ELDDemoEffectOp, Decoy),
    DEMO_ID("effect_ops", "leap_back", ELDDemoEffectOp, LeapBack),
    DEMO_ID("effect_ops", "cooldown_refresh", ELDDemoEffectOp, CooldownRefresh),
    DEMO_ID("effect_ops", "breach_stacks", ELDDemoEffectOp, BreachStacks),
    DEMO_ID("effect_ops", "mobility_shield", ELDDemoEffectOp, MobilityShield),
    DEMO_ID("effect_ops", "shield_pulse", ELDDemoEffectOp, ShieldPulse),
    DEMO_ID("effect_ops", "basic_rider", ELDDemoEffectOp, BasicRider),
    DEMO_ID("effect_ops", "cast_frenzy", ELDDemoEffectOp, CastFrenzy),
    DEMO_ID("effect_ops", "affliction_amp", ELDDemoEffectOp, AfflictionAmp),
    DEMO_ID("effect_ops", "shield_break_retort", ELDDemoEffectOp, ShieldBreakRetort),
    DEMO_ID("effect_ops", "hit_haste", ELDDemoEffectOp, HitHaste),
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
    if (!Root.Int(TEXT("schema_version"), Version, 1, 1))
    {
        return false;
    }
    FFields Retired(Check, Root.Take(TEXT("retired_ranges")), TEXT("native_ids.retired_ranges"));
    for (const auto& Pair : {TPair<const TCHAR*, int64>(TEXT("abilities"), 61), {TEXT("equipment"), 98}})
    {
        const TArray<FValue>* Range;
        int64 First, Last;
        if (!Array(Retired.Take(Pair.Key), Range, Check, Retired.Path, 2, 2)
            || !Integer((*Range)[0], First, Check, Retired.Path, 0, 0)
            || !Integer((*Range)[1], Last, Check, Retired.Path, Pair.Value, Pair.Value))
        {
            return false;
        }
    }
    if (!Retired.Done())
    {
        return false;
    }
    for (const TCHAR* Category : {TEXT("character"), TEXT("abilities"), TEXT("attributes"),
        TEXT("equipment"), TEXT("monsters"), TEXT("statuses"), TEXT("effect_ops")})
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
        {TEXT("STAT.AbilityCritMultiplier"), TEXT("ratio"), TEXT("bp"), &P.AbilityCritMultiplierBp, 10000, MAX_int64},
        {TEXT("STAT.CooldownHaste.Base"), TEXT("points"), TEXT("p2"), &P.CooldownHasteBaseP2, 0, MAX_int64},
        {TEXT("STAT.CooldownHaste.Max"), TEXT("points"), TEXT("p2"), &P.CooldownHasteMaxP2, 0, MAX_int64},
        {TEXT("UNIT.BasicAttackWindupSec"), TEXT("ms"), TEXT("ms"), &P.BasicAttackWindupMs, 1, MAX_int64},
        {TEXT("CAPTURE.SpraySec"), TEXT("ms"), TEXT("ms"), &P.CaptureSprayMs, 1, MAX_int64},
        {TEXT("CAPTURE.GuardSec"), TEXT("ms"), TEXT("ms"), &P.CaptureGuardMs, 1, MAX_int64},
        {TEXT("TIMING.DefendRouteBudgetSec"), TEXT("ms"), TEXT("ms"), &P.DefendRouteBudgetMs, 1, MAX_int64},
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
        {TEXT("AIRDROP.FirstLandSec"), TEXT("ms"), TEXT("ms"), &P.AirdropFirstLandMs, 1, MAX_int64},
        {TEXT("AIRDROP.IntervalSec"), TEXT("ms"), TEXT("ms"), &P.AirdropIntervalMs, 1, MAX_int64},
        {TEXT("AIRDROP.AnnounceLeadSec"), TEXT("ms"), TEXT("ms"), &P.AirdropAnnounceLeadMs, 1, MAX_int64},
        {TEXT("LEADER.RefreshSec"), TEXT("ms"), TEXT("ms"), &P.LeaderRefreshMs, 1, MAX_int64},
        {TEXT("SHOP.SellRatio"), TEXT("ratio"), TEXT("bp"), &P.SellRatioBp, 0, 10000},
        {TEXT("REWARD.AssistWindowSec"), TEXT("ms"), TEXT("ms"), &P.AssistWindowMs, 1, MAX_int64},
        {TEXT("REWARD.AssistGoldRatio"), TEXT("ratio"), TEXT("bp"), &P.AssistGoldBp, 0, 10000},
        {TEXT("SHOP.UpgradeStatBonusRatio"), TEXT("ratio"), TEXT("bp"), &P.UpgradeStatBonusBp, 0, 10000},
        {TEXT("CHOICE.UpgradeWeight"), TEXT("ratio"), TEXT("bp"), &P.Choice.UpgradeWeightBp, 1, 1000000},
        {TEXT("CHOICE.NewActiveOpenSlotWeight"), TEXT("ratio"), TEXT("bp"), &P.Choice.NewActiveOpenSlotWeightBp, 1, 1000000},
        {TEXT("CHOICE.NewActiveFullSlotWeight"), TEXT("ratio"), TEXT("bp"), &P.Choice.NewActiveFullSlotWeightBp, 1, 1000000},
        {TEXT("CHOICE.NewPassiveWeight"), TEXT("ratio"), TEXT("bp"), &P.Choice.NewPassiveWeightBp, 1, 1000000},
        {TEXT("CHOICE.AffinityStep"), TEXT("ratio"), TEXT("bp"), &P.Choice.AffinityStepBp, 10000, 1000000},
        {TEXT("CHOICE.AffinityCap"), TEXT("ratio"), TEXT("bp"), &P.Choice.AffinityCapBp, 10000, 1000000},
        {TEXT("CHOICE.ThirdSlotAttributeChance"), TEXT("ratio"), TEXT("bp"), &P.Choice.ThirdSlotAttributeBp, 0, 10000},
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
        {TEXT("VISION.RadiusCm"), &P.VisionRadiusCm}, {TEXT("SHOP.InteractRadiusCm"), &P.ShopInteractRadiusCm},
        {TEXT("CAPTURE.RadiusCm"), &P.CaptureRadiusCm}, {TEXT("CAMP.ResetArrivalRadiusCm"), &P.CampResetArrivalRadiusCm},
        {TEXT("DORM.RadiusCm"), &P.DormRadiusCm}, {TEXT("AIRDROP.PickupRadiusCm"), &P.AirdropPickupRadiusCm},
        {TEXT("LEADER.CircleRadiusCm"), &P.LeaderCircleRadiusCm}, {TEXT("LEADER.CircleMaxOffsetCm"), &P.LeaderCircleMaxOffsetCm}})
    {
        if (!F.Fraction(Field.Key, TEXT("cm"), *Field.Out, true))
        {
            return false;
        }
    }
    if (!F.Fraction(TEXT("COMBAT.RearArcDeg"), TEXT("deg"), P.RearArcDeg, true)
        || !Check.Require(P.RearArcDeg.Numerator > 0
            && (P.RearArcDeg.Denominator > MAX_int64 / 360 || P.RearArcDeg.Numerator < 360 * P.RearArcDeg.Denominator),
            F.Path, TEXT("the rear arc must be a proper, non-empty sector")))
    {
        return false;
    }
    struct FCountField { const TCHAR* Key; int64* Out; int64 Min; };
    for (const auto& Field : {
        FCountField{TEXT("SHOP.StartingGold"), &P.StartingGold, 0}, {TEXT("SHOP.UpgradeCost"), &P.UpgradeCost, 1},
        {TEXT("XP.BaseToNext"), &P.XPBaseToNext, 1}, {TEXT("XP.IncreasePerLevel"), &P.XPIncreasePerLevel, 0},
        {TEXT("REWARD.PlayerXP.Base"), &P.PlayerXPBase, 0}, {TEXT("REWARD.PlayerXP.PerLevel"), &P.PlayerXPPerLevel, 0},
        {TEXT("REWARD.PlayerGold.Base"), &P.PlayerGoldBase, 0}, {TEXT("REWARD.PlayerGold.PerLevel"), &P.PlayerGoldPerLevel, 0},
        {TEXT("AI.RetryPolicy.MaxAttempts"), &P.AI.RetryMaxAttempts, 1},
        {TEXT("CAMP.ReturnDeadEndPlans"), &P.CampReturnDeadEndPlans, 1},
        {TEXT("AIRDROP.RewardXP"), &P.AirdropRewardXP, 1}, {TEXT("AIRDROP.RewardGold"), &P.AirdropRewardGold, 1},
        {TEXT("LEADER.MinLevel"), &P.LeaderMinLevel, 1}, {TEXT("LEADER.BountyGoldPerRefresh"), &P.LeaderBountyGoldPerRefresh, 1},
        {TEXT("LEADER.BountyGoldMax"), &P.LeaderBountyGoldMax, 1}})
    {
        if (!F.Int(Field.Key, *Field.Out, Field.Min))
        {
            return false;
        }
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
        DEMO_FORMULA(SellGold, FloorPurchasePriceTimesSellRatio), DEMO_FORMULA(XPToNext, LinearIncrement),
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
        DEMO_POLICY(StatStacking, AddFlatThenAdditivePercent), DEMO_POLICY(DuplicateEquipment, Allowed),
        DEMO_POLICY(DuplicateEquipmentPassive, OncePerFamily), DEMO_POLICY(DuplicateEquipmentActive, IndependentSources),
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
    if (!F.Flag(TEXT("ShopRequiresLineOfSight"), Out.ShopRequiresLineOfSight)
        || !F.Flag(TEXT("CampGroupRetaliation"), Out.CampGroupRetaliation)
        || !F.Flag(TEXT("CampReturningDamageable"), Out.CampReturningDamageable)
        || !F.Flag(TEXT("CampReturningControllable"), Out.CampReturningControllable)
        || !Check.Require(!Out.ShopRequiresLineOfSight && Out.CampGroupRetaliation
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
constexpr uint32 TemporaryStats = ~(Bit(ELDDemoBonusStat::MaxHPFlat) | Bit(ELDDemoBonusStat::MaxMPFlat) | Bit(ELDDemoBonusStat::HPRegenFlat));

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
    static const TCHAR* Names[] = {TEXT("None"), TEXT("Release"), TEXT("EmpowerHit"), TEXT("BasicHit"), TEXT("BasicCrit"),
        TEXT("Cleanse"), TEXT("LandingHit"), TEXT("LowHP"), TEXT("AbilityHit"), TEXT("Control")};
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

bool Effect(const FValue& Value, FLDDemoEffectDefinition& Out, FCheck& Check, const FString& Path)
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
            || !OptionalFixed(F, TEXT("max_hp_ratio"), TEXT("ratio"), TEXT("bp"), Out.MaxHPRatioBp, 1, 10000)
            || !OptionalFixed(F, TEXT("cleanse_multiplier"), TEXT("ratio"), TEXT("bp"), Out.MultiplierBp, 10001))
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
    case ELDDemoEffectOp::Root:
        if (!F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::StatBonus:
        if (!Stats(F.Take(TEXT("stats")), Out.Stats, Check, Path + TEXT(".stats")))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::OutOfCombatRegen:
        // max_hp_ratio: share of the current max HP per second once delay has passed without combat;
        // surge: regeneration multiplier for surge_duration after an ability hit; in_combat_ratio: share kept in combat.
        if (!F.Fixed(TEXT("delay"), TEXT("ms"), TEXT("ms"), Out.DelayMs, 1)
            || !F.Fixed(TEXT("max_hp_ratio"), TEXT("ratio"), TEXT("bp"), Out.MaxHPPerSecBp, 10, 10000)
            || !Check.Require(Out.MaxHPPerSecBp % 10 == 0, Path, TEXT("the share must be a whole 0.001 of the maximum per second"))
            || !OptionalFixed(F, TEXT("surge_duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !OptionalFixed(F, TEXT("surge_multiplier"), TEXT("ratio"), TEXT("bp"), Out.MultiplierBp, 10001)
            || !Check.Require((Out.DurationMs > 0) == (Out.MultiplierBp > 0), Path, TEXT("a regen surge needs its duration and multiplier"))
            || !OptionalFixed(F, TEXT("in_combat_ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 10000))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::Lifesteal:
    case ELDDemoEffectOp::RestoreMana:
    case ELDDemoEffectOp::Cleave:
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 10000))
        {
            return false;
        }
        if (Out.GetOp() == ELDDemoEffectOp::Cleave && !F.Fraction(TEXT("radius"), TEXT("cm"), Out.RadiusCm, true))
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
    case ELDDemoEffectOp::DashBoost:
        // skill_*: the boost of every dash or jump active (character and gear), fixed at its release.
        if (!F.Fixed(TEXT("cooldown_ratio"), TEXT("ratio"), TEXT("bp"), Out.CooldownRatioBp, 1, 10000)
            || !F.Fixed(TEXT("distance_bonus"), TEXT("ratio"), TEXT("bp"), Out.DistanceBonusBp)
            || !Check.Require(Out.CooldownRatioBp < 10000 || Out.DistanceBonusBp > 0, Path, TEXT("ineffective dash boost"))
            || !OptionalFixed(F, TEXT("skill_cooldown_ratio"), TEXT("ratio"), TEXT("bp"), Out.SkillCooldownRatioBp, 1, 10000)
            || !OptionalFixed(F, TEXT("skill_distance_bonus"), TEXT("ratio"), TEXT("bp"), Out.SkillDistanceBonusBp, 1))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::Slow:
        // A slow always leaves some move speed; the projection clamps the rest to STAT.MoveSpeed.Min.
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !OptionalFixed(F, TEXT("vamp"), TEXT("ratio"), TEXT("bp"), Out.VampRatioBp, 1, 10000))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::PathSlow:
        // Every body whose foot comes within half_width of the travelled segment is slowed when the dash ends.
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !F.Fraction(TEXT("half_width"), TEXT("cm"), Out.RadiusCm, true))
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
            || !OptionalFixed(F, TEXT("omnivamp"), TEXT("ratio"), TEXT("bp"), Out.VampRatioBp, 1, 10000))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::AmbushDamage:
        // dash_credit: readiness gained per dash or jump; shred_*: the first strike's resistance shred (ratio of both).
        if (!F.Fixed(TEXT("delay"), TEXT("ms"), TEXT("ms"), Out.DelayMs, 1) || !DamageTerms(F, Out, Check, Path)
            || !OptionalFixed(F, TEXT("dash_credit"), TEXT("ms"), TEXT("ms"), Out.AmountMs, 1)
            || !OptionalFixed(F, TEXT("shred_ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 10000)
            || !OptionalFixed(F, TEXT("shred_duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !Check.Require((Out.RatioBp > 0) == (Out.DurationMs > 0), Path, TEXT("a shred needs its ratio and duration")))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::ControlDamageAmp:
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1))
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
    case ELDDemoEffectOp::Stealth:
        // RadiusCm is the reveal distance; RatioBp is the movement-speed penalty while hidden (below 100%).
        if (!F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !F.Fraction(TEXT("reveal_radius"), TEXT("cm"), Out.RadiusCm, true)
            || !F.Fixed(TEXT("move_speed_penalty"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 0, 9999)
            || (F.Has(TEXT("ambush_ready")) && (!F.Flag(TEXT("ambush_ready"), Out.bAmbushReady)
                || !Check.Require(Out.bAmbushReady, Path, TEXT("an optional flag is present only when set")))))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::Disarm:
    case ELDDemoEffectOp::Stun:
    case ELDDemoEffectOp::Jump:
        if (!F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::Backstab:
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 10000)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || (F.Has(TEXT("impaired")) && (!F.Flag(TEXT("impaired"), Out.bImpaired)
                || !Check.Require(Out.bImpaired, Path, TEXT("an optional flag is present only when set")))))
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
            break;
        }
        FString Trigger;
        if (!F.Fixed(TEXT("armor_ratio"), TEXT("ratio"), TEXT("bp"), Out.ArmorRatioBp)
            || !OptionalFixed(F, TEXT("max_hp_ratio"), TEXT("ratio"), TEXT("bp"), Out.MaxHPRatioBp, 1, 10000)
            || !F.Fraction(TEXT("radius"), TEXT("cm"), Out.RadiusCm, true)
            || !F.Fixed(TEXT("interval"), TEXT("ms"), TEXT("ms"), Out.IntervalMs, 1)
            || !F.Fixed(TEXT("critical_multiplier"), TEXT("ratio"), TEXT("bp"), Out.CriticalMultiplierBp, 10000)
            || !F.Text(TEXT("trigger"), Trigger)
            || !Check.Require(Trigger.Equals(TEXT("Basic"), ESearchCase::CaseSensitive)
                || Trigger.Equals(TEXT("BasicOrAbility"), ESearchCase::CaseSensitive), Path, TEXT("unknown reflection trigger"))
            || !F.Flag(TEXT("rear_exempt"), Out.bRearExempt)
            || !Check.Require(Out.BaseP2 > 0 || Out.ArmorRatioBp > 0 || Out.MaxHPRatioBp > 0, Path, TEXT("ineffective effect"))
            // cripple: the reflection also cuts the attacker's attack and move speed by this ratio for this long.
            || !OptionalFixed(F, TEXT("cripple_ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999)
            || !OptionalFixed(F, TEXT("cripple_duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !Check.Require((Out.RatioBp > 0) == (Out.DurationMs > 0), Path, TEXT("a cripple needs its ratio and duration")))
        {
            return false;
        }
        Out.bAbilityTrigger = Trigger.Equals(TEXT("BasicOrAbility"), ESearchCase::CaseSensitive);
        break;
    }
    // #112 handlers (LDCombatMechanics). Field use: see FLDDemoEffectDefinition and docs/systems/COMBAT_AND_GROWTH.md.
    case ELDDemoEffectOp::Knockback:
    case ELDDemoEffectOp::LeapBack:
        if (!F.Fraction(TEXT("distance"), TEXT("cm"), Out.DistanceCm, true)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::HealCut:
    case ELDDemoEffectOp::HitHaste:
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, Out.GetOp() == ELDDemoEffectOp::HealCut ? 9999 : 10000)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::Dot:
        if (!DamageTerms(F, Out, Check, Path) || !F.Fixed(TEXT("interval"), TEXT("ms"), TEXT("ms"), Out.IntervalMs, 1)
            || !IntInto(F, TEXT("count"), Out.Count, 1, 10) || !F.Fixed(TEXT("shred"), TEXT("points"), TEXT("p2"), Out.ShredP2))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::MobilityPunish:
    case ELDDemoEffectOp::ZoneDamage:
        if (!DamageTerms(F, Out, Check, Path))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::Volley:
        if (!IntInto(F, TEXT("count"), Out.Count, 2, 5) || !F.Fixed(TEXT("interval"), TEXT("ms"), TEXT("ms"), Out.IntervalMs, 1))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::RefreshDebuffs:
    case ELDDemoEffectOp::CooldownRefresh:
        break;
    case ELDDemoEffectOp::OnHit:
        if (!F.Fixed(TEXT("scale"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 10000))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::DebuffAmp:
    case ELDDemoEffectOp::Vulnerable:
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::ShieldShatter:
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 10000))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::DebuffHeal:
        if (!F.Fixed(TEXT("max_hp_ratio"), TEXT("ratio"), TEXT("bp"), Out.MaxHPRatioBp, 1, 10000))
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
    case ELDDemoEffectOp::ZoneShield:
        if (!F.Fixed(TEXT("base"), TEXT("hp"), TEXT("p2"), Out.BaseP2) || !F.Fixed(TEXT("ap_ratio"), TEXT("ratio"), TEXT("bp"), Out.APRatioBp)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !Check.Require(Out.BaseP2 > 0 || Out.APRatioBp > 0, Path, TEXT("ineffective effect")))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::ZoneSlow:
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999)
            || !F.Fixed(TEXT("interval"), TEXT("ms"), TEXT("ms"), Out.IntervalMs, 1)
            || !F.Fixed(TEXT("linger"), TEXT("ms"), TEXT("ms"), Out.SecondaryDurationMs))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::ZoneRoot:
        if (!F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::DamageGuard:
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !F.Fixed(TEXT("trigger_damage"), TEXT("points"), TEXT("p2"), Out.ThresholdP2))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::ShieldBurst:
    {
        FString Type;
        if (!F.Fixed(TEXT("base"), TEXT("points"), TEXT("p2"), Out.BaseP2)
            || !F.Fixed(TEXT("shield_ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp)
            || !F.Fixed(TEXT("cap"), TEXT("points"), TEXT("p2"), Out.CapP2)
            || !F.Text(TEXT("damage_type"), Type)
            || !Check.Require(Type.Equals(TEXT("Physical"), ESearchCase::CaseSensitive)
                || Type.Equals(TEXT("Magical"), ESearchCase::CaseSensitive), Path, TEXT("unknown damage type"))
            || !Check.Require(Out.BaseP2 > 0 || (Out.RatioBp > 0 && Out.CapP2 > 0), Path, TEXT("ineffective effect")))
        {
            return false;
        }
        Out.DamageType = Type.Equals(TEXT("Physical"), ESearchCase::CaseSensitive) ? ELDDemoDamageType::Physical : ELDDemoDamageType::Magical;
        break;
    }
    case ELDDemoEffectOp::Decoy:
        if (!F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !F.Fraction(TEXT("radius"), TEXT("cm"), Out.RadiusCm, true)
            || !F.Fixed(TEXT("taunt_duration"), TEXT("ms"), TEXT("ms"), Out.SecondaryDurationMs, 1))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::CooldownRefund:
    {
        FString Entries;
        if (!TriggerField(F, Out.Trigger, {ELDDemoEffectTrigger::Release, ELDDemoEffectTrigger::EmpowerHit,
                ELDDemoEffectTrigger::BasicHit, ELDDemoEffectTrigger::BasicCrit, ELDDemoEffectTrigger::Cleanse})
            || !F.Text(TEXT("entries"), Entries))
        {
            return false;
        }
        Out.Entries = Entries.Equals(TEXT("Basic"), ESearchCase::CaseSensitive) ? ELDDemoCooldownEntries::Basic
            : Entries.Equals(TEXT("Self"), ESearchCase::CaseSensitive) ? ELDDemoCooldownEntries::Self
            : Entries.Equals(TEXT("OtherActives"), ESearchCase::CaseSensitive) ? ELDDemoCooldownEntries::OtherActives
            : Entries.Equals(TEXT("Actives"), ESearchCase::CaseSensitive) ? ELDDemoCooldownEntries::Actives
            : ELDDemoCooldownEntries::None;
        // Exactly one magnitude: an absolute cut (cooldown) or a share of the duration issued at this release (ratio).
        if (!Check.Require(Out.Entries != ELDDemoCooldownEntries::None, Path, TEXT("unknown cooldown entries"))
            || !Check.Require(F.Has(TEXT("cooldown")) != F.Has(TEXT("ratio")), Path, TEXT("one refund magnitude is required"))
            || !OptionalFixed(F, TEXT("cooldown"), TEXT("ms"), TEXT("ms"), Out.AmountMs, 1)
            || !OptionalFixed(F, TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 10000)
            || !Check.Require((Out.Trigger == ELDDemoEffectTrigger::BasicHit) == F.Has(TEXT("duration")), Path,
                TEXT("a basic-hit refund lasts a window; no other trigger has one"))
            || !OptionalFixed(F, TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1))
        {
            return false;
        }
        break;
    }
    case ELDDemoEffectOp::AbilityCrit:
    {
        FString Mode;
        if (!F.Text(TEXT("mode"), Mode))
        {
            return false;
        }
        if (Mode.Equals(TEXT("Impaired"), ESearchCase::CaseSensitive))
        {
            Out.CritMode = ELDDemoCritMode::Impaired;
            break;
        }
        const TArray<FValue>* Ids = nullptr;
        if (!Check.Require(Mode.Equals(TEXT("Chance"), ESearchCase::CaseSensitive), Path, TEXT("unknown crit mode"))
            || !F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 10000)
            || !Array(F.Take(TEXT("abilities")), Ids, Check, Path + TEXT(".abilities"), 1, 8))
        {
            return false;
        }
        Out.CritMode = ELDDemoCritMode::Chance;
        for (const FValue& IdValue : *Ids)
        {
            FString Id;
            if (!String(IdValue, Id, Check, Path + TEXT(".abilities"), 1, 63)
                || !Check.Require(Native(TEXT("abilities"), Id) != nullptr && Id.StartsWith(TEXT("active_")), Path,
                    TEXT("an ability crit names approved actives"))
                || !Check.Require(!Out.AbilityIds.Contains(Id), Path, TEXT("duplicate ability")))
            {
                return false;
            }
            Out.AbilityIds.Add(MoveTemp(Id));
        }
        break;
    }
    case ELDDemoEffectOp::ResistShred:
        if (!F.Fixed(TEXT("armor_ratio"), TEXT("ratio"), TEXT("bp"), Out.ArmorRatioBp, 0, 10000)
            || !F.Fixed(TEXT("magic_resist_ratio"), TEXT("ratio"), TEXT("bp"), Out.MagicResistRatioBp, 0, 10000)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !Check.Require(Out.ArmorRatioBp > 0 || Out.MagicResistRatioBp > 0, Path, TEXT("ineffective shred")))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::Momentum:
        // base: physical damage per 100 cm moved within duration after the release (up to cap), joined to the primary
        // hit of the next basic landed within window after the gathering ends.
        if (!F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !F.Fixed(TEXT("window"), TEXT("ms"), TEXT("ms"), Out.DelayMs, 1)
            || !F.Fixed(TEXT("base"), TEXT("points"), TEXT("p2"), Out.BaseP2, 1)
            || !F.Fixed(TEXT("cap"), TEXT("points"), TEXT("p2"), Out.CapP2, 1))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::BreachStacks:
        if (!DamageTerms(F, Out, Check, Path) || !F.Fixed(TEXT("interval"), TEXT("ms"), TEXT("ms"), Out.IntervalMs, 1)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !IntInto(F, TEXT("max_stacks"), Out.Count, 1, 10) || !F.Fixed(TEXT("shred"), TEXT("points"), TEXT("p2"), Out.ShredP2)
            || !F.Fixed(TEXT("heal_cut"), TEXT("ratio"), TEXT("bp"), Out.SecondaryRatioBp, 0, 9999))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::MobilityShield:
        if (!F.Fixed(TEXT("base"), TEXT("hp"), TEXT("p2"), Out.BaseP2, 1)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !F.Fixed(TEXT("internal_cooldown"), TEXT("ms"), TEXT("ms"), Out.IntervalMs, 1)
            || !F.Fixed(TEXT("bonus_ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 0, 10000)
            || !F.Fixed(TEXT("bonus_window"), TEXT("ms"), TEXT("ms"), Out.SecondaryDurationMs)
            || !Check.Require((Out.RatioBp > 0) == (Out.SecondaryDurationMs > 0), Path, TEXT("a shield bonus needs both ratio and window")))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::ShieldPulse:
        if (!DamageTerms(F, Out, Check, Path) || !F.Fraction(TEXT("radius"), TEXT("cm"), Out.RadiusCm, true)
            || !F.Fixed(TEXT("cooldown_cut"), TEXT("ms"), TEXT("ms"), Out.CooldownCutMs, 1)
            || !F.Fixed(TEXT("internal_cooldown"), TEXT("ms"), TEXT("ms"), Out.IntervalMs, 1)
            || !F.Fixed(TEXT("slow"), TEXT("ratio"), TEXT("bp"), Out.SecondaryRatioBp, 0, 9999)
            || !F.Fixed(TEXT("slow_duration"), TEXT("ms"), TEXT("ms"), Out.SecondaryDurationMs)
            || !Check.Require((Out.SecondaryRatioBp > 0) == (Out.SecondaryDurationMs > 0), Path, TEXT("a pulse slow needs ratio and duration")))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::BasicRider:
    {
        FString Type;
        if (!F.Fixed(TEXT("base"), TEXT("points"), TEXT("p2"), Out.BaseP2) || !F.Fixed(TEXT("ap_ratio"), TEXT("ratio"), TEXT("bp"), Out.APRatioBp)
            || !F.Text(TEXT("damage_type"), Type)
            || !Check.Require(Type.Equals(TEXT("Physical"), ESearchCase::CaseSensitive)
                || Type.Equals(TEXT("Magical"), ESearchCase::CaseSensitive), Path, TEXT("unknown damage type"))
            || !Check.Require(Out.BaseP2 > 0 || Out.APRatioBp > 0, Path, TEXT("ineffective effect")))
        {
            return false;
        }
        Out.DamageType = Type.Equals(TEXT("Physical"), ESearchCase::CaseSensitive) ? ELDDemoDamageType::Physical : ELDDemoDamageType::Magical;
        break;
    }
    case ELDDemoEffectOp::CastFrenzy:
        if (!Stats(F.Take(TEXT("stats")), Out.Stats, Check, Path + TEXT(".stats"))
            || !Check.Require((StatMask(Out.Stats) & ~TemporaryStats) == 0, Path, TEXT("timed bonuses cannot change resource maxima"))
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::TauntRetaliate:
        // base + per_hit x (counted attacks, at most max_hits) + armor_ratio x the caster's armor, around it when the
        // taunt ends.
    {
        int64 MaxHits = 0;
        FString Type;
        if (!F.Fraction(TEXT("radius"), TEXT("cm"), Out.RadiusCm, true)
            || !F.Fixed(TEXT("base"), TEXT("points"), TEXT("p2"), Out.BaseP2)
            || !F.Fixed(TEXT("per_hit"), TEXT("points"), TEXT("p2"), Out.PerCountP2, 1)
            || !F.Fixed(TEXT("armor_ratio"), TEXT("ratio"), TEXT("bp"), Out.ArmorRatioBp)
            || !F.Int(TEXT("max_hits"), MaxHits, 1, 20)
            || !F.Text(TEXT("damage_type"), Type)
            || !Check.Require(Type.Equals(TEXT("Physical"), ESearchCase::CaseSensitive)
                || Type.Equals(TEXT("Magical"), ESearchCase::CaseSensitive), Path, TEXT("unknown damage type")))
        {
            return false;
        }
        Out.Count = int32(MaxHits);
        Out.DamageType = Type.Equals(TEXT("Physical"), ESearchCase::CaseSensitive) ? ELDDemoDamageType::Physical : ELDDemoDamageType::Magical;
        break;
    }
    case ELDDemoEffectOp::TriggeredShield:
        if (!TriggerField(F, Out.Trigger, {ELDDemoEffectTrigger::LandingHit, ELDDemoEffectTrigger::LowHP})
            || !F.Fixed(TEXT("base"), TEXT("hp"), TEXT("p2"), Out.BaseP2)
            || !F.Fixed(TEXT("max_hp_ratio"), TEXT("ratio"), TEXT("bp"), Out.MaxHPRatioBp, 0, 10000)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !Check.Require(Out.BaseP2 > 0 || Out.MaxHPRatioBp > 0, Path, TEXT("ineffective shield")))
        {
            return false;
        }
        if (Out.Trigger == ELDDemoEffectTrigger::LowHP
            && (!F.Fixed(TEXT("threshold"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999)
                || !F.Fixed(TEXT("interval"), TEXT("ms"), TEXT("ms"), Out.IntervalMs, 1)))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::BasicStacks:
    {
        int64 MaxStacks = 0;
        if (!Stats(F.Take(TEXT("stats")), Out.Stats, Check, Path + TEXT(".stats"))
            || !Check.Require((StatMask(Out.Stats) & ~TemporaryStats) == 0, Path, TEXT("stacks cannot change resource maxima"))
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !F.Int(TEXT("max_stacks"), MaxStacks, 1, 10)
            || !OptionalFixed(F, TEXT("windup_reduction"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999))
        {
            return false;
        }
        Out.Count = int32(MaxStacks);
        break;
    }
    case ELDDemoEffectOp::LongReach:
        // ratio: damage amplification per 100 cm between the feet, up to cap; cast_range: added Unit/projectile range.
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 10000)
            || !F.Fixed(TEXT("cap"), TEXT("ratio"), TEXT("bp"), Out.CapBp, 1, 10000)
            || (F.Has(TEXT("cast_range")) && !F.Fraction(TEXT("cast_range"), TEXT("cm"), Out.DistanceCm, true)))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::AfflictionAmp:
        if (!F.Fixed(TEXT("dot_ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1)
            || !F.Fixed(TEXT("slow_scale"), TEXT("ratio"), TEXT("bp"), Out.SecondaryRatioBp, 10000))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::CritDamageReduction:
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999))
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
    {
        int64 Every = 0;
        FString Type;
        if (!F.Int(TEXT("count"), Every, 2, 10) || !F.Fixed(TEXT("base"), TEXT("points"), TEXT("p2"), Out.BaseP2, 1)
            || !F.Text(TEXT("damage_type"), Type)
            || !Check.Require(Type.Equals(TEXT("Physical"), ESearchCase::CaseSensitive)
                || Type.Equals(TEXT("Magical"), ESearchCase::CaseSensitive), Path, TEXT("unknown damage type")))
        {
            return false;
        }
        Out.Count = int32(Every);
        Out.DamageType = Type.Equals(TEXT("Physical"), ESearchCase::CaseSensitive) ? ELDDemoDamageType::Physical : ELDDemoDamageType::Magical;
        break;
    }
    case ELDDemoEffectOp::ManaOnHit:
        if (!TriggerField(F, Out.Trigger, {ELDDemoEffectTrigger::AbilityHit, ELDDemoEffectTrigger::Control})
            || !F.Fixed(TEXT("mana"), TEXT("mp"), TEXT("p2"), Out.BaseP2, 1))
        {
            return false;
        }
        break;
    default:
        return Check.Require(false, Path, TEXT("unimplemented effect projection"));
    }
    return F.Done();
}

bool Effects(const FValue& Value, TArray<FLDDemoEffectDefinition>& Out, FOpMask Expected,
    FCheck& Check, const FString& Path)
{
    const TArray<FValue>* Rows;
    if (!Array(Value, Rows, Check, Path, 1, 8))
    {
        return false;
    }
    FOpMask Mask;
    for (int32 I = 0; I < Rows->Num(); ++I)
    {
        FLDDemoEffectDefinition Definition;
        if (!Effect((*Rows)[I], Definition, Check, FString::Printf(TEXT("%s[%d]"), *Path, I)))
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
    return Check.Require(Mask == Expected, Path, TEXT("missing or extra effect; definition/target projection mismatch"));
}

bool Rank(const FValue& Value, FLDDemoRankDefinition& Out, int32 ExpectedRank, bool Active,
    FOpMask ExpectedOps, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    if (!F.SmallInt(TEXT("rank"), Out.Rank, ExpectedRank, ExpectedRank))
    {
        return false;
    }
    if (Active && (!F.Fixed(TEXT("mana_cost"), TEXT("mp"), TEXT("p2"), Out.ManaCostP2)
        || !F.Fixed(TEXT("cooldown"), TEXT("ms"), TEXT("ms"), Out.CooldownMs, 1)))
    {
        return false;
    }
    return Effects(F.Take(TEXT("effects")), Out.Effects, ExpectedOps, Check, Path + TEXT(".effects")) && F.Done();
}

// 112 §1: one to three distinct mechanic tags, decoded by an explicit whitelist.
bool Tags(const FValue& Value, TArray<ELDDemoMechanicTag>& Out, FCheck& Check, const FString& Path)
{
    static const TCHAR* const Names[] = {TEXT("BasicAttack"), TEXT("Mobility"), TEXT("Control"), TEXT("Shield"), TEXT("Tank"), TEXT("Breach")};
    const TArray<FValue>* Rows;
    if (!Array(Value, Rows, Check, Path, 1, 3))
    {
        return false;
    }
    for (const auto& Row : *Rows)
    {
        FString Name;
        if (!String(Row, Name, Check, Path, 1, 16))
        {
            return false;
        }
        int32 Index = INDEX_NONE;
        for (int32 I = 0; I < UE_ARRAY_COUNT(Names); ++I)
        {
            if (Name.Equals(Names[I], ESearchCase::CaseSensitive)) { Index = I; }
        }
        const ELDDemoMechanicTag Tag = static_cast<ELDDemoMechanicTag>(Index == INDEX_NONE ? 0 : Index);
        if (!Check.Require(Index != INDEX_NONE && !Out.Contains(Tag), Path, TEXT("unknown or repeated mechanic tag")))
        {
            return false;
        }
        Out.Add(Tag);
    }
    return true;
}

bool Ability(const FValue& Value, FLDDemoAbilityDefinition& Out, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    FString Kind;
    if (!NativeIdentity(F, TEXT("abilities"), Out.Id, Out.NativeId) || !F.Text(TEXT("kind"), Kind))
    {
        return false;
    }
    ELDDemoTargetMode Mode = ELDDemoTargetMode::Self;
    // Rank3Ops: the Lv3 qualitative change, required on rank 3 only; Rank3Drops: handlers rank 3 replaces.
    FOpMask ExpectedOps, Rank3Ops, Rank3Drops;
    uint32 ExpectedStats = 0, TimedStats = 0;
    bool Active = false;
    using EOp = ELDDemoEffectOp;
    switch (Out.GetNativeId())
    {
    case ELDDemoAbilityId::ActiveArcBolt:
        Active = true; Mode = ELDDemoTargetMode::DirectionProjectile; ExpectedOps = OpBit(EOp::Damage);
        Rank3Ops = OpBit(EOp::ResistShred); break;
    case ELDDemoAbilityId::ActiveGuardPulse:
        Active = true; ExpectedOps = OpBit(EOp::Shield) | OpBit(EOp::DispelRoot) | OpBit(EOp::DispelSlow);
        Rank3Ops = OpBit(EOp::CooldownRefund); break;
    case ELDDemoAbilityId::ActiveBindingSnare:
        Active = true; Mode = ELDDemoTargetMode::Unit; ExpectedOps = OpBit(ELDDemoEffectOp::Damage) | OpBit(ELDDemoEffectOp::Root);
        Rank3Ops = ExpectedOps | OpBit(ELDDemoEffectOp::Vulnerable); break;
    case ELDDemoAbilityId::PassiveBattleRhythm:
        ExpectedOps = OpBit(EOp::StatBonus) | OpBit(EOp::BasicStacks); ExpectedStats = Bit(ELDDemoBonusStat::AttackSpeedBonusRatio); break;
    case ELDDemoAbilityId::PassiveSecondWind:
        ExpectedOps = OpBit(EOp::OutOfCombatRegen); break;
    case ELDDemoAbilityId::PassivePrecision:
        ExpectedOps = OpBit(EOp::StatBonus) | OpBit(EOp::CooldownRefund); ExpectedStats = Bit(ELDDemoBonusStat::CritChanceBonusRatio);
        Rank3Ops = OpBit(EOp::AbilityCrit); break;
    case ELDDemoAbilityId::ActiveSlipperThrow:
        Active = true; Mode = ELDDemoTargetMode::Unit; ExpectedOps = OpBit(EOp::Damage) | OpBit(EOp::Slow); break;
    case ELDDemoAbilityId::ActiveFartDash:
        Active = true; Mode = ELDDemoTargetMode::DirectionDash;
        ExpectedOps = OpBit(ELDDemoEffectOp::Dash) | OpBit(ELDDemoEffectOp::SelfArea) | OpBit(ELDDemoEffectOp::Damage) | OpBit(ELDDemoEffectOp::Slow);
        // Lv3: a poison zone at the start point whose pulses trigger on-hit effects at a coefficient.
        Rank3Ops = ExpectedOps | OpBit(ELDDemoEffectOp::Zone) | OpBit(ELDDemoEffectOp::ZoneDamage) | OpBit(ELDDemoEffectOp::OnHit);
        break;
    case ELDDemoAbilityId::ActiveComboFlurry:
        Active = true; ExpectedOps = OpBit(EOp::TimedStatBonus); TimedStats = Bit(ELDDemoBonusStat::AttackSpeedBonusRatio);
        Rank3Ops = OpBit(EOp::CooldownRefund); break;
    case ELDDemoAbilityId::ActiveSlickMoves:
        Active = true; ExpectedOps = OpBit(EOp::TimedStatBonus);
        TimedStats = Bit(ELDDemoBonusStat::MoveSpeedBonusRatio) | Bit(ELDDemoBonusStat::AttackRangeCmFlat);
        Rank3Ops = OpBit(EOp::DispelSlow) | OpBit(EOp::Momentum); break;
    case ELDDemoAbilityId::PassiveCornered:
        ExpectedOps = OpBit(EOp::LowHpStatBonus); ExpectedStats = Bit(ELDDemoBonusStat::ADBonusRatio);
        Rank3Ops = OpBit(EOp::TriggeredShield); break;
    case ELDDemoAbilityId::PassiveLurking:
        ExpectedOps = OpBit(EOp::AmbushDamage); break;
    case ELDDemoAbilityId::PassivePublicShame:
        ExpectedOps = OpBit(EOp::ControlDamageAmp); Rank3Ops = OpBit(EOp::ManaOnHit); break;
    case ELDDemoAbilityId::PassiveEagleEye:
        ExpectedOps = OpBit(EOp::StatBonus) | OpBit(EOp::LongReach); ExpectedStats = Bit(ELDDemoBonusStat::AttackRangeCmFlat); break;
    case ELDDemoAbilityId::ActiveMonkeyPeach:
        Active = true; Mode = ELDDemoTargetMode::Unit;
        ExpectedOps = OpBit(EOp::Damage) | OpBit(EOp::Disarm) | OpBit(EOp::CooldownRefund); Rank3Ops = OpBit(EOp::AbilityCrit); break;
    case ELDDemoAbilityId::ActiveThousandYear:
        Active = true; Mode = ELDDemoTargetMode::Unit; ExpectedOps = OpBit(EOp::Damage) | OpBit(EOp::Backstab); break;
    case ELDDemoAbilityId::ActiveLunge:
        Active = true; Mode = ELDDemoTargetMode::DirectionDash; ExpectedOps = OpBit(EOp::Dash) | OpBit(EOp::EmpowerBasic);
        Rank3Ops = OpBit(EOp::CooldownRefund) | OpBit(EOp::PathSlow); break;
    case ELDDemoAbilityId::PassiveIronCrotch:
        ExpectedOps = OpBit(EOp::Thorns); break;
    case ELDDemoAbilityId::ActiveBodySlam:
        // The landing delivers the self-area hits around the landing foot.
        Active = true; Mode = ELDDemoTargetMode::GroundPoint;
        ExpectedOps = OpBit(EOp::Jump) | OpBit(EOp::SelfArea) | OpBit(EOp::Damage) | OpBit(EOp::Slow);
        Rank3Ops = OpBit(EOp::TriggeredShield);
        break;
    case ELDDemoAbilityId::ActiveComeHere:
        Active = true; ExpectedOps = OpBit(EOp::Taunt) | OpBit(EOp::TimedStatBonus);
        TimedStats = Bit(ELDDemoBonusStat::ArmorFlat) | Bit(ELDDemoBonusStat::MagicResistFlat);
        Rank3Ops = OpBit(EOp::TauntRetaliate); break;
    case ELDDemoAbilityId::ActivePorcelainFlop:
        // The self area names the recoil's targets; slow (stun at Lv3) and the shield are the recoil's payload.
        Active = true;
        ExpectedOps = OpBit(ELDDemoEffectOp::DamageGuard) | OpBit(ELDDemoEffectOp::SelfArea) | OpBit(ELDDemoEffectOp::Slow) | OpBit(ELDDemoEffectOp::Shield);
        Rank3Ops = OpBit(ELDDemoEffectOp::Stun); Rank3Drops = OpBit(ELDDemoEffectOp::Slow);
        break;
    case ELDDemoAbilityId::ActiveExposePost:
        Active = true; Mode = ELDDemoTargetMode::Unit; ExpectedOps = OpBit(ELDDemoEffectOp::Dot);
        Rank3Ops = ExpectedOps | OpBit(ELDDemoEffectOp::MobilityPunish); break;
    case ELDDemoAbilityId::ActiveBarrageRant:
        Active = true; Mode = ELDDemoTargetMode::DirectionProjectile;
        ExpectedOps = OpBit(ELDDemoEffectOp::Damage) | OpBit(ELDDemoEffectOp::Volley) | OpBit(ELDDemoEffectOp::RefreshDebuffs);
        Rank3Ops = ExpectedOps | OpBit(ELDDemoEffectOp::OnHit); break;
    case ELDDemoAbilityId::ActiveCancelBlast:
        Active = true; Mode = ELDDemoTargetMode::Unit; ExpectedOps = OpBit(ELDDemoEffectOp::Damage) | OpBit(ELDDemoEffectOp::DebuffAmp);
        Rank3Ops = ExpectedOps | OpBit(ELDDemoEffectOp::ShieldShatter) | OpBit(ELDDemoEffectOp::DebuffHeal); break;
    case ELDDemoAbilityId::ActivePieChart:
        Active = true; Mode = ELDDemoTargetMode::GroundPoint;
        ExpectedOps = OpBit(ELDDemoEffectOp::Zone) | OpBit(ELDDemoEffectOp::ZoneShield) | OpBit(ELDDemoEffectOp::ZoneSlow);
        Rank3Ops = ExpectedOps | OpBit(ELDDemoEffectOp::ZoneRoot); break;
    case ELDDemoAbilityId::ActiveTableFlip:
        Active = true; ExpectedOps = OpBit(ELDDemoEffectOp::Shield) | OpBit(ELDDemoEffectOp::SelfArea) | OpBit(ELDDemoEffectOp::ShieldBurst);
        Rank3Ops = ExpectedOps | OpBit(ELDDemoEffectOp::Knockback); break;
    case ELDDemoAbilityId::PassiveDoxMaster:
        ExpectedOps = OpBit(ELDDemoEffectOp::BreachStacks); break;
    case ELDDemoAbilityId::PassiveThickSkin:
        ExpectedOps = OpBit(ELDDemoEffectOp::MobilityShield); break;
    case ELDDemoAbilityId::PassiveMentalGymnastics:
        ExpectedOps = OpBit(ELDDemoEffectOp::ShieldPulse); break;
    case ELDDemoAbilityId::PassiveKeyboardWarrior:
        ExpectedOps = OpBit(ELDDemoEffectOp::BasicRider);
        Rank3Ops = ExpectedOps | OpBit(ELDDemoEffectOp::CastFrenzy); break;
    default:
        return Check.Require(false, Path, TEXT("unimplemented ability projection"));
    }
    Out.Kind = Active ? ELDDemoAbilityKind::Active : ELDDemoAbilityKind::Passive;
    if (!Check.Require(Kind.Equals(Active ? TEXT("active") : TEXT("passive"), ESearchCase::CaseSensitive), Path, TEXT("wrong ability kind"))
        || !Tags(F.Take(TEXT("tags")), Out.Tags, Check, Path + TEXT(".tags")) || !F.Flag(TEXT("draftable"), Out.Draftable)
        || !Target(F.Take(TEXT("target")), Out.Target, Check, Path + TEXT(".target"))
        || !Check.Require(Out.Target.Mode == Mode, Path, TEXT("wrong definition target")))
    {
        return false;
    }
    if (Active && (!F.Fixed(TEXT("windup"), TEXT("ms"), TEXT("ms"), Out.WindupMs)
        || !F.Identifier(TEXT("cooldown_family"), Out.CooldownFamily)
        || !Check.Require(Out.CooldownFamily == TEXT("cd_") + Out.Id.Mid(7), Path, TEXT("wrong cooldown family"))))
    {
        return false;
    }
    const TArray<FValue>* Rows;
    if (!Array(F.Take(TEXT("ranks")), Rows, Check, Path, 3, 3))
    {
        return false;
    }
    for (int32 I = 0; I < Rows->Num(); ++I)
    {
        FLDDemoRankDefinition Definition;
        const FOpMask RankOps = I == 2 ? ((ExpectedOps & ~Rank3Drops) | Rank3Ops) : ExpectedOps;
        if (!Rank((*Rows)[I], Definition, I + 1, Active, RankOps, Check, FString::Printf(TEXT("%s.ranks[%d]"), *Path, I)))
        {
            return false;
        }
        const FLDDemoEffectDefinition* StatEffect = Definition.Effects.FindByPredicate([](const FLDDemoEffectDefinition& Effect)
            { return Effect.GetOp() == ELDDemoEffectOp::StatBonus || Effect.GetOp() == ELDDemoEffectOp::LowHpStatBonus; });
        if (ExpectedStats && !Check.Require(StatEffect && StatMask(StatEffect->Stats) == ExpectedStats,
            Path, TEXT("wrong passive stat definition")))
        {
            return false;
        }
        if (TimedStats && !Check.Require(Definition.Effects.ContainsByPredicate([TimedStats](const FLDDemoEffectDefinition& Effect)
            { return Effect.GetOp() == ELDDemoEffectOp::TimedStatBonus && StatMask(Effect.Stats) == TimedStats; }),
            Path, TEXT("wrong timed stat definition")))
        {
            return false;
        }
        Out.Ranks.Add(MoveTemp(Definition));
    }
    return F.Done();
}

bool Attribute(const FValue& Value, FLDDemoAttributeDefinition& Out, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    if (!NativeIdentity(F, TEXT("attributes"), Out.Id, Out.NativeId)
        || !Stats(F.Take(TEXT("stats")), Out.Stats, Check, Path + TEXT(".stats")))
    {
        return false;
    }
    // Wild cards name their deductions explicitly; every other card has no penalties field at all.
    if (F.Object.IsValid() && F.Object->HasField(TEXT("penalties"))
        && !Stats(F.Take(TEXT("penalties")), Out.Penalties, Check, Path + TEXT(".penalties")))
    {
        return false;
    }
    if (!F.Done())
    {
        return false;
    }
    using S = ELDDemoBonusStat;
    uint32 Expected = 0, ExpectedPenalties = 0;
    switch (static_cast<ELDDemoAttributeId>(Out.NativeId))
    {
    case ELDDemoAttributeId::AttrMight: Expected = Bit(S::ADFlat) | Bit(S::CritChanceBonusRatio); break;
    case ELDDemoAttributeId::AttrMightHp: Expected = Bit(S::ADFlat) | Bit(S::MaxHPFlat); break;
    case ELDDemoAttributeId::AttrMightHaste: Expected = Bit(S::ADFlat) | Bit(S::CooldownHasteFlat); break;
    case ELDDemoAttributeId::AttrFocus: Expected = Bit(S::APFlat) | Bit(S::CooldownHasteFlat); break;
    case ELDDemoAttributeId::AttrFocusAs: Expected = Bit(S::APFlat) | Bit(S::AttackSpeedBonusRatio); break;
    case ELDDemoAttributeId::AttrFocusDefense: Expected = Bit(S::APFlat) | Bit(S::ArmorFlat) | Bit(S::MagicResistFlat); break;
    case ELDDemoAttributeId::AttrReach: Expected = Bit(S::AttackRangeCmFlat) | Bit(S::AttackSpeedBonusRatio); break;
    case ELDDemoAttributeId::AttrReachMs: Expected = Bit(S::AttackRangeCmFlat) | Bit(S::MoveSpeedBonusRatio); break;
    case ELDDemoAttributeId::AttrRoamHybrid: Expected = Bit(S::MoveSpeedBonusRatio) | Bit(S::ADFlat) | Bit(S::APFlat); break;
    case ELDDemoAttributeId::AttrResolve: Expected = Bit(S::MaxHPFlat) | Bit(S::ArmorFlat) | Bit(S::MagicResistFlat); break;
    case ELDDemoAttributeId::AttrVitality: Expected = Bit(S::MaxHPFlat) | Bit(S::MaxMPFlat) | Bit(S::HPRegenFlat); break;
    case ELDDemoAttributeId::AttrWildGlassCannon:
        Expected = Bit(S::ADFlat) | Bit(S::APFlat) | Bit(S::AttackSpeedBonusRatio); ExpectedPenalties = Bit(S::MaxHPFlat); break;
    case ELDDemoAttributeId::AttrWildJuggernaut:
        Expected = Bit(S::MaxHPFlat) | Bit(S::ArmorFlat) | Bit(S::MagicResistFlat); ExpectedPenalties = Bit(S::MoveSpeedBonusRatio); break;
    case ELDDemoAttributeId::AttrWildBrawler:
        Expected = Bit(S::AttackSpeedBonusRatio) | Bit(S::MoveSpeedBonusRatio); ExpectedPenalties = Bit(S::AttackRangeCmFlat); break;
    default: return Check.Require(false, Path, TEXT("unimplemented attribute projection"));
    }
    return Check.Require(StatMask(Out.Stats) == Expected, Path, TEXT("wrong attribute stats"))
        && Check.Require(StatMask(Out.Penalties) == ExpectedPenalties, Path, TEXT("wrong attribute penalties"));
}

bool SameHandlers(const TArray<FLDDemoEffectDefinition>& Base, const TArray<FLDDemoEffectDefinition>& Upgraded)
{
    if (Base.Num() != Upgraded.Num())
    {
        return false;
    }
    for (int32 I = 0; I < Base.Num(); ++I)
    {
        if (Base[I].Opcode != Upgraded[I].Opcode) { return false; }
    }
    return true;
}

bool Equipment(const FValue& Value, FLDDemoEquipmentDefinition& Out, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    if (!NativeIdentity(F, TEXT("equipment"), Out.Id, Out.NativeId)
        || !F.Int(TEXT("buy_price"), Out.BuyPrice, 1) || !F.Int(TEXT("sell_gold"), Out.SellGold)
        || !Tags(F.Take(TEXT("tags")), Out.Tags, Check, Path + TEXT(".tags"))
        || !Stats(F.Take(TEXT("stats")), Out.Stats, Check, Path + TEXT(".stats"))
        || !Stats(F.Take(TEXT("upgraded_stats")), Out.UpgradedStats, Check, Path + TEXT(".upgraded_stats")))
    {
        return false;
    }
    uint32 ExpectedStats = 0;
    FOpMask ActiveOps, PassiveOps;
    ELDDemoTargetMode Mode = ELDDemoTargetMode::Self;
    const TCHAR* ActiveFamily = nullptr;
    const TCHAR* PassiveFamily = nullptr;
    switch (Out.GetNativeId())
    {
    case ELDDemoEquipmentId::ItemIronPlate:
        ExpectedStats = Bit(ELDDemoBonusStat::MaxHPFlat) | Bit(ELDDemoBonusStat::ArmorFlat);
        PassiveFamily = TEXT("passive_crit_guard"); PassiveOps = OpBit(ELDDemoEffectOp::CritDamageReduction); break;
    case ELDDemoEquipmentId::ItemEdgeBlade:
        ExpectedStats = Bit(ELDDemoBonusStat::ADFlat) | Bit(ELDDemoBonusStat::AttackSpeedBonusRatio);
        PassiveFamily = TEXT("passive_third_strike"); PassiveOps = OpBit(ELDDemoEffectOp::NthBasicBonus); break;
    case ELDDemoEquipmentId::ItemRunicFocus:
        ExpectedStats = Bit(ELDDemoBonusStat::APFlat) | Bit(ELDDemoBonusStat::CooldownHasteFlat);
        PassiveFamily = TEXT("passive_mana_refund"); PassiveOps = OpBit(ELDDemoEffectOp::ManaOnHit); break;
    case ELDDemoEquipmentId::ItemSiphonEmblem:
        ExpectedStats = Bit(ELDDemoBonusStat::ADFlat) | Bit(ELDDemoBonusStat::MagicResistFlat);
        PassiveFamily = TEXT("passive_siphon"); PassiveOps = OpBit(ELDDemoEffectOp::Lifesteal); break;
    case ELDDemoEquipmentId::ItemPhaseAnchor:
        ExpectedStats = Bit(ELDDemoBonusStat::MoveSpeedBonusRatio);
        PassiveFamily = TEXT("passive_stride"); PassiveOps = OpBit(ELDDemoEffectOp::DashBoost); break;
    case ELDDemoEquipmentId::ItemArcaneBattery:
        ExpectedStats = Bit(ELDDemoBonusStat::MaxMPFlat) | Bit(ELDDemoBonusStat::MagicResistFlat);
        ActiveFamily = TEXT("cd_arcane_battery"); ActiveOps = OpBit(ELDDemoEffectOp::RestoreMana) | OpBit(ELDDemoEffectOp::Shield); break;
    case ELDDemoEquipmentId::ItemEchoMaul:
        ExpectedStats = Bit(ELDDemoBonusStat::ADFlat) | Bit(ELDDemoBonusStat::AttackRangeCmFlat);
        PassiveFamily = TEXT("passive_cleave"); PassiveOps = OpBit(ELDDemoEffectOp::Cleave); break;
    case ELDDemoEquipmentId::ItemSlingshot:
        ExpectedStats = Bit(ELDDemoBonusStat::AttackRangeCmFlat) | Bit(ELDDemoBonusStat::AttackSpeedBonusRatio); break;
    case ELDDemoEquipmentId::ItemPaperBox:
        ExpectedStats = Bit(ELDDemoBonusStat::APFlat) | Bit(ELDDemoBonusStat::MoveSpeedBonusRatio);
        ActiveFamily = TEXT("cd_paper_box"); ActiveOps = OpBit(ELDDemoEffectOp::Stealth); break;
    case ELDDemoEquipmentId::ItemNailUniform:
        ExpectedStats = Bit(ELDDemoBonusStat::MaxHPFlat) | Bit(ELDDemoBonusStat::ArmorFlat);
        PassiveFamily = TEXT("passive_prickle"); PassiveOps = OpBit(ELDDemoEffectOp::Thorns); break;
    case ELDDemoEquipmentId::ItemMegaphone:
        ExpectedStats = Bit(ELDDemoBonusStat::APFlat) | Bit(ELDDemoBonusStat::CooldownHasteFlat);
        PassiveFamily = TEXT("passive_megaphone"); PassiveOps = OpBit(ELDDemoEffectOp::AfflictionAmp); break;
    case ELDDemoEquipmentId::ItemDossier:
        ExpectedStats = Bit(ELDDemoBonusStat::APFlat) | Bit(ELDDemoBonusStat::MaxHPFlat); Mode = ELDDemoTargetMode::Unit;
        ActiveFamily = TEXT("cd_dossier"); ActiveOps = OpBit(ELDDemoEffectOp::Damage) | OpBit(ELDDemoEffectOp::HealCut); break;
    case ELDDemoEquipmentId::ItemPrivacyScreen:
        ExpectedStats = Bit(ELDDemoBonusStat::MaxHPFlat) | Bit(ELDDemoBonusStat::APFlat);
        PassiveFamily = TEXT("passive_privacy_screen"); PassiveOps = OpBit(ELDDemoEffectOp::ShieldBreakRetort); break;
    case ELDDemoEquipmentId::ItemSkateboard:
        ExpectedStats = Bit(ELDDemoBonusStat::ADFlat) | Bit(ELDDemoBonusStat::MoveSpeedBonusRatio);
        PassiveFamily = TEXT("passive_skateboard"); PassiveOps = OpBit(ELDDemoEffectOp::HitHaste); break;
    case ELDDemoEquipmentId::ItemDecoyWig:
        ExpectedStats = Bit(ELDDemoBonusStat::ADFlat) | Bit(ELDDemoBonusStat::APFlat);
        ActiveFamily = TEXT("cd_decoy_wig");
        ActiveOps = OpBit(ELDDemoEffectOp::DispelRoot) | OpBit(ELDDemoEffectOp::DispelSlow) | OpBit(ELDDemoEffectOp::LeapBack)
            | OpBit(ELDDemoEffectOp::Decoy); break;
    case ELDDemoEquipmentId::ItemEnergyDrink:
        ExpectedStats = Bit(ELDDemoBonusStat::CooldownHasteFlat) | Bit(ELDDemoBonusStat::AttackSpeedBonusRatio);
        ActiveFamily = TEXT("cd_energy_drink"); ActiveOps = OpBit(ELDDemoEffectOp::CooldownRefresh); break;
    default:
        return Check.Require(false, Path, TEXT("unimplemented equipment projection"));
    }
    if (!Check.Require(StatMask(Out.Stats) == ExpectedStats, Path, TEXT("wrong equipment stats")))
    {
        return false;
    }
    const FValue Active = F.Take(TEXT("active"));
    const FValue Passive = F.Take(TEXT("passive"));
    if (!Check.Good)
    {
        return false;
    }
    Out.HasActive = Active->Type != EJson::Null;
    Out.HasPassive = Passive->Type != EJson::Null;
    if (!Check.Require(Out.HasActive == (ActiveFamily != nullptr) && Out.HasPassive == (PassiveFamily != nullptr),
        Path, TEXT("wrong active/passive presence")))
    {
        return false;
    }
    if (Out.HasActive)
    {
        FFields A(Check, Active, Path + TEXT(".active"));
        if (!Target(A.Take(TEXT("target")), Out.Active.Target, Check, A.Path + TEXT(".target"))
            || !Check.Require(Out.Active.Target.Mode == Mode, A.Path, TEXT("wrong active target"))
            || !A.Fixed(TEXT("windup"), TEXT("ms"), TEXT("ms"), Out.Active.WindupMs)
            || !A.Identifier(TEXT("cooldown_family"), Out.Active.CooldownFamily)
            || !Check.Require(Out.Active.CooldownFamily == ActiveFamily, A.Path, TEXT("wrong cooldown family"))
            || !Rank(A.Take(TEXT("rank")), Out.Active.Rank, 1, true, ActiveOps, Check, A.Path + TEXT(".rank")))
        {
            return false;
        }
        // 112 §7: optional until its value is approved; an item missing it is simply not upgradable.
        if (A.Object.IsValid() && A.Object->HasField(TEXT("upgraded_rank")))
        {
            Out.Active.HasUpgradedRank = true;
            if (!Rank(A.Take(TEXT("upgraded_rank")), Out.Active.UpgradedRank, 2, true, ActiveOps, Check, A.Path + TEXT(".upgraded_rank"))
                || !Check.Require(SameHandlers(Out.Active.Rank.Effects, Out.Active.UpgradedRank.Effects), A.Path,
                    TEXT("the upgraded active must keep the base handlers in the same order")))
            {
                return false;
            }
        }
        if (!A.Done())
        {
            return false;
        }
    }
    if (Out.HasPassive)
    {
        FFields P(Check, Passive, Path + TEXT(".passive"));
        if (!P.Identifier(TEXT("family"), Out.Passive.Family)
            || !Check.Require(Out.Passive.Family == PassiveFamily, P.Path, TEXT("wrong passive family"))
            || !Effects(P.Take(TEXT("effects")), Out.Passive.Effects, PassiveOps, Check, P.Path + TEXT(".effects")))
        {
            return false;
        }
        if (P.Object.IsValid() && P.Object->HasField(TEXT("upgraded")))
        {
            Out.Passive.HasUpgraded = true;
            FFields U(Check, P.Take(TEXT("upgraded")), P.Path + TEXT(".upgraded"));
            if (!Effects(U.Take(TEXT("effects")), Out.Passive.UpgradedEffects, PassiveOps, Check, U.Path + TEXT(".effects")) || !U.Done()
                || !Check.Require(SameHandlers(Out.Passive.Effects, Out.Passive.UpgradedEffects), P.Path,
                    TEXT("the upgraded passive must keep the base handlers in the same order")))
            {
                return false;
            }
        }
        if (!P.Done())
        {
            return false;
        }
    }
    return F.Done();
}

bool Monster(const FValue& Value, FLDDemoMonsterDefinition& Out, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    FString Tier;
    if (!NativeIdentity(F, TEXT("monsters"), Out.Id, Out.NativeId) || !F.Text(TEXT("tier"), Tier)
        || !Check.Require(Tier.Equals(TEXT("Low"), ESearchCase::CaseSensitive)
            || Tier.Equals(TEXT("High"), ESearchCase::CaseSensitive), Path, TEXT("unknown camp tier")))
    {
        return false;
    }
    Out.Tier = Tier.Equals(TEXT("Low"), ESearchCase::CaseSensitive) ? ELDDemoCampTier::Low : ELDDemoCampTier::High;
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
        && F.Int(TEXT("reward_xp"), Out.RewardXP) && F.Int(TEXT("reward_gold"), Out.RewardGold)
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

bool Ids(const FValue& Value, TArray<FString>& Out, FCheck& Check, const FString& Path, int32 Count)
{
    const TArray<FValue>* Rows;
    if (!Array(Value, Rows, Check, Path, Count, Count))
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
    if (!F.SmallInt(TEXT("max_level"), Out.MaxLevel, 10, 10)
        || !F.SmallInt(TEXT("max_choices"), Out.MaxChoices, 10, 10)
        || !F.SmallInt(TEXT("max_skill_rank"), Out.MaxSkillRank, 3, 3)
        || !F.SmallInt(TEXT("active_slots"), Out.ActiveSlots, 2, 2)
        || !F.SmallInt(TEXT("equipment_slots"), Out.EquipmentSlots, 2, 2)
        || !F.Int(TEXT("xp_start"), Out.XPStart, 0, 0))
    {
        return false;
    }
    for (const auto& Pair : {TPair<const TCHAR*, TArray<int64>*>(TEXT("xp_thresholds"), &Out.XPThresholds),
        {TEXT("xp_to_next"), &Out.XPToNext}})
    {
        const int32 Count = Pair.Value == &Out.XPThresholds ? 10 : 9;
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
    FFields Opening(Check, F.Take(TEXT("opening_offer")), TEXT("content.progression.opening_offer"));
    if (!Opening.SmallInt(TEXT("active_count"), Out.OpeningActiveCount, 0, 3)
        || !Opening.SmallInt(TEXT("passive_count"), Out.OpeningPassiveCount, 0, 3) || !Opening.Done())
    {
        return false;
    }
    const TArray<FValue>* Rerolls;
    if (!Array(F.Take(TEXT("reroll_levels")), Rerolls, Check, F.Path, 0, 10))
    {
        return false;
    }
    for (const auto& Row : *Rerolls)
    {
        int64 Level;
        if (!Integer(Row, Level, Check, F.Path, 2, Out.MaxLevel)
            || !Check.Require(Out.RerollLevels.IsEmpty() || Out.RerollLevels.Last() < Level, F.Path,
                TEXT("reroll levels must be distinct and ascending")))
        {
            return false;
        }
        Out.RerollLevels.Add(int32(Level));
    }
    return F.Done();
}

bool Content(const FValue& Value, FLDDemoContentDefinitions& Out, FCheck& Check)
{
    FFields F(Check, Value, TEXT("content"));
    FFields Identity(Check, F.Take(TEXT("identity")), TEXT("content.identity"));
    int64 Revision;
    if (!Identity.Int(TEXT("schema_version"), Revision, 1, 1)
        || !Identity.Int(TEXT("handler_revision"), Revision, 1, 1)
        || !Identity.Int(TEXT("projection_revision"), Revision, 3, 3))
    {
        return false;
    }
    FFields Approval(Check, Identity.Take(TEXT("approval")), TEXT("content.identity.approval"));
    if (!Approval.Text(TEXT("status"), Out.Identity.ApprovalStatus)
        || !Check.Require(Out.Identity.ApprovalStatus.Equals(TEXT("approved"), ESearchCase::CaseSensitive), Approval.Path, TEXT("approval metadata required"))
        || !Approval.Text(TEXT("decision"), Out.Identity.ApprovalDecision)
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
    // D19: the innate dash is projected into the single-rank active shape Combat executes for every source.
    FFields Dash(Check, Character.Take(TEXT("dash")), TEXT("content.character.dash"));
    FLDDemoEquipmentActive& InnateDash = Out.Character.Dash;
    FLDDemoEffectDefinition& DashMove = InnateDash.Rank.Effects.AddDefaulted_GetRef();
    InnateDash.Target.Mode = ELDDemoTargetMode::DirectionDash;
    InnateDash.Rank.Rank = 1;
    DashMove.Opcode = uint8(ELDDemoEffectOp::Dash);
    if (!Dash.Fixed(TEXT("mana_cost"), TEXT("mp"), TEXT("p2"), InnateDash.Rank.ManaCostP2)
        || !Dash.Fixed(TEXT("cooldown"), TEXT("ms"), TEXT("ms"), InnateDash.Rank.CooldownMs, 1)
        || !Dash.Fraction(TEXT("distance"), TEXT("cm"), DashMove.DistanceCm, true)
        || !Dash.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), DashMove.DurationMs, 1)
        || !Check.Require(DashMove.DurationMs < InnateDash.Rank.CooldownMs, Dash.Path, TEXT("dash outlasts its cooldown"))
        || !Dash.Done() || !Character.Done()
        || !Definitions(F.Take(TEXT("abilities")), Out.Abilities, Check, TEXT("content.abilities"), 1, 30, Ability)
        || !Definitions(F.Take(TEXT("attributes")), Out.Attributes, Check, TEXT("content.attributes"), 3, 14, Attribute)
        || !Definitions(F.Take(TEXT("equipment")), Out.Equipment, Check, TEXT("content.equipment"), 1, 16, Equipment)
        || !Progression(F.Take(TEXT("progression")), Out.Progression, Check))
    {
        return false;
    }
    FFields Camps(Check, F.Take(TEXT("camps")), TEXT("content.camps"));
    if (!Definitions(Camps.Take(TEXT("monsters")), Out.Monsters, Check, TEXT("content.camps.monsters"), 2, 2, Monster))
    {
        return false;
    }
    const TArray<FValue>* Placements;
    if (!Array(Camps.Take(TEXT("placements")), Placements, Check, Camps.Path, 5, 5))
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
            || !Boss.Int(TEXT("reward_xp"), Body.RewardXP) || !Boss.Int(TEXT("reward_gold"), Body.RewardGold))
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
        || !Ids(World.Take(TEXT("spawn_ids")), Out.World.SpawnIds, Check, World.Path + TEXT(".spawn_ids"), 10)
        || !Ids(World.Take(TEXT("camp_ids")), Out.World.CampIds, Check, World.Path + TEXT(".camp_ids"), 5)
        || !World.Identifier(TEXT("shop_id"), Out.World.ShopId) || !World.Identifier(TEXT("capture_id"), Out.World.CaptureId)
        || !World.Done())
    {
        return false;
    }
    TSet<FString> DisplayIds;
    DisplayIds.Add(Out.Character.Id);
    for (const auto& Row : Out.Abilities) { DisplayIds.Add(Row.Id); }
    for (const auto& Row : Out.Attributes) { DisplayIds.Add(Row.Id); }
    for (const auto& Row : Out.Equipment) { DisplayIds.Add(Row.Id); }
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
        && CanonicalBytes(Display, DisplayBytes, Check) && Hash(DisplayBytes, DisplayDigest, Check, TEXT("content.display"))
        && Check.Require(DisplayDigest == Out.Identity.Display, TEXT("content.display"), TEXT("display digest mismatch")) && F.Done();
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
        TEXT("#pragma once\n#define LD_DEMO_CONTENT_SCHEMA_VERSION 1\n#define LD_DEMO_CONTENT_HANDLER_REVISION 1\n")
        TEXT("#define LD_DEMO_CONTENT_PROJECTION_REVISION 3\n#define LD_DEMO_CONTENT_PAYLOAD_SHA256 TEXT(\"%s\")\n")
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
        || !Root.SmallInt(TEXT("schema_version"), Out.SchemaVersion, 1, 1)
        || !Root.SmallInt(TEXT("handler_revision"), Out.HandlerRevision, 1, 1)
        || !Root.SmallInt(TEXT("projection_revision"), Out.ProjectionRevision, 3, 3)
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
