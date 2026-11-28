"""S3 combat geometry & spatial duel model (L1 math + L2 offline simulation).

Read-only with respect to the repository: loads SourceData/Demo/content.json from the repo root
given on the command line. Writes only to --out (default: this directory).

L1 part (escape_matrix): for every active skill computes, from the shipped numbers, how much time a
target has between the first legible cue and resolution, how far it must travel to leave the hit
area (hit tests are centre-of-foot for line/area/cone, projectile radius + 40 cm body radius for
projectiles - LDCombatElementsDirector.cpp:682-704, LDCombatEffects.cpp:1216-1243/1086), and
whether walking (at several speeds/reaction times) or only the D dash can get out.

L2 part (duel): a 60 Hz 2D kinematic duel between two scripted policies in a circular arena, using
the real skill numbers for a representative subset, to compare rule variants (move speed, cast
commitment, dash cooldown) and operation quality (reaction, dodge, aim lead). It is NOT a native
measurement; policies are scripts, not humans.

Usage:
  python combat_model.py <repo_root> [--out DIR] [--quick]
  python combat_model.py <repo_root> --d01b      # D01-B class-scoped variants -> data_d01b_results.json
  python combat_model.py <repo_root> --s3b       # S3b skill-review variants -> data_s3b_skill_variants.json
"""
from __future__ import annotations

import json
import math
import random
import statistics
import sys
from dataclasses import dataclass, field, replace
from pathlib import Path

# ---------------------------------------------------------------------------------------------
# Content
# ---------------------------------------------------------------------------------------------


def v(x, default=0.0):
    if isinstance(x, dict):
        return x.get("value", default)
    return default if x is None else x


class Content:
    def __init__(self, root: Path):
        d = json.loads((root / "SourceData/Demo/content.json").read_text(encoding="utf-8"))
        self.p = {k: (v(x) if isinstance(x, dict) else x) for k, x in d["parameters"].items()}
        self.ab = {a["id"]: a for a in d["abilities"]}
        self.char = d["character"]
        self.names = {r["id"]: r.get("name_text") for r in d["display"]}

    def eff(self, aid, op):
        for e in self.ab[aid]["effects"]:
            if e["op"] == op:
                return e
        return None


BODY_R = 40.0  # UNIT.CollisionRadiusCm, used by projectile hit tests only

# ---------------------------------------------------------------------------------------------
# L1: escape matrix
# ---------------------------------------------------------------------------------------------

# Which enemy-visible cue exists during the windup (LDHumanoidVisualComponent.cpp:243-300):
# geometry tells only for hose/cola/seethe; others show a generic hand/feet tell (direction via facing).
GEOMETRY_TELL = {"active_hose_down", "active_cola_shake", "active_seethe"}


def escape_rows(c: Content, speeds=(240, 270, 300, 330), reactions=(0.20, 0.30)):
    rows = []
    for aid, a in c.ab.items():
        t = a["target"]
        mode = t["mode"]
        wu = v(a.get("windup"))
        tele = wu >= 0.15  # perfect-dodge/telegraph threshold (LDCombatActions.cpp:2354,2382,2435)
        row = {"id": aid, "name": c.names.get(aid), "mode": mode, "windup": wu, "telegraphed": tele,
               "tell": "geometry" if aid in GEOMETRY_TELL else ("generic" if wu > 0 else "none")}
        need = None      # distance to leave the hit area from the worst (aimed/centred) position
        t_cue = None     # seconds from the first legible cue to resolution
        cls = None
        extra = {}
        if mode == "Unit":
            need = None
            t_cue = wu
            cls = "lock-on"
            extra["note"] = "release re-checks range/LoS (LDCombatActions.cpp:1604-1620): only escapable at range edge"
            extra["range"] = v(t["range"])
        elif mode == "DirectionLine":
            need = v(t["half_width"])
            t_cue = wu
            cls = "line"
            extra["range"] = v(t["range"])
        elif mode == "DirectionCone":
            # Worst case: on the axis at range r -> lateral need r*sin(half angle); use mid-range 200 cm.
            half = math.radians(v(t["angle"]) / 2)
            need = 200 * math.sin(half)
            t_cue = wu
            cls = "cone"
            extra["range"] = v(t["range"])
        elif mode == "DirectionProjectile":
            need = v(t["projectile_radius"]) + BODY_R
            spd = v(t["projectile_speed"])
            rng = v(t["range"])
            t_cue = wu + rng / spd
            cls = "projectile"
            extra.update(range=rng, speed=spd, flight_at_max=rng / spd)
        elif mode == "Self":
            sa = c.eff(aid, "self_area") or c.eff(aid, "taunt")
            if sa is not None and v(sa.get("radius")):
                need = v(sa.get("radius"))
                t_cue = wu
                cls = "self-area"
            else:
                cls = "self-buff"
        elif mode == "GroundPoint":
            gi = c.eff(aid, "ground_impact")
            zone = c.eff(aid, "zone")
            trap = c.eff(aid, "contact_trap")
            if gi:
                need = v(gi.get("radius"))
                # Landing spot shown only from release (ground warning), the windup only shows a generic tell.
                t_cue = v(gi.get("delay"))
                extra["t_from_windup_start"] = wu + v(gi.get("delay"))
                extra["inner_radius"] = v(gi.get("inner_radius"))
                cls = "ground-delay"
            elif zone:
                need = v(zone.get("radius"))
                t_cue = wu  # first pulse at release (delay 0); later pulses every interval
                extra["pulse_interval"] = v(zone.get("interval"))
                cls = "zone"
            elif trap:
                need = v(trap.get("radius")) - v(trap.get("trigger_radius"))
                t_cue = v(trap.get("delay"))
                cls = "trap"
            else:
                cls = "placement"
            extra["range"] = v(t.get("range"))
        elif mode == "DirectionDash":
            dash = c.eff(aid, "dash")
            cls = "dash-skill"
            extra.update(distance=v(dash.get("distance")), duration=v(dash.get("duration")))
            t_cue = wu
        row.update(cls=cls, need_cm=need, t_cue=t_cue, **extra)
        walk = {}
        if need is not None and t_cue is not None:
            for s in speeds:
                for r in reactions:
                    budget = s * max(0.0, t_cue - r)
                    entry = {"margin_cm": round(budget - need, 1)}
                    if cls in ("self-area", "ground-delay", "zone"):
                        # share of the disc from which walking out is possible after reacting
                        frac = 1.0 - (max(0.0, need - budget) / need) ** 2
                        entry["escapable_area"] = round(frac, 3)
                    walk[f"v{s}_r{int(r * 1000)}"] = entry
            # A target already moving across the aim (no reaction) under a non-leading aim.
            if cls in ("line", "projectile", "cone"):
                row["unled_cross_cm_at_240"] = round(240 * (t_cue if cls != "projectile" else wu + 500 / v(t["projectile_speed"])), 1)
            if cls == "projectile":
                row["min_dodge_range_cm"] = {f"v{s}_r{int(r * 1000)}": round(max(0.0, extra["speed"] * (need / s + r - wu)), 0)
                                             for s in speeds for r in reactions}
        row["walk"] = walk
        rows.append(row)
    return rows


def dash_cover_time(dist_needed, dash_dist=450.0, dash_dur=0.18):
    """EaseOutCubic dash (LDCharacterMovementComponent FLDRootMotionSource_EaseOutDash)."""
    if dist_needed >= dash_dist:
        return None
    u = 1 - (1 - dist_needed / dash_dist) ** (1 / 3)
    return u * dash_dur


# ---------------------------------------------------------------------------------------------
# L2: spatial duel
# ---------------------------------------------------------------------------------------------

@dataclass
class Rules:
    move_speed: float = 240.0
    dash_cd: float = 9.0
    dash_dist: float = 450.0
    dash_dur: float = 0.18
    # How a directed (non-self, non-dash) cast treats the caster's movement:
    #  "stop"   - the cast order replaces the move order: stand through the windup, then a re-click (current rules)
    #  "free"   - keeps walking through the windup (move-cast)
    #  "commit" - stand through the windup and a recovery
    cast_move: str = "stop"
    recovery: float = 0.0
    reclick: float = 0.10       # human re-click after a stop (0 for "free")
    hp_scale: float = 1.0
    body_radius_hits: bool = False  # line/area tests include the 40 cm body radius
    knockback_dr: bool = False
    windup_floor: float = 0.0       # minimum windup for directed/ground skills
    ground_cue_at_windup: bool = False  # ground landing spot visible from windup start
    stagger_on_melee_basic: float = 0.0  # move-slow ratio applied by basic hits (0.6 s)
    ranged_cast_slow: float = 0.0        # self-slow after a directed cast with range > 500 (0.6 s)
    # Per-skill overrides (D01-B "threat grammar": only the named classes change), keyed by ability id:
    # {"windup": s, "half_width": cm, "angle": deg, "delay": s, "recovery": s (no move/cast/basic after release)}
    per_skill: dict = field(default_factory=dict)
    label: str = "baseline"


@dataclass
class Skill:
    aid: str
    rank: int = 1


@dataclass
class Build:
    key: str
    name: str
    skills: list
    points: dict
    standoff: float  # preferred distance (0 = pursue)


@dataclass
class Operator:
    reaction: float = 0.25     # to a legible cue
    dodge: float = 0.85        # probability of acting on a threat it can read
    dash_dodge: float = 0.6    # probability of spending D to dodge when walking can't
    lead: float = 1.0          # 0 = aim at current position, 1 = full linear lead
    aim_sigma: float = 20.0    # cm lateral aim noise
    juke: float = 1.0          # strafing while kiting / approaching (0..1)
    label: str = "skilled"


SKILLED = Operator()
CASUAL = Operator(reaction=0.45, dodge=0.45, dash_dodge=0.25, lead=0.0, aim_sigma=60.0, juke=0.3, label="casual")
NOVICE = Operator(reaction=0.65, dodge=0.15, dash_dodge=0.05, lead=0.0, aim_sigma=90.0, juke=0.0, label="novice")

BUILDS = {
    "melee": Build("melee", "近战揍人", [Skill("active_elbow", 2), Skill("active_pants_down", 2), Skill("active_bail_out", 1)],
                   {"strength": 18, "agility": 12}, 0),
    "ranged": Build("ranged", "远程风筝", [Skill("active_snark_bolt", 2), Skill("active_zap", 2), Skill("active_hose_down", 1)],
                    {"brains": 18, "agility": 12}, 560),
    "cycle": Build("cycle", "技能循环", [Skill("active_calculus_drop", 2), Skill("active_quick_meeting", 2), Skill("active_seethe", 1)],
                   {"brains": 18, "physique": 12}, 420),
    "control": Build("control", "控制", [Skill("active_pants_down", 2), Skill("active_cola_shake", 2), Skill("active_elbow", 1)],
                     {"strength": 18, "thick_skin": 12}, 0),
    "heavy": Build("heavy", "重击", [Skill("active_calculus_drop", 2), Skill("active_crotch_grab", 2), Skill("active_elbow", 1)],
                   {"strength": 18, "physique": 12}, 0),
    "defense": Build("defense", "防御", [Skill("active_not_mad", 2), Skill("active_open_mic", 2), Skill("active_charged_ram", 1)],
                     {"physique": 18, "thick_skin": 12}, 0),
    "mobility": Build("mobility", "机动", [Skill("active_bail_out", 2), Skill("active_charged_ram", 2), Skill("active_crotch_grab", 1)],
                      {"agility": 18, "strength": 12}, 0),
}

LEVEL = 10


def project(c: Content, b: Build, rules: Rules, level=LEVEL):
    k = level - 1
    pts = b.points
    skin = pts.get("thick_skin", 0)
    st = {
        "max_hp": (860 + 125 * k + 70 * pts.get("physique", 0)) * rules.hp_scale,
        "ad_base": 70 + 3.2 * k,
        "ad": 70 + 3.2 * k + 3.5 * pts.get("strength", 0),
        "ap": 6 * pts.get("brains", 0),
        "armor": 18 + 2 * k + 3.5 * skin,
        "mr": 18 + 2 * k + 3.5 * skin,
        "haste": pts.get("brains", 0),
        "crit": 0.006 * pts.get("strength", 0),
        "as": 0.85 * (1 + 0.02 * k + 0.02 * pts.get("agility", 0)),
        "speed": rules.move_speed * (1 + 0.005 * pts.get("agility", 0)),
        "regen": 1.6 + 0.12 * k + 0.08 * pts.get("physique", 0),
        "ks_strength": pts.get("strength", 0) >= 18,
        "ks_agility": pts.get("agility", 0) >= 18,
        "ks_skin": skin >= 18,
        "G": 1 + 0.04 * k,
    }
    st["bonus_ad"] = st["ad"] - st["ad_base"]
    st["bonus_hp"] = 70 * pts.get("physique", 0)
    return st


class Fighter:
    def __init__(self, c, build, op, rules, pos, idx, power=1.0):
        self.c, self.b, self.op, self.rules, self.idx = c, build, op, rules, idx
        self.st = project(c, build, rules)
        self.hp = self.st["max_hp"]
        self.shield = []  # (amount, expires)
        self.pos = list(pos)
        self.vel = [0.0, 0.0]
        self.power = power
        self.cd = {s.aid: 0.0 for s in build.skills}
        self.momentum = 50.0
        self.next_basic = 0.0
        self.combo = 0
        self.combo_t = -9.0
        self.action = None  # dict(kind, until, ...)
        self.stand_until = 0.0
        self.lock_until = 0.0
        self.dash_ready = 0.0
        self.dash = None  # (start, dur, origin, dir, dist)
        self.kb = None
        self.root_until = 0.0
        self.stun_until = 0.0
        self.slows = []  # (ratio, until)
        self.dr_stage, self.dr_until = 0, -9.0
        self.empower = None  # (charges, until, base, adr, apr, as_bonus)
        self.agi_buff_until = 0.0
        self.agi_empower_until = 0.0
        self.skin_ready = 0.0
        self.strafe_dir, self.strafe_until = 1, 0.0
        self.dodge_plan = None  # (vec, until)
        self.seen = set()
        self.stats = {"dmg": 0.0, "basic": 0.0, "skill": 0.0, "casts": 0, "hits": 0, "dodged": 0, "dash_dodged": 0,
                      "cc_time": 0.0, "in_range_time": 0.0, "first_dmg": None}

    # --- helpers ---
    def speed(self, t):
        s = self.st["speed"]
        if self.st["ks_agility"] and t < self.agi_buff_until:
            s *= 1.25
        slow = max([r for r, u in self.slows if u > t], default=0.0)
        return max(138.4, min(540.0, s * (1 - slow)))

    def can_act(self, t):
        return t >= self.stun_until and (self.kb is None)

    def can_move(self, t):
        return self.can_act(t) and t >= self.root_until and t >= self.stand_until and self.dash is None

    def impaired(self, t):
        return (t < self.root_until or t < self.stun_until or self.kb is not None
                or any(u > t for _, u in self.slows))


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def norm(x, y):
    m = math.hypot(x, y)
    return (x / m, y / m) if m > 1e-9 else (0.0, 0.0)


class Duel:
    DT = 1 / 60

    def __init__(self, c, rules, ba, bb, opa, opb, seed, power=(1.0, 1.0), arena=1800.0, start_gap=900.0, cap=45.0):
        self.c, self.rules, self.rng = c, rules, random.Random(seed)
        ang = self.rng.uniform(0, 2 * math.pi)
        dx, dy = math.cos(ang) * start_gap / 2, math.sin(ang) * start_gap / 2
        self.f = [Fighter(c, ba, opa, rules, (-dx, -dy), 0, power[0]), Fighter(c, bb, opb, rules, (dx, dy), 1, power[1])]
        self.arena, self.cap = arena, cap
        self.threats = []  # dicts: owner, kind, resolve_at, cue_at, geometry, payload
        self.projectiles = []
        self.zones = []
        self.t = 0.0

    # ---------------- damage ----------------
    def raw(self, att, base, adr, apr, rank, adaptive=0.0, hpr=0.0, dtype="Physical"):
        bs = [1, 1.35, 1.7][rank - 1]
        rs = [1, 1.25, 1.5][rank - 1]
        st = att.st
        amount = base * st["G"] * bs + adr * rs * st["ad"] + apr * rs * st["ap"] + hpr * st["bonus_hp"]
        magical = dtype == "Magical"
        if adaptive:
            if st["bonus_ad"] * 1.4 >= st["ap"]:
                amount += adaptive * rs * st["bonus_ad"]
                magical = False
            else:
                amount += adaptive * rs * st["ap"]
                magical = True
        return amount, magical

    def deal(self, att, dfn, amount, magical, kind, t):
        if amount <= 0 or dfn.hp <= 0:
            return 0.0
        amount *= att.power
        if att.st["ks_strength"] and dfn.impaired(t):
            amount *= 1.14
        resist = dfn.st["mr"] if magical else dfn.st["armor"]
        amount *= 100 / (100 + resist)
        if dfn.st["ks_skin"] and t >= dfn.skin_ready and amount >= 30:
            amount -= max(45, 0.22 * amount)
            dfn.skin_ready = t + 5
            amount = max(0.0, amount)
        for i, (s, u) in enumerate(sorted(dfn.shield, key=lambda x: x[1])):
            pass
        dfn.shield = [(s, u) for s, u in dfn.shield if u > t]
        dfn.shield.sort(key=lambda x: x[1])
        rest = amount
        new = []
        for s, u in dfn.shield:
            take = min(s, rest)
            rest -= take
            if s - take > 0:
                new.append((s - take, u))
        dfn.shield = new
        lost = min(dfn.hp, rest)
        dfn.hp -= lost
        att.stats["dmg"] += amount
        att.stats[kind] += amount
        att.stats["hits"] += 1
        if att.stats["first_dmg"] is None:
            att.stats["first_dmg"] = t
        if kind == "skill":
            att.momentum = min(100, att.momentum + 5)
        return amount

    def hard_cc(self, dfn, dur, t, kind="root"):
        if t > dfn.dr_until:
            dfn.dr_stage = 0
        eff = max(min(0.2, dur), dur * [1, 0.6, 0.3][min(2, dfn.dr_stage)])
        dfn.dr_stage += 1
        dfn.dr_until = t + eff + 2.0
        if kind == "root":
            dfn.root_until = max(dfn.root_until, t + eff)
        else:
            dfn.stun_until = max(dfn.stun_until, t + eff)
            if dfn.action and dfn.action["kind"] != "basic_landed":
                dfn.action = None
        dfn.stats["cc_time"] += eff

    def knockback(self, att, dfn, distance, dur, t, from_pos=None, toward=False):
        if dfn.st["ks_skin"] and t < dfn.skin_ready - 5 + 1.5:
            distance *= 0.5
        if self.rules.knockback_dr:
            if t > dfn.dr_until:
                dfn.dr_stage = 0
            scale = [1, 0.6, 0.3][min(2, dfn.dr_stage)]
            dfn.dr_stage += 1
            dfn.dr_until = t + dur + 2.0
            distance *= scale
            dur *= scale
        src = from_pos or att.pos
        d = norm(dfn.pos[0] - src[0], dfn.pos[1] - src[1])
        if toward:
            d = (-d[0], -d[1])
            distance = max(0.0, min(distance, dist(dfn.pos, att.pos) - 120))
        dfn.kb = (t, max(dur, 1e-3), list(dfn.pos), d, distance)
        if dfn.action and dfn.action["kind"] not in ("basic_landed",):
            dfn.action = None
        dfn.stats["cc_time"] += dur

    # ---------------- casting ----------------
    def skill_range(self, aid):
        a = self.c.ab[aid]
        m = a["target"]["mode"]
        if m == "DirectionDash":
            return v(self.c.eff(aid, "dash")["distance"]) + 150
        if m == "Self":
            sa = self.c.eff(aid, "self_area")
            if sa:
                return v(sa["radius"]) - 20
            if aid in ("active_not_mad",):
                return 9999
            if aid == "active_triple_combo":
                return 260
            return 300
        if m == "DirectionLine":
            return v(a["target"]["range"]) - 30
        if m == "DirectionCone":
            return v(a["target"]["range"]) - 40
        return v(a["target"]["range"])

    def windup(self, aid):
        a = self.c.ab[aid]
        w = v(a.get("windup"))
        m = a["target"]["mode"]
        if m in ("DirectionLine", "DirectionCone", "DirectionProjectile", "GroundPoint", "Unit") and w > 0:
            w = max(w, self.rules.windup_floor)
        return self.ov(aid, "windup", w)

    def ov(self, aid, key, default):
        return self.rules.per_skill.get(aid, {}).get(key, default)

    def half_width(self, aid):
        return self.ov(aid, "half_width", v(self.c.ab[aid]["target"]["half_width"]))

    def cone_angle(self, aid):
        return self.ov(aid, "angle", v(self.c.ab[aid]["target"]["angle"]))

    def ground_delay(self, aid):
        gi = self.c.eff(aid, "ground_impact")
        return self.ov(aid, "delay", v(gi["delay"])) if gi else 0.0

    def aim_point(self, me, foe, horizon):
        lead = me.op.lead
        p = [foe.pos[0] + foe.vel[0] * horizon * lead, foe.pos[1] + foe.vel[1] * horizon * lead]
        n = self.rng.gauss(0, me.op.aim_sigma)
        d = norm(p[0] - me.pos[0], p[1] - me.pos[1])
        return [p[0] - d[1] * n, p[1] + d[0] * n]

    def try_cast(self, me, foe, t):
        d = dist(me.pos, foe.pos)
        for s in me.b.skills:
            aid = s.aid
            if t < me.cd[aid]:
                continue
            a = self.c.ab[aid]
            cost = v(a.get("momentum_cost"))
            if me.momentum < cost:
                continue
            m = a["target"]["mode"]
            rng = self.skill_range(aid)
            if aid == "active_not_mad":
                if me.hp > 0.6 * me.st["max_hp"] and not me.impaired(t):
                    continue
            elif aid == "active_bail_out":
                if not (260 < d < 520):
                    continue
            elif aid == "active_triple_combo":
                if d > 260:
                    continue
            elif d > rng:
                continue
            if m == "DirectionDash" and aid != "active_bail_out" and d < 150:
                continue
            self.start_cast(me, foe, s, t)
            return True
        return False

    def start_cast(self, me, foe, s, t):
        aid = s.aid
        a = self.c.ab[aid]
        m = a["target"]["mode"]
        wu = self.windup(aid)
        cd = v(a.get("cooldown")) * 100 / (100 + me.st["haste"])
        me.cd[aid] = t + wu + max(0.3, cd)
        me.momentum -= v(a.get("momentum_cost"))
        me.stats["casts"] += 1
        horizon = wu
        if m == "DirectionProjectile":
            horizon = wu + dist(me.pos, foe.pos) / v(a["target"]["projectile_speed"])
        elif m == "GroundPoint":
            gi = self.c.eff(aid, "ground_impact")
            horizon = wu + (self.ground_delay(aid) if gi else 0.0)
        aim = self.aim_point(me, foe, horizon)
        act = {"kind": "cast", "aid": aid, "rank": s.rank, "release": t + wu, "aim": aim, "start": t}
        me.action = act
        directed = m in ("DirectionLine", "DirectionCone", "DirectionProjectile", "GroundPoint", "Unit")
        if directed:
            if self.rules.cast_move == "stop":
                me.stand_until = t + wu + me.op.reaction * 0 + self.rules.reclick
            elif self.rules.cast_move == "commit":
                me.stand_until = t + wu + self.rules.recovery
        rec = self.ov(aid, "recovery", 0.0)
        if rec > 0:
            dash = self.c.eff(aid, "dash")
            end = t + wu + (v(dash["duration"]) if dash else 0.0) + rec
            me.stand_until = max(me.stand_until, end)
            me.lock_until = max(me.lock_until, end)
        # Register a threat for the defender's dodge logic.
        self.register_threat(me, foe, act, t)

    def register_threat(self, me, foe, act, t):
        aid = act["aid"]
        a = self.c.ab[aid]
        m = a["target"]["mode"]
        th = {"owner": me.idx, "aid": aid, "start": t, "mode": m}
        if m == "DirectionLine":
            th.update(resolve=act["release"], cue=t, kind="line", origin=list(me.pos),
                      dir=norm(act["aim"][0] - me.pos[0], act["aim"][1] - me.pos[1]),
                      half=self.half_width(aid), length=v(a["target"]["range"]))
        elif m == "DirectionCone":
            th.update(resolve=act["release"], cue=t, kind="cone", origin=list(me.pos),
                      dir=norm(act["aim"][0] - me.pos[0], act["aim"][1] - me.pos[1]),
                      angle=self.cone_angle(aid), length=v(a["target"]["range"]))
        elif m == "Self":
            sa = self.c.eff(aid, "self_area")
            if not sa:
                return
            th.update(resolve=act["release"], cue=t, kind="circle_self", radius=v(sa["radius"]))
        elif m == "GroundPoint":
            gi = self.c.eff(aid, "ground_impact")
            if gi:
                delay = self.ground_delay(aid)
                cue = t if self.rules.ground_cue_at_windup else act["release"]
                th.update(resolve=act["release"] + delay, cue=cue, kind="circle", center=act["aim"], radius=v(gi["radius"]))
            else:
                zone = self.c.eff(aid, "zone")
                if zone:
                    th.update(resolve=act["release"], cue=t, kind="circle", center=act["aim"], radius=v(zone["radius"]))
                else:
                    return
        elif m == "DirectionProjectile":
            th.update(kind="projectile_pending", resolve=None, cue=t,
                      dir=norm(act["aim"][0] - me.pos[0], act["aim"][1] - me.pos[1]))
        elif m == "Unit":
            th.update(kind="lockon", resolve=act["release"], cue=t)
        else:
            return
        self.threats.append(th)

    def release(self, me, foe, t):
        act = me.action
        me.action = None
        aid = act["aid"]
        a = self.c.ab[aid]
        m = a["target"]["mode"]
        rank = act["rank"]
        dmg_e = self.c.eff(aid, "damage")
        if m == "Unit":
            if dist(me.pos, foe.pos) <= v(a["target"]["range"]) + 1:
                self.apply_payload(me, foe, aid, rank, t)
            else:
                me.cd[aid] = t  # refused at release: no cost (LDCombatActions.cpp:1611)
            return
        if m == "DirectionLine":
            th = next((x for x in self.threats if x["owner"] == me.idx and x["aid"] == aid and x.get("kind") == "line"), None)
            d = th["dir"] if th else norm(act["aim"][0] - me.pos[0], act["aim"][1] - me.pos[1])
            half = self.half_width(aid) + (BODY_R if self.rules.body_radius_hits else 0)
            if self.in_line(me.pos, d, v(a["target"]["range"]), half, foe.pos):
                self.apply_payload(me, foe, aid, rank, t)
            return
        if m == "DirectionCone":
            th = next((x for x in self.threats if x["owner"] == me.idx and x["aid"] == aid and x.get("kind") == "cone"), None)
            d = th["dir"] if th else norm(act["aim"][0] - me.pos[0], act["aim"][1] - me.pos[1])
            off = (foe.pos[0] - me.pos[0], foe.pos[1] - me.pos[1])
            r = math.hypot(*off)
            pad = BODY_R if self.rules.body_radius_hits else 0
            if r <= v(a["target"]["range"]) + pad:
                cosang = (off[0] * d[0] + off[1] * d[1]) / max(r, 1e-6)
                if r < 1 or cosang >= math.cos(math.radians(self.cone_angle(aid) / 2)):
                    self.apply_payload(me, foe, aid, rank, t)
            return
        if m == "Self":
            sa = self.c.eff(aid, "self_area")
            if sa:
                pad = BODY_R if self.rules.body_radius_hits else 0
                if dist(me.pos, foe.pos) <= v(sa["radius"]) + pad:
                    self.apply_payload(me, foe, aid, rank, t)
            else:
                self.apply_self(me, aid, rank, t)
            return
        if m == "GroundPoint":
            gi = self.c.eff(aid, "ground_impact")
            zone = self.c.eff(aid, "zone")
            if gi:
                self.zones.append({"owner": me.idx, "aid": aid, "rank": rank, "center": act["aim"], "at": t + self.ground_delay(aid),
                                   "radius": v(gi["radius"]), "inner": v(gi["inner_radius"]), "kind": "impact"})
            elif zone:
                for k in range(int(v(zone["count"]))):
                    self.zones.append({"owner": me.idx, "aid": aid, "rank": rank, "center": act["aim"], "at": t + k * v(zone["interval"]),
                                       "radius": v(zone["radius"]), "inner": 0, "kind": "pulse"})
            return
        if m == "DirectionProjectile":
            d = norm(act["aim"][0] - me.pos[0], act["aim"][1] - me.pos[1])
            self.projectiles.append({"owner": me.idx, "aid": aid, "rank": rank, "pos": list(me.pos), "dir": d,
                                     "speed": v(a["target"]["projectile_speed"]), "left": v(a["target"]["range"]),
                                     "radius": v(a["target"]["projectile_radius"])})
            for th in self.threats:
                if th["owner"] == me.idx and th["aid"] == aid and th["kind"] == "projectile_pending":
                    th.update(kind="projectile", origin=list(me.pos), dir=d, speed=v(a["target"]["projectile_speed"]),
                              half=v(a["target"]["projectile_radius"]) + BODY_R, released=t)
            return
        if m == "DirectionDash":
            dash = self.c.eff(aid, "dash")
            d = norm(act["aim"][0] - me.pos[0], act["aim"][1] - me.pos[1])
            dd = v(dash["distance"])
            if aid == "active_bail_out" or not self.c.eff(aid, "dash_contact"):
                # innate-like clamp to 135 cm from the target ahead
                ahead = (foe.pos[0] - me.pos[0]) * d[0] + (foe.pos[1] - me.pos[1]) * d[1]
                if 0 < ahead and dist(me.pos, foe.pos) < dd + 135:
                    dd = max(0.0, min(dd, ahead - 135))
            me.dash = (t, v(dash["duration"]), list(me.pos), d, dd, aid, rank, False)
            if aid == "active_bail_out":
                emp = self.c.eff(aid, "empower_basic")
                me.empower = (1, t + v(emp["duration"]), v(emp["base"]), v(emp["ad_ratio"]), v(emp["ap_ratio"]), 0.0, rank)
            if aid == "active_charged_ram":
                sh = self.c.eff(aid, "shield")
                amt = (v(sh["base"]) * me.st["G"] * [1, 1.35, 1.7][rank - 1] + v(sh["ad_ratio"]) * me.st["ad"]
                       + v(sh["max_hp_ratio"]) * me.st["max_hp"] + 0.55 * (me.st["armor"] + me.st["mr"]))
                me.shield.append((amt * me.power, t + v(sh["duration"])))
            return

    def in_line(self, origin, d, length, half, p):
        off = (p[0] - origin[0], p[1] - origin[1])
        fwd = off[0] * d[0] + off[1] * d[1]
        side = abs(off[0] * d[1] - off[1] * d[0])
        return 0 <= fwd <= length and side <= half

    def apply_self(self, me, aid, rank, t):
        if aid == "active_not_mad":
            sh = self.c.eff(aid, "shield")
            amt = (v(sh["base"]) * me.st["G"] * [1, 1.35, 1.7][rank - 1] + v(sh["ad_ratio"]) * me.st["ad"]
                   + v(sh["ap_ratio"]) * me.st["ap"] + v(sh["max_hp_ratio"]) * me.st["max_hp"] + 0.5 * (me.st["armor"] + me.st["mr"]))
            me.shield.append((amt * me.power, t + v(sh["duration"])))
            me.slows = []
            me.root_until = min(me.root_until, t)
        elif aid == "active_triple_combo":
            emp = self.c.eff(aid, "empower_basic")
            me.empower = (3, t + v(emp["duration"]), v(emp["base"]), v(emp["ad_ratio"]), v(emp["ap_ratio"]), 0.35, rank)

    def apply_payload(self, me, foe, aid, rank, t, scale=1.0):
        a = self.c.ab[aid]
        total = 0.0
        for e in a["effects"]:
            op = e["op"]
            if op in ("damage",):
                amt, mag = self.raw(me, v(e.get("base")), v(e.get("ad_ratio")), v(e.get("ap_ratio")), rank,
                                    v(e.get("adaptive_ratio")), v(e.get("max_hp_ratio")), e.get("damage_type", "Physical"))
                if aid == "active_zap":
                    amt *= 1 + 0.7 + 0.49  # two bounces return to the only target in a duel
                total += self.deal(me, foe, amt * scale, mag, "skill", t)
            elif op == "root":
                dur = v(e["duration"]) + (0.3 if aid == "active_pants_down" and math.hypot(*foe.vel) > 5 else 0)
                self.hard_cc(foe, dur, t, "root")
            elif op == "knockback":
                self.knockback(me, foe, v(e["distance"]), v(e["duration"]), t)
            elif op == "slow":
                foe.slows.append((v(e["ratio"]), t + v(e["duration"])))
            elif op == "chill":
                foe.slows.append((0.10, t + 4))
            elif op == "wet":
                foe.slows.append((0.15, t + 4))
            elif op == "burn":
                stacks = 2 if aid == "active_seethe" else 1
                per = (4.5 * me.st["G"] + 0.03 * me.st["ap"]) * stacks * 3
                self.deal(me, foe, per, True, "skill", t)
            elif op == "self_area" and aid == "active_open_mic":
                pass
            elif op == "shield" and aid == "active_open_mic":
                amt = (v(e["base"]) * me.st["G"] * [1, 1.35, 1.7][rank - 1] + v(e["ap_ratio"]) * me.st["ap"]
                       + v(e["max_hp_ratio"]) * me.st["max_hp"] + 0.6 * (me.st["armor"] + me.st["mr"])) * 1.25
                me.shield.append((amt * me.power, t + v(e["duration"])))
            elif op == "backstab_lunge":
                pass
            elif op == "dash_contact":
                pass
        return total

    # ---------------- defender logic ----------------
    def threat_hits(self, th, p, t):
        k = th["kind"]
        if k == "line":
            return self.in_line(th["origin"], th["dir"], th["length"], th["half"], p)
        if k == "cone":
            off = (p[0] - th["origin"][0], p[1] - th["origin"][1])
            r = math.hypot(*off)
            return r <= th["length"] and (r < 1 or (off[0] * th["dir"][0] + off[1] * th["dir"][1]) / r >= math.cos(math.radians(th["angle"] / 2)))
        if k == "circle":
            return dist(th["center"], p) <= th["radius"]
        if k == "circle_self":
            owner = self.f[th["owner"]]
            return dist(owner.pos, p) <= th["radius"]
        if k == "projectile":
            return True
        return False

    def plan_dodge(self, me, t):
        foe = self.f[1 - me.idx]
        if me.dodge_plan and me.dodge_plan[1] > t:
            return
        me.dodge_plan = None
        for th in self.threats:
            if th["owner"] == me.idx or th.get("resolve") is None and th["kind"] != "projectile":
                continue
            key = (id(th),)
            if key in me.seen:
                continue
            cue = th.get("cue", th["start"])
            if t < cue + me.op.reaction:
                continue
            me.seen.add(key)
            if self.rng.random() > me.op.dodge:
                continue
            k = th["kind"]
            if k == "lockon":
                continue
            # time left and escape vector
            if k == "projectile":
                o, d = th["origin"], th["dir"]
                rel = (me.pos[0] - o[0], me.pos[1] - o[1])
                along = rel[0] * d[0] + rel[1] * d[1]
                side = rel[0] * d[1] - rel[1] * d[0]
                travelled = (t - th["released"]) * th["speed"]
                if along < travelled:
                    continue
                left = (along - travelled) / th["speed"]
                need = th["half"] - abs(side)
                if need <= 0:
                    continue
                sgn = 1 if side >= 0 else -1
                vec = (d[1] * sgn, -d[0] * sgn)
            elif k == "line" or k == "cone":
                left = th["resolve"] - t
                o, d = th["origin"], th["dir"]
                rel = (me.pos[0] - o[0], me.pos[1] - o[1])
                side = rel[0] * d[1] - rel[1] * d[0]
                if not self.threat_hits(th, me.pos, t):
                    continue
                half = th.get("half", math.hypot(*rel) * math.sin(math.radians(th.get("angle", 60) / 2)))
                need = half - abs(side) + 5
                sgn = 1 if side >= 0 else -1
                vec = (d[1] * sgn, -d[0] * sgn)
            elif k in ("circle", "circle_self"):
                left = th["resolve"] - t
                center = th["center"] if k == "circle" else self.f[th["owner"]].pos
                if not self.threat_hits(th, me.pos, t):
                    continue
                need = th["radius"] - dist(center, me.pos) + 5
                vec = norm(me.pos[0] - center[0], me.pos[1] - center[1])
                if vec == (0.0, 0.0):
                    vec = (1.0, 0.0)
            else:
                continue
            if left <= 0:
                continue
            walk = me.speed(t) * left if me.can_move(t) else 0.0
            if walk >= need:
                me.dodge_plan = (vec, t + min(left, need / max(me.speed(t), 1) + 0.05))
                me.stats["dodged"] += 1
                return
            if t >= me.dash_ready and me.can_act(t) and t >= me.root_until and self.rng.random() < me.op.dash_dodge:
                self.start_dash(me, vec, t)
                me.stats["dash_dodged"] += 1
                return

    def start_dash(self, me, vec, t):
        me.dash = (t, self.rules.dash_dur, list(me.pos), vec, self.rules.dash_dist, None, 0, True)
        cd = self.rules.dash_cd * (0.6 if me.st["ks_agility"] else 1.0)
        me.dash_ready = t + cd
        me.action = None if (me.action and me.action["kind"] == "basic") else me.action
        if me.st["ks_agility"]:
            me.agi_buff_until = t + 3.0
            me.agi_empower_until = t + 3.0

    # ---------------- step ----------------
    def step(self):
        t = self.t
        dt = self.DT
        for me in self.f:
            foe = self.f[1 - me.idx]
            me.hp = min(me.st["max_hp"], me.hp + me.st["regen"] * dt)
            me.momentum = min(100, me.momentum + 5 * dt)
            if dist(me.pos, foe.pos) <= 250:
                me.stats["in_range_time"] += dt
        # displacement first
        for me in self.f:
            if me.kb:
                s, dur, o, d, dd = me.kb
                u = min(1.0, (t - s) / dur)
                me.pos = [o[0] + d[0] * dd * u, o[1] + d[1] * dd * u]
                if u >= 1.0:
                    me.kb = None
            elif me.dash:
                s, dur, o, d, dd, aid, rank, innate = me.dash
                u = min(1.0, (t - s) / dur)
                prog = 1 - (1 - u) ** 3
                old = list(me.pos)
                me.pos = [o[0] + d[0] * dd * prog, o[1] + d[1] * dd * prog]
                foe = self.f[1 - me.idx]
                if aid and self.c.eff(aid, "dash_contact") and dist(me.pos, foe.pos) <= 80 and not me.dash[7]:
                    me.dash = (s, dur, o, d, dd, aid, rank, True)
                    self.apply_payload(me, foe, aid, rank, t)
                    if aid == "active_crotch_grab":
                        fd = norm(foe.vel[0], foe.vel[1])
                        behind = (fd[0] * d[0] + fd[1] * d[1]) > 0.5 or foe.impaired(t)
                        if behind:
                            self.hard_cc(foe, 1.1, t, "stun")
                        foe.slows.append((0.35, t + 1.5))
                if u >= 1.0:
                    me.dash = None
            self.clamp_arena(me)
        # threats & releases
        for me in self.f:
            foe = self.f[1 - me.idx]
            if me.hp <= 0:
                continue
            if me.action and t >= me.action.get("release", 1e9):
                if me.action["kind"] == "basic":
                    self.release_basic(me, foe, t)
                else:
                    self.release(me, foe, t)
                    self.threats = [x for x in self.threats if not (x["owner"] == me.idx and x["kind"] in ("line", "cone", "circle_self", "lockon")
                                                                     and x["aid"] == x.get("aid") and x.get("resolve", 0) <= t + 1e-9)]
        self.advance_projectiles(t)
        self.advance_zones(t)
        self.threats = [x for x in self.threats if (x.get("resolve") or 1e9) > t - 0.05 and (x["kind"] != "projectile" or t - x["released"] < 1.0)]
        # decisions & movement
        for me in self.f:
            foe = self.f[1 - me.idx]
            if me.hp <= 0 or foe.hp <= 0:
                continue
            me.vel = [0.0, 0.0]
            if not me.can_act(t) or me.dash or me.kb:
                continue
            self.plan_dodge(me, t)
            if me.dash:
                continue
            busy = me.action is not None or t < me.lock_until
            if me.dodge_plan and me.dodge_plan[1] > t:
                if me.can_move(t):
                    self.move(me, me.dodge_plan[0], t)
                if not busy:
                    self.try_cast(me, foe, t) if me.b.standoff else None
                continue
            if not busy:
                if self.try_cast(me, foe, t):
                    busy = True
            d = dist(me.pos, foe.pos)
            if not busy and d <= 250 and t >= me.next_basic and (me.b.standoff == 0 or d < me.b.standoff * 0.6):
                self.start_basic(me, foe, t)
                busy = True
            if me.action and me.action["kind"] == "basic":
                continue
            if not me.can_move(t):
                continue
            self.navigate(me, foe, t)
        for me in self.f:
            if me.vel != [0.0, 0.0]:
                me.pos[0] += me.vel[0] * dt
                me.pos[1] += me.vel[1] * dt
                self.clamp_arena(me)
        self.t += dt

    def clamp_arena(self, me):
        r = math.hypot(*me.pos)
        if r > self.arena:
            me.pos = [me.pos[0] / r * self.arena, me.pos[1] / r * self.arena]

    def move(self, me, vec, t):
        s = me.speed(t)
        me.vel = [vec[0] * s, vec[1] * s]

    def navigate(self, me, foe, t):
        d = dist(me.pos, foe.pos)
        to = norm(foe.pos[0] - me.pos[0], foe.pos[1] - me.pos[1])
        so = me.b.standoff
        if so == 0:
            # pursue; dash to close
            if d > 320 and d < self.rules.dash_dist + 230 and t >= me.dash_ready and self.rng.random() < (0.25 if me.op.dash_dodge > 0.5 else 0.6):
                self.start_dash(me, to, t)
                return
            juke = me.op.juke * 0.35 if d > 300 else 0.0
            self.strafe_update(me, t)
            vec = norm(to[0] + juke * me.strafe_dir * -to[1], to[1] + juke * me.strafe_dir * to[0])
            self.move(me, vec, t)
            return
        # kite
        if d < 260 and t >= me.dash_ready:
            away = (-to[0], -to[1])
            # bias the escape toward the arena centre when near the wall
            c = norm(-me.pos[0], -me.pos[1])
            if math.hypot(*me.pos) > self.arena - 400:
                away = norm(away[0] + c[0], away[1] + c[1])
            self.start_dash(me, away, t)
            return
        self.strafe_update(me, t)
        if d < so - 40:
            away = (-to[0], -to[1])
            if math.hypot(*me.pos) > self.arena - 300:
                tang = (-to[1] * me.strafe_dir, to[0] * me.strafe_dir)
                away = norm(away[0] + 1.2 * tang[0], away[1] + 1.2 * tang[1])
            vec = norm(away[0] + me.op.juke * 0.3 * me.strafe_dir * -to[1], away[1] + me.op.juke * 0.3 * me.strafe_dir * to[0])
        elif d > so + 60:
            vec = to
        else:
            vec = (-to[1] * me.strafe_dir, to[0] * me.strafe_dir) if me.op.juke > 0 else (0.0, 0.0)
        if vec != (0.0, 0.0):
            self.move(me, vec, t)

    def strafe_update(self, me, t):
        if t >= me.strafe_until:
            me.strafe_dir = self.rng.choice((-1, 1))
            me.strafe_until = t + self.rng.uniform(0.35, 0.9)

    def start_basic(self, me, foe, t):
        as_ = me.st["as"] * (1.35 if me.empower and me.empower[5] and t < me.empower[1] else 1.0)
        wu = math.ceil(1000 * 0.18 * 0.85 / as_) / 1000
        me.action = {"kind": "basic", "release": t + wu, "start": t}
        me.next_basic = t + math.ceil(1000 / as_) / 1000

    def release_basic(self, me, foe, t):
        me.action = None
        if t - me.combo_t > 2.5:
            me.combo = 0
        me.combo_t = t
        step = me.combo
        me.combo = (me.combo + 1) % 3
        ratio = [0.9, 0.9, 1.3][step]
        amt = me.st["ad"] * ratio
        if self.rng.random() < me.st["crit"]:
            amt *= 2
        if me.empower and t < me.empower[1] and me.empower[0] > 0:
            ch, until, base, adr, apr, asb, rank = me.empower
            bs = [1, 1.35, 1.7][rank - 1]
            rs = [1, 1.25, 1.5][rank - 1]
            amt += base * me.st["G"] * bs + adr * rs * me.st["ad"] + apr * rs * me.st["ap"]
            me.empower = (ch - 1, until, base, adr, apr, asb, rank) if ch > 1 else None
        if me.st["ks_agility"] and t < me.agi_empower_until:
            amt += 35 + 0.45 * me.st["ad"]
            me.agi_empower_until = 0.0
        self.deal(me, foe, amt, False, "basic", t)
        me.momentum = min(100, me.momentum + 10 * (1 / me.st["as"]) / 1.334)
        if self.rules.stagger_on_melee_basic > 0:
            foe.slows.append((self.rules.stagger_on_melee_basic, t + 0.6))

    def advance_projectiles(self, t):
        keep = []
        for p in self.projectiles:
            me = self.f[p["owner"]]
            foe = self.f[1 - p["owner"]]
            stepd = p["speed"] * self.DT
            p["pos"][0] += p["dir"][0] * stepd
            p["pos"][1] += p["dir"][1] * stepd
            p["left"] -= stepd
            if dist(p["pos"], foe.pos) <= p["radius"] + BODY_R:
                self.apply_payload(me, foe, p["aid"], p["rank"], t)
                continue
            if p["left"] > 0:
                keep.append(p)
        self.projectiles = keep

    def advance_zones(self, t):
        keep = []
        for z in self.zones:
            if t < z["at"]:
                keep.append(z)
                continue
            me = self.f[z["owner"]]
            foe = self.f[1 - z["owner"]]
            d = dist(z["center"], foe.pos)
            pad = BODY_R if self.rules.body_radius_hits else 0
            if d <= z["radius"] + pad:
                if z["kind"] == "impact":
                    scale = 1.0 if d <= z["inner"] + pad else 0.6
                    self.apply_payload(me, foe, z["aid"], z["rank"], t, scale)
                else:
                    e = self.c.eff(z["aid"], "zone_damage")
                    amt, mag = self.raw(me, v(e["base"]), 0, v(e["ap_ratio"]), z["rank"], 0, 0, "Magical")
                    self.deal(me, foe, amt, True, "skill", t)
                    per = (4.5 * me.st["G"] + 0.03 * me.st["ap"]) * 3
                    self.deal(me, foe, per, True, "skill", t)
                    foe.slows.append((0.2, t + 0.5))
        self.zones = keep

    def run(self):
        while self.t < self.cap and all(f.hp > 0 for f in self.f):
            self.step()
        winner = None
        if self.f[0].hp <= 0 and self.f[1].hp > 0:
            winner = 1
        elif self.f[1].hp <= 0 and self.f[0].hp > 0:
            winner = 0
        firsts = [f.stats["first_dmg"] for f in self.f if f.stats["first_dmg"] is not None]
        start = min(firsts) if firsts else self.t
        return {
            "winner": winner,
            "ttk": (self.t - start) if winner is not None else None,
            "t_end": self.t,
            "hp_share": [max(0.0, f.hp) / f.st["max_hp"] for f in self.f],
            "stats": [dict(f.stats) for f in self.f],
        }


# ---------------------------------------------------------------------------------------------
# Experiments
# ---------------------------------------------------------------------------------------------

def run_pair(c, rules, a, b, opa, opb, n, seed, power=(1.0, 1.0)):
    res = []
    for i in range(n):
        # alternate sides to cancel spawn asymmetry
        if i % 2 == 0:
            r = Duel(c, rules, BUILDS[a], BUILDS[b], opa, opb, seed + i, power).run()
        else:
            r = Duel(c, rules, BUILDS[b], BUILDS[a], opb, opa, seed + i, (power[1], power[0])).run()
            r["winner"] = None if r["winner"] is None else 1 - r["winner"]
            r["hp_share"] = r["hp_share"][::-1]
            r["stats"] = r["stats"][::-1]
        res.append(r)
    wins = sum(1 for r in res if r["winner"] == 0)
    losses = sum(1 for r in res if r["winner"] == 1)
    ttks = [r["ttk"] for r in res if r["ttk"] is not None]
    s0 = [r["stats"][0] for r in res]
    s1 = [r["stats"][1] for r in res]
    out = {
        "a": a, "b": b, "n": n,
        "a_win": wins / n, "b_win": losses / n, "timeout": (n - wins - losses) / n,
        "score_a": (wins + 0.5 * (n - wins - losses)) / n,
        "ttk_median": statistics.median(ttks) if ttks else None,
        "ttk_p25": statistics.quantiles(ttks, n=4)[0] if len(ttks) >= 4 else None,
        "ttk_p75": statistics.quantiles(ttks, n=4)[2] if len(ttks) >= 4 else None,
        "a_in_range_share": statistics.mean(x["in_range_time"] for x in s0) / max(1e-9, statistics.mean(r["t_end"] for r in res)),
        "a_dodges": statistics.mean(x["dodged"] + x["dash_dodged"] for x in s0),
        "b_dodges": statistics.mean(x["dodged"] + x["dash_dodged"] for x in s1),
        "a_basic_share": statistics.mean(x["basic"] / x["dmg"] if x["dmg"] else 0 for x in s0),
        "b_basic_share": statistics.mean(x["basic"] / x["dmg"] if x["dmg"] else 0 for x in s1),
    }
    return out


VARIANTS = {
    "V0_baseline": Rules(),
    "V1_speed270": Rules(move_speed=270, label="270"),
    "V2_speed300": Rules(move_speed=300, label="300"),
    "V3_freecast": Rules(cast_move="free", reclick=0.0, label="move-cast"),
    "V4_commit": Rules(cast_move="commit", recovery=0.25, reclick=0.0, windup_floor=0.25, ground_cue_at_windup=True,
                       body_radius_hits=True, knockback_dr=True, move_speed=270, label="threat-grammar"),
    "V5_highmob": Rules(move_speed=320, cast_move="free", reclick=0.0, hp_scale=1.25, dash_cd=6.0, dash_dist=350,
                        label="high-mobility"),
    "V6_counterplay": Rules(move_speed=255, knockback_dr=True, stagger_on_melee_basic=0.15, ranged_cast_slow=0.0,
                            ground_cue_at_windup=True, label="contact-pact"),
    "V7_readable": Rules(move_speed=270, windup_floor=0.30, ground_cue_at_windup=True, label="readable-windows"),
}

# D01-B as actually proposed (added after the S5 review): only the named classes change.
#  class 2 (quick lines/cones): narrow so that move_speed * windup >= half width (no lead -> miss a moving target)
#  class 3 (delayed ground): landing visible from windup start, delay 0.55 -> 0.35
#  class 4 (high-reward dash/hard control in the sample builds): 0.25 s recovery (no move/cast/basic)
D01B_SKILLS = {
    "active_pants_down": {"half_width": 35.0},
    "active_hose_down": {"half_width": 50.0},
    "active_cola_shake": {"angle": 30.0, "windup": 0.22},
    "active_calculus_drop": {"delay": 0.35},
    "active_crotch_grab": {"recovery": 0.25},
    "active_charged_ram": {"recovery": 0.25},
}
VARIANTS_D01B = {
    "V0_baseline": Rules(),
    "V1_speed270": Rules(move_speed=270, label="270"),
    "V8_D01B_240": Rules(per_skill=D01B_SKILLS, ground_cue_at_windup=True, label="D01-B@240"),
    "V8_D01B_262": Rules(move_speed=262, per_skill=D01B_SKILLS, ground_cue_at_windup=True, label="D01-B@262"),
    "V8_D01B_262_c2only": Rules(move_speed=262, label="D01-B class2 only",
                                per_skill={k: x for k, x in D01B_SKILLS.items() if k in ("active_pants_down", "active_hose_down", "active_cola_shake")}),
    "V8_D01B_262_c4only": Rules(move_speed=262, label="D01-B class4 only",
                                per_skill={k: x for k, x in D01B_SKILLS.items() if k in ("active_crotch_grab", "active_charged_ram")}),
}


def experiment_d01b(c, n=40):
    pairs = [("melee", "ranged"), ("control", "ranged"), ("heavy", "ranged"), ("mobility", "ranged"), ("heavy", "control"),
             ("melee", "cycle"), ("melee", "melee"), ("control", "control")]
    out = {}
    for vk, rules in VARIANTS_D01B.items():
        out[vk] = {}
        for op in (SKILLED, CASUAL):
            out[vk][op.label] = [run_pair(c, rules, a, b, op, op, n, 1000) for a, b in pairs]
        sharp = replace(SKILLED, juke=0.3, lead=0.5, aim_sigma=40.0, label="sharp")
        dull = replace(SKILLED, juke=0.3, lead=0.5, aim_sigma=40.0, reaction=0.55, dodge=0.2, dash_dodge=0.1, label="dull")
        out[vk]["reaction_value"] = [{"arch": arch, "sharp_score": run_pair(c, rules, arch, arch, sharp, dull, n, 7000)["score_a"]}
                                     for arch in ("melee", "ranged", "control", "cycle", "heavy")]
    return out


def experiment_s3b(c, n=40):
    """S3b skill-review variants (L2, direction only): S02 as a real Type 2, S18 landing cue from windup start."""
    s02 = {"active_pants_down": {"windup": 0.22, "half_width": 50.0}}
    s18_cue = {}
    s18_040 = {"active_calculus_drop": {"delay": 0.40}}
    variants = {
        "V0_baseline": Rules(),
        "S02_type2": Rules(per_skill=s02, label="S02 0.22s/50cm"),
        "S18_cue_055": Rules(per_skill=s18_cue, ground_cue_at_windup=True, label="S18 cue@windup"),
        "S18_cue_040": Rules(per_skill=s18_040, ground_cue_at_windup=True, label="S18 cue@windup delay0.40"),
        "S02+S18_040": Rules(per_skill={**s02, **s18_040}, ground_cue_at_windup=True, label="both"),
    }
    pairs = [("control", "ranged"), ("melee", "ranged"), ("heavy", "ranged"), ("cycle", "melee"), ("heavy", "control"),
             ("control", "control"), ("melee", "melee")]
    still = replace(SKILLED, juke=0.0, label="no-juke")
    out = {}
    for vk, rules in variants.items():
        out[vk] = {}
        for op in (SKILLED, CASUAL):
            out[vk][op.label] = [run_pair(c, rules, a, b, op, op, n, 1000) for a, b in pairs]
        # Juke value: same skilled operators, the defender either strafes (juke 1.0) or does not (0.0).
        out[vk]["juke_value"] = [
            {"attacker": a, "defender": b,
             "attacker_vs_juker": run_pair(c, rules, a, b, SKILLED, SKILLED, n, 9000)["score_a"],
             "attacker_vs_still": run_pair(c, rules, a, b, SKILLED, still, n, 9000)["score_a"]}
            for a, b in (("control", "ranged"), ("melee", "ranged"), ("control", "melee"))]
    return out


def experiments(c, n=40, quick=False):
    if quick:
        n = 12
    out = {}
    # E2: melee vs ranged and mirror across rule variants, skilled operators.
    pairs = [("melee", "ranged"), ("heavy", "ranged"), ("mobility", "ranged"), ("melee", "cycle"),
             ("control", "ranged"), ("defense", "ranged"), ("melee", "melee"), ("ranged", "ranged")]
    out["E2_variants"] = {}
    for vk, rules in VARIANTS.items():
        rows = [run_pair(c, rules, a, b, SKILLED, SKILLED, n, 1000) for a, b in pairs]
        out["E2_variants"][vk] = rows
    # E3: operation vs build power: skilled vs casual mirror, with build power multipliers.
    out["E3_skill_vs_power"] = {}
    for vk in ("V0_baseline", "V4_commit", "V5_highmob"):
        rules = VARIANTS[vk]
        grid = []
        for arch in ("melee", "ranged", "cycle", "control"):
            for pw in (1.0, 1.3, 1.7, 2.5):
                r = run_pair(c, rules, arch, arch, CASUAL, SKILLED, n, 3000, power=(pw, 1.0))
                grid.append({"arch": arch, "casual_power": pw, "casual_score": r["score_a"], "ttk_median": r["ttk_median"]})
        out["E3_skill_vs_power"][vk] = grid
    # E4: round robin of representative builds at baseline (skilled mirror operators).
    keys = list(BUILDS)
    rr = []
    for i, a in enumerate(keys):
        for b in keys[i + 1:]:
            rr.append(run_pair(c, VARIANTS["V0_baseline"], a, b, SKILLED, SKILLED, n, 5000))
    out["E4_round_robin_baseline"] = rr
    # E5: value of reaction-dodging alone (same aim/juke; only reaction + dodge differ), mirror at parity.
    sharp = replace(SKILLED, juke=0.3, lead=0.5, aim_sigma=40.0, label="sharp")
    dull = replace(SKILLED, juke=0.3, lead=0.5, aim_sigma=40.0, reaction=0.55, dodge=0.2, dash_dodge=0.1, label="dull")
    out["E5_reaction_value"] = {}
    for vk in ("V0_baseline", "V2_speed300", "V4_commit", "V7_readable"):
        out["E5_reaction_value"][vk] = [
            {"arch": arch, "sharp_score": run_pair(c, VARIANTS[vk], arch, arch, sharp, dull, n, 7000)["score_a"]}
            for arch in ("melee", "ranged", "control", "cycle", "heavy")]
    return out


def main():
    root = Path(sys.argv[1])
    outdir = Path(sys.argv[sys.argv.index("--out") + 1]) if "--out" in sys.argv else Path(__file__).parent
    quick = "--quick" in sys.argv
    c = Content(root)
    esc = escape_rows(c)
    (outdir / "data_escape_matrix.json").write_text(json.dumps(esc, ensure_ascii=False, indent=1), encoding="utf-8")
    dash_cover = {need: dash_cover_time(need) for need in (65, 80, 95, 100, 200, 240, 300, 340)}
    res = {"escape_rows": len(esc), "dash_cover_s": dash_cover}
    if "--s3b" in sys.argv:
        (outdir / "data_s3b_skill_variants.json").write_text(json.dumps(experiment_s3b(c), ensure_ascii=False, indent=1), encoding="utf-8")
        return
    if "--d01b" in sys.argv:
        (outdir / "data_d01b_results.json").write_text(json.dumps(experiment_d01b(c), ensure_ascii=False, indent=1), encoding="utf-8")
        return
    if "--no-duel" not in sys.argv:
        res["experiments"] = experiments(c, quick=quick)
    (outdir / "data_duel_results.json").write_text(json.dumps(res, ensure_ascii=False, indent=1), encoding="utf-8")
    print(json.dumps({"dash_cover_s": dash_cover}, indent=1))


if __name__ == "__main__":
    main()
