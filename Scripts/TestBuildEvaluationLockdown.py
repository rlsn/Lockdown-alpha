"""Lockdown adapter tests (Scripts/BuildEvaluationLockdown.py): capabilities from the runtime catalog, plan admission,
native input, evidence judging over synthetic authority traces, canonical replay events and run identity.

The synthetic traces exercise the adapter's parsing and refusals; they are not game evidence. Native Cases run in
RunBuildEvaluation.ps1.
"""

import hashlib
import json
import copy
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import BuildEvaluation as core  # noqa: E402
import BuildEvaluationLockdown as lockdown  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
S, O, H, CAMP = "1" * 32, "2" * 32, "3" * 32, "4" * 32
CONTENT_ROOT = hashlib.sha256((ROOT / "Content/Lockdown/Data/Demo/manifest.json").read_bytes()).hexdigest()
# 600 XP reaches level 6: the opening opportunity plus the ones of levels 3 and 6.
SUPPLY = 600
STATS = ("stat_agility", "stat_brains", "stat_physique", "stat_strength", "stat_thick_skin")


def stat_points(*points):
    return [{"id": stat, "points": value} for stat, value in zip(STATS, points)]


BUILDS = {
    "subject": {"actives": [{"id": "active_elbow", "slot": "Q"}, {"id": "active_flyer_trap", "slot": "W"},
                            {"id": "active_not_mad", "slot": "E"}],
                "blueprints": [], "stat_points": stat_points(2, 0, 1, 2, 0)},
    "opponent": {"actives": [{"id": "active_open_mic", "slot": "Q"}, {"id": "active_stare_down", "slot": "W"},
                             {"id": "active_bail_out", "slot": "E"}],
                 "blueprints": [], "stat_points": stat_points(0, 2, 1, 0, 2)},
}


def manifest(subject=None, opponent=None, sample="legal_search", supply=SUPPLY, design="paired_arms"):
    subject = subject or {"choices": ["ai_select"] * 3}
    opponent = opponent or {"choices": ["ai_select"] * 3}
    arms = [{"id": "ref", "plan": {"subject": {"choices": ["ai_select"] * 3}, "opponent": {"choices": ["ai_select"] * 3}}},
            {"id": "t", "plan": {"subject": subject, "opponent": opponent}}]
    return {"schema": core.MANIFEST_SCHEMA, "schema_version": 1,
            "experiment": {"id": "lockdown-test", "hypothesis": "adapter test"},
            "adapter": {"module": "BuildEvaluationLockdown", "id": "lockdown-demo"},
            "scenario": {"kind": "duel_site", "parameters": {"site": "capture", "duel_timeout_ms": 45000,
                                                             "supply_xp": {"subject": supply, "opponent": supply}}},
            "sample_class": sample, "assumptions": ["test supply"],
            "design": {"kind": design, "reference": "ref", "arms": arms},
            "blocks": {"root_seed": "7", "splits": {"search": 0, "confirm": 1}, "sides": 2},
            "metrics": {"primary": "duel_score", "secondary": ["damage_dealt_hp"], "material_difference": None},
            "budget": {"max_cases": 10, "case_wall_timeout_s": 120, "run_wall_timeout_s": 600}}


def contender(role, slot, contender_id, hp=31000, body=True, **rest):
    row = {"role": role, "slot": slot, "contender": contender_id, "progression_valid": True, "level": 6, "xp": SUPPLY,
           "choices": 3, "planned_choices": 3, "unspent_points": 0, **copy.deepcopy(BUILDS[role]),
           "has_body": body, "hp_p2": hp, "max_hp_p2": 62000, "mp_p2": 0, "max_mp_p2": 0, "profile_bound": True,
           "profile_phase": "Engaging", "profile_failure": "", "supply_xp": SUPPLY, "supply_status": "Granted",
           "presets_requested": 0, "presets_queued": 0}
    row.update(rest)
    return row


def result(kind="kill", victim="opponent", killer="subject", status="Completed", **rest):
    out = {"schema": lockdown.NATIVE_RESULT_SCHEMA, "schema_version": 2, "run_id": "r", "case_id": "c000a-t", "seed": "7",
           "status": status, "reason": "", "generation": 2, "content_root": CONTENT_ROOT, "started_at_ms": 5000, "ended_at_ms": 20000,
           "outcome": {"kind": kind, "victim": victim if kind == "kill" else "", "killer": killer if kind == "kill" else "",
                       "t_ms": 15000 if kind == "kill" else 45000},
           "contenders": [contender("subject", 0, S), contender("opponent", 1, O, hp=0, body=False)],
           "trace": "traces/c000a-t.log", "trace_lines": 10}
    out.update(rest)
    return out


def hit(t, src, victim, dmg, absorbed, lost, before, after, monster="-"):
    return (f"LD_HIT t={t} src={src} src_kind={'camp' if src == CAMP else 'contender'} src_monster={monster} credit={src} "
            f"victim={victim} victim_life=1 victim_kind=contender victim_monster=- via=basic class=basic type=physical "
            f"dmg={dmg} absorbed={absorbed} hp_lost={lost} hp={before}->{after} max_hp=620.00 shield=0.00->0.00 amp_bp=10000 "
            f"resist=0.00 crit=0 protected=0 periodic=0 reflect=0 added=0 controls=- vamp=0.00 lethal={1 if after == '0.00' else 0} pool_bp=0")


def heartbeat(t, contender_id, pos, hp="620.00/620.00"):
    return (f"LD_HEARTBEAT t={t} contender={contender_id} phase=Alive life=1 pos={pos} hp={hp} mp=280.00/280.00 shield=0.00 "
            f"level=6 xp={SUPPLY} pending=0 order=- order_at=- action=- statuses=- protected_until=0 respawn_at=0")


def choice(t, contender_id, ordinal, card, result_kind, slot, stored, level, pending):
    return (f"LD_CHOICE t={t} contender={contender_id} ordinal={ordinal} kind=ActiveChoice card={card} result={result_kind} "
            f"slot={slot} stored={stored} level={level} pending={pending}")


def trace(*extra, drop=()):
    lines = [
        ("begin", f"LD_EVAL_CASE_BEGIN case=c000a-t index=0 seed=7 subject={S} opponent={O}"),
        ("start", "LD_MATCH_STARTED generation=2 contenders=3 open_at_ms=185000 countdown_ms=5000 started_at_ms=5000"),
        ("s_offer1", f"LD_OFFER t=0 contender={S} ordinal=1 level=1 cards=active_elbow,active_open_mic,active_unplug"),
        ("o_offer1", f"LD_OFFER t=0 contender={O} ordinal=1 level=1 cards=active_open_mic,active_snark_bolt,active_unplug"),
        ("s_choice1", choice(33, S, 1, "active_elbow", "learned", "Q", "-", 1, 0)),
        ("o_choice1", choice(33, O, 1, "active_open_mic", "learned", "Q", "-", 1, 0)),
        ("s_reward", f"LD_REWARD t=5016 contender={S} cause=dev_fixture role=recipient xp=600 xp_after=600 level=1->6 "),
        ("s_level", f"LD_LEVEL_UP t=5016 contender={S} level=1->6 choices_earned=2 unspent_points=5"),
        ("o_reward", f"LD_REWARD t=5016 contender={O} cause=dev_fixture role=recipient xp=600 xp_after=600 level=1->6 "),
        ("o_level", f"LD_LEVEL_UP t=5016 contender={O} level=1->6 choices_earned=2 unspent_points=5"),
        ("s_offer2", f"LD_OFFER t=5016 contender={S} ordinal=2 level=6 cards=active_flyer_trap,active_seethe,active_stare_down"),
        ("o_offer2", f"LD_OFFER t=5016 contender={O} ordinal=2 level=6 cards=active_stare_down,active_calculus_drop,active_seethe"),
        ("s_choice2", choice(5033, S, 2, "active_flyer_trap", "learned", "W", "-", 6, 1)),
        ("o_choice2", choice(5033, O, 2, "active_stare_down", "learned", "W", "-", 6, 1)),
        ("s_offer3", f"LD_OFFER t=5033 contender={S} ordinal=3 level=6 cards=active_not_mad,active_snark_bolt,active_quick_meeting"),
        ("o_offer3", f"LD_OFFER t=5033 contender={O} ordinal=3 level=6 cards=active_not_mad,active_bail_out,active_triple_combo"),
        ("s_choice3", choice(5283, S, 3, "active_not_mad", "learned", "E", "-", 6, 0)),
        ("o_choice3", choice(5283, O, 3, "active_bail_out", "blueprint", "-", "NotSafe", 6, 0)),
        ("s_points", f"LD_BUILD t=5300 contender={S} kind=AllocatePoints card=- slot=- points=2,0,1,2,0 unspent=0 level=6"),
        ("o_points", f"LD_BUILD t=5300 contender={O} kind=AllocatePoints card=- slot=- points=0,2,1,0,2 unspent=0 level=6"),
        ("s_heartbeat", heartbeat(6000, S, "100,0,0")),
        ("o_heartbeat", heartbeat(6000, O, "-100,5,0")),
        ("o_learn", f"LD_BUILD t=9300 contender={O} kind=LearnBlueprint card=active_bail_out slot=E points=- unspent=0 level=6"),
        ("hit1", hit(10000, S, O, "100.00", "0.00", "100.00", "620.00", "520.00")),
        ("hit2", hit(11000, O, S, "50.00", "10.00", "40.00", "620.00", "580.00")),
        ("lethal", hit(20000, S, O, "600.00", "0.00", "520.00", "520.00", "0.00")),
        ("death", f"LD_DEATH_RECEIPT contender={O} life=1 respawn_at_ms=35000 t=20000 killer={S} cause=Contender fatal=basic fatal_body={S} assisters=- no_reward=None victim_level=6"),
        ("death_reward", f"LD_REWARD t=20000 contender={S} cause=death role=killer xp=140 xp_after=740 level=6->6 victim={O} victim_life=1 victim_kind=contender"),
        ("late_heartbeat", heartbeat(21000, S, "101,0,0", hp="580.00/620.00")),
    ]
    lines = [line for key, line in lines if key not in drop]
    return "\n".join(lines + list(extra)) + "\n"


class AdapterCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.caps = lockdown.describe(ROOT)
        cls.content = json.loads((ROOT / "Content/Lockdown/Data/Demo/demo_content.json").read_text(encoding="utf-8"))

    def evidence(self, value=None, text=None, plan_manifest=None, case=None, count_lines=True):
        case = case or {"case_id": "c000a-t", "seed": "7", "arm": "t", "block": "c000a", "side": 0}
        text = text if text is not None else trace()
        value = dict(value or result())
        if count_lines:
            value["trace_lines"] = len(text.splitlines())
        return lockdown.case_evidence(case, plan_manifest or manifest(), self.caps, value, text)


class CapabilitiesTest(AdapterCase):
    def test_capabilities_are_the_runtime_catalog(self):
        manifest_bytes = (ROOT / "Content/Lockdown/Data/Demo/manifest.json").read_bytes()
        self.assertEqual(self.caps["content"]["digest"], hashlib.sha256(manifest_bytes).hexdigest(),
                         "the content digest is the native catalog root")
        actives = {row["id"]: bool(row.get("opening")) for row in self.content["abilities"]}
        stats = {row["id"] for row in self.content["stats"]}
        self.assertEqual(set(self.caps["mechanics"]), set(actives) | stats)
        self.assertTrue(all(row["status"] == "Supported" for row in self.caps["mechanics"].values()))
        for active, opening in actives.items():
            self.assertEqual((self.caps["mechanics"][active]["kind"], self.caps["mechanics"][active]["opening"]),
                             ("active", opening))
        self.assertTrue(all(self.caps["mechanics"][stat]["kind"] == "stat" for stat in stats))
        self.assertEqual(set(self.caps["decisions"]), {"ai_select", "option_index", "definition", "preset_offer"})
        for decision in lockdown.UNPLANNABLE.values():
            self.assertNotIn(decision, self.caps["decisions"], "replacing, allocating and learning are never plannable")
        self.assertEqual(self.caps["decisions"]["preset_offer"]["sample_classes"], ["diagnostic"])
        duel = self.caps["scenarios"]["duel_site"]
        self.assertIn("capture", duel["parameters"]["site"])
        self.assertEqual(duel["parameters"]["duel_timeout_ms"][1], self.content["boss"]["appear_at"]["ms"])
        self.assertEqual(duel["parameters"]["active_offer_levels"], self.content["progression"]["active_offer_levels"])
        self.assertEqual(duel["evidence_level"], "NativeSpatial")
        self.assertEqual(lockdown.level_for(self.content["progression"], 180), 3)
        self.assertEqual(lockdown.level_for(self.content["progression"], 179), 2)
        self.assertEqual([lockdown._opportunities(self.caps, xp) for xp in (0, 179, 180, 599, 600)], [1, 1, 2, 2, 3])


class PlanTest(AdapterCase):
    def requirements(self, subject, supply=SUPPLY):
        return lockdown.requirements(manifest()["scenario"] | {"parameters": {**manifest()["scenario"]["parameters"],
                                                                                  "supply_xp": {"subject": supply, "opponent": supply}}},
                                     {"subject": subject, "opponent": {"choices": ["ai_select"] * lockdown._opportunities(self.caps, supply)}},
                                     self.caps)

    def plan_cases(self, subject, sample="legal_search"):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "m.json"
            path.write_text(json.dumps(manifest(subject=subject, sample=sample)), encoding="utf-8")
            run = Path(temp) / "run"
            _, cases = core.plan(path, run, ROOT)
            return cases, (run / "native_input.json").exists()

    def test_known_illegal_plans_are_refused_before_any_run(self):
        needs = self.requirements({"choices": ["ai_select"] * 2})
        self.assertTrue(any("has 2 rules but its supply of 600 XP gives 3 active opportunities" in item for item in needs["problems"]))
        needs = self.requirements({"choices": ["ai_select"] * 2}, supply=180)
        self.assertEqual(needs["problems"], [], "180 XP gives the opening opportunity and the level-3 one")
        needs = self.requirements({"choices": [{"rule": "definition", "definition": "active_stare_down"}, "ai_select", "ai_select"]})
        self.assertTrue(any("ordinal 1 can never offer active_stare_down" in item for item in needs["problems"]))
        needs = self.requirements({"choices": ["ai_select", {"rule": "definition", "definition": "active_stare_down"}, "ai_select"]})
        self.assertEqual(needs["problems"], [], "later opportunities draw from every active not held")
        needs = self.requirements({"choices": ["ai_select", {"rule": "definition", "definition": "stat_strength"}, "ai_select"]})
        self.assertTrue(any("stat_strength is a stat, never an offered active" in item for item in needs["problems"]))
        needs = self.requirements({"choices": ["ai_select"] * 3, "presets": [["active_seethe", "stat_strength"]]})
        self.assertTrue(any("presets name stat stat_strength" in item for item in needs["problems"]))
        self.assertIn("preset_offer", needs["decisions"])
        needs = self.requirements({"choices": ["ai_select"] * 3, "presets": [["active_seethe"], ["active_not_mad"], ["active_bail_out"]]})
        self.assertTrue(any("prepares 3 offers for 2 later opportunities" in item for item in needs["problems"]))
        with self.assertRaises(ValueError):
            self.requirements({"choices": ["best_card", "ai_select", "ai_select"]})
        with self.assertRaises(ValueError):
            self.requirements({"choices": [{"rule": "option_index", "index": 3}, "ai_select", "ai_select"]})
        with self.assertRaises(ValueError):
            self.requirements({"choices": [{"rule": "ai_select", "index": 0}, "ai_select", "ai_select"]})
        for invalid in (True, False, 1.0):
            with self.subTest(index=invalid), self.assertRaisesRegex(ValueError, "integer index"):
                self.requirements({"choices": [{"rule": "option_index", "index": invalid}, "ai_select", "ai_select"]})
        for presets in ([["active_seethe", [], "active_not_mad"]], [[]], [["active_seethe", "active_not_mad", "active_bail_out", "active_unplug"]],
                        [["active_seethe", "active_seethe"]]):
            with self.subTest(presets=presets), self.assertRaisesRegex(ValueError, "one to three distinct active ids"):
                self.requirements({"choices": ["ai_select"] * 3, "presets": presets})
        for retired in ({"choices": ["ai_select"] * 3, "rerolls": 1},
                        {"choices": [{"rule": "ai_select", "reroll": True}, "ai_select", "ai_select"]},
                        {"choices": [{"rule": "definition", "definition": "active_seethe", "rank": 2}, "ai_select", "ai_select"]}):
            with self.subTest(plan=retired), self.assertRaises(ValueError):
                self.requirements(retired)

    def test_plans_requiring_a_replacement_allocation_or_learn_are_unsupported_decisions(self):
        plans = {
            "replace_active": {"choices": ["ai_select", {"rule": "definition", "definition": "active_seethe", "replace": "Q"}, "ai_select"]},
            "allocate_points": {"choices": ["ai_select"] * 3, "points": {"stat_strength": 2}},
            "learn_blueprint": {"choices": ["ai_select"] * 3, "learn": [{"definition": "active_seethe", "slot": "E"}]},
        }
        for decision, subject in plans.items():
            with self.subTest(decision=decision):
                self.assertIn(decision, self.requirements(subject)["decisions"])
                cases, native_written = self.plan_cases(subject)
                self.assertTrue(all(case["status"] == "Unsupported" for case in cases))
                reasons = {case["arm"]: case["reason"] for case in cases}
                self.assertIn(f"UnsupportedDecision: {decision}", reasons["t"])
                self.assertIn("DesignIncomplete", reasons["ref"])
                self.assertFalse(native_written, "an unsupported decision is never mapped onto a runnable plan")
        self.assertIn("replace_active",
                      self.requirements({"choices": ["ai_select"] * 3, "replace": {"ordinal": 2, "slot": "W"}})["decisions"])

    def test_boolean_option_indices_are_unsupported_before_native_execution(self):
        for index in (True, False):
            with self.subTest(index=index):
                cases, native_written = self.plan_cases({"choices": [{"rule": "option_index", "index": index}, "ai_select", "ai_select"]})
                self.assertTrue(all(case["status"] == "Unsupported" for case in cases))
                self.assertTrue(any("integer index" in case["reason"] for case in cases))
                self.assertFalse(native_written)

    def test_an_uncatalogued_candidate_is_unsupported_end_to_end(self):
        for definition in ("passive_debt_collector", "attr_protein", "opening_athlete"):
            subject = {"choices": ["ai_select", {"rule": "definition", "definition": definition}, "ai_select"]}
            with self.subTest(definition=definition), tempfile.TemporaryDirectory() as temp:
                path = Path(temp) / "m.json"
                path.write_text(json.dumps(manifest(subject=subject)), encoding="utf-8")
                code = core.main(["plan", "--manifest", str(path), "--run-dir", str(Path(temp) / "run"), "--project-root", str(ROOT)])
                cases = core.read_cases(Path(temp) / "run")
                self.assertEqual(code, 3)
                reasons = {case["arm"]: case["reason"] for case in cases}
                self.assertIn(f"UnsupportedMechanic: {definition} is not in the adapter's runtime catalog", reasons["t"])
                self.assertTrue(all(case["status"] == "Unsupported" for case in cases))

    def test_presets_are_admitted_only_in_a_diagnostic_sample(self):
        subject = {"choices": ["ai_select", {"rule": "definition", "definition": "active_stare_down"}, "ai_select"],
                   "presets": [["active_stare_down", "active_seethe", "active_not_mad"]]}
        for sample, expected in (("legal_search", "Unsupported"), ("diagnostic", "NotRun")):
            cases, _ = self.plan_cases(subject, sample)
            self.assertEqual({case["status"] for case in cases if case["arm"] == "t"}, {expected})

    def test_native_input_swaps_sides_and_keeps_exact_seeds(self):
        value = manifest(subject={"choices": ["ai_select", {"rule": "option_index", "index": 2},
                                              {"rule": "definition", "definition": "active_not_mad"}],
                                  "presets": [["active_not_mad", "active_seethe"]]}, sample="diagnostic")
        value["resolved"] = {}
        cases = [{"case_id": "c000a-t", "seed": "18446744073709551615", "arm": "t", "side": 0},
                 {"case_id": "c000b-t", "seed": "18446744073709551615", "arm": "t", "side": 1}]
        native = lockdown.native_input("run-1", value, self.caps, cases)
        self.assertEqual((native["schema"], native["schema_version"], native["content_root"]),
                         (lockdown.NATIVE_INPUT_SCHEMA, 2, self.caps["content"]["digest"]))
        first, second = native["cases"]
        self.assertEqual(set(first), {"case_id", "seed", "site", "duel_timeout_ms", "wall_timeout_s", "contenders"})
        self.assertEqual(first["seed"], "18446744073709551615")
        self.assertEqual([(row["role"], row["slot"]) for row in first["contenders"]], [("subject", 0), ("opponent", 1)])
        self.assertEqual([(row["role"], row["slot"]) for row in second["contenders"]], [("subject", 1), ("opponent", 0)])
        subject, opponent = first["contenders"]
        self.assertEqual(set(subject), {"role", "slot", "supply_xp", "choices", "presets"}, "the native contender keys")
        self.assertEqual(subject["supply_xp"], SUPPLY)
        self.assertEqual(subject["choices"], [{"rule": "ai_select"}, {"rule": "option_index", "index": 2},
                                              {"rule": "definition", "definition": "active_not_mad"}])
        self.assertEqual(subject["presets"], [["active_not_mad", "active_seethe"]])
        self.assertEqual((opponent["choices"], opponent["presets"]), ([{"rule": "ai_select"}] * 3, []))
        self.assertEqual(first["wall_timeout_s"], 120)


class EvidenceTest(AdapterCase):
    def test_a_decided_duel_measures_only_committed_hits_up_to_the_outcome(self):
        out = self.evidence(text=trace(hit(21000, S, O, "9.00", "0.00", "9.00", "0.00", "0.00")))
        self.assertEqual(out["status"], "Completed", out["reason"])
        metrics = out["metrics"]
        self.assertEqual(metrics["duel_score"], {"value": 1, "censored": False})
        self.assertEqual(metrics["damage_dealt_hp"]["value"], 620.0)
        self.assertEqual(metrics["damage_taken_hp"]["value"], 40.0)
        self.assertEqual(metrics["subject_hp_end_bp"]["value"], 5000)
        self.assertEqual(metrics["decision_time_ms"], {"value": 15000, "censored": False})
        facts = out["facts"]
        self.assertEqual(facts["offers"]["subject"]["2"], ["active_flyer_trap", "active_seethe", "active_stare_down"])
        self.assertEqual(facts["choices"]["subject"]["1"], "active_elbow→Q", "a learned choice names its slot")
        self.assertEqual(facts["choices"]["subject"]["2"], "active_flyer_trap→W")
        self.assertEqual(facts["choices"]["opponent"]["3"], "active_bail_out→blueprint", "a stored choice is a blueprint")
        self.assertEqual([(step["kind"], step["card"], step["slot"], step["points"]) for step in facts["build_steps"]["opponent"]],
                         [("AllocatePoints", "-", "-", "0,2,1,0,2"), ("LearnBlueprint", "active_bail_out", "E", "-")])
        self.assertEqual(facts["builds"]["subject"]["actives"], ["active_elbow/Q", "active_flyer_trap/W", "active_not_mad/E"])
        self.assertEqual(facts["builds"]["subject"]["stat_points"]["stat_strength"], 2)
        self.assertEqual(facts["builds"]["opponent"]["blueprints"], [])

    def test_a_choice_kept_as_a_blueprint_is_held_at_the_end(self):
        value = result()
        value["contenders"][1]["actives"] = value["contenders"][1]["actives"][:2]
        value["contenders"][1]["blueprints"] = ["active_bail_out"]
        out = self.evidence(value, trace(drop=("o_learn",)))
        self.assertEqual(out["status"], "Completed", out["reason"])
        self.assertEqual(out["facts"]["builds"]["opponent"]["blueprints"], ["active_bail_out"])
        value["contenders"][1]["blueprints"] = []
        self.assertIn("choice active_bail_out is neither learned nor a blueprint at the end",
                      self.evidence(value, trace(drop=("o_learn",)))["reason"])

    def test_an_undecided_duel_is_censored_not_lost(self):
        value = result(kind="timeout", ended_at_ms=50000)
        value["contenders"][1] = contender("opponent", 1, O, hp=10000)
        out = self.evidence(value, trace(drop=("lethal", "death", "death_reward")))
        self.assertEqual(out["status"], "Completed", out["reason"])
        self.assertEqual(out["metrics"]["duel_score"], {"value": 0, "censored": True})
        self.assertEqual(out["metrics"]["decision_time_ms"], {"value": 45000, "censored": True})

    def test_non_supply_rewards_at_the_outcome_boundary_are_uncontrolled(self):
        timeout = result(kind="timeout", ended_at_ms=50000)
        timeout["contenders"][1] = contender("opponent", 1, O, hp=10000)
        boundary = f"LD_REWARD t=50000 contender={S} cause=map_event role=recipient xp=40 xp_after=640 level=6->6 "
        out = self.evidence(timeout, trace(boundary, drop=("lethal", "death", "death_reward")))
        self.assertEqual(out["status"], "Invalid")
        self.assertIn("UncontrolledXP", out["reason"])
        after = boundary.replace("t=50000", "t=50001")
        self.assertEqual(self.evidence(timeout, trace(after, drop=("lethal", "death", "death_reward")))["status"], "Completed")
        self.assertEqual(self.evidence()["status"], "Completed", "the deciding kill's own reward is exempt")
        killer_reward = (f"LD_REWARD t=20000 contender={S} cause=death role=killer xp=140 xp_after=740 level=6->6 "
                         f"victim={O} victim_life=1 victim_kind=contender")
        self.assertIn(killer_reward, trace())
        for original, replacement in (("cause=death", "cause=map_event"), (f"victim={O}", f"victim={CAMP}"),
                                      ("role=killer", "role=assist"), (f"contender={S}", f"contender={O}")):
            with self.subTest(replacement=replacement):
                out = self.evidence(text=trace().replace(killer_reward, killer_reward.replace(original, replacement, 1)))
                self.assertEqual(out["status"], "Invalid")
                self.assertIn("UncontrolledXP", out["reason"])
        extra = f"LD_REWARD t=20000 contender={O} cause=map_event role=recipient xp=40 xp_after=640 level=6->6 "
        self.assertIn("UncontrolledXP", self.evidence(text=trace(extra))["reason"])

    def test_contamination_and_missing_evidence_invalidate_the_case(self):
        cases = {
            "ExternalInteraction": trace(hit(12000, CAMP, S, "30.00", "0.00", "30.00", "580.00", "550.00", "monster_scavenger")),
            "UncontrolledXP": trace(f"LD_REWARD t=12000 contender={S} cause=death role=killer xp=40 xp_after=640 level=6->6 "),
            "SupplyMismatch": trace(drop=("o_reward",)),
            "TraceIncomplete": trace(drop=("death",)),
        }
        for reason, text in cases.items():
            out = self.evidence(text=text)
            self.assertEqual(out["status"], "Invalid", reason)
            self.assertIn(reason, out["reason"])
            self.assertEqual(out["metrics"], {}, "an invalid Case has no metric")
        out = self.evidence(result(killer="other"))
        self.assertIn("ExternalKill", out["reason"])
        value = result()
        value["contenders"][0]["choices"] = 2
        self.assertIn("PlanNotReached", self.evidence(value)["reason"])
        value = result()
        value["contenders"][1]["supply_status"] = "Pending"
        self.assertIn("SupplyMissing: the opponent supply is Pending", self.evidence(value)["reason"])
        native = result(status="BudgetExceeded", reason="the Case exceeded its wall-clock budget")
        self.assertEqual((self.evidence(native)["status"], self.evidence(native)["reason"]),
                         ("BudgetExceeded", "the Case exceeded its wall-clock budget"))
        self.assertIn("another Case", self.evidence(result(seed="8"))["reason"])
        self.assertIn("another schema", self.evidence(result(schema_version=1))["reason"], "the retired result schema")

    def test_malformed_events_and_terminal_values_are_rejected_without_crashing(self):
        traces = (
            trace().replace("LD_DEATH_RECEIPT contender=", "LD_DEATH_RECEIPT missing=", 1).replace("t=20000 killer=", "t=bad killer=", 1),
            trace().replace("LD_HIT t=10000", "LD_HIT t=missing", 1),
            trace().replace("cause=dev_fixture role=recipient xp=600", "cause=dev_fixture role=recipient xp=bad", 1),
        )
        for text in traces:
            with self.subTest(trace=text[:60]):
                out = self.evidence(text=text)
                self.assertEqual(out["status"], "Invalid")
                self.assertIn("TraceIncomplete", out["reason"])
        for key, invalid in (("slot", False), ("hp_p2", None), ("actives", [None]), ("actives", [{"id": "active_elbow", "slot": "R"}]),
                             ("blueprints", [1]), ("stat_points", [{"id": "stat_agility", "points": -1}]),
                             ("unspent_points", True), ("blueprints", None)):
            value = result()
            value["contenders"][0][key] = invalid
            with self.subTest(field=key, value=invalid):
                out = self.evidence(value)
                self.assertEqual(out["status"], "Invalid")
                self.assertIn("TraceIncomplete", out["reason"])
        value = result()
        del value["contenders"][0]["stat_points"]
        self.assertIn("terminal progression are missing", self.evidence(value)["reason"])
        premature = result(kind="timeout", ended_at_ms=49999)
        self.assertIn("ended before the duel horizon",
                      self.evidence(premature, trace(drop=("lethal", "death", "death_reward")))["reason"])

    def test_every_planned_offer_and_choice_is_required_for_both_sides(self):
        for key in ("begin", "start", "s_offer1", "o_offer1", "s_choice1", "o_choice1",
                    "s_offer2", "o_offer2", "s_choice2", "o_choice2", "s_offer3", "o_offer3",
                    "s_choice3", "o_choice3"):
            with self.subTest(missing=key):
                out = self.evidence(text=trace(drop=(key,)))
                self.assertEqual(out["status"], "Invalid", out["reason"])
                self.assertIn("TraceIncomplete", out["reason"])
        out = self.evidence(text="")
        self.assertEqual(out["status"], "Invalid")
        self.assertIn("TraceIncomplete", out["reason"])
        zero_xp = manifest(supply=0)
        for arm in zero_xp["design"]["arms"]:
            for role in lockdown.ROLES:
                arm["plan"][role]["choices"] = ["ai_select"]
        value = result(kind="timeout")
        for row in value["contenders"]:
            row.update(level=1, xp=0, choices=1, planned_choices=1, supply_xp=0, supply_status="None")
        self.assertIn("TraceIncomplete", self.evidence(value, "", zero_xp)["reason"])
        self.assertIn("line count", self.evidence(result(), trace(), count_lines=False)["reason"])

    def test_committed_choice_must_match_its_offer_and_plan(self):
        bad_choice = trace().replace("card=active_flyer_trap result=learned", "card=active_unknown result=learned")
        self.assertIn("choice is not in its offer", self.evidence(text=bad_choice)["reason"])
        value = result()
        value["contenders"][0]["actives"] = value["contenders"][0]["actives"][:2]
        self.assertIn("choice active_not_mad is neither learned nor a blueprint at the end", self.evidence(value)["reason"])
        early_choice = trace().replace(f"LD_CHOICE t=5033 contender={S} ordinal=2",
                                       f"LD_CHOICE t=5000 contender={S} ordinal=2")
        self.assertIn("inconsistent offer or choice times", self.evidence(text=early_choice)["reason"])
        subject_choice2 = choice(5033, S, 2, "active_flyer_trap", "learned", "W", "-", 6, 1)
        bad_level = trace().replace(subject_choice2, subject_choice2.replace("level=6", "level=0"))
        self.assertIn("invalid level", self.evidence(text=bad_level)["reason"])
        late_level = trace().replace(subject_choice2, subject_choice2.replace("level=6", "level=7"))
        self.assertIn("inconsistent offer or choice times or levels", self.evidence(text=late_level)["reason"],
                      "a choice cannot be committed above the terminal level")
        plan = manifest(subject={"choices": ["ai_select", {"rule": "option_index", "index": 1}, "ai_select"]})
        self.assertIn("planned option index", self.evidence(plan_manifest=plan)["reason"])
        plan = manifest(subject={"choices": ["ai_select", {"rule": "definition", "definition": "active_seethe"}, "ai_select"]})
        self.assertIn("planned definition", self.evidence(plan_manifest=plan)["reason"])
        for definition in ("active_flyer_trap", "active_not_mad"):
            ordinal = 2 if definition == "active_flyer_trap" else 3
            choices = ["ai_select"] * 3
            choices[ordinal - 1] = {"rule": "definition", "definition": definition}
            out = self.evidence(plan_manifest=manifest(subject={"choices": choices}))
            self.assertEqual(out["status"], "Completed", out["reason"])
        unplanned = choice(5400, S, 4, "active_seethe", "blueprint", "-", "NoEmptySlot", 6, 0)
        self.assertIn("committed unplanned ordinal 4", self.evidence(text=trace(unplanned))["reason"])

    def test_choice_metadata_required_for_prefix_cannot_be_missing_or_contradictory(self):
        subject_choice2 = choice(5033, S, 2, "active_flyer_trap", "learned", "W", "-", 6, 1)
        opponent_choice3 = choice(5283, O, 3, "active_bail_out", "blueprint", "-", "NotSafe", 6, 0)
        variants = {
            "missing kind": (subject_choice2, subject_choice2.replace("kind=ActiveChoice ", "")),
            "missing result": (subject_choice2, subject_choice2.replace("result=learned ", "")),
            "missing slot": (subject_choice2, subject_choice2.replace("slot=W ", "")),
            "missing stored": (subject_choice2, subject_choice2.replace("stored=- ", "")),
            "missing pending": (subject_choice2, subject_choice2.replace(" pending=1", "")),
            "another command kind": (subject_choice2, subject_choice2.replace("kind=ActiveChoice", "kind=LearnBlueprint")),
            "unknown result": (subject_choice2, subject_choice2.replace("result=learned", "result=replaced")),
            "learned without a slot": (subject_choice2, subject_choice2.replace("slot=W", "slot=-")),
            "learned with a stored reason": (subject_choice2, subject_choice2.replace("stored=-", "stored=NotSafe")),
            "blueprint in a slot": (opponent_choice3, opponent_choice3.replace("slot=-", "slot=E")),
            "blueprint without stored": (opponent_choice3, opponent_choice3.replace("stored=NotSafe ", "")),
        }
        for name, (original, replacement) in variants.items():
            with self.subTest(variant=name):
                self.assertNotEqual(original, replacement)
                out = self.evidence(text=trace().replace(original, replacement, 1))
                self.assertEqual(out["status"], "Invalid")
                self.assertIn("TraceIncomplete", out["reason"])
                self.assertIn("incomplete choice metadata", out["reason"])

    def test_an_offer_is_drawn_once_before_its_choice(self):
        offer = f"LD_OFFER t=5016 contender={S} ordinal=2 level=6 cards=active_flyer_trap,active_seethe,active_stare_down"
        redraw = f"LD_OFFER t=5020 contender={S} ordinal=2 level=6 cards=active_flyer_trap,active_unplug,active_bail_out"
        out = self.evidence(text=trace().replace(offer, offer + "\n" + redraw))
        self.assertEqual(out["status"], "Invalid")
        self.assertIn("duplicate or out-of-order offer evidence", out["reason"], "a second draw of one ordinal is not legal")
        malformed = trace().replace(offer, offer.replace("active_stare_down", "active_seethe"))
        self.assertIn("malformed offer", self.evidence(text=malformed)["reason"])
        four = trace().replace(offer, offer + ",active_unplug")
        self.assertIn("malformed offer", self.evidence(text=four)["reason"])

    def test_a_post_horizon_kill_in_old_evidence_is_not_a_valid_result(self):
        value = result()
        value["outcome"]["t_ms"] = 45001
        late = trace().replace("t=20000", "t=50001")
        value["ended_at_ms"] = 50016
        out = self.evidence(value, late)
        self.assertEqual(out["status"], "Invalid")
        self.assertIn("after the duel horizon", out["reason"])

    def test_a_hit_line_that_contradicts_itself_is_an_evidence_violation(self):
        broken = trace().replace("hp_lost=40.00 hp=620.00->580.00", "hp_lost=40.00 hp=620.00->590.00")
        out = self.evidence(text=broken)
        self.assertTrue(any("hp_lost 40.0" in item for item in out["violations"]), out["violations"])
        self.assertEqual(lockdown.hit_violations(lockdown.parse_line(hit(1, S, O, "1.00", "0.00", "2.00", "5.00", "3.00"))[1]),
                         ["LD_HIT t=1 hp_lost + absorbed exceeds dmg"])

    def test_a_prepared_offer_must_be_the_offer_drawn(self):
        subject = {"choices": ["ai_select", {"rule": "definition", "definition": "active_flyer_trap"}, "ai_select"],
                   "presets": [["active_flyer_trap", "active_seethe", "active_stare_down"]]}
        plan = manifest(subject=subject, sample="diagnostic")
        value = result()
        value["contenders"][0].update(presets_requested=1, presets_queued=1)
        out = self.evidence(value, plan_manifest=plan)
        self.assertEqual(out["status"], "Completed", out["reason"])
        drifted = trace().replace("cards=active_flyer_trap,active_seethe,active_stare_down",
                                  "cards=active_seethe,active_flyer_trap,active_stare_down")
        self.assertIn("PresetNotApplied", self.evidence(value, drifted, plan)["reason"])
        value["contenders"][0]["presets_queued"] = 0
        self.assertIn("PresetNotApplied: the subject queued 0 of 1 presets", self.evidence(value, plan_manifest=plan)["reason"])

    def test_branch_label_and_prefix(self):
        subject = {"choices": ["ai_select", {"rule": "option_index", "index": 0}, "ai_select"]}
        out = self.evidence(plan_manifest=manifest(subject=subject))
        self.assertEqual(out["facts"]["label"], "active_flyer_trap→W")
        prefix = json.loads(out["facts"]["prefix"])
        self.assertEqual(prefix["branch"]["ordinal"], 2)
        self.assertEqual(prefix["branch"]["offer"]["cards"], ["active_flyer_trap", "active_seethe", "active_stare_down"])
        self.assertEqual(prefix["history"][0]["choice"]["card"], "active_elbow")
        self.assertEqual((prefix["history"][0]["choice"]["result"], prefix["history"][0]["choice"]["slot"]), ("learned", "Q"))
        self.assertTrue(out["facts"]["random_fingerprint"])
        # The arm keeping the production choice is compared at the design's decision point, on the same prefix.
        reference = self.evidence(plan_manifest=manifest(subject=subject),
                                  case={"case_id": "c000a-t", "seed": "7", "arm": "ref", "block": "c000a", "side": 0})
        self.assertEqual(reference["facts"]["prefix"], out["facts"]["prefix"])
        self.assertEqual(reference["facts"]["label"], "active_flyer_trap→W")

    def test_branch_point_follows_the_first_differing_normalized_rule(self):
        common = {"rule": "definition", "definition": "active_elbow"}
        plan = manifest(subject={"choices": [common, {"rule": "option_index", "index": 0}, "ai_select"]})
        plan["design"]["arms"][0]["plan"]["subject"]["choices"] = [
            common, {"rule": "option_index", "index": 1}, {"rule": "ai_select"}]
        self.assertEqual(lockdown.branch_ordinal(plan["design"]), 2)
        out = self.evidence(plan_manifest=plan)
        self.assertEqual(out["facts"]["label"], "active_flyer_trap→W")
        prefix = json.loads(out["facts"]["prefix"])
        self.assertEqual(prefix["history"][0]["offer"]["cards"][0], "active_elbow")
        self.assertEqual(prefix["history"][0]["choice"]["card"], "active_elbow")
        self.assertEqual(prefix["history"][0]["choice"]["kind"], "ActiveChoice")
        self.assertEqual(prefix["branch"]["ordinal"], 2)
        plan["design"]["arms"][0]["plan"]["subject"]["choices"][1] = {"rule": "option_index", "index": 0}
        self.assertIsNone(lockdown.branch_ordinal(plan["design"]), "ai_select spellings normalize to one rule")

    def test_a_different_prior_choice_outcome_blocks_pairing_even_when_definition_ids_match(self):
        subject = {"choices": ["ai_select", "ai_select", {"rule": "option_index", "index": 0}]}
        plan = manifest(subject=subject, design="decision")
        plan["design"].pop("reference")
        plan["blocks"]["sides"] = 1
        learned = choice(5033, S, 2, "active_flyer_trap", "learned", "W", "-", 6, 1)
        stored = choice(5033, S, 2, "active_flyer_trap", "blueprint", "-", "NotSafe", 6, 1)
        learn_later = f"LD_BUILD t=5300 contender={S} kind=LearnBlueprint card=active_flyer_trap slot=W points=- unspent=5 level=6"

        def case(arm, text):
            case_id = f"c000a-{arm}"
            row = {"case_id": case_id, "seed": "7", "arm": arm, "block": "c000a",
                   "split": "confirm", "index": 0, "side": 0}
            text = text.replace("LD_EVAL_CASE_BEGIN case=c000a-t", f"LD_EVAL_CASE_BEGIN case={case_id}")
            evidence = self.evidence(result(case_id=case_id), text, plan, row)
            self.assertEqual(evidence["status"], "Completed", evidence["reason"])
            return {**row, **evidence}

        reference = case("ref", trace())
        same = case("t", trace())
        changed = case("t", trace(learn_later).replace(learned, stored))
        self.assertEqual(reference["facts"]["offers"], changed["facts"]["offers"])
        self.assertEqual(reference["facts"]["prefix"], same["facts"]["prefix"])
        self.assertNotEqual(reference["facts"]["prefix"], changed["facts"]["prefix"])
        for treatment, expected in ((same, 1), (changed, 0)):
            metrics = core.compute_metrics(plan, self.caps, [reference, treatment])
            score = metrics["splits"]["confirm"]["metrics"]["duel_score"]
            self.assertEqual(score["contrasts"][0]["n"], expected)
            self.assertEqual(score["decision"]["blocks"], expected)
        findings = core.compute_findings(plan, self.caps, [reference, changed], metrics, [], [], None, {})
        self.assertTrue(any(finding["conclusion"] == "RuleViolation" for finding in findings["findings"]))

    def test_prefix_preserves_prior_choice_metadata(self):
        plan = manifest(subject={"choices": ["ai_select", "ai_select", {"rule": "option_index", "index": 0}]})
        value = result()
        ids = {"subject": S, "opponent": O}
        base = trace()
        prefix = json.loads(lockdown.facts({}, plan, value, lockdown.parse_trace(base), ids)["prefix"])
        self.assertEqual(prefix["branch"]["ordinal"], 3)
        prior = prefix["history"][1]
        self.assertEqual(prior["offer"]["cards"], ["active_flyer_trap", "active_seethe", "active_stare_down"])
        self.assertEqual({key: prior["choice"][key] for key in ("kind", "card", "result", "slot", "stored", "level", "pending")},
                         {"kind": "ActiveChoice", "card": "active_flyer_trap", "result": "learned", "slot": "W",
                          "stored": "-", "level": "6", "pending": "1"})
        subject_choice2 = choice(5033, S, 2, "active_flyer_trap", "learned", "W", "-", 6, 1)
        for original, replacement in ((subject_choice2, choice(5033, S, 2, "active_flyer_trap", "blueprint", "-", "NotSafe", 6, 1)),
                                      (subject_choice2, subject_choice2.replace("slot=W", "slot=E")),
                                      (subject_choice2, subject_choice2.replace("pending=1", "pending=0")),
                                      (subject_choice2, subject_choice2.replace("level=6", "level=5")),
                                      (subject_choice2, subject_choice2.replace("t=5033", "t=5034"))):
            with self.subTest(replacement=replacement):
                changed = base.replace(original, replacement, 1)
                self.assertNotEqual(changed, base)
                different = lockdown.facts({}, plan, value, lockdown.parse_trace(changed), ids)["prefix"]
                self.assertNotEqual(json.dumps(prefix, sort_keys=True, ensure_ascii=False),
                                    json.dumps(json.loads(different), sort_keys=True, ensure_ascii=False))

    def test_branch_prefix_preserves_candidate_order_level_and_time(self):
        plan = manifest(subject={"choices": ["ai_select", {"rule": "option_index", "index": 0}, "ai_select"]})
        value = result()
        ids = {"subject": S, "opponent": O}
        base = trace()
        prefix = lockdown.facts({}, plan, value, lockdown.parse_trace(base), ids)["prefix"]
        variations = (
            ("cards=active_flyer_trap,active_seethe,active_stare_down", "cards=active_seethe,active_flyer_trap,active_stare_down"),
            ("cards=active_flyer_trap,active_seethe,active_stare_down", "cards=active_flyer_trap,active_seethe,active_triple_combo"),
            ("cards=active_flyer_trap,active_seethe,active_stare_down", "cards=active_flyer_trap,active_seethe"),
            (f"LD_OFFER t=5016 contender={S} ordinal=2", f"LD_OFFER t=5017 contender={S} ordinal=2"),
            (f"contender={S} ordinal=2 level=6 cards=", f"contender={S} ordinal=2 level=5 cards="),
        )
        for original, replacement in variations:
            with self.subTest(original=original, replacement=replacement):
                changed = base.replace(original, replacement, 1)
                self.assertNotEqual(changed, base)
                self.assertNotEqual(prefix, lockdown.facts({}, plan, value, lockdown.parse_trace(changed), ids)["prefix"])

    def test_the_native_limits_and_schemas_are_the_drivers(self):
        header = (ROOT / "Source/Lockdown/Private/Tests/LDBuildEvaluationProbe.h").read_text(encoding="utf-8")
        self.assertIn(f"inline constexpr int32 MaxCases = {lockdown.NATIVE_MAX_CASES};", header)
        self.assertEqual(self.caps["limits"]["max_cases_per_run"], lockdown.NATIVE_MAX_CASES)
        self.assertIn(f'InputSchema[] = TEXT("{lockdown.NATIVE_INPUT_SCHEMA}");', header)
        self.assertIn(f'ResultSchema[] = TEXT("{lockdown.NATIVE_RESULT_SCHEMA}");', header)
        self.assertIn(f'RunSchema[] = TEXT("{lockdown.NATIVE_RUN_SCHEMA}");', header)
        self.assertIn(f"InputSchemaVersion = {lockdown.NATIVE_SCHEMA_VERSION};", header)
        self.assertIn(f"ResultSchemaVersion = {lockdown.NATIVE_SCHEMA_VERSION};", header)


class ReplayAndIdentityTest(AdapterCase):
    def test_canonical_events_name_roles_and_stop_at_the_end(self):
        events, spatial = lockdown.canonical_events(trace(hit(25000, S, O, "1.00", "0.00", "1.00", "0.00", "0.00")), result())
        self.assertTrue(all(S not in line and O not in line for line in events))
        self.assertTrue(any(line.startswith("LD_HIT t=20000 src=subject") for line in events))
        self.assertTrue(any(line.startswith("LD_CHOICE t=5283 contender=opponent ordinal=3") for line in events))
        self.assertTrue(any(line.startswith("LD_BUILD t=9300 contender=opponent kind=LearnBlueprint") for line in events))
        self.assertFalse(any("t=25000" in line for line in events), "events after the Case's end are not compared")
        self.assertEqual(spatial["opponent@6000"], (-100.0, 5.0, 0.0))
        self.assertNotIn("subject@21000", spatial)

    def test_replay_evidence_contains_terminal_build_and_heartbeat_resources(self):
        run = {"schema": lockdown.NATIVE_RUN_SCHEMA, "schema_version": 2, "content_root": CONTENT_ROOT, "run_id": "r",
               "passed": True, "fixed_time_step": True, "seed_derivation": 1, "fixed_delta_seconds": 1 / 60,
               "product_version": "0.10.0", "engine_version": "5.8.4", "source_commit": "abc", "source_dirty": "false"}
        frozen = manifest()
        frozen["resolved"] = {"run_id": "r"}
        case = {"case_id": "c000a-t", "seed": "7", "arm": "t", "side": 0}
        binary = {"editor_sha256": "a" * 64, "module_sha256": "b" * 64}
        runner = {"binary_digests": binary, "binary_digests_after": dict(binary), "exit_code": 0, "timed_out": False}
        snapshot = lockdown.replay_evidence(case, frozen, self.caps, run, runner, result(), trace())
        self.assertEqual(snapshot["problems"], [])
        terminal = snapshot["terminal"]["contenders"]["subject"]
        self.assertEqual(terminal["hp_p2"], 31000)
        self.assertEqual(terminal["actives"], BUILDS["subject"]["actives"])
        self.assertEqual(terminal["stat_points"], BUILDS["subject"]["stat_points"])
        self.assertEqual((terminal["blueprints"], terminal["unspent_points"]), ([], 0))
        self.assertEqual(snapshot["resources"][0]["mp"], "280.00/280.00")
        value = result()
        del value["contenders"][0]["blueprints"]
        snapshot = lockdown.replay_evidence(case, frozen, self.caps, run, runner, value, trace())
        self.assertTrue(any("terminal contender build" in problem for problem in snapshot["problems"]))
        malformed = trace().replace("mp=280.00/280.00 shield=0.00", "shield=0.00", 1)
        snapshot = lockdown.replay_evidence(case, frozen, self.caps, run, runner, result(), malformed)
        self.assertTrue(any("lacks resource" in problem for problem in snapshot["problems"]))
        snapshot = lockdown.replay_evidence(case, frozen, self.caps, run, runner, result(), "")
        self.assertTrue(any("both duelists" in problem for problem in snapshot["problems"]))
        snapshot = lockdown.replay_evidence(case, frozen, self.caps, run, runner, result(status="BudgetExceeded"), trace())
        self.assertTrue(any("not the completed" in problem for problem in snapshot["problems"]))
        changed = dict(runner, binary_digests_after={"editor_sha256": "a" * 64, "module_sha256": "c" * 64})
        snapshot = lockdown.replay_evidence(case, frozen, self.caps, run, changed, result(), trace())
        self.assertTrue(any("binary identity" in problem for problem in snapshot["problems"]))

    def test_run_identity(self):
        good = {"schema": lockdown.NATIVE_RUN_SCHEMA, "schema_version": 2, "content_root": self.caps["content"]["digest"].upper(),
                "fixed_time_step": True, "seed_derivation": 1}
        self.assertEqual(lockdown.identity_problems(good, self.caps), [])
        self.assertTrue(lockdown.identity_problems(dict(good, schema_version=1), self.caps), "the retired run schema")
        self.assertTrue(lockdown.identity_problems(dict(good, content_root="0" * 64), self.caps))
        self.assertTrue(lockdown.identity_problems(dict(good, fixed_time_step=False), self.caps))
        self.assertTrue(lockdown.identity_problems(dict(good, seed_derivation=2), self.caps))


if __name__ == "__main__":
    unittest.main()
