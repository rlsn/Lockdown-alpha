"""S3 telemetry slice (L4 evidence, AI-only native full match, read-only).

Reads a Lockdown authority log (LD_HIT lines) and summarises contender-vs-contender
engagements per victim life: time from first PvP damage to death, damage share
basic/ability/periodic, peak 1 s burst as a share of max HP, attacker count.

Usage: python telemetry_pvp.py <log> [--json out.json]
"""
import json
import re
import statistics
import sys
from collections import defaultdict

KV = re.compile(r"(\w+)=([^\s]+)")


def parse(path):
    hits = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            i = line.find("LD_HIT ")
            if i < 0:
                continue
            d = dict(KV.findall(line[i:]))
            if d.get("src_kind") != "contender" or d.get("victim_kind") != "contender":
                continue
            if d.get("src") == d.get("victim"):
                continue
            hits.append({
                "t": int(d["t"]), "src": d["src"], "victim": d["victim"], "life": d.get("victim_life"),
                "via": d.get("via", "-"), "cls": d.get("class", "-"), "hp_lost": float(d.get("hp_lost", 0)),
                "absorbed": float(d.get("absorbed", 0)), "max_hp": float(d.get("max_hp", 1)),
                "periodic": d.get("periodic") == "1", "lethal": d.get("lethal") == "1",
                "controls": d.get("controls", "-"),
            })
    return hits


def summarise(hits, gap_ms=8000):
    by_life = defaultdict(list)
    for h in hits:
        by_life[(h["victim"], h["life"])].append(h)
    deaths = []
    for key, rows in by_life.items():
        rows.sort(key=lambda r: r["t"])
        lethal = [r for r in rows if r["lethal"]]
        if not lethal:
            continue
        end = lethal[0]["t"]
        # The engagement that killed: walk back while consecutive PvP hits are < gap_ms apart.
        seg = [r for r in rows if r["t"] <= end]
        start_idx = len(seg) - 1
        while start_idx > 0 and seg[start_idx]["t"] - seg[start_idx - 1]["t"] < gap_ms:
            start_idx -= 1
        seg = seg[start_idx:]
        dmg = sum(r["hp_lost"] + r["absorbed"] for r in seg)
        by_cls = defaultdict(float)
        for r in seg:
            k = "periodic" if r["periodic"] else r["cls"]
            by_cls[k] += r["hp_lost"] + r["absorbed"]
        peak = 0.0
        j = 0
        win = 0.0
        for i, r in enumerate(seg):
            win += r["hp_lost"] + r["absorbed"]
            while seg[j]["t"] < r["t"] - 1000:
                win -= seg[j]["hp_lost"] + seg[j]["absorbed"]
                j += 1
            peak = max(peak, win)
        deaths.append({
            "victim": key[0][:8], "life": key[1], "ttk_ms": end - seg[0]["t"], "hits": len(seg), "end_t": end,
            "attackers": len({r["src"] for r in seg}), "max_hp": seg[-1]["max_hp"],
            "peak_1s_share": peak / seg[-1]["max_hp"], "share": {k: v / dmg for k, v in by_cls.items()} if dmg else {},
            "controls": sum(1 for r in seg if r["controls"] not in ("-", "")),
        })
    return deaths


def by_phase(deaths, key, bins=4):
    """D04-A diagnostics per phase (S3b): equal-count bins of match time (end_t) or of victim max HP (growth proxy).
    Alarms are research signals, not limits: a solo source taking >=50% max HP within 1 s, and multi-attacker deaths."""
    rows = sorted(deaths, key=lambda d: d[key])
    out = []
    for b in range(bins):
        part = rows[len(rows) * b // bins:len(rows) * (b + 1) // bins]
        if not part:
            continue
        ttk = [d["ttk_ms"] / 1000 for d in part]
        out.append({
            "bin": b + 1, key + "_range": [part[0][key], part[-1][key]], "deaths": len(part),
            "ttk_median_s": statistics.median(ttk),
            "peak_1s_share_median": statistics.median(d["peak_1s_share"] for d in part),
            "solo_burst_ge50_share": sum(1 for d in part if d["attackers"] == 1 and d["peak_1s_share"] >= 0.5) / len(part),
            "multi_attacker_share": sum(1 for d in part if d["attackers"] > 1) / len(part),
            "controls_median": statistics.median(d["controls"] for d in part),
        })
    return out


def main():
    hits = parse(sys.argv[1])
    deaths = summarise(hits)
    ttk = [d["ttk_ms"] / 1000 for d in deaths]
    out = {
        "pvp_hits": len(hits),
        "deaths": len(deaths),
        "ttk_s": {"median": statistics.median(ttk) if ttk else None,
                  "p25": statistics.quantiles(ttk, n=4)[0] if len(ttk) > 3 else None,
                  "p75": statistics.quantiles(ttk, n=4)[2] if len(ttk) > 3 else None},
        "multi_attacker_deaths": sum(1 for d in deaths if d["attackers"] > 1),
        "peak_1s_share_median": statistics.median([d["peak_1s_share"] for d in deaths]) if deaths else None,
        "damage_share_mean": {k: statistics.mean([d["share"].get(k, 0) for d in deaths])
                              for k in ("basic", "ability", "periodic")} if deaths else {},
        "phase_by_match_time": by_phase(deaths, "end_t"),
        "phase_by_victim_max_hp": by_phase(deaths, "max_hp"),
        "rows": deaths,
    }
    text = json.dumps(out, ensure_ascii=False, indent=1)
    if "--json" in sys.argv:
        open(sys.argv[sys.argv.index("--json") + 1], "w", encoding="utf-8").write(text)
    print(json.dumps({k: v for k, v in out.items() if k != "rows"}, ensure_ascii=False, indent=1))


if __name__ == "__main__":
    main()
