import copy
import json
import tempfile
import unittest
from pathlib import Path

from SummarizeOpponentTelemetry import describe, load, summarize

HUMAN = "00000000000000000000000000000001"


def ai_id(slot):
    return f"{slot + 2:032x}"


def contender(kind, slot, ident, level_curve, gold_curve, final_level, winner=False, purchases=(), attempts=0):
    times = [i * 1000 for i in range(len(level_curve))]
    return {"contender": ident, "name": ident[-4:], "kind": kind, "slot": slot, "winner": winner, "left": False,
            "final_level": final_level, "kills": 1, "deaths": 2, "capture_attempts": attempts, "capture_resumes": 1,
            "capture_paused": 1, "capture_lost": 0, "assists": 1, "capture_sprays": 1, "capture_interrupted": 0,
            "first_active_ms": 2000 if kind == "OpponentAI" else -1,
            "skills": [{"id": "active_lunge", "rank": 3, "active": True}, {"id": "passive_lurking", "rank": 1, "active": False}]
            if kind == "OpponentAI" else [],
            "max_rank_skills": 1 if kind == "OpponentAI" else 0, "rerolls_earned": 2 if kind == "OpponentAI" else 0,
            "rerolls_used": 1 if kind == "OpponentAI" else 0, "choices": 4 if kind == "OpponentAI" else 1,
            "replacements": [{"t_ms": 50000, "level": 5, "replaced": "active_arc_bolt", "replaced_rank": 2,
                              "new": "active_lunge", "new_rank": 1}] if slot == 0 else [],
            "purchases": [{"t_ms": t, "item": "item_edge_blade", "slot": 0, "price": 150, "gold_after": 50}
                          for t in purchases],
            "sales": [], "curve": {"t_ms": times, "level": level_curve, "xp": [0] * len(times), "gold": gold_curve}}


def match(index, duration_ms=125000, winner_slot=1, first_kill=30000):
    samples = duration_ms // 1000
    winner = ai_id(winner_slot) if winner_slot >= 0 else ""
    rows = [contender("Human", -1, HUMAN, [1] * samples, [200] * samples, 1)]
    for slot in range(3):
        level = [1 + (i * 1000) // 60000 for i in range(samples)]
        gold = [200 + 10 * i for i in range(samples)]
        rows.append(contender("OpponentAI", slot, ai_id(slot), level, gold, level[-1], ai_id(slot) == winner,
                              purchases=(40000,) if slot != 2 else (), attempts=2))
    return {"schema": 3, "match": index, "generation": index, "result_id": f"{index:032x}",
            "content_root": "ab" * 32, "dev_content_reload": False,
            "reason": "GuardSucceeded" if winner else "HostEnded", "winner": winner, "duration_ms": duration_ms,
            "capture_open_ms": 60000, "first_kill_ms": first_kill, "first_death_ms": 20000, "kill_feed_gaps": 0,
            "kills": [{"t_ms": first_kill, "victim": HUMAN, "killer": ai_id(0), "cause": "Contender",
                       "assisters": [ai_id(1)]}] if first_kill >= 0 else [],
            "deaths": [{"t_ms": first_kill, "victim": HUMAN, "life": 1, "outcome": "Granted", "killer": ai_id(0),
                        "killer_gold": 300, "killer_xp": 60, "assists": [{"contender": ai_id(1), "gold": 75, "xp": 60}]}]
            if first_kill >= 0 else [],
            "capture_contests": 3, "capture_sprays": 2, "capture_repaints": 1, "graffiti_cleared_by_death": 1,
            "graffiti_cleared_by_leave": 0, "guard_success_ms": 20000 if winner else -1, "contenders": rows}


class SummaryTest(unittest.TestCase):
    def test_aggregates_outcomes_times_and_curves_across_matches(self):
        summary = summarize([match(1), match(2, duration_ms=185000, winner_slot=0, first_kill=-1)])
        self.assertEqual(summary["matches"], 2)
        self.assertEqual(summary["content_roots"], ["ab" * 32])
        self.assertFalse(summary["dev_content_reload"])
        self.assertEqual(summary["reasons"]["GuardSucceeded"], 2)
        self.assertEqual(summary["ai_capture_wins"], 2)
        self.assertEqual(summary["wins_by_slot"], {"0": 1, "1": 1, "2": 0})
        self.assertEqual(summary["duration_ms"]["min"], 125000)
        self.assertEqual(summary["duration_ms"]["max"], 185000)
        self.assertEqual(summary["first_kill_ms"], {"count": 1, "min": 30000, "mean": 30000, "median": 30000,
                                                    "max": 30000, "missing": 1})
        self.assertEqual(summary["capture"]["attempts"]["max"], 6)
        self.assertEqual(summary["ai"]["purchases"]["count"], 6)
        self.assertEqual(summary["ai"]["first_purchase_ms"]["missing"], 2)
        # Minute 3 only exists in the longer match; its curve ends at 184 s, so minute 3 (180 s) has three AI samples.
        minutes = {row["minute"]: row for row in summary["ai_level_by_minute"]}
        self.assertEqual(minutes[1]["count"], 6)
        self.assertEqual(minutes[1]["mean"], 2)
        self.assertEqual(minutes[3]["count"], 3)
        self.assertEqual(summary["per_match"][1]["ai_final_levels"], [4, 4, 4])
        self.assertTrue(any("AI slot 0" in line for line in describe(summary)))
        # 112 §13
        self.assertEqual(summary["capture"]["sprays"]["mean"], 2)
        self.assertEqual(summary["capture"]["cleared_by_repaint"], 2)
        self.assertEqual(summary["capture"]["cleared_by_death"], 2)
        self.assertEqual(summary["capture"]["guard_success_ms"]["median"], 20000)
        self.assertEqual(summary["deaths"]["count"], 1)
        self.assertEqual(summary["deaths"]["kills_with_assist"], 1)
        self.assertEqual(summary["deaths"]["assist_gold"]["max"], 75)
        self.assertEqual(summary["growth"]["first_active_ms"]["median"], 2000)
        self.assertEqual(summary["growth"]["max_rank_skills"], {"1": 6})
        self.assertEqual(summary["growth"]["reroll_use_rate"], 0.5)
        self.assertEqual(summary["growth"]["replacements"], 2)
        self.assertEqual(summary["growth"]["replacement_rate"], round(2 / 24, 3))
        self.assertEqual(summary["growth"]["replacement_level"]["mean"], 5)
        self.assertEqual(summary["ai"]["assists"]["count"], 6)

    def test_host_ended_match_has_no_winner_slot(self):
        summary = summarize([match(1, winner_slot=-1)])
        self.assertEqual(summary["reasons"]["HostEnded"], 1)
        self.assertEqual(summary["ai_capture_wins"], 0)
        self.assertEqual(summary["per_match"][0]["winner_slot"], -1)
        self.assertEqual(summary["capture"]["guard_success_ms"]["missing"], 1)

    def test_rejects_inconsistent_matches(self):
        cases = []
        bad = match(1); bad["match"] = 2; cases.append(bad)
        bad = match(1); bad["schema"] = 1; cases.append(bad)
        bad = match(1); bad["guard_success_ms"] = -1; cases.append(bad)
        bad = match(1, winner_slot=-1); bad["guard_success_ms"] = 100; cases.append(bad)
        bad = match(1); bad["kills"][0]["assisters"] = [ai_id(0)]; cases.append(bad)
        bad = match(1); bad["deaths"][0]["outcome"] = "NoReward"; cases.append(bad)
        bad = match(1); bad["deaths"][0]["assists"][0]["contender"] = HUMAN; cases.append(bad)
        bad = match(1); bad["contenders"][1]["rerolls_used"] = 3; cases.append(bad)
        bad = match(1); bad["contenders"][1]["first_active_ms"] = -1; cases.append(bad)
        bad = match(1); bad["contenders"][1]["max_rank_skills"] = 3; cases.append(bad)
        bad = match(1); del bad["contenders"][1]["replacements"]; cases.append(bad)
        bad = match(1); bad["contenders"][1]["curve"]["gold"].pop(); cases.append(bad)
        bad = match(1); bad["contenders"][1]["curve"]["t_ms"][3] = 0; cases.append(bad)
        bad = match(1); bad["contenders"][1]["winner"] = True; cases.append(bad)
        bad = match(1); bad["contenders"][3]["slot"] = 0; cases.append(bad)
        bad = match(1); bad["contenders"] = bad["contenders"][1:]; cases.append(bad)
        bad = match(1); bad["first_kill_ms"] = bad["duration_ms"] + 1; cases.append(bad)
        bad = match(1); bad["kills"][0]["cause"] = "Skill"; cases.append(bad)
        bad = match(1); bad["contenders"][1]["capture_attempts"] = True; cases.append(bad)
        bad = match(1); bad["content_root"] = "AB" * 32; cases.append(bad)
        bad = match(1); del bad["dev_content_reload"]; cases.append(bad)
        for case in cases:
            with self.assertRaises(ValueError):
                summarize([case])
        with self.assertRaises(ValueError):
            summarize([])


class LoadTest(unittest.TestCase):
    def write(self, directory, report, matches):
        (directory / "Telemetry.json").write_text(json.dumps(report), encoding="utf-8")
        for index, value in enumerate(matches, start=1):
            (directory / f"Match{index:02d}.json").write_text(json.dumps(value), encoding="utf-8")

    def report(self, count, passed=True):
        return {"passed": passed, "role": "Telemetry", "detail": "" if passed else "timed out",
                "matches_requested": count, "matches_completed": count,
                "files": [f"Match{index:02d}.json" for index in range(1, count + 1)]}

    def test_loads_a_complete_run(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            self.write(directory, self.report(2), [match(1), match(2)])
            report, matches = load(directory)
            self.assertEqual(len(matches), 2)
            self.assertEqual(summarize(matches)["matches"], 2)

    def test_rejects_a_failed_or_short_run(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            self.write(directory, self.report(1, passed=False), [match(1)])
            with self.assertRaises(ValueError):
                load(directory)
            short = self.report(2)
            short["matches_completed"] = 1
            short["files"] = short["files"][:1]
            self.write(directory, short, [match(1)])
            with self.assertRaises(ValueError):
                load(directory)
            renamed = self.report(1)
            renamed["files"] = ["../Match01.json"]
            self.write(directory, renamed, [match(1)])
            with self.assertRaises(ValueError):
                load(directory)
            self.write(directory, self.report(2), [match(1)])
            with self.assertRaises(OSError):
                load(directory)


if __name__ == "__main__":
    unittest.main()
