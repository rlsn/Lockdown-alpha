"""S3 Combat Geometry extraction (L1, read-only).

Reads SourceData/Demo/content.json from a given repo root and prints a compact
per-ability geometry table (JSON) used by COMBAT_GEOMETRY_MATRIX.md and
combat_model.py. Never writes into the repo.

Usage: python extract_geometry.py <repo_root> [--out abilities.json]
"""
import json
import sys
from pathlib import Path

GEOM_FIELDS = (
    "radius", "inner_radius", "trigger_radius", "width", "length", "angle", "speed",
    "delay", "duration", "distance", "interval", "size", "count", "targets",
    "extra_duration", "stun_duration", "disarm_duration", "charges",
)


def val(x):
    if isinstance(x, dict) and "value" in x:
        return x["value"]
    return x


def compact(x):
    if isinstance(x, dict):
        if set(x.keys()) <= {"unit", "value"} and "value" in x:
            return x["value"]
        return {k: compact(v) for k, v in x.items()}
    if isinstance(x, list):
        return [compact(v) for v in x]
    return x


def summarize_effect(e):
    out = {"op": e["op"]}
    for k in GEOM_FIELDS + ("base", "ad_ratio", "ap_ratio", "max_hp_ratio", "ratio", "damage_type"):
        if k in e:
            out[k] = val(e[k])
    if "effects" in e:
        out["effects"] = [summarize_effect(s) for s in e["effects"]]
    return out


def main():
    root = Path(sys.argv[1])
    content = json.loads((root / "SourceData/Demo/content.json").read_text(encoding="utf-8"))
    names = {}
    for row in content["display"]:
        names[row["id"]] = row.get("name_text")
    rows = []
    for a in content["abilities"]:
        tgt = a.get("target", {})
        rows.append({
            "id": a["id"],
            "name": names.get(a["id"]),
            "opening": a.get("opening", False),
            "element": a.get("element"),
            "hit_type": a.get("hit_type"),
            "mode": tgt.get("mode"),
            "range": val(tgt.get("range")),
            "target": compact(tgt),
            "windup": val(a.get("windup")),
            "cost": val(a.get("momentum_cost")),
            "cooldown": val(a.get("cooldown")),
            "effects": [summarize_effect(e) for e in a["effects"]],
            "ranks": {k: {"name": v.get("name"), "text": v.get("text"),
                          "patches": [compact(p) for p in v.get("patches", [])]}
                      for k, v in a.get("ranks", {}).items()},
        })
    text = json.dumps(rows, ensure_ascii=False, indent=1)
    if "--out" in sys.argv:
        Path(sys.argv[sys.argv.index("--out") + 1]).write_text(text, encoding="utf-8")
    else:
        print(text)


if __name__ == "__main__":
    main()
