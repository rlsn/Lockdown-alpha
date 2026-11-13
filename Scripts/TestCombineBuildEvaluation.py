"""Tests for pooling independent recorded paired runs; synthetic cases are not game evidence."""

import copy
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import BuildEvaluation as core  # noqa: E402
import CombineBuildEvaluation as combine  # noqa: E402


class CombineTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.caps = {"adapter": {"id": "lockdown-demo", "version": 11},
                     "content": {"id": "demo", "version": "1", "digest": "a" * 64},
                     "metrics": {"score": {"unit": "score", "kind": "score", "direction": "higher_better"}},
                     "scenarios": {"duel_site": {"metrics": ["score"]}}}
        self.first = self.run_fixture("first", "101", 1)
        self.second = self.run_fixture("second", "202", 2)

    def tearDown(self):
        self.temp.cleanup()

    def run_fixture(self, name, seed, score):
        run = self.root / name
        (run / "native").mkdir(parents=True)
        manifest = {"schema": "build_evaluation.manifest", "schema_version": 2,
                    "experiment": {"id": name, "hypothesis": "test"},
                    "adapter": {"id": "lockdown-demo", "module": "BuildEvaluationLockdown"},
                    "scenario": {"kind": "duel_site", "parameters": {"site": "capture"}},
                    "sample_class": "diagnostic", "assumptions": ["test"],
                    "design": {"kind": "paired_arms", "reference": "ref",
                               "arms": [{"id": "ref", "plan": {}}, {"id": "t", "plan": {}}]},
                    "blocks": {"root_seed": seed, "splits": {"search": 0, "confirm": 1}, "sides": 2},
                    "metrics": {"primary": "score", "secondary": [], "material_difference": None},
                    "budget": {"max_cases": 4},
                    "resolved": {"run_id": name, "adapter": self.caps["adapter"], "core_version": 1,
                                 "capabilities_sha256": core.digest_json(self.caps), "content": self.caps["content"],
                                 "cases": 4}}
        digests = {"editor_sha256": "b" * 64, "module_sha256": "c" * 64}
        runner = {"fixed_fps": 60, "exit_code": 0, "timed_out": False, "budget_stop": None, "log_errors": [],
                  "binary_digests": digests, "binary_digests_after": digests}
        native = {"schema": "native", "schema_version": 1, "content_root": "a" * 64, "product_version": "1",
                  "source_commit": "commit", "source_dirty": "unknown", "engine_version": "engine",
                  "fixed_time_step": True, "fixed_delta_seconds": 1 / 60, "seed_derivation": 1,
                  "run_id": name, "passed": True, "cases_requested": 4, "cases_written": 4}
        cases = []
        for side in range(2):
            spawn = {"subject": "100,0,0", "opponent": "-100,0,0"}
            if side:
                spawn = {"subject": spawn["opponent"], "opponent": spawn["subject"]}
            for arm in ("ref", "t"):
                cases.append({"case_id": f"c000{'ab'[side]}-{arm}", "block": f"c000{'ab'[side]}",
                              "split": "confirm", "index": 0, "side": side, "arm": arm,
                              "seed": str(core.block_seed(seed, "confirm", 0)),
                              "status": "Completed", "evidence_adapter": self.caps["adapter"], "violations": [],
                              "facts": {"spawn": spawn}, "metrics": {"score": {"value": score if arm == "t" else 0,
                                                                                "censored": False}}})
        core.write_json(run / "manifest.json", manifest)
        core.write_json(run / "capabilities.json", self.caps)
        core.write_json(run / "runner.json", runner)
        core.write_json(run / "findings.json", {"findings": []})
        core.write_json(run / "native" / "native_run.json", native)
        core.write_cases(run, cases)
        return run

    def test_independent_seeds_are_reindexed_and_incomplete_pairs_are_excluded(self):
        cases = core.read_cases(self.second)
        cases[0]["status"] = "Invalid"
        core.write_cases(self.second, cases)
        result = combine.combine([self.first, self.second])
        self.assertEqual({row["seed"] for row in result["seed_mapping"].values()},
                         {str(core.block_seed("101", "confirm", 0)), str(core.block_seed("202", "confirm", 0))})
        metric = result["metrics"]["splits"]["confirm"]["metrics"]["score"]
        self.assertEqual(metric["contrasts"][0]["n"], 1)
        self.assertEqual(metric["contrasts"][0]["mean"], 1)
        self.assertEqual(metric["contrasts"][0]["excluded_blocks"], ["c001"])
        self.assertEqual(result["run_statuses"][str(self.second.resolve())]["Invalid"], 1)

    def test_different_binary_or_same_seed_cannot_be_combined(self):
        path = self.second / "runner.json"
        runner = core.read_json(path)
        changed = copy.deepcopy(runner)
        changed["binary_digests"]["module_sha256"] = changed["binary_digests_after"]["module_sha256"] = "d" * 64
        core.write_json(path, changed)
        with self.assertRaisesRegex(core.PlanError, "identity differs"):
            combine.combine([self.first, self.second])
        core.write_json(path, runner)
        path = self.second / "manifest.json"
        manifest = core.read_json(path)
        manifest["blocks"]["root_seed"] = "101"
        core.write_json(path, manifest)
        cases = core.read_cases(self.second)
        for case in cases:
            case["seed"] = str(core.block_seed("101", "confirm", 0))
        core.write_cases(self.second, cases)
        with self.assertRaisesRegex(core.PlanError, "more than one run"):
            combine.combine([self.first, self.second])

    def test_incomplete_plan_or_rule_violation_is_refused(self):
        cases = core.read_cases(self.second)
        core.write_cases(self.second, cases[:-1])
        with self.assertRaisesRegex(core.PlanError, "incomplete"):
            combine.combine([self.first, self.second])
        core.write_cases(self.second, cases)
        core.write_json(self.second / "findings.json", {"findings": [{"conclusion": "RuleViolation"}]})
        with self.assertRaisesRegex(core.PlanError, "rule violation"):
            combine.combine([self.first, self.second])

    def test_positions_must_match_between_arms_and_swap_between_sides(self):
        original = core.read_cases(self.second)
        for index, value in ((1, {"subject": "101,0,0", "opponent": "-100,0,0"}),
                             (2, {"subject": "101,0,0", "opponent": "-101,0,0"})):
            cases = copy.deepcopy(original)
            cases[index]["facts"]["spawn"] = value
            core.write_cases(self.second, cases)
            with self.subTest(case=index), self.assertRaises(core.PlanError):
                combine.combine([self.first, self.second])

    def test_existing_output_is_not_overwritten(self):
        output = self.root / "combined.json"
        output.write_text("keep", encoding="utf-8")
        self.assertEqual(combine.main(["--run", str(self.first), "--run", str(self.second), "--output", str(output)]), 2)
        self.assertEqual(output.read_text(encoding="utf-8"), "keep")


if __name__ == "__main__":
    unittest.main()
