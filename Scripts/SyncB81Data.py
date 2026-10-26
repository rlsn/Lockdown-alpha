#!/usr/bin/env python3
"""B81 native enabled subset; B80's full disabled inventory remains frozen."""
import argparse
import hashlib
import json
from pathlib import Path
from SyncV08Data import validate_schema, verify_provenance

ROOT = Path(__file__).resolve().parents[1]
NEW_ITEMS = ["iron_helm", "scholars_hood", "scouts_cowl", "reinforced_leggings",
             "runners_trousers", "laborers_grip", "iron_longsword", "arcane_focus",
             "duelists_parrying_dagger", "wayfarer_boots", "channelers_vest",
             "cleansing_charm", "mirror_amulet", "arcane_lens"]


def encode(value):
    return (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    verify_provenance(ROOT)
    def read(name):
        return json.loads((ROOT / ("docs/v08/data/" + name + ".json")).read_text())
    adapters = json.loads((ROOT / "docs/v08/B80_ADAPTERS.json").read_text())
    schema = json.loads((ROOT / "docs/v08/schemas/catalog.schema.json").read_text())
    for name in ("abilities", "items"):
        validate_schema(read(name), schema, name)
    # Retention of an ID does not imply its schema8 effects are implemented:
    # Ember Line adds a Burning surface in v08, so its entire action stays hidden.
    abilities = [a for a in read("abilities")["abilities"]
                 if a["origin"] == "v07_retained" and a["id"] != "ember_line"]
    items = [i for i in read("items")["items"] if i["origin"] == "v07_retained" or i["id"] in NEW_ITEMS]
    assert len(abilities) == 27 and len(items) == 54
    old_items = json.loads((ROOT / "docs/v07/data/items.json").read_text())["items"]
    supported_item_ops = {i["active"]["op"] for i in old_items if i["active"]}
    assert all(not i["active"] or i["active"]["op"] in supported_item_ops for i in items)
    assert all(e["op"] != "surface_apply" for a in abilities for e in a["effects"])
    kits = read("starter_kits")["templates"]
    # No substitute starter or fake Frostline implementation. Preset5 is disabled.
    kits = [{**kit, "skills": [] if kit["id"] == "arcanist" else kit["skills"]} for kit in kits]
    combat = {"schema_version": 8, "milestone": "B81", "abilities": abilities, "starter_kits": kits}
    equipment = {"schema_version": 8, "milestone": "B81", "items": items,
                 "ability_gates": [{**{k: a[k] for k in ("id", "circle", "school", "requirements_any", "potency_fields")},
                                    "skill": adapters["ability_ids"][a["id"]]} for a in abilities]}
    outputs = {}
    hashes = {}
    for name, value in (("combat", combat), ("equipment", equipment)):
        raw = encode(value)
        outputs[Path(f"Content/Lockdown/Data/V08/{name}_b81.json")] = raw
        hashes[name] = hashlib.sha256(raw).hexdigest()
    header = '#pragma once\n' + ''.join(f'#define LD_B81_{name.upper()}_SHA TEXT("{value}")\n' for name, value in hashes.items())
    outputs[Path("Source/Lockdown/Private/Development/LDB81Contract.h")] = header.encode()
    manifest = {"schema_version": 8, "milestone": "B81", "profile": "v08_integration",
                "enabled_abilities": [a["id"] for a in abilities], "enabled_items": [i["id"] for i in items],
                "enabled_presets": [0, 1, 2, 3, 4, 6, 7, 8],
                "disabled_presets": {"5": "Arcanist needs Frostline; B82/B83 dependency"},
                "disabled_retained_abilities": {"ember_line": "v08 Burning surface requires B83; v07 damage zone preserved in v07 profile"},
                "payload_sha256": hashes,
                "source_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                  for p in sorted((ROOT / "docs/v08/data").glob("*.json"))},
                "scope": "Retained handlers with B81 slots/traits and fourteen new scalar/protection items. Elemental/modifier content stays hidden. No grants."}
    outputs[Path("Content/Lockdown/Data/V08/B81Manifest.json")] = encode(manifest)
    for name, raw in outputs.items():
        if args.check:
            assert (ROOT / name).read_bytes().replace(b"\r\n", b"\n") == raw, name
        else:
            (ROOT / name).write_bytes(raw)
    print("PASS B81: 27 retained abilities, 54 enabled items, 8 presets; Ember Line surface and elemental content hidden")


if __name__ == "__main__":
    main()
