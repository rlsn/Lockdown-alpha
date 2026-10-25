#!/usr/bin/env python3
"""Reproduce the B80 audit/import checkpoint; not native v0.8 acceptance.

Later implementation deliberately changes this frozen baseline. Preserve its
recorded evidence instead of updating historical hashes to hide those changes.
"""
import argparse
import csv
import json
from pathlib import Path
import subprocess
import zipfile

import SyncV08Data as sync

ROOT = sync.ROOT
EVIDENCE = ROOT / "docs/v08/evidence/B80"


def csv_rows(name):
    with (ROOT / name).open(encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=EVIDENCE / "Audit.json")
    args = parser.parse_args()
    adopted = sync.verify_provenance(ROOT)
    for path, expected in sync.build_outputs(ROOT).items():
        sync.require((ROOT / path).read_bytes() == expected, f"Stale imported output: {path}")

    old_attributes = subprocess.check_output(["git", "show", "ddf7be5:.gitattributes"], cwd=ROOT)
    addition = ("\n# B80 adoption/import manifests hash physical bytes, including source archives.\n"
                "/docs/v08/** -text whitespace=cr-at-eol\n"
                "/Content/Lockdown/Data/V08/*.json -text\n"
                "/Scripts/*V08*.py text eol=lf\n").encode()
    sync.require((ROOT / ".gitattributes").read_bytes().replace(b"\r\n", b"\n") ==
                 old_attributes.replace(b"\r\n", b"\n") + addition,
                 "Unexpected change to inherited Git attributes")

    manifest = sync.read(ROOT, "docs/v07/evidence/B76/SourceManifest.json")
    matched = 0
    for name, entry in manifest["files"].items():
        if name == ".gitattributes":
            continue  # B80 appends byte-preservation rules for its own new files.
        raw = (ROOT / name).read_bytes()
        if entry["encoding"] == "LF-normalized text":
            raw = raw.replace(b"\r\n", b"\n")
        sync.require(sync.sha(raw) == entry["sha256"], f"Changed B76 source: {name}")
        matched += 1
    evidence_count = 0
    for name, expected in sync.read(ROOT, "docs/v07/evidence/B76/Summary.json")["evidenceHashes"].items():
        raw = (ROOT / "docs/v07/evidence/B76" / name).read_bytes().replace(b"\r\n", b"\n")
        sync.require(sync.sha(raw) == expected, f"Changed B76 evidence: {name}")
        evidence_count += 1
    # Adding the disabled import directory must not mutate any native code,
    # configs, module staging rules or existing runtime assets.
    runtime_changes = subprocess.check_output([
        "git", "diff", "--name-only", "ddf7be5", "--", "Source", "Config", "Content",
        ":(exclude)Content/Lockdown/Data/V08"], cwd=ROOT, text=True).splitlines()
    runtime_untracked = subprocess.check_output([
        "git", "ls-files", "--others", "--exclude-standard", "--", "Source", "Config", "Content",
        ":(exclude)Content/Lockdown/Data/V08"], cwd=ROOT, text=True).splitlines()
    sync.require(not runtime_changes and not runtime_untracked, "B80 unexpectedly changed native runtime")

    archives = []
    for archive, directory in [("Project_Lockdown_V07_Source.zip", "docs/v07"),
                               ("Project_Lockdown_Master_R7_Source.zip", "docs/master")]:
        matches, archive_only = 0, []
        with zipfile.ZipFile(ROOT / "docs/v08/reference" / archive) as source:
            for name in source.namelist():
                if name.endswith("/"):
                    continue
                relative = name.split("/", 1)[1]
                local = ROOT / directory / relative
                if not local.is_file():
                    archive_only.append(relative)
                    continue
                sync.require(local.read_bytes().replace(b"\r\n", b"\n") ==
                             source.read(name).replace(b"\r\n", b"\n"), f"Archive differs: {relative}")
                matches += 1
        archives.append({"archive": archive, "existing_files_LF_matched": matches,
                         "archive_only": archive_only})

    ledger = csv_rows("docs/v08/FeatureLedger.csv")
    by_id = sync.rows_by_id(ledger, "working ledger")
    index = sync.read(ROOT, "docs/v08/data/acceptance_index.json")
    for case in index["new_cases"]:
        sync.require(by_id[case["id"]]["result"] == "Not run (v0.8 native)", "Inferred native pass")
        sync.require(by_id[case["id"]]["definition"] == case["key"], "Wrong acceptance mapping")
    previous = sync.rows_by_id(csv_rows("docs/v07/FeatureLedger.csv"), "v07 ledger")
    for case in index["inherited_required_cases"]:
        actual, old = by_id[case["id"]], previous[case["id"]]
        sync.require(actual["definition"] == case["requirement"] and
                     actual["baseline_status"] == old["status"], "Lost inherited disposition")
        sync.require(actual["v08_expectation"], "Missing rebased expectation")
    expected_ids = {"B80"} | {c["id"] for c in index["new_cases"] + index["inherited_required_cases"]}
    for name, key, prefix in [("abilities", "abilities", "ability"), ("items", "items", "item"),
                              ("acquisition", "recipes", "recipe"), ("class_traits", "traits", "trait"),
                              ("environment", "reactions", "reaction"),
                              ("operation_contracts", "operations", "operation"),
                              ("modifier_contracts", "modifiers", "modifier")]:
        expected_ids.update(prefix + ":" + row["id"] for row in
                            sync.read(ROOT, f"docs/v08/data/{name}.json")[key])
    sync.require(set(by_id) == expected_ids and len(ledger) == 363, "Incomplete working ledger")
    inherited = csv_rows("docs/v08/evidence/B80/InheritedV06Acceptance.csv")
    original = csv_rows("docs/v07/evidence/B70/InheritedAcceptance.csv")
    sync.require(len(inherited) == len(original) == 48, "Missing E01-E48")
    for actual, old in zip(inherited, original):
        sync.require(all(actual[k] == v for k, v in old.items()), "Changed inherited v06 status")
        sync.require(set(actual["V08LedgerLinks"].split(";")) <= by_id.keys(), "Unmapped v06 case")
    deferrals = csv_rows("docs/v08/evidence/B80/Deferrals.csv")
    old_deferrals = csv_rows("docs/v07/evidence/B77/Deferrals.csv")
    sync.require(len(deferrals) == len(old_deferrals) == 8, "Missing named v07 deferral")
    v06_names = set()
    for actual, old in zip(deferrals, old_deferrals):
        sync.require(all(actual[k] == v for k, v in old.items()), "Changed deferred disposition")
        sync.require(set(actual["AffectedV08NewCases"].split(";")) <= by_id.keys(), "Unmapped deferral")
        v06_names.update(actual["InheritedFollowUp"].split(";"))
    sync.require(v06_names == {f"V06-D{i:02}" for i in range(1, 9)}, "Missing named v06 deferral")

    environment = sync.read(ROOT, "docs/v08/evidence/B80/EnginePreflight.json")
    pin = sync.read(ROOT, "Config/PlatformLock.json")["engine"]
    sync.require(all(environment["engineVersion"][k.title() + "Version"] == pin[k]
                     for k in ("major", "minor", "patch")), "Engine pin differs")
    sync.require(environment["engineVersion"]["Changelist"] == pin["changelist"], "Changelist differs")
    checks = sync.read(ROOT, "docs/v08/evidence/B80/Checks.json")
    sync.require(len(checks) == 8 and all(c["exit_code"] == 0 for c in checks), "Incomplete/failed B80 checks")
    implementation_paths = ["Scripts/SyncV08Data.py", "Scripts/TestV08Import.py", "Scripts/CheckV08Baseline.py",
                            "docs/v08/B80_BASELINE_REVIEW.md", "docs/v08/B80_ADAPTERS.json",
                            "docs/v08/SOURCE_MANIFEST.json", "docs/v08/FeatureLedger.csv",
                            "Content/Lockdown/Data/V08/catalog.json", "Content/Lockdown/Data/V08/ImportManifest.json"]
    report = {
        "milestone": "B80", "date": "2026-10-25", "result": "PASS",
        "scope": "Source/provenance/import/ledger checks only; no native v08 gameplay or new build",
        "audit_head": "7d9d66dfe77407f2105f2552b297709a0366146a",
        "runtime_baseline": "ddf7be5aaf74cbf8be013308e130233bcb1a410c",
        "original_package_files": len(adopted["original_package"]),
        "adoption_edits": adopted["adoption_edits"], "archives": archives,
        "retained_B76_source_hashes_matched": matched,
        "intentional_B76_manifest_difference": ".gitattributes: append byte-preservation rules for v08 only",
        "retained_B76_evidence_hashes_matched": evidence_count,
        "native_runtime_changes": runtime_changes, "unexpected_runtime_untracked": runtime_untracked,
        "ledger_rows": len(ledger), "new_acceptance_native_not_run": 68,
        "inherited_acceptance": 55, "inherited_v06_rows_preserved": 48,
        "named_deferrals": [r["DeferredID"] for r in deferrals] + sorted(v06_names),
        "native_v08": "Not run; profile disabled", "human_and_long_runs": "Still deferred",
        "preflight": "EnginePreflight.json; fresh discovery passed, buildVerified=false",
        "checks": checks,
        "implementation_hashes": {name: sync.sha((ROOT / name).read_bytes()) for name in implementation_paths},
        "evidence_hashes": {p.name: sync.sha(p.read_bytes()) for p in sorted(EVIDENCE.iterdir())
                            if p.is_file() and p.name != "Audit.json"},
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(sync.encode(report))
    print(f"PASS B80: {matched} B76 source hashes; {evidence_count} evidence hashes; "
          "363 ledger rows; 68 new / 55 inherited / 48 economy cases; runtime disabled")


if __name__ == "__main__":
    main()
