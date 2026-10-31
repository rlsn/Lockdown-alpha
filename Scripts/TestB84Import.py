"""Validate the current catalog using only source data and native declarations."""
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

import SyncB84Data as compiler
from CatalogTools import encode, read_json, validate_schema


class CatalogImport(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        shutil.copytree(compiler.ROOT / compiler.SOURCE, self.root / compiler.SOURCE)
        for name in ("Source/Lockdown/Public/Development/LDCharacterBuild.h",
                     "Source/Lockdown/Public/Inventory/LDInventoryTypes.h",
                     "Source/Lockdown/Private/Inventory/LDEquipmentCatalog.cpp",
                     "Source/Lockdown/Private/Inventory/LDAcquisitionCatalog.cpp"):
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(compiler.ROOT / name, target)

    def edit(self, name, change):
        path = self.root / compiler.SOURCE / (name + ".json")
        value = read_json(path)
        change(value)
        path.write_bytes(encode(value))

    def assert_rejected(self, name, change, message=None):
        self.edit(name, change)
        with self.assertRaisesRegex(ValueError, message or "."):
            compiler.build_outputs(self.root)

    def stage(self, outputs):
        for name, data in outputs.items():
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)

    def test_reproducible_without_docs_or_archives(self):
        self.assertFalse((self.root / "docs").exists())
        first = compiler.build_outputs(self.root)
        self.assertEqual(first, compiler.build_outputs(self.root))
        compiler.check_outputs(compiler.ROOT, first)

    def test_explicit_utf8_under_legacy_system_encoding(self):
        expected = compiler.build_outputs(self.root)
        original = Path.read_text

        def legacy_read(path, encoding=None, errors=None, **kwargs):
            return original(path, encoding=encoding or "cp936", errors=errors, **kwargs)

        with patch.object(Path, "read_text", new=legacy_read):
            self.assertEqual(compiler.build_outputs(self.root), expected)
        combat = json.loads(expected[Path("Content/Lockdown/Data/V08/combat_b84.json")])
        self.assertTrue(any("\u2019" in ability["name"] for ability in combat["abilities"]))

    def test_unknown_operation(self):
        self.assert_rejected("abilities", lambda data: data["abilities"][-1]["effects"][0].update(op="fake_damage"))

    def test_duplicate_item(self):
        self.assert_rejected("items", lambda data: data["items"][-1].update(id=data["items"][-2]["id"]), "duplicate IDs")

    def test_duplicate_ability(self):
        self.assert_rejected("abilities", lambda data: data["abilities"][-1].update(id=data["abilities"][-2]["id"]), "duplicate IDs")

    def test_boolean_integer(self):
        self.assert_rejected("abilities", lambda data: data["abilities"][-1].update(mana_cost=True))

    def test_missing_binding(self):
        self.assert_rejected("items", lambda data: data["items"][-1].pop("activation_binding"))

    def test_mixed_schema(self):
        self.assert_rejected("environment", lambda data: data.update(schema_version=9), "Mixed catalog")

    def test_unknown_item(self):
        self.assert_rejected("items", lambda data: data["items"][-1].update(id="unknown_item"), "Unmapped")

    def test_duplicate_native_identity(self):
        self.assert_rejected("native_ids", lambda data: data["ability_ids"].update(anchor_bash=11), "duplicate native")

    def test_native_identity_cannot_be_boolean(self):
        self.assert_rejected("native_ids", lambda data: data["ability_ids"].update(legacy_step=True), "invalid native")

    def test_native_enum_renumbering(self):
        path = self.root / "Source/Lockdown/Public/Development/LDCharacterBuild.h"
        text = path.read_text(encoding="utf-8-sig")
        changed = text.replace("AnchorBash, BraceWall", "BraceWall, AnchorBash")
        self.assertNotEqual(text, changed)
        path.write_text(changed, encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Renumbered"):
            compiler.build_outputs(self.root)

    def test_unknown_recipe_reference(self):
        self.assert_rejected("acquisition", lambda data: data["recipes"][0].update(inputs={"unknown_resource": 1}),
                             "unknown recipe")

    def test_duplicate_recipe(self):
        self.assert_rejected("acquisition", lambda data: data["recipes"][-1].update(id=data["recipes"][0]["id"]),
                             "duplicate IDs")

    def test_unknown_recipe_output(self):
        self.assert_rejected("acquisition", lambda data: data["recipes"][0].update(output="unknown_output"),
                             "unknown recipe")

    def test_unknown_material_adapter(self):
        self.assert_rejected("acquisition", lambda data: data["materials"][0].update(id="unknown_material"),
                             "Unknown native material")

    def test_unknown_starting_skill(self):
        self.assert_rejected("starter_kits", lambda data: data["templates"][0].update(skills=["unknown_skill"]),
                             "unknown starting skill")

    def test_missing_input(self):
        (self.root / compiler.SOURCE / "abilities.json").unlink()
        with self.assertRaises(FileNotFoundError):
            compiler.build_outputs(self.root)

    def test_nonfinite_json_rejects(self):
        path = self.root / compiler.SOURCE / "environment.json"
        path.write_text('{"schema_version": 8, "value": NaN}', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "non-finite"):
            compiler.build_outputs(self.root)

    def test_unsupported_schema_keyword_rejects(self):
        with self.assertRaisesRegex(ValueError, "unsupported"):
            validate_schema({}, {"unsupported": True})

    def test_stale_payload_rejects(self):
        outputs = compiler.build_outputs(self.root)
        self.stage(outputs)
        compiler.check_outputs(self.root, outputs)
        self.edit("abilities", lambda data: data["abilities"][0].update(name="Changed fixture name"))
        changed = compiler.build_outputs(self.root)
        self.assertNotEqual(outputs, changed)
        with self.assertRaisesRegex(ValueError, "Stale"):
            compiler.check_outputs(self.root, changed)


if __name__ == "__main__":
    unittest.main()
