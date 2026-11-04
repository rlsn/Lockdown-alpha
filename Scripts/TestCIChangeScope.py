import tempfile
import unittest
from pathlib import Path

import CIChangeScope


class NativeModulesTest(unittest.TestCase):
    def test_current_runner_closure(self):
        modules = CIChangeScope.native_python_modules(CIChangeScope.SCRIPTS)
        for name in ('ExportDemoNativeFixtures', 'CheckRuleReport', 'DemoContent', 'DemoWorld', 'CatalogTools',
                     'SyncDemoContentData', 'SyncDemoWorldData', 'DemoContentTestFixtures'):
            self.assertIn(name, modules)
        for name in ('CheckEconomyEvidence', 'MeasureDemoArt', 'TestRuleReport', 'CIChangeScope'):
            self.assertNotIn(name, modules)

    def test_follows_imports_and_rejects_missing_script(self):
        with tempfile.TemporaryDirectory() as temp:
            scripts = Path(temp)
            (scripts / 'RunTests.ps1').write_text("& python -B (Join-Path $PSScriptRoot 'A.py')\n", encoding='utf-8')
            (scripts / 'A.py').write_text('import json\nfrom B import thing\n', encoding='utf-8')
            (scripts / 'B.py').write_text('import C\n', encoding='utf-8')
            (scripts / 'C.py').write_text('', encoding='utf-8')
            (scripts / 'D.py').write_text('import A\n', encoding='utf-8')
            self.assertEqual(CIChangeScope.native_python_modules(scripts), {'A', 'B', 'C'})
            (scripts / 'A.py').unlink()
            with self.assertRaises(RuntimeError):
                CIChangeScope.native_python_modules(scripts)

    def test_runner_without_python_is_refused(self):
        with tempfile.TemporaryDirectory() as temp:
            (Path(temp) / 'RunTests.ps1').write_text('Write-Host none\n', encoding='utf-8')
            with self.assertRaises(RuntimeError):
                CIChangeScope.native_python_modules(Path(temp))


class ClassifyTest(unittest.TestCase):
    native = {'DemoContent', 'ExportDemoNativeFixtures'}

    def test_python_only(self):
        scope, forced = CIChangeScope.classify(
            ['Scripts/CheckEconomyEvidence.py', 'Scripts/TestEconomyEvidence.py', 'docs/DEVELOPMENT.md',
             'README.md', '.github/workflows/pr-check.yml'], self.native)
        self.assertEqual((scope, forced), ('python', []))

    def test_native_inputs(self):
        for path in ('Source/Lockdown/Private/Tests/LDArtRulesTests.cpp', 'SourceData/Demo/content.json',
                     'Config/DefaultEngine.ini', 'Content/Lockdown/Generated/Maps/L_Demo.umap',
                     'Scripts/RunTests.ps1', 'Scripts/DemoContent.py', 'Scripts/Sub/Tool.py', 'Lockdown.uproject'):
            scope, forced = CIChangeScope.classify(['docs/x.md', path], self.native)
            self.assertEqual((scope, forced), ('native', [path]), path)

    def test_windows_separators(self):
        self.assertEqual(CIChangeScope.classify(['Scripts\\DemoContent.py'], self.native)[0], 'native')


if __name__ == '__main__':
    unittest.main()
