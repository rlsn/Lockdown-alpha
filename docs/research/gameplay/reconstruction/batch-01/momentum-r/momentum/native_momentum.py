"""Momentum 原生账本分析（L4/L5，只读日志）。

输入：打包真人局 Game.log（EXP-01，1 真人 + 4 AI）与 7 AI 整局截取（EXP-03）。
输出：native_momentum.json + 控制台摘要。用法：python -B native_momentum.py
口径：
- 交战状态按每人每秒 LD_HEARTBEAT 归类：距该人最近一次参赛者互相 LD_HIT ≤5s = PvP；否则距最近一次与营地的 LD_HIT ≤5s = PvE；否则 = 脱战。
  （5s = MOMENTUM.IdleDelaySec，与劲的交战回复窗口同口径。）
- 进入 PvP 的劲：PvP 命中前 ≥5s 无 PvP 命中视为一次新遭遇，取遭遇开始前最后一条心跳的劲。
- 收支来自每条生命结束时的 LD_MOMENTUM；爆劲实付来自 LD_OVERDRIVE paid=。
"""
from __future__ import annotations

import bisect
import json
import re
import statistics as st
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[7] / "Artifacts"
LOGS = {
    "EXP01_human_match": ROOT / "20261128-123211-963-Packaged-Interactive" / "Game.log",
    "EXP03_7ai_match": ROOT / "GameplayResearch" / "telemetry_7ai_excerpt.log",
}
REJECTS = ["None", "InvalidRequest", "NotOwner", "StaleMatch", "StaleLife", "StaleRequest", "RequestConflict",
           "ReceiptExpired", "Cancelled", "StaleSource", "SourceUnavailable", "Busy", "Cooldown", "AttackNotReady",
           "MomentumShort", "ControlBlocked", "TargetUnavailable", "InvalidDirection", "OutOfRange", "Blocked", "Dead",
           "MatchClosed", "AuthorityDataUnavailable", "CapacityUnavailable", "NotStarted", "HealthShort"]
ENTRY = {"1": "Basic", "2": "Q", "3": "W", "4": "Dash", "5": "E"}
BOOKS = ["basic", "combat", "skill", "reaction", "dodge", "other", "idle", "spent"]

RE_HB = re.compile(r"LD_HEARTBEAT t=(\d+) contender=(\S+) phase=Alive .*?level=(\d+).*?momentum=([\d.]+)/")
RE_HB2 = re.compile(r"LD_HEARTBEAT t=(\d+) contender=(\S+) phase=Alive .*?momentum=([\d.]+)/.*?level=(\d+)")
RE_HIT = re.compile(r"LD_HIT t=(\d+) src=(\S+) src_kind=(\S+) .*?victim=(\S+) victim_life=\S+ victim_kind=(\S+)")
RE_LEDGER = re.compile(r"LD_MOMENTUM entity=(\S+) life=(\d+) basic=(\S+) combat=(\S+) skill=(\S+) reaction=(\S+) "
                       r"dodge=(\S+) other=(\S+) idle=(\S+) spent=(\S+) overdrive=(\d+) end=(\S+)")
RE_OD = re.compile(r"LD_OVERDRIVE t=(\d+) src=(\S+) active=(\S+) paid=(\d+)")
RE_ROSTER = re.compile(r"LD_MATCH_ROSTER .*?contender=(\S+) name=\S+ kind=(\S+)")
RE_RECEIPT = re.compile(r"LD_COMBAT_INPUT receipt .*?entry=(\d+) phase=(\d+) reject=(\d+)")


def pct(xs, f):
    return round(100 * sum(1 for x in xs if f(x)) / len(xs), 1) if xs else None


def dist(xs):
    if not xs:
        return {"n": 0}
    return {"n": len(xs), "mean": round(st.mean(xs), 1), "median": round(st.median(xs), 1),
            "ge60_pct": pct(xs, lambda x: x >= 60), "full_pct": pct(xs, lambda x: x >= 99.99),
            "lt25_pct": pct(xs, lambda x: x < 25), "lt35_pct": pct(xs, lambda x: x < 35)}


def analyse(path: Path):
    hb, pvp, pve, ledgers, ods, kinds, receipts = [], defaultdict(list), defaultdict(list), [], [], {}, Counter()
    with path.open(encoding="utf-8", errors="replace") as f:
        for line in f:
            if "LD_HEARTBEAT" in line and "phase=Alive" in line:
                m = RE_HB2.search(line)
                if m:
                    hb.append((int(m.group(1)), m.group(2), float(m.group(3)), int(m.group(4))))
            elif "LD_HIT " in line:
                m = RE_HIT.search(line)
                if m and m.group(3) == "contender":
                    t, src, vic, vk = int(m.group(1)), m.group(2), m.group(4), m.group(5)
                    if vk == "contender":
                        pvp[src].append(t); pvp[vic].append(t)
                    elif vk == "camp":
                        pve[src].append(t)
            elif "LD_MOMENTUM " in line:
                m = RE_LEDGER.search(line)
                if m:
                    ledgers.append((m.group(1), [float(x) for x in m.groups()[2:10]], int(m.group(11))))
            elif "LD_OVERDRIVE " in line:
                m = RE_OD.search(line)
                if m:
                    ods.append((m.group(2), m.group(3), int(m.group(4)) / 100))
            elif "LD_MATCH_ROSTER" in line:
                m = RE_ROSTER.search(line)
                if m:
                    kinds[m.group(1)] = m.group(2)
            elif "LD_COMBAT_INPUT receipt" in line:
                m = RE_RECEIPT.search(line)
                if m and m.group(2) in ("3", "4"):  # Finished / Rejected 终态
                    receipts[(ENTRY.get(m.group(1), m.group(1)), REJECTS[int(m.group(3))])] += 1
    for d in (pvp, pve):
        for k in d:
            d[k].sort()

    def last_before(lst, t):
        i = bisect.bisect_right(lst, t)
        return lst[i - 1] if i else None

    # 1. 状态分布
    states = defaultdict(lambda: defaultdict(list))
    for t, e, m, lvl in hb:
        who = "human" if kinds.get(e) == "Human" else "ai"
        lp, le = last_before(pvp.get(e, []), t), last_before(pve.get(e, []), t)
        s = "pvp" if lp is not None and t - lp <= 5000 else "pve" if le is not None and t - le <= 5000 else "idle"
        states[who][s].append(m)
        states["all"][s].append(m)
        states[who]["phase_min_%02d" % (t // 60000)].append(m)
    state_out = {who: {s: dist(v) for s, v in d.items() if not s.startswith("phase")} for who, d in states.items()}
    phase_out = {s: dist(v)["mean"] for s, v in sorted(states["all"].items()) if s.startswith("phase")} if False else {}
    by_min = defaultdict(list)
    for t, e, m, lvl in hb:
        by_min[t // 60000].append(m)
    phase_out = {f"{k:02d}min": round(st.mean(v), 1) for k, v in sorted(by_min.items())}

    # 2. 进入 PvP 遭遇时的劲
    hb_by = defaultdict(list)
    for t, e, m, lvl in hb:
        hb_by[e].append((t, m))
    entry = defaultdict(list)
    for e, hits in pvp.items():
        prev = None
        hts = [x[0] for x in hb_by.get(e, [])]
        for t in hits:
            if prev is None or t - prev > 5000:
                i = bisect.bisect_right(hts, t) - 1
                if i >= 0 and t - hts[i] <= 1500:
                    who = "human" if kinds.get(e) == "Human" else "ai"
                    entry[who].append(hb_by[e][i][1]); entry["all"].append(hb_by[e][i][1])
            prev = t
    entry_out = {k: dist(v) for k, v in entry.items()}

    # 3. 收支账本
    book = {}
    for who in ("human", "ai", "all"):
        rows = [r for r in ledgers if who == "all" or (who == "human") == (kinds.get(r[0]) == "Human")]
        tot = [sum(r[1][i] for r in rows) for i in range(8)]
        income = sum(tot[:6])
        od_paid = sum(p for s, a, p in ods if who == "all" or (who == "human") == (kinds.get(s) == "Human"))
        od_n = sum(r[2] for r in rows)
        book[who] = {"lives": len(rows), **{b: round(v, 1) for b, v in zip(BOOKS, tot)},
                     "income_share_pct": {b: round(100 * v / income, 1) if income else None for b, v in zip(BOOKS[:6], tot[:6])},
                     "overdrives": od_n, "overdrive_paid": round(od_paid, 1),
                     "spent_on_normal_casts": round(tot[7] - od_paid, 1)}
    od_by = Counter(a for s, a, p in ods)
    hp = [(t, e) for t, e, m, l in hb]
    return {"alive_seconds": len(hb), "state": state_out, "momentum_by_match_minute": phase_out,
            "pvp_entry": entry_out, "ledger": book, "overdrive_by_active": dict(od_by),
            "human_receipts": {f"{k[0]}:{k[1]}": v for k, v in receipts.most_common()}}


def main():
    out = {k: analyse(p) for k, p in LOGS.items() if p.exists()}
    here = Path(__file__).with_name("native_momentum.json")
    here.write_text(json.dumps(out, ensure_ascii=False, indent=1), encoding="utf-8")
    for k, r in out.items():
        print("==", k, "alive s", r["alive_seconds"])
        for who, d in r["state"].items():
            print("  state", who, {s: (v.get("n"), v.get("mean"), v.get("ge60_pct"), v.get("full_pct"), v.get("lt35_pct")) for s, v in d.items()})
        print("  pvp_entry", {k2: (v.get("n"), v.get("mean"), v.get("ge60_pct"), v.get("full_pct")) for k2, v in r["pvp_entry"].items()})
        for who, b in r["ledger"].items():
            print("  ledger", who, b)
        print("  by minute", r["momentum_by_match_minute"])
        print("  od by active", r["overdrive_by_active"])
        print("  receipts", r["human_receipts"])


if __name__ == "__main__":
    main()
