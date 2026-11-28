"""Turn Lockdown playtest logs into a per-match timeline (docs/DEVELOPMENT.md, playtest troubleshooting).

Give it the Host log, one or more client logs, or both (``Saved/Logs/Lockdown.log`` of each player, or the Host.log and
ClientN.log files of a scenario run). Lines are merged by their UTC timestamps, split by match generation and every
contender id is replaced by its roster name. For each match the report lists the header and roster, the result, every
death with the hits (Host) or own damage (client) before it, the growth timeline, the events around each player mark,
the frame-time and connection summaries, and the warnings and errors.

From the Host's lines it also computes the gameplay metrics of the #287 spec §16 for each match and across all matches:
damage share by source (basic / skill / card / reaction / other, on contenders and on camps), Q/W/E casts per
person per fight (LD_CAST), time to kill, momentum income by kind (LD_MOMENTUM), auto-empower triggers (LD_EMPOWER: host,
target, cost, paid-and-missed, re-trigger loops, armed time), breakout builds against a measured
same-level bare-build DPS baseline (--baseline-run of BuildEvaluation bare-mirror duels, or --baseline-dps; none by
default), pick rates of offered actives and cards, outrageous hits (>= 60% of the
target's max HP), the longest chain (LD_CHAIN) and damage factors (LD_HIT factor_bp). A metric whose lines are absent
says "no data" instead of a number. --metrics-only prints just these sections.

``--check-client`` exits non-zero when a log carries Host-only authority lines written while that process was not hosting
(outside LD_DEMO_LOBBY .. LD_DEMO_FRONTEND), the check that a client log holds only what that player could see.
"""

import argparse
import json
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
        "LD_HIT", "LD_HEARTBEAT", "LD_REWARD", "LD_LEVEL_UP", "LD_OFFER", "LD_OFFER_WAITING", "LD_CHOICE", "LD_BUILD",
        "LD_CARD", "LD_CARD_OFFER", "LD_DEATH_RECEIPT", "LD_CAMP_DEATH", "LD_MATCH_STARTING", "LD_MATCH_HEADER", "LD_MATCH_ROSTER",
        "LD_MATCH_SUMMARY", "LD_MATCH_SUMMARY_ROW", "LD_MATCH_STARTED", "LD_MATCH_RESULT", "LD_AI_DECISION",
        "LD_AI_SUMMARY", "LD_MEMBER_LEFT", "LD_PLAYER_MARK_REJECTED", "LD_CAST", "LD_MOMENTUM", "LD_EMPOWER",
    }
)
# Once-per-second or ten-second samples: summarized, never listed one by one in a timeline.
PERIODIC_TAGS = frozenset({"LD_HEARTBEAT", "LD_SELF_STATE", "LD_PERF", "LD_NET", "LD_PERF_HITCH"})
# Routine AI and order bookkeeping, and the every-few-seconds auto-empower triggers: counted around a mark, listed only
# with --mark-all.
ROUTINE_TAGS = frozenset({"LD_AI_DECISION", "LD_NAV_ACCEPT", "LD_NAV_END", "LD_CAMP_ATTACK", "LD_CAMP_CHASE_PATH_REJECTED",
                          "LD_EMPOWER"})
GROWTH_TAGS = (
    "LD_REWARD", "LD_LEVEL_UP", "LD_OFFER", "LD_OFFER_WAITING", "LD_CHOICE", "LD_BUILD", "LD_CARD_OFFER", "LD_CARD",
    "LD_SELF_REWARD", "LD_SELF_LEVEL", "LD_SELF_OFFER", "LD_SELF_BUILD",
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
    # The damage multiplier: factor_bp with its per-source factors from P2 on, the single amp_bp before it.
    if event.get("factor_bp") not in (None, "", "10000"):
        extras.append(f"x{int(event.get('factor_bp')) / 10000:.2f}" + (f" {event.get('factors')}" if event.get("factors", "-") != "-" else ""))
    elif event.get("amp_bp") not in (None, "", "10000"):
        extras.append(f"amp x{int(event.get('amp_bp')) / 10000:.2f}")
    extra = f" [{', '.join(extras)}]" if extras else ""
    return (f"{source} -> {victim} {event.get('via')} {event.get('class')} dmg {event.get('dmg')} "
            f"(shield {event.get('absorbed')}) hp {event.get('hp')}{extra}")


class Report:
    def __init__(self, events, names, death_window, mark_before, mark_after, mark_all=False,
                 baseline=None, baseline_source=None, metrics_only=False):
        self.events = events
        self.baseline = baseline
        self.baseline_source = baseline_source
        self.metrics_only = metrics_only
        self.metrics = []
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
        if not self.metrics_only:
            self.roster(events)
            self.result(events, start)
        self.gameplay_metrics(events)
        if self.metrics_only:
            return
        self.deaths(events, start)
        self.growth(events, start)
        self.marks(events, start)
        self.performance(events)
        self.problems(events, start)

    def gameplay_metrics(self, events):
        metrics = match_metrics(events, self.baseline)
        self.metrics.append(metrics)
        self.out("### Metrics (spec §16)")
        for text in metric_lines(metrics, self.names, self.baseline_source):
            self.out(text)
        self.out()

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
            build = (f" skills {row.get('skills')} blueprints {row.get('blueprints')} points {row.get('points')}"
                 if row.get("skills") else "")
            self.out(f"- {self.names(row.get('contender'))}: K/D/A {row.get('kills')}/{row.get('deaths')}/{row.get('assists')} "
                     f"level {row.get('level')}{build}")
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

    def growth(self, events, start):
        rows = [event for event in events if event.tag in GROWTH_TAGS]
        if not rows:
            return
        self.out("### Growth")
        for event in rows:
            who = self.names(event.get("contender")) if event.get("contender") else event.source
            if event.tag == "LD_REWARD":
                victim = event.get("victim")
                of = (f" of {self.names(victim)}" if event.get("victim_kind") == "contender"
                      else f" of camp:{self.monsters.get(victim, victim[:8])}" if event.get("victim_kind") == "camp"
                      else f" {event.get('event')} #{event.get('serial')}" if event.get("event") else "")
                text = (f"+{event.get('xp')} xp ({event.get('cause')} {event.get('role')}{of}) "
                        f"xp {event.get('xp_after')} level {event.get('level')}")
            elif event.tag in ("LD_LEVEL_UP", "LD_SELF_LEVEL"):
                text = f"level {event.get('level')} (+{event.get('choices_earned')} active offers)"
            elif event.tag in ("LD_OFFER", "LD_SELF_OFFER"):
                text = f"offer #{event.get('ordinal')}: {event.get('cards')}"
            elif event.tag == "LD_OFFER_WAITING":
                text = f"offer #{event.get('ordinal')} waits for an eligible active"
            elif event.tag == "LD_CHOICE":
                text = (f"chose {event.get('card')} ({event.get('result')}, slot {event.get('slot')}, "
                        f"stored {event.get('stored')})")
            elif event.tag == "LD_BUILD":
                text = (f"{event.get('kind')} {event.get('card')} slot {event.get('slot')} points {event.get('points')} "
                        f"unspent {event.get('unspent')}")
            elif event.tag == "LD_CARD_OFFER":
                text = f"card offer #{event.get('ordinal')} ({event.get('source')}): {event.get('cards')}"
            elif event.tag == "LD_CARD":
                text = (f"{event.get('kind')} {event.get('card')}#{event.get('serial')} installed {event.get('installed')} "
                        f"on {event.get('target')} capacity {event.get('capacity')}")
            elif event.tag == "LD_SELF_REWARD":
                text = f"+{event.get('amount')} {event.get('kind')}"
            else:
                text = (f"build level {event.get('level')}: skills {event.get('skills')} blueprints {event.get('blueprints')} "
                        f"points {event.get('points')} unspent {event.get('unspent')}")
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


# ---------------------------------------------------------------- §16 metrics (#287 spec §16, #290)
# Read from the Host's authority lines only; a client log alone has no LD_HIT and every metric says so.

FIGHT_GAP_MS = 5000  # MOMENTUM.IdleDelaySec: a combat window ends 5 s after the last combat event
OUTRAGEOUS_SHARE = 0.6  # a single hit of at least 60% of the target's max HP
BREAKOUT_RATIO = 3.0  # actual output / same-level bare-build output
CHAIN_OUTRAGEOUS_STEPS = 5
# LD_CAST is written at the end of the authority advance that committed the release: at most one advance (about 17 ms at
# 60 FPS) after it, so a release just before its caster's death can carry a time just after the death receipt.
CAST_LAG_MS = 50
LOW_PICK_RATE = 0.04
# The same-level bare build (no cards, no points, level-1 skills) has no default output rate: the breakout ratio needs a
# measured one (--baseline-run of BuildEvaluation bare-mirror duels, or --baseline-dps). The old spec §3.9 estimate
# (L1 72, L10 154, extended linearly) predates the current HP curve and overstated the native L16 bare mirror ~1.6x.
# A reaction's settle hit carries its applier's own source and LD_HIT has no reaction field, so reaction damage stays in
# its applier's category; a separate reaction share would need an authority field on LD_HIT.
DAMAGE_CATEGORIES = (("basic", "basic"), ("skill", "skill"), ("card", "card"), ("other", "other"))
DAMAGE_NOTE = "reaction settles count under their applier's source; reaction share: no data (LD_HIT has no reaction field)"
MOMENTUM_INCOME = ("basic", "combat", "skill", "reaction", "dodge", "other")
NO_DATA = "no data"
NOT_APPLICABLE = "n/a (the Host log has no such mechanic)"
NO_BASELINE = ("no measured same-level bare-build baseline; pass --baseline-run with BuildEvaluation bare-mirror runs "
               "or --baseline-dps")
# Auto-empower (Batch 01 momentum redesign): one LD_EMPOWER per empowered release (host, target, momentum before and
# cost); LD_HIT empower=<serial> marks the hits of that release and its derived events; LD_MOMENTUM empower=<count> and
# armed_ms=<ms at or above the threshold> mark a Host that has the mechanic even when nobody triggered it.
EMPOWER_HOSTS = ("Basic", "Q", "W", "E", "D")
EMPOWER_TARGETS = ("contender", "camp", "none")
# Two triggers of one body closer than this cannot both be paid from income: a refund loop or a derived re-trigger.
EMPOWER_RAPID_MS = 250


def authority_ms(event, key="t"):
    value = event.get(key, "")
    return int(value) if value.lstrip("-").isdigit() else None


def number(event, key, default=0.0):
    try:
        return float(event.get(key, default))
    except ValueError:
        return default


def parse_baseline(text):
    """'1=72,10=154' -> ((1, 72.0), (10, 154.0)): distinct levels, positive DPS."""
    points = []
    for part in text.split(","):
        level, _, dps = part.partition("=")
        points.append((int(level), float(dps)))
    points.sort()
    if not points or len({level for level, _ in points}) != len(points) or any(dps <= 0 for _, dps in points):
        raise ValueError("--baseline-dps takes level=dps pairs with distinct levels and positive DPS")
    return tuple(points)


def baseline_at(points, level):
    """The bare-build DPS at level, linear between the two surrounding measured levels; None outside the measured levels
    (a guess along the nearest segment is not a measurement)."""
    if not points[0][0] <= level <= points[-1][0]:
        return None
    for (low, low_dps), (high, high_dps) in zip(points, points[1:]):
        if level <= high:
            return low_dps + (high_dps - low_dps) * (level - low) / (high - low)
    return points[0][1]


def baseline_from_runs(paths):
    """((level, DPS), ...) and its description from BuildEvaluation duel_site runs: every Completed, uncensored Case of an
    arm whose two sides keep every point unspent (a bare mirror) gives the duelists' mean output over its engage-to-kill
    time; Invalid Cases (no engagement, never cast, contamination) never count."""
    samples, runs = defaultdict(list), []
    for path in paths:
        run = Path(path)
        manifest = json.loads((run / "manifest.json").read_text(encoding="utf-8"))
        bare = {arm["id"] for arm in manifest["design"]["arms"]
                if all(arm["plan"][role].get("keep_points_unspent") for role in ("subject", "opponent"))}
        runs.append(run.name)
        for text in (run / "cases.jsonl").read_text(encoding="utf-8").splitlines():
            case = json.loads(text)
            metrics = case.get("metrics", {})
            ttk = metrics.get("engage_to_kill_ms") or {}
            levels = {build.get("level") for build in case.get("facts", {}).get("builds", {}).values()}
            if (case.get("status") != "Completed" or case.get("arm") not in bare or ttk.get("censored")
                    or not ttk.get("value") or len(levels) != 1):
                continue
            output = (metrics["damage_dealt_hp"]["value"] + metrics["damage_taken_hp"]["value"]) / 2
            samples[levels.pop()].append(output * 1000 / ttk["value"])
    if not samples:
        raise ValueError(f"no Completed, uncensored bare-mirror Case in {', '.join(runs)}")
    points = tuple((level, sum(rows) / len(rows)) for level, rows in sorted(samples.items()))
    source = ("measured bare mirror " + ", ".join(f"L{level} {dps:.1f} DPS (n={len(samples[level])})" for level, dps in points)
              + f" from {', '.join(runs)}")
    return points, source


def hit_category(event):
    via = event.get("via", "")
    if via == "basic":
        return "basic"
    if via.startswith("learned:"):
        return "skill"
    if via.startswith("card:"):
        return "card"
    return "other"


def training_scope(events, roster):
    """What the rest-room private training leaves in the Host log: its dummies (LD_TRAINING_DUMMY_SPAWN, or contender-kind
    bodies outside the roster) and the rest lives of the contenders (every LD_HEARTBEAT of that life inside a rest:
    from the match start to the first LD_FIELD_ENTER, then each LD_REST_ENTER to the next LD_FIELD_ENTER). A log without
    those lines has no training scope."""
    dummies = {event.get("dummy") for event in events if event.tag == "LD_TRAINING_DUMMY_SPAWN"}
    if roster:
        for event in events:
            if event.tag == "LD_HIT":
                for side in ("src", "victim"):
                    if event.get(f"{side}_kind") == "contender" and event.get(side) not in roster:
                        dummies.add(event.get(side))
    dummies.discard("")
    marks = sorted((authority_ms(event, "at_ms"), event.tag) for event in events
                   if event.tag in ("LD_FIELD_ENTER", "LD_REST_ENTER") and authority_ms(event, "at_ms") is not None)
    windows, start = [], None
    if marks:
        start = float("-inf")
    for at, tag in marks:
        if tag == "LD_FIELD_ENTER" and start is not None:
            windows.append((start, at))
            start = None
        elif tag == "LD_REST_ENTER" and start is None:
            start = at
    if start is not None and marks:
        windows.append((start, float("inf")))
    seen = defaultdict(list)
    for event in events:
        if event.tag == "LD_HEARTBEAT" and authority_ms(event) is not None:
            seen[(event.get("contender"), event.get("life"))].append(authority_ms(event))
    rest_lives = {key for key, times in seen.items()
                  if windows and all(any(low <= t < high for low, high in windows) for t in times)}
    return dummies, rest_lives


def field_share(events):
    """(field ms, match ms) of a finished match: the field segments before the showdown (each LD_FIELD_ENTER final=0 to
    the next rest, showdown entry or the result) over the whole match from its start to LD_MATCH_RESULT. None for a
    match without a result or without a field entry."""
    ended = next((authority_ms(event, "ended_ms") for event in events if event.tag == "LD_MATCH_RESULT"), None)
    started = next((authority_ms(event, "started_at_ms") for event in events if event.tag == "LD_MATCH_STARTED"), None) or 0
    marks = sorted((authority_ms(event, "at_ms"), event.tag == "LD_FIELD_ENTER" and event.get("final") == "0")
                   for event in events if event.tag in ("LD_FIELD_ENTER", "LD_REST_ENTER")
                   and authority_ms(event, "at_ms") is not None)
    if ended is None or ended <= started or not any(field for _, field in marks):
        return None
    ends = [at for at, _ in marks[1:]] + [ended]
    return sum(min(end, ended) - at for (at, field), end in zip(marks, ends) if field and at < ended), ended - started


def field_share_text(shares):
    shares = [share for share in shares if share]
    if not shares:
        return f"{NO_DATA} (no finished match with LD_FIELD_ENTER and LD_MATCH_RESULT)"
    field, total = sum(share[0] for share in shares), sum(share[1] for share in shares)
    return f"{field / total:.1%} ({field / 1000:.0f} s of {total / 1000:.0f} s)"


class LevelTrack:
    """Each contender's level by authority time, from LD_LEVEL_UP (level 1 before the first)."""

    def __init__(self, events):
        self.steps = defaultdict(list)
        for event in events:
            if event.tag == "LD_LEVEL_UP" and authority_ms(event) is not None:
                after = event.get("level", "").partition("->")[2]
                if after.isdigit():
                    self.steps[event.get("contender")].append((authority_ms(event), int(after)))
        for rows in self.steps.values():
            rows.sort()

    def __call__(self, contender, t):
        level = 1
        for at, after in self.steps.get(contender, []):
            if at > t:
                break
            level = after
        return level


def stats_text(values, digits=1, unit=""):
    values = sorted(values)
    if not values:
        return NO_DATA
    mean = sum(values) / len(values)
    middle = len(values) // 2
    median = values[middle] if len(values) % 2 else (values[middle - 1] + values[middle]) / 2
    return (f"n={len(values)} mean {mean:.{digits}f}{unit} median {median:.{digits}f}{unit} "
            f"min {values[0]:.{digits}f}{unit} max {values[-1]:.{digits}f}{unit}")


@dataclass
class Fight:
    contender: str
    start: int
    end: int
    casts: int = 0
    pvp_hits: int = 0
    dealt: float = 0.0
    opponents: set = field(default_factory=set)
    died: bool = False
    lingering: int = 0
    level: int = 1
    same_level: bool = True


def contender_fights(contender, casts, hits, deaths, contenders):
    """One contender's fights: its LD_CAST releases and the hits it exchanged with other contenders, split where the gap
    exceeds FIGHT_GAP_MS (the combat window) and ended by its death. Fights without a hit between contenders are dropped."""
    timeline = [(authority_ms(event), 0, "cast", event) for event in casts if event.get("contender") == contender]
    timeline += [(authority_ms(event), 1, "hit", event) for event in hits
                 if event.get("protected") != "1" and event.get("src") != event.get("victim")
                 and event.get("src") in contenders and event.get("victim") in contenders
                 and contender in (event.get("src"), event.get("victim"))]
    timeline += [(authority_ms(event), 2, "death", event) for event in deaths if event.get("contender") == contender]
    timeline.sort(key=lambda row: (row[0], row[1]))
    fights, current = [], None
    for t, _, kind, event in timeline:
        if current and current.died and kind == "cast" and t - current.end <= CAST_LAG_MS:
            current.casts += max(int(number(event, "added", 1)), 1)
            continue
        # A released effect outlives its caster (a zone, a projectile in flight): its hits after the death, within the
        # combat window, still belong to the fight that ended with that death instead of opening a fight of a dead body.
        if (current and current.died and kind == "hit" and event.get("src") == contender
                and t - current.end <= FIGHT_GAP_MS):
            current.opponents.add(event.get("victim"))
            current.dealt += number(event, "dmg")
            current.lingering += 1
            continue
        if current and (t - current.end > FIGHT_GAP_MS or current.died):
            fights.append(current)
            current = None
        if kind == "death":
            if current:
                current.end, current.died = t, True
            continue
        if current is None:
            current = Fight(contender, t, t)
        current.end = t
        if kind == "cast":
            current.casts += max(int(number(event, "added", 1)), 1)
            continue
        current.pvp_hits += 1
        current.opponents.add(event.get("victim") if event.get("src") == contender else event.get("src"))
        if event.get("src") == contender:
            current.dealt += number(event, "dmg")
    if current:
        fights.append(current)
    return [fight for fight in fights if fight.pvp_hits]


def kill_times(deaths, hits, contenders, levels):
    """Each contender death by another contender: from the first damage between the pair in the engagement that ended
    in the death (their hits either way, no more than FIGHT_GAP_MS apart) to the death receipt. A kill counts as mutual
    only when the victim also hit the killer in that engagement; a one-sided kill (an idle or fleeing victim) is no time
    to kill of a fight and is only counted."""
    kills = []
    for death in deaths:
        victim, killer, t_death = death.get("contender"), death.get("killer"), authority_ms(death)
        if killer in ("", "-", victim) or killer not in contenders:
            continue
        pair = sorted(((authority_ms(event), event.get("src")) for event in hits if event.get("protected") != "1"
                       and {event.get("src"), event.get("victim")} == {victim, killer} and authority_ms(event) <= t_death),
                      reverse=True)
        if not pair:
            continue
        start = last = t_death
        sources = set()
        for t, source in pair:
            if last - t > FIGHT_GAP_MS:
                break
            start = last = t
            sources.add(source)
        # Levels just before the death: the killer's reward for it lands at the same authority time.
        level = levels(victim, t_death - 1)
        kills.append({"victim": victim, "killer": killer, "ttk_ms": t_death - start, "level": level,
                      "same_level": levels(killer, t_death - 1) == level, "mutual": victim in sources})
    return kills


def match_metrics(events, baseline):
    """Every §16 number this match's Host lines support; a value is None (or empty) when its lines are absent."""
    hits = [event for event in events if event.tag == "LD_HIT" and authority_ms(event) is not None]
    # The roster when the log has one: rest-room training dummies are contender-kind bodies outside it.
    contenders = {event.get("contender") for event in events if event.tag == "LD_MATCH_ROSTER"}
    if not contenders:
        contenders |= {event.get("src") for event in hits if event.get("src_kind") == "contender"}
        contenders |= {event.get("victim") for event in hits if event.get("victim_kind") == "contender"}
    contenders.discard("")
    dummies, rest_lives = training_scope(events, contenders)
    contenders -= dummies
    casts = [event for event in events if event.tag == "LD_CAST" and authority_ms(event) is not None]
    deaths = [event for event in events if event.tag == "LD_DEATH_RECEIPT" and authority_ms(event) is not None]
    levels = LevelTrack(events)
    out = {"has_hits": bool(hits), "has_casts": bool(casts), "contenders": sorted(contenders)}

    share = {"contender": defaultdict(float), "camp": defaultdict(float)}
    for event in hits:
        if event.get("src") not in contenders or event.get("src") == event.get("victim") or event.get("protected") == "1":
            continue
        group = "camp" if event.get("victim_kind") == "camp" else "contender" if event.get("victim") in contenders else None
        if group is None:
            continue
        share[group][hit_category(event)] += number(event, "dmg")
    out["damage_share"] = share

    fights = [fight for contender in sorted(contenders) for fight in contender_fights(contender, casts, hits, deaths, contenders)]
    for fight in fights:
        fight.level = levels(fight.contender, fight.start)
        fight.same_level = all(levels(other, fight.start) == fight.level for other in fight.opponents)
    out["fights"] = fights
    # An AI contender whose every fight holds no Q/W/E release: an execution failure, not a style (a human is not judged).
    humans = {event.get("contender") for event in events if event.tag == "LD_MATCH_ROSTER" and event.get("kind") == "Human"}
    out["never_cast"] = ({contender: sum(1 for fight in fights if fight.contender == contender)
                          for contender in sorted(contenders - humans)
                          if any(fight.contender == contender for fight in fights)
                          and not any(fight.casts for fight in fights if fight.contender == contender)}
                         if casts else None)
    out["kills"] = kill_times(deaths, hits, contenders, levels)

    # Momentum and perfect dodges of the field lives only: rest lives train against their dummies.
    def field_life(event):
        return event.get("entity") not in dummies and (event.get("entity"), event.get("life")) not in rest_lives

    # LD_MOMENTUM is written when a body leaves (death, rest, retirement); a life still alive when the match ends or the log
    # stops has none. Dodges are counted over the same lives, and the field lives without a ledger are reported.
    ledgers = [event for event in events if event.tag == "LD_MOMENTUM" and field_life(event)]
    ledger_lives = {(event.get("entity"), event.get("life")) for event in ledgers}
    field_lives = {(event.get("contender"), event.get("life")) for event in events if event.tag == "LD_HEARTBEAT"
                   and event.get("contender") in contenders and field_life({"entity": event.get("contender"),
                                                                               "life": event.get("life")})}
    out["lives_without_ledger"] = len(field_lives - ledger_lives)
    momentum = defaultdict(float)
    for event in ledgers:
        # A Host without overdrive or perfect dodges writes no overdrive= or dodge= field: those keys stay absent.
        for key in (*MOMENTUM_INCOME, "idle", "spent", "overdrive"):
            if key in event.fields:
                momentum[key] += number(event, key)
    out["momentum"] = dict(momentum) if ledgers else None
    out["momentum_lives"] = len(ledgers)
    dodge_lines = [event for event in events if event.tag == "LD_PERFECT_DODGE"]
    out["perfect_dodges"] = (sum(1 for event in dodge_lines if field_life(event)
                                 and (event.get("entity"), event.get("life")) in ledger_lives)
                             if dodge_lines or "dodge" in momentum else None)
    out["training_lines"] = sum(1 for event in events if event.tag in ("LD_MOMENTUM", "LD_PERFECT_DODGE")
                                and not field_life(event))
    empowers = [event for event in events if event.tag == "LD_EMPOWER" and authority_ms(event) is not None
                and field_life(event)]
    out["empower"] = (empower_stats(empowers, hits, ledgers, len(contenders), field_share(events))
                      if empowers or any("empower" in event.fields for event in ledgers) else None)

    # Breakout: damage to contenders over the contender's fights against the same-level bare build over the same time;
    # only fights at a measured baseline level count, the others are reported as left out.
    ratios, outside = {}, 0
    for contender in sorted(contenders) if baseline else ():
        own = [fight for fight in fights if fight.contender == contender]
        inside = [(fight, baseline_at(baseline, fight.level)) for fight in own]
        inside = [(fight, dps) for fight, dps in inside if dps is not None]
        outside += len(own) - len(inside)
        expected = sum(dps * max(fight.end - fight.start, 1000) / 1000 for fight, dps in inside)
        if expected:
            ratios[contender] = sum(fight.dealt for fight, _ in inside) / expected
    out["output_ratio"] = ratios if baseline else None
    out["baseline_outside"] = outside

    outrageous = [(number(event, "dmg") / number(event, "max_hp"), event) for event in hits
                  if event.get("victim") in contenders and event.get("src") != event.get("victim")
                  and number(event, "max_hp") > 0 and number(event, "dmg") >= OUTRAGEOUS_SHARE * number(event, "max_hp")]
    out["outrageous"] = sorted(outrageous, key=lambda row: -row[0])

    chains = [int(number(event, "steps")) for event in events if event.tag == "LD_CHAIN"]
    out["chains"] = chains or None
    match_hits = [event for event in hits if event.get("src") not in dummies and event.get("victim") not in dummies]
    factored = [event for event in match_hits if event.get("factor_bp")]
    out["factors"] = None
    if factored:
        sources = defaultdict(int)
        for event in factored:
            for part in event.get("factors", "").split(","):
                key, _, bp = part.rpartition(":")
                if key and bp.lstrip("-").isdigit():
                    sources[key] += 1
        out["factors"] = {"hits": len(factored), "max_bp": max(int(number(event, "factor_bp")) for event in factored),
                          "sources": dict(sources)}
    out["amp_max_bp"] = max((int(number(event, "amp_bp")) for event in match_hits if event.get("amp_bp")), default=None)
    out["reaction_kinds"] = sorted({event.get("kind") for event in events if event.tag == "LD_REACTION"
                                    and event.get("trigger") not in dummies and event.get("primary") not in dummies})

    # Field objectives (F2 arena, F3 mini flag): each serial should log Announced, Open and one terminal phase once.
    objectives = {}
    for event in events:
        if event.tag == "LD_OBJECTIVE" and event.get("serial", "").isdigit():
            row = objectives.setdefault(int(event.get("serial")), {"kind": event.get("kind"), "phases": [], "lines": 0})
            row["lines"] += 1
            if event.get("phase") not in row["phases"]:
                row["phases"].append(event.get("phase"))
    out["objectives"] = objectives

    # Offers and picks: an LD_OFFER (actives) or LD_CARD_OFFER (cards) lists the offered ids; LD_CHOICE and an LD_CARD
    # PickCard is a pick; an LD_CARD with installed=1 is an installation. Actives are active_*; every other id is a card.
    offered, picked, installed = defaultdict(int), defaultdict(int), defaultdict(set)
    for event in events:
        card = event.get("card", "-")
        if event.tag in ("LD_OFFER", "LD_CARD_OFFER"):
            for offer in event.get("cards", "").split(","):
                if offer and offer != "-":
                    offered[offer] += 1
        elif event.tag == "LD_CHOICE" and card not in ("", "-"):
            picked[card] += 1
        elif event.tag == "LD_CARD" and card not in ("", "-"):
            if event.get("kind") == "PickCard":
                picked[card] += 1
            if event.get("installed") == "1":
                installed[card].add(event.get("contender"))
    out["offered"], out["picked"], out["installed"] = dict(offered), dict(picked), dict(installed)

    summary = next((event for event in events if event.tag == "LD_MATCH_SUMMARY"), None)
    rows = [event for event in events if event.tag == "LD_MATCH_SUMMARY_ROW" and summary is not None
            and event.get("contender") == summary.get("winner")]
    out["winner_build"] = build_fingerprint(rows[0]) if rows else None
    out["field_share"] = field_share(events)
    return out


def build_fingerprint(row):
    """A winner's build: its Q/W/E actives, allocated points and every installed card copy with its place, all from the
    LD_MATCH_SUMMARY_ROW (`cards=+card_x#serial@Kind[:active][:Element]`; the serial is dropped, duplicates count). None when the
    row has no points or cards field."""
    if not row.get("skills") or "points" not in row.fields or "cards" not in row.fields:
        return None
    actives = sorted(item.partition("/")[0] for item in row.get("skills").split(",") if item and item != "-")
    installed = []
    for item in row.get("cards", "-").split(","):
        if item.startswith("+"):
            card, _, place = item[1:].partition("@")
            installed.append(f"{card.partition('#')[0]}:{place}")
    return (tuple(actives), row.get("points"), tuple(sorted(installed)))


def share_line(amounts):
    total = sum(amounts.values())
    if not total:
        return NO_DATA
    return f"{total:.0f} dmg: " + ", ".join(f"{label} {100 * amounts.get(key, 0) / total:.1f}%" for key, label in DAMAGE_CATEGORIES)


def casts_text(fights, has_casts):
    if not has_casts:
        return f"{NO_DATA} (no LD_CAST line: the Host build predates the cast trace)"
    return stats_text([fight.casts for fight in fights])


def ttk_text(kills):
    mutual = [kill["ttk_ms"] / 1000 for kill in kills if kill["mutual"]]
    one_sided = len(kills) - len(mutual)
    return stats_text(mutual, 1, " s") + (f" (+{one_sided} one-sided kills)" if one_sided else "")


def momentum_text(momentum, lives, dodges):
    income = sum(momentum.get(key, 0) for key in MOMENTUM_INCOME) if momentum else 0
    if not income:
        return f"{NO_DATA} (no LD_MOMENTUM line)"
    parts = ", ".join(f"{key} {100 * momentum.get(key, 0) / income:.1f}%" for key in MOMENTUM_INCOME if key in momentum)
    overdrives = f"{momentum['overdrive']:.0f}" if "overdrive" in momentum else NOT_APPLICABLE
    return (f"{income:.0f} over {lives} field lives ({parts}); idle drift {momentum.get('idle', 0):+.0f}, spent "
            f"{momentum.get('spent', 0):.0f}, overdrives {overdrives}, "
            f"perfect dodges {NOT_APPLICABLE if dodges is None else dodges}")


def empower_stats(empowers, hits, ledgers, contenders, share):
    """Additive auto-empower totals of one match's field lives (empower_merge adds matches)."""
    tagged = defaultdict(int)
    for event in hits:
        if event.get("empower") not in ("", "-"):
            tagged[(event.get("src"), event.get("empower"))] += 1
    rapid, last = 0, {}
    for event in sorted(empowers, key=lambda event: (event.get("entity"), authority_ms(event))):
        key = (event.get("entity"), event.get("life"))
        if key in last and authority_ms(event) - last[key] < EMPOWER_RAPID_MS:
            rapid += 1
        last[key] = authority_ms(event)
    has_tags = any("empower" in event.fields for event in hits)
    armed = [number(event, "armed_ms") for event in ledgers if "armed_ms" in event.fields]
    return {
        "triggers": len(empowers),
        "hosts": {host: sum(1 for event in empowers if event.get("host") == host) for host in EMPOWER_HOSTS},
        "targets": {target: sum(1 for event in empowers if event.get("target") == target) for target in EMPOWER_TARGETS},
        "cost": sum(number(event, "cost") for event in empowers),
        "before": sum(number(event, "before") for event in empowers),
        # A trigger none of whose hits landed: the release paid and missed. Unknown when LD_HIT has no empower field.
        "whiffs": (sum(1 for event in empowers if not tagged.get((event.get("entity"), event.get("serial")))) if has_tags
                   else None),
        "max_hits": max(tagged.values(), default=0) if has_tags else None,
        "rapid": rapid,
        "armed_ms": sum(armed) if armed else None,
        "contender_field_ms": contenders * share[0] if share else None,
    }


def empower_merge(total, stats):
    if stats is None:
        return total
    if total is None:
        return {key: dict(value) if isinstance(value, dict) else value for key, value in stats.items()}
    for key, value in stats.items():
        if isinstance(value, dict):
            for inner, count in value.items():
                total[key][inner] += count
        elif key == "max_hits":
            total[key] = None if value is None or total[key] is None else max(total[key], value)
        else:
            total[key] = None if value is None or total[key] is None else total[key] + value
    return total


def empower_text(stats):
    if stats is None:
        return NOT_APPLICABLE
    count = stats["triggers"]
    minutes = (stats["contender_field_ms"] or 0) / 60000
    armed = (f"{100 * stats['armed_ms'] / stats['contender_field_ms']:.0f}% of contender field time"
             if stats["armed_ms"] is not None and stats["contender_field_ms"]
             else f"{NO_DATA} (no armed_ms or no finished field time)")
    if not count:
        return f"0 triggers; armed {armed}"
    def shares(counts):
        return ", ".join(f"{key} {100 * value / count:.0f}%" for key, value in counts.items() if value)
    rate = f"{count / minutes:.2f} per contender field minute" if minutes else f"rate {NO_DATA} (no finished field time)"
    whiffs = (f"{100 * stats['whiffs'] / count:.0f}%" if stats["whiffs"] is not None
              else f"{NO_DATA} (LD_HIT has no empower field)")
    most = stats["max_hits"] if stats["max_hits"] is not None else NO_DATA
    return (f"{count} triggers ({rate}); hosts {shares(stats['hosts'])}; targets {shares(stats['targets'])}; "
            f"paid and missed {whiffs}; mean cost {stats['cost'] / count:.1f}, mean momentum before "
            f"{stats['before'] / count:.1f}; re-triggers within {EMPOWER_RAPID_MS} ms {stats['rapid']}; "
            f"most hits from one trigger {most}; armed {armed}")


OBJECTIVE_PHASES = {"1": "Announced", "2": "Open", "3": "Won", "4": "Expired"}  # ELDObjectivePhase (LDFieldPacing.h)
OBJECTIVE_TERMINAL = ("3", "4")


def objective_text(objectives):
    if not objectives:
        return f"{NO_DATA} (no LD_OBJECTIVE line)"
    parts = []
    for serial, row in sorted(objectives.items()):
        flow = "→".join(OBJECTIVE_PHASES.get(phase, phase) for phase in row["phases"])
        end = "" if any(phase in OBJECTIVE_TERMINAL for phase in row["phases"]) else ", never ended"
        repeated = row["lines"] - len(row["phases"])
        parts.append(f"#{serial} {row['kind']} {flow}{end}" + (f", {repeated} repeated lines" if repeated else ""))
    return "; ".join(parts)


def metric_lines(metrics, names, baseline_source):
    share_line_text = (f"- Field time share (field segments before the showdown / whole match): "
                       f"{field_share_text([metrics['field_share']])}")
    if not metrics["has_hits"]:
        # The phase lines stand without a hit: a match nobody fought in still has its field share.
        return [f"- {NO_DATA}: the metrics need the Host log (LD_HIT, LD_CAST, LD_DEATH_RECEIPT, LD_MOMENTUM).",
                *([share_line_text] if metrics["field_share"] else [])]
    lines = [f"- Damage share, hits on contenders: {share_line(metrics['damage_share']['contender'])}",
             f"- Damage share, hits on camps: {share_line(metrics['damage_share']['camp'])}",
             f"  - {DAMAGE_NOTE}"]
    fights, kills = metrics["fights"], metrics["kills"]
    lines.append(f"- Casts per person per fight (Q/W/E releases): {casts_text(fights, metrics['has_casts'])}")
    lines.append(f"  - same-level fights: {casts_text([fight for fight in fights if fight.same_level], metrics['has_casts'])}")
    if metrics["never_cast"]:
        lines.append(f"  - AI that never cast in any of their fights: {len(metrics['never_cast'])} ("
                     + ", ".join(f"{names(contender)} {count} fights" for contender, count in metrics["never_cast"].items()) + ")")
    lines.append(f"- Time to kill (first damage between the pair to the death): {ttk_text(kills)}")
    for level in sorted({kill["level"] for kill in kills if kill["same_level"]}):
        lines.append(f"  - same level L{level}: {ttk_text([kill for kill in kills if kill['same_level'] and kill['level'] == level])}")
    lines.append(f"- Momentum income: {momentum_text(metrics['momentum'], metrics['momentum_lives'], metrics['perfect_dodges'])}"
                 + (f" ({metrics['training_lines']} rest-training ledger/dodge lines left out)" if metrics["training_lines"] else "")
                 + (f"; incomplete: {metrics['lives_without_ledger']} field lives have no ledger (alive at the match end)"
                    if metrics["lives_without_ledger"] else ""))
    lines.append(f"- Auto-empower: {empower_text(metrics['empower'])}")
    ratios = metrics["output_ratio"]
    if ratios is None:
        lines.append(f"- Breakout builds: {NO_DATA} ({NO_BASELINE})")
    else:
        breakouts = sum(1 for ratio in ratios.values() if ratio >= BREAKOUT_RATIO)
        ranked = ", ".join(f"{names(contender)} x{ratio:.2f}" for contender, ratio in sorted(ratios.items(), key=lambda row: -row[1]))
        lines.append(f"- Breakout builds (output / same-level bare build >= {BREAKOUT_RATIO:g}, baseline {baseline_source}): "
                     f"{breakouts}" + (f" ({ranked})" if ranked else "")
                     + (f"; {metrics['baseline_outside']} fights outside the measured levels left out"
                        if metrics["baseline_outside"] else ""))
    outrageous = metrics["outrageous"]
    lines.append(f"- Outrageous hits (>= {OUTRAGEOUS_SHARE:.0%} of the target's max HP): {len(outrageous)}")
    for ratio, event in outrageous[:5]:
        lines.append(f"  - t={event.get('t')} {names(event.get('src'))} -> {names(event.get('victim'))} "
                     f"{event.get('via')} {event.get('dmg')} ({ratio:.0%} of {event.get('max_hp')})")
    chains = metrics["chains"]
    lines.append(f"- Longest chain: {max(chains)} steps ({sum(1 for steps in chains if steps >= CHAIN_OUTRAGEOUS_STEPS)} of "
                 f"{len(chains)} chains >= {CHAIN_OUTRAGEOUS_STEPS})" if chains
                 else f"- Longest chain: {NO_DATA} (no LD_CHAIN line)")
    factors = metrics["factors"]
    if factors:
        top = ", ".join(f"{key} {count}" for key, count in sorted(factors["sources"].items(), key=lambda row: -row[1])[:8])
        lines.append(f"- Damage factors: {factors['hits']} hits, max factor_bp {factors['max_bp']}; sources {top or '-'}")
    else:
        old = f"; old amp_bp peaks at {metrics['amp_max_bp']}" if metrics["amp_max_bp"] is not None else ""
        lines.append(f"- Damage factors: {NO_DATA} (LD_HIT has no factor_bp{old})")
    kinds = metrics["reaction_kinds"]
    lines.append(f"- Reaction kinds: {len(kinds)}" + (f" ({', '.join(kinds)})" if kinds else ""))
    lines.append(f"- Field objectives: {objective_text(metrics['objectives'])}")
    cards = sum(count for card, count in metrics["offered"].items() if not card.startswith("active_"))
    lines.append(f"- Cards offered: {cards}" if cards else f"- Cards offered: {NO_DATA} (no card offer)")
    lines.append(share_line_text)
    return lines


def pick_lines(all_metrics):
    offered, picked, installed, entries = defaultdict(int), defaultdict(int), defaultdict(int), 0
    for metrics in all_metrics:
        entries += len(metrics["contenders"])
        for card, count in metrics["offered"].items():
            offered[card] += count
        for card, count in metrics["picked"].items():
            picked[card] += count
        for card, holders in metrics["installed"].items():
            installed[card] += len(holders)
    lines = []
    for title, keep in (("Actives", lambda card: card.startswith("active_")),
                        ("Cards", lambda card: not card.startswith("active_"))):
        ids = sorted((card for card in offered if keep(card)), key=lambda card: (-picked[card] / offered[card], card))
        if not ids:
            lines.append(f"- {title} pick rate: {NO_DATA} (none offered)")
            continue
        low = sum(1 for card in ids if picked[card] / offered[card] < LOW_PICK_RATE)
        lines.append(f"- {title} pick rate (picked / offered), {len(ids)} ids, {low} below {LOW_PICK_RATE:.0%}:")
        lines.append("")
        lines.append("  | id | offered | picked | pick rate |")
        lines.append("  | --- | --- | --- | --- |")
        lines += [f"  | {card} | {offered[card]} | {picked[card]} | {100 * picked[card] / offered[card]:.1f}% |" for card in ids]
        lines.append("")
    held = sorted((card for card in installed if not card.startswith("active_")), key=lambda card: (-installed[card], card))
    if held and entries:
        lines.append(f"- Installed (share of {entries} contender-matches): "
                     + ", ".join(f"{card} {100 * installed[card] / entries:.0f}%" for card in held))
    return lines


def objective_totals(host):
    rows = [row for metrics in host for row in metrics["objectives"].values()]
    if not rows:
        return f"{NO_DATA} (no LD_OBJECTIVE line)"
    ended = sum(1 for row in rows if any(phase in OBJECTIVE_TERMINAL for phase in row["phases"]))
    repeated = sum(row["lines"] - len(row["phases"]) for row in rows)
    return f"{ended} of {len(rows)}" + (f"; {repeated} repeated phase lines" if repeated else "")


def aggregate_lines(all_metrics, baseline_source):
    """The §16 table across every match of these logs; the targets guide judgement and are never a gate."""
    host = [metrics for metrics in all_metrics if metrics["has_hits"]]
    lines = [f"## Metrics across {len(host)} matches with Host lines (spec §16)", ""]
    # Every match with phase lines counts for the field share, with or without a hit.
    share_text = field_share_text([metrics["field_share"] for metrics in all_metrics])
    if not host:
        return lines + [f"{NO_DATA}: no match has Host authority lines (LD_HIT).",
                        *(["", f"Field time share (before the showdown / whole match): {share_text}"]
                          if any(metrics["field_share"] for metrics in all_metrics) else [])]
    kills = [kill for metrics in host for kill in metrics["kills"]]
    # Casts only from matches whose Host wrote LD_CAST: a match of an older Host has fights but no cast record.
    fights = [fight for metrics in host if metrics["has_casts"] for fight in metrics["fights"]]
    has_casts = any(metrics["has_casts"] for metrics in host)
    no_casts = sum(1 for metrics in host if not metrics["has_casts"])
    same_kills = [kill for kill in kills if kill["same_level"]]
    breakouts = [sum(1 for ratio in metrics["output_ratio"].values() if ratio >= BREAKOUT_RATIO) for metrics in host
                 if metrics["output_ratio"] is not None]
    moments = [len(metrics["outrageous"]) + sum(1 for steps in (metrics["chains"] or []) if steps >= CHAIN_OUTRAGEOUS_STEPS)
               for metrics in host]
    winners = [metrics["winner_build"] for metrics in host if metrics["winner_build"]]
    winner_text = (f"{len(set(winners))} in {len(winners)} decided matches" if winners
                   else f"{NO_DATA} (no winner row with points and cards)")
    lines += ["| Metric | Target | Result |", "| --- | --- | --- |",
              f"| Same-level L1 time to kill | 10–12 s | {ttk_text([kill for kill in same_kills if kill['level'] == 1])} |",
              f"| Same-level time to kill, any level | — | {ttk_text(same_kills)} |",
              f"| Time to kill, every kill between contenders | — | {ttk_text(kills)} |",
              f"| Casts per person per same-level fight | 7–10 | "
              f"{casts_text([fight for fight in fights if fight.same_level], has_casts)} |",
              f"| Casts per person per fight, any level | — | {casts_text(fights, has_casts)} |",
              f"| AI that never cast in any of their fights | 0 | "
              f"{sum(len(metrics['never_cast']) for metrics in host if metrics['never_cast'] is not None) if has_casts else NO_DATA} |",
              f"| Breakout builds per match (x{BREAKOUT_RATIO:g} the same-level bare build) | 1–2 | "
              f"{stats_text(breakouts, 2) if breakouts else f'{NO_DATA} ({NO_BASELINE})'} |",
              f"| Distinct winner builds (actives, points, installed cards) | ≥ 8 per 50 matches | {winner_text} |",
              f"| Matches with an outrageous moment (hit ≥ 60% max HP or chain ≥ 5) | every match | "
              f"{sum(1 for count in moments if count)} of {len(host)} |",
              f"| Reaction kinds per match | ≥ 6 (after P4) | {stats_text([len(metrics['reaction_kinds']) for metrics in host])} |",
              f"| Field objectives that ended (Won or Expired) | every one | "
              f"{objective_totals(host)} |",
              f"| Field time share (before the showdown / whole match) | ≥ 65% | "
              f"{share_text} |", "",
              *([f"Casts leave out {no_casts} matches without LD_CAST lines.", ""] if no_casts and has_casts else []),
              f"Breakout baseline: {baseline_source or 'none measured'}.", ""]
    totals = {"contender": defaultdict(float), "camp": defaultdict(float)}
    momentum, lives, dodges, missing, empower = defaultdict(float), 0, None, 0, None
    for metrics in host:
        for group, amounts in metrics["damage_share"].items():
            for key, value in amounts.items():
                totals[group][key] += value
        for key, value in (metrics["momentum"] or {}).items():
            momentum[key] += value
        lives += metrics["momentum_lives"]
        if metrics["perfect_dodges"] is not None:
            dodges = (dodges or 0) + metrics["perfect_dodges"]
        missing += metrics["lives_without_ledger"]
        empower = empower_merge(empower, metrics["empower"])
    lines.append(f"- Damage share, hits on contenders: {share_line(totals['contender'])}")
    lines.append(f"- Damage share, hits on camps: {share_line(totals['camp'])}")
    lines.append(f"  - {DAMAGE_NOTE}")
    lines.append(f"- Momentum income: {momentum_text(momentum, lives, dodges)}"
                 + (f"; incomplete: {missing} field lives have no ledger (alive at the match end)" if missing else ""))
    lines.append(f"- Auto-empower: {empower_text(empower)}")
    for level in sorted({kill["level"] for kill in same_kills}):
        lines.append(f"- Same-level time to kill at L{level}: {ttk_text([kill for kill in same_kills if kill['level'] == level])}")
    chains = [steps for metrics in host for steps in (metrics["chains"] or [])]
    lines.append(f"- Longest chain: {max(chains)} steps" if chains else f"- Longest chain: {NO_DATA} (no LD_CHAIN line)")
    if not any(metrics["factors"] for metrics in host):
        lines.append(f"- Damage factors: {NO_DATA} (LD_HIT has no factor_bp)")
    return lines + pick_lines(host)


def summarize(logs, death_window=10.0, mark_before=30.0, mark_after=10.0, mark_all=False, baseline=None, metrics_only=False,
              baseline_source=None):
    events, names = [], Names()
    for source, rows in logs:
        assign_generations(rows)
        names.learn(rows)
        events.extend(rows)
    events.sort(key=lambda event: (event.when, event.source, event.order))
    source = baseline_source or (None if baseline is None else
                                 "--baseline-dps " + ",".join(f"{level}={dps:g}" for level, dps in baseline))
    report = Report(events, names, death_window, mark_before, mark_after, mark_all, baseline, source, metrics_only)
    report.out("# Lockdown playtest summary")
    report.out()
    # A generation is unique only inside one Host process. When several Host logs reuse one, each Host log's match stays
    # apart and the client logs of that generation, which cannot be tied to one of them, are listed on their own.
    hosts = defaultdict(set)
    for event in events:
        if event.generation is not None and event.tag in ("LD_MATCH_HEADER", "LD_MATCH_STARTED", "LD_MATCH_STARTING"):
            hosts[event.generation].add(event.source)
    by_generation = defaultdict(list)
    for event in events:
        run = event.source if event.generation is not None and len(hosts.get(event.generation, ())) > 1 else ""
        by_generation[(event.generation, run) if event.generation is not None else None].append(event)
    for key in sorted(key for key in by_generation if key is not None):
        report.match(f"{key[0]} ({key[1]})" if key[1] else key[0], by_generation[key])
    if report.metrics:
        for text in aggregate_lines(report.metrics, source):
            report.out(text)
        report.out()
    outside = [event for event in by_generation.get(None, []) if event.verbosity in ("Warning", "Error", "Fatal")]
    if outside and not metrics_only:
        report.out("## Outside a match")
        report.problems(outside, None)
    if not [key for key in by_generation if key is not None]:
        report.out("No match was found in these logs (no LD_MATCH_HEADER / LD_SELF_MATCH line).")
    return "\n".join(report.lines).rstrip() + "\n"


def collect(paths):
    """(label, events) per log. The label is the given label=, else the file name; a label already taken by another file
    (Telemetry.log of two runs, say) gets the parent folder's name, then a number, so independent logs never merge."""
    logs, taken = [], {}

    def unique(label, file):
        key = str(Path(file).resolve()).lower()
        if taken.get(label, key) == key:
            taken[label] = key
            return label
        for candidate in (f"{label} ({Path(file).resolve().parent.name})", *(f"{label} ({n})" for n in range(2, 1000))):
            if taken.get(candidate, key) == key:
                taken[candidate] = key
                return candidate
        raise ValueError(f"too many logs named {label}")

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
            name = unique(label if not target.is_dir() else file.stem, file)
            rows = read_log(file, name)
            if any(row.tag for row in rows):
                logs.append((name, rows))
    return logs


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("logs", nargs="+", help="log files (optionally label=path) or directories of *.log files")
    parser.add_argument("--out", help="write the report here instead of stdout")
    parser.add_argument("--death-window", type=float, default=10.0, help="seconds of hits listed before each death")
    parser.add_argument("--mark-before", type=float, default=30.0, help="seconds of events listed before each mark")
    parser.add_argument("--mark-after", type=float, default=10.0, help="seconds of events listed after each mark")
    parser.add_argument("--mark-all", action="store_true", help="also list routine AI and order lines around each mark")
    parser.add_argument("--baseline-dps", type=parse_baseline, default=None, metavar="LEVEL=DPS,...",
                        help="measured same-level bare-build DPS by level for breakout builds")
    parser.add_argument("--baseline-run", action="append", default=None, metavar="RUN_DIR",
                        help="BuildEvaluation run directory whose bare-mirror Cases measure the bare-build DPS by level "
                             "(repeatable; for example p3-ttk-baseline-l1/l6/l10)")
    parser.add_argument("--metrics-only", action="store_true", help="print only the spec §16 metric sections")
    parser.add_argument("--check-client", action="store_true",
                        help="fail when a log that never hosted a match contains Host-only authority lines")
    args = parser.parse_args(argv)
    baseline, baseline_source = args.baseline_dps, None
    if args.baseline_run:
        if baseline:
            parser.error("--baseline-dps and --baseline-run are alternatives")
        try:
            baseline, baseline_source = baseline_from_runs(args.baseline_run)
        except (OSError, ValueError, KeyError) as error:
            parser.error(f"--baseline-run: {error}")
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
    text = summarize(logs, args.death_window, args.mark_before, args.mark_after, args.mark_all, baseline,
                     args.metrics_only, baseline_source)
    if args.out:
        Path(args.out).write_text(text, encoding="utf-8")
    else:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
