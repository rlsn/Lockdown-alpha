"""Lockdown adapter tests (Scripts/BuildEvaluationLockdown.py): capabilities from the runtime catalog, plan admission,
native input, evidence judging over synthetic authority traces, canonical replay events and run identity.

The synthetic traces exercise the adapter's parsing and refusals; they are not game evidence. Native Cases run in
RunBuildEvaluation.ps1.
"""

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import BuildEvaluation as core  # noqa: E402
import BuildEvaluationLockdown as lockdown  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
S, O, H, CAMP = "1" * 32, "2" * 32, "3" * 32, "4" * 32


def manifest(subject=None, opponent=None, sample="legal_search", supply=180, design="paired_arms"):
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
    row = {"role": role, "slot": slot, "contender": contender_id, "progression_valid": True, "level": 3, "xp": 180,
           "choices": 3, "planned_choices": 3, "rerolls_left": 0, "opening_card": "opening_athlete", "skills": [], "cards": [],
           "has_body": body, "hp_p2": hp, "max_hp_p2": 62000, "mp_p2": 0, "max_mp_p2": 0, "profile_bound": True,
           "profile_phase": "Engaging", "profile_failure": "", "supply_xp": 180, "supply_status": "Granted",
           "presets_requested": 0, "presets_queued": 0}
    row.update(rest)
    return row


def result(kind="kill", victim="opponent", killer="subject", status="Completed", **rest):
    out = {"schema": lockdown.NATIVE_RESULT_SCHEMA, "schema_version": 1, "run_id": "r", "case_id": "c000a-t", "seed": "7",
           "status": status, "reason": "", "generation": 2, "content_root": "x", "started_at_ms": 5000, "ended_at_ms": 20000,
           "outcome": {"kind": kind, "victim": victim if kind == "kill" else "", "killer": killer if kind == "kill" else "",
                       "t_ms": 15000 if kind == "kill" else -1},
           "contenders": [contender("subject", 0, S), contender("opponent", 1, O, hp=0, body=False)],
           "trace": "traces/c000a-t.log", "trace_lines": 10}
    out.update(rest)
    return out


def hit(t, src, victim, dmg, absorbed, lost, before, after, monster="-"):
    return (f"LD_HIT t={t} src={src} src_kind={'camp' if src == CAMP else 'contender'} src_monster={monster} credit={src} "
            f"victim={victim} victim_life=1 victim_kind=contender victim_monster=- via=basic class=basic type=physical "
            f"dmg={dmg} absorbed={absorbed} hp_lost={lost} hp={before}->{after} max_hp=620.00 shield=0.00->0.00 amp_bp=10000 "
            f"resist=0.00 crit=0 protected=0 periodic=0 reflect=0 added=0 controls=- vamp=0.00 lethal={1 if after == '0.00' else 0} pool_bp=0")


def trace(*extra, drop=()):
    lines = [
        "LD_MATCH_STARTED generation=2 contenders=3 open_at_ms=185000 countdown_ms=5000 started_at_ms=5000",
        f"LD_OFFER t=0 contender={S} ordinal=1 rerolled=0 level=1 cards=Opening:opening_athlete:0>1:q0,Opening:opening_rider:0>1:q0,Opening:opening_grinder:0>1:q0",
        f"LD_OFFER t=0 contender={O} ordinal=1 rerolled=0 level=1 cards=Opening:opening_rider:0>1:q0,Opening:opening_keyboard:0>1:q0,Opening:opening_grinder:0>1:q0",
        f"LD_CHOICE t=33 contender={S} ordinal=1 kind=Opening card=opening_athlete rank=1 replaced=- level=1 pending=0 revealed=active_elbow quality=0",
        f"LD_REWARD t=5016 contender={S} cause=dev_fixture role=recipient xp=180 xp_after=180 level=1->3 ",
        f"LD_REWARD t=5016 contender={O} cause=dev_fixture role=recipient xp=180 xp_after=180 level=1->3 ",
        f"LD_OFFER t=5016 contender={S} ordinal=2 rerolled=0 level=3 cards=Attribute:attr_protein:0>1:q1,LearnPassive:passive_pressed:0>1:q0,Attribute:attr_canteen:0>1:q1",
        f"LD_CHOICE t=5033 contender={S} ordinal=2 kind=Attribute card=attr_protein rank=0 replaced=- level=3 pending=1 revealed=- quality=1",
        f"LD_HEARTBEAT t=6000 contender={S} phase=Alive life=1 pos=100,0,0 hp=620.00/620.00",
        f"LD_HEARTBEAT t=6000 contender={O} phase=Alive life=1 pos=-100,5,0 hp=620.00/620.00",
        hit(10000, S, O, "100.00", "0.00", "100.00", "620.00", "520.00"),
        hit(11000, O, S, "50.00", "10.00", "40.00", "620.00", "580.00"),
        hit(20000, S, O, "600.00", "0.00", "520.00", "520.00", "0.00"),
        f"LD_DEATH_RECEIPT contender={O} life=1 respawn_at_ms=35000 t=20000 killer={S} cause=Contender fatal=basic fatal_body={S} assisters=- no_reward=None victim_level=3",
        f"LD_REWARD t=20000 contender={S} cause=death role=killer xp=140 xp_after=320 level=3->4 victim={O} victim_life=1 victim_kind=contender",
        f"LD_HEARTBEAT t=21000 contender={S} phase=Alive life=1 pos=101,0,0 hp=580.00/620.00",
    ]
    lines = [line for index, line in enumerate(lines) if index not in drop]
    return "\n".join(lines + list(extra)) + "\n"


class AdapterCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.caps = lockdown.describe(ROOT)
        cls.content = json.loads((ROOT / "Content/Lockdown/Data/Demo/demo_content.json").read_text(encoding="utf-8"))

    def evidence(self, value=None, text=None, plan_manifest=None, case=None):
        case = case or {"case_id": "c000a-t", "seed": "7", "arm": "t", "block": "c000a", "side": 0}
        return lockdown.case_evidence(case, plan_manifest or manifest(), self.caps, value or result(), text if text is not None else trace())


class CapabilitiesTest(AdapterCase):
    def test_capabilities_are_the_runtime_catalog(self):
        manifest_bytes = (ROOT / "Content/Lockdown/Data/Demo/manifest.json").read_bytes()
        self.assertEqual(self.caps["content"]["digest"], hashlib.sha256(manifest_bytes).hexdigest(),
                         "the content digest is the native catalog root")
        expected = ({row["id"] for row in self.content["abilities"]} | {row["id"] for row in self.content["attributes"]}
                    | {row["id"] for row in self.content["opening_cards"]})
        self.assertEqual(set(self.caps["mechanics"]), expected)
        self.assertTrue(all(row["status"] == "Supported" for row in self.caps["mechanics"].values()))
        duel = self.caps["scenarios"]["duel_site"]
        self.assertIn("capture", duel["parameters"]["site"])
        self.assertEqual(duel["parameters"]["duel_timeout_ms"][1], self.content["boss"]["appear_at"]["ms"])
        self.assertEqual(duel["evidence_level"], "NativeSpatial")
        self.assertEqual(lockdown.level_for(self.content["progression"], 180), 3)
        self.assertEqual(lockdown.level_for(self.content["progression"], 179), 2)


class PlanTest(AdapterCase):
    def requirements(self, subject, supply=180):
        return lockdown.requirements(manifest()["scenario"] | {"parameters": {**manifest()["scenario"]["parameters"],
                                                                                  "supply_xp": {"subject": supply, "opponent": supply}}},
                                     {"subject": subject, "opponent": {"choices": ["ai_select"] * lockdown.level_for(self.content["progression"], supply)}},
                                     self.caps)

    def test_known_illegal_plans_are_refused_before_any_run(self):
        needs = self.requirements({"choices": ["ai_select"] * 2})
        self.assertTrue(any("has 2 rules but its supply of 180 XP gives 3 opportunities" in item for item in needs["problems"]))
        needs = self.requirements({"choices": ["ai_select", {"rule": "definition", "definition": "opening_athlete"}, "ai_select"]})
        self.assertTrue(any("ordinal 2 can never offer opening" in item for item in needs["problems"]))
        needs = self.requirements({"choices": [{"rule": "definition", "definition": "attr_protein"}, "ai_select", "ai_select"]})
        self.assertTrue(any("ordinal 1 can never offer attribute" in item for item in needs["problems"]))
        needs = self.requirements({"choices": ["ai_select"] * 3, "presets": [["opening_rider", "attr_protein", "attr_canteen"]]})
        self.assertTrue(any("presets start at ordinal 2" in item for item in needs["problems"]))
        self.assertIn("preset_offer", needs["decisions"])
        with self.assertRaises(ValueError):
            self.requirements({"choices": ["best_card", "ai_select", "ai_select"]})
        with self.assertRaises(ValueError):
            self.requirements({"choices": [{"rule": "option_index", "index": 3}, "ai_select", "ai_select"]})

    def test_an_uncatalogued_candidate_card_is_unsupported_end_to_end(self):
        subject = {"choices": ["ai_select", {"rule": "definition", "definition": "passive_debt_collector"}, "ai_select"]}
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "m.json"
            path.write_text(json.dumps(manifest(subject=subject)), encoding="utf-8")
            code = core.main(["plan", "--manifest", str(path), "--run-dir", str(Path(temp) / "run"), "--project-root", str(ROOT)])
            cases = core.read_cases(Path(temp) / "run")
        self.assertEqual(code, 3)
        reasons = {case["arm"]: case["reason"] for case in cases}
        self.assertIn("UnsupportedMechanic: passive_debt_collector is not in the adapter's runtime catalog", reasons["t"])
        self.assertTrue(all(case["status"] == "Unsupported" for case in cases))

    def test_presets_are_admitted_only_in_a_diagnostic_sample(self):
        subject = {"choices": ["ai_select", {"rule": "definition", "definition": "passive_freeloader"}, "ai_select"],
                   "presets": [["passive_freeloader", "attr_canteen", "attr_hand_out"]]}
        for sample, expected in (("legal_search", "Unsupported"), ("diagnostic", "NotRun")):
            with tempfile.TemporaryDirectory() as temp:
                path = Path(temp) / "m.json"
                path.write_text(json.dumps(manifest(subject=subject, sample=sample)), encoding="utf-8")
                _, cases = core.plan(path, Path(temp) / "run", ROOT)
            self.assertEqual({case["status"] for case in cases if case["arm"] == "t"}, {expected})

    def test_native_input_swaps_sides_and_keeps_exact_seeds(self):
        value = manifest()
        value["resolved"] = {}
        cases = [{"case_id": "c000a-t", "seed": "18446744073709551615", "arm": "t", "side": 0},
                 {"case_id": "c000b-t", "seed": "18446744073709551615", "arm": "t", "side": 1}]
        native = lockdown.native_input("run-1", value, self.caps, cases)
        self.assertEqual((native["schema"], native["schema_version"], native["content_root"]),
                         (lockdown.NATIVE_INPUT_SCHEMA, 1, self.caps["content"]["digest"]))
        first, second = native["cases"]
        self.assertEqual(first["seed"], "18446744073709551615")
        self.assertEqual([(row["role"], row["slot"]) for row in first["contenders"]], [("subject", 0), ("opponent", 1)])
        self.assertEqual([(row["role"], row["slot"]) for row in second["contenders"]], [("subject", 1), ("opponent", 0)])
        self.assertEqual(first["contenders"][0]["choices"], [{"rule": "ai_select"}] * 3)
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
        self.assertEqual(out["facts"]["offers"]["subject"]["2"], ["attr_protein", "passive_pressed", "attr_canteen"])
        self.assertEqual(out["facts"]["choices"]["subject"]["2"], "attr_protein")

    def test_an_undecided_duel_is_censored_not_lost(self):
        value = result(kind="timeout", ended_at_ms=50000)
        value["contenders"][1] = contender("opponent", 1, O, hp=10000)
        out = self.evidence(value, trace(drop=(12, 13, 14)))
        self.assertEqual(out["status"], "Completed", out["reason"])
        self.assertEqual(out["metrics"]["duel_score"], {"value": 0, "censored": True})
        self.assertEqual(out["metrics"]["decision_time_ms"], {"value": 45000, "censored": True})

    def test_contamination_and_missing_evidence_invalidate_the_case(self):
        cases = {
            "ExternalInteraction": trace(hit(12000, CAMP, S, "30.00", "0.00", "30.00", "580.00", "550.00", "monster_scavenger")),
            "UncontrolledXP": trace(f"LD_REWARD t=12000 contender={S} cause=death role=killer xp=40 xp_after=220 level=3->3 "),
            "SupplyMismatch": trace(drop=(5,)),
            "TraceIncomplete": trace(drop=(13,)),
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
        native = result(status="BudgetExceeded", reason="the Case exceeded its wall-clock budget")
        self.assertEqual((self.evidence(native)["status"], self.evidence(native)["reason"]),
                         ("BudgetExceeded", "the Case exceeded its wall-clock budget"))
        self.assertIn("another Case", self.evidence(result(seed="8"))["reason"])

    def test_a_hit_line_that_contradicts_itself_is_an_evidence_violation(self):
        broken = trace().replace("hp_lost=40.00 hp=620.00->580.00", "hp_lost=40.00 hp=620.00->590.00")
        out = self.evidence(text=broken)
        self.assertTrue(any("hp_lost 40.0" in item for item in out["violations"]), out["violations"])
        self.assertEqual(lockdown.hit_violations(lockdown.parse_line(hit(1, S, O, "1.00", "0.00", "2.00", "5.00", "3.00"))[1]),
                         ["LD_HIT t=1 hp_lost + absorbed exceeds dmg"])

    def test_a_prepared_offer_must_be_the_offer_drawn(self):
        subject = {"choices": ["ai_select", {"rule": "definition", "definition": "attr_protein"}, "ai_select"],
                   "presets": [["attr_protein", "passive_pressed", "attr_canteen"]]}
        plan = manifest(subject=subject, sample="diagnostic")
        value = result()
        value["contenders"][0].update(presets_requested=1, presets_queued=1)
        self.assertEqual(self.evidence(value, plan_manifest=plan)["status"], "Completed")
        drifted = trace().replace("cards=Attribute:attr_protein:0>1:q1,LearnPassive:passive_pressed:0>1:q0,Attribute:attr_canteen",
                                  "cards=Attribute:attr_canteen:0>1:q1,LearnPassive:passive_pressed:0>1:q0,Attribute:attr_protein")
        self.assertIn("PresetNotApplied", self.evidence(value, drifted, plan)["reason"])

    def test_branch_label_and_prefix(self):
        subject = {"choices": ["ai_select", {"rule": "option_index", "index": 0}, "ai_select"]}
        out = self.evidence(plan_manifest=manifest(subject=subject))
        self.assertEqual(out["facts"]["label"], "attr_protein")
        self.assertEqual(out["facts"]["prefix"].split("|")[-1], "2:['attr_protein', 'passive_pressed', 'attr_canteen']")
        self.assertTrue(out["facts"]["random_fingerprint"])
        # The arm keeping the production choice is compared at the design's decision point, on the same prefix.
        reference = self.evidence(plan_manifest=manifest(subject=subject),
                                  case={"case_id": "c000a-t", "seed": "7", "arm": "ref", "block": "c000a", "side": 0})
        self.assertEqual(reference["facts"]["prefix"], out["facts"]["prefix"])
        self.assertEqual(reference["facts"]["label"], "attr_protein")

    def test_the_native_case_limit_is_the_drivers(self):
        header = (ROOT / "Source/Lockdown/Private/Tests/LDBuildEvaluationProbe.h").read_text(encoding="utf-8")
        self.assertIn(f"inline constexpr int32 MaxCases = {lockdown.NATIVE_MAX_CASES};", header)
        self.assertEqual(self.caps["limits"]["max_cases_per_run"], lockdown.NATIVE_MAX_CASES)


class ReplayAndIdentityTest(AdapterCase):
    def test_canonical_events_name_roles_and_stop_at_the_end(self):
        events, spatial = lockdown.canonical_events(trace(hit(25000, S, O, "1.00", "0.00", "1.00", "0.00", "0.00")), result())
        self.assertTrue(all(S not in line and O not in line for line in events))
        self.assertTrue(any(line.startswith("LD_HIT t=20000 src=subject") for line in events))
        self.assertFalse(any("t=25000" in line for line in events), "events after the Case's end are not compared")
        self.assertEqual(spatial["opponent@6000"], (-100.0, 5.0, 0.0))
        self.assertNotIn("subject@21000", spatial)

    def test_run_identity(self):
        good = {"schema": lockdown.NATIVE_RUN_SCHEMA, "schema_version": 1, "content_root": self.caps["content"]["digest"].upper(),
                "fixed_time_step": True, "seed_derivation": 1}
        self.assertEqual(lockdown.identity_problems(good, self.caps), [])
        self.assertTrue(lockdown.identity_problems(dict(good, content_root="0" * 64), self.caps))
        self.assertTrue(lockdown.identity_problems(dict(good, fixed_time_step=False), self.caps))
        self.assertTrue(lockdown.identity_problems(dict(good, seed_derivation=2), self.caps))


if __name__ == "__main__":
    unittest.main()
