"""Behavioral refusal/projection/integrity tests; all content is synthetic and test-only."""
from contextlib import redirect_stderr, redirect_stdout
from copy import deepcopy
import base64
import ctypes
from decimal import Decimal, localcontext
from fractions import Fraction
import io
import json
import math
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from CatalogTools import encode
import DemoContent as demo
from DemoContentTestFixtures import write_fixture, write_world, world_fixture
import SyncDemoContentData as sync
import SyncDemoWorldData as world_sync


def leaves(value, path="content"):
    if type(value) is dict:
        return {key: child for name, item in value.items()
                for key, child in leaves(item, f"{path}.{name}").items()}
    if type(value) is list:
        return {key: child for index, item in enumerate(value)
                for key, child in leaves(item, f"{path}[{index}]").items()}
    return {path: value}


class ExactNumbers(unittest.TestCase):
    def test_reader_preserves_decimal_lexeme_before_float(self):
        value = demo.parse_json(b'{"x":0.0100000000000000001}', "fixture")
        self.assertIs(type(value["x"]), Decimal)
        self.assertEqual(value["x"], Decimal("0.0100000000000000001"))
        with self.assertRaisesRegex(demo.ContentError, "PrecisionLoss"):
            demo.fixed(value["x"], 2, "x")

    def test_exact_fixed_points_and_bounds(self):
        for raw, places, expected in (
            ("0.01", 2, 1), ("0.0100000000000000000", 2, 1), ("1e-2", 2, 1),
            ("0.0001", 4, 1), ("0.001", 3, 1), ("-0.00", 2, 0),
            ("92233720368547758.07", 2, demo.INT64_MAX),
        ):
            with self.subTest(raw=raw):
                self.assertEqual(demo.fixed(Decimal(raw), places, "fixture"), expected)
        for raw, places in (("0.001", 2), ("0.00001", 4), ("0.0001", 3),
                            ("92233720368547758.08", 2), ("1e309", 2), ("1e-1000000", 2)):
            with self.subTest(raw=raw), self.assertRaises(demo.ContentError):
                demo.fixed(Decimal(raw), places, "fixture")

    def test_exact_bounded_rational_instead_of_float(self):
        self.assertEqual(demo.rational(Decimal("0.125"), "rate"), Fraction(1, 8))
        self.assertEqual(demo.rational(Decimal("0.0000000000000000005"), "rate"), Fraction(1, 2 * 10 ** 18))
        for raw in ("1e309", "1e-309", "0.0100000000000000001"):
            with self.subTest(raw=raw), self.assertRaises(demo.ContentError):
                demo.rational(Decimal(raw), "rate")

    def test_binary_float_and_boolean_are_not_author_numbers(self):
        for value in (True, False, 0.01, float("inf"), float("nan")):
            with self.subTest(value=value), self.assertRaises(demo.ContentError):
                demo.fixed(value, 2, "value")
        with self.assertRaises(demo.ContentError):
            demo.integer(True, "count")

    def test_duplicate_json_keys_at_every_depth(self):
        for raw in (b'{"x":1,"x":2}', b'{"outer":{"x":1,"x":2}}', b'[{"x":1,"x":2}]'):
            with self.subTest(raw=raw), self.assertRaisesRegex(demo.ContentError, "DuplicateKey"):
                demo.parse_json(raw, "fixture")

    def test_nonfinite_and_bad_json_never_parse_as_success(self):
        for raw in (b'{"x":NaN}', b'{"x":Infinity}', b'{"x":-Infinity}', b'{', b'"\xff"'):
            with self.subTest(raw=raw), self.assertRaises(demo.ContentError):
                demo.parse_json(raw, "fixture")
        parsed = demo.parse_json(b'{"x":1e309}', "fixture")
        self.assertIs(type(parsed["x"]), Decimal)
        self.assertTrue(parsed["x"].is_finite())
        with self.assertRaisesRegex(demo.ContentError, "NumericOverflow"):
            demo.fixed(parsed["x"], 2, "fixture")

    def test_wire_identity_and_digest_shapes(self):
        for identity in ("active_snark_bolt", "x" * 63):
            wire = demo.definition_to_wire(identity)
            self.assertEqual(len(wire), 64)
            self.assertEqual(demo.definition_from_wire(wire), identity)
        for identity in ("ArcBolt", "x" * 64, "a\0b", "", "arc-bolt", "\u4e2d", "id\n"):
            with self.subTest(identity=identity), self.assertRaises(demo.ContentError):
                demo.definition_to_wire(identity)
        for raw in (b"x" * 64, b"x\0y" + b"\0" * 61, b"\xff\0" + b"\0" * 62, b"x\0", b"\0" * 64):
            with self.subTest(raw=raw), self.assertRaises(demo.ContentError):
                demo.definition_from_wire(raw)
        digest = bytes(range(32))
        self.assertEqual(demo.digest_to_wire(demo.digest_from_wire(digest)), digest)
        for bad in (digest.hex().upper(), "a" * 63, "g" * 64, digest):
            with self.subTest(bad=bad), self.assertRaises(demo.ContentError):
                demo.digest_to_wire(bad)
        for bad in (b"a" * 31, b"a" * 33, "a" * 32, [0] * 32):
            with self.subTest(bad=bad), self.assertRaises(demo.ContentError):
                demo.digest_from_wire(bad)


class DemoContentImport(unittest.TestCase):
    def setUp(self):
        scratch = demo.ROOT / "Artifacts" / "DemoContent"
        scratch.mkdir(parents=True, exist_ok=True)
        directory = tempfile.TemporaryDirectory(prefix="test-", dir=scratch)
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.author = write_fixture(self.root)
        self.baseline = demo.compile_content(self.root)
        sync.write_outputs(self.root, self.baseline)

    def edit(self, change, relative=demo.CONTENT_SOURCE):
        path = self.root / relative
        value = json.loads(path.read_bytes())
        change(value)
        path.write_bytes(encode(value))

    def assert_outputs_equal(self, expected=None):
        expected = self.baseline.outputs if expected is None else expected
        for relative, raw in expected.items():
            self.assertEqual((self.root / relative).read_bytes(), raw, str(relative))

    def assert_refused(self, change, pattern=".", relative=demo.CONTENT_SOURCE):
        self.edit(change, relative)
        with self.assertRaisesRegex(ValueError, pattern):
            sync.synchronize(self.root)
        self.assert_outputs_equal()
        self.assertFalse((self.root / demo.DATA / ".compile.lock").exists())
        self.assertFalse(list((self.root / "Artifacts").rglob("transaction-*")))

    def set_raw_number(self, key, token):
        self.edit(lambda value: value["parameters"][key].update(value="RAW_NUMERIC_TOKEN"))
        path = self.root / demo.CONTENT_SOURCE
        path.write_bytes(path.read_bytes().replace(b'"RAW_NUMERIC_TOKEN"', token.encode("ascii")))

    def test_complete_projection_is_exact_integer_tree_and_immutable(self):
        result = self.baseline
        self.assertEqual(set(result.outputs), set(demo.OUTPUT_PATHS))
        self.assertEqual(len(result.digest), 32)
        self.assertEqual(result.catalog.parameter("STAT.MaxHP.Base"), {"unit": "hp", "p2": 10000})
        self.assertEqual(result.catalog.parameter("STAT.HPRegen.Base"), {"unit": "hp/s", "numerator": 5, "denominator": 4})
        self.assertEqual(result.catalog.parameter("STAT.AttackSpeedPerLevelRatio"), {"unit": "ratio", "bp": 300})
        self.assertEqual(result.catalog.parameter("UNIT.BasicAttackWindupSec"), {"unit": "ms", "ms": 125})
        self.assertEqual(result.catalog.data["progression"]["xp_to_next"],
                         (50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190))
        self.assertEqual(result.catalog.definition("monster_guardian")["attack_interval_ms"], 1334)
        self.assertEqual(result.catalog.definition("character_demo")["basic_attack"]["interval_ms"], 1000)
        self.assertEqual(result.catalog.definition("active_snark_bolt")["native_id"], 94)
        projected = demo.parse_json(result.outputs[demo.CONTENT_OUTPUT], "output")
        demo.validate_integer_tree(projected, "output")
        with self.assertRaises(TypeError):
            result.catalog.data["character"]["id"] = "mutated"
        with self.assertRaises(TypeError):
            result.catalog.definition("active_snark_bolt")["effects"][0]["base"]["p2"] = 1
        with self.assertRaisesRegex(demo.ContentError, "UnknownId"):
            result.catalog.definition("snark_bolt")
        with self.assertRaisesRegex(demo.ContentError, "UnknownParameter"):
            result.catalog.parameter("CAMP.Low.AggroRadiusCm")

    def assert_leaf_projection(self, result, author):
        source_leaves = leaves(author)
        target_leaves = leaves(json.loads(result.outputs[demo.CONTENT_OUTPUT]))
        fields = result.catalog.field_projection
        self.assertEqual(set(fields), set(source_leaves))
        for source, destinations in fields.items():
            with self.subTest(source=source):
                self.assertTrue(destinations)
                self.assertTrue(all(destination in target_leaves for destination in destinations))
                if source.endswith(".value"):
                    exact = Fraction(Decimal(str(source_leaves[source])))
                    if len(destinations) == 2:
                        self.assertEqual(target_leaves[destinations[0]], exact.numerator)
                        self.assertEqual(target_leaves[destinations[1]], exact.denominator)
                    else:
                        storage = destinations[0].rsplit(".", 1)[-1]
                        scaled = exact * {"p2": 100, "bp": 10000, "ms": 1000}[storage]
                        self.assertEqual(scaled.denominator, 1)
                        self.assertEqual(target_leaves[destinations[0]], scaled.numerator)
                elif source.endswith(".unit"):
                    self.assertEqual("ms" if source_leaves[source] == "s" else source_leaves[source],
                                     target_leaves[destinations[0]])
                else:
                    self.assertEqual(source_leaves[source], target_leaves[destinations[0]])

    def test_every_author_leaf_has_exact_destination_after_row_sorting(self):
        self.assert_leaf_projection(self.baseline, self.author)
        fields = self.baseline.catalog.field_projection
        self.assertEqual(fields["content.abilities[0].effects[0].base.value"],
                         ("content.abilities[2].effects[0].base.p2",))
        self.assertEqual(fields["content.stats[0].per_point.MaxHPFlat.value"],
                         ("content.stats[2].per_point.MaxHPFlat.p2",))

    def test_single_value_set_projects_all_effects_costs_and_coefficients(self):
        bolt = self.baseline.catalog.definition("active_snark_bolt")
        guard = self.baseline.catalog.definition("active_not_mad")
        snare = self.baseline.catalog.definition("active_pants_down")
        stare = self.baseline.catalog.definition("active_stare_down")
        # #198: one value set per active, no rank rows or kind.
        for row in (bolt, guard, snare, stare):
            self.assertFalse({"ranks", "kind", "draftable"} & set(row))
        self.assertEqual(bolt["effects"][0]["base"]["p2"], 800)
        self.assertEqual(bolt["effects"][0]["ap_ratio"]["bp"], 1250)
        self.assertEqual([effect["opcode"] for effect in bolt["effects"]], [1, 12])
        self.assertEqual((bolt["mana_cost"]["p2"], bolt["cooldown"]["ms"]), (600, 3000))
        self.assertEqual(guard["effects"][0]["base"]["p2"], 2000)
        self.assertEqual([effect["opcode"] for effect in guard["effects"]], [2, 37, 3])
        self.assertEqual(snare["effects"][1]["duration"]["ms"], 250)
        self.assertEqual({key: value["p2"] for key, value in stare["effects"][1]["stats"].items()},
                         {"ArmorFlat": 300, "MagicResistFlat": 200})
        self.assertEqual({row["id"]: row["opening"] for row in self.baseline.catalog.data["abilities"]},
                         {"active_not_mad": True, "active_pants_down": True, "active_snark_bolt": True,
                          "active_stare_down": False})

    def test_stats_project_per_point_bonuses_and_growth_parameters(self):
        data = self.baseline.catalog.data
        self.assertEqual([row["id"] for row in data["stats"]],
                         ["stat_agility", "stat_brains", "stat_physique", "stat_strength", "stat_thick_skin"])
        self.assertEqual({row["id"]: row["native_id"] for row in data["stats"]},
                         {"stat_physique": 27, "stat_thick_skin": 28, "stat_strength": 29, "stat_brains": 30, "stat_agility": 31})
        brains = self.baseline.catalog.definition("stat_brains")["per_point"]
        self.assertEqual((brains["APFlat"]["p2"], brains["CooldownHasteFlat"]["p2"]), (400, 200))
        self.assertEqual(brains["MPRegenFlat"], {"unit": "mp/s", "numerator": 1, "denominator": 4})
        self.assertEqual(self.baseline.catalog.definition("stat_agility")["per_point"]["MoveSpeedBonusRatio"], {"unit": "ratio", "bp": 100})
        self.assertEqual(data["progression"]["active_offer_levels"], (4, 9))
        self.assertFalse({"max_choices", "max_skill_rank", "opening_offer", "reroll_levels"} & set(data["progression"]))
        self.assertFalse({"attributes", "opening_cards"} & set(data))
        for key, expected in (("GROWTH.OpeningPoints", 2), ("GROWTH.PointsPerLevel", 4), ("GROWTH.MaxLevelPointsXP", 250),
                              ("GROWTH.MaxLevelPoints", 1), ("GROWTH.NpcActivePoints", 5),
                              ("GROWTH.BuildSafeWindowSec", {"unit": "ms", "ms": 2500})):
            self.assertEqual(self.baseline.catalog.parameter(key), expected)
        self.assertFalse([key for key in data["parameters"] if key.startswith("CHOICE.")])

    def test_mutating_each_active_value_changes_projected_value(self):
        baseline_bytes = (self.root / demo.CONTENT_SOURCE).read_bytes()
        changes = 0
        for ability_index in range(len(self.author["abilities"])):
            for field, amount, output_field in (("mana_cost", 1, "p2"), ("cooldown", 1, "ms")):
                with self.subTest(ability=ability_index, field=field):
                    (self.root / demo.CONTENT_SOURCE).write_bytes(baseline_bytes)
                    self.edit(lambda data: data["abilities"][ability_index][field].update(
                        value=data["abilities"][ability_index][field]["value"] + amount))
                    result = demo.compile_content(self.root)
                    identity = self.author["abilities"][ability_index]["id"]
                    old = self.baseline.catalog.definition(identity)[field][output_field]
                    new = result.catalog.definition(identity)[field][output_field]
                    self.assertEqual(new - old, 100 if output_field == "p2" else 1000)
                    changes += 1
        self.assertEqual(changes, 8)
        self.assert_outputs_equal()

    def test_deterministic_bytes_do_not_depend_on_author_object_or_definition_order(self):
        self.assertEqual(self.baseline.outputs, demo.build_outputs(self.root))

        def reorder(value):
            if type(value) is dict:
                return {key: reorder(value[key]) for key in reversed(value)}
            if type(value) is list:
                return [reorder(child) for child in value]
            return value

        shuffled = reorder(self.author)
        for key in ("abilities", "stats", "display"):
            shuffled[key].reverse()
        shuffled["camps"]["monsters"].reverse()
        shuffled["camps"]["placements"].reverse()
        shuffled["world"]["spawn_ids"].reverse()
        shuffled["world"]["camp_ids"].reverse()
        (self.root / demo.CONTENT_SOURCE).write_bytes(encode(shuffled))
        result = demo.compile_content(self.root)
        self.assertEqual(self.baseline.outputs, result.outputs)
        self.assert_leaf_projection(result, shuffled)
        self.assertFalse((self.root / "docs").exists())

    def test_decimal_context_cannot_round_author_input(self):
        with localcontext() as context:
            context.prec = 2
            self.assertEqual(self.baseline.outputs, demo.build_outputs(self.root))

    def test_integer_above_double_precision_is_preserved_exactly(self):
        expected = 9007199254740993
        self.edit(lambda data: data["parameters"].update({"AIRDROP.RewardXP": expected}))
        result = demo.compile_content(self.root)
        self.assertEqual(result.catalog.parameter("AIRDROP.RewardXP"), expected)
        self.assertIn(str(expected).encode("ascii"), result.outputs[demo.CONTENT_OUTPUT])

    def test_root_manifest_covers_actual_bytes_and_has_no_self_hash(self):
        result = self.baseline
        raw = result.outputs[demo.MANIFEST_OUTPUT]
        manifest = json.loads(raw)
        self.assertEqual(result.digest, bytes.fromhex(demo.sha256(raw)))
        self.assertEqual(manifest["content"]["sha256"], demo.sha256(result.outputs[demo.CONTENT_OUTPUT]))
        self.assertEqual(manifest["world"]["sha256"], demo.sha256((self.root / demo.WORLD_OUTPUT).read_bytes()))
        self.assertEqual(manifest["world"]["contract"]["sha256"], demo.sha256((self.root / demo.WORLD_CONTRACT).read_bytes()))
        self.assertEqual(manifest["author_schema_sha256"], demo.sha256((self.root / demo.SCHEMA_SOURCE).read_bytes()))
        self.assertEqual(manifest["native_ids_sha256"], demo.sha256((self.root / demo.IDS_SOURCE).read_bytes()))
        self.assertEqual(manifest["native_contract_sha256"], demo.sha256((self.root / demo.NATIVE_IDS).read_bytes()))
        self.assertEqual(manifest["display_sha256"], demo.sha256(encode(json.loads(result.outputs[demo.CONTENT_OUTPUT])["display"])))
        self.assertNotIn(result.digest.hex(), raw.decode("utf-8"))
        self.assertIn(result.digest.hex(), result.outputs[demo.CONTENT_CONTRACT].decode("ascii"))
        self.assertEqual(demo.verify_runtime_files(self.root, result.digest), result.digest)

    def test_each_real_contract_input_changes_root_identity(self):
        for relative in (demo.SCHEMA_SOURCE, demo.IDS_SOURCE, demo.NATIVE_IDS):
            with self.subTest(relative=str(relative)):
                path = self.root / relative
                before = path.read_bytes()
                path.write_bytes(before + b"\n")
                result = demo.compile_content(self.root)
                self.assertNotEqual(result.digest, self.baseline.digest)
                self.assertEqual(result.outputs[demo.CONTENT_OUTPUT], self.baseline.outputs[demo.CONTENT_OUTPUT])
                path.write_bytes(before)
        self.edit(lambda data: data["identity"]["approval"].update(decision="TEST ONLY: another fixture review"))
        self.assertNotEqual(demo.compile_content(self.root).digest, self.baseline.digest)

    def test_real_world_change_requires_contract_and_changes_root(self):
        world = world_fixture()
        next(row for row in world["anchors"] if row["id"] == "s00")["position_cm"]["x"]["numerator"] += 1
        (self.root / demo.WORLD_OUTPUT).write_bytes(encode(demo.canonical(world)))
        with self.assertRaisesRegex(demo.ContentError, "WorldIntegrity"):
            demo.compile_content(self.root)
        write_world(self.root, world)
        with self.assertRaisesRegex(demo.ContentError, "StaleWorldOutput"):
            demo.compile_content(self.root)
        def move_rest_anchor(source):
            # A field spawn no longer owns a rest sign-up disk.
            rest = next(row for row in source["anchors"] if row["id"] == "s00")["position_cm"]
            rest.update(x=4401)
        self.edit(move_rest_anchor, demo.WORLD_SOURCE)
        world_sync.synchronize(self.root)
        result = demo.compile_content(self.root)
        self.assertNotEqual(result.digest, self.baseline.digest)
        self.assertEqual(result.outputs[demo.CONTENT_OUTPUT], self.baseline.outputs[demo.CONTENT_OUTPUT])

    def test_missing_any_required_input_refuses_without_output_write(self):
        for relative in demo.INPUT_PATHS:
            with self.subTest(relative=str(relative)):
                path = self.root / relative
                before = path.read_bytes()
                path.unlink()
                with self.assertRaisesRegex(demo.ContentError, "MissingInput"):
                    sync.synchronize(self.root)
                self.assert_outputs_equal()
                path.write_bytes(before)

    def test_pending_content_and_empty_decision_are_not_approval(self):
        for change in (lambda data: data["identity"]["approval"].update(status="pending"),
                       lambda data: data["identity"]["approval"].update(decision="   "),
                       lambda data: data["identity"].pop("approval")):
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(change=change):
                self.assert_refused(change)

    def test_original_precision_attack_and_exponent_overflow_refuse_atomically(self):
        for raw in ("0.0100000000000000001", "1e309", "NaN", "Infinity", "-Infinity", "0.001"):
            with self.subTest(raw=raw):
                (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
                self.set_raw_number("STAT.AD.Base", raw)
                with self.assertRaises(demo.ContentError):
                    sync.synchronize(self.root)
                self.assert_outputs_equal()

    def test_bad_counts_revisions_ids_and_offer_levels_never_accept_bool(self):
        mutations = (
            lambda data: data["identity"].update(schema_version=True),
            lambda data: data["parameters"].update({"AIRDROP.RewardXP": True}),
            lambda data: data["boss"].update(reward_xp=True),
            lambda data: data["progression"].update(active_offer_levels=[True]),
            lambda data: data["camps"]["monsters"][0].update(count_per_camp=True),
            lambda data: data["progression"].update(max_level=True),
            lambda data: data["parameters"].update({"AIRDROP.RewardXP": 1.0}),
            lambda data: data["parameters"].update({"GROWTH.OpeningPoints": True}),
            lambda data: data["abilities"][0].update(opening=1),
        )
        for mutation in mutations:
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation)

    def test_integer_overflow_and_checked_multiply_refuse(self):
        mutations = (
            lambda data: data["camps"]["monsters"][0].update(reward_xp=demo.INT64_MAX),
            lambda data: data["parameters"].update({"XP.BaseToNext": demo.INT64_MAX}),
            lambda data: data["parameters"].update({"REWARD.PlayerXP.PerLevel": demo.INT64_MAX}),
            lambda data: data["parameters"].update({"AIRDROP.RewardXP": demo.INT64_MAX + 1}),
            lambda data: data["parameters"]["STAT.MaxHP.Max"].update(value=demo.INT64_MAX),
        )
        for mutation in mutations:
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation, "Overflow")

    def test_invalid_quantities_and_units_refuse(self):
        mutations = (
            lambda data: data["parameters"]["STAT.AD.Base"].update(unit="hp"),
            lambda data: data["parameters"]["STAT.CritChance.Base"].update(value=1.0001),
            lambda data: data["parameters"]["STAT.CritChance.Base"].update(value=0.00001),
            lambda data: data["parameters"]["LIFE.RespawnSec.Field1"].update(value=0.0001),
            lambda data: data["parameters"]["LIFE.RespawnSec.Field3"].update(value=0),
            lambda data: data["parameters"]["MATCH.FieldSegmentSec"].update(value=0),
            lambda data: data["parameters"]["REST.ReturnSearchRadiusCm"].update(unit="s"),
            lambda data: data["parameters"]["AI.DecisionIntervalSec"].update(value=-1),
            lambda data: data["parameters"]["STAT.BaseAttackSpeed"].update(value=0),
            lambda data: data["abilities"][0]["target"].update(projectile_speed={"unit": "cm/s", "value": -1}),
            lambda data: data["abilities"][0]["mana_cost"].update(value=-1),
            lambda data: data["parameters"]["GROWTH.BuildSafeWindowSec"].update(value=0.0001),
            lambda data: data["stats"][0]["per_point"]["HPRegenFlat"].update(unit="mp/s"),
        )
        for mutation in mutations:
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation)

    def test_field_event_timing_refuses(self):
        # #198 outer §5: the drops, the Boss and the camp markers live inside the timed field segments.
        for mutation in (
            lambda data: data["parameters"]["AIRDROP.LandSec"][1].update(value=data["parameters"]["AIRDROP.LandSec"][0]["value"]),
            lambda data: data["parameters"]["AIRDROP.LandSec"][0].update(value=5),
            lambda data: data["parameters"]["AIRDROP.ExpireSec"].update(value=60),
            lambda data: data["parameters"]["AIRDROP.LandSec"].append({"unit": "s", "value": 260}),
            lambda data: data["boss"]["leave_at"].update(value=data["boss"]["appear_at"]["value"]),
            lambda data: data["boss"]["leave_at"].update(value=10000),
            lambda data: data["parameters"]["CAMPMARK.RevealSec"].update(value=data["parameters"]["MATCH.FieldSegmentSec"]["value"]),
        ):
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation, "InvalidTiming")
        for mutation in (
            lambda data: data["parameters"].update({"AIRDROP.LandSec": [{"unit": "s", "value": 10 + 30 * n} for n in range(7)]}),
            lambda data: data["parameters"].update({"AIRDROP.LandSec": []}),
            lambda data: data["boss"].update(approval={"duration": {"unit": "s", "value": 90}}),
        ):
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation)

    def test_extra_fields_at_root_nested_rank_and_effect_refuse(self):
        for mutation in (
            lambda data: data.update(candidate_mode=True),
            lambda data: data["parameters"].update({"CAMP.Low.AggroRadiusCm": {"unit": "cm", "value": 0}}),
            lambda data: data["abilities"][0].update(extra=1),
            lambda data: data["abilities"][0]["effects"][0].update(hidden_legacy_fallback=True),
            lambda data: data["stats"][0].update(extra=1),
            lambda data: data["character"]["dash"].update(windup={"unit": "s", "value": 0}),
        ):
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation)

    def test_unknown_duplicate_retired_ids_and_missing_handlers_refuse(self):
        for mutation in (
            lambda data: data["abilities"][0].update(id="unknown_ability"),
            lambda data: data["abilities"][0].update(id="snark_bolt"),
            lambda data: data["stats"][0].update(id="StatPhysique"),
            lambda data: data["stats"][0].update(id=data["stats"][1]["id"]),
            # #198: retired passives, cower and attribute cards stay refused, and an active keeps its own contract.
            lambda data: data["abilities"][0].update(id="passive_lucky_star"),
            lambda data: data["abilities"][0].update(id="active_cower"),
            lambda data: data["abilities"][0].update(id="active_sock_shot", cooldown_family="cd_sock_shot"),
            lambda data: data["stats"][0].update(id="attr_protein"),
            lambda data: data["abilities"][0]["effects"][0].update(op="legacy_damage"),
            lambda data: data["abilities"][0]["effects"].append({"op": "pierce"}),
            lambda data: data["abilities"][0]["effects"].append(deepcopy(data["abilities"][0]["effects"][0])),
            lambda data: data["abilities"][1]["effects"].pop(),
        ):
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation)

    def test_native_id_tampering_and_tombstone_reuse_refuse(self):
        original = (self.root / demo.IDS_SOURCE).read_bytes()
        for mutation in (
            lambda data: data["abilities"].update(active_snark_bolt=True),
            lambda data: data["abilities"].update(active_snark_bolt=91),
            lambda data: data["abilities"].update(active_snark_bolt=95),
            lambda data: data["abilities"].update(active_snark_bolt=200),
            # A retired identity and its retired number cannot return.
            lambda data: data["abilities"].update(active_arc_bolt=62),
            lambda data: data["retired_ranges"].update(abilities=[[0, 90]]),
            # #198 retirements may be neither dropped nor reused.
            lambda data: data["retired_ranges"].update(abilities=[[0, 91]]),
            lambda data: data["retired_ranges"].update(stats=[[0, 25]]),
            lambda data: data["retired_ranges"].update(stats=[0, 26]),
            lambda data: data["retired_ranges"].pop("stats"),
            lambda data: data["retired_ranges"].update(attributes=[[0, 26]]),
            lambda data: data["retired_ranges"].update(effect_ops=[[6, 6]]),
            lambda data: data["retired_ranges"].pop("statuses"),
            lambda data: data["abilities"].update(passive_pressed=104),
            lambda data: data["abilities"].update(active_cower=119),
            lambda data: data["abilities"].pop("active_sock_shot"),
            lambda data: data["stats"].update(stat_physique=15),
            lambda data: data.update(attributes={"attr_protein": 15}),
            lambda data: data["statuses"].update(cowering=28),
            lambda data: data["effect_ops"].update(pierce=73),
            lambda data: data["effect_ops"].update(dot=42),
            lambda data: data["statuses"].update(enraged=7),
            lambda data: data["effect_ops"].pop("shield"),
        ):
            (self.root / demo.IDS_SOURCE).write_bytes(original)
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation, relative=demo.IDS_SOURCE)
        (self.root / demo.IDS_SOURCE).write_bytes(original)
        path = self.root / demo.NATIVE_IDS
        # An unused number: a renumbering, not a collision with another declared identity.
        path.write_bytes(path.read_bytes().replace(b"ActiveSnarkBolt=94", b"ActiveSnarkBolt=200"))
        with self.assertRaisesRegex(ValueError, "NativeId"):
            demo.compile_content(self.root)

    def test_single_value_set_shape_refuses_ranks_kinds_and_missing_values(self):
        for mutation in (
            lambda data: data["abilities"][0].pop("mana_cost"),
            lambda data: data["abilities"][0].pop("cooldown"),
            lambda data: data["abilities"][0].pop("effects"),
            lambda data: data["abilities"][0].update(effects=[]),
            lambda data: data["abilities"][0]["cooldown"].update(value=0),
            lambda data: data["abilities"][0].update(kind="active"),
            lambda data: data["abilities"][0].update(ranks=[{"rank": 1, "mana_cost": data["abilities"][0]["mana_cost"],
                                                             "cooldown": data["abilities"][0]["cooldown"],
                                                             "effects": data["abilities"][0]["effects"]}]),
        ):
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation)

    def test_invalid_targets_families_and_timed_stat_binding_refuse(self):
        for mutation in (
            lambda data: data["abilities"][0].update(target={"mode": "Self"}),
            lambda data: data["abilities"][0].update(cooldown_family="unknown_family"),
            lambda data: data["abilities"][3]["effects"][1]["stats"].update(ADFlat={"unit": "points", "value": 1}),
            lambda data: data["character"].pop("dash"),
            lambda data: data["character"]["dash"].update(distance={"unit": "cm", "value": 0}),
            lambda data: data["character"]["dash"].update(duration=deepcopy(data["character"]["dash"]["cooldown"])),
        ):
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation)

    def test_element_actions_and_reaction_parameters_refuse_invalid_shapes(self):
        # #198 session A: synthetic DirectionLine/brace/contact-dash/wet-zone actives join the fixture, then each
        # invalid shape refuses with its own code.
        q = lambda unit, value: {"unit": unit, "value": value}
        damage = {"op": "damage", "base": q("points", 5), "ad_ratio": q("ratio", 0), "ap_ratio": q("ratio", 0.5),
                  "max_hp_ratio": q("ratio", 0), "damage_type": "Magical"}
        added = [
            {"id": "active_hose_down", "opening": False, "cooldown_family": "cd_hose_down", "windup": q("s", 0.25),
             "target": {"mode": "DirectionLine", "range": q("cm", 300), "half_width": q("cm", 40)},
             "mana_cost": q("mp", 5), "cooldown": q("s", 6),
             "effects": [deepcopy(damage), {"op": "knockback", "distance": q("cm", 100), "duration": q("s", 0.125)}, {"op": "wet"}]},
            {"id": "active_charged_ram", "opening": False, "cooldown_family": "cd_charged_ram", "windup": q("s", 0),
             "target": {"mode": "DirectionDash"}, "mana_cost": q("mp", 5), "cooldown": q("s", 8),
             "effects": [{"op": "dash", "distance": q("cm", 200), "duration": q("s", 0.25)},
                         {"op": "shield", "base": q("hp", 10), "ap_ratio": q("ratio", 0), "duration": q("s", 1)},
                         {"op": "dash_contact", "targets": "Every"}, deepcopy(damage), {"op": "electrify"}]},
            {"id": "active_brace", "opening": False, "cooldown_family": "cd_brace", "windup": q("s", 0),
             "target": {"mode": "Self"}, "mana_cost": q("mp", 5), "cooldown": q("s", 9),
             "effects": [{"op": "brace", "duration": q("s", 2), "ratio": q("ratio", 0.25), "limit": q("points", 40),
                          "radius": q("cm", 150), "base": q("points", 10), "record_ratio": q("ratio", 0.5)}]},
            {"id": "active_dirty_puddle", "opening": False, "cooldown_family": "cd_dirty_puddle", "windup": q("s", 0.25),
             "target": {"mode": "GroundPoint", "range": q("cm", 400)}, "mana_cost": q("mp", 5), "cooldown": q("s", 9),
             "effects": [{"op": "zone", "radius": q("cm", 150), "duration": q("s", 2), "interval": q("s", 1),
                          "delay": q("s", 0), "count": 2},
                         {"op": "zone_damage", "base": q("points", 3), "ad_ratio": q("ratio", 0), "ap_ratio": q("ratio", 0.1),
                          "damage_type": "Magical"},
                         {"op": "zone_wet"}, {"op": "zone_haste", "ratio": q("ratio", 0.1)}]},
        ]
        author = deepcopy(self.author)
        author["abilities"].extend(added)
        author["display"].extend({"id": row["id"], "namespace": "DemoContent", "name_key": row["id"] + ".name",
                                  "name_text": "测试名称 " + row["id"], "description_key": row["id"] + ".description",
                                  "description_text": "TEST ONLY 合成说明 " + row["id"], "icon_id": "icon_" + row["id"]}
                                 for row in added)
        (self.root / demo.CONTENT_SOURCE).write_bytes(encode(author))
        demo.compile_author(self.root)

        def ability(data, identity):
            return next(row for row in data["abilities"] if row["id"] == identity)

        def first(data, identity, op):
            return next(effect for effect in ability(data, identity)["effects"] if effect["op"] == op)

        for mutation, code in (
            (lambda d: ability(d, "active_dirty_puddle")["effects"].append({"op": "wet"}), "TargetEffectMismatch"),
            (lambda d: ability(d, "active_dirty_puddle")["effects"].pop(2), "MissingHandler"),
            (lambda d: first(d, "active_dirty_puddle", "zone_haste")["ratio"].update(value=1), "NumericDomain"),
            (lambda d: first(d, "active_brace", "brace")["ratio"].update(value=1), "NumericDomain"),
            (lambda d: first(d, "active_brace", "brace")["limit"].update(value=0), "IneffectiveEffect"),
            (lambda d: first(d, "active_brace", "brace")["radius"].update(value=0), "InvalidGeometry"),
            (lambda d: ability(d, "active_brace")["effects"].append({"op": "wet"}), "TargetEffectMismatch"),
            (lambda d: ability(d, "active_hose_down")["target"].pop("half_width"), "SchemaVariant"),
            (lambda d: ability(d, "active_hose_down")["target"]["half_width"].update(value=0), "NumericDomain"),
            (lambda d: ability(d, "active_hose_down")["effects"].append({"op": "root", "duration": q("s", 1)}),
             "TargetEffectMismatch"),
            (lambda d: first(d, "active_charged_ram", "dash_contact").update(targets="All"), "SchemaVariant"),
            (lambda d: first(d, "active_charged_ram", "dash_contact").update(count=1), "SchemaVariant"),
            (lambda d: first(d, "active_charged_ram", "dash_contact").pop("targets"), "SchemaVariant"),
            (lambda d: ability(d, "active_charged_ram")["effects"].pop(2), "TargetEffectMismatch"),
            (lambda d: d["parameters"]["COMBAT.FogIntervalSec"].update(value=3), "InvalidTiming"),
            (lambda d: d["parameters"]["COMBAT.ConductSlow"].update(value=1), "NumericDomain"),
            (lambda d: d["parameters"]["COMBAT.WetSlow"].update(value=0), "NumericDomain"),
            (lambda d: d["parameters"]["COMBAT.DischargeRadiusCm"].update(value=0), "InvalidGeometry"),
            (lambda d: d["parameters"].update({"COMBAT.OverloadBreachStacks": 5}), "NumericDomain"),
            (lambda d: d["parameters"].update({"COMBAT.OverloadBreachStacks": 11}), "NumericOverflow"),
            (lambda d: d["parameters"].pop("COMBAT.ReactionBase"), "MissingField"),
        ):
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(author))
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation, code)

    def test_each_stat_declares_exactly_its_positive_per_point_bonuses(self):
        for mutation in (
            lambda data: data["stats"][0].update(per_point={"CritChanceBonusRatio": {"unit": "ratio", "value": 0.5}}),
            lambda data: data["stats"][0]["per_point"].pop("HPRegenFlat"),
            lambda data: data["stats"][2]["per_point"].update(APFlat={"unit": "points", "value": 1}),
            lambda data: data["stats"][0].update(penalties={"MaxHPFlat": {"unit": "hp", "value": 5}}),
            lambda data: data["stats"][0]["per_point"]["MaxHPFlat"].update(value=0),
            lambda data: data["stats"][4].update(per_point={}),
            lambda data: data["stats"].pop(),
            lambda data: data["stats"].append(deepcopy(data["stats"][0])),
            lambda data: data["stats"][1].update(id="stat_physique"),
        ):
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation, "UnknownField|IneffectiveChoice|EmptyDefinition|ArrayLength|DuplicateId")

    def test_capped_gains_clamp_instead_of_refusing(self):
        # Every attribute clamps to its bounds in the projection (#112 §4): a source sum past a cap still compiles.
        for mutation in (
            lambda data: data["parameters"]["STAT.AD.Max"].update(value=40),
            lambda data: data["parameters"]["STAT.AttackSpeed.Max"].update(value=1.1),
            lambda data: data["parameters"]["STAT.MoveSpeed.Max"].update(value=510),
            lambda data: data["parameters"]["STAT.CritChance.Base"].update(value=0.99),
        ):
            with self.subTest(mutation=mutation):
                changed = deepcopy(self.author)
                mutation(changed)
                (self.root / demo.CONTENT_SOURCE).write_bytes(encode(changed))
                demo.compile_author(self.root)
        (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))

    def test_opening_pool_offer_levels_growth_and_recovery_refuse_invalid_inputs(self):
        for mutation, code in (
            # #198: the opening opportunity draws three distinct actives from the opening pool.
            (lambda data: [row.update(opening=False) for row in data["abilities"] if row["id"] == "active_snark_bolt"],
             "OpeningPool"),
            (lambda data: [row.update(opening=True) for row in data["abilities"]], None),
            (lambda data: data["abilities"][0].pop("opening"), "MissingField"),
            (lambda data: data["abilities"][0].update(draftable=True), "UnknownField"),
            (lambda data: data["abilities"][0].update(tags=["Mobility"]), "UnknownField"),
            (lambda data: data["progression"].update(active_offer_levels=[9, 4]), "ActiveOfferLevels"),
            (lambda data: data["progression"].update(active_offer_levels=[4, 4]), "ActiveOfferLevels"),
            (lambda data: data["progression"].update(active_offer_levels=[17]), "NumericOverflow"),
            (lambda data: data["progression"].update(active_offer_levels=[1]), "NumericDomain"),
            (lambda data: data["progression"].pop("active_offer_levels"), "MissingField"),
            (lambda data: data["progression"].update(active_offer_levels=[]), None),
            (lambda data: data["progression"].update(active_offer_levels=[2, 16]), None),
            (lambda data: data["progression"].update(reroll_levels=[5, 11]), "UnknownField"),
            (lambda data: data["progression"].update(max_skill_rank=2), "UnknownField"),
            (lambda data: data["progression"].update(opening_offer={"matched_skill_weight": 3, "other_skill_weight": 2}),
             "UnknownField"),
            (lambda data: data.update(opening_cards=[]), "UnknownField"),
            (lambda data: data.update(attributes=[]), "UnknownField"),
            (lambda data: data["parameters"].update({"CHOICE.AttributeQuality.GreenChance": {"unit": "ratio", "value": 0.4}}),
             "UnknownField"),
            (lambda data: data["parameters"].pop("GROWTH.PointsPerLevel"), "MissingField"),
            (lambda data: data["parameters"].update({"GROWTH.OpeningPoints": 0}), "NumericDomain"),
            (lambda data: data["parameters"].update({"GROWTH.MaxLevelPoints": -1}), "NumericDomain"),
            (lambda data: data["parameters"].update({"GROWTH.MaxLevelPointsXP": {"unit": "xp", "value": 400}}), "SchemaType"),
            (lambda data: data["parameters"]["GROWTH.BuildSafeWindowSec"].update(value=0), "NumericDomain"),
            (lambda data: data["parameters"].update({"GROWTH.PointsPerLevel": demo.INT64_MAX}), "NumericOverflow"),
            (lambda data: data["parameters"]["REGEN.OutOfCombatMaxHPRatioPerSec"].update(value=0), "NumericDomain"),
            (lambda data: data["parameters"]["REGEN.OutOfCombatMaxMPRatioPerSec"].update(value=0.0155), "NumericDomain"),
            (lambda data: data["parameters"]["REGEN.OutOfCombatDelaySec"].update(value=0), "NumericDomain"),
            (lambda data: data["parameters"].pop("REGEN.OutOfCombatDelaySec"), "MissingField"),
            # #198 rest phases: PK sign-up is the tail of every build window; retired timeline keys stay rejected.
            (lambda data: data["parameters"]["REST.SignupSec"].update(value=26), "InvalidTiming"),
            (lambda data: data["parameters"]["REST.OpeningBuildSec"].update(value=7), "InvalidTiming"),
            (lambda data: data["parameters"]["REST.SignupSec"].update(value=25), None),
            (lambda data: data["parameters"]["REST.ReturnSearchRadiusCm"].update(value=0), "InvalidGeometry"),
            (lambda data: data["parameters"].update({"GROWTH.NpcActivePoints": 0}), "NumericDomain"),
            (lambda data: data["parameters"].pop("REST.PKMaxSec"), "MissingField"),
            (lambda data: data["parameters"].update({"MATCH.DevelopmentSec": {"unit": "s", "value": 180}}), "UnknownField"),
            (lambda data: data["parameters"].update({"MATCH.CountdownSec": {"unit": "s", "value": 5}}), "UnknownField"),
            (lambda data: data["parameters"].update({"LIFE.RespawnSec": {"unit": "s", "value": 15}}), "UnknownField"),
            # #198 PK ring and spectator throw: ordered shrink steps inside the PK cap, nested radii, positive throw.
            (lambda data: data["parameters"]["PK.RingFinalSec"].update(value=45), "InvalidTiming"),
            (lambda data: data["parameters"]["PK.RingFinalSec"].update(value=44.999), None),
            (lambda data: data["parameters"]["PK.RingMidSec"].update(value=8), "InvalidTiming"),
            (lambda data: data["parameters"]["PK.RingShrinkStartSec"].update(value=0), "NumericDomain"),
            (lambda data: data["parameters"]["PK.RingDamageTickSec"].update(value=0), "NumericDomain"),
            (lambda data: data["parameters"]["PK.RingMidRadiusCm"].update(value=1500), "InvalidGeometry"),
            (lambda data: data["parameters"]["PK.RingFinalRadiusCm"].update(value=500), "InvalidGeometry"),
            (lambda data: data["parameters"]["PK.RingFinalRadiusCm"].update(value=0), "InvalidGeometry"),
            (lambda data: data["parameters"]["PK.RingDamageMidMaxHPRatioPerSec"].update(value=0), "NumericDomain"),
            (lambda data: data["parameters"]["PK.TeamFormatRatio"].update(value=0), None),
            (lambda data: data["parameters"]["PK.TeamFormatRatio"].update(value=1.5), "NumericOverflow"),
            (lambda data: data["parameters"].update({"PK.WinnerPointsMax": 0}), "NumericDomain"),
            (lambda data: data["parameters"]["THROW.RangeCm"].update(value=0), "InvalidGeometry"),
            (lambda data: data["parameters"]["THROW.RadiusCm"].update(value=0), "InvalidGeometry"),
            (lambda data: data["parameters"]["THROW.SlowRatio"].update(value=0), "NumericDomain"),
            (lambda data: data["parameters"].pop("THROW.ExposeSec"), "MissingField"),
        ):
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(mutation=mutation):
                if code is None:
                    changed = deepcopy(self.author)
                    mutation(changed)
                    (self.root / demo.CONTENT_SOURCE).write_bytes(encode(changed))
                    demo.compile_author(self.root)
                else:
                    self.assert_refused(mutation, code)

    def test_build_bounds_zero_gain_and_xp_consistency_refuse(self):
        for mutation in (
            lambda data: data["parameters"]["STAT.MaxHP.Max"].update(value=150),
            lambda data: data["parameters"]["STAT.MaxHP.Min"].update(value=200),
            lambda data: data["stats"][2]["per_point"]["ADFlat"].update(value=0),
            lambda data: data["progression"]["xp_thresholds"].__setitem__(4, 259),
            lambda data: data["formulas"]["ResistanceScale"].update(value=0),
        ):
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation)

    def test_invalid_camp_references_counts_and_display_refuse(self):
        for mutation in (
            lambda data: data["camps"]["placements"][0].update(monster_id="unknown_monster"),
            lambda data: data["camps"]["placements"][0].update(anchor_id="unknown_camp"),
            lambda data: data["camps"]["placements"].pop(),
            lambda data: data["camps"]["monsters"][1].update(tier="Low"),
            lambda data: data["camps"]["monsters"][0].update(count_per_camp=3),
            lambda data: data["world"]["spawn_ids"].__setitem__(1, data["world"]["spawn_ids"][0]),
            lambda data: data["display"].pop(),
            lambda data: data["display"][0].pop("portrait_icon_id"),
            lambda data: data["display"][0].update(icon_id="unresolved_icon"),
        ):
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation)

    def test_display_text_is_required_plain_unique_and_number_free(self):
        for mutation, pattern in (
            (lambda data: data["display"][0].pop("name_text"), "MissingField"),
            (lambda data: data["display"][1].pop("description_text"), "MissingField"),
            (lambda data: data["display"][1].update(name_text=1), "SchemaType"),
            (lambda data: data["display"][1].update(description_text=""), "StringLength"),
            (lambda data: data["display"][1].update(name_text="名" * 33), "StringLength"),
            (lambda data: data["display"][1].update(description_text="说" * 321), "StringLength"),
            (lambda data: data["display"][1].update(description_text="换行\n说明"), "DisplayText"),
            (lambda data: data["display"][1].update(name_text="名称\u200b"), "DisplayText"),
            (lambda data: data["display"][1].update(name_text="名称\u202e"), "DisplayText"),
            (lambda data: data["display"][1].update(description_text="造成{Damage}点伤害"), "DisplayText"),
            (lambda data: data["display"][1].update(name_text="名\U0001f600"), "DisplayText"),
            (lambda data: data["display"][1].update(name_text=" 名称"), "DisplayText"),
            (lambda data: data["display"][1].update(name_text="名称\u3000"), "DisplayText"),
            (lambda data: data["display"][1].update(description_text="造成70点伤害"), "DisplayText"),
            (lambda data: data["display"][1].update(description_text="造成７０点伤害"), "DisplayText"),
            (lambda data: data["display"][2].update(name_text=data["display"][1]["name_text"]), "DisplayText"),
        ):
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(pattern=pattern):
                self.assert_refused(mutation, pattern)
        (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
        self.edit(lambda data: data["display"][1].update(name_text="502胶水"))
        named = demo.compile_content(self.root)
        self.assertIn("502胶水", {row["name_text"] for row in json.loads(named.outputs[demo.CONTENT_OUTPUT])["display"]})

    def test_card_templates_read_the_single_value_set_and_one_stat_point(self):
        def display(data, identity):
            return next(row for row in data["display"] if row["id"] == identity)

        for mutation, pattern in (
            (lambda data: display(data, "active_snark_bolt").update(description_text="眩晕 [stun.duration] 秒"), "CardTemplate"),
            (lambda data: display(data, "active_snark_bolt").update(description_text="多 [per_point.APFlat]"), "CardTemplate"),
            (lambda data: display(data, "active_stare_down").update(description_text="攻 [timed_stat_bonus.ADFlat]"), "CardTemplate"),
            (lambda data: display(data, "stat_brains").update(description_text="每点 [per_point.ADFlat]"), "CardTemplate"),
            (lambda data: display(data, "stat_brains").update(description_text="每点 [damage.amount]"), "CardTemplate"),
            (lambda data: display(data, "stat_brains").update(description_text="每点 [per_point.APFlat"), "CardTemplate"),
            (lambda data: display(data, "active_snark_bolt").update(advanced_key="active_snark_bolt.advanced",
                                                                    advanced_text="高级"), "UnknownField"),
        ):
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(pattern=pattern, mutation=mutation):
                self.assert_refused(mutation, pattern)
        (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
        self.edit(lambda data: display(data, "stat_physique").update(
            description_text="每点 +[per_point.MaxHPFlat]，回复 +[per_point.HPRegenFlat]/秒。"))
        texts = {row["id"]: row["description_text"] for row in json.loads(demo.compile_content(self.root).outputs[demo.CONTENT_OUTPUT])["display"]}
        self.assertIn("[per_point.HPRegenFlat]", texts["stat_physique"])

    def test_components_project_kinds_openings_bindings_and_category_bands(self):
        # #198 B: components keep their authored native ID order and carry their opcodes, openings and bindings.
        data = self.baseline.catalog.data
        self.assertEqual([(row["id"], row["native_id"]) for row in data["components"]],
                         [("component_fist_fire", 1), ("component_kick_down", 7), ("component_dash_punch", 12),
                          ("component_dirty_trick", 18)])
        fire = self.baseline.catalog.definition("component_fist_fire")
        self.assertEqual((fire["kind"], fire["pool"], fire["opening"]), ("base", "common", "always"))
        self.assertEqual(fire["effects"], ({"op": "burn", "opcode": 71, "basic_stacks": 2},))
        self.assertEqual(self.baseline.catalog.definition("component_kick_down")["opening"]["actives"], ("active_pants_down",))
        punch = self.baseline.catalog.definition("component_dash_punch")
        self.assertNotIn("opening", punch)
        self.assertEqual((punch["effects"][0]["trigger"], punch["effects"][0]["charges"]), ("DashComplete", 1))
        trick = self.baseline.catalog.definition("component_dirty_trick")
        self.assertEqual(trick["effects"], ({"op": "active_element", "opcode": 104},))
        self.assertEqual(trick["binding"]["actives"], ("active_snark_bolt", "active_pants_down"))
        self.assertEqual(trick["binding"]["elements"], ("fire", "water"))
        self.assertNotIn("draw_requires_actives", trick)
        self.edit(lambda value: next(row for row in value["components"] if row["id"] == "component_dash_punch").update(
            draw_requires_actives=["active_snark_bolt", "active_pants_down"]))
        required = demo.compile_content(self.root).catalog.definition("component_dash_punch")["draw_requires_actives"]
        self.assertEqual(required, ("active_snark_bolt", "active_pants_down"))
        self.edit(lambda value: [row.update(form="dash_shape") for row in value["components"]
                                 if row["id"] in ("component_fist_fire", "component_kick_down")])
        self.assertEqual(demo.compile_content(self.root).catalog.definition("component_kick_down")["form"], "dash_shape")
        self.assertEqual([(band["levels"], band["base"], band["mod"], band["connector"])
                          for band in data["progression"]["component_category_weights"]],
                         [((2, 8), 60, 20, 20), ((9, 16), 30, 30, 40)])
        self.assertEqual(self.baseline.catalog.parameter("GROWTH.EmptyComponentPoolPoints"), 2)

    def test_component_rows_category_bands_and_component_ops_refuse_invalid_inputs(self):
        def piece(data, identity):
            return next(row for row in data["components"] if row["id"] == identity)

        def bolt(data):
            return next(row for row in data["abilities"] if row["id"] == "active_snark_bolt")

        bands = lambda data: data["progression"]["component_category_weights"]
        for mutation, code in (
            # Level-up category bands: contiguous from 2 to max_level, each a 100 percent split with all three keys.
            (lambda data: bands(data)[1]["levels"].__setitem__(0, 10), "CategoryWeights"),
            (lambda data: bands(data)[1]["levels"].__setitem__(0, 8), "CategoryWeights"),
            (lambda data: bands(data)[0]["levels"].__setitem__(0, 3), "CategoryWeights"),
            (lambda data: bands(data)[1]["levels"].__setitem__(1, 15), "CategoryWeights"),
            (lambda data: bands(data)[0].update(base=61), "CategoryWeights"),
            (lambda data: bands(data)[0].pop("connector"), "MissingField"),
            (lambda data: bands(data).reverse(), "CategoryWeights"),
            (lambda data: data["progression"].pop("component_category_weights"), "MissingField"),
            (lambda data: data["parameters"].pop("GROWTH.EmptyComponentPoolPoints"), "MissingField"),
            (lambda data: data["parameters"].update({"GROWTH.EmptyComponentPoolPoints": 0}), "NumericDomain"),
            # Bindings name known damaging actives and distinct fire/water/electric elements.
            (lambda data: piece(data, "component_dirty_trick")["binding"]["actives"].append("active_missing"), "InvalidReference"),
            (lambda data: piece(data, "component_dirty_trick")["binding"]["actives"].append("active_not_mad"), "ComponentBinding"),
            (lambda data: piece(data, "component_dirty_trick")["binding"]["elements"].append("fire"), "ComponentBinding"),
            (lambda data: piece(data, "component_dirty_trick")["binding"]["elements"].append("ice"), "UnknownEnum"),
            (lambda data: piece(data, "component_dirty_trick").pop("binding"), "ComponentBinding"),
            (lambda data: piece(data, "component_fist_fire").update(
                binding={"kind": "active_element", "actives": ["active_snark_bolt"], "elements": ["fire"]}), "ComponentBinding"),
            # Openings name actives of the opening pool, and only common base components open.
            (lambda data: piece(data, "component_kick_down")["opening"]["actives"].append("active_missing"), "InvalidReference"),
            (lambda data: piece(data, "component_kick_down")["opening"]["actives"].append("active_stare_down"), "OpeningPool"),
            (lambda data: piece(data, "component_fist_fire").update(pool="scarce"), "OpeningPool"),
            (lambda data: piece(data, "component_fist_fire").update(opening="never"), "SchemaVariant"),
            (lambda data: piece(data, "component_fist_fire").update(kind="charm"), "UnknownEnum"),
            # A draw requirement names distinct known actives on a common (level-up drawable) component.
            (lambda data: piece(data, "component_dash_punch").update(draw_requires_actives=["active_missing"]), "InvalidReference"),
            (lambda data: piece(data, "component_dash_punch").update(
                draw_requires_actives=["active_snark_bolt", "active_snark_bolt"]), "InvalidReference"),
            (lambda data: piece(data, "component_dash_punch").update(draw_requires_actives="active_snark_bolt"), "SchemaType"),
            (lambda data: piece(data, "component_dash_punch").update(draw_requires_actives=[]), "ArrayLength"),
            (lambda data: piece(data, "component_dash_punch").update(pool="scarce", draw_requires_actives=["active_snark_bolt"]),
             "DrawRequirement"),
            # A form groups at least two components of one kind.
            (lambda data: piece(data, "component_kick_down").update(form="solo_form"), "UnknownEnum"),
            (lambda data: piece(data, "component_kick_down").update(form="dash_shape"), "ComponentForm"),
            # Component and active operation forms never cross.
            (lambda data: bolt(data)["effects"].append({"op": "lifesteal", "ratio": {"unit": "ratio", "value": 0.1}}),
             "ComponentOnlyEffect"),
            (lambda data: piece(data, "component_fist_fire")["effects"].append({"op": "root", "duration": {"unit": "s", "value": 1}}),
             "ComponentEffect"),
            (lambda data: piece(data, "component_fist_fire")["effects"][0].update(stacks=1), "ObjectShape"),
            (lambda data: piece(data, "component_dash_punch")["effects"][0].pop("trigger"), "ObjectShape"),
            (lambda data: piece(data, "component_dash_punch")["effects"][0].pop("charges"), "ObjectShape"),
            (lambda data: piece(data, "component_dash_punch")["effects"][0].update(damage_type="Magical"), "NumericDomain"),
            (lambda data: piece(data, "component_kick_down")["effects"].append({"op": "basic_damage_scale", "ratio": {"unit": "ratio", "value": 1}}),
             "NumericDomain"),
            (lambda data: piece(data, "component_kick_down")["effects"].append(
                {"op": "nth_basic_bonus", "count": 3, "base": {"unit": "points", "value": 5}, "damage_type": "Physical"}), "SchemaVariant"),
            (lambda data: data["components"].reverse(), "ComponentOrder"),
            (lambda data: data["components"].append(deepcopy(data["components"][0])), "DuplicateId"),
            (lambda data: data["components"].append(dict(deepcopy(data["components"][0]), id="active_snark_bolt")), "UnknownId"),
            (lambda data: next(row for row in data["display"] if row["id"] == "component_fist_fire").update(
                description_text="点火 [root.duration]"), "CardTemplate"),
            (lambda data: data["display"].remove(next(row for row in data["display"] if row["id"] == "component_kick_down")),
             "DisplayReference"),
        ):
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(code=code, mutation=mutation):
                self.assert_refused(mutation, code)

    def test_schema_keywords_revisions_and_quantity_storage_are_not_silent(self):
        original = (self.root / demo.SCHEMA_SOURCE).read_bytes()
        for mutation in (
            lambda data: data.update(unknown_keyword=True),
            lambda data: data["$defs"]["hp"].update(**{"x-quantity": "Float"}),
            lambda data: data["properties"]["character"].update(additionalProperties=True),
            lambda data: data["$defs"]["hp"].update(**{"$ref": "#/$defs/hp"}),
            lambda data: data["$defs"]["hp"].update(required=True),
            lambda data: data["$defs"]["hp"]["properties"]["value"].update(minimum=True),
            lambda data: data["$defs"]["hp"]["properties"]["unit"].update(const="cm"),
            lambda data: data["$defs"]["id"].update(pattern="["),
        ):
            (self.root / demo.SCHEMA_SOURCE).write_bytes(original)
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation, "InvalidSchema", demo.SCHEMA_SOURCE)
        (self.root / demo.SCHEMA_SOURCE).write_bytes(original)
        self.assert_refused(lambda data: data["identity"].update(projection_revision=1))

    def test_empty_placeholder_or_arbitrary_hash_is_not_a_world(self):
        original = (self.root / demo.WORLD_CONTRACT).read_bytes()
        for raw in (b"{}", b'{"schema_version":1}', b"null", b"[]", b""):
            with self.subTest(raw=raw):
                (self.root / demo.WORLD_OUTPUT).write_bytes(raw)
                contract = original.replace(demo.sha256(self.baseline.inputs[demo.WORLD_OUTPUT]).encode("ascii"),
                                            demo.sha256(raw).encode("ascii"))
                (self.root / demo.WORLD_CONTRACT).write_bytes(contract)
                with self.assertRaises(demo.ContentError):
                    sync.synchronize(self.root)
                self.assert_outputs_equal()
        write_world(self.root, world_fixture())
        (self.root / demo.WORLD_CONTRACT).write_bytes(original.replace(
            demo.sha256(self.baseline.inputs[demo.WORLD_OUTPUT]).encode("ascii"), b"0" * 64))
        with self.assertRaisesRegex(demo.ContentError, "WorldIntegrity"):
            demo.compile_content(self.root)

    def test_world_shape_coordinates_units_revisions_and_references_refuse(self):
        mutations = (
            lambda data: data.update(schema_version=True),
            lambda data: data.update(projection_revision=1),
            lambda data: data.update(extra=True),
            lambda data: data["anchors"].pop(),
            lambda data: data["anchors"][0].update(id=data["anchors"][1]["id"]),
            lambda data: data["anchors"][0].update(surface_id="unknown_surface"),
            lambda data: data["anchors"][0]["position_cm"]["x"].update(denominator=0),
            lambda data: data["anchors"][0]["position_cm"]["x"].update(numerator=5001),
            lambda data: data["anchors"][0]["position_cm"]["x"].update(numerator=0.5),
            lambda data: data["anchors"][0]["position_cm"]["x"].update(numerator=True),
            lambda data: data["anchors"][0]["position_cm"]["x"].update(numerator=200, denominator=2),
            lambda data: data["anchors"].__setitem__(0, None),
            lambda data: data["geometry"].update(surfaces=[]),
        )
        for mutation in mutations:
            world = world_fixture()
            mutation(world)
            write_world(self.root, world)
            with self.subTest(mutation=mutation), self.assertRaises(demo.ContentError):
                sync.synchronize(self.root)
            self.assert_outputs_equal()

    def zone_cases(self, kind, zone):
        """Mutations of the first anchor of `kind` (a dorm or airdrop disk of radius `zone`) found on the current
        layout: bounds, illegal ground, and a legal spot overlapping each neighbouring zone rule in turn."""
        import DemoWorld

        author = json.loads((self.root / demo.WORLD_SOURCE).read_bytes())
        geometry = DemoWorld.Geometry(world_fixture())
        rows = {row["id"]: row for row in author["anchors"]}
        target = next(identity for identity, row in rows.items() if row["kind"] == kind)
        body, camp_clear, dorm_reach = 40, author["geometry"]["camp_clearance_cm"], 400

        def at(identity):
            p = rows[identity]["position_cm"]
            return p["x"], p["y"], p["z"]

        def near(centre, within, rule):
            for distance in range(0, 1500, 25):
                for step in range(48):
                    angle = 2 * math.pi * step / 48
                    for z in (0, 150, 300):
                        p = (round(centre[0] + distance * math.cos(angle)), round(centre[1] + distance * math.sin(angle)), z)
                        if (math.dist(p[:2], centre[:2]) < within and rule(p)
                                and geometry.legal(tuple(Fraction(v) for v in p), Fraction(body + zone))):
                            return p
            return None

        def apart(p, others):
            return all(math.dist(p[:2], c[:2]) > r for c, r in others)

        capture = (at("capture"), 300 + zone)
        camps = [(at(i), camp_clear + zone) for i, r in rows.items() if r["kind"] == "Camp"]
        events = [(at(i), (dorm_reach if r["kind"] == "Dorm" else 150) + zone) for i, r in rows.items()
                  if r["kind"] in ("Dorm", "Airdrop") and i != target]

        def move(p):
            return lambda data: next(row for row in data["anchors"] if row["id"] == target)["position_cm"].update(
                x=p[0], y=p[1], z=p[2])

        def first(spots):
            spot = next((p for p in spots if p), None)
            self.assertIsNotNone(spot, f"the layout offers a legal {kind} spot for this case")
            return spot

        def against_tree():
            # A body fits beside a trunk, but the disk round it does not.
            for row in author["geometry"]["blockers"]:
                if row["shape"] == "cylinder":
                    c = row["center_cm"]
                    p = (c["x"] + row["radius_cm"] + body + 20, c["y"], c["z"])
                    q = tuple(Fraction(v) for v in p)
                    if (geometry.legal(q, Fraction(body)) and not geometry.legal(q, Fraction(body + zone))
                            and apart(p, [capture] + camps + events)):
                        yield p

        label = kind.lower()
        on_capture = first([near(capture[0], capture[1], lambda p: apart(p, camps + events))])
        by_camp = first(near(c[0], c[1], lambda p: apart(p, [capture] + events)) for c in camps)
        cases = [
            (lambda data: data.update(anchors=[row for row in data["anchors"] if row["kind"] != kind]),
             f"expected 1..[0-9]+ {kind} anchors, found 0"),
            (move(first(against_tree())), f"{label} disk lacks legal capsule support"),
            (move(on_capture), f"{label} disk overlaps the capture disk"),
            (move(by_camp), f"{label} disk overlaps a camp clearing"),
        ]
        if events:
            cases.append((move(first(near(c[0], c[1], lambda p: apart(p, [capture] + camps)) for c in events)),
                          "disk overlaps another event disk"))
        return cases

    def check_zone_cases(self, cases):
        source = self.root / demo.WORLD_SOURCE
        original = source.read_bytes()
        for mutation, message in cases:
            self.edit(mutation, demo.WORLD_SOURCE)
            with self.subTest(message=message), self.assertRaisesRegex(demo.ContentError, message):
                world_sync.synchronize(self.root)
            source.write_bytes(original)
            self.assert_outputs_equal()

    def test_dorm_disks_must_be_legal_ground_apart_from_other_zones(self):
        # D21 (#79): each dorm disk stands on legal ground and keeps apart from the capture circle,
        # every camp clearing and every other event disk.
        self.check_zone_cases(self.zone_cases("Dorm", 400))

    def test_airdrop_points_must_be_legal_ground_apart_from_other_zones(self):
        # D26 (#79): each airdrop pickup disk stands on legal ground and keeps apart from the capture circle,
        # every camp clearing and every other event disk.
        self.check_zone_cases(self.zone_cases("Airdrop", 150))

    def test_exact_file_bytes_including_newlines_are_required(self):
        for relative in demo.OUTPUT_PATHS:
            path = self.root / relative
            raw = path.read_bytes()
            path.write_bytes(raw.replace(b"\n", b"\r\n"))
            with self.subTest(relative=str(relative)), self.assertRaisesRegex(demo.ContentError, "StaleOutput"):
                sync.synchronize(self.root, check=True)
            path.write_bytes(raw)
        path = self.root / demo.WORLD_OUTPUT
        path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
        with self.assertRaisesRegex(demo.ContentError, "NonCanonical|WorldIntegrity"):
            demo.compile_content(self.root)

    def test_runtime_integrity_refuses_all_tampered_outputs(self):
        for relative in (*demo.OUTPUT_PATHS, demo.WORLD_OUTPUT, demo.WORLD_CONTRACT):
            path = self.root / relative
            before = path.read_bytes()
            path.write_bytes(before + b" ")
            with self.subTest(relative=str(relative)), self.assertRaises(demo.ContentError):
                demo.verify_runtime_files(self.root, self.baseline.digest)
            path.write_bytes(before)
        with self.assertRaisesRegex(demo.ContentError, "CatalogIntegrity"):
            demo.verify_runtime_files(self.root, b"\0" * 32)

    def test_cli_check_never_repairs_or_writes(self):
        out, err = io.StringIO(), io.StringIO()
        with patch.object(sync, "write_outputs", side_effect=AssertionError("check wrote output")):
            with redirect_stdout(out), redirect_stderr(err):
                self.assertEqual(sync.main(["--check"], self.root), 0)
        self.assertIn("PASS", out.getvalue())
        (self.root / demo.CONTENT_OUTPUT).unlink()
        with redirect_stderr(err):
            self.assertEqual(sync.main(["--check"], self.root), 2)
        self.assertFalse((self.root / demo.CONTENT_OUTPUT).exists())
        self.assertIn("StaleOutput", err.getvalue())

    def test_successful_transaction_updates_exact_set_then_is_noop(self):
        self.edit(lambda data: data["boss"].update(reward_xp=31))
        compilation = demo.compile_content(self.root)
        installed = []
        install = sync._install_output

        def record(protected, raw):
            installed.append(protected.path.relative_to(self.root))
            install(protected, raw)

        with patch.object(sync, "_install_output", side_effect=record):
            sync.write_outputs(self.root, compilation)
        self.assertEqual(installed, list(demo.OUTPUT_PATHS))
        self.assertEqual(installed[-1], demo.MANIFEST_OUTPUT)
        self.assert_outputs_equal(compilation.outputs)
        self.assertEqual(demo.verify_runtime_files(self.root, compilation.digest), compilation.digest)
        with patch.object(sync, "_stage", side_effect=AssertionError("unchanged generation rewrote files")):
            sync.write_outputs(self.root, compilation)

    def test_staging_failure_never_overwrites_existing_outputs(self):
        self.edit(lambda data: data["boss"].update(reward_xp=31))
        compilation = demo.compile_content(self.root)
        for failure_at in range(1, 7):
            stage, count = sync._stage, 0

            def fail(path, raw):
                nonlocal count
                count += 1
                if count == failure_at:
                    raise OSError("injected staging failure")
                stage(path, raw)

            with self.subTest(failure_at=failure_at), patch.object(sync, "_stage", side_effect=fail):
                with self.assertRaisesRegex(OSError, "injected"):
                    sync.write_outputs(self.root, compilation)
            self.assert_outputs_equal()
            self.assertFalse((self.root / demo.DATA / ".compile.lock").exists())

    def test_each_commit_failure_rolls_back_all_installed_files(self):
        self.edit(lambda data: data["boss"].update(reward_xp=31))
        compilation = demo.compile_content(self.root)
        install = sync._install_output
        for failure_at in (1, 2, 3):
            count = 0

            def fail(protected, raw):
                nonlocal count
                count += 1
                if count == failure_at:
                    raise OSError("injected commit failure")
                install(protected, raw)

            with self.subTest(failure_at=failure_at), patch.object(sync, "_install_output", side_effect=fail):
                with self.assertRaisesRegex(OSError, "injected"):
                    sync.write_outputs(self.root, compilation)
            self.assert_outputs_equal()
            self.assertFalse((self.root / demo.DATA / ".compile.lock").exists())
            self.assertFalse(list((self.root / "Artifacts").rglob("transaction-*")))

    def test_first_generation_failure_leaves_no_partial_outputs(self):
        for relative in demo.OUTPUT_PATHS:
            (self.root / relative).unlink()
        install, count = sync._install_output, 0

        def fail(protected, raw):
            nonlocal count
            count += 1
            if count == 3:
                raise OSError("injected manifest failure")
            install(protected, raw)

        with patch.object(sync, "_install_output", side_effect=fail), self.assertRaises(OSError):
            sync.write_outputs(self.root, self.baseline)
        self.assertTrue(all(not (self.root / path).exists() for path in demo.OUTPUT_PATHS))
        self.assertTrue((self.root / demo.WORLD_OUTPUT).is_file())

    def test_partial_in_place_write_failure_restores_original_bytes(self):
        self.edit(lambda data: data["boss"].update(reward_xp=31))
        compilation = demo.compile_content(self.root)

        def fail(protected, raw):
            protected.write(raw[:31])
            raise OSError("injected failure after partial write")

        with patch.object(sync, "_install_output", side_effect=fail), self.assertRaisesRegex(OSError, "partial write"):
            sync.write_outputs(self.root, compilation)
        self.assert_outputs_equal()
        self.assertFalse((self.root / demo.DATA / ".compile.lock").exists())

    def test_source_changed_before_protection_is_preserved(self):
        self.edit(lambda data: data["boss"].update(reward_xp=31))
        compilation = demo.compile_content(self.root)
        source = self.root / demo.CONTENT_SOURCE
        changed = source.read_bytes() + b"\n"
        source.write_bytes(changed)
        with self.assertRaisesRegex(demo.ContentError, "InputsChanged"):
            sync.write_outputs(self.root, compilation)
        self.assert_outputs_equal()
        self.assertEqual(source.read_bytes(), changed)

    def test_edit_between_snapshot_and_handle_acquisition_is_preserved(self):
        self.edit(lambda data: data["boss"].update(reward_xp=31))
        compilation = demo.compile_content(self.root)
        open_output = sync._open_output
        external = b"external owner edit\n"

        def interrupt(path, original):
            if path == self.root / demo.CONTENT_CONTRACT:
                path.write_bytes(external)
            return open_output(path, original)

        with patch.object(sync, "_open_output", side_effect=interrupt), self.assertRaisesRegex(demo.ContentError, "ConcurrentEdit"):
            sync.write_outputs(self.root, compilation)
        self.assertEqual((self.root / demo.CONTENT_CONTRACT).read_bytes(), external)
        self.assertEqual((self.root / demo.CONTENT_OUTPUT).read_bytes(), self.baseline.outputs[demo.CONTENT_OUTPUT])
        self.assertEqual((self.root / demo.MANIFEST_OUTPUT).read_bytes(), self.baseline.outputs[demo.MANIFEST_OUTPUT])

    def test_actual_editor_save_between_comparison_and_first_write_is_blocked(self):
        self.edit(lambda data: data["boss"].update(reward_xp=31))
        compilation = demo.compile_content(self.root)
        install = sync._install_output
        candidate = self.root / "editor-candidate.tmp"
        external = b"external editor save"
        candidate.write_bytes(external)
        attempts = []

        def interrupt(protected, raw):
            if not attempts:
                with self.assertRaises(OSError):
                    os.replace(candidate, protected.path)
                with self.assertRaises(OSError):
                    protected.path.write_bytes(external)
                attempts.append(protected.path)
            install(protected, raw)

        with patch.object(sync, "_install_output", side_effect=interrupt):
            sync.write_outputs(self.root, compilation)
        self.assertEqual(attempts, [self.root / demo.CONTENT_OUTPUT])
        self.assertEqual(candidate.read_bytes(), external)
        self.assert_outputs_equal(compilation.outputs)

    def test_other_process_cannot_edit_first_output_during_second_write_or_inputs(self):
        self.edit(lambda data: data["boss"].update(reward_xp=31))
        compilation = demo.compile_content(self.root)
        install, count = sync._install_output, 0
        candidate = self.root / "editor-candidate.tmp"
        external = b"external editor save"
        candidate.write_bytes(external)
        observations = []
        script = (
            "import json,os,sys\n"
            "from pathlib import Path\n"
            "target,source,candidate=map(Path,sys.argv[1:])\n"
            "result={}\n"
            "for name,operation in [('overwrite',lambda:target.write_bytes(b'editor')), "
            "('replace',lambda:os.replace(candidate,target)), ('input',lambda:source.write_bytes(b'editor'))]:\n"
            " try:\n"
            "  operation(); result[name]='allowed'\n"
            " except OSError as error:\n"
            "  result[name]=[error.errno,getattr(error,'winerror',None)]\n"
            "print(json.dumps(result))\n"
        )

        def interrupt(protected, raw):
            nonlocal count
            count += 1
            if count == 2:
                process = subprocess.run(
                    [sys.executable, "-B", "-c", script, str(self.root / demo.CONTENT_OUTPUT),
                     str(self.root / demo.CONTENT_SOURCE), str(candidate)],
                    capture_output=True, text=True, check=True, timeout=30)
                observations.append(json.loads(process.stdout))
                self.assertEqual((self.root / demo.CONTENT_OUTPUT).read_bytes(), compilation.outputs[demo.CONTENT_OUTPUT])
            install(protected, raw)

        with patch.object(sync, "_install_output", side_effect=interrupt):
            sync.write_outputs(self.root, compilation)
        self.assertEqual(len(observations), 1)
        for operation in ("overwrite", "replace", "input"):
            self.assertIsInstance(observations[0][operation], list)
            self.assertTrue(observations[0][operation][0] or observations[0][operation][1])
        self.assertEqual(candidate.read_bytes(), external)
        self.assertEqual((self.root / demo.CONTENT_SOURCE).read_bytes(), compilation.inputs[demo.CONTENT_SOURCE])
        self.assert_outputs_equal(compilation.outputs)

    def test_skipped_output_is_protected_and_verified_at_every_final_checkpoint(self):
        path = self.root / demo.SCHEMA_SOURCE
        path.write_bytes(path.read_bytes() + b"\n")
        compilation = demo.compile_content(self.root)
        self.assertEqual(compilation.outputs[demo.CONTENT_OUTPUT], self.baseline.outputs[demo.CONTENT_OUTPUT])
        install, verify = sync._install_output, sync._verify_protected
        checkpoints = []

        def interrupt(protected, raw):
            with self.assertRaises(OSError):
                (self.root / demo.CONTENT_OUTPUT).write_bytes(b"external edit to skipped output")
            install(protected, raw)

        def record(inputs, outputs, result, conflicts):
            checkpoints.append((set(inputs), set(outputs)))
            verify(inputs, outputs, result, conflicts)

        with patch.object(sync, "_install_output", side_effect=interrupt), patch.object(sync, "_verify_protected", side_effect=record):
            sync.write_outputs(self.root, compilation)
        self.assertEqual(len(checkpoints), 3)
        for inputs, outputs in checkpoints:
            self.assertEqual(inputs, set(compilation.inputs))
            self.assertEqual(outputs, set(demo.OUTPUT_PATHS))
        self.assert_outputs_equal(compilation.outputs)

    def test_protection_covers_cleanup_and_unlock_until_commit_point(self):
        self.edit(lambda data: data["boss"].update(reward_xp=31))
        compilation = demo.compile_content(self.root)
        unlock = sync._remove_lock
        attempts = []

        def interrupt(protected):
            unlock(protected)
            for relative in (demo.CONTENT_OUTPUT, demo.MANIFEST_OUTPUT, demo.CONTENT_SOURCE):
                with self.assertRaises(OSError):
                    (self.root / relative).write_bytes(b"late edit")
                attempts.append(relative)

        with patch.object(sync, "_remove_lock", side_effect=interrupt):
            sync.write_outputs(self.root, compilation)
        self.assertEqual(len(attempts), 3)
        self.assert_outputs_equal(compilation.outputs)
        (self.root / demo.CONTENT_OUTPUT).write_bytes(b"legitimate edit after the commit point")
        self.assertEqual((self.root / demo.CONTENT_OUTPUT).read_bytes(), b"legitimate edit after the commit point")

    def test_rollback_handle_excludes_editor_saves(self):
        self.edit(lambda data: data["boss"].update(reward_xp=31))
        compilation = demo.compile_content(self.root)
        install, restore, count = sync._install_output, sync._restore_output, 0
        candidate = self.root / "editor-rollback.tmp"
        candidate.write_bytes(b"external rollback-time save")
        attempts = []

        def fail(protected, raw):
            nonlocal count
            count += 1
            if count == 2:
                raise OSError("injected commit failure")
            install(protected, raw)

        def interrupt(protected, raw):
            with self.assertRaises(OSError):
                os.replace(candidate, protected.path)
            attempts.append(protected.path)
            restore(protected, raw)

        with patch.object(sync, "_install_output", side_effect=fail), patch.object(sync, "_restore_output", side_effect=interrupt):
            with self.assertRaisesRegex(OSError, "injected"):
                sync.write_outputs(self.root, compilation)
        self.assertTrue(attempts)
        self.assertEqual(candidate.read_bytes(), b"external rollback-time save")
        self.assert_outputs_equal()

    def test_existing_writer_handle_causes_refusal_before_any_install(self):
        self.edit(lambda data: data["boss"].update(reward_xp=31))
        compilation = demo.compile_content(self.root)
        with (self.root / demo.CONTENT_OUTPUT).open("r+b"):
            with patch.object(sync, "_install_output", side_effect=AssertionError("installed despite a live writer")):
                with self.assertRaises(OSError):
                    sync.write_outputs(self.root, compilation)
        self.assert_outputs_equal()

    def test_descriptor_allocation_failure_removes_only_its_new_file(self):
        import msvcrt

        target = self.root / "descriptor-failure.json"
        with patch.object(msvcrt, "open_osfhandle", side_effect=OSError("descriptor allocation failed")):
            with self.assertRaisesRegex(OSError, "descriptor allocation"):
                sync._ProtectedFile(target, writable=True, create=True)
        self.assertFalse(target.exists())
        self.assert_outputs_equal()

    def assert_recovery_originals(self, expected=None, path=None):
        expected = self.baseline.outputs if expected is None else expected
        path = self.root / demo.DATA / ".compile.lock" if path is None else path
        journal = json.loads(path.read_bytes())
        self.assertEqual(journal["format"], "demo-content-recovery-1")
        for relative, raw in expected.items():
            self.assertEqual(base64.b64decode(journal["originals"][str(relative)]), raw, str(relative))
        return journal

    def test_final_verification_detects_previous_and_skipped_conflicts_without_overwrite(self):
        for skipped in (False, True):
            with self.subTest(skipped=skipped):
                if (self.root / demo.DATA / ".compile.lock").exists():
                    (self.root / demo.DATA / ".compile.lock").unlink()
                for relative, raw in self.baseline.outputs.items():
                    (self.root / relative).write_bytes(raw)
                if skipped:
                    path = self.root / demo.SCHEMA_SOURCE
                    path.write_bytes(path.read_bytes() + b"\n")
                    (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
                else:
                    self.edit(lambda data: data["boss"].update(reward_xp=31))
                compilation = demo.compile_content(self.root)
                open_output, cleanup = sync._open_output, sync._remove_transaction
                held = {}
                external = b"unexpected bytes supplied by a faulty protection/storage provider"

                def capture(path, original):
                    held[path] = open_output(path, original)
                    return held[path]

                def corrupt(transaction):
                    cleanup(transaction)
                    # Ordinary editor writes are blocked by Windows. This injects
                    # a broken provider via its own handle to test the final check.
                    held[self.root / demo.CONTENT_OUTPUT].write(external)

                with patch.object(sync, "_open_output", side_effect=capture), patch.object(sync, "_remove_transaction", side_effect=corrupt):
                    with self.assertRaisesRegex(demo.ContentError, "RecoveryRequired"):
                        sync.write_outputs(self.root, compilation)
                self.assertEqual((self.root / demo.CONTENT_OUTPUT).read_bytes(), external)
                self.assertEqual((self.root / demo.CONTENT_CONTRACT).read_bytes(), self.baseline.outputs[demo.CONTENT_CONTRACT])
                self.assertEqual((self.root / demo.MANIFEST_OUTPUT).read_bytes(), self.baseline.outputs[demo.MANIFEST_OUTPUT])
                journal = self.assert_recovery_originals()
                self.assertEqual(base64.b64decode(journal["conflicts"][str(demo.CONTENT_OUTPUT)]), external)

    def test_backup_cleanup_failure_retains_every_original_and_rolls_back(self):
        self.edit(lambda data: data["boss"].update(reward_xp=31))
        compilation = demo.compile_content(self.root)

        def fail(transaction):
            (transaction / "old-0").unlink()
            raise OSError("injected backup cleanup failure")

        with patch.object(sync, "_remove_transaction", side_effect=fail), self.assertRaisesRegex(demo.ContentError, "RecoveryRequired"):
            sync.write_outputs(self.root, compilation)
        self.assert_outputs_equal()
        self.assertEqual(self.assert_recovery_originals()["phase"], "rolled_back_cleanup_failed")

    def test_cleanup_error_after_all_backup_files_deleted_keeps_originals(self):
        self.edit(lambda data: data["boss"].update(reward_xp=31))
        compilation = demo.compile_content(self.root)
        cleanup = sync._remove_transaction

        def fail(transaction):
            cleanup(transaction)
            raise OSError("injected error after backup deletion")

        with patch.object(sync, "_remove_transaction", side_effect=fail), self.assertRaisesRegex(demo.ContentError, "RecoveryRequired"):
            sync.write_outputs(self.root, compilation)
        self.assert_outputs_equal()
        self.assertFalse(list((self.root / "Artifacts").rglob("transaction-*")))
        self.assert_recovery_originals()

    def test_lock_removal_failure_restores_outputs_even_after_backup_cleanup(self):
        self.edit(lambda data: data["boss"].update(reward_xp=31))
        compilation = demo.compile_content(self.root)
        with patch.object(sync, "_remove_lock", side_effect=OSError("injected lock removal failure")):
            with self.assertRaisesRegex(demo.ContentError, "RecoveryRequired"):
                sync.write_outputs(self.root, compilation)
        self.assert_outputs_equal()
        self.assertFalse(list((self.root / "Artifacts").rglob("transaction-*")))
        self.assert_recovery_originals()

    def test_actual_windows_readonly_lock_cleanup_failure_is_recoverable(self):
        self.edit(lambda data: data["boss"].update(reward_xp=31))
        compilation = demo.compile_content(self.root)
        unlock = sync._remove_lock
        lock_path = self.root / demo.DATA / ".compile.lock"

        def refuse(protected):
            protected.path.chmod(stat.S_IREAD)
            unlock(protected)

        try:
            with patch.object(sync, "_remove_lock", side_effect=refuse), self.assertRaisesRegex(demo.ContentError, "RecoveryRequired"):
                sync.write_outputs(self.root, compilation)
            self.assert_outputs_equal()
            self.assert_recovery_originals()
        finally:
            if lock_path.exists():
                lock_path.chmod(stat.S_IREAD | stat.S_IWRITE)

    def test_error_after_unlock_recreates_recovery_journal_before_releasing_outputs(self):
        self.edit(lambda data: data["boss"].update(reward_xp=31))
        compilation = demo.compile_content(self.root)
        unlock = sync._remove_lock

        def fail(protected):
            unlock(protected)
            raise OSError("injected error after lock removal")

        with patch.object(sync, "_remove_lock", side_effect=fail), self.assertRaisesRegex(demo.ContentError, "RecoveryRequired"):
            sync.write_outputs(self.root, compilation)
        self.assert_outputs_equal()
        self.assert_recovery_originals()

    def test_retained_journal_blocks_identical_noop_retry_and_check(self):
        with patch.object(sync, "_remove_lock", side_effect=OSError("no-op lock cleanup failure")):
            with self.assertRaisesRegex(demo.ContentError, "RecoveryRequired"):
                sync.write_outputs(self.root, self.baseline)
        self.assert_outputs_equal()
        self.assert_recovery_originals()
        with self.assertRaisesRegex(demo.ContentError, "CompilerBusy"):
            sync.write_outputs(self.root, self.baseline)
        with self.assertRaisesRegex(demo.ContentError, "CompilerBusy"):
            sync.synchronize(self.root, check=True)

    def test_backup_recovery_record_survives_unavailable_primary_lock(self):
        self.edit(lambda data: data["boss"].update(reward_xp=31))
        compilation = demo.compile_content(self.root)
        unlock = sync._remove_lock

        def fail(protected):
            unlock(protected)
            raise OSError("injected post-unlock error")

        with patch.object(sync, "_remove_lock", side_effect=fail), \
                patch.object(sync, "_keep_recovery", side_effect=OSError("cannot reclaim primary lock")):
            with self.assertRaisesRegex(demo.ContentError, "RecoveryRequired") as caught:
                sync.write_outputs(self.root, compilation)
        record = Path(caught.exception.path)
        self.assertTrue(record.name.startswith("recovery-"))
        self.assert_recovery_originals(path=record)
        self.assert_outputs_equal()
        self.assertFalse((self.root / demo.DATA / ".compile.lock").exists())
        with self.assertRaisesRegex(demo.ContentError, "CompilerBusy"):
            sync.write_outputs(self.root, self.baseline)
        with self.assertRaisesRegex(demo.ContentError, "CompilerBusy"):
            sync.synchronize(self.root, check=True)

    def test_handle_cleanup_failure_is_explicitly_committed_and_releases_handles(self):
        self.edit(lambda data: data["boss"].update(reward_xp=31))
        compilation = demo.compile_content(self.root)
        close = sync._ProtectedFile.close
        failed = False

        def fail_once(protected):
            nonlocal failed
            if protected.path == self.root / demo.CONTENT_OUTPUT and protected.fd is not None and not failed:
                failed = True
                raise OSError("injected close failure")
            close(protected)

        with patch.object(sync._ProtectedFile, "close", new=fail_once):
            with self.assertRaisesRegex(demo.ContentError, "committed=True"):
                sync.write_outputs(self.root, compilation)
        self.assert_outputs_equal(compilation.outputs)
        self.assertEqual(self.assert_recovery_originals()["phase"], "committed_handle_cleanup_failed")
        with (self.root / demo.CONTENT_OUTPUT).open("r+b") as stream:
            self.assertEqual(stream.read(), compilation.outputs[demo.CONTENT_OUTPUT])
        with self.assertRaisesRegex(demo.ContentError, "CompilerBusy"):
            sync.write_outputs(self.root, compilation)

    def test_posix_rename_cannot_displace_a_protected_file(self):
        from ctypes import wintypes

        target = self.root / "rename-target.json"
        candidate = self.root / "rename-candidate.tmp"
        target.write_bytes(b"original")
        candidate.write_bytes(b"external candidate")
        protected = sync._ProtectedFile(target, writable=True)
        api = sync._windows_api()
        handle = api.CreateFileW(str(candidate), 0x80010000, 7, None, 3, 0x80, None)
        self.assertNotEqual(handle, ctypes.c_void_p(-1).value)
        name = str(target)

        class RenameInfo(ctypes.Structure):
            _fields_ = [("Flags", wintypes.DWORD), ("RootDirectory", wintypes.HANDLE),
                        ("FileNameLength", wintypes.DWORD), ("FileName", wintypes.WCHAR * (len(name) + 1))]

        info = RenameInfo(3, None, len(name.encode("utf-16-le")), name)
        try:
            self.assertFalse(api.SetFileInformationByHandle(handle, 22, ctypes.byref(info), ctypes.sizeof(info)))
            self.assertEqual(ctypes.get_last_error(), 32)
            self.assertEqual(target.read_bytes(), b"original")
            protected.close()
            self.assertTrue(api.SetFileInformationByHandle(handle, 22, ctypes.byref(info), ctypes.sizeof(info)))
            self.assertTrue(api.CloseHandle(handle))
            handle = None
            self.assertEqual(target.read_bytes(), b"external candidate")
        finally:
            if handle is not None:
                api.CloseHandle(handle)
            protected.close()

    def test_cleanup_plus_rollback_failure_blocks_retry_even_when_new_bytes_match(self):
        self.edit(lambda data: data["boss"].update(reward_xp=31))
        compilation = demo.compile_content(self.root)
        with patch.object(sync, "_remove_lock", side_effect=OSError("unlock failure")), \
                patch.object(sync, "_restore_output", side_effect=OSError("rollback device failure")):
            with self.assertRaisesRegex(demo.ContentError, "RecoveryRequired"):
                sync.write_outputs(self.root, compilation)
        self.assert_outputs_equal(compilation.outputs)
        self.assert_recovery_originals()
        with self.assertRaisesRegex(demo.ContentError, "CompilerBusy"):
            sync.write_outputs(self.root, compilation)

    def test_unrecoverable_io_preserves_originals_and_blocks_another_generation(self):
        self.edit(lambda data: data["boss"].update(reward_xp=31))
        compilation = demo.compile_content(self.root)
        install, count = sync._install_output, 0

        def fail(protected, raw):
            nonlocal count
            count += 1
            if count == 2:
                raise OSError("device refused replacement")
            install(protected, raw)

        with patch.object(sync, "_install_output", side_effect=fail), \
                patch.object(sync, "_restore_output", side_effect=OSError("rollback refused")):
            with self.assertRaisesRegex(demo.ContentError, "RecoveryRequired") as caught:
                sync.write_outputs(self.root, compilation)
        recovery = Path(caught.exception.path)
        self.assertTrue(recovery.is_file())
        self.assert_recovery_originals()
        self.assertTrue((self.root / demo.DATA / ".compile.lock").exists())
        with self.assertRaisesRegex(demo.ContentError, "CompilerBusy"):
            sync.write_outputs(self.root, compilation)
        with self.assertRaises(demo.ContentError):
            demo.verify_runtime_files(self.root, self.baseline.digest)


if __name__ == "__main__":
    unittest.main()
