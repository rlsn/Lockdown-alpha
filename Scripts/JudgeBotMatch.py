"""Judge a bot match run (RunDemoOpponentCheck.ps1 -BotMatch) by invariants only.

A bot match is the accelerated telemetry run with a chosen number of AI: one idle Listen Host and the AI play whole
matches through the normal lobby. This reads what that run left behind (Telemetry.json, the MatchNN.json files and the
role logs) and answers one question: did every match run to a locked result without an error while every AI played?

It fails when
- the run did not complete every requested match, or a match took longer than its wall-clock bound;
- a match file is malformed, has no result id, or ended without a locked outcome (no reason, or the Host ended it);
- the room was not one human Host plus the requested number of AI, or a contender left;
- an AI never took an active choice, never learned an active or never gained XP;
- a log holds an Error or Fatal line, a failed assertion or LD_DEMO_AUTHORITY_FAULT.

Game warnings (LogLD*: Warning: LD_...), such as a camp member that cannot walk home on the current map, are counted
and reported; they do not fail the run. Levels, kills, winners and every other number are not judged: exact values
belong to the rule tests.
"""

import argparse
import json
from pathlib import Path
import re

from SummarizeOpponentTelemetry import MATCH_FILE, check_match

VERDICT_SCHEMA = 1
# The same lines RunDemoOpponentCheck.ps1 -Telemetry refuses; LogPython is the editor plugins' own start-up noise.
ERROR_LINE = re.compile(r"Fatal error|Assertion failed|Log(?!Python:)\w+: (Error|Fatal):|LD_DEMO_AUTHORITY_FAULT")
WARNING_LINE = re.compile(r"LogLD\w+: Warning: (LD_[A-Z0-9_]+)")
RESULT_ID = re.compile(r"[0-9A-Fa-f]{32}")
# A match that reached its own end; "None" is no result and "HostEnded" is a person stopping the match.
UNLOCKED_REASONS = ("None", "HostEnded")
MAX_QUOTED = 5


def scan_logs(logs):
    """logs: {file name: iterable of lines}. Returns (error lines with their file, warning counts by token)."""
    errors, warnings = [], {}
    for name in sorted(logs):
        for line in logs[name]:
            if ERROR_LINE.search(line):
                errors.append(f"{name}: {line.strip()}")
            found = WARNING_LINE.search(line)
            if found:
                warnings[found.group(1)] = warnings.get(found.group(1), 0) + 1
    return errors, dict(sorted(warnings.items()))


def idle_ais(match):
    """Why each AI of one checked match does not count as having played; empty when all did."""
    reasons = []
    for row in match["contenders"]:
        if row["kind"] != "OpponentAI":
            continue
        missing = []
        if row["choices"] < 1:
            missing.append("no active choice")
        if not row["learns"]:
            missing.append("no active learned")
        if max(row["curve"]["xp"]) <= 0:
            missing.append("no XP gained")
        if missing:
            reasons.append(f"AI slot {row['slot']} ({', '.join(missing)})")
    return reasons


def judge(report, matches, logs, opponents, timeout_seconds):
    """report: Telemetry.json or None; matches: the parsed MatchNN.json files in order; logs: see scan_logs."""
    failures, rows = [], []
    if not isinstance(report, dict):
        failures.append("The run left no Telemetry.json")
        report = {}
    requested = report.get("matches_requested")
    walls = report.get("match_wall_seconds") or []
    if report and not report.get("passed"):
        failures.append(f"The telemetry run failed: {report.get('detail') or 'no detail'}")
    if report and not (isinstance(requested, int) and requested >= 1 and report.get("matches_completed") == requested
                       and len(matches) == requested and len(walls) == requested):
        failures.append(f"The run completed {report.get('matches_completed')} of {requested} requested match(es) "
                        f"and left {len(matches)} match file(s)")
    for index, match in enumerate(matches, start=1):
        where = f"Match{index:02d}"
        wall = walls[index - 1] if index <= len(walls) else None
        row = {"match": index, "wall_seconds": wall}
        rows.append(row)
        try:
            check_match(match, index)
        except ValueError as error:
            failures.append(f"Malformed match file: {error}")
            continue
        ais = [c for c in match["contenders"] if c["kind"] == "OpponentAI"]
        humans = len(match["contenders"]) - len(ais)
        row.update({"reason": match["reason"], "duration_ms": match["duration_ms"], "ai": len(ais),
                    "winner_slot": next((c["slot"] for c in match["contenders"] if c["winner"]), -1)})
        if not RESULT_ID.fullmatch(str(match.get("result_id", ""))) or match["reason"] in UNLOCKED_REASONS:
            failures.append(f"{where}: no locked result (reason {match['reason']}, result id {match.get('result_id')!r})")
        if not isinstance(wall, (int, float)) or isinstance(wall, bool) or wall <= 0 or wall > timeout_seconds:
            failures.append(f"{where}: took {wall} s of wall time, outside its {timeout_seconds} s bound")
        if len(ais) != opponents or humans != 1:
            failures.append(f"{where}: the room held {humans} human(s) and {len(ais)} AI, not the idle Host and {opponents} AI")
        left = [c["name"] for c in match["contenders"] if c.get("left")]
        if left:
            failures.append(f"{where}: a contender left the match: {', '.join(left)}")
        idle = idle_ais(match)
        if idle:
            failures.append(f"{where}: an AI did not play: {'; '.join(idle)}")
    errors, warnings = scan_logs(logs)
    if not logs:
        failures.append("The run left no log to check for errors")
    if errors:
        more = f" (and {len(errors) - MAX_QUOTED} more)" if len(errors) > MAX_QUOTED else ""
        failures.append(f"{len(errors)} error line(s) logged{more}: " + " | ".join(errors[:MAX_QUOTED]))
    return {"schema": VERDICT_SCHEMA, "passed": not failures, "opponents": opponents,
            "timeout_seconds": timeout_seconds, "failures": failures, "error_lines": len(errors),
            "warnings": warnings, "matches": rows}


def load(evidence, log_directory):
    evidence, log_directory = Path(evidence), Path(log_directory)
    report_file = evidence / "Telemetry.json"
    report = json.loads(report_file.read_text(encoding="utf-8")) if report_file.is_file() else None
    files = sorted(path for path in evidence.iterdir() if MATCH_FILE.fullmatch(path.name))
    matches = [json.loads(path.read_text(encoding="utf-8")) for path in files]
    logs = {path.name: path.read_text(encoding="utf-8-sig", errors="replace").splitlines()
            for path in sorted(log_directory.glob("*.log"))}
    return report, matches, logs


def describe(verdict):
    lines = []
    for row in verdict["matches"]:
        if "reason" not in row:
            lines.append(f"Match {row['match']}: malformed")
            continue
        winner = f"AI slot {row['winner_slot']}" if row["winner_slot"] >= 0 else "no AI winner"
        lines.append(f"Match {row['match']}: {row['reason']}, {winner}, {row['duration_ms'] / 1000:.0f} s of match time "
                     f"in {row['wall_seconds']} s of wall time, {row['ai']} AI")
    for token, count in verdict["warnings"].items():
        lines.append(f"WARNING (reported, not a failure): {token} x{count}")
    for failure in verdict["failures"]:
        lines.append(f"FAILED: {failure}")
    lines.append("Bot match verdict: " + ("passed" if verdict["passed"] else f"failed ({len(verdict['failures'])})"))
    return lines


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("evidence", type=Path, help="directory holding Telemetry.json and the MatchNN.json files")
    parser.add_argument("--logs", type=Path, help="directory holding the role logs (default: the evidence directory)")
    parser.add_argument("--opponents", type=int, required=True, help="the AI count the run asked for")
    parser.add_argument("--timeout-seconds", type=int, required=True, help="wall-clock bound of one match")
    parser.add_argument("--output", type=Path, help="verdict path (default: Verdict.json beside the logs)")
    args = parser.parse_args()
    log_directory = args.logs or args.evidence
    try:
        result = judge(*load(args.evidence, log_directory), args.opponents, args.timeout_seconds)
    except (OSError, ValueError) as error:
        parser.exit(2, f"Unreadable bot match evidence {args.evidence}: {error}\n")
    (args.output or log_directory / "Verdict.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    for line in describe(result):
        print(line)
    raise SystemExit(0 if result["passed"] else 1)
