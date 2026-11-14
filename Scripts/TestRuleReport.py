import copy
import re
import unittest
from pathlib import Path

from CheckRuleReport import (IMPORT_TESTS, MAX_FILTER_CHARS, REQUIRED_TESTS, automation_filter, check_registrations,
                             check_report, fixture_profile, select_tests)


class RuleReportTests(unittest.TestCase):
    def setUp(self):
        self.report = {
            "failed": 0,
            "notRun": 0,
            "inProcess": 0,
            "succeeded": len(REQUIRED_TESTS),
            "tests": [
                {"fullTestPath": name, "state": "Success"}
                for name in sorted(REQUIRED_TESTS)
            ],
        }

    def test_complete_required_set(self):
        self.assertEqual(check_report(self.report), len(REQUIRED_TESTS))

    def test_integral_json_number_counters(self):
        for field in ("failed", "notRun", "inProcess", "succeeded"):
            self.report[field] = float(self.report[field])
        self.assertEqual(check_report(self.report), len(REQUIRED_TESTS))

    def test_missing_test_cannot_be_hidden_by_success_counter(self):
        missing = self.report["tests"].pop()
        self.report["succeeded"] = 10000
        with self.assertRaisesRegex(ValueError, re.escape(missing["fullTestPath"])):
            check_report(self.report)

    def test_unexpected_success_cannot_replace_required_test(self):
        self.report["tests"][0]["fullTestPath"] = "Lockdown.Rules.Unregistered"
        with self.assertRaisesRegex(ValueError, "test set differs"):
            check_report(self.report)

    def test_duplicate_test_cannot_replace_required_test(self):
        self.report["tests"][0] = copy.deepcopy(self.report["tests"][1])
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            check_report(self.report)

    def test_failed_counter_names_failing_test_and_errors(self):
        failing = self.report["tests"][3]
        failing["state"] = "Fail"
        failing["entries"] = [
            {"event": {"type": "Info", "message": "noise"}},
            {"event": {"type": "Error", "message": "Expected 'x' to be not null."},
             "filename": "D:/ci/Source/Lockdown/Private/Tests/LDDemoCoreTests.cpp", "lineNumber": 371},
        ]
        self.report["failed"] = 1
        with self.assertRaises(ValueError) as raised:
            check_report(self.report)
        message = str(raised.exception)
        self.assertIn(failing["fullTestPath"] + " [Fail]", message)
        self.assertIn("Expected 'x' to be not null. (LDDemoCoreTests.cpp:371)", message)
        self.assertNotIn("noise", message)

    def test_every_test_must_succeed(self):
        for state in ("Fail", "NotRun", "InProcess", "Skipped", "", None):
            with self.subTest(state=state):
                report = copy.deepcopy(self.report)
                report["tests"][0]["state"] = state
                with self.assertRaisesRegex(ValueError, "did not succeed"):
                    check_report(report)

    def test_failure_and_incomplete_counters(self):
        for field in ("failed", "notRun", "inProcess"):
            with self.subTest(field=field):
                report = copy.deepcopy(self.report)
                report[field] = 1
                with self.assertRaisesRegex(ValueError, field):
                    check_report(report)

    def test_missing_or_malformed_counters_fail_closed(self):
        for field in ("failed", "notRun", "inProcess", "succeeded"):
            for value in (-1, 0.5, "0", False, None, float("nan"), float("inf")):
                with self.subTest(field=field, value=value):
                    report = copy.deepcopy(self.report)
                    report[field] = value
                    with self.assertRaisesRegex(ValueError, field):
                        check_report(report)
            report = copy.deepcopy(self.report)
            del report[field]
            with self.assertRaisesRegex(ValueError, field):
                check_report(report)

    def test_malformed_result_shapes_fail_closed(self):
        for report in (None, [], {}, {"tests": []}):
            with self.subTest(report=report), self.assertRaises(ValueError):
                check_report(report)
        for tests in (None, {}, [None], [{}], [{"fullTestPath": []}]):
            with self.subTest(tests=tests):
                report = copy.deepcopy(self.report)
                report["tests"] = tests
                with self.assertRaises(ValueError):
                    check_report(report)

    def test_required_set_matches_current_native_registrations(self):
        root = Path(__file__).resolve().parents[1]
        registrations = []
        for source in (root / "Source").rglob("*.cpp"):
            registrations.extend(
                re.findall(
                    r'IMPLEMENT_\w+_AUTOMATION_TEST\s*\(\s*\w+\s*,\s*'
                    r'"(Lockdown\.Rules\.[^"]+)"',
                    source.read_text(encoding="utf-8-sig"),
                )
            )
        self.assertEqual(len(registrations), len(set(registrations)))
        self.assertSetEqual(set(registrations), set(REQUIRED_TESTS))
        check_registrations()


class NarrowedRunTests(unittest.TestCase):
    @staticmethod
    def report(names):
        return {"failed": 0, "notRun": 0, "inProcess": 0, "succeeded": len(names),
                "tests": [{"fullTestPath": name, "state": "Success"} for name in sorted(names)]}

    @staticmethod
    def runs(term, name):
        anchored_start, anchored_end = term.startswith("^"), term.endswith("$")
        text = term[1 if anchored_start else 0:len(term) - (1 if anchored_end else 0)]
        return ((name.startswith(text) if anchored_start else text in name)
                and (name.endswith(text) if anchored_end else True))

    def ran(self, automation_filter_text):
        terms = automation_filter_text.split("+")
        return {name for name in REQUIRED_TESTS if any(self.runs(term, name) for term in terms)}

    def test_full_run_is_unchanged(self):
        self.assertEqual(select_tests(), REQUIRED_TESTS)
        self.assertEqual(automation_filter(REQUIRED_TESTS), "Lockdown.Rules")
        self.assertEqual(select_tests(["Lockdown.Rules"]), REQUIRED_TESTS)

    def test_prefix_selects_by_path_prefix_and_runs_exactly_those(self):
        selected = select_tests(["Lockdown.Rules.DemoGrowth"])
        self.assertTrue(selected and all(name.startswith("Lockdown.Rules.DemoGrowth") for name in selected))
        self.assertIn("Lockdown.Rules.DemoGrowthWire", selected)
        self.assertEqual(self.ran(automation_filter(selected)), selected)
        both = select_tests(["Lockdown.Rules.DemoGrowth", "Lockdown.Rules.HUDScoreboard"])
        self.assertEqual(both, selected | {"Lockdown.Rules.HUDScoreboard"})
        self.assertEqual(check_report(self.report(both), both), len(both))

    def test_skip_import_runs_everything_else_exactly(self):
        selected = select_tests(skip_import=True)
        self.assertEqual(selected, REQUIRED_TESTS - IMPORT_TESTS)
        text = automation_filter(selected)
        self.assertLessEqual(len(text), MAX_FILTER_CHARS)
        self.assertEqual(self.ran(text), selected)
        self.assertIn("Lockdown.Rules.DemoContent.DevReload", selected)
        self.assertEqual(select_tests(["Lockdown.Rules.DemoContent"], skip_import=True),
                         {name for name in REQUIRED_TESTS if name.startswith("Lockdown.Rules.DemoContent.")} - IMPORT_TESTS)
        self.assertEqual(fixture_profile(selected), "runtime")
        self.assertEqual(fixture_profile(REQUIRED_TESTS), "full")

    def test_every_prefix_selection_runs_exactly_its_tests(self):
        groups = {".".join(name.split(".")[:3]) for name in REQUIRED_TESTS}
        for group in sorted(groups):
            for skip in (False, True):
                with self.subTest(group=group, skip=skip):
                    try:
                        selected = select_tests([group], skip)
                    except ValueError:
                        self.assertTrue(skip)
                        continue
                    self.assertEqual(self.ran(automation_filter(selected)), selected)

    def test_narrowed_report_must_match_its_selection(self):
        selected = select_tests(["Lockdown.Rules.DemoGrowth"])
        with self.assertRaisesRegex(ValueError, "unexpected"):
            check_report(self.report(selected | {"Lockdown.Rules.HUDScoreboard"}), selected)
        with self.assertRaisesRegex(ValueError, "missing"):
            check_report(self.report(set(sorted(selected)[1:])), selected)
        with self.assertRaisesRegex(ValueError, "missing"):
            check_report(self.report(REQUIRED_TESTS - IMPORT_TESTS))

    def test_bad_filters_are_refused(self):
        for prefix in ("Lockdown.Rules.NoSuchTest", "Rules.DemoGrowth", "Lockdown.RulesX", "^Lockdown.Rules.HUD",
                       "Lockdown.Rules.HUD+Lockdown.Rules.Demo", "Lockdown.Rules.HUD$", "Lockdown.Rules.HUD Score", ""):
            with self.subTest(prefix=prefix), self.assertRaises(ValueError):
                select_tests([prefix])
        with self.assertRaises(ValueError):
            select_tests(sorted(IMPORT_TESTS), skip_import=True)
        with self.assertRaises(ValueError):
            automation_filter({"Lockdown.Rules.Unregistered"})


if __name__ == "__main__":
    unittest.main()
