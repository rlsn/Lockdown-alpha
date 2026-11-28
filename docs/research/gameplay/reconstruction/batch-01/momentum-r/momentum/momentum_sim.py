"""劲资源架构离线模拟（L2）。只读 SourceData\\Demo\\content.json，不改任何正式文件。

用法：python -B momentum_sim.py [--content <content.json>]  → momentum_sim.json + 控制台表格

模型（全部为假设，需原生复测校准；原生参照见 native_momentum.json）：
- 单人视角，L10，一场“持续交战”12 s（含第三方纠缠）与原生分布的短交战（TTK 对数正态，中位 4.8 s）。
- 普攻：在射程内的时间比例 BASIC_UPTIME，命中即按现行规则回劲（每秒约 7.5，与攻速无关）。
- 技能：冷却好且劲够就按优先级施放；敌对技能以 SKILL_HIT_P 概率命中 1 个参赛者；自身增益类不计命中回劲。
- 反应：元素构筑每次技能命中以 rate 概率触发反应。
- 承伤：交战中按 HP_LOSS_PER_S 均匀掉血（只在 M5 产生劲）。
- “R” 是抽象占位：只研究它的可用性与对普通技能的挤占，不研究伤害。
"""
from __future__ import annotations

import argparse
import json
import math
import random
from pathlib import Path

DEFAULT_CONTENT = Path(__file__).resolve().parents[7] / "SourceData" / "Demo" / "content.json"
DT = 0.02
LEVEL = 10
BASIC_UPTIME = 0.6
SKILL_HIT_P = 0.75
HP_LOSS_PER_S = 0.12      # 每秒损失 12% 生命（约 8 s 被打空），示意
CAST_TAU = 0.0            # 技能转好后等到“有合适目标/角度”的平均延迟（指数分布）；0 = 一好就放（饱和上限）
SELF_ONLY = {"active_not_mad", "active_unplug", "active_brace", "active_triple_combo", "active_flurry_rush"}

ARCH = {
    # id: (中文名, 技能, 冷却急速 H, 攻速倍率, 额外规则)
    "bare": ("裸构筑", ["active_elbow", "active_snark_bolt", "active_not_mad"], 0, 1.0, {}),
    "attack_speed": ("普攻攻速", ["active_triple_combo", "active_bail_out", "active_not_mad"], 0, 1.8,
                     {"third_breath": 10}),
    "heavy": ("慢速重击", ["active_calculus_drop", "active_charged_ram", "active_elbow"], 0, 1.0,
              {"cd_mult": {"active_calculus_drop": 1.5}}),
    "skill_engine": ("高频技能", ["active_zap", "active_calculus_drop", "active_hose_down"], 100, 1.0,
                     {"cd_mult": {"active_zap": 0.65}, "cost_mult": {"active_zap": 1.5}, "brains": True,
                      "reaction": 0.2}),
    "element": ("元素循环", ["active_seethe", "active_hose_down", "active_zap"], 40, 1.0,
                {"reaction": 0.5, "reaction_card": 15, "brains": True}),
    "control": ("控制", ["active_pants_down", "active_cola_shake", "active_stare_down"], 20, 1.0, {}),
    "shield": ("护盾防反", ["active_not_mad", "active_unplug", "active_brace"], 0, 1.0, {"thick_skin": True}),
    "dash": ("冲刺机动", ["active_bail_out", "active_crotch_grab", "active_charged_ram"], 0, 1.2,
             {"paid_dash": 15, "dash_cd": 3.6, "bump_reset_p": 0.4}),
    "zone": ("召唤与区域", ["active_school_bag", "active_desk_fan", "active_quick_meeting"], 20, 1.0, {}),
    "hybrid": ("文武混合", ["active_elbow", "active_zap", "active_triple_combo"], 20, 1.0, {"martial_steal": 8}),
}


def v(x, d=0.0):
    return float(x.get("value", d)) if isinstance(x, dict) else (float(x) if x is not None else d)


class Fighter:
    def __init__(self, content, arch, rules, start, rng):
        self.rng = rng
        self.name, ids, self.H, asm, self.ex = ARCH[arch]
        ab = {a["id"]: a for a in content["abilities"]}
        self.skills = []
        for i in ids:
            a = ab[i]
            cd = v(a["cooldown"]) * self.ex.get("cd_mult", {}).get(i, 1.0)
            cd = max(0.3, cd * 100 / (100 + self.H))
            cost = v(a["momentum_cost"]) * self.ex.get("cost_mult", {}).get(i, 1.0) * rules.get("skill_cost_mult", 1.0)
            if rules.get("skill_cost_self_free") and i in SELF_ONLY:
                cost = 0.0
            if rules.get("haste_tax"):
                # 急速税：按基础冷却施放免费；冷却急速省下的那部分节拍才要付劲（H=100 付原价，H=40 付约 57%）
                cost = v(a["momentum_cost"]) * self.ex.get("cost_mult", {}).get(i, 1.0) * min(1.0, 2 * self.H / (100 + self.H))
            self.skills.append({"id": i, "cd": cd, "cost": cost, "hostile": i not in SELF_ONLY, "ready": 0.0, "opp": None})
        self.AS = 0.85 * (1 + 0.02 * (LEVEL - 1)) * asm
        self.interval = 1 / self.AS
        self.rules = rules
        self.m = start
        self.t = 0.0
        self.next_basic = 0.0
        self.basic_n = 0
        self.dash_ready = 0.0
        self.thick_ready = 0.0
        self.steal_armed = False
        self.stats = dict(casts=0, mom_wait=0.0, cd_wait=0.0, wasted=0.0, area=0.0, ge60=0.0, full=0.0, r=0, r_first=None,
                          income=0.0, hot=0.0, cold=0.0)

    # ---- 收入（按架构的收入规则）
    def gain(self, x, kind):
        r = self.rules
        if kind == "passive" and not r.get("passive", True):
            return
        if kind == "hurt":
            x *= r.get("hurt_gain", 0.0)
        if kind in ("basic", "skill", "reaction", "card") and r.get("action_scale"):
            x *= r["action_scale"]
        if x <= 0:
            return
        self.stats["income"] += x
        room = r.get("max", 100.0) - self.m
        if x > room:
            self.stats["wasted"] += x - room
        self.m = min(r.get("max", 100.0), self.m + x)

    def step(self):
        r, s, t = self.rules, self.stats, self.t
        brains_regen = 1.08 if self.ex.get("brains") else 0.0
        self.gain((5.0 + brains_regen) * DT, "passive")
        self.gain(100 * HP_LOSS_PER_S * DT, "hurt")
        if r.get("drain"):
            self.m = max(0.0, self.m - r["drain"] * DT)
        # 冲刺卡
        if "paid_dash" in self.ex and t >= self.dash_ready and self.m >= self.ex["paid_dash"] + 25:
            self.m -= self.ex["paid_dash"]
            self.dash_ready = t + self.ex["dash_cd"]
            if self.rng.random() < self.ex["bump_reset_p"]:
                self.gain(self.ex["paid_dash"], "card")
                self.dash_ready = t + 0.5
        # 厚脸皮关键石：受击触发 +10，5 s 一次
        if self.ex.get("thick_skin") and t >= self.thick_ready:
            self.gain(10.0, "card")
            self.thick_ready = t + 5.0
        # R（抽象）
        rc = r.get("r_cost")
        if rc is not None and self.m >= rc and t >= getattr(self, "r_lock", 0.0):
            if r.get("r_policy") == "greedy" or self._r_wont_starve(rc):
                self.m -= rc if not r.get("r_spend_all") else self.m
                s["r"] += 1
                if s["r_first"] is None:
                    s["r_first"] = t
                self.r_lock = t + r.get("r_cd", 0.0)
        # 技能
        any_ready_short = any_cd = False
        for sk in self.skills:
            if t < sk["ready"]:
                any_cd = True
                continue
            if sk["opp"] is None:
                sk["opp"] = t + (self.rng.expovariate(1 / CAST_TAU) if CAST_TAU > 0 else 0.0)
            if t < sk["opp"]:
                continue
            if self.m < sk["cost"]:
                any_ready_short = True
                continue
            self.m -= sk["cost"]
            sk["ready"] = t + sk["cd"]
            sk["opp"] = None
            s["casts"] += 1
            if sk["hostile"] and self.rng.random() < SKILL_HIT_P:
                self.gain(5.0 * r.get("skill_hit_scale", 1.0) * (sk["cd"] / 5.0 if r.get("tempo_norm") else 1.0), "skill")
                if self.ex.get("martial_steal"):
                    self.steal_armed = True
                rp = self.ex.get("reaction", 0.0)
                if rp and self.rng.random() < rp:
                    self.gain(10.0 + self.ex.get("reaction_card", 0.0), "reaction")
                    if self.ex.get("brains"):
                        self.gain(0.3 * sk["cost"] if sk["cost"] else 15.0, "card")
                        for o in self.skills:
                            o["ready"] = max(t, o["ready"] - 1.0)
            break
        if any_ready_short:
            s["mom_wait"] += DT
        elif any_cd and all(t < sk["ready"] for sk in self.skills):
            s["cd_wait"] += DT
        # 普攻
        if t >= self.next_basic:
            self.next_basic = t + self.interval
            if self.rng.random() < BASIC_UPTIME:
                self.basic_n += 1
                self.gain(10 * self.interval / 1.334, "basic")
                if self.ex.get("third_breath") and self.basic_n % 3 == 0:
                    self.gain(self.ex["third_breath"], "card")
                if self.steal_armed:
                    self.gain(self.ex["martial_steal"], "card")
                    self.steal_armed = False
        s["area"] += self.m * DT
        s["ge60"] += DT * (self.m >= 60)
        s["full"] += DT * (self.m >= r.get("max", 100.0) - 1e-9)
        s["hot"] += DT * (self.m >= 70)
        s["cold"] += DT * (self.m < 30)
        self.t += DT

    def _r_wont_starve(self, rc):
        # “理性”：花掉 R 之后，仍够下一个将要转好的技能
        nxt = min(self.skills, key=lambda k: k["ready"])
        return self.m - rc >= nxt["cost"] * 0.5


def fight(content, arch, rules, start, seconds, seed):
    rng = random.Random(seed)
    f = Fighter(content, arch, rules, start, rng)
    while f.t < seconds:
        f.step()
    s = f.stats
    return {"casts_per_s": s["casts"] / seconds, "mom_wait_pct": 100 * s["mom_wait"] / seconds,
            "cd_wait_pct": 100 * s["cd_wait"] / seconds, "mean": s["area"] / seconds, "ge60_pct": 100 * s["ge60"] / seconds,
            "full_pct": 100 * s["full"] / seconds, "wasted_per_s": s["wasted"] / seconds, "income_per_s": s["income"] / seconds,
            "r_uses": s["r"], "r_first": s["r_first"], "hot_pct": 100 * s["hot"] / seconds, "cold_pct": 100 * s["cold"] / seconds,
            "end": f.m}


def avg(content, arch, rules, start, seconds, n=60):
    rows = [fight(content, arch, rules, start, seconds, 1000 + i) for i in range(n)]
    out = {k: round(sum(r[k] for r in rows) / n, 2) for k in rows[0] if k != "r_first"}
    firsts = [r["r_first"] for r in rows if r["r_first"] is not None]
    out["r_first_median"] = round(sorted(firsts)[len(firsts) // 2], 2) if firsts else None
    out["r_any_pct"] = round(100 * len(firsts) / n, 1)
    return out


def short_fights(content, arch, rules, start_dist, n=300, seed=5):
    """原生式短交战：时长 ~ 对数正态（中位 4.8 s、均值 5.4 s），起始劲按 start_dist 抽样。"""
    mu, sg = math.log(4.8), math.sqrt(2 * math.log(5.4 / 4.8))
    rng = random.Random(seed)
    got_r, firsts, area, ge60, tt, casts, spent_ratio_num, spent_ratio_den = 0, [], 0.0, 0.0, 0.0, 0, 0.0, 0.0
    for i in range(n):
        d = rng.lognormvariate(mu, sg)
        st = start_dist(rng)
        f = Fighter(content, arch, rules, st, random.Random(seed * 7 + i))
        while f.t < d:
            f.step()
        area += f.stats["area"]; ge60 += f.stats["ge60"]; tt += f.t; casts += f.stats["casts"]
        if f.stats["r_first"] is not None:
            got_r += 1
            firsts.append(f.stats["r_first"])
    firsts.sort()
    return {"r_in_fight_pct": round(100 * got_r / n, 1), "r_first_median": round(firsts[len(firsts) // 2], 2) if firsts else None,
            "mean": round(area / tt, 1), "ge60_pct": round(100 * ge60 / tt, 1), "casts_per_fight": round(casts / n, 2)}


# 架构（R 成本、收入规则均为示意值）
NATIVE_ENTRY = lambda rng: 50.0 if rng.random() < 0.88 else rng.uniform(60, 100)   # 原生：进入遭遇时 ≥60 仅约 12%
ZERO_ENTRY = lambda rng: 0.0
ARCHS = {
    "M0 现行（无 R，仅作基线）": dict(),
    "M1 共享消耗：技能照旧耗劲，R 花 50": dict(r_cost=50, r_policy="greedy"),
    "M1' 共享消耗 + 理性留劲": dict(r_cost=50, r_policy="rational"),
    "M2 战斗积累：技能 0 耗，无被动回劲，进场 0，R 花 50": dict(skill_cost_mult=0.0, passive=False, r_cost=50, r_policy="greedy", entry0=True),
    "M2n 同 M2，技能命中回劲按冷却归一": dict(skill_cost_mult=0.0, passive=False, tempo_norm=True, r_cost=50, r_policy="greedy", entry0=True),
    "M2p 同 M2，但保留交战被动 +5/s": dict(skill_cost_mult=0.0, r_cost=50, r_policy="greedy", entry0=True),
    "M3 混合：仅敌对技能耗劲（×0.6），自身增益免费，R 花 50": dict(skill_cost_mult=0.6, skill_cost_self_free=True, r_cost=50, r_policy="greedy"),
    "M4 状态型：技能 0 耗，交战中每秒 -6，R 清空（≥40 可用，8 s 一次）": dict(skill_cost_mult=0.0, drain=6.0, r_cost=40, r_spend_all=True, r_cd=8.0, r_policy="greedy", entry0=True),
    "M5 攻防热度：M2 + 承伤回劲": dict(skill_cost_mult=0.0, passive=False, hurt_gain=0.5, r_cost=50, r_policy="greedy", entry0=True),
    "M6 急速税：基础节拍免费、急速部分付劲，保留被动，R 花 50": dict(haste_tax=True, r_cost=50, r_policy="rational"),
}


def run_regime(content, tau):
    global CAST_TAU
    CAST_TAU = tau
    res = {"sustained_12s": {}, "short_native": {}}
    for an, rules in ARCHS.items():
        res["sustained_12s"][an], res["short_native"][an] = {}, {}
        entry0 = rules.get("entry0", False)
        for a in ARCH:
            res["sustained_12s"][an][a] = avg(content, a, rules, 0.0 if entry0 else 50.0, 12.0, n=40)
            res["short_native"][an][a] = short_fights(content, a, rules, ZERO_ENTRY if entry0 else NATIVE_ENTRY, n=200)
    return res


def calibrate(content):
    """选 CAST_TAU 使 M0 短交战的平均劲与 ≥60 占比接近原生全员值（61.5 / 46%；AI 55 / 36%）。"""
    global CAST_TAU
    rows = {}
    for tau in (0.0, 1.0, 2.0, 3.0, 4.0, 6.0):
        CAST_TAU = tau
        ms = [short_fights(content, a, {}, NATIVE_ENTRY, n=150) for a in ARCH]
        rows[tau] = {"mean": round(sum(m["mean"] for m in ms) / len(ms), 1),
                     "ge60_pct": round(sum(m["ge60_pct"] for m in ms) / len(ms), 1),
                     "casts_per_fight": round(sum(m["casts_per_fight"] for m in ms) / len(ms), 2)}
    best = min(rows, key=lambda k: abs(rows[k]["mean"] - 58) + abs(rows[k]["ge60_pct"] - 41) / 2)
    return best, rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--content", default=str(DEFAULT_CONTENT))
    args = ap.parse_args()
    content = json.loads(Path(args.content).read_text(encoding="utf-8"))
    tau, cal = calibrate(content)
    print("calibration (tau -> mean, ge60, casts/fight):", cal, "chosen", tau)
    res = {"assumptions": {"level": LEVEL, "basic_uptime": BASIC_UPTIME, "skill_hit_p": SKILL_HIT_P,
                           "hp_loss_per_s": HP_LOSS_PER_S, "dt": DT}, "calibration": {str(k): v for k, v in cal.items()},
           "tau_native": tau, "native_calibrated": run_regime(content, tau), "saturated": run_regime(content, 0.0)}
    out = Path(__file__).with_name("momentum_sim.json")
    out.write_text(json.dumps(res, ensure_ascii=False, indent=1), encoding="utf-8")
    keys = ["casts_per_s", "mom_wait_pct", "mean", "ge60_pct", "full_pct", "wasted_per_s", "income_per_s", "r_uses",
            "r_first_median"]
    for regime in ("native_calibrated", "saturated"):
        print("########", regime)
        for an in ARCHS:
            print("==", an)
            print("   ", "arch".ljust(13), " ".join(k[:10].rjust(10) for k in keys), "  short: R%/first/mean/ge60/casts")
            for a in ARCH:
                r = res[regime]["sustained_12s"][an][a]
                sn = res[regime]["short_native"][an][a]
                print("   ", a.ljust(13), " ".join(str(r.get(k)).rjust(10) for k in keys), " ",
                      sn["r_in_fight_pct"], sn["r_first_median"], sn["mean"], sn["ge60_pct"], sn["casts_per_fight"])


if __name__ == "__main__":
    main()
