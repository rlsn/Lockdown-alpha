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
        self.assertEqual(approval, self.world.world["approval"])
        for text in ("2026-11-02", "D01", "D02", "D03", "playtest", "not a balance"):
            self.assertIn(text, approval["decision"])
        self.assertNotIn("TEST ONLY", approval["decision"])
        self.assertEqual((len(self.data["abilities"]), len(self.data["attributes"]), len(self.data["equipment"]),
                          len(self.data["camps"]["monsters"])), (6, 5, 7, 2))
        self.assertEqual([row["id"] for row in self.data["abilities"]], [
            "active_arc_bolt", "active_binding_snare", "active_guard_pulse",
            "passive_battle_rhythm", "passive_precision", "passive_second_wind"])
        self.assertEqual(self.data["identity"]["projection_revision"], 3)
        self.assertEqual(self.data["character"]["basic_attack"]["interval_ms"], 1429)
        self.assertEqual(self.data["character"]["basic_attack"]["damage_type"], "Physical")
        self.assertEqual(self.data["character"]["basic_attack"]["ad_ratio"]["bp"], 10000)

    def test_every_approved_parameter_value_and_unit(self):
        expected = {}
        for stat, unit, values in (
            ("MaxHP", "hp", ("620", "58", "1", "3000")),
            ("MaxMP", "mp", ("280", "22", "0", "800")),
            ("AD", "points", ("54", "3.2", "0", "250")),
            ("AP", "points", ("0", "0", "0", "250")),
            ("Armor", "points", ("18", "2", "0", "200")),
            ("MagicResist", "points", ("18", "2", "0", "200")),
            ("HPRegen", "hp/s", ("1.60", "0.12", "0", "12")),
            ("MPRegen", "mp/s", ("1.20", "0.08", "0", "4")),
        ):
            for suffix, value in zip(("Base", "PerLevel", "Min", "Max"), values):
                expected[f"STAT.{stat}.{suffix}"] = (unit, value)
        for key, unit, value in (
            ("STAT.BaseAttackSpeed", "attacks/s", ".70"), ("STAT.AttackSpeedPerLevelRatio", "ratio", ".02"),
            ("STAT.AttackSpeed.Min", "attacks/s", ".30"), ("STAT.AttackSpeed.Max", "attacks/s", "2.50"),
            ("STAT.CritChance.Base", "ratio", "0"), ("STAT.CritMultiplier", "ratio", "2"),
            ("STAT.CooldownHaste.Base", "points", "0"), ("STAT.CooldownHaste.Max", "points", "60"),
            ("UNIT.MoveSpeedCmPerSec", "cm/s", "600"), ("STAT.MoveSpeed.Min", "cm/s", "300"), ("STAT.MoveSpeed.Max", "cm/s", "900"),
            ("UNIT.CollisionRadiusCm", "cm", "40"), ("UNIT.BasicAttackRangeCm", "cm", "220"),
            ("STAT.AttackRange.Min", "cm", "80"), ("STAT.AttackRange.Max", "cm", "1000"),
            ("UNIT.BasicAttackWindupSec", "ms", ".250"), ("VISION.RadiusCm", "cm", "1400"),
            ("SHOP.InteractRadiusCm", "cm", "350"), ("CAPTURE.RadiusCm", "cm", "600"),
            ("CAPTURE.HoldSec", "ms", "25"), ("TIMING.DefendRouteBudgetSec", "ms", "16"),
            ("MATCH.DevelopmentSec", "ms", "180"), ("LIFE.RespawnSec", "ms", "26"), ("LIFE.ProtectionSec", "ms", "3"),
            ("REGEN.OutOfCombatDelaySec", "ms", "5"), ("REGEN.OutOfCombatMaxHPRatioPerSec", "ratio", ".015"),
            ("REGEN.OutOfCombatMaxMPRatioPerSec", "ratio", ".015"),
            ("SHOP.SellRatio", "ratio", ".65"), ("CAMP.ResetArrivalRadiusCm", "cm", "100"),
            ("AI.RetreatHPRatio", "ratio", ".25"), ("AI.ResumeFarmHPRatio", "ratio", ".60"),
            ("AI.HighCampEntryHPRatio", "ratio", ".80"), ("AI.HighCampReserveHPRatio", "ratio", ".35"),
            ("AI.LowCampReserveHPRatio", "ratio", ".15"), ("AI.CaptureReactionBudgetSec", "ms", "2"),
            ("AI.DecisionIntervalSec", "ms", ".25"), ("AI.RetryPolicy.InitialBackoffSec", "ms", ".25"),
            ("AI.RetryPolicy.MaxBackoffSec", "ms", ".50"), ("AI.ExploreRevisitIntervalSec", "ms", "20"),
            ("AI.EngagementBudgetSec", "ms", "40"),
        ):
            expected[key] = (unit, value)
        for key, value in (
            ("SHOP.StartingGold", 150), ("CHOICE.BaseWeight", 1), ("XP.BaseToNext", 80), ("XP.IncreasePerLevel", 20),
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
        self.assertEqual(ids["statuses"], {"root": 1, "shield": 2, "camp_returning": 3, "respawn_protection": 4})
        self.assertEqual(ids["effect_ops"], {
            "damage": 1, "shield": 2, "dispel_root": 3, "root": 4, "stat_bonus": 5,
            "out_of_combat_regen": 6, "lifesteal": 7, "dash": 8, "restore_mana": 9, "cleave": 10})
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

    def test_display_names_are_the_approved_campus_names(self):
        # CONTENT_AND_BALANCE 3.2/4/5/6.2/12.5 approved display names; respawn_protection is the approved interim name.
        self.assertEqual({row["id"]: row["name_text"] for row in self.data["display"]}, {
            "character_demo": "大一新生",
            "active_arc_bolt": "粉笔头", "active_guard_pulse": "假装无事发生", "active_binding_snare": "扒裤衩",
            "passive_battle_rhythm": "单身二十年的手速", "passive_second_wind": "多喝热水", "passive_precision": "正中要害",
            "attr_might": "举铁", "attr_focus": "刷整蛊视频", "attr_vitality": "食堂加餐", "attr_resolve": "穿秋裤",
            "attr_reach": "长臂猿",
            "item_iron_plate": "《五年挨打三年模拟》", "item_edge_blade": "鸡毛掸子", "item_runic_focus": "辣条",
            "item_siphon_emblem": "老爸的皮带", "item_phase_anchor": "跑道钉鞋", "item_arcane_battery": "保温杯",
            "item_echo_maul": "值日扫帚",
            "monster_scavenger": "纪律委员", "monster_guardian": "体育老师",
            "root": "定身", "shield": "淡定", "camp_returning": "回岗中", "respawn_protection": "复活保护",
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
        for row, damage, ap, mana, cooldown in zip(arc["ranks"], (70, 100, 140), (6500, 7500, 8500), (45, 50, 55), (8, 7, 6)):
            self.assertEqual(row["effects"][0]["base"]["p2"], damage * 100)
            self.assertEqual(row["effects"][0]["ap_ratio"]["bp"], ap)
            self.assertEqual(row["effects"][0]["ad_ratio"]["bp"], 0)
            self.assertEqual(row["effects"][0]["damage_type"], "Magical")
            self.assertEqual(row["mana_cost"]["p2"], mana * 100)
            self.assertEqual(row["cooldown"]["ms"], cooldown * 1000)
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
        for row, damage, root_ms, mana, cooldown in zip(snare["ranks"], (20, 40, 60), (1000, 1300, 1600), (60, 65, 70), (14, 13, 12)):
            self.assertEqual(row["effects"][0]["base"]["p2"], damage * 100)
            self.assertEqual(row["effects"][0]["ap_ratio"]["bp"], 3000)
            self.assertEqual(row["effects"][1]["duration"]["ms"], root_ms)
            self.assertEqual(row["mana_cost"]["p2"], mana * 100)
            self.assertEqual(row["cooldown"]["ms"], cooldown * 1000)

    def test_passives_attributes_and_build_bounds(self):
        for identity, field in (("passive_battle_rhythm", "AttackSpeedBonusRatio"), ("passive_precision", "CritChanceBonusRatio")):
            self.assertEqual([row["effects"][0]["stats"][field]["bp"] for row in self.catalog.definition(identity)["ranks"]],
                             [800, 1400, 2000])
        self.assertEqual([amount(row["effects"][0]["rate"]) for row in self.catalog.definition("passive_second_wind")["ranks"]],
                         [3, 5, 7])
        self.assertTrue(all(row["effects"][0]["delay"]["ms"] == 6000 for row in self.catalog.definition("passive_second_wind")["ranks"]))
        expected = {
            "attr_might": {"ADFlat": 8}, "attr_focus": {"APFlat": 16}, "attr_vitality": {"MaxHPFlat": 100},
            "attr_resolve": {"ArmorFlat": 6, "MagicResistFlat": 6}, "attr_reach": {"AttackRangeCmFlat": 40},
        }
        for identity, stats in expected.items():
            self.assertEqual({key: amount(value) for key, value in self.catalog.definition(identity)["stats"].items()}, stats)
        # Ten choices (the opening one and nine level-ups) toward one coordinate plus its best two items.
        for stat, maximum in (("MaxHP", "2502"), ("MaxMP", "718"), ("AD", "202.8"), ("AP", "220"),
                              ("Armor", "126"), ("MagicResist", "120")):
            field = stat + "Flat"
            base = amount(self.catalog.parameter("STAT." + stat + ".Base")) + 9 * amount(self.catalog.parameter("STAT." + stat + ".PerLevel"))
            equipment = max(amount(row["stats"][field]) if field in row["stats"] else Fraction(0) for row in self.data["equipment"])
            self.assertEqual(base + content._choice_bonus(self.data, field) + 2 * equipment, Fraction(maximum))
        self.assertEqual(Fraction(7, 10) * (1 + Fraction(18, 100) + Fraction(20, 100) + Fraction(20, 100)), Fraction("1.106"))

    def test_every_equipment_price_stat_and_effect(self):
        expected = {
            "item_iron_plate": (310, 201, {"MaxHPFlat": 180, "ArmorFlat": 15}),
            "item_edge_blade": (320, 208, {"ADFlat": 20, "AttackSpeedBonusRatio": Fraction(".10")}),
            "item_runic_focus": (360, 234, {"APFlat": 30, "CooldownHasteFlat": 15}),
            "item_siphon_emblem": (360, 234, {"ADFlat": 12, "MagicResistFlat": 12}),
            "item_phase_anchor": (400, 260, {"MoveSpeedBonusRatio": Fraction(".12")}),
            "item_arcane_battery": (400, 260, {"MaxMPFlat": 120, "MagicResistFlat": 10}),
            "item_echo_maul": (400, 260, {"ADFlat": 14, "AttackRangeCmFlat": 40}),
        }
        for identity, (price, sell, stats) in expected.items():
            item = self.catalog.definition(identity)
            self.assertEqual((item["buy_price"], item["sell_gold"]), (price, sell))
            self.assertEqual({key: amount(value) for key, value in item["stats"].items()}, stats)
        dash = self.catalog.definition("item_phase_anchor")["active"]["rank"]
        self.assertEqual((dash["mana_cost"]["p2"], dash["cooldown"]["ms"]), (4000, 28000))
        self.assertEqual((amount(dash["effects"][0]["distance"]), dash["effects"][0]["duration"]["ms"]), (450, 250))
        battery = self.catalog.definition("item_arcane_battery")["active"]["rank"]
        self.assertEqual((battery["mana_cost"]["p2"], battery["cooldown"]["ms"]), (0, 40000))
        self.assertEqual(battery["effects"][0]["ratio"]["bp"], 2500)
        self.assertEqual((battery["effects"][1]["base"]["p2"], battery["effects"][1]["ap_ratio"]["bp"],
                          battery["effects"][1]["duration"]["ms"]), (12000, 4000, 2500))
        self.assertEqual(self.catalog.definition("item_siphon_emblem")["passive"]["effects"][0]["ratio"]["bp"], 1500)
        cleave = self.catalog.definition("item_echo_maul")["passive"]["effects"][0]
        self.assertEqual((cleave["ratio"]["bp"], amount(cleave["radius"])), (3500, 180))

    def test_monster_and_progression_values(self):
        self.assertEqual(self.data["progression"]["xp_thresholds"], (0, 80, 180, 300, 440, 600, 780, 980, 1200, 1440))
        self.assertEqual(self.data["progression"]["xp_to_next"], (80, 100, 120, 140, 160, 180, 200, 220, 240))
        # D24: the opening offer is two of four basic attribute cards (not attr_focus) plus one passive.
        self.assertEqual(self.data["progression"]["opening_offer"],
                         {"attribute_ids": ("attr_might", "attr_reach", "attr_resolve", "attr_vitality"),
                          "attribute_count": 2, "passive_count": 1})
        for identity, tier, count, hp, resist, attack, rate, windup, reach, speed, leash, grace, respawn, xp, gold, interval in (
            ("monster_scavenger", "Low", 2, 90, 8, 10, ".80", 350, 150, 480, 900, 3000, 40000, 40, 30, 1250),
            ("monster_guardian", "High", 1, 420, 20, 48, ".65", 500, 175, 520, 1100, 4000, 65000, 160, 120, 1539),
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
        captures = (3875.255, 3875.436, 3875.833, 3875.833, 3875.436, 3875.255, 3875.554, 3875.820, 3875.820, 3875.554)
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
            ("l1", 2000.103, 1345.505), ("h1", 2000, 1640.297), ("l2", 2000.103, 2901.504),
            ("h2", 1999.499, 3340.230), ("l3", 1999.499, 2631.535),
        ):
            self.assertAlmostEqual(self.world.report["routes"][identity]["capture_cm"], capture, delta=.00051)
            self.assertAlmostEqual(self.world.report["routes"][identity]["shop_cm"], shop, delta=.00051)
        self.assertAlmostEqual(self.world.report["route_upper_bound_cm"], 9224.661, delta=.00051)
        self.assertLess(self.world.report["route_upper_bound_cm"], 9600)
        self.assertAlmostEqual(self.world.report["min_inner_angular_gap_degrees"], 11.662, delta=.00051)
        candidates = [row["capture_cm"] for key, row in self.world.report["routes"].items() if ":" in key]
        self.assertEqual(len(candidates), 70)
        self.assertAlmostEqual(min(candidates), 3775.611, delta=.00051)
        self.assertAlmostEqual(max(candidates), 3976.064, delta=.00051)

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
