"""R architecture fairness calculator (L1 arithmetic on shipped numbers; read-only).

For representative L10 builds (30 attribute points, no cards, skill ranks 2/2/1) it estimates what one R
activation is worth under each candidate R architecture, using the same nominal strength, so the
spread across builds shows which architectures are structurally biased toward attack frequency,
skill size or cast count. Not a balance proposal; every coefficient below is an illustrative probe.

Usage: python r_value_model.py <repo_root> [--json out.json]
"""
import json
import sys
from pathlib import Path


def v(x, d=0.0):
    return x.get("value", d) if isinstance(x, dict) else (d if x is None else x)


BUILDS = {
    "攻速": ({"agility": 18, "strength": 12}, ["active_triple_combo", "active_bail_out", "active_elbow"], 0.85),
    "重击": ({"strength": 18, "physique": 12}, ["active_calculus_drop", "active_crotch_grab", "active_elbow"], 0.7),
    "技能循环": ({"brains": 18, "physique": 12}, ["active_calculus_drop", "active_quick_meeting", "active_seethe"], 0.45),
    "元素": ({"brains": 18, "agility": 12}, ["active_hose_down", "active_zap", "active_cola_shake"], 0.35),
    "控制": ({"strength": 18, "thick_skin": 12}, ["active_pants_down", "active_cola_shake", "active_elbow"], 0.7),
    "护盾防反": ({"physique": 18, "thick_skin": 12}, ["active_not_mad", "active_unplug", "active_open_mic"], 0.8),
    "冲刺": ({"agility": 18, "strength": 12}, ["active_bail_out", "active_charged_ram", "active_crotch_grab"], 0.8),
    "工事": ({"brains": 18, "physique": 12}, ["active_school_bag", "active_desk_fan", "active_flyer_trap"], 0.3),
}
RANKS = [2, 2, 1]
L = 10
WINDOW = 4.0
TARGET_EHP = (860 + 125 * (L - 1)) * (100 + 18 + 2 * (L - 1)) / 100  # L10 target, no defensive points


def stats(pts):
    k = L - 1
    ad_base = 70 + 3.2 * k
    s = {
        "ad": ad_base + 3.5 * pts.get("strength", 0),
        "ap": 6 * pts.get("brains", 0),
        "haste": pts.get("brains", 0),
        "as": min(5.0, 0.85 * (1 + 0.02 * k + 0.02 * pts.get("agility", 0))),
        "max_hp": 860 + 125 * k + 70 * pts.get("physique", 0),
        "resist": 18 + 2 * k + 3.5 * pts.get("thick_skin", 0),
        "G": 1 + 0.04 * k,
        "dash_cd": 9.0 * (0.6 if pts.get("agility", 0) >= 18 else 1.0),
    }
    s["bonus_ad"] = s["ad"] - ad_base
    s["bonus_hp"] = 70 * pts.get("physique", 0)
    return s


def skill_value(a, rank, s):
    """Raw damage (or damage-equivalent shield) of one cast, before resistances."""
    bs = [1, 1.35, 1.7][rank - 1]
    rs = [1, 1.25, 1.5][rank - 1]
    total = 0.0
    for e in a["effects"]:
        op = e["op"]
        if op in ("damage", "zone_damage", "frenzy_waves", "bag", "contact_trap", "parry_stance", "brace", "empower_basic"):
            amt = v(e.get("base")) * s["G"] * bs + v(e.get("ad_ratio")) * rs * s["ad"] + v(e.get("ap_ratio")) * rs * s["ap"]
            amt += v(e.get("max_hp_ratio")) * s["bonus_hp"]
            if op == "zone_damage":
                amt *= v(a["effects"][0].get("count"), 3)
            if op == "empower_basic":
                amt *= v(e.get("charges"), 1)
            total += amt
        elif op == "shield":
            total += 0.8 * (v(e.get("base")) * s["G"] * bs + v(e.get("ad_ratio")) * s["ad"] + v(e.get("ap_ratio")) * s["ap"]
                            + v(e.get("max_hp_ratio")) * s["max_hp"])
        elif op == "fan":
            total += 60.0  # displacement only; nominal value
    if a["id"] == "active_zap":
        total *= 1 + 0.7 + 0.49
    return total


def main():
    root = Path(sys.argv[1])
    content = json.loads((root / "SourceData/Demo/content.json").read_text(encoding="utf-8"))
    ab = {a["id"]: a for a in content["abilities"]}
    rows = {}
    for name, (pts, skills, engage) in BUILDS.items():
        s = stats(pts)
        basic = s["ad"] * (0.9 + 0.9 + 1.3) / 3
        hits = WINDOW * s["as"] * engage
        vals = []
        casts = 0.0
        for sid, rk in zip(skills, RANKS):
            a = ab[sid]
            cd = max(0.3, v(a["cooldown"]) * 100 / (100 + s["haste"]))
            n = min(1.0, WINDOW / cd)
            casts += n
            vals.append(skill_value(a, rk, s))
        window_dmg = hits * basic + sum(x * min(1.0, WINDOW / max(0.3, v(ab[i]["cooldown"]) * 100 / (100 + s["haste"])))
                                         for x, i in zip(vals, skills))
        best = max(vals)
        last = vals[0]
        r = {
            "basic_hits_4s": round(hits, 2), "casts_4s": round(casts, 2), "dashes_4s": round(WINDOW / s["dash_cd"], 2),
            "window_raw": round(window_dmg), "best_skill_raw": round(best),
            # Probes, each with the same nominal strength:
            "R1_next_skill_+50%": 0.5 * best,                     # next Q/W/E +50 %
            "R5_echo_last_70%": 0.7 * last,                       # replay the last cast at 70 %
            "R3_state_all_dmg_+30%": 0.3 * window_dmg,            # 4 s state, all damage +30 %
            "R3_state_per_hit_+30": 30.0 * (hits + casts),        # 4 s state, +30 flat per hit event
            "R2_fixed_ult_250": 250.0,                            # one build-independent ultimate
            "R4_port_budget_200": 200.0,                          # fuse whose payload is authored per port to a budget
        }
        for k in list(r):
            if k.startswith("R"):
                r[k + "_ehp%"] = round(100 * r[k] / TARGET_EHP, 1)
                r[k] = round(r[k])
        rows[name] = r
    probes = [k for k in next(iter(rows.values())) if k.startswith("R") and not k.endswith("_ehp%")]
    spread = {}
    for p in probes:
        xs = [rows[b][p] for b in rows]
        spread[p] = round(max(xs) / max(1, min(xs)), 2)
    out = {"target_ehp": round(TARGET_EHP), "rows": rows, "spread_max_over_min": spread}
    text = json.dumps(out, ensure_ascii=False, indent=1)
    if "--json" in sys.argv:
        Path(sys.argv[sys.argv.index("--json") + 1]).write_text(text, encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
