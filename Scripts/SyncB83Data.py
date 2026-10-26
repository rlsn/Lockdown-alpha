#!/usr/bin/env python3
"""Build B83 environmental payloads without rewriting B80/B81/B82 history."""
import argparse
import hashlib
import json
import re
from pathlib import Path
from SyncB81Data import NEW_ITEMS, encode
from SyncV08Data import verify_provenance, native_enum, validate_schema

ROOT = Path(__file__).resolve().parents[1]


def native_ids(path, enum):
    result = {}
    next_id = 0
    for raw in native_enum(ROOT, path, enum):
        name, assigned, value = raw.partition("=")
        if assigned:
            next_id = int(value)
        result[name.strip()] = next_id
        next_id += 1
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    verify_provenance(ROOT)
    def read(name):
        return json.loads((ROOT / f"docs/v08/data/{name}.json").read_text())
    schema = json.loads((ROOT / "docs/v08/schemas/catalog.schema.json").read_text())
    for name in ("abilities", "items"):
        validate_schema(read(name), schema, name)
    adapters = json.loads((ROOT / "docs/v08/B80_ADAPTERS.json").read_text())
    ids = adapters["ability_ids"]
    skills = native_ids("Source/Lockdown/Public/Development/LDCharacterBuild.h", "ELDSkill")
    for name, number in skills.items():
        key = "legacy_" + name.lower() if 1 <= number <= 9 else re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()
        assert ids[key] == number, (name, number)
    assert skills["Frostline"] == 38 and skills["WaterJet"] == 40 and skills["Firebolt"] == 39
    item_ids = native_ids("Source/Lockdown/Public/Inventory/LDInventoryTypes.h", "ELDItemKind")
    source = (ROOT / "Source/Lockdown/Private/Inventory/LDEquipmentCatalog.cpp").read_text()
    mappings = re.findall(r'(?:\{TEXT\("([a-z_]+)"\),|Kinds.Add\(TEXT\("([a-z_]+)"\),)ELDItemKind::(\w+)', source)
    for old, added, native in mappings:
        assert adapters["item_ids"][old or added] == item_ids[native], native
    abilities = [a for a in read("abilities")["abilities"] if
                 (a["origin"] == "v07_retained" and a["id"] != "ember_line") or
                 a["id"] in ("frostline", "water_jet", "firebolt", "gust", "entangle", "chain_lightning", "shatter_burst", "conductive_field")]
    names = NEW_ITEMS + ["river_waders", "river_treads", "frost_prism", "water_flask", "frost_capsule", "oil_flask", "gust_vial"]
    items = [i for i in read("items")["items"] if i["origin"] == "v07_retained" or i["id"] in names]
    assert len(abilities) == 35 and len(items) == 61
    old_items = json.loads((ROOT / "docs/v07/data/items.json").read_text())["items"]
    supported_ops = {i["active"]["op"] for i in old_items if i["active"]} | {"element_item_hit", "element_item_field", "surface_clear"}
    assert all(not i["active"] or i["active"]["op"] in supported_ops for i in items)
    for item in items:
        active = item["active"]
        if active and active["op"] == "element_item_hit":
            assert item["id"] == "frost_prism" and active["element"] == "frost" and active["direct_frost"]
        if active and active["op"] == "element_item_field":
            assert item["id"] in ("water_flask", "frost_capsule", "oil_flask") and active["state"] in ("wet", "frozen", "oil")
    assert all(e["state"] in ("wet", "frozen", "burning", "electrified", "overgrown") for a in abilities for e in a["effects"] if e["op"] == "surface_apply")
    combat = dict(schema_version=8, milestone="B83", abilities=abilities, starter_kits=read("starter_kits")["templates"], environment=read("environment"), substrates={"soil": [-2800,-2000,-1200,-400],"flammable": [-1200,-800,-1200,-800],"metal": [200,800,-1200,-600]})
    equipment = dict(schema_version=8, milestone="B83", items=items, ability_gates=[
        {**{k: a[k] for k in ("id", "circle", "school", "requirements_any", "potency_fields")}, "skill": ids[a["id"]]}
        for a in abilities])
    outputs = {}
    hashes = {}
    for name, value in (("combat", combat), ("equipment", equipment)):
        raw = encode(value)
        outputs[Path(f"Content/Lockdown/Data/V08/{name}_b83.json")] = raw
        hashes[name] = hashlib.sha256(raw).hexdigest()
    outputs[Path("Source/Lockdown/Private/Development/LDB83Contract.h")] = (
        "#pragma once\n" + "".join(f'#define LD_B83_{k.upper()}_SHA TEXT("{v}")\n' for k, v in hashes.items())).encode()
    outputs[Path("Content/Lockdown/Data/V08/B83Manifest.json")] = encode(dict(
        schema_version=8, milestone="B83", enabled_abilities=[a["id"] for a in abilities],
        enabled_items=[i["id"] for i in items], enabled_presets=list(range(9)), payload_sha256=hashes,
        scope="Six states/Oil and ten bounded reactions; remaining catalog/acquisition stays in B84."))
    for path, raw in outputs.items():
        if args.check:
            assert (ROOT / path).read_bytes().replace(b"\r\n", b"\n") == raw, path
        else:
            (ROOT / path).write_bytes(raw)
    print("PASS B83: 35 abilities, 61 items, six states/Oil and ten reactions; remaining catalog disabled")


if __name__ == "__main__":
    main()
