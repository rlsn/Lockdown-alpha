"""Turn Lockdown playtest logs into a per-match timeline (docs/DEVELOPMENT.md, playtest troubleshooting).

Give it the Host log, one or more client logs, or both (``Saved/Logs/Lockdown.log`` of each player, or the Host.log and
ClientN.log files of a scenario run). Lines are merged by their UTC timestamps, split by match generation and every
contender id is replaced by its roster name. For each match the report lists the header and roster, the result, every
death with the hits (Host) or own damage (client) before it, the economy timeline, the events around each player mark,
the frame-time and connection summaries, and the warnings and errors.

``--check-client`` exits non-zero when a log carries Host-only authority lines written while that process was not hosting
(outside LD_DEMO_LOBBY .. LD_DEMO_FRONTEND), the check that a client log holds only what that player could see.
"""

import argparse
import re
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

LINE = re.compile(
    r"^\[(?P<ts>\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]\[\s*(?P<frame>\d+)\]"
    r"(?P<category>[A-Za-z0-9_]+): (?:(?P<verbosity>Display|Warning|Error|Verbose|VeryVerbose|Fatal): )?(?P<message>.*)$"
)
FIELD = re.compile(r'([A-Za-z_][A-Za-z0-9_]*)=("(?:[^"\\]|\\.)*"|\S*)')
TAG = re.compile(r"^(LD_[A-Z0-9_]+)\b")
ID = re.compile(r"\b[0-9A-F]{32}\b")

# Authority lines only the Host writes; a client log must never contain them.
HOST_ONLY_TAGS = frozenset(
    {
        "LD_HIT", "LD_HEARTBEAT", "LD_REWARD", "LD_LEVEL_UP", "LD_OFFER", "LD_CHOICE", "LD_SHOP_TRADE",
        "LD_SHOP_REJECTED", "LD_DEATH_RECEIPT", "LD_CAMP_DEATH", "LD_MATCH_STARTING", "LD_MATCH_HEADER", "LD_MATCH_ROSTER",
        "LD_MATCH_SUMMARY", "LD_MATCH_SUMMARY_ROW", "LD_MATCH_STARTED", "LD_MATCH_RESULT", "LD_AI_DECISION",
        "LD_AI_SUMMARY", "LD_MEMBER_LEFT", "LD_PLAYER_MARK_REJECTED",
    }
)
# Once-per-second or ten-second samples: summarized, never listed one by one in a timeline.
PERIODIC_TAGS = frozenset({"LD_HEARTBEAT", "LD_SELF_STATE", "LD_PERF", "LD_NET", "LD_PERF_HITCH"})
# Routine AI and order bookkeeping: counted around a mark, listed only with --mark-all.
ROUTINE_TAGS = frozenset({"LD_AI_DECISION", "LD_NAV_ACCEPT", "LD_NAV_END", "LD_CAMP_ATTACK", "LD_CAMP_CHASE_PATH_REJECTED"})
ECONOMY_TAGS = (
    "LD_REWARD", "LD_LEVEL_UP", "LD_OFFER", "LD_CHOICE", "LD_SHOP_TRADE", "LD_SHOP_REJECTED",
    "LD_SELF_REWARD", "LD_SELF_LEVEL", "LD_SELF_OFFER", "LD_SELF_BUILD", "LD_SELF_ITEMS",
)


@dataclass
class Event:
    when: datetime
    source: str
    category: str
    verbosity: str
    message: str
    tag: str = ""
    fields: dict = field(default_factory=dict)
    generation: int | None = None
    order: int = 0

    def get(self, key, default=""):
        return self.fields.get(key, default)


def unquote(value):
    if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
        out, chars, index = [], value[1:-1], 0
        while index < len(chars):
            char = chars[index]
            if char == "\\" and index + 1 < len(chars):
                index += 1
                char = {"n": "\n", "r": "\r", "t": "\t"}.get(chars[index], chars[index])
            out.append(char)
            index += 1
        return "".join(out)
    return value


def parse_fields(text):
    return {key: unquote(value) for key, value in FIELD.findall(text)}


def parse_line(line, source, order=0):
    match = LINE.match(line.rstrip("\r\n"))
    if not match:
        return None
    when = datetime.strptime(match["ts"], "%Y.%m.%d-%H.%M.%S:%f").replace(tzinfo=timezone.utc)
    message = match["message"]
    tag = TAG.match(message)
    event = Event(when, source, match["category"], match["verbosity"] or "Log", message, order=order)
    if tag:
        event.tag = tag[1]
        event.fields = parse_fields(message[len(event.tag):])
    return event


def read_log(path, source):
    events = []
    with open(path, encoding="utf-8-sig", errors="replace") as handle:
        for order, line in enumerate(handle):
            event = parse_line(line, source, order)
            if event:
                events.append(event)
    return events


def assign_generations(events):
    """Each log tracks its own current match: a Host from its start and reset lines, a client from its join line."""
    current = None
    for event in events:
        tag = event.tag
        if tag in ("LD_MATCH_STARTING", "LD_MATCH_HEADER", "LD_MATCH_STARTED"):
            current = int(event.get("generation", 0)) or None
        elif tag == "LD_ROOM_RESET" or tag == "LD_DEMO_LOBBY":
            current = None
        elif tag == "LD_SELF_MATCH":
            current = int(event.get("match", 0)) or None
        elif tag in ("LD_SELF_RESULT", "LD_SELF_RESULT_ROW"):
            current = int(event.get("match", 0)) or current
        event.generation = current


def is_host_log(events):
    return any(event.tag in ("LD_DEMO_LOBBY", "LD_MATCH_STARTING", "LD_MATCH_HEADER", "LD_MATCH_STARTED") for event in events)


def check_client(events):
    """Host-only authority lines written while this process was not hosting.

    Only a hosting process initializes the Demo world's GameMode (LD_DEMO_LOBBY); it hosts until its GameMode opens the
    frontend again (LD_DEMO_FRONTEND). Everything else a process writes is a client's or the frontend's.
    """
    leaks, hosting = [], False
    for event in events:
        if event.tag in ("LD_DEMO_LOBBY", "LD_MATCH_STARTING", "LD_MATCH_HEADER", "LD_MATCH_STARTED"):
            hosting = True
        elif event.tag == "LD_DEMO_FRONTEND":
            hosting = False
        if hosting:
            continue
        if (event.tag in HOST_ONLY_TAGS or (event.tag == "LD_NET" and event.get("role") == "host")
                or (event.tag == "LD_PLAYER_MARK" and event.get("source") == "host_view")):
            leaks.append(event)
    return leaks


class Names:
    def __init__(self):
        self.names = {}

    def learn(self, events):
        for event in events:
            if event.tag in ("LD_MATCH_ROSTER", "LD_SELF_ROSTER", "LD_MATCH_SUMMARY_ROW", "LD_SELF_RESULT_ROW") and event.get("contender"):
                self.names.setdefault(event.get("contender"), event.get("name"))
        # Public kill notices also name both sides, for a client log that started after the roster.
        for event in events:
            if event.tag == "LD_SELF_KILLFEED":
                for key in ("victim", "killer"):
                    if event.get(key, "-") != "-" and event.get(f"{key}_name"):
                        self.names.setdefault(event.get(key), event.get(f"{key}_name"))

    def __call__(self, contender):
        if not contender or contender == "-":
            return "-"
        name = self.names.get(contender)
        if name is None:
            return contender[:8]
        # Players may share a display name (every unnamed player is the default one); the id prefix tells them apart.
        if sum(1 for other in self.names.values() if other == name) > 1:
            return f"{name}({contender[-4:]})"
        return name

    def text(self, message):
        return ID.sub(lambda match: self(match[0]), message)


def clock(event, start):
    seconds = (event.when - start).total_seconds() if start else 0.0
    sign = "-" if seconds < 0 else ""
    seconds = abs(seconds)
    return f"{sign}{int(seconds // 60):02d}:{seconds % 60:04.1f}"


def describe_hit(event, names):
    victim = names(event.get("victim")) if event.get("victim_kind") != "camp" else f"camp:{event.get('victim_monster')}"
    source = names(event.get("src")) if event.get("src_kind") != "camp" else f"camp:{event.get('src_monster')}"
    extras = [flag for flag in ("crit", "backstab", "protected", "periodic", "reflect") if event.get(flag) == "1"]
    if event.get("controls", "-") != "-":
        extras.append(event.get("controls"))
    if event.get("vamp", "0.00") not in ("0.00", ""):
        extras.append(f"vamp {event.get('vamp')}")
    extra = f" [{', '.join(extras)}]" if extras else ""
    return (f"{source} -> {victim} {event.get('via')} {event.get('class')} dmg {event.get('dmg')} "
            f"(shield {event.get('absorbed')}) hp {event.get('hp')}{extra}")


class Report:
    def __init__(self, events, names, death_window, mark_before, mark_after, mark_all=False):
        self.events = events
        self.names = names
        self.death_window = death_window
        self.mark_before = mark_before
        self.mark_after = mark_after
        self.mark_all = mark_all
        self.lines = []
        # A camp member's monster, so its reward reads as the camp it came from.
        self.monsters = {event.get("member"): event.get("monster") for event in events if event.tag == "LD_CAMP_DEATH"}

    def out(self, text=""):
        self.lines.append(text)

    def match(self, generation, events):
        names = self.names
        starts = [event.when for event in events if event.tag in ("LD_MATCH_STARTING", "LD_MATCH_STARTED", "LD_SELF_MATCH")]
        start = min(starts) if starts else events[0].when
        self.out(f"## Match {generation}")
        self.out(f"UTC {start.strftime('%Y-%m-%d %H:%M:%S')} · sources: {', '.join(sorted({event.source for event in events}))}")
        for event in events:
            if event.tag == "LD_MATCH_HEADER":
                self.out(f"- Host: product {event.get('product')} commit {event.get('commit')[:12]} dirty {event.get('dirty')} "
                         f"catalog {event.get('catalog')[:12]} net {event.get('net')} {event.get('address')}")
            elif event.tag == "LD_SELF_MATCH":
                self.out(f"- {event.source}: joined as {event.get('name')} product {event.get('product')} "
                         f"commit {event.get('commit')[:12]} server {event.get('server')}")
        self.out()
        self.roster(events)
        self.result(events, start)
        self.deaths(events, start)
        self.economy(events, start)
        self.marks(events, start)
        self.performance(events)
        self.problems(events, start)

    def roster(self, events):
        rows = {}
        for event in events:
            if event.tag in ("LD_MATCH_ROSTER", "LD_SELF_ROSTER"):
                rows.setdefault(event.get("contender"), event)
        if not rows:
            return
        self.out("### Roster")
        for contender, event in rows.items():
            host = " (host)" if event.get("host") == "1" else ""
            address = f" {event.get('address')}" if event.get("address") else ""
            self.out(f"- {event.get('slot')}: {self.names(contender)} [{event.get('kind')}{host}] {contender[:8]}{address}")
        self.out()

    def result(self, events, start):
        summary = [event for event in events if event.tag in ("LD_MATCH_SUMMARY", "LD_SELF_RESULT")]
        rows = [event for event in events if event.tag in ("LD_MATCH_SUMMARY_ROW", "LD_SELF_RESULT_ROW")]
        if not summary:
            return
        head = summary[0]
        self.out("### Result")
        self.out(f"- {clock(head, start)} {head.get('reason')} winner {self.names(head.get('winner'))} "
                 f"duration {int(head.get('duration_ms', '0') or 0) / 1000:.1f}s")
        seen = set()
        for row in rows:
            if row.get("contender") in seen:
                continue
            seen.add(row.get("contender"))
            build = (f" skills {row.get('skills')} cards {row.get('cards')} items {row.get('equipment')}"
                     if row.get("skills") else "")
            gold = f" gold {row.get('gold')}" if row.get("gold") else ""
            self.out(f"- {self.names(row.get('contender'))}: K/D/A {row.get('kills')}/{row.get('deaths')}/{row.get('assists')} "
                     f"level {row.get('level')}{gold}{build}")
        self.out()

    def deaths(self, events, start):
        deaths = [event for event in events if event.tag in ("LD_DEATH_RECEIPT", "LD_SELF_DEATH")]
        # Without the Host's record, a client still saw every contender death in the public kill feed (once per serial).
        if not any(event.tag == "LD_DEATH_RECEIPT" for event in events):
            seen = set()
            for event in events:
                if event.tag == "LD_SELF_KILLFEED" and event.get("serial") not in seen:
                    seen.add(event.get("serial"))
                    deaths.append(event)
            deaths.sort(key=lambda event: (event.when, event.order))
        if not deaths:
            return
        self.out("### Deaths")
        for death in deaths:
            if death.tag == "LD_SELF_KILLFEED":
                killer = self.names(death.get("killer")) if death.get("killer", "-") != "-" else death.get("cause")
                assisters = ", ".join(self.names(item) for item in death.get("assisters", "-").split(",") if item != "-") or "-"
                self.out(f"- {clock(death, start)} {self.names(death.get('victim'))} died to {killer} ({death.get('cause')}, "
                         f"assists {assisters}) [public kill feed, {death.source}]")
                continue
            if death.tag == "LD_DEATH_RECEIPT":
                victim = death.get("contender")
                killer = self.names(death.get("killer")) if death.get("killer", "-") != "-" else death.get("cause")
                assisters = ", ".join(self.names(item) for item in death.get("assisters", "-").split(",") if item != "-") or "-"
                self.out(f"- {clock(death, start)} {self.names(victim)} died to {killer} via {death.get('fatal')} "
                         f"(assists {assisters}; no_reward {death.get('no_reward')})")
                before = [event for event in events if event.tag == "LD_HIT" and event.get("victim") == victim
                          and 0 <= (death.when - event.when).total_seconds() <= self.death_window]
                for hit in before:
                    self.out(f"  - {clock(hit, start)} {describe_hit(hit, self.names)}")
            else:
                killer = self.names(death.get("killer")) if death.get("killer", "-") != "-" else death.get("killer_name")
                self.out(f"- {clock(death, start)} [{death.source}] own death: killed by {killer or death.get('cause')} "
                         f"({death.get('cause')})")
                before = [event for event in events if event.source == death.source
                          and event.tag in ("LD_SELF_HP", "LD_SELF_STATUS", "LD_SELF_SEEN_CAST")
                          and 0 <= (death.when - event.when).total_seconds() <= self.death_window]
                for item in before:
                    self.out(f"  - {clock(item, start)} {self.names.text(item.message)}")
        self.out()

    def economy(self, events, start):
        rows = [event for event in events if event.tag in ECONOMY_TAGS]
        if not rows:
            return
        self.out("### Economy")
        for event in rows:
            who = self.names(event.get("contender")) if event.get("contender") else event.source
            if event.tag == "LD_REWARD":
                victim = event.get("victim")
                of = (f" of {self.names(victim)}" if event.get("victim_kind") == "contender"
                      else f" of camp:{self.monsters.get(victim, victim[:8])}" if event.get("victim_kind") == "camp"
                      else f" {event.get('event')} #{event.get('serial')}" if event.get("event") else "")
                text = (f"+{event.get('gold')} gold +{event.get('xp')} xp ({event.get('cause')} {event.get('role')}{of}) "
                        f"gold {event.get('gold_after')} level {event.get('level')}")
            elif event.tag in ("LD_LEVEL_UP", "LD_SELF_LEVEL"):
                text = f"level {event.get('level')} (+{event.get('choices_earned')} choices)"
            elif event.tag in ("LD_OFFER", "LD_SELF_OFFER"):
                text = f"offer #{event.get('ordinal')}{' reroll' if event.get('rerolled') == '1' else ''}: {event.get('cards')}"
            elif event.tag == "LD_CHOICE":
                text = f"chose {event.get('card')} rank {event.get('rank')} ({event.get('kind')}) replaced {event.get('replaced')}"
            elif event.tag == "LD_SHOP_TRADE":
                text = f"{event.get('kind')} {event.get('item')} slot {event.get('slot')} gold {event.get('gold_delta')} -> {event.get('gold_after')}"
            elif event.tag == "LD_SHOP_REJECTED":
                text = f"shop {event.get('kind')} rejected: {event.get('reason')}"
            elif event.tag == "LD_SELF_REWARD":
                text = f"+{event.get('amount')} {event.get('kind')}"
            elif event.tag == "LD_SELF_BUILD":
                text = f"build level {event.get('level')}: skills {event.get('skills')} cards {event.get('cards')}"
            else:
                text = f"gold {event.get('gold')} items {event.get('items')}"
            self.out(f"- {clock(event, start)} {who}: {text}")
        self.out()

    def marks(self, events, start):
        marks = [event for event in events if event.tag == "LD_PLAYER_MARK"]
        if not marks:
            return
        self.out("### Marks")
        for mark in marks:
            who = self.names(mark.get("contender"))
            self.out(f"- {clock(mark, start)} mark #{mark.get('seq')} by {who} [{mark.source}, {mark.get('source')}] "
                     f"{mark.get('phase')} at {mark.get('pos')}")
            state = [event for event in events if event.tag in ("LD_HEARTBEAT", "LD_SELF_STATE") and event.when <= mark.when
                     and (event.get("contender") == mark.get("contender") or (event.tag == "LD_SELF_STATE" and event.source == mark.source))]
            if state:
                self.out(f"  - state {clock(state[-1], start)}: {self.names.text(state[-1].message)}")
            around = [event for event in events if event is not mark and event.tag and event.tag not in PERIODIC_TAGS
                      and -self.mark_before <= (event.when - mark.when).total_seconds() <= self.mark_after]
            routine = [event for event in around if event.tag in ROUTINE_TAGS and not self.mark_all]
            listed = [event for event in around if self.mark_all or event.tag not in ROUTINE_TAGS]
            for event in listed[-80:]:
                self.out(f"  - {clock(event, start)} [{event.source}] {self.names.text(event.message)}")
            if routine:
                self.out(f"  - (+{len(routine)} routine AI/order lines; --mark-all lists them)")
        self.out()

    def performance(self, events):
        perf = defaultdict(list)
        for event in events:
            if event.tag == "LD_PERF":
                perf[(event.source, event.get("role"))].append(event)
        nets = defaultdict(list)
        for event in events:
            if event.tag == "LD_NET":
                key = (event.source, self.names(event.get("contender")) if event.get("role") == "host" else "server")
                nets[key].append(event)
        hitches = [event for event in events if event.tag == "LD_PERF_HITCH"]
        if not perf and not nets and not hitches:
            return
        self.out("### Performance and network")
        for (source, role), rows in sorted(perf.items()):
            fps = [float(row.get("fps", 0) or 0) for row in rows]
            frame = [float(row.get("frame_ms", 0) or 0) for row in rows]
            peak = [float(row.get("peak_ms", 0) or 0) for row in rows]
            hitch_count = sum(int(row.get("hitches", 0) or 0) for row in rows)
            self.out(f"- {source} ({role}): {len(rows)} windows, fps min {min(fps):.1f} avg {sum(fps) / len(fps):.1f}, "
                     f"frame avg {sum(frame) / len(frame):.2f} ms, peak max {max(peak):.1f} ms, hitches {hitch_count}")
        for (source, peer), rows in sorted(nets.items()):
            rtt = [float(row.get("rtt_ms")) for row in rows if row.get("rtt_ms", "-") != "-"]
            jitter = [float(row.get("jitter_ms")) for row in rows if row.get("jitter_ms", "-") != "-"]
            loss = [max(float(row.get("loss_in")), float(row.get("loss_out"))) for row in rows if row.get("loss_in", "-") != "-"]
            corrections = sum(int(row.get("corrections", row.get("corrections_sent", 0)) or 0) for row in rows)
            if rtt:
                self.out(f"- {source} -> {peer}: rtt avg {sum(rtt) / len(rtt):.0f} max {max(rtt):.0f} ms, jitter max "
                         f"{max(jitter or [0]):.1f} ms, loss max {max(loss or [0]):.1f}%, corrections {corrections}")
            else:
                self.out(f"- {source} -> {peer}: no latency samples, corrections {corrections}")
        for hitch in sorted(hitches, key=lambda item: -float(item.get("frame_ms", 0) or 0))[:10]:
            self.out(f"- hitch {hitch.get('frame_ms')} ms on {hitch.source} at t={hitch.get('t')} (+{hitch.get('suppressed')} suppressed)")
        self.out()

    def problems(self, events, start):
        bad = [event for event in events if event.verbosity in ("Warning", "Error", "Fatal")]
        if not bad:
            return
        self.out("### Warnings and errors")
        groups = defaultdict(list)
        for event in bad:
            groups[(event.verbosity, event.source, event.tag or event.message[:60])].append(event)
        for (verbosity, source, key), rows in sorted(groups.items(), key=lambda item: item[1][0].when):
            first = rows[0]
            self.out(f"- {verbosity} x{len(rows)} [{source}] {clock(first, start)} {self.names.text(first.message)[:240]}")
        self.out()


def summarize(logs, death_window=10.0, mark_before=30.0, mark_after=10.0, mark_all=False):
    events, names = [], Names()
    for source, rows in logs:
        assign_generations(rows)
        names.learn(rows)
        events.extend(rows)
    events.sort(key=lambda event: (event.when, event.source, event.order))
    report = Report(events, names, death_window, mark_before, mark_after, mark_all)
    report.out("# Lockdown playtest summary")
    report.out()
    by_generation = defaultdict(list)
    for event in events:
        by_generation[event.generation].append(event)
    for generation in sorted(key for key in by_generation if key is not None):
        report.match(generation, by_generation[generation])
    outside = [event for event in by_generation.get(None, []) if event.verbosity in ("Warning", "Error", "Fatal")]
    if outside:
        report.out("## Outside a match")
        report.problems(outside, None)
    if not [key for key in by_generation if key is not None]:
        report.out("No match was found in these logs (no LD_MATCH_HEADER / LD_SELF_MATCH line).")
    return "\n".join(report.lines).rstrip() + "\n"


def collect(paths):
    logs = []
    for path in paths:
        if "=" in path and not Path(path).exists():
            label, _, path = path.partition("=")
        else:
            label = Path(path).stem
        target = Path(path)
        if not target.exists():
            print(f"Not found: {target}", file=sys.stderr)
            continue
        files = sorted(target.glob("*.log")) if target.is_dir() else [target]
        for file in files:
            rows = read_log(file, label if not target.is_dir() else file.stem)
            if any(row.tag for row in rows):
                logs.append((label if not target.is_dir() else file.stem, rows))
    return logs


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("logs", nargs="+", help="log files (optionally label=path) or directories of *.log files")
    parser.add_argument("--out", help="write the report here instead of stdout")
    parser.add_argument("--death-window", type=float, default=10.0, help="seconds of hits listed before each death")
    parser.add_argument("--mark-before", type=float, default=30.0, help="seconds of events listed before each mark")
    parser.add_argument("--mark-after", type=float, default=10.0, help="seconds of events listed after each mark")
    parser.add_argument("--mark-all", action="store_true", help="also list routine AI and order lines around each mark")
    parser.add_argument("--check-client", action="store_true",
                        help="fail when a log that never hosted a match contains Host-only authority lines")
    args = parser.parse_args(argv)
    logs = collect(args.logs)
    if not logs:
        print("No Lockdown LD_ lines found in the given logs.", file=sys.stderr)
        return 2
    if args.check_client:
        failed = False
        for source, rows in logs:
            leaks = check_client(rows)
            for event in leaks[:20]:
                print(f"{source}: Host-only line while not hosting: {event.message[:160]}", file=sys.stderr)
            failed |= bool(leaks)
            if not leaks:
                role = "hosted, and outside hosting" if is_host_log(rows) else "client log"
                print(f"{source}: {role} holds no Host-only authority lines ({len(rows)} lines checked)")
        if failed:
            return 1
    text = summarize(logs, args.death_window, args.mark_before, args.mark_after, args.mark_all)
    if args.out:
        Path(args.out).write_text(text, encoding="utf-8")
    else:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
