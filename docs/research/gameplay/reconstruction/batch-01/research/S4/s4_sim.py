"""S4 隔离研究脚本：冲刺/精准闪避几何、劲经济与爆劲价值、冲刺节奏（L1 数学 + L2 离线模拟）。

只读 SourceData\\Demo\\content.json（基线 e62ff3047），不改任何正式文件。
用法：python -B s4_sim.py [--content <content.json>] [--out s4_results.json]
所有“方案参数”都是为了比较机制而取的示意值，不是数值提案。
"""
from __future__ import annotations

import argparse
import json
import math
import random
from pathlib import Path

DEFAULT_CONTENT = Path(__file__).resolve().parents[7] / "SourceData" / "Demo" / "content.json"
BODY_R = 40.0          # UNIT.CollisionRadiusCm
DASH_D, DASH_T = 450.0, 0.18
PD_WARN_W = 0.30       # MOMENTUM.PerfectDodgeWindowSec
PD_GRAZE_W = 0.12      # MOMENTUM.DashDodgeWindowSec
RT_MU, RT_SD = 0.25, 0.05   # 假设：看到预警到按键的反应时间（真人，未实测）
SPEEDS = (240.0, 270.0, 300.0)


def v(x, default=0.0):
    if isinstance(x, dict):
        return float(x.get("value", default))
    return float(x) if x is not None else default


def norm_cdf(x, mu=RT_MU, sd=RT_SD):
    return 0.5 * (1 + math.erf((x - mu) / (sd * math.sqrt(2))))


def dash_exit_time(d, D=DASH_D, T=DASH_T):
    """EaseOutCubic x(t)=D(1-(1-t/T)^3) 走出 d 厘米所需时间；d>=D 返回 None。"""
    if d >= D:
        return None
    return T * (1 - (1 - d / D) ** (1 / 3))


# ---------------------------------------------------------------- 1. 威胁几何
def threat_table(content):
    rows = []
    for a in content["abilities"]:
        t = a["target"]
        mode = t["mode"]
        wind = v(a["windup"])
        eff = {e["op"]: e for e in a["effects"]}
        row = None
        if "ground_impact" in eff:
            g = eff["ground_impact"]
            row = ("圆形落点", v(g["radius"]), wind + v(g["delay"]), "中心")
        elif "contact_trap" in eff:
            g = eff["contact_trap"]
            row = ("陷阱引信", v(g["radius"]), v(g["delay"]), "触发点")
        elif "zone" in eff:
            g = eff["zone"]
            row = ("持续区(首跳)", v(g["radius"]), wind + v(g.get("delay")), "中心")
        elif "self_area" in eff:
            # 典型受害者站在施法者普攻距离附近（200cm），最坏在 80cm 贴身
            r = v(eff["self_area"]["radius"])
            row = ("自身范围", r - 200.0, wind, "距施法者200cm")
        elif mode == "DirectionLine":
            row = ("直线", v(t["half_width"]), wind, "线中")
        elif mode == "DirectionProjectile":
            rng = v(t["range"]); spd = v(t["projectile_speed"])
            row = ("弹道@半程", v(t["projectile_radius"]) + BODY_R, wind + rng / 2 / spd, "弹道中线")
        elif mode == "DirectionCone":
            row = ("扇形", None, wind, "—")
        elif mode == "Unit":
            row = ("单位锁定", None, wind, "—")
        if not row:
            continue
        kind, need, tele, where = row
        out = {"id": a["id"], "kind": kind, "need_cm": need, "telegraph_s": round(tele, 3), "where": where,
               "windup_s": wind, "warning_pd_eligible": wind >= 0.15}
        if need is not None and need > 0:
            for s in SPEEDS:
                out[f"walk_margin_{int(s)}"] = round(tele - RT_MU - need / s, 3)
            ex = dash_exit_time(need)
            out["dash_exit_s"] = None if ex is None else round(ex, 3)
            out["dash_margin"] = None if ex is None else round(tele - RT_MU - ex, 3)
            # 预警精准闪避：按键须落在 [结算-0.3, 结算-走出时间] 内，且不早于反应时间
            if ex is not None and out["warning_pd_eligible"]:
                lo, hi = max(RT_MU, tele - PD_WARN_W), tele - ex
                out["warn_pd_window_s"] = round(max(0.0, hi - lo), 3)
                out["warn_pd_needs_wait_s"] = round(max(0.0, tele - PD_WARN_W - RT_MU), 3)
        else:
            # 只能靠原生擦身闪避（命中前 0.12s 内起冲刺）：P(反应落在 [命中-0.12, 命中])
            out["graze_p_react"] = round(norm_cdf(tele) - norm_cdf(tele - PD_GRAZE_W), 3)
            out["graze_p_react_w018"] = round(norm_cdf(tele) - norm_cdf(tele - 0.18), 3)
        rows.append(out)
    # 普攻（基础攻速 0.85 时前摇 0.18s）
    rows.append({"id": "basic_attack", "kind": "普攻", "need_cm": None, "telegraph_s": 0.18, "where": "—",
                 "windup_s": 0.18, "warning_pd_eligible": False,
                 "graze_p_react": round(norm_cdf(0.18) - norm_cdf(0.06), 3),
                 "graze_p_react_w018": round(norm_cdf(0.18) - norm_cdf(0.0), 3)})
    return rows


# ---------------------------------------------------------------- 2. 劲经济与爆劲
BUILDS = {
    # 名称: (技能, 冷却急速 H, 攻速倍率, 说明)
    "bare_L10": (["active_elbow", "active_snark_bolt", "active_not_mad"], 0, 1.0, "L10 裸构筑"),
    "caster_H40": (["active_zap", "active_calculus_drop", "active_seethe"], 40, 1.0, "法术循环 H40"),
    "skill_engine_H100": (["active_zap", "active_calculus_drop", "active_hose_down"], 100, 1.0, "技能永动 H100"),
    "attack_speed": (["active_triple_combo", "active_bail_out", "active_not_mad"], 0, 1.8, "攻速流"),
}
LEVEL = 10
AD = 70 + 3.2 * (LEVEL - 1)
AP = 60.0


def skill_value(ab):
    """一次施放的原始数值（伤害或护盾当量），L10 一级技能，忽略抗性；只用于同一构筑内相对比较。"""
    g = 1 + 0.04 * (LEVEL - 1)
    total = 0.0
    for e in ab["effects"]:
        op = e["op"]
        if op in ("damage", "zone_damage"):
            mult = 3.0 if op == "zone_damage" else 1.0
            ratio = v(e.get("ad_ratio")) * AD + v(e.get("ap_ratio")) * AP + v(e.get("adaptive_ratio")) * max(AD - 70, AP)
            total += mult * (v(e.get("base")) * g + ratio)
        elif op in ("shield",):
            total += v(e.get("base")) * g + v(e.get("ad_ratio")) * AD + v(e.get("ap_ratio")) * AP
        elif op == "empower_basic":
            ch = v(e.get("charges"), 1) or 1
            total += ch * (v(e.get("base")) * g + v(e.get("ad_ratio")) * AD + v(e.get("ap_ratio")) * AP)
    if "chain_bounce" in {e["op"] for e in ab["effects"]}:
        total *= 1.0  # 1v1 不弹射
    return total


def run_econ(content, build, policy, seconds=60.0, dt=0.01, seed=7):
    """单人对假人持续交战。policy:
    none      永不爆劲
    hold      现行：劲>=60 且技能就绪时按住 0.35s 后放爆劲（普攻不中断），总付 max(60, 技能劲耗)
    instant   修饰键即时：同上但无 0.35s
    overflow  只在“下一次技能前劲会溢出 100”时用即时爆劲（理性玩家）
    state     独立爆劲：60 劲开 4s 状态，期间每次技能 ×1.35，不再额外收费（示意）
    bank      停手攒劲：最大伤害技能只以即时爆劲施放，其余技能只在不妨碍攒劲时施放
    bank_hold 同 bank，但爆劲走 0.35s 长按
    """
    rng = random.Random(seed)
    abs_ = {a["id"]: a for a in content["abilities"]}
    ids, H, as_mult, _ = BUILDS[build]
    skills = [abs_[i] for i in ids]
    cds = [max(0.3, v(s["cooldown"]) * 100 / (100 + H)) for s in skills]
    costs = [v(s["momentum_cost"]) for s in skills]
    vals = [skill_value(s) for s in skills]
    winds = [v(s["windup"]) for s in skills]
    AS = 0.85 * (1 + 0.02 * (LEVEL - 1)) * as_mult
    interval = 1.0 / AS
    basic_gain = 10 * interval / 1.334
    basic_dmg_avg = AD * (0.9 + 0.9 + 1.3) / 3
    mom, t = 50.0, 0.0
    ready = [0.0] * len(skills)
    next_basic = 0.0
    busy_until = 0.0
    hold_until, hold_slot = -1.0, -1
    state_until = -1.0
    dmg_basic = dmg_skill = 0.0
    casts = ods = 0
    wasted = 0.0
    at_cap = 0.0
    mom_area = 0.0
    events = []

    def gain(x):
        nonlocal mom, wasted
        room = 100 - mom
        if x > room:
            wasted += x - room
        mom = min(100.0, mom + x)

    def cast(i, od):
        nonlocal mom, dmg_skill, casts, ods, busy_until
        pay = max(60.0, costs[i]) if od == "pay" else costs[i]
        mom -= pay
        mult = 1.35 if od in ("pay", "state") else 1.0
        dmg_skill += vals[i] * mult
        events.append((t + winds[i], vals[i] * mult))
        casts += 1
        ods += od == "pay"
        ready[i] = t + cds[i]
        busy_until = t + winds[i]
        gain(5.0)

    while t < seconds:
        gain(5.0 * dt)
        mom_area += mom * dt
        if mom >= 100 - 1e-9:
            at_cap += dt
        # 爆劲蓄力完成
        if hold_slot >= 0 and t >= hold_until:
            i = hold_slot; hold_slot = -1
            if mom >= max(60.0, costs[i]):
                cast(i, "pay")
            elif mom >= costs[i]:
                cast(i, "none")
        if hold_slot < 0 and t >= busy_until:
            for i in range(len(skills)):
                if t < ready[i] or mom < costs[i]:
                    continue
                in_state = t < state_until
                if policy == "state" and not in_state and mom >= 60 + costs[i]:
                    mom -= 60.0; state_until = t + 4.0; ods += 1; in_state = True
                if policy in ("bank", "bank_hold"):
                    big = max(range(len(skills)), key=lambda j: vals[j])
                    if i == big:
                        if mom < max(60.0, costs[i]):
                            continue
                        if policy == "bank_hold":
                            hold_slot, hold_until = i, t + 0.35
                            ready[i] = t + 0.35 + 1e-6
                        else:
                            cast(i, "pay")
                        break
                    if mom - costs[i] < max(60.0, costs[big]):
                        continue
                    cast(i, "none")
                    break
                if policy == "hold" and mom >= max(60.0, costs[i]):
                    hold_slot, hold_until = i, t + 0.35
                    ready[i] = t + 0.35 + 1e-6  # 占住这个键
                elif policy == "instant" and mom >= max(60.0, costs[i]):
                    cast(i, "pay")
                elif policy == "overflow":
                    nxt = min([r for j, r in enumerate(ready) if j != i] + [t + cds[i]])
                    income = (5.0 + basic_gain / interval) * max(0.0, nxt - t) + 5.0
                    would_waste = mom - costs[i] + income > 100
                    cast(i, "pay" if would_waste and mom >= max(60.0, costs[i]) else "none")
                else:
                    cast(i, "state" if in_state else "none")
                break
        if t >= next_basic and t >= busy_until:
            dmg_basic += basic_dmg_avg
            events.append((t, basic_dmg_avg))
            gain(basic_gain)
            next_basic = t + interval
        t += dt
    total = dmg_basic + dmg_skill
    events.sort()
    burst, j, acc = 0.0, 0, 0.0
    for te, d in events:
        acc += d
        while events[j][0] < te - 2.0:
            acc -= events[j][1]
            j += 1
        burst = max(burst, acc)
    return {"build": build, "policy": policy, "dps": round(total / seconds, 1), "skill_dps": round(dmg_skill / seconds, 1),
            "burst_2s": round(burst, 1), "avg_momentum": round(mom_area / seconds, 1), "casts": casts, "overdrives": ods, "wasted_momentum": round(wasted, 1),
            "time_at_cap_pct": round(100 * at_cap / seconds, 1),
            "spend_rate_on_cd": round(sum(c / d for c, d in zip(costs, cds)), 1),
            "income_rate": round(5.0 + basic_gain / interval, 1)}


def econ_table(content):
    out = []
    for b in BUILDS:
        base = None
        for p in ("none", "hold", "instant", "overflow", "state", "bank", "bank_hold"):
            r = run_econ(content, b, p)
            if p == "none":
                base = r["dps"]
            r["dps_vs_none_pct"] = round(100 * (r["dps"] / base - 1), 1)
            out.append(r)
    return out


# ---------------------------------------------------------------- 3. 冲刺节奏 vs 交战时长
def dash_cadence(seed=11, n=20000):
    """EXP-03 野外命 TTK 均值 5.4s、中位 4.8s → 对数正态 sigma≈0.485。
    所有方案共用同一套“跑图占用”：进战前 60s 跑图，跑图想冲刺的需求按泊松到达（强度 r），
    按各方案的使用策略消耗位移；进战后只为威胁使用。威胁 1 在交战 0.5s，威胁 2 在交战中点（假设）。
    r=1/9 时现行方案进战可用约 50%（原 v1 假设）；r=1/3 近似真人“冷却一好就冲”（90 次/776s，L5 n=1）。"""
    mu, sigma = math.log(4.8), math.sqrt(2 * math.log(5.4 / 4.8))
    rng = random.Random(seed)
    WARM = 60.0

    class Track:
        def __init__(self, charges, cd):
            self.max, self.cd, self.c, self.done = charges, cd, charges, None

        def advance(self, t):
            while self.done is not None and self.done <= t:
                self.c += 1
                self.done = self.done + self.cd if self.c < self.max else None

        def use(self, t):
            self.advance(t)
            if self.c <= 0:
                return False
            self.c -= 1
            if self.done is None:
                self.done = t + self.cd
            return True

    # (轨道列表, 跑图用哪条轨道, 跑图至少要剩几格才用, 威胁时按什么顺序用)
    options = {
        "现行 1 次·9s": ([(1, 9.0)], 0, 1, [0]),
        "1 次·7.5s": ([(1, 7.5)], 0, 1, [0]),
        "身手关键石 1 次·5.4s": ([(1, 5.4)], 0, 1, [0]),
        "B 2 充能·各 6.5s·跑图有格就用": ([(2, 6.5)], 0, 1, [0]),
        "B 2 充能·各 6.5s·跑图只用满格（留一格）": ([(2, 6.5)], 0, 2, [0]),
        "C 闪身 4s(D) + 疾冲 9s(F)·跑图只用 F": ([(1, 4.0), (1, 9.0)], 1, 1, [0, 1]),
    }
    res = {}
    for r_name, r in (("跑图需求 1/9s", 1 / 9), ("跑图需求 1/3s（近似真人）", 1 / 3)):
        block = {}
        for name, (tracks, travel_i, keep, order) in options.items():
            first = second = 0
            for _ in range(n):
                ts = [Track(c, cd) for c, cd in tracks]
                t = 0.0
                while True:
                    t += rng.expovariate(r)
                    if t >= WARM:
                        break
                    tr = ts[travel_i]
                    tr.advance(t)
                    if tr.c >= keep:
                        tr.use(t)
                ttk = rng.lognormvariate(mu, sigma)
                t1, t2 = WARM + 0.5, WARM + 0.5 + ttk * 0.5
                first += any(ts[i].use(t1) for i in order)
                second += any(ts[i].use(t2) for i in order)
            block[name] = {"p_first_threat": round(first / n, 3), "p_second_threat": round(second / n, 3)}
        res[r_name] = block
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--content", default=str(DEFAULT_CONTENT))
    ap.add_argument("--out", default=str(Path(__file__).with_name("s4_results.json")))
    args = ap.parse_args()
    content = json.loads(Path(args.content).read_text(encoding="utf-8"))
    res = {"assumptions": {"reaction_time": [RT_MU, RT_SD], "body_radius": BODY_R, "level": LEVEL, "AP": AP},
           "threats": threat_table(content), "economy": econ_table(content), "cadence": dash_cadence()}
    Path(args.out).write_text(json.dumps(res, ensure_ascii=False, indent=1), encoding="utf-8")
    for k in ("threats", "economy"):
        print(f"== {k}")
        for r in res[k]:
            print(r)
    print("== cadence")
    for k2, r in res["cadence"].items():
        print(k2, r)


if __name__ == "__main__":
    main()
