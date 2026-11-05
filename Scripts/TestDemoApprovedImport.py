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
        for text in ("2026-11-02", "D01", "D02", "D03", "2026-11-04", "#82", "15.7-15.9", "playtest", "not a balance"):
            self.assertIn(text, approval["decision"])
        self.assertNotIn("TEST ONLY", approval["decision"])
        self.assertIn("player 400, camps 320/347, route budget 26s, move-speed floor 230, camera 2250 (1300-2400)", approval["decision"])
        self.assertEqual((len(self.data["abilities"]), len(self.data["attributes"]), len(self.data["equipment"]),
                          len(self.data["camps"]["monsters"])), (30, 14, 16, 2))
        self.assertEqual([row["id"] for row in self.data["abilities"]], [
            "active_arc_bolt", "active_barrage_rant", "active_binding_snare", "active_body_slam", "active_cancel_blast",
            "active_combo_flurry", "active_come_here", "active_expose_post", "active_fart_dash", "active_guard_pulse",
            "active_lunge", "active_monkey_peach", "active_pie_chart", "active_porcelain_flop", "active_slick_moves",
            "active_slipper_throw", "active_table_flip", "active_thousand_year",
            "passive_battle_rhythm", "passive_cornered", "passive_dox_master", "passive_eagle_eye", "passive_iron_crotch",
            "passive_keyboard_warrior", "passive_lurking", "passive_mental_gymnastics", "passive_precision",
            "passive_public_shame", "passive_second_wind", "passive_thick_skin"])
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
            ("STAT.CooldownHaste.Base", "points", "0"), ("STAT.CooldownHaste.Max", "points", "60"),
            ("UNIT.MoveSpeedCmPerSec", "cm/s", "400"), ("STAT.MoveSpeed.Min", "cm/s", "230"), ("STAT.MoveSpeed.Max", "cm/s", "900"),
            ("UNIT.CollisionRadiusCm", "cm", "40"), ("UNIT.BasicAttackRangeCm", "cm", "220"),
            ("STAT.AttackRange.Min", "cm", "80"), ("STAT.AttackRange.Max", "cm", "1000"),
            ("UNIT.BasicAttackWindupSec", "ms", ".250"), ("VISION.RadiusCm", "cm", "1400"),
            ("SHOP.InteractRadiusCm", "cm", "350"), ("CAPTURE.RadiusCm", "cm", "300"),
            ("CAPTURE.SpraySec", "ms", "3.5"), ("CAPTURE.GuardSec", "ms", "20"), ("TIMING.DefendRouteBudgetSec", "ms", "26"),
            ("MATCH.DevelopmentSec", "ms", "120"), ("LIFE.RespawnSec", "ms", "15"), ("LIFE.ProtectionSec", "ms", "3"),
            ("REGEN.OutOfCombatDelaySec", "ms", "5"), ("REGEN.OutOfCombatMaxHPRatioPerSec", "ratio", ".015"),
            ("REGEN.OutOfCombatMaxMPRatioPerSec", "ratio", ".015"),
            ("SHOP.SellRatio", "ratio", ".65"), ("CAMP.ResetArrivalRadiusCm", "cm", "100"),
            ("REWARD.AssistWindowSec", "ms", "15"), ("REWARD.AssistGoldRatio", "ratio", ".5"),
            ("SHOP.UpgradeStatBonusRatio", "ratio", ".35"), ("CHOICE.UpgradeWeight", "ratio", "2.2"),
            ("CHOICE.NewActiveOpenSlotWeight", "ratio", "1.5"), ("CHOICE.NewActiveFullSlotWeight", "ratio", ".30"),
            ("CHOICE.NewPassiveWeight", "ratio", "1"), ("CHOICE.AffinityStep", "ratio", "1.30"),
            ("CHOICE.AffinityCap", "ratio", "1.70"), ("CHOICE.ThirdSlotAttributeChance", "ratio", ".65"),
            ("AI.RetreatHPRatio", "ratio", ".25"), ("AI.ResumeFarmHPRatio", "ratio", ".60"),
            ("AI.HighCampEntryHPRatio", "ratio", ".80"), ("AI.HighCampReserveHPRatio", "ratio", ".35"),
            ("AI.LowCampReserveHPRatio", "ratio", ".15"), ("AI.CaptureReactionBudgetSec", "ms", "2"),
            ("AI.DecisionIntervalSec", "ms", ".25"), ("AI.RetryPolicy.InitialBackoffSec", "ms", ".25"),
            ("AI.RetryPolicy.MaxBackoffSec", "ms", ".50"), ("AI.ExploreRevisitIntervalSec", "ms", "20"),
            ("AI.EngagementBudgetSec", "ms", "40"), ("COMBAT.RearArcDeg", "deg", "120"),
        ):
            expected[key] = (unit, value)
        for key, value in (
            ("SHOP.StartingGold", 320), ("SHOP.UpgradeCost", 240), ("XP.BaseToNext", 80), ("XP.IncreasePerLevel", 20),
            ("REWARD.PlayerXP.Base", 100), ("REWARD.PlayerXP.PerLevel", 20),
            ("REWARD.PlayerGold.Base", 80), ("REWARD.PlayerGold.PerLevel", 10), ("AI.RetryPolicy.MaxAttempts", 3),
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
        self.assertEqual(self.data["policies"]["DuplicateEquipmentActive"], "IndependentSources")
        self.assertEqual(min(amount(self.catalog.parameter("VISION.RadiusCm")),
                             2 * amount(self.catalog.parameter("UNIT.BasicAttackRangeCm"))), 440)

    def test_respawn_protection_is_display_metadata_not_an_effect_or_duration_change(self):
        ids = content.parse_json((content.ROOT / content.IDS_SOURCE).read_bytes(), "native IDs")
        self.assertEqual(ids["statuses"], {"root": 1, "shield": 2, "camp_returning": 3, "respawn_protection": 4,
                                           "slow": 5, "stat_boost": 6, "enraged": 7, "ambush_ready": 8, "exposed": 9,
                                           "taunt": 10, "stealth": 11, "disarm": 12, "stun": 13, "empowered": 14,
                                           "knockback": 20, "heal_cut": 22, "damage_guard": 23,
                                           "vulnerable": 24})
        self.assertEqual(ids["effect_ops"], {
            "damage": 1, "shield": 2, "dispel_root": 3, "root": 4, "stat_bonus": 5,
            "out_of_combat_regen": 6, "lifesteal": 7, "dash": 8, "restore_mana": 9, "cleave": 10, "dash_boost": 11,
            "slow": 12, "timed_stat_bonus": 13, "self_area": 14, "low_hp_stat_bonus": 15, "ambush_damage": 16,
            "control_damage_amp": 17, "taunt": 18, "stealth": 19, "disarm": 20, "stun": 21, "empower_basic": 22,
            "jump": 23, "thorns": 24, "backstab": 25, "cooldown_refund": 26, "ability_crit": 27, "resist_shred": 28,
            "momentum": 29, "taunt_retaliate": 30, "triggered_shield": 31, "basic_stacks": 32, "long_reach": 33,
            "crit_damage_reduction": 34, "nth_basic_bonus": 35, "mana_on_hit": 36, "dispel_slow": 37, "path_slow": 38,
            "knockback": 40, "heal_cut": 41, "dot": 42, "mobility_punish": 43, "volley": 44, "refresh_debuffs": 45,
            "on_hit": 46, "debuff_amp": 47, "shield_shatter": 48, "debuff_heal": 49, "zone": 50, "zone_shield": 51,
            "zone_slow": 52, "zone_root": 53, "zone_damage": 54, "damage_guard": 55, "shield_burst": 56,
            "vulnerable": 57, "decoy": 58, "leap_back": 59, "cooldown_refresh": 60, "breach_stacks": 61,
            "mobility_shield": 62, "shield_pulse": 63, "basic_rider": 64, "cast_frenzy": 65, "affliction_amp": 66,
            "shield_break_retort": 67, "hit_haste": 68})
        entries = [row for row in self.data["display"] if row["id"] == "respawn_protection"]
        self.assertEqual(len(entries), 1)
        self.assertEqual({key: value for key, value in entries[0].items() if not key.endswith("_text")}, {
            "id": "respawn_protection", "namespace": "DemoContent",
            "name_key": "respawn_protection.name", "description_key": "respawn_protection.description",
            "icon_id": "icon_respawn_protection"})
        self.assertEqual(entries[0]["name_text"], "免死金牌")
        self.assertEqual(self.catalog.parameter("LIFE.ProtectionSec"), {"unit": "ms", "ms": 3000})
        for row in (*self.data["abilities"], *self.data["equipment"]):
            self.assertNotIn("respawn_protection", {effect["op"] for effect in content.all_effects(row)})

    def test_display_names_are_the_approved_fog_harbor_names(self):
        # #111 雾港 v2 display names (CONTENT_AND_BALANCE 3.2/4/5/6.2/12.5); exposed (the resistance shred) reads 破防了 because display names are unique.
        self.assertEqual({row["id"]: row["name_text"] for row in self.data["display"]}, {
            "character_demo": "脆皮大学生",
            "active_arc_bolt": "天外飞瓶", "active_guard_pulse": "我没事我很好", "active_binding_snare": "扒裤子",
            "passive_battle_rhythm": "抢票练出来的手速", "passive_second_wind": "发疯文学", "passive_precision": "专攻下三路",
            "active_slipper_throw": "珍珠奶茶泼脸", "active_fart_dash": "放个屁就润", "active_combo_flurry": "无影大比兜",
            "active_slick_moves": "秦王绕柱", "passive_cornered": "他急了", "passive_lurking": "潜水党",
            "passive_public_shame": "已截图", "passive_eagle_eye": "网线够长", "item_slingshot": "大号滋水枪",
            "active_monkey_peach": "掏蛋子", "active_thousand_year": "年轻人不讲武德", "active_lunge": "一个滑铲",
            "passive_iron_crotch": "祖传铁裆", "item_nail_uniform": "扎心铆钉皮衣",
            "active_body_slam": "体重即正义", "active_come_here": "你瞅啥",
            "attr_might": "吃了口蛋白粉", "attr_focus": "5G 冲浪", "attr_vitality": "V 我 50 吃顿好的", "attr_resolve": "我不听我不听",
            "attr_reach": "伸手党", "attr_might_hp": "体育生特训", "attr_might_haste": "武德充沛", "attr_focus_as": "键盘钢琴家",
            "attr_focus_defense": "表面笑嘻嘻", "attr_reach_ms": "放风筝专家", "attr_roam_hybrid": "特种兵跑图",
            "attr_wild_glass_cannon": "彻底疯狂", "attr_wild_juggernaut": "半吨重坦", "attr_wild_brawler": "社交悍匪",
            "item_iron_plate": "防弹秋裤", "item_edge_blade": "折叠椅（摔跤同款）", "item_runic_focus": "键盘",
            "item_siphon_emblem": "大金链子", "item_phase_anchor": "《润学指南》", "item_arcane_battery": "充电宝",
            "item_echo_maul": "自拍杆", "item_paper_box": "路人甲 T 恤",
            "monster_scavenger": "已读不回的 HR", "monster_guardian": "追着办卡的私教",
            "root": "提裤子", "shield": "心理防线", "camp_returning": "回工位", "respawn_protection": "免死金牌",
            "slow": "腿软", "stat_boost": "上头", "enraged": "急了", "ambush_ready": "蹲到了", "exposed": "破防了",
            "taunt": "瞅你咋地", "stealth": "查无此人", "disarm": "捂裆", "stun": "CPU 干烧了", "empowered": "蓄力大比兜",
            "active_porcelain_flop": "碰瓷倒地", "active_expose_post": "挂人小作文",
            "active_barrage_rant": "键盘连喷", "active_cancel_blast": "实锤冲塔",
            "active_pie_chart": "拉群画饼", "active_table_flip": "掀桌不干了", "passive_dox_master": "开盒大师",
            "passive_thick_skin": "社交牛逼症", "passive_mental_gymnastics": "精神胜利法",
            "passive_keyboard_warrior": "阴阳大师", "item_megaphone": "大喇叭", "item_dossier": "黑料小本本",
            "item_privacy_screen": "防窥膜", "item_skateboard": "滑板鞋", "item_decoy_wig": "假发片",
            "item_energy_drink": "红牛", "knockback": "被掀飞", "heal_cut": "被开盒",
            "damage_guard": "碰瓷中", "vulnerable": "裤子没了",
        })
        for row in self.data["display"]:
            with self.subTest(identity=row["id"]):
                self.assertEqual(row["namespace"], "DemoContent")
                self.assertTrue(row["description_text"])
                self.assertIsNone(content.DISPLAY_DESCRIPTION_DIGIT.search(row["description_text"]))
                self.assertIsNone(content.DISPLAY_TEXT_FORBIDDEN.search(row["description_text"]))

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
        arc = self.catalog.definition("active_arc_bolt")
        self.assertEqual((amount(arc["target"]["range"]), amount(arc["target"]["projectile_radius"]),
                          amount(arc["target"]["projectile_speed"]), arc["windup"]["ms"]), (650, 45, 1600, 200))
        for row, damage, ap, mana, cooldown in zip(arc["ranks"], (100, 130, 160), (6500, 7500, 8500), (45, 50, 55), (6400, 5600, 4800)):
            self.assertEqual(row["effects"][0]["base"]["p2"], damage * 100)
            self.assertEqual(row["effects"][0]["ap_ratio"]["bp"], ap)
            self.assertEqual(row["effects"][0]["ad_ratio"]["bp"], 8000)
            # #81: the thrown bottle (active_arc_bolt) is Physical; its amounts follow the core-numbers ticket (D18).
            self.assertEqual(row["effects"][0]["damage_type"], "Physical")
            self.assertEqual(row["mana_cost"]["p2"], mana * 100)
            self.assertEqual(row["cooldown"]["ms"], cooldown)
        guard = self.catalog.definition("active_guard_pulse")
        self.assertEqual((guard["target"]["mode"], guard["windup"]["ms"]), ("Self", 0))
        for row, shield, mana, cooldown in zip(guard["ranks"], (90, 150, 220), (50, 55, 60), (16, 14, 12)):
            self.assertEqual(row["effects"][0]["base"]["p2"], shield * 100)
            self.assertEqual(row["effects"][0]["ap_ratio"]["bp"], 4000)
            self.assertEqual(row["effects"][0]["duration"]["ms"], 2500)
            self.assertEqual(row["effects"][1]["op"], "dispel_root")
            self.assertEqual(row["mana_cost"]["p2"], mana * 100)
            self.assertEqual(row["cooldown"]["ms"], cooldown * 1000)
        snare = self.catalog.definition("active_binding_snare")
        self.assertEqual((amount(snare["target"]["range"]), snare["windup"]["ms"]), (550, 200))
        for row, damage, root_ms, mana, cooldown in zip(snare["ranks"], (100, 120, 140), (1000, 1300, 1600), (60, 65, 70), (11200, 10400, 9600)):
            self.assertEqual(row["effects"][0]["base"]["p2"], damage * 100)
            self.assertEqual(row["effects"][0]["ad_ratio"]["bp"], 6000)
            self.assertEqual(row["effects"][0]["ap_ratio"]["bp"], 3000)
            self.assertEqual(row["effects"][0]["damage_type"], "Magical")
            self.assertEqual(row["effects"][1]["duration"]["ms"], root_ms)
            self.assertEqual(row["mana_cost"]["p2"], mana * 100)
            self.assertEqual(row["cooldown"]["ms"], cooldown)

    def test_build_expansion_a_entries(self):
        # #77/#81 entries approved by the user on 2026-11-04 as playtest input (CONTENT_AND_BALANCE 15.7).
        slipper = self.catalog.definition("active_slipper_throw")
        self.assertEqual((slipper["target"]["mode"], amount(slipper["target"]["range"]), slipper["windup"]["ms"]), ("Unit", 600, 200))
        for row, base, slow, cooldown in zip(slipper["ranks"], (95, 125, 155), (2500, 3000, 3500), (9600, 8800, 8000)):
            damage, slowed = row["effects"]
            self.assertEqual((damage["base"]["p2"], damage["ad_ratio"]["bp"], damage["ap_ratio"]["bp"], damage["damage_type"]),
                             (base * 100, 6400, 0, "Physical"))
            self.assertEqual((slowed["op"], slowed["ratio"]["bp"], slowed["duration"]["ms"]), ("slow", slow, 1500))
            self.assertEqual((row["mana_cost"]["p2"], row["cooldown"]["ms"]), (5000, cooldown))
        fart = self.catalog.definition("active_fart_dash")
        self.assertEqual((fart["target"]["mode"], fart["windup"]["ms"]), ("DirectionDash", 0))
        for row, base, cooldown in zip(fart["ranks"], (120, 145, 170), (12800, 11200, 9600)):
            dash, area, damage, slowed = row["effects"][:4]
            self.assertEqual((amount(dash["distance"]), dash["duration"]["ms"], amount(area["radius"])), (350, 200, 200))
            self.assertEqual((damage["base"]["p2"], damage["ad_ratio"]["bp"], damage["ap_ratio"]["bp"], damage["damage_type"]),
                             (base * 100, 3000, 3000, "Magical"))
            self.assertEqual((slowed["ratio"]["bp"], slowed["duration"]["ms"]), (3000, 1500))
            self.assertEqual((row["mana_cost"]["p2"], row["cooldown"]["ms"]), (5000, cooldown))
        # #112 §2 Lv3 gas: three pulses at 0.5/1.5/2.5 s that trigger on-hit effects at 0.5.
        self.assertEqual([len(row["effects"]) for row in fart["ranks"]], [4, 4, 7])
        zone, gas, on_hit = fart["ranks"][2]["effects"][4:]
        self.assertEqual((zone["op"], amount(zone["radius"]), zone["duration"]["ms"], zone["interval"]["ms"], zone["delay"]["ms"],
                          zone["count"]), ("zone", 200, 2500, 1000, 500, 3))
        self.assertEqual((gas["op"], gas["base"]["p2"], gas["ad_ratio"]["bp"], gas["ap_ratio"]["bp"], gas["damage_type"]),
                         ("zone_damage", 2500, 1000, 1000, "Magical"))
        self.assertEqual((on_hit["op"], on_hit["scale"]["bp"]), ("on_hit", 5000))
        for identity, field, values, duration, mana, cooldowns in (
            ("active_combo_flurry", "AttackSpeedBonusRatio", (3000, 4500, 6000), 4000, 5000, (16000, 14000, 12000)),
            ("active_slick_moves", "MoveSpeedBonusRatio", (3000, 4000, 5000), 2000, 4000, (14000, 13000, 12000)),
        ):
            for row, value, cooldown in zip(self.catalog.definition(identity)["ranks"], values, cooldowns):
                effect = row["effects"][0]
                self.assertEqual((effect["op"], effect["stats"][field]["bp"], effect["duration"]["ms"]),
                                 ("timed_stat_bonus", value, duration))
                self.assertEqual((row["mana_cost"]["p2"], row["cooldown"]["ms"]), (mana, cooldown))
        # #112: 他急了 12/18/24% AD with 10/15/20% omnivamp below half HP.
        cornered = [row["effects"][0] for row in self.catalog.definition("passive_cornered")["ranks"]]
        self.assertEqual([(e["threshold"]["bp"], e["stats"]["ADBonusRatio"]["bp"], e["omnivamp"]["bp"]) for e in cornered],
                         [(5000, 1200, 1000), (5000, 1800, 1500), (5000, 2400, 2000)])
        lurking = [row["effects"][0] for row in self.catalog.definition("passive_lurking")["ranks"]]
        self.assertEqual([(e["delay"]["ms"], e["base"]["p2"], e["ad_ratio"]["bp"], e["ap_ratio"]["bp"], e["damage_type"])
                          for e in lurking], [(4000, b * 100, 2000, 2000, "Magical") for b in (40, 70, 100)])
        shame = [row["effects"][0] for row in self.catalog.definition("passive_public_shame")["ranks"]]
        # #112: 已截图 8/13/18% against any controlled or slowed target.
        self.assertEqual([e["ratio"]["bp"] for e in shame], [800, 1300, 1800])
        eye = [row["effects"][0]["stats"]["AttackRangeCmFlat"] for row in self.catalog.definition("passive_eagle_eye")["ranks"]]
        self.assertEqual([amount(value) for value in eye], [80, 140, 200])
        sling = self.catalog.definition("item_slingshot")
        self.assertEqual((sling["buy_price"], sling["sell_gold"], sling["active"], sling["passive"]), (320, 208, None, None))
        self.assertEqual({key: amount(value) for key, value in sling["stats"].items()},
                         {"AttackRangeCmFlat": 120, "AttackSpeedBonusRatio": Fraction(".12")})
        # 15.3: max eagle eye plus one slingshot reaches 540 cm; two reach 660 cm.
        base = amount(self.catalog.parameter("UNIT.BasicAttackRangeCm"))
        self.assertEqual((base + 200 + 120, base + 200 + 240), (540, 660))

    def test_build_expansion_a_refusals(self):
        author = content.parse_json((content.ROOT / content.CONTENT_SOURCE).read_bytes(), "content author")
        schema = content.AuthorSchema(content.parse_json((content.ROOT / content.SCHEMA_SOURCE).read_bytes(), "content schema"))
        ids = content.parse_json((content.ROOT / content.IDS_SOURCE).read_bytes(), "native IDs")
        schema.validate(author)

        def ability(data, identity):
            return next(row for row in data["abilities"] if row["id"] == identity)

        def first(data, identity, op):
            return next(effect for effect in ability(data, identity)["ranks"][0]["effects"] if effect["op"] == op)

        for name, mutate, code in (
            ("area on a unit target", lambda d: [rank["effects"].append({"op": "self_area", "radius": {"unit": "cm", "numerator": 100, "denominator": 1}})
                                                 for rank in ability(d, "active_slipper_throw")["ranks"]], "TargetEffectMismatch"),
            ("dash hits without an area", lambda d: [rank["effects"].remove(next(e for e in rank["effects"] if e["op"] == "self_area"))
                                                     for rank in ability(d, "active_fart_dash")["ranks"]], "TargetEffectMismatch"),
            ("slow that stops the body", lambda d: first(d, "active_slipper_throw", "slow")["ratio"].update(bp=10000), "NumericDomain"),
            ("zero-radius area", lambda d: first(d, "active_fart_dash", "self_area")["radius"].update(numerator=0), "NumericDomain"),
            ("timed resource maximum", lambda d: first(d, "active_combo_flurry", "timed_stat_bonus")["stats"].update(
                MaxHPFlat={"unit": "hp", "p2": 100}), "UnknownField"),
            ("threshold at a full pool", lambda d: first(d, "passive_cornered", "low_hp_stat_bonus")["threshold"].update(bp=10000), "NumericDomain"),
            ("ineffective ambush", lambda d: [first(d, "passive_lurking", "ambush_damage")[field].update(**{key: 0})
                                              for field, key in (("base", "p2"), ("ad_ratio", "bp"), ("ap_ratio", "bp"))], "IneffectiveEffect"),
            ("zero control amplification", lambda d: first(d, "passive_public_shame", "control_damage_amp")["ratio"].update(bp=0), "NumericDomain"),
        ):
            projected = schema.project(author)
            mutate(projected)
            with self.subTest(case=name), self.assertRaisesRegex(content.ContentError, code):
                content.validate_definitions(projected, ids)

    def test_passives_attributes_and_build_bounds(self):
        for identity, field, values in (("passive_battle_rhythm", "AttackSpeedBonusRatio", [1200, 2000, 2800]),
                                        ("passive_precision", "CritChanceBonusRatio", [800, 1400, 2000])):
            self.assertEqual([row["effects"][0]["stats"][field]["bp"] for row in self.catalog.definition(identity)["ranks"]], values)
        # #112 Q7: 发疯文学 regenerates 0.8/1.2/1.6% of max HP per second out of combat.
        self.assertEqual([row["effects"][0]["max_hp_ratio"]["bp"] for row in self.catalog.definition("passive_second_wind")["ranks"]],
                         [80, 120, 160])
        self.assertTrue(all(row["effects"][0]["delay"]["ms"] == 6000 for row in self.catalog.definition("passive_second_wind")["ranks"]))
        # #112 §4: fourteen repeatable composite cards; the three wild cards name explicit deductions.
        ratio = Fraction
        expected = {
            "attr_might": ({"ADFlat": 7, "CritChanceBonusRatio": ratio(".05")}, None),
            "attr_might_hp": ({"ADFlat": 6, "MaxHPFlat": 50}, None),
            "attr_might_haste": ({"ADFlat": 6, "CooldownHasteFlat": 8}, None),
            "attr_focus": ({"APFlat": 15, "CooldownHasteFlat": 8}, None),
            "attr_focus_as": ({"APFlat": 14, "AttackSpeedBonusRatio": ratio(".08")}, None),
            "attr_focus_defense": ({"APFlat": 14, "ArmorFlat": 8, "MagicResistFlat": 8}, None),
            "attr_reach": ({"AttackRangeCmFlat": 35, "AttackSpeedBonusRatio": ratio(".06")}, None),
            "attr_reach_ms": ({"AttackRangeCmFlat": 30, "MoveSpeedBonusRatio": ratio(".045")}, None),
            "attr_roam_hybrid": ({"MoveSpeedBonusRatio": ratio(".055"), "ADFlat": 4, "APFlat": 8}, None),
            "attr_resolve": ({"MaxHPFlat": 40, "ArmorFlat": 9, "MagicResistFlat": 9}, None),
            "attr_vitality": ({"MaxHPFlat": 60, "MaxMPFlat": 60, "HPRegenFlat": ratio("1.5")}, None),
            "attr_wild_glass_cannon": ({"ADFlat": 11, "APFlat": 22, "AttackSpeedBonusRatio": ratio(".08")}, {"MaxHPFlat": 55}),
            "attr_wild_juggernaut": ({"MaxHPFlat": 110, "ArmorFlat": 14, "MagicResistFlat": 14}, {"MoveSpeedBonusRatio": ratio(".04")}),
            "attr_wild_brawler": ({"AttackSpeedBonusRatio": ratio(".14"), "MoveSpeedBonusRatio": ratio(".08")}, {"AttackRangeCmFlat": 30}),
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
            "item_paper_box": (400, 260, {"APFlat": 20, "MoveSpeedBonusRatio": Fraction(".08")}),
            "item_nail_uniform": (380, 247, {"MaxHPFlat": 150, "ArmorFlat": 18}),
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
        self.assertIsNone(spikes["active"])
        self.assertEqual(spikes["passive"]["family"], "passive_stride")
        boost = spikes["passive"]["effects"][0]
        self.assertEqual((boost["op"], boost["cooldown_ratio"]["bp"], boost["distance_bonus"]["bp"]), ("dash_boost", 5000, 5000))
        battery = self.catalog.definition("item_arcane_battery")["active"]["rank"]
        self.assertEqual((battery["mana_cost"]["p2"], battery["cooldown"]["ms"]), (0, 40000))
        self.assertEqual(battery["effects"][0]["ratio"]["bp"], 2500)
        self.assertEqual((battery["effects"][1]["base"]["p2"], battery["effects"][1]["ap_ratio"]["bp"],
                          battery["effects"][1]["duration"]["ms"]), (12000, 4000, 2500))
        self.assertEqual(self.catalog.definition("item_siphon_emblem")["passive"]["effects"][0]["ratio"]["bp"], 1500)
        cleave = self.catalog.definition("item_echo_maul")["passive"]["effects"][0]
        self.assertEqual((cleave["ratio"]["bp"], amount(cleave["radius"])), (3500, 180))
        # Approved on 2026-11-04: box mana cost 40; the other box terms follow CONTENT_AND_BALANCE 15.4.
        box = self.catalog.definition("item_paper_box")["active"]
        self.assertEqual((box["target"]["mode"], box["windup"]["ms"], box["cooldown_family"]), ("Self", 0, "cd_paper_box"))
        self.assertEqual((box["rank"]["mana_cost"]["p2"], box["rank"]["cooldown"]["ms"]), (4000, 45000))
        stealth = box["rank"]["effects"][0]
        self.assertEqual((stealth["op"], stealth["duration"]["ms"], amount(stealth["reveal_radius"]),
                          stealth["move_speed_penalty"]["bp"]), ("stealth", 3000, 250, 1000))

    def test_build_expansion_b_pending_proposal_values(self):
        # CONTENT_AND_BALANCE 15.8 values, approved on 2026-11-04 as playtest input.
        peach = self.catalog.definition("active_monkey_peach")
        self.assertEqual((peach["target"]["mode"], amount(peach["target"]["range"]), peach["windup"]["ms"]), ("Unit", 250, 200))
        for row, damage, disarm, mana, cooldown in zip(peach["ranks"], (90, 120, 150), (1000, 1300, 1600), (40, 45, 50), (9600, 8800, 8000)):
            self.assertEqual([effect["op"] for effect in row["effects"]][:3], ["damage", "disarm", "cooldown_refund"])
            self.assertEqual((row["effects"][0]["base"]["p2"], row["effects"][0]["ad_ratio"]["bp"], row["effects"][0]["damage_type"]),
                             (damage * 100, 7000, "Physical"))
            self.assertEqual((row["effects"][1]["duration"]["ms"], row["mana_cost"]["p2"], row["cooldown"]["ms"]), (disarm, mana * 100, cooldown))
        poke = self.catalog.definition("active_thousand_year")
        self.assertEqual((poke["target"]["mode"], amount(poke["target"]["range"])), ("Unit", 200))
        for row, damage, stun, mana, cooldown in zip(poke["ranks"], (95, 130, 165), (600, 700, 800), (50, 55, 60), (8000, 7200, 6400)):
            self.assertEqual((row["effects"][0]["base"]["p2"], row["effects"][0]["ad_ratio"]["bp"], row["effects"][0]["ap_ratio"]["bp"],
                              row["effects"][0]["damage_type"]), (damage * 100, 6000, 6000, "Magical"))
            self.assertEqual((row["effects"][1]["op"], row["effects"][1]["ratio"]["bp"], row["effects"][1]["duration"]["ms"]), ("backstab", 5000, stun))
            self.assertEqual((row["mana_cost"]["p2"], row["cooldown"]["ms"]), (mana * 100, cooldown))
        lunge = self.catalog.definition("active_lunge")
        self.assertEqual(lunge["target"]["mode"], "DirectionDash")
        for row, bonus, cooldown in zip(lunge["ranks"], (50, 75, 100), (11200, 9600, 8000)):
            dash, empower = row["effects"][:2]
            self.assertEqual((amount(dash["distance"]), dash["duration"]["ms"]), (400, 250))
            self.assertEqual((empower["op"], empower["base"]["p2"], empower["ad_ratio"]["bp"], empower["duration"]["ms"]),
                             ("empower_basic", bonus * 100, 4000, 3000))
            self.assertEqual((row["mana_cost"]["p2"], row["cooldown"]["ms"]), (4000, cooldown))
        for row, base in zip(self.catalog.definition("passive_iron_crotch")["ranks"], (15, 22, 30)):
            thorns = row["effects"][0]
            self.assertEqual((thorns["base"]["p2"], thorns["armor_ratio"]["bp"], amount(thorns["radius"]), thorns["interval"]["ms"],
                              thorns["critical_multiplier"]["bp"], thorns["trigger"], thorns["rear_exempt"], thorns["damage_type"]),
                             (base * 100, 1500, 300, 500, 20000, "BasicOrAbility", True, "Magical"))
        prickle = self.catalog.definition("item_nail_uniform")["passive"]
        self.assertEqual(prickle["family"], "passive_prickle")
        thorns = prickle["effects"][0]
        self.assertEqual((thorns["base"]["p2"], thorns["armor_ratio"]["bp"], thorns["critical_multiplier"]["bp"], thorns["trigger"],
                          thorns["rear_exempt"]), (2000, 1000, 10000, "Basic", False))
        self.assertEqual(amount(self.catalog.parameter("COMBAT.RearArcDeg")), 120)

    def test_cross_batch_entries_pending_proposal_values(self):
        # CONTENT_AND_BALANCE 15.9 values, approved on 2026-11-04: the jump/area/slow and taunt/timed-bonus assemblies.
        slam = self.catalog.definition("active_body_slam")
        self.assertEqual((slam["target"]["mode"], amount(slam["target"]["range"]), slam["windup"]["ms"], slam["cooldown_family"]),
                         ("GroundPoint", 400, 0, "cd_body_slam"))
        for row, base, mana, cooldown in zip(slam["ranks"], (75, 105, 135), (60, 65, 70), (12800, 12000, 11200)):
            self.assertEqual([effect["op"] for effect in row["effects"]][:4], ["jump", "self_area", "damage", "slow"])
            jump, area, damage, slowed = row["effects"][:4]
            self.assertEqual((jump["duration"]["ms"], amount(area["radius"])), (400, 200))
            # 2026-11-04 user decision: 75/105/135 + 0.4 AD + 6% of the caster's max HP.
            self.assertEqual((damage["base"]["p2"], damage["ad_ratio"]["bp"], damage["ap_ratio"]["bp"], damage["max_hp_ratio"]["bp"],
                              damage["damage_type"]), (base * 100, 4000, 0, 600, "Physical"))
            self.assertEqual((slowed["ratio"]["bp"], slowed["duration"]["ms"]), (3000, 1500))
            self.assertEqual((row["mana_cost"]["p2"], row["cooldown"]["ms"]), (mana * 100, cooldown))
        come = self.catalog.definition("active_come_here")
        self.assertEqual((come["target"]["mode"], come["windup"]["ms"], come["cooldown_family"]), ("Self", 0, "cd_come_here"))
        for row, taunt_ms, bonus, cooldown in zip(come["ranks"], (1000, 1250, 1500), (10, 15, 20), (18000, 16000, 14000)):
            taunt, boost = row["effects"][:2]
            self.assertEqual((taunt["op"], amount(taunt["radius"]), taunt["duration"]["ms"]), ("taunt", 300, taunt_ms))
            self.assertEqual((boost["op"], boost["duration"]["ms"]), ("timed_stat_bonus", 3000))
            self.assertEqual({key: amount(value) for key, value in boost["stats"].items()}, {"ArmorFlat": bonus, "MagicResistFlat": bonus})
            self.assertEqual((row["mana_cost"]["p2"], row["cooldown"]["ms"]), (6000, cooldown))

    def test_issue_112_existing_entry_rework_and_rank3_changes(self):
        # #112 §2/§3/§5 values approved on 2026-11-05 as playtest input; rank 3 adds its qualitative change only.
        def ops(identity, index):
            return [effect["op"] for effect in self.catalog.definition(identity)["ranks"][index]["effects"]]

        def effect(identity, index, op):
            return next(row for row in self.catalog.definition(identity)["ranks"][index]["effects"] if row["op"] == op)

        for identity, rank3 in (
            ("active_arc_bolt", {"resist_shred"}), ("active_guard_pulse", {"cooldown_refund"}),
            ("active_combo_flurry", {"cooldown_refund"}), ("active_slick_moves", {"dispel_slow", "momentum"}),
            ("active_monkey_peach", {"ability_crit"}), ("active_lunge", {"cooldown_refund", "path_slow"}),
            ("active_body_slam", {"triggered_shield"}), ("active_come_here", {"taunt_retaliate"}),
            ("passive_precision", {"ability_crit"}), ("passive_cornered", {"triggered_shield"}),
            ("passive_public_shame", {"mana_on_hit"}),
        ):
            with self.subTest(identity=identity):
                self.assertEqual(ops(identity, 0), ops(identity, 1))
                self.assertEqual(set(ops(identity, 2)) - set(ops(identity, 0)), rank3)
        arc = self.catalog.definition("active_arc_bolt")["target"]
        self.assertEqual((arc["attack_range_ratio"]["bp"], amount(arc["attack_range_bonus_cap"])), (5000, 300))
        shred = effect("active_arc_bolt", 2, "resist_shred")
        self.assertEqual((shred["armor_ratio"]["bp"], shred["magic_resist_ratio"]["bp"], shred["duration"]["ms"]), (2000, 0, 3000))
        shield = effect("active_guard_pulse", 2, "shield")
        self.assertEqual((shield["max_hp_ratio"]["bp"], shield["cleanse_multiplier"]["bp"]), (500, 20000))
        self.assertEqual(effect("active_guard_pulse", 2, "cooldown_refund")["ratio"]["bp"], 4000)
        self.assertEqual(effect("active_slipper_throw", 2, "slow")["vamp"]["bp"], 1500)
        flurry = effect("active_combo_flurry", 2, "cooldown_refund")
        self.assertEqual((flurry["trigger"], flurry["entries"], flurry["cooldown"]["ms"], flurry["duration"]["ms"]),
                         ("BasicHit", "OtherActives", 400, 4000))
        self.assertEqual(amount(effect("active_slick_moves", 0, "timed_stat_bonus")["stats"]["AttackRangeCmFlat"]), 60)
        momentum = effect("active_slick_moves", 2, "momentum")
        self.assertEqual((momentum["duration"]["ms"], momentum["window"]["ms"], momentum["base"]["p2"], momentum["cap"]["p2"]),
                         (3000, 3000, 500, 10000))
        self.assertEqual(effect("active_monkey_peach", 0, "cooldown_refund")["entries"], "Basic")
        self.assertTrue(effect("active_thousand_year", 2, "backstab")["impaired"])
        self.assertNotIn("impaired", effect("active_thousand_year", 1, "backstab"))
        self.assertEqual(effect("active_lunge", 2, "cooldown_refund")["cooldown"]["ms"], 3000)
        path = effect("active_lunge", 2, "path_slow")
        self.assertEqual((path["ratio"]["bp"], path["duration"]["ms"], amount(path["half_width"])), (3000, 1000, 80))
        landing = effect("active_body_slam", 2, "triggered_shield")
        self.assertEqual((landing["trigger"], landing["max_hp_ratio"]["bp"], landing["duration"]["ms"]), ("LandingHit", 500, 2500))
        answer = effect("active_come_here", 2, "taunt_retaliate")
        self.assertEqual((answer["base"]["p2"], answer["per_hit"]["p2"], answer["armor_ratio"]["bp"], answer["max_hits"],
                          answer["damage_type"]), (4000, 2500, 3000, 6, "Magical"))
        stacks = effect("passive_battle_rhythm", 2, "basic_stacks")
        self.assertEqual((stacks["stats"]["MoveSpeedBonusRatio"]["bp"], stacks["duration"]["ms"], stacks["max_stacks"],
                          stacks["windup_reduction"]["bp"]), (400, 1500, 3, 2000))
        wind = effect("passive_second_wind", 2, "out_of_combat_regen")
        self.assertEqual((wind["surge_duration"]["ms"], wind["surge_multiplier"]["bp"], wind["in_combat_ratio"]["bp"]), (3000, 20000, 5000))
        self.assertEqual(effect("passive_precision", 0, "cooldown_refund")["cooldown"]["ms"], 800)
        self.assertEqual(effect("passive_precision", 2, "ability_crit")["abilities"],
                         ("active_monkey_peach", "active_slipper_throw", "active_binding_snare", "active_thousand_year"))
        low = effect("passive_cornered", 2, "triggered_shield")
        self.assertEqual((low["base"]["p2"], low["threshold"]["bp"], low["interval"]["ms"], low["duration"]["ms"]), (15000, 4000, 20000, 2500))
        self.assertEqual([effect("passive_iron_crotch", index, "thorns")["max_hp_ratio"]["bp"] for index in range(3)], [150, 150, 150])
        reach = effect("passive_eagle_eye", 2, "long_reach")
        self.assertEqual((reach["ratio"]["bp"], reach["cap"]["bp"], amount(reach["cast_range"])), (250, 2000, 80))
        lurk = effect("passive_lurking", 2, "ambush_damage")
        self.assertEqual((lurk["dash_credit"]["ms"], lurk["shred_ratio"]["bp"], lurk["shred_duration"]["ms"]), (2000, 1500, 2500))
        self.assertEqual(effect("passive_public_shame", 2, "mana_on_hit")["mana"]["p2"], 1200)
        self.assertEqual(self.catalog.definition("item_iron_plate")["passive"]["effects"][0]["ratio"]["bp"], 1500)
        third = self.catalog.definition("item_edge_blade")["passive"]["effects"][0]
        self.assertEqual((third["count"], third["base"]["p2"], third["damage_type"]), (3, 2000, "Physical"))
        self.assertEqual(self.catalog.definition("item_runic_focus")["passive"]["effects"][0]["mana"]["p2"], 500)
        self.assertEqual(self.catalog.definition("item_nail_uniform")["passive"]["effects"][0]["max_hp_ratio"]["bp"], 100)
        stride = self.catalog.definition("item_phase_anchor")["passive"]["effects"][0]
        self.assertEqual((stride["skill_cooldown_ratio"]["bp"], stride["skill_distance_bonus"]["bp"]), (8000, 2500))
        self.assertTrue(self.catalog.definition("item_paper_box")["active"]["rank"]["effects"][0]["ambush_ready"])
        # The four starting items fit the 320 opening gold.
        self.assertTrue(all(self.catalog.definition(identity)["buy_price"] <= 320
                            for identity in ("item_iron_plate", "item_edge_blade", "item_slingshot", "item_runic_focus")))

    def test_issue_112_rank3_changes_refuse_on_lower_ranks(self):
        author = content.parse_json((content.ROOT / content.CONTENT_SOURCE).read_bytes(), "content author")
        schema = content.AuthorSchema(content.parse_json((content.ROOT / content.SCHEMA_SOURCE).read_bytes(), "content schema"))
        ids = content.parse_json((content.ROOT / content.IDS_SOURCE).read_bytes(), "native IDs")
        for identity, op in (("active_monkey_peach", "ability_crit"), ("active_lunge", "path_slow"), ("passive_cornered", "triggered_shield")):
            projected = schema.project(author)
            row = next(item for item in projected["abilities"] if item["id"] == identity)
            row["ranks"][0]["effects"].append(next(e for e in row["ranks"][2]["effects"] if e["op"] == op))
            with self.subTest(identity=identity), self.assertRaises(content.ContentError):
                content.validate_definitions(projected, ids)
            projected = schema.project(author)
            row = next(item for item in projected["abilities"] if item["id"] == identity)
            row["ranks"][2]["effects"] = [e for e in row["ranks"][2]["effects"] if e["op"] != op]
            with self.subTest(identity=identity, missing=True), self.assertRaises(content.ContentError):
                content.validate_definitions(projected, ids)

    def test_only_the_body_slam_scales_with_the_casters_max_hp(self):
        scaled = {row["id"] for row in self.data["abilities"] for rank in row["ranks"] for effect in rank["effects"]
                  if effect["op"] == "damage" and effect["max_hp_ratio"]["bp"] != 0}
        self.assertEqual(scaled, {"active_body_slam"})

    def test_cross_batch_entries_refuse_a_jump_without_its_area(self):
        author = content.parse_json((content.ROOT / content.CONTENT_SOURCE).read_bytes(), "content author")
        schema = content.AuthorSchema(content.parse_json((content.ROOT / content.SCHEMA_SOURCE).read_bytes(), "content schema"))
        ids = content.parse_json((content.ROOT / content.IDS_SOURCE).read_bytes(), "native IDs")
        projected = schema.project(author)
        slam = next(row for row in projected["abilities"] if row["id"] == "active_body_slam")
        for rank in slam["ranks"]:
            rank["effects"] = [effect for effect in rank["effects"] if effect["op"] != "self_area"]
        with self.assertRaisesRegex(content.ContentError, "TargetEffectMismatch"):
            content.validate_definitions(projected, ids)

    def test_build_expansion_b_contracts_reject_misplaced_mechanics(self):
        author = content.parse_json((content.ROOT / content.CONTENT_SOURCE).read_bytes(), "content author")
        schema = content.AuthorSchema(content.parse_json((content.ROOT / content.SCHEMA_SOURCE).read_bytes(), "content schema"))
        ids = content.parse_json((content.ROOT / content.IDS_SOURCE).read_bytes(), "native IDs")
        for identity, change in (
            ("active_thousand_year", lambda effects: effects.pop(0)),
            ("active_monkey_peach", lambda effects: effects.append({"op": "stun", "duration": {"unit": "ms", "ms": 500}})),
            ("active_lunge", lambda effects: effects.pop(0)),
        ):
            projected = schema.project(author)
            ability = next(row for row in projected["abilities"] if row["id"] == identity)
            for rank in ability["ranks"]:
                change(rank["effects"])
            with self.subTest(identity=identity), self.assertRaises(content.ContentError):
                content.validate_definitions(projected, ids)
        projected = schema.project(author)
        crotch = next(row for row in projected["abilities"] if row["id"] == "passive_iron_crotch")
        crotch["ranks"][0]["effects"][0]["critical_multiplier"] = {"unit": "ratio", "bp": 5000}
        with self.assertRaisesRegex(content.ContentError, "NumericDomain"):
            content.validate_definitions(projected, ids)

    def test_monster_and_progression_values(self):
        self.assertEqual(self.data["progression"]["xp_thresholds"], (0, 80, 180, 300, 440, 600, 780, 980, 1200, 1440))
        self.assertEqual(self.data["progression"]["xp_to_next"], (80, 100, 120, 140, 160, 180, 200, 220, 240))
        # 112 §6: the opening offer is two new actives and one new passive; level 4 and 7 each earn one reroll.
        self.assertEqual(self.data["progression"]["opening_offer"], {"active_count": 2, "passive_count": 1})
        self.assertEqual(self.data["progression"]["reroll_levels"], (4, 7))
        for identity, tier, count, hp, resist, attack, rate, windup, reach, speed, leash, grace, respawn, xp, gold, interval in (
            ("monster_scavenger", "Low", 2, 90, 8, 10, ".80", 350, 150, 320, 900, 3000, 25000, 40, 30, 1250),
            ("monster_guardian", "High", 1, 420, 20, 48, ".65", 500, 175, 347, 1100, 4000, 65000, 160, 120, 1539),
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

    def test_exact_world_surface_anchors_and_member_homes(self):
        expected = {
            "s00": (4400, 0), "s01": (3560, 2586), "s02": (1360, 4185), "s03": (-1360, 4185), "s04": (-3560, 2586),
            "s05": (-4400, 0), "s06": (-3560, -2586), "s07": (-1360, -4185), "s08": (1360, -4185), "s09": (3560, -2586),
            "l1": (2473, 803), "h1": (0, 2600), "l2": (-2473, 803), "h2": (-1528, -2103), "l3": (1528, -2103),
            "shop": (778, 778), "capture": (0, 0),
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
            "CAMP.ResetArrivalRadiusCm", "CAPTURE.RadiusCm", "SHOP.InteractRadiusCm", "TIMING.DefendRouteBudgetSec",
            "UNIT.BasicAttackRangeCm", "UNIT.CollisionRadiusCm", "UNIT.MoveSpeedCmPerSec", "VISION.RadiusCm"))

    def test_published_route_lengths_and_whole_ground_budget(self):
        captures = (4175.255, 4175.436, 4175.833, 4175.833, 4175.436, 4175.255, 4175.554, 4175.820, 4175.820, 4175.554)
        shops = (3354.614, 3183.627, 3479.117, 3672.275, 4718.133, 4886.361, 5224.744, 5202.690, 4647.008, 4386.503)
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
            ("l1", 2300.103, 1345.505), ("h1", 2300, 1640.297), ("l2", 2300.103, 2901.504),
            ("h2", 2299.499, 3340.230), ("l3", 2299.499, 2631.535),
        ):
            self.assertAlmostEqual(self.world.report["routes"][identity]["capture_cm"], capture, delta=.00051)
            self.assertAlmostEqual(self.world.report["routes"][identity]["shop_cm"], shop, delta=.00051)
        self.assertAlmostEqual(self.world.report["route_upper_bound_cm"], 9524.661, delta=.00051)
        speed = amount(self.data["parameters"]["UNIT.MoveSpeedCmPerSec"])
        budget = amount(self.data["parameters"]["TIMING.DefendRouteBudgetSec"])
        self.assertEqual((speed, budget), (400, 26))
        self.assertLess(self.world.report["route_upper_bound_cm"], speed * budget)
        self.assertGreater(self.world.report["route_upper_bound_cm"], speed * 23)
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
