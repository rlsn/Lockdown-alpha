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
# The stats' design order (ascending native id), the order of every per-stat row and points= list.
STATS = ("stat_physique", "stat_thick_skin", "stat_strength", "stat_brains", "stat_agility")


# Supply to level 6: two draws on bonus levels 2, 4 and 6, one on 3 and 5 (progression.bonus_component_levels).
LEVEL6_DRAW_LEVELS = (2, 2, 3, 4, 4, 5, 6, 6)
LEVEL6_GRANTS = ("component_punch_heal", "component_breath_strike", "component_fist_water", "component_fist_shock",
                 "component_blast_chase", "component_hard_skin", "component_catch_breath", "component_kick_down")

def stat_points(*points):
    return [{"id": stat, "points": value} for stat, value in zip(STATS, points)]


BUILDS = {
    "subject": {"actives": [{"id": "active_elbow", "slot": "Q"}, {"id": "active_flyer_trap", "slot": "W"},
                            {"id": "active_not_mad", "slot": "E"}],
                "blueprints": [], "stat_points": stat_points(1, 0, 2, 0, 2)},
    "opponent": {"actives": [{"id": "active_open_mic", "slot": "Q"}, {"id": "active_stare_down", "slot": "W"},
                             {"id": "active_bail_out", "slot": "E"}],
                 "blueprints": [], "stat_points": stat_points(1, 2, 0, 2, 0)},
}


def manifest(subject=None, opponent=None, sample="legal_search", supply=SUPPLY, design="paired_arms"):
    subject = subject or {"choices": ["ai_select"] * 3}
    opponent = opponent or {"choices": ["ai_select"] * 3}
    arms = [{"id": "ref", "plan": {"subject": {"choices": ["ai_select"] * 3}, "opponent": {"choices": ["ai_select"] * 3}}},
            {"id": "t", "plan": {"subject": subject, "opponent": opponent}}]
    return {"schema": core.MANIFEST_SCHEMA, "schema_version": core.MANIFEST_SCHEMA_VERSION,
            "experiment": {"id": "lockdown-test", "hypothesis": "adapter test"},
            "adapter": {"module": "BuildEvaluationLockdown", "id": "lockdown-demo"},
            "scenario": {"kind": "duel_site", "parameters": {"site": "capture", "duel_timeout_ms": 45000,
                                                             "supply_xp": {"subject": supply, "opponent": supply}}},
            "sample_class": sample, "assumptions": ["test supply"],
            "design": {"kind": design, "reference": "ref", "arms": arms},
            "blocks": {"root_seed": "7", "splits": {"search": 0, "confirm": 1}, "sides": 2},
            "metrics": {"primary": "duel_score", "secondary": ["damage_dealt_hp"], "material_difference": None},
            "budget": {"class": "standard", "max_cases": 10, "case_wall_timeout_s": 120, "run_wall_timeout_s": 600,
                       "max_case_events": 200000, "max_run_disk_bytes": 2 * core.GiB, "max_root_disk_bytes": 6 * core.GiB}}


def contender(role, slot, contender_id, hp=31000, body=True, **rest):
    row = {"role": role, "slot": slot, "contender": contender_id, "progression_valid": True, "level": 6, "xp": SUPPLY,
           "choices": 3, "planned_choices": 3, "unspent_points": 0, **copy.deepcopy(BUILDS[role]),
           "has_body": body, "hp_p2": hp, "max_hp_p2": 62000, "mp_p2": 0, "max_mp_p2": 0, "profile_bound": True,
           "profile_phase": "Engaging", "profile_failure": "", "supply_xp": SUPPLY, "supply_status": "Granted",
           "presets_requested": 0, "presets_queued": 0, "planned_steps": 0, "steps_committed": 0, "components": []}
    row.update(rest)
    return row


def result(kind="kill", victim="opponent", killer="subject", status="Completed", **rest):
    out = {"schema": lockdown.NATIVE_RESULT_SCHEMA, "schema_version": lockdown.NATIVE_RESULT_SCHEMA_VERSION,
           "run_id": "r", "case_id": "c000a-t", "seed": "7",
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
        ("start", "LD_MATCH_STARTED generation=2 contenders=3 started_at_ms=0 rest_ends_at_ms=5000"),
        ("s_offer1", f"LD_OFFER t=0 contender={S} ordinal=1 level=1 cards=active_elbow,active_open_mic,active_unplug"),
        ("o_offer1", f"LD_OFFER t=0 contender={O} ordinal=1 level=1 cards=active_open_mic,active_snark_bolt,active_unplug"),
        ("s_choice1", choice(33, S, 1, "active_elbow", "learned", "Q", "-", 1, 0)),
        ("o_choice1", choice(33, O, 1, "active_open_mic", "learned", "Q", "-", 1, 0)),
        ("field", "LD_FIELD_ENTER generation=2 segment=0 at_ms=5000 field_t_ms=0 final=0"),
        ("s_spawn", f"LD_EVAL_FIELD_SPAWN t=5000 generation=2 contender={S} life=2 pos=100,0,0"),
        ("o_spawn", f"LD_EVAL_FIELD_SPAWN t=5000 generation=2 contender={O} life=2 pos=-100,5,0"),
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
        ("o_choice3", choice(5283, O, 3, "active_bail_out", "blueprint", "-", "NoEmptySlot", 6, 0)),
        ("s_points", f"LD_BUILD t=5300 contender={S} kind=AllocatePoints card=- slot=- points=1,0,2,0,2 unspent=0 level=6"),
        ("o_points", f"LD_BUILD t=5300 contender={O} kind=AllocatePoints card=- slot=- points=1,2,0,2,0 unspent=0 level=6"),
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


# #198 workshop: the subject owns its build. The opening base B18 is bound to the opening active with water, three opening
# points go to strength, B18 is rebuilt with fire in the opening rest, then the drawn mod M03 goes onto the opening active and
# the fifteen level points go to physique: every one of the 18 points 600 XP earns.
WORKSHOP = {
    "choices": [{"rule": "definition", "definition": "active_elbow"}, "ai_select", "ai_select"],
    "build": [
        {"step": "opening_base", "at": "opening_rest", "component": "component_dirty_trick",
         "bind": {"active": "active_elbow", "element": "water"}},
        {"step": "allocate_points", "at": "opening_rest", "points": {"stat_strength": 3}},
        {"step": "uninstall", "at": "opening_rest", "component": "component_dirty_trick"},
        {"step": "install", "at": "opening_rest", "component": "component_dirty_trick",
         "bind": {"active": "active_elbow", "element": "fire"}},
        {"step": "install", "at": "field", "component": "component_long_kick", "target": {"kind": "active", "id": "active_elbow"}},
        {"step": "allocate_points", "at": "field", "points": {"stat_physique": 15}},
    ],
}
GRANTS = "component_long_kick,component_fist_water,component_ball_kick,component_seeing_red,component_hold_down"


def build_line(t, kind, card="-", points="-", installed="-", bind="-", unspent=0, level=1):
    return (f"LD_BUILD t={t} contender={S} kind={kind} card={card} slot=- points={points} unspent={unspent} level={level} forgot=- "
            f"refunded=0 paid=0 ordinal=0 installed={installed} bind={bind} install_stored=- reclaimed=-")


WORKSHOP_LINES = {
    "field_enter": "LD_FIELD_ENTER generation=2 segment=0 at_ms=5000 field_t_ms=0 final=0",
    "w_base": build_line(100, "OpeningBase", "component_dirty_trick", installed="1", bind="active_elbow:Water", unspent=3),
    "w_points": build_line(200, "AllocatePoints", points="0,0,3,0,0"),
    "w_off": build_line(300, "UninstallComponent", "component_dirty_trick", installed="0"),
    "w_fire": build_line(400, "InstallComponent", "component_dirty_trick", installed="1", bind="active_elbow:Fire"),
    "w_level": f"LD_LEVEL_UP t=5016 contender={S} level=1->6 choices_earned=2 unspent_points=15 components={GRANTS} empty_pool_grants=0",
    "w_mod": build_line(5400, "InstallComponent", "component_long_kick", installed="1", bind="- on=Active:active_elbow",
                        unspent=15, level=6),
    "w_body": build_line(5500, "AllocatePoints", points="15,0,0,0,0", level=6),
}


def workshop_trace(*extra, drop=(), replace=None):
    lines = dict(WORKSHOP_LINES, **(replace or {}))
    return trace(*[line for key, line in lines.items() if key not in drop], *extra, drop=("field", "s_points", "s_level"))


def component(name, installed=False, source="LevelUp", target_kind="None", target_id="", element="None", bound_active=""):
    return {"id": name, "installed": installed, "ever_installed": installed or name == "component_dirty_trick",
            "source": source, "source_level": 1 if source == "OpeningPick" else 2, "target_kind": target_kind,
            "target_id": target_id, "element": element, "bound_active": bound_active, "bound_component": ""}


def workshop_components():
    return [component("component_ball_kick"),
            component("component_dirty_trick", True, "OpeningPick", element="Fire", bound_active="active_elbow"),
            component("component_fist_water"), component("component_hold_down"),
            component("component_long_kick", True, target_kind="Active", target_id="active_elbow"),
            component("component_seeing_red")]


def workshop_result(**subject):
    value = result()
    value["contenders"][0].update(dict({"planned_steps": 6, "steps_committed": 6, "stat_points": stat_points(15, 0, 3, 0, 0),
                                        "components": workshop_components()}, **subject))
    return value


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
        components = {row["id"]: row for row in self.content["components"]}
        self.assertEqual(set(self.caps["mechanics"]), set(actives) | stats | set(components))
        self.assertTrue(all(row["status"] == "Supported" for row in self.caps["mechanics"].values()))
        for active, opening in actives.items():
            self.assertEqual((self.caps["mechanics"][active]["kind"], self.caps["mechanics"][active]["opening"]),
                             ("active", opening))
        self.assertTrue(all(self.caps["mechanics"][stat]["kind"] == "stat" for stat in stats))
        for name, row in components.items():
            mechanic = self.caps["mechanics"][name]
            self.assertEqual((mechanic["kind"], mechanic["pool"], mechanic["targets"]), (row["kind"], row["pool"], row.get("targets")))
            self.assertIn(mechanic["kind"], lockdown.COMPONENT_KINDS, "components are mechanics of their catalog kind")
            opening = row.get("opening")
            self.assertEqual(mechanic["opening_base"],
                             "always" if opening == "always" else opening["actives"] if isinstance(opening, dict) else [])
        trick = self.caps["mechanics"]["component_dirty_trick"]
        self.assertEqual((trick["binding"]["kind"], trick["binding"]["elements"]), ("active_element", ["fire", "water", "electric"]),
                         "B18 binds an active and an element")
        self.assertEqual(self.caps["content"]["components"], len(components))
        self.assertEqual(set(self.caps["decisions"]),
                         {"ai_select", "option_index", "definition", "preset_offer", "preset_level_components",
                          "choose_opening_base", "allocate_points", "install_component", "uninstall_component",
                          "respec_points", "learn_blueprint", "replace_active"})
        self.assertTrue(all(row["description"] for row in self.caps["decisions"].values()))
        for decision, reason in lockdown.UNPLANNABLE.values():
            self.assertNotIn(decision, self.caps["decisions"], "NPC, forget, FillGap, Dismantle and elite choices are never plannable")
            self.assertTrue(reason)
        self.assertEqual(self.caps["decisions"]["preset_offer"]["sample_classes"], ["diagnostic"])
        self.assertEqual(self.caps["decisions"]["preset_level_components"]["sample_classes"], ["diagnostic"])
        duel = self.caps["scenarios"]["duel_site"]
        self.assertIn("capture", duel["parameters"]["site"])
        self.assertEqual(duel["parameters"]["duel_timeout_ms"][1],
                         min(self.content["parameters"]["MATCH.FieldSegmentSec"]["ms"], self.content["boss"]["appear_at"]["ms"]) - 1)
        self.assertEqual(duel["parameters"]["active_offer_levels"], self.content["progression"]["active_offer_levels"])
        self.assertEqual(duel["parameters"]["stat_design_order"], list(STATS))
        self.assertEqual((duel["parameters"]["opening_points"], duel["parameters"]["points_per_level"]),
                         (self.content["parameters"]["GROWTH.OpeningPoints"], self.content["parameters"]["GROWTH.PointsPerLevel"]))
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

    def test_duel_timeout_cannot_reach_the_next_rest(self):
        scenario = manifest()["scenario"]
        end = self.content["parameters"]["MATCH.FieldSegmentSec"]["ms"]
        scenario["parameters"]["duel_timeout_ms"] = end - 1
        self.assertEqual(lockdown.scenario_problems(scenario, self.caps), [])
        scenario["parameters"]["duel_timeout_ms"] = end
        self.assertTrue(any("duel_timeout_ms" in problem for problem in lockdown.scenario_problems(scenario, self.caps)))

    def test_shipped_manifests_are_admitted_by_current_capabilities(self):
        paths = sorted((ROOT / "Scripts" / "BuildEvaluationManifests").glob("*.json"))
        self.assertTrue(paths)
        for path in paths:
            value = core.validate_manifest(core.read_json(path))
            for arm in value["design"]["arms"]:
                with self.subTest(manifest=path.name, arm=arm["id"]):
                    needs = lockdown.requirements(value["scenario"], arm["plan"], self.caps)
                    self.assertEqual(needs["problems"], [], "shipped experiments must satisfy current plan rules")
                    self.assertEqual(core.admit_arm(
                        self.caps, self.caps["scenarios"][value["scenario"]["kind"]], value, arm, lockdown), [],
                        "shipped experiments must use supported mechanics and decisions")

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

    def test_unplannable_decisions_are_unsupported_with_their_reason(self):
        plans = {
            "buy_npc_offer": {"choices": ["ai_select"] * 3, "build": [{"step": "buy_npc_offer", "at": "opening_rest"}]},
            "forget_active": {"choices": ["ai_select"] * 3, "build": [{"step": "forget", "at": "opening_rest", "slot": "Q"}]},
            "fill_gap": {"choices": ["ai_select"] * 3, "build": [{"step": "fill_gap", "at": "opening_rest"}]},
            "dismantle_components": {"choices": ["ai_select"] * 3, "build": [{"step": "dismantle", "at": "opening_rest"}]},
            "choose_elite_component": {"choices": ["ai_select"] * 3, "build": [{"step": "component_choice", "at": "field"}]},
            "replace_at_choice": {"choices": ["ai_select", {"rule": "definition", "definition": "active_seethe", "replace": "Q"},
                                              "ai_select"]},
        }
        for decision, subject in plans.items():
            with self.subTest(decision=decision):
                problems = self.requirements(subject)["problems"]
                self.assertTrue(any(item.startswith(f"UnsupportedDecision: {decision} (") for item in problems), problems)
                cases, native_written = self.plan_cases(subject)
                self.assertTrue(all(case["status"] == "Unsupported" for case in cases))
                reasons = {case["arm"]: case["reason"] for case in cases}
                self.assertIn(f"UnsupportedDecision: {decision}", reasons["t"])
                self.assertIn(lockdown.UNPLANNABLE[next(key for key, row in lockdown.UNPLANNABLE.items() if row[0] == decision)][1],
                              reasons["t"], "the refusal states its reason")
                self.assertIn("DesignIncomplete", reasons["ref"])
                self.assertFalse(native_written, "an unsupported decision is never mapped onto a runnable plan")
        for retired in ({"choices": ["ai_select"] * 3, "points": {"stat_strength": 2}},
                        {"choices": ["ai_select"] * 3, "learn": [{"definition": "active_seethe", "slot": "E"}]},
                        {"choices": ["ai_select"] * 3, "build": [{"step": "reroll", "at": "field"}]}):
            with self.subTest(plan=retired), self.assertRaises(ValueError):
                self.requirements(retired)

    def test_a_workshop_plan_is_admitted_with_its_decisions_and_mechanics(self):
        needs = self.requirements(WORKSHOP)
        self.assertEqual(needs["problems"], [])
        self.assertTrue({"choose_opening_base", "allocate_points", "uninstall_component", "install_component"} <= set(needs["decisions"]))
        self.assertTrue({"component_dirty_trick", "component_long_kick", "stat_strength", "stat_physique", "active_elbow"}
                        <= set(needs["mechanics"]))
        cases, native_written = self.plan_cases(WORKSHOP)
        self.assertEqual({case["status"] for case in cases}, {"NotRun"})
        self.assertTrue(native_written)
        cases, _ = self.plan_cases({**WORKSHOP, "build": [{"step": "install", "at": "field", "component": "component_unknown",
                                                            "target": {"kind": "dash"}}, *WORKSHOP["build"]]})
        self.assertIn("UnsupportedMechanic: component_unknown is not in the adapter's runtime catalog",
                      {case["arm"]: case["reason"] for case in cases}["t"])

    def test_level_component_presets_are_conditional_and_use_only_earned_distinct_common_grants(self):
        grants = list(LEVEL6_GRANTS)
        subject = {"choices": ["ai_select"] * 3, "level_components": grants}
        needs = self.requirements(subject)
        self.assertEqual(needs["problems"], [])
        self.assertIn("preset_level_components", needs["decisions"])
        cases, native_written = self.plan_cases(subject, sample="diagnostic")
        self.assertEqual({case["status"] for case in cases}, {"NotRun"})
        self.assertTrue(native_written)
        cases, _ = self.plan_cases(subject, sample="legal_search")
        self.assertTrue(all(case["status"] == "Unsupported" for case in cases))
        self.assertIn("diagnostic", {case["arm"]: case["reason"] for case in cases}["t"])
        for invalid, reason in ((grants[:-1], "8"),
                                (grants[:-1] + ["component_dirty_fist"], "scarce")):
            with self.subTest(grants=invalid):
                problems = self.requirements({**subject, "level_components": invalid})["problems"]
                self.assertTrue(any(reason in item.lower() for item in problems), problems)
        with self.assertRaisesRegex(ValueError, "distinct"):
            self.requirements({**subject, "level_components": [grants[0]] * len(grants)})

    def test_level_component_preset_refuses_an_install_not_in_the_fixed_inventory(self):
        subject = {"choices": ["ai_select"] * 3,
                   "level_components": ["component_fist_fire", *LEVEL6_GRANTS[1:]],
                   "build": [{"step": "install", "at": "field", "component": "component_punch_heal"},
                             {"step": "allocate_points", "at": "field", "points": {"stat_strength": 18}}]}
        problems = self.requirements(subject)["problems"]
        self.assertTrue(any("component_punch_heal" in item and "preset" in item for item in problems), problems)

    def test_level_component_preset_rejects_an_opening_duplicate_and_a_zero_weight_category(self):
        grants = list(LEVEL6_GRANTS)
        subject = {"choices": [{"rule": "definition", "definition": "active_elbow"}, "ai_select", "ai_select"],
                   "level_components": grants,
                   "build": [{"step": "opening_base", "at": "opening_rest", "component": "component_punch_heal"},
                             {"step": "allocate_points", "at": "field", "points": {"stat_strength": 18}}]}
        self.assertTrue(any("already owned" in item for item in self.requirements(subject)["problems"]))
        caps = copy.deepcopy(self.caps)
        band = next(row for row in caps["scenarios"]["duel_site"]["parameters"]["component_category_weights"]
                    if row["levels"][0] <= 2 <= row["levels"][1])
        band["base"] = 0
        value = manifest(subject={"choices": ["ai_select"] * 3, "level_components": grants}, sample="diagnostic")
        needs = lockdown.requirements(value["scenario"], value["design"]["arms"][1]["plan"], caps)
        self.assertTrue(any("component_punch_heal" in item and "level 2" in item for item in needs["problems"]))

    def test_workshop_plans_duel_site_cannot_carry_out_are_illegal(self):
        content = self.content
        opening = [row["id"] for row in content["abilities"] if row.get("opening")]
        trick = next(row for row in content["components"] if row["id"] == "component_dirty_trick")
        not_b18_opening = next(active for active in opening if active not in trick["opening"]["actives"])
        not_bindable = next(row["id"] for row in content["abilities"] if row["id"] not in trick["binding"]["actives"])
        build = WORKSHOP["build"]

        def plan(steps, choices=None):
            return {"choices": choices or WORKSHOP["choices"], "build": steps}

        def bind(active, element="fire"):
            return {"step": "opening_base", "at": "opening_rest", "component": "component_dirty_trick",
                    "bind": {"active": active, "element": element}}

        points = {"step": "allocate_points", "at": "field", "points": {"stat_physique": 15}}
        variants = {
            "allocates 17 of the 18 points": plan([*build[:5], {**points, "points": {"stat_physique": 14}}]),
            "the opening rest holds 3": plan([{"step": "allocate_points", "at": "opening_rest", "points": {"stat_strength": 4}},
                                              {**points, "points": {"stat_physique": 14}}]),
            "choose that opening active by definition": plan(build, ["ai_select"] * 3),
            "not an opening option": plan([bind(not_b18_opening)], [{"rule": "definition", "definition": not_b18_opening},
                                                                     "ai_select", "ai_select"]),
            "binds a learned active and an element": plan([{"step": "opening_base", "at": "opening_rest",
                                                            "component": "component_dirty_trick"}]),
            "binds one of": plan([bind(not_bindable)]),
            "cannot prove active_hose_down is learned here": plan([bind("active_hose_down")]),
            "takes no binding": plan([{"step": "opening_base", "at": "opening_rest", "component": "component_fist_fire",
                                       "bind": {"active": "active_elbow", "element": "fire"}}]),
            "never an opening base": plan([{"step": "opening_base", "at": "opening_rest", "component": "component_long_kick"}]),
            "is picked in the opening rest": plan([{**build[0], "at": "field"}]),
            "never a level-up draw": plan([*build, {"step": "install", "at": "field", "component": "component_owe_it",
                                                     "target": {"kind": "self"}}]),
            "does not go onto active_seethe": plan([*build[:4], {**build[4], "target": {"kind": "active", "id": "active_seethe"}},
                                                    build[5]]),
            "cannot prove active_hose_down is learned": plan([*build[:4], {**build[4], "target": {"kind": "active",
                                                                                                 "id": "active_hose_down"}}, build[5]]),
            "needs a target": plan([*build[:4], {"step": "install", "at": "field", "component": "component_long_kick"}, build[5]]),
            "does not go onto the innate D": plan([*build[:4], {**build[4], "target": {"kind": "dash"}}, build[5]]),
            "is not installed by an earlier step": plan([*build[:4], {**build[4], "target": {"kind": "component",
                                                                                            "id": "component_third_punch"}}, build[5]]),
            "rest-window move": plan([*build[:2], {"step": "uninstall", "at": "opening_rest", "component": "component_dirty_trick"},
                                      {**build[3], "at": "field"}, *build[4:]]),
            "a rest-window command": plan([*build[:4], {"step": "uninstall", "at": "field", "component": "component_dirty_trick"},
                                           *build[4:]]),
            "not owned in the opening rest": plan([*build[:4], {**build[4], "at": "opening_rest"}, build[5]]),
            "no point is allocated by an earlier step": plan([build[0], {"step": "respec", "at": "opening_rest"}, *build[1:]]),
            "never leaves a blueprint pending": plan([*build, {"step": "learn", "at": "field", "blueprint": "active_seethe",
                                                               "slot": "E"}]),
            "nothing can replace it": plan([build[0], {"step": "replace", "at": "opening_rest", "blueprint": "active_seethe",
                                                       "slot": "Q"}, *build[1:]]),
            "the opening rest comes before the field": plan([*build[:4], build[5], build[1]]),
        }
        for expected, subject in variants.items():
            with self.subTest(expected=expected):
                problems = self.requirements(subject)["problems"]
                self.assertTrue(any(expected in item and item.startswith("IllegalDecision") for item in problems), problems)
        self.assertTrue(any("no level-up draws a component" in item
                            for item in self.requirements(plan([*build[:4], build[4], {**points, "points": {"stat_physique": 1}}],
                                                               [{"rule": "definition", "definition": "active_elbow"}]),
                                                          supply=0)["problems"]))

    def test_workshop_step_shapes_are_strict(self):
        base = {"step": "install", "at": "field", "component": "component_long_kick", "target": {"kind": "active", "id": "active_elbow"}}
        shapes = (
            {**base, "slot": "Q"}, {**base, "at": "rest_1"}, {**base, "component": "Long-Kick"},
            {**base, "target": {"kind": "self", "id": "active_elbow"}}, {**base, "target": {"kind": "active"}},
            {**base, "target": {"kind": "segment", "id": "active_elbow"}},
            {"step": "opening_base", "at": "opening_rest", "component": "component_dirty_trick",
             "bind": {"active": "active_elbow", "element": "ice"}},
            {"step": "allocate_points", "at": "field", "points": {"stat_strength": 0}},
            {"step": "allocate_points", "at": "field", "points": {"stat_strength": True}},
            {"step": "allocate_points", "at": "field", "points": {}},
            {"step": "learn", "at": "field", "blueprint": "active_seethe", "slot": "R"},
            {"step": "learn", "at": "field", "blueprint": "active_seethe"},
        )
        for shape in shapes:
            with self.subTest(step=shape), self.assertRaises(ValueError):
                self.requirements({**WORKSHOP, "build": [shape]})
        with self.assertRaisesRegex(ValueError, "at most 32 steps"):
            self.requirements({**WORKSHOP, "build": [{"step": "respec", "at": "opening_rest"}] * (lockdown.NATIVE_MAX_BUILD_STEPS + 1)})
        header = (ROOT / "Source/Lockdown/Private/Tests/LDBuildEvaluationProbe.h").read_text(encoding="utf-8")
        self.assertIn(f"inline constexpr int32 MaxBuildSteps = {lockdown.NATIVE_MAX_BUILD_STEPS};", header)

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

    def test_fixed_opening_prevents_later_presets_from_reoffering_an_owned_active(self):
        subject = {"choices": [{"rule": "definition", "definition": "active_elbow"},
                               {"rule": "definition", "definition": "active_flyer_trap"},
                               {"rule": "definition", "definition": "active_not_mad"}],
                   "opening_preset": ["active_elbow"],
                   "presets": [["active_flyer_trap"], ["active_not_mad"]]}
        cases, written = self.plan_cases(subject, "diagnostic")
        self.assertTrue(written)
        self.assertEqual({case["status"] for case in cases}, {"NotRun"})
        cases, _ = self.plan_cases(subject, "legal_search")
        self.assertEqual({case["status"] for case in cases if case["arm"] == "t"}, {"Unsupported"})
        native = lockdown.native_input("run-1", manifest(subject=subject, sample="diagnostic"), self.caps,
                                       [{"case_id": "c000a-t", "seed": "7", "arm": "t", "side": 0}])
        self.assertEqual(native["cases"][0]["contenders"][0]["opening_preset"], ["active_elbow"])
        subject["presets"][1].append("active_elbow")
        problems = self.requirements(subject)["problems"]
        self.assertTrue(any("active_elbow" in problem and "already" in problem for problem in problems), problems)

    def test_native_input_swaps_sides_and_keeps_exact_seeds(self):
        value = manifest(subject={"choices": ["ai_select", {"rule": "option_index", "index": 2},
                                              {"rule": "definition", "definition": "active_not_mad"}],
                                  "presets": [["active_not_mad", "active_seethe"]]}, sample="diagnostic")
        value["resolved"] = {}
        cases = [{"case_id": "c000a-t", "seed": "18446744073709551615", "arm": "t", "side": 0},
                 {"case_id": "c000b-t", "seed": "18446744073709551615", "arm": "t", "side": 1}]
        native = lockdown.native_input("run-1", value, self.caps, cases)
        self.assertEqual((native["schema"], native["schema_version"], native["content_root"]),
                         (lockdown.NATIVE_INPUT_SCHEMA, 5, self.caps["content"]["digest"]))
        first, second = native["cases"]
        self.assertEqual(set(first), {"case_id", "seed", "site", "duel_timeout_ms", "wall_timeout_s", "contenders"})
        self.assertEqual(first["seed"], "18446744073709551615")
        self.assertEqual([(row["role"], row["slot"]) for row in first["contenders"]], [("subject", 0), ("opponent", 1)])
        self.assertEqual([(row["role"], row["slot"]) for row in second["contenders"]], [("subject", 1), ("opponent", 0)])
        subject, opponent = first["contenders"]
        self.assertEqual(set(subject), {"role", "slot", "supply_xp", "choices", "opening_preset", "presets",
                                        "level_components", "build"},
                         "the native contender keys")
        self.assertEqual(subject["supply_xp"], SUPPLY)
        self.assertEqual(subject["choices"], [{"rule": "ai_select"}, {"rule": "option_index", "index": 2},
                                              {"rule": "definition", "definition": "active_not_mad"}])
        self.assertEqual(subject["presets"], [["active_not_mad", "active_seethe"]])
        self.assertEqual(subject["opening_preset"], [])
        self.assertEqual(subject["level_components"], [])
        self.assertEqual((opponent["choices"], opponent["presets"], opponent["build"]), ([{"rule": "ai_select"}] * 3, [], []))
        self.assertEqual(subject["build"], [], "without a build list the production rule builds")
        self.assertEqual(first["wall_timeout_s"], 120)
        workshop = manifest(subject=WORKSHOP)
        native = lockdown.native_input("run-2", workshop, self.caps, cases[:1])
        self.assertEqual(native["cases"][0]["contenders"][0]["build"], WORKSHOP["build"],
                         "the planned steps reach the native driver unchanged and in order")

    def test_kite_stance_reaches_native_only_when_planned(self):
        kite = {"choices": ["ai_select"] * 3, "stance": {"kind": "kite", "standoff_cm": 600}}
        case = [{"case_id": "c000a-t", "seed": "7", "arm": "t", "side": 0}]
        native = lockdown.native_input("run-1", manifest(subject=kite, sample="diagnostic"), self.caps, case)
        subject, opponent = native["cases"][0]["contenders"]
        self.assertEqual(subject["stance"], {"kind": "kite", "standoff_cm": 600})
        self.assertNotIn("stance", opponent, "pursue is spelled by omission so old inputs stay byte-identical")
        for invalid in ({"kind": "pursue", "standoff_cm": 600}, {"kind": "kite", "standoff_cm": 100},
                        {"kind": "kite", "standoff_cm": 2001}, {"kind": "kite"}, {"kind": "kite", "standoff_cm": True},
                        {"kind": "kite", "standoff_cm": 600, "extra": 1},
                        {"kind": "guard", "standoff_cm": 600}, {"kind": "kite", "standoff_cm": 600, "threshold_bp": 3000},
                        {"kind": "guard", "standoff_cm": 600, "threshold_bp": 9001},
                        {"kind": "hold", "standoff_cm": 600, "leash_cm": 499}):
            with self.subTest(stance=invalid), self.assertRaises(ValueError):
                lockdown.native_input("run-1", manifest(subject={**kite, "stance": invalid}, sample="diagnostic"),
                                      self.caps, case)
        for valid in ({"kind": "burst", "standoff_cm": 700, "leash_cm": 1200}, {"kind": "hold", "standoff_cm": 600},
                      {"kind": "guard", "standoff_cm": 600, "threshold_bp": 3500}):
            with self.subTest(stance=valid):
                native = lockdown.native_input("run-1", manifest(subject={**kite, "stance": valid}, sample="diagnostic"),
                                               self.caps, case)
                self.assertEqual(native["cases"][0]["contenders"][0]["stance"], valid)

    def test_operation_knobs_reach_native_with_any_stance(self):
        case = [{"case_id": "c000a-t", "seed": "7", "arm": "t", "side": 0}]
        for valid in ({"kind": "kite", "standoff_cm": 450, "reaction_ms": 800, "aim_error_cm": 200, "skip_bp": 3500},
                      {"kind": "pursue", "reaction_ms": 150}, {"kind": "pursue", "aim_error_cm": 0},
                      {"kind": "guard", "standoff_cm": 600, "threshold_bp": 3500, "skip_bp": 9000, "reaction_ms": 1500,
                       "aim_error_cm": 400}):
            with self.subTest(stance=valid):
                plan = manifest(subject={"choices": ["ai_select"] * 3, "stance": valid}, sample="diagnostic")
                native = lockdown.native_input("run-1", plan, self.caps, case)
                self.assertEqual(native["cases"][0]["contenders"][0]["stance"], valid)
        for invalid in ({"kind": "kite", "standoff_cm": 450, "reaction_ms": 1501},
                        {"kind": "kite", "standoff_cm": 450, "aim_error_cm": -1},
                        {"kind": "kite", "standoff_cm": 450, "skip_bp": 9001},
                        {"kind": "kite", "standoff_cm": 450, "skip_bp": True},
                        {"kind": "kite", "standoff_cm": 450, "reaction_ms": 800.0},
                        {"kind": "pursue"}, {"kind": "pursue", "reaction_ms": 150, "standoff_cm": 600},
                        {"kind": "pursue", "reaction_ms": 150, "leash_cm": 600}):
            with self.subTest(stance=invalid), self.assertRaises(ValueError):
                plan = manifest(subject={"choices": ["ai_select"] * 3, "stance": invalid}, sample="diagnostic")
                lockdown.native_input("run-1", plan, self.caps, case)


class EvidenceTest(AdapterCase):
    def test_operation_knobs_are_facts_and_must_reach_the_native_profile(self):
        stance = {"kind": "kite", "standoff_cm": 450, "reaction_ms": 800, "aim_error_cm": 200, "skip_bp": 3500}
        plan = manifest(subject={"choices": ["ai_select"] * 3, "stance": stance}, sample="diagnostic")
        begin = f"LD_EVAL_CASE_BEGIN case=c000a-t index=0 seed=7 subject={S} opponent={O}"
        line = (f"LD_EVAL_PROFILE contender={S} rival={O} site=capture choices=3 "
                f"reaction_ms=800 aim_error_cm=200 skip_bp=3500")
        text = trace().replace(begin, begin + "\n" + line)
        out = self.evidence(text=text, plan_manifest=plan)
        self.assertEqual(out["status"], "Completed", out["reason"])
        self.assertEqual(out["facts"]["operation"], {
            "subject": {"reaction_ms": 800, "aim_error_cm": 200, "skip_bp": 3500},
            "opponent": {"reaction_ms": 0, "aim_error_cm": 0, "skip_bp": 0}})
        for invalid in (trace(), text.replace("skip_bp=3500", "skip_bp=0"), text.replace(line, line + "\n" + line)):
            with self.subTest(text=invalid[:0]):
                out = self.evidence(text=invalid, plan_manifest=plan)
                self.assertEqual(out["status"], "Invalid")
                self.assertIn("Setup: the subject operation knobs did not reach the native profile", out["reason"])
        self.assertEqual(self.evidence()["facts"]["operation"]["subject"],
                         {"reaction_ms": 0, "aim_error_cm": 0, "skip_bp": 0}, "no knobs is exact play")

    def test_actual_field_spawn_is_not_the_first_opening_rest_heartbeat(self):
        rest = heartbeat(100, S, "6900,-500,2") + "\n" + heartbeat(100, O, "7300,-500,2")
        field = "LD_FIELD_ENTER generation=2 segment=0 at_ms=5000 field_t_ms=0 final=0"
        spawn = (f"LD_EVAL_FIELD_SPAWN t=5000 generation=2 contender={S} life=2 pos=-4400,2400,102\n"
                 f"LD_EVAL_FIELD_SPAWN t=5000 generation=2 contender={O} life=2 pos=-2400,4400,102")
        text = trace(drop=("s_spawn", "o_spawn")).replace(field, rest + "\n" + field + "\n" + spawn)
        out = self.evidence(text=text)
        self.assertEqual(out["status"], "Completed", out["reason"])
        self.assertEqual(out["facts"]["spawn"], {"subject": "-4400,2400,102", "opponent": "-2400,4400,102"})
        for invalid in (text.replace(spawn.splitlines()[0] + "\n", ""),
                        text.replace("life=2 pos=-4400", "life=0 pos=-4400"),
                        text.replace("t=5000 generation=2 contender=" + S, "t=4999 generation=2 contender=" + S)):
            self.assertIn("field spawn", self.evidence(text=invalid)["reason"].lower())

    def test_opening_preset_requires_both_staging_and_the_matching_offer(self):
        subject = {"choices": [{"rule": "definition", "definition": "active_elbow"}, "ai_select", "ai_select"],
                   "opening_preset": ["active_elbow"]}
        plan = manifest(subject=subject, sample="diagnostic")
        original = f"LD_OFFER t=0 contender={S} ordinal=1 level=1 cards=active_elbow,active_open_mic,active_unplug"
        fixed = f"LD_OFFER t=0 contender={S} ordinal=1 level=1 cards=active_elbow"
        staged = f"LD_EVAL_OPENING_PRESET contender={S} cards=active_elbow"
        text = trace().replace(original, staged + "\n" + fixed)
        out = self.evidence(text=text, plan_manifest=plan)
        self.assertEqual(out["status"], "Completed", out["reason"])
        self.assertIn("PresetNotApplied", self.evidence(text=text.replace(staged + "\n", ""), plan_manifest=plan)["reason"])
        self.assertIn("PresetNotApplied", self.evidence(text=text.replace(fixed, original), plan_manifest=plan)["reason"])

    def test_level_component_preset_requires_ordered_staging_grants_and_terminal_provenance(self):
        grants = list(LEVEL6_GRANTS)
        subject = {"choices": ["ai_select"] * 3, "level_components": grants}
        plan = manifest(subject=subject, sample="diagnostic")
        value = result()
        value["contenders"][0]["components"] = [
            {**component(name), "source_level": level} for level, name in zip(LEVEL6_DRAW_LEVELS, grants)]
        joined = ",".join(grants)
        stage = f"LD_EVAL_COMPONENT_PRESET t=5005 contender={S} ticket=1 components={joined}"
        reward = f"LD_REWARD t=5016 contender={S} cause=dev_fixture role=recipient xp=600 xp_after=600 level=1->6 "
        old_grant = f"LD_LEVEL_UP t=5016 contender={S} level=1->6 choices_earned=2 unspent_points=5"
        grant = old_grant + f" components={joined} empty_pool_grants=0"
        text = trace().replace(reward, stage + "\n" + reward).replace(old_grant, grant)
        out = self.evidence(value, text, plan)
        self.assertEqual(out["status"], "Completed", out["reason"])
        self.assertEqual(out["facts"]["component_presets"]["subject"][0]["components"], joined)
        variants = [
            text.replace(stage + "\n", ""),
            text.replace(stage, stage + "\n" + stage),
            text.replace(stage + "\n", "").replace(grant, grant + "\n" + stage),
            text.replace("t=5005 contender=" + S, "t=6000 contender=" + S),
            text.replace(grant, old_grant + f" components={','.join(reversed(grants))} empty_pool_grants=0"),
        ]
        for invalid in variants:
            with self.subTest(trace=invalid):
                self.assertIn("PresetNotApplied", self.evidence(value, invalid, plan)["reason"])
        altered = copy.deepcopy(value)
        altered["contenders"][0]["components"][2]["source_level"] = 2
        self.assertIn("provenance", self.evidence(altered, text, plan)["reason"])
        altered["contenders"][0]["components"].pop()
        self.assertIn("provenance", self.evidence(altered, text, plan)["reason"])
        self.assertIn("unplanned level-component", self.evidence(text=trace(stage))["reason"])

    def test_first_field_is_duel_clock_origin_after_opening_rest(self):
        out = self.evidence(text=trace())
        self.assertEqual(out["status"], "Completed", out["reason"])
        self.assertEqual(out["metrics"]["decision_time_ms"], {"value": 15000, "censored": False})

    def test_match_start_cannot_be_substituted_for_field_start(self):
        value = result(started_at_ms=0)
        value["outcome"]["t_ms"] = 20000
        out = self.evidence(value, trace())
        self.assertEqual(out["status"], "Invalid", out)
        self.assertIn("field", out["reason"].lower())

    def test_field_start_must_be_unique_and_match_generation_and_time(self):
        field = "LD_FIELD_ENTER generation=2 segment=0 at_ms=5000 field_t_ms=0 final=0"
        for text in (trace(drop=("field",)), trace(field),
                     trace().replace(field, field.replace("generation=2", "generation=3")),
                     trace().replace(field, field.replace("at_ms=5000", "at_ms=5001")),
                     trace().replace(field, field.replace("field_t_ms=0", "field_t_ms=1")),
                     trace().replace("LD_MATCH_STARTED generation=2", "LD_MATCH_STARTED generation=3")):
            with self.subTest(text=text):
                out = self.evidence(text=text)
                self.assertEqual(out["status"], "Invalid", out)
                self.assertIn("TraceIncomplete", out["reason"])

    def test_timeout_receipt_must_match_the_planned_horizon(self):
        value = result(kind="timeout", ended_at_ms=50000)
        value["contenders"][1] = contender("opponent", 1, O, hp=10000)
        value["outcome"]["t_ms"] = 0
        out = self.evidence(value, trace(drop=("lethal", "death", "death_reward")))
        self.assertEqual(out["status"], "Invalid", out)
        self.assertIn("horizon", out["reason"])

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
                         [("AllocatePoints", "-", "-", "1,2,0,2,0"), ("LearnBlueprint", "active_bail_out", "E", "-")])
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

    def test_simultaneous_reciprocal_kills_are_an_observed_trade_not_a_win_or_timeout(self):
        value = result(kind="trade")
        value["outcome"]["t_ms"] = 15000
        value["contenders"][0] = contender("subject", 0, S, hp=0, body=False)
        reciprocal_hit = hit(20000, O, S, "580.00", "0.00", "580.00", "580.00", "0.00")
        reciprocal_death = (f"LD_DEATH_RECEIPT contender={S} life=1 respawn_at_ms=35000 t=20000 "
                            f"killer={O} cause=Contender fatal=basic fatal_body={O} assisters=- no_reward=None victim_level=6")
        reciprocal_reward = (f"LD_REWARD t=20000 contender={O} cause=death role=killer xp=140 xp_after=740 level=6->6 "
                             f"victim={S} victim_life=1 victim_kind=contender")
        text = trace(reciprocal_hit, reciprocal_death, reciprocal_reward)
        out = self.evidence(value, text)
        self.assertEqual(out["status"], "Completed", out["reason"])
        self.assertEqual(out["metrics"]["duel_score"], {"value": 0, "censored": False})
        self.assertEqual(out["metrics"]["decision_time_ms"], {"value": 15000, "censored": False})
        for invalid in (text.replace(reciprocal_death + "\n", ""),
                        text.replace(reciprocal_hit, reciprocal_hit.replace("t=20000", "t=20001"))):
            self.assertEqual(self.evidence(value, invalid)["status"], "Invalid")
        note = next(line for line in lockdown.limitations(manifest(), self.caps) if "duel_score 为 0" in line)
        self.assertIn("outcome.kind=trade", note)
        self.assertIn("不删失", note, "the report must not call a simultaneous trade timeout censoring")

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
        self.assertIn("another schema", self.evidence(result(schema_version=3))["reason"], "the old result uses the match clock")

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
        opponent_choice3 = choice(5283, O, 3, "active_bail_out", "blueprint", "-", "NoEmptySlot", 6, 0)
        variants = {
            "missing kind": (subject_choice2, subject_choice2.replace("kind=ActiveChoice ", "")),
            "missing result": (subject_choice2, subject_choice2.replace("result=learned ", "")),
            "missing slot": (subject_choice2, subject_choice2.replace("slot=W ", "")),
            "missing stored": (subject_choice2, subject_choice2.replace("stored=- ", "")),
            "missing pending": (subject_choice2, subject_choice2.replace(" pending=1", "")),
            "another command kind": (subject_choice2, subject_choice2.replace("kind=ActiveChoice", "kind=LearnBlueprint")),
            "unknown result": (subject_choice2, subject_choice2.replace("result=learned", "result=replaced")),
            "learned without a slot": (subject_choice2, subject_choice2.replace("slot=W", "slot=-")),
            "learned with a stored reason": (subject_choice2, subject_choice2.replace("stored=-", "stored=NoEmptySlot")),
            "blueprint in a slot": (opponent_choice3, opponent_choice3.replace("slot=-", "slot=E")),
            "blueprint without stored": (opponent_choice3, opponent_choice3.replace("stored=NoEmptySlot ", "")),
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
        stored = choice(5033, S, 2, "active_flyer_trap", "blueprint", "-", "NoEmptySlot", 6, 1)
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
        for original, replacement in ((subject_choice2, choice(5033, S, 2, "active_flyer_trap", "blueprint", "-", "NoEmptySlot", 6, 1)),
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
        self.assertIn(f"InputSchemaVersion = {lockdown.NATIVE_INPUT_SCHEMA_VERSION};", header)
        self.assertIn(f"ResultSchemaVersion = {lockdown.NATIVE_RESULT_SCHEMA_VERSION};", header)


class WorkshopEvidenceTest(AdapterCase):
    """Every planned build step is proven committed, in order and as planned, from the authority LD_BUILD records and the
    terminal components; anything else is Invalid with the precise reason."""

    def workshop(self, value=None, text=None, plan=None):
        return self.evidence(value or workshop_result(), text if text is not None else workshop_trace(),
                             plan or manifest(subject=WORKSHOP))

    def test_a_committed_workshop_plan_completes_with_its_build_facts(self):
        out = self.workshop()
        self.assertEqual(out["status"], "Completed", out["reason"])
        steps = out["facts"]["build_steps"]["subject"]
        self.assertEqual([(step["kind"], step["card"], step["bind"], step["on"], step["points"]) for step in steps],
                         [("OpeningBase", "component_dirty_trick", "active_elbow:Water", None, "-"),
                          ("AllocatePoints", "-", "-", None, "0,0,3,0,0"),
                          ("UninstallComponent", "component_dirty_trick", "-", None, "-"),
                          ("InstallComponent", "component_dirty_trick", "active_elbow:Fire", None, "-"),
                          ("InstallComponent", "component_long_kick", "-", "Active:active_elbow", "-"),
                          ("AllocatePoints", "-", "-", None, "15,0,0,0,0")])
        self.assertEqual(out["facts"]["grants"]["subject"][0]["components"], GRANTS)
        installed = {row["id"]: row for row in out["facts"]["builds"]["subject"]["components"] if row["installed"]}
        self.assertEqual((installed["component_dirty_trick"]["element"], installed["component_long_kick"]["target_id"]),
                         ("Fire", "active_elbow"))

    def test_a_missing_or_refused_step_is_invalid(self):
        out = self.workshop(text=workshop_trace(drop=("w_mod",)))
        self.assertEqual(out["status"], "Invalid")
        self.assertIn("TraceIncomplete: the subject has 5 LD_BUILD records for 6 planned build steps", out["reason"])
        out = self.workshop(workshop_result(steps_committed=4))
        self.assertIn("PlanNotReached: the subject committed 4 of 6 planned build steps", out["reason"])
        refused = workshop_result(steps_committed=4, profile_phase="Failed",
                                  profile_failure="build step 5 (InstallComponent component_long_kick): the component is not "
                                                  "owned (the seeded supply did not grant it)")
        refused.update(status="Invalid", reason="subject plan refused: build step 5 (InstallComponent component_long_kick)")
        out = self.workshop(refused)
        self.assertEqual(out["status"], "Invalid")
        self.assertEqual(out["reason"], "PlanNotReached: the subject build step 5 (InstallComponent component_long_kick): the "
                                        "component is not owned (the seeded supply did not grant it)",
                         "a component the seeded supply did not give ends the Case, never forced")
        out = self.workshop(workshop_result(planned_steps=0))
        self.assertIn("TraceIncomplete: the native subject plan has 0 build steps, planned 6", out["reason"])

    def test_a_step_committed_otherwise_than_planned_is_invalid(self):
        variants = {
            "committed bind=active_elbow:Water, planned active_elbow:Fire":
                {"w_fire": WORKSHOP_LINES["w_fire"].replace("active_elbow:Fire", "active_elbow:Water")},
            "committed on=Active:active_hose_down, planned Active:active_elbow":
                {"w_mod": WORKSHOP_LINES["w_mod"].replace("on=Active:active_elbow", "on=Active:active_hose_down")},
            "committed points=0,3,0,0,0, planned 0,0,3,0,0": {"w_points": WORKSHOP_LINES["w_points"].replace("0,0,3,0,0", "0,3,0,0,0")},
            "committed card=component_fist_fire, planned component_dirty_trick":
                {"w_base": WORKSHOP_LINES["w_base"].replace("card=component_dirty_trick", "card=component_fist_fire")},
            "committed installed=0, planned 1": {"w_base": WORKSHOP_LINES["w_base"].replace("installed=1", "installed=0")},
            "committed after the opening rest": {"w_fire": WORKSHOP_LINES["w_fire"].replace("t=400", "t=5200")},
            "committed before the field supply and the planned choices": {"w_mod": WORKSHOP_LINES["w_mod"].replace("t=5400", "t=5012")},
            "has no authority time in order before the outcome": {"w_body": WORKSHOP_LINES["w_body"].replace("t=5500", "t=20001")},
        }
        for expected, replace in variants.items():
            with self.subTest(expected=expected):
                out = self.workshop(text=workshop_trace(replace=replace))
                self.assertEqual(out["status"], "Invalid")
                self.assertIn(expected, out["reason"])
        out = self.workshop(text=workshop_trace(drop=("field_enter",)))
        self.assertIn("TraceIncomplete: no single LD_FIELD_ENTER of the first field", out["reason"])
        extra = build_line(5600, "AllocatePoints", points="1,0,0,0,0", level=6)
        self.assertIn("7 LD_BUILD records for 6 planned build steps", self.workshop(text=workshop_trace(extra))["reason"],
                      "a planned build sends nothing else")

    def test_terminal_components_must_be_where_the_plan_put_them(self):
        moved = workshop_components()
        moved[4] = component("component_long_kick", True, target_kind="Component", target_id="component_third_punch")
        self.assertIn("PlanNotReached: the subject component_long_kick ends as", self.workshop(workshop_result(components=moved))["reason"])
        water = workshop_components()
        water[1]["element"] = "Water"
        self.assertIn("component_dirty_trick ends as", self.workshop(workshop_result(components=water))["reason"])
        extra = workshop_components()
        extra[0]["installed"] = True
        self.assertIn("component_ball_kick is installed without a planned step",
                      self.workshop(workshop_result(components=extra))["reason"])
        lost = [row for row in workshop_components() if row["id"] != "component_long_kick"]
        self.assertIn("does not own the planned component_long_kick", self.workshop(workshop_result(components=lost))["reason"])
        for broken in ([{"id": "component_long_kick"}], [dict(workshop_components()[0], installed=1)], "x"):
            with self.subTest(components=broken):
                out = self.workshop(workshop_result(components=broken))
                self.assertIn("TraceIncomplete: native contender identities or terminal progression are missing", out["reason"])

    def test_an_unplanned_side_keeps_the_production_rule_unchecked(self):
        out = self.evidence()
        self.assertEqual(out["status"], "Completed", out["reason"])
        self.assertEqual(len(out["facts"]["build_steps"]["opponent"]), 2, "the production builds are facts, not plan evidence")

    def test_same_materials_different_uses_branch_on_the_shared_prefix(self):
        def arms(first, second):
            plan = manifest(subject=first, design="decision")
            plan["design"].pop("reference")
            plan["design"]["arms"] = [{"id": "a", "plan": {"subject": first, "opponent": {"choices": ["ai_select"] * 3}}},
                                      {"id": "b", "plan": {"subject": second, "opponent": {"choices": ["ai_select"] * 3}}}]
            return plan

        def judged(plan, arm, text, value=None):
            case_id = f"c000a-{arm}"
            value = value or workshop_result()
            value["case_id"] = case_id
            text = text.replace("LD_EVAL_CASE_BEGIN case=c000a-t", f"LD_EVAL_CASE_BEGIN case={case_id}")
            out = self.evidence(value, text, plan, {"case_id": case_id, "seed": "7", "arm": arm, "side": 0})
            self.assertEqual(out["status"], "Completed", out["reason"])
            return out["facts"]

        water = copy.deepcopy(WORKSHOP)
        water["build"][3]["bind"]["element"] = "water"
        plan = arms(WORKSHOP, water)
        self.assertEqual(lockdown.branch_point(plan["design"]), 4, "the opening choice and three opening steps come first")
        self.assertIsNone(lockdown.branch_ordinal(plan["design"]), "the choices are the same")
        fire = judged(plan, "a", workshop_trace())
        self.assertEqual(fire["label"], "InstallComponent component_dirty_trick bind=active_elbow:Fire")
        components = workshop_components()
        components[1]["element"] = "Water"
        other = judged(plan, "b", workshop_trace(replace={"w_fire": WORKSHOP_LINES["w_fire"].replace(":Fire", ":Water")}),
                       workshop_result(components=components))
        self.assertEqual(other["label"], "InstallComponent component_dirty_trick bind=active_elbow:Water")
        self.assertEqual(fire["prefix"], other["prefix"], "the same history up to the rebuild")
        prefix = json.loads(fire["prefix"])
        self.assertEqual(prefix["branch"], {"position": 4})
        self.assertEqual([step["kind"] for step in prefix["builds"]], ["OpeningBase", "AllocatePoints", "UninstallComponent"])
        self.assertEqual(prefix["history"][0]["choice"]["card"], "active_elbow")
        # A field branch shares the seeded level-up components: different draws are not the same materials.
        strength = copy.deepcopy(WORKSHOP)
        strength["build"][5]["points"] = {"stat_strength": 15}
        plan = arms(WORKSHOP, strength)
        self.assertEqual(lockdown.branch_point(plan["design"]), 8)
        same = judged(plan, "a", workshop_trace())
        self.assertEqual(json.loads(same["prefix"])["grants"][0]["components"], GRANTS)
        drawn = workshop_trace(replace={"w_level": WORKSHOP_LINES["w_level"].replace("component_seeing_red", "component_thick_hide")})
        self.assertNotEqual(same["prefix"], judged(plan, "a", drawn)["prefix"])


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

    @staticmethod
    def swapped(text, first, second):
        lines = text.splitlines()
        a = next(i for i, line in enumerate(lines) if line.startswith(first))
        b = next(i for i, line in enumerate(lines) if line.startswith(second))
        lines[a], lines[b] = lines[b], lines[a]
        return "\n".join(lines) + "\n"

    def test_canonical_events_accept_engine_order_of_independent_same_tick_preparation(self):
        base, _ = lockdown.canonical_events(trace(), result())
        for first, second in ((f"LD_OFFER t=5016 contender={S}", f"LD_OFFER t=5016 contender={O}"),
                              (f"LD_CHOICE t=33 contender={S}", f"LD_CHOICE t=33 contender={O}"),
                              (f"LD_BUILD t=5300 contender={S}", f"LD_BUILD t=5300 contender={O}")):
            events, _ = lockdown.canonical_events(self.swapped(trace(), first, second), result())
            self.assertEqual(events, base, first)
        relative, _ = lockdown.canonical_events(trace().replace("generation=2", "generation=7"), result(generation=7))
        self.assertEqual(relative, base, "the process-wide match generation is relative to the Case")
        self.assertTrue(any("generation=case" in line for line in base))

    def test_canonical_events_keep_every_causal_or_cross_tick_order(self):
        base, _ = lockdown.canonical_events(trace(), result())
        same_tick_hits = trace(hit(12000, S, O, "1.00", "0.00", "1.00", "520.00", "519.00"),
                               hit(12000, O, S, "2.00", "0.00", "2.00", "580.00", "578.00"))
        hits, _ = lockdown.canonical_events(same_tick_hits, result())
        swapped_hits, _ = lockdown.canonical_events(
            self.swapped(same_tick_hits, f"LD_HIT t=12000 src={S}", f"LD_HIT t=12000 src={O}"), result())
        self.assertNotEqual(hits, swapped_hits, "combat events at one tick keep their authority order")
        own = trace().replace(f"LD_BUILD t=5300 contender={O}",
                              build_line(5300, "InstallComponent", "component_long_kick", installed="1", level=6)
                              + f"\nLD_BUILD t=5300 contender={O}")
        own_events, _ = lockdown.canonical_events(own, result())
        own_swapped, _ = lockdown.canonical_events(
            self.swapped(own, f"LD_BUILD t=5300 contender={S} kind=AllocatePoints", f"LD_BUILD t=5300 contender={S} kind=InstallComponent"),
            result())
        self.assertNotEqual(own_events, own_swapped, "one contender's own commands keep their order")
        across, _ = lockdown.canonical_events(
            self.swapped(trace(), f"LD_CHOICE t=5283 contender={O}", f"LD_BUILD t=5300 contender={S}"), result())
        self.assertNotEqual(across, base, "commands at different ticks are never reordered")
        foreign, _ = lockdown.canonical_events(trace().replace("LD_EVAL_FIELD_SPAWN t=5000 generation=2",
                                                               "LD_EVAL_FIELD_SPAWN t=5000 generation=3"), result())
        self.assertNotEqual(foreign, base, "another match's generation is still a difference")
        interleaved = trace().replace(
            f"LD_BUILD t=5300 contender={O}",
            hit(5300, S, O, "1.00", "0.00", "1.00", "620.00", "619.00") + f"\nLD_BUILD t=5300 contender={O}")
        split, _ = lockdown.canonical_events(interleaved, result())
        split_swapped, _ = lockdown.canonical_events(
            self.swapped(interleaved, f"LD_BUILD t=5300 contender={S}", f"LD_BUILD t=5300 contender={O}"), result())
        self.assertNotEqual(split, split_swapped, "a combat event between commands ends the reorderable run")

    def test_replay_evidence_contains_terminal_build_and_heartbeat_resources(self):
        run = {"schema": lockdown.NATIVE_RUN_SCHEMA, "schema_version": lockdown.NATIVE_RESULT_SCHEMA_VERSION,
               "content_root": CONTENT_ROOT, "run_id": "r",
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
        good = {"schema": lockdown.NATIVE_RUN_SCHEMA, "schema_version": lockdown.NATIVE_RESULT_SCHEMA_VERSION,
                "content_root": self.caps["content"]["digest"].upper(),
                "fixed_time_step": True, "seed_derivation": 1}
        self.assertEqual(lockdown.identity_problems(good, self.caps), [])
        self.assertTrue(lockdown.identity_problems(dict(good, schema_version=3), self.caps), "the old run uses the match clock")
        self.assertTrue(lockdown.identity_problems(dict(good, content_root="0" * 64), self.caps))
        self.assertTrue(lockdown.identity_problems(dict(good, fixed_time_step=False), self.caps))
        self.assertTrue(lockdown.identity_problems(dict(good, seed_derivation=2), self.caps))


if __name__ == "__main__":
    unittest.main()
