#!/usr/bin/env python3
"""Focused B80 importer rejection tests. These are not Unreal gameplay tests."""
import json
import re
from pathlib import Path
import shutil
import tempfile
import unittest

import SyncV08Data as sync


class ImportBoundaries(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="lockdown-b80-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        shutil.copytree(sync.ROOT / "docs/v08", self.root / "docs/v08")
        shutil.copytree(sync.ROOT / "docs/v07/data", self.root / "docs/v07/data")
        for name in ("docs/v07/B70_ADAPTERS.json", "docs/v07/evidence/B77/Audit.json",
                     "Source/Lockdown/Public/Development/LDCharacterBuild.h",
                     "Source/Lockdown/Public/Inventory/LDInventoryTypes.h",
                     "Source/Lockdown/Private/Inventory/LDEquipmentCatalog.cpp"):
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(sync.ROOT / name, target)
        # B80 is a frozen import fixture, not the current runtime generator.
        # Preserve the actual retained prefix/values (including adversarial edits
        # below), while removing later appended native identities in this copy.
        for name, last in (("Development/LDCharacterBuild.h", "GateWeave"),
                           ("Inventory/LDInventoryTypes.h", "Heartbreaker")):
            path = self.root / "Source/Lockdown/Public" / name
            path.write_text(re.sub(r"(" + last + r")\s*,[^}]+", r"\1 ", path.read_text(), count=1))

    def edit_catalog(self, name, edit):
        path = sync.SPEC / "data" / (name + ".json")
        data = sync.read(self.root, path)
        edit(data)
        (self.root / path).write_bytes(sync.encode(data))
        # Explicitly re-adopt test input so rejection tests reach semantic gates.
        manifest_path = sync.SPEC / "SOURCE_MANIFEST.json"
        manifest = sync.read(self.root, manifest_path)
        relative = "data/" + name + ".json"
        manifest["adopted_package"][relative] = sync.sha((self.root / path).read_bytes())
        manifest["adoption_edits"].append(relative)
        (self.root / manifest_path).write_bytes(sync.encode(manifest))

    def test_deterministic_disabled_contract(self):
        first = sync.build_outputs(self.root)
        self.assertEqual(first, sync.build_outputs(self.root))
        payload = json.loads(first[Path("Content/Lockdown/Data/V08/catalog.json")])
        self.assertFalse(payload["runtime_enabled"])
        self.assertEqual((payload["enabled_abilities"], payload["enabled_items"]), ([], []))
        adapters = payload["adapters"]
        self.assertEqual(adapters["ability_ids"]["anchor_bash"], 10)
        self.assertEqual(adapters["ability_ids"]["gate_weave"], 37)
        self.assertEqual(len(adapters["ability_ids"]), 62)
        self.assertEqual(len(set(adapters["item_ids"].values())), 80)
        for trait in payload["catalogs"]["class_traits"]["traits"]:
            self.assertTrue(set(trait["signature_suggestions"]) <= adapters["ability_ids"].keys())

    def test_changed_adopted_bytes_reject(self):
        with (self.root / "docs/v08/data/items.json").open("ab") as handle:
            handle.write(b"\n")
        with self.assertRaisesRegex(ValueError, "Changed adopted input"):
            sync.build_outputs(self.root)

    def test_unknown_operation_reject(self):
        self.edit_catalog("abilities", lambda d: d["abilities"][-1]["effects"][0].update(op="fake_damage"))
        with self.assertRaises(ValueError):
            sync.build_outputs(self.root)

    def test_duplicate_id_reject(self):
        self.edit_catalog("items", lambda d: d["items"][-1].update(id=d["items"][-2]["id"]))
        with self.assertRaisesRegex(ValueError, "duplicate IDs"):
            sync.build_outputs(self.root)

    def test_boolean_integer_field_reject(self):
        self.edit_catalog("abilities", lambda d: d["abilities"][-1].update(mana_cost=True))
        with self.assertRaises(ValueError):
            sync.build_outputs(self.root)

    def test_renumbered_native_id_reject(self):
        path = self.root / "Source/Lockdown/Public/Development/LDCharacterBuild.h"
        path.write_text(path.read_text().replace("AnchorBash, BraceWall", "BraceWall, AnchorBash"))
        with self.assertRaisesRegex(ValueError, "Renumbered skill"):
            sync.build_outputs(self.root)

    def test_unknown_class_reject(self):
        self.edit_catalog("class_traits", lambda d: d["traits"][-1].update(class_id="unknown"))
        with self.assertRaisesRegex(ValueError, "Unmapped class"):
            sync.build_outputs(self.root)

    def test_retained_recipe_mutation_reject(self):
        self.edit_catalog("acquisition", lambda d: d["recipes"][0].update(fee_gold=999))
        with self.assertRaisesRegex(ValueError, "Changed retained recipe"):
            sync.build_outputs(self.root)

    def test_required_active_binding_reject(self):
        self.edit_catalog("items", lambda d: d["items"][-1].pop("activation_binding"))
        with self.assertRaises(ValueError):
            sync.build_outputs(self.root)


if __name__ == "__main__":
    unittest.main(verbosity=2)
