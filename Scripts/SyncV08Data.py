#!/usr/bin/env python3
"""B80: validate and stage the disabled schema8 contract, never enable gameplay.

Run with --check to compare deterministic output without writing. The original
specification checks remain separate from native operation/asset acceptance.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SPEC = Path("docs/v08")
DATA_NAMES = (
    "abilities", "acceptance_index", "acquisition", "class_traits", "effects",
    "environment", "items", "modifier_contracts", "operation_contracts",
    "starter_kits", "v08_rules", "world_and_routes",
)
STAGES = {"B80": "Audit/import", "B81": "Slots/traits", "B82": "River core",
          "B83": "Fields", "B84": "Catalog/acquisition",
          "B85": "World integration", "B86": "Native usability"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read(root, name):
    return json.loads((root / name).read_text(encoding="utf-8-sig"))


def encode(value):
    return (json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n").encode()


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def validate_schema(value, schema, path="catalog"):
    """Evaluate the structural keywords used by the bundled catalog schema.

    No external package or network schema resolver is needed. Unsupported
    validation keywords reject rather than silently weakening the contract.
    """
    supported = {"$schema", "title", "type", "required", "properties", "items",
                 "const", "enum", "oneOf", "anyOf", "minItems", "maxItems",
                 "minLength", "pattern", "minimum", "maximum", "additionalProperties"}
    require(set(schema) <= supported, f"{path}: unsupported schema keywords")
    types = {"object": lambda x: isinstance(x, dict),
             "array": lambda x: isinstance(x, list),
             "string": lambda x: isinstance(x, str),
             "integer": lambda x: type(x) is int,
             "number": lambda x: type(x) in (int, float),
             "boolean": lambda x: type(x) is bool,
             "null": lambda x: x is None}
    if "type" in schema:
        require(schema["type"] in types, f"{path}: unknown schema type")
        require(types[schema["type"]](value), f"{path}: expected {schema['type']}")
    if "const" in schema:
        require(value == schema["const"], f"{path}: wrong constant")
    if "enum" in schema:
        require(value in schema["enum"], f"{path}: unknown enum {value}")
    for keyword in ("oneOf", "anyOf"):
        if keyword not in schema:
            continue
        matches = 0
        for branch in schema[keyword]:
            try:
                validate_schema(value, branch, path)
                matches += 1
            except ValueError:
                pass
        require(matches == 1 if keyword == "oneOf" else matches > 0,
                f"{path}: {keyword} mismatch")
    if isinstance(value, dict):
        require(set(schema.get("required", [])) <= value.keys(), f"{path}: missing required fields")
        properties = schema.get("properties", {})
        additional = schema.get("additionalProperties", True)
        for key, child in value.items():
            if key in properties:
                validate_schema(child, properties[key], f"{path}.{key}")
            elif isinstance(additional, dict):
                validate_schema(child, additional, f"{path}.{key}")
            else:
                require(additional, f"{path}: unexpected {key}")
    if isinstance(value, list):
        require(schema.get("minItems", 0) <= len(value) <= schema.get("maxItems", float("inf")),
                f"{path}: wrong array length")
        if "items" in schema:
            for index, child in enumerate(value):
                validate_schema(child, schema["items"], f"{path}[{index}]")
    if isinstance(value, str):
        require(len(value) >= schema.get("minLength", 0), f"{path}: empty string")
        require("pattern" not in schema or re.search(schema["pattern"], value), f"{path}: invalid ID")
    if type(value) in (int, float):
        require(schema.get("minimum", -float("inf")) <= value <= schema.get("maximum", float("inf")),
                f"{path}: numeric bounds")


def rows_by_id(rows, label):
    result = {row["id"]: row for row in rows}
    require(len(result) == len(rows), f"{label}: duplicate IDs")
    return result


def verify_provenance(root):
    manifest = read(root, SPEC / "SOURCE_MANIFEST.json")
    previous = read(root, "docs/v07/evidence/B77/Audit.json")["preservation"]["v08UserDraftFiles"]
    require(manifest["original_package"] == {k.removeprefix("docs/v08/"): v for k, v in previous.items()},
            "Original input hashes must match the B77 record")
    require(set(manifest["adopted_package"]) == set(manifest["original_package"]), "Adoption inventory changed")
    changed = []
    for name, expected in manifest["adopted_package"].items():
        require(sha((root / SPEC / name).read_bytes()) == expected, f"Changed adopted input: {name}")
        if expected != manifest["original_package"][name]:
            changed.append(name)
    require(set(changed) == set(manifest["adoption_edits"]), "Undocumented adoption edit")
    for row in manifest["inputs"]:
        require(sha((root / SPEC / row["file"]).read_bytes()) == row["sha256"], "Source archive mismatch")
    return manifest


def native_enum(root, path, name):
    source = (root / path).read_text(encoding="utf-8")
    match = re.search(r"enum class " + name + r"\s*:\s*uint8\s*\{([^}]+)\}", source)
    require(match is not None, f"Missing native enum: {name}")
    return {name.strip(): number for number, name in enumerate(match[1].split(","))}


def build_outputs(root):
    verify_provenance(root)
    catalogs = {name: read(root, SPEC / "data" / (name + ".json")) for name in DATA_NAMES}
    require({p.stem for p in (root / SPEC / "data").glob("*.json")} == set(DATA_NAMES), "Unknown data file")
    require(all(d["schema_version"] == 8 for d in catalogs.values()), "Mixed catalog schemas")
    schema = read(root, SPEC / "schemas/catalog.schema.json")
    for name in ("abilities", "items"):
        validate_schema(catalogs[name], schema, name)
    groups = {"abilities": ("abilities", 52), "items": ("items", 80),
              "acquisition": ("recipes", 20), "class_traits": ("traits", 9),
              "environment": ("reactions", 10), "operation_contracts": ("operations", 50),
              "modifier_contracts": ("modifiers", 18)}
    for name, (key, count) in groups.items():
        require(len(rows_by_id(catalogs[name][key], name)) == count, f"{name}: count mismatch")

    old = {name: read(root, f"docs/v07/data/{name}.json") for name in
           ("abilities", "items", "acquisition", "starter_kits", "operation_contracts")}
    abilities = rows_by_id(catalogs["abilities"]["abilities"], "abilities")
    items = rows_by_id(catalogs["items"]["items"], "items")
    old_abilities = rows_by_id(old["abilities"]["abilities"], "retained abilities")
    old_items = rows_by_id(old["items"]["items"], "retained items")
    require(old_abilities.keys() <= abilities.keys() and old_items.keys() <= items.keys(), "Removed stable IDs")
    old_recipes = rows_by_id(old["acquisition"]["recipes"], "retained recipes")
    recipes = rows_by_id(catalogs["acquisition"]["recipes"], "recipes")
    require(old_recipes.keys() <= recipes.keys(), "Removed recipe IDs")
    require(all(recipes[k] == v for k, v in old_recipes.items()), "Changed retained recipe")

    ops = rows_by_id(catalogs["operation_contracts"]["operations"], "operations")
    for row in abilities.values():
        require(all(e["op"] in ops for e in row["effects"]), f"Unknown operation: {row['id']}")
    for row in items.values():
        require(row["active"] is None or row["active"]["op"] in ops, f"Unknown item operation: {row['id']}")

    old_adapters = read(root, "docs/v07/B70_ADAPTERS.json")
    skills = native_enum(root, "Source/Lockdown/Public/Development/LDCharacterBuild.h", "ELDSkill")
    skill_ids = dict(old_adapters["ability_wire_ids"])
    require(len(skills) == len(skill_ids) == 38, "Native legacy skill inventory changed")
    for name in ("None", "Step", "Ambush", "Spark", "Patch", "Pickpocket", "Parley", "Analyze", "Survey", "Heave"):
        stable = "none" if name == "None" else "legacy_" + name.lower()
        require(skills[name] == skill_ids[stable], f"Renumbered legacy skill: {name}")
    for name in old_abilities:
        native = "".join(part.title() for part in name.split("_"))
        require(skills[native] == skill_ids[name], f"Renumbered skill: {name}")
    for name in abilities:
        if name not in skill_ids:
            skill_ids[name] = len(skill_ids)

    item_enums = native_enum(root, "Source/Lockdown/Public/Inventory/LDInventoryTypes.h", "ELDItemKind")
    source = (root / "Source/Lockdown/Private/Inventory/LDEquipmentCatalog.cpp").read_text()
    item_names = dict(re.findall(r'\{TEXT\("([a-z_]+)"\),ELDItemKind::(\w+)\}', source))
    require(set(item_names) == set(old_items), "Native item adapter changed")
    item_ids = {name: item_enums[native] for name, native in item_names.items()}
    next_id = len(item_enums)
    for name in items:
        if name not in item_ids:
            item_ids[name] = next_id
            next_id += 1
    require(max(skill_ids.values()) < 256 and max(item_ids.values()) < 256, "Reserved ID capacity exceeded")

    traits = catalogs["class_traits"]["traits"]
    trait_by_class = {t["class_id"]: t for t in traits}
    require(len(trait_by_class) == 9 and all(not t["breakable"] and not t["signature_exclusive"] for t in traits),
            "Exactly one nonbreakable trait per class required")
    legacy_skills = {"laborer": ["legacy_heave", "legacy_patch"],
                     "rogue": ["legacy_pickpocket", "legacy_survey"],
                     "scholar": ["legacy_spark", "legacy_analyze"]}
    classes = []
    starters = catalogs["starter_kits"]["templates"]
    names = list(legacy_skills) + [s["id"] for s in starters]
    require(set(names) == set(trait_by_class) and len(names) == 9, "Unmapped class")
    for preset, name in enumerate(names):
        kit = next((s for s in starters if s["id"] == name), None)
        learned = legacy_skills[name] if kit is None else kit["skills"]
        require(set(learned) <= skill_ids.keys(), f"Unresolved starter: {name}")
        trait = trait_by_class[name]
        require(kit is None or kit["trait_id"] == trait["id"], f"Trait mismatch: {name}")
        signatures = learned if name in ("rogue", "scholar") else ["legacy_heave"] if name == "laborer" else trait["signature_suggestions"]
        require(set(signatures) <= skill_ids.keys(), f"Unresolved signature: {name}")
        classes.append({"id": name, "preset": preset, "trait_id": trait["id"],
                        "starting_skills": learned, "signature_suggestions": signatures,
                        "runtime_enabled": False})
    resolved = copy.deepcopy(catalogs)
    for trait in resolved["class_traits"]["traits"]:
        trait["signature_suggestions"] = next(c["signature_suggestions"] for c in classes if c["trait_id"] == trait["id"])

    adapters = {
        "schema_version": 8, "milestone": "B80", "stages": STAGES,
        "runtime_baseline": "ddf7be5aaf74cbf8be013308e130233bcb1a410c",
        "audit_head": "7d9d66dfe77407f2105f2552b297709a0366146a",
        "profile": {"id": "v08_integration", "runtime_enabled": False,
                    "reason": "B80 import only; native profile/handlers/serializers start in B81"},
        "ability_ids": skill_ids, "item_ids": item_ids,
        "id_policy": "Retained native IDs preserved; new IDs reserved only, not implemented enums/operations",
        "legacy_items_outside_80": {n: i for n, i in item_enums.items() if n not in item_names.values()},
        "book_policy": "One Book wrapper per learned definition, weight1/Rank1; preserve distinct legacy wrappers. No gear-action books or grants.",
        "classes": classes,
        "legacy_starts": old_adapters["legacy_starts"],
        "legacy_skill_adapter": {**old_adapters["legacy_skill_adapter"],
                                 "learning": "Preserve actual legacy learning/costs; six shared slots in B81, no innate shortcut"},
        "recipe_ids": list(recipes),
        "operations": [{"id": name, "runtime_enabled": False,
                        "disposition": "Retained handler needs v08 revalidation" if name in
                        {o["id"] for o in old["operation_contracts"]["operations"]} else "New handler pending"} for name in ops],
        "binding_migration": {"weapon": "main_hand", "offhand": "off_hand", "body": "torso",
                              "feet": "feet", "charm_a": "accessory_1", "charm_b": "accessory_2"},
        "migration_policy": "Fresh match/UI boundary only; preview/revalidate full draft; stable identity/cooldown/lineage; conflicts unbound, no free instances. Prior quick consumables may fill accessory3/4 in reference order. Body active candidate uses shared slot6 only if free.",
        "wire_plan": {"schema": 8, "status": "Proposed; no wire8 accepted at B80",
                      "action_version": 4, "loadout_version": 2, "binding_mask_bits": 16,
                      "shared_actions": 6, "physical_bindings": 11, "accessories": 4,
                      "source_kinds": ["learned", "gear", "accessory", "basic", "world"],
                      "authority": "Derive source on server; bounded tagged refs/targets; validate match/life/sequence/build/inventory/item revisions and digest. Update owner views and requests together; preserve wire3/version1 regression."},
        "catalog_counts": {name: count for name, (_, count) in groups.items()},
        "retained_counts": {"abilities": 28, "items": 40, "recipes": 12},
    }
    hashes = {f"{name}.json": sha((root / SPEC / "data" / f"{name}.json").read_bytes()) for name in DATA_NAMES}
    payload = {"schema_version": 8, "milestone": "B80", "profile_id": "v08_integration",
               "runtime_enabled": False, "enabled_abilities": [], "enabled_items": [],
               "source_hashes": hashes, "catalogs": resolved, "adapters": adapters}
    raw = encode(payload)
    manifest = {"schema_version": 8, "milestone": "B80", "runtime_enabled": False,
                "payload": "catalog.json", "sha256": sha(raw), "source_hashes": hashes,
                "normalization": "Legacy trait signature markers resolved through audited native starter IDs; source JSON remains intact.",
                "activation": "No native loader/Build.cs dependency. Import is not engine acceptance; B81 must explicitly implement and enable supported definitions."}
    return {Path("Content/Lockdown/Data/V08/catalog.json"): raw,
            Path("Content/Lockdown/Data/V08/ImportManifest.json"): encode(manifest),
            SPEC / "B80_ADAPTERS.json": encode(adapters)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    # Run all reference/reachability/operation checks before writing any output.
    subprocess.run([sys.executable, "-B", str(ROOT / SPEC / "tools/check_spec.py")], check=True)
    outputs = build_outputs(ROOT)
    for path, raw in outputs.items():
        target = ROOT / path
        if args.check:
            require(target.is_file() and target.read_bytes() == raw, f"Stale/missing output: {path}")
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(raw)
    print("PASS B80 schema8 import: 52 abilities / 80 items / 20 recipes / 9 traits / 10 reactions; runtime disabled")


if __name__ == "__main__":
    main()
