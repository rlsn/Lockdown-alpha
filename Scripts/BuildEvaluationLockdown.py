"""Lockdown v0.10 Demo adapter of the build evaluation core (docs/systems/BUILD_EVALUATION.md).

It describes what the current runtime can evaluate, from the catalog the native game loads (demo_content.json, whose
manifest digest is the native catalog root), and turns the native Host driver's evidence into Case statuses, metrics and
facts. It reads the authority LD_* lines the Host log contract already defines; it never evaluates damage, draws offers or
components or decides an outcome itself. A definition that is not in the runtime catalog has no native handler and is
unsupported.

#198 growth: a plan fixes the active choice of each active opportunity (the opening one and one per level of
progression.active_offer_levels reached). Without a build list the production AI rule (LDOpponentPolicy::DecideFollowUpBuild)
does every other build command. #198 workshop (review §4, spec §19): a side's build list owns every build command except the
active choices: the native evaluation policy sends exactly its steps, in order, through the players' build commands when
growth's gate admits them, and the production rule never builds for that side. Supported steps: the opening base pick (B18's
active and element), exact points per stat, a component install on a named target (a field first install of a level-up
component, or an opening-rest move), an uninstall, a respec, a blueprint learn and an active replacement. Components are
mechanics. Plans are checked against what duel_site's legal supply can give (the opening pick, the seeded level-up draws of
the common pool); a step the seeded Case cannot carry out ends Invalid (PlanNotReached), never forced. Every step's commit is
verified against the authority LD_BUILD record and the terminal components. NPC purchases, forgetting, FillGap, Dismantle,
elite component choices and a replacement inside an active choice are not plannable and are refused as UnsupportedDecision
with their reason.
"""

import hashlib
import json
import math
import re
from pathlib import Path

ADAPTER_ID = "lockdown-demo"
ADAPTER_VERSION = 8
# LDBuildEvaluation::MaxCases and MaxBuildSteps of the native driver (LDBuildEvaluationProbe.h); a larger plan is refused
# before any run.
NATIVE_MAX_CASES = 500
NATIVE_MAX_BUILD_STEPS = 32
NATIVE_INPUT_SCHEMA = "lockdown.build_evaluation.native_input"
NATIVE_RESULT_SCHEMA = "lockdown.build_evaluation.native_result"
NATIVE_RUN_SCHEMA = "lockdown.build_evaluation.native_run"
NATIVE_INPUT_SCHEMA_VERSION = 3
NATIVE_RESULT_SCHEMA_VERSION = 4
SEED_DERIVATION = 1
ROLES = ("subject", "opponent")
PUBLIC_PLACE_KINDS = ("Spawn", "Camp", "Capture", "Dorm", "Airdrop")
CATALOG_EVIDENCE = "in the runtime catalog the native importer accepted (it refuses any definition without a handler)"
CANONICAL_TAGS = ("LD_OFFER", "LD_CHOICE", "LD_BUILD", "LD_REWARD", "LD_LEVEL_UP", "LD_HIT", "LD_DEATH_RECEIPT")
FIELD = re.compile(r'(\w+)=("(?:[^"\\]|\\.)*"|\S*)')
GUID = re.compile(r"\b[0-9A-F]{32}\b")
DEFINITION = re.compile(r"^[a-z][a-z0-9_]*$")
P2 = re.compile(r"^-?\d+\.\d{2}$")
SLOTS = ("Q", "W", "E")
COMPONENT_KINDS = ("base", "mod", "connector")
# Build windows of duel_site: the opening rest R0 (after the opening choice, before the field supply) and the field (once
# the supply's level and every planned choice are reached).
WINDOWS = ("opening_rest", "field")
# The catalog's binding elements and their authority log names (LDLog::EnumName of ELDDemoElement).
ELEMENTS = {"fire": "Fire", "water": "Water", "electric": "Electric"}
# Install targets of a mod or connector and their authority log names (ELDComponentTargetKind); the holder itself is None.
TARGET_KINDS = {"active": "Active", "component": "Component", "dash": "Dash", "self": None}
# Planned build steps: their capability decision, the LD_BUILD kind (ELDBuildCommandKind) and their fields.
STEPS = {
    "opening_base": ("choose_opening_base", "OpeningBase", ("component", "bind")),
    "allocate_points": ("allocate_points", "AllocatePoints", ("points",)),
    "install": ("install_component", "InstallComponent", ("component", "bind", "target", "base")),
    "uninstall": ("uninstall_component", "UninstallComponent", ("component",)),
    "learn": ("learn_blueprint", "LearnBlueprint", ("blueprint", "slot")),
    "replace": ("replace_active", "ReplaceActive", ("blueprint", "slot")),
    "respec": ("respec_points", "Respec", ()),
}
REQUIRED_FIELDS = {"opening_base": ("component",), "install": ("component",), "uninstall": ("component",),
                   "allocate_points": ("points",), "learn": ("blueprint", "slot"), "replace": ("blueprint", "slot")}
# Spec §5: these never open on the field.
REST_ONLY = ("uninstall", "replace", "respec")
# Decisions a plan may name that the current runtime does not carry out as planned, with the reason; the plan is refused
# as UnsupportedDecision. Build steps by their step name; "replace" inside an active choice rule.
UNPLANNABLE = {
    "buy_npc_offer": ("buy_npc_offer", "a rest NPC purchase opens an extra active opportunity whose ordinal the choices "
                      "would have to name; the native plan does not carry it"),
    "forget": ("forget_active", "the AI has no forget command; a replacement forgets the replaced active"),
    "fill_gap": ("fill_gap", "a rest service for a never-enabled mod or connector without an entry; duel_site's only rest "
                 "(the opening rest) precedes the supply and holds only the opening base, so the native plan does not carry it"),
    "dismantle": ("dismantle_components", "a rest service surrendering GROWTH.DismantleInputCount owned common components; "
                  "duel_site's only rest (the opening rest) holds only the opening base, so the native plan does not carry it"),
    "component_choice": ("choose_elite_component", "an elite camp's field reward; camps are outside the duel (a camp hit "
                         "invalidates the Case), so no choice is ever pending"),
    "replace": ("replace_at_choice", "an active choice learns into an empty slot or keeps a blueprint; a replacement is "
                "its own rest-window build step (replace)"),
}
UNPLANNABLE_STEPS = ("buy_npc_offer", "forget", "fill_gap", "dismantle", "component_choice")

METRICS = {
    "duel_score": {"unit": "score", "kind": "score", "direction": "higher_better",
                   "description": "+1 the subject killed the opponent, -1 the subject died first, 0 undecided at the duel timeout"},
    "damage_dealt_hp": {"unit": "hp", "kind": "continuous", "direction": "higher_better",
                        "description": "HP the opponent lost to the subject's committed hits up to the outcome (shield absorption excluded)"},
    "damage_taken_hp": {"unit": "hp", "kind": "continuous", "direction": "lower_better",
                        "description": "HP the subject lost to the opponent's committed hits up to the outcome"},
    "subject_hp_end_bp": {"unit": "basis points", "kind": "continuous", "direction": "higher_better",
                          "description": "the subject's HP share when the Case ended (0 when dead)"},
    "decision_time_ms": {"unit": "ms", "kind": "time_to_event", "direction": "descriptive",
                         "description": "authority time from first-field entry to the first duelist death, censored at the duel timeout"},
}


# ---------------------------------------------------------------- capabilities

def _content_paths(project_root):
    data = Path(project_root) / "Content" / "Lockdown" / "Data" / "Demo"
    return data / "manifest.json", data / "demo_content.json", data / "world.json"


def product_version(project_root):
    section = None
    for line in (Path(project_root) / "Config" / "DefaultGame.ini").read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
        elif section == "/Script/EngineSettings.GeneralProjectSettings" and line.startswith("ProjectVersion="):
            return line.split("=", 1)[1].strip()
    return "unknown"


def cumulative_xp(progression):
    out = [0]
    for step in progression["xp_to_next"]:
        out.append(out[-1] + step)
    return out


def level_for(progression, xp):
    return sum(1 for threshold in cumulative_xp(progression) if threshold <= xp)


def describe(project_root):
    manifest_path, content_path, world_path = _content_paths(project_root)
    content = json.loads(content_path.read_text(encoding="utf-8"))
    world = json.loads(world_path.read_text(encoding="utf-8"))
    mechanics = {}
    for ability in content["abilities"]:
        mechanics[ability["id"]] = {"status": "Supported", "kind": "active", "opening": bool(ability.get("opening")),
                                    "evidence": CATALOG_EVIDENCE}
    for stat in content["stats"]:
        mechanics[stat["id"]] = {"status": "Supported", "kind": "stat", "evidence": CATALOG_EVIDENCE}
    for component in content["components"]:
        opening = component.get("opening")
        binding = component.get("binding")
        mechanics[component["id"]] = {
            "status": "Supported", "kind": component["kind"], "pool": component["pool"],
            # The opening base pick offers it always, after one of these opening actives, or never.
            "opening_base": "always" if opening == "always" else list(opening["actives"]) if isinstance(opening, dict) else [],
            "binding": ({"kind": binding["kind"], "actives": list(binding["actives"]), "elements": list(binding["elements"])}
                        if binding else None),
            "targets": component.get("targets"),
            "evidence": CATALOG_EVIDENCE}
    places = sorted(anchor["id"] for anchor in world["anchors"] if anchor["kind"] in PUBLIC_PLACE_KINDS)
    progression = content["progression"]
    boss_appear_ms = int(content["boss"]["appear_at"]["ms"])
    first_field_ms = int(content["parameters"]["MATCH.FieldSegmentSec"]["ms"])
    return {
        "schema": "build_evaluation.capabilities", "schema_version": 1,
        "adapter": {"id": ADAPTER_ID, "version": ADAPTER_VERSION},
        "content": {"id": "lockdown-demo-content", "version": product_version(project_root),
                    "digest": hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
                    "abilities": len(content["abilities"]), "stats": len(content["stats"]),
                    "components": len(content["components"])},
        "mechanics": mechanics,
        "decisions": {
            "ai_select": {"description": "the production active preference (LDOpponentPolicy::SelectActive)"},
            "option_index": {"description": "the option at index 0..2 of the actual active offer; refused when it has fewer"},
            "definition": {"description": "the actual active offer's option of one active; refused when not offered"},
            "preset_offer": {"description": "a one-to-three-active offer prepared by DevQueueOffer from ordinal 2",
                             "sample_classes": ["diagnostic"]},
            "choose_opening_base": {"description": "build step opening_base, in the opening rest: a named option of the actual "
                                                   "opening base pick, enabled at once, with B18's learned active and element; "
                                                   "refused when not offered"},
            "allocate_points": {"description": "build step allocate_points: exact points per stat id in one command; a planned "
                                               "build spends every point its supply earns"},
            "install_component": {"description": "build step install: an owned component onto a named target (B18's active and "
                                                 "element, a learned active, an installed host, the innate D, the holder, "
                                                 "C12's installed base): a field first install of a level-up component the "
                                                 "seeded supply must grant, or an opening-rest move of the opening base"},
            "uninstall_component": {"description": "build step uninstall, rest window only (the opening rest in duel_site); "
                                                   "what sits on it comes off with it"},
            "respec_points": {"description": "build step respec, rest window only: every allocated point back"},
            "learn_blueprint": {"description": "build step learn: a pending blueprint into a named empty slot (duel_site "
                                               "never leaves a blueprint pending, so its plans are refused there)"},
            "replace_active": {"description": "build step replace, rest window only: a pending blueprint into an occupied "
                                              "slot, the old active forgotten and its attachments back in the inventory "
                                              "(duel_site's only rest holds one active, so its plans are refused there)"},
        },
        "scenarios": {
            "duel_site": {
                "evidence_level": "NativeSpatial",
                "sample_classes": ["legal_search", "diagnostic"],
                "sides": 2,
                "budget_classes": ["standard"],
                "metrics": list(METRICS),
                "parameters": {"site": places, "duel_timeout_ms": [1000, min(first_field_ms, boss_appear_ms) - 1],
                               "first_field_ms": first_field_ms,
                               "supply_xp": [0, cumulative_xp(progression)[-1]],
                               "xp_to_next": progression["xp_to_next"],
                               "active_offer_levels": progression["active_offer_levels"],
                               "component_category_weights": progression["component_category_weights"],
                               "opening_points": int(content["parameters"]["GROWTH.OpeningPoints"]),
                               "points_per_level": int(content["parameters"]["GROWTH.PointsPerLevel"]),
                               "stat_design_order": [stat["id"] for stat in
                                                     sorted(content["stats"], key=lambda row: row["native_id"])]},
                "description": ("A Listen Host of L_Demo runs one ordinary match per Case: the Host only orchestrates; two AI "
                                "contenders choose their active offers along their plans from a T=0 XP supply, build by their "
                                "planned build steps (or, without them, by the production AI rule), walk to a public place and "
                                "fight each other with the production engagement until a death or the duel timeout."),
            }
        },
        "metrics": METRICS,
        "limits": {"max_cases_per_run": NATIVE_MAX_CASES},
        "random_streams": {
            "seeded": ["growth offers (MatchSecret)", "critical rolls", "spawn anchors and AI opening timing",
                       "airdrop points", "AI contender and session identities"],
            "unseeded": ["leader notice circle (ledger identity)", "camp member and effect identities (not ordering inputs)"],
        },
    }


# ---------------------------------------------------------------- plans

def _rule(rule, where):
    if rule == "ai_select":
        return {"rule": "ai_select"}
    if not isinstance(rule, dict) or rule.get("rule") not in ("ai_select", "option_index", "definition"):
        raise ValueError(f"{where}: ai_select, or an object with rule option_index or definition")
    unknown = set(rule) - {"rule", "index", "definition", "replace"}
    if unknown:
        raise ValueError(f"{where}: unknown fields {sorted(unknown)}")
    if rule["rule"] == "ai_select":
        if set(rule) - {"rule", "replace"}:
            raise ValueError(f"{where}: ai_select takes nothing else")
        return dict(rule)
    if rule["rule"] == "option_index":
        index = rule.get("index")
        if isinstance(index, bool) or not isinstance(index, int) or index not in (0, 1, 2) or "definition" in rule:
            raise ValueError(f"{where}: option_index takes integer index 0..2")
    if rule["rule"] == "definition" and (not isinstance(rule.get("definition"), str) or "index" in rule):
        raise ValueError(f"{where}: definition takes a definition id")
    return dict(rule)


def _side_plan(plan, role):
    side = plan.get(role)
    if not isinstance(side, dict) or set(side) - {"choices", "presets", "build"} or "choices" not in side:
        raise ValueError(f"plan.{role}: an object with choices (and optional presets and build)")
    if not isinstance(side["choices"], list):
        raise ValueError(f"plan.{role}.choices: a list")
    choices = [_rule(rule, f"plan.{role}.choices[{index}]") for index, rule in enumerate(side["choices"])]
    presets = side.get("presets", [])
    if not isinstance(presets, list) or any(not isinstance(row, list) or not 1 <= len(row) <= 3
                                            or any(not isinstance(card, str) for card in row) or len(set(row)) != len(row)
                                            for row in presets):
        raise ValueError(f"plan.{role}.presets: lists of one to three distinct active ids")
    return choices, presets


def _definition(value, where):
    if not isinstance(value, str) or not DEFINITION.fullmatch(value):
        raise ValueError(f"{where}: a definition id")
    return value


def _build_step(step, where):
    """One planned build step in its normalized shape (the native input's); unplannable steps keep only step and at."""
    if not isinstance(step, dict) or step.get("step") not in set(STEPS) | set(UNPLANNABLE_STEPS):
        raise ValueError(f"{where}: an object whose step is one of {sorted(set(STEPS) | set(UNPLANNABLE_STEPS))}")
    kind = step["step"]
    if step.get("at") not in WINDOWS:
        raise ValueError(f"{where}.at: one of {list(WINDOWS)}")
    if kind in UNPLANNABLE_STEPS:
        return {"step": kind, "at": step["at"]}
    fields = STEPS[kind][2]
    unknown = set(step) - {"step", "at"} - set(fields)
    missing = [key for key in REQUIRED_FIELDS.get(kind, ()) if key not in step]
    if unknown or missing:
        raise ValueError(f"{where}: {kind} takes {['step', 'at', *fields]} (unknown {sorted(unknown)}, missing {missing})")
    out = {"step": kind, "at": step["at"]}
    for key in ("component", "blueprint", "base"):
        if key in step:
            out[key] = _definition(step[key], f"{where}.{key}")
    if "slot" in step:
        if step["slot"] not in SLOTS:
            raise ValueError(f"{where}.slot: one of {list(SLOTS)}")
        out["slot"] = step["slot"]
    if "bind" in step:
        bind = step["bind"]
        if not isinstance(bind, dict) or set(bind) != {"active", "element"} or bind["element"] not in ELEMENTS:
            raise ValueError(f"{where}.bind: an object with active and element (one of {list(ELEMENTS)})")
        out["bind"] = {"active": _definition(bind["active"], f"{where}.bind.active"), "element": bind["element"]}
    if "target" in step:
        target = step["target"]
        named = isinstance(target, dict) and target.get("kind") in ("active", "component")
        if (not isinstance(target, dict) or target.get("kind") not in TARGET_KINDS
                or set(target) != ({"kind", "id"} if named else {"kind"})):
            raise ValueError(f"{where}.target: kind active or component with an id, or kind dash or self alone")
        out["target"] = {"kind": target["kind"], **({"id": _definition(target["id"], f"{where}.target.id")} if named else {})}
    if "points" in step:
        points = step["points"]
        if (not isinstance(points, dict) or not points
                or any(isinstance(count, bool) or not isinstance(count, int) or not 1 <= count <= 255 for count in points.values())):
            raise ValueError(f"{where}.points: an object of stat ids to 1..255 points")
        out["points"] = {_definition(stat, f"{where}.points"): count for stat, count in sorted(points.items())}
    return out


def _side_build(plan, role):
    """The side's planned build steps in order; empty when the production rule builds for it."""
    steps = plan[role].get("build", [])
    if not isinstance(steps, list) or len(steps) > NATIVE_MAX_BUILD_STEPS:
        raise ValueError(f"plan.{role}.build: a list of at most {NATIVE_MAX_BUILD_STEPS} steps")
    return [_build_step(step, f"plan.{role}.build[{index}]") for index, step in enumerate(steps)]


def _supply_problem(component, row, level, params):
    """Why the seeded level-up supply of duel_site can never grant this component; None when a draw can."""
    if row["pool"] != "common":
        return f"{component} is a {row['pool']} component (Boss and airdrop rewards only), never a level-up draw of the supply"
    if level < 2:
        return f"the supply reaches level {level}: no level-up draws a component"
    if not any(band[row["kind"]] > 0 for band in params["component_category_weights"]
               for reached in range(2, level + 1) if band["levels"][0] <= reached <= band["levels"][1]):
        return f"no level 2..{level} band draws a {row['kind']}"
    return None


def _build_needs(role, steps, choices, caps, supply_xp):
    """What a planned build needs, and what duel_site provably cannot give it. Components come only from the opening base
    pick and the seeded level-up draws of the common pool; a named active is learned only through a definition choice."""
    params = caps["scenarios"]["duel_site"]["parameters"]
    rows = caps["mechanics"]
    level = level_for({"xp_to_next": params["xp_to_next"]}, supply_xp)
    earned = params["opening_points"] + params["points_per_level"] * (level - 1)
    opening = choices[0]["definition"] if choices and choices[0]["rule"] == "definition" else None
    learned = {"opening_rest": {opening} - {None},
               "field": {rule["definition"] for rule in choices if rule["rule"] == "definition"}}
    # component -> its planned state: installed, its host component and repeated base (for what comes off with it).
    owned = {}
    allocated = 0
    picked = False
    field = False
    mechanics, decisions, problems = set(), set(), []
    for index, step in enumerate(steps):
        kind, at = step["step"], step["at"]
        decisions.add(STEPS[kind][0])
        where = f"plan.{role}.build[{index}] ({kind})"

        def problem(text):
            problems.append(f"IllegalDecision: {where}: {text}")

        if at == "opening_rest" and field:
            problem("the steps run in order and the opening rest comes before the field")
        field |= at == "field"
        bind, target = step.get("bind"), step.get("target") or {}
        names = [step.get("component"), step.get("blueprint"), step.get("base"), (bind or {}).get("active"),
                 target.get("id"), *step.get("points", {})]
        names = [name for name in names if name]
        mechanics.update(names)
        if any(name not in rows for name in names):
            continue
        component = step.get("component")
        row = rows.get(component, {})
        if component and row.get("kind") not in COMPONENT_KINDS:
            problem(f"{component} is a {row.get('kind')}, not a component")
            continue
        if kind in REST_ONLY and at != "opening_rest":
            problem("a rest-window command (spec §5); duel_site has no rest window after the supply")
            continue

        def check_binding():
            binding = row.get("binding")
            if not binding:
                if bind:
                    problem(f"{component} takes no binding")
                return
            if not bind:
                problem(f"{component} binds a learned active and an element")
            elif bind["active"] not in binding["actives"] or bind["element"] not in binding["elements"]:
                problem(f"{component} binds one of {binding['actives']} with one of {binding['elements']}")
            elif bind["active"] not in learned[at]:
                problem(f"cannot prove {bind['active']} is learned here: choose it by definition"
                        + (" at ordinal 1 (the only active of the opening rest)" if at == "opening_rest" else ""))

        if kind == "opening_base":
            options = row.get("opening_base")
            if at != "opening_rest":
                problem("the opening base is picked in the opening rest")
            elif picked:
                problem("the opening base is picked once")
            elif row["kind"] != "base":
                problem(f"{component} is a {row['kind']}, never an opening base")
            elif options != "always" and not options:
                problem(f"{component} is never an opening base option")
            elif options != "always" and opening is None:
                problem(f"{component} is an opening option only after one of {options}; choose that opening active by definition")
            elif options != "always" and opening not in options:
                problem(f"{component} is not an opening option after {opening}")
            else:
                check_binding()
            picked = True
            owned[component] = {"installed": True, "host": None, "base": None}
        elif kind == "install":
            held = owned.get(component)
            if held and held["installed"]:
                problem(f"{component} is already installed by an earlier step")
            elif held and at == "field":
                problem(f"re-installing the once-installed {component} is a rest-window move (spec §5); duel_site has no "
                        "rest window after the supply")
            elif not held and at == "opening_rest":
                problem(f"{component} is not owned in the opening rest: only the opening base pick gives a component there")
            elif not held and _supply_problem(component, row, level, params):
                problem(_supply_problem(component, row, level, params))
            host = None
            if row["kind"] == "base":
                if step.get("target") or step.get("base"):
                    problem(f"base component {component} takes no target")
                check_binding()
            else:
                targets = row.get("targets") or {}
                if bind:
                    problem(f"{row['kind']} {component} takes no binding")
                if not target:
                    problem(f"{row['kind']} {component} needs a target")
                elif target["kind"] == "active" and target["id"] not in targets.get("actives", []):
                    problem(f"{component} does not go onto {target['id']}")
                elif target["kind"] == "active" and target["id"] not in learned[at]:
                    problem(f"cannot prove {target['id']} is learned here: choose it by definition")
                elif target["kind"] == "component" and target["id"] not in targets.get("components", []):
                    problem(f"{component} does not go onto {target['id']}")
                elif target["kind"] == "component" and not owned.get(target["id"], {}).get("installed"):
                    problem(f"the host {target['id']} is not installed by an earlier step")
                elif target["kind"] in ("dash", "self") and not targets.get(target["kind"]):
                    problem(f"{component} does not go onto {'the innate D' if target['kind'] == 'dash' else 'the holder itself'}")
                host = target.get("id") if target.get("kind") == "component" else None
                bases, base = targets.get("bases", []), step.get("base")
                if bases and not base:
                    problem(f"{component} repeats an installed base of {bases}")
                elif base and base not in bases:
                    problem(f"{component} cannot repeat {base}")
                elif base and not owned.get(base, {}).get("installed"):
                    problem(f"the repeated base {base} is not installed by an earlier step")
            owned[component] = {"installed": True, "host": host, "base": step.get("base")}
        elif kind == "uninstall":
            if not owned.get(component, {}).get("installed"):
                problem(f"{component} is not installed by an earlier step")
            else:
                removing = [component]
                for name in removing:
                    owned[name]["installed"] = False
                    removing += [other for other, state in owned.items() if state["installed"] and other not in removing
                                 and name in (state["host"], state["base"])]
        elif kind == "allocate_points":
            wrong = [stat for stat in step["points"] if rows[stat]["kind"] != "stat"]
            if wrong:
                problem(f"{wrong} are not stats")
            allocated += sum(step["points"].values())
            budget = params["opening_points"] if at == "opening_rest" else earned
            if allocated > budget:
                problem(f"{allocated} points allocated by here; {'the opening rest holds' if at == 'opening_rest' else 'the supply earns'} {budget}")
        elif kind == "respec":
            if not allocated:
                problem("no point is allocated by an earlier step")
            allocated = 0
        elif kind == "learn":
            problem("duel_site never leaves a blueprint pending: every planned choice is learned at its choice (in the "
                    "opening rest, or at the field start inside the build safe window, always with an empty slot)")
        elif kind == "replace":
            problem("the opening rest holds the opening active and no pending blueprint, so nothing can replace it")
    if steps and allocated != earned:
        problems.append(f"IllegalDecision: plan.{role}.build allocates {allocated} of the {earned} points its supply earns; a "
                        "planned build owns every build command, so it spends every earned point (spec §4.1)")
    return mechanics, decisions, problems


def requirements(scenario, plan, caps):
    if set(plan) - set(ROLES):
        raise ValueError(f"plan: only {ROLES}")
    supply = scenario["parameters"]["supply_xp"]
    mechanics, decisions, problems = set(), set(), []
    for role in ROLES:
        choices, presets = _side_plan(plan, role)
        steps = _side_build(plan, role)
        for index, rule in enumerate(choices):
            if "replace" in rule:
                decision, reason = UNPLANNABLE["replace"]
                problems.append(f"UnsupportedDecision: {decision} (plan.{role}.choices[{index}]): {reason}")
        unplannable = [(index, step["step"]) for index, step in enumerate(steps) if step["step"] in UNPLANNABLE_STEPS]
        for index, kind in unplannable:
            decision, reason = UNPLANNABLE[kind]
            problems.append(f"UnsupportedDecision: {decision} (plan.{role}.build[{index}]): {reason}")
        if not unplannable:
            needs = _build_needs(role, steps, choices, caps, supply[role])
            mechanics |= needs[0]
            decisions |= needs[1]
            problems += needs[2]
        opportunities = _opportunities(caps, supply[role])
        if len(choices) != opportunities:
            problems.append(f"IllegalDecision: plan.{role} has {len(choices)} rules but its supply of {supply[role]} XP "
                            f"gives {opportunities} active opportunities (the opening one included)")
        for ordinal, rule in enumerate(choices, 1):
            decisions.add(rule["rule"])
            if rule["rule"] != "definition":
                continue
            mechanics.add(rule["definition"])
            row = caps["mechanics"].get(rule["definition"], {})
            if row and row.get("kind") != "active":
                problems.append(f"IllegalDecision: {rule['definition']} is a {row.get('kind')}, never an offered active")
            elif row and ordinal == 1 and not row.get("opening"):
                problems.append(f"IllegalDecision: plan.{role} ordinal 1 can never offer {rule['definition']}"
                                " (the opening offer draws from the opening pool only)")
        if presets:
            decisions.add("preset_offer")
            if len(presets) > max(0, opportunities - 1):
                problems.append(f"IllegalDecision: plan.{role} prepares {len(presets)} offers for {max(0, opportunities - 1)} later opportunities")
        for row in presets:
            for card in row:
                mechanics.add(card)
                kind = caps["mechanics"].get(card, {}).get("kind")
                if kind and kind != "active":
                    problems.append(f"IllegalDecision: plan.{role} presets name {kind} {card}; only actives are offered")
    return {"mechanics": sorted(mechanics), "decisions": sorted(decisions), "problems": problems}


def _opportunities(caps, supply_xp):
    """Active opportunities a supply gives: the opening one plus one per active_offer_levels entry reached."""
    params = caps["scenarios"]["duel_site"]["parameters"]
    level = level_for({"xp_to_next": params["xp_to_next"]}, supply_xp)
    return 1 + sum(1 for offer_level in params["active_offer_levels"] if offer_level <= level)


def scenario_problems(scenario, caps):
    if scenario["kind"] != "duel_site":
        return []
    params = scenario["parameters"]
    spec = caps["scenarios"]["duel_site"]["parameters"]
    problems = []
    unknown = set(params) - {"site", "duel_timeout_ms", "supply_xp"}
    if unknown:
        problems.append(f"IllegalScenario: unknown duel_site parameters {sorted(unknown)}")
    if params.get("site") not in spec["site"]:
        problems.append(f"IllegalScenario: site {params.get('site')} is not a public place of this world")
    timeout = params.get("duel_timeout_ms")
    low, high = spec["duel_timeout_ms"]
    if isinstance(timeout, bool) or not isinstance(timeout, int) or not low <= timeout <= high:
        problems.append(f"IllegalScenario: duel_timeout_ms must be {low}..{high} (the duel must end before the first field ends or the Boss appears)")
    supply = params.get("supply_xp")
    low, high = spec["supply_xp"]
    if not isinstance(supply, dict) or set(supply) != set(ROLES) or any(
            isinstance(value, bool) or not isinstance(value, int) or not low <= value <= high for value in supply.values()):
        problems.append(f"IllegalScenario: supply_xp names subject and opponent, each {low}..{high} XP")
    return problems


def native_input(run_id, manifest, caps, cases):
    plans = {arm["id"]: arm["plan"] for arm in manifest["design"]["arms"]}
    params = manifest["scenario"]["parameters"]
    out = []
    for case in cases:
        plan = plans[case["arm"]]
        subject_slot = case["side"]
        contenders = []
        for role, slot in (("subject", subject_slot), ("opponent", 1 - subject_slot)):
            choices, presets = _side_plan(plan, role)
            contenders.append({"role": role, "slot": slot, "supply_xp": params["supply_xp"][role], "choices": choices,
                               "presets": presets, "build": _side_build(plan, role)})
        out.append({"case_id": case["case_id"], "seed": case["seed"], "site": params["site"],
                    "duel_timeout_ms": params["duel_timeout_ms"], "wall_timeout_s": manifest["budget"]["case_wall_timeout_s"],
                    "contenders": contenders})
    return {"schema": NATIVE_INPUT_SCHEMA, "schema_version": NATIVE_INPUT_SCHEMA_VERSION, "run_id": run_id,
            "content_root": caps["content"]["digest"], "cases": out}


# ---------------------------------------------------------------- evidence

def parse_line(line):
    head, _, rest = line.partition(" ")
    fields = {}
    for key, value in FIELD.findall(rest):
        if value.startswith('"'):
            value = value[1:-1].replace('\\"', '"').replace("\\\\", "\\")
        fields.setdefault(key, value)
    return head, fields


def parse_trace(text):
    return [(tag, fields, line) for tag, fields, line in
            ((*parse_line(line), line) for line in text.splitlines() if line.startswith("LD_"))]


def _hundredths(value):
    """An LDLog::P2 decimal as exact hundredths."""
    negative = value.startswith("-")
    whole, _, fraction = value.lstrip("-").partition(".")
    amount = int(whole) * 100 + int((fraction + "00")[:2])
    return -amount if negative else amount


def _int(fields, key):
    return int(fields[key])


def _roles(result):
    return {row["contender"]: row["role"] for row in result["contenders"]}


def selection_problems(events, ids, plan, contenders, t_out):
    """Validate both sides' active offer and committed choice at every planned ordinal, and the terminal build."""
    problems = []
    roles = {contender: role for role, contender in ids.items()}
    choices = {role: _side_plan(plan, role)[0] for role in ROLES}
    selections = {role: {ordinal: {"offer": None, "choice": None} for ordinal in range(1, len(choices[role]) + 1)}
                  for role in ROLES}
    for tag, fields, _ in events:
        role = roles.get(fields.get("contender"))
        if not role or tag not in ("LD_OFFER", "LD_CHOICE"):
            continue
        raw_ordinal = fields.get("ordinal", "")
        if not raw_ordinal.isdigit() or int(raw_ordinal) < 1:
            problems.append(f"TraceIncomplete: {role} has a {tag} without a valid ordinal")
            continue
        ordinal = int(raw_ordinal)
        if ordinal not in selections[role]:
            if tag == "LD_CHOICE":
                problems.append(f"TraceIncomplete: {role} committed unplanned ordinal {ordinal}")
            continue
        item = selections[role][ordinal]
        if (not fields.get("t", "").isdigit() or int(fields["t"]) > t_out
                or not fields.get("level", "").isdigit() or int(fields["level"]) < 1):
            problems.append(f"TraceIncomplete: {role} ordinal {ordinal} has a missing or post-outcome time or invalid level")
            continue
        if tag == "LD_OFFER":
            cards = fields.get("cards", "").split(",")
            if not 1 <= len(cards) <= 3 or any(not DEFINITION.fullmatch(card) for card in cards) or len(set(cards)) != len(cards):
                problems.append(f"TraceIncomplete: {role} ordinal {ordinal} has a malformed offer")
                continue
            if item["offer"] is not None or item["choice"] is not None:
                problems.append(f"TraceIncomplete: {role} ordinal {ordinal} has duplicate or out-of-order offer evidence")
            else:
                item["offer"] = cards
                item["offer_time"] = int(fields["t"])
                item["offer_level"] = int(fields["level"])
        else:
            result, slot, stored = fields.get("result"), fields.get("slot"), fields.get("stored")
            if (fields.get("kind") != "ActiveChoice" or result not in ("learned", "blueprint") or not fields.get("pending", "").isdigit()
                    or (result == "learned" and (slot not in SLOTS or stored != "-"))
                    or (result == "blueprint" and (slot != "-" or not stored))):
                problems.append(f"TraceIncomplete: {role} ordinal {ordinal} has incomplete choice metadata")
            if item["offer"] is None or item["choice"] is not None:
                problems.append(f"TraceIncomplete: {role} ordinal {ordinal} has a duplicate choice or no offer")
            else:
                item["choice"] = fields
    for role in ROLES:
        row = contenders[role]
        held = {active["id"] for active in row["actives"]} | set(row["blueprints"])
        for ordinal, item in selections[role].items():
            offer, chosen = item["offer"], item["choice"]
            if offer is None or chosen is None:
                problems.append(f"TraceIncomplete: {role} ordinal {ordinal} lacks an offer or committed choice")
                continue
            if (item["offer_time"] > int(chosen["t"]) or item["offer_level"] > int(chosen["level"])
                    or int(chosen["level"]) > row["level"]):
                problems.append(f"TraceIncomplete: {role} ordinal {ordinal} has inconsistent offer or choice times or levels")
            card = chosen.get("card")
            if card not in offer:
                problems.append(f"TraceIncomplete: {role} ordinal {ordinal} choice is not in its offer")
                continue
            if card not in held:
                problems.append(f"TraceIncomplete: {role} ordinal {ordinal} choice {card} is neither learned nor a blueprint at the end")
            rule = choices[role][ordinal - 1]
            if rule["rule"] == "option_index" and (rule["index"] >= len(offer) or offer[rule["index"]] != card):
                problems.append(f"TraceIncomplete: {role} ordinal {ordinal} did not commit the planned option index")
            if rule["rule"] == "definition" and rule["definition"] != card:
                problems.append(f"TraceIncomplete: {role} ordinal {ordinal} did not commit the planned definition")
    return problems


COMPONENT_ROW = {"id": str, "installed": bool, "ever_installed": bool, "source": str, "source_level": int, "target_kind": str,
                 "target_id": str, "element": str, "bound_active": str, "bound_component": str}


def _component_row(row):
    return (isinstance(row, dict) and set(COMPONENT_ROW) <= row.keys()
            and all(isinstance(row[key], kind) and (kind is bool or not isinstance(row[key], bool)) for key, kind in COMPONENT_ROW.items()))


def expected_build(step, caps):
    """The LD_BUILD fields the authority logs for a committed planned step (DevelopmentBindingText splits into bind, on and
    base); None marks a field the line does not carry."""
    kind = step["step"]
    order = caps["scenarios"]["duel_site"]["parameters"]["stat_design_order"]
    out = {"kind": STEPS[kind][1], "card": step.get("component") or step.get("blueprint") or "-",
           "slot": step["slot"] if kind in ("learn", "replace") else "-",
           "points": ",".join(str(step["points"].get(stat, 0)) for stat in order) if kind == "allocate_points" else "-",
           "installed": "1" if kind in ("opening_base", "install") else "0" if kind == "uninstall" else "-",
           "bind": "-", "on": None, "base": None, "install_stored": "-"}
    if kind in ("opening_base", "install"):
        bind, target = step.get("bind"), step.get("target")
        if bind:
            out["bind"] = f"{bind['active']}:{ELEMENTS[bind['element']]}"
        if target and TARGET_KINDS[target["kind"]]:
            out["on"] = f"{TARGET_KINDS[target['kind']]}:{target.get('id', 'None')}"
        out["base"] = step.get("base")
    return out


def planned_components(steps):
    """The terminal placement of every component the plan touched: installed with its target, or back in the inventory."""
    out = {}
    for step in steps:
        component = step.get("component")
        if step["step"] in ("opening_base", "install"):
            bind, target = step.get("bind") or {}, step.get("target") or {}
            out[component] = {"installed": True, "target_kind": TARGET_KINDS.get(target.get("kind")) or "None",
                              "target_id": target.get("id", ""), "element": ELEMENTS.get(bind.get("element"), "None"),
                              "bound_active": bind.get("active", ""), "bound_component": step.get("base") or ""}
        elif step["step"] == "uninstall" and component in out:
            removing = [component]
            for name in removing:
                out[name]["installed"] = False
                removing += [other for other, state in out.items() if state["installed"] and other not in removing and
                             ((state["target_kind"] == "Component" and state["target_id"] == name) or state["bound_component"] == name)]
    return out


def build_problems(events, ids, plan, contenders, t_out, caps):
    """A planned build owns every build command of its side: one LD_BUILD per planned step, in order, in its window, before
    the outcome, carrying exactly the planned kind, subject, slot, points and placement; the terminal components agree."""
    problems = []
    enters = [fields for tag, fields, _ in events if tag == "LD_FIELD_ENTER" and fields.get("segment") == "0"]
    field_at = int(enters[0]["at_ms"]) if len(enters) == 1 and enters[0].get("at_ms", "").isdigit() else None
    for role in ROLES:
        steps = _side_build(plan, role)
        row = contenders[role]
        if row["planned_steps"] != len(steps):
            problems.append(f"TraceIncomplete: the native {role} plan has {row['planned_steps']} build steps, planned {len(steps)}")
        if not steps:
            continue
        if row["steps_committed"] != len(steps):
            problems.append(f"PlanNotReached: the {role} committed {row['steps_committed']} of {len(steps)} planned build steps")
        if field_at is None:
            problems.append("TraceIncomplete: no single LD_FIELD_ENTER of the first field places the planned build windows")
        own = [(tag, fields) for tag, fields, _ in events if fields.get("contender") == ids[role]]
        builds = [fields for tag, fields in own if tag == "LD_BUILD"]
        if len(builds) != len(steps):
            problems.append(f"TraceIncomplete: the {role} has {len(builds)} LD_BUILD records for {len(steps)} planned build steps "
                            "(a planned build sends nothing else)")
        supplied = [int(fields["t"]) for tag, fields in own if tag == "LD_REWARD" and fields.get("cause") == "dev_fixture"
                    and fields.get("t", "").isdigit()]
        chosen = [int(fields["t"]) for tag, fields in own if tag == "LD_CHOICE" and fields.get("t", "").isdigit()]
        field_from = max([field_at or 0] + supplied + chosen)
        previous = -1
        for index, (step, fields) in enumerate(zip(steps, builds)):
            label = f"the {role} build step {index + 1} ({step['step']})"
            t = fields.get("t", "")
            if not t.isdigit() or int(t) < previous or int(t) > t_out:
                problems.append(f"TraceIncomplete: {label} has no authority time in order before the outcome")
                continue
            previous = int(t)
            if field_at is not None and step["at"] == "opening_rest" and previous >= field_at:
                problems.append(f"PlanNotReached: {label} committed after the opening rest")
            if field_at is not None and step["at"] == "field" and previous < field_from:
                problems.append(f"PlanNotReached: {label} committed before the field supply and the planned choices")
            for key, value in expected_build(step, caps).items():
                if fields.get(key) != value:
                    problems.append(f"PlanNotReached: {label} committed {key}={fields.get(key)}, planned {value}")
            if step["step"] == "replace" and fields.get("forgot") in (None, "", "-"):
                problems.append(f"TraceIncomplete: {label} forgot no active")
        terminal = {component["id"]: component for component in row["components"]}
        planned = planned_components(steps)
        for component, state in planned.items():
            held = terminal.get(component)
            if not held:
                problems.append(f"PlanNotReached: the {role} does not own the planned {component} at the end")
            elif not state["installed"] and held["installed"]:
                problems.append(f"PlanNotReached: the {role} {component} is installed at the end, planned back in the inventory")
            elif state["installed"] and any(held[key] != value for key, value in state.items()):
                problems.append(f"PlanNotReached: the {role} {component} ends as "
                                f"{ {key: held[key] for key in state} }, planned {state}")
        for component, held in terminal.items():
            if held["installed"] and not planned.get(component, {}).get("installed"):
                problems.append(f"PlanNotReached: the {role} {component} is installed without a planned step")
    return problems


def case_evidence(case, manifest, caps, result, trace_text):
    out = {"status": "Invalid", "reason": "", "metrics": {}, "facts": {}, "violations": []}
    if result.get("schema") != NATIVE_RESULT_SCHEMA or result.get("schema_version") != NATIVE_RESULT_SCHEMA_VERSION:
        out["reason"] = "the native result has another schema"
        return out
    if result.get("case_id") != case["case_id"] or result.get("seed") != case["seed"]:
        out["reason"] = "the native result belongs to another Case or seed"
        return out
    if result.get("status") != "Completed":
        out.update(status=result.get("status", "Faulted"), reason=result.get("reason", ""))
        # The native driver ends a Case Invalid the moment a side's plan cannot run as written (an unoffered active, a
        # component the seeded supply did not grant, a step growth refused): the plan was not reached.
        rows = result.get("contenders")
        failures = [f"the {row.get('role')} {row['profile_failure']}" for row in rows if isinstance(row, dict)
                    and isinstance(row.get("profile_failure"), str) and row["profile_failure"]] if isinstance(rows, list) else []
        if out["status"] == "Invalid" and failures:
            out["reason"] = "PlanNotReached: " + "; ".join(failures)
        return out
    content_root = result.get("content_root")
    if not isinstance(content_root, str) or content_root.lower() != caps["content"]["digest"]:
        out["reason"] = "the native result belongs to another content root"
        return out
    rows = result.get("contenders")
    required = {"role", "slot", "contender", "progression_valid", "profile_bound", "choices", "planned_choices",
                "supply_xp", "supply_status", "presets_queued", "presets_requested", "level", "unspent_points",
                "stat_points", "actives", "blueprints", "has_body", "hp_p2", "max_hp_p2", "mp_p2", "max_mp_p2",
                "planned_steps", "steps_committed", "components"}
    integer_fields = ("slot", "level", "unspent_points", "choices", "planned_choices", "supply_xp",
                      "presets_queued", "presets_requested", "hp_p2", "max_hp_p2", "mp_p2", "max_mp_p2",
                      "planned_steps", "steps_committed")
    if (not isinstance(rows, list) or len(rows) != len(ROLES) or
            any(not isinstance(row, dict) or not required <= row.keys() for row in rows) or
            any(row["role"] not in ROLES for row in rows) or
            {row["role"] for row in rows} != set(ROLES) or
            any(not isinstance(row["contender"], str) or not GUID.fullmatch(row["contender"]) for row in rows) or
            len({row["contender"] for row in rows}) != len(ROLES) or
            any(isinstance(row[key], bool) or not isinstance(row[key], int) or row[key] < 0
                for row in rows for key in integer_fields) or
            any(not isinstance(row[key], bool) for row in rows for key in ("progression_valid", "profile_bound", "has_body")) or
            any(not isinstance(row["supply_status"], str) for row in rows) or
            any(not isinstance(row[key], list) for row in rows for key in ("stat_points", "actives", "blueprints")) or
            any(not isinstance(stat, dict) or not {"id", "points"} <= stat.keys() or not isinstance(stat["id"], str)
                or isinstance(stat["points"], bool) or not isinstance(stat["points"], int) or stat["points"] < 0
                for row in rows for stat in row["stat_points"]) or
            any(not isinstance(active, dict) or not {"id", "slot"} <= active.keys()
                or not isinstance(active["id"], str) or active["slot"] not in SLOTS
                for row in rows for active in row["actives"]) or
            any(not isinstance(blueprint, str) for row in rows for blueprint in row["blueprints"]) or
            any(not isinstance(row["components"], list) for row in rows) or
            any(not _component_row(component) for row in rows for component in row["components"])):
        out["reason"] = "TraceIncomplete: native contender identities or terminal progression are missing"
        return out
    if (not isinstance(result.get("outcome"), dict) or not {"kind", "victim", "killer", "t_ms"} <= result["outcome"].keys() or
            any(isinstance(result.get(key), bool) or not isinstance(result.get(key), int)
                for key in ("generation", "started_at_ms", "ended_at_ms")) or
            any(not isinstance(result["outcome"][key], str) for key in ("kind", "victim", "killer")) or
            isinstance(result["outcome"]["t_ms"], bool) or not isinstance(result["outcome"]["t_ms"], int) or
            result["generation"] <= 0 or result["started_at_ms"] < 0 or result["ended_at_ms"] < result["started_at_ms"]):
        out["reason"] = "TraceIncomplete: native outcome or authority times are missing"
        return out
    if "side" in case and any(row["slot"] != (case["side"] if row["role"] == "subject" else 1 - case["side"]) for row in rows):
        out["reason"] = "TraceIncomplete: the native contender slots do not match the planned side"
        return out
    if not trace_text.strip() or not parse_trace(trace_text):
        out["reason"] = "TraceIncomplete: the native trace is missing or empty"
        return out
    roles = _roles(result)
    ids = {role: contender for contender, role in roles.items()}
    events = parse_trace(trace_text)
    for tag, fields, _ in events:
        if tag in ("LD_HIT", "LD_REWARD", "LD_DEATH_RECEIPT") and not fields.get("t", "").isdigit():
            out["reason"] = f"TraceIncomplete: {tag} has no valid authority time"
            return out
        if tag == "LD_HIT" and any(not fields.get(key) for key in ("src", "victim")):
            out["reason"] = "TraceIncomplete: LD_HIT has no source or victim"
            return out
        if tag == "LD_REWARD" and fields.get("contender") in ids.values() and not fields.get("xp", "").isdigit():
            out["reason"] = "TraceIncomplete: LD_REWARD has no valid XP"
            return out
    out["facts"] = facts(case, manifest, result, events, ids)
    params = manifest["scenario"]["parameters"]
    t0 = int(result["started_at_ms"])
    outcome = result["outcome"]
    duelists = set(ids.values())
    reasons = []
    if result.get("trace_lines") != len(trace_text.splitlines()):
        reasons.append("TraceIncomplete: the captured trace line count does not match the result")
    begins = [fields for tag, fields, _ in events if tag == "LD_EVAL_CASE_BEGIN"]
    starts = [fields for tag, fields, _ in events if tag == "LD_MATCH_STARTED"]
    if (len(begins) != 1 or begins[0].get("case") != case["case_id"] or begins[0].get("seed") != case["seed"]
            or any(begins[0].get(role) != ids[role] for role in ROLES)
            or len(starts) != 1 or starts[0].get("generation") != str(result["generation"])
            or not starts[0].get("started_at_ms", "").isdigit() or int(starts[0]["started_at_ms"]) > t0):
        reasons.append("TraceIncomplete: the Case identity or match-start record is missing or inconsistent")
    fields = [fields for tag, fields, _ in events if tag == "LD_FIELD_ENTER"]
    if (len(fields) != 1 or fields[0].get("generation") != str(result["generation"])
            or fields[0].get("segment") != "0" or fields[0].get("at_ms") != str(t0)
            or fields[0].get("field_t_ms") != "0" or fields[0].get("final") != "0"):
        reasons.append("TraceIncomplete: the first field entry is missing or disagrees with the duel start")
    if result["ended_at_ms"] >= t0 + caps["scenarios"]["duel_site"]["parameters"]["first_field_ms"]:
        reasons.append("TraceIncomplete: the Case crossed the first field boundary")
    if outcome["kind"] == "kill":
        victim = ids.get(outcome["victim"])
        deaths = [fields for tag, fields, _ in events if tag == "LD_DEATH_RECEIPT" and fields.get("contender") == victim]
        lethal = [fields for tag, fields, _ in events if tag == "LD_HIT" and fields.get("victim") == victim and fields.get("lethal") == "1"]
        if not deaths or not lethal:
            reasons.append("TraceIncomplete: the death has no lethal LD_HIT or LD_DEATH_RECEIPT")
            t_out = int(result["ended_at_ms"])
        else:
            t_out = _int(deaths[0], "t")
            if outcome["t_ms"] != t_out - t0 or not t0 <= t_out <= min(result["ended_at_ms"], t0 + int(params["duel_timeout_ms"])):
                reasons.append("TraceIncomplete: kill time disagrees with the receipt or is after the duel horizon")
        if outcome["killer"] not in ROLES or outcome["killer"] == outcome["victim"]:
            reasons.append(f"ExternalKill: the {outcome['victim']} was killed by {outcome['killer'] or 'no contender'}")
    elif outcome["kind"] == "timeout":
        t_out = t0 + int(params["duel_timeout_ms"])
        if result["ended_at_ms"] < t_out:
            reasons.append("TraceIncomplete: the Case ended before the duel horizon")
        if outcome["t_ms"] != int(params["duel_timeout_ms"]):
            reasons.append("TraceIncomplete: the timeout disagrees with the duel horizon")
    else:
        reasons.append(f"TraceIncomplete: outcome {outcome['kind']}")
        t_out = int(result["ended_at_ms"])
    for row in result["contenders"]:
        if not row["progression_valid"] or not row["profile_bound"]:
            reasons.append(f"Setup: the {row['role']} had no seeded identity or evaluation profile")
        if row["choices"] != row["planned_choices"]:
            reasons.append(f"PlanNotReached: the {row['role']} consumed {row['choices']} of {row['planned_choices']} planned choices")
        if row["supply_xp"] > 0 and row["supply_status"] != "Granted":
            reasons.append(f"SupplyMissing: the {row['role']} supply is {row['supply_status']}")
        if row["supply_xp"] != params["supply_xp"][row["role"]]:
            reasons.append(f"SupplyMismatch: the {row['role']} native supply differs from the plan")
        if row["presets_queued"] != row["presets_requested"]:
            reasons.append(f"PresetNotApplied: the {row['role']} queued {row['presets_queued']} of {row['presets_requested']} presets")
        planned = _side_plan(next(arm["plan"] for arm in manifest["design"]["arms"] if arm["id"] == case["arm"]), row["role"])[0]
        if row["planned_choices"] != len(planned):
            reasons.append(f"PlanNotReached: the {row['role']} result does not match the planned choice count")
    supplied = {role: 0 for role in ROLES}
    for tag, fields, _ in events:
        t = int(fields["t"]) if fields.get("t", "").lstrip("-").isdigit() else None
        if tag == "LD_REWARD" and fields.get("contender") in duelists:
            role = roles[fields["contender"]]
            if fields.get("cause") == "dev_fixture":
                supplied[role] += int(fields["xp"])
            elif t is None or t < t_out or (t == t_out and not _is_outcome_kill_reward(fields, outcome, ids, t_out)):
                reasons.append(f"UncontrolledXP: the {role} gained {fields.get('xp')} XP from {fields.get('cause')} "
                               f"at or before the outcome")
        if tag == "LD_HIT" and t is not None and t <= t_out:
            source_in, victim_in = fields.get("src") in duelists, fields.get("victim") in duelists
            if source_in != victim_in:
                reasons.append(f"ExternalInteraction: a hit between a duelist and {fields.get('src_monster') if not source_in else fields.get('victim_monster')}"
                               f" at t={t}")
            out["violations"] += hit_violations(fields)
    for role in ROLES:
        if supplied[role] != params["supply_xp"][role]:
            reasons.append(f"SupplyMismatch: the {role} received {supplied[role]} dev_fixture XP, planned {params['supply_xp'][role]}")
    plans = {arm["id"]: arm["plan"] for arm in manifest["design"]["arms"]}
    reasons += selection_problems(events, ids, plans[case["arm"]], {row["role"]: row for row in rows}, t_out)
    reasons += build_problems(events, ids, plans[case["arm"]], {row["role"]: row for row in rows}, t_out, caps)
    for role in ROLES:
        _, presets = _side_plan(plans[case["arm"]], role)
        offered = out["facts"]["offers"][role]
        for position, preset in enumerate(presets):
            if offered.get(str(position + 2)) != preset:
                reasons.append(f"PresetNotApplied: the {role} offer at ordinal {position + 2} is {offered.get(str(position + 2))}, prepared {preset}")
    if out["violations"]:
        reasons.append("EvidenceViolation: a committed hit contradicts its recorded damage")
    if reasons:
        out.update(status="Invalid", reason="; ".join(dict.fromkeys(reasons)))
        return out
    subject, opponent = ids["subject"], ids["opponent"]
    dealt = taken = 0
    for tag, fields, _ in events:
        if tag != "LD_HIT" or not t0 <= int(fields["t"]) <= t_out:
            continue
        if fields["src"] == subject and fields["victim"] == opponent:
            dealt += _hundredths(fields["hp_lost"])
        elif fields["src"] == opponent and fields["victim"] == subject:
            taken += _hundredths(fields["hp_lost"])
    me = next(row for row in result["contenders"] if row["role"] == "subject")
    score = 0 if outcome["kind"] == "timeout" else (1 if outcome["victim"] == "opponent" else -1)
    out.update(status="Completed", reason="", metrics={
        "duel_score": {"value": score, "censored": outcome["kind"] == "timeout"},
        "damage_dealt_hp": {"value": dealt / 100, "censored": False},
        "damage_taken_hp": {"value": taken / 100, "censored": False},
        "subject_hp_end_bp": {"value": (me["hp_p2"] * 10000 // me["max_hp_p2"]) if me["has_body"] and me["max_hp_p2"] else 0,
                              "censored": False},
        "decision_time_ms": {"value": t_out - t0, "censored": outcome["kind"] == "timeout"},
    })
    return out


def _is_outcome_kill_reward(fields, outcome, ids, t_out):
    """The killer's own reward for the deciding kill, committed at the outcome boundary."""
    return (outcome["kind"] == "kill" and outcome["killer"] in ids and outcome["victim"] in ids
            and fields.get("t") == str(t_out) and fields.get("cause") == "death" and fields.get("role") == "killer"
            and fields.get("contender") == ids[outcome["killer"]] and fields.get("victim") == ids[outcome["victim"]])


def hit_violations(fields):
    """Evidence-contract checks of one LD_HIT: the HP it removed is what the line says it removed."""
    before, _, after = fields.get("hp", "").partition("->")
    try:
        lost, dealt, absorbed = (_hundredths(fields[key]) for key in ("hp_lost", "dmg", "absorbed"))
        drop = _hundredths(before) - _hundredths(after)
    except (KeyError, ValueError):
        return [f"LD_HIT without parsable damage fields at t={fields.get('t')}"]
    out = []
    if lost != drop:
        out.append(f"LD_HIT t={fields.get('t')} hp_lost {lost / 100} != hp {before}->{after}")
    if lost + absorbed > dealt:
        out.append(f"LD_HIT t={fields.get('t')} hp_lost + absorbed exceeds dmg")
    if (fields.get("lethal") == "1") != (_hundredths(after) == 0):
        out.append(f"LD_HIT t={fields.get('t')} lethal flag disagrees with the remaining HP")
    return out


def branch_ordinal(design):
    """The first ordinal where the normalized subject rules differ across the design's arms."""
    choices = [_side_plan(arm["plan"], "subject")[0] for arm in design["arms"]]
    for index in range(max((len(rules) for rules in choices), default=0)):
        rules = [row[index] if index < len(row) else None for row in choices]
        if any(rule != rules[0] for rule in rules[1:]):
            return index + 1
    return None


def decision_sequence(plan):
    """The subject's planned decisions in the order the native policy takes them: the opening choice, the opening-rest build
    steps, the later choices, then the field build steps."""
    choices = _side_plan(plan, "subject")[0]
    steps = _side_build(plan, "subject")
    return ([("choice", 1, choices[0])] if choices else []) + \
        [("build", index, step) for index, step in enumerate(steps) if step["at"] == "opening_rest"] + \
        [("choice", ordinal, rule) for ordinal, rule in enumerate(choices, 1) if ordinal > 1] + \
        [("build", index, step) for index, step in enumerate(steps) if step["at"] == "field"]


def branch_point(design):
    """The first position of the subject's decision sequence where the design's arms differ, or None."""
    sequences = [decision_sequence(arm["plan"]) for arm in design["arms"]]
    for position in range(max((len(row) for row in sequences), default=0)):
        items = [row[position] if position < len(row) else None for row in sequences]
        if any(item != items[0] for item in items[1:]):
            return position
    return None


BUILD_FIELDS = ("t", "kind", "card", "slot", "points", "unspent", "level", "installed", "bind", "on", "base", "install_stored",
                "forgot", "refunded", "paid", "reclaimed")


def _build_label(record):
    """A short report label of one committed build step."""
    if not record:
        return ""
    parts = [record.get("kind") or "", record.get("card") if record.get("card") not in (None, "-") else ""]
    parts += [f"{key}={record[key]}" for key in ("slot", "points", "bind", "on", "base") if record.get(key) not in (None, "-")]
    return " ".join(part for part in parts if part)


def facts(case, manifest, result, events, ids):
    offers = {role: {} for role in ROLES}
    chosen = {role: {} for role in ROLES}
    offer_details = {role: {} for role in ROLES}
    choice_details = {role: {} for role in ROLES}
    build_steps = {role: [] for role in ROLES}
    grants = {role: [] for role in ROLES}
    services = {role: [] for role in ROLES}
    roles = {contender: role for role, contender in ids.items()}
    spawn = {}
    for tag, fields, _ in events:
        role = roles.get(fields.get("contender"))
        if tag == "LD_HEARTBEAT" and role and role not in spawn:
            spawn[role] = fields.get("pos")
        if not role:
            continue
        if tag == "LD_OFFER" and fields.get("ordinal", "").isdigit():
            cards = [card for card in fields.get("cards", "").split(",") if card]
            offers[role].setdefault(fields["ordinal"], cards)
            offer_details[role].setdefault(fields["ordinal"], {"t": fields.get("t"), "level": fields.get("level"), "cards": cards})
        elif tag == "LD_CHOICE" and fields.get("ordinal", "").isdigit() and fields.get("card"):
            # The label names where the active went: a Q/W/E slot, or a blueprint kept for later.
            where = fields.get("slot") if fields.get("result") == "learned" else "blueprint"
            chosen[role][fields["ordinal"]] = f"{fields['card']}→{where}"
            choice_details[role][fields["ordinal"]] = {
                key: fields.get(key) for key in ("t", "kind", "card", "result", "slot", "stored", "level", "pending")}
        elif tag == "LD_BUILD":
            build_steps[role].append({key: fields.get(key) for key in BUILD_FIELDS})
        elif tag == "LD_LEVEL_UP":
            grants[role].append({key: fields.get(key) for key in ("t", "level", "components", "empty_pool_grants")})
        elif tag == "LD_MATERIAL_SERVICE":
            services[role].append({key: fields.get(key) for key in ("t", "kind", "received", "gap", "paid", "removed", "uninstalled")})
    label, prefix = _branch_facts(case, manifest, offer_details, choice_details, chosen, build_steps, grants)
    fingerprint = hashlib.sha256(json.dumps([offers["subject"].get("1"), offers["opponent"].get("1"), spawn],
                                            sort_keys=True).encode("utf-8")).hexdigest()[:16]
    start = int(result["started_at_ms"])
    horizon = min(int(result["ended_at_ms"]), start + int(manifest["scenario"]["parameters"]["duel_timeout_ms"]))
    first_hit = next(({"role": roles[fields["src"]], "t_ms": int(fields["t"]) - start}
                      for tag, fields, _ in events if tag == "LD_HIT" and fields.get("src") in roles
                      and fields.get("victim") in roles and fields.get("t", "").isdigit()
                      and start <= int(fields["t"]) <= horizon and fields.get("hp_lost", "0.00") != "0.00"), None)
    builds = {row["role"]: {"level": row["level"], "unspent_points": row["unspent_points"],
                            "actives": [f"{active['id']}/{active['slot']}" for active in row["actives"]],
                            "blueprints": list(row["blueprints"]),
                            "stat_points": {stat["id"]: stat["points"] for stat in row["stat_points"]},
                            "components": [dict(component) for component in row.get("components", [])]}
              for row in result["contenders"]}
    return {"offers": offers, "choices": chosen, "build_steps": build_steps, "grants": grants, "material_services": services,
            "builds": builds, "label": label, "prefix": prefix, "random_fingerprint": fingerprint, "first_hit": first_hit,
            "spawn": spawn, "outcome": result.get("outcome")}


def _branch_facts(case, manifest, offer_details, choice_details, chosen, build_steps, grants):
    """The branch label and the comparable prefix: every subject decision before the branch point (offers and choices,
    committed build steps and level-up component grants) and, at a choice branch, the offer the branches decide on."""
    design = manifest["design"]
    point = branch_point(design)
    if point is None:
        return "", None
    common = decision_sequence(design["arms"][0]["plan"])[:point]
    items = [row[point] if point < len(row) else None for row in (decision_sequence(arm["plan"]) for arm in design["arms"])]
    history = [{"ordinal": ordinal, "offer": offer_details["subject"].get(str(ordinal)),
                "choice": choice_details["subject"].get(str(ordinal))} for kind, ordinal, _ in common if kind == "choice"]

    def at(value):
        return int(value) if isinstance(value, str) and value.isdigit() else None

    ordinals = {item[1] for item in items if item and item[0] == "choice"}
    if all(item and item[0] == "choice" for item in items) and len(ordinals) == 1:
        ordinal = ordinals.pop()
        offer = offer_details["subject"].get(str(ordinal))
        limit = at((offer or {}).get("t"))
        branch = {"ordinal": ordinal, "offer": offer}
        before = [record for record in build_steps["subject"] if limit is not None and at(record["t"]) is not None
                  and at(record["t"]) < limit]
        label = chosen["subject"].get(str(ordinal), "")
    else:
        # A build branch: everything up to the commit of the decision just before it.
        kind, index, _ = common[-1] if common else ("choice", 0, None)
        record = (choice_details["subject"].get(str(index)) if kind == "choice"
                  else build_steps["subject"][index] if index < len(build_steps["subject"]) else None)
        limit = at((record or {}).get("t"))
        branch = {"position": point}
        before = [record for record in build_steps["subject"] if limit is not None and at(record["t"]) is not None
                  and at(record["t"]) <= limit]
        mine = next((row[point] for arm in design["arms"] if arm["id"] == case.get("arm")
                     for row in [decision_sequence(arm["plan"])] if point < len(row)), None)
        label = (chosen["subject"].get(str(mine[1]), "") if mine and mine[0] == "choice"
                 else _build_label(build_steps["subject"][mine[1]]) if mine and mine[1] < len(build_steps["subject"]) else "")
    granted = [row for row in grants["subject"] if limit is not None and at(row["t"]) is not None and at(row["t"]) <= limit]
    prefix = json.dumps({"history": history, "builds": before, "grants": granted, "branch": branch},
                        sort_keys=True, ensure_ascii=False, separators=(",", ":"))
    return label, prefix


def canonical_events(trace_text, result):
    """Discrete authority events up to the Case's end with identities as roles; spatial heartbeat samples apart."""
    names = {contender: role for contender, role in _roles(result).items()}
    others = {}

    def role_of(match):
        value = match.group(0)
        if value in names:
            return names[value]
        others.setdefault(value, f"other{len(others) + 1}")
        return others[value]

    end = int(result.get("ended_at_ms", -1))
    events, spatial = [], {}
    for tag, fields, line in parse_trace(trace_text):
        t = int(fields["t"]) if fields.get("t", "").lstrip("-").isdigit() else None
        if t is not None and end >= 0 and t > end:
            continue
        if tag in CANONICAL_TAGS:
            events.append(GUID.sub(role_of, line))
        elif tag == "LD_HEARTBEAT" and fields.get("pos", "-") != "-":
            key = f"{GUID.sub(role_of, fields['contender'])}@{t}"
            spatial[key] = tuple(float(part) for part in fields["pos"].split(","))
    return events, spatial


def replay_evidence(case, manifest, caps, native_run, runner, result, trace_text):
    """Canonical identity, terminal build/resources and heartbeat resources for a valid replay comparison."""
    problems = identity_problems(native_run, caps)
    run_id = manifest["resolved"]["run_id"]
    if native_run.get("run_id") != run_id or result.get("run_id") != run_id:
        problems.append("the native run or result does not match the planned run identity")
    if not native_run.get("passed"):
        problems.append("the native run did not finish successfully")
    if result.get("status") != "Completed" or result.get("case_id") != case["case_id"] or result.get("seed") != case["seed"]:
        problems.append("the native result is not the completed planned Case and seed")
    run_fields = ("content_root", "product_version", "engine_version", "source_commit", "source_dirty",
                  "fixed_delta_seconds", "seed_derivation")
    identity = {key: native_run.get(key) for key in run_fields}
    if any(identity[key] in (None, "", "-") for key in run_fields) or identity["source_commit"] == "unknown":
        problems.append("the native run is missing build, engine, content or timing identity")
    if isinstance(identity["content_root"], str):
        identity["content_root"] = identity["content_root"].lower()
    if (isinstance(identity["fixed_delta_seconds"], bool) or not isinstance(identity["fixed_delta_seconds"], (int, float))
            or not math.isfinite(identity["fixed_delta_seconds"]) or identity["fixed_delta_seconds"] <= 0):
        problems.append("the native fixed time step is missing or invalid")
    binary = runner.get("binary_digests")
    after = runner.get("binary_digests_after")
    keys = ("editor_sha256", "module_sha256")
    if (not isinstance(binary, dict) or not isinstance(after, dict)
            or any(not isinstance(binary.get(key), str) or not re.fullmatch(r"[0-9a-f]{64}", binary[key]) for key in keys)
            or binary != after):
        problems.append("the editor or game-module binary identity is missing or changed during the run")
    if runner.get("timed_out") or runner.get("exit_code") != 0:
        problems.append("the runner did not exit successfully")
    identity["binary_digests"] = binary
    terminal_fields = ("status", "case_id", "seed", "content_root", "started_at_ms", "ended_at_ms", "outcome")
    contender_fields = ("role", "slot", "contender", "progression_valid", "level", "xp", "choices", "planned_choices",
                        "unspent_points", "stat_points", "actives", "blueprints", "has_body", "hp_p2", "max_hp_p2",
                        "mp_p2", "max_mp_p2", "profile_bound", "profile_phase", "profile_failure", "supply_xp",
                        "supply_status", "presets_requested", "presets_queued", "planned_steps", "steps_committed", "components")
    terminal = {key: result.get(key) for key in terminal_fields}
    rows = result.get("contenders")
    if any(key not in result for key in terminal_fields) or not isinstance(rows, list) or len(rows) != len(ROLES):
        problems.append("the terminal Case identity, outcome or contenders are incomplete")
        rows = []
    terminal["contenders"] = {}
    roles = {}
    for row in rows:
        if not isinstance(row, dict) or any(key not in row for key in contender_fields) or row.get("role") not in ROLES:
            problems.append("a terminal contender build or resource field is missing")
            continue
        role = row["role"]
        if role in terminal["contenders"] or not isinstance(row["contender"], str):
            problems.append("the terminal contender identities are ambiguous")
            continue
        terminal["contenders"][role] = {key: row[key] for key in contender_fields}
        roles[row["contender"]] = role
    if set(terminal["contenders"]) != set(ROLES) or len(roles) != len(ROLES):
        problems.append("the terminal state does not identify both duelists")
    resources, positions, seen = [], set(), set()
    end = result.get("ended_at_ms", -1)
    for tag, fields, _ in parse_trace(trace_text):
        if tag != "LD_HEARTBEAT":
            continue
        role = roles.get(fields.get("contender"))
        if not fields.get("t", "").isdigit():
            problems.append("a heartbeat lacks an authority time")
            continue
        t = int(fields["t"])
        if isinstance(end, int) and t > end:
            continue
        if not fields.get("contender"):
            problems.append(f"a heartbeat at {t} lacks an identity")
            continue
        position = fields.get("pos", "-")
        if position != "-":
            try:
                coordinates = tuple(float(part) for part in position.split(","))
            except ValueError:
                coordinates = ()
            if len(coordinates) != 3 or not all(math.isfinite(value) for value in coordinates):
                problems.append(f"a heartbeat at {t} has a malformed position")
            elif role:
                positions.add(role)
        if not role:
            continue
        required = ("phase", "life", "pos", "hp", "mp", "shield")
        if any(key not in fields for key in required):
            problems.append(f"the {role} heartbeat at {t} lacks resource or position evidence")
            continue
        if (role, t) in seen:
            problems.append(f"the {role} heartbeat at {t} is duplicated")
            continue
        seen.add((role, t))
        for key in ("hp", "mp"):
            pieces = fields[key].split("/")
            if fields[key] != "-" and (len(pieces) != 2 or any(not P2.fullmatch(piece) for piece in pieces)):
                problems.append(f"the {role} heartbeat at {t} has malformed {key}")
        if fields["shield"] != "-" and not P2.fullmatch(fields["shield"]):
            problems.append(f"the {role} heartbeat at {t} has malformed shield")
        resources.append({"role": role, "t_ms": t, **{key: fields[key] for key in ("phase", "life", "hp", "mp", "shield")}})
    if {role for role, _ in seen} != set(ROLES) or positions != set(ROLES):
        problems.append("both duelists need heartbeat resource and position evidence")
    return {"identity": identity, "terminal": terminal, "resources": resources, "problems": problems}


def identity_problems(native_run, caps):
    problems = []
    if native_run.get("schema") != NATIVE_RUN_SCHEMA or native_run.get("schema_version") != NATIVE_RESULT_SCHEMA_VERSION:
        problems.append("the native run record has another schema")
    root = native_run.get("content_root")
    if not isinstance(root, str) or root.lower() != caps["content"]["digest"]:
        problems.append(f"the native run loaded catalog {native_run.get('content_root')}, planned {caps['content']['digest']}")
    if not native_run.get("fixed_time_step"):
        problems.append("the native run did not use a fixed time step; its Cases are not replayable")
    if native_run.get("seed_derivation") != SEED_DERIVATION:
        problems.append(f"the native seed derivation {native_run.get('seed_derivation')} is not {SEED_DERIVATION}")
    return problems


def limitations(manifest, caps):
    content = caps["content"]
    out = [
        f"内容为当前运行目录 {content['version']}（{content['abilities']} 个主动技能、{content['stats']} 项属性、"
        f"{content.get('components', 0)} 件构件）；目录外的机制一律 UnsupportedMechanic，不代表 v0.11 候选内容已被评估。",
        "计划规定每次主动机会选哪个主动（并总是请求立即学会）。没有构筑步骤的一方，学习图纸、加点、开局基础构件与启用构件用生产 AI 规则；"
        "有构筑步骤的一方由计划拥有全部构筑命令（主动选择除外），只按顺序发送计划的开局基础构件（含 B18 主动与元素）、"
        "逐属性加点、指定对象安装、卸下、洗点、学习与替换，生产规则不再为它构筑；它必须花完供给赚到的全部点数。",
        "构件只来自开局基础构件与种子决定的升级普通池抽取：计划要用的构件在该 Case 没抽到、指定主动没出现或 growth 拒绝某一步时，"
        "该 Case 判 Invalid（PlanNotReached），不强塞也不改用别的构件。休息窗口只有供给之前的开局休息，所以野外搬动、卸下、替换与洗点"
        "以及学习图纸在 duel_site 中是 IllegalDecision。",
        "NPC 购买、单独遗忘、FillGap、Dismantle、精英营地构件选择与选择时直接替换不可规划（UnsupportedDecision，附原因）。",
        "对决双方使用同一个固定的 DEV 评估画像：按计划选择与构筑、走到公开地点、只攻击指定对手、不撤退；交战微操是生产 AI 的 Engage。"
        "结果反映固定策略下的机制差异，不代表玩家适应或六类策略画像。",
        "成长由 T=0 的经验供给推动（cause=dev_fixture，经正常奖励复合事务），是计划假设，不证明真实对局中能在同样时间拿到这些经验。",
        "两名 AI 在真实 L_Demo 中移动、寻路与交战（NativeSpatial）；房主是唯一真人，只编排、不参战、不计分；没有拥有连接、网络或渲染证据。",
        "营地、Boss、空投与房主的介入被判为 Invalid 并单列，不计入结果；对决时限须早于第一野外段结束和 Boss 出现。",
        "duel_score 为 0 表示到时未分胜负（删失），不是输；\"谁先被击倒\" 不是旗杆整局胜利。",
    ]
    if manifest["sample_class"] == "diagnostic":
        out.append("本实验用 DevQueueOffer 预置候选：样本是条件实验（diagnostic），不能估计自然抽到率或全局主动健康。")
    if manifest["sample_class"] == "legal_search":
        out.append("候选由原生抽取按 Case 种子真实抽出，选择按计划规则；搜索频率不是自然出现率。")
    return out


def replay_hint(run_dir, case_id):
    return f"重放：`.\\Scripts\\RunBuildEvaluation.ps1 -ReplayRun \"{run_dir}\" -Case {case_id}`"
