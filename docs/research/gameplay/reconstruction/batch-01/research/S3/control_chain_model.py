"""S3b control-chain / player-agency model (L1 rules + L2 scripted chains).

Isolated research script: reads nothing from the game, changes nothing. Control durations, cooldowns and windups are the
1-3 rank values in SourceData/Demo/content.json (see data_abilities.json); the diminishing-returns rule mirrors
LDCombatControlRules.cpp DiminishHardControl (2 s window after the control ends, 100/60/30 %, 0.2 s floor; knockback is
not a hard control). Attackers are scripts that chain controls; the victim's agency is measured, not its damage taken.

    python control_chain_model.py            # prints the table and writes data_control_chains.json
"""
import json
import os
from dataclasses import dataclass, field

TICK = 0.01
HORIZON = 8.0
ACTION_WINDOW = 0.35  # a gap shorter than reaction (~0.25 s) + one input is not a real chance to act

LOCK = {"stun", "knockback"}          # no voluntary action
FORCED = {"taunt"}                    # can only swing at the taunter
NO_MOVE = {"root"} | LOCK | FORCED    # cannot walk or dash
HARD = {"root", "stun", "disarm", "taunt", "silence"}
WEIGHTS = {"stun": 1.0, "knockback": 1.0, "taunt": 0.8, "root": 0.5, "silence": 0.2, "disarm": 0.2}


@dataclass
class Skill:
    name: str
    kind: str          # control kind landed on the victim
    dur: float
    cd: float
    windup: float
    wall_stun: float = 0.0  # extra stun when the push ends on a wall (3B1 / card_ball_kick)
    extra: list = field(default_factory=list)  # further (kind, dur) applied together (e.g. knockback + silence)


@dataclass
class Attacker:
    skills: list
    haste: float = 0.0
    react: float = 0.0     # 0 = pre-timed perfect chain; 0.25 = fires after seeing the previous control end
    ready: dict = field(default_factory=dict)
    busy_until: float = 0.0


@dataclass
class Rules:
    name: str
    kb_in_dr: bool = False            # C1: knockback lock joins the hard-control chain
    kb_advances_dr: bool = False      # C1-lite: knockback keeps its full flight but advances the chain count
    post_immunity: float = 0.0        # C2: immune to hard control for N s after a hard control ends
    post_immunity_kb: bool = False    # C2 variant: the immunity also blocks knockback input-lock
    guard_threshold: float = 0.0      # C3: no-move seconds inside the rolling window that trigger the guard
    guard_window: float = 4.0
    guard_duration: float = 0.0       # C3: seconds of "break free" (controls displace but do not lock input)
    guard_cooldown: float = 6.0
    tumble_after: int = 0             # C5: from the Nth chained knockback on, flight keeps input (physics unchanged)
    weighted: bool = False            # C3 meter weights agency loss per control kind (WEIGHTS), as the S1 prototype does
    cut: str = "Immediate"            # C3 on trigger: "Immediate" ends live controls (V-Cut); "Next" only guards new ones (V-Next)


def diminish(chain, now, dur):
    count = min(chain["count"] + 1, 2) if now < chain["window_end"] else 0
    scale = (1.0, 0.6, 0.3)[count]
    res = dur if count == 0 else max(min(0.2, dur), dur * scale)
    chain["count"] = count
    chain["window_end"] = max(chain["window_end"], now + res + 2.0)
    return res


def simulate(attackers, rules, horizon=HORIZON, walls=True):
    states = []  # (start, end, kind)
    chain = {"count": 0, "window_end": -1.0}
    immune_until = -1.0
    guard_until, guard_ready = -1.0, 0.0
    kb_chain = {"n": 0, "last_end": -9.0}
    pending = []  # (land_time, attacker_idx, skill)
    for a in attackers:
        a.ready = {s.name: 0.0 for s in a.skills}
        a.busy_until = 0.0

    def active(t, kinds):
        return any(s <= t < e and k in kinds for s, e, k in states)

    def no_move_end(t):
        end = t
        changed = True
        while changed:
            changed = False
            for s, e, k in states:
                if k in NO_MOVE and s <= end + 1e-9 and e > end:
                    end, changed = e, True
        return end

    def apply(t, kind, dur):
        nonlocal immune_until
        if t < guard_until:
            return  # C3 break-free: displacement still plays, input is not taken
        if kind in HARD and t < immune_until:
            return
        if kind == "knockback":
            if rules.post_immunity_kb and t < immune_until:
                return
            n = kb_chain["n"] + 1 if t - kb_chain["last_end"] < 2.0 else 1
            kb_chain["n"] = n
            kb_chain["last_end"] = t + dur
            if rules.tumble_after and n >= rules.tumble_after:
                return  # C5: the body still flies, the player keeps input
            if rules.kb_in_dr:
                dur = diminish(chain, t, dur)
            elif rules.kb_advances_dr:
                diminish(chain, t, dur)
        elif kind in HARD:
            dur = diminish(chain, t, dur)
        states.append((t, t + dur, kind))
        if kind in HARD and rules.post_immunity:
            immune_until = max(immune_until, t + dur + rules.post_immunity)

    t = 0.0
    weights = []  # weighted agency loss per tick (max weight of the live controls)
    simulate.triggers = []
    while t < horizon:
        if rules.weighted:
            weights.append(max([WEIGHTS.get(k, 0.0) for s, e, k in states if s <= t < e] + [0.0]))
        # C3: rolling no-move meter
        if rules.guard_threshold and t >= guard_ready and t >= guard_until:
            lo = t - rules.guard_window
            meter = 0.0
            if rules.weighted:
                meter = sum(weights[-int(round(rules.guard_window / TICK)):]) * TICK
            else:
                for s, e, k in states:
                    if k in NO_MOVE:
                        meter += max(0.0, min(e, t) - max(s, lo))
            if meter >= rules.guard_threshold - 1e-9:
                guard_until = t + rules.guard_duration
                guard_ready = t + rules.guard_cooldown
                simulate.triggers.append(round(t, 2))
                if rules.cut == "Immediate":
                    states[:] = [(s, min(e, t) if e > t else e, k) for s, e, k in states]
        for land, ai, sk in list(pending):
            if land <= t + 1e-9:
                pending.remove((land, ai, sk))
                apply(t, sk.kind, sk.dur)
                for k, d in sk.extra:
                    apply(t, k, d)
                if sk.wall_stun and walls and sk.kind == "knockback":
                    pending.append((t + sk.dur, ai, Skill(sk.name + "_wall", "stun", sk.wall_stun, 0, 0)))
        lock_end = no_move_end(t)
        last_end = max([e for s, e, k in states if k in NO_MOVE] + [-9.0])
        for ai, a in enumerate(attackers):
            if t < a.busy_until:
                continue
            for sk in a.skills:
                if t + 1e-9 < a.ready[sk.name]:
                    continue
                if t + sk.windup + 1e-9 < max(lock_end, last_end + a.react):
                    continue  # would land inside the current lock (or before the attacker sees it end): hold it
                pending.append((t + sk.windup, ai, sk))
                a.ready[sk.name] = t + max(0.3, sk.cd * 100.0 / (100.0 + a.haste))
                a.busy_until = t + sk.windup
                break
        t = round(t + TICK, 6)
    return states


def measure(states, horizon=HORIZON):
    steps = int(horizon / TICK)
    lock = no_move = 0.0
    longest = cur = 0.0
    windows, run, first_window = 0, 0.0, None
    for i in range(steps):
        t = i * TICK
        kinds = {k for s, e, k in states if s <= t < e}
        if kinds & (LOCK | FORCED):
            lock += TICK
        if kinds & NO_MOVE:
            no_move += TICK
            cur += TICK
            longest = max(longest, cur)
            if run >= ACTION_WINDOW:
                windows += 1
            run = 0.0
        else:
            cur = 0.0
            run += TICK
            if run >= ACTION_WINDOW and first_window is None:
                first_window = round(t - ACTION_WINDOW + TICK, 2)
    if run >= ACTION_WINDOW:
        windows += 1
    span = int(4.0 / TICK)
    marks = [1 if ({k for s, e, k in states if s <= i * TICK < e} & NO_MOVE) else 0 for i in range(steps)]
    acc = sum(marks[:span])
    worst = acc
    for i in range(span, steps):
        acc += marks[i] - marks[i - span]
        worst = max(worst, acc)
    return {
        "worst_4s_no_move_s": round(worst * TICK, 2),
        "lock_s": round(lock, 2),
        "no_move_s": round(no_move, 2),
        "longest_no_move_s": round(longest, 2),
        "first_escape_s": first_window,
        "escape_windows": windows,
    }


def first_combo(states, n=3):
    hard = sorted(states)[:n]
    return round(sum(e - s for s, e, k in hard), 2)


# Rank values from data_abilities.json (1 = base; 2B / 3B1 from branch patches).
S02 = Skill("S02_root", "root", 1.1, 6.0, 0.14)
S02_moving = Skill("S02_root_moving", "root", 1.4, 6.0, 0.14)
N1 = Skill("N1_push", "knockback", 0.12, 4.2, 0.15)
N1_3B1 = Skill("N1_3B1_root", "root", 0.6, 4.2, 0.15 + 0.5)  # last of three puffs roots
S01 = Skill("S01_kick", "knockback", 0.25, 4.2, 0.15)
S01_wall = Skill("S01_2B3B1", "knockback", 0.25, 4.2, 0.15, wall_stun=0.8)
S09_wall = Skill("S09_2B3B1", "knockback", 0.15, 4.9, 0.20, wall_stun=0.8)
S22_silence = Skill("S22_2B3B1", "knockback", 0.15, 5.6, 0.15, extra=[("silence", 1.0)])
S03_stun = Skill("S03_backstab", "stun", 1.1, 6.3, 0.12)
S15_taunt = Skill("S15_taunt", "taunt", 1.0, 8.4, 0.20)
S15_2A = Skill("S15_2A", "taunt", 1.4, 8.4, 0.20)
THIRD_SHOVE = Skill("card_third_shove", "knockback", 0.12, 2.6, 0.15, wall_stun=0.8)  # every 3rd basic; ball_kick wall stun
FREEZE = Skill("freeze_reaction", "root", 1.2, 4.2, 0.15)  # wet + chill

SCENARIOS = {
    "A_control_solo_L10": lambda: [Attacker([S02, N1, S01], haste=18)],
    "B_knockback_wall_solo": lambda: [Attacker([S01_wall, S09_wall, THIRD_SHOVE], haste=0)],
    "B_knockback_open_field": "B_knockback_wall_solo",
    "C_control_haste150": lambda: [Attacker([S02, N1_3B1, S01], haste=150)],
    "C_control_haste400_cap": lambda: [Attacker([S02, N1_3B1, S01], haste=400)],
    "D_gank_3v1": lambda: [Attacker([S02]), Attacker([S03_stun, S01]), Attacker([S15_2A, N1])],
    "D_gank_3v1_reactive": lambda: [Attacker([S02], react=0.25), Attacker([S03_stun, S01], react=0.25),
                                    Attacker([S15_2A, N1], react=0.25)],
    "F_gank_haste100_wall": lambda: [Attacker([S01_wall, S02], haste=100), Attacker([S09_wall, S03_stun], haste=100),
                                     Attacker([S15_2A, THIRD_SHOVE], haste=100)],
    "E_freeze_plus_root": lambda: [Attacker([FREEZE, S02, S22_silence], haste=40)],
}

RULES = [
    Rules("R0_current"),
    Rules("C1_kb_in_DR", kb_in_dr=True),
    Rules("C1lite_kb_advances_DR", kb_advances_dr=True),
    Rules("C2_immune0.5", post_immunity=0.5),
    Rules("C2_immune1.0_incl_kb", post_immunity=1.0, post_immunity_kb=True),
    Rules("C3_guard_2.0in4_for1.0", guard_threshold=2.0, guard_window=4.0, guard_duration=1.0, guard_cooldown=6.0),
    Rules("C3_guard_2.5in4_for0.8", guard_threshold=2.5, guard_window=4.0, guard_duration=0.8, guard_cooldown=6.0),
    Rules("C5_tumble_from3rd_kb", tumble_after=3),
    Rules("C3+C5", guard_threshold=2.0, guard_window=4.0, guard_duration=1.0, guard_cooldown=6.0, tumble_after=3),
]


def main():
    out = {}
    header = f"{'scenario':28} {'rules':26} lock  nomove worst4 longest first win combo3"
    print(header)
    for name, make in SCENARIOS.items():
        walls = True
        if isinstance(make, str):
            make, walls = SCENARIOS[make], False
        base_combo = None
        for r in RULES:
            st = simulate(make(), r, walls=walls)
            m = measure(st)
            m["combo3_s"] = first_combo(st)
            if base_combo is None:
                base_combo = m["combo3_s"]
            m["combo3_retained"] = round(m["combo3_s"] / base_combo, 2) if base_combo else None
            out.setdefault(name, {})[r.name] = m
            print(f"{name:28} {r.name:26} {m['lock_s']:4.2f} {m['no_move_s']:5.2f} {m['worst_4s_no_move_s']:5.2f} {m['longest_no_move_s']:6.2f} "
                  f"{str(m['first_escape_s']):5} {m['escape_windows']:3} {m['combo3_s']:5.2f}")
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data_control_chains.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"horizon_s": HORIZON, "action_window_s": ACTION_WINDOW, "results": out}, f, ensure_ascii=False, indent=1)


if __name__ == "__main__":
    main()
