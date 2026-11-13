"""Monte Carlo model of ordinary component acquisition, installation and build formation.

Reads the authoritative SourceData/Demo/content.json (catalog, hosts, opening pool, progression bands, offer levels,
bonus component levels and workshop capacity) and replays the draw rules of LDProgression::GrantLevelComponents: one
ordinary material per level 2-16 and a second one on each `bonus_component_levels` level, each drawn by
DrawLevelComponent (category by the band weights over the categories that still hold an unowned, drawable common
definition: hostable now and, when it lists `draw_requires_actives`, with one of those actives learned; then uniform
within the category). Active opportunities: the opening three-choice from the opening pool, then three unlearned actives
at each `active_offer_levels` entry; the component rolls of that level see the build before the new active is chosen.
Optional elite-camp two-choices draw two distinct unowned common definitions uniformly (LDProgression::DrawFieldReward)
and optional rest dismantles trade GROWTH.DismantleInputCount idle components for one uniform unowned common one;
airdrop/Boss scarce rewards are out of scope.

Installation follows the workshop capacity (LDProgression::WorkshopCapacity): every installed base, mod and connector
takes one slot. After each level the modelled player re-chooses the installed set greedily (working transforms first,
then the ones that link to what is already installed); everything else waits in the storage. The legacy supply
(`--legacy-supply`) reproduces the earlier rules: one draw per level and no capacity (every hostable component counts).

Hosting is authoritative (content targets). Usefulness is a design model: TAGS records what each active and component
produces and needs, and whether a component transforms behaviour or only adds efficiency. The metrics are therefore
model estimates of build formation, not observed player data. Uses Python's PRNG rather than the native splitmix, so
the distributions match in law, not draw-for-draw.
"""
from __future__ import annotations

import argparse
import json
import math
import random
import statistics
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONTENT = ROOT / "SourceData" / "Demo" / "content.json"

ELEMENTS = ("fire", "water", "electric")

# Active id -> tags it produces. "control" is hard control (root, stun, silence, disarm, taunt); "soft" is a slow.
ACTIVE_TAGS: dict[str, set[str]] = {
    "active_elbow": {"hit", "knockback", "melee"},
    "active_pants_down": {"hit", "control", "ranged"},
    "active_crotch_grab": {"hit", "control", "melee"},
    "active_account_banned": {"hit", "control", "ranged"},
    "active_unplug": {"hit", "control", "melee"},
    "active_snark_bolt": {"hit", "projectile", "soft", "ranged"},
    "active_seethe": {"hit", "fire", "area"},
    "active_quick_meeting": {"hit", "zone", "fire", "soft", "ranged", "area"},
    "active_hose_down": {"hit", "knockback", "water", "area"},
    "active_zap": {"hit", "electric", "chain", "ranged"},
    "active_bail_out": {"dash", "empower"},
    "active_apology_slide": {"dash", "hit", "soft"},
    "active_not_mad": {"shield"},
    "active_stare_down": {"control", "area"},
    "active_triple_combo": {"empower"},
    "active_calculus_drop": {"hit", "area", "ranged", "impact"},
    "active_flyer_trap": {"hit", "trap", "soft", "ranged", "area"},
    "active_open_mic": {"hit", "wave", "soft", "area"},
    "active_charged_ram": {"dash", "hit", "shield", "electric"},
    "active_brace": {"shield", "hit", "area"},
    "active_dirty_puddle": {"hit", "zone", "water", "area", "ranged"},
    "active_sock_shot": {"hit", "projectile", "ranged"},
}
INNATE = {"basic", "dash"}

# Component id -> (class, produces, needs). class: "transform" changes behaviour, targeting, timing, location, movement
# or resources; "efficiency" scales numbers; "generic" marks the broad C13-C16 attachments that the skill-centric analysis does not count
# as transformative paths. `needs` is a list of alternatives; each alternative is a set of tags that must all be present.
T, E, G = "transform", "efficiency", "generic"
COMPONENT_TAGS: dict[str, tuple[str, set[str], list[set[str]]]] = {
    "component_fist_fire": (T, {"fire", "burn"}, [{"basic"}]),
    "component_tab_ledger": (T, {"ledger"}, [{"hit"}, {"basic"}]),
    "component_spiky_hide": (T, {"counter"}, [set()]),
    "component_punch_heal": (E, {"heal"}, [{"basic"}]),
    "component_dash_bump": (T, {"hit"}, [{"dash"}]),
    "component_third_punch": (T, {"knockback"}, [{"basic"}]),
    "component_kick_down": (E, set(), [{"control"}, {"soft"}, {"knockback"}]),
    "component_breach_fist": (E, set(), [{"basic"}]),
    "component_hard_skin": (E, {"shield"}, [set()]),
    "component_fist_water": (T, {"water"}, [{"basic"}]),
    "component_fist_shock": (T, {"electric"}, [{"basic"}]),
    "component_dash_punch": (T, {"empower"}, [{"dash"}]),
    "component_catch_breath": (E, set(), [{"hit"}]),
    "component_long_arm": (E, set(), [{"ranged"}]),
    "component_seeing_red": (E, set(), [{"two_actives"}]),
    "component_thick_hide": (E, {"shield"}, [{"hit"}]),
    "component_held_shot": (E, set(), [{"hit"}]),
    "component_dirty_trick": (T, {"element_any"}, [{"hit"}]),
    "component_clear_head": (E, set(), [set()]),
    "component_run_away": (E, set(), [set()]),
    "component_guard_up": (E, set(), [set()]),
    "component_crowd_hit": (T, set(), [set()]),
    "component_settle_later": (T, set(), [{"ledger"}, {"counter"}]),
    "component_long_kick": (T, {"knockback"}, [{"knockback"}]),
    "component_hold_down": (E, set(), [{"control"}]),
    "component_reach_out": (E, set(), [set()]),
    "component_again_now": (E, set(), [set()]),
    "component_one_slam": (T, set(), [set()]),
    "component_ram_through": (T, {"knockback"}, [{"dash"}]),
    "component_quick_hop": (T, set(), [{"dash"}]),
    "component_big_tab": (T, set(), [{"ledger"}]),
    "component_pick_you": (T, set(), [set()]),
    "component_owe_it": (T, set(), [set()]),
    "component_fire_skin": (T, {"shield"}, [{"fire"}]),
    "component_ball_kick": (T, {"impact", "control"}, [{"knockback"}]),
    "component_bump_again": (T, set(), [{"dash"}]),
    "component_roll_bump": (T, {"knockback"}, [{"dash"}]),
    "component_pin_punch": (T, {"empower"}, [{"control"}]),
    "component_after_pin": (T, {"hit"}, [{"zone"}, {"area"}, {"control"}]),
    "component_control_mana": (T, {"mana"}, [{"control"}]),
    "component_breath_strike": (T, {"counter"}, [{"heal"}]),
    "component_blast_chase": (T, {"empower"}, [{"reaction"}]),
    "component_shell_burst": (T, {"hit"}, [{"shield"}]),
    "component_hurt_heal": (E, {"heal"}, [{"hit"}]),
    "component_dirty_fist": (T, set(), [set()]),
    "component_boomerang": (T, {"hit"}, [{"projectile"}, {"wave"}]),
    "component_skewer": (T, {"hit"}, [{"projectile"}, {"impact"}, {"wave"}]),
    "component_encore": (T, {"hit"}, [set()]),
    "component_next_up": (G, set(), [{"hit"}]),
    "component_setup_line": (G, set(), [{"two_actives"}]),
    "component_blast_reload": (G, set(), [{"reaction"}]),
    "component_group_refund": (G, set(), [{"hit"}]),
    "component_zone_tether": (T, {"zone"}, [{"zone"}]),
    "component_shove_off": (T, {"knockback"}, [{"shield"}]),
    "component_finisher": (T, {"knockback"}, [{"empower"}, {"wave"}]),
    "component_long_handle": (T, {"ranged"}, [{"empower"}]),
    "component_come_here": (T, {"pull"}, [{"knockback"}, {"control"}]),
    "component_move_meeting": (T, set(), [{"zone", "dash"}, {"trap", "dash"}]),
    "component_bottled_up": (T, {"hit"}, [{"shield", "two_actives"}]),
    "component_spend_fist": (T, {"hit"}, [{"empower", "two_actives"}]),
    "component_wide_open": (T, {"area"}, [{"control", "two_actives"}]),
    "component_set_it_off": (T, {"burst"}, [{"zone"}]),
    "component_meet_here": (T, {"zone"}, [{"zone", "hit"}, {"trap", "hit"}]),
}

# Pre-rework semantics of the reworked ids, for --legacy-content comparisons.
LEGACY_OVERRIDES = {
    "component_catch_breath": (E, set(), [set()]),
    "component_long_arm": (E, set(), [{"basic"}]),
    "component_seeing_red": (E, set(), [set()]),
    "component_thick_hide": (E, set(), [set()]),
}


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


class Catalog:
    def __init__(self, content: dict, *, filtered: bool, tags: dict | None = None, overrides: dict | None = None):
        overrides = overrides or {}
        self.content = content
        self.filtered = filtered
        self.actives = [a["id"] for a in content["abilities"]]
        self.opening = [a["id"] for a in content["abilities"] if a.get("opening")]
        prog = content["progression"]
        self.max_level = prog["max_level"]
        self.offer_levels = list(prog["active_offer_levels"])
        self.bonus_levels = set(overrides.get("bonus_levels", prog.get("bonus_component_levels", [])))
        capacity = prog.get("workshop_capacity")
        self.capacity_base = overrides.get("capacity_base", capacity["base"] if capacity else None)
        self.capacity_step = overrides.get("capacity_step", capacity["levels_per_slot"] if capacity else None)
        self.affinity = overrides.get("affinity", prog.get("active_affinity_weight", 1))
        self.dismantle_count = int(content["parameters"].get("GROWTH.DismantleInputCount", 3))
        self.bands = [(b["levels"][0], b["levels"][1], (b["base"], b["mod"], b["connector"]))
                      for b in prog["component_category_weights"]]
        self.components = {c["id"]: c for c in content["components"]}
        self.common = [c["id"] for c in content["components"] if c.get("pool") == "common"]
        self.tags = tags or COMPONENT_TAGS
        missing = [cid for cid in self.components if cid not in self.tags]
        if missing:
            raise SystemExit(f"TAGS missing component ids: {missing}")
        missing = [aid for aid in self.actives if aid not in ACTIVE_TAGS]
        if missing:
            raise SystemExit(f"ACTIVE_TAGS missing active ids: {missing}")

    def draws(self, level: int, legacy: bool) -> int:
        return 1 if legacy or level not in self.bonus_levels else 2

    def capacity(self, level: int, legacy: bool) -> int | None:
        if legacy or self.capacity_base is None:
            return None
        return self.capacity_base + (level - 1) // self.capacity_step

    def uniform_unowned(self, rng: random.Random, owned: set[str], count: int) -> list[str]:
        pool = [cid for cid in self.common if cid not in owned]
        return rng.sample(pool, min(count, len(pool)))

    def kind(self, cid: str) -> int:
        return {"base": 0, "mod": 1, "connector": 2}[self.components[cid]["kind"]]

    def opening_bases(self, opening_active: str) -> list[str]:
        out = []
        for cid in self.common:
            c = self.components[cid]
            if c["kind"] != "base":
                continue
            o = c.get("opening")
            if o == "always" or (isinstance(o, dict) and opening_active in o.get("actives", [])):
                out.append(cid)
        return out

    def hosts(self, cid: str, actives: list[str], owned: set[str]) -> list[str]:
        c = self.components[cid]
        if c["kind"] == "base":
            b = c.get("binding") or {}
            listed = b.get("actives")
            return ["self"] if not listed or any(a in listed for a in actives) else []
        t = c.get("targets", {})
        extra = t.get("bases", [])
        if extra and not any(e in owned for e in extra):
            return []
        out = []
        if t.get("self"):
            out.append("self")
        if t.get("dash"):
            out.append("D")
        out += [a for a in t.get("actives", []) if a in actives]
        out += [h for h in t.get("components", []) if h in owned and h != cid]
        return out

    def drawable(self, cid: str, actives: list[str], owned: set[str]) -> bool:
        """Hostable now and, when the component lists draw_requires_actives, one of them learned."""
        required = self.components[cid].get("draw_requires_actives")
        return bool(self.hosts(cid, actives, owned)) and (not required or any(a in required for a in actives))

    def band(self, level: int) -> tuple[int, int, int]:
        rows = [w for lo, hi, w in self.bands if lo <= level <= hi]
        if len(rows) != 1:
            raise SystemExit(f"level {level} not covered by exactly one band")
        return rows[0]

    def form(self, cid: str) -> str | None:
        return self.components[cid].get("form")

    def skill_affine(self, cid: str, actives: list[str]) -> bool:
        """Lists a learned active as a target, binding or draw requirement (LDProgression::DrawLevelComponent affinity)."""
        c = self.components[cid]
        listed = set(c.get("targets", {}).get("actives", [])) | set((c.get("binding") or {}).get("actives", []) or [])
        listed |= set(c.get("draw_requires_actives", []))
        return bool(listed & set(actives))

    def draw(self, rng: random.Random, level: int, actives: list[str], owned: set[str], weights=None) -> str | None:
        w = weights(level) if weights else self.band(level)
        pools: list[list[str]] = [[], [], []]
        for cid in self.common:
            if cid in owned:
                continue
            if self.filtered and not self.drawable(cid, actives, owned):
                continue
            pools[self.kind(cid)].append(cid)
        total = sum(w[k] for k in range(3) if pools[k])
        if total <= 0:
            return None
        roll = rng.randrange(total)
        for k in range(3):
            if not pools[k]:
                continue
            if roll < w[k]:
                ticket = [self.affinity if self.skill_affine(cid, actives) else 1 for cid in pools[k]]
                pick = rng.randrange(sum(ticket))
                for cid, t in zip(pools[k], ticket):
                    if pick < t:
                        return cid
                    pick -= t
            roll -= w[k]
        raise AssertionError("unreachable")


def build_tags(cat: Catalog, actives: list[str], owned: set[str]) -> set[str]:
    tags = set(INNATE)
    for a in actives:
        tags |= ACTIVE_TAGS[a]
    if len(actives) >= 2:
        tags.add("two_actives")
    for cid in owned:
        if cat.hosts(cid, actives, owned):
            tags |= cat.tags[cid][1]
    if "element_any" in tags:
        tags |= set(ELEMENTS)
    if sum(e in tags for e in ELEMENTS) >= 2:
        tags.add("reaction")
    return tags


def satisfied(needs: list[set[str]], tags: set[str]) -> bool:
    return any(alt <= tags for alt in needs)


def working(cat: Catalog, cid: str, actives: list[str], owned: set[str], tags: set[str]) -> bool:
    return bool(cat.hosts(cid, actives, owned)) and satisfied(cat.tags[cid][2], tags)


def active_hosts(cat: Catalog, cid: str, actives: list[str], owned: set[str]) -> set[str]:
    return {h for h in cat.hosts(cid, actives, owned) if h in cat.actives}


def links(cat: Catalog, actives: list[str], owned: set[str]) -> dict[str, set[str]]:
    """Undirected component graph: X-Y when Y needs a tag X produces that the actives alone do not supply, or Y hosts on X."""
    base_tags = set(INNATE)
    for a in actives:
        base_tags |= ACTIVE_TAGS[a]
    if len(actives) >= 2:
        base_tags.add("two_actives")
    tags = build_tags(cat, actives, owned)
    live = [c for c in owned if working(cat, c, actives, owned, tags) and cat.tags[c][0] == T]
    graph: dict[str, set[str]] = {c: set() for c in live}
    for y in live:
        needed = set().union(*cat.tags[y][2]) - {"two_actives"}
        for x in live:
            if x == y:
                continue
            produced = cat.tags[x][1] | (set(ELEMENTS) if "element_any" in cat.tags[x][1] else set())
            feeds = (needed & produced) - base_tags
            if "reaction" in needed and produced & set(ELEMENTS):
                feeds.add("reaction")
            hosted = x in cat.components[y].get("targets", {}).get("components", [])
            if feeds or hosted:
                graph[x].add(y)
                graph[y].add(x)
    return graph


def has_chain(cat: Catalog, actives: list[str], owned: set[str], graph: dict[str, set[str]]) -> bool:
    for mid, nbrs in graph.items():
        for a in nbrs:
            for b in nbrs:
                if a >= b:
                    continue
                skills = set()
                for c in (a, mid, b):
                    skills |= active_hosts(cat, c, actives, owned)
                if len(skills) >= 2:
                    return True
    return False


def transform_on_active(cat: Catalog, actives: list[str], owned: set[str]) -> bool:
    tags = build_tags(cat, actives, owned)
    return any(cat.tags[c][0] == T and cat.components[c]["kind"] != "base" and active_hosts(cat, c, actives, owned)
               and working(cat, c, actives, owned, tags) for c in owned)


def score_active(cat: Catalog, active: str, actives: list[str], owned: set[str]) -> int:
    trial = actives + [active]
    tags = build_tags(cat, trial, owned)
    return sum(1 for c in owned if working(cat, c, trial, owned, tags)) * 3 + sum(
        1 for c in cat.common if c not in owned and cat.tags[c][0] == T and active in cat.components[c].get("targets", {}).get("actives", []))


def score_component(cat: Catalog, cid: str, actives: list[str], owned: set[str]) -> int:
    trial = owned | {cid}
    graph = links(cat, actives, trial)
    return 10 * len(graph.get(cid, ())) + (5 if working(cat, cid, actives, trial, build_tags(cat, actives, trial)) else 0) + (
        2 if cat.tags[cid][0] == T else 0)


VALUE = {T: 3, E: 1, G: 1}
# The "themed" chooser's reading of what the learned actives are about, in SourceData use tags.
ACTIVE_THEME = {"projectile": "projectile", "zone": "area", "area": "area", "trap": "area", "control": "control",
                "fire": "element", "water": "element", "electric": "element", "dash": "collision", "knockback": "collision",
                "shield": "defense", "empower": "basic", "ranged": "spell", "chain": "spell", "wave": "spell", "impact": "area"}
# Build themes the "machine" chooser commits to (SourceData use tags; tempo and ledger ride along with any theme).
MACHINE_THEMES = ("basic", "collision", "spell", "area", "control", "element", "projectile", "defense")
POLICIES = ("random", "synergy", "themed", "machine")


def place(cat: Catalog, c: str, actives: list[str], owned: set[str], placed: dict[str, str]) -> str | None:
    """The first host of c that no installed component of the same form already holds (TargetConflict), else None."""
    form = cat.form(c)
    for host in cat.hosts(c, actives, owned):
        if not form or not any(h == host and cat.form(o) == form for o, h in placed.items()):
            return host
    return None


def machine_theme(cat: Catalog, actives: list[str], owned: set[str], live: list[str], current: str | None) -> str | None:
    """The theme with the most working transforms owned plus the learned actives' themes; switch only on a clear lead (2)."""
    theme_of_actives = Counter(ACTIVE_THEME[t] for a in actives for t in ACTIVE_TAGS[a] if t in ACTIVE_THEME)
    score = Counter()
    for t in MACHINE_THEMES:
        score[t] = sum(1 for c in live if cat.tags[c][0] == T and t in cat.components[c].get("tags", [])) + 0.5 * theme_of_actives[t]
    best = max(MACHINE_THEMES, key=lambda t: (score[t], t))
    if current is None or score[best] >= score[current] + 2:
        return best
    return current


def choose_installed(cat: Catalog, rng: random.Random, actives: list[str], owned: set[str], capacity: int | None,
                     previous: set[str], policy: str = "synergy", theme: str | None = None) -> set[str]:
    """The modelled player's installed set: everything placeable without a capacity, else a pick by the policy.

    random: a uniformly ordered fill of working components (no value judgement). synergy: greedy by value (a working
    transform 3, a working efficiency or generic attachment 1) plus 2 per link to an already chosen component and 0.5
    inertia for what was installed before. themed: synergy plus 1 per use tag shared with the learned actives' themes.
    machine: synergy plus 3 for the committed build theme (machine_theme). Every policy respects form conflicts (one
    component of a form per host) and hosted components need their host.
    """
    hostable = [c for c in sorted(owned) if cat.hosts(c, actives, owned)]
    if capacity is None:
        placed: dict[str, str] = {}
        for c in hostable:
            host = place(cat, c, actives, owned, placed)
            if host:
                placed[c] = host
        return set(placed)
    tags = build_tags(cat, actives, owned)
    live = [c for c in hostable if working(cat, c, actives, owned, tags)]
    graph = links(cat, actives, owned)
    act_theme = {ACTIVE_THEME[t] for a in actives for t in ACTIVE_TAGS[a] if t in ACTIVE_THEME} if policy == "themed" else set()
    order = {c: rng.random() for c in live}
    placed = {}
    while len(placed) < capacity:
        best, best_score, best_host = None, -1e9, None
        for c in live:
            if c in placed:
                continue
            hosted_on = [h for h in cat.components[c].get("targets", {}).get("components", []) if h in owned]
            if hosted_on and not set(hosted_on) & set(placed) and not cat.hosts(c, actives, set()):
                continue
            host = place(cat, c, actives, owned, placed)
            if host is None:
                continue
            ctags = set(cat.components[c].get("tags", []))
            if policy == "random":
                score = order[c]
            else:
                score = (VALUE[cat.tags[c][0]] + 2 * len(graph.get(c, set()) & set(placed)) + (0.5 if c in previous else 0)
                         + len(act_theme & ctags) + order[c] * 0.01)
                if policy == "machine":
                    score += 3 if theme in ctags else 0
            if score > best_score:
                best, best_score, best_host = c, score, host
        if best is None:
            break
        placed[best] = best_host
    return set(placed)


def tag_signature(cat: Catalog, installed: set[str]) -> tuple[str, ...]:
    """The two SourceData use tags most represented among the installed components (ties by name)."""
    counts = Counter(tag for c in installed for tag in cat.components[c].get("tags", []))
    return tuple(sorted(sorted(counts, key=lambda t: (-counts[t], t))[:2]))


def run_one(cat: Catalog, rng: random.Random, policy: str, elite_levels: list[int], weights=None, *, legacy: bool = False,
            dismantle_levels: list[int] | None = None) -> dict:
    pick = (lambda opts, key: max(opts, key=lambda o: (key(o), rng.random()))) if policy != "random" else (
        lambda opts, key: rng.choice(opts))
    actives: list[str] = []
    owned: set[str] = set()
    first = pick(rng.sample(cat.opening, 3), lambda a: score_active(cat, a, [], owned))
    actives.append(first)
    owned.add(pick(cat.opening_bases(first), lambda c: score_component(cat, c, actives, owned)))
    result = {"useful_mod": None, "pair": None, "chain": None, "alternative": None, "pivot": None}
    seen_pair_tags: set[str] = set()
    pending_offer = False
    installed: set[str] = set()
    ever_installed: set[str] = set()
    choice_levels = 0
    left_out_at_end = 0
    replacements = 0
    theme: str | None = None
    theme_changes = 0

    def observe(level: int, new: str | None):
        nonlocal installed, choice_levels, left_out_at_end, replacements, theme, theme_changes
        before = installed
        if policy == "machine":
            full = build_tags(cat, actives, owned)
            live = [c for c in owned if cat.hosts(c, actives, owned) and working(cat, c, actives, owned, full)]
            picked = machine_theme(cat, actives, owned, live, theme)
            theme_changes += 1 if theme is not None and picked != theme else 0
            theme = picked
        installed = choose_installed(cat, rng, actives, owned, cat.capacity(level, legacy), before, policy, theme)
        replacements += len((before & owned) - installed)
        ever_installed.update(installed)
        full_tags = build_tags(cat, actives, owned)
        left_out = [c for c in owned if c not in installed and cat.tags[c][0] == T
                    and cat.hosts(c, actives, owned) and working(cat, c, actives, owned, full_tags)]
        choice_levels += 1 if left_out else 0
        left_out_at_end = len(left_out)
        graph = links(cat, actives, installed)
        if result["useful_mod"] is None and transform_on_active(cat, actives, installed):
            result["useful_mod"] = level
        if result["pair"] is None and any(graph.values()):
            result["pair"] = level
        if result["chain"] is None and has_chain(cat, actives, installed, graph):
            result["chain"] = level
        tags = build_tags(cat, actives, installed)
        if result["alternative"] is None:
            for y in graph:
                needed = set().union(*cat.tags[y][2]) - {"two_actives"}
                providers = [x for x in graph if x != y and (cat.tags[x][1] & needed)]
                if len(providers) >= 2:
                    result["alternative"] = level
                    break
        if new and new in graph and graph[new]:
            pair_tags = set()
            for other in graph[new]:
                pair_tags |= (cat.tags[other][1] | cat.tags[new][1])
            if result["pivot"] is None and level >= 7 and cat.tags[new][0] == T and not (pair_tags <= seen_pair_tags):
                result["pivot"] = level
            seen_pair_tags.update(pair_tags)
        del tags

    for level in range(2, cat.max_level + 1):
        if level in cat.offer_levels:
            pending_offer = True
        new = None
        for _ in range(cat.draws(level, legacy)):
            drawn = cat.draw(rng, level, actives, owned, weights)
            if drawn:
                owned.add(drawn)
                new = drawn
        if pending_offer and len(actives) < cat.content["progression"]["active_slots"]:
            pool = [a for a in cat.actives if a not in actives]
            actives.append(pick(rng.sample(pool, 3), lambda a: score_active(cat, a, actives, owned)))
            pending_offer = False
        if level in elite_levels:
            opts = cat.uniform_unowned(rng, owned, 2)
            if opts:
                chosen = pick(opts, lambda c: score_component(cat, c, actives, owned))
                owned.add(chosen)
                new = chosen
        if dismantle_levels and level in dismantle_levels:
            full = build_tags(cat, actives, owned)
            dead = sorted(c for c in owned - installed if not working(cat, c, actives, owned, full))
            if len(dead) >= cat.dismantle_count:
                surrendered = set(rng.sample(dead, cat.dismantle_count))
                received = cat.uniform_unowned(rng, owned, 1)
                owned -= surrendered
                if received:
                    owned.add(received[0])
                    new = received[0]
        observe(level, new)
    tags = build_tags(cat, actives, installed)
    full_tags = build_tags(cat, actives, owned)
    dead = [c for c in owned if not working(cat, c, actives, owned, full_tags)]
    benched = [c for c in owned if c not in installed and c not in dead]
    generic = [c for c in installed if cat.tags[c][0] == G]
    graph = links(cat, actives, installed)
    linked = {c for c, n in graph.items() if n}
    transforms = [c for c in installed if cat.tags[c][0] == T and working(cat, c, actives, installed, tags)]
    working_owned = [c for c in owned if c not in dead and cat.hosts(c, actives, owned)]
    working_transforms = [c for c in working_owned if cat.tags[c][0] == T]
    result.update(owned=len(owned), installed=len(installed), idle=len(dead), benched=len(benched),
                  generic=len(generic), unlinked_transform=len(set(transforms) - linked), transforms=len(transforms),
                  idle_ids=dead, actives=list(actives), owned_ids=sorted(owned), swaps=len(ever_installed - installed),
                  choice_levels=choice_levels, left_out=left_out_at_end, signature=tag_signature(cat, installed),
                  replacements=replacements, theme_changes=theme_changes, installed_ids=sorted(installed),
                  working_ids=sorted(working_owned),
                  forced_out=left_out_at_end / len(working_transforms) if working_transforms else 0.0)
    return result


def summarize(results: list[dict]) -> dict:
    n = len(results)
    out = {"runs": n}
    for key, label in (("useful_mod", "useful active modifier"), ("pair", "two-component synergy"),
                       ("chain", "three-component cross-skill chain"), ("alternative", "alternative provider"),
                       ("pivot", "pivot-motivating new mechanic (L>=7)")):
        levels = [r[key] for r in results if r[key] is not None]
        by = {str(lv): sum(1 for x in levels if x <= lv) / n for lv in (4, 6, 8, 10, 12, 16)}
        out[label] = {"by_level": by, "median_level": statistics.median(levels) if levels else None}
    for key in ("owned", "installed", "idle", "benched", "generic", "transforms", "unlinked_transform", "swaps",
                "choice_levels", "left_out", "replacements", "theme_changes", "forced_out"):
        vals = [r[key] for r in results]
        out[key] = {"mean": statistics.fmean(vals), "p10": sorted(vals)[n // 10], "p90": sorted(vals)[(9 * n) // 10]}
    out["idle_share"] = sum(r["idle"] for r in results) / max(1, sum(r["owned"] for r in results))
    out["runs_with_idle"] = sum(1 for r in results if r["idle"]) / n
    idle_ids = Counter(i for r in results for i in r["idle_ids"])
    owned_ids = Counter(i for r in results for i in r["owned_ids"])
    out["most_idle"] = [(cid, round(idle_ids[cid] / owned_ids[cid], 3), owned_ids[cid]) for cid, _ in idle_ids.most_common(8)]
    out["runs_with_install_choice_at_end"] = sum(1 for r in results if r["left_out"]) / n
    signatures = Counter(r["signature"] for r in results)
    out["signatures"] = {"distinct": len(signatures),
                         "entropy_bits": round(-sum(c / n * math.log2(c / n) for c in signatures.values()), 3),
                         "top": [["+".join(sig), round(c / n, 3)] for sig, c in signatures.most_common(8)]}
    # Must-install detection: of the runs that end holding a working, hostable copy, how often it is installed.
    working_ids = Counter(i for r in results for i in r["working_ids"])
    installed_when_working = Counter(i for r in results for i in r["installed_ids"] if i in r["working_ids"])
    rates = {cid: installed_when_working[cid] / working_ids[cid] for cid in working_ids if working_ids[cid] >= max(20, n // 50)}
    ranked = sorted(rates.items(), key=lambda kv: (-kv[1], kv[0]))
    out["install_rate_when_working"] = {"top": [[cid, round(v, 3)] for cid, v in ranked[:10]],
                                        "bottom": [[cid, round(v, 3)] for cid, v in ranked[-8:]],
                                        "at_least_95": sum(1 for _, v in ranked if v >= 0.95),
                                        "below_50": sum(1 for _, v in ranked if v < 0.5)}
    tag_share = Counter(t for r in results for t in r["signature"])
    out["signature_tag_share"] = {t: round(c / n, 3) for t, c in tag_share.most_common()}
    return out


def simulate(content: dict, *, runs: int, seed: int, policy: str, filtered: bool, elite_levels: list[int],
             tags: dict | None = None, weights=None, legacy: bool = False, dismantle_levels: list[int] | None = None,
             overrides: dict | None = None) -> dict:
    cat = Catalog(content, filtered=filtered, tags=tags, overrides=overrides)
    rng = random.Random(seed)
    return summarize([run_one(cat, rng, policy, elite_levels, weights, legacy=legacy, dismantle_levels=dismantle_levels)
                      for _ in range(runs)])


def legacy_tags() -> dict:
    return {**COMPONENT_TAGS, **LEGACY_OVERRIDES}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--content", type=Path, default=CONTENT)
    parser.add_argument("--runs", type=int, default=4000)
    parser.add_argument("--seed", type=int, default=20261113)
    parser.add_argument("--policy", choices=POLICIES, default="synergy")
    parser.add_argument("--unfiltered", action="store_true", help="draw without the hostable draw filter (pre-rework rules)")
    parser.add_argument("--legacy-tags", action="store_true", help="pre-rework semantics for B13-B16")
    parser.add_argument("--elite", type=str, default="", help="comma-separated levels with an elite-camp two-choice")
    parser.add_argument("--dismantle", type=str, default="", help="comma-separated levels with a rest dismantle of idle parts")
    parser.add_argument("--legacy-supply", action="store_true", help="one draw per level and no workshop capacity")
    parser.add_argument("--capacity", type=str, default="", help="BASE,LEVELS_PER_SLOT workshop capacity override")
    parser.add_argument("--bonus-levels", type=str, default=None, help="comma-separated bonus draw levels override ('' = none)")
    parser.add_argument("--affinity", type=int, default=None, help="within-category draw weight of a card listing a learned active")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args(argv)
    content = load(args.content)
    overrides: dict = {}
    if args.capacity:
        base, step = (int(x) for x in args.capacity.split(","))
        overrides.update(capacity_base=base, capacity_step=step)
    if args.bonus_levels is not None:
        overrides["bonus_levels"] = [int(x) for x in args.bonus_levels.split(",") if x]
    if args.affinity is not None:
        overrides["affinity"] = args.affinity
    elite = [int(x) for x in args.elite.split(",") if x]
    dismantle = [int(x) for x in args.dismantle.split(",") if x]
    summary = simulate(content, runs=args.runs, seed=args.seed, policy=args.policy, filtered=not args.unfiltered,
                       elite_levels=elite, tags=legacy_tags() if args.legacy_tags else None, legacy=args.legacy_supply,
                       dismantle_levels=dismantle, overrides=overrides)
    text = json.dumps(summary, ensure_ascii=False, indent=2)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
