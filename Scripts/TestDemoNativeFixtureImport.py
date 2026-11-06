"""Test-only export consistency; these checks do not substitute for UE automation."""
from concurrent.futures import ProcessPoolExecutor
import json
from pathlib import Path
import pickle
import re
import tempfile
import unittest

import DemoContent as demo
import ExportDemoNativeFixtures as native


def _compile_valid_case(root):
    """Recompile one exported case with the compiler; its written outputs must be exactly the compiler's."""
    root = Path(root)
    compilation = demo.compile_content(root)
    demo.check_outputs(root, compilation.outputs)
    return compilation.digest.hex(), demo.verify_runtime_files(root, compilation.digest) == compilation.digest


class DemoNativeFixturesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        artifacts = demo.ROOT / "Artifacts" / "DemoNativeCatalog"
        artifacts.mkdir(parents=True, exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(prefix="export-test-", dir=artifacts)
        cls.addClassCleanup(cls.temp.cleanup)
        cls.root = Path(cls.temp.name) / "fixtures"
        cls.index = native.export(cls.root)

    def test_valid_outputs_use_the_existing_compiler_contract(self):
        cases = self.index["valid"]
        with ProcessPoolExecutor(min(len(cases), native.default_jobs())) as pool:
            results = list(pool.map(_compile_valid_case, [str(self.root / case["name"]) for case in cases]))
        for case, (digest, runtime_matches) in zip(cases, results):
            with self.subTest(case=case["name"]):
                self.assertEqual(digest, case["catalog_sha256"])
                self.assertTrue(runtime_matches)

    def test_approved_base_is_distinct_and_byte_identical_to_approved_inputs(self):
        root = self.root / "approved_base"
        for path in native.BUNDLE_PATHS + (demo.CONTENT_SOURCE,):
            self.assertEqual((root / path).read_bytes(), (demo.ROOT / path).read_bytes(), str(path))
        expected = json.loads((root / "native_expected.json").read_bytes())
        synthetic = json.loads((self.root / "base" / "native_expected.json").read_bytes())
        self.assertEqual(expected["Parameters.AD.BaseP2"], "7000")
        self.assertEqual(expected["Parameters.AP.BaseP2"], "0")
        self.assertEqual(expected["Parameters.CritMultiplierBp"], "20000")
        self.assertEqual(expected["Parameters.AttackSpeed.Base.Numerator"], "9")
        self.assertEqual(expected["Parameters.AttackSpeed.Base.Denominator"], "10")
        self.assertEqual(expected["Parameters.BasicAttackWindupMs"], "250")
        self.assertNotEqual(expected["Parameters.AD.BaseP2"], synthetic["Parameters.AD.BaseP2"])

    def test_every_rank_quantity_has_a_distinct_native_change_assertion(self):
        baseline = json.loads((self.root / "base" / "native_expected.json").read_bytes())
        data = json.loads((self.root / "base" / demo.CONTENT_OUTPUT).read_bytes())
        expected = set()

        def quantities(value, path):
            if type(value) is dict:
                if "unit" in value:
                    expected.add(path)
                else:
                    for key, child in value.items():
                        quantities(child, path + "." + key)
            elif type(value) is list:
                for index, child in enumerate(value):
                    quantities(child, f"{path}[{index}]")

        for index, ability in enumerate(data["abilities"]):
            quantities(ability["ranks"], f"content.abilities[{index}].ranks")
        for index, item in enumerate(data["equipment"]):
            if item["active"] is not None:
                quantities(item["active"]["rank"], f"content.equipment[{index}].active.rank")
        seen = set()
        for case in self.index["valid"]:
            if not case["mutated_quantity"]:
                continue
            with self.subTest(case=case["name"]):
                self.assertNotIn(case["mutated_quantity"], seen)
                seen.add(case["mutated_quantity"])
                self.assertTrue(case["changed_paths"])
                changed = json.loads((self.root / case["name"] / "native_expected.json").read_bytes())
                for path in case["changed_paths"]:
                    self.assertFalse(path.startswith("Identity."))
                    self.assertNotEqual(changed[path], baseline[path])
        self.assertEqual(seen, expected)

    def assert_reflected_projection(self, root_type, expected, *headers):
        structs = {}
        for relative in headers:
            header = (demo.ROOT / relative).read_text(encoding="utf-8")
            for name, body in re.findall(r"struct (?:LOCKDOWN_API )?(\w+)\s*\{(.*?)\n\};", header, re.S):
                structs[name] = re.findall(r"UPROPERTY\(\)\s+([\w<>]+)\s+(\w+)(?:\[(\d+)\])?", body)

        def assert_struct(name, value):
            self.assertIn(name, structs)
            fields = structs[name]
            self.assertEqual(set(value), {field for _, field, _ in fields}, name)
            for field_type, field, count in fields:
                child = value[field]
                if count:
                    self.assertEqual(len(child), int(count), name + "." + field)
                elif field_type.startswith("TArray<"):
                    inner = field_type[7:-1]
                    self.assertIs(type(child), list)
                    if inner in structs:
                        for element in child:
                            assert_struct(inner, element)
                elif field_type in structs:
                    assert_struct(field_type, child)
                elif field_type == "FString":
                    self.assertIs(type(child), str)
                elif field_type == "bool":
                    self.assertIs(type(child), bool)
                else:
                    self.assertIs(type(child), int, name + "." + field)
        assert_struct(root_type, expected)

    def test_expected_projection_covers_every_reflected_native_member(self):
        compilation = demo.compile_content(self.root / "base")
        expected = native.expected_projection(compilation)
        self.assert_reflected_projection("FLDDemoContentDefinitions", expected,
                                         Path("Source", "Lockdown", "Public", "Development", "LDDemoContentDefinitions.h"))
        self.assertEqual(native.flatten(expected), json.loads((self.root / "base" / "native_expected.json").read_bytes()))

    def test_world_projection_covers_every_reflected_native_member(self):
        compilation = demo.compile_content(self.root / "base")
        expected = native.expected_world_projection(compilation)
        self.assert_reflected_projection("FLDDemoWorldGeometry", expected,
                                         Path("Source", "Lockdown", "Public", "Development", "LDDemoContentDefinitions.h"),
                                         Path("Source", "Lockdown", "Public", "Region", "LDMapGeometry.h"))
        self.assertEqual(native.flatten(expected), json.loads((self.root / "base" / "native_world_expected.json").read_bytes()))
        world = json.loads(compilation.inputs[demo.WORLD_OUTPUT])
        self.assertEqual(len(expected["Blockers"]), len(world["geometry"]["blockers"]))
        self.assertIn(1, {row["Shape"] for row in expected["Blockers"]})
        self.assertEqual(len(expected["Anchors"]), len(json.loads((demo.ROOT / demo.WORLD_SOURCE).read_bytes())["anchors"]))
        self.assertEqual(len(expected["SpawnOffsetsCm"]), 7)
        self.assertNotIn("Connections", expected)

    def test_valid_world_changes_use_the_single_producer(self):
        from DemoWorld import compile_world

        for name in ("base", "world_spawn_shift"):
            root = self.root / name
            generated = compile_world(root)
            for path, expected in generated.outputs.items():
                self.assertEqual((root / path).read_bytes(), expected)
            world = json.loads((root / demo.WORLD_OUTPUT).read_bytes())
            self.assertEqual(world["source_sha256"], demo.sha256((root / demo.WORLD_SOURCE).read_bytes()))
            self.assertEqual(world["author_schema_sha256"], demo.sha256((root / demo.WORLD_SCHEMA_SOURCE).read_bytes()))
        baseline = json.loads((self.root / "base" / "native_world_expected.json").read_bytes())
        shifted = json.loads((self.root / "world_spawn_shift" / "native_world_expected.json").read_bytes())
        position_fields = [key for key in shifted if ".PositionCm." in key and shifted[key] != baseline[key]]
        self.assertTrue(position_fields)

    def test_refusals_reach_semantics_instead_of_only_hash_checks(self):
        for case in self.index["invalid"]:
            if case["name"].startswith("byte_identity_") or case["name"] in ("missing_world", "stale_expected_root"):
                continue
            with self.subTest(case=case["name"]):
                root = self.root / case["name"]
                manifest_raw = (root / demo.MANIFEST_OUTPUT).read_bytes()
                manifest = json.loads(manifest_raw)
                self.assertEqual(demo.sha256(manifest_raw), case["catalog_sha256"])
                for key, path in (("content", demo.CONTENT_OUTPUT), ("world", demo.WORLD_OUTPUT)):
                    self.assertEqual(manifest[key]["sha256"], demo.sha256((root / path).read_bytes()))
                self.assertEqual((root / demo.CONTENT_CONTRACT).read_bytes(),
                                 demo.content_contract_bytes(manifest["content"]["sha256"], case["catalog_sha256"]))

    def test_exact_integer_and_rational_expectations(self):
        expected = lambda name: json.loads((self.root / name / "native_expected.json").read_bytes())
        self.assertEqual(expected("int64_max_ms")["Parameters.RespawnMs"], str(demo.INT64_MAX))
        self.assertEqual(expected("above_double_integer")["Parameters.RespawnMs"], "9007199254740993")
        self.assertEqual(expected("exact_gold")["Parameters.StartingGold"], "9007199254740993")
        self.assertEqual(expected("exact_rational")["Parameters.MPRegen.PerLevel.Numerator"], "1")
        self.assertEqual(expected("exact_rational")["Parameters.MPRegen.PerLevel.Denominator"], "1000000000000000000")
        world = json.loads((self.root / "signed_world_boundary" / demo.WORLD_OUTPUT).read_bytes())
        self.assertEqual(world["geometry"]["surfaces"][0]["min_cm"]["x"]["numerator"], demo.INT64_MIN)
        self.assertNotIn("signed_world_boundary", {case["name"] for case in self.index["valid"]})
        self.assertIn("signed_world_boundary", {case["name"] for case in self.index["invalid"]})
        self.assertEqual(expected("base")["Formulas.AttackMoveAcquireRadius"], "14")
        decision = expected("unicode_scalar_boundary")["Identity.ApprovalDecision"]
        self.assertEqual(len(decision), 512)
        self.assertEqual(len(decision.encode("utf-16-le")) // 2, 513)

    def test_export_cannot_overwrite_or_create_live_inputs(self):
        baseline = (self.root / "base" / demo.CONTENT_OUTPUT).read_bytes()
        with self.assertRaises(demo.ContentError):
            native.export(self.root)
        with self.assertRaises(demo.ContentError):
            native.export(demo.ROOT / "SourceData" / "Demo" / "never-a-fixture-directory")
        self.assertEqual((self.root / "base" / demo.CONTENT_OUTPUT).read_bytes(), baseline)
        self.assertFalse((demo.ROOT / "SourceData" / "Demo" / "never-a-fixture-directory").exists())


class DemoNativeFixtureParallelismTest(unittest.TestCase):
    def test_parallel_export_writes_the_same_bytes_as_inline_export(self):
        artifacts = demo.ROOT / "Artifacts" / "DemoNativeCatalog"
        artifacts.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="export-jobs-test-", dir=artifacts) as temp:
            trees = {}
            for jobs in (1, 3):
                root = Path(temp) / f"jobs-{jobs}"
                native.export(root, rank_mutations=False, jobs=jobs)
                trees[jobs] = {path.relative_to(root).as_posix(): path.read_bytes() for path in root.rglob("*") if path.is_file()}
            self.assertGreater(len(trees[1]), 100)
            self.assertEqual(trees[1], trees[3])

    def test_export_refuses_a_non_positive_job_count(self):
        with self.assertRaises(demo.ContentError):
            native.export(demo.ROOT / "Artifacts" / "never-exported-jobs-0", jobs=0)
        self.assertFalse((demo.ROOT / "Artifacts" / "never-exported-jobs-0").exists())

    def test_content_errors_cross_process_boundaries_intact(self):
        error = pickle.loads(pickle.dumps(demo.ContentError("Code", Path("a", "b"), "detail")))
        self.assertIs(type(error), demo.ContentError)
        self.assertEqual((error.code, error.path, error.detail, str(error)),
                         ("Code", str(Path("a", "b")), "detail", f"Code: {Path('a', 'b')}: detail"))


if __name__ == "__main__":
    unittest.main()
