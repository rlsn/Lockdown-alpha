"""Lockdown Demo adapter of the build evaluation core (docs/systems/BUILD_EVALUATION.md).

It describes what the current runtime can evaluate, from the catalog the native game loads (demo_content.json, whose
manifest digest is the native catalog root), and turns the native Host driver's evidence into Case statuses, metrics and
facts. It reads the authority LD_* lines the Host log contract already defines; it never evaluates damage, draws offers or
cards or decides an outcome itself. A definition that is not in the runtime catalog has no native handler and is
unsupported.

#198 growth: a plan fixes the active choice of each active opportunity (the opening one and one per level of
progression.active_offer_levels reached). Without a build list the production AI rule (LDOpponentPolicy::DecideFollowUpBuild)
does every other build command. Workshop (review §4, spec §19): a side's build list owns every build command except the
active choices: the native evaluation policy sends exactly its steps, in order, through the players' build commands when
growth's gate admits them, and the production rule never builds for that side. Supported steps: exact points per stat, a card
install on a named place (a learned skill, the innate D or the body, which carries the basic attack; Y25 with its element; an
element medal on the body or a learned skill), an uninstall, a
respec, a blueprint learn and an active replacement. Cards are mechanics. A planned side skips every card offer (spec §6.4);
its cards are the level cards of its plan, one per level its supply crosses, received on the field with the supply. A step
the seeded Case cannot carry out ends Invalid (PlanNotReached), never forced. Every step's commit is verified against the
authority LD_BUILD or LD_CARD record and the terminal cards. NPC purchases, forgetting, card offer picks and a replacement
inside an active choice are not plannable and are refused as UnsupportedDecision with their reason.
"""

import hashlib
import json
import math
import re
from pathlib import Path

ADAPTER_ID = "lockdown-demo"
ADAPTER_VERSION = 16
# LDBuildEvaluation::MaxCases and MaxBuildSteps of the native driver (LDBuildEvaluationProbe.h); a larger plan is refused
# before any run.
NATIVE_MAX_CASES = 500
NATIVE_MAX_BUILD_STEPS = 32
NATIVE_INPUT_SCHEMA = "lockdown.build_evaluation.native_input"
NATIVE_RESULT_SCHEMA = "lockdown.build_evaluation.native_result"
NATIVE_RUN_SCHEMA = "lockdown.build_evaluation.native_run"
NATIVE_INPUT_SCHEMA_VERSION = 6
NATIVE_RESULT_SCHEMA_VERSION = 7
SEED_DERIVATION = 1
CAST_LAG_MS = 50
# Sample validity, not a balance target: a duelist that holds a learned active and casts none over this much engagement
# (every active is ready at the field entry) did not exercise its build, so the Case is Invalid (NeverCast).
NEVER_CAST_GRACE_MS = 5000
# Stances that walk into the rival: a timeout with no hit between two of them is an approach failure (NoEngagement); a
# kite, hold or guard stance may keep its distance on purpose, so such a timeout stays a censored result.
ENGAGING_STANCES = ("pursue", "burst")
ROLES = ("subject", "opponent")
PUBLIC_PLACE_KINDS = ("Spawn", "Camp", "Capture", "Dorm", "Airdrop")
CATALOG_EVIDENCE = "in the runtime catalog the native importer accepted (it refuses any definition without a handler)"
CANONICAL_TAGS = ("LD_EVAL_OPENING_PRESET", "LD_EVAL_CARD_PRESET", "LD_EVAL_FIELD_SPAWN", "LD_OFFER", "LD_CHOICE",
                  "LD_BUILD", "LD_CARD", "LD_REWARD", "LD_LEVEL_UP", "LD_HIT", "LD_DEATH_RECEIPT", "LD_CAST")
# Growth commands that mutate only their own contender's record, with any draw seeded by (match, contender, request)
# (LDDevelopmentDirector.cpp ApplyBuild, DrawHead's OfferSeed(match, contender, ordinal)); two AI controllers'
# commands in one tick follow engine actor-tick order.
INDEPENDENT_PREPARATION_TAGS = ("LD_OFFER", "LD_CHOICE", "LD_BUILD", "LD_CARD")
FIELD = re.compile(r'(\w+)=("(?:[^"\\]|\\.)*"|\S*)')
GUID = re.compile(r"\b[0-9A-F]{32}\b")
DEFINITION = re.compile(r"^[a-z][a-z0-9_]*$")
P2 = re.compile(r"^-?\d+\.\d{2}$")
SLOTS = ("Q", "W", "E")
# Build windows of duel_site: the opening rest R0 (after the opening choice, before the field supply) and the field (once
# the supply's level and every planned choice are reached).
WINDOWS = ("opening_rest", "field")
# Y25's element choices and their authority log names (LDLog::EnumName of ELDDemoElement).
ELEMENTS = {"fire": "Fire", "water": "Water", "electric": "Electric"}
# A card's place by its form (ELDCardTargetKind log names); a skill card names its learned skill instead.
CARD_PLACES = {"dash": "Dash", "body": "Body"}
FORM_PLACE = {"dash": "dash", "body": "body", "skill": None, "medal": None}
# Planned build steps: their capability decision, the authority kind (ELDBuildCommandKind) and their fields. Card steps are
# logged as LD_CARD, the others as LD_BUILD.
STEPS = {
    "allocate_points": ("allocate_points", "AllocatePoints", ("points",)),
    "install": ("install_card", "InstallCard", ("card", "target", "element")),
    "uninstall": ("uninstall_card", "UninstallCard", ("card",)),
    "learn": ("learn_blueprint", "LearnBlueprint", ("blueprint", "slot")),
    "replace": ("replace_active", "ReplaceActive", ("blueprint", "slot")),
    "respec": ("respec_points", "Respec", ()),
}
CARD_STEPS = ("install", "uninstall")
REQUIRED_FIELDS = {"install": ("card", "target"), "uninstall": ("card",), "allocate_points": ("points",),
                   "learn": ("blueprint", "slot"), "replace": ("blueprint", "slot")}
# Spec §5: these never open on the field.
REST_ONLY = ("replace", "respec")
# Decisions a plan may name that the current runtime does not carry out as planned, with the reason; the plan is refused
# as UnsupportedDecision. Build steps by their step name; "replace" inside an active choice rule.
UNPLANNABLE = {
    "forget": ("forget_active", "the AI has no forget command; a replacement forgets the replaced active"),
    "pick_card": ("pick_card", "a planned side skips every card offer (the draw is random); its cards are its level_cards"),
    "replace": ("replace_at_choice", "an active choice learns into an empty slot or keeps a blueprint; a replacement is "
                "its own rest-window build step (replace)"),
}
UNPLANNABLE_STEPS = ("forget", "pick_card")

METRICS = {
    "duel_score": {"unit": "score", "kind": "score", "direction": "higher_better",
                   "description": "+1 the subject killed the opponent, -1 the subject died first, 0 simultaneous trade or timeout"},
    "damage_dealt_hp": {"unit": "hp", "kind": "continuous", "direction": "higher_better",
                        "description": "HP the opponent lost to the subject's committed hits up to the outcome (shield absorption excluded)"},
    "damage_taken_hp": {"unit": "hp", "kind": "continuous", "direction": "lower_better",
                        "description": "HP the subject lost to the opponent's committed hits up to the outcome"},
    "subject_hp_end_bp": {"unit": "basis points", "kind": "continuous", "direction": "higher_better",
                          "description": "the subject's HP share when the Case ended (0 when dead)"},
    "decision_time_ms": {"unit": "ms", "kind": "time_to_event", "direction": "descriptive",
                         "description": "authority time from first-field entry to the first duelist death, censored at the duel timeout"},
    "engage_to_kill_ms": {"unit": "ms", "kind": "time_to_event", "direction": "descriptive",
                          "description": "authority time from the first committed hit between the duelists to the first duelist "
                                         "death (time to kill), censored at the duel timeout (counted from the duel start when "
                                         "no hit landed)"},
    "subject_casts": {"unit": "casts", "kind": "continuous", "direction": "descriptive",
                      "description": "the subject's Q/W/E releases (LD_CAST) from first-field entry to the outcome "
                                     "(and the outcome's own advance, at most 50 ms later)"},
    "opponent_casts": {"unit": "casts", "kind": "continuous", "direction": "descriptive",
                       "description": "the opponent's Q/W/E releases (LD_CAST) from first-field entry to the outcome "
                                      "(and the outcome's own advance, at most 50 ms later)"},
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
                                    "target_mode": ability["target"]["mode"],
                                    "effect_ops": sorted({effect["op"] for effect in ability.get("effects", [])}),
                                    "evidence": CATALOG_EVIDENCE}
    for stat in content["stats"]:
        mechanics[stat["id"]] = {"status": "Supported", "kind": "stat", "evidence": CATALOG_EVIDENCE}
    for card in content["cards"]:
        mechanics[card["id"]] = {
            "status": "Supported", "kind": "card", "form": card["form"], "type": card["type"], "vocab": list(card["vocab"]),
            "choose_element": list(card.get("choose_element", [])),
            "effect_ops": [effect["op"] for effect in card.get("effects", [])],
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
                    "cards": len(content["cards"])},
        "mechanics": mechanics,
        "decisions": {
            "ai_select": {"description": "the production active preference (LDOpponentPolicy::SelectActive)"},
            "option_index": {"description": "the option at index 0..2 of the actual active offer; refused when it has fewer"},
            "definition": {"description": "the actual active offer's option of one active; refused when not offered"},
            "preset_offer": {"description": "a diagnostic opening offer or a later one-to-three-active offer prepared by DevQueueOffer",
                             "sample_classes": ["diagnostic"]},
            "breakout_preset": {"description": "a DEV breakout sample (LDBreakoutPresets) applied at the field entry instead of "
                                               "the side's XP supply: its max-level skills, branches, cards and points",
                                "sample_classes": ["diagnostic"]},
            "preset_level_cards": {"description": "the card each card-offer level (2..level, every CARDS.OfferEveryLevels) of the supply gives instead of its offer "
                                                  "(a planned side skips every card offer)", "sample_classes": ["diagnostic"]},
            "allocate_points": {"description": "build step allocate_points: exact points per stat id in one command; a planned "
                                               "build spends every point its supply earns, or none when the side sets "
                                               "keep_points_unspent"},
            "install_card": {"description": "build step install: a stored copy of a level card onto its place (a learned "
                                            "skill, the innate D or the body; Y25 with its element; a medal on the body "
                                            "or a learned skill, outside the workshop capacity), within the workshop "
                                            "capacity of the supply's level"},
            "uninstall_card": {"description": "build step uninstall: an installed copy back to the library (any window)"},
            "respec_points": {"description": "build step respec, rest window only: every allocated point back"},
            "learn_blueprint": {"description": "build step learn: a pending blueprint into a named empty slot (duel_site "
                                               "never leaves a blueprint pending, so its plans are refused there)"},
            "replace_active": {"description": "build step replace, rest window only: a pending blueprint into an occupied "
                                              "slot, the old active forgotten and its cards back in the library "
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
                               "card_capacity_base": int(content["parameters"]["CARDS.CapacityBase"]),
                               "card_levels_per_slot": int(content["parameters"]["CARDS.LevelsPerSlot"]),
                               "card_offer_every_levels": int(content["parameters"]["CARDS.OfferEveryLevels"]),
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
    if (not isinstance(side, dict) or "choices" not in side
            or set(side) - {"choices", "opening_preset", "presets", "level_cards", "build", "stance", "keep_points_unspent",
                            "breakout_preset"}):
        raise ValueError(f"plan.{role}: an object with choices (and optional opening_preset, presets, level_cards, build, "
                         "stance, keep_points_unspent and breakout_preset)")
    preset = side.get("breakout_preset")
    if preset is not None and (not isinstance(preset, str) or not preset.startswith("breakout_")):
        raise ValueError(f"plan.{role}.breakout_preset: a DEV breakout sample id (breakout_*)")
    if not isinstance(side.get("keep_points_unspent", False), bool):
        raise ValueError(f"plan.{role}.keep_points_unspent: true or false")
    if not isinstance(side["choices"], list):
        raise ValueError(f"plan.{role}.choices: a list")
    choices = [_rule(rule, f"plan.{role}.choices[{index}]") for index, rule in enumerate(side["choices"])]
    _opening_preset(plan, role)
    _level_cards(plan, role)
    _stance(plan, role)
    presets = side.get("presets", [])
    if not isinstance(presets, list) or any(not isinstance(row, list) or not 1 <= len(row) <= 3
                                            or any(not isinstance(card, str) for card in row) or len(set(row)) != len(row)
                                            for row in presets):
        raise ValueError(f"plan.{role}.presets: lists of one to three distinct active ids")
    return choices, presets


def _opening_preset(plan, role):
    cards = plan[role].get("opening_preset", [])
    if (not isinstance(cards, list) or len(cards) > 3 or any(not isinstance(card, str) for card in cards)
            or len(set(cards)) != len(cards)):
        raise ValueError(f"plan.{role}.opening_preset: zero to three distinct active ids")
    return cards


def _level_cards(plan, role):
    cards = plan[role].get("level_cards", [])
    if (not isinstance(cards, list) or len(cards) > MAX_LEVEL_CARDS
            or any(not isinstance(card, str) or not DEFINITION.fullmatch(card) for card in cards)):
        raise ValueError(f"plan.{role}.level_cards: at most {MAX_LEVEL_CARDS} card ids (copies may repeat)")
    return cards


MAX_LEVEL_CARDS = 23


def card_levels(level, every=1):
    """The levels 2..level a supply crosses that CARDS.OfferEveryLevels divides: each gives one card offer, replaced by
    one level card."""
    return [n for n in range(2, level + 1) if n % every == 0]


def card_capacity(params, level):
    return params["card_capacity_base"] + (level - 1) // params["card_levels_per_slot"]


STANCE_KINDS = ("kite", "burst", "hold", "guard")
STANCE_STANDOFF_CM = (200, 2000)
STANCE_THRESHOLD_BP = (1000, 9000)
STANCE_LEASH_CM = (500, 5000)
# Operation knobs (any stance, each optional) and their native limits; an absent knob is its default (exact play, no
# warning dodge, no lead aim). lead_aim is a flag.
OPERATION_KNOBS = {"reaction_ms": (0, 1500), "aim_error_cm": (0, 400), "skip_bp": (0, 9000), "warning_react_ms": (0, 1500)}
OPERATION_FLAGS = ("lead_aim",)
OPERATION_DEFAULTS = {"reaction_ms": 0, "aim_error_cm": 0, "skip_bp": 0, "warning_react_ms": -1, "lead_aim": False}


def _stance(plan, role):
    """The optional engagement stance (absent: the production pursue): {"kind": "kite"|"burst"|"hold"|"guard",
    "standoff_cm": N, "threshold_bp": N (guard only, required), "leash_cm": N (optional)}, each with the optional operation
    knobs reaction_ms, aim_error_cm, skip_bp, warning_react_ms and the flag lead_aim. {"kind": "pursue"} is written only to
    carry knobs."""
    stance = plan[role].get("stance")
    if stance is None:
        return None

    def bounded(key, bounds):
        value = stance[key]
        return isinstance(value, int) and not isinstance(value, bool) and bounds[0] <= value <= bounds[1]

    knobs = (set(OPERATION_KNOBS) | set(OPERATION_FLAGS)) & set(stance) if isinstance(stance, dict) else set()
    if (not isinstance(stance, dict)
            or any(not (isinstance(stance[key], bool) if key in OPERATION_FLAGS else bounded(key, OPERATION_KNOBS[key]))
                   for key in knobs)):
        raise ValueError(f"plan.{role}.stance: optional integer knobs " + ", ".join(
            f"{key} {low}..{high}" for key, (low, high) in OPERATION_KNOBS.items())
            + " and optional true/false " + ", ".join(OPERATION_FLAGS))
    if stance.get("kind") == "pursue":
        if not knobs or set(stance) - knobs - {"kind"}:
            raise ValueError(f"plan.{role}.stance: a pursue stance carries only operation knobs, at least one "
                             f"(omit the stance to pursue exactly)")
        return {key: stance[key] for key in ("kind", *OPERATION_KNOBS, *OPERATION_FLAGS) if key in stance}
    guard = stance.get("kind") == "guard"
    keys = {"kind", "standoff_cm"} | ({"threshold_bp"} if guard else set())
    if (stance.get("kind") not in STANCE_KINDS or not keys <= set(stance)
            or set(stance) - keys - {"leash_cm"} - knobs or not bounded("standoff_cm", STANCE_STANDOFF_CM)
            or (guard and not bounded("threshold_bp", STANCE_THRESHOLD_BP))
            or ("leash_cm" in stance and not bounded("leash_cm", STANCE_LEASH_CM))):
        raise ValueError(f"plan.{role}.stance: {{\"kind\": kite|burst|hold|guard, \"standoff_cm\": "
                         f"{STANCE_STANDOFF_CM[0]}..{STANCE_STANDOFF_CM[1]}, \"threshold_bp\": "
                         f"{STANCE_THRESHOLD_BP[0]}..{STANCE_THRESHOLD_BP[1]} (guard only), optional \"leash_cm\": "
                         f"{STANCE_LEASH_CM[0]}..{STANCE_LEASH_CM[1]}}} (omit it to pursue)")
    return {key: stance[key] for key in ("kind", "standoff_cm", "threshold_bp", "leash_cm", *OPERATION_KNOBS, *OPERATION_FLAGS)
            if key in stance}


def operation_knobs(plan, role):
    """The role's planned operation knobs, each its default when not written."""
    stance = _stance(plan, role) or {}
    return {key: stance.get(key, default) for key, default in OPERATION_DEFAULTS.items()}


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
    for key in ("card", "blueprint"):
        if key in step:
            out[key] = _definition(step[key], f"{where}.{key}")
    if "slot" in step:
        if step["slot"] not in SLOTS:
            raise ValueError(f"{where}.slot: one of {list(SLOTS)}")
        out["slot"] = step["slot"]
    if "target" in step:
        target = step["target"]
        if not isinstance(target, str) or (target not in CARD_PLACES and not DEFINITION.fullmatch(target)):
            raise ValueError(f"{where}.target: dash, body or a learned skill's id")
        out["target"] = target
    if "element" in step:
        if step["element"] not in ELEMENTS:
            raise ValueError(f"{where}.element: one of {list(ELEMENTS)}")
        out["element"] = step["element"]
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


def _build_needs(role, steps, choices, caps, supply_xp, level_cards, keep_unspent=False):
    """What a planned build needs, and what duel_site provably cannot give it. Cards come only from the plan's level cards,
    received on the field with the supply; a named skill is learned only through a definition choice.
    keep_unspent: the plan leaves every earned point unspent (a bare build), so its point steps must net to zero."""
    params = caps["scenarios"]["duel_site"]["parameters"]
    rows = caps["mechanics"]
    level = level_for({"xp_to_next": params["xp_to_next"]}, supply_xp)
    earned = params["opening_points"] + params["points_per_level"] * (level - 1)
    capacity = card_capacity(params, level)
    opening = choices[0]["definition"] if choices and choices[0]["rule"] == "definition" else None
    learned = {"opening_rest": {opening} - {None},
               "field": {rule["definition"] for rule in choices if rule["rule"] == "definition"}}
    received = {}
    for card in level_cards:
        received[card] = received.get(card, 0) + 1
    installed = {}
    # A medal's element slot per host: a second medal there sends the first back to the library.
    medal_slots = {}
    allocated = 0
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
        target = step.get("target")
        names = [step.get("card"), step.get("blueprint"), *step.get("points", {})]
        if target and target not in CARD_PLACES:
            names.append(target)
        names = [name for name in names if name]
        mechanics.update(names)
        if any(name not in rows for name in names):
            continue
        card = step.get("card")
        row = rows.get(card, {})
        if card and row.get("kind") != "card":
            problem(f"{card} is a {row.get('kind')}, not a card")
            continue
        if kind in REST_ONLY and at != "opening_rest":
            problem("a rest-window command (spec §5); duel_site has no rest window after the supply")
            continue
        if kind == "install":
            if at == "opening_rest":
                problem("no card is owned in the opening rest: the level cards arrive with the field supply")
            medal = row.get("form") == "medal"
            if medal and target and medal_slots.get(target):
                installed[medal_slots[target]] -= 1
                medal_slots[target] = None
            if at != "opening_rest" and installed.get(card, 0) >= received.get(card, 0):
                problem(f"no stored copy of {card}: the level cards give {received.get(card, 0)}")
            if not medal and sum(n for name, n in installed.items() if rows.get(name, {}).get("form") != "medal") >= capacity:
                problem(f"the workshop of level {level} holds {capacity} cards")
            place = FORM_PLACE.get(row.get("form"))
            if medal and target == "body":
                pass
            elif place and target != place:
                problem(f"a {row.get('form')} card goes on the {place}")
            elif not place and (target in CARD_PLACES or rows.get(target, {}).get("kind") != "active"):
                problem(f"a skill card names a learned skill, not {target}")
            elif not place and target not in learned[at]:
                problem(f"cannot prove {target} is learned here: choose it by definition")
            choices_of = row.get("choose_element", [])
            if bool(choices_of) != ("element" in step):
                problem(f"{card} {'chooses one of ' + str(choices_of) if choices_of else 'takes no element'}")
            elif "element" in step and step["element"] not in choices_of:
                problem(f"{card} chooses one of {choices_of}")
            installed[card] = installed.get(card, 0) + 1
            if medal and target:
                medal_slots[target] = card
        elif kind == "uninstall":
            if not installed.get(card):
                problem(f"no copy of {card} is installed by an earlier step")
            else:
                installed[card] -= 1
                host = next((host for host, held in medal_slots.items() if held == card), None)
                if host:
                    medal_slots[host] = None
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
    if keep_unspent and not steps:
        problems.append(f"IllegalDecision: plan.{role}.keep_points_unspent needs a planned build; without one the production "
                        "rule allocates every point")
    elif keep_unspent and allocated:
        problems.append(f"IllegalDecision: plan.{role}.build leaves {allocated} points allocated; keep_points_unspent keeps "
                        f"all {earned} earned points unspent")
    elif steps and not keep_unspent and allocated != earned:
        problems.append(f"IllegalDecision: plan.{role}.build allocates {allocated} of the {earned} points its supply earns; a "
                        "planned build owns every build command, so it spends every earned point (spec §4.1) unless the side "
                        "sets keep_points_unspent")
    return mechanics, decisions, problems


def requirements(scenario, plan, caps):
    if set(plan) - set(ROLES):
        raise ValueError(f"plan: only {ROLES}")
    supply = scenario["parameters"]["supply_xp"]
    mechanics, decisions, problems = set(), set(), []
    for role in ROLES:
        choices, presets = _side_plan(plan, role)
        opening = _opening_preset(plan, role)
        level_cards = _level_cards(plan, role)
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
            needs = _build_needs(role, steps, choices, caps, supply[role], level_cards,
                                 plan[role].get("keep_points_unspent", False))
            mechanics |= needs[0]
            decisions |= needs[1]
            problems += needs[2]
        if plan[role].get("breakout_preset"):
            decisions.add("breakout_preset")
        if level_cards:
            decisions.add("preset_level_cards")
            mechanics.update(level_cards)
            params = caps["scenarios"]["duel_site"]["parameters"]
            level = level_for({"xp_to_next": params["xp_to_next"]}, supply[role])
            offered = card_levels(level, params["card_offer_every_levels"])
            if len(level_cards) != len(offered):
                problems.append(f"IllegalDecision: plan.{role}.level_cards needs {len(offered)} cards for levels 2..{level}")
            for card in level_cards:
                kind = caps["mechanics"].get(card, {}).get("kind")
                if kind and kind != "card":
                    problems.append(f"IllegalDecision: {card} is a {kind}, not a card")
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
        if opening or presets:
            decisions.add("preset_offer")
        if presets:
            if len(presets) > max(0, opportunities - 1):
                problems.append(f"IllegalDecision: plan.{role} prepares {len(presets)} offers for {max(0, opportunities - 1)} later opportunities")
        for card in opening:
            mechanics.add(card)
            row = caps["mechanics"].get(card, {})
            if row and (row.get("kind") != "active" or not row.get("opening")):
                problems.append(f"IllegalDecision: plan.{role}.opening_preset contains {card}, which is not an opening active")
        for row in presets:
            for card in row:
                mechanics.add(card)
                kind = caps["mechanics"].get(card, {}).get("kind")
                if kind and kind != "active":
                    problems.append(f"IllegalDecision: plan.{role} presets name {kind} {card}; only actives are offered")
        known_chosen = set()
        for ordinal, rule in enumerate(choices, 1):
            offered = opening if ordinal == 1 else presets[ordinal - 2] if ordinal - 2 < len(presets) else []
            for card in known_chosen.intersection(offered):
                problems.append(f"IllegalDecision: plan.{role} ordinal {ordinal} offers {card}, already chosen earlier")
            selected = None
            if rule["rule"] == "definition":
                selected = rule["definition"]
                if offered and selected not in offered:
                    problems.append(f"IllegalDecision: plan.{role} ordinal {ordinal} preset does not offer {selected}")
            elif rule["rule"] == "option_index" and offered:
                if rule["index"] >= len(offered):
                    problems.append(f"IllegalDecision: plan.{role} ordinal {ordinal} preset has no option {rule['index']}")
                else:
                    selected = offered[rule["index"]]
            elif rule["rule"] == "ai_select" and len(offered) == 1:
                selected = offered[0]
            if selected:
                if selected in known_chosen:
                    problems.append(f"IllegalDecision: plan.{role} ordinal {ordinal} selects {selected}, already chosen earlier")
                known_chosen.add(selected)
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
            row = {"role": role, "slot": slot, "supply_xp": params["supply_xp"][role], "choices": choices,
                   "opening_preset": _opening_preset(plan, role), "presets": presets,
                   "level_cards": _level_cards(plan, role), "build": _side_build(plan, role)}
            stance = _stance(plan, role)
            if stance is not None:
                row["stance"] = stance
            if plan[role].get("breakout_preset"):
                row["breakout_preset"] = plan[role]["breakout_preset"]
            contenders.append(row)
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


def breakout_roles(plan):
    """The sides whose build is a DEV breakout sample: the sample replaces the whole build at the field entry, so the
    ordinary supply, choice, build-step and level-card evidence does not apply to them (breakout_problems does)."""
    return {role for role in ROLES if plan[role].get("breakout_preset")}


def breakout_problems(events, ids, plan, contenders):
    """Prove each breakout sample was committed once by the authority and that the side ends on it."""
    problems = []
    for role in sorted(breakout_roles(plan)):
        preset = plan[role]["breakout_preset"]
        applied = [fields for tag, fields, _ in events if tag == "LD_DEV_BREAKOUT_PRESET" and fields.get("contender") == ids[role]]
        if len(applied) != 1 or applied[0].get("preset") != preset:
            problems.append(f"PresetNotApplied: the {role} needs exactly one committed {preset}")
            continue
        row, fields = contenders[role], applied[0]
        if (not fields.get("level", "").isdigit() or not fields.get("cards", "").isdigit()
                or row["level"] != int(fields["level"]) or len(row["cards"]) != int(fields["cards"])):
            problems.append(f"PresetNotApplied: the {role} ends at level {row['level']} with {len(row['cards'])} cards, "
                            f"{preset} committed level {fields.get('level')} with {fields.get('cards')} cards")
    for tag, fields, _ in events:
        if tag == "LD_DEV_BREAKOUT_PRESET" and not any(fields.get("contender") == ids[role] for role in breakout_roles(plan)):
            problems.append("PresetNotApplied: a breakout sample was committed for a side that plans none")
    return problems


def selection_problems(events, ids, plan, contenders, t_out):
    """Validate both sides' active offer and committed choice at every planned ordinal, and the terminal build."""
    problems = []
    roles = {contender: role for role, contender in ids.items() if role not in breakout_roles(plan)}
    choices = {role: [] if role in breakout_roles(plan) else _side_plan(plan, role)[0] for role in ROLES}
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


CARD_ROW = {"id": str, "serial": int, "installed": bool, "target_kind": str, "target_id": str, "element": str}


def _card_row(row):
    return (isinstance(row, dict) and set(CARD_ROW) <= row.keys()
            and all(isinstance(row[key], kind) and (kind is bool or not isinstance(row[key], bool)) for key, kind in CARD_ROW.items()))


def _card_place(step):
    """The LD_CARD target text of a planned install (DevelopmentCardTargetText): Basic, Dash, Body or Skill:<id>, with Y25's
    element after a colon."""
    target = step["target"]
    text = CARD_PLACES.get(target) or f"Skill:{target}"
    return text + (f":{ELEMENTS[step['element']]}" if "element" in step else "")


def expected_build(step, caps):
    """The fields the authority logs for a committed planned step: LD_CARD for a card step, LD_BUILD otherwise."""
    kind = step["step"]
    if kind in CARD_STEPS:
        return {"kind": STEPS[kind][1], "card": step["card"], "installed": "1" if kind == "install" else "0",
                "target": _card_place(step) if kind == "install" else "-", "stored": "-"}
    order = caps["scenarios"]["duel_site"]["parameters"]["stat_design_order"]
    return {"kind": STEPS[kind][1], "card": step.get("blueprint") or "-",
            "slot": step["slot"] if kind in ("learn", "replace") else "-",
            "points": ",".join(str(step["points"].get(stat, 0)) for stat in order) if kind == "allocate_points" else "-"}


def planned_cards(steps):
    """The installed copies a plan ends with, as (card, target kind, target id, element) with their count."""
    out = {}
    # An element medal (id prefix medal_) takes its host's single element slot and returns the medal held there.
    medal_slots = {}
    for step in steps:
        if step["step"] not in CARD_STEPS:
            continue
        if step["step"] == "install":
            target = step["target"]
            key = (step["card"], CARD_PLACES.get(target, "Skill"), "" if target in CARD_PLACES else target,
                   ELEMENTS.get(step.get("element"), "None"))
            if step["card"].startswith("medal_"):
                if medal_slots.get(target):
                    out[medal_slots[target]] -= 1
                medal_slots[target] = key
            out[key] = out.get(key, 0) + 1
        else:
            for key in sorted(out):
                if key[0] == step["card"] and out[key] > 0:
                    out[key] -= 1
                    host = next((host for host, held in medal_slots.items() if held == key), None)
                    if host:
                        medal_slots[host] = None
                    break
    return {key: count for key, count in out.items() if count}


def build_problems(events, ids, plan, contenders, t_out, caps):
    """A planned build owns every build command of its side: one LD_BUILD per planned step, in order, in its window, before
    the outcome, carrying exactly the planned kind, subject, slot, points and placement; the terminal cards agree. The
    skipped card offers (SkipCards) are the policy's own and are not steps."""
    problems = []
    enters = [fields for tag, fields, _ in events if tag == "LD_FIELD_ENTER" and fields.get("segment") == "0"]
    field_at = int(enters[0]["at_ms"]) if len(enters) == 1 and enters[0].get("at_ms", "").isdigit() else None
    for role in ROLES:
        if role in breakout_roles(plan):
            continue
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
        builds = [fields for tag, fields in own if tag == "LD_BUILD" or (tag == "LD_CARD" and fields.get("kind") != "SkipCards")]
        if len(builds) != len(steps):
            problems.append(f"TraceIncomplete: the {role} has {len(builds)} LD_BUILD and LD_CARD records for {len(steps)} planned build steps "
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
        terminal = {}
        for card in row["cards"]:
            if card["installed"]:
                key = (card["id"], card["target_kind"], card["target_id"], card["element"])
                terminal[key] = terminal.get(key, 0) + 1
        planned = planned_cards(steps)
        if terminal != planned:
            problems.append(f"PlanNotReached: the {role} ends with installed cards {sorted(terminal.items())}, planned "
                            f"{sorted(planned.items())}")
    return problems


def level_card_problems(events, ids, plan, contenders, t_out, caps, supply):
    """Prove each level-card preset was staged, committed with the supply in place of the level-up card offers and held."""
    problems = []
    params = caps["scenarios"]["duel_site"]["parameters"]
    for role in ROLES:
        if role in breakout_roles(plan):
            continue
        expected = _level_cards(plan, role)
        own = [(index, tag, fields) for index, (tag, fields, _) in enumerate(events) if fields.get("contender") == ids[role]]
        staged = [(index, fields) for index, tag, fields in own if tag == "LD_EVAL_CARD_PRESET"]
        if not expected:
            if staged:
                problems.append(f"PresetNotApplied: the {role} has an unplanned level-card preset")
            continue
        level = level_for({"xp_to_next": params["xp_to_next"]}, supply[role])
        interval = f"1->{level}"
        rewards = [(index, fields) for index, tag, fields in own
                   if tag == "LD_REWARD" and fields.get("cause") == "dev_fixture"]
        grants = [(index, fields) for index, tag, fields in own
                  if tag == "LD_LEVEL_UP" and fields.get("level") == interval]
        if len(staged) != 1 or len(rewards) != 1 or len(grants) != 1:
            problems.append(f"PresetNotApplied: the {role} needs one staged preset, one supply reward and one level-up grant")
        else:
            stage_index, stage = staged[0]
            reward_index, reward = rewards[0]
            grant_index, grant = grants[0]
            times = [entry.get("t", "") for entry in (stage, reward, grant)]
            if (not all(t.isdigit() for t in times) or not stage_index < reward_index < grant_index
                    or not int(times[0]) <= int(times[1]) == int(times[2]) <= t_out
                    or not stage.get("ticket", "").isdigit() or int(stage["ticket"]) <= 0
                    or reward.get("level") != interval
                    or stage.get("cards", "").split(",") != expected or grant.get("card_offers") != "0"):
                problems.append(f"PresetNotApplied: the {role} staged, supplied or granted level cards differ from the plan")
        held = sorted(card["id"] for card in contenders[role]["cards"])
        if held != sorted(expected):
            problems.append(f"PresetNotApplied: the {role} ends with cards {held}, the level cards are {sorted(expected)}")
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
        # card the level cards did not give, a step growth refused): the plan was not reached.
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
                "stat_points", "actives", "blueprints", "has_body", "hp_p2", "max_hp_p2", "momentum_p2", "max_momentum_p2",
                "planned_steps", "steps_committed", "cards"}
    integer_fields = ("slot", "level", "unspent_points", "choices", "planned_choices", "supply_xp",
                      "presets_queued", "presets_requested", "hp_p2", "max_hp_p2", "momentum_p2", "max_momentum_p2",
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
            any(not isinstance(row["cards"], list) for row in rows) or
            any(not _card_row(card) for row in rows for card in row["cards"])):
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
    for role, contender in ids.items():
        spawns = [fields for tag, fields, _ in events if tag == "LD_EVAL_FIELD_SPAWN" and fields.get("contender") == contender]
        if (len(spawns) != 1 or spawns[0].get("t") != str(t0) or spawns[0].get("generation") != str(result["generation"])
                or not spawns[0].get("life", "").isdigit() or int(spawns[0]["life"]) < 1
                or not re.fullmatch(r"-?\d+,-?\d+,-?\d+", spawns[0].get("pos", ""))):
            reasons.append(f"TraceIncomplete: the {role} field spawn is missing, duplicated or inconsistent")
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
        if any(tag == "LD_DEATH_RECEIPT" and row.get("contender") in duelists - {victim} and row.get("t") == str(t_out)
               for tag, row, _ in events):
            reasons.append("TraceIncomplete: simultaneous deaths were reported as a single kill, not a trade")
    elif outcome["kind"] == "trade":
        t_out = t0 + outcome["t_ms"]
        if (outcome["victim"] or outcome["killer"] or not t0 <= t_out <= min(result["ended_at_ms"],
                                                                             t0 + int(params["duel_timeout_ms"]))):
            reasons.append("TraceIncomplete: trade time or outcome identity is inconsistent")
        for victim_role, killer_role in (("subject", "opponent"), ("opponent", "subject")):
            victim, killer = ids[victim_role], ids[killer_role]
            deaths = [row for tag, row, _ in events if tag == "LD_DEATH_RECEIPT" and row.get("contender") == victim
                      and row.get("t") == str(t_out)]
            lethal = [row for tag, row, _ in events if tag == "LD_HIT" and row.get("victim") == victim
                      and row.get("t") == str(t_out) and row.get("lethal") == "1" and row.get("credit") == killer]
            if (len(deaths) != 1 or len(lethal) != 1 or deaths[0].get("killer") != killer
                    or deaths[0].get("life") != lethal[0].get("victim_life")):
                reasons.append(f"TraceIncomplete: the {victim_role} trade death lacks a matching reciprocal lethal hit and receipt")
    elif outcome["kind"] == "timeout":
        t_out = t0 + int(params["duel_timeout_ms"])
        if result["ended_at_ms"] < t_out:
            reasons.append("TraceIncomplete: the Case ended before the duel horizon")
        if outcome["t_ms"] != int(params["duel_timeout_ms"]):
            reasons.append("TraceIncomplete: the timeout disagrees with the duel horizon")
    else:
        reasons.append(f"TraceIncomplete: outcome {outcome['kind']}")
        t_out = int(result["ended_at_ms"])
    samples = breakout_roles(next(arm["plan"] for arm in manifest["design"]["arms"] if arm["id"] == case["arm"]))
    for row in result["contenders"]:
        if not row["progression_valid"] or not row["profile_bound"]:
            reasons.append(f"Setup: the {row['role']} had no seeded identity or evaluation profile")
        if row["supply_xp"] != params["supply_xp"][row["role"]]:
            reasons.append(f"SupplyMismatch: the {row['role']} native supply differs from the plan")
        if row["role"] in samples:
            continue
        if row["choices"] != row["planned_choices"]:
            reasons.append(f"PlanNotReached: the {row['role']} consumed {row['choices']} of {row['planned_choices']} planned choices")
        if row["supply_xp"] > 0 and row["supply_status"] != "Granted":
            reasons.append(f"SupplyMissing: the {row['role']} supply is {row['supply_status']}")
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
        # A breakout sample stands in for its side's supply: the sample sets the level, no dev_fixture XP is granted.
        if supplied[role] != (0 if role in samples else params["supply_xp"][role]):
            reasons.append(f"SupplyMismatch: the {role} received {supplied[role]} dev_fixture XP, planned {params['supply_xp'][role]}")
    plans = {arm["id"]: arm["plan"] for arm in manifest["design"]["arms"]}
    reasons += breakout_problems(events, ids, plans[case["arm"]], {row["role"]: row for row in rows})
    reasons += selection_problems(events, ids, plans[case["arm"]], {row["role"]: row for row in rows}, t_out)
    reasons += build_problems(events, ids, plans[case["arm"]], {row["role"]: row for row in rows}, t_out, caps)
    reasons += level_card_problems(events, ids, plans[case["arm"]], {row["role"]: row for row in rows},
                                        t_out, caps, params["supply_xp"])
    reasons += operation_problems(events, ids, plans[case["arm"]])
    for role in ROLES:
        _, presets = _side_plan(plans[case["arm"]], role)
        opening = _opening_preset(plans[case["arm"]], role)
        offered = out["facts"]["offers"][role]
        prepared = [fields for tag, fields, _ in events
                    if tag == "LD_EVAL_OPENING_PRESET" and fields.get("contender") == ids[role]]
        if opening and (offered.get("1") != opening or len(prepared) != 1
                        or prepared[0].get("cards", "").split(",") != opening):
            reasons.append(f"PresetNotApplied: the {role} opening offer or staged evidence differs from {opening}")
        if not opening and prepared:
            reasons.append(f"PresetNotApplied: the {role} has an unplanned opening preset")
        for position, preset in enumerate(presets):
            if offered.get(str(position + 2)) != preset:
                reasons.append(f"PresetNotApplied: the {role} offer at ordinal {position + 2} is {offered.get(str(position + 2))}, prepared {preset}")
    casts = {role: 0 for role in ROLES}
    for tag, row, _ in events:
        if tag == "LD_CAST" and row.get("contender") in duelists:
            if not row.get("t", "").isdigit() or not row.get("added", "").isdigit() or int(row["added"]) < 1:
                reasons.append("TraceIncomplete: LD_CAST has no valid authority time or count")
            # LD_CAST carries the end of the advance that committed the release (at most one 60 FPS advance later).
            elif t0 <= int(row["t"]) <= t_out + CAST_LAG_MS:
                casts[roles[row["contender"]]] += int(row["added"])
    # A duelist's skill hit proves a Q/W/E release; a trace without any LD_CAST then comes from a Host build that predates
    # the cast trace, and its cast counts would be zeros of the wrong shape.
    if not any(tag == "LD_CAST" for tag, _, _ in events) and any(
            tag == "LD_HIT" and row.get("src") in duelists and row.get("via", "").startswith("learned:")
            for tag, row, _ in events):
        reasons.append("TraceIncomplete: duelist skill hits without any LD_CAST line (the Host build predates the cast trace)")
    duel_hits = [int(row["t"]) for tag, row, _ in events if tag == "LD_HIT" and t0 <= int(row["t"]) <= t_out
                 and {row["src"], row["victim"]} == duelists]
    plan = plans[case["arm"]]
    engaging = all((_stance(plan, role) or {}).get("kind", "pursue") in ENGAGING_STANCES for role in ROLES)
    if outcome["kind"] == "timeout" and not duel_hits and engaging:
        reasons.append(f"NoEngagement: the duelists never hit each other before the duel horizon (closest approach "
                       f"{_closest_approach(events, ids, t0, t_out)}; an approach or pathing failure, not a censored fight)")
    elif duel_hits:
        for row in rows:
            knobs = operation_knobs(plan, row["role"])
            engaged_ms = t_out - min(duel_hits)
            # SkipBp holds casts on purpose and ReactionMs delays each one: zero casts can then be the planned play.
            if (row["actives"] and not casts[row["role"]] and not knobs["skip_bp"]
                    and engaged_ms >= NEVER_CAST_GRACE_MS + knobs["reaction_ms"]):
                reasons.append(f"NeverCast: the {row['role']} held {len(row['actives'])} learned "
                               f"active{'s' if len(row['actives']) != 1 else ''} and cast none in "
                               f"{engaged_ms / 1000:g} s of engagement (an AI execution failure; the sample "
                               f"does not exercise its build)")
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
    score = 0 if outcome["kind"] in ("timeout", "trade") else (1 if outcome["victim"] == "opponent" else -1)
    engaged = min((int(fields["t"]) for tag, fields, _ in events if tag == "LD_HIT" and t0 <= int(fields["t"]) <= t_out
                   and {fields["src"], fields["victim"]} == {subject, opponent}), default=t0)
    out.update(status="Completed", reason="", metrics={
        "duel_score": {"value": score, "censored": outcome["kind"] == "timeout"},
        "damage_dealt_hp": {"value": dealt / 100, "censored": False},
        "damage_taken_hp": {"value": taken / 100, "censored": False},
        "subject_hp_end_bp": {"value": (me["hp_p2"] * 10000 // me["max_hp_p2"]) if me["has_body"] and me["max_hp_p2"] else 0,
                              "censored": False},
        "decision_time_ms": {"value": t_out - t0, "censored": outcome["kind"] == "timeout"},
        "engage_to_kill_ms": {"value": t_out - engaged, "censored": outcome["kind"] == "timeout"},
        "subject_casts": {"value": casts["subject"], "censored": False},
        "opponent_casts": {"value": casts["opponent"], "censored": False},
    })
    return out


def _closest_approach(events, ids, t0, t_out):
    """The smallest XY distance between the duelists over their same-time heartbeats, or "unknown" without any."""
    feet = {}
    for tag, fields, _ in events:
        if tag == "LD_HEARTBEAT" and fields.get("contender") in ids.values() and fields.get("t", "").isdigit() \
                and t0 <= int(fields["t"]) <= t_out and re.fullmatch(r"-?[\d.]+,-?[\d.]+,-?[\d.]+", fields.get("pos", "")):
            x, y, _ = (float(part) for part in fields["pos"].split(","))
            feet.setdefault(int(fields["t"]), {})[fields["contender"]] = (x, y)
    gaps = [math.dist(*pair.values()) for pair in feet.values() if len(pair) == 2]
    return f"{min(gaps):.0f} cm" if gaps else "unknown"


def _is_outcome_kill_reward(fields, outcome, ids, t_out):
    """The killer's own reward for the deciding kill, committed at the outcome boundary."""
    pairs = ([(ids[outcome["killer"]], ids[outcome["victim"]])]
             if outcome["kind"] == "kill" and outcome["killer"] in ids and outcome["victim"] in ids
             else [(ids["subject"], ids["opponent"]), (ids["opponent"], ids["subject"])]
             if outcome["kind"] == "trade" else [])
    return (fields.get("t") == str(t_out) and fields.get("cause") == "death" and fields.get("role") == "killer"
            and (fields.get("contender"), fields.get("victim")) in pairs)


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


BUILD_FIELDS = ("t", "kind", "card", "serial", "slot", "points", "unspent", "level", "installed", "target", "stored",
                "forgot", "refunded", "paid", "reclaimed")


def _build_label(record):
    """A short report label of one committed build step."""
    if not record:
        return ""
    parts = [record.get("kind") or "", record.get("card") if record.get("card") not in (None, "-") else ""]
    parts += [f"{key}={record[key]}" for key in ("slot", "points", "target") if record.get(key) not in (None, "-")]
    return " ".join(part for part in parts if part)


def operation_problems(events, ids, plan):
    """A role with any operation knob must show exactly one native profile line carrying the same knobs."""
    reasons = []
    for role in ROLES:
        knobs = operation_knobs(plan, role)
        if knobs == OPERATION_DEFAULTS:
            continue
        lines = [fields for tag, fields, _ in events if tag == "LD_EVAL_PROFILE" and fields.get("contender") == ids[role]]
        # A knob left at its default may be absent from a profile line written before it existed.
        if len(lines) != 1 or any(lines[0].get(key) != str(int(value))
                                  and not (value == OPERATION_DEFAULTS[key] and key not in lines[0])
                                  for key, value in knobs.items()):
            reasons.append(f"Setup: the {role} operation knobs did not reach the native profile")
    return reasons


def facts(case, manifest, result, events, ids):
    offers = {role: {} for role in ROLES}
    chosen = {role: {} for role in ROLES}
    offer_details = {role: {} for role in ROLES}
    choice_details = {role: {} for role in ROLES}
    build_steps = {role: [] for role in ROLES}
    grants = {role: [] for role in ROLES}
    card_presets = {role: [] for role in ROLES}
    roles = {contender: role for role, contender in ids.items()}
    spawn = {}
    for tag, fields, _ in events:
        role = roles.get(fields.get("contender"))
        if tag == "LD_EVAL_FIELD_SPAWN" and role and role not in spawn:
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
        elif tag == "LD_BUILD" or (tag == "LD_CARD" and fields.get("kind") != "SkipCards"):
            build_steps[role].append({key: fields.get(key) for key in BUILD_FIELDS if key in fields})
        elif tag == "LD_LEVEL_UP":
            grants[role].append({key: fields.get(key) for key in ("t", "level", "card_offers")})
        elif tag == "LD_EVAL_CARD_PRESET":
            card_presets[role].append({key: fields.get(key) for key in ("t", "ticket", "cards")})
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
                            "cards": [dict(card) for card in row.get("cards", [])]}
              for row in result["contenders"]}
    plan = next((arm["plan"] for arm in manifest["design"]["arms"] if arm["id"] == case.get("arm")), None)
    operation = {role: operation_knobs(plan, role) for role in ROLES} if plan else None
    return {"offers": offers, "choices": chosen, "build_steps": build_steps, "grants": grants,
            "card_presets": card_presets, "builds": builds, "label": label,
            "operation": operation,
            "prefix": prefix, "random_fingerprint": fingerprint, "first_hit": first_hit,
            "spawn": spawn, "outcome": result.get("outcome")}


def _branch_facts(case, manifest, offer_details, choice_details, chosen, build_steps, grants):
    """The branch label and the comparable prefix: every subject decision before the branch point (offers and choices,
    committed build steps and level-up grants) and, at a choice branch, the offer the branches decide on."""
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
    # The match generation counts matches in one editor process; only the Case's own generation becomes relative.
    own_generation = re.compile(rf"\bgeneration={int(result.get('generation', -1))}\b")
    events, spatial, run = [], {}, []

    def flush():
        # Within one tick's run of independent preparation commands, order by contender role; each contender's own
        # commands keep their order. Any other event ends the run, so combat order is never rearranged.
        for role in sorted({who for _, who, _ in run}):
            events.extend(line for _, who, line in run if who == role)
        run.clear()

    for tag, fields, line in parse_trace(trace_text):
        t = int(fields["t"]) if fields.get("t", "").lstrip("-").isdigit() else None
        # LD_CAST carries the end of the advance that committed the release, at most CAST_LAG_MS after it.
        if t is not None and end >= 0 and t > end + (CAST_LAG_MS if tag == "LD_CAST" else 0):
            continue
        if tag in CANONICAL_TAGS:
            line = own_generation.sub("generation=case", GUID.sub(role_of, line))
            if tag in INDEPENDENT_PREPARATION_TAGS and t is not None and "contender" in fields:
                if run and run[0][0] != t:
                    flush()
                run.append((t, GUID.sub(role_of, fields["contender"]), line))
                continue
            flush()
            events.append(line)
        elif tag == "LD_HEARTBEAT" and fields.get("pos", "-") != "-":
            key = f"{GUID.sub(role_of, fields['contender'])}@{t}"
            spatial[key] = tuple(float(part) for part in fields["pos"].split(","))
    flush()
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
                        "momentum_p2", "max_momentum_p2", "profile_bound", "profile_phase", "profile_failure", "supply_xp",
                        "supply_status", "presets_requested", "presets_queued", "planned_steps", "steps_committed", "cards")
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
        required = ("phase", "life", "pos", "hp", "momentum", "shield")
        if any(key not in fields for key in required):
            problems.append(f"the {role} heartbeat at {t} lacks resource or position evidence")
            continue
        if (role, t) in seen:
            problems.append(f"the {role} heartbeat at {t} is duplicated")
            continue
        seen.add((role, t))
        for key in ("hp", "momentum"):
            pieces = fields[key].split("/")
            if fields[key] != "-" and (len(pieces) != 2 or any(not P2.fullmatch(piece) for piece in pieces)):
                problems.append(f"the {role} heartbeat at {t} has malformed {key}")
        if fields["shield"] != "-" and not P2.fullmatch(fields["shield"]):
            problems.append(f"the {role} heartbeat at {t} has malformed shield")
        resources.append({"role": role, "t_ms": t, **{key: fields[key] for key in ("phase", "life", "hp", "momentum", "shield")}})
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
        f"{content.get('cards', 0)} 张构筑牌）；目录外的机制一律 UnsupportedMechanic，不代表 v0.11 候选内容已被评估。",
        "计划规定每次主动机会选哪个主动（并总是请求立即学会）。没有构筑步骤的一方，学习图纸、加点、选牌与装牌用生产 AI 规则；"
        "有构筑步骤的一方由计划拥有全部构筑命令（主动选择除外），只按顺序发送计划的逐属性加点、指定位置装牌（Y25 带元素）、"
        "卸牌、洗点、学习与替换，生产规则不再为它构筑；它必须花完供给赚到的全部点数。",
        "有构筑步骤的一方跳过全部牌三选一（抽取是随机的）；它的牌只来自计划的 level_cards，供给跨过的每个给牌等级一张，"
        "随野外供给入库。计划要装的牌不在库里、指定主动没出现或 growth 拒绝某一步时，该 Case 判 Invalid（PlanNotReached），"
        "不强塞也不改用别的牌。休息窗口只有供给之前的开局休息，所以替换与洗点以及学习图纸在 duel_site 中是 IllegalDecision。",
        "NPC 购买、单独遗忘、选牌与选择时直接替换不可规划（UnsupportedDecision，附原因）。",
        "对决双方使用同一个固定的 DEV 评估画像：按计划选择与构筑、走到公开地点、只攻击指定对手、不撤退；交战微操是生产 AI 的 Engage。"
        "结果反映固定策略下的机制差异，不代表玩家适应或六类策略画像。",
        "成长由 T=0 的经验供给推动（cause=dev_fixture，经正常奖励复合事务），是计划假设，不证明真实对局中能在同样时间拿到这些经验。",
        "两名 AI 在真实 L_Demo 中移动、寻路与交战（NativeSpatial）；房主是唯一真人，只编排、不参战、不计分；没有拥有连接、网络或渲染证据。",
        "营地、Boss、空投与房主的介入被判为 Invalid 并单列，不计入结果；对决时限须早于第一野外段结束和 Boss 出现。",
        "duel_score 为 0 有两种来源：到时未分胜负（outcome.kind=timeout，删失）或同一权威时刻双方互杀（outcome.kind=trade，"
        "已观测、不删失）；两者都不是输；\"谁先被击倒\" 不是旗杆整局胜利。",
    ]
    if manifest["sample_class"] == "diagnostic":
        out.append("本实验用 DevQueueOffer 预置候选：样本是条件实验（diagnostic），不能估计自然抽到率或全局主动健康。")
    uneven = [arm["id"] for arm in manifest["design"]["arms"]
              if len({bool(arm["plan"][role].get("keep_points_unspent")) for role in ROLES}) > 1]
    if uneven:
        out.append(f"属性点预算不对等（{', '.join(f'`{arm}`' for arm in uneven)}）：一方花完赚到的全部属性点，另一方"
                   "（keep_points_unspent）全部不分配；这些 arm 的结果同时包含属性点预算差与构筑本身，不能单独归因于构筑质变。")
    out.append(f"无效样本不计入结论：双方都是走近交战的架势（pursue/burst）而到时从未互相命中（NoEngagement，接近或寻路"
               f"失败，不是删失，原因里写最近距离）；学会主动的一方在至少 {NEVER_CAST_GRACE_MS / 1000:g} s（另加其 reaction_ms）"
               f"交战中一次未施放（NeverCast，AI 执行失败；设了 skip_bp 的一方不判）。kite/hold/guard 架势到时未交手仍是删失。")
    if manifest["sample_class"] == "legal_search":
        out.append("候选由原生抽取按 Case 种子真实抽出，选择按计划规则；搜索频率不是自然出现率。")
    return out


def replay_hint(run_dir, case_id):
    return f"重放：`.\\Scripts\\RunBuildEvaluation.ps1 -ReplayRun \"{run_dir}\" -Case {case_id}`"
