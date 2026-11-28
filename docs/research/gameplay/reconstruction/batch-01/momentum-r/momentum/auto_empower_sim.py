"""自动强化（无 R 键）劲参数研究（L2）。只读 content.json。

规则模型（PO 第四道指令）：Q/W/E 不耗劲；劲 >= 门槛 T 时，宿主的下一次合法动作自动强化并扣 C 劲；成功释放即扣（打空也扣）；
无额外强化冷却、无泄劲。强化动作及其派生事件不产劲（防递归口径，沿用 build-integration §2.2）。
宿主处理器：拳（普攻：一次触发强化接下来 3 拳）、攻（攻类技能）、守（守类技能）、动（D 冲刺）、阵（放置类技能）。

用法：python -B auto_empower_sim.py → auto_empower_sim.json + 控制台表
"""
from __future__ import annotations

import json
import math
import random
from pathlib import Path

CONTENT = Path(__file__).resolve().parents[7] / "SourceData" / "Demo" / "content.json"
DT = 0.02
LEVEL = 10
UPTIME = 0.6          # 普攻在射程内的时间比例
HIT_P = 0.75          # 敌对技能命中率
TAU_SKILL = 4.0       # 技能转好后等出手机会的平均延迟（原生 AI 校准，见 MOMENTUM_RESEARCH §2）
TAU_DASH = 3.0        # 交战中 D 转好后平均多久用掉
SELF_ONLY = {"active_not_mad", "active_unplug", "active_brace", "active_triple_combo", "active_flurry_rush"}

# 代表构筑：(名, 技能, 急速, 攻速倍率, 宿主, 宿主技能 id 或 None, 额外)
BUILDS = {
    "fist_as": ("普攻攻速·拳宿主", ["active_triple_combo", "active_bail_out", "active_not_mad"], 0, 1.8, "basic", None, {"third_breath": 10}),
    "heavy_basic": ("慢速重击·拳宿主", ["active_calculus_drop", "active_charged_ram", "active_elbow"], 0, 0.85, "basic", None, {}),
    "skill_atk": ("技能循环·攻宿主(板砖)", ["active_zap", "active_calculus_drop", "active_hose_down"], 60, 1.0, "skill", "active_calculus_drop", {}),
    "element_atk": ("元素循环·攻宿主(电)", ["active_seethe", "active_hose_down", "active_zap"], 40, 1.0, "skill", "active_zap",
                    {"reaction": 0.5, "reaction_card": 15}),
    "shield_guard": ("护盾防反·守宿主(锅盖)", ["active_not_mad", "active_unplug", "active_brace"], 0, 1.0, "skill", "active_not_mad",
                     {"thick_skin": True}),
    "dash_d": ("冲刺机动·动宿主(D)", ["active_bail_out", "active_crotch_grab", "active_charged_ram"], 0, 1.2, "dash", None, {}),
    "zone_ward": ("工事区域·阵宿主(书包)", ["active_school_bag", "active_desk_fan", "active_quick_meeting"], 20, 1.0, "skill", "active_school_bag", {}),
    "bare_q": ("裸构筑·攻宿主(甩鞋)", ["active_elbow", "active_snark_bolt", "active_not_mad"], 0, 1.0, "skill", "active_snark_bolt", {}),
}


def v(x, d=0.0):
    return float(x.get("value", d)) if isinstance(x, dict) else (float(x) if x is not None else d)


def fight(content, build, P, start, seconds, rng):
    name, ids, H, asm, host, host_id, ex = BUILDS[build]
    ab = {a["id"]: a for a in content["abilities"]}
    sk = [{"id": i, "cd": max(0.3, v(ab[i]["cooldown"]) * 100 / (100 + H)), "ready": 0.0, "opp": None,
           "hostile": i not in SELF_ONLY} for i in ids]
    AS = 0.85 * (1 + 0.02 * (LEVEL - 1)) * asm
    itv = 1 / AS
    m, t, nb, dash_ready, dash_opp, thick = start, 0.0, 0.0, 0.0, None, 0.0
    fist_left = 0
    st = dict(emp=0, first=None, host_actions=0, wasted=0.0, area=0.0, income=0.0, pve=0)

    def gain(x):
        nonlocal m
        if x <= 0:
            return
        st["income"] += x
        room = 100 - m
        if x > room:
            st["wasted"] += x - room
        m = min(100.0, m + x)

    def try_empower():
        nonlocal m
        if m >= P["T"]:
            m -= P["C"]
            st["emp"] += 1
            if st["first"] is None:
                st["first"] = t
            return True
        return False

    while t < seconds:
        gain(P.get("passive", 5.0) * DT)
        gain(100 * P.get("hp_loss", 0.12) * DT * P.get("hurt_gain", 0.0))
        if ex.get("thick_skin") and t >= thick:
            gain(10.0); thick = t + 5.0
        # 技能
        for s in sk:
            if t < s["ready"]:
                continue
            if s["opp"] is None:
                s["opp"] = t + (rng.expovariate(1 / TAU_SKILL) if TAU_SKILL > 0 else 0.0)
            if t < s["opp"]:
                continue
            s["ready"], s["opp"] = t + s["cd"], None
            emp = False
            if host == "skill" and s["id"] == host_id:
                st["host_actions"] += 1
                emp = try_empower()
            if s["hostile"] and rng.random() < HIT_P and not emp:
                gain(5.0)
                if ex.get("reaction") and rng.random() < ex["reaction"]:
                    gain(P.get("reaction", 10.0) + ex.get("reaction_card", 0.0))
            break
        # D
        if dash_opp is None and t >= dash_ready:
            dash_opp = t + rng.expovariate(1 / TAU_DASH)
        if dash_opp is not None and t >= dash_opp:
            dash_ready, dash_opp = t + 9.0, None
            if host == "dash":
                st["host_actions"] += 1
                try_empower()
        # 普攻
        if t >= nb:
            nb = t + itv
            if rng.random() < UPTIME:
                emp = False
                if host == "basic":
                    st["host_actions"] += 1
                    if fist_left > 0:
                        fist_left -= 1; emp = True
                    elif try_empower():
                        fist_left = 2; emp = True
                if not emp:
                    gain(10 * itv / 1.334)
                    if ex.get("third_breath") and rng.random() < 1 / 3:
                        gain(ex["third_breath"])
        st["area"] += m * DT
        t += DT
    return st


def short(content, build, P, start_fn, n=250, seed=3):
    mu, sg = math.log(4.8), math.sqrt(2 * math.log(5.4 / 4.8))
    rng = random.Random(seed)
    emp = first = got = 0
    firsts = []
    for i in range(n):
        d = rng.lognormvariate(mu, sg)
        st = fight(content, build, P, start_fn(rng), d, random.Random(seed * 11 + i))
        emp += st["emp"]
        if st["first"] is not None:
            got += 1; firsts.append(st["first"])
    firsts.sort()
    return {"emp_per_fight": round(emp / n, 2), "pct_fights_with_emp": round(100 * got / n, 1),
            "first_median": round(firsts[len(firsts) // 2], 2) if firsts else None}


def sustained(content, build, P, start, secs=30.0, n=40):
    rows = [fight(content, build, P, start, secs, random.Random(500 + i)) for i in range(n)]
    emp = sum(r["emp"] for r in rows) / n
    ha = sum(r["host_actions"] for r in rows) / n
    return {"emp_interval_s": round(secs / emp, 2) if emp else None,
            "host_emp_share_pct": round(100 * emp * (3 if BUILDS[build][4] == "basic" else 1) / ha, 1) if ha else None,
            "mean_m": round(sum(r["area"] for r in rows) / n / secs, 1),
            "wasted_per_s": round(sum(r["wasted"] for r in rows) / n / secs, 2),
            "income_per_s": round(sum(r["income"] for r in rows) / n / secs, 2)}


def carry(entry_pve, delay, rate, base, travel_mean=6.0, n=20000, seed=9):
    """打野后赶路进入 PvP 的劲：从 entry_pve 起，脱战 delay 秒后以 rate/s 回落到 base。赶路时间 ~ 指数分布。"""
    rng = random.Random(seed)
    out = []
    for _ in range(n):
        tt = rng.expovariate(1 / travel_mean)
        out.append(max(base, entry_pve - rate * max(0.0, tt - delay)) if entry_pve > base else entry_pve)
    return out


def main():
    content = json.loads(CONTENT.read_text(encoding="utf-8"))
    res = {"grid": {}, "carry": {}}
    grid = [(T, C, S) for T in (50, 60, 75) for C in (30, 40, 50, 60) if C <= T for S in (30, 50)]
    for T, C, S in grid:
        P = {"T": T, "C": C}
        key = f"T{T}_C{C}_S{S}"
        res["grid"][key] = {b: {**short(content, b, P, lambda r, s=S: float(s)), **sustained(content, b, P, float(S))} for b in BUILDS}
        g = res["grid"][key]
        iv = [g[b]["emp_interval_s"] or 99 for b in BUILDS]
        print(key, "emp/fight", [g[b]["emp_per_fight"] for b in BUILDS], "first", [g[b]["first_median"] for b in BUILDS],
              "interval", iv, "hostshare", [g[b]["host_emp_share_pct"] for b in BUILDS], "waste", [g[b]["wasted_per_s"] for b in BUILDS])
    # 受击回劲变体
    for hg in (0.3, 0.6):
        P = {"T": 60, "C": 40, "hurt_gain": hg}
        key = f"T60_C40_S30_hurt{hg}"
        res["grid"][key] = {b: {**short(content, b, P, lambda r: 30.0), **sustained(content, b, P, 30.0)} for b in BUILDS}
        g = res["grid"][key]
        print(key, "emp/fight", [g[b]["emp_per_fight"] for b in BUILDS], "interval", [g[b]["emp_interval_s"] for b in BUILDS])
    for pve in (70, 90, 100):
        for name, (d, r, b) in {"现行(5s,10/s→50)": (5, 10, 50), "慢落(5s,5/s→30)": (5, 5, 30), "快落(3s,10/s→30)": (3, 10, 30),
                                 "保温(8s,5/s→30)": (8, 5, 30)}.items():
            xs = carry(pve, d, r, b)
            res["carry"][f"pve{pve}_{name}"] = {"ge60_pct": round(100 * sum(x >= 60 for x in xs) / len(xs), 1),
                                               "ge75_pct": round(100 * sum(x >= 75 for x in xs) / len(xs), 1),
                                               "mean": round(sum(xs) / len(xs), 1)}
    for k, x in res["carry"].items():
        print("carry", k, x)
    Path(__file__).with_name("auto_empower_sim.json").write_text(json.dumps(res, ensure_ascii=False, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
