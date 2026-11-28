"""劲-B（M2p）的 R 价格 / 脱战基线敏感度与一场交战内 R 次数（L2，原生校准 τ=4）。
用法：python -B sensitivity_m2p.py  → sensitivity_m2p.json
"""
import json

import momentum_sim as ms

c = json.load(open(ms.DEFAULT_CONTENT, encoding="utf-8"))
ms.CAST_TAU = 4.0
A = list(ms.ARCH)
out = {}
for price in (40, 50, 60):
    for base in (0, 30):
        rules = dict(skill_cost_mult=0.0, r_cost=price, r_policy="greedy")
        rows = {a: ms.short_fights(c, a, rules, (lambda rng, b=base: float(b)), n=150) for a in A}
        f = [rows[a]["r_first_median"] for a in A if rows[a]["r_first_median"] is not None]
        p = [rows[a]["r_in_fight_pct"] for a in A]
        out[f"price{price}_base{base}"] = {"first_min": min(f), "first_max": max(f), "ratio": round(max(f) / max(min(f), 0.01), 2),
                                           "Rpct_min": min(p), "Rpct_max": max(p)}
rules = dict(skill_cost_mult=0.0, r_cost=50, r_policy="greedy")
out["r_uses_6.9s_price50_base0"] = {a: ms.avg(c, a, rules, 0.0, 6.9, n=60)["r_uses"] for a in A}
out["r_uses_12s_price50_base0"] = {a: ms.avg(c, a, rules, 0.0, 12.0, n=60)["r_uses"] for a in A}
json.dump(out, open("sensitivity_m2p.json", "w", encoding="utf-8"), ensure_ascii=False, indent=1)
print(json.dumps(out, ensure_ascii=False, indent=1))
