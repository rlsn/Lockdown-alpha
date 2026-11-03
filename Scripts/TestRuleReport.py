import copy
import re
import unittest
from pathlib import Path

from CheckRuleReport import REQUIRED_TESTS, check_report


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


if __name__ == "__main__":
    unittest.main()
