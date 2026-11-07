"""Summarize a DEV opponent telemetry run (RunDemoOpponentCheck.ps1 -Telemetry -MatchCount N) into one report.

The Host writes Telemetry.json and one MatchNN.json per match (LDDemoOpponentTelemetry.cpp): the end reason and
winner, the flagpole opening, the first kill, every kill with its assisters, every contender death with its killer and
assist rewards, the spray/repaint/clear counts and the winning guard time, each contender's level, XP and gold curves,
purchases, sales, spray attempts, and the growth record of #112 §13 (first active, final skills and top-rank count, rerolls
earned and used, choices, active replacements with their level, advanced skills of the final build). This aggregates them
across matches. The numbers describe how
these matches went; they are not balance targets and the summary judges nothing.
"""

import argparse
import json
from pathlib import Path
import re
import statistics

MATCH_SCHEMA = 3
SUMMARY_SCHEMA = 3
OPPONENTS = 3
REASONS = ("GuardSucceeded", "HostEnded", "None")
KINDS = ("Human", "OpponentAI")
CAUSES = ("Contender", "CampLow", "CampHigh", "Unowned")
OUTCOMES = ("Granted", "NoReward")
MATCH_FILE = re.compile(r"Match[0-9]{2}\.json")
MINUTE_MS = 60000


def require(condition, message):
    if not condition:
        raise ValueError(message)


def field(row, name, kind):
    require(isinstance(row, dict) and name in row, f"Missing telemetry field: {name}")
    value = row[name]
    if kind is int:
        require(isinstance(value, (int, float)) and not isinstance(value, bool) and float(value).is_integer(),
                f"Telemetry field {name} is not an integer")
        return int(value)
    require(isinstance(value, kind), f"Telemetry field {name} has the wrong type")
    return value


def stats(values):
    values = list(values)
    if not values:
        return {"count": 0}
    return {"count": len(values), "min": min(values), "mean": round(statistics.fmean(values), 1),
            "median": statistics.median(values), "max": max(values)}


def optional_stats(values):
    # -1 marks a match or contender that never reached the event.
    values = list(values)
    result = stats(v for v in values if v >= 0)
    result["missing"] = sum(1 for v in values if v < 0)
    return result


def check_curve(curve, duration_ms, where):
    times = [int(v) for v in field(curve, "t_ms", list)]
    require(times, f"{where}: empty curve")
    require(all(b > a for a, b in zip(times, times[1:])), f"{where}: curve times do not increase")
    require(times[0] >= 0 and times[-1] <= duration_ms, f"{where}: curve outside the match")
    for name in ("level", "xp", "gold"):
        series = field(curve, name, list)
        require(len(series) == len(times), f"{where}: {name} curve length differs from its times")
        require(all(isinstance(v, (int, float)) and float(v).is_integer() for v in series), f"{where}: {name} not integral")
    return times


def check_match(match, index):
    where = f"Match{index:02d}"
    require(field(match, "schema", int) == MATCH_SCHEMA, f"{where}: unsupported schema")
    require(field(match, "match", int) == index, f"{where}: match index out of order")
    require(field(match, "reason", str) in REASONS, f"{where}: unknown end reason")
    require(re.fullmatch(r"[0-9a-f]{64}", field(match, "content_root", str)), f"{where}: malformed content root")
    field(match, "dev_content_reload", bool)
    duration = field(match, "duration_ms", int)
    require(duration > 0, f"{where}: nonpositive duration")
    for name in ("capture_open_ms", "first_kill_ms", "first_death_ms"):
        value = field(match, name, int)
        require(-1 <= value <= duration, f"{where}: {name} outside the match")
    require(field(match, "kill_feed_gaps", int) >= 0, f"{where}: negative kill feed gaps")
    for name in ("capture_contests", "capture_sprays", "capture_repaints", "graffiti_cleared_by_death",
                 "graffiti_cleared_by_leave"):
        require(field(match, name, int) >= 0, f"{where}: negative {name}")
    guard = field(match, "guard_success_ms", int)
    require((guard >= 0) == (match["reason"] == "GuardSucceeded") and guard <= duration,
            f"{where}: a guard time belongs to exactly a guard win")
    for kill in field(match, "kills", list):
        require(field(kill, "cause", str) in CAUSES, f"{where}: unknown kill cause")
        require(0 <= field(kill, "t_ms", int) <= duration, f"{where}: kill outside the match")
        assisters = field(kill, "assisters", list)
        require(not assisters or kill["cause"] == "Contender", f"{where}: only a contender kill has assisters")
        require(kill["killer"] not in assisters and len(set(assisters)) == len(assisters),
                f"{where}: an assister repeats or is the killer")
    contenders = field(match, "contenders", list)
    winner = field(match, "winner", str)
    roster = {field(row, "contender", str) for row in contenders}
    for death in field(match, "deaths", list):
        require(0 <= field(death, "t_ms", int) <= duration, f"{where}: death outside the match")
        require(field(death, "victim", str) in roster, f"{where}: a death names no contender")
        require(field(death, "life", int) >= 1, f"{where}: a death without a life")
        outcome = field(death, "outcome", str)
        require(outcome in OUTCOMES, f"{where}: unknown reward outcome")
        killer = field(death, "killer", str)
        require((outcome == "Granted") == bool(killer), f"{where}: only a granted reward names a killer")
        require(not killer or killer in roster, f"{where}: the killer is not on the roster")
        for name in ("killer_gold", "killer_xp"):
            require(field(death, name, int) >= 0, f"{where}: negative {name}")
        parts = field(death, "assists", list)
        require(not parts or outcome == "Granted", f"{where}: assists without a granted reward")
        for part in parts:
            require(field(part, "contender", str) in roster and part["contender"] not in (killer, death["victim"]),
                    f"{where}: an assister is off the roster, the killer or the victim")
            require(field(part, "gold", int) >= 0 and field(part, "xp", int) >= 0, f"{where}: negative assist reward")
    ids = set()
    slots = []
    for row in contenders:
        kind = field(row, "kind", str)
        require(kind in KINDS, f"{where}: unknown contender kind")
        contender = field(row, "contender", str)
        require(contender and contender not in ids, f"{where}: missing or repeated contender")
        ids.add(contender)
        slot = field(row, "slot", int)
        require((slot >= 0) == (kind == "OpponentAI"), f"{where}: only an AI has a slot")
        if slot >= 0:
            slots.append(slot)
        require(field(row, "winner", bool) == (contender == winner), f"{where}: winner flag disagrees with the result")
        require(field(row, "final_level", int) >= 1, f"{where}: final level below one")
        for name in ("kills", "deaths", "assists", "capture_attempts", "capture_resumes", "capture_paused", "capture_lost",
                     "capture_sprays", "capture_interrupted", "max_rank_skills", "rerolls_used", "choices"):
            require(field(row, name, int) >= 0, f"{where}: negative {name}")
        require(field(row, "rerolls_used", int) <= field(row, "rerolls_earned", int), f"{where}: more rerolls used than earned")
        require(-1 <= field(row, "first_active_ms", int) <= duration, f"{where}: first active outside the match")
        skills = field(row, "skills", list)
        for skill in skills:
            field(skill, "id", str)
            require(field(skill, "rank", int) >= 1, f"{where}: a learned skill below rank one")
            field(skill, "active", bool)
        require(row["max_rank_skills"] <= len(skills), f"{where}: more advanced skills than skills")
        require((row["first_active_ms"] >= 0) or not any(skill["active"] for skill in skills),
                f"{where}: an active learned without a first-active time")
        for change in field(row, "replacements", list):
            require(0 <= field(change, "t_ms", int) <= duration, f"{where}: replacement outside the match")
            require(field(change, "level", int) >= 1, f"{where}: replacement below level one")
            field(change, "replaced", str)
            require(field(change, "replaced_rank", int) >= 1, f"{where}: a replaced active below rank one")
        for name in ("purchases", "sales"):
            for trade in field(row, name, list):
                require(0 <= field(trade, "t_ms", int) <= duration, f"{where}: trade outside the match")
                field(trade, "item", str)
        check_curve(field(row, "curve", dict), duration, f"{where} {contender}")
    require(sorted(slots) == list(range(OPPONENTS)), f"{where}: the room does not hold AI slots 0 to {OPPONENTS - 1}")
    require(len(contenders) > OPPONENTS, f"{where}: no human in the room")
    require(not winner or winner in ids, f"{where}: winner is not on the roster")
    return match


def load(directory):
    directory = Path(directory)
    report = json.loads((directory / "Telemetry.json").read_text(encoding="utf-8"))
    require(field(report, "role", str) == "Telemetry", "Telemetry.json is not a telemetry report")
    require(field(report, "passed", bool), f"Telemetry run failed: {field(report, 'detail', str)}")
    requested = field(report, "matches_requested", int)
    files = field(report, "files", list)
    require(field(report, "matches_completed", int) == requested == len(files) and requested >= 1,
            "Telemetry run did not complete every requested match")
    matches = []
    for index, name in enumerate(files, start=1):
        require(isinstance(name, str) and MATCH_FILE.fullmatch(name) and name == f"Match{index:02d}.json",
                f"Unexpected telemetry file name: {name}")
        matches.append(json.loads((directory / name).read_text(encoding="utf-8")))
    return report, matches


def value_at(curve, name, at_ms):
    # The last sample at or before the instant; None when the match had not lasted that long.
    times = curve["t_ms"]
    if not times or times[-1] < at_ms:
        return None
    index = None
    for position, at in enumerate(times):
        if at > at_ms:
            break
        index = position
    return None if index is None else int(curve[name][index])


def by_minute(ais, name, minutes):
    rows = []
    for minute in range(1, minutes + 1):
        values = [v for v in (value_at(ai["curve"], name, minute * MINUTE_MS) for ai in ais) if v is not None and v >= 0]
        if values:
            rows.append({"minute": minute, **stats(values)})
    return rows


def summarize(matches):
    require(matches, "No telemetry matches")
    checked = [check_match(match, index) for index, match in enumerate(matches, start=1)]
    reasons = {reason: 0 for reason in REASONS}
    wins_by_slot = {str(slot): 0 for slot in range(OPPONENTS)}
    ai_capture_wins = 0
    ais = []
    per_match = []
    for match in checked:
        reasons[match["reason"]] += 1
        rows = match["contenders"]
        match_ais = sorted((row for row in rows if row["kind"] == "OpponentAI"), key=lambda row: row["slot"])
        ais.extend(match_ais)
        winner = next((row for row in rows if row["winner"]), None)
        winner_slot = winner["slot"] if winner else -1
        if winner and winner["kind"] == "OpponentAI" and match["reason"] == "GuardSucceeded":
            ai_capture_wins += 1
            wins_by_slot[str(winner_slot)] += 1
        per_match.append({
            "match": match["match"], "reason": match["reason"], "winner_slot": winner_slot,
            "winner_kind": winner["kind"] if winner else "", "duration_ms": match["duration_ms"],
            "capture_open_ms": match["capture_open_ms"], "first_kill_ms": match["first_kill_ms"],
            "capture_attempts": sum(row["capture_attempts"] for row in rows),
            "capture_contests": match["capture_contests"],
            "sprays": match["capture_sprays"], "repaints": match["capture_repaints"],
            "guard_success_ms": match["guard_success_ms"],
            "ai_purchases": sum(len(row["purchases"]) for row in match_ais),
            "ai_final_levels": [row["final_level"] for row in match_ais],
        })
    longest = max(match["duration_ms"] for match in checked)
    first_purchases = [min((t["t_ms"] for t in ai["purchases"]), default=-1) for ai in ais]
    deaths = [death for match in checked for death in match["deaths"]]
    granted = [death for death in deaths if death["outcome"] == "Granted"]
    assist_parts = [part for death in granted for part in death["assists"]]
    replacements = [change for ai in ais for change in ai["replacements"]]
    choices = sum(ai["choices"] for ai in ais)
    earned = sum(ai["rerolls_earned"] for ai in ais)
    max_rank = {}
    for ai in ais:
        max_rank[str(ai["max_rank_skills"])] = max_rank.get(str(ai["max_rank_skills"]), 0) + 1
    return {
        "schema": SUMMARY_SCHEMA,
        "matches": len(checked),
        # Every content catalog the run played, in first-played order; more than one only under hot reload.
        "content_roots": list(dict.fromkeys(match["content_root"] for match in checked)),
        "dev_content_reload": any(match["dev_content_reload"] for match in checked),
        "reasons": reasons,
        "ai_capture_wins": ai_capture_wins,
        "wins_by_slot": wins_by_slot,
        "duration_ms": stats(match["duration_ms"] for match in checked),
        "capture_open_ms": optional_stats(match["capture_open_ms"] for match in checked),
        "first_kill_ms": optional_stats(match["first_kill_ms"] for match in checked),
        "first_death_ms": optional_stats(match["first_death_ms"] for match in checked),
        "kill_feed_gaps": sum(match["kill_feed_gaps"] for match in checked),
        "capture": {
            "attempts": stats(row["capture_attempts"] for row in per_match),
            "contests": stats(match["capture_contests"] for match in checked),
            "resumes": stats(sum(r["capture_resumes"] for r in match["contenders"]) for match in checked),
            "sprays": stats(match["capture_sprays"] for match in checked),
            "repaints": stats(match["capture_repaints"] for match in checked),
            "cleared_by_death": sum(match["graffiti_cleared_by_death"] for match in checked),
            "cleared_by_leave": sum(match["graffiti_cleared_by_leave"] for match in checked),
            "cleared_by_repaint": sum(match["capture_repaints"] for match in checked),
            "guard_success_ms": optional_stats(match["guard_success_ms"] for match in checked),
        },
        # Every contender death of every match with its committed rewards (a NoReward death has no killer).
        "deaths": {
            "count": len(deaths),
            "granted": len(granted),
            "assisters_per_kill": stats(len(death["assists"]) for death in granted),
            "kills_with_assist": sum(1 for death in granted if death["assists"]),
            "killer_gold": stats(death["killer_gold"] for death in granted),
            "killer_xp": stats(death["killer_xp"] for death in granted),
            "assist_gold": stats(part["gold"] for part in assist_parts),
            "assist_xp": stats(part["xp"] for part in assist_parts),
        },
        # AI growth (#112 §13): reroll use is used / earned; the replacement rate is replaced actives per consumed choice.
        "growth": {
            "first_active_ms": optional_stats(ai["first_active_ms"] for ai in ais),
            "max_rank_skills": dict(sorted(max_rank.items(), key=lambda item: int(item[0]))),
            "rerolls_earned": earned,
            "rerolls_used": sum(ai["rerolls_used"] for ai in ais),
            "reroll_use_rate": round(sum(ai["rerolls_used"] for ai in ais) / earned, 3) if earned else None,
            "choices": choices,
            "replacements": len(replacements),
            "replacement_rate": round(len(replacements) / choices, 3) if choices else None,
            "replacement_level": stats(change["level"] for change in replacements),
            "replaced_rank": stats(change["replaced_rank"] for change in replacements),
        },
        "ai": {
            "final_level": stats(ai["final_level"] for ai in ais),
            "kills": stats(ai["kills"] for ai in ais),
            "assists": stats(ai["assists"] for ai in ais),
            "deaths": stats(ai["deaths"] for ai in ais),
            "purchases": stats(len(ai["purchases"]) for ai in ais),
            "sales": stats(len(ai["sales"]) for ai in ais),
            "first_purchase_ms": optional_stats(first_purchases),
        },
        "ai_level_by_minute": by_minute(ais, "level", longest // MINUTE_MS),
        "ai_gold_by_minute": by_minute(ais, "gold", longest // MINUTE_MS),
        "per_match": per_match,
    }


def seconds(value):
    return "-" if value is None or value < 0 else f"{value / 1000:.0f}s"


def describe(summary):
    lines = []
    for row in summary["per_match"]:
        lines.append("Match {match}: {reason}, winner {winner}, {duration}, flagpole {circle}, first kill {kill}, "
                     "attempts {attempts}, contests {contests}, sprays {sprays}, repaints {repaints}, guard {guard}, "
                     "AI purchases {purchases}, AI levels {levels}".format(
                         match=row["match"], reason=row["reason"],
                         winner=f"AI slot {row['winner_slot']}" if row["winner_kind"] == "OpponentAI"
                         else (row["winner_kind"] or "none"),
                         duration=seconds(row["duration_ms"]), circle=seconds(row["capture_open_ms"]),
                         kill=seconds(row["first_kill_ms"]), attempts=row["capture_attempts"],
                         contests=row["capture_contests"], sprays=row["sprays"], repaints=row["repaints"],
                         guard=seconds(row["guard_success_ms"]), purchases=row["ai_purchases"],
                         levels="/".join(str(v) for v in row["ai_final_levels"])))
    duration = summary["duration_ms"]
    lines.append(f"{summary['matches']} match(es): AI capture wins {summary['ai_capture_wins']} "
                 f"(by slot {summary['wins_by_slot']}), duration {seconds(duration['min'])}-{seconds(duration['max'])} "
                 f"(median {seconds(duration['median'])}), kill feed gaps {summary['kill_feed_gaps']}")
    deaths, growth = summary["deaths"], summary["growth"]
    lines.append(f"Deaths {deaths['count']} ({deaths['granted']} rewarded, {deaths['kills_with_assist']} with assists); "
                 f"AI rerolls {growth['rerolls_used']}/{growth['rerolls_earned']}, replacements {growth['replacements']} "
                 f"of {growth['choices']} choices, advanced skills per AI {growth['max_rank_skills']}")
    return lines


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("evidence", type=Path, help="directory holding Telemetry.json and the MatchNN.json files")
    parser.add_argument("--output", type=Path, help="summary path (default: Summary.json in the evidence directory)")
    args = parser.parse_args()
    try:
        _, loaded = load(args.evidence)
        result = summarize(loaded)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Invalid opponent telemetry {args.evidence}: {error}\n")
    target = args.output or args.evidence / "Summary.json"
    target.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    for line in describe(result):
        print(line)
    print(f"Summary: {target}")
