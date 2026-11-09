"""Lockdown v0.10 Demo adapter of the build evaluation core (docs/systems/BUILD_EVALUATION.md).

It describes what the current runtime can evaluate, from the catalog the native game loads (demo_content.json, whose
manifest digest is the native catalog root), and turns the native Host driver's evidence into Case statuses, metrics and
facts. It reads the authority LD_* lines the Host log contract already defines; it never evaluates damage, draws offers or
decides an outcome itself. A definition that is not in the runtime catalog has no native handler and is unsupported.
"""

import hashlib
import json
import re
from pathlib import Path

ADAPTER_ID = "lockdown-demo"
ADAPTER_VERSION = 1
# LDBuildEvaluation::MaxCases of the native driver (LDBuildEvaluationProbe.h); a larger plan is refused before any run.
NATIVE_MAX_CASES = 500
NATIVE_INPUT_SCHEMA = "lockdown.build_evaluation.native_input"
NATIVE_RESULT_SCHEMA = "lockdown.build_evaluation.native_result"
NATIVE_RUN_SCHEMA = "lockdown.build_evaluation.native_run"
NATIVE_SCHEMA_VERSION = 1
SEED_DERIVATION = 1
ROLES = ("subject", "opponent")
PUBLIC_PLACE_KINDS = ("Spawn", "Camp", "Capture", "Dorm", "Airdrop")
CATALOG_EVIDENCE = "in the runtime catalog the native importer accepted (it refuses any definition without a handler)"
CANONICAL_TAGS = ("LD_OFFER", "LD_CHOICE", "LD_REWARD", "LD_LEVEL_UP", "LD_HIT", "LD_DEATH_RECEIPT")
FIELD = re.compile(r'(\w+)=("(?:[^"\\]|\\.)*"|\S*)')
GUID = re.compile(r"\b[0-9A-F]{32}\b")
SLOTS = ("Q", "W", "E")

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
                         "description": "authority time from T=0 to the first duelist death, censored at the duel timeout"},
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
        mechanics[ability["id"]] = {"status": "Supported", "kind": ability["kind"], "draftable": bool(ability.get("draftable")),
                                    "evidence": CATALOG_EVIDENCE}
    for attribute in content["attributes"]:
        mechanics[attribute["id"]] = {"status": "Supported", "kind": "attribute", "evidence": CATALOG_EVIDENCE}
    for card in content["opening_cards"]:
        mechanics[card["id"]] = {"status": "Supported", "kind": "opening", "evidence": CATALOG_EVIDENCE}
    places = sorted(anchor["id"] for anchor in world["anchors"] if anchor["kind"] in PUBLIC_PLACE_KINDS)
    progression = content["progression"]
    boss_appear_ms = int(content["boss"]["appear_at"]["ms"])
    return {
        "schema": "build_evaluation.capabilities", "schema_version": 1,
        "adapter": {"id": ADAPTER_ID, "version": ADAPTER_VERSION},
        "content": {"id": "lockdown-demo-content", "version": product_version(project_root),
                    "digest": hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
                    "abilities": sum(1 for row in content["abilities"]), "attributes": len(content["attributes"]),
                    "opening_cards": len(content["opening_cards"])},
        "mechanics": mechanics,
        "decisions": {
            "ai_select": {"description": "the production AI choice (LDOpponentPolicy::SelectChoice), rerolls included"},
            "option_index": {"description": "the option at index 0..2 of the actual head offer"},
            "definition": {"description": "the actual head offer's option of one definition; refused when not offered"},
            "preset_offer": {"description": "a three-card head offer prepared by DevQueueOffer from ordinal 2",
                             "sample_classes": ["diagnostic"]},
        },
        "scenarios": {
            "duel_site": {
                "evidence_level": "NativeSpatial",
                "sample_classes": ["legal_search", "diagnostic"],
                "sides": 2,
                "metrics": list(METRICS),
                "parameters": {"site": places, "duel_timeout_ms": [1000, boss_appear_ms],
                               "supply_xp": [0, cumulative_xp(progression)[-1]], "max_choices": progression["max_choices"],
                               "xp_to_next": progression["xp_to_next"]},
                "description": ("A Listen Host of L_Demo runs one ordinary match per Case: the Host only orchestrates; two AI "
                                "contenders grow along their plans from a T=0 XP supply, walk to a public place and fight each "
                                "other with the production engagement until a death or the duel timeout."),
            }
        },
        "metrics": METRICS,
        "limits": {"max_cases_per_run": NATIVE_MAX_CASES},
        "random_streams": {
            "seeded": ["growth offers (MatchSecret)", "critical rolls", "spawn anchors and AI opening plans",
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
        if set(rule) != {"rule"}:
            raise ValueError(f"{where}: ai_select takes nothing else")
        return {"rule": "ai_select"}
    if rule["rule"] == "option_index" and (rule.get("index") not in (0, 1, 2) or "definition" in rule):
        raise ValueError(f"{where}: option_index takes index 0..2")
    if rule["rule"] == "definition" and (not isinstance(rule.get("definition"), str) or "index" in rule):
        raise ValueError(f"{where}: definition takes a definition id")
    if "replace" in rule and rule["replace"] not in SLOTS:
        raise ValueError(f"{where}: replace is Q, W or E")
    return dict(rule)


def _side_plan(plan, role):
    side = plan.get(role)
    if not isinstance(side, dict) or set(side) - {"choices", "presets"} or "choices" not in side:
        raise ValueError(f"plan.{role}: an object with choices (and optional presets)")
    if not isinstance(side["choices"], list):
        raise ValueError(f"plan.{role}.choices: a list")
    choices = [_rule(rule, f"plan.{role}.choices[{index}]") for index, rule in enumerate(side["choices"])]
    presets = side.get("presets", [])
    if not isinstance(presets, list) or any(not isinstance(row, list) or len(row) != 3 or len(set(row)) != 3
                                            or any(not isinstance(card, str) for card in row) for row in presets):
        raise ValueError(f"plan.{role}.presets: lists of three distinct definition ids")
    return choices, presets


def requirements(scenario, plan, caps):
    if set(plan) - set(ROLES):
        raise ValueError(f"plan: only {ROLES}")
    progression_limit = caps["scenarios"]["duel_site"]["parameters"]["max_choices"]
    supply = scenario["parameters"]["supply_xp"]
    mechanics, decisions, problems = set(), set(), []
    for role in ROLES:
        choices, presets = _side_plan(plan, role)
        opportunities = _opportunities(caps, supply[role])
        if len(choices) != opportunities:
            problems.append(f"IllegalDecision: plan.{role} has {len(choices)} rules but its supply of {supply[role]} XP "
                            f"gives {opportunities} opportunities (the opening one included)")
        if len(choices) > progression_limit:
            problems.append(f"IllegalDecision: plan.{role} plans more than {progression_limit} choices")
        for ordinal, rule in enumerate(choices, 1):
            decisions.add(rule["rule"])
            if rule["rule"] != "definition":
                continue
            mechanics.add(rule["definition"])
            row = caps["mechanics"].get(rule["definition"], {})
            kind = row.get("kind")
            if kind and (ordinal == 1) != (kind == "opening"):
                problems.append(f"IllegalDecision: plan.{role} ordinal {ordinal} can never offer {kind} {rule['definition']}"
                                " (the opening choice offers opening cards only, later ones never do)")
            if row.get("draftable") is False:
                problems.append(f"IllegalDecision: {rule['definition']} is not draftable and is never offered")
        if presets:
            decisions.add("preset_offer")
            if len(presets) > max(0, opportunities - 1):
                problems.append(f"IllegalDecision: plan.{role} prepares {len(presets)} offers for {max(0, opportunities - 1)} later opportunities")
        for row in presets:
            for card in row:
                mechanics.add(card)
                if caps["mechanics"].get(card, {}).get("kind") == "opening":
                    problems.append(f"IllegalDecision: plan.{role} presets name opening card {card}; presets start at ordinal 2")
    return {"mechanics": sorted(mechanics), "decisions": sorted(decisions), "problems": problems}


def _opportunities(caps, supply_xp):
    """Opportunities a supply gives: the opening one plus one per level reached (the catalog's XP table)."""
    return level_for({"xp_to_next": caps["scenarios"]["duel_site"]["parameters"]["xp_to_next"]}, supply_xp)


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
        problems.append(f"IllegalScenario: duel_timeout_ms must be {low}..{high} (the Boss appears after that and is not a duelist)")
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
                               "presets": presets})
        out.append({"case_id": case["case_id"], "seed": case["seed"], "site": params["site"],
                    "duel_timeout_ms": params["duel_timeout_ms"], "wall_timeout_s": manifest["budget"]["case_wall_timeout_s"],
                    "contenders": contenders})
    return {"schema": NATIVE_INPUT_SCHEMA, "schema_version": NATIVE_SCHEMA_VERSION, "run_id": run_id,
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


def case_evidence(case, manifest, caps, result, trace_text):
    out = {"status": "Invalid", "reason": "", "metrics": {}, "facts": {}, "violations": []}
    if result.get("schema") != NATIVE_RESULT_SCHEMA or result.get("schema_version") != NATIVE_SCHEMA_VERSION:
        out["reason"] = "the native result has another schema"
        return out
    if result.get("case_id") != case["case_id"] or result.get("seed") != case["seed"]:
        out["reason"] = "the native result belongs to another Case or seed"
        return out
    roles = _roles(result)
    ids = {role: contender for contender, role in roles.items()}
    events = parse_trace(trace_text)
    out["facts"] = facts(case, manifest, result, events, ids)
    if result["status"] != "Completed":
        out.update(status=result["status"], reason=result.get("reason", ""))
        return out
    params = manifest["scenario"]["parameters"]
    t0 = int(result["started_at_ms"])
    outcome = result["outcome"]
    duelists = set(ids.values())
    reasons = []
    if outcome["kind"] == "kill":
        victim = ids.get(outcome["victim"])
        deaths = [fields for tag, fields, _ in events if tag == "LD_DEATH_RECEIPT" and fields.get("contender") == victim]
        lethal = [fields for tag, fields, _ in events if tag == "LD_HIT" and fields.get("victim") == victim and fields.get("lethal") == "1"]
        if not deaths or not lethal:
            reasons.append("TraceIncomplete: the death has no lethal LD_HIT or LD_DEATH_RECEIPT")
            t_out = int(result["ended_at_ms"])
        else:
            t_out = _int(deaths[0], "t")
        if outcome["killer"] not in ROLES or outcome["killer"] == outcome["victim"]:
            reasons.append(f"ExternalKill: the {outcome['victim']} was killed by {outcome['killer'] or 'no contender'}")
    elif outcome["kind"] == "timeout":
        t_out = t0 + int(params["duel_timeout_ms"])
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
        if row["presets_queued"] != row["presets_requested"]:
            reasons.append(f"PresetNotApplied: the {row['role']} queued {row['presets_queued']} of {row['presets_requested']} presets")
    supplied = {role: 0 for role in ROLES}
    for tag, fields, _ in events:
        t = int(fields["t"]) if fields.get("t", "").lstrip("-").isdigit() else None
        if tag == "LD_REWARD" and fields.get("contender") in duelists:
            role = roles[fields["contender"]]
            if fields.get("cause") == "dev_fixture":
                supplied[role] += int(fields["xp"])
            elif t is not None and t < t_out:
                reasons.append(f"UncontrolledXP: the {role} gained {fields.get('xp')} XP from {fields.get('cause')} before the outcome")
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
    for role in ROLES:
        _, presets = _side_plan(plans[case["arm"]], role)
        offered = out["facts"]["offers"][role]
        for position, preset in enumerate(presets):
            if offered.get(str(position + 2)) != preset:
                reasons.append(f"PresetNotApplied: the {role} offer at ordinal {position + 2} is {offered.get(str(position + 2))}, prepared {preset}")
    if reasons:
        out.update(status="Invalid", reason="; ".join(dict.fromkeys(reasons)))
        return out
    subject, opponent = ids["subject"], ids["opponent"]
    dealt = taken = 0
    for tag, fields, _ in events:
        if tag != "LD_HIT" or int(fields["t"]) > t_out:
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


def branch_ordinal(plan):
    """The first subject ordinal whose rule is not the production choice: the decision point of a branch arm."""
    choices, _ = _side_plan(plan, "subject")
    for ordinal, rule in enumerate(choices, 1):
        if rule["rule"] != "ai_select":
            return ordinal
    return None


def facts(case, manifest, result, events, ids):
    offers = {role: {} for role in ROLES}
    rerolled = {role: {} for role in ROLES}
    chosen = {role: {} for role in ROLES}
    roles = {contender: role for role, contender in ids.items()}
    spawn = {}
    for tag, fields, _ in events:
        role = roles.get(fields.get("contender"))
        if tag == "LD_HEARTBEAT" and role and role not in spawn:
            spawn[role] = fields.get("pos")
        if not role:
            continue
        if tag == "LD_OFFER":
            cards = [card.split(":")[1] for card in fields.get("cards", "").split(",") if card.count(":") >= 2]
            target = rerolled if fields.get("rerolled") == "1" else offers
            target[role].setdefault(fields["ordinal"], cards)
        elif tag == "LD_CHOICE":
            chosen[role][fields["ordinal"]] = fields.get("card")
    # One decision point for the whole design: the first ordinal where any arm departs from the production choice, so an
    # arm that keeps the production choice there is compared on the same prefix.
    points = [point for point in (branch_ordinal(arm["plan"]) for arm in manifest["design"]["arms"]) if point]
    point = min(points) if points else None
    label = chosen["subject"].get(str(point), "") if point else ""
    prefix = None
    if point:
        earlier = [f"{ordinal}:{offers['subject'].get(str(ordinal))}>{chosen['subject'].get(str(ordinal))}" for ordinal in range(1, point)]
        prefix = "|".join(earlier + [f"{point}:{offers['subject'].get(str(point))}"])
    fingerprint = hashlib.sha256(json.dumps([offers["subject"].get("1"), offers["opponent"].get("1"), spawn],
                                            sort_keys=True).encode("utf-8")).hexdigest()[:16]
    first_hit = next(({"role": roles[fields["src"]], "t_ms": int(fields["t"]) - int(result.get("started_at_ms", 0))}
                      for tag, fields, _ in events if tag == "LD_HIT" and fields.get("src") in roles
                      and fields.get("victim") in roles and fields.get("hp_lost", "0.00") != "0.00"), None)
    builds = {row["role"]: {"level": row["level"], "opening_card": row["opening_card"],
                            "skills": [f"{skill['id']}@{skill['rank']}{'/' + skill['slot'] if skill['slot'] else ''}" for skill in row["skills"]],
                            "cards": [f"{card['id']}*{card['count']}" for card in row["cards"]]} for row in result["contenders"]}
    return {"offers": offers, "rerolled_offers": rerolled, "choices": chosen, "builds": builds, "label": label,
            "prefix": prefix, "random_fingerprint": fingerprint, "first_hit": first_hit, "spawn": spawn,
            "outcome": result.get("outcome")}


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


def identity_problems(native_run, caps):
    problems = []
    if native_run.get("schema") != NATIVE_RUN_SCHEMA or native_run.get("schema_version") != NATIVE_SCHEMA_VERSION:
        problems.append("the native run record has another schema")
    if native_run.get("content_root", "").lower() != caps["content"]["digest"]:
        problems.append(f"the native run loaded catalog {native_run.get('content_root')}, planned {caps['content']['digest']}")
    if not native_run.get("fixed_time_step"):
        problems.append("the native run did not use a fixed time step; its Cases are not replayable")
    if native_run.get("seed_derivation") != SEED_DERIVATION:
        problems.append(f"the native seed derivation {native_run.get('seed_derivation')} is not {SEED_DERIVATION}")
    return problems


def limitations(manifest, caps):
    out = [
        f"内容为当前运行目录 {caps['content']['version']}（{caps['content']['abilities']} 个技能定义、{caps['content']['attributes']} 张属性卡、"
        f"{caps['content']['opening_cards']} 张开局卡）；目录外的机制一律 UnsupportedMechanic，不代表 v0.11 候选卡已被评估。",
        "对决双方使用同一个固定的 DEV 评估画像：按计划选卡、走到公开地点、只攻击指定对手、不撤退；交战微操是生产 AI 的 Engage。"
        "结果反映固定策略下的机制差异，不代表玩家适应或六类策略画像。",
        "成长由 T=0 的经验供给推动（cause=dev_fixture，经正常奖励复合事务），是计划假设，不证明真实对局中能在同样时间拿到这些经验。",
        "两名 AI 在真实 L_Demo 中移动、寻路与交战（NativeSpatial）；房主是唯一真人，只编排、不参战、不计分；没有拥有连接、网络或渲染证据。",
        "营地、Boss、空投与房主的介入被判为 Invalid 并单列，不计入结果；对决时限不超过 Boss 出现时间。",
        "duel_score 为 0 表示到时未分胜负（删失），不是输；\"谁先被击倒\" 不是旗杆整局胜利。",
    ]
    if manifest["sample_class"] == "diagnostic":
        out.append("本实验用 DevQueueOffer 预置候选：样本是条件实验（diagnostic），不能估计自然抽到率或全局卡牌健康。")
    if manifest["sample_class"] == "legal_search":
        out.append("候选由原生抽卡按 Case 种子真实抽出，选择按计划规则；搜索频率不是自然出现率。")
    return out


def replay_hint(run_dir, case_id):
    return f"重放：`.\\Scripts\\RunBuildEvaluation.ps1 -ReplayRun \"{run_dir}\" -Case {case_id}`"
