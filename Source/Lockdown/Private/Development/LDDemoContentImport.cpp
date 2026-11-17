#include "Development/LDDemoContentJson.h"
#include "Development/LDDemoContentMath.h"
#include "Development/LDDemoCardText.h"
#include "Region/LDMapGeometry.h"
#include "Framework/LDShowdownSettings.h"
#include "../World/LDDemoWorldContract.h"

namespace LDDemoContent
{
namespace
{
struct FNativeId
{
    const TCHAR* Category;
    const TCHAR* Id;
    uint16 Number;
};

#define DEMO_ID(Category, Name, Enum, Value) {TEXT(Category), TEXT(Name), uint16(Enum::Value)}
const FNativeId NativeIds[] =
{
    DEMO_ID("character", "character_demo", ELDDemoCharacterId, CharacterDemo),
    DEMO_ID("final_mods", "final_bowling", ELDDemoFinalModId, FinalBowling),
    DEMO_ID("final_mods", "final_forced_exit", ELDDemoFinalModId, FinalForcedExit),
    DEMO_ID("final_mods", "final_traffic", ELDDemoFinalModId, FinalTraffic),
    DEMO_ID("final_mods", "final_split", ELDDemoFinalModId, FinalSplit),
    DEMO_ID("final_mods", "final_landlord", ELDDemoFinalModId, FinalLandlord),
    DEMO_ID("final_mods", "final_breach", ELDDemoFinalModId, FinalBreach),
    DEMO_ID("final_mods", "final_comeback", ELDDemoFinalModId, FinalComeback),
    DEMO_ID("final_mods", "final_open_mic", ELDDemoFinalModId, FinalOpenMic),
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
    DEMO_ID("cards", "card_third_breath", ELDDemoCardId, CardThirdBreath),
    DEMO_ID("cards", "card_punch_think", ELDDemoCardId, CardPunchThink),
    DEMO_ID("cards", "card_solid_punch", ELDDemoCardId, CardSolidPunch),
    DEMO_ID("cards", "card_third_shove", ELDDemoCardId, CardThirdShove),
    DEMO_ID("cards", "card_both_hands", ELDDemoCardId, CardBothHands),
    DEMO_ID("cards", "card_quick_hands", ELDDemoCardId, CardQuickHands),
    DEMO_ID("cards", "card_scholar_punch", ELDDemoCardId, CardScholarPunch),
    DEMO_ID("cards", "card_heated_up", ELDDemoCardId, CardHeatedUp),
    DEMO_ID("cards", "card_breach_fist", ELDDemoCardId, CardBreachFist),
    DEMO_ID("cards", "card_fire_fist", ELDDemoCardId, CardFireFist),
    DEMO_ID("cards", "card_water_fist", ELDDemoCardId, CardWaterFist),
    DEMO_ID("cards", "card_shock_fist", ELDDemoCardId, CardShockFist),
    DEMO_ID("cards", "card_next_up", ELDDemoCardId, CardNextUp),
    DEMO_ID("cards", "card_pay_back", ELDDemoCardId, CardPayBack),
    DEMO_ID("cards", "card_setup_line", ELDDemoCardId, CardSetupLine),
    DEMO_ID("cards", "card_encore", ELDDemoCardId, CardEncore),
    DEMO_ID("cards", "card_again_now", ELDDemoCardId, CardAgainNow),
    DEMO_ID("cards", "card_big_one", ELDDemoCardId, CardBigOne),
    DEMO_ID("cards", "card_blood_price", ELDDemoCardId, CardBloodPrice),
    DEMO_ID("cards", "card_lend_fist", ELDDemoCardId, CardLendFist),
    DEMO_ID("cards", "card_hurt_heal", ELDDemoCardId, CardHurtHeal),
    DEMO_ID("cards", "card_held_shot", ELDDemoCardId, CardHeldShot),
    DEMO_ID("cards", "card_practice", ELDDemoCardId, CardPractice),
    DEMO_ID("cards", "card_add_fire", ELDDemoCardId, CardAddFire),
    DEMO_ID("cards", "card_add_water", ELDDemoCardId, CardAddWater),
    DEMO_ID("cards", "card_add_shock", ELDDemoCardId, CardAddShock),
    DEMO_ID("cards", "card_blast_reload", ELDDemoCardId, CardBlastReload),
    DEMO_ID("cards", "card_bump_reset", ELDDemoCardId, CardBumpReset),
    DEMO_ID("cards", "card_bump_again", ELDDemoCardId, CardBumpAgain),
    DEMO_ID("cards", "card_dash_punch", ELDDemoCardId, CardDashPunch),
    DEMO_ID("cards", "card_double_dash", ELDDemoCardId, CardDoubleDash),
    DEMO_ID("cards", "card_pass_through", ELDDemoCardId, CardPassThrough),
    DEMO_ID("cards", "card_shock_dash", ELDDemoCardId, CardShockDash),
    DEMO_ID("cards", "card_spiky_hide", ELDDemoCardId, CardSpikyHide),
    DEMO_ID("cards", "card_takedown", ELDDemoCardId, CardTakedown),
    DEMO_ID("cards", "card_hang_on", ELDDemoCardId, CardHangOn),
    DEMO_ID("cards", "card_reaction_refund", ELDDemoCardId, CardReactionRefund),
    DEMO_ID("cards", "card_wave_after", ELDDemoCardId, CardWaveAfter),
    DEMO_ID("cards", "card_hard_skin", ELDDemoCardId, CardHardSkin),
    DEMO_ID("cards", "card_ball_kick", ELDDemoCardId, CardBallKick),
    DEMO_ID("cards", "card_skill_crit", ELDDemoCardId, CardSkillCrit),
    DEMO_ID("cards", "card_skill_drain", ELDDemoCardId, CardSkillDrain),
    DEMO_ID("cards", "card_thick_hide", ELDDemoCardId, CardThickHide),
    DEMO_ID("cards", "card_overfed", ELDDemoCardId, CardOverfed),
    DEMO_ID("cards", "card_berserk", ELDDemoCardId, CardBerserk),
    DEMO_ID("cards", "card_quick_brain", ELDDemoCardId, CardQuickBrain),
    DEMO_ID("cards", "card_punch_heal", ELDDemoCardId, CardPunchHeal),
    DEMO_ID("cards", "card_grow_fat", ELDDemoCardId, CardGrowFat),
    DEMO_ID("cards", "card_kill_streak", ELDDemoCardId, CardKillStreak),
    DEMO_ID("cards", "card_tough_skin", ELDDemoCardId, CardToughSkin),
    DEMO_ID("cards", "card_charge_up", ELDDemoCardId, CardChargeUp),
    DEMO_ID("cards", "card_live_wire", ELDDemoCardId, CardLiveWire),
    DEMO_ID("cards", "card_long_soak", ELDDemoCardId, CardLongSoak),
    DEMO_ID("cards", "card_louder_blast", ELDDemoCardId, CardLouderBlast),
    DEMO_ID("cards", "card_endless_burn", ELDDemoCardId, CardEndlessBurn),
    DEMO_ID("monsters", "monster_scavenger", ELDDemoMonsterId, MonsterScavenger),
    DEMO_ID("monsters", "monster_guardian", ELDDemoMonsterId, MonsterGuardian),
    DEMO_ID("monsters", "monster_dean", ELDDemoMonsterId, MonsterDean),
    DEMO_ID("monsters", "monster_advisor", ELDDemoMonsterId, MonsterAdvisor),
    DEMO_ID("monsters", "monster_promoter", ELDDemoMonsterId, MonsterPromoter),
    DEMO_ID("monsters", "monster_courier", ELDDemoMonsterId, MonsterCourier),
    DEMO_ID("monsters", "monster_agent", ELDDemoMonsterId, MonsterAgent),
    DEMO_ID("monsters", "monster_guard", ELDDemoMonsterId, MonsterGuard),
    DEMO_ID("monsters", "monster_auditor", ELDDemoMonsterId, MonsterAuditor),
    DEMO_ID("monsters", "monster_client", ELDDemoMonsterId, MonsterClient),
    DEMO_ID("monsters", "monster_director", ELDDemoMonsterId, MonsterDirector),
    DEMO_ID("monsters", "monster_final_client", ELDDemoMonsterId, MonsterFinalClient),
    DEMO_ID("monsters", "monster_property", ELDDemoMonsterId, MonsterProperty),
    DEMO_ID("statuses", "root", ELDDemoStatusId, Root),
    DEMO_ID("statuses", "shield", ELDDemoStatusId, Shield),
    DEMO_ID("statuses", "camp_returning", ELDDemoStatusId, CampReturning),
    DEMO_ID("statuses", "camp_enraged", ELDDemoStatusId, CampEnraged),
    DEMO_ID("statuses", "camp_guarding", ELDDemoStatusId, CampGuarding),
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
    DEMO_ID("effect_ops", "lifesteal", ELDDemoEffectOp, Lifesteal),
    DEMO_ID("effect_ops", "dash", ELDDemoEffectOp, Dash),
    DEMO_ID("effect_ops", "slow", ELDDemoEffectOp, Slow),
    DEMO_ID("effect_ops", "timed_stat_bonus", ELDDemoEffectOp, TimedStatBonus),
    DEMO_ID("effect_ops", "self_area", ELDDemoEffectOp, SelfArea),
    DEMO_ID("effect_ops", "taunt", ELDDemoEffectOp, Taunt),
    DEMO_ID("effect_ops", "disarm", ELDDemoEffectOp, Disarm),
    DEMO_ID("effect_ops", "stun", ELDDemoEffectOp, Stun),
    DEMO_ID("effect_ops", "empower_basic", ELDDemoEffectOp, EmpowerBasic),
    DEMO_ID("effect_ops", "thorns", ELDDemoEffectOp, Thorns),
    DEMO_ID("effect_ops", "triggered_shield", ELDDemoEffectOp, TriggeredShield),
    DEMO_ID("effect_ops", "dispel_slow", ELDDemoEffectOp, DispelSlow),
    DEMO_ID("effect_ops", "knockback", ELDDemoEffectOp, Knockback),
    DEMO_ID("effect_ops", "zone", ELDDemoEffectOp, Zone),
    DEMO_ID("effect_ops", "zone_slow", ELDDemoEffectOp, ZoneSlow),
    DEMO_ID("effect_ops", "zone_damage", ELDDemoEffectOp, ZoneDamage),
    DEMO_ID("effect_ops", "silence", ELDDemoEffectOp, Silence),
    DEMO_ID("effect_ops", "breach", ELDDemoEffectOp, Breach),
    DEMO_ID("effect_ops", "burn", ELDDemoEffectOp, Burn),
    DEMO_ID("effect_ops", "zone_burn", ELDDemoEffectOp, ZoneBurn),
    DEMO_ID("effect_ops", "channel_cone", ELDDemoEffectOp, ChannelCone),
    DEMO_ID("effect_ops", "ground_impact", ELDDemoEffectOp, GroundImpact),
    DEMO_ID("effect_ops", "attack_slow", ELDDemoEffectOp, AttackSlow),
    DEMO_ID("effect_ops", "contact_trap", ELDDemoEffectOp, ContactTrap),
    DEMO_ID("effect_ops", "dash_contact", ELDDemoEffectOp, DashContact),
    DEMO_ID("effect_ops", "wet", ELDDemoEffectOp, Wet),
    DEMO_ID("effect_ops", "electrify", ELDDemoEffectOp, Electrify),
    DEMO_ID("effect_ops", "chain_bounce", ELDDemoEffectOp, ChainBounce),
    DEMO_ID("effect_ops", "brace", ELDDemoEffectOp, Brace),
    DEMO_ID("effect_ops", "zone_wet", ELDDemoEffectOp, ZoneWet),
    DEMO_ID("effect_ops", "zone_haste", ELDDemoEffectOp, ZoneHaste),
    DEMO_ID("effect_ops", "grievous", ELDDemoEffectOp, Grievous),
    DEMO_ID("effect_ops", "basic_element", ELDDemoEffectOp, BasicElement),
    DEMO_ID("effect_ops", "idle_ready_amp", ELDDemoEffectOp, IdleReadyAmp),
    DEMO_ID("effect_ops", "cooldown_cost", ELDDemoEffectOp, CooldownCost),
    DEMO_ID("effect_ops", "knockback_impact", ELDDemoEffectOp, KnockbackImpact),
    DEMO_ID("effect_ops", "skill_lifesteal", ELDDemoEffectOp, SkillLifesteal),
    DEMO_ID("effect_ops", "projectile_return", ELDDemoEffectOp, ProjectileReturn),
    DEMO_ID("effect_ops", "projectile_pierce", ELDDemoEffectOp, ProjectilePierce),
    DEMO_ID("effect_ops", "echo_impact", ELDDemoEffectOp, EchoImpact),
    DEMO_ID("effect_ops", "hit_haste_other", ELDDemoEffectOp, HitHasteOther),
    DEMO_ID("effect_ops", "setup_mark", ELDDemoEffectOp, SetupMark),
    DEMO_ID("effect_ops", "reaction_haste", ELDDemoEffectOp, ReactionHaste),
    DEMO_ID("effect_ops", "zone_tether", ELDDemoEffectOp, ZoneTether),
    DEMO_ID("effect_ops", "final_knockback", ELDDemoEffectOp, FinalKnockback),
    DEMO_ID("effect_ops", "empower_reach", ELDDemoEffectOp, EmpowerReach),
    DEMO_ID("effect_ops", "pull", ELDDemoEffectOp, Pull),
    DEMO_ID("effect_ops", "vulnerable", ELDDemoEffectOp, Vulnerable),
    DEMO_ID("effect_ops", "next_basic_amp", ELDDemoEffectOp, NextBasicAmp),
    DEMO_ID("effect_ops", "momentum_gain", ELDDemoEffectOp, MomentumGain),
    DEMO_ID("effect_ops", "momentum_steal", ELDDemoEffectOp, MomentumSteal),
    DEMO_ID("effect_ops", "follow_up", ELDDemoEffectOp, FollowUp),
    DEMO_ID("effect_ops", "recast", ELDDemoEffectOp, Recast),
    DEMO_ID("effect_ops", "dash_back", ELDDemoEffectOp, DashBack),
    DEMO_ID("effect_ops", "self_buff", ELDDemoEffectOp, SelfBuff),
    DEMO_ID("effect_ops", "burn_burst", ELDDemoEffectOp, BurnBurst),
    DEMO_ID("effect_ops", "pulse", ELDDemoEffectOp, Pulse),
    DEMO_ID("effect_ops", "zone_guard", ELDDemoEffectOp, ZoneGuard),
    DEMO_ID("effect_ops", "zone_momentum", ELDDemoEffectOp, ZoneMomentum),
    DEMO_ID("effect_ops", "overcharge", ELDDemoEffectOp, Overcharge),
    DEMO_ID("effect_ops", "follow_basic", ELDDemoEffectOp, FollowBasic),
    DEMO_ID("effect_ops", "shield_burst_break", ELDDemoEffectOp, ShieldBurstBreak),
    DEMO_ID("effect_ops", "basic_rider", ELDDemoEffectOp, BasicRider),
    DEMO_ID("effect_ops", "delayed_burst", ELDDemoEffectOp, DelayedBurst),
    DEMO_ID("effect_ops", "shield_reflect", ELDDemoEffectOp, ShieldReflect),
    DEMO_ID("effect_ops", "shield_end_heal", ELDDemoEffectOp, ShieldEndHeal),
    DEMO_ID("effect_ops", "shield_hit_bonus", ELDDemoEffectOp, ShieldHitBonus),
    DEMO_ID("effect_ops", "brace_stun", ELDDemoEffectOp, BraceStun),
    DEMO_ID("effect_ops", "brace_heal", ELDDemoEffectOp, BraceHeal),
    DEMO_ID("effect_ops", "brace_stack", ELDDemoEffectOp, BraceStack),
    DEMO_ID("effect_ops", "brace_shove", ELDDemoEffectOp, BraceShove),
    DEMO_ID("effect_ops", "brace_shield", ELDDemoEffectOp, BraceShield),
    DEMO_ID("effect_ops", "projectile_zone", ELDDemoEffectOp, ProjectileZone),
    DEMO_ID("effect_ops", "combo_momentum", ELDDemoEffectOp, ComboMomentum),
    DEMO_ID("effect_ops", "basic_cooldown_cut", ELDDemoEffectOp, BasicCooldownCut),
    DEMO_ID("effect_ops", "combo_skill_hit", ELDDemoEffectOp, ComboSkillHit),
    DEMO_ID("effect_ops", "combo_knockback", ELDDemoEffectOp, ComboKnockback),
    DEMO_ID("effect_ops", "combo_double", ELDDemoEffectOp, ComboDouble),
    DEMO_ID("effect_ops", "overcap_extra_hit", ELDDemoEffectOp, OvercapExtraHit),
    DEMO_ID("effect_ops", "basic_ap_rider", ELDDemoEffectOp, BasicApRider),
    DEMO_ID("effect_ops", "focus_attack_speed", ELDDemoEffectOp, FocusAttackSpeed),
    DEMO_ID("effect_ops", "skill_hit_momentum", ELDDemoEffectOp, SkillHitMomentum),
    DEMO_ID("effect_ops", "auto_recast", ELDDemoEffectOp, AutoRecast),
    DEMO_ID("effect_ops", "skill_damage_factor", ELDDemoEffectOp, SkillDamageFactor),
    DEMO_ID("effect_ops", "hp_cost", ELDDemoEffectOp, HpCost),
    DEMO_ID("effect_ops", "skill_ad_rider", ELDDemoEffectOp, SkillAdRider),
    DEMO_ID("effect_ops", "skill_growth", ELDDemoEffectOp, SkillGrowth),
    DEMO_ID("effect_ops", "skill_element", ELDDemoEffectOp, SkillElement),
    DEMO_ID("effect_ops", "dash_contact_reset", ELDDemoEffectOp, DashContactReset),
    DEMO_ID("effect_ops", "dash_split", ELDDemoEffectOp, DashSplit),
    DEMO_ID("effect_ops", "dash_phase", ELDDemoEffectOp, DashPhase),
    DEMO_ID("effect_ops", "dash_contact_element", ELDDemoEffectOp, DashContactElement),
    DEMO_ID("effect_ops", "takedown_reset", ELDDemoEffectOp, TakedownReset),
    DEMO_ID("effect_ops", "last_stand", ELDDemoEffectOp, LastStand),
    DEMO_ID("effect_ops", "reaction_momentum", ELDDemoEffectOp, ReactionMomentum),
    DEMO_ID("effect_ops", "reaction_element", ELDDemoEffectOp, ReactionElement),
    DEMO_ID("effect_ops", "skill_crit", ELDDemoEffectOp, SkillCrit),
    DEMO_ID("effect_ops", "lifesteal_skills", ELDDemoEffectOp, LifestealSkills),
    DEMO_ID("effect_ops", "armor_to_hp", ELDDemoEffectOp, ArmorToHp),
    DEMO_ID("effect_ops", "overheal_shield", ELDDemoEffectOp, OverhealShield),
    DEMO_ID("effect_ops", "missing_hp_attack_speed", ELDDemoEffectOp, MissingHpAttackSpeed),
    DEMO_ID("effect_ops", "haste_momentum", ELDDemoEffectOp, HasteMomentum),
    DEMO_ID("effect_ops", "heal_growth", ELDDemoEffectOp, HealGrowth),
    DEMO_ID("effect_ops", "kill_growth", ELDDemoEffectOp, KillGrowth),
    DEMO_ID("effect_ops", "control_growth", ELDDemoEffectOp, ControlGrowth),
    DEMO_ID("effect_ops", "idle_charge", ELDDemoEffectOp, IdleCharge),
    DEMO_ID("effect_ops", "element_aura", ELDDemoEffectOp, ElementAura),
    DEMO_ID("effect_ops", "element_duration", ELDDemoEffectOp, ElementDuration),
    DEMO_ID("effect_ops", "reaction_factor", ELDDemoEffectOp, ReactionFactor),
    DEMO_ID("effect_ops", "burn_cap", ELDDemoEffectOp, BurnCap),
DEMO_ID("effect_ops", "cast_hit_refund", ELDDemoEffectOp, CastHitRefund),
DEMO_ID("effect_ops", "distance_amp", ELDDemoEffectOp, DistanceAmp),
DEMO_ID("effect_ops", "alternate_amp", ELDDemoEffectOp, AlternateAmp),
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

template <typename TNumber>
bool NativeIdentity(FFields& Fields, const TCHAR* Category, FString& Id, TNumber& Number)
{
    int64 RawNumber;
    if (!Fields.Identifier(TEXT("id"), Id) || !Fields.Int(TEXT("native_id"), RawNumber, 1, int64(TNumericLimits<TNumber>::Max())))
    {
        return false;
    }
    const FNativeId* Entry = Native(Category, Id);
    if (!Fields.Check.Require(Entry && RawNumber == Entry->Number, Fields.Path, TEXT("unknown, retired or renumbered native ID")))
    {
        return false;
    }
    Number = static_cast<TNumber>(RawNumber);
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
    // passives 104-113/120-125, cower 119, the attribute cards 15-26, statuses 28-29, effect op 36 and every operation without a consumer;
    // #287 P2 retires the workshop components 1-63 below the build cards).
    struct FRetiredRange { int64 First; int64 Last; };
    static const FRetiredRange RetiredAbilities[] = {{0, 91}, {104, 113}, {119, 125}};
    static const FRetiredRange RetiredStats[] = {{0, 26}};
    static const FRetiredRange RetiredStatuses[] = {{7, 8}, {11, 11}, {15, 19}, {21, 24}, {28, 29}};
    static const FRetiredRange RetiredCards[] = {{0, 199}};
    static const FRetiredRange RetiredEffectOps[] = {{5, 6}, {9, 11}, {15, 17}, {19, 19}, {23, 23}, {25, 30}, {32, 36}, {38, 39}, {41, 49}, {51, 51}, {53, 53}, {55, 68}, {72, 76}, {83, 89}, {100, 100}, {102, 102}, {104, 104}, {120, 129}, {131, 137}, {139, 145}, {147, 151}, {154, 154}, {159, 159}, {161, 161}, {165, 169}};
    FFields Retired(Check, Root.Take(TEXT("retired_ranges")), TEXT("native_ids.retired_ranges"));
    for (const auto& Pair : {TPair<const TCHAR*, TArrayView<const FRetiredRange>>(TEXT("abilities"), RetiredAbilities),
        {TEXT("stats"), RetiredStats}, {TEXT("cards"), RetiredCards}, {TEXT("statuses"), RetiredStatuses},
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
    for (const TCHAR* Category : {TEXT("character"), TEXT("abilities"), TEXT("stats"), TEXT("cards"), TEXT("final_mods"),
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

// A fixed-length array of ratios ({"unit": "ratio", "bp": n}), each at least Min.
bool RatioArray(FFields& F, const TCHAR* Key, int32 Count, int64 Min, TArray<int64>& Out, FCheck& Check)
{
    const TArray<FValue>* Rows = nullptr;
    const FString Path = F.Path + TEXT(".") + Key;
    if (!Array(F.Take(Key), Rows, Check, Path, Count, Count))
    {
        return false;
    }
    Out.Reset();
    for (int32 Index = 0; Index < Rows->Num(); ++Index)
    {
        FFields Row(Check, (*Rows)[Index], FString::Printf(TEXT("%s[%d]"), *Path, Index));
        int64 Bp = 0;
        if (!Row.Literal(TEXT("unit"), TEXT("ratio")) || !Row.Int(TEXT("bp"), Bp, Min) || !Row.Done())
        {
            return false;
        }
        Out.Add(Bp);
    }
    return true;
}

// Momentum, combo, overdrive and skill rank parameters (CONTENT_AND_BALANCE §3.3-§3.6).
bool CoreParameters(FFields& F, FLDDemoContentParameters& P, FCheck& Check)
{
    FLDDemoMomentumParameters& M = P.Momentum;
    FLDDemoOverdriveParameters& O = P.Overdrive;
    struct FFixedField { const TCHAR* Key; const TCHAR* Unit; const TCHAR* Storage; int64* Out; int64 Min; int64 Max; };
    const FFixedField Fixed[] =
    {
        {TEXT("MOMENTUM.Max"), TEXT("momentum"), TEXT("p2"), &M.MaxP2, 1, MAX_int64},
        {TEXT("MOMENTUM.Start"), TEXT("momentum"), TEXT("p2"), &M.StartP2, 1, MAX_int64},
        {TEXT("MOMENTUM.BasicPerRefInterval"), TEXT("momentum"), TEXT("p2"), &M.BasicPerRefP2, 1, MAX_int64},
        {TEXT("MOMENTUM.BasicRefIntervalSec"), TEXT("ms"), TEXT("ms"), &M.BasicRefIntervalMs, 1, MAX_int64},
        {TEXT("MOMENTUM.SkillHit"), TEXT("momentum"), TEXT("p2"), &M.SkillHitP2, 1, MAX_int64},
        {TEXT("MOMENTUM.Reaction"), TEXT("momentum"), TEXT("p2"), &M.ReactionP2, 1, MAX_int64},
        {TEXT("MOMENTUM.PerfectDodge"), TEXT("momentum"), TEXT("p2"), &M.PerfectDodgeP2, 1, MAX_int64},
        {TEXT("MOMENTUM.PerfectDodgeWindowSec"), TEXT("ms"), TEXT("ms"), &M.PerfectDodgeWindowMs, 1, MAX_int64},
        {TEXT("MOMENTUM.IdleDelaySec"), TEXT("ms"), TEXT("ms"), &M.IdleDelayMs, 1, MAX_int64},
        {TEXT("COMBO.ResetSec"), TEXT("ms"), TEXT("ms"), &P.Combo.ResetMs, 1, MAX_int64},
        {TEXT("OVERDRIVE.Threshold"), TEXT("momentum"), TEXT("p2"), &O.ThresholdP2, 1, MAX_int64},
        {TEXT("OVERDRIVE.HoldSec"), TEXT("ms"), TEXT("ms"), &O.HoldMs, 1, MAX_int64},
        {TEXT("OVERDRIVE.Scale"), TEXT("ratio"), TEXT("bp"), &O.ScaleBp, 10000, MAX_int64},
        {TEXT("OVERDRIVE.WaterSlow"), TEXT("ratio"), TEXT("bp"), &O.WaterSlowBp, 1, 9999},
        {TEXT("OVERDRIVE.WaterSlowSec"), TEXT("ms"), TEXT("ms"), &O.WaterSlowMs, 1, MAX_int64},
        {TEXT("OVERDRIVE.GustPushSec"), TEXT("ms"), TEXT("ms"), &O.GustPushMs, 1, MAX_int64},
        {TEXT("OVERDRIVE.ShockReactionRatio"), TEXT("ratio"), TEXT("bp"), &O.ShockReactionBp, 1, MAX_int64},
        {TEXT("OVERDRIVE.RotDurationScale"), TEXT("ratio"), TEXT("bp"), &O.RotDurationBp, 10000, MAX_int64},
        {TEXT("SKILL.CooldownFloorSec"), TEXT("ms"), TEXT("ms"), &P.SkillRanks.CooldownFloorMs, 1, MAX_int64},
        {TEXT("AIRDROP.Momentum"), TEXT("momentum"), TEXT("p2"), &P.AirdropMomentumP2, 1, MAX_int64},
        {TEXT("AI.DodgeChance"), TEXT("ratio"), TEXT("bp"), &P.AI.DodgeChanceBp, 0, 10000},
        {TEXT("AI.BreakoutChance"), TEXT("ratio"), TEXT("bp"), &P.AI.BreakoutChanceBp, 0, 10000},
        {TEXT("DAMAGE.FactorTechCap"), TEXT("ratio"), TEXT("bp"), &P.DamageFactorTechCapBp, 10001, MAX_int64},
    };
    for (const auto& Field : Fixed)
    {
        if (!F.Fixed(Field.Key, Field.Unit, Field.Storage, *Field.Out, Field.Min, Field.Max))
        {
            return false;
        }
    }
    if (!F.Fraction(TEXT("MOMENTUM.CombatPerSec"), TEXT("momentum/s"), M.CombatPerSec, true)
        || !F.Fraction(TEXT("MOMENTUM.IdleReturnPerSec"), TEXT("momentum/s"), M.IdleReturnPerSec, true)
        || !F.Fraction(TEXT("OVERDRIVE.GustPushCm"), TEXT("cm"), O.GustPushCm, true)
        || !F.Int(TEXT("OVERDRIVE.FireBurnStacks"), O.FireBurnStacks, 1)
        || !RatioArray(F, TEXT("COMBO.StepRatios"), 3, 1, P.Combo.StepRatiosBp, Check)
        || !RatioArray(F, TEXT("SKILL.RankBaseScale"), 3, 10000, P.SkillRanks.RankBaseScaleBp, Check)
        || !RatioArray(F, TEXT("SKILL.RankRatioScale"), 3, 10000, P.SkillRanks.RankRatioScaleBp, Check))
    {
        return false;
    }
    const TArray<FValue>* Levels = nullptr;
    if (!Array(F.Take(TEXT("SKILL.PointLevels")), Levels, Check, F.Path + TEXT(".SKILL.PointLevels"), 1, 15))
    {
        return false;
    }
    P.SkillRanks.PointLevels.Reset();
    for (const auto& Row : *Levels)
    {
        int64 Level = 0;
        if (!Integer(Row, Level, Check, F.Path + TEXT(".SKILL.PointLevels"), 2, LDDemoProgression::MaxLevel)
            || !Check.Require(P.SkillRanks.PointLevels.IsEmpty() || Level > P.SkillRanks.PointLevels.Last(), F.Path,
                TEXT("skill point levels must be strictly ascending")))
        {
            return false;
        }
        P.SkillRanks.PointLevels.Add(Level);
    }
    const auto& Base = P.SkillRanks.RankBaseScaleBp;
    const auto& Ratio = P.SkillRanks.RankRatioScaleBp;
    return Check.Require(M.StartP2 <= M.MaxP2 && O.ThresholdP2 <= M.MaxP2, F.Path,
            TEXT("the momentum start and the overdrive threshold must fit the bar"))
        && Check.Require(Base[0] == 10000 && Ratio[0] == 10000 && Base[0] <= Base[1] && Base[1] <= Base[2]
            && Ratio[0] <= Ratio[1] && Ratio[1] <= Ratio[2], F.Path, TEXT("rank 1 is the authored set and ranks never weaken it"));
}

bool Parameters(const FValue& Value, FLDDemoContentParameters& P, FCheck& Check)
{
    FFields F(Check, Value, TEXT("content.parameters"));
    if (!P2Bounds(F, TEXT("MaxHP"), TEXT("hp"), P.MaxHP)
        || !P2Bounds(F, TEXT("AD"), TEXT("points"), P.AD)
        || !P2Bounds(F, TEXT("AP"), TEXT("points"), P.AP)
        || !P2Bounds(F, TEXT("Armor"), TEXT("points"), P.Armor)
        || !P2Bounds(F, TEXT("MagicResist"), TEXT("points"), P.MagicResist)
        || !RateBounds(F, TEXT("HPRegen"), TEXT("hp/s"), P.HPRegen)
        || !CoreParameters(F, P, Check))
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
        {TEXT("UNIT.BasicAttackWindupSec"), TEXT("ms"), TEXT("ms"), &P.BasicAttackWindupMs, 1, MAX_int64},
        {TEXT("CAPTURE.SpraySec"), TEXT("ms"), TEXT("ms"), &P.CaptureSprayMs, 1, MAX_int64},
        {TEXT("CAPTURE.GuardSec"), TEXT("ms"), TEXT("ms"), &P.CaptureGuardMs, 1, MAX_int64},
        {TEXT("SHOWDOWN.UnlockDelaySec"), TEXT("ms"), TEXT("ms"), &P.ShowdownUnlockDelayMs, 0, MAX_int32},
        {TEXT("SHOWDOWN.DurationSec"), TEXT("ms"), TEXT("ms"), &P.ShowdownDurationMs, 1, MAX_int32},
        {TEXT("SHOWDOWN.FirstRespawnSec"), TEXT("ms"), TEXT("ms"), &P.ShowdownFirstRespawnMs, 1, MAX_int32},
        {TEXT("SHOWDOWN.SecondRespawnSec"), TEXT("ms"), TEXT("ms"), &P.ShowdownSecondRespawnMs, 1, MAX_int32},
        {TEXT("SHOWDOWN.RespawnCutoffSec"), TEXT("ms"), TEXT("ms"), &P.ShowdownRespawnCutoffMs, 1, MAX_int32},
        {TEXT("SHOWDOWN.ClosureStartsSec"), TEXT("ms"), TEXT("ms"), &P.ShowdownClosureStartsMs, 0, MAX_int32},
        {TEXT("MATCH.FieldSegmentSec"), TEXT("ms"), TEXT("ms"), &P.FieldSegmentMs, 1, MAX_int64},
        {TEXT("REST.OpeningBuildSec"), TEXT("ms"), TEXT("ms"), &P.RestOpeningBuildMs, 1, MAX_int64},
        {TEXT("REST.BuildSec"), TEXT("ms"), TEXT("ms"), &P.RestBuildMs, 1, MAX_int64},
        {TEXT("REST.SignupSec"), TEXT("ms"), TEXT("ms"), &P.RestSignupMs, 1, MAX_int64},
        {TEXT("REST.DrawSec"), TEXT("ms"), TEXT("ms"), &P.RestDrawMs, 1, MAX_int64},
        {TEXT("REST.BettingSec"), TEXT("ms"), TEXT("ms"), &P.RestBettingMs, 1, MAX_int64},
        {TEXT("REST.PKReadySec"), TEXT("ms"), TEXT("ms"), &P.RestPKReadyMs, 1, MAX_int64},
        {TEXT("REST.PKMaxSec"), TEXT("ms"), TEXT("ms"), &P.RestPKMaxMs, 1, MAX_int64},
        {TEXT("REST.SettlementSec"), TEXT("ms"), TEXT("ms"), &P.RestSettlementMs, 1, MAX_int64},
        {TEXT("REST.PayoutSec"), TEXT("ms"), TEXT("ms"), &P.RestPayoutMs, 1, MAX_int64},
        {TEXT("REST.ReturnSec"), TEXT("ms"), TEXT("ms"), &P.RestReturnMs, 1, MAX_int64},
        {TEXT("REST.EntryReadyTimeoutSec"), TEXT("ms"), TEXT("ms"), &P.RestEntryReadyTimeoutMs, 1, MAX_int64},
        {TEXT("PK.RingShrinkStartSec"), TEXT("ms"), TEXT("ms"), &P.PKRingShrinkStartMs, 1, MAX_int64},
        {TEXT("PK.RingMidSec"), TEXT("ms"), TEXT("ms"), &P.PKRingMidMs, 1, MAX_int64},
        {TEXT("PK.RingFinalSec"), TEXT("ms"), TEXT("ms"), &P.PKRingFinalMs, 1, MAX_int64},
        {TEXT("PK.RingPreviewSec"), TEXT("ms"), TEXT("ms"), &P.PKRingPreviewMs, 1, MAX_int64},
        {TEXT("PK.RingDamageTickSec"), TEXT("ms"), TEXT("ms"), &P.PKRingTickMs, 1, MAX_int64},
        {TEXT("PK.RingDamageEarlyMaxHPRatioPerSec"), TEXT("ratio"), TEXT("bp"), &P.PKRingEarlyDamageBp, 1, 10000},
        {TEXT("PK.RingDamageMidMaxHPRatioPerSec"), TEXT("ratio"), TEXT("bp"), &P.PKRingMidDamageBp, 1, 10000},
        {TEXT("PK.RingDamageFinalMaxHPRatioPerSec"), TEXT("ratio"), TEXT("bp"), &P.PKRingFinalDamageBp, 1, 10000},
        {TEXT("PK.TeamFormatRatio"), TEXT("ratio"), TEXT("bp"), &P.PKTeamFormatBp, 0, 10000},
        {TEXT("PK.RewardMultiplier"), TEXT("ratio"), TEXT("bp"), &P.PKRewardMultiplierBp, 10000, MAX_int64},
        {TEXT("THROW.CooldownSec"), TEXT("ms"), TEXT("ms"), &P.ThrowCooldownMs, 1, MAX_int64},
        {TEXT("THROW.TelegraphSec"), TEXT("ms"), TEXT("ms"), &P.ThrowTelegraphMs, 1, MAX_int64},
        {TEXT("THROW.SlowRatio"), TEXT("ratio"), TEXT("bp"), &P.ThrowSlowBp, 1, 10000},
        {TEXT("THROW.SlowSec"), TEXT("ms"), TEXT("ms"), &P.ThrowSlowMs, 1, MAX_int64},
        {TEXT("THROW.ExposeSec"), TEXT("ms"), TEXT("ms"), &P.ThrowExposeMs, 1, MAX_int64},
        {TEXT("LIFE.RespawnSec.Field1"), TEXT("ms"), TEXT("ms"), &P.RespawnField1Ms, 1, MAX_int64},
        {TEXT("LIFE.RespawnSec.Field2"), TEXT("ms"), TEXT("ms"), &P.RespawnField2Ms, 1, MAX_int64},
        {TEXT("LIFE.RespawnSec.Field3"), TEXT("ms"), TEXT("ms"), &P.RespawnField3Ms, 1, MAX_int64},
        {TEXT("LIFE.ProtectionSec"), TEXT("ms"), TEXT("ms"), &P.ProtectionMs, 0, MAX_int64},
        {TEXT("REGEN.OutOfCombatDelaySec"), TEXT("ms"), TEXT("ms"), &P.OutOfCombatRegenDelayMs, 1, MAX_int64},
        {TEXT("REGEN.OutOfCombatMaxHPRatioPerSec"), TEXT("ratio"), TEXT("bp"), &P.OutOfCombatMaxHPRegenBp, 0, 10000},
        {TEXT("DORM.OutOfCombatDelaySec"), TEXT("ms"), TEXT("ms"), &P.DormRegenDelayMs, 1, MAX_int64},
        {TEXT("DORM.MaxHPRatioPerSec"), TEXT("ratio"), TEXT("bp"), &P.DormMaxHPRegenBp, 0, 10000},
        {TEXT("CAMP.ReturnDeadEndSec"), TEXT("ms"), TEXT("ms"), &P.CampReturnDeadEndMs, 1, MAX_int64},
        {TEXT("CAMP.GrowthStageSec"), TEXT("ms"), TEXT("ms"), &P.CampGrowthStageMs, 1, MAX_int64},
        {TEXT("CAMP.GrowthHPRatioPerStage"), TEXT("ratio"), TEXT("bp"), &P.CampGrowthHPPerStageBp, 0, MAX_int64},
        {TEXT("CAMP.GrowthAttackRatioPerStage"), TEXT("ratio"), TEXT("bp"), &P.CampGrowthAttackPerStageBp, 0, MAX_int64},
        {TEXT("CAMP.GrowthResistPerStage"), TEXT("points"), TEXT("p2"), &P.CampGrowthResistPerStageP2, 0, MAX_int64},
        {TEXT("CAMP.GrowthRewardXPRatioPerStage"), TEXT("ratio"), TEXT("bp"), &P.CampGrowthRewardXPPerStageBp, 0, MAX_int64},
        {TEXT("AIRDROP.ExpireSec"), TEXT("ms"), TEXT("ms"), &P.AirdropExpireMs, 1, MAX_int64},
        {TEXT("AIRDROP.AnnounceLeadSec"), TEXT("ms"), TEXT("ms"), &P.AirdropAnnounceLeadMs, 1, MAX_int64},
        {TEXT("AIRDROP.HealMaxHPRatio"), TEXT("ratio"), TEXT("bp"), &P.AirdropHealMaxHPBp, 1, 10000},
        {TEXT("AIRDROP.MoveSpeedRatio"), TEXT("ratio"), TEXT("bp"), &P.AirdropMoveSpeedBp, 1, 10000},
        {TEXT("AIRDROP.MoveSpeedSec"), TEXT("ms"), TEXT("ms"), &P.AirdropMoveSpeedMs, 1, MAX_int64},
        {TEXT("CAMPMARK.RevealSec"), TEXT("ms"), TEXT("ms"), &P.CampMarkRevealMs, 1, MAX_int64},
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
        {TEXT("COMBAT.KeystonePhysiqueHealToShieldRatio"), TEXT("ratio"), TEXT("bp"), &P.KeystonePhysiqueHealToShieldBp, 1, 10000},
        {TEXT("COMBAT.KeystonePhysiqueShieldMaxHPRatio"), TEXT("ratio"), TEXT("bp"), &P.KeystonePhysiqueShieldMaxHPBp, 1, 10000},
        {TEXT("COMBAT.KeystonePhysiqueShieldDurationSec"), TEXT("ms"), TEXT("ms"), &P.KeystonePhysiqueShieldDurationMs, 1, MAX_int64},
        {TEXT("COMBAT.KeystonePhysiqueIntervalSec"), TEXT("ms"), TEXT("ms"), &P.KeystonePhysiqueIntervalMs, 1, MAX_int64},
        {TEXT("COMBAT.KeystoneThickSkinGuardPoints"), TEXT("points"), TEXT("p2"), &P.KeystoneThickSkinGuardP2, 1, MAX_int64},
        {TEXT("COMBAT.KeystoneThickSkinGuardCapRatio"), TEXT("ratio"), TEXT("bp"), &P.KeystoneThickSkinGuardCapBp, 1, 9999},
        {TEXT("COMBAT.KeystoneThickSkinIntervalSec"), TEXT("ms"), TEXT("ms"), &P.KeystoneThickSkinIntervalMs, 1, MAX_int64},
        {TEXT("COMBAT.KeystoneStrengthDamageRatio"), TEXT("ratio"), TEXT("bp"), &P.KeystoneStrengthDamageBp, 1, 10000},
        {TEXT("COMBAT.KeystoneBrainsMomentumRefundRatio"), TEXT("ratio"), TEXT("bp"), &P.KeystoneBrainsMomentumRefundBp, 1, 10000},
        {TEXT("COMBAT.KeystoneAgilityDamageBase"), TEXT("points"), TEXT("p2"), &P.KeystoneAgilityDamageBaseP2, 1, MAX_int64},
        {TEXT("COMBAT.KeystoneAgilityADRatio"), TEXT("ratio"), TEXT("bp"), &P.KeystoneAgilityADRatioBp, 1, 10000},
        {TEXT("COMBAT.KeystoneAgilityEmpowerDurationSec"), TEXT("ms"), TEXT("ms"), &P.KeystoneAgilityEmpowerDurationMs, 1, MAX_int64},
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
    // #198 outer §5: the approved airdrop landing instants (at most the room's six drops).
    const TArray<FValue>* Lands = nullptr;
    const FString LandPath = F.Path + TEXT(".AIRDROP.LandSec");
    if (!Array(F.Take(TEXT("AIRDROP.LandSec")), Lands, Check, LandPath, 1, 6))
    {
        return false;
    }
    P.AirdropLandMs.Reset();
    for (int32 Index = 0; Index < Lands->Num(); ++Index)
    {
        FFields Land(Check, (*Lands)[Index], FString::Printf(TEXT("%s[%d]"), *LandPath, Index));
        int64 LandMs = 0;
        if (!Land.Literal(TEXT("unit"), TEXT("ms")) || !Land.Int(TEXT("ms"), LandMs, 1) || !Land.Done())
        {
            return false;
        }
        P.AirdropLandMs.Add(LandMs);
    }
    struct FCmField { const TCHAR* Key; FLDDemoRational* Out; };
    for (const auto& Field : {
        FCmField{TEXT("UNIT.CollisionRadiusCm"), &P.CollisionRadiusCm},
        {TEXT("VISION.RadiusCm"), &P.VisionRadiusCm},
        {TEXT("CAPTURE.RadiusCm"), &P.CaptureRadiusCm}, {TEXT("CAMP.ResetArrivalRadiusCm"), &P.CampResetArrivalRadiusCm},
        {TEXT("DORM.RadiusCm"), &P.DormRadiusCm}, {TEXT("AIRDROP.PickupRadiusCm"), &P.AirdropPickupRadiusCm},
        {TEXT("LEADER.CircleRadiusCm"), &P.LeaderCircleRadiusCm}, {TEXT("LEADER.CircleMaxOffsetCm"), &P.LeaderCircleMaxOffsetCm},
        {TEXT("COMBAT.DischargeRadiusCm"), &P.DischargeRadiusCm}, {TEXT("COMBAT.FogRadiusCm"), &P.FogRadiusCm},
        {TEXT("COMBAT.ConductRadiusCm"), &P.ConductRadiusCm}, {TEXT("COMBAT.OverloadRadiusCm"), &P.OverloadRadiusCm},
        {TEXT("REST.ReturnSearchRadiusCm"), &P.RestReturnSearchRadiusCm},
        {TEXT("PK.ArenaRadiusCm"), &P.PKArenaRadiusCm}, {TEXT("PK.RingMidRadiusCm"), &P.PKRingMidRadiusCm},
        {TEXT("PK.RingFinalRadiusCm"), &P.PKRingFinalRadiusCm},
        {TEXT("THROW.RadiusCm"), &P.ThrowRadiusCm}, {TEXT("THROW.RangeCm"), &P.ThrowRangeCm}})
    {
        if (!F.Fraction(Field.Key, TEXT("cm"), *Field.Out, true))
        {
            return false;
        }
    }
    struct FCountField { const TCHAR* Key; int64* Out; int64 Min; };
    for (const auto& Field : {
        FCountField{TEXT("XP.BaseToNext"), &P.XPBaseToNext, 1}, {TEXT("XP.IncreasePerLevel"), &P.XPIncreasePerLevel, 0},
        {TEXT("SHOWDOWN.RespawnLimit"), &P.ShowdownRespawnLimit, 0},
        {TEXT("REWARD.PlayerXP.Base"), &P.PlayerXPBase, 0}, {TEXT("REWARD.PlayerXP.PerLevel"), &P.PlayerXPPerLevel, 0},
        {TEXT("AI.RetryPolicy.MaxAttempts"), &P.AI.RetryMaxAttempts, 1},
        {TEXT("CAMP.ReturnDeadEndPlans"), &P.CampReturnDeadEndPlans, 1},
        {TEXT("CAMP.GrowthMaxStage"), &P.CampGrowthMaxStage, 1},
        {TEXT("AIRDROP.RewardXP"), &P.AirdropRewardXP, 1}, {TEXT("LEADER.MinLevel"), &P.LeaderMinLevel, 1},
        {TEXT("COMBAT.BreachMaxStacks"), &P.BreachMaxStacks, 1}, {TEXT("COMBAT.BurnMaxStacks"), &P.BurnMaxStacks, 1},
        {TEXT("GROWTH.OpeningPoints"), &P.OpeningPoints, 1}, {TEXT("GROWTH.PointsPerLevel"), &P.PointsPerLevel, 1},
        {TEXT("GROWTH.MaxLevelPointsXP"), &P.MaxLevelPointsXP, 1}, {TEXT("GROWTH.MaxLevelPoints"), &P.MaxLevelPoints, 1},
        {TEXT("GROWTH.KeystoneThresholdPoints"), &P.KeystoneThresholdPoints, 1},
        {TEXT("COMBAT.OverloadBreachStacks"), &P.OverloadBreachStacks, 1},
        {TEXT("GROWTH.NpcActivePoints"), &P.NpcActivePoints, 1},
        {TEXT("GROWTH.ActiveSkipPoints"), &P.ActiveSkipPoints, 1}, {TEXT("GROWTH.NpcActiveSkipPoints"), &P.NpcActiveSkipPoints, 1},
        {TEXT("AI.CardVocabScore"), &P.AI.CardVocabScore, 1}, {TEXT("AI.BreakoutCardScore"), &P.AI.BreakoutCardScore, 1},
        {TEXT("PK.ParticipationPoints"), &P.PKParticipationPoints, 1}, {TEXT("PK.WinnerPointsMax"), &P.PKWinnerPointsMax, 1},
        {TEXT("PK.BettingPoolMaxPoints"), &P.PKBettingPoolMaxPoints, 1}})
    {
        if (!F.Int(Field.Key, *Field.Out, Field.Min))
        {
            return false;
        }
    }
    // Spec §3.12 card supply and §3.8 chain guard (small counts).
    for (const auto& Field : {
        TPair<const TCHAR*, int32*>(TEXT("CARDS.CapacityBase"), &P.Cards.CapacityBase), {TEXT("CARDS.LevelsPerSlot"), &P.Cards.LevelsPerSlot},
        {TEXT("CARDS.OfferSize"), &P.Cards.OfferSize}, {TEXT("CARDS.FreeRerolls"), &P.Cards.FreeRerolls},
        {TEXT("CARDS.RerollsPerRest"), &P.Cards.RerollsPerRest}, {TEXT("CARDS.LockSlots"), &P.Cards.LockSlots},
        {TEXT("CARDS.VocabWeight"), &P.Cards.VocabWeight}, {TEXT("CHAIN.MaxDepth"), &P.ChainMaxDepth},
        {TEXT("CHAIN.MaxEffectsPerStep"), &P.ChainMaxEffectsPerStep}})
    {
        const bool bMayBeZero = FCString::Strcmp(Field.Key, TEXT("CARDS.FreeRerolls")) == 0
            || FCString::Strcmp(Field.Key, TEXT("CARDS.RerollsPerRest")) == 0 || FCString::Strcmp(Field.Key, TEXT("CARDS.LockSlots")) == 0;
        if (!F.SmallInt(Field.Key, *Field.Value, bMayBeZero ? 0 : 1, 64))
        {
            return false;
        }
    }
    if (!Check.Require(P.Cards.LockSlots < P.Cards.OfferSize, TEXT("CARDS.LockSlots"), TEXT("a locked card leaves room for new ones")))
    {
        return false;
    }
    FLDShowdownParameters Showdown;
    FString ShowdownError;
    if (!FLDShowdownParameters::FromContent(P, Showdown, ShowdownError))
    { return Check.Require(false, F.Path, *ShowdownError); }
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
        DEMO_POLICY(CampMomentumPolicy, NotApplicable),
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
    {TEXT("MomentumRegenFlat"), ELDDemoBonusStat::MomentumRegenFlat, TEXT("momentum/s"), nullptr},
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
// Effect opcodes are uint8 and reach past 127 (#198 F), so the per-definition operation set is a 256-bit mask.
struct FOpMask
{
    uint64 Words[4] = {0, 0, 0, 0};
    constexpr FOpMask operator|(const FOpMask& Other) const
    { return {{Words[0] | Other.Words[0], Words[1] | Other.Words[1], Words[2] | Other.Words[2], Words[3] | Other.Words[3]}}; }
    constexpr FOpMask operator&(const FOpMask& Other) const
    { return {{Words[0] & Other.Words[0], Words[1] & Other.Words[1], Words[2] & Other.Words[2], Words[3] & Other.Words[3]}}; }
    constexpr FOpMask operator~() const { return {{~Words[0], ~Words[1], ~Words[2], ~Words[3]}}; }
    FOpMask& operator|=(const FOpMask& Other) { *this = *this | Other; return *this; }
    constexpr bool operator==(const FOpMask& Other) const
    { return Words[0] == Other.Words[0] && Words[1] == Other.Words[1] && Words[2] == Other.Words[2] && Words[3] == Other.Words[3]; }
    constexpr bool operator!=(const FOpMask& Other) const { return !(*this == Other); }
    constexpr bool Any() const { return (Words[0] | Words[1] | Words[2] | Words[3]) != 0; }
};
constexpr FOpMask OpBit(ELDDemoEffectOp Op)
{
    FOpMask Out;
    Out.Words[uint8(Op) / 64] = uint64(1) << (uint8(Op) % 64);
    return Out;
}

// Temporary and conditional bonuses never change resource maxima or regeneration (no conversion exists for them).
constexpr uint32 TemporaryStats = ~(Bit(ELDDemoBonusStat::MaxHPFlat) | Bit(ELDDemoBonusStat::HPRegenFlat)
    | Bit(ELDDemoBonusStat::MomentumRegenFlat));

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

bool OptionalFlag(FFields& F, const TCHAR* Key, bool& Out)
{
    return !F.Has(Key) || F.Flag(Key, Out);
}

bool OptionalDamageType(FFields& F, ELDDemoDamageType& Out, FCheck& Check, const FString& Path)
{
    FString Type;
    if (!F.Has(TEXT("damage_type"))) { return true; }
    if (!F.Text(TEXT("damage_type"), Type)
        || !Check.Require(Type.Equals(TEXT("Physical"), ESearchCase::CaseSensitive) || Type.Equals(TEXT("Magical"), ESearchCase::CaseSensitive),
            Path, TEXT("unknown damage type"))) { return false; }
    Out = Type.Equals(TEXT("Physical"), ESearchCase::CaseSensitive) ? ELDDemoDamageType::Physical : ELDDemoDamageType::Magical;
    return true;
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

// Spec §7: the operations only a card carries (DemoContent.CARD_ONLY_OPS); burn, breach and empower_basic also have an
// active form.
constexpr FOpMask CardOnlyOps = OpBit(ELDDemoEffectOp::Thorns) | OpBit(ELDDemoEffectOp::Lifesteal) | OpBit(ELDDemoEffectOp::TriggeredShield) |
    OpBit(ELDDemoEffectOp::BasicElement) | OpBit(ELDDemoEffectOp::IdleReadyAmp) |
    OpBit(ELDDemoEffectOp::CooldownCost) | OpBit(ELDDemoEffectOp::KnockbackImpact) |
    OpBit(ELDDemoEffectOp::SkillLifesteal) | OpBit(ELDDemoEffectOp::HitHasteOther) |
    OpBit(ELDDemoEffectOp::SetupMark) | OpBit(ELDDemoEffectOp::ReactionHaste) |
    OpBit(ELDDemoEffectOp::ComboMomentum) | OpBit(ELDDemoEffectOp::BasicCooldownCut) |
    OpBit(ELDDemoEffectOp::ComboSkillHit) | OpBit(ELDDemoEffectOp::ComboKnockback) |
    OpBit(ELDDemoEffectOp::ComboDouble) | OpBit(ELDDemoEffectOp::OvercapExtraHit) |
    OpBit(ELDDemoEffectOp::BasicApRider) | OpBit(ELDDemoEffectOp::FocusAttackSpeed) |
    OpBit(ELDDemoEffectOp::SkillHitMomentum) | OpBit(ELDDemoEffectOp::AutoRecast) |
    OpBit(ELDDemoEffectOp::SkillDamageFactor) | OpBit(ELDDemoEffectOp::HpCost) |
    OpBit(ELDDemoEffectOp::SkillAdRider) | OpBit(ELDDemoEffectOp::SkillGrowth) |
    OpBit(ELDDemoEffectOp::SkillElement) | OpBit(ELDDemoEffectOp::DashContactReset) |
    OpBit(ELDDemoEffectOp::DashSplit) | OpBit(ELDDemoEffectOp::DashPhase) |
    OpBit(ELDDemoEffectOp::DashContactElement) | OpBit(ELDDemoEffectOp::TakedownReset) |
    OpBit(ELDDemoEffectOp::LastStand) | OpBit(ELDDemoEffectOp::ReactionMomentum) |
    OpBit(ELDDemoEffectOp::ReactionElement) | OpBit(ELDDemoEffectOp::SkillCrit) |
    OpBit(ELDDemoEffectOp::LifestealSkills) | OpBit(ELDDemoEffectOp::ArmorToHp) |
    OpBit(ELDDemoEffectOp::OverhealShield) | OpBit(ELDDemoEffectOp::MissingHpAttackSpeed) |
    OpBit(ELDDemoEffectOp::HasteMomentum) | OpBit(ELDDemoEffectOp::HealGrowth) |
    OpBit(ELDDemoEffectOp::KillGrowth) | OpBit(ELDDemoEffectOp::ControlGrowth) |
    OpBit(ELDDemoEffectOp::IdleCharge) | OpBit(ELDDemoEffectOp::ElementAura) |
    OpBit(ELDDemoEffectOp::ElementDuration) | OpBit(ELDDemoEffectOp::ReactionFactor) |
    OpBit(ELDDemoEffectOp::BurnCap);
constexpr FOpMask CardOps = CardOnlyOps | OpBit(ELDDemoEffectOp::Burn) | OpBit(ELDDemoEffectOp::Breach) | OpBit(ELDDemoEffectOp::EmpowerBasic);
// The operations each card form may carry (DemoContent.FORM_OPS), indexed by ELDCardForm.
constexpr FOpMask FormOps[] = {
    OpBit(ELDDemoEffectOp::Burn) | OpBit(ELDDemoEffectOp::Breach) | OpBit(ELDDemoEffectOp::BasicElement) |
    OpBit(ELDDemoEffectOp::ComboMomentum) | OpBit(ELDDemoEffectOp::BasicCooldownCut) |
    OpBit(ELDDemoEffectOp::ComboSkillHit) | OpBit(ELDDemoEffectOp::ComboKnockback) |
    OpBit(ELDDemoEffectOp::ComboDouble) | OpBit(ELDDemoEffectOp::OvercapExtraHit) |
    OpBit(ELDDemoEffectOp::BasicApRider) | OpBit(ELDDemoEffectOp::FocusAttackSpeed),
    OpBit(ELDDemoEffectOp::HitHasteOther) | OpBit(ELDDemoEffectOp::SkillHitMomentum) |
    OpBit(ELDDemoEffectOp::SetupMark) | OpBit(ELDDemoEffectOp::AutoRecast) | OpBit(ELDDemoEffectOp::CooldownCost) |
    OpBit(ELDDemoEffectOp::SkillDamageFactor) | OpBit(ELDDemoEffectOp::HpCost) |
    OpBit(ELDDemoEffectOp::SkillAdRider) | OpBit(ELDDemoEffectOp::SkillLifesteal) |
    OpBit(ELDDemoEffectOp::IdleReadyAmp) | OpBit(ELDDemoEffectOp::SkillGrowth) |
    OpBit(ELDDemoEffectOp::SkillElement) | OpBit(ELDDemoEffectOp::ReactionHaste),
    OpBit(ELDDemoEffectOp::DashContactReset) | OpBit(ELDDemoEffectOp::HitHasteOther) |
    OpBit(ELDDemoEffectOp::EmpowerBasic) | OpBit(ELDDemoEffectOp::DashSplit) | OpBit(ELDDemoEffectOp::DashPhase) |
    OpBit(ELDDemoEffectOp::DashContactElement),
    OpBit(ELDDemoEffectOp::Thorns) | OpBit(ELDDemoEffectOp::TakedownReset) | OpBit(ELDDemoEffectOp::LastStand) |
    OpBit(ELDDemoEffectOp::ReactionMomentum) | OpBit(ELDDemoEffectOp::ReactionElement) |
    OpBit(ELDDemoEffectOp::TriggeredShield) | OpBit(ELDDemoEffectOp::KnockbackImpact) |
    OpBit(ELDDemoEffectOp::SkillCrit) | OpBit(ELDDemoEffectOp::LifestealSkills) |
    OpBit(ELDDemoEffectOp::ArmorToHp) | OpBit(ELDDemoEffectOp::OverhealShield) |
    OpBit(ELDDemoEffectOp::MissingHpAttackSpeed) | OpBit(ELDDemoEffectOp::HasteMomentum) |
    OpBit(ELDDemoEffectOp::Lifesteal) | OpBit(ELDDemoEffectOp::HealGrowth) | OpBit(ELDDemoEffectOp::KillGrowth) |
    OpBit(ELDDemoEffectOp::ControlGrowth) | OpBit(ELDDemoEffectOp::IdleCharge) |
    OpBit(ELDDemoEffectOp::ElementAura) | OpBit(ELDDemoEffectOp::ElementDuration) |
    OpBit(ELDDemoEffectOp::ReactionFactor) | OpBit(ELDDemoEffectOp::BurnCap),
};
// Workshop mod operations a skill branch may also give an active (DemoContent.BRANCH_MOD_OPS).
constexpr FOpMask BranchModOps = OpBit(ELDDemoEffectOp::Pull) | OpBit(ELDDemoEffectOp::ProjectileReturn)
    | OpBit(ELDDemoEffectOp::ProjectilePierce) | OpBit(ELDDemoEffectOp::EchoImpact) | OpBit(ELDDemoEffectOp::ZoneTether)
    | OpBit(ELDDemoEffectOp::FinalKnockback) | OpBit(ELDDemoEffectOp::EmpowerReach);

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

// bCard: the effect belongs to a card (spec §7) and takes the card form of its operation.
bool Effect(const FValue& Value, FLDDemoEffectDefinition& Out, bool bCard, FCheck& Check, const FString& Path)
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
    if (!Check.Require(bCard ? (CardOps & OpFlag).Any() : !(CardOnlyOps & OpFlag).Any(), Path,
        bCard ? TEXT("not a card operation") : TEXT("an active cannot carry a card operation")))
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
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !OptionalFixed(F, TEXT("ghost"), TEXT("ms"), TEXT("ms"), Out.ExtraDurationMs, 1))
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
    {
        FString Type;
        if (!F.Fixed(TEXT("base"), TEXT("points"), TEXT("p2"), Out.BaseP2)
            || !F.Text(TEXT("damage_type"), Type)
            || !Check.Require(Type.Equals(TEXT("Physical"), ESearchCase::CaseSensitive)
                || Type.Equals(TEXT("Magical"), ESearchCase::CaseSensitive), Path, TEXT("unknown damage type")))
        {
            return false;
        }
        Out.DamageType = Type.Equals(TEXT("Physical"), ESearchCase::CaseSensitive) ? ELDDemoDamageType::Physical : ELDDemoDamageType::Magical;
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
        // #198 B: a card's package is armed by a completed voluntary dash and names its charges.
        if ((bCard && (!TriggerField(F, Out.Trigger, {ELDDemoEffectTrigger::DashComplete})
                || !Check.Require(F.Has(TEXT("charges")), Path, TEXT("a card's empowered basic names its charges"))))
            || (F.Has(TEXT("charges")) && !IntInto(F, TEXT("charges"), Out.Count, 1, 10))
            // Branch terms: momentum per empowered hit, breach stacks, a stun on the last charge, certain crits.
            || !OptionalFixed(F, TEXT("momentum"), TEXT("momentum"), TEXT("p2"), Out.LimitP2, 1)
            || (F.Has(TEXT("breach")) && !IntInto(F, TEXT("breach"), Out.BasicCount, 1, 5))
            || !OptionalFixed(F, TEXT("last_stun"), TEXT("ms"), TEXT("ms"), Out.ExtraDurationMs, 1)
            || !OptionalFlag(F, TEXT("crit"), Out.bAlwaysCrit)
            || (F.Has(TEXT("stats")) && (!Stats(F.Take(TEXT("stats")), Out.Stats, Check, Path + TEXT(".stats"))
                || !Check.Require((StatMask(Out.Stats) & ~TemporaryStats) == 0, Path, TEXT("timed bonuses cannot change resource maxima")))))
        {
            return false;
        }
        break;
    }
        break;
    }
    // #112 handlers (LDCombatMechanics). Field use: see FLDDemoEffectDefinition and docs/systems/COMBAT_AND_GROWTH.md.
    case ELDDemoEffectOp::Knockback:
        // A push along the caster-to-target direction; a block ends it early, stunning for wall_stun when a branch names one.
        if (!F.Fraction(TEXT("distance"), TEXT("cm"), Out.DistanceCm, true)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !OptionalFixed(F, TEXT("wall_stun"), TEXT("ms"), TEXT("ms"), Out.ExtraDurationMs, 1))
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
    case ELDDemoEffectOp::TriggeredShield:
        // #147 Damaged: a shield whenever the holder takes damage, then interval of rest (only after a granted shield);
        // AbilityHit: the same shield whenever a native direct hit of the holder damages an enemy.
        // burst_ratio + burst_radius: that shield broken answers the bodies around with a share of its amount.
        if (!TriggerField(F, Out.Trigger, {ELDDemoEffectTrigger::Damaged, ELDDemoEffectTrigger::AbilityHit})
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
    case ELDDemoEffectOp::Breach:
    case ELDDemoEffectOp::Burn:
        // stacks: per hit of an active; basic_stacks: per primary basic hit of a card, never both.
        if ((F.Has(TEXT("stacks")) && !IntInto(F, TEXT("stacks"), Out.Count, 1, 10))
            || (F.Has(TEXT("basic_stacks")) && !IntInto(F, TEXT("basic_stacks"), Out.BasicCount, 1, 10))
            || !Check.Require(bCard ? Out.BasicCount > 0 && Out.Count == 0 : Out.Count > 0 && Out.BasicCount == 0, Path,
                bCard ? TEXT("a card applies its stacks with each primary basic hit")
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
            || !IntInto(F, TEXT("count"), Out.Count, 1, 20)
            || (F.Has(TEXT("slow_waves")) && !IntInto(F, TEXT("slow_waves"), Out.BasicCount, 2, 20))
            || !OptionalFixed(F, TEXT("slow"), TEXT("ratio"), TEXT("bp"), Out.SecondaryRatioBp, 1, 9999)
            || !OptionalFixed(F, TEXT("slow_duration"), TEXT("ms"), TEXT("ms"), Out.SecondaryDurationMs, 1)
            || !Check.Require((Out.SecondaryRatioBp > 0) == (Out.SecondaryDurationMs > 0)
                && (Out.Count == 1 || Out.IntervalMs <= MAX_int64 / (Out.Count - 1)), Path, TEXT("invalid channel timing or final slow")))
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
            || !F.Fixed(TEXT("delay"), TEXT("ms"), TEXT("ms"), Out.DelayMs, bImpact ? 0 : 1)
            || !Check.Require(Numbers.LE(Numbers.Value(Out.DistanceCm), Numbers.Value(Out.RadiusCm)), Path,
                TEXT("inner/trigger radius must fit the effect radius"))) { return false; }
        if (bImpact ? !F.Fixed(TEXT("outer_ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999)
                    : !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)) { return false; }
        // Branch terms: a center stun and a lingering wet area; a trap's count and the wire between two of them.
        if (bImpact ? (!OptionalFixed(F, TEXT("center_stun"), TEXT("ms"), TEXT("ms"), Out.ExtraDurationMs, 1)
                        || !OptionalFixed(F, TEXT("linger"), TEXT("ms"), TEXT("ms"), Out.SecondaryDurationMs, 1))
                    : ((F.Has(TEXT("max")) && !IntInto(F, TEXT("max"), Out.Count, 2, 5))
                        || (F.Has(TEXT("wire_distance")) && !F.Fraction(TEXT("wire_distance"), TEXT("cm"), Out.ExtraDistanceCm, true))
                        || !OptionalFixed(F, TEXT("wire_base"), TEXT("points"), TEXT("p2"), Out.BaseP2)
                        || !OptionalFixed(F, TEXT("wire_ap_ratio"), TEXT("ratio"), TEXT("bp"), Out.APRatioBp)
                        || !Check.Require((Out.ExtraDistanceCm.Numerator > 0) == (Out.BaseP2 > 0 || Out.APRatioBp > 0), Path,
                            TEXT("a trap wire needs its distance and damage")))) { return false; }
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
    case ELDDemoEffectOp::BasicElement:
    {
        FString Element;
        if (!F.Text(TEXT("element"), Element)
            || !Check.Require(ElementField(Element, Out.Element), Path + TEXT(".element"), TEXT("unknown element")))
        { return false; }
        break;
    }
    case ELDDemoEffectOp::CooldownCost:
        if (!F.Fixed(TEXT("cooldown_ratio"), TEXT("ratio"), TEXT("bp"), Out.SecondaryRatioBp, 1)
            || !F.Fixed(TEXT("cost_ratio"), TEXT("ratio"), TEXT("bp"), Out.CostRatioBp, 1)) { return false; }
        break;
    case ELDDemoEffectOp::KnockbackImpact:
        if (!DamageTerms(F, Out, Check, Path)
            || !F.Fixed(TEXT("stun_duration"), TEXT("ms"), TEXT("ms"), Out.SecondaryDurationMs, 1)) { return false; }
        break;
    case ELDDemoEffectOp::ProjectileReturn:
    {
        // A return keeps at most the outbound damage unless a skill branch names more and an element.
        bool bElectric = false, bWet = false;
        if (!F.Fixed(TEXT("damage_ratio"), TEXT("ratio"), TEXT("bp"), Out.MultiplierBp, 1, bCard ? 10000 : MAX_int64)
            || !OptionalFlag(F, TEXT("electrify"), bElectric) || !OptionalFlag(F, TEXT("wet"), bWet)
            || !Check.Require(!(bElectric && bWet) && (!bCard || !(bElectric || bWet)), Path, TEXT("invalid return element")))
        { return false; }
        Out.Element = bElectric ? ELDDemoElement::Electric : bWet ? ELDDemoElement::Water : ELDDemoElement::None;
        break;
    }
    case ELDDemoEffectOp::ProjectilePierce:
        if (!IntInto(F, TEXT("count"), Out.Count, 1, 5)
            || !F.Fixed(TEXT("damage_ratio"), TEXT("ratio"), TEXT("bp"), Out.MultiplierBp, 1, 10000)) { return false; }
        break;
    case ELDDemoEffectOp::EchoImpact:
        if (!F.Fixed(TEXT("delay"), TEXT("ms"), TEXT("ms"), Out.DelayMs, 1)
            || !F.Fixed(TEXT("damage_ratio"), TEXT("ratio"), TEXT("bp"), Out.MultiplierBp, 1, 10000)
            || !F.Fixed(TEXT("cooldown_ratio"), TEXT("ratio"), TEXT("bp"), Out.SecondaryRatioBp, 1)) { return false; }
        {
            // Branch terms: the echo's center multiplier and its shock.
            bool bElectric = false;
            if (!OptionalFixed(F, TEXT("center_ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1)
                || !OptionalFlag(F, TEXT("electrify"), bElectric)) { return false; }
            Out.Element = bElectric ? ELDDemoElement::Electric : ELDDemoElement::None;
        }
        break;
    // Connectors C13-C16.
    case ELDDemoEffectOp::HitHasteOther:
    case ELDDemoEffectOp::ReactionHaste:
        if (!F.Fixed(TEXT("cooldown"), TEXT("ms"), TEXT("ms"), Out.AmountMs, 1)) { return false; }
        break;
    case ELDDemoEffectOp::SetupMark:
        if (!F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1)) { return false; }
        break;
    case ELDDemoEffectOp::ZoneTether:
        if (!F.Fixed(TEXT("radius_ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 10000)) { return false; }
        break;
    case ELDDemoEffectOp::FinalKnockback:
        if (!F.Fraction(TEXT("distance"), TEXT("cm"), Out.DistanceCm, true)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !F.Fixed(TEXT("cost_ratio"), TEXT("ratio"), TEXT("bp"), Out.CostRatioBp, 1, 10000)
            || !Check.Require(Out.DistanceCm.Numerator > 0, Path, TEXT("a finisher needs a positive distance"))) { return false; }
        break;
    case ELDDemoEffectOp::EmpowerReach:
        if (!F.Fraction(TEXT("range"), TEXT("cm"), Out.DistanceCm, true)
            || !F.Fixed(TEXT("damage_ratio"), TEXT("ratio"), TEXT("bp"), Out.MultiplierBp, 1, 10000)
            || !Check.Require(Out.DistanceCm.Numerator > 0, Path, TEXT("a reach bonus needs a positive range"))) { return false; }
        break;
    case ELDDemoEffectOp::Pull:
        if (!F.Fraction(TEXT("distance"), TEXT("cm"), Out.DistanceCm, true)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !F.Fraction(TEXT("min_distance"), TEXT("cm"), Out.RadiusCm, true)
            || !Check.Require(Out.DistanceCm.Numerator > 0, Path, TEXT("a pull needs a positive distance"))
            // Branch term: two pulled bodies that meet each take this physical damage.
            || !OptionalFixed(F, TEXT("base"), TEXT("points"), TEXT("p2"), Out.BaseP2)
            || !OptionalFixed(F, TEXT("ad_ratio"), TEXT("ratio"), TEXT("bp"), Out.ADRatioBp)) { return false; }
        break;
    // Connectors C17-C21.
    case ELDDemoEffectOp::Vulnerable:
    {
        FString Scope;
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 10000)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !F.Text(TEXT("scope"), Scope) || !OptionalFlag(F, TEXT("own"), Out.bOwnOnly)) { return false; }
        if (Scope.Equals(TEXT("all"), ESearchCase::CaseSensitive)) { Out.Scope = ELDDemoDamageScope::All; }
        else if (Scope.Equals(TEXT("skill"), ESearchCase::CaseSensitive)) { Out.Scope = ELDDemoDamageScope::Skill; }
        else if (Scope.Equals(TEXT("basic"), ESearchCase::CaseSensitive)) { Out.Scope = ELDDemoDamageScope::Basic; }
        else { return Check.Require(false, Path, TEXT("unknown vulnerability scope")); }
        break;
    }
    case ELDDemoEffectOp::NextBasicAmp:
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !OptionalFlag(F, TEXT("on_catch"), Out.bOnCatch)) { return false; }
        break;
    case ELDDemoEffectOp::MomentumGain:
        if (!F.Fixed(TEXT("momentum"), TEXT("momentum"), TEXT("p2"), Out.BaseP2, 1)
            || !OptionalFlag(F, TEXT("center"), Out.bCenterOnly)) { return false; }
        break;
    case ELDDemoEffectOp::MomentumSteal:
        if (!F.Fixed(TEXT("momentum"), TEXT("momentum"), TEXT("p2"), Out.BaseP2, 1)
            || !F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.MultiplierBp, 1)
            || !OptionalFixed(F, TEXT("delay"), TEXT("ms"), TEXT("ms"), Out.DelayMs, 1)
            || !OptionalFixed(F, TEXT("free_cast"), TEXT("ms"), TEXT("ms"), Out.ExtraDurationMs, 1)) { return false; }
        break;
    case ELDDemoEffectOp::FollowUp:
    {
        bool bElectric = false, bWet = false;
        if (!F.Fixed(TEXT("delay"), TEXT("ms"), TEXT("ms"), Out.DelayMs, 1)
            || !IntInto(F, TEXT("count"), Out.Count, 1, 10)
            || !F.Fixed(TEXT("interval"), TEXT("ms"), TEXT("ms"), Out.IntervalMs, 1)
            || !OptionalFixed(F, TEXT("base"), TEXT("points"), TEXT("p2"), Out.BaseP2)
            || !OptionalFixed(F, TEXT("ad_ratio"), TEXT("ratio"), TEXT("bp"), Out.ADRatioBp)
            || !OptionalFixed(F, TEXT("ap_ratio"), TEXT("ratio"), TEXT("bp"), Out.APRatioBp)
            || !OptionalDamageType(F, Out.DamageType, Check, Path)
            || !OptionalFlag(F, TEXT("electrify"), bElectric) || !OptionalFlag(F, TEXT("wet"), bWet)
            || !OptionalFixed(F, TEXT("attack_slow"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999)
            || !OptionalFixed(F, TEXT("attack_slow_duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !Check.Require((Out.RatioBp > 0) == (Out.DurationMs > 0) && !(bElectric && bWet), Path,
                TEXT("a follow-up attack slow needs its duration; one element at most"))
            || !Check.Require(Out.BaseP2 > 0 || Out.ADRatioBp > 0 || Out.APRatioBp > 0 || bElectric || bWet || Out.RatioBp > 0, Path,
                TEXT("ineffective follow-up"))) { return false; }
        Out.Element = bElectric ? ELDDemoElement::Electric : bWet ? ELDDemoElement::Water : ELDDemoElement::None;
        break;
    }
    case ELDDemoEffectOp::Recast:
    {
        FString Control;
        if (!F.Fixed(TEXT("window"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !F.Fixed(TEXT("damage_ratio"), TEXT("ratio"), TEXT("bp"), Out.MultiplierBp, 1)
            || !F.Text(TEXT("control"), Control)
            || !F.Fixed(TEXT("control_duration"), TEXT("ms"), TEXT("ms"), Out.SecondaryDurationMs)
            || !OptionalFixed(F, TEXT("base"), TEXT("points"), TEXT("p2"), Out.BaseP2)
            || !OptionalFixed(F, TEXT("ad_ratio"), TEXT("ratio"), TEXT("bp"), Out.ADRatioBp)
            || !OptionalFixed(F, TEXT("same_target_stun"), TEXT("ms"), TEXT("ms"), Out.ExtraDurationMs, 1)
            || !OptionalFlag(F, TEXT("free_direction"), Out.bFreeDirection)) { return false; }
        if (Control.Equals(TEXT("same"), ESearchCase::CaseSensitive)) { Out.RecastControl = ELDDemoRecastControl::Same; }
        else if (Control.Equals(TEXT("root"), ESearchCase::CaseSensitive)) { Out.RecastControl = ELDDemoRecastControl::Root; }
        else if (Control.Equals(TEXT("disarm"), ESearchCase::CaseSensitive)) { Out.RecastControl = ELDDemoRecastControl::Disarm; }
        else { return Check.Require(false, Path, TEXT("unknown recast control")); }
        if (!Check.Require((Out.RecastControl == ELDDemoRecastControl::Same) == (Out.SecondaryDurationMs == 0), Path,
            TEXT("a replaced control names its duration"))) { return false; }
        break;
    }
    case ELDDemoEffectOp::DashBack:
        if (!F.Fraction(TEXT("distance"), TEXT("cm"), Out.DistanceCm, true)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !OptionalFlag(F, TEXT("as_dash"), Out.bAsDash)) { return false; }
        break;
    case ELDDemoEffectOp::SelfBuff:
        if (!F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !OptionalFixed(F, TEXT("cut"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999)
            || (F.Has(TEXT("stats")) && (!Stats(F.Take(TEXT("stats")), Out.Stats, Check, Path + TEXT(".stats"))
                || !Check.Require((StatMask(Out.Stats) & ~TemporaryStats) == 0, Path, TEXT("timed bonuses cannot change resource maxima"))))
            || !Check.Require(Out.RatioBp > 0 || !Out.Stats.IsEmpty(), Path, TEXT("ineffective self buff"))) { return false; }
        break;
    case ELDDemoEffectOp::BurnBurst:
        if (!IntInto(F, TEXT("stacks"), Out.Count, 1, 10)
            || !F.Fixed(TEXT("base"), TEXT("points"), TEXT("p2"), Out.BaseP2)
            || !F.Fixed(TEXT("ap_ratio"), TEXT("ratio"), TEXT("bp"), Out.APRatioBp)) { return false; }
        Out.DamageType = ELDDemoDamageType::Magical;
        break;
    case ELDDemoEffectOp::Pulse:
        if (!IntInto(F, TEXT("count"), Out.Count, 1, 10)
            || !F.Fixed(TEXT("interval"), TEXT("ms"), TEXT("ms"), Out.IntervalMs, 1)
            || !F.Fixed(TEXT("damage_ratio"), TEXT("ratio"), TEXT("bp"), Out.MultiplierBp, 1, 10000)
            || !OptionalFixed(F, TEXT("momentum"), TEXT("momentum"), TEXT("p2"), Out.LimitP2, 1)
            || !OptionalFixed(F, TEXT("shield_base"), TEXT("points"), TEXT("p2"), Out.BaseP2, 1)
            || !OptionalFixed(F, TEXT("shield_max_hp_ratio"), TEXT("ratio"), TEXT("bp"), Out.MaxHPRatioBp, 1, 10000)) { return false; }
        break;
    case ELDDemoEffectOp::ZoneGuard:
    case ELDDemoEffectOp::ShieldReflect:
    case ELDDemoEffectOp::ShieldEndHeal:
    case ELDDemoEffectOp::ShieldHitBonus:
    case ELDDemoEffectOp::BraceHeal:
    case ELDDemoEffectOp::BraceStack:
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 10000)) { return false; }
        break;
    case ELDDemoEffectOp::ZoneMomentum:
        if (!F.Fixed(TEXT("momentum"), TEXT("momentum"), TEXT("p2"), Out.BaseP2, 1)) { return false; }
        break;
    case ELDDemoEffectOp::Overcharge:
    case ELDDemoEffectOp::BraceStun:
    case ELDDemoEffectOp::BraceShield:
        if (!F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)) { return false; }
        break;
    case ELDDemoEffectOp::FollowBasic:
        break;
    case ELDDemoEffectOp::ShieldBurstBreak:
        if (!F.Fixed(TEXT("base"), TEXT("points"), TEXT("p2"), Out.BaseP2)
            || !F.Fixed(TEXT("ap_ratio"), TEXT("ratio"), TEXT("bp"), Out.APRatioBp)
            || !F.Fraction(TEXT("radius"), TEXT("cm"), Out.RadiusCm, true)) { return false; }
        Out.DamageType = ELDDemoDamageType::Magical;
        break;
    case ELDDemoEffectOp::BasicRider:
    {
        bool bElectric = false;
        if (!F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !OptionalFlag(F, TEXT("electrify"), bElectric)
            || (F.Has(TEXT("breach")) && !IntInto(F, TEXT("breach"), Out.Count, 1, 5))
            || !Check.Require(bElectric || Out.Count > 0, Path, TEXT("ineffective basic rider"))) { return false; }
        Out.Element = bElectric ? ELDDemoElement::Electric : ELDDemoElement::None;
        break;
    }
    case ELDDemoEffectOp::DelayedBurst:
        if (!F.Fixed(TEXT("delay"), TEXT("ms"), TEXT("ms"), Out.DelayMs, 1)
            || !F.Fraction(TEXT("radius"), TEXT("cm"), Out.RadiusCm, true)
            || !F.Fixed(TEXT("reaction_ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1)) { return false; }
        break;
    case ELDDemoEffectOp::BraceShove:
        if (!F.Fraction(TEXT("distance"), TEXT("cm"), Out.DistanceCm, true)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)) { return false; }
        break;
    case ELDDemoEffectOp::ProjectileZone:
        if (!F.Fraction(TEXT("radius"), TEXT("cm"), Out.RadiusCm, true)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !F.Fixed(TEXT("interval"), TEXT("ms"), TEXT("ms"), Out.IntervalMs, 1)) { return false; }
        break;

    // Spec §7 card operations (field use: FLDDemoEffectDefinition; the compiler refuses the same shapes).
    case ELDDemoEffectOp::Thorns:
    case ELDDemoEffectOp::BasicApRider:
    case ELDDemoEffectOp::SkillAdRider:
    {
        // Y01 reflects RatioBp of a direct hit; A11/K13 ride along their hit with one stat ratio.
        FString Type;
        const bool bThorns = Out.GetOp() == ELDDemoEffectOp::Thorns;
        const bool bAP = Out.GetOp() == ELDDemoEffectOp::BasicApRider;
        if ((bThorns ? !F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1)
                : !F.Fixed(bAP ? TEXT("ap_ratio") : TEXT("ad_ratio"), TEXT("ratio"), TEXT("bp"), bAP ? Out.APRatioBp : Out.ADRatioBp, 1))
            || !F.Text(TEXT("damage_type"), Type)
            || !Check.Require(Type.Equals(TEXT("Physical"), ESearchCase::CaseSensitive)
                || Type.Equals(TEXT("Magical"), ESearchCase::CaseSensitive), Path, TEXT("unknown damage type")))
        { return false; }
        Out.DamageType = Type.Equals(TEXT("Physical"), ESearchCase::CaseSensitive) ? ELDDemoDamageType::Physical : ELDDemoDamageType::Magical;
        break;
    }
    case ELDDemoEffectOp::IdleReadyAmp:
    case ELDDemoEffectOp::IdleCharge:
        if (!F.Fixed(TEXT("interval"), TEXT("ms"), TEXT("ms"), Out.IntervalMs, 1)
            || !F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1)) { return false; }
        break;
    case ELDDemoEffectOp::SkillLifesteal:
    case ELDDemoEffectOp::SkillDamageFactor:
    case ELDDemoEffectOp::SkillGrowth:
    case ELDDemoEffectOp::ReactionFactor:
    case ELDDemoEffectOp::MissingHpAttackSpeed:
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1)) { return false; }
        break;
    case ELDDemoEffectOp::HpCost:
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 10000)) { return false; }
        break;
    case ELDDemoEffectOp::ComboMomentum:
    case ELDDemoEffectOp::SkillHitMomentum:
    case ELDDemoEffectOp::ReactionMomentum:
        if (!F.Fixed(TEXT("momentum"), TEXT("momentum"), TEXT("p2"), Out.LimitP2, 1)) { return false; }
        break;
    case ELDDemoEffectOp::BasicCooldownCut:
        if (!F.Fixed(TEXT("cooldown"), TEXT("ms"), TEXT("ms"), Out.AmountMs, 1)) { return false; }
        break;
    case ELDDemoEffectOp::ComboSkillHit:
    case ELDDemoEffectOp::DashContactReset:
    case ELDDemoEffectOp::DashPhase:
    case ELDDemoEffectOp::TakedownReset:
    case ELDDemoEffectOp::ReactionElement:
    case ELDDemoEffectOp::SkillCrit:
    case ELDDemoEffectOp::LifestealSkills:
        break;
    case ELDDemoEffectOp::ComboKnockback:
    case ELDDemoEffectOp::DashSplit:
        if (!F.Fraction(TEXT("distance"), TEXT("cm"), Out.DistanceCm, true)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)
            || !Check.Require(Out.DistanceCm.Numerator > 0, Path, TEXT("a move needs a positive distance"))) { return false; }
        break;
    case ELDDemoEffectOp::ComboDouble:
    case ELDDemoEffectOp::AutoRecast:
        if (!F.Fixed(TEXT("damage_ratio"), TEXT("ratio"), TEXT("bp"), Out.MultiplierBp, 1, 10000)
            || (Out.GetOp() == ELDDemoEffectOp::AutoRecast && !F.Fixed(TEXT("delay"), TEXT("ms"), TEXT("ms"), Out.DelayMs, 1)))
        { return false; }
        break;
    case ELDDemoEffectOp::OvercapExtraHit:
        if (!F.Fraction(TEXT("step"), TEXT("attacks/s"), Out.StepRate, true)
            || !Check.Require(Out.StepRate.Numerator > 0, Path, TEXT("an attack-speed step must be positive"))
            || !F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 10000)) { return false; }
        break;
    case ELDDemoEffectOp::FocusAttackSpeed:
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)) { return false; }
        break;
    case ELDDemoEffectOp::SkillElement:
    case ELDDemoEffectOp::DashContactElement:
    {
        FString Element;
        if (!F.Text(TEXT("element"), Element)
            || !Check.Require(ElementField(Element, Out.Element), Path + TEXT(".element"), TEXT("unknown element"))
            || (Out.GetOp() == ELDDemoEffectOp::SkillElement && !IntInto(F, TEXT("stacks"), Out.Count, 1, 10)))
        { return false; }
        break;
    }
    case ELDDemoEffectOp::LastStand:
        if (!F.Fixed(TEXT("threshold"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 9999)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)) { return false; }
        break;
    case ELDDemoEffectOp::ArmorToHp:
        if (!F.Fixed(TEXT("max_hp"), TEXT("hp"), TEXT("p2"), Out.BaseP2, 1)) { return false; }
        break;
    case ELDDemoEffectOp::OverhealShield:
        if (!F.Fixed(TEXT("cap_ratio"), TEXT("ratio"), TEXT("bp"), Out.MultiplierBp, 1, 10000)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1)) { return false; }
        break;
    case ELDDemoEffectOp::HasteMomentum:
        if (!F.Fixed(TEXT("haste"), TEXT("points"), TEXT("p2"), Out.BaseP2, 1)
            || !F.Fixed(TEXT("momentum"), TEXT("momentum"), TEXT("p2"), Out.LimitP2, 1)) { return false; }
        break;
    case ELDDemoEffectOp::HealGrowth:
        if (!F.Fixed(TEXT("heal"), TEXT("hp"), TEXT("p2"), Out.BaseP2, 1)
            || !F.Fixed(TEXT("max_hp"), TEXT("hp"), TEXT("p2"), Out.LimitP2, 1)) { return false; }
        break;
    case ELDDemoEffectOp::KillGrowth:
    case ELDDemoEffectOp::ControlGrowth:
        if (!Stats(F.Take(TEXT("stats")), Out.Stats, Check, Path + TEXT(".stats"))
            || !Check.Require((StatMask(Out.Stats) & ~(Bit(ELDDemoBonusStat::ADFlat) | Bit(ELDDemoBonusStat::APFlat)
                | Bit(ELDDemoBonusStat::ArmorFlat) | Bit(ELDDemoBonusStat::MagicResistFlat))) == 0, Path,
                TEXT("match growth adds flat attack, power or resistances"))
            || (Out.GetOp() == ELDDemoEffectOp::KillGrowth
                && !F.Fixed(TEXT("assist_ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 0, 10000)))
        { return false; }
        break;
    case ELDDemoEffectOp::ElementAura:
        if (!F.Fraction(TEXT("radius"), TEXT("cm"), Out.RadiusCm, true)) { return false; }
        break;
    case ELDDemoEffectOp::ElementDuration:
        if (!F.Fixed(TEXT("multiplier"), TEXT("ratio"), TEXT("bp"), Out.MultiplierBp, 10001)) { return false; }
        break;
    case ELDDemoEffectOp::BurnCap:
        if (!IntInto(F, TEXT("count"), Out.Count, 1, 100)) { return false; }
        break;
    default:
        return Check.Require(false, Path, TEXT("unimplemented effect projection"));
    }
    return F.Done();
}

bool Effects(const FValue& Value, TArray<FLDDemoEffectDefinition>& Out, bool bCard, FOpMask& Mask,
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
        if (!Effect((*Rows)[I], Definition, bCard, Check, FString::Printf(TEXT("%s[%d]"), *Path, I)))
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
    return F.Fixed(TEXT("momentum_cost"), TEXT("momentum"), TEXT("p2"), Out.MomentumCostP2)
        && F.Fixed(TEXT("cooldown"), TEXT("ms"), TEXT("ms"), Out.CooldownMs, 1)
        && Effects(F.Take(TEXT("effects")), Out.Effects, false, Mask, Check, Path + TEXT(".effects"))
        && Check.Require(Mask == ExpectedOps, Path + TEXT(".effects"), TEXT("missing or extra effect; definition/target projection mismatch"));
}

// An active's element (none|fire|water|shock|chill|rot|gust) and hit type (targeted|projectile|area|dash|self).
bool SkillElementField(FFields& F, ELDDemoSkillElement& Out, FCheck& Check, const FString& Path)
{
    FString Name;
    if (!F.Identifier(TEXT("element"), Name)) { return false; }
    using E = ELDDemoSkillElement;
    const TPair<const TCHAR*, E> Names[] = {{TEXT("none"), E::None}, {TEXT("fire"), E::Fire}, {TEXT("water"), E::Water},
        {TEXT("shock"), E::Shock}, {TEXT("chill"), E::Chill}, {TEXT("rot"), E::Rot}, {TEXT("gust"), E::Gust}};
    for (const auto& Row : Names)
    {
        if (Name.Equals(Row.Key, ESearchCase::CaseSensitive)) { Out = Row.Value; return true; }
    }
    return Check.Require(false, Path + TEXT(".element"), TEXT("unknown skill element"));
}

bool HitTypeField(FFields& F, ELDDemoHitType& Out, FCheck& Check, const FString& Path)
{
    FString Name;
    if (!F.Identifier(TEXT("hit_type"), Name)) { return false; }
    using H = ELDDemoHitType;
    const TPair<const TCHAR*, H> Names[] = {{TEXT("targeted"), H::Targeted}, {TEXT("projectile"), H::Projectile},
        {TEXT("area"), H::Area}, {TEXT("dash"), H::Dash}, {TEXT("self"), H::Self}};
    for (const auto& Row : Names)
    {
        if (Name.Equals(Row.Key, ESearchCase::CaseSensitive)) { Out = Row.Value; return true; }
    }
    return Check.Require(false, Path + TEXT(".hit_type"), TEXT("unknown hit type"));
}

// The six resolved branch variants of an active in path order A, B, A1, A2, B1, B2 (issue #287 spec §5.2). The content
// compiler applied the registered patches and checked each variant's target form; the operations are any active form.
bool Branches(const FValue& Value, TArray<FLDDemoAbilityBranch>& Out, FCheck& Check, const FString& Path)
{
    static const TPair<const TCHAR*, ELDSkillBranch> Paths[] = {{TEXT("A"), ELDSkillBranch::A}, {TEXT("B"), ELDSkillBranch::B},
        {TEXT("A1"), ELDSkillBranch::A1}, {TEXT("A2"), ELDSkillBranch::A2}, {TEXT("B1"), ELDSkillBranch::B1}, {TEXT("B2"), ELDSkillBranch::B2}};
    const TArray<FValue>* Rows;
    if (!Array(Value, Rows, Check, Path, UE_ARRAY_COUNT(Paths), UE_ARRAY_COUNT(Paths)))
    {
        return false;
    }
    Out.Reset();
    for (int32 Index = 0; Index < Rows->Num(); ++Index)
    {
        const FString RowPath = FString::Printf(TEXT("%s[%d]"), *Path, Index);
        FFields F(Check, (*Rows)[Index], RowPath);
        FLDDemoAbilityBranch& Branch = Out.AddDefaulted_GetRef();
        FString Name;
        FOpMask Mask;
        if (!F.Text(TEXT("path"), Name)
            || !Check.Require(Name.Equals(Paths[Index].Key, ESearchCase::CaseSensitive), RowPath, TEXT("branch paths must be A, B, A1, A2, B1, B2"))
            || !Target(F.Take(TEXT("target")), Branch.Target, Check, RowPath + TEXT(".target"))
            || !F.Fixed(TEXT("windup"), TEXT("ms"), TEXT("ms"), Branch.WindupMs)
            || !SkillElementField(F, Branch.Element, Check, RowPath) || !HitTypeField(F, Branch.HitType, Check, RowPath)
            || !F.Fixed(TEXT("momentum_cost"), TEXT("momentum"), TEXT("p2"), Branch.Values.MomentumCostP2)
            || !F.Fixed(TEXT("cooldown"), TEXT("ms"), TEXT("ms"), Branch.Values.CooldownMs, 1)
            || !Effects(F.Take(TEXT("effects")), Branch.Values.Effects, false, Mask, Check, RowPath + TEXT(".effects"))
            || !F.Done())
        {
            return false;
        }
        Branch.Path = Paths[Index].Value;
    }
    return true;
}

// Spec §6.3: distinct vocabulary keys as ELDCardVocab bits (DemoContent.CARD_VOCAB order).
bool VocabField(FFields& F, uint32& Out, int32 MinKeys, FCheck& Check, const FString& Path)
{
    static const TCHAR* const Names[] = {TEXT("basic_hit"), TEXT("combo3"), TEXT("skill_hit"), TEXT("skill_cast"), TEXT("overdrive"), TEXT("reaction"), TEXT("element"), TEXT("dash"), TEXT("dash_contact"), TEXT("perfect_dodge"), TEXT("knockback"), TEXT("wall_impact"), TEXT("shield"), TEXT("shield_break"), TEXT("controlled"), TEXT("control"), TEXT("damaged"), TEXT("low_hp"), TEXT("kill"), TEXT("idle"), TEXT("ground"), TEXT("heal"), TEXT("momentum"), TEXT("attack_speed"), TEXT("move_speed"), TEXT("crit"), TEXT("armor"), TEXT("max_hp")};
    static_assert(UE_ARRAY_COUNT(Names) == int32(ELDCardVocab::Count));
    const TArray<FValue>* Rows;
    if (!Array(F.Take(TEXT("vocab")), Rows, Check, Path + TEXT(".vocab"), MinKeys, 8))
    {
        return false;
    }
    Out = 0;
    for (const FValue& Row : *Rows)
    {
        FString Name;
        if (!String(Row, Name, Check, Path + TEXT(".vocab"), 1, 63))
        {
            return false;
        }
        int32 Found = INDEX_NONE;
        for (int32 I = 0; I < UE_ARRAY_COUNT(Names); ++I)
        {
            if (Name.Equals(Names[I], ESearchCase::CaseSensitive)) { Found = I; }
        }
        if (!Check.Require(Found != INDEX_NONE && (Out & (1u << Found)) == 0, Path + TEXT(".vocab"), TEXT("unknown or repeated vocabulary key")))
        {
            return false;
        }
        Out |= 1u << Found;
    }
    return true;
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
        || !SkillElementField(F, Out.Element, Check, Path) || !HitTypeField(F, Out.HitType, Check, Path)
        || !VocabField(F, Out.VocabMask, 0, Check, Path)
        || !Values(F, Out.Values, ExpectedOps, Check, Path)
        || !Branches(F.Take(TEXT("branches")), Out.Branches, Check, Path + TEXT(".branches")))
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
    case ELDDemoStatId::StatBrains: Expected = Bit(S::APFlat) | Bit(S::CooldownHasteFlat) | Bit(S::MomentumRegenFlat); break;
    case ELDDemoStatId::StatAgility: Expected = Bit(S::AttackSpeedBonusRatio) | Bit(S::MoveSpeedBonusRatio); break;
    default: return Check.Require(false, Path, TEXT("unimplemented stat projection"));
    }
    return Check.Require(StatMask(Out.PerPoint) == Expected, Path, TEXT("wrong stat bonuses"));
}

// Spec §6-§7: one build card row (DemoContent._validate_cards refuses the same shapes).
bool Card(const FValue& Value, FLDDemoCardDefinition& Out, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    FString Form, Type;
    if (!NativeIdentity(F, TEXT("cards"), Out.Id, Out.NativeId) || !F.Text(TEXT("form"), Form) || !F.Text(TEXT("type"), Type))
    {
        return false;
    }
    static const TCHAR* const Forms[] = {TEXT("basic"), TEXT("skill"), TEXT("dash"), TEXT("body")};
    static const TCHAR* const Types[] = {TEXT("trigger"), TEXT("modify"), TEXT("convert"), TEXT("grow"), TEXT("element")};
    static_assert(UE_ARRAY_COUNT(Forms) == int32(ELDCardForm::Count) && UE_ARRAY_COUNT(Types) == int32(ELDCardType::Count));
    int32 FormIndex = INDEX_NONE, TypeIndex = INDEX_NONE;
    for (int32 I = 0; I < UE_ARRAY_COUNT(Forms); ++I) { if (Form.Equals(Forms[I], ESearchCase::CaseSensitive)) { FormIndex = I; } }
    for (int32 I = 0; I < UE_ARRAY_COUNT(Types); ++I) { if (Type.Equals(Types[I], ESearchCase::CaseSensitive)) { TypeIndex = I; } }
    if (!Check.Require(FormIndex != INDEX_NONE, Path + TEXT(".form"), TEXT("unknown card form"))
        || !Check.Require(TypeIndex != INDEX_NONE, Path + TEXT(".type"), TEXT("unknown card type"))
        || !VocabField(F, Out.VocabMask, 1, Check, Path))
    {
        return false;
    }
    Out.Form = ELDCardForm(FormIndex);
    Out.Type = ELDCardType(TypeIndex);
    if (F.Has(TEXT("choose_element")))
    {
        const TArray<FValue>* Rows;
        if (!Array(F.Take(TEXT("choose_element")), Rows, Check, Path + TEXT(".choose_element"), 2, 3))
        {
            return false;
        }
        for (const FValue& Row : *Rows)
        {
            FString Name;
            ELDDemoElement Element = ELDDemoElement::None;
            if (!String(Row, Name, Check, Path + TEXT(".choose_element"), 1, 63)
                || !Check.Require(ElementField(Name, Element) && !Out.ChooseElements.Contains(Element), Path + TEXT(".choose_element"),
                    TEXT("unknown or repeated element")))
            {
                return false;
            }
            Out.ChooseElements.Add(Element);
        }
    }
    FOpMask Mask;
    if (!Effects(F.Take(TEXT("effects")), Out.Effects, true, Mask, Check, Path + TEXT(".effects")))
    {
        return false;
    }
    for (const FLDDemoEffectDefinition& Effect : Out.Effects)
    {
        const bool bStacks = Effect.GetOp() == ELDDemoEffectOp::Burn || Effect.GetOp() == ELDDemoEffectOp::Breach;
        if (!Check.Require(!bStacks || (Effect.BasicCount > 0 && Effect.Count == 0), Path,
            TEXT("a card applies its stacks with each primary basic hit")))
        {
            return false;
        }
    }
    return Check.Require((Mask & ~FormOps[FormIndex]).None(), Path, TEXT("an operation outside the card's form"))
        && Check.Require(Out.ChooseElements.IsEmpty() != (Mask & OpBit(ELDDemoEffectOp::ElementAura)).Any(), Path,
            TEXT("an element aura names its element choices and nothing else does"))
        && F.Done();
}

bool Cards(const FValue& Value, TArray<FLDDemoCardDefinition>& Out, FCheck& Check)
{
    const TArray<FValue>* Rows;
    if (!Array(Value, Rows, Check, TEXT("content.cards"), 1, 85))
    {
        return false;
    }
    for (int32 I = 0; I < Rows->Num(); ++I)
    {
        FLDDemoCardDefinition Row;
        const FString Path = FString::Printf(TEXT("content.cards[%d]"), I);
        if (!Card((*Rows)[I], Row, Check, Path)
            || !Check.Require(Out.IsEmpty() || Out.Last().NativeId < Row.NativeId, Path,
                TEXT("cards are listed once each in ascending native ID order")))
        {
            return false;
        }
        Out.Add(MoveTemp(Row));
    }
    return true;
}

bool ValidateDisplayText(const FString& Text,bool bAllowDigits,FCheck& Check,const FString& Path);

bool MonsterSkill(const FValue& Value, FLDDemoMonsterSkill& Out, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    FString Pattern;
    const TCHAR* Names[] = {TEXT("Enrage"),TEXT("Cone"),TEXT("Charge"),TEXT("Projectile"),TEXT("Guard"),TEXT("Circle"),TEXT("SweepLine"),TEXT("EchoCircle"),TEXT("Fan"),TEXT("DoubleCircle"),TEXT("RotatingCone")};
    if (!F.Text(TEXT("pattern"), Pattern)) { return false; }
    int32 Index = INDEX_NONE;
    for (int32 I=0; I<UE_ARRAY_COUNT(Names); ++I) { if (Pattern==Names[I]) { Index=I; break; } }
    if (!Check.Require(Index!=INDEX_NONE,Path,TEXT("unknown monster attack pattern"))) { return false; }
    Out.Pattern=static_cast<ELDMonsterPattern>(Index+1);
    return F.Text(TEXT("name"),Out.Name)
        && Check.Require(Out.Name.Len()<=64,Path,TEXT("monster skill name exceeds its display bound"))
        && ValidateDisplayText(Out.Name,false,Check,Path)
        && F.Int(TEXT("every"),Out.Every,1,100)
        && F.Fixed(TEXT("windup"),TEXT("ms"),TEXT("ms"),Out.WindupMs,1)
        && F.Fixed(TEXT("recovery"),TEXT("ms"),TEXT("ms"),Out.RecoveryMs,1)
        && F.Fixed(TEXT("ratio"),TEXT("ratio"),TEXT("bp"),Out.RatioBp,1,22500)
        && F.Fraction(TEXT("range"),TEXT("cm"),Out.RangeCm,true)
        && F.Fraction(TEXT("radius"),TEXT("cm"),Out.RadiusCm,true)
        && F.Int(TEXT("angle"),Out.AngleDegrees,1,180)
        && F.Int(TEXT("pulses"),Out.Pulses,1,3)
        && F.Fixed(TEXT("pulse_interval"),TEXT("ms"),TEXT("ms"),Out.PulseIntervalMs,1)
        && F.Int(TEXT("pulse_angle"),Out.PulseAngleDegrees,0,120)
        && F.Fraction(TEXT("offset"),TEXT("cm"),Out.OffsetCm)
        && F.Fixed(TEXT("passive"),TEXT("ratio"),TEXT("bp"),Out.PassiveBp,0,10000)
        && F.Fixed(TEXT("evolved_passive"),TEXT("ratio"),TEXT("bp"),Out.EvolvedPassiveBp,0,10000)
        && F.Fixed(TEXT("low_hp_threshold"),TEXT("ratio"),TEXT("bp"),Out.LowHPThresholdBp,0,10000)
        && F.Fixed(TEXT("low_hp_haste"),TEXT("ratio"),TEXT("bp"),Out.LowHPHasteBp,0,10000)
        && F.Fixed(TEXT("guard_duration"),TEXT("ms"),TEXT("ms"),Out.GuardDurationMs,1)
        && F.Int(TEXT("evolve_stage"),Out.EvolveStage,1,255)
        && F.Int(TEXT("echo_stage"),Out.EchoStage,1,255)
        && F.Fixed(TEXT("echo_delay"),TEXT("ms"),TEXT("ms"),Out.EchoDelayMs,1)
        && F.Fixed(TEXT("echo_ratio"),TEXT("ratio"),TEXT("bp"),Out.EchoRatioBp,1,22500)
        && F.Fixed(TEXT("evolved_slow"),TEXT("ratio"),TEXT("bp"),Out.EvolvedSlowBp,0,10000)
        && F.Fixed(TEXT("evolved_slow_duration"),TEXT("ms"),TEXT("ms"),Out.EvolvedSlowMs,1)
        && F.Fraction(TEXT("projectile_speed"),TEXT("cm/s"),Out.ProjectileSpeedCmPerSec,true)
        && Check.Require(Out.RatioBp*Out.Pulses<=22500,Path,TEXT("monster skill exceeds its total damage budget"))
        && F.Done();
}

bool Monster(const FValue& Value, FLDDemoMonsterDefinition& Out, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    FString Tier;
    if (!NativeIdentity(F, TEXT("monsters"), Out.Id, Out.NativeId) || !F.Text(TEXT("tier"), Tier)
        || !Check.Require(Tier.Equals(TEXT("Low"), ESearchCase::CaseSensitive)
            || Tier.Equals(TEXT("High"), ESearchCase::CaseSensitive) || Tier.Equals(TEXT("Elite"), ESearchCase::CaseSensitive) || Tier==TEXT("Ancient"),
            Path, TEXT("unknown camp tier")))
    {
        return false;
    }
    Out.Tier = Tier.Equals(TEXT("Low"), ESearchCase::CaseSensitive) ? ELDDemoCampTier::Low
        : Tier.Equals(TEXT("High"), ESearchCase::CaseSensitive) ? ELDDemoCampTier::High : Tier==TEXT("Elite") ? ELDDemoCampTier::Elite : ELDDemoCampTier::Ancient;
    return MonsterSkill(F.Take(TEXT("skill")), Out.Skill, Check, Path+TEXT(".skill"))
        && F.Int(TEXT("count_per_camp"), Out.CountPerCamp, 1)
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
        && F.Fixed(TEXT("ap"), TEXT("points"), TEXT("p2"), Out.APP2, 0, 0)
        && F.Fixed(TEXT("crit_chance"), TEXT("ratio"), TEXT("bp"), Out.CritChanceBp, 0, 0)
        && Check.Require(Out.HPRegenPerSec.Numerator == 0,
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
    return F.Done();
}

bool FinalMod(const FValue& Value, FLDDemoFinalModDefinition& Out, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    if (!NativeIdentity(F, TEXT("final_mods"), Out.Id, Out.NativeId)) { return false; }
    const auto Time = [&F](const TCHAR* Key, int64& Amount) { return F.Fixed(Key, TEXT("ms"), TEXT("ms"), Amount, 1); };
    const auto Ratio = [&F](const TCHAR* Key, int64& Amount) { return F.Fixed(Key, TEXT("ratio"), TEXT("bp"), Amount, 1, 9999); };
    const auto Cm = [&F](const TCHAR* Key, FLDDemoRational& Amount) { return F.Fraction(Key, TEXT("cm"), Amount, true); };
    const auto Base = [&F, &Out]() { return F.Fixed(TEXT("base"), TEXT("points"), TEXT("p2"), Out.BaseP2, 1); };
    bool Valid = false;
    switch (Out.GetNativeId())
    {
    case ELDDemoFinalModId::FinalBowling:
        Out.Requirement = ELDDemoFinalModRequirement::ForcedMove;
        Valid = F.Literal(TEXT("requirement"), TEXT("ForcedMove")) && Base() && Ratio(TEXT("ap_ratio"), Out.APRatioBp)
            && Cm(TEXT("distance"), Out.DistanceCm) && Ratio(TEXT("distance_ratio"), Out.DistanceScaleBp);
        break;
    case ELDDemoFinalModId::FinalForcedExit:
        Out.Requirement = ELDDemoFinalModRequirement::HardControl;
        Valid = F.Literal(TEXT("requirement"), TEXT("HardControl")) && Cm(TEXT("distance"), Out.DistanceCm)
            && Time(TEXT("delay"), Out.DelayMs) && Time(TEXT("interval"), Out.IntervalMs)
            && Ratio(TEXT("control_ratio"), Out.ControlScaleBp);
        break;
    case ELDDemoFinalModId::FinalTraffic:
        Out.Requirement = ELDDemoFinalModRequirement::Dash;
        Valid = F.Literal(TEXT("requirement"), TEXT("Dash")) && Cm(TEXT("radius"), Out.RadiusCm)
            && Time(TEXT("duration"), Out.DurationMs) && Ratio(TEXT("slow"), Out.SlowBp)
            && Time(TEXT("slow_duration"), Out.SlowMs) && Time(TEXT("cooldown_cost"), Out.CooldownCostMs);
        break;
    case ELDDemoFinalModId::FinalSplit:
        Out.Requirement = ELDDemoFinalModRequirement::PlacedZone;
        Valid = F.Literal(TEXT("requirement"), TEXT("PlacedZone")) && Cm(TEXT("offset"), Out.OffsetCm)
            && Ratio(TEXT("radius_ratio"), Out.RadiusScaleBp) && Ratio(TEXT("damage_ratio"), Out.DamageScaleBp)
            && Time(TEXT("duration_cost"), Out.DurationCostMs);
        break;
    case ELDDemoFinalModId::FinalLandlord:
        Valid = F.Literal(TEXT("requirement"), TEXT("Basic")) && Base() && Ratio(TEXT("ad_ratio"), Out.ADRatioBp)
            && Cm(TEXT("radius"), Out.RadiusCm) && Time(TEXT("delay"), Out.DelayMs)
            && Time(TEXT("interval"), Out.IntervalMs) && Ratio(TEXT("damage_ratio"), Out.DamageScaleBp);
        break;
    case ELDDemoFinalModId::FinalBreach:
        Valid = F.Literal(TEXT("requirement"), TEXT("Basic")) && Time(TEXT("outside"), Out.OutsideMs)
            && Time(TEXT("rearm_outside"), Out.RearmOutsideMs) && Time(TEXT("window"), Out.WindowMs)
            && Cm(TEXT("distance"), Out.DistanceCm) && Time(TEXT("cooldown_cost"), Out.CooldownCostMs)
            && Check.Require(Out.RearmOutsideMs >= Out.OutsideMs, Path, TEXT("rearm outside must not be shorter than the first entry"));
        break;
    case ELDDemoFinalModId::FinalComeback:
        Valid = F.Literal(TEXT("requirement"), TEXT("Basic")) && Base() && Ratio(TEXT("ap_ratio"), Out.APRatioBp)
            && Cm(TEXT("radius"), Out.RadiusCm) && Time(TEXT("duration"), Out.DurationMs)
            && Time(TEXT("interval"), Out.IntervalMs) && Time(TEXT("window"), Out.WindowMs)
            && Time(TEXT("pulse_interval"), Out.PulseIntervalMs)
            && Ratio(TEXT("slow"), Out.SlowBp) && Time(TEXT("slow_duration"), Out.SlowMs)
            && Ratio(TEXT("damage_ratio"), Out.DamageScaleBp);
        break;
    case ELDDemoFinalModId::FinalOpenMic:
        Valid = F.Literal(TEXT("requirement"), TEXT("Basic")) && Base() && Ratio(TEXT("ap_ratio"), Out.APRatioBp)
            && Cm(TEXT("radius"), Out.RadiusCm) && Time(TEXT("duration"), Out.DurationMs) && Time(TEXT("window"), Out.WindowMs)
            && Time(TEXT("pulse_interval"), Out.PulseIntervalMs)
            && Ratio(TEXT("slow"), Out.SlowBp) && Time(TEXT("slow_duration"), Out.SlowMs)
            && Ratio(TEXT("move_speed_cost"), Out.MoveSpeedCostBp);
        break;
    default:
        return Check.Require(false, Path, TEXT("unimplemented final mod"));
    }
    return Valid && F.Done();
}

bool CardTemplates(const FLDDemoContentDefinitions& Out, FCheck& Check);

bool Content(const FValue& Value, FLDDemoContentDefinitions& Out, FCheck& Check)
{
    FFields F(Check, Value, TEXT("content"));
    FFields Identity(Check, F.Take(TEXT("identity")), TEXT("content.identity"));
    int64 Revision;
    if (!Identity.Int(TEXT("schema_version"), Revision, 3, 3)
        || !Identity.Int(TEXT("handler_revision"), Revision, 10, 10)
        || !Identity.Int(TEXT("projection_revision"), Revision, 9, 9))
    {
        return false;
    }
    FFields Approval(Check, Identity.Take(TEXT("approval")), TEXT("content.identity.approval"));
    if (!Approval.Text(TEXT("status"), Out.Identity.ApprovalStatus)
        || !Check.Require(Out.Identity.ApprovalStatus.Equals(TEXT("approved"), ESearchCase::CaseSensitive), Approval.Path, TEXT("approval metadata required"))
        || !Approval.Text(TEXT("decision"), Out.Identity.ApprovalDecision, 1, 4096)
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
    if (!Dash.Fixed(TEXT("cooldown"), TEXT("ms"), TEXT("ms"), InnateDash.Values.CooldownMs, 1)
        || !Dash.Fraction(TEXT("distance"), TEXT("cm"), DashMove.DistanceCm, true)
        || !Dash.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), DashMove.DurationMs, 1)
        || !Check.Require(DashMove.DurationMs < InnateDash.Values.CooldownMs, Dash.Path, TEXT("dash outlasts its cooldown"))
        || !Dash.Done() || !Character.Done()
        || !Definitions(F.Take(TEXT("abilities")), Out.Abilities, Check, TEXT("content.abilities"), 1, 22, Ability)
        || !Definitions(F.Take(TEXT("stats")), Out.Stats, Check, TEXT("content.stats"), 5, 5, StatDefinition)
        || !Cards(F.Take(TEXT("cards")), Out.Cards, Check)
        || !Definitions(F.Take(TEXT("final_mods")), Out.FinalMods, Check, TEXT("content.final_mods"), 8, 8, FinalMod)
        || !Progression(F.Take(TEXT("progression")), Out.Progression, Check))
    {
        return false;
    }
    FFields Camps(Check, F.Take(TEXT("camps")), TEXT("content.camps"));
    if (!Definitions(Camps.Take(TEXT("monsters")), Out.Monsters, Check, TEXT("content.camps.monsters"), 12, 12, Monster))
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
            || !Boss.Fixed(TEXT("leave_at"), TEXT("ms"), TEXT("ms"), Rules.LeaveAtMs, 1)
            || !Boss.Int(TEXT("reward_xp"), Body.RewardXP)
            || !Boss.Done())
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
        Rules.Id = Body.Id;
        Out.Monsters.Add(MoveTemp(Body));
    }
    FFields World(Check, F.Take(TEXT("world")), TEXT("content.world"));
    if (!World.Identifier(TEXT("map_id"), Out.World.MapId)
        || !Ids(World.Take(TEXT("spawn_ids")), Out.World.SpawnIds, Check, World.Path + TEXT(".spawn_ids"),
            LDDemoWorld::MinSpawns, LDDemoWorld::MaxSpawns)
        || !Ids(World.Take(TEXT("camp_ids")), Out.World.CampIds, Check, World.Path + TEXT(".camp_ids"), 1, LDDemoWorld::MaxCamps)
        || !World.Identifier(TEXT("capture_id"), Out.World.CaptureId)
        || !Ids(World.Take(TEXT("pk_spawn_ids")), Out.World.PKSpawnIds, Check, World.Path + TEXT(".pk_spawn_ids"),
            LDDemoWorld::PKSpawns, LDDemoWorld::PKSpawns)
        || !World.Identifier(TEXT("pk_throw_id"), Out.World.PKThrowId)
        || !World.Done())
    {
        return false;
    }
    // #198: the PK ring is read in index order, so the references are exactly pk_spawn_0..5 and pk_throw.
    for (int32 Index = 0; Index < Out.World.PKSpawnIds.Num(); ++Index)
    {
        if (!Check.Require(Out.World.PKSpawnIds[Index].Equals(FString::Printf(TEXT("pk_spawn_%d"), Index), ESearchCase::CaseSensitive),
            World.Path + TEXT(".pk_spawn_ids"), TEXT("PK spawns must be pk_spawn_0 to pk_spawn_5 in order")))
        {
            return false;
        }
    }
    if (!Check.Require(Out.World.PKThrowId.Equals(TEXT("pk_throw"), ESearchCase::CaseSensitive),
        World.Path + TEXT(".pk_throw_id"), TEXT("the PK throw anchor must be pk_throw")))
    {
        return false;
    }
    TSet<FString> DisplayIds;
    DisplayIds.Add(Out.Character.Id);
    // A branch step's row (its name and one-line text) shows its active's icon.
    TMap<FString, FString> BranchIcons;
    for (const auto& Row : Out.Abilities)
    {
        DisplayIds.Add(Row.Id);
        for (const auto& Branch : Row.Branches)
        {
            const FString Id = LDSkillBranch::DisplayId(Row.Id, Branch.Path);
            DisplayIds.Add(Id);
            BranchIcons.Add(Id, TEXT("icon_") + Row.Id);
        }
    }
    for (const auto& Row : Out.Stats) { DisplayIds.Add(Row.Id); }
    for (const auto& Row : Out.Cards) { DisplayIds.Add(Row.Id); }
    for (const auto& Row : Out.FinalMods) { DisplayIds.Add(Row.Id); }
    for (const auto& Row : Out.Monsters) { DisplayIds.Add(Row.Id); }
    for (const auto& Entry : NativeIds)
    {
        if (FCString::Strcmp(Entry.Category, TEXT("statuses")) == 0) { DisplayIds.Add(Entry.Id); }
    }
    const FValue Display = F.Take(TEXT("display"));
    TArray<FString> DisplayNames;
    const auto ParseDisplay = [&Out, &DisplayIds, &DisplayNames, &BranchIcons](const FValue& Row, FLDDemoDisplayDefinition& D, FCheck& C, const FString& Path)
    {
        FFields Fields(C, Row, Path);
        if (!Fields.Identifier(TEXT("id"), D.Id) || !Fields.Text(TEXT("namespace"), D.Namespace)
            || !Fields.Text(TEXT("name_key"), D.NameKey, 1, 128)
            || !Fields.Text(TEXT("name_text"), D.NameText, 1, 32)
            || !ValidateDisplayText(D.NameText, true, C, Path + TEXT(".name_text"))
            || !Fields.Text(TEXT("description_key"), D.DescriptionKey, 1, 128)
            || !Fields.Text(TEXT("description_text"), D.DescriptionText, 1, 320)
            || !ValidateDisplayText(D.DescriptionText, false, C, Path + TEXT(".description_text"))
            || !Fields.Identifier(TEXT("icon_id"), D.IconId)
            || !C.Require(DisplayIds.Contains(D.Id) && D.Namespace.Equals(TEXT("DemoContent"), ESearchCase::CaseSensitive)
                && D.NameKey.Equals(D.Id + TEXT(".name"), ESearchCase::CaseSensitive)
                && D.DescriptionKey.Equals(D.Id + TEXT(".description"), ESearchCase::CaseSensitive)
                && D.IconId == (BranchIcons.Contains(D.Id) ? BranchIcons[D.Id] : TEXT("icon_") + D.Id), Path,
                TEXT("unknown or noncanonical display reference"))
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
        // A final mod's row splits its description into effect, cost and notes lines; on any other row they are unknown fields.
        if (Out.FinalMods.ContainsByPredicate([&D](const FLDDemoFinalModDefinition& Mod) { return Mod.Id == D.Id; }))
        {
            struct FPart { const TCHAR* Name; FString* Key; FString* Text; };
            const FPart Parts[] = {{TEXT("effect"), &D.EffectKey, &D.EffectText}, {TEXT("cost"), &D.CostKey, &D.CostText},
                {TEXT("notes"), &D.NotesKey, &D.NotesText}};
            for (const FPart& Part : Parts)
            {
                const FString Name(Part.Name);
                if (!Fields.Text(*(Name + TEXT("_key")), *Part.Key, 1, 128)
                    || !Fields.Text(*(Name + TEXT("_text")), *Part.Text, 1, 320)
                    || !ValidateDisplayText(*Part.Text, false, C, Path + TEXT(".") + Name + TEXT("_text"))
                    || !C.Require(Part.Key->Equals(D.Id + TEXT(".") + Name, ESearchCase::CaseSensitive), Path,
                        TEXT("unknown or noncanonical display reference")))
                {
                    return false;
                }
            }
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

// #147/#198: every placeholder of an active's or a card's description reads one of its effects; of a stat's, a
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
        // Each rank step's one-line text reads the variant of its own branch path.
        for (const FLDDemoAbilityBranch& Branch : Ability.Branches)
        {
            const FString Id = LDSkillBranch::DisplayId(Ability.Id, Branch.Path);
            TArray<LDDemoCardText::FToken> Steps;
            if (!Tokens(Id, Steps)) { return false; }
            for (const LDDemoCardText::FToken& Token : Steps)
            {
                if (Token.bValue && !Check.Require(LDDemoCardText::FindEffect(Branch.Values, Token) != nullptr,
                    TEXT("content.display.") + Id, TEXT("a branch placeholder names no effect of its variant")))
                {
                    return false;
                }
            }
        }
    }
    for (const FLDDemoCardDefinition& Card : Out.Cards)
    {
        TArray<LDDemoCardText::FToken> Parsed;
        if (!Tokens(Card.Id, Parsed))
        {
            return false;
        }
        for (const LDDemoCardText::FToken& Token : Parsed)
        {
            if (Token.bValue && !Check.Require(LDDemoCardText::FindEffect(Card.Effects, Token) != nullptr,
                TEXT("content.display.") + Card.Id, TEXT("a card placeholder names no effect of this card")))
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
        TEXT("#pragma once\n#define LD_DEMO_CONTENT_SCHEMA_VERSION 3\n#define LD_DEMO_CONTENT_HANDLER_REVISION 10\n")
        TEXT("#define LD_DEMO_CONTENT_PROJECTION_REVISION 9\n#define LD_DEMO_CONTENT_PAYLOAD_SHA256 TEXT(\"%s\")\n")
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
        TEXT("#pragma once\n#define LD_DEMO_WORLD_SCHEMA_VERSION %d\n#define LD_DEMO_WORLD_PROJECTION_REVISION %d\n")
        TEXT("#define LD_DEMO_WORLD_PAYLOAD_SHA256 TEXT(\"%s\")\n"),
        LD_DEMO_WORLD_SCHEMA_VERSION, LD_DEMO_WORLD_PROJECTION_REVISION, *Identity.World.ToHex());
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
        || !Root.SmallInt(TEXT("schema_version"), Out.SchemaVersion, 3, 3)
        || !Root.SmallInt(TEXT("handler_revision"), Out.HandlerRevision, 10, 10)
        || !Root.SmallInt(TEXT("projection_revision"), Out.ProjectionRevision, 9, 9)
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
    if (!Contract.SmallInt(TEXT("schema_version"), Out.WorldSchemaVersion, LD_DEMO_WORLD_SCHEMA_VERSION, LD_DEMO_WORLD_SCHEMA_VERSION)
        || !Contract.SmallInt(TEXT("projection_revision"), Out.WorldProjectionRevision,
            LD_DEMO_WORLD_PROJECTION_REVISION, LD_DEMO_WORLD_PROJECTION_REVISION)
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
        {TEXT("refund"), EField::Refund}, {TEXT("burst"), EField::Burst}, {TEXT("limit"), EField::Limit},
        {TEXT("multiplier"), EField::Multiplier}, {TEXT("angle"), EField::Angle}, {TEXT("cost_ratio"), EField::CostRatio},
        {TEXT("extra_duration"), EField::ExtraDuration}, {TEXT("step"), EField::Step}};
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
