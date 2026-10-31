#!/usr/bin/env python3
"""Compile the current schema8 catalogs without documentation/archive inputs."""
import argparse
import hashlib
from pathlib import Path
import re

from CatalogTools import encode, native_ids, read_json, require, rows_by_id, validate_schema

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path("SourceData/Catalogs/V08")
DATA_NAMES = ("abilities", "items", "acquisition", "starter_kits", "environment", "modifier_contracts")


def build_outputs(root=ROOT):
    root = Path(root)
    data = {name: read_json(root / SOURCE / (name + ".json")) for name in DATA_NAMES}
    require(all(value.get("schema_version") == 8 for value in data.values()), "Mixed catalog schemas")
    identities = read_json(root / SOURCE / "native_ids.json")
    require(set(identities) == {"schema_version", "ability_ids", "item_ids"}
            and identities["schema_version"] == 8, "Invalid native identity contract")
    for label in ("ability_ids", "item_ids"):
        mapping = identities[label]
        require(isinstance(mapping, dict) and all(type(value) is int and 0 <= value < 256
                                                 for value in mapping.values()),
                f"{label}: invalid native ID")
        require(len(set(mapping.values())) == len(mapping), f"{label}: duplicate native IDs")
    schema = read_json(root / SOURCE / "catalog.schema.json")
    for name in ("abilities", "items"):
        validate_schema(data[name], schema, name)

    skills = native_ids(root, "Source/Lockdown/Public/Development/LDCharacterBuild.h", "ELDSkill")
    kinds = native_ids(root, "Source/Lockdown/Public/Inventory/LDInventoryTypes.h", "ELDItemKind")
    expected_skills = {
        "legacy_" + name.lower() if 1 <= value <= 9 else re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower(): value
        for name, value in skills.items()
    }
    require(identities["ability_ids"] == expected_skills, "Renumbered or unmapped native skill")
    source = (root / "Source/Lockdown/Private/Inventory/LDEquipmentCatalog.cpp").read_text(encoding="utf-8-sig")
    mappings = re.findall(r'(?:\{TEXT\("([a-z_]+)"\),|Kinds.Add\(TEXT\("([a-z_]+)"\),)ELDItemKind::(\w+)', source)
    names = {old or added: name for old, added, name in mappings}

    items = data["items"]["items"]
    abilities = data["abilities"]["abilities"]
    acquisition = data["acquisition"]
    item_rows = rows_by_id(items, "items")
    ability_rows = rows_by_id(abilities, "abilities")
    recipes = rows_by_id(acquisition["recipes"], "recipes")
    require((len(items), len(abilities), len(recipes)) == (80, 52, 20), "Catalog count mismatch")
    require(set(identities["item_ids"]) == set(item_rows), "Unmapped catalog item")
    for identity in item_rows:
        require(identity in names and names[identity] in kinds
                and identities["item_ids"][identity] == kinds[names[identity]],
                f"Renumbered or unmapped item: {identity}")
    require(set(ability_rows) <= set(expected_skills), "Unmapped catalog ability")

    materials = rows_by_id(acquisition["materials"], "materials")
    acquisition_source = (root / "Source/Lockdown/Private/Inventory/LDAcquisitionCatalog.cpp").read_text(encoding="utf-8-sig")
    material_names = dict(re.findall(r'\{TEXT\("(\w+)"\),ELDItemKind::(\w+)\}', acquisition_source))
    require(set(materials) <= set(material_names)
            and all(material_names[identity] in kinds for identity in materials),
            "Unknown native material adapter")
    ingredients = set(item_rows) | set(materials)
    for recipe in recipes.values():
        require(isinstance(recipe["inputs"], dict) and recipe["inputs"], f"{recipe['id']}: missing inputs")
        require(set(recipe["inputs"]) <= ingredients and recipe["output"] in ingredients,
                f"{recipe['id']}: unknown recipe reference")
        require(all(type(count) is int and count > 0 for count in recipe["inputs"].values()),
                f"{recipe['id']}: invalid ingredient count")
    starters = data["starter_kits"]["templates"]
    rows_by_id(starters, "starter kits")
    for starter in starters:
        require(set(starter["skills"]) <= set(ability_rows), f"{starter['id']}: unknown starting skill")
    modifiers = rows_by_id(data["modifier_contracts"]["modifiers"], "modifiers")
    require(len(modifiers) == 18, "Modifier count mismatch")

    gates = [
        {**{key: ability[key] for key in ("id", "circle", "school", "requirements_any", "potency_fields")},
         "skill": identities["ability_ids"][ability["id"]]}
        for ability in abilities
    ]
    values = {
        "combat": dict(schema_version=8, milestone="B84", abilities=abilities, starter_kits=starters,
                       environment=data["environment"], modifiers=data["modifier_contracts"]),
        "equipment": dict(schema_version=8, milestone="B84", items=items, ability_gates=gates,
                          modifiers=data["modifier_contracts"]),
        "acquisition": dict(items=[{**item, "kind": names[item["id"]]} for item in items],
                            acquisition=acquisition),
    }
    outputs = {}
    hashes = {}
    for name, value in values.items():
        raw = encode(value)
        outputs[Path(f"Content/Lockdown/Data/V08/{name}_b84.json")] = raw
        hashes[name] = hashlib.sha256(raw).hexdigest()
    outputs[Path("Source/Lockdown/Private/Development/LDB84Contract.h")] = (
        "#pragma once\n" + "".join(f'#define LD_B84_{key.upper()}_SHA TEXT("{value}")\n'
                                  for key, value in hashes.items())
    ).encode("utf-8")
    outputs[Path("Content/Lockdown/Data/V08/B84Manifest.json")] = encode(dict(
        milestone="B84", enabled_abilities=[ability["id"] for ability in abilities],
        enabled_items=[item["id"] for item in items], enabled_recipes=list(recipes),
        enabled_presets=list(range(9)), payload_sha256=hashes,
    ))
    return outputs


def check_outputs(root, outputs):
    for relative, expected in outputs.items():
        path = Path(root) / relative
        require(path.is_file() and path.read_bytes().replace(b"\r\n", b"\n") == expected,
                f"Stale or missing output: {relative}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    outputs = build_outputs()
    if args.check:
        check_outputs(ROOT, outputs)
    else:
        for relative, raw in outputs.items():
            path = ROOT / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            if not path.exists() or path.read_bytes() != raw:
                path.write_bytes(raw)
    print("PASS B84: 52 abilities / 80 items / 20 recipes / 18 modifiers; stable native identities")


if __name__ == "__main__":
    main()
