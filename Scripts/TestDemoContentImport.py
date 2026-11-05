"""Behavioral refusal/projection/integrity tests; all content is synthetic and test-only."""
from contextlib import redirect_stderr, redirect_stdout
from copy import deepcopy
import base64
import ctypes
from decimal import Decimal, localcontext
from fractions import Fraction
import io
import json
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
        for identity in ("active_arc_bolt", "x" * 63):
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
        self.assertEqual(result.catalog.definition("item_iron_plate")["sell_gold"], 25)
        self.assertEqual(result.catalog.data["progression"]["xp_to_next"], (50, 60, 70, 80, 90, 100, 110, 120, 130))
        self.assertEqual(result.catalog.definition("monster_guardian")["attack_interval_ms"], 1334)
        self.assertEqual(result.catalog.definition("character_demo")["basic_attack"]["interval_ms"], 1000)
        self.assertEqual(result.catalog.definition("active_arc_bolt")["native_id"], 62)
        projected = demo.parse_json(result.outputs[demo.CONTENT_OUTPUT], "output")
        demo.validate_integer_tree(projected, "output")
        with self.assertRaises(TypeError):
            result.catalog.data["character"]["id"] = "mutated"
        with self.assertRaises(TypeError):
            result.catalog.definition("active_arc_bolt")["ranks"][0]["effects"][0]["base"]["p2"] = 1
        with self.assertRaisesRegex(demo.ContentError, "UnknownId"):
            result.catalog.definition("arc_bolt")
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
        self.assertEqual(fields["content.abilities[1].ranks[0].effects[0].base.value"],
                         ("content.abilities[3].ranks[0].effects[0].base.p2",))

    def test_each_rank_projects_all_effects_costs_and_coefficients(self):
        arc = self.baseline.catalog.definition("active_arc_bolt")
        guard = self.baseline.catalog.definition("active_guard_pulse")
        snare = self.baseline.catalog.definition("active_binding_snare")
        for rank in range(1, 4):
            with self.subTest(rank=rank):
                row = arc["ranks"][rank - 1]
                self.assertEqual(row["effects"][0]["base"]["p2"], 800 * rank)
                self.assertEqual(row["effects"][0]["ap_ratio"]["bp"], 1250 * rank)
                self.assertEqual(row["effects"][0]["opcode"], 1)
                self.assertEqual(row["mana_cost"]["p2"], (5 + rank) * 100)
                self.assertEqual(row["cooldown"]["ms"], 3000)
                self.assertEqual(guard["ranks"][rank - 1]["effects"][0]["base"]["p2"], 2000 * rank)
                self.assertEqual(guard["ranks"][rank - 1]["effects"][1]["opcode"], 3)
                self.assertEqual(snare["ranks"][rank - 1]["effects"][1]["duration"]["ms"], 250 * rank)
        battery = self.baseline.catalog.definition("item_arcane_battery")["active"]["rank"]
        self.assertEqual([row["opcode"] for row in battery["effects"]], [9, 2])
        self.assertEqual(battery["effects"][0]["ratio"]["bp"], 2000)

    def test_mutating_each_active_rank_field_changes_projected_value(self):
        baseline_bytes = (self.root / demo.CONTENT_SOURCE).read_bytes()
        changes = 0
        for ability_index in range(3):
            for rank_index in range(3):
                for field, amount, output_field in (("mana_cost", 1, "p2"), ("cooldown", 1, "ms")):
                    with self.subTest(ability=ability_index, rank=rank_index, field=field):
                        (self.root / demo.CONTENT_SOURCE).write_bytes(baseline_bytes)
                        self.edit(lambda data: data["abilities"][ability_index]["ranks"][rank_index][field].update(
                            value=data["abilities"][ability_index]["ranks"][rank_index][field]["value"] + amount))
                        result = demo.compile_content(self.root)
                        identity = self.author["abilities"][ability_index]["id"]
                        old = self.baseline.catalog.definition(identity)["ranks"][rank_index][field][output_field]
                        new = result.catalog.definition(identity)["ranks"][rank_index][field][output_field]
                        self.assertEqual(new - old, 100 if output_field == "p2" else 1000)
                        changes += 1
        self.assertEqual(changes, 18)
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
        for key in ("abilities", "attributes", "equipment", "display"):
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
        self.edit(lambda data: data["parameters"].update({"REWARD.PlayerGold.Base": expected}))
        result = demo.compile_content(self.root)
        self.assertEqual(result.catalog.parameter("REWARD.PlayerGold.Base"), expected)
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
        self.edit(lambda source: next(row for row in source["anchors"] if row["id"] == "s00")["position_cm"].update(x=4401),
                  demo.WORLD_SOURCE)
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

    def test_bad_counts_revisions_ids_and_ranks_never_accept_bool(self):
        mutations = (
            lambda data: data["identity"].update(schema_version=True),
            lambda data: data["parameters"].update({"SHOP.UpgradeCost": True}),
            lambda data: data["equipment"][0].update(buy_price=True),
            lambda data: data["abilities"][0]["ranks"][0].update(rank=True),
            lambda data: data["camps"]["monsters"][0].update(count_per_camp=True),
            lambda data: data["progression"].update(max_level=True),
            lambda data: data["parameters"].update({"SHOP.UpgradeCost": 1.0}),
            lambda data: data["abilities"][0].update(draftable=1),
        )
        for mutation in mutations:
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation)

    def test_integer_overflow_and_checked_multiply_refuse(self):
        mutations = (
            lambda data: data["equipment"][0].update(buy_price=demo.INT64_MAX),
            lambda data: data["parameters"].update({"XP.BaseToNext": demo.INT64_MAX}),
            lambda data: data["parameters"].update({"REWARD.PlayerGold.PerLevel": demo.INT64_MAX}),
            lambda data: data["parameters"].update({"SHOP.StartingGold": demo.INT64_MAX + 1}),
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
            lambda data: data["parameters"]["LIFE.RespawnSec"].update(value=0.0001),
            lambda data: data["parameters"]["LIFE.RespawnSec"].update(value=0),
            lambda data: data["parameters"]["AI.DecisionIntervalSec"].update(value=-1),
            lambda data: data["parameters"]["STAT.BaseAttackSpeed"].update(value=0),
            lambda data: data["abilities"][0]["target"].update(projectile_speed={"unit": "cm/s", "value": -1}),
            lambda data: data["abilities"][0]["ranks"][0]["mana_cost"].update(value=-1),
        )
        for mutation in mutations:
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation)

    def test_extra_fields_at_root_nested_rank_and_effect_refuse(self):
        for mutation in (
            lambda data: data.update(candidate_mode=True),
            lambda data: data["parameters"].update({"CAMP.Low.AggroRadiusCm": {"unit": "cm", "value": 0}}),
            lambda data: data["abilities"][0]["ranks"][0].update(extra=1),
            lambda data: data["abilities"][0]["ranks"][0]["effects"][0].update(hidden_legacy_fallback=True),
            lambda data: data["equipment"][5]["active"].update(range={"unit": "cm", "value": 120}),
            lambda data: data["character"]["dash"].update(windup={"unit": "s", "value": 0}),
        ):
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation)

    def test_unknown_duplicate_retired_ids_and_missing_handlers_refuse(self):
        for mutation in (
            lambda data: data["abilities"][0].update(id="unknown_ability"),
            lambda data: data["abilities"][0].update(id="arc_bolt"),
            lambda data: data["equipment"][0].update(id="ItemIronPlate"),
            lambda data: data["equipment"][0].update(id=data["equipment"][1]["id"]),
            lambda data: data["abilities"][0]["ranks"][0]["effects"][0].update(op="legacy_damage"),
            lambda data: data["abilities"][0]["ranks"][0]["effects"].append(deepcopy(data["abilities"][0]["ranks"][0]["effects"][0])),
            lambda data: data["abilities"][1]["ranks"][0]["effects"].pop(),
        ):
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation)

    def test_native_id_tampering_and_tombstone_reuse_refuse(self):
        original = (self.root / demo.IDS_SOURCE).read_bytes()
        for mutation in (
            lambda data: data["abilities"].update(active_arc_bolt=True),
            lambda data: data["abilities"].update(active_arc_bolt=61),
            lambda data: data["abilities"].update(active_arc_bolt=63),
            lambda data: data["abilities"].update(active_arc_bolt=200),
            lambda data: data["retired_ranges"].update(abilities=[0, 60]),
            lambda data: data["effect_ops"].pop("shield"),
        ):
            (self.root / demo.IDS_SOURCE).write_bytes(original)
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation, relative=demo.IDS_SOURCE)
        (self.root / demo.IDS_SOURCE).write_bytes(original)
        path = self.root / demo.NATIVE_IDS
        # An unused number: a renumbering, not a collision with another declared identity.
        path.write_bytes(path.read_bytes().replace(b"ActiveArcBolt=62", b"ActiveArcBolt=200"))
        with self.assertRaisesRegex(ValueError, "NativeId"):
            demo.compile_content(self.root)

    def test_missing_wrong_or_reordered_ranks_refuse(self):
        for mutation in (
            lambda data: data["abilities"][0]["ranks"].pop(),
            lambda data: data["abilities"][0]["ranks"].reverse(),
            lambda data: data["abilities"][0]["ranks"][1].update(rank=1),
            lambda data: data["abilities"][0]["ranks"][2].pop("mana_cost"),
            lambda data: data["equipment"][5]["active"]["rank"].update(rank=2),
        ):
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation)

    def test_invalid_targets_families_and_passive_binding_refuse(self):
        for mutation in (
            lambda data: data["abilities"][0].update(target={"mode": "Self"}),
            lambda data: data["abilities"][0].update(cooldown_family="unknown_family"),
            lambda data: data["equipment"][3]["passive"].update(family="passive_cleave"),
            lambda data: data["abilities"][3]["ranks"][0]["effects"][0].update(stats={"ADFlat": {"unit": "points", "value": 1}}),
            lambda data: data["equipment"][5].update(active=None),
            lambda data: data["equipment"][0].update(active=deepcopy(data["equipment"][5]["active"])),
            lambda data: data["equipment"][4].update(passive=None),
            lambda data: data["equipment"][4]["passive"].update(family="passive_cleave"),
            lambda data: data["equipment"][4]["passive"]["effects"][0].update(cooldown_ratio={"unit": "ratio", "value": 0}),
            lambda data: data["equipment"][4]["passive"]["effects"][0].update(
                cooldown_ratio={"unit": "ratio", "value": 1}, distance_bonus={"unit": "ratio", "value": 0}),
            lambda data: data["character"].pop("dash"),
            lambda data: data["character"]["dash"].update(distance={"unit": "cm", "value": 0}),
            lambda data: data["character"]["dash"].update(duration=deepcopy(data["character"]["dash"]["cooldown"])),
        ):
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation)

    def test_known_but_undeclared_stats_and_penalties_refuse(self):
        for mutation in (
            lambda data: data["attributes"][0].update(stats={"CritChanceBonusRatio": {"unit": "ratio", "value": 0.5}}),
            lambda data: data["equipment"][0]["stats"].update(APFlat={"unit": "points", "value": 1}),
            lambda data: data["attributes"][0].update(penalties={"MaxHPFlat": {"unit": "hp", "value": 5}}),
            lambda data: data["attributes"][5].pop("penalties"),
            lambda data: data["attributes"][5].update(penalties={"MaxHPFlat": {"unit": "hp", "value": 5}}),
            lambda data: data["attributes"][5]["penalties"]["AttackRangeCmFlat"].update(value=0),
        ):
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(mutation=mutation):
                self.assert_refused(mutation, "UnknownField|IneffectiveChoice")

    def test_capped_gains_clamp_instead_of_refusing(self):
        # Every attribute clamps to its bounds in the projection (#112 §4): a source sum past a cap still compiles.
        for mutation in (
            lambda data: data["parameters"]["STAT.AD.Max"].update(value=30),
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

    def test_opening_offer_and_out_of_combat_recovery_refuse_invalid_inputs(self):
        for mutation, code in (
            (lambda data: data["progression"]["opening_offer"].update(active_count=3), "OpeningOffer"),
            (lambda data: data["progression"]["opening_offer"].update(active_count=1), "OpeningOffer"),
            (lambda data: data["progression"]["opening_offer"].update(active_count=0, passive_count=3), None),
            (lambda data: data["progression"]["opening_offer"].update(attribute_count=1), "UnknownField"),
            (lambda data: data["progression"]["opening_offer"].pop("passive_count"), "MissingField"),
            # Only draftable skills fill the opening: one draftable active cannot fill two active cards.
            (lambda data: [row.update(draftable=False) for row in data["abilities"] if row["id"] != "active_arc_bolt"
                           and row["kind"] == "active"], "OpeningOffer"),
            (lambda data: data["progression"].update(reroll_levels=[7, 4]), "RerollLevels"),
            (lambda data: data["progression"].update(reroll_levels=[4, 4]), "RerollLevels"),
            (lambda data: data["progression"].update(reroll_levels=[11]), "NumericOverflow"),
            (lambda data: data["progression"].pop("reroll_levels"), "MissingField"),
            (lambda data: data["abilities"][0].update(tags=[]), "ArrayLength"),
            (lambda data: data["abilities"][0].update(tags=["Mobility", "Mobility"]), "MechanicTags"),
            (lambda data: data["abilities"][0].update(tags=["Burst"]), "UnknownEnum|SchemaVariant"),
            (lambda data: data["equipment"][0].update(tags=["Tank", "Shield", "Breach", "Mobility"]), "ArrayLength"),
            (lambda data: data["equipment"][0].pop("tags"), "MissingField"),
            (lambda data: data["abilities"][0].pop("draftable"), "MissingField|SchemaVariant"),
            (lambda data: data["parameters"]["REGEN.OutOfCombatMaxHPRatioPerSec"].update(value=0), "NumericDomain"),
            (lambda data: data["parameters"]["REGEN.OutOfCombatMaxMPRatioPerSec"].update(value=0.0155), "NumericDomain"),
            (lambda data: data["parameters"]["REGEN.OutOfCombatDelaySec"].update(value=0), "NumericDomain"),
            (lambda data: data["parameters"].pop("REGEN.OutOfCombatDelaySec"), "MissingField"),
        ):
            (self.root / demo.CONTENT_SOURCE).write_bytes(encode(self.author))
            with self.subTest(mutation=mutation):
                if code is None:
                    # Three draftable passives exist in the fixture, so an all-passive opening is a valid composition.
                    changed = deepcopy(self.author)
                    mutation(changed)
                    (self.root / demo.CONTENT_SOURCE).write_bytes(encode(changed))
                    demo.compile_author(self.root)
                else:
                    self.assert_refused(mutation, code)

    def test_build_bounds_zero_gain_arbitrage_and_xp_consistency_refuse(self):
        for mutation in (
            lambda data: data["parameters"]["STAT.MaxHP.Max"].update(value=150),
            lambda data: data["parameters"]["STAT.MaxHP.Min"].update(value=200),
            lambda data: data["attributes"][0]["stats"]["ADFlat"].update(value=0),
            lambda data: data["parameters"]["SHOP.SellRatio"].update(value=1),
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
            (lambda data: data["display"][1].update(description_text="说" * 161), "StringLength"),
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
        self.edit(lambda data: data["equipment"][0].update(buy_price=53))
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
        self.edit(lambda data: data["equipment"][0].update(buy_price=53))
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
        self.edit(lambda data: data["equipment"][0].update(buy_price=53))
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
        self.edit(lambda data: data["equipment"][0].update(buy_price=53))
        compilation = demo.compile_content(self.root)

        def fail(protected, raw):
            protected.write(raw[:31])
            raise OSError("injected failure after partial write")

        with patch.object(sync, "_install_output", side_effect=fail), self.assertRaisesRegex(OSError, "partial write"):
            sync.write_outputs(self.root, compilation)
        self.assert_outputs_equal()
        self.assertFalse((self.root / demo.DATA / ".compile.lock").exists())

    def test_source_changed_before_protection_is_preserved(self):
        self.edit(lambda data: data["equipment"][0].update(buy_price=53))
        compilation = demo.compile_content(self.root)
        source = self.root / demo.CONTENT_SOURCE
        changed = source.read_bytes() + b"\n"
        source.write_bytes(changed)
        with self.assertRaisesRegex(demo.ContentError, "InputsChanged"):
            sync.write_outputs(self.root, compilation)
        self.assert_outputs_equal()
        self.assertEqual(source.read_bytes(), changed)

    def test_edit_between_snapshot_and_handle_acquisition_is_preserved(self):
        self.edit(lambda data: data["equipment"][0].update(buy_price=53))
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
        self.edit(lambda data: data["equipment"][0].update(buy_price=53))
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
        self.edit(lambda data: data["equipment"][0].update(buy_price=53))
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
        self.edit(lambda data: data["equipment"][0].update(buy_price=53))
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
        self.edit(lambda data: data["equipment"][0].update(buy_price=53))
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
        self.edit(lambda data: data["equipment"][0].update(buy_price=53))
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
                    self.edit(lambda data: data["equipment"][0].update(buy_price=53))
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
        self.edit(lambda data: data["equipment"][0].update(buy_price=53))
        compilation = demo.compile_content(self.root)

        def fail(transaction):
            (transaction / "old-0").unlink()
            raise OSError("injected backup cleanup failure")

        with patch.object(sync, "_remove_transaction", side_effect=fail), self.assertRaisesRegex(demo.ContentError, "RecoveryRequired"):
            sync.write_outputs(self.root, compilation)
        self.assert_outputs_equal()
        self.assertEqual(self.assert_recovery_originals()["phase"], "rolled_back_cleanup_failed")

    def test_cleanup_error_after_all_backup_files_deleted_keeps_originals(self):
        self.edit(lambda data: data["equipment"][0].update(buy_price=53))
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
        self.edit(lambda data: data["equipment"][0].update(buy_price=53))
        compilation = demo.compile_content(self.root)
        with patch.object(sync, "_remove_lock", side_effect=OSError("injected lock removal failure")):
            with self.assertRaisesRegex(demo.ContentError, "RecoveryRequired"):
                sync.write_outputs(self.root, compilation)
        self.assert_outputs_equal()
        self.assertFalse(list((self.root / "Artifacts").rglob("transaction-*")))
        self.assert_recovery_originals()

    def test_actual_windows_readonly_lock_cleanup_failure_is_recoverable(self):
        self.edit(lambda data: data["equipment"][0].update(buy_price=53))
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
        self.edit(lambda data: data["equipment"][0].update(buy_price=53))
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
        self.edit(lambda data: data["equipment"][0].update(buy_price=53))
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
        self.edit(lambda data: data["equipment"][0].update(buy_price=53))
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
        self.edit(lambda data: data["equipment"][0].update(buy_price=53))
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
        self.edit(lambda data: data["equipment"][0].update(buy_price=53))
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
