"""Build evaluation core tests (Scripts/BuildEvaluation.py), driven by a fake adapter.

The fake adapter proves the core's contract and dependency direction only; it is never evidence about the game.
"""

import json
import math
import re
import sys
import tempfile
import types
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import BuildEvaluation as core  # noqa: E402

DIGEST = "f" * 64


def fake_adapter(name="FakeEvalAdapter"):
    module = types.ModuleType(name)
    module.ADAPTER_ID = "fake-game"
    module.ADAPTER_VERSION = 1

    def describe(_root):
        return {"schema": core.CAPABILITIES_SCHEMA, "schema_version": 1, "adapter": {"id": "fake-game", "version": 1},
                "content": {"id": "fake", "version": "1", "digest": DIGEST},
                "mechanics": {"card_a": {"status": "Supported"}, "card_b": {"status": "Supported"},
                              "card_x": {"status": "Disabled", "reason": "not wired"}},
                "decisions": {"pick": {}, "prepare": {"sample_classes": ["diagnostic"]}},
                "scenarios": {"toy": {"evidence_level": "NativeControlled", "sample_classes": ["legal_search", "diagnostic"],
                                      "sides": 2, "metrics": ["score", "time"]}},
                "metrics": {"score": {"unit": "pt", "kind": "score", "direction": "higher_better"},
                            "time": {"unit": "ms", "kind": "time_to_event", "direction": "descriptive"}},
                "random_streams": {"seeded": ["all"], "unseeded": []}}

    def requirements(_scenario, plan, _caps):
        if "cards" not in plan:
            raise ValueError("plan.cards is required")
        return {"mechanics": plan["cards"], "decisions": ["pick"] + (["prepare"] if plan.get("prepare") else []), "problems": []}

    def scenario_problems(scenario, _caps):
        return [] if scenario["parameters"].get("ok", True) else ["IllegalScenario: not ok"]

    def native_input(run_id, _manifest, _caps, cases):
        return {"run_id": run_id, "cases": [case["case_id"] for case in cases]}

    def case_evidence(_case, _manifest, _caps, result, _trace):
        return {key: result.get(key, default) for key, default in
                (("status", "Faulted"), ("reason", ""), ("metrics", {}), ("facts", {}), ("violations", []))}

    def canonical_events(trace, _result):
        events, spatial = [], {}
        for line in trace.splitlines():
            if line.startswith("POS "):
                _, key, x, y = line.split()
                spatial[key] = (float(x), float(y), 0.0)
            elif line.startswith("RES "):
                continue
            elif line:
                events.append(line)
        return events, spatial

    def replay_evidence(case, frozen, _caps, run, runner, result, trace):
        problems = []
        if run.get("run_id") != frozen["resolved"]["run_id"] or result.get("run_id") != run.get("run_id"):
            problems.append("run identity differs")
        if result.get("case_id") != case["case_id"] or result.get("seed") != case["seed"]:
            problems.append("case identity differs")
        if not run.get("passed") or result.get("status") != "Completed":
            problems.append("the run or Case did not complete")
        resources = [line for line in trace.splitlines() if line.startswith("RES ")]
        if not resources:
            problems.append("resource evidence is missing")
        return {"identity": {"content": run.get("content"), "build": run.get("build"), "binary": runner.get("binary")},
                "terminal": {key: result.get(key) for key in ("case_id", "seed", "status", "metrics", "terminal")},
                "resources": resources, "problems": problems}

    module.describe = describe
    module.requirements = requirements
    module.scenario_problems = scenario_problems
    module.native_input = native_input
    module.case_evidence = case_evidence
    module.canonical_events = canonical_events
    module.replay_evidence = replay_evidence
    module.identity_problems = lambda run, caps: [] if run.get("content") == caps["content"]["digest"] else ["content differs"]
    module.limitations = lambda _manifest, _caps: ["fake limitation"]
    module.replay_hint = lambda _run, case_id: f"replay {case_id}"
    sys.modules[name] = module
    return module


def manifest(design="paired_arms", arms=None, splits=(1, 3), sides=1, threshold=None, sample="legal_search", **overrides):
    arms = arms or [{"id": "ref", "plan": {"cards": ["card_a"]}}, {"id": "t", "plan": {"cards": ["card_b"]}}]
    out = {"schema": core.MANIFEST_SCHEMA, "schema_version": 1,
           "experiment": {"id": "exp-1", "hypothesis": "t beats ref"},
           "adapter": {"module": "FakeEvalAdapter", "id": "fake-game"},
           "scenario": {"kind": "toy", "parameters": {}}, "sample_class": sample, "assumptions": ["toy supply"],
           "design": {"kind": design, "arms": arms},
           "blocks": {"root_seed": "42", "splits": {"search": splits[0], "confirm": splits[1]}, "sides": sides},
           "metrics": {"primary": "score", "secondary": ["time"], "material_difference": threshold},
           "budget": {"max_cases": 100, "case_wall_timeout_s": 60, "run_wall_timeout_s": 600}}
    if design == "paired_arms":
        out["design"]["reference"] = "ref"
    out.update(overrides)
    return out


class CoreCase(unittest.TestCase):
    def setUp(self):
        fake_adapter()
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def plan(self, value, name="run"):
        path = self.root / f"{name}.json"
        path.write_text(json.dumps(value), encoding="utf-8")
        frozen, cases = core.plan(path, self.root / name, self.root)
        return self.root / name, frozen, cases

    def native(self, run_dir, values, statuses=None, facts=None, missing=(), content=DIGEST, traces=None):
        """values: case_id -> (score, time, censored)."""
        native = run_dir / "native"
        (native / "results").mkdir(parents=True, exist_ok=True)
        (native / "traces").mkdir(exist_ok=True)
        core.write_json(native / "native_run.json", {"run_id": run_dir.name, "content": content, "build": "fake-1",
                                                    "passed": True, "detail": ""})
        core.write_json(run_dir / "runner.json", {"binary": "fake-binary"})
        for case in core.read_cases(run_dir):
            if case["status"] != "NotRun" or case["case_id"] in missing:
                continue
            score, time, censored = values(case)
            result = {"run_id": run_dir.name, "case_id": case["case_id"], "seed": case["seed"],
                      "status": (statuses or {}).get(case["case_id"], "Completed"), "reason": "set by test",
                      "metrics": {"score": {"value": score, "censored": False}, "time": {"value": time, "censored": censored}},
                      "facts": (facts or (lambda _case: {}))(case), "terminal": {"hp": 42, "mp": 10, "build": ["x"]},
                      "trace": f"traces/{case['case_id']}.log"}
            core.write_json(native / "results" / f"{case['case_id']}.json", result)
            (native / "traces" / f"{case['case_id']}.log").write_text((traces or {}).get(case["case_id"], "E1\nE2\nRES a 42 10\nPOS a 0 0\n"),
                                                                      encoding="utf-8")


class ManifestTest(CoreCase):
    def refused(self, value, part):
        with self.assertRaises(core.PlanError) as caught:
            self.plan(value)
        self.assertIn(part, str(caught.exception))

    def test_every_field_is_explicit_and_unknown_fields_are_refused(self):
        bad = manifest()
        del bad["budget"]["max_cases"]
        self.refused(bad, "max_cases")
        bad = manifest()
        del bad["metrics"]["material_difference"]
        self.refused(bad, "material_difference")
        self.refused(manifest(extra=1), "unknown fields")
        bad = manifest()
        bad["design"]["arms"][0]["id"] = "bad id"
        self.refused(bad, "identifier")
        self.refused(manifest(assumptions=[]), "assumptions")
        bad = manifest()
        bad["blocks"]["root_seed"] = "18446744073709551616"
        self.refused(bad, "root_seed")
        bad = manifest()
        bad["design"]["reference"] = "missing"
        self.refused(bad, "reference arm")
        self.refused(manifest(design="factorial_2x2"), "levels")

    def test_an_adapter_without_the_contract_is_refused(self):
        sys.modules["BrokenEvalAdapter"] = types.ModuleType("BrokenEvalAdapter")
        bad = manifest()
        bad["adapter"]["module"] = "BrokenEvalAdapter"
        self.refused(bad, "adapter contract")

    def test_the_core_names_no_game(self):
        source = (Path(core.__file__)).read_text(encoding="utf-8")
        for word in (r"Lockdown", r"lockdown", r"\bLD_", r"opening", r"attr_", r"passive_", r"active_", r"Q/W/E", r"flagpole",
                     r"Unreal", r"BuildEvaluationLockdown"):
            self.assertIsNone(re.search(word, source), f"the core must not know '{word}'")


class PlanTest(CoreCase):
    def test_blocks_are_deterministic_and_splits_never_share_seeds(self):
        blocks = core.expand_blocks({"root_seed": "42", "splits": {"search": 3, "confirm": 4}, "sides": 2})
        self.assertEqual(blocks, core.expand_blocks({"root_seed": "42", "splits": {"search": 3, "confirm": 4}, "sides": 2}))
        self.assertEqual(len(blocks), 14)
        self.assertEqual(blocks[0]["block"], "s000a")
        self.assertEqual(blocks[1]["seed"], blocks[0]["seed"], "swapped sides share the block's world seed")
        search = {block["seed"] for block in blocks if block["split"] == "search"}
        confirm = {block["seed"] for block in blocks if block["split"] == "confirm"}
        self.assertFalse(search & confirm)
        self.assertEqual(len(search | confirm), 7)
        other = core.expand_blocks({"root_seed": "43", "splits": {"search": 3, "confirm": 4}, "sides": 1})
        self.assertFalse({block["seed"] for block in other} & (search | confirm), "another root draws other seeds")

    def test_seed_collisions_are_refused_within_a_split(self):
        original = core.block_seed
        try:
            core.block_seed = lambda _root, _split, _index: 7
            with self.assertRaisesRegex(core.PlanError, "seeds collide"):
                core.expand_blocks({"root_seed": "42", "splits": {"search": 2, "confirm": 0}, "sides": 2})
        finally:
            core.block_seed = original

    def test_unsupported_mechanics_refuse_their_arm_and_leave_the_design_incomplete(self):
        arms = [{"id": "ref", "plan": {"cards": ["card_a"]}}, {"id": "t", "plan": {"cards": ["card_z", "card_x"]}}]
        run_dir, frozen, cases = self.plan(manifest(arms=arms))
        reasons = {case["arm"]: case["reason"] for case in cases}
        self.assertTrue(all(case["status"] == "Unsupported" for case in cases))
        self.assertIn("UnsupportedMechanic: card_x (Disabled", reasons["t"])
        self.assertIn("UnsupportedMechanic: card_z is not in the adapter's runtime catalog", reasons["t"])
        self.assertIn("DesignIncomplete", reasons["ref"])
        self.assertFalse((run_dir / "native_input.json").exists(), "nothing runnable reaches the native input")
        self.assertEqual(frozen["resolved"]["runnable_cases"], 0)
        path = self.root / "again.json"
        path.write_text(json.dumps(manifest(arms=arms)), encoding="utf-8")
        self.assertEqual(core.main(["plan", "--manifest", str(path), "--run-dir", str(self.root / "again"),
                                    "--project-root", str(self.root)]), 3)

    def test_a_decision_outside_its_sample_class_and_a_malformed_plan_are_refused(self):
        arms = [{"id": "ref", "plan": {"cards": ["card_a"], "prepare": True}}, {"id": "t", "plan": {}}]
        _, _, cases = self.plan(manifest(arms=arms))
        reasons = {case["arm"]: case["reason"] for case in cases}
        self.assertIn("UnsupportedDecision: prepare is admitted only in ['diagnostic']", reasons["ref"])
        self.assertIn("IllegalPlan: plan.cards is required", reasons["t"])
        _, _, cases = self.plan(manifest(arms=[{"id": "ref", "plan": {"cards": ["card_a"], "prepare": True}},
                                               {"id": "t", "plan": {"cards": ["card_b"]}}], sample="diagnostic"), "diag")
        self.assertTrue(all(case["status"] == "NotRun" for case in cases))

    def test_scenario_problems_refuse_every_case(self):
        _, _, cases = self.plan(manifest(scenario={"kind": "toy", "parameters": {"ok": False}}))
        self.assertTrue(all(case["status"] == "Unsupported" and "IllegalScenario" in case["reason"] for case in cases))
        _, _, cases = self.plan(manifest(scenario={"kind": "arena", "parameters": {}}), "other")
        self.assertTrue(all("UnsupportedScenario: arena" in case["reason"] for case in cases))

    def test_unknown_primary_and_secondary_metrics_produce_unsupported_reports(self):
        for field in ("primary", "secondary"):
            with self.subTest(field=field):
                value = manifest()
                value["metrics"][field] = "unknown" if field == "primary" else ["unknown"]
                run_dir, _, cases = self.plan(value, field)
                self.assertTrue(all(case["status"] == "Unsupported" for case in cases))
                self.assertFalse((run_dir / "native_input.json").exists())
                metrics, findings = core.analyze(run_dir)
                self.assertEqual(metrics["splits"]["confirm"]["metrics"]["unknown"]["supported"], False)
                self.assertTrue(any("UnsupportedMetric: unknown" in str(item.get("detail"))
                                    for item in findings["findings"]))
                self.assertIn("UnsupportedMetric: unknown", (run_dir / "report.md").read_text(encoding="utf-8"))
                self.assertEqual(core.main(["analyze", "--run-dir", str(run_dir)]), 0)

    def test_a_budget_smaller_than_the_plan_starts_nothing(self):
        value = manifest()
        value["budget"]["max_cases"] = 7
        with self.assertRaises(core.PlanError) as caught:
            self.plan(value)
        self.assertIn("8 runnable Cases exceed max_cases 7", str(caught.exception))
        self.assertFalse((self.root / "run" / "cases.jsonl").exists())

    def test_a_plan_past_the_adapters_native_limit_starts_nothing(self):
        adapter = fake_adapter()
        describe = adapter.describe
        adapter.describe = lambda root: dict(describe(root), limits={"max_cases_per_run": 5})
        with self.assertRaises(core.PlanError) as caught:
            self.plan(manifest())
        self.assertIn("exceed the 5 one native run accepts", str(caught.exception))


class AnalyzeTest(CoreCase):
    def test_paired_contrast_uses_completed_blocks_and_a_t_interval(self):
        run_dir, _, _ = self.plan(manifest())
        confirm = {"c000a": 1, "c001a": 3, "c002a": 2}
        self.native(run_dir, lambda case: ((confirm.get(case["block"], 0) if case["arm"] == "t" else 0), 1000, False))
        metrics, findings = core.analyze(run_dir)
        contrast = metrics["splits"]["confirm"]["metrics"]["score"]["contrasts"][0]
        self.assertEqual((contrast["treatment"], contrast["reference"], contrast["n"]), ("t", "ref", 3))
        self.assertAlmostEqual(contrast["mean"], 2.0)
        half = core.t975(2) * 1.0 / math.sqrt(3)
        self.assertAlmostEqual(contrast["ci95"][0], 2.0 - half)
        confirmed = [finding for finding in findings["findings"] if finding["title"].startswith("确认集")]
        self.assertEqual([finding["conclusion"] for finding in confirmed], ["NoTarget"],
                         "without an approved threshold an effect is reported, never judged")
        self.assertTrue(confirmed[0]["minimal_reproduction"])
        report = (run_dir / "report.md").read_text(encoding="utf-8")
        self.assertIn("在本次覆盖的构筑、策略、场景与预算内", report)
        self.assertIn("搜索集（探索，不构成结论）", report)
        self.assertIn("fake limitation", report)

    def test_swapped_sides_are_averaged_within_each_independent_seed(self):
        run_dir, _, _ = self.plan(manifest(splits=(0, 4), sides=2))
        treatment = {"c000a": 0, "c000b": 2, "c001a": 0, "c001b": 0,
                     "c002a": 2, "c002b": 4, "c003a": -1, "c003b": 1}
        self.native(run_dir, lambda case: (treatment[case["block"]] if case["arm"] == "t" else 0, 1000, False))
        metrics, findings = core.analyze(run_dir)
        entry = metrics["splits"]["confirm"]
        score = entry["metrics"]["score"]
        contrast = score["contrasts"][0]
        self.assertEqual((entry["blocks"], entry["execution_blocks"]), (4, 8))
        self.assertEqual(score["arms"]["t"]["n"], 4)
        self.assertEqual(score["arms"]["t"]["cases"], 8)
        self.assertEqual(contrast["differences"], {"c000": 1, "c001": 0, "c002": 3, "c003": 0})
        self.assertEqual((contrast["n"], contrast["mean"], contrast["nonzero_blocks"]), (4, 1, 2))
        self.assertEqual(contrast["nonzero_share_ci95"], core.wilson95(2, 4))
        sd = math.sqrt(2)
        self.assertAlmostEqual(contrast["ci95"][0], 1 - core.t975(3) * sd / 2)
        confirmed = next(item for item in findings["findings"] if item["title"].startswith("确认集"))
        self.assertEqual(confirmed["minimal_reproduction"],
                         ["c002a-ref", "c002a-t", "c002b-ref", "c002b-t"])
        self.assertIn("4 个独立种子，8 个执行侧", (run_dir / "report.md").read_text(encoding="utf-8"))

    def test_an_incomplete_side_excludes_the_whole_seed_from_inference(self):
        run_dir, _, _ = self.plan(manifest(splits=(0, 3), sides=2))
        self.native(run_dir, lambda case: (case["index"] + (case["arm"] == "t"), 1000, False),
                    statuses={"c001b-t": "BudgetExceeded"})
        metrics, _ = core.analyze(run_dir)
        score = metrics["splits"]["confirm"]["metrics"]["score"]
        self.assertEqual(score["arms"]["t"]["n"], 2)
        self.assertEqual(score["arms"]["ref"]["n"], 3)
        self.assertEqual(score["contrasts"][0]["n"], 2)
        self.assertEqual(score["contrasts"][0]["excluded_blocks"], ["c001"])

    def test_a_declared_threshold_judges_only_the_confirmation_split(self):
        for threshold, expected in ((0.5, "BalanceConcern"), (50.0, "NoCounterexampleInCoveredSet")):
            run_dir, _, _ = self.plan(manifest(threshold=threshold), f"t{threshold}")
            self.native(run_dir, lambda case: ((2 + (case["index"] % 2) * 0.1) if case["arm"] == "t" else 0, 1000, False))
            _, findings = core.analyze(run_dir)
            self.assertEqual([finding["conclusion"] for finding in findings["findings"] if finding["title"].startswith("确认集")],
                             [expected])

    def test_failed_and_missing_cases_are_listed_never_counted(self):
        run_dir, _, _ = self.plan(manifest())
        self.native(run_dir, lambda case: (1, 1000, False), statuses={"c000a-t": "Invalid", "c001a-ref": "BudgetExceeded"},
                    missing=("c002a-t",))
        metrics, findings = core.analyze(run_dir)
        cases = {case["case_id"]: case for case in core.read_cases(run_dir)}
        self.assertEqual(cases["c002a-t"]["status"], "Interrupted")
        self.assertEqual(cases["c001a-ref"]["status"], "BudgetExceeded")
        contrast = metrics["splits"]["confirm"]["metrics"]["score"]["contrasts"][0]
        self.assertEqual(contrast["n"], 0)
        self.assertEqual(sorted(contrast["excluded_blocks"]), ["c000", "c001", "c002"])
        summary = metrics["splits"]["confirm"]["metrics"]["score"]["arms"]["t"]
        self.assertEqual(summary["n"], 1, "only the Completed treatment Case is a denominator")
        conclusions = [finding["conclusion"] for finding in findings["findings"]]
        self.assertIn("InsufficientEvidence", conclusions)
        self.assertNotIn("NoTarget", conclusions)

    def test_censored_time_to_event_is_counted_at_its_horizon(self):
        run_dir, _, _ = self.plan(manifest())
        self.native(run_dir, lambda case: (0, 45000, True) if case["arm"] == "t" else (1, 3000, False))
        metrics, _ = core.analyze(run_dir)
        arms = metrics["splits"]["confirm"]["metrics"]["time"]["arms"]
        self.assertEqual(arms["t"]["censored"], 3)
        self.assertEqual(arms["ref"]["censored"], 0)
        self.assertIn("restricted mean", arms["t"]["note"])

    def test_decision_regret_best_share_and_a_shared_prefix(self):
        arms = [{"id": f"o{index}", "plan": {"cards": ["card_a"]}} for index in range(3)]
        run_dir, _, _ = self.plan(manifest(design="decision", arms=arms, splits=(0, 2)))
        table = {("c000a", "o0"): 1, ("c000a", "o1"): -1, ("c000a", "o2"): 1, ("c001a", "o0"): -1, ("c001a", "o1"): -1,
                 ("c001a", "o2"): 0}
        self.native(run_dir, lambda case: (table[(case["block"], case["arm"])], 1000, False),
                    facts=lambda case: {"prefix": case["block"], "label": f"card-{case['arm']}"})
        metrics, findings = core.analyze(run_dir)
        decision = metrics["splits"]["confirm"]["metrics"]["score"]["decision"]
        self.assertEqual(decision["blocks"], 2)
        self.assertAlmostEqual(decision["mean_regret"]["o1"], 1.5)
        self.assertAlmostEqual(decision["best_share"]["o0"], 0.25)
        self.assertAlmostEqual(decision["best_share"]["o2"], 0.75)
        self.assertEqual(decision["share_blocks_choice_matters"], 1.0)
        self.assertIsNone(decision["share_blocks_spread_at_least_threshold"])
        self.assertEqual(len(metrics["splits"]["confirm"]["metrics"]["score"]["contrasts"]), 3)
        self.assertNotIn("RuleViolation", [finding["conclusion"] for finding in findings["findings"]])
        self.assertIn("card-o1", (run_dir / "report.md").read_text(encoding="utf-8"))
        run_dir, _, _ = self.plan(manifest(design="decision", arms=arms, splits=(0, 2)), "unpaired")
        self.native(run_dir, lambda case: (0, 1000, False), facts=lambda case: {"prefix": case["arm"]})
        _, findings = core.analyze(run_dir)
        self.assertIn("同一决策块的分支前缀不一致（不是同一真实 Offer）", [finding["title"] for finding in findings["findings"]])
        metrics, _ = core.analyze(run_dir)
        self.assertEqual(metrics["splits"]["confirm"]["metrics"]["score"]["contrasts"][0]["n"], 0)

    def test_decision_and_interaction_also_use_seed_means(self):
        arms = [{"id": f"o{index}", "plan": {"cards": ["card_a"]}} for index in range(3)]
        run_dir, _, _ = self.plan(manifest(design="decision", arms=arms, splits=(0, 2), sides=2), "decision")
        self.native(run_dir, lambda case: ((case["side"] + int(case["arm"][1:])) * (case["index"] + 1), 1000, False))
        metrics, _ = core.analyze(run_dir)
        decision = metrics["splits"]["confirm"]["metrics"]["score"]["decision"]
        self.assertEqual(decision["blocks"], 2)
        self.assertEqual(decision["rows"][0]["values"], {"o0": 0.5, "o1": 1.5, "o2": 2.5})
        self.assertEqual(decision["spread"]["n"], 2)
        arms = [{"id": f"{a}{b}", "levels": {"first": a, "second": b}, "plan": {"cards": ["card_a"]}}
                for a in ("treatment", "reference") for b in ("treatment", "reference")]
        run_dir, _, _ = self.plan(manifest(design="factorial_2x2", arms=arms, splits=(0, 2), sides=2), "factorial")
        self.native(run_dir, lambda case: ((case["side"] + case["index"] + 1)
                    if case["arm"] == "treatmenttreatment" else 0, 1000, False))
        metrics, _ = core.analyze(run_dir)
        term = metrics["splits"]["confirm"]["metrics"]["score"]["interaction"]
        self.assertEqual(term["terms"], {"c000": 1.5, "c001": 2.5})
        self.assertEqual(term["n"], 2)

    def test_factorial_interaction_against_reference_substitutes(self):
        arms = [{"id": f"{a}{b}", "levels": {"first": a, "second": b}, "plan": {"cards": ["card_a"]}}
                for a in ("treatment", "reference") for b in ("treatment", "reference")]
        run_dir, _, _ = self.plan(manifest(design="factorial_2x2", arms=arms, splits=(0, 2)))
        values = {"treatmenttreatment": 5, "treatmentreference": 2, "referencetreatment": 1, "referencereference": 0}
        self.native(run_dir, lambda case: (values[case["arm"]] + case["index"], 1000, False))
        metrics, _ = core.analyze(run_dir)
        term = metrics["splits"]["confirm"]["metrics"]["score"]["interaction"]
        self.assertEqual(term["n"], 2)
        self.assertAlmostEqual(term["mean"], 2.0)
        self.assertTrue(term["degenerate"], "identical interaction terms give no interval")
        self.assertIsNone(term["ci95"])
        self.assertEqual(term["nonzero_blocks"], 2)

    def test_identical_differences_are_never_judged_against_a_threshold(self):
        run_dir, _, _ = self.plan(manifest(threshold=0.5))
        self.native(run_dir, lambda case: (0, 1000, False))
        _, findings = core.analyze(run_dir)
        confirmed = [finding for finding in findings["findings"] if finding["title"].startswith("确认集")]
        self.assertEqual([finding["conclusion"] for finding in confirmed], ["InsufficientEvidence"],
                         "three zero differences do not show the effect is below the threshold")
        self.assertTrue(confirmed[0]["degenerate"])
        self.assertEqual(confirmed[0]["nonzero_blocks"], 0)
        self.assertAlmostEqual(confirmed[0]["nonzero_share_ci95"][1], core.wilson95(0, 3)[1])
        self.assertIn("退化", (run_dir / "report.md").read_text(encoding="utf-8"))
        self.assertAlmostEqual(core.wilson95(0, 6)[1], 0.3903, places=3)

    def test_an_evidence_violation_survives_a_second_analysis(self):
        run_dir, _, _ = self.plan(manifest())

        def write():
            self.native(run_dir, lambda case: (1, 1000, False))
            path = run_dir / "native" / "results" / "c000a-t.json"
            result = core.read_json(path)
            result["violations"] = ["hit contradicts itself"]
            core.write_json(path, result)

        write()
        _, first = core.analyze(run_dir)
        _, second = core.analyze(run_dir)
        for findings in (first, second):
            self.assertIn("证据合同被违反", [finding["title"] for finding in findings["findings"]])
        self.assertEqual(core.main(["analyze", "--run-dir", str(run_dir)]), 1)

    def test_a_run_of_other_content_invalidates_every_case(self):
        run_dir, _, _ = self.plan(manifest())
        self.native(run_dir, lambda case: (1, 1000, False), content="0" * 64)
        _, findings = core.analyze(run_dir)
        self.assertTrue(all(case["status"] == "Invalid" for case in core.read_cases(run_dir)))
        self.assertEqual(findings["findings"][0]["conclusion"], "RuleViolation")

    def test_analysis_records_evidence_version_and_invalidates_changed_binaries(self):
        run_dir, _, _ = self.plan(manifest())
        self.native(run_dir, lambda case: (1, 1000, False))
        metrics, _ = core.analyze(run_dir)
        self.assertEqual(metrics["analysis"]["completed_evidence_versions"], ["1"])
        self.assertIn("录制 Adapter", (run_dir / "report.md").read_text(encoding="utf-8"))
        self.assertIn("判读 Adapter", (run_dir / "report.md").read_text(encoding="utf-8"))
        runner = core.read_json(run_dir / "runner.json")
        runner.update(binary_digests={"module": "before"}, binary_digests_after={"module": "after"})
        core.write_json(run_dir / "runner.json", runner)
        _, findings = core.analyze(run_dir)
        self.assertTrue(all(case["status"] == "Invalid" for case in core.read_cases(run_dir)))
        self.assertTrue(any("changed" in str(finding.get("detail")) for finding in findings["findings"]))

    def test_a_seed_that_changes_nothing_is_a_rule_violation(self):
        run_dir, _, _ = self.plan(manifest())
        self.native(run_dir, lambda case: (1, 1000, False), facts=lambda case: {"random_fingerprint": "same"})
        _, findings = core.analyze(run_dir)
        self.assertTrue(any("更换种子没有改变" in finding["title"] for finding in findings["findings"]))

    def test_only_a_search_split_is_exploration(self):
        run_dir, _, _ = self.plan(manifest(splits=(2, 0)))
        self.native(run_dir, lambda case: (1, 1000, False))
        _, findings = core.analyze(run_dir)
        self.assertEqual([finding["conclusion"] for finding in findings["findings"]], ["InsufficientEvidence"])

    def test_t_quantiles(self):
        self.assertAlmostEqual(core.t975(1), 12.706)
        self.assertAlmostEqual(core.t975(30), 2.042)
        self.assertAlmostEqual(core.t975(40), 2.021)
        self.assertTrue(1.96 < core.t975(1000) < 1.98)


class ReplayTest(CoreCase):
    def test_replay_reports_the_first_divergence_and_the_spatial_deviation(self):
        run_dir, _, _ = self.plan(manifest(splits=(0, 1)))
        self.native(run_dir, lambda case: (1, 1000, False))
        replay_dir = self.root / "replay"
        _, case = core.replay_plan(run_dir, "c000a-t", replay_dir)
        self.assertEqual(case["seed"], core.read_cases(run_dir)[1]["seed"])
        self.assertEqual(core.read_json(replay_dir / "native_input.json")["cases"], ["c000a-t"])
        self.native(replay_dir, lambda case: (1, 1000, False))
        self.assertEqual(core.compare(run_dir, replay_dir, "c000a-t", 0.0)["conclusion"], "Replayed")
        self.native(replay_dir, lambda case: (1, 1000, False), traces={"c000a-t": "E1\nE9\nRES a 42 10\nPOS a 0 0\n"})
        out = core.compare(run_dir, replay_dir, "c000a-t", 0.0)
        self.assertEqual((out["conclusion"], out["first_divergence"]["index"]), ("RuleViolation", 1))
        self.native(replay_dir, lambda case: (1, 1000, False), traces={"c000a-t": "E1\nE2\nRES a 42 10\nPOS a 0.4 0\n"})
        self.assertEqual(core.compare(run_dir, replay_dir, "c000a-t", 0.5)["conclusion"], "Replayed")
        self.assertEqual(core.compare(run_dir, replay_dir, "c000a-t", 0.1)["conclusion"], "RuleViolation")
        with self.assertRaises(core.PlanError):
            core.replay_plan(run_dir, "no-such-case", self.root / "none")

    def test_replay_rejects_missing_evidence_and_terminal_or_identity_differences(self):
        run_dir, _, _ = self.plan(manifest(splits=(0, 1)))
        self.native(run_dir, lambda case: (1, 1000, False))
        replay_dir = self.root / "replay"
        core.replay_plan(run_dir, "c000a-t", replay_dir)
        self.native(replay_dir, lambda case: (1, 1000, False))
        path = replay_dir / "native" / "results" / "c000a-t.json"
        original = core.read_json(path)
        for key, value, field in (("status", "BudgetExceeded", "evidence_problems"),
                                  ("seed", "999", "evidence_problems"),
                                  ("terminal", {"hp": 41, "mp": 10, "build": ["x"]}, "terminal_divergence")):
            changed = dict(original, **{key: value})
            core.write_json(path, changed)
            out = core.compare(run_dir, replay_dir, "c000a-t", 0)
            self.assertEqual(out["conclusion"], "RuleViolation", key)
            self.assertTrue(out[field], key)
        core.write_json(path, original)
        native_run = replay_dir / "native" / "native_run.json"
        run = core.read_json(native_run)
        core.write_json(native_run, dict(run, build="different"))
        self.assertTrue(core.compare(run_dir, replay_dir, "c000a-t", 0)["identity_divergence"])
        core.write_json(native_run, run)
        source_trace = run_dir / "native" / "traces" / "c000a-t.log"
        replay_trace = replay_dir / "native" / "traces" / "c000a-t.log"
        replay_trace.write_text("E1\nE2\nRES a 41 10\nPOS a 0 0\n", encoding="utf-8")
        out = core.compare(run_dir, replay_dir, "c000a-t", 0)
        self.assertEqual(out["conclusion"], "RuleViolation")
        self.assertTrue(out["resources_divergence"])
        source_trace.write_text("", encoding="utf-8")
        replay_trace.write_text("", encoding="utf-8")
        out = core.compare(run_dir, replay_dir, "c000a-t", 0)
        self.assertEqual(out["conclusion"], "RuleViolation")
        self.assertTrue(any("empty" in problem for problem in out["evidence_problems"]))

    def test_replay_comparison_distinguishes_boolean_and_numeric_values(self):
        difference = core.first_difference({"alive": True}, {"alive": 1})
        self.assertEqual(difference["path"], "$.alive")


if __name__ == "__main__":
    unittest.main()
