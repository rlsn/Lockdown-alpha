#!/usr/bin/env python3
"""Recheck the B70 frozen inputs and adapter arithmetic, not Unreal gameplay.

Run at the B70 checkpoint. Later runtime/spec edits intentionally invalidate this
comparison; keep its recorded report as historical evidence instead of updating
the frozen manifests to hide a migration.
"""
import argparse
import csv
import hashlib
import json
from collections import Counter
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / "docs/v07/evidence/B70"


def read(path):
    return json.loads((ROOT / path).read_text(encoding="utf-8-sig"))


def digest(path):
    return hashlib.sha256((ROOT / path).read_bytes()).hexdigest()


def verify_manifest(path):
    files = read(path)["files"]
    mismatches = [name for name, expected in files.items()
                  if not (ROOT / name).is_file() or digest(name) != expected]
    if mismatches:
        raise ValueError(f"Changed frozen inputs in {path}: {mismatches}")
    return len(files)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source_count = verify_manifest("docs/v06/evidence/B66/SourceManifest.json")
    spec_count = verify_manifest("docs/v07/evidence/B70/AdoptedSpec.json")
    runtime_count = verify_manifest("docs/v07/evidence/B70/RuntimeInputs.json")
    adapters = read("docs/v07/B70_ADAPTERS.json")
    rules = read("docs/v07/data/v07_rules.json")
    old_rules = read("docs/v04/data/prototype_rules.json")
    for name, expected in read("docs/v07/evidence/B70/Verification.json")["evidenceHashes"].items():
        if digest("docs/v07/evidence/B70/" + name) != expected:
            raise ValueError(f"Changed B70 evidence: {name}")
    abilities = read("docs/v07/data/abilities.json")["abilities"]
    expected_classes = {row["id"]: row for row in old_rules["classes"]}
    rows = []
    score_rows = []
    for preset in adapters["legacy_starts"]:
        name = preset["id"]
        old = expected_classes[name]
        natural = preset["v07_attributes"]
        assert preset["v06_attributes"] == old["attributes"], name
        assert preset["starting_skills"] == old["startingSkills"], name
        assert sum(old["attributes"].values()) == 60, name
        assert sum(natural.values()) == rules["attributes"]["starting_budget"], name
        assert sorted(natural.values()) == [8, 8, 8, 8, 10, 11], name
        stat = preset["witness_attribute"]
        # Legacy Witness score is the selected natural stat + 2*(rank-1).
        # A retained starter at rank 1 needs only permanent attribute investment.
        spent = max(0, 12 - natural[stat])
        level = 1 + spent
        assert level == preset["rank1_witness_level"], name
        assert natural[stat] + spent <= 16 and level <= 15, name
        rows.append({"class": name, "rank1_witness_level": level,
                     "xp": rules["progression"]["cumulative_xp"][level - 1],
                     "attribute": stat, "allocated_points": spent,
                     "score": natural[stat] + spent,
                     "starting_capacity": 60 + 2 * (natural["STR"] - 10)})
        for skill, attribute, threshold in [("Heave", "STR", 12), ("Parley", "CHA", 12),
                                            ("Analyze", "INT", 12), ("Survey", "WIS", 12),
                                            ("Analyze coupon", "INT", 14)]:
            allocated = max(0, threshold - natural[attribute])
            trained_allocated = max(0, threshold - natural[attribute] - 2)
            assert natural[attribute] + allocated <= rules["attributes"]["natural_cap"]
            assert 1 + allocated <= rules["profiles"]["v07_integration"]["level_cap"]
            score_rows.append({"class": name, "check": skill, "threshold": threshold,
                               "rank1_attribute_level": 1 + allocated,
                               "rank2_attribute_level": max(3, 1 + trained_allocated),
                               "scope": "arithmetic after legal learning/equip; not a journey"})
    ids = adapters["ability_wire_ids"]
    assert len(ids) == 38 and set(ids.values()) == set(range(38))
    for i, name in enumerate(["none", "legacy_step", "legacy_ambush", "legacy_spark", "legacy_patch",
                              "legacy_pickpocket", "legacy_parley", "legacy_analyze", "legacy_survey", "legacy_heave"]):
        assert ids[name] == i, name
    assert all(ids[a["id"]] >= 10 for a in abilities)
    assert adapters["wire"]["skill_id_bits"] == 16
    assert adapters["wire"]["max_skill_id"] == 37
    assert adapters["status"] == "Adopted design; runtime implementation pending B71-B76"
    with (ROOT / "docs/v07/FeatureLedger.csv").open(encoding="utf-8-sig", newline="") as f:
        ledger = list(csv.DictReader(f))
    with (ROOT / "docs/v07/templates/FEATURE_LEDGER.csv").open(encoding="utf-8-sig", newline="") as f:
        template = list(csv.DictReader(f))
    assert [(r["id"], r["requirement"]) for r in ledger] == [(r["id"], r["requirement"]) for r in template]
    assert Counter(r["status"] for r in ledger) == {"Complete (source/build audit)": 1, "Not run": 54}
    assert ledger[0]["id"] == "C001" and ledger[0]["status"].startswith("Complete")
    with (EVIDENCE / "InheritedAcceptance.csv").open(encoding="utf-8-sig", newline="") as f:
        inherited = list(csv.DictReader(f))
    with (ROOT / "docs/v06/FeatureLedger.csv").open(encoding="utf-8-sig", newline="") as f:
        prior = list(csv.DictReader(f))
    assert len(inherited) == len(prior) == 48
    for actual, expected in zip(inherited, prior):
        assert actual["AcceptanceID"] == expected["AcceptanceID"]
        assert actual["B67Closeout"] == expected["B67Closeout"]
        assert actual["DeferredFollowUp"] == expected["DeferredFollowUp"]
    changes = subprocess.check_output(
        ["git", "diff", "--name-only", adapters["runtime_revision"], "--",
         "Source", "Config", "Content", "Lockdown.uproject"], cwd=ROOT, text=True).splitlines()
    untracked = subprocess.check_output(
        ["git", "ls-files", "--others", "--exclude-standard", "--",
         "Source", "Config", "Content"], cwd=ROOT, text=True).splitlines()
    assert not changes and not untracked, (changes, untracked)
    report = {
        "scope": "B70 source/spec identity and proposed adapter arithmetic only; no gameplay execution",
        "runtimeRevision": adapters["runtime_revision"],
        "sourceHashesMatched": source_count, "runtimeInputHashesMatched": runtime_count,
        "suppliedSpecHashesMatched": spec_count, "runtimeChanges": changes,
        "workingLedger": {"sourceBuildAuditComplete": 1, "notRun": 54},
        "inheritedAcceptance": dict(Counter(r["B67Closeout"] for r in inherited)),
        "legacyAdapterArithmetic": rows,
        "legacyScoreReachability": score_rows,
        "wireIds": len(ids), "result": "PASS"
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"PASS B70: {source_count} retained source, {runtime_count} runtime inputs, "
          f"{spec_count} supplied spec hashes; 48 inherited rows; C001 audit complete/54 Not run")


if __name__ == "__main__":
    main()
