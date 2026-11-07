"""Exact approved baseline and complete-world assertions; not a native/play-balance test."""
from decimal import Decimal
from fractions import Fraction
import json
import math
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


# #147 build v7 (CONTENT_AND_BALANCE): the twelve actives in authored order.
ACTIVE_IDS = ("active_pants_down", "active_crotch_grab", "active_snark_bolt", "active_seethe", "active_bail_out", "active_elbow",
              "active_not_mad", "active_stare_down", "active_quick_meeting", "active_account_banned", "active_unplug",
              "active_triple_combo")
# Active frames shared by both ranks: (target mode, range cm or None, windup s, mana, cooldown s).
ACTIVE_FRAMES = {
    "active_pants_down": ("Unit", 450, r(".2"), 60, 12),
    "active_crotch_grab": ("Unit", 200, r(".15"), 50, 14),
    "active_snark_bolt": ("DirectionProjectile", 750, r(".2"), 45, 8),
    "active_seethe": ("Self", None, r(".25"), 55, 9),
    "active_bail_out": ("DirectionDash", None, 0, 40, 10),
    "active_elbow": ("Unit", 200, r(".15"), 55, 12),
    "active_not_mad": ("Self", None, 0, 50, 14),
    "active_stare_down": ("Self", None, r(".2"), 60, 15),
    "active_quick_meeting": ("GroundPoint", 650, r(".25"), 65, 12),
    "active_account_banned": ("Unit", 600, r(".2"), 60, 13),
    "active_unplug": ("Unit", 250, r(".15"), 50, 13),
    "active_triple_combo": ("Self", None, 0, 40, 12),
}
# Every skill's effects per rank (初级, 高级) in authored order and author units; "base" grows with the owner's level at runtime.
RANK_EFFECTS = {
    "active_account_banned": (
        [("damage", {"ad_ratio": 0, "ap_ratio": r("0.8"), "base": 60, "damage_type": "Magical", "max_hp_ratio": 0}),
         ("silence", {"duration": r("1.25")})],
        [("damage", {"ad_ratio": 0, "ap_ratio": r("0.8"), "base": 90, "damage_type": "Magical", "max_hp_ratio": 0}),
         ("silence", {"duration": r("1.25")}),
         ("breach", {"stacks": 3})],
    ),
    "active_bail_out": (
        [("dash", {"distance": 350, "duration": r("0.2")}),
         ("empower_basic", {"ad_ratio": r("0.4"), "ap_ratio": 0, "base": 40, "damage_type": "Physical", "duration": 3})],
        [("dash", {"distance": 350, "duration": r("0.2")}),
         ("empower_basic", {"ad_ratio": r("0.4"), "ap_ratio": 0, "base": 60, "damage_type": "Physical", "duration": 3}),
         ("path_slow", {"duration": r("1.5"), "half_width": 80, "ratio": r("0.4")})],
    ),
    "active_crotch_grab": (
        [("damage", {"ad_ratio": r("0.8"), "ap_ratio": 0, "base": 80, "damage_type": "Physical", "max_hp_ratio": 0}),
         ("stun", {"duration": r("0.6")})],
        [("damage", {"ad_ratio": r("0.8"), "ap_ratio": 0, "base": 120, "damage_type": "Physical", "max_hp_ratio": 0}),
         ("stun", {"duration": r("0.6")}),
         ("execute", {"hp_below": r("0.5"), "ratio": r("0.3")})],
    ),
    "active_elbow": (
        [("damage", {"ad_ratio": r("0.6"), "ap_ratio": 0, "base": 70, "damage_type": "Physical", "max_hp_ratio": 0}),
         ("knockback", {"distance": 250, "duration": r("0.25")})],
        [("damage", {"ad_ratio": r("0.6"), "ap_ratio": 0, "base": 105, "damage_type": "Physical", "max_hp_ratio": 0}),
         ("knockback", {"distance": 250, "duration": r("0.25"), "wall_distance": 225, "wall_stun": 1})],
    ),
    "active_not_mad": (
        [("shield", {"ap_ratio": r("0.6"), "base": 80, "duration": 3, "max_hp_ratio": r("0.06")}),
         ("dispel_slow", {})],
        [("shield", {"ap_ratio": r("0.6"), "base": 120, "duration": 3, "max_hp_ratio": r("0.06")}),
         ("dispel_slow", {}),
         ("dispel_root", {}),
         ("dispel_disarm", {})],
    ),
    "active_pants_down": (
        [("damage", {"ad_ratio": r("0.5"), "ap_ratio": 0, "base": 60, "damage_type": "Physical", "max_hp_ratio": 0}),
         ("root", {"duration": 1})],
        [("damage", {"ad_ratio": r("0.5"), "ap_ratio": 0, "base": 90, "damage_type": "Physical", "max_hp_ratio": 0}),
         ("root", {"duration": r("1.25"), "slowed_stun": 1})],
    ),
    "active_quick_meeting": (
        [("zone", {"count": 1, "delay": 0, "duration": 3, "interval": 1, "radius": 250}),
         ("zone_damage", {"ad_ratio": 0, "ap_ratio": r("0.7"), "base": 50, "damage_type": "Magical"}),
         ("zone_slow", {"interval": r("0.25"), "linger": r("0.5"), "ratio": r("0.3")}),
         ("zone_burn", {"interval": 1, "stacks": 1})],
        [("zone", {"count": 1, "delay": 0, "duration": 3, "interval": 1, "radius": 250}),
         ("zone_damage", {"ad_ratio": 0, "ap_ratio": r("0.7"), "base": 75, "damage_type": "Magical"}),
         ("zone_slow", {"interval": r("0.25"), "linger": r("0.5"), "ratio": r("0.3")}),
         ("zone_burn", {"interval": 1, "stacks": 1}),
         ("zone_root", {"duration": r("0.75")})],
    ),
    "active_seethe": (
        [("self_area", {"radius": 300}),
         ("damage", {"ad_ratio": 0, "ap_ratio": r("0.7"), "base": 70, "damage_type": "Magical", "max_hp_ratio": r("0.03")}),
         ("burn", {"stacks": 2})],
        [("self_area", {"radius": 300}),
         ("damage", {"ad_ratio": 0, "ap_ratio": r("0.7"), "base": 105, "damage_type": "Magical", "max_hp_ratio": r("0.03")}),
         ("burn", {"stacks": 2}),
         ("echo", {"delay": 1, "ratio": r("0.5")})],
    ),
    "active_snark_bolt": (
        [("damage", {"ad_ratio": 0, "ap_ratio": 1, "base": 90, "damage_type": "Magical", "max_hp_ratio": 0}),
         ("slow", {"duration": r("1.5"), "ratio": r("0.25")})],
        [("damage", {"ad_ratio": 0, "ap_ratio": 1, "base": 135, "damage_type": "Magical", "max_hp_ratio": 0}),
         ("slow", {"duration": r("1.5"), "ratio": r("0.25")}),
         ("pierce", {})],
    ),
    "active_stare_down": (
        [("taunt", {"duration": r("0.75"), "radius": 300}),
         ("timed_stat_bonus", {"duration": 3, "stats": {"ArmorFlat": 12, "MagicResistFlat": 12}})],
        [("taunt", {"duration": 1, "radius": 300}),
         ("timed_stat_bonus", {"duration": 3, "stats": {"ArmorFlat": 18, "MagicResistFlat": 18}}),
         ("taunt_shield", {"base": 60, "duration": 3, "max_hp_ratio": r("0.04")})],
    ),
    "active_triple_combo": (
        [("empower_basic", {"ad_ratio": r("0.15"), "ap_ratio": 0, "base": 20, "charges": 3, "damage_type": "Physical", "duration": 5,
                            "stats": {"AttackSpeedBonusRatio": r("0.5")}})],
        [("empower_basic", {"ad_ratio": r("0.15"), "ap_ratio": 0, "base": 30, "charges": 3, "cleave_radius": 180,
                            "cleave_ratio": r("0.35"), "damage_type": "Physical", "duration": 5,
                            "stats": {"AttackSpeedBonusRatio": r("0.5")}})],
    ),
    "active_unplug": (
        [("damage", {"ad_ratio": r("0.5"), "ap_ratio": 0, "base": 50, "damage_type": "Physical", "max_hp_ratio": 0}),
         ("disarm", {"duration": r("1.25")})],
        [("damage", {"ad_ratio": r("0.5"), "ap_ratio": 0, "base": 75, "damage_type": "Physical", "max_hp_ratio": 0}),
         ("disarm", {"duration": r("1.25")}),
         ("slow", {"duration": r("1.25"), "ratio": r("0.3")})],
    ),
    "passive_freeloader": (
        [("lifesteal", {"ratio": r("0.12")})],
        [("lifesteal", {"hp_below": r("0.5"), "low_hp_multiplier": 2, "ratio": r("0.18")})],
    ),
    "passive_iced_americano": (
        [("stat_bonus", {"stats": {"CooldownHasteFlat": 20, "MPRegenFlat": 2}})],
        [("stat_bonus", {"stats": {"CooldownHasteFlat": 30, "MPRegenFlat": 2}}),
         ("cooldown_refund", {"cooldown": r("0.25"), "entries": "Actives", "trigger": "BasicHit"})],
    ),
    "passive_kick_down": (
        [("control_damage_amp", {"ratio": r("0.12")})],
        [("control_damage_amp", {"hp_below": r("0.5"), "ratio": r("0.18")})],
    ),
    "passive_lucky_star": (
        [("stat_bonus", {"stats": {"CritChanceBonusRatio": r("0.15")}})],
        [("stat_bonus", {"stats": {"CritChanceBonusRatio": r("0.2")}}),
         ("ability_crit", {"abilities": ACTIVE_IDS, "mode": "Chance", "ratio": 1})],
    ),
    "passive_moral_victory": (
        [("triggered_shield", {"base": 40, "duration": 3, "interval": 10, "max_hp_ratio": r("0.04"), "trigger": "Damaged"})],
        [("triggered_shield", {"base": 60, "burst_radius": 300, "burst_ratio": r("0.5"), "duration": 3, "interval": 10,
                               "max_hp_ratio": r("0.04"), "trigger": "Damaged"})],
    ),
    "passive_pressed": (
        [("low_hp_stat_bonus", {"stats": {"AttackSpeedBonusRatio": r("0.35"), "MoveSpeedBonusRatio": r("0.1")},
                                "threshold": r("0.4")})],
        [("low_hp_stat_bonus", {"lifesteal": r("0.15"), "stats": {"AttackSpeedBonusRatio": r("0.5"), "MoveSpeedBonusRatio": r("0.1")},
                                "threshold": r("0.4")})],
    ),
    "passive_special_forces": (
        [("cast_frenzy", {"duration": 2, "stats": {"MoveSpeedBonusRatio": r("0.25")}})],
        [("cast_frenzy", {"duration": 2, "stats": {"MoveSpeedBonusRatio": r("0.35")}}),
         ("dash_boost", {"cooldown_ratio": r("0.7"), "distance_bonus": 0})],
    ),
    "passive_stoke": (
        [("burn", {"stacks": 3})],
        [("burn", {"basic_stacks": 1, "stacks": 4})],
    ),
    "passive_thick_face": (
        [("thorns", {"armor_ratio": 0, "base": 4, "critical_multiplier": 1, "damage_type": "Magical", "interval": r("0.001"),
                     "max_hp_ratio": r("0.008"), "radius": 1000, "trigger": "Basic"})],
        [("thorns", {"armor_ratio": 0, "base": 6, "critical_multiplier": 1, "damage_type": "Magical", "interval": r("0.001"),
                     "max_hp_ratio": r("0.008"), "radius": 1000, "slow": r("0.2"), "slow_duration": 1, "trigger": "Basic"})],
    ),
    "passive_tilted": (
        [("breach", {"basic_stacks": 1})],
        [("breach", {"basic_stacks": 2, "stacks": 2})],
    ),
}
# The advanced rank's one gameplay change (#147 "高级 = 数值 + 一条玩法变化"): its new effects, or new fields of an effect
# both ranks carry.
ADVANCED_CHANGES = {
    "active_pants_down": {"root.slowed_stun"}, "active_crotch_grab": {"execute"}, "active_snark_bolt": {"pierce"},
    "active_seethe": {"echo"}, "active_bail_out": {"path_slow"},
    "active_elbow": {"knockback.wall_distance", "knockback.wall_stun"}, "active_not_mad": {"dispel_root", "dispel_disarm"},
    "active_stare_down": {"taunt_shield"}, "active_quick_meeting": {"zone_root"}, "active_account_banned": {"breach"},
    "active_unplug": {"slow"}, "active_triple_combo": {"empower_basic.cleave_ratio", "empower_basic.cleave_radius"},
    "passive_pressed": {"low_hp_stat_bonus.lifesteal"}, "passive_tilted": {"breach.stacks"},
    "passive_stoke": {"burn.basic_stacks"}, "passive_kick_down": {"control_damage_amp.hp_below"},
    "passive_thick_face": {"thorns.slow", "thorns.slow_duration"},
    "passive_freeloader": {"lifesteal.hp_below", "lifesteal.low_hp_multiplier"}, "passive_special_forces": {"dash_boost"},
    "passive_moral_victory": {"triggered_shield.burst_ratio", "triggered_shield.burst_radius"},
    "passive_iced_americano": {"cooldown_refund"}, "passive_lucky_star": {"ability_crit"},
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
        cls.model = world.PathModel(cls.geometry, Fraction(40))
        cls.anchors = {row["id"]: row for row in cls.world.world["anchors"]}

    def test_approval_and_complete_selected_directory(self):
        approval = self.data["identity"]["approval"]
        self.assertEqual(approval["status"], "approved")
        # A content-only approval does not rewrite the independently approved world geometry.
        self.assertEqual(self.world.world["approval"]["status"], "approved")
        for text in ("2026-11-02", "D01-D03", "2026-11-04", "#82", "playtest", "not a balance"):
            self.assertIn(text, approval["decision"])
        self.assertNotIn("TEST ONLY", approval["decision"])
        self.assertIn("camera 2250 (1300-2400)", approval["decision"])
        self.assertIn("2026-11-07 movement -25% (player 300, floor 173, cap 675, camps 240/260, Boss 225, route 35s), "
                      "flagpole 180s, guard 30s, airdrop 80s", approval["decision"])
        self.assertIn("#147 build v7 (two skill ranks growing with level, slotted three-choice, 12+10+8 catalog)",
                      approval["decision"])
        self.assertEqual((len(self.data["abilities"]), len(self.data["attributes"]), len(self.data["equipment"]),
                          len(self.data["camps"]["monsters"])), (22, 8, 13, 2))
        self.assertEqual([row["id"] for row in self.data["abilities"]], [
            "active_account_banned", "active_bail_out", "active_crotch_grab", "active_elbow", "active_not_mad",
            "active_pants_down", "active_quick_meeting", "active_seethe", "active_snark_bolt", "active_stare_down",
            "active_triple_combo", "active_unplug",
            "passive_freeloader", "passive_iced_americano", "passive_kick_down", "passive_lucky_star", "passive_moral_victory",
            "passive_pressed", "passive_special_forces", "passive_stoke", "passive_thick_face", "passive_tilted"])
        # #147: mechanic tags are gone from skills and equipment.
        self.assertFalse(any("tags" in row for row in (*self.data["abilities"], *self.data["equipment"])))
        self.assertEqual(self.data["identity"]["projection_revision"], 3)
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
            ("STAT.AttackSpeed.Min", "attacks/s", ".30"), ("STAT.AttackSpeed.Max", "attacks/s", "2.50"),
            ("STAT.CritChance.Base", "ratio", "0"), ("STAT.CritMultiplier", "ratio", "2"), ("STAT.AbilityCritMultiplier", "ratio", "1.5"),
            ("STAT.CooldownHaste.Base", "points", "0"), ("STAT.CooldownHaste.Max", "points", "60"), ("SKILL.GrowthPerLevel", "ratio", ".10"),
            ("UNIT.MoveSpeedCmPerSec", "cm/s", "300"), ("STAT.MoveSpeed.Min", "cm/s", "173"), ("STAT.MoveSpeed.Max", "cm/s", "675"),
            ("UNIT.CollisionRadiusCm", "cm", "40"), ("UNIT.BasicAttackRangeCm", "cm", "220"),
            ("STAT.AttackRange.Min", "cm", "80"), ("STAT.AttackRange.Max", "cm", "1000"),
            ("UNIT.BasicAttackWindupSec", "ms", ".250"), ("VISION.RadiusCm", "cm", "1400"),
            ("SHOP.InteractRadiusCm", "cm", "600"), ("CAPTURE.RadiusCm", "cm", "300"),
            ("CAPTURE.SpraySec", "ms", "3.5"), ("CAPTURE.GuardSec", "ms", "30"), ("TIMING.DefendRouteBudgetSec", "ms", "35"),
            ("MATCH.CountdownSec", "ms", "5"), ("MATCH.DevelopmentSec", "ms", "180"), ("LIFE.RespawnSec", "ms", "15"), ("LIFE.ProtectionSec", "ms", "3"),
            ("REGEN.OutOfCombatDelaySec", "ms", "5"), ("REGEN.OutOfCombatMaxHPRatioPerSec", "ratio", ".015"),
            ("REGEN.OutOfCombatMaxMPRatioPerSec", "ratio", ".015"),
            ("DORM.RadiusCm", "cm", "400"), ("DORM.OutOfCombatDelaySec", "ms", "3"),
            ("DORM.MaxHPRatioPerSec", "ratio", ".1"), ("DORM.MaxMPRatioPerSec", "ratio", ".1"),
            ("SHOP.SellRatio", "ratio", ".65"), ("CAMP.ResetArrivalRadiusCm", "cm", "100"),
            ("CAMP.ReturnDeadEndSec", "ms", "6"),
            ("AIRDROP.FirstLandSec", "ms", "45"), ("AIRDROP.IntervalSec", "ms", "80"),
            ("AIRDROP.AnnounceLeadSec", "ms", "15"), ("AIRDROP.PickupRadiusCm", "cm", "150"),
            ("LEADER.RefreshSec", "ms", "10"), ("LEADER.CircleRadiusCm", "cm", "1500"),
            ("LEADER.CircleMaxOffsetCm", "cm", "1000"),
            ("REWARD.AssistWindowSec", "ms", "15"), ("REWARD.AssistGoldRatio", "ratio", ".5"),
            ("SHOP.UpgradeStatBonusRatio", "ratio", ".35"), ("CHOICE.NewActiveFullSlotWeight", "ratio", ".5"),
            ("CHOICE.FreeSlotAttributeChance", "ratio", ".4"),
            ("AI.RetreatHPRatio", "ratio", ".25"), ("AI.ResumeFarmHPRatio", "ratio", ".60"),
            ("AI.HighCampEntryHPRatio", "ratio", ".80"), ("AI.HighCampReserveHPRatio", "ratio", ".35"),
            ("AI.LowCampReserveHPRatio", "ratio", ".15"), ("AI.CaptureReactionBudgetSec", "ms", "2"),
            ("AI.DecisionIntervalSec", "ms", ".25"), ("AI.RetryPolicy.InitialBackoffSec", "ms", ".25"),
            ("AI.RetryPolicy.MaxBackoffSec", "ms", ".50"), ("AI.ExploreRevisitIntervalSec", "ms", "20"),
            ("AI.EngagementBudgetSec", "ms", "40"),
            ("COMBAT.BreachPerStack", "points", "3"), ("COMBAT.BreachSec", "ms", "4"),
            ("COMBAT.BurnPerSec", "points", "3"), ("COMBAT.BurnAPRatioPerSec", "ratio", ".03"),
            ("COMBAT.BurnTickSec", "ms", ".5"), ("COMBAT.BurnSec", "ms", "3"), ("COMBAT.LifestealCap", "ratio", ".35"),
        ):
            expected[key] = (unit, value)
        for key, value in (
            ("SHOP.StartingGold", 320), ("SHOP.UpgradeCost", 240), ("XP.BaseToNext", 80), ("XP.IncreasePerLevel", 20),
            ("REWARD.PlayerXP.Base", 100), ("REWARD.PlayerXP.PerLevel", 20),
            ("REWARD.PlayerGold.Base", 80), ("REWARD.PlayerGold.PerLevel", 10), ("AI.RetryPolicy.MaxAttempts", 3),
            ("CAMP.ReturnDeadEndPlans", 3), ("AIRDROP.RewardXP", 200), ("AIRDROP.RewardGold", 150),
            ("LEADER.MinLevel", 3), ("LEADER.BountyGoldPerRefresh", 50), ("LEADER.BountyGoldMax", 250),
            ("COMBAT.BreachMaxStacks", 5), ("COMBAT.BurnMaxStacks", 5),
        ):
            expected[key] = (None, value)
        self.assertEqual(set(self.data["parameters"]), set(expected))
        for key, (unit, value) in expected.items():
            with self.subTest(key=key):
                actual = self.catalog.parameter(key)
                if unit is None:
                    self.assertIs(type(actual), int)
                    self.assertEqual(actual, value)
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
        self.assertFalse(self.data["policies"]["ShopRequiresLineOfSight"])
        self.assertEqual(self.data["policies"]["CampAggroTrigger"], "ConfirmedCombatHit")
        self.assertEqual(self.data["policies"]["MissingSourceCooldown"], "ContinueServerTime")
        self.assertEqual(self.data["policies"]["DuplicateEquipmentPassive"], "OncePerFamily")
        self.assertNotIn("DuplicateEquipmentActive", self.data["policies"])
        self.assertEqual(min(amount(self.catalog.parameter("VISION.RadiusCm")),
                             2 * amount(self.catalog.parameter("UNIT.BasicAttackRangeCm"))), 440)

    def test_respawn_protection_is_display_metadata_not_an_effect_or_duration_change(self):
        ids = content.parse_json((content.ROOT / content.IDS_SOURCE).read_bytes(), "native IDs")
        self.assertEqual(ids["statuses"], {"root": 1, "shield": 2, "camp_returning": 3, "respawn_protection": 4,
                                           "slow": 5, "stat_boost": 6, "exposed": 9,
                                           "taunt": 10, "disarm": 12, "stun": 13, "empowered": 14,
                                           "knockback": 20, "silence": 25, "burn": 26})
        self.assertEqual(ids["effect_ops"], {
            "damage": 1, "shield": 2, "dispel_root": 3, "root": 4, "stat_bonus": 5,
            "lifesteal": 7, "dash": 8, "cleave": 10, "dash_boost": 11,
            "slow": 12, "timed_stat_bonus": 13, "self_area": 14, "low_hp_stat_bonus": 15,
            "control_damage_amp": 17, "taunt": 18, "disarm": 20, "stun": 21, "empower_basic": 22,
            "thorns": 24, "cooldown_refund": 26, "ability_crit": 27, "triggered_shield": 31,
            "crit_damage_reduction": 34, "nth_basic_bonus": 35, "mana_on_hit": 36, "dispel_slow": 37, "path_slow": 38,
            "knockback": 40, "zone": 50, "zone_slow": 52, "zone_root": 53, "zone_damage": 54,
            "cast_frenzy": 65, "affliction_amp": 66,
            "shield_break_retort": 67, "hit_haste": 68, "silence": 69, "breach": 70, "burn": 71, "execute": 72,
            "pierce": 73, "echo": 74, "taunt_shield": 75, "dispel_disarm": 76, "zone_burn": 77})
        entries = [row for row in self.data["display"] if row["id"] == "respawn_protection"]
        self.assertEqual(len(entries), 1)
        self.assertEqual({key: value for key, value in entries[0].items() if not key.endswith("_text")}, {
            "id": "respawn_protection", "namespace": "DemoContent",
            "name_key": "respawn_protection.name", "description_key": "respawn_protection.description",
            "icon_id": "icon_respawn_protection"})
        self.assertEqual(entries[0]["name_text"], "复活保护")
        self.assertEqual(self.catalog.parameter("LIFE.ProtectionSec"), {"unit": "ms", "ms": 3000})
        for row in (*self.data["abilities"], *self.data["equipment"]):
            self.assertNotIn("respawn_protection", {effect["op"] for effect in content.all_effects(row)})

    def test_display_names_are_the_approved_fog_harbor_names(self):
        # #111 雾港 v2 entry names (CONTENT_AND_BALANCE 3.2/4/5/6.2/12.5); #148 status names use generic game terms.
        self.assertEqual({row["id"]: row["name_text"] for row in self.data["display"]}, {
            "character_demo": "脆皮大学生",
            # #147 build v7 skills and attribute cards.
            "active_pants_down": "扒裤子", "active_crotch_grab": "掏蛋子", "active_snark_bolt": "阴阳怪气",
            "active_seethe": "原地红温", "active_bail_out": "润了润了", "active_elbow": "一个肘击", "active_not_mad": "我没急",
            "active_stare_down": "你瞅啥", "active_quick_meeting": "临时开个短会", "active_account_banned": "你号没了",
            "active_unplug": "拔你网线", "active_triple_combo": "一键三连",
            "passive_pressed": "急了", "passive_tilted": "破防了", "passive_stoke": "拱火", "passive_kick_down": "痛打落水狗",
            "passive_thick_face": "城墙脸皮", "passive_freeloader": "白嫖怪", "passive_special_forces": "特种兵",
            "passive_moral_victory": "精神胜利", "passive_iced_americano": "冰美式续命", "passive_lucky_star": "欧皇附体",
            "attr_protein": "吃了口蛋白粉", "attr_fast_hands": "单身手速", "attr_all_nighter": "熬夜冲浪", "attr_canteen": "食堂大锅饭",
            "attr_big_talk": "满嘴跑火车", "attr_morning_rush": "早八冲刺", "attr_hand_out": "伸手党", "attr_wild_frenzy": "彻底疯狂",
            "item_iron_plate": "防弹秋裤", "item_edge_blade": "折叠椅（摔跤同款）", "item_runic_focus": "键盘",
            "item_siphon_emblem": "大金链子", "item_phase_anchor": "《润学指南》", "item_arcane_battery": "充电宝",
            "item_echo_maul": "自拍杆", "item_slingshot": "大号滋水枪", "item_nail_uniform": "扎心铆钉皮衣",
            "item_megaphone": "大喇叭", "item_dossier": "黑料小本本", "item_privacy_screen": "防窥膜", "item_skateboard": "滑板鞋",
            "monster_scavenger": "已读不回的 HR", "monster_guardian": "追着办卡的私教", "monster_dean": "画饼老板",
            "root": "定身", "shield": "护盾", "camp_returning": "回位", "respawn_protection": "复活保护",
            "slow": "减速", "stat_boost": "增益", "exposed": "破防",
            "taunt": "嘲讽", "disarm": "缴械", "stun": "眩晕", "empowered": "强化普攻",
            "knockback": "击退",
            "silence": "沉默", "burn": "灼烧",
        })
        for row in self.data["display"]:
            with self.subTest(identity=row["id"]):
                self.assertEqual(row["namespace"], "DemoContent")
                self.assertTrue(row["description_text"])
                self.assertIsNone(content.DISPLAY_DESCRIPTION_DIGIT.search(row["description_text"]))
                self.assertIsNone(content.DISPLAY_TEXT_FORBIDDEN.search(row["description_text"]))
                # #148: rule text names attributes, resources, damage, statuses, the basic attack and the dash generically.
                self.assertEqual([term for term in RETIRED_RULE_TERMS if term in row["description_text"]], [])
                # #147: every skill card has an advanced line for its top rank, under the same text rules.
                skill = row["id"].startswith(("active_", "passive_"))
                self.assertEqual(("advanced_key" in row, "advanced_text" in row), (skill, skill))
                if skill:
                    self.assertEqual(row["advanced_key"], row["id"] + ".advanced")
                    self.assertTrue(row["advanced_text"])
                    self.assertIsNone(content.DISPLAY_DESCRIPTION_DIGIT.search(row["advanced_text"]))
                    self.assertIsNone(content.DISPLAY_TEXT_FORBIDDEN.search(row["advanced_text"]))
                    self.assertEqual([term for term in RETIRED_RULE_TERMS if term in row["advanced_text"]], [])

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

    def test_all_active_rank_values(self):
        # #147 build v7 actives (CONTENT_AND_BALANCE): two ranks sharing target, windup, mana and cooldown.
        self.assertEqual(tuple(row["id"] for row in self.data["abilities"] if row["kind"] == "active"), tuple(sorted(ACTIVE_IDS)))
        for identity, (mode, reach, windup, mana, cooldown) in ACTIVE_FRAMES.items():
            with self.subTest(identity=identity):
                row = self.catalog.definition(identity)
                self.assertEqual((row["target"]["mode"], amount(row["windup"]), row["cooldown_family"], row["draftable"]),
                                 (mode, windup, "cd_" + identity[len("active_"):], True))
                self.assertEqual(amount(row["target"]["range"]) if "range" in row["target"] else None, reach)
                for rank in row["ranks"]:
                    self.assertEqual((amount(rank["mana_cost"]), amount(rank["cooldown"])), (mana, cooldown))
        bolt = self.catalog.definition("active_snark_bolt")["target"]
        self.assertEqual((amount(bolt["projectile_radius"]), amount(bolt["projectile_speed"])), (45, 1600))
        self.assert_rank_effects(ACTIVE_IDS)

    def assert_rank_effects(self, identities):
        for identity in identities:
            with self.subTest(identity=identity):
                ranks = self.catalog.definition(identity)["ranks"]
                self.assertEqual([rank["rank"] for rank in ranks], [1, 2])
                self.assertEqual(tuple([(effect["op"], author_units(effect)) for effect in rank["effects"]] for rank in ranks),
                                 RANK_EFFECTS[identity])

    def test_advanced_rank_adds_one_change_and_grows_bases_by_half(self):
        # #147 D2: the advanced rank keeps every coefficient and basic effect, holds each growing base at 1.5x the basic
        # rank's, and adds its one gameplay change.
        self.assertEqual(self.data["progression"]["max_skill_rank"], 2)
        self.assertEqual(set(ADVANCED_CHANGES), {row["id"] for row in self.data["abilities"]})
        for identity, change in ADVANCED_CHANGES.items():
            with self.subTest(identity=identity):
                basic, advanced = (rank["effects"] for rank in self.catalog.definition(identity)["ranks"])
                self.assertEqual([effect["op"] for effect in basic], [effect["op"] for effect in advanced][:len(basic)])
                twins = {effect["op"]: effect for effect in advanced}
                added = set(twins) - {effect["op"] for effect in basic}
                for effect in basic:
                    twin = twins[effect["op"]]
                    self.assertLessEqual(set(effect), set(twin))
                    added |= {f"{effect['op']}.{field}" for field in set(twin) - set(effect)}
                    for field in ("ad_ratio", "ap_ratio", "max_hp_ratio", "armor_ratio"):
                        if field in effect:
                            self.assertEqual(amount(twin[field]), amount(effect[field]), f"{effect['op']}.{field}")
                    if "base" in effect:
                        self.assertEqual(amount(twin["base"]), amount(effect["base"]) * Fraction(3, 2), effect["op"])
                self.assertEqual(added, change)

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

        def first(data, identity, op, rank=0):
            return next(effect for effect in ability(data, identity)["ranks"][rank]["effects"] if effect["op"] == op)

        def display(data, identity):
            return next(row for row in data["display"] if row["id"] == identity)

        for name, mutate, code in (
            ("area on a unit target", lambda d: [rank["effects"].append({"op": "self_area", "radius": {"unit": "cm", "numerator": 100, "denominator": 1}})
                                                 for rank in ability(d, "active_pants_down")["ranks"]], "TargetEffectMismatch"),
            ("slow that stops the body", lambda d: first(d, "active_snark_bolt", "slow")["ratio"].update(bp=10000), "NumericDomain"),
            ("zero-radius area", lambda d: first(d, "active_seethe", "self_area")["radius"].update(numerator=0), "NumericDomain"),
            ("timed resource maximum", lambda d: first(d, "active_stare_down", "timed_stat_bonus")["stats"].update(
                MaxHPFlat={"unit": "hp", "p2": 100}), "UnknownField"),
            ("threshold at a full pool", lambda d: first(d, "passive_pressed", "low_hp_stat_bonus")["threshold"].update(bp=10000),
             "NumericDomain"),
            ("zone payload without its zone", lambda d: [rank["effects"].remove(next(e for e in rank["effects"] if e["op"] == "zone"))
                                                         for rank in ability(d, "active_quick_meeting")["ranks"]], "MissingHandler"),
            ("ineffective zone damage", lambda d: [first(d, "active_quick_meeting", "zone_damage")[field].update(**{key: 0})
                                                   for field, key in (("base", "p2"), ("ad_ratio", "bp"), ("ap_ratio", "bp"))],
             "IneffectiveEffect"),
            ("zero control amplification", lambda d: first(d, "passive_kick_down", "control_damage_amp")["ratio"].update(bp=0),
             "NumericDomain"),
            ("advanced base off 1.5x", lambda d: first(d, "active_snark_bolt", "damage", 1)["base"].update(p2=13600), "AdvancedRank"),
            ("coefficient changes between ranks", lambda d: first(d, "active_snark_bolt", "damage", 1)["ap_ratio"].update(bp=9000),
             "AdvancedRank"),
            ("card placeholder naming an absent effect", lambda d: display(d, "active_snark_bolt").update(
                description_text="造成 [execute.ratio] 伤害。"), "CardTemplate"),
            ("advanced placeholder naming an absent effect", lambda d: display(d, "active_snark_bolt").update(
                advanced_text="造成 [zone.radius] 伤害。"), "CardTemplate"),
            ("advanced line on a non-skill", lambda d: display(d, "item_iron_plate").update(
                advanced_key="item_iron_plate.advanced", advanced_text="升级说明。"), "DisplayReference"),
        ):
            projected = schema.project(author)
            mutate(projected)
            with self.subTest(case=name), self.assertRaisesRegex(content.ContentError, code):
                content.validate_definitions(projected, ids)

    def test_passives_attributes_and_build_bounds(self):
        passives = tuple(sorted(identity for identity in RANK_EFFECTS if identity.startswith("passive_")))
        self.assertEqual(tuple(row["id"] for row in self.data["abilities"] if row["kind"] == "passive"), passives)
        self.assertEqual(len(passives), 10)
        for identity in passives:
            row = self.catalog.definition(identity)
            self.assertEqual((row["target"]["mode"], row["draftable"]), ("Self", True))
            self.assertFalse({"cooldown_family", "windup"} & set(row))
            self.assertTrue(all(set(rank) == {"rank", "effects"} for rank in row["ranks"]))
        self.assert_rank_effects(passives)
        # #147 v7: eight repeatable composite cards; the wild card names its explicit deduction.
        expected = {
            "attr_protein": ({"ADFlat": 6, "MaxHPFlat": 60}, None),
            "attr_fast_hands": ({"AttackSpeedBonusRatio": r(".1"), "ADFlat": 4}, None),
            "attr_all_nighter": ({"APFlat": 24, "CooldownHasteFlat": 10}, None),
            "attr_canteen": ({"MaxHPFlat": 90, "ArmorFlat": 6, "MagicResistFlat": 6}, None),
            "attr_big_talk": ({"APFlat": 18, "MaxHPFlat": 60}, None),
            "attr_morning_rush": ({"MoveSpeedBonusRatio": r(".05"), "MaxHPFlat": 70}, None),
            "attr_hand_out": ({"AttackRangeCmFlat": 60, "AttackSpeedBonusRatio": r(".08")}, None),
            "attr_wild_frenzy": ({"ADFlat": 14, "AttackSpeedBonusRatio": r(".12")}, {"MaxHPFlat": 80}),
        }
        self.assertEqual({row["id"] for row in self.data["attributes"]}, set(expected))
        for identity, (stats, penalties) in expected.items():
            row = self.catalog.definition(identity)
            self.assertEqual({key: amount(value) for key, value in row["stats"].items()}, stats)
            self.assertEqual(None if "penalties" not in row else {key: amount(value) for key, value in row["penalties"].items()},
                             penalties)

    def test_every_equipment_price_stat_and_effect(self):
        expected = {
            "item_iron_plate": (310, 201, {"MaxHPFlat": 180, "ArmorFlat": 15}),
            "item_edge_blade": (320, 208, {"ADFlat": 20, "AttackSpeedBonusRatio": Fraction(".10")}),
            "item_runic_focus": (320, 208, {"APFlat": 30, "CooldownHasteFlat": 15}),
            "item_siphon_emblem": (360, 234, {"ADFlat": 12, "MagicResistFlat": 12}),
            "item_phase_anchor": (400, 260, {"MoveSpeedBonusRatio": Fraction(".12")}),
            "item_arcane_battery": (400, 260, {"MaxMPFlat": 120, "MagicResistFlat": 10}),
            "item_echo_maul": (400, 260, {"ADFlat": 14, "AttackRangeCmFlat": 40}),
            "item_slingshot": (320, 208, {"AttackRangeCmFlat": 120, "AttackSpeedBonusRatio": Fraction(".12")}),
            "item_nail_uniform": (380, 247, {"MaxHPFlat": 150, "ArmorFlat": 18}),
            "item_dossier": (400, 260, {"APFlat": 25, "MaxHPFlat": 120}),
        }
        for identity, (price, sell, stats) in expected.items():
            item = self.catalog.definition(identity)
            self.assertEqual((item["buy_price"], item["sell_gold"]), (price, sell))
            self.assertEqual({key: amount(value) for key, value in item["stats"].items()}, stats)
        # D19/D20: the innate D dash belongs to the character; the spikes only boost it passively.
        dash = self.data["character"]["dash"]
        self.assertEqual((dash["mana_cost"]["p2"], dash["cooldown"]["ms"], amount(dash["distance"]), dash["duration"]["ms"]),
                         (0, 18000, 300, 200))
        spikes = self.catalog.definition("item_phase_anchor")
        self.assertEqual(spikes["passive"]["family"], "passive_stride")
        boost = spikes["passive"]["effects"][0]
        self.assertEqual((boost["op"], boost["cooldown_ratio"]["bp"], boost["distance_bonus"]["bp"]), ("dash_boost", 5000, 5000))
        self.assertEqual((boost["skill_cooldown_ratio"]["bp"], boost["skill_distance_bonus"]["bp"]), (8000, 2500))
        # #147: no equipment has an active or mechanic tags; the power bank, dossier and slingshot keep only their fixed
        # stats until a passive is approved.
        self.assertTrue(all(set(item) == {"id", "buy_price", "stats", "passive", "native_id", "sell_gold", "upgraded_stats"}
                            for item in self.data["equipment"]))
        for identity in ("item_arcane_battery", "item_dossier", "item_slingshot"):
            self.assertIsNone(self.catalog.definition(identity)["passive"])
        self.assertEqual(self.catalog.definition("item_siphon_emblem")["passive"]["effects"][0]["ratio"]["bp"], 1500)
        cleave = self.catalog.definition("item_echo_maul")["passive"]["effects"][0]
        self.assertEqual((cleave["ratio"]["bp"], amount(cleave["radius"])), (3500, 180))
        # CONTENT_AND_BALANCE 15.8 item passives, approved on 2026-11-04 as playtest input.
        self.assertEqual(self.catalog.definition("item_iron_plate")["passive"]["effects"][0]["ratio"]["bp"], 1500)
        third = self.catalog.definition("item_edge_blade")["passive"]["effects"][0]
        self.assertEqual((third["count"], third["base"]["p2"], third["damage_type"]), (3, 2000, "Physical"))
        self.assertEqual(self.catalog.definition("item_runic_focus")["passive"]["effects"][0]["mana"]["p2"], 500)
        prickle = self.catalog.definition("item_nail_uniform")["passive"]
        self.assertEqual(prickle["family"], "passive_prickle")
        thorns = prickle["effects"][0]
        self.assertEqual((thorns["base"]["p2"], thorns["armor_ratio"]["bp"], thorns["max_hp_ratio"]["bp"],
                          thorns["critical_multiplier"]["bp"], thorns["trigger"]), (2000, 1000, 100, 10000, "Basic"))
        self.assertNotIn("rear_exempt", thorns)
        # The four starting items fit the 320 opening gold.
        self.assertTrue(all(self.catalog.definition(identity)["buy_price"] <= 320
                            for identity in ("item_iron_plate", "item_edge_blade", "item_slingshot", "item_runic_focus")))

    def test_advanced_changes_refuse_on_the_basic_rank(self):
        author, schema, ids = self.approved_projection_inputs()
        for identity, op in (("active_crotch_grab", "execute"), ("active_bail_out", "path_slow"), ("active_not_mad", "dispel_disarm"),
                             ("passive_lucky_star", "ability_crit"), ("passive_special_forces", "dash_boost")):
            projected = schema.project(author)
            row = next(item for item in projected["abilities"] if item["id"] == identity)
            row["ranks"][0]["effects"].append(next(e for e in row["ranks"][1]["effects"] if e["op"] == op))
            with self.subTest(identity=identity), self.assertRaisesRegex(content.ContentError, "MissingHandler"):
                content.validate_definitions(projected, ids)
            projected = schema.project(author)
            row = next(item for item in projected["abilities"] if item["id"] == identity)
            row["ranks"][1]["effects"] = [e for e in row["ranks"][1]["effects"] if e["op"] != op]
            with self.subTest(identity=identity, missing=True), self.assertRaisesRegex(content.ContentError, "MissingHandler"):
                content.validate_definitions(projected, ids)

    def test_only_seethe_damage_scales_with_the_casters_max_hp(self):
        scaled = {row["id"] for row in self.data["abilities"] for rank in row["ranks"] for effect in rank["effects"]
                  if effect["op"] == "damage" and effect["max_hp_ratio"]["bp"] != 0}
        self.assertEqual(scaled, {"active_seethe"})

    def test_self_area_damage_refuses_without_its_area(self):
        author, schema, ids = self.approved_projection_inputs()
        projected = schema.project(author)
        seethe = next(row for row in projected["abilities"] if row["id"] == "active_seethe")
        for rank in seethe["ranks"]:
            rank["effects"] = [effect for effect in rank["effects"] if effect["op"] != "self_area"]
        with self.assertRaisesRegex(content.ContentError, "TargetEffectMismatch"):
            content.validate_definitions(projected, ids)

    def test_v7_contracts_reject_misplaced_mechanics(self):
        author, schema, ids = self.approved_projection_inputs()
        for identity, change in (
            ("active_account_banned", lambda effects: effects.pop(0)),
            ("active_unplug", lambda effects: effects.append({"op": "stun", "duration": {"unit": "ms", "ms": 500}})),
            ("active_bail_out", lambda effects: effects.pop(0)),
            ("passive_tilted", lambda effects: effects[0].pop("basic_stacks")),
        ):
            projected = schema.project(author)
            ability = next(row for row in projected["abilities"] if row["id"] == identity)
            for rank in ability["ranks"]:
                change(rank["effects"])
            with self.subTest(identity=identity), self.assertRaises(content.ContentError):
                content.validate_definitions(projected, ids)
        projected = schema.project(author)
        face = next(row for row in projected["abilities"] if row["id"] == "passive_thick_face")
        face["ranks"][0]["effects"][0]["critical_multiplier"] = {"unit": "ratio", "bp": 5000}
        with self.assertRaisesRegex(content.ContentError, "NumericDomain"):
            content.validate_definitions(projected, ids)
        projected = schema.project(author)
        star = next(row for row in projected["abilities"] if row["id"] == "passive_lucky_star")
        crit = next(effect for effect in star["ranks"][1]["effects"] if effect["op"] == "ability_crit")
        crit["abilities"] = [*crit["abilities"], "active_arc_bolt"]
        with self.assertRaisesRegex(content.ContentError, "InvalidReference"):
            content.validate_definitions(projected, ids)

    def test_monster_and_progression_values(self):
        self.assertEqual(self.data["progression"]["xp_thresholds"], (0, 80, 180, 300, 440, 600, 780, 980, 1200, 1440))
        self.assertEqual(self.data["progression"]["xp_to_next"], (80, 100, 120, 140, 160, 180, 200, 220, 240))
        # 112 §6: the opening offer is two new actives and one new passive; level 4 and 7 each earn one reroll.
        self.assertEqual(self.data["progression"]["opening_offer"], {"active_count": 2, "passive_count": 1})
        self.assertEqual(self.data["progression"]["reroll_levels"], (4, 7))
        # #147: two skill ranks (初级/高级).
        self.assertEqual(self.data["progression"]["max_skill_rank"], 2)
        for identity, tier, count, hp, resist, attack, rate, windup, reach, speed, leash, grace, respawn, xp, gold, interval in (
            ("monster_scavenger", "Low", 2, 90, 8, 10, ".80", 350, 150, 240, 900, 3000, 25000, 40, 30, 1250),
            ("monster_guardian", "High", 1, 420, 20, 48, ".65", 500, 175, 260, 1100, 4000, 65000, 160, 120, 1539),
        ):
            row = self.catalog.definition(identity)
            self.assertEqual((row["tier"], row["count_per_camp"], row["reward_xp"], row["reward_gold"]), (tier, count, xp, gold))
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
        for key, expected in (("hp", 1500), ("armor", 30), ("magic_resist", 30), ("attack", 70), ("attack_range", 250),
                              ("move_speed", 225), ("collision_radius", 40), ("aggro_radius", 400)):
            self.assertEqual(amount(boss[key]), expected, key)
        self.assertEqual((boss["attack_interval"]["ms"], boss["attack_windup"]["ms"], boss["attacker_memory"]["ms"],
                          boss["appear_at"]["ms"], boss["reward_xp"], boss["reward_gold"]), (1500, 500, 5000, 60000, 300, 250))
        self.assertEqual(boss["approval"]["duration"]["ms"], 90000)
        self.assertEqual({key: value["bp"] for key, value in boss["approval"]["stats"].items()},
                         {"ADBonusRatio": 2500, "APBonusRatio": 2500, "MoveSpeedBonusRatio": 1000})

    def test_exact_world_surface_anchors_and_member_homes(self):
        expected = {
            "s00": (4400, 0), "s01": (3560, 2586), "s02": (1360, 4185), "s03": (-1360, 4185), "s04": (-3560, 2586),
            "s05": (-4400, 0), "s06": (-3560, -2586), "s07": (-1360, -4185), "s08": (1360, -4185), "s09": (3560, -2586),
            "l1": (2473, 803), "h1": (0, 2600), "l2": (-2473, 803), "h2": (-1528, -2103), "l3": (1528, -2103),
            "shop": (778, 778), "capture": (0, 0), "dorm_nw": (-4400, 4400), "dorm_se": (4400, -4400),
            "airdrop_ne": (1940, 2670), "airdrop_nw": (-1940, 2670), "airdrop_s": (0, -3300),
            "boss_patrol_1": (2400, 2400), "boss_patrol_2": (-2400, 2400), "boss_patrol_3": (-2400, -2400),
            "boss_patrol_4": (2400, -2400),
        }
        self.assertEqual(set(self.anchors), set(expected))
        for identity, xy in expected.items():
            self.assertEqual(world.point(self.anchors[identity]["position_cm"], identity), (*xy, 0))
        homes = {member["id"]: world.point(member["position_cm"], member["id"]) for row in self.anchors.values() for member in row.get("members", ())}
        self.assertEqual(homes, {"l1_0": (2450, 874, 0), "l1_1": (2496, 732, 0), "h1_0": (0, 2600, 0),
                                 "l2_0": (-2496, 732, 0), "l2_1": (-2450, 874, 0), "h2_0": (-1528, -2103, 0),
                                 "l3_0": (1589, -2059, 0), "l3_1": (1467, -2147, 0)})
        self.assertEqual(self.geometry.minimum, (-5000, -5000, 0))
        self.assertEqual(self.geometry.maximum, (5000, 5000, 0))
        self.assertEqual(self.world.world["map_id"], "demo_world")
        self.assertEqual((self.world.world["schema_version"], self.world.world["projection_revision"]), (2, 2))

    def test_exact_polygons_heights_and_collision_classification(self):
        polygons = {
            "midwall_054": ((1748, 832), (1983, 1155), (486, 2243), (251, 1919)),
            "midwall_126": ((-251, 1919), (-486, 2243), (-1983, 1155), (-1748, 832)),
            "midwall_198": ((-1903, 354), (-2283, 231), (-1711, -1529), (-1331, -1405)),
            "midwall_270": ((-925, -1700), (-925, -2100), (925, -2100), (925, -1700)),
            "midwall_342": ((1331, -1405), (1711, -1529), (2283, 231), (1903, 354)),
            "wall_n": ((-5300, 5000), (5300, 5000), (5300, 5300), (-5300, 5300)),
            "wall_s": ((-5300, -5300), (5300, -5300), (5300, -5000), (-5300, -5000)),
            "wall_e": ((5000, -5300), (5300, -5300), (5300, 5300), (5000, 5300)),
            "wall_w": ((-5300, -5300), (-5000, -5300), (-5000, 5300), (-5300, 5300)),
            "shop_stall": ((718, 718), (838, 718), (838, 838), (718, 838)),
        }
        self.assertEqual(set(self.geometry.polygons), set(polygons))
        for identity, xy in polygons.items():
            self.assertEqual(self.geometry.polygons[identity], tuple((*p, 0) for p in xy))
            row = self.geometry.rows[identity]
            self.assertEqual(content._wire_fraction(row["height_cm"], identity), 80 if identity == "shop_stall" else 600)
            self.assertTrue(row["blocks_movement"])
            self.assertEqual(row["blocks_sight"], identity != "shop_stall")
            self.assertNotIn("min_cm", row)
        self.assertEqual(content._wire_fraction(self.world.world["geometry"]["sight_sample_height_cm"], "sight"), 120)
        self.assertEqual(content._wire_fraction(self.world.world["geometry"]["camp_clearance_cm"], "clearance"), 500)

    def test_exact_placement_connections_and_parameter_references(self):
        placement = self.world.world["placement"]
        self.assertEqual([world.point(row, "offset") for row in placement["spawn_offsets_cm"]],
                         [(0, 0, 0), (100, 0, 0), (50, 87, 0), (-50, 87, 0), (-100, 0, 0), (-50, -87, 0), (50, -87, 0)])
        self.assertEqual(content._wire_fraction(placement["spawn_validation_radius_cm"], "envelope"), 200)
        self.assertEqual([(row["id"], tuple(row["wall_ids"]), row["camp_id"]) for row in self.world.world["connections"]], [
            ("g018", ("midwall_342", "midwall_054"), "l1"), ("g090", ("midwall_054", "midwall_126"), "h1"),
            ("g162", ("midwall_126", "midwall_198"), "l2"), ("g234", ("midwall_198", "midwall_270"), "h2"),
            ("g306", ("midwall_270", "midwall_342"), "l3")])
        self.assertEqual(self.world.world["parameter_refs"], (
            "AIRDROP.PickupRadiusCm", "CAMP.ResetArrivalRadiusCm", "CAPTURE.RadiusCm", "DORM.RadiusCm", "SHOP.InteractRadiusCm", "TIMING.DefendRouteBudgetSec",
            "UNIT.BasicAttackRangeCm", "UNIT.CollisionRadiusCm", "UNIT.MoveSpeedCmPerSec", "VISION.RadiusCm"))

    def test_published_route_lengths_and_whole_ground_budget(self):
        captures = (4175.255, 4175.436, 4175.833, 4175.833, 4175.436, 4175.255, 4175.554, 4175.820, 4175.820, 4175.554)
        shops = (3104.614, 2933.627, 3229.117, 3422.275, 4468.133, 4636.361, 4974.744, 4952.690, 4397.008, 4136.503)
        lows = (
            (2087.615, 6923.428, 3559.634), (2088.219, 6516.459, 5110.357), (3560.434, 5111.733, 6515.950),
            (5111.733, 3560.434, 6923.191), (6516.459, 2088.219, 6922.817), (6923.428, 2087.615, 6515.710),
            (6923.400, 3559.057, 5110.874), (6516.137, 5110.667, 3560.234), (5110.667, 6516.137, 2088.767),
            (3559.057, 6923.400, 2088.615),
        )
        for index in range(10):
            identity = f"s{index:02d}"
            route = self.world.report["routes"][identity]
            self.assertAlmostEqual(route["capture_cm"], captures[index], delta=.00051)
            self.assertAlmostEqual(route["shop_cm"], shops[index], delta=.00051)
            start = world.point(self.anchors[identity]["position_cm"], identity)
            for camp_id, expected in zip(("l1", "l2", "l3"), lows[index]):
                target = world.point(self.anchors[camp_id]["position_cm"], camp_id)
                self.assertAlmostEqual(self.model.distance(start, target, 0), expected, delta=.00051)
        for identity, capture, shop in (
            ("l1", 2300.103, 1095.505), ("h1", 2300, 1390.297), ("l2", 2300.103, 2651.504),
            ("h2", 2299.499, 3090.230), ("l3", 2299.499, 2381.535),
        ):
            self.assertAlmostEqual(self.world.report["routes"][identity]["capture_cm"], capture, delta=.00051)
            self.assertAlmostEqual(self.world.report["routes"][identity]["shop_cm"], shop, delta=.00051)
        self.assertAlmostEqual(self.world.report["route_upper_bound_cm"], 9524.661, delta=.00051)
        speed = amount(self.data["parameters"]["UNIT.MoveSpeedCmPerSec"])
        budget = amount(self.data["parameters"]["TIMING.DefendRouteBudgetSec"])
        self.assertEqual((speed, budget), (300, 35))
        self.assertLess(self.world.report["route_upper_bound_cm"], speed * budget)
        self.assertGreater(self.world.report["route_upper_bound_cm"], speed * 31)
        self.assertAlmostEqual(self.world.report["min_inner_angular_gap_degrees"], 11.662, delta=.00051)
        candidates = [row["capture_cm"] for key, row in self.world.report["routes"].items() if ":" in key]
        self.assertEqual(len(candidates), 70)
        self.assertAlmostEqual(min(candidates), 4075.611, delta=.00051)
        self.assertAlmostEqual(max(candidates), 4276.064, delta=.00051)

    def test_actual_melee_opportunities_and_sight_counterexamples(self):
        self.assertTrue(self.geometry.sight_clear((0, 1500, 0), (0, 2600, 0)))
        self.assertFalse(self.geometry.sight_clear((0, -1650, 0), (0, -2150, 0)))
        for identity, approach in (
            ("l1", (2616, 849, 0)), ("h1", (0, 2800, 0)), ("l2", (-2616, 849, 0)),
            ("h2", (-1646, -2265, 0)), ("l3", (1616, -2224, 0)),
        ):
            self.assertTrue(self.geometry.legal(approach, Fraction(40)))
            for member in self.anchors[identity]["members"]:
                home = world.point(member["position_cm"], member["id"])
                distance = math.sqrt(float(world.squared_distance(approach, home)))
                self.assertGreaterEqual(distance, 80)
                self.assertLessEqual(distance, 220)
                self.assertTrue(self.geometry.sight_clear(approach, home))
        low = self.anchors["l1"]
        self.assertLess(world.squared_distance(world.point(low["position_cm"], "l1"),
                                              world.point(low["members"][0]["position_cm"], "l1_0")), 80 ** 2)
        self.assertFalse(self.geometry.legal((778, 778, 0), Fraction(40)))

    def test_byte_provenance_and_acyclic_producer_order(self):
        self.assertEqual(self.world.world["source_sha256"], content.sha256((content.ROOT / content.WORLD_SOURCE).read_bytes()))
        self.assertEqual(self.world.world["author_schema_sha256"], content.sha256((content.ROOT / content.WORLD_SCHEMA_SOURCE).read_bytes()))
        self.assertFalse(set(world.OUTPUT_PATHS).intersection(self.world.inputs))
        self.assertFalse(set(content.OUTPUT_PATHS).intersection(self.world.inputs))
        with patch.object(content, "compile_content", side_effect=AssertionError("world depends on generated content root")):
            self.assertEqual(world.compile_world().outputs, self.world.outputs)
        self.assertEqual(set(self.world.world), {
            "schema_version", "projection_revision", "map_id", "layout_revision", "geometry_revision",
            "source_sha256", "author_schema_sha256", "approval", "geometry", "placement", "connections", "parameter_refs", "anchors"})
        self.assertNotIn(self.root_bundle.digest.hex(), self.world.outputs[content.WORLD_OUTPUT].decode("utf-8"))
        self.assertEqual(content.verify_runtime_files(content.ROOT, self.root_bundle.digest), self.root_bundle.digest)
        content.check_outputs(content.ROOT, self.root_bundle.outputs)
        world_sync.synchronize(check=True)


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
        cases = (
            ((), ()),
            (("approval",), ("properties", "approval")),
            (("geometry",), ("properties", "geometry")),
            (("geometry", "surfaces", 0), ("properties", "geometry", "properties", "surfaces", "items")),
            (("geometry", "surfaces", 0, "min_cm"), ("$defs", "vector")),
            (("geometry", "surfaces", 0, "max_cm"), ("$defs", "vector")),
            (("geometry", "blockers", 9), ("properties", "geometry", "properties", "blockers", "items")),
            (("geometry", "blockers", 9, "polygon_cm", 3), ("$defs", "vector")),
            (("placement",), ("properties", "placement")),
            (("placement", "spawn_offsets_cm", 6), ("$defs", "vector")),
            (("connections", 4), ("properties", "connections", "items")),
            (("anchors", 16), ("$defs", "anchor", "oneOf", 0)),
            (("anchors", 16, "position_cm"), ("$defs", "vector")),
            (("anchors", 14), ("$defs", "anchor", "oneOf", 1)),
            (("anchors", 14, "members", 1), ("$defs", "anchor", "oneOf", 1, "properties", "members", "items")),
            (("anchors", 14, "members", 1, "position_cm"), ("$defs", "vector")),
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
        for change in (
            lambda d: d.update(coordinate_unit="m"),
            lambda d: d["approval"].update(status="pending"),
            lambda d: d["geometry"]["blockers"].pop(),
            lambda d: d["geometry"]["blockers"][0]["polygon_cm"].reverse(),
            lambda d: d["geometry"]["blockers"][0]["polygon_cm"][0].update(z=1),
            lambda d: d["geometry"]["blockers"][0].update(height_cm=0),
            lambda d: d["geometry"]["blockers"][-1].update(blocks_sight=True),
            lambda d: d["placement"]["spawn_offsets_cm"][1].update(x=300),
            lambda d: d["placement"]["spawn_offsets_cm"][1].update(x=True),
            lambda d: d["connections"][0]["wall_ids"].__setitem__(0, "unknown_wall"),
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
        self.edit(content.WORLD_SOURCE, lambda d: d.update(layout_revision=2))
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
