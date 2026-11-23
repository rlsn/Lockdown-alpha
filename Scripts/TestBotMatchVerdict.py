import copy
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from JudgeBotMatch import describe, judge, load, scan_logs
from TestOpponentTelemetry import match

CLEAN_LOG = {"Telemetry.log": ["[2026.01.01-00.00.00:000][  1]LogLDTest: Display: LD_DEMO_OPPONENT_TELEMETRY match=1"]}


def played(index, opponents=7, reason="ObjectiveTimeout"):
    """A full-room match as the telemetry writes it: every AI chose, learned and gained XP."""
    value = match(index, opponents=opponents, winner_slot=0)
    value["reason"] = reason
    value["guard_success_ms"] = 20000 if reason == "GuardSucceeded" else -1
    for row in value["contenders"]:
        if row["kind"] == "OpponentAI":
            row["curve"]["xp"] = [10 * i for i in range(len(row["curve"]["t_ms"]))]
    return value


def report(count=1, wall=300.0, passed=True, detail=""):
    return {"passed": passed, "role": "Telemetry", "detail": detail, "matches_requested": count,
            "matches_completed": count if passed else count - 1, "opponents": 7,
            "files": [f"Match{i:02d}.json" for i in range(1, count + 1)], "match_wall_seconds": [wall] * count}


def verdict(matches=None, logs=None, run=None, opponents=7, timeout=1200):
    matches = [played(1)] if matches is None else matches
    return judge(report(len(matches)) if run is None else run, matches, CLEAN_LOG if logs is None else logs,
                 opponents, timeout)


class VerdictTest(unittest.TestCase):
    def assert_fails(self, result, fragment):
        self.assertFalse(result["passed"])
        self.assertTrue(any(fragment in failure for failure in result["failures"]), result["failures"])

    def test_full_room_matches_that_ran_to_a_locked_result_pass(self):
        result = verdict([played(1), played(2, reason="GuardSucceeded"), played(3, reason="SprayCount")])
        self.assertTrue(result["passed"], result["failures"])
        self.assertEqual([row["ai"] for row in result["matches"]], [7, 7, 7])
        self.assertEqual(result["matches"][1]["reason"], "GuardSucceeded")
        self.assertEqual(describe(result)[-1], "Bot match verdict: passed")

    def test_levels_kills_and_winner_are_not_judged(self):
        value = played(1)
        for row in value["contenders"]:
            row["final_level"], row["kills"], row["deaths"], row["winner"] = 1, 0, 0, False
        value["winner"], value["reason"] = "", "NoChampion"
        self.assertTrue(verdict([value])["passed"])

    def test_a_match_without_a_locked_result_fails(self):
        for reason in ("None", "HostEnded"):
            value = played(1)
            value["reason"] = reason
            self.assert_fails(verdict([value]), "no locked result")
        value = played(1)
        value["result_id"] = ""
        self.assert_fails(verdict([value]), "no locked result")

    def test_an_unfinished_or_overlong_run_fails(self):
        self.assert_fails(verdict(run=report(passed=False, detail="Telemetry match 1 timed out at stage 3.")), "timed out")
        self.assert_fails(verdict(run=report(count=2)), "completed 2 of 2 requested match(es) and left 1")
        self.assert_fails(verdict(run=report(wall=1200.5)), "outside its 1200 s bound")
        self.assert_fails(judge(None, [], CLEAN_LOG, 7, 1200), "no readable Telemetry.json")

    def test_an_empty_report_without_match_files_is_not_a_pass(self):
        result = judge({}, [], CLEAN_LOG, 7, 1200)
        self.assert_fails(result, "did not report a pass")
        self.assert_fails(result, "completed None of None requested match(es) and left 0 match file(s)")
        self.assert_fails(judge({"passed": True}, [], CLEAN_LOG, 7, 1200), "left 0 match file(s)")
        self.assert_fails(judge({"passed": True, "matches_requested": 0, "matches_completed": 0,
                                 "match_wall_seconds": []}, [], CLEAN_LOG, 7, 1200), "left 0 match file(s)")

    def test_a_match_file_without_a_match_is_not_a_pass(self):
        for empty in (None, [], "text", 5):
            result = judge(report(), [empty], CLEAN_LOG, 7, 1200)
            self.assert_fails(result, "Match01: the match file holds no match to judge")
        result = judge(report(2), [None, played(2)], CLEAN_LOG, 7, 1200)
        self.assert_fails(result, "Match01: the match file holds no match to judge")
        self.assertEqual(result["matches"][1]["reason"], "ObjectiveTimeout")

    def test_a_leaver_without_a_usable_name_is_named_by_its_id(self):
        value = played(1)
        value["contenders"][2]["left"] = True
        del value["contenders"][2]["name"]
        value["contenders"][3]["left"] = True
        value["contenders"][3]["name"] = 7
        result = verdict([value], logs={"A.log": ["LogLDX: Error: one"]})
        self.assert_fails(result, "a contender left the match: " + value["contenders"][2]["contender"] + ", "
                          + value["contenders"][3]["contender"])
        self.assert_fails(result, "1 error line(s) logged")

    def test_a_runner_failure_fails_an_otherwise_clean_run(self):
        result = judge(report(), [played(1)], CLEAN_LOG, 7, 1200, ["The Host exited with code 3"])
        self.assert_fails(result, "The Host exited with code 3")
        self.assertEqual(result["matches"][0]["reason"], "ObjectiveTimeout")

    def test_a_structural_surprise_in_a_match_file_fails_only_that_match(self):
        no_killer = played(1)
        del no_killer["kills"][0]["killer"]
        not_a_dict = played(2)
        not_a_dict["contenders"][1]["curve"] = None
        result = verdict([no_killer, not_a_dict, played(3)], logs={"A.log": ["LogLDX: Error: one"]})
        self.assert_fails(result, "Malformed match file: Match01: KeyError")
        self.assert_fails(result, "Malformed match file: Match02")
        self.assert_fails(result, "1 error line(s) logged")
        self.assertEqual(result["matches"][2]["reason"], "ObjectiveTimeout")

    def test_the_room_must_be_the_idle_host_and_the_requested_ai(self):
        self.assert_fails(verdict([played(1, opponents=3)]), "not the idle Host and 7 AI")
        self.assertTrue(verdict([played(1, opponents=3)], opponents=3)["passed"])
        value = played(1)
        value["contenders"][2]["left"] = True
        self.assert_fails(verdict([value]), "a contender left the match")

    def test_every_ai_must_have_played(self):
        value = played(1)
        value["contenders"][3]["curve"]["xp"] = [0] * len(value["contenders"][3]["curve"]["t_ms"])
        value["contenders"][5]["choices"] = 0
        value["contenders"][5]["learns"] = []
        value["contenders"][5]["actives"] = []
        value["contenders"][5]["first_active_ms"] = -1
        result = verdict([value])
        self.assert_fails(result, "AI slot 2 (no XP gained)")
        self.assert_fails(result, "AI slot 4 (no active choice, no active learned)")
        # The idle human Host is not an AI: it never chooses, learns or gains XP and that is not a failure.
        self.assertEqual(played(1)["contenders"][0]["learns"], [])

    def test_a_malformed_match_file_fails_without_stopping_the_other_checks(self):
        broken = played(1)
        del broken["contenders"][1]["learns"]
        result = verdict([broken, played(2)])
        self.assert_fails(result, "Malformed match file")
        self.assertEqual(result["matches"][0], {"match": 1, "wall_seconds": 300.0})
        self.assertEqual(result["matches"][1]["reason"], "ObjectiveTimeout")
        self.assertIn("Match 1: malformed", describe(result))

    def test_error_lines_fail_and_map_warnings_are_only_reported(self):
        logs = {"Telemetry.log": [
            "[1]LogLDWorld: Warning: LD_CAMP_RETURN_DEAD_END camp=h03 slot=0 epoch=4 cause=Blocked refused=2",
            "[2]LogLDWorld: Warning: LD_CAMP_RETURN_DEAD_END camp=h03 slot=0 epoch=5 cause=Blocked refused=3",
            "[3]LogLDWorld: Warning: LD_CAMP_PATH_STALLED member=abc goal=X=1 Y=2 Z=3",
            "[4]LogPython: Error: an editor plugin's own start-up noise",
            "[5]LogSlate: Warning: Could not find a font"]}
        result = verdict(logs=logs)
        self.assertTrue(result["passed"], result["failures"])
        self.assertEqual(result["warnings"], {"LD_CAMP_PATH_STALLED": 1, "LD_CAMP_RETURN_DEAD_END": 2})
        self.assertIn("WARNING (reported, not a failure): LD_CAMP_RETURN_DEAD_END x2", describe(result))
        for line in ("[6]LogLDCombat: Error: LD_DEMO_AUTHORITY_FAULT Falling interval exceeds the current core step",
                     "[6]LogNet: Error: something", "[6]LogWindows: Fatal: crash", "Assertion failed: Ptr [File:x.cpp]",
                     "Fatal error: [File:x.cpp]", "[6]LogLDMatch: Display: LD_DEMO_AUTHORITY_FAULT detail"):
            failing = verdict(logs={"Telemetry.log": logs["Telemetry.log"] + [line]})
            self.assert_fails(failing, "1 error line(s) logged")
            self.assertEqual(failing["error_lines"], 1)
        self.assert_fails(verdict(logs={}), "no log to check")
        errors, _ = scan_logs({"A.log": ["LogLDX: Error: one"] * 9})
        self.assertEqual(len(errors), 9)
        self.assert_fails(verdict(logs={"A.log": ["LogLDX: Error: one"] * 9}), "(and 4 more)")


class CommandLineTest(unittest.TestCase):
    def run_judge(self, directory, matches, log_lines, opponents=7):
        (directory / "Telemetry.json").write_text(json.dumps(report(len(matches))), encoding="utf-8")
        for index, value in enumerate(matches, start=1):
            (directory / f"Match{index:02d}.json").write_text(json.dumps(value), encoding="utf-8")
        # Unreal writes its logs as UTF-8 with a byte order mark and Chinese text in them.
        (directory / "Telemetry.log").write_text("\n".join(log_lines) + "\n", encoding="utf-8-sig")
        return self.run_judge_only(directory, opponents=opponents)

    def run_judge_only(self, directory, *extra, opponents=7):
        script = Path(__file__).with_name("JudgeBotMatch.py")
        return subprocess.run([sys.executable, "-B", str(script), str(directory), "--opponents", str(opponents),
                               "--timeout-seconds", "1200", *extra], capture_output=True, text=True, encoding="utf-8")

    def test_writes_the_verdict_and_exits_by_it(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            done = self.run_judge(directory, [played(1)], ["LogLDMatch: Warning: 本地设置存储无效，未选择任何用户文件。",
                                                           "LogLDWorld: Warning: LD_CAMP_PATH_STALLED member=a goal=b"])
            self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
            self.assertIn("Bot match verdict: passed", done.stdout)
            saved = json.loads((directory / "Verdict.json").read_text(encoding="utf-8"))
            self.assertTrue(saved["passed"])
            self.assertEqual(saved["warnings"], {"LD_CAMP_PATH_STALLED": 1})
            loaded_report, loaded_matches, loaded_logs, problems = load(directory, directory)
            self.assertEqual((loaded_report["opponents"], len(loaded_matches), list(loaded_logs), problems),
                             (7, 1, ["Telemetry.log"], []))

    def test_a_broken_match_file_keeps_its_number_and_the_rest_is_still_judged(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.run_judge(directory, [played(1), played(2), played(3)], ["LogLDCombat: Error: LD_DEMO_AUTHORITY_FAULT step"])
            (directory / "Match02.json").write_text('{"schema": 5, "match": 2,', encoding="utf-8")
            done = self.run_judge_only(directory)
            self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
            self.assertIn("FAILED: Unreadable evidence file Match02.json", done.stdout)
            self.assertIn("FAILED: 1 error line(s) logged", done.stdout)
            self.assertIn("Match 3: ObjectiveTimeout", done.stdout)
            saved = json.loads((directory / "Verdict.json").read_text(encoding="utf-8"))
            self.assertEqual([row["match"] for row in saved["matches"]], [1, 2, 3])
            self.assertNotIn("reason", saved["matches"][1])
            (directory / "Match02.json").write_text("null", encoding="utf-8")
            done = self.run_judge_only(directory)
            self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
            self.assertIn("FAILED: Match02: the match file holds no match to judge", done.stdout)

    def test_an_unreadable_report_or_a_runner_failure_still_writes_a_failed_verdict(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.run_judge(directory, [played(1)], ["clean"])
            done = self.run_judge_only(directory, "--run-failure", "The Host timed out and was stopped")
            self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
            self.assertIn("FAILED: The Host timed out and was stopped", done.stdout)
            (directory / "Telemetry.json").write_text("{", encoding="utf-8")
            done = self.run_judge_only(directory)
            self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
            self.assertIn("FAILED: Unreadable evidence file Telemetry.json", done.stdout)
            self.assertFalse(json.loads((directory / "Verdict.json").read_text(encoding="utf-8"))["passed"])

    def test_a_failed_verdict_exits_nonzero_and_lists_every_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            value = copy.deepcopy(played(1))
            value["reason"] = "None"
            done = self.run_judge(directory, [value], ["LogLDCombat: Error: LD_DEMO_AUTHORITY_FAULT step"])
            self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
            self.assertIn("FAILED: Match01: no locked result", done.stdout)
            self.assertIn("FAILED: 1 error line(s) logged", done.stdout)
            self.assertFalse(json.loads((directory / "Verdict.json").read_text(encoding="utf-8"))["passed"])


if __name__ == "__main__":
    unittest.main()
