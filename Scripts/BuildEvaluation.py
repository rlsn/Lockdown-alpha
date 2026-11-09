"""Build evaluation core: experiment plans, capability admission, Cases, paired statistics, findings and reports.

docs/systems/BUILD_EVALUATION.md describes the system. This module knows no game: no card table, offer shape, slot count,
attribute dimensions or victory rule. A gameplay adapter module (named in the manifest, imported by name) declares what it
can evaluate, lists what each plan arm references, writes the native input, and turns native evidence into per-Case
statuses, metrics and facts. Whatever an adapter does not declare is refused as unsupported, never approximated.

Subcommands (all write only inside the run directory):
  plan         validate a manifest, admit it against the adapter's capabilities, expand Cases, write the native input
  analyze      read native evidence, judge every Case, compute paired statistics, write metrics, findings and report
  replay-plan  a new run of one recorded Case with the same identity, seed and plan
  compare      compare complete replay evidence, terminal state, resources, events and spatial tolerance
"""

import argparse
import hashlib
import importlib
import json
import math
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

MANIFEST_SCHEMA = "build_evaluation.manifest"
CAPABILITIES_SCHEMA = "build_evaluation.capabilities"
SCHEMA_VERSION = 1
CORE_VERSION = 1

EXECUTION_STATUSES = ("NotRun", "Running", "Completed", "Unsupported", "Invalid", "Faulted", "BudgetExceeded", "Interrupted")
EVIDENCE_LEVELS = ("Static", "NativeControlled", "NativeSpatial", "OwnedConnection", "Rendered")
CONCLUSIONS = ("RuleViolation", "BalanceConcern", "NoCounterexampleInCoveredSet", "InsufficientEvidence", "NoTarget")
SAMPLE_CLASSES = ("natural", "legal_search", "diagnostic")
DESIGNS = ("paired_arms", "decision", "factorial_2x2")
METRIC_KINDS = ("score", "continuous", "time_to_event")
DIRECTIONS = ("higher_better", "lower_better", "descriptive")
SPLITS = ("search", "confirm")
ADAPTER_CONTRACT = ("ADAPTER_ID", "ADAPTER_VERSION", "describe", "requirements", "scenario_problems", "native_input",
                    "case_evidence", "canonical_events", "replay_evidence", "identity_problems", "limitations", "replay_hint")

IDENTIFIER = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.-]{0,79}$")
MODULE_NAME = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


class PlanError(Exception):
    """A manifest, capability document or adapter answer that cannot be planned; nothing is started."""


# ---------------------------------------------------------------- files

def read_json(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path, value):
    path = Path(path)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(path)


def write_text(path, text):
    path = Path(path)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(text, encoding="utf-8")
    temporary.replace(path)


def read_cases(run_dir):
    with open(Path(run_dir) / "cases.jsonl", encoding="utf-8") as handle:
        return [json.loads(line) for line in handle if line.strip()]


def write_cases(run_dir, cases):
    write_text(Path(run_dir) / "cases.jsonl",
               "".join(json.dumps(case, ensure_ascii=False, sort_keys=True) + "\n" for case in cases))


def digest_json(value):
    return hashlib.sha256(json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()


# ---------------------------------------------------------------- adapter

def load_adapter(name):
    if not isinstance(name, str) or not MODULE_NAME.match(name):
        raise PlanError(f"adapter.module: '{name}' is not a module name")
    try:
        module = importlib.import_module(name)
    except ImportError as error:
        raise PlanError(f"adapter.module: cannot import '{name}': {error}") from error
    missing = [item for item in ADAPTER_CONTRACT if not hasattr(module, item)]
    if missing:
        raise PlanError(f"adapter '{name}' does not implement the adapter contract: missing {missing}")
    return module


def check_capabilities(caps, adapter):
    if not isinstance(caps, dict) or caps.get("schema") != CAPABILITIES_SCHEMA or caps.get("schema_version") != SCHEMA_VERSION:
        raise PlanError("the adapter's capability document has another schema")
    if caps.get("adapter", {}).get("id") != adapter.ADAPTER_ID:
        raise PlanError("the capability document names another adapter")
    for key in ("content", "mechanics", "decisions", "scenarios", "metrics", "random_streams"):
        if key not in caps:
            raise PlanError(f"the capability document lacks '{key}'")
    for metric_id, metric in caps["metrics"].items():
        if metric.get("kind") not in METRIC_KINDS or metric.get("direction") not in DIRECTIONS:
            raise PlanError(f"capability metric {metric_id} has an unknown kind or direction")
    for kind, scenario in caps["scenarios"].items():
        if scenario.get("evidence_level") not in EVIDENCE_LEVELS:
            raise PlanError(f"capability scenario {kind} has an unknown evidence level")
        unknown = [metric for metric in scenario.get("metrics", []) if metric not in caps["metrics"]]
        if unknown:
            raise PlanError(f"capability scenario {kind} names undeclared metrics {unknown}")


# ---------------------------------------------------------------- manifest

def _only(value, allowed, where):
    if not isinstance(value, dict):
        raise PlanError(f"{where}: an object is required")
    unknown = sorted(set(value) - set(allowed))
    if unknown:
        raise PlanError(f"{where}: unknown fields {unknown}")


def _required(value, key, where, kind):
    if key not in value:
        raise PlanError(f"{where}.{key} is required (no default is assumed)")
    item = value[key]
    if kind is int and (isinstance(item, bool) or not isinstance(item, int)):
        raise PlanError(f"{where}.{key}: an integer is required")
    if kind is not int and not isinstance(item, kind):
        raise PlanError(f"{where}.{key}: {kind.__name__ if isinstance(kind, type) else kind} is required")
    return item


def _identifier(text, where):
    if not isinstance(text, str) or not IDENTIFIER.match(text):
        raise PlanError(f"{where}: '{text}' is not an identifier")
    return text


def validate_manifest(manifest):
    """Structure only; admission against capabilities happens in plan(). Every budget field is explicit."""
    _only(manifest, ("schema", "schema_version", "experiment", "adapter", "scenario", "sample_class", "assumptions",
                     "design", "blocks", "metrics", "budget"), "manifest")
    if manifest.get("schema") != MANIFEST_SCHEMA or manifest.get("schema_version") != SCHEMA_VERSION:
        raise PlanError(f"manifest: schema must be {MANIFEST_SCHEMA}/{SCHEMA_VERSION}")
    experiment = _required(manifest, "experiment", "manifest", dict)
    _only(experiment, ("id", "hypothesis"), "manifest.experiment")
    _identifier(_required(experiment, "id", "manifest.experiment", str), "manifest.experiment.id")
    if not _required(experiment, "hypothesis", "manifest.experiment", str).strip():
        raise PlanError("manifest.experiment.hypothesis must state what is tested")
    adapter = _required(manifest, "adapter", "manifest", dict)
    _only(adapter, ("module", "id"), "manifest.adapter")
    _required(adapter, "module", "manifest.adapter", str)
    _required(adapter, "id", "manifest.adapter", str)
    scenario = _required(manifest, "scenario", "manifest", dict)
    _only(scenario, ("kind", "parameters"), "manifest.scenario")
    _required(scenario, "kind", "manifest.scenario", str)
    _required(scenario, "parameters", "manifest.scenario", dict)
    if manifest.get("sample_class") not in SAMPLE_CLASSES:
        raise PlanError(f"manifest.sample_class must be one of {SAMPLE_CLASSES}")
    assumptions = _required(manifest, "assumptions", "manifest", list)
    if any(not isinstance(item, str) or not item.strip() for item in assumptions):
        raise PlanError("manifest.assumptions: non-empty strings")
    if manifest["sample_class"] != "natural" and not assumptions:
        raise PlanError("manifest.assumptions: a legal_search or diagnostic sample states its supply/preparation assumptions")
    design = _required(manifest, "design", "manifest", dict)
    _only(design, ("kind", "reference", "arms"), "manifest.design")
    if design.get("kind") not in DESIGNS:
        raise PlanError(f"manifest.design.kind must be one of {DESIGNS}")
    arms = _required(design, "arms", "manifest.design", list)
    ids = []
    for index, arm in enumerate(arms):
        where = f"manifest.design.arms[{index}]"
        _only(arm, ("id", "plan", "levels"), where)
        ids.append(_identifier(_required(arm, "id", where, str), f"{where}.id"))
        _required(arm, "plan", where, dict)
        if design["kind"] == "factorial_2x2":
            levels = _required(arm, "levels", where, dict)
            _only(levels, ("first", "second"), f"{where}.levels")
            if levels.get("first") not in ("treatment", "reference") or levels.get("second") not in ("treatment", "reference"):
                raise PlanError(f"{where}.levels: first and second are treatment or reference")
        elif "levels" in arm:
            raise PlanError(f"{where}.levels belongs to a factorial_2x2 design only")
    if len(set(ids)) != len(ids):
        raise PlanError("manifest.design.arms: arm ids are unique")
    if design["kind"] == "paired_arms":
        if len(arms) < 2 or design.get("reference") not in ids:
            raise PlanError("manifest.design: paired_arms needs at least two arms and a reference arm id")
    elif "reference" in design:
        raise PlanError("manifest.design.reference belongs to a paired_arms design only")
    if design["kind"] == "decision" and len(arms) < 2:
        raise PlanError("manifest.design: a decision compares at least two alternatives")
    if design["kind"] == "factorial_2x2":
        cells = sorted((arm["levels"]["first"], arm["levels"]["second"]) for arm in arms)
        if len(arms) != 4 or cells != sorted((a, b) for a in ("reference", "treatment") for b in ("reference", "treatment")):
            raise PlanError("manifest.design: factorial_2x2 needs exactly the four level combinations")
    blocks = _required(manifest, "blocks", "manifest", dict)
    _only(blocks, ("root_seed", "splits", "sides"), "manifest.blocks")
    root = _required(blocks, "root_seed", "manifest.blocks", str)
    if not root.isdigit() or len(root) > 20 or int(root) >= 2 ** 64:
        raise PlanError("manifest.blocks.root_seed: a decimal uint64 string")
    splits = _required(blocks, "splits", "manifest.blocks", dict)
    _only(splits, SPLITS, "manifest.blocks.splits")
    for split in SPLITS:
        count = _required(splits, split, "manifest.blocks.splits", int)
        if count < 0 or count > 1000:
            raise PlanError(f"manifest.blocks.splits.{split}: 0..1000")
    if sum(splits[split] for split in SPLITS) < 1:
        raise PlanError("manifest.blocks.splits: at least one block")
    sides = _required(blocks, "sides", "manifest.blocks", int)
    if sides not in (1, 2):
        raise PlanError("manifest.blocks.sides: 1, or 2 to also run every block with sides swapped")
    metrics = _required(manifest, "metrics", "manifest", dict)
    _only(metrics, ("primary", "secondary", "material_difference"), "manifest.metrics")
    _required(metrics, "primary", "manifest.metrics", str)
    secondary = _required(metrics, "secondary", "manifest.metrics", list)
    if any(not isinstance(item, str) for item in secondary):
        raise PlanError("manifest.metrics.secondary: metric ids")
    if "material_difference" not in metrics:
        raise PlanError("manifest.metrics.material_difference is required: an approved threshold, or null when none is approved")
    threshold = metrics["material_difference"]
    if threshold is not None and (isinstance(threshold, bool) or not isinstance(threshold, (int, float)) or not threshold > 0):
        raise PlanError("manifest.metrics.material_difference: a positive number or null")
    budget = _required(manifest, "budget", "manifest", dict)
    _only(budget, ("max_cases", "case_wall_timeout_s", "run_wall_timeout_s"), "manifest.budget")
    for key, low, high in (("max_cases", 1, 100000), ("case_wall_timeout_s", 10, 3600), ("run_wall_timeout_s", 30, 604800)):
        value = _required(budget, key, "manifest.budget", int)
        if not low <= value <= high:
            raise PlanError(f"manifest.budget.{key}: {low}..{high}")
    return manifest


def block_seed(root_seed, split, index):
    material = f"build-evaluation/v{CORE_VERSION}|{root_seed}|{split}|{index}".encode("utf-8")
    return int.from_bytes(hashlib.sha256(material).digest()[:8], "little")


def expand_blocks(blocks):
    out = []
    seen_seeds = set()
    for split in SPLITS:
        for index in range(blocks["splits"][split]):
            seed = block_seed(blocks["root_seed"], split, index)
            if seed in seen_seeds:
                raise PlanError("block seeds collide; choose another root_seed")
            seen_seeds.add(seed)
            for side in range(blocks["sides"]):
                out.append({"block": f"{split[0]}{index:03d}{'ab'[side]}", "split": split, "index": index,
                            "side": side, "seed": str(seed)})
    search = {item["seed"] for item in out if item["split"] == "search"}
    confirm = {item["seed"] for item in out if item["split"] == "confirm"}
    if search & confirm:
        raise PlanError("search and confirmation seeds collide; choose another root_seed")
    return out


def admit_arm(caps, scenario_caps, manifest, arm, adapter):
    """Unsupported reasons of one arm; an empty list admits it."""
    scenario = manifest["scenario"]
    try:
        needs = adapter.requirements(scenario, arm["plan"], caps)
    except ValueError as error:
        return [f"IllegalPlan: {error}"]
    reasons = list(needs.get("problems", []))
    for mechanic in sorted(set(needs.get("mechanics", []))):
        row = caps["mechanics"].get(mechanic)
        if not row:
            reasons.append(f"UnsupportedMechanic: {mechanic} is not in the adapter's runtime catalog")
        elif row.get("status") != "Supported":
            reasons.append(f"UnsupportedMechanic: {mechanic} ({row.get('status')}: {row.get('reason', '')})")
    for decision in sorted(set(needs.get("decisions", []))):
        row = caps["decisions"].get(decision)
        if row is None:
            reasons.append(f"UnsupportedDecision: {decision}")
        elif row.get("sample_classes") and manifest["sample_class"] not in row["sample_classes"]:
            reasons.append(f"UnsupportedDecision: {decision} is admitted only in {row['sample_classes']} samples")
    return reasons


def plan(manifest_path, run_dir, project_root):
    run_dir = Path(run_dir)
    manifest = validate_manifest(read_json(manifest_path))
    adapter = load_adapter(manifest["adapter"]["module"])
    if manifest["adapter"]["id"] != adapter.ADAPTER_ID:
        raise PlanError(f"manifest.adapter.id {manifest['adapter']['id']} is not the module's adapter {adapter.ADAPTER_ID}")
    caps = adapter.describe(Path(project_root))
    check_capabilities(caps, adapter)
    run_dir.mkdir(parents=True, exist_ok=True)
    blocks = expand_blocks(manifest["blocks"])
    scenario_reasons = []
    scenario_caps = caps["scenarios"].get(manifest["scenario"]["kind"])
    if not scenario_caps:
        scenario_reasons.append(f"UnsupportedScenario: {manifest['scenario']['kind']}")
    else:
        if manifest["sample_class"] not in scenario_caps.get("sample_classes", []):
            scenario_reasons.append(f"UnsupportedSampleClass: {manifest['sample_class']} in {manifest['scenario']['kind']}")
        if manifest["blocks"]["sides"] > scenario_caps.get("sides", 1):
            scenario_reasons.append("UnsupportedSides: the scenario has no swappable sides")
        for metric in [manifest["metrics"]["primary"]] + manifest["metrics"]["secondary"]:
            if metric not in scenario_caps.get("metrics", []):
                scenario_reasons.append(f"UnsupportedMetric: {metric}")
        scenario_reasons += adapter.scenario_problems(manifest["scenario"], caps)
    arms = {}
    for arm in manifest["design"]["arms"]:
        arms[arm["id"]] = scenario_reasons + ([] if scenario_reasons else admit_arm(caps, scenario_caps, manifest, arm, adapter))
    # A design compares arms within blocks: one unsupported arm leaves the comparison without its reference cell.
    design_blocked = any(arms.values())
    cases = []
    for block in blocks:
        for arm in manifest["design"]["arms"]:
            reasons = arms[arm["id"]] or (["DesignIncomplete: another arm of this design is unsupported"] if design_blocked else [])
            cases.append({"case_id": f"{block['block']}-{arm['id']}", "block": block["block"], "split": block["split"],
                          "index": block["index"], "side": block["side"], "seed": block["seed"], "arm": arm["id"],
                          "status": "Unsupported" if reasons else "NotRun", "reason": "; ".join(reasons),
                          "metrics": {}, "facts": {}})
    runnable = [case for case in cases if case["status"] == "NotRun"]
    if len(runnable) > manifest["budget"]["max_cases"]:
        raise PlanError(f"budget: {len(runnable)} runnable Cases exceed max_cases {manifest['budget']['max_cases']}; nothing is started")
    limit = caps.get("limits", {}).get("max_cases_per_run")
    if limit is not None and len(runnable) > limit:
        raise PlanError(f"adapter limit: {len(runnable)} runnable Cases exceed the {limit} one native run accepts; split the plan")
    run_id = run_dir.name
    frozen = dict(manifest)
    frozen["resolved"] = {
        "run_id": run_id, "core_version": CORE_VERSION, "planned_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "adapter": caps["adapter"], "content": caps["content"], "capabilities_sha256": digest_json(caps),
        "evidence_level": scenario_caps["evidence_level"] if scenario_caps else "Static",
        "cases": len(cases), "runnable_cases": len(runnable), "source_manifest": str(manifest_path),
        "project_root": str(Path(project_root).resolve()),
    }
    write_json(run_dir / "manifest.json", frozen)
    write_json(run_dir / "capabilities.json", caps)
    write_cases(run_dir, cases)
    write_json(run_dir / "coverage.json", coverage(frozen, cases, arms))
    if runnable:
        write_json(run_dir / "native_input.json", adapter.native_input(run_id, frozen, caps, runnable))
    return frozen, cases


def coverage(manifest, cases, admission=None):
    by_status = {status: 0 for status in EXECUTION_STATUSES}
    for case in cases:
        by_status[case["status"]] += 1
    arms = {}
    for arm in manifest["design"]["arms"]:
        rows = [case for case in cases if case["arm"] == arm["id"]]
        arms[arm["id"]] = {"cases": len(rows), "by_status": {status: sum(1 for case in rows if case["status"] == status)
                                                             for status in EXECUTION_STATUSES if any(case["status"] == status for case in rows)}}
        if admission is not None:
            arms[arm["id"]]["admission"] = admission[arm["id"]] or ["Supported"]
    return {"requested": len(cases), "by_status": by_status, "arms": arms,
            "denominator_note": ("Only Completed Cases enter statistics. Inference uses independent seeds, averaging the "
                                 "requested sides within each complete seed; incomplete seeds are excluded.") }


# ---------------------------------------------------------------- statistics

_T975 = {1: 12.706, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571, 6: 2.447, 7: 2.365, 8: 2.306, 9: 2.262, 10: 2.228,
         11: 2.201, 12: 2.179, 13: 2.160, 14: 2.145, 15: 2.131, 16: 2.120, 17: 2.110, 18: 2.101, 19: 2.093, 20: 2.086,
         21: 2.080, 22: 2.074, 23: 2.069, 24: 2.064, 25: 2.060, 26: 2.056, 27: 2.052, 28: 2.048, 29: 2.045, 30: 2.042}
_T975_TAIL = ((30, 2.042), (40, 2.021), (60, 2.000), (120, 1.980), (math.inf, 1.960))


def t975(df):
    """Two-sided 95% Student t quantile; table values, linear in 1/df beyond 30."""
    if df < 1:
        raise ValueError("df >= 1")
    if df <= 30:
        return _T975[int(df)]
    for (low, low_t), (high, high_t) in zip(_T975_TAIL, _T975_TAIL[1:]):
        if df <= high:
            position = (1 / low - 1 / df) / (1 / low - (0 if high == math.inf else 1 / high))
            return low_t + (high_t - low_t) * position
    return 1.960


def summarize(values):
    """Mean with a Student t interval. Identical values give no interval: a zero spread bounds nothing (degenerate)."""
    values = [float(value) for value in values]
    n = len(values)
    out = {"n": n, "mean": None, "sd": None, "ci95": None, "degenerate": False, "min": None, "max": None}
    if not n:
        return out
    mean = sum(values) / n
    out.update(mean=mean, min=min(values), max=max(values))
    if n >= 2:
        sd = math.sqrt(sum((value - mean) ** 2 for value in values) / (n - 1))
        out["sd"] = sd
        if sd == 0:
            out["degenerate"] = True
        else:
            half = t975(n - 1) * sd / math.sqrt(n)
            out["ci95"] = [mean - half, mean + half]
    return out


def wilson95(successes, n):
    """Two-sided 95% Wilson score interval of a proportion; None without observations."""
    if n <= 0:
        return None
    z = 1.959964
    p = successes / n
    centre = (p + z * z / (2 * n)) / (1 + z * z / n)
    half = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / (1 + z * z / n)
    return [max(0.0, centre - half), min(1.0, centre + half)]


def metric_value(case, metric):
    row = case.get("metrics", {}).get(metric)
    if case["status"] != "Completed" or row is None or row.get("value") is None:
        return None
    return float(row["value"])


def blocks_of(cases, split):
    out = {}
    for case in cases:
        if case["split"] == split:
            out.setdefault(case["block"], {})[case["arm"]] = case
    return out


def clusters_of(cases, split):
    """Keep side-specific execution blocks together under their shared independent seed."""
    out = {}
    for case in cases:
        if case["split"] != split:
            continue
        key = f"{split[0]}{case['index']:03d}"
        cluster = out.setdefault(key, {"seed": case["seed"], "sides": {}})
        if cluster["seed"] != case["seed"]:
            raise PlanError(f"seed cluster {key} contains different seeds")
        row = cluster["sides"].setdefault(case["side"], {})
        if case["arm"] in row:
            raise PlanError(f"seed cluster {key} repeats side {case['side']} arm {case['arm']}")
        row[case["arm"]] = case
    return out


def cluster_values(cluster, arms, metric, sides):
    """An estimate uses the same complete set of sides and arms, or excludes the entire seed."""
    values = {arm: [] for arm in arms}
    for side in range(sides):
        row = cluster["sides"].get(side, {})
        for arm in arms:
            value = metric_value(row[arm], metric) if arm in row else None
            if value is None:
                return None
            values[arm].append(value)
    return {arm: sum(items) / sides for arm, items in values.items()}


def arm_summaries(clusters, arms, metric, kind, sides):
    out = {}
    for arm in arms:
        values, included, excluded = [], [], []
        for key, cluster in sorted(clusters.items()):
            value = cluster_values(cluster, [arm], metric, sides)
            if value is None:
                excluded.append(key)
                continue
            values.append(value[arm])
            included.extend(cluster["sides"][side][arm] for side in range(sides))
        summary = summarize(values)
        summary["censored"] = sum(1 for case in included if case.get("metrics", {}).get(metric, {}).get("censored"))
        summary["cases"] = len(included)
        summary["excluded_blocks"] = excluded
        if kind == "time_to_event":
            summary["note"] = "restricted mean: a censored Case counts at its horizon before averaging sides"
        out[arm] = summary
    return out


def paired(clusters, treatment, reference, metric, sides, blocked=()):
    diffs, used, excluded = [], [], []
    for block, cluster in sorted(clusters.items()):
        values = cluster_values(cluster, [treatment, reference], metric, sides)
        if values is None or block in blocked:
            excluded.append(block)
            continue
        diffs.append(values[treatment] - values[reference])
        used.append(block)
    out = summarize(diffs)
    nonzero = sum(1 for diff in diffs if diff != 0)
    out.update(treatment=treatment, reference=reference, blocks=used, excluded_blocks=excluded,
               differences=dict(zip(used, diffs)), nonzero_blocks=nonzero, nonzero_share_ci95=wilson95(nonzero, len(diffs)))
    return out


def decision_analysis(clusters, arms, metric, direction, threshold, sides, blocked=()):
    if direction not in ("higher_better", "lower_better"):
        raise ValueError(f"decision analysis requires a favourable direction, not {direction!r}")
    sign = -1.0 if direction == "lower_better" else 1.0
    regret = {arm: [] for arm in arms}
    best_share = {arm: 0.0 for arm in arms}
    spreads, rows = [], []
    for block, cluster in sorted(clusters.items()):
        values = cluster_values(cluster, arms, metric, sides)
        if values is None or block in blocked:
            continue
        scored = {arm: sign * value for arm, value in values.items()}
        top = max(scored.values())
        winners = [arm for arm, value in scored.items() if value == top]
        for arm in arms:
            regret[arm].append(top - scored[arm])
            if arm in winners:
                best_share[arm] += 1.0 / len(winners)
        spread = top - min(scored.values())
        spreads.append(spread)
        labels = {}
        for arm in arms:
            side_labels = [cluster["sides"][side][arm].get("facts", {}).get("label") or "" for side in range(sides)]
            labels[arm] = (side_labels[0] if sides == 1 else
                           " / ".join(f"{'ab'[side]}:{label}" for side, label in enumerate(side_labels)))
        rows.append({"block": block, "values": values, "best": winners, "spread": spread, "labels": labels})
    n = len(rows)
    out = {"blocks": n, "rows": rows,
           "mean_regret": {arm: (sum(values) / n if n else None) for arm, values in regret.items()},
           "best_share": {arm: (share / n if n else None) for arm, share in best_share.items()},
           "share_blocks_choice_matters": (sum(1 for spread in spreads if spread > 0) / n) if n else None,
           "spread": summarize(spreads)}
    out["share_blocks_spread_at_least_threshold"] = (
        sum(1 for spread in spreads if spread >= threshold) / n if n and threshold is not None else None)
    return out


def interaction(clusters, arms_by_cell, metric, sides):
    tt, tr, rt, rr = (arms_by_cell[cell] for cell in (("treatment", "treatment"), ("treatment", "reference"),
                                                      ("reference", "treatment"), ("reference", "reference")))
    terms, used, excluded = [], [], []
    for block, cluster in sorted(clusters.items()):
        values = cluster_values(cluster, [tt, tr, rt, rr], metric, sides)
        if values is None:
            excluded.append(block)
            continue
        terms.append(values[tt] - values[tr] - values[rt] + values[rr])
        used.append(block)
    out = summarize(terms)
    nonzero = sum(1 for term in terms if term != 0)
    out.update(formula=f"V({tt}) - V({tr}) - V({rt}) + V({rr})", blocks=used, excluded_blocks=excluded,
               terms=dict(zip(used, terms)), nonzero_blocks=nonzero, nonzero_share_ci95=wilson95(nonzero, len(terms)))
    return out


def judge(estimate, threshold):
    """A conclusion for one confirmed contrast; without an approved threshold there is no target to pass or fail.

    A degenerate interval (every block gave the same difference) bounds nothing and is never judged against a threshold.
    """
    if estimate["n"] < 2 or (estimate["ci95"] is None and not estimate.get("degenerate")):
        return "InsufficientEvidence"
    if threshold is None:
        return "NoTarget"
    if estimate.get("degenerate"):
        return "InsufficientEvidence"
    low, high = estimate["ci95"]
    if low > threshold or high < -threshold:
        return "BalanceConcern"
    if -threshold <= low and high <= threshold:
        return "NoCounterexampleInCoveredSet"
    return "InsufficientEvidence"


# ---------------------------------------------------------------- analysis

def analyze(run_dir, rejudge=False):
    run_dir = Path(run_dir)
    manifest = read_json(run_dir / "manifest.json")
    caps = read_json(run_dir / "capabilities.json")
    adapter = load_adapter(manifest["adapter"]["module"])
    cases = read_cases(run_dir)
    evidence_adapter = {"id": adapter.ADAPTER_ID, "version": adapter.ADAPTER_VERSION}
    recorded_adapter = caps.get("adapter")
    if (not isinstance(recorded_adapter, dict) or recorded_adapter.get("id") != adapter.ADAPTER_ID
            or manifest["adapter"]["id"] != adapter.ADAPTER_ID):
        raise PlanError("the recorded evidence belongs to another adapter; --rejudge only accepts the same adapter id")

    def same_adapter_version(record):
        return isinstance(record, dict) and all(record.get(key) == value for key, value in evidence_adapter.items())

    if not rejudge:
        stale = any(case["status"] == "Completed" and not same_adapter_version(case.get("evidence_adapter")) for case in cases)
        unjudged = any(case["status"] == "NotRun" for case in cases) and not same_adapter_version(recorded_adapter)
        if stale or unjudged:
            raise PlanError("the adapter changed since this evidence was recorded or judged; use --rejudge to re-evaluate it")
    if rejudge:
        # Judge the recorded native evidence again (an updated adapter); admission refusals stay as planned.
        for case in cases:
            if case["status"] != "Unsupported":
                case.update(status="NotRun", reason="", metrics={}, facts={}, violations=[])
                case.pop("evidence_adapter", None)
    native_dir = run_dir / "native"
    run_record = read_json(native_dir / "native_run.json") if (native_dir / "native_run.json").exists() else None
    runner = read_json(run_dir / "runner.json") if (run_dir / "runner.json").exists() else {}
    identity_problems = adapter.identity_problems(run_record, caps) if run_record else []
    if not run_record and any((native_dir / "results" / f"{case['case_id']}.json").exists()
                              for case in cases if case["status"] != "Unsupported"):
        identity_problems.append("the native run identity is missing")
    if run_record and ("binary_digests" in runner or "binary_digests_after" in runner):
        if not runner.get("binary_digests") or runner["binary_digests"] != runner.get("binary_digests_after"):
            identity_problems.append("the recorded binaries are missing or changed during the native run")
    for case in cases:
        if identity_problems and case["status"] != "Unsupported":
            case.update(status="Invalid", reason="; ".join(identity_problems), metrics={}, facts={})
            continue
        if case["status"] != "NotRun":
            continue
        result_path = native_dir / "results" / f"{case['case_id']}.json"
        if not result_path.exists():
            detail = (run_record or {}).get("detail") or runner.get("detail") or "no native result"
            case.update(status="Interrupted", reason=f"no native result ({detail})")
            continue
        result = read_json(result_path)
        trace_path = native_dir / result.get("trace", f"traces/{case['case_id']}.log")
        trace = trace_path.read_text(encoding="utf-8") if trace_path.exists() else ""
        if identity_problems:
            case.update(status="Invalid", reason="; ".join(identity_problems))
            continue
        evidence = adapter.case_evidence(case, manifest, caps, result, trace)
        if evidence.get("status") not in EXECUTION_STATUSES or evidence["status"] in ("NotRun", "Running", "Unsupported"):
            raise PlanError(f"adapter returned status {evidence.get('status')} for {case['case_id']}")
        # Evidence-contract violations stay with their Case, so a later analysis reports them again.
        case.update(status=evidence["status"], reason=evidence.get("reason", ""), metrics=evidence.get("metrics", {}),
                    facts=evidence.get("facts", {}), violations=list(evidence.get("violations", [])),
                    evidence_adapter=evidence_adapter)
    violations = [{"case": case["case_id"], "detail": item} for case in cases for item in case.get("violations", [])]
    write_cases(run_dir, cases)
    write_json(run_dir / "coverage.json", coverage(manifest, cases))
    metrics = compute_metrics(manifest, caps, cases)
    evidence_versions = sorted({str(case.get("evidence_adapter", {}).get("version", "unknown"))
                                for case in cases if case["status"] == "Completed"})
    metrics["analysis"] = {"adapter": {"id": adapter.ADAPTER_ID, "version": adapter.ADAPTER_VERSION},
                           "completed_evidence_versions": evidence_versions}
    findings = compute_findings(manifest, caps, cases, metrics, violations, identity_problems, run_record, runner)
    write_json(run_dir / "metrics.json", metrics)
    write_json(run_dir / "findings.json", findings)
    write_text(run_dir / "report.md", render_report(run_dir, manifest, caps, cases, metrics, findings, adapter, run_record))
    return metrics, findings


def compute_metrics(manifest, caps, cases):
    design = manifest["design"]
    arms = [arm["id"] for arm in design["arms"]]
    names = [manifest["metrics"]["primary"]] + manifest["metrics"]["secondary"]
    threshold = manifest["metrics"]["material_difference"]
    sides = manifest["blocks"]["sides"]
    scenario = caps["scenarios"].get(manifest["scenario"]["kind"], {})
    out = {"schema": "build_evaluation.metrics", "schema_version": SCHEMA_VERSION, "design": design["kind"],
           "primary": manifest["metrics"]["primary"], "material_difference": threshold,
           "inference_unit": "seed_mean_over_complete_sides", "splits": {}}
    for split in SPLITS:
        rows = [case for case in cases if case["split"] == split]
        if not rows:
            continue
        by_block = blocks_of(rows, split)
        clusters = clusters_of(rows, split)
        blocked = set()
        if design["kind"] == "decision":
            for cluster_id, cluster in clusters.items():
                for row in cluster["sides"].values():
                    prefixes = {case["facts"].get("prefix") for case in row.values() if case["status"] == "Completed"}
                    if len(prefixes) > 1:
                        blocked.add(cluster_id)
        entry = {"blocks": len(clusters), "execution_blocks": len(by_block), "sides_per_seed": sides, "metrics": {}}
        for name in names:
            spec = caps["metrics"].get(name)
            if not spec or name not in scenario.get("metrics", []):
                entry["metrics"][name] = {"supported": False, "reason": f"UnsupportedMetric: {name}"}
                continue
            metric = {"supported": True, "unit": spec.get("unit"), "kind": spec["kind"], "direction": spec["direction"],
                      "arms": arm_summaries(clusters, arms, name, spec["kind"], sides)}
            if design["kind"] == "paired_arms":
                metric["contrasts"] = [paired(clusters, arm, design["reference"], name, sides)
                                       for arm in arms if arm != design["reference"]]
            elif design["kind"] == "decision":
                metric["contrasts"] = [paired(clusters, b, a, name, sides, blocked)
                                       for i, a in enumerate(arms) for b in arms[i + 1:]]
                if spec["direction"] in ("higher_better", "lower_better"):
                    metric["decision"] = decision_analysis(clusters, arms, name, spec["direction"],
                                                           threshold if name == manifest["metrics"]["primary"] else None, sides, blocked)
            else:
                cells = {(arm["levels"]["first"], arm["levels"]["second"]): arm["id"] for arm in design["arms"]}
                metric["interaction"] = interaction(clusters, cells, name, sides)
                metric["contrasts"] = [paired(clusters, cells[("treatment", second)], cells[("reference", second)], name, sides)
                                       for second in ("reference", "treatment")]
            entry["metrics"][name] = metric
        out["splits"][split] = entry
    return out


def compute_findings(manifest, caps, cases, metrics, violations, identity_problems, run_record, runner):
    findings = []
    primary = manifest["metrics"]["primary"]
    threshold = manifest["metrics"]["material_difference"]
    scope = "在本次覆盖的构筑、策略、场景与预算内"

    def add(conclusion, title, **rest):
        findings.append({"id": f"F{len(findings) + 1:03d}", "conclusion": conclusion, "title": title, "scope": scope, **rest})

    for problem in identity_problems:
        add("RuleViolation", "运行身份与计划不符，全部 Case 无效", detail=problem)
    for item in violations:
        add("RuleViolation", "证据合同被违反", detail=item["detail"], evidence=[item["case"]])
    for fault in runner.get("log_errors", []):
        add("RuleViolation", "运行日志出现错误或权威故障", detail=fault)
    # Branches of one decision must share the prefix before the branch point; a mismatch makes the comparison unpaired.
    if manifest["design"]["kind"] == "decision":
        for split in SPLITS:
            for block, row in sorted(blocks_of(cases, split).items()):
                prefixes = {case["facts"].get("prefix") for case in row.values() if case["status"] == "Completed"}
                if len(prefixes) > 1:
                    add("RuleViolation", "同一决策块的分支前缀不一致（不是同一真实 Offer）", evidence=sorted(case["case_id"] for case in row.values()))
    fingerprints = {}
    for case in cases:
        if case["status"] == "Completed" and case["facts"].get("random_fingerprint"):
            fingerprints.setdefault(case["arm"], {}).setdefault(case["seed"], []).append(
                (case["side"], case["facts"]["random_fingerprint"]))
    for arm, seen in fingerprints.items():
        if len(seen) > 1 and len({tuple(sorted(values)) for values in seen.values()}) == 1:
            add("RuleViolation", f"更换种子没有改变 {arm} 的随机结果", evidence=sorted(seen))
    not_completed = [case for case in cases if case["status"] not in ("Completed", "Unsupported")]
    if not_completed:
        add("InsufficientEvidence", "部分可运行 Case 没有得到有效结果，已从分母排除",
            detail={status: sum(1 for case in not_completed if case["status"] == status)
                    for status in sorted({case["status"] for case in not_completed})},
            evidence=[f"{case['case_id']}: {case['status']} {case['reason']}" for case in not_completed])
    unsupported = [case for case in cases if case["status"] == "Unsupported"]
    if unsupported:
        reasons = sorted({case["reason"] for case in unsupported})
        add("InsufficientEvidence", "部分方案不被当前玩法 Adapter 支持，没有运行", detail=reasons)
    confirm = metrics["splits"].get("confirm")
    if not confirm:
        if metrics["splits"]:
            add("InsufficientEvidence", "没有确认集：搜索集结果只作探索，不构成结论")
        return {"schema": "build_evaluation.findings", "schema_version": SCHEMA_VERSION, "findings": findings}
    metric = confirm["metrics"][primary]
    if not metric.get("supported"):
        if not unsupported:
            add("InsufficientEvidence", "主指标不受场景支持，不能产生统计结论", detail=metric["reason"])
        return {"schema": "build_evaluation.findings", "schema_version": SCHEMA_VERSION, "findings": findings}
    direction = metric["direction"]
    sign = {"higher_better": 1.0, "lower_better": -1.0}.get(direction)
    estimates = []
    if "interaction" in metric:
        estimates.append(("interaction", metric["interaction"], metric["interaction"].get("terms", {})))
    for contrast in metric.get("contrasts", []):
        estimates.append((f"{contrast['treatment']} - {contrast['reference']}", contrast, contrast.get("differences", {})))
    for label, estimate, per_block in estimates:
        conclusion = judge(estimate, threshold)
        reproduce = []
        if per_block:
            block = max(per_block, key=lambda key: abs(per_block[key]))
            reproduce = sorted(case["case_id"] for case in cases if f"c{case['index']:03d}" == block and case["split"] == "confirm"
                               and case["status"] == "Completed")
        add(conclusion, f"确认集 {primary}: {label}", metric=primary, estimate=estimate["mean"], ci95=estimate["ci95"],
            degenerate=estimate.get("degenerate", False), nonzero_blocks=estimate.get("nonzero_blocks"),
            nonzero_share_ci95=estimate.get("nonzero_share_ci95"),
            n=estimate["n"], excluded_blocks=estimate.get("excluded_blocks", []), direction=direction,
            favourable_sign=sign, minimal_reproduction=reproduce)
    return {"schema": "build_evaluation.findings", "schema_version": SCHEMA_VERSION, "findings": findings}


def _number(value, digits=3):
    if value is None:
        return "—"
    if isinstance(value, float):
        return f"{value:.{digits}f}".rstrip("0").rstrip(".") if not value.is_integer() else str(int(value))
    return str(value)


def _interval(ci, degenerate=False):
    if degenerate:
        return "退化（各种子相同，不构成区间）"
    return "—" if not ci else f"[{_number(ci[0])}, {_number(ci[1])}]"


def _share(contrast):
    bound = contrast.get("nonzero_share_ci95")
    if bound is None:
        return "—"
    return f"{contrast['nonzero_blocks']}/{contrast['n']} [{_number(bound[0])}, {_number(bound[1])}]"


def render_report(run_dir, manifest, caps, cases, metrics, findings, adapter, run_record):
    resolved = manifest["resolved"]
    lines = [f"# 构筑评估报告：{manifest['experiment']['id']}", "",
             f"**结论范围：在本次覆盖的构筑、策略、场景与预算内。** 有限搜索不能证明不存在其他更强组合；未发现反例只说明已覆盖范围。",
             "", f"假设：{manifest['experiment']['hypothesis']}", "", "## 身份与输入", "",
             "| 项 | 值 |", "| --- | --- |",
             f"| 运行 | `{resolved['run_id']}` |",
             f"| 录制 Adapter | `{resolved['adapter']['id']}` v{resolved['adapter'].get('version')} |",
             f"| 判读 Adapter | 当前 v{metrics['analysis']['adapter']['version']}；完成 Case 的证据判读版本 "
             f"{', '.join(metrics['analysis']['completed_evidence_versions']) or '无'} |",
             f"| 内容 | {resolved['content'].get('id')} {resolved['content'].get('version', '')} `{resolved['content'].get('digest', '')}` |",
             f"| 能力表 | `{resolved['capabilities_sha256']}` |",
             f"| 场景 / 证据级别 | `{manifest['scenario']['kind']}` / {resolved['evidence_level']} |",
             f"| 样本类别 | {manifest['sample_class']} |",
             f"| 设计 | {manifest['design']['kind']}，{len(manifest['design']['arms'])} 个 arm |",
             f"| 种子 | root `{manifest['blocks']['root_seed']}`；搜索 {manifest['blocks']['splits']['search']} 个，确认 {manifest['blocks']['splits']['confirm']} 个；每个种子 {manifest['blocks']['sides']} 侧 |",
             f"| 主指标 / 实质差异阈值 | `{manifest['metrics']['primary']}` / {_number(manifest['metrics']['material_difference']) if manifest['metrics']['material_difference'] is not None else '未批准（证据足够时为 NoTarget）'} |",
             f"| 预算 | 最多 {manifest['budget']['max_cases']} 个 Case；单 Case {manifest['budget']['case_wall_timeout_s']} s；整次 {manifest['budget']['run_wall_timeout_s']} s |"]
    if run_record:
        lines += [f"| 原生运行 | 源码 `{run_record.get('source_commit')}`（dirty {run_record.get('source_dirty')}），产品 {run_record.get('product_version')}，"
                  f"引擎 {run_record.get('engine_version')}，固定步长 {run_record.get('fixed_time_step')} ({_number(run_record.get('fixed_delta_seconds'), 5)} s)，"
                  f"{_number(run_record.get('wall_seconds'), 1)} s |"]
    lines += ["", "假设与准备：", ""] + [f"- {item}" for item in manifest["assumptions"]] + [""]
    lines += ["## 覆盖与执行状态", "", "| arm | Case | 状态 |", "| --- | --- | --- |"]
    for arm in manifest["design"]["arms"]:
        rows = [case for case in cases if case["arm"] == arm["id"]]
        counts = ", ".join(f"{status} {sum(1 for case in rows if case['status'] == status)}"
                           for status in EXECUTION_STATUSES if any(case["status"] == status for case in rows))
        lines.append(f"| `{arm['id']}` | {len(rows)} | {counts} |")
    excluded = [case for case in cases if case["status"] not in ("Completed",)]
    if excluded:
        lines += ["", "未进入统计的 Case：", ""]
        lines += [f"- `{case['case_id']}` {case['status']}：{case['reason']}" for case in excluded[:60]]
        if len(excluded) > 60:
            lines.append(f"- …另 {len(excluded) - 60} 个，见 cases.jsonl")
    lines += [""]
    for split, entry in metrics["splits"].items():
        tag = "确认集" if split == "confirm" else "搜索集（探索，不构成结论）"
        lines += [f"## {tag}：{entry['blocks']} 个独立种子，{entry['execution_blocks']} 个执行侧", "",
                  "同一种子的各侧先取平均再统计；缺任一所需侧的种子不进入该项分母。"]
        for name, metric in entry["metrics"].items():
            if not metric.get("supported"):
                lines += ["", f"### `{name}`", "", f"未支持：{metric['reason']}，没有统计。", ""]
                continue
            lines += [f"### `{name}`（{metric['unit']}，{metric['kind']}，{metric['direction']}）", "",
                      "| arm | 独立种子 n | 均值 | 95% 区间 | 删失 Case |", "| --- | --- | --- | --- | --- |"]
            for arm, summary in metric["arms"].items():
                lines.append(f"| `{arm}` | {summary['n']} | {_number(summary['mean'])} | {_interval(summary['ci95'], summary.get('degenerate'))} | {summary['censored']} |")
            if metric["kind"] == "time_to_event":
                lines.append("")
                lines.append("删失 Case 按其时限计入（受限均值），不是事件时间。")
            if metric.get("contrasts"):
                lines += ["", "| 成对比较 | 种子 | 平均差 | 95% 区间 | 平均差非零的种子（95% Wilson 区间） | 排除的种子 |",
                          "| --- | --- | --- | --- | --- | --- |"]
                for contrast in metric["contrasts"]:
                    lines.append(f"| `{contrast['treatment']}` − `{contrast['reference']}` | {contrast['n']} | {_number(contrast['mean'])} | "
                                 f"{_interval(contrast['ci95'], contrast.get('degenerate'))} | {_share(contrast)} | {len(contrast['excluded_blocks'])} |")
            if metric.get("interaction"):
                term = metric["interaction"]
                lines += ["", f"交互 `{term['formula']}`：{term['n']} 个种子，均值 {_number(term['mean'])}，"
                          f"95% 区间 {_interval(term['ci95'], term.get('degenerate'))}，非零 {_share(term)}。"]
            if metric.get("decision"):
                decision = metric["decision"]
                lines += ["", f"决策：{decision['blocks']} 个完整种子；各侧平均后选项有差异的种子占比 {_number(decision['share_blocks_choice_matters'])}；"
                          f"差距均值 {_number(decision['spread']['mean'])}。", "",
                          "| arm | 平均后悔值 | 最佳占比 |", "| --- | --- | --- |"]
                for arm in decision["mean_regret"]:
                    lines.append(f"| `{arm}` | {_number(decision['mean_regret'][arm])} | {_number(decision['best_share'][arm])} |")
                if decision["rows"]:
                    lines += ["", "| 块 | " + " | ".join(f"`{arm}`" for arm in decision["mean_regret"]) + " | 最佳 |",
                              "| --- | " + " | ".join("---" for _ in decision["mean_regret"]) + " | --- |"]
                    for row in decision["rows"]:
                        cells = [f"{_number(row['values'][arm])} {row['labels'].get(arm) or ''}".strip() for arm in decision["mean_regret"]]
                        lines.append(f"| {row['block']} | " + " | ".join(cells) + f" | {', '.join(row['best'])} |")
            lines.append("")
    lines += ["## 发现", ""]
    if not findings["findings"]:
        lines.append("没有发现。")
    for finding in findings["findings"]:
        lines.append(f"- **{finding['id']} {finding['conclusion']}**：{finding['title']}")
        if "estimate" in finding:
            lines.append(f"  - 估计 {_number(finding['estimate'])}，95% 区间 {_interval(finding['ci95'], finding.get('degenerate'))}，"
                         f"n={finding['n']} 个种子，平均差非零的种子 {finding.get('nonzero_blocks')}（95% Wilson 区间 {_interval(finding.get('nonzero_share_ci95'))}）")
        if finding.get("detail"):
            lines.append(f"  - 详情：{finding['detail']}")
        if finding.get("evidence"):
            lines.append(f"  - 证据：{', '.join(str(item) for item in finding['evidence'][:12])}")
        if finding.get("minimal_reproduction"):
            lines.append(f"  - 最小复现：{', '.join(finding['minimal_reproduction'])}；"
                         f"{adapter.replay_hint(str(run_dir), finding['minimal_reproduction'][0])}")
    lines += ["", "## 限制", ""] + [f"- {item}" for item in adapter.limitations(manifest, caps)]
    lines += ["- 未批准的平衡目标不填临时阈值；没有趣味性总分。", ""]
    return "\n".join(lines)


# ---------------------------------------------------------------- replay

def replay_plan(source_dir, case_id, run_dir):
    source_dir, run_dir = Path(source_dir), Path(run_dir)
    manifest = read_json(source_dir / "manifest.json")
    caps_then = read_json(source_dir / "capabilities.json")
    adapter = load_adapter(manifest["adapter"]["module"])
    cases = [case for case in read_cases(source_dir) if case["case_id"] == case_id]
    if not cases:
        raise PlanError(f"case {case_id} is not in {source_dir}")
    case = dict(cases[0])
    if case["status"] == "Unsupported":
        raise PlanError(f"case {case_id} was never runnable: {case['reason']}")
    caps = adapter.describe(Path(manifest["resolved"]["project_root"]))
    if digest_json(caps) != digest_json(caps_then):
        raise PlanError("the adapter's capabilities changed since the source run; a replay compares like with like only")
    run_dir.mkdir(parents=True, exist_ok=True)
    frozen = dict(manifest)
    frozen["resolved"] = dict(manifest["resolved"], run_id=run_dir.name, replay_of={"run": str(source_dir), "case": case_id},
                              planned_at=datetime.now(timezone.utc).isoformat(timespec="seconds"), cases=1, runnable_cases=1)
    case.update(status="NotRun", reason="", metrics={}, facts={}, violations=[])
    write_json(run_dir / "manifest.json", frozen)
    write_json(run_dir / "capabilities.json", caps)
    write_cases(run_dir, [case])
    write_json(run_dir / "coverage.json", coverage(frozen, [case]))
    write_json(run_dir / "native_input.json", adapter.native_input(run_dir.name, frozen, caps, [case]))
    return frozen, case


def _project_root():
    return Path(__file__).resolve().parents[1]


def first_difference(source, replay, path="$"):
    if isinstance(source, dict) and isinstance(replay, dict):
        for key in sorted(set(source) | set(replay)):
            if key not in source or key not in replay:
                return {"path": f"{path}.{key}", "source": source.get(key), "replay": replay.get(key)}
            difference = first_difference(source[key], replay[key], f"{path}.{key}")
            if difference:
                return difference
    elif isinstance(source, list) and isinstance(replay, list):
        for index in range(max(len(source), len(replay))):
            if index >= len(source) or index >= len(replay):
                return {"path": f"{path}[{index}]", "source": source[index] if index < len(source) else None,
                        "replay": replay[index] if index < len(replay) else None}
            difference = first_difference(source[index], replay[index], f"{path}[{index}]")
            if difference:
                return difference
    elif type(source) is not type(replay) or source != replay:
        return {"path": path, "source": source, "replay": replay}
    return None


def compare(source_dir, replay_dir, case_id, spatial_tolerance_cm):
    source_dir, replay_dir = Path(source_dir), Path(replay_dir)
    if not math.isfinite(spatial_tolerance_cm) or spatial_tolerance_cm < 0:
        raise PlanError("spatial tolerance must be a finite non-negative number")
    manifest = read_json(source_dir / "manifest.json")
    adapter = load_adapter(manifest["adapter"]["module"])

    def load(run_dir):
        frozen = read_json(run_dir / "manifest.json")
        caps = read_json(run_dir / "capabilities.json")
        matches = [case for case in read_cases(run_dir) if case["case_id"] == case_id]
        if len(matches) != 1:
            raise PlanError(f"{run_dir} must contain exactly one planned Case {case_id}")
        case = matches[0]
        problems = []
        if digest_json(caps) != frozen["resolved"]["capabilities_sha256"]:
            problems.append("the capability document does not match the recorded digest")
        recorded_adapter = caps.get("adapter", {})
        if (recorded_adapter != frozen["resolved"].get("adapter") or recorded_adapter.get("id") != adapter.ADAPTER_ID
                or recorded_adapter.get("version") != adapter.ADAPTER_VERSION):
            problems.append("the adapter identity or version differs from the recorded capabilities")
        result_path = run_dir / "native" / "results" / f"{case_id}.json"
        if not result_path.exists():
            raise PlanError(f"{run_dir} has no native result for {case_id}")
        native_run_path = run_dir / "native" / "native_run.json"
        if not native_run_path.exists():
            raise PlanError(f"{run_dir} has no native run identity")
        native_run = read_json(native_run_path)
        runner_path = run_dir / "runner.json"
        runner = read_json(runner_path) if runner_path.exists() else {}
        result = read_json(result_path)
        trace_path = run_dir / "native" / result.get("trace", f"traces/{case_id}.log")
        trace = trace_path.read_text(encoding="utf-8") if trace_path.exists() else ""
        if not trace.strip():
            problems.append("the native trace is missing or empty")
        judged = adapter.case_evidence(case, frozen, caps, result, trace)
        if judged.get("status") != "Completed":
            problems.append(f"the Case is {judged.get('status')}: {judged.get('reason', '')}")
        if judged.get("violations"):
            problems.extend(f"evidence violation: {item}" for item in judged["violations"])
        snapshot = adapter.replay_evidence(case, frozen, caps, native_run, runner, result, trace)
        if not isinstance(snapshot, dict) or not {"identity", "terminal", "resources", "problems"} <= snapshot.keys():
            raise PlanError("the adapter did not provide the replay evidence contract")
        problems.extend(snapshot["problems"])
        events, spatial = adapter.canonical_events(trace, result) if not problems else ([], {})
        if not problems and not events:
            problems.append("the canonical trace has no discrete events")
        return {"manifest": frozen, "caps": caps, "case": case, "snapshot": snapshot,
                "events": events, "spatial": spatial, "problems": problems}

    a, b = load(source_dir), load(replay_dir)
    problems = [f"source: {item}" for item in a["problems"]] + [f"replay: {item}" for item in b["problems"]]
    original_plan = {key: value for key, value in a["manifest"].items() if key != "resolved"}
    replay_plan_value = {key: value for key, value in b["manifest"].items() if key != "resolved"}
    case_keys = ("case_id", "block", "split", "index", "side", "seed", "arm")
    case_a = {key: a["case"].get(key) for key in case_keys}
    case_b = {key: b["case"].get(key) for key in case_keys}
    if original_plan != replay_plan_value or case_a != case_b or a["caps"] != b["caps"]:
        problems.append("the replay plan, Case identity or capabilities differ from the source")
    replay_of = b["manifest"]["resolved"].get("replay_of", {})
    if (replay_of.get("case") != case_id or not replay_of.get("run")
            or Path(replay_of["run"]).resolve() != source_dir.resolve()):
        problems.append("the replay manifest does not identify this source Case")
    identity_difference = first_difference(a["snapshot"]["identity"], b["snapshot"]["identity"])
    terminal_difference = first_difference(a["snapshot"]["terminal"], b["snapshot"]["terminal"])
    resource_difference = first_difference(a["snapshot"]["resources"], b["snapshot"]["resources"])
    events_a, events_b = a["events"], b["events"]
    spatial_a, spatial_b = a["spatial"], b["spatial"]
    first = None
    for index in range(max(len(events_a), len(events_b))):
        a = events_a[index] if index < len(events_a) else None
        b = events_b[index] if index < len(events_b) else None
        if a != b:
            first = {"index": index, "source": a, "replay": b}
            break
    worst = 0.0
    spatial_missing = sorted(set(spatial_a) ^ set(spatial_b))
    for key in set(spatial_a) & set(spatial_b):
        worst = max(worst, math.dist(spatial_a[key], spatial_b[key]))
    identical = first is None and not problems
    spatial_ok = not spatial_missing and worst <= spatial_tolerance_cm and not problems
    identity_ok = identity_difference is None and not problems
    terminal_ok = terminal_difference is None and not problems
    resources_ok = resource_difference is None and not problems
    out = {"schema": "build_evaluation.replay", "schema_version": SCHEMA_VERSION, "case": case_id,
           "source": str(source_dir), "replay": str(replay_dir), "discrete_events": len(events_a),
           "discrete_identical": identical, "first_divergence": first, "spatial_samples": len(set(spatial_a) & set(spatial_b)),
           "spatial_missing": spatial_missing[:20], "spatial_max_deviation_cm": worst,
           "spatial_tolerance_cm": spatial_tolerance_cm, "spatial_within_tolerance": spatial_ok,
           "identity_identical": identity_ok, "identity_divergence": identity_difference,
           "terminal_identical": terminal_ok, "terminal_divergence": terminal_difference,
           "resources_identical": resources_ok, "resources_divergence": resource_difference,
           "evidence_problems": problems,
           "conclusion": "Replayed" if identical and spatial_ok and identity_ok and terminal_ok and resources_ok else "RuleViolation"}
    write_json(replay_dir / "replay.json", out)
    report = replay_dir / "report.md"
    if report.exists():
        text = report.read_text(encoding="utf-8")
        text += (f"\n## 重放比对\n\n源 Case `{case_id}`（{source_dir}）：离散事件 {len(events_a)} 条，"
                 f"{'完全一致' if identical else '未通过'}；空间样本 {out['spatial_samples']} 个，"
                 f"最大偏差 {worst:.3f} cm（容差 {spatial_tolerance_cm} cm）。结论：{out['conclusion']}。\n")
        for label, difference in (("身份", identity_difference), ("终态", terminal_difference), ("资源", resource_difference)):
            if difference:
                text += f"\n- {label}首个分歧：`{difference}`\n"
        for problem in problems:
            text += f"\n- 证据问题：{problem}\n"
        if first:
            text += f"\n- 源：`{first['source']}`\n- 重放：`{first['replay']}`\n"
        write_text(report, text)
    return out


# ---------------------------------------------------------------- CLI

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("plan")
    p.add_argument("--manifest", required=True)
    p.add_argument("--run-dir", required=True)
    p.add_argument("--project-root", default=str(_project_root()))
    a = sub.add_parser("analyze")
    a.add_argument("--run-dir", required=True)
    a.add_argument("--rejudge", action="store_true", help="judge every recorded Case again from its native evidence")
    r = sub.add_parser("replay-plan")
    r.add_argument("--source", required=True)
    r.add_argument("--case", required=True)
    r.add_argument("--run-dir", required=True)
    c = sub.add_parser("compare")
    c.add_argument("--source", required=True)
    c.add_argument("--replay", required=True)
    c.add_argument("--case", required=True)
    c.add_argument("--spatial-tolerance-cm", type=float, default=0.0)
    args = parser.parse_args(argv)
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    try:
        if args.command == "plan":
            frozen, cases = plan(args.manifest, args.run_dir, args.project_root)
            runnable = frozen["resolved"]["runnable_cases"]
            print(f"Planned {len(cases)} Case(s), {runnable} runnable, {len(cases) - runnable} unsupported: {args.run_dir}")
            return 0 if runnable else 3
        if args.command == "analyze":
            metrics, findings = analyze(args.run_dir, args.rejudge)
            counts = {}
            for finding in findings["findings"]:
                counts[finding["conclusion"]] = counts.get(finding["conclusion"], 0) + 1
            print(f"Analyzed {args.run_dir}: findings {counts or 'none'}")
            return 1 if counts.get("RuleViolation") else 0
        if args.command == "replay-plan":
            replay_plan(args.source, args.case, args.run_dir)
            print(f"Replay of {args.case} planned: {args.run_dir}")
            return 0
        out = compare(args.source, args.replay, args.case, args.spatial_tolerance_cm)
        print(f"Replay {args.case}: {out['conclusion']} (events {out['discrete_events']}, "
              f"first divergence {out['first_divergence'] and out['first_divergence']['index']}, "
              f"spatial max {out['spatial_max_deviation_cm']:.3f} cm)")
        return 0 if out["conclusion"] == "Replayed" else 1
    except PlanError as error:
        print(f"BUILD EVALUATION REFUSED: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
