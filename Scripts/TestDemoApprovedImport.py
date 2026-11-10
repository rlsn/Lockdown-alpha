"""Exact approved content baseline, world contract, and world-element function tests on synthetic geometry.

Not a native or play-balance test, and nothing here pins the current map layout.
"""
from decimal import Decimal
from fractions import Fraction
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

from CatalogTools import encode
import DemoContent as content
import DemoWorld as world
import SyncDemoContentData as content_sync
import SyncDemoWorldData as world_sync


def amount(value):
    return content.quantity(value)


# #148: memes that rule text no longer uses; entry names that contain them (他急了, 无影大比兜) are kept.
RETIRED_RULE_TERMS = ("精神状态", "电量", "力大砖飞", "阴阳怪气", "嘴硬", "心大", "手速", "润速", "整活频率", "物理超度",
                      "精神攻击", "心理防线", "大比兜", "提裤子", "捂裆", "CPU 干烧", "瞅你咋地", "腿软", "上头", "蹲到了",
                      "破防了", "免死金牌", "回暖", "回电")


def r(text):
    return Fraction(text)


def author_units(value):
    """A projected effect in author units (points/hp/mp, seconds, ratios, cm), without its op and opcode."""
    if type(value) in (int, str, bool, tuple):
        return value
    if "unit" in value:
        return amount(value)
    return {key: author_units(child) for key, child in value.items() if key not in ("op", "opcode")}


# #198 actives (CONTENT_AND_BALANCE, issue #198 approval 2026-11-10): one value set each at G=1, in S order.
ACTIVE_IDS = ("active_elbow", "active_pants_down", "active_crotch_grab", "active_account_banned", "active_unplug",
              "active_snark_bolt", "active_seethe", "active_quick_meeting", "active_hose_down", "active_zap",
              "active_bail_out", "active_apology_slide", "active_charged_ram", "active_not_mad", "active_stare_down",
              "active_brace", "active_triple_combo", "active_calculus_drop", "active_flyer_trap", "active_dirty_puddle",
              "active_sock_shot", "active_open_mic")
# (target mode, range cm or None, windup s, mana, cooldown s, in the opening pool).
ACTIVE_FRAMES = {
    "active_elbow": ("Unit", 220, r(".15"), 50, 10, True),
    "active_pants_down": ("Unit", 400, r(".2"), 55, 12, True),
    "active_crotch_grab": ("Unit", 200, r(".2"), 55, 14, True),
    "active_account_banned": ("Unit", 600, r(".25"), 60, 13, True),
    "active_unplug": ("Unit", 300, r(".2"), 45, 12, True),
    "active_snark_bolt": ("DirectionProjectile", 750, r(".2"), 45, 8, True),
    "active_seethe": ("Self", None, r(".25"), 55, 10, True),
    "active_quick_meeting": ("GroundPoint", 650, r(".25"), 60, 12, True),
    "active_hose_down": ("DirectionLine", 550, r(".3"), 50, 11, True),
    "active_zap": ("Unit", 650, r(".25"), 55, 11, True),
    "active_bail_out": ("DirectionDash", None, 0, 40, 9, True),
    "active_apology_slide": ("DirectionDash", None, r(".15"), 50, 11, True),
    "active_charged_ram": ("DirectionDash", None, r(".1"), 55, 13, True),
    "active_not_mad": ("Self", None, 0, 50, 14, False),
    "active_stare_down": ("Self", None, r(".2"), 60, 15, False),
    "active_brace": ("Self", None, r(".1"), 55, 16, False),
    "active_triple_combo": ("Self", None, 0, 40, 12, True),
    "active_calculus_drop": ("GroundPoint", 650, r(".25"), 65, 12, True),
    "active_flyer_trap": ("GroundPoint", 550, r(".15"), 55, 12, False),
    "active_dirty_puddle": ("GroundPoint", 600, r(".25"), 60, 14, True),
    "active_sock_shot": ("DirectionProjectile", 650, r(".15"), 50, 12, True),
    "active_open_mic": ("DirectionCone", 450, r(".2"), 50, 10, True),
}


def magical(base, ap, hp=0):
    return ("damage", {"ad_ratio": 0, "ap_ratio": r(ap), "base": base, "damage_type": "Magical", "max_hp_ratio": r(hp)})


def physical(base, ad, ap=0):
    return ("damage", {"ad_ratio": r(ad), "ap_ratio": r(ap), "base": base, "damage_type": "Physical", "max_hp_ratio": 0})


# Every active's effects in authored order and author units; "base" grows with the owner's level at runtime.
ACTIVE_EFFECTS = {
    "active_elbow": [physical(75, ".65"), ("knockback", {"distance": 300, "duration": r(".25")})],
    "active_pants_down": [physical(55, ".45"), ("root", {"duration": r("1.5")})],
    "active_crotch_grab": [physical(85, ".6"), ("stun", {"duration": 1})],
    "active_account_banned": [magical(65, ".65"), ("silence", {"duration": r("1.6")})],
    "active_unplug": [physical(50, ".45"), ("disarm", {"duration": r("1.8")})],
    "active_snark_bolt": [magical(90, ".8"), ("slow", {"duration": r("1.5"), "ratio": r(".25")})],
    "active_seethe": [("self_area", {"radius": 300}), magical(65, ".55", ".02"), ("burn", {"stacks": 2})],
    "active_quick_meeting": [
        ("zone", {"count": 3, "delay": 0, "duration": 3, "interval": 1, "radius": 240}),
        ("zone_damage", {"ad_ratio": 0, "ap_ratio": r(".2"), "base": 28, "damage_type": "Magical"}),
        ("zone_slow", {"duration": r(".5"), "interval": 1, "ratio": r(".2")}),
        ("zone_burn", {"interval": 1, "stacks": 1})],
    "active_hose_down": [magical(55, ".55"), ("knockback", {"distance": 180, "duration": r(".15")}), ("wet", {})],
    "active_zap": [magical(60, ".55"), ("electrify", {}),
                   ("chain_bounce", {"count": 2, "interval": r(".15"), "radius": 350, "ratio": r(".7")})],
    "active_bail_out": [
        ("dash", {"distance": 350, "duration": r(".2")}),
        ("empower_basic", {"ad_ratio": r(".35"), "ap_ratio": 0, "base": 40, "damage_type": "Physical", "duration": 3})],
    "active_apology_slide": [
        ("dash", {"distance": 400, "duration": r(".3")}), ("dash_contact", {"targets": "First"}), physical(65, ".5", ".3"),
        ("slow", {"duration": r("1.5"), "ratio": r(".25")})],
    "active_charged_ram": [
        ("dash", {"distance": 450, "duration": r(".3")}),
        ("shield", {"ap_ratio": 0, "base": 60, "duration": 2, "max_hp_ratio": r(".04")}),
        ("dash_contact", {"targets": "Every"}), magical(35, ".35"), ("electrify", {})],
    "active_not_mad": [
        ("shield", {"ap_ratio": r(".6"), "base": 90, "duration": 3, "max_hp_ratio": r(".05")}), ("dispel_slow", {}), ("dispel_root", {})],
    "active_stare_down": [
        ("taunt", {"duration": r("1.1"), "radius": 300}),
        ("timed_stat_bonus", {"duration": 3, "stats": {"ArmorFlat": 14, "MagicResistFlat": 14}})],
    "active_brace": [
        ("brace", {"base": 50, "duration": 3, "limit": 100, "radius": 300, "ratio": r(".2"), "record_ratio": r(".6")})],
    "active_triple_combo": [
        ("empower_basic", {"ad_ratio": r(".1"), "ap_ratio": 0, "base": 16, "charges": 3, "damage_type": "Physical", "duration": 5,
                           "stats": {"AttackSpeedBonusRatio": r(".25")}})],
    "active_calculus_drop": [
        ("ground_impact", {"delay": r(".55"), "inner_radius": 80, "outer_ratio": r(".6"), "radius": 200}), magical(100, ".9")],
    "active_flyer_trap": [
        ("contact_trap", {"delay": r(".45"), "duration": 6, "radius": 180, "trigger_radius": 100}), magical(100, ".75"),
        ("slow", {"duration": r("1.5"), "ratio": r(".3")})],
    "active_dirty_puddle": [
        ("zone", {"count": 4, "delay": 0, "duration": 4, "interval": 1, "radius": 220}),
        ("zone_damage", {"ad_ratio": 0, "ap_ratio": r(".15"), "base": 18, "damage_type": "Magical"}),
        ("zone_wet", {}), ("zone_haste", {"ratio": r(".15")})],
    "active_sock_shot": [
        magical(70, ".65"), ("attack_slow", {"duration": 3, "ratio": r(".2")}), ("grievous", {"duration": 3, "ratio": r(".35")})],
    "active_open_mic": [
        ("channel_cone", {"count": 3, "interval": r(".3"), "slow": r(".3"), "slow_duration": r("1.2")}), magical(40, ".3")],
}
# #198 five point-bought stats: native id and the bonuses of one point.
STAT_POINTS = {
    "stat_physique": (27, {"MaxHPFlat": 30, "HPRegenFlat": r(".08")}),
    "stat_thick_skin": (28, {"ArmorFlat": r("2.5"), "MagicResistFlat": r("2.5")}),
    "stat_strength": (29, {"ADFlat": r("2.5"), "CritChanceBonusRatio": r(".004")}),
    "stat_brains": (30, {"APFlat": 5, "CooldownHasteFlat": 1, "MPRegenFlat": r(".03")}),
    "stat_agility": (31, {"AttackSpeedBonusRatio": r(".02"), "MoveSpeedBonusRatio": r(".005")}),
}


# #198 B base components B01-B18: (display name, opening "always" / actives / None, effects in author units at G=1).
DIRECT_DAMAGE_OPENING = ["active_elbow", "active_pants_down", "active_crotch_grab", "active_account_banned", "active_unplug",
                         "active_snark_bolt", "active_seethe", "active_quick_meeting", "active_apology_slide",
                         "active_calculus_drop", "active_open_mic", "active_hose_down", "active_zap",
                         "active_charged_ram", "active_dirty_puddle", "active_sock_shot"]
COMPONENT_FRAMES = {
    "component_fist_fire": ("拳头冒火", "always", [("burn", {"basic_stacks": 1})]),
    "component_tab_ledger": ("揍一下记一笔", "always", [
        ("ledger", {"ad_ratio": 0, "ap_ratio": r(".08"), "base": 10, "count": 5, "damage_type": "Magical", "duration": 6})]),
    "component_spiky_hide": ("打我也硌手", "always", [
        ("thorns", {"armor_ratio": 0, "base": 10, "critical_multiplier": 1, "damage_type": "Magical", "max_hp_ratio": r(".01"),
                    "radius": 600, "trigger": "Basic"})]),
    "component_punch_heal": ("揍人续命", "always", [("lifesteal", {"ratio": r(".12")})]),
    "component_dash_bump": ("撞你个趔趄", "always", [
        ("dash_contact_damage", {"ad_ratio": r(".25"), "ap_ratio": r(".25"), "base": 30, "damage_type": "Physical"})]),
    "component_third_punch": ("第三拳滚蛋", "always", [("nth_basic_bonus", {"count": 3, "distance": 120, "duration": r(".12")})]),
    "component_kick_down": ("逮住就往狠了打", ["active_elbow", "active_pants_down", "active_crotch_grab", "active_account_banned",
                                       "active_unplug", "active_snark_bolt", "active_quick_meeting", "active_apology_slide",
                                       "active_open_mic", "active_hose_down", "active_dirty_puddle"], [("control_damage_amp", {"ratio": r(".12")})]),
    "component_breach_fist": ("越揍越不经打", None, [("breach", {"basic_stacks": 1})]),
    "component_hard_skin": ("挨揍起硬皮", "always", [
        ("triggered_shield", {"base": 45, "duration": 3, "interval": 10, "max_hp_ratio": r(".035"), "trigger": "Damaged"})]),
    "component_fist_water": ("拳头甩脏水", "always", [("basic_element", {"element": "water"})]),
    "component_fist_shock": ("拳头漏电", ["active_seethe", "active_quick_meeting", "active_hose_down", "active_dirty_puddle"],
                     [("basic_element", {"element": "electric"})]),
    "component_dash_punch": ("蹿过去抡一拳", "always", [
        ("empower_basic", {"ad_ratio": r(".25"), "ap_ratio": 0, "base": 25, "charges": 1, "damage_type": "Physical", "duration": 3,
                           "trigger": "DashComplete"})]),
    "component_catch_breath": ("喘口气接着干", None, [("stat_bonus", {"stats": {"CooldownHasteFlat": 15, "MPRegenFlat": 1}})]),
    "component_long_arm": ("伸长胳膊抽你", None, [
        ("stat_bonus", {"stats": {"AttackRangeCmFlat": 120}}), ("basic_damage_scale", {"ratio": r(".9")})]),
    "component_seeing_red": ("打急眼了", None, [
        ("low_hp_stat_bonus", {"stats": {"AttackSpeedBonusRatio": r(".25"), "MoveSpeedBonusRatio": r(".1")}, "threshold": r(".4")})]),
    "component_thick_hide": ("脸皮磨厚点", None, [("primary_guard", {"base": 8, "ratio": r(".15")})]),
    "component_held_shot": ("憋着一发狠的", DIRECT_DAMAGE_OPENING, [("idle_ready_amp", {"delay": 2, "ratio": r(".2")})]),
    "component_dirty_trick": ("招里掺点脏的", DIRECT_DAMAGE_OPENING, [("active_element", {})]),
}
# #198 F mods M01-M12 and connectors C01-C12 in native ID order (catalog sections 8-10): name, kind, pool.
WORKSHOP_FRAMES = {
    "component_crowd_hit": ("一群人一起挨", "mod", "common"),
    "component_settle_later": ("等会儿一起算", "mod", "common"),
    "component_long_kick": ("一脚踹出老远", "mod", "common"),
    "component_hold_down": ("按住多揍一会", "mod", "common"),
    "component_reach_out": ("站远点照样抽你", "mod", "common"),
    "component_again_now": ("再来一下别喘", "mod", "common"),
    "component_one_slam": ("合一块砸你", "mod", "common"),
    "component_ram_through": ("闷头撞到底", "mod", "common"),
    "component_quick_hop": ("蹿两步就回头", "mod", "common"),
    "component_big_tab": ("攒够一顿狠的", "mod", "common"),
    "component_pick_you": ("就逮着你揍", "mod", "scarce"),
    "component_owe_it": ("这顿打先欠着", "mod", "scarce"),
    "component_fire_skin": ("烧着还挺扛揍", "connector", "common"),
    "component_ball_kick": ("拿你当球踢", "connector", "common"),
    "component_bump_again": ("撞着了再撞", "connector", "common"),
    "component_roll_bump": ("滚过去撞他", "connector", "common"),
    "component_pin_punch": ("摁住再补一拳", "connector", "common"),
    "component_after_pin": ("炸完再摁住", "connector", "common"),
    "component_control_mana": ("控住薅点蓝", "connector", "common"),
    "component_breath_strike": ("回口气就抽你", "connector", "common"),
    "component_blast_chase": ("炸完追着揍", "connector", "common"),
    "component_shell_burst": ("壳碎了也崩你", "connector", "common"),
    "component_hurt_heal": ("打疼了我回血", "connector", "common"),
    "component_dirty_fist": ("拳上的脏活也算", "connector", "scarce"),
}


class ApprovedDemoData(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.author = content.compile_author()
        cls.catalog = cls.author.catalog
        cls.world = world.compile_world(author=cls.author)
        cls.root_bundle = content.compile_content()
        cls.data = cls.catalog.data
        cls.geometry = world.Geometry(cls.world.world)
        cls.anchors = {row["id"]: row for row in cls.world.world["anchors"]}

    def test_approval_and_complete_selected_directory(self):
        approval = self.data["identity"]["approval"]
        self.assertEqual(approval["status"], "approved")
        # A content-only approval does not rewrite the independently approved world geometry.
        self.assertEqual(self.world.world["approval"]["status"], "approved")
        for text in ("2026-11-02", "D01-D03", "2026-11-04", "#82", "playtest", "not a balance"):
            self.assertIn(text, approval["decision"])
        self.assertNotIn("TEST ONLY", approval["decision"])
        self.assertIn("camera 2250", approval["decision"])
        for text in ("2026-11-07 movement -25%", "flagpole 180s", "vision/leash 1050", "#147 build v7",
                     "2026-11-08 #166 level 16/QWE", "#97 camp growth stages, Elite camp tier",
                     "2026-11-09 #144 equipment, gold and shops removed",
                     "2026-11-10 #198 v0.11 workshop growth (stats/points/single-level actives) approved: 全部按当前规范批准，进入运行数据"):
            self.assertIn(text, approval["decision"])
        self.assertEqual((len(self.data["abilities"]), len(self.data["stats"]), len(self.data["camps"]["monsters"])), (22, 5, 3))
        self.assertEqual([row["id"] for row in self.data["abilities"]], sorted(ACTIVE_IDS))
        self.assertFalse({"attributes", "opening_cards"} & set(self.data))
        self.assertEqual((self.data["identity"]["schema_version"], self.data["identity"]["handler_revision"],
                          self.data["identity"]["projection_revision"]), (2, 3, 5))
        self.assertEqual(self.data["character"]["basic_attack"]["interval_ms"], 1112)
        self.assertEqual(self.data["character"]["basic_attack"]["damage_type"], "Physical")
        self.assertEqual(self.data["character"]["basic_attack"]["ad_ratio"]["bp"], 10000)

    def test_every_approved_parameter_value_and_unit(self):
        expected = {}
        for stat, unit, values in (
            ("MaxHP", "hp", ("620", "58", "1", "3000")),
            ("MaxMP", "mp", ("280", "22", "0", "800")),
            ("AD", "points", ("70", "3.2", "0", "250")),
            ("AP", "points", ("0", "0", "0", "250")),
            ("Armor", "points", ("18", "2", "0", "200")),
            ("MagicResist", "points", ("18", "2", "0", "200")),
            ("HPRegen", "hp/s", ("1.60", "0.12", "0", "12")),
            ("MPRegen", "mp/s", ("1.20", "0.08", "0", "4")),
        ):
            for suffix, value in zip(("Base", "PerLevel", "Min", "Max"), values):
                expected[f"STAT.{stat}.{suffix}"] = (unit, value)
        for key, unit, value in (
            ("STAT.BaseAttackSpeed", "attacks/s", ".90"), ("STAT.AttackSpeedPerLevelRatio", "ratio", ".02"),
            ("STAT.AttackSpeed.Min", "attacks/s", ".30"), ("STAT.AttackSpeed.Max", "attacks/s", "2.30"),
            ("STAT.CritChance.Base", "ratio", "0"), ("STAT.CritMultiplier", "ratio", "2"),
            ("STAT.CooldownHaste.Base", "points", "0"), ("STAT.CooldownHaste.Max", "points", "60"),
            ("SKILL.GrowthPerLevel", "ratio", ".15"), ("SKILL.GrowthPerLevelAfter10", "ratio", ".075"),
            ("UNIT.MoveSpeedCmPerSec", "cm/s", "300"), ("STAT.MoveSpeed.Min", "cm/s", "173"), ("STAT.MoveSpeed.Max", "cm/s", "675"),
            ("UNIT.CollisionRadiusCm", "cm", "40"), ("UNIT.BasicAttackRangeCm", "cm", "220"),
            ("STAT.AttackRange.Min", "cm", "80"), ("STAT.AttackRange.Max", "cm", "1000"),
            ("UNIT.BasicAttackWindupSec", "ms", ".250"), ("VISION.RadiusCm", "cm", "1050"),
            ("CAPTURE.RadiusCm", "cm", "300"),
            ("CAPTURE.SpraySec", "ms", "3.5"), ("CAPTURE.GuardSec", "ms", "30"),
            # #198 match timeline: three 180 s field segments, rest phases and field respawn waits by T at death
            # (approved https://github.com/rlsn/Lockdown-alpha/issues/198#issuecomment-6752628806).
            ("MATCH.FieldSegmentSec", "ms", "180"),
            ("REST.OpeningBuildSec", "ms", "180"), ("REST.BuildSec", "ms", "120"), ("REST.SignupSec", "ms", "10"),
            ("REST.DrawSec", "ms", "5"), ("REST.BettingSec", "ms", "8"), ("REST.PKReadySec", "ms", "3"),
            ("REST.PKMaxSec", "ms", "60"), ("REST.SettlementSec", "ms", "3"), ("REST.PayoutSec", "ms", "12"),
            ("REST.ReturnSec", "ms", "5"), ("REST.ReturnSearchRadiusCm", "cm", "300"),
            ("REST.EntryReadyTimeoutSec", "ms", "30"),
            # #198 PK ring, format and spectator throw (approved
            # https://github.com/rlsn/Lockdown-alpha/issues/198#issuecomment-6752628806).
            ("PK.ArenaRadiusCm", "cm", "1800"), ("PK.RingMidRadiusCm", "cm", "600"), ("PK.RingFinalRadiusCm", "cm", "150"),
            ("PK.RingShrinkStartSec", "ms", "10"), ("PK.RingMidSec", "ms", "40"), ("PK.RingFinalSec", "ms", "55"),
            ("PK.RingPreviewSec", "ms", "10"), ("PK.RingDamageTickSec", "ms", ".25"),
            ("PK.RingDamageEarlyMaxHPRatioPerSec", "ratio", ".05"), ("PK.RingDamageMidMaxHPRatioPerSec", "ratio", ".12"),
            ("PK.RingDamageFinalMaxHPRatioPerSec", "ratio", ".25"), ("PK.TeamFormatRatio", "ratio", ".5"),
            ("THROW.CooldownSec", "ms", "10"), ("THROW.TelegraphSec", "ms", ".8"), ("THROW.RadiusCm", "cm", "120"),
            ("THROW.RangeCm", "cm", "4000"), ("THROW.SlowRatio", "ratio", ".2"), ("THROW.SlowSec", "ms", "1"),
            ("THROW.ExposeSec", "ms", "2"),
            ("LIFE.RespawnSec.Field1", "ms", "8"), ("LIFE.RespawnSec.Field2", "ms", "10"),
            ("LIFE.RespawnSec.Field3", "ms", "12"), ("LIFE.RespawnSec.Final", "ms", "15"),
            ("LIFE.ProtectionSec", "ms", "3"),
            ("REGEN.OutOfCombatDelaySec", "ms", "5"), ("REGEN.OutOfCombatMaxHPRatioPerSec", "ratio", ".015"),
            ("REGEN.OutOfCombatMaxMPRatioPerSec", "ratio", ".015"),
            ("DORM.RadiusCm", "cm", "400"), ("DORM.OutOfCombatDelaySec", "ms", "3"),
            ("DORM.MaxHPRatioPerSec", "ratio", ".1"), ("DORM.MaxMPRatioPerSec", "ratio", ".1"),
            ("CAMP.ResetArrivalRadiusCm", "cm", "100"),
            ("CAMP.ReturnDeadEndSec", "ms", "6"), ("CAMP.GrowthStageSec", "ms", "60"),
            ("CAMP.GrowthHPRatioPerStage", "ratio", ".12"), ("CAMP.GrowthAttackRatioPerStage", "ratio", ".08"),
            ("CAMP.GrowthResistPerStage", "points", "2"), ("CAMP.GrowthRewardXPRatioPerStage", "ratio", ".1"),
            # #198 outer §5 (approved https://github.com/rlsn/Lockdown-alpha/issues/198#issuecomment-6752601175, comment
            # 6752628806: "批准整套方案，按票内参数实施"): six drops at explicit T, 40 s expiry, camp markers at segment+15 s.
            ("AIRDROP.ExpireSec", "ms", "40"), ("CAMPMARK.RevealSec", "ms", "15"),
            ("AIRDROP.AnnounceLeadSec", "ms", "15"), ("AIRDROP.PickupRadiusCm", "cm", "150"),
            # #198 outer parameters: charm waves at T=30/120/.../570, 45 field seconds each.
            ("CHARM.FirstWaveSec", "ms", "30"), ("CHARM.WaveIntervalSec", "ms", "90"), ("CHARM.LifetimeSec", "ms", "45"),
            ("CHARM.PickupRadiusCm", "cm", "100"), ("CHARM.HealMaxHPRatio", "ratio", ".20"),
            ("CHARM.ManaMaxMPRatio", "ratio", ".25"), ("CHARM.MoveSpeedRatio", "ratio", ".20"), ("CHARM.MoveSpeedSec", "ms", "6"),
            ("LEADER.RefreshSec", "ms", "10"), ("LEADER.CircleRadiusCm", "cm", "1500"),
            ("LEADER.CircleMaxOffsetCm", "cm", "1000"),
            ("REWARD.AssistWindowSec", "ms", "15"), ("GROWTH.BuildSafeWindowSec", "ms", "4"),
            ("AI.RetreatHPRatio", "ratio", ".25"), ("AI.ResumeFarmHPRatio", "ratio", ".60"),
            ("AI.HighCampEntryHPRatio", "ratio", ".80"), ("AI.HighCampReserveHPRatio", "ratio", ".35"),
            ("AI.LowCampReserveHPRatio", "ratio", ".15"), ("AI.CaptureReactionBudgetSec", "ms", "2"),
            ("AI.DecisionIntervalSec", "ms", ".25"), ("AI.RetryPolicy.InitialBackoffSec", "ms", ".25"),
            ("AI.RetryPolicy.MaxBackoffSec", "ms", ".50"), ("AI.ExploreRevisitIntervalSec", "ms", "20"),
            ("AI.EngagementBudgetSec", "ms", "40"),
            ("COMBAT.BreachPerStack", "points", "3"), ("COMBAT.BreachSec", "ms", "4"),
            ("COMBAT.BurnPerSec", "points", "3"), ("COMBAT.BurnAPRatioPerSec", "ratio", ".03"),
            ("COMBAT.BurnTickSec", "ms", ".5"), ("COMBAT.BurnSec", "ms", "3"), ("COMBAT.LifestealCap", "ratio", ".35"),
            # #198 elements and reactions (CONTENT_AND_BALANCE §4.4).
            ("COMBAT.WetSec", "ms", "4"), ("COMBAT.WetSlow", "ratio", ".15"), ("COMBAT.ElectrifiedSec", "ms", "4"),
            ("COMBAT.DischargeRadiusCm", "cm", "350"), ("COMBAT.DischargeIntervalSec", "ms", "1"),
            ("COMBAT.ReactionBase", "points", "20"), ("COMBAT.ReactionPerLevel", "points", "1"),
            ("COMBAT.ReactionAPRatio", "ratio", ".2"), ("COMBAT.FogRadiusCm", "cm", "250"), ("COMBAT.FogSec", "ms", "3"),
            ("COMBAT.FogIntervalSec", "ms", "1"), ("COMBAT.ConductRadiusCm", "cm", "450"), ("COMBAT.ConductSlow", "ratio", ".3"),
            ("COMBAT.ConductSlowSec", "ms", "1.5"), ("COMBAT.OverloadRadiusCm", "cm", "200"),
        ):
            expected[key] = (unit, value)
        for key, value in (
            ("XP.BaseToNext", 80), ("XP.IncreasePerLevel", 20),
            ("REWARD.PlayerXP.Base", 100), ("REWARD.PlayerXP.PerLevel", 20), ("AI.RetryPolicy.MaxAttempts", 3),
            ("CAMP.ReturnDeadEndPlans", 3), ("CAMP.GrowthMaxStage", 10), ("AIRDROP.RewardXP", 200),
            ("CHARM.WaveCount", 7), ("CHARM.PerWave", 2),
            ("LEADER.MinLevel", 3),
            ("COMBAT.BreachMaxStacks", 5), ("COMBAT.BurnMaxStacks", 5), ("COMBAT.OverloadBreachStacks", 2),
            # #198 points: 3 at the opening, 3 per level-up, then 3 per full 400 XP past level 16.
            ("GROWTH.OpeningPoints", 3), ("GROWTH.PointsPerLevel", 3), ("GROWTH.MaxLevelPointsXP", 400), ("GROWTH.MaxLevelPoints", 3),
            # #198: a rest NPC charges 3 points for one active-only three-choice.
            ("GROWTH.NpcActivePoints", 3),
            # #198 PK points: 1 for every entrant, winners share at most 3, betting pool at most 2.
            ("PK.ParticipationPoints", 1), ("PK.WinnerPointsMax", 3), ("PK.BettingPoolMaxPoints", 2),
            # #198 B: a level-up whose component roll finds the common pool empty gives 3 points instead.
            ("GROWTH.EmptyComponentPoolPoints", 3),
            # #198 G (spec §4.5): FillGap costs 4 points; dismantle takes 3 distinct common components for 1.
            ("GROWTH.FillGapPoints", 4), ("GROWTH.DismantleInputCount", 3),
        ):
            expected[key] = (None, value)
        expected["AIRDROP.LandSec"] = ("ms", ("45", "125", "225", "305", "405", "485"))
        self.assertEqual(set(self.data["parameters"]), set(expected))
        for key, (unit, value) in expected.items():
            with self.subTest(key=key):
                actual = self.catalog.parameter(key)
                if unit is None:
                    self.assertIs(type(actual), int)
                    self.assertEqual(actual, value)
                elif type(value) is tuple:
                    self.assertEqual([row["unit"] for row in actual], [unit] * len(value))
                    self.assertEqual([amount(row) for row in actual], [Fraction(item) for item in value])
                else:
                    self.assertEqual(actual["unit"], unit)
                    self.assertEqual(amount(actual), Fraction(value))

    def test_explicit_formulas_and_fixed_product_policies(self):
        self.assertEqual(self.data["formulas"]["AttackMoveAcquireRadius"], "MinVisionTwiceAttackRange")
        self.assertEqual(self.data["formulas"]["Resistance"], "PointsOverScalePlusPoints")
        self.assertEqual(self.data["formulas"]["ResourceMaxChange"], "FloorFinalRatioRejectAliveUnderflow")
        self.assertEqual(amount(self.data["formulas"]["ResistanceScale"]), 100)
        self.assertEqual(amount(self.data["formulas"]["HasteScale"]), 100)
        self.assertEqual(self.data["policies"]["RootBlocks"], ("Move", "Dash"))
        self.assertEqual(self.data["policies"]["CampAggroTrigger"], "ConfirmedCombatHit")
        self.assertEqual(self.data["policies"]["MissingSourceCooldown"], "ContinueServerTime")
        self.assertEqual(min(amount(self.catalog.parameter("VISION.RadiusCm")),
                             2 * amount(self.catalog.parameter("UNIT.BasicAttackRangeCm"))), 440)

    def test_respawn_protection_is_display_metadata_not_an_effect_or_duration_change(self):
        ids = content.parse_json((content.ROOT / content.IDS_SOURCE).read_bytes(), "native IDs")
        self.assertEqual(ids["statuses"], {"root": 1, "shield": 2, "camp_returning": 3, "respawn_protection": 4,
                                           "slow": 5, "stat_boost": 6, "exposed": 9,
                                           "taunt": 10, "disarm": 12, "stun": 13, "empowered": 14,
                                           "knockback": 20, "silence": 25, "burn": 26,
                                           "attack_slowed": 27, "wet": 30, "electrified": 31, "braced": 32,
                                           "soured": 33})
        self.assertEqual(ids["effect_ops"], {
            "damage": 1, "shield": 2, "dispel_root": 3, "root": 4, "stat_bonus": 5, "lifesteal": 7, "dash": 8,
            "slow": 12, "timed_stat_bonus": 13, "self_area": 14, "low_hp_stat_bonus": 15,
            "control_damage_amp": 17, "taunt": 18, "disarm": 20, "stun": 21, "empower_basic": 22,
            "thorns": 24, "cooldown_refund": 26, "triggered_shield": 31,
            "nth_basic_bonus": 35, "mana_on_hit": 36, "dispel_slow": 37,
            "knockback": 40, "zone": 50, "zone_slow": 52, "zone_damage": 54, "shield_break_retort": 67,
            "silence": 69, "breach": 70, "burn": 71, "zone_burn": 77,
            "channel_cone": 78, "ground_impact": 79, "attack_slow": 80, "contact_trap": 81, "dash_contact": 82,
            "primary_guard": 86, "dash_contact_damage": 87, "dash_contact_refund": 88,
            "wet": 90, "electrify": 91, "chain_bounce": 92, "brace": 93, "zone_wet": 94, "zone_haste": 95, "grievous": 96,
            "ledger": 100, "basic_element": 101, "basic_damage_scale": 102, "idle_ready_amp": 103, "active_element": 104,
            # #198 F mods and connectors.
            "spread_unit": 120, "spread_projectile": 121, "spread_ledger": 122, "spread_counter": 123, "delay_ledger": 124,
            "delay_counter": 125, "knockback_scale": 126, "control_scale": 127, "range_bonus": 128, "counter_range_bonus": 129,
            "cooldown_cost": 130, "compress_cone": 131, "compress_zone": 132, "dash_scale": 133, "ledger_variant": 134,
            "marked_counter": 135, "damage_defer": 136, "fire_shield": 137, "knockback_impact": 138, "contact_knockback": 139,
            "control_mark": 140, "aftershock": 141, "control_mana": 142, "heal_counter": 143, "reaction_empower": 144,
            "shield_burst": 145, "skill_lifesteal": 146, "extra_base": 147, "compress_chain": 148})
        entries = [row for row in self.data["display"] if row["id"] == "respawn_protection"]
        self.assertEqual(len(entries), 1)
        self.assertEqual({key: value for key, value in entries[0].items() if not key.endswith("_text")}, {
            "id": "respawn_protection", "namespace": "DemoContent",
            "name_key": "respawn_protection.name", "description_key": "respawn_protection.description",
            "icon_id": "icon_respawn_protection"})
        self.assertEqual(entries[0]["name_text"], "复活保护")
        self.assertEqual(self.catalog.parameter("LIFE.ProtectionSec"), {"unit": "ms", "ms": 3000})
        for row in self.data["abilities"]:
            self.assertNotIn("respawn_protection", {effect["op"] for effect in content.all_effects(row)})

    def test_display_names_are_the_approved_fog_harbor_names(self):
        # #111 雾港 v2 entry names (CONTENT_AND_BALANCE 3.2/4/5/6.2/12.5); #148 status names use generic game terms.
        self.assertEqual({row["id"]: row["name_text"] for row in self.data["display"]}, {
            "character_demo": "脆皮大学生",
            # #198 v0.11 actives and the five point-bought stats.
            "active_elbow": "滚远点", "active_pants_down": "扒裤子", "active_crotch_grab": "掏蛋子",
            "active_account_banned": "给老子闭嘴", "active_unplug": "拧你手腕", "active_snark_bolt": "甩你一鞋",
            "active_seethe": "烫你屁股", "active_quick_meeting": "脚底开锅", "active_bail_out": "贴脸补一拳",
            "active_apology_slide": "铲你脚脖子", "active_not_mad": "锅盖挡脸", "active_stare_down": "来打老子啊",
            "active_triple_combo": "左右开抽", "active_calculus_drop": "板砖糊脸", "active_flyer_trap": "踩一脚试试",
            "active_open_mic": "扯嗓子喷你", "active_hose_down": "滋你一脸", "active_zap": "电你个哆嗦",
            "active_charged_ram": "带电撞你", "active_brace": "顶着揍", "active_dirty_puddle": "满地泼脏水",
            "active_sock_shot": "馊水糊脸",
            "stat_physique": "体格", "stat_thick_skin": "脸皮", "stat_strength": "力道", "stat_brains": "脑力", "stat_agility": "身手",
            # #198 B base components B01-B18.
            **{identity: frame[0] for identity, frame in COMPONENT_FRAMES.items()},
            **{identity: frame[0] for identity, frame in WORKSHOP_FRAMES.items()},
            "monster_scavenger": "已读不回的 HR", "monster_guardian": "追着办卡的私教", "monster_dean": "画饼老板",
            "monster_advisor": "催毕设的导师",
            "root": "定身", "shield": "护盾", "camp_returning": "回位", "respawn_protection": "复活保护",
            "slow": "减速", "stat_boost": "增益", "exposed": "破防",
            "taunt": "嘲讽", "disarm": "缴械", "stun": "眩晕", "empowered": "强化普攻",
            "knockback": "击退", "silence": "沉默", "burn": "灼烧", "attack_slowed": "攻速降低",
            "wet": "潮湿", "electrified": "感电", "braced": "顶着揍中", "soured": "馊臭",
        })
        self.assertIn("「来打老子啊」", next(row for row in self.data["display"] if row["id"] == "taunt")["description_text"])
        for row in self.data["display"]:
            with self.subTest(identity=row["id"]):
                self.assertEqual(row["namespace"], "DemoContent")
                self.assertTrue(row["description_text"])
                self.assertIsNone(content.DISPLAY_DESCRIPTION_DIGIT.search(row["description_text"]))
                self.assertIsNone(content.DISPLAY_TEXT_FORBIDDEN.search(row["description_text"]))
                # #148: rule text names attributes, resources, damage, statuses, the basic attack and the dash generically.
                self.assertEqual([term for term in RETIRED_RULE_TERMS if term in row["description_text"]], [])
                # #198: cards have one line; actives and stats read their numbers from placeholders.
                self.assertFalse({"advanced_key", "advanced_text"} & set(row))
                if row["id"] in ACTIVE_FRAMES or row["id"] in STAT_POINTS:
                    self.assertIn("[", row["description_text"])
                if row["id"] in COMPONENT_FRAMES or row["id"] in WORKSHOP_FRAMES:
                    self.assertEqual(row["icon_id"], "icon_" + row["id"])

    def test_respawn_protection_requires_exact_display_keys_without_runtime_state(self):
        author = content.parse_json((content.ROOT / content.CONTENT_SOURCE).read_bytes(), "content author")
        schema = content.AuthorSchema(content.parse_json((content.ROOT / content.SCHEMA_SOURCE).read_bytes(), "content schema"))
        ids = content.parse_json((content.ROOT / content.IDS_SOURCE).read_bytes(), "native IDs")
        schema.validate(author)
        projected = schema.project(author)
        projected["display"] = [row for row in projected["display"] if row["id"] != "respawn_protection"]
        with self.assertRaisesRegex(content.ContentError, "DisplayReference"):
            content.validate_definitions(projected, ids)
        projected = schema.project(author)
        projected["display"].append(dict(next(row for row in projected["display"] if row["id"] == "respawn_protection")))
        with self.assertRaisesRegex(content.ContentError, "DuplicateId"):
            content.validate_definitions(projected, ids)
        for field in ("name_key", "description_key", "icon_id"):
            projected = schema.project(author)
            next(row for row in projected["display"] if row["id"] == "respawn_protection")[field] = "unresolved_reference"
            with self.subTest(field=field), self.assertRaisesRegex(content.ContentError, "DisplayReference"):
                content.validate_definitions(projected, ids)
        for field in ("ExpiresAtServerMs", "RespawnAt", "Life", "BodyIdentity", "Source"):
            candidate = content.parse_json((content.ROOT / content.CONTENT_SOURCE).read_bytes(), "content author")
            next(row for row in candidate["display"] if row["id"] == "respawn_protection")[field] = 1
            with self.subTest(field=field), self.assertRaisesRegex(content.ContentError, "UnknownField"):
                schema.validate(candidate)

    def test_respawn_protection_native_mapping_rejects_omission_alias_and_renumbering(self):
        for change in (
            lambda statuses: statuses.pop("respawn_protection"),
            lambda statuses: statuses.update(respawn_protection=3),
            lambda statuses: statuses.update(respawn_protection=5),
            lambda statuses: statuses.update(respawn_protection=True),
            lambda statuses: statuses.update(unknown_protection=statuses.pop("respawn_protection")),
        ):
            ids = content.parse_json((content.ROOT / content.IDS_SOURCE).read_bytes(), "native IDs")
            change(ids["statuses"])
            with self.subTest(statuses=ids["statuses"]), self.assertRaises(content.ContentError):
                content.validate_native_ids(content.ROOT, ids)

    def test_all_active_values(self):
        # #198: every ability is a single-level active carrying one value set at G=1.
        for identity, (mode, reach, windup, mana, cooldown, opening) in ACTIVE_FRAMES.items():
            with self.subTest(identity=identity):
                row = self.catalog.definition(identity)
                self.assertFalse({"ranks", "kind", "draftable"} & set(row))
                self.assertEqual((row["target"]["mode"], amount(row["windup"]), row["cooldown_family"], row["opening"]),
                                 (mode, windup, "cd_" + identity[len("active_"):], opening))
                self.assertEqual(amount(row["target"]["range"]) if "range" in row["target"] else None, reach)
                self.assertEqual((amount(row["mana_cost"]), amount(row["cooldown"])), (mana, cooldown))
                self.assertEqual([(effect["op"], author_units(effect)) for effect in row["effects"]], ACTIVE_EFFECTS[identity])
        bolt = self.catalog.definition("active_snark_bolt")["target"]
        self.assertEqual((amount(bolt["projectile_radius"]), amount(bolt["projectile_speed"])), (45, 1600))
        sock = self.catalog.definition("active_sock_shot")["target"]
        self.assertEqual((amount(sock["projectile_radius"]), amount(sock["projectile_speed"])), (40, 1300))
        self.assertEqual(amount(self.catalog.definition("active_hose_down")["target"]["half_width"]), 80)
        self.assertEqual(amount(self.catalog.definition("active_open_mic")["target"]["angle"]), 70)

    def test_opening_pool_and_active_offer_levels(self):
        # #198: the opening draws 18 actives (S01-S13, S17, S18, S20-S22); S14-S16/S19 come only from later opportunities.
        self.assertEqual({row["id"] for row in self.data["abilities"] if row["opening"]},
                         {identity for identity, frame in ACTIVE_FRAMES.items() if frame[-1]})
        self.assertEqual({row["id"] for row in self.data["abilities"] if not row["opening"]},
                         {"active_not_mad", "active_stare_down", "active_brace", "active_flyer_trap"})
        self.assertEqual(sum(1 for row in self.data["abilities"] if row["opening"]), 18)
        self.assertEqual(self.data["progression"]["active_offer_levels"], (3, 6))
        self.assertEqual(set(self.data["progression"]),
                         {"max_level", "active_slots", "xp_start", "xp_thresholds", "xp_to_next", "active_offer_levels",
                          "component_category_weights"})

    def test_all_base_component_values_openings_and_category_bands(self):
        # #198 B: B01-B18 at G=1 in native ID order (issue #198 catalog section 7, AI spec section 4.3).
        self.assertEqual([row["id"] for row in self.data["components"]], list(COMPONENT_FRAMES) + list(WORKSHOP_FRAMES))
        for number, (identity, (_, kind, pool)) in enumerate(WORKSHOP_FRAMES.items(), len(COMPONENT_FRAMES) + 1):
            with self.subTest(identity=identity):
                row = self.catalog.definition(identity)
                self.assertEqual((row["native_id"], row["kind"], row["pool"], "opening" in row), (number, kind, pool, False))
        for number, (identity, (name, opening, effects)) in enumerate(COMPONENT_FRAMES.items(), 1):
            with self.subTest(identity=identity):
                row = self.catalog.definition(identity)
                self.assertEqual((row["native_id"], row["kind"], row["pool"]), (number, "base", "common"))
                self.assertEqual(row.get("opening"), opening if type(opening) is not list else {"actives": tuple(opening)})
                self.assertEqual([(effect["op"], author_units(effect)) for effect in row["effects"]], effects)
        self.assertEqual({identity for identity, frame in COMPONENT_FRAMES.items() if frame[1] == "always"},
                         {"component_fist_fire", "component_tab_ledger", "component_spiky_hide", "component_punch_heal",
                          "component_dash_bump", "component_third_punch", "component_hard_skin", "component_fist_water",
                          "component_dash_punch"})
        binding = self.catalog.definition("component_dirty_trick")["binding"]
        self.assertEqual((binding["kind"], binding["elements"]), ("active_element", ("fire", "water", "electric")))
        self.assertEqual(set(binding["actives"]), set(ACTIVE_FRAMES) - {"active_bail_out", "active_not_mad", "active_stare_down",
                                                                        "active_triple_combo"})
        self.assertEqual([(band["levels"], band["base"], band["mod"], band["connector"])
                          for band in self.data["progression"]["component_category_weights"]],
                         [((2, 6), 55, 25, 20), ((7, 11), 40, 30, 30), ((12, 16), 25, 35, 40)])

    def test_retired_content_ids_stay_reserved_and_absent(self):
        ids = content.parse_json((content.ROOT / content.IDS_SOURCE).read_bytes(), "native IDs")
        self.assertEqual(ids["retired_ranges"]["abilities"], [[0, 91], [104, 113], [119, 125]])
        self.assertEqual(ids["retired_ranges"]["stats"], [[0, 26]])
        self.assertIn([28, 29], ids["retired_ranges"]["statuses"])
        self.assertFalse({"attributes"} & set(ids) | {"attributes"} & set(ids["retired_ranges"]))
        # Sock shot returns on its reserved number as S21; no retired ability, card or cower status returns.
        self.assertEqual(ids["abilities"]["active_sock_shot"], 116)
        self.assertEqual(self.catalog.definition("active_sock_shot")["native_id"], 116)
        self.assertFalse([identity for identity in ids["abilities"] if identity.startswith("passive_") or identity == "active_cower"])
        self.assertFalse({"cowering", "contact_guard"} & set(ids["statuses"]))
        self.assertNotIn("STAT.AbilityCritMultiplier", self.data["parameters"])
        self.assertEqual(next(row for row in self.data["display"] if row["id"] == "character_demo")["description_text"],
                         "已成年，一碰就碎但嘴很硬。开局只会普攻；升级攒属性点、挑技能、捡构件。")
        self.assertFalse([row["id"] for row in self.data["display"]
                          if row["id"].startswith(("passive_", "attr_", "opening_")) or row["id"] == "active_cower"])

    def test_camp_perception_fits_inside_vision(self):
        # #97: a camp member's perception (its leash) beyond VISION.RadiusCm would fault the camp decision at T=0.
        author = content.parse_json((content.ROOT / content.CONTENT_SOURCE).read_bytes(), "content author")
        schema = content.AuthorSchema(content.parse_json((content.ROOT / content.SCHEMA_SOURCE).read_bytes(), "content schema"))
        ids = content.parse_json((content.ROOT / content.IDS_SOURCE).read_bytes(), "native IDs")
        schema.validate(author)
        projected = schema.project(author)
        guardian = next(row for row in projected["camps"]["monsters"] if row["id"] == "monster_guardian")
        guardian["leash_radius"].update(numerator=1100, denominator=1)
        content.validate_definitions(projected, ids)
        with self.assertRaisesRegex(content.ContentError, "InvalidGeometry"):
            content.validate_numbers(projected)

    def test_grown_skill_bases_fit_inside_int64_at_the_top_level(self):
        # #147/#198: a base that fits ungrown but overflows once grown for the top level is refused at import,
        # never served as a missing value set.
        author = content.parse_json((content.ROOT / content.CONTENT_SOURCE).read_bytes(), "content author")
        schema = content.AuthorSchema(content.parse_json((content.ROOT / content.SCHEMA_SOURCE).read_bytes(), "content schema"))
        ids = content.parse_json((content.ROOT / content.IDS_SOURCE).read_bytes(), "native IDs")
        schema.validate(author)
        projected = schema.project(author)
        content.validate_definitions(projected, ids)
        bolt = next(row for row in projected["abilities"] if row["id"] == "active_snark_bolt")
        bolt["effects"][0]["base"]["p2"] = 4 * 10 ** 14
        with self.assertRaises(content.ContentError):
            content.validate_numbers(projected)

    def approved_projection_inputs(self):
        author = content.parse_json((content.ROOT / content.CONTENT_SOURCE).read_bytes(), "content author")
        schema = content.AuthorSchema(content.parse_json((content.ROOT / content.SCHEMA_SOURCE).read_bytes(), "content schema"))
        ids = content.parse_json((content.ROOT / content.IDS_SOURCE).read_bytes(), "native IDs")
        schema.validate(author)
        return author, schema, ids

    def test_skill_target_and_domain_refusals(self):
        author, schema, ids = self.approved_projection_inputs()

        def ability(data, identity):
            return next(row for row in data["abilities"] if row["id"] == identity)

        def first(data, identity, op):
            return next(effect for effect in ability(data, identity)["effects"] if effect["op"] == op)

        def display(data, identity):
            return next(row for row in data["display"] if row["id"] == identity)

        for name, mutate, code in (
            ("area on a unit target", lambda d: ability(d, "active_pants_down")["effects"].append(
                {"op": "self_area", "radius": {"unit": "cm", "numerator": 100, "denominator": 1}}), "TargetEffectMismatch"),
            ("slow that stops the body", lambda d: first(d, "active_snark_bolt", "slow")["ratio"].update(bp=10000), "NumericDomain"),
            ("zero-radius area", lambda d: first(d, "active_seethe", "self_area")["radius"].update(numerator=0), "NumericDomain"),
            ("timed resource maximum", lambda d: first(d, "active_stare_down", "timed_stat_bonus")["stats"].update(
                MaxHPFlat={"unit": "hp", "p2": 100}), "UnknownField"),
            ("zone payload without its zone", lambda d: ability(d, "active_quick_meeting")["effects"].remove(
                first(d, "active_quick_meeting", "zone")), "MissingHandler"),
            ("ineffective zone damage", lambda d: [first(d, "active_quick_meeting", "zone_damage")[field].update(**{key: 0})
                                                   for field, key in (("base", "p2"), ("ad_ratio", "bp"), ("ap_ratio", "bp"))],
             "IneffectiveEffect"),
            ("zone slow that stops the body", lambda d: first(d, "active_quick_meeting", "zone_slow")["ratio"].update(bp=10000),
             "NumericDomain"),
            ("zone pulses past its duration", lambda d: first(d, "active_quick_meeting", "zone").update(count=5), "InvalidTiming"),
            ("card placeholder naming an absent effect", lambda d: display(d, "active_snark_bolt").update(
                description_text="眩晕 [stun.duration] 秒。"), "CardTemplate"),
            ("stat placeholder on an active", lambda d: display(d, "active_snark_bolt").update(
                description_text="多 [per_point.APFlat]。"), "CardTemplate"),
            ("stat card naming another stat's bonus", lambda d: display(d, "stat_strength").update(
                description_text="每点 +[per_point.APFlat]。"), "CardTemplate"),
            ("opening pool below one offer", lambda d: [row.update(opening=False) for row in d["abilities"]], "OpeningPool"),
            ("unreachable active offer level", lambda d: d["progression"].update(active_offer_levels=[6, 3]), "ActiveOfferLevels"),
        ):
            projected = schema.project(author)
            mutate(projected)
            with self.subTest(case=name), self.assertRaisesRegex(content.ContentError, code):
                content.validate_definitions(projected, ids)

    def test_point_bought_stats(self):
        # #198: five stats bought with points; one point adds these flat and additive-percent bonuses.
        self.assertEqual({row["id"]: row["native_id"] for row in self.data["stats"]},
                         {identity: native for identity, (native, _) in STAT_POINTS.items()})
        for identity, (_, bonuses) in STAT_POINTS.items():
            with self.subTest(identity=identity):
                row = self.catalog.definition(identity)
                self.assertEqual(set(row), {"id", "native_id", "per_point"})
                self.assertEqual({key: amount(value) for key, value in row["per_point"].items()}, bonuses)
        texts = {row["id"]: row["description_text"] for row in self.data["display"]}
        self.assertEqual(texts["stat_brains"],
                         "每点法术强度 +[per_point.APFlat]、技能急速 +[per_point.CooldownHasteFlat]、法力回复 +[per_point.MPRegenFlat]/秒。")

    def test_expanded_skill_geometry_and_recovery_refusals(self):
        author, schema, ids = self.approved_projection_inputs()
        cases = (
            ("active_open_mic", "channel_cone", "slow_duration", None, "ObjectShape"),
            ("active_calculus_drop", "ground_impact", "inner_radius", {"unit": "cm", "numerator": 201, "denominator": 1}, "InvalidGeometry"),
            ("active_calculus_drop", "ground_impact", "outer_ratio", {"unit": "ratio", "bp": 10000}, "NumericDomain"),
            ("active_flyer_trap", "contact_trap", "trigger_radius", {"unit": "cm", "numerator": 0, "denominator": 1}, "InvalidGeometry"),
            ("active_open_mic", "channel_cone", "slow", {"unit": "ratio", "bp": 10000}, "NumericDomain"),
            # #198 session A.
            ("active_zap", "chain_bounce", "radius", {"unit": "cm", "numerator": 0, "denominator": 1}, "InvalidGeometry"),
            ("active_zap", "chain_bounce", "ratio", {"unit": "ratio", "bp": 10000}, "NumericDomain"),
            ("active_brace", "brace", "ratio", {"unit": "ratio", "bp": 10000}, "NumericDomain"),
            ("active_brace", "brace", "record_ratio", {"unit": "ratio", "bp": 0}, "IneffectiveEffect"),
            ("active_sock_shot", "grievous", "ratio", {"unit": "ratio", "bp": 0}, "NumericDomain"),
            ("active_dirty_puddle", "zone_haste", "ratio", {"unit": "ratio", "bp": 10000}, "NumericDomain"),
        )
        for identity, op, field, value, error in cases:
            projected = schema.project(author)
            row = next(item for item in projected["abilities"] if item["id"] == identity)
            effect = next(item for item in row["effects"] if item["op"] == op)
            if value is None:
                del effect[field]
            else:
                effect[field] = value
            with self.subTest(identity=identity, field=field), self.assertRaisesRegex(content.ContentError, error):
                content.validate_definitions(projected, ids)

    def test_innate_dash(self):
        # D19/D20: the innate D dash belongs to the character.
        dash = self.data["character"]["dash"]
        self.assertEqual((dash["mana_cost"]["p2"], dash["cooldown"]["ms"], amount(dash["distance"]), dash["duration"]["ms"]),
                         (0, 18000, 300, 200))

    def test_retired_mechanics_refuse_on_actives(self):
        # #198 deletes the advanced-rank changes end to end: their operations and fields no longer exist.
        author, schema, ids = self.approved_projection_inputs()
        for identity, effect in (
            ("active_crotch_grab", {"op": "execute", "ratio": {"unit": "ratio", "bp": 3000}, "hp_below": {"unit": "ratio", "bp": 5000}}),
            ("active_snark_bolt", {"op": "pierce"}), ("active_not_mad", {"op": "dispel_disarm"}),
            ("active_quick_meeting", {"op": "zone_root", "duration": {"unit": "ms", "ms": 1000}}),
            ("active_seethe", {"op": "echo", "delay": {"unit": "ms", "ms": 1000}, "ratio": {"unit": "ratio", "bp": 5000}}),
            ("active_account_banned", {"op": "breach", "stacks": 3}),
        ):
            projected = schema.project(author)
            next(item for item in projected["abilities"] if item["id"] == identity)["effects"].append(effect)
            with self.subTest(identity=identity, op=effect["op"]), self.assertRaisesRegex(content.ContentError, "MissingHandler"):
                content.validate_definitions(projected, ids)
        for identity, op, field, value in (
            ("active_pants_down", "root", "slowed_stun", {"unit": "s", "value": 1.2}),
            ("active_elbow", "knockback", "wall_stun", {"unit": "s", "value": 1.2}),
            ("active_triple_combo", "empower_basic", "basic_stacks", 2),
            ("active_quick_meeting", "zone_slow", "linger", {"unit": "s", "value": 0.5}),
        ):
            candidate = content.parse_json((content.ROOT / content.CONTENT_SOURCE).read_bytes(), "content author")
            row = next(item for item in candidate["abilities"] if item["id"] == identity)
            next(item for item in row["effects"] if item["op"] == op)[field] = value
            with self.subTest(identity=identity, field=field), self.assertRaisesRegex(content.ContentError, "SchemaVariant"):
                schema.validate(candidate)

    def test_only_seethe_damage_scales_with_the_casters_max_hp(self):
        scaled = {row["id"] for row in self.data["abilities"] for effect in row["effects"]
                  if effect["op"] == "damage" and effect["max_hp_ratio"]["bp"] != 0}
        self.assertEqual(scaled, {"active_seethe"})

    def test_self_area_damage_refuses_without_its_area(self):
        author, schema, ids = self.approved_projection_inputs()
        projected = schema.project(author)
        seethe = next(row for row in projected["abilities"] if row["id"] == "active_seethe")
        seethe["effects"] = [effect for effect in seethe["effects"] if effect["op"] != "self_area"]
        with self.assertRaisesRegex(content.ContentError, "TargetEffectMismatch"):
            content.validate_definitions(projected, ids)

    def test_v7_contracts_reject_misplaced_mechanics(self):
        author, schema, ids = self.approved_projection_inputs()
        for identity, change in (
            ("active_account_banned", lambda effects: effects.pop(0)),
            ("active_unplug", lambda effects: effects.append({"op": "stun", "duration": {"unit": "ms", "ms": 500}})),
            ("active_bail_out", lambda effects: effects.pop(0)),
            ("active_seethe", lambda effects: next(e for e in effects if e["op"] == "burn").update(basic_stacks=1)),
            ("active_stare_down", lambda effects: effects.append({"op": "stat_bonus", "stats": {"ADFlat": {"unit": "points", "p2": 100}}})),
        ):
            projected = schema.project(author)
            change(next(row for row in projected["abilities"] if row["id"] == identity)["effects"])
            with self.subTest(identity=identity), self.assertRaises(content.ContentError):
                content.validate_definitions(projected, ids)

    def test_monster_and_progression_values(self):
        self.assertEqual(self.data["progression"]["xp_thresholds"],
                         (0, 80, 180, 300, 440, 600, 780, 980, 1200, 1440, 1700, 1980, 2280, 2600, 2940, 3300))
        self.assertEqual(self.data["progression"]["xp_to_next"],
                         (80, 100, 120, 140, 160, 180, 200, 220, 240, 260, 280, 300, 320, 340, 360))
        self.assertEqual((self.data["progression"]["max_level"], self.data["progression"]["active_slots"]), (16, 3))
        for identity, tier, count, hp, resist, attack, rate, windup, reach, speed, leash, grace, respawn, xp, interval in (
            ("monster_scavenger", "Low", 2, 90, 8, 10, ".80", 350, 150, 240, 900, 3000, 25000, 20, 1250),
            ("monster_guardian", "High", 1, 630, 20, 72, ".65", 500, 175, 260, 1050, 4000, 65000, 80, 1539),
            ("monster_advisor", "Elite", 1, 1350, 30, 90, ".60", 500, 175, 260, 1050, 4000, 90000, 180, 1667),
        ):
            row = self.catalog.definition(identity)
            self.assertEqual((row["tier"], row["count_per_camp"], row["reward_xp"]), (tier, count, xp))
            for key, expected in (("hp", hp), ("armor", resist), ("magic_resist", resist), ("attack", attack),
                                  ("attack_speed", Fraction(rate)), ("attack_range", reach), ("move_speed", speed),
                                  ("leash_radius", leash), ("perception_radius", leash), ("collision_radius", 40)):
                self.assertEqual(amount(row[key]), expected)
            self.assertEqual((row["attack_windup"]["ms"], row["lose_target_grace"]["ms"], row["respawn"]["ms"],
                              row["attack_interval_ms"]), (windup, grace, respawn, interval))
            self.assertTrue(all(amount(row[key]) == 0 for key in ("hp_regen", "max_mp", "mp_regen", "ap", "crit_chance")))

    def test_approved_boss(self):
        # D26 (#79), user-approved 2026-11-04 ("Approve as proposed (Recommended)") and 2026-11-05 (windup, waypoints).
        boss = self.data["boss"]
        self.assertEqual(boss["id"], "monster_dean")
        for key, expected in (("hp", 4500), ("armor", 30), ("magic_resist", 30), ("attack", 210), ("attack_range", 250),
                              ("move_speed", 225), ("collision_radius", 40), ("aggro_radius", 400)):
            self.assertEqual(amount(boss[key]), expected, key)
        # #198 outer §5 (approved issue #198 comments 6752601175 / 6752628806): field T=240 s once, leaves at T=540 s,
        # and the old 90 s AD/AP +25% / move speed +10% approval buff is removed.
        self.assertEqual((boss["attack_interval"]["ms"], boss["attack_windup"]["ms"], boss["attacker_memory"]["ms"],
                          boss["appear_at"]["ms"], boss["leave_at"]["ms"], boss["reward_xp"]), (1500, 500, 5000, 240000, 540000, 300))
        self.assertNotIn("approval", boss)

    def test_world_identity_and_shared_values(self):
        self.assertEqual(self.world.world["map_id"], "demo_world")
        self.assertEqual((self.world.world["schema_version"], self.world.world["projection_revision"]), (3, 3))
        self.assertEqual(content._wire_fraction(self.world.world["geometry"]["sight_sample_height_cm"], "sight"), 120)
        self.assertEqual(content._wire_fraction(self.world.world["geometry"]["camp_clearance_cm"], "clearance"), 300)

    def test_exact_placement_and_parameter_references(self):
        placement = self.world.world["placement"]
        self.assertEqual([world.point(row, "offset") for row in placement["spawn_offsets_cm"]],
                         [(0, 0, 0), (100, 0, 0), (50, 87, 0), (-50, 87, 0), (-100, 0, 0), (-50, -87, 0), (50, -87, 0)])
        self.assertEqual(content._wire_fraction(placement["spawn_validation_radius_cm"], "envelope"), 200)
        self.assertEqual(self.world.world["parameter_refs"], (
            "AIRDROP.PickupRadiusCm", "CAMP.ResetArrivalRadiusCm", "CAPTURE.RadiusCm", "DORM.RadiusCm",
            "UNIT.BasicAttackRangeCm", "UNIT.CollisionRadiusCm", "VISION.RadiusCm"))

    def test_byte_provenance_and_acyclic_producer_order(self):
        self.assertEqual(self.world.world["source_sha256"], content.sha256((content.ROOT / content.WORLD_SOURCE).read_bytes()))
        self.assertEqual(self.world.world["author_schema_sha256"], content.sha256((content.ROOT / content.WORLD_SCHEMA_SOURCE).read_bytes()))
        self.assertFalse(set(world.OUTPUT_PATHS).intersection(self.world.inputs))
        self.assertFalse(set(content.OUTPUT_PATHS).intersection(self.world.inputs))
        with patch.object(content, "compile_content", side_effect=AssertionError("world depends on generated content root")):
            self.assertEqual(world.compile_world().outputs, self.world.outputs)
        self.assertEqual(set(self.world.world), {
            "schema_version", "projection_revision", "map_id", "layout_revision", "geometry_revision",
            "source_sha256", "author_schema_sha256", "approval", "geometry", "placement", "parameter_refs", "anchors"})
        self.assertNotIn(self.root_bundle.digest.hex(), self.world.outputs[content.WORLD_OUTPUT].decode("utf-8"))
        self.assertEqual(content.verify_runtime_files(content.ROOT, self.root_bundle.digest), self.root_bundle.digest)
        content.check_outputs(content.ROOT, self.root_bundle.outputs)
        world_sync.synchronize(check=True)


def wire(value):
    value = Fraction(value)
    return {"numerator": value.numerator, "denominator": value.denominator}


def vec(x, y, z=0):
    return {"x": wire(x), "y": wire(y), "z": wire(z)}


def box(x0, y0, x1, y1, z=0):
    return [vec(x0, y0, z), vec(x1, y0, z), vec(x1, y1, z), vec(x0, y1, z)]


class WorldGeometryFunctions(unittest.TestCase):
    """What each world element does, on a small synthetic world instead of the current map layout."""

    @classmethod
    def setUpClass(cls):
        def polygon(identity, points, height, sight):
            return {"id": identity, "shape": "polygon", "polygon_cm": points, "height_cm": wire(height),
                    "blocks_movement": True, "blocks_sight": sight}
        blockers = [
            polygon("wall", box(-100, -1000, 100, 1000), 200, True),
            polygon("fence", box(-1000, 2000, 1000, 2020), 100, False),
            {"id": "tree", "shape": "cylinder", "center_cm": vec(-2000, -3000), "radius_cm": wire(50), "height_cm": wire(200),
             "blocks_movement": True, "blocks_sight": True},
            {"id": "high_tree", "shape": "cylinder", "center_cm": vec(2200, 300, 150), "radius_cm": wire(50),
             "height_cm": wire(200), "blocks_movement": True, "blocks_sight": True},
        ]
        elevation = {
            "level_step_cm": wire(150),
            "plateaus": [{"id": "hill_1", "level": 1, "polygon_cm": box(2000, -500, 3000, 500, 150)},
                         {"id": "hill_2", "level": 2, "polygon_cm": box(2400, -200, 2800, 200, 300)}],
            "ramps": [{"id": "ramp_0", "lower_level": 0,
                       "polygon_cm": [vec(1700, 100), vec(1700, -100), vec(2000, -100, 150), vec(2000, 100, 150)]}],
        }
        cls.geometry = world.Geometry({"geometry": {
            "surfaces": [{"id": "ground", "min_cm": vec(-5000, -5000), "max_cm": vec(5000, 5000)}],
            "elevation": elevation, "blockers": blockers}})
        cls.radius = Fraction(40)

    def test_wall_blocks_walking_and_sight(self):
        g = self.geometry
        self.assertFalse(g.legal((0, 0, 0), self.radius))
        self.assertFalse(g.legal((-139, 0, 0), self.radius))
        self.assertTrue(g.legal((-140, 0, 0), self.radius))
        self.assertFalse(g.sight_clear((-500, 0, 0), (500, 0, 0)))
        self.assertTrue(g.sight_clear((-500, 1500, 0), (500, 1500, 0)))

    def test_fence_blocks_walking_but_not_sight(self):
        g = self.geometry
        self.assertFalse(g.legal((0, 2010, 0), self.radius))
        self.assertFalse(g.legal((0, 1961, 0), self.radius))
        self.assertTrue(g.legal((0, 1960, 0), self.radius))
        self.assertTrue(g.sight_clear((0, 1500, 0), (0, 2500, 0)))

    def test_tree_blocks_walking_within_body_radius_and_sight_through_its_trunk(self):
        g = self.geometry
        self.assertFalse(g.legal((-2000, -3000 + 89, 0), self.radius))
        self.assertTrue(g.legal((-2000, -3000 + 90, 0), self.radius))
        self.assertFalse(g.sight_clear((-2300, -3000, 0), (-1700, -3000, 0)))
        self.assertTrue(g.sight_clear((-2300, -2950, 0), (-1700, -2950, 0)))

    def test_standing_follows_the_level_under_the_whole_body(self):
        g = self.geometry
        self.assertTrue(g.legal((2500, -400, 150), self.radius))
        self.assertFalse(g.legal((2500, -400, 0), self.radius))
        # A body across a cliff edge stands on no single level.
        self.assertFalse(g.legal((2010, -400, 150), self.radius))
        self.assertFalse(g.legal((1990, -400, 0), self.radius))
        self.assertEqual([g.terrain.tier(p) for p in ((1000, 0), (1850, 0), (2100, 0), (2600, 0))], [0, 1, 2, 4])

    def test_high_ground_sees_low_ground_not_the_reverse(self):
        clear = self.geometry.sight_clear
        ground, ramp, low, high = (1000, 0, 0), (1850, 0, 75), (2100, 0, 150), (2600, 0, 300)
        self.assertEqual((clear(ground, low), clear(low, ground)), (False, True))
        self.assertEqual((clear(ground, ramp), clear(ramp, ground)), (False, True))
        self.assertEqual((clear(ramp, low), clear(low, ramp)), (False, True))
        self.assertEqual((clear(low, high), clear(high, low)), (False, True))
        self.assertTrue(clear(high, (1500, 0, 0)))
        # A plateau between two ground points hides them from each other.
        self.assertFalse(clear((1500, 0, 0), (3500, 0, 0)))
        self.assertTrue(clear((1500, 700, 0), (3500, 700, 0)))

    def test_obstacles_hide_only_from_their_level_and_below(self):
        clear = self.geometry.sight_clear
        self.assertFalse(clear((1000, 0, 0), (-300, 0, 0)))
        self.assertTrue(clear((2100, 0, 150), (-300, 0, 0)))
        self.assertFalse(clear((2050, 300, 150), (2350, 300, 150)))
        self.assertTrue(clear((2500, 150, 300), (2100, 380, 150)))


class ApprovedDemoRefusals(unittest.TestCase):
    def setUp(self):
        base = content.ROOT / "Artifacts" / "DemoContent"
        base.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix="approved-test-", dir=base)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        for relative in (*content.AUTHOR_INPUT_PATHS, content.WORLD_SOURCE, content.WORLD_SCHEMA_SOURCE):
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(content.ROOT / relative, target)
        self.world = world_sync.synchronize(self.root)
        self.content = content_sync.synchronize(self.root)
        self.originals = {**self.world.outputs, **self.content.outputs}

    def edit(self, relative, change):
        path = self.root / relative
        data = json.loads(path.read_bytes(), parse_float=Decimal)
        change(data)
        # These mutations only alter integer geometry or string metadata.
        path.write_bytes(encode(data))

    def unchanged(self):
        for relative, expected in self.originals.items():
            self.assertEqual((self.root / relative).read_bytes(), expected, str(relative))

    def editable_schema_accepts(self):
        schema = content.parse_json((self.root / content.WORLD_SCHEMA_SOURCE).read_bytes(), "test schema")
        author = content.parse_json((self.root / content.WORLD_SOURCE).read_bytes(), "test author")
        content.AuthorSchema(schema).validate(author)

    @staticmethod
    def at(value, path):
        for key in path:
            value = value[key]
        return value

    def assert_native_author_refusal(self, pattern):
        self.editable_schema_accepts()
        for synchronize in (world_sync.synchronize, content_sync.synchronize):
            with self.assertRaisesRegex(content.ContentError, pattern):
                synchronize(self.root)
            self.unchanged()

    def test_schema_and_author_cannot_add_unconsumed_friction(self):
        self.edit(content.WORLD_SCHEMA_SOURCE,
                  lambda d: d["properties"]["geometry"]["properties"].update(friction={"type": "integer"}))
        self.edit(content.WORLD_SOURCE, lambda d: d["geometry"].update(friction=1))
        self.assert_native_author_refusal("AuthorField.*friction")

    def test_schema_and_author_cannot_change_centimeters_to_meters(self):
        self.edit(content.WORLD_SCHEMA_SOURCE,
                  lambda d: d["properties"]["coordinate_unit"].update(const="m"))
        self.edit(content.WORLD_SOURCE, lambda d: d.update(coordinate_unit="m"))
        self.assert_native_author_refusal("WorldUnit")

    def test_coordinated_schema_extensions_reject_every_nested_author_object_kind(self):
        source_raw = (self.root / content.WORLD_SOURCE).read_bytes()
        schema_raw = (self.root / content.WORLD_SCHEMA_SOURCE).read_bytes()
        # One row of each author shape, wherever the current layout puts it.
        source = json.loads(source_raw)
        polygon = next(i for i, row in enumerate(source["geometry"]["blockers"]) if row["shape"] == "polygon")
        cylinder = next(i for i, row in enumerate(source["geometry"]["blockers"]) if row["shape"] == "cylinder")
        place = next(i for i, row in enumerate(source["anchors"]) if "members" not in row)
        camp = next(i for i, row in enumerate(source["anchors"]) if len(row.get("members", ())) == 2)
        cases = (
            ((), ()),
            (("approval",), ("properties", "approval")),
            (("geometry",), ("properties", "geometry")),
            (("geometry", "surfaces", 0), ("properties", "geometry", "properties", "surfaces", "items")),
            (("geometry", "surfaces", 0, "min_cm"), ("$defs", "vector")),
            (("geometry", "surfaces", 0, "max_cm"), ("$defs", "vector")),
            (("geometry", "blockers", polygon), ("$defs", "blocker", "oneOf", 0)),
            (("geometry", "blockers", polygon, "polygon_cm", 3), ("$defs", "vector")),
            (("geometry", "blockers", cylinder), ("$defs", "blocker", "oneOf", 1)),
            (("geometry", "blockers", cylinder, "center_cm"), ("$defs", "vector")),
            (("placement",), ("properties", "placement")),
            (("placement", "spawn_offsets_cm", 6), ("$defs", "vector")),
            (("anchors", place), ("$defs", "anchor", "oneOf", 0)),
            (("anchors", place, "position_cm"), ("$defs", "vector")),
            (("anchors", camp), ("$defs", "anchor", "oneOf", 1)),
            (("anchors", camp, "members", 1), ("$defs", "anchor", "oneOf", 1, "properties", "members", "items")),
            (("anchors", camp, "members", 1, "position_cm"), ("$defs", "vector")),
        )
        for author_path, schema_path in cases:
            with self.subTest(path=author_path):
                (self.root / content.WORLD_SOURCE).write_bytes(source_raw)
                (self.root / content.WORLD_SCHEMA_SOURCE).write_bytes(schema_raw)
                self.edit(content.WORLD_SCHEMA_SOURCE,
                          lambda d: self.at(d, schema_path)["properties"].update(unconsumed_value={"type": "integer"}))
                self.edit(content.WORLD_SOURCE, lambda d: self.at(d, author_path).update(unconsumed_value=1))
                self.assert_native_author_refusal("AuthorField.*unconsumed_value")

    def test_schema_cannot_coerce_boolean_or_decimal_author_coordinates(self):
        source_raw = (self.root / content.WORLD_SOURCE).read_bytes()
        schema_raw = (self.root / content.WORLD_SCHEMA_SOURCE).read_bytes()
        for token in (b"true", b"1.0", b"1e0"):
            with self.subTest(token=token):
                (self.root / content.WORLD_SOURCE).write_bytes(source_raw)
                (self.root / content.WORLD_SCHEMA_SOURCE).write_bytes(schema_raw)
                self.edit(content.WORLD_SCHEMA_SOURCE, lambda d: d["$defs"].update(
                    coordinate={"oneOf": [{"type": "number"}, {"type": "boolean"}]}))
                self.edit(content.WORLD_SOURCE,
                          lambda d: d["anchors"][9]["position_cm"].update(x="COORDINATE_TOKEN"))
                path = self.root / content.WORLD_SOURCE
                path.write_bytes(path.read_bytes().replace(b'"COORDINATE_TOKEN"', token))
                self.assert_native_author_refusal("IntegerType")

    def test_schema_descriptions_only_change_schema_provenance(self):
        self.edit(content.WORLD_SCHEMA_SOURCE, lambda d: d.update(
            title="Documented Demo world contract", description="Metadata only; it cannot expand producer semantics."))
        self.edit(content.WORLD_SCHEMA_SOURCE,
                  lambda d: d["$defs"]["vector"].update(description="Integer centimeter coordinates."))
        self.editable_schema_accepts()
        updated = world_sync.synchronize(self.root)
        expected, actual = content.canonical(self.world.world), content.canonical(updated.world)
        self.assertNotEqual(expected["author_schema_sha256"], actual["author_schema_sha256"])
        actual["author_schema_sha256"] = expected["author_schema_sha256"]
        self.assertEqual(actual, expected)
        updated_content = content_sync.synchronize(self.root)
        self.assertEqual(updated_content.outputs[content.CONTENT_OUTPUT], self.content.outputs[content.CONTENT_OUTPUT])
        self.assertNotEqual(updated_content.digest, self.content.digest)
        self.assertEqual(content.verify_runtime_files(self.root, updated_content.digest), updated_content.digest)

    def test_bad_full_world_input_refuses_without_partial_outputs(self):
        original = (self.root / content.WORLD_SOURCE).read_bytes()
        source = json.loads(original)
        rows = source["geometry"]["blockers"]
        # Cases pick their rows by kind, not by a position in one layout's file.
        # The boundary wall reaches farthest from the centre; without it a ground edge stays open.
        reach = lambda row: max(abs(v[a]) for v in row["polygon_cm"] for a in "xy")
        wall = max((i for i, row in enumerate(rows) if row["shape"] == "polygon"), key=lambda i: reach(rows[i]))
        polygon = next(i for i, row in enumerate(rows) if row["shape"] == "polygon")
        tree = next(i for i, row in enumerate(rows) if row["shape"] == "cylinder")
        capture = next(row for row in source["anchors"] if row["kind"] == "Capture")["position_cm"]
        for change in (
            lambda d: d.update(coordinate_unit="m"),
            lambda d: d["approval"].update(status="pending"),
            lambda d: d["geometry"]["blockers"].pop(wall),
            lambda d: d["geometry"]["blockers"][polygon]["polygon_cm"].reverse(),
            lambda d: d["geometry"]["blockers"][polygon]["polygon_cm"][0].update(z=1),
            lambda d: d["geometry"]["blockers"][polygon].update(height_cm=0),
            lambda d: d["geometry"]["blockers"][tree]["center_cm"].update(z=1),
            lambda d: d["geometry"]["blockers"][tree].update(radius_cm=0),
            lambda d: d["geometry"]["blockers"][tree].update(shape="polygon"),
            lambda d: d["geometry"]["blockers"][tree]["center_cm"].update(x=capture["x"], y=capture["y"]),
            lambda d: d["placement"]["spawn_offsets_cm"][1].update(x=300),
            lambda d: d["placement"]["spawn_offsets_cm"][1].update(x=True),
            lambda d: d["parameter_refs"].append("UNAPPROVED.NumericDefault"),
            lambda d: d["geometry"]["surfaces"][0]["min_cm"].update(x=content.INT64_MIN),
        ):
            (self.root / content.WORLD_SOURCE).write_bytes(original)
            with self.subTest(change=change):
                self.edit(content.WORLD_SOURCE, change)
                with self.assertRaises(content.ContentError):
                    world_sync.synchronize(self.root)
                self.unchanged()
        (self.root / content.WORLD_SOURCE).write_bytes(original)

    def test_unknown_schema_and_old_world_projection_are_rejected(self):
        self.edit(content.WORLD_SCHEMA_SOURCE, lambda d: d.update(unknown_keyword=True))
        with self.assertRaisesRegex(content.ContentError, "InvalidSchema"):
            world_sync.synchronize(self.root)
        self.unchanged()
        raw = json.loads(self.world.outputs[content.WORLD_OUTPUT])
        raw["projection_revision"] = 1
        with self.assertRaises(content.ContentError):
            world.validate_payload(encode(content.canonical(raw)))

    def test_root_refuses_self_consistent_but_unauthored_world_bytes(self):
        data = json.loads(self.world.outputs[content.WORLD_OUTPUT])
        next(row for row in data["anchors"] if row["id"] == "s00")["position_cm"]["x"]["numerator"] += 1
        raw = encode(content.canonical(data))
        (self.root / content.WORLD_OUTPUT).write_bytes(raw)
        (self.root / content.WORLD_CONTRACT).write_bytes(world.world_contract_bytes(raw))
        with self.assertRaisesRegex(content.ContentError, "StaleWorldOutput"):
            content_sync.synchronize(self.root)
        for relative, expected in self.content.outputs.items():
            self.assertEqual((self.root / relative).read_bytes(), expected)

    def test_world_contract_write_failure_rolls_back_world_bundle(self):
        self.edit(content.WORLD_SOURCE, lambda d: d.update(layout_revision=3))
        compilation = world.compile_world(self.root)
        install = content_sync._install_output

        def fail(protected, raw):
            if protected.path == self.root / content.WORLD_CONTRACT:
                raise OSError("injected world contract failure")
            install(protected, raw)

        with patch.object(content_sync, "_install_output", side_effect=fail), self.assertRaises(OSError):
            content_sync.write_outputs(self.root, compilation)
        self.unchanged()
        self.assertFalse((self.root / content.DATA / ".compile.lock").exists())

    def test_world_and_content_generation_share_the_same_recovery_gate(self):
        with patch.object(content_sync, "_remove_lock", side_effect=OSError("injected world cleanup failure")):
            with self.assertRaisesRegex(content.ContentError, "RecoveryRequired"):
                content_sync.write_outputs(self.root, self.world)
        journal = json.loads((self.root / content.DATA / ".compile.lock").read_bytes())
        self.assertEqual(journal["bundle_kind"], "world")
        self.assertNotIn("expected_catalog_sha256", journal)
        self.assertEqual(journal["expected_world_sha256"], self.world.digest.hex())
        with self.assertRaisesRegex(content.ContentError, "CompilerBusy"):
            content_sync.write_outputs(self.root, self.content)
        with self.assertRaisesRegex(content.ContentError, "CompilerBusy"):
            world_sync.synchronize(self.root, check=True)
        self.unchanged()


if __name__ == "__main__":
    unittest.main()
