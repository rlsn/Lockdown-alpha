"""Combine independently seeded Lockdown paired runs without merging their local block indices."""

import argparse
import copy
import json
import re
import sys
from collections import Counter
from pathlib import Path

import BuildEvaluation as core


NATIVE_FIELDS = ("schema", "schema_version", "content_root", "product_version", "source_commit", "source_dirty",
                 "engine_version", "fixed_time_step", "fixed_delta_seconds", "seed_derivation")
POSITION = re.compile(r"^-?\d+,-?\d+,-?\d+$")


def _identity(run, manifest, caps, runner, native):
    resolved = manifest["resolved"]
    if (manifest["adapter"]["id"] != "lockdown-demo" or manifest["scenario"]["kind"] != "duel_site"
            or manifest["design"]["kind"] != "paired_arms"):
        raise core.PlanError(f"{run}: only recorded Lockdown duel_site paired_arms runs can be combined")
    if (resolved["adapter"] != caps["adapter"] or core.digest_json(caps) != resolved["capabilities_sha256"]
            or resolved["content"] != caps["content"] or native.get("content_root") != caps["content"]["digest"]):
        raise core.PlanError(f"{run}: recorded adapter, capabilities or content identity differs")
    if (native.get("run_id") != resolved["run_id"] or native.get("passed") is not True
            or runner.get("exit_code") != 0 or runner.get("timed_out") is not False
            or runner.get("budget_stop") is not None or runner.get("log_errors") != []
            or not runner.get("binary_digests") or runner["binary_digests"] != runner.get("binary_digests_after")):
        raise core.PlanError(f"{run}: native run, budget or executable identity is incomplete")
    digests = runner["binary_digests"]
    if (not isinstance(digests, dict) or set(digests) != {"editor_sha256", "module_sha256"}
            or any(not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value) for value in digests.values())):
        raise core.PlanError(f"{run}: recorded executable digests are incomplete")
    fps = runner.get("fixed_fps")
    if (any(native.get(key) in (None, "") for key in NATIVE_FIELDS) or not isinstance(fps, int) or fps <= 0
            or native.get("fixed_time_step") is not True
            or not isinstance(native.get("fixed_delta_seconds"), (int, float))
            or abs(native["fixed_delta_seconds"] - 1 / fps) > 1e-8):
        raise core.PlanError(f"{run}: native timing or source identity is missing")
    return {"adapter": resolved["adapter"], "core_version": resolved["core_version"],
            "capabilities_sha256": resolved["capabilities_sha256"], "content": resolved["content"],
            "native": {key: native[key] for key in NATIVE_FIELDS}, "fixed_fps": runner["fixed_fps"],
            "binary_digests": runner["binary_digests"]}


def _design(manifest):
    value = copy.deepcopy(manifest)
    value.pop("resolved", None)
    value["experiment"].pop("id", None)
    value["blocks"].pop("root_seed", None)
    return value


def combine(run_dirs):
    if len(run_dirs) < 2:
        raise core.PlanError("at least two independent runs are required")
    runs = [Path(path).resolve() for path in run_dirs]
    if len(set(runs)) != len(runs):
        raise core.PlanError("a run cannot be combined with itself")
    identity = design = caps = manifest = None
    seen_seeds, rows, mapping, counts = set(), [], {}, {}
    next_index = Counter()
    for run in runs:
        current = core.read_json(run / "manifest.json")
        current_caps = core.read_json(run / "capabilities.json")
        runner = core.read_json(run / "runner.json")
        native = core.read_json(run / "native" / "native_run.json")
        findings = core.read_json(run / "findings.json")
        if any(row.get("conclusion") == "RuleViolation" for row in findings.get("findings", [])):
            raise core.PlanError(f"{run}: recorded evidence contains a rule violation")
        current_identity = _identity(run, current, current_caps, runner, native)
        if identity is None:
            identity, design, caps, manifest = current_identity, _design(current), current_caps, current
        elif current_identity != identity or _design(current) != design or current_caps != caps:
            raise core.PlanError(f"{run}: experiment design or recorded identity differs")
        cases = core.read_cases(run)
        expected = current["resolved"]["cases"]
        planned = {f"{block['block']}-{arm['id']}": {**block, "arm": arm["id"]}
                   for block in core.expand_blocks(current["blocks"]) for arm in current["design"]["arms"]}
        if (len(planned) != expected or len(cases) != expected or native.get("cases_requested") != expected
                or native.get("cases_written") != expected):
            raise core.PlanError(f"{run}: the recorded case set is incomplete")
        seeds = {case["seed"] for case in cases}
        if seen_seeds & seeds:
            raise core.PlanError(f"{run}: an independent seed occurs in more than one run")
        seen_seeds.update(seeds)
        counts[str(run)] = dict(Counter(case["status"] for case in cases))
        local = {}
        for case in cases:
            seed, split, side, arm = case["seed"], case["split"], case["side"], case["arm"]
            cell = planned.get(case.get("case_id"))
            if cell is None or any(case.get(key) != cell[key] for key in ("block", "split", "index", "side", "seed", "arm")):
                raise core.PlanError(f"{run}: Case identity does not match the frozen seed plan")
            if split not in core.SPLITS or side not in range(current["blocks"]["sides"]):
                raise core.PlanError(f"{run}: invalid split or side")
            if arm not in {item["id"] for item in current["design"]["arms"]}:
                raise core.PlanError(f"{run}: an unplanned arm appears")
            group = local.setdefault(seed, {"split": split, "cells": {}})
            if group["split"] != split or (arm, side) in group["cells"]:
                raise core.PlanError(f"{run}: seed {seed} has duplicate or inconsistent cells")
            group["cells"][arm, side] = case
            if case["status"] == "Completed":
                spawn = case.get("facts", {}).get("spawn", {})
                if (case.get("evidence_adapter") != current["resolved"]["adapter"] or case.get("violations")
                        or set(spawn) != {"subject", "opponent"} or spawn["subject"] == spawn["opponent"]
                        or any(not isinstance(pos, str) or not POSITION.fullmatch(pos) for pos in spawn.values())):
                    raise core.PlanError(f"{run}: {case['case_id']} lacks valid recorded evidence or spawn positions")
        expected_cells = {(arm["id"], side) for arm in current["design"]["arms"]
                          for side in range(current["blocks"]["sides"])}
        for seed, group in local.items():
            cells = group["cells"]
            if set(cells) != expected_cells:
                raise core.PlanError(f"{run}: seed {seed} does not contain every planned arm and side")
            for side in range(current["blocks"]["sides"]):
                positions = [case["facts"]["spawn"] for (arm, s), case in cells.items()
                             if s == side and case["status"] == "Completed"]
                if any(position != positions[0] for position in positions[1:]):
                    raise core.PlanError(f"{run}: seed {seed} did not share positions between arms")
            if current["blocks"]["sides"] == 2:
                for arm in (item["id"] for item in current["design"]["arms"]):
                    a, b = cells[arm, 0], cells[arm, 1]
                    if a["status"] == b["status"] == "Completed":
                        first, second = a["facts"]["spawn"], b["facts"]["spawn"]
                        if first["subject"] != second["opponent"] or first["opponent"] != second["subject"]:
                            raise core.PlanError(f"{run}: seed {seed} did not swap the two spawn positions")
            split = group["split"]
            index = next_index[split]
            next_index[split] += 1
            cluster = f"{split[0]}{index:03d}"
            mapping[cluster] = {"seed": seed, "run": str(run),
                                "cases": {f"{arm}:{side}": case["case_id"] for (arm, side), case in cells.items()}}
            for (arm, side), case in cells.items():
                row = dict(case)
                row.update(index=index, block=f"{cluster}{'ab'[side]}")
                rows.append(row)
    metrics = core.compute_metrics(manifest, caps, rows)
    return {"schema": "lockdown.build_evaluation.combined", "schema_version": 1, "identity": identity,
            "run_statuses": counts, "seed_mapping": mapping, "metrics": metrics}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", action="append", required=True, help="recorded run directory; repeat for each run")
    parser.add_argument("--output", required=True, help="new JSON output path; an existing file is never overwritten")
    args = parser.parse_args(argv)
    try:
        result = combine(args.run)
        output = Path(args.output)
        with output.open("x", encoding="utf-8", newline="\n") as handle:
            json.dump(result, handle, ensure_ascii=False, indent=2, sort_keys=True)
            handle.write("\n")
        print(f"Combined {len(result['seed_mapping'])} independent seeds: {output}")
        return 0
    except (OSError, ValueError, KeyError, TypeError, core.PlanError) as error:
        print(f"COMBINATION REFUSED: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
