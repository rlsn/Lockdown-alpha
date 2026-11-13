import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import CIChangeScope


class NativeModulesTest(unittest.TestCase):
    def test_current_runner_closure(self):
        modules = CIChangeScope.native_python_modules(CIChangeScope.SCRIPTS)
        # The fixture cache uses CIChangeScope's import closure, so the decision script is itself a native input.
        for name in ('ExportDemoNativeFixtures', 'CheckRuleReport', 'DemoContent', 'DemoWorld', 'CatalogTools',
                     'SyncDemoContentData', 'SyncDemoWorldData', 'DemoContentTestFixtures', 'CIChangeScope'):
            self.assertIn(name, modules)
        for name in ('SummarizePlaytestLog', 'MeasureDemoArt', 'TestRuleReport'):
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

    def test_documentation_only(self):
        for paths in (['README.md'], ['AGENTS.md', 'docs/DEVELOPMENT.md'],
                      ['docs/diagram.svg', 'docs/nested/data.json'], ['Source/Notes.md'],
                      ['docs\\systems\\guide.md']):
            with self.subTest(paths=paths):
                self.assertEqual(CIChangeScope.classify(paths, self.native), ('docs', []))

    def test_workflow_only_still_runs_python(self):
        self.assertEqual(CIChangeScope.classify(
            ['README.md', '.github/workflows/pr-check.yml'], self.native), ('python', []))

    def test_documentation_rename_to_code_needs_native(self):
        self.assertEqual(CIChangeScope.classify(
            ['docs/example.cpp', 'Source/Example.cpp'], self.native), ('native', ['Source/Example.cpp']))

    def test_python_only(self):
        scope, forced = CIChangeScope.classify(
            ['Scripts/SummarizePlaytestLog.py', 'Scripts/TestSummarizePlaytestLog.py', 'docs/DEVELOPMENT.md',
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


class ImportInputsTest(unittest.TestCase):
    native = {'DemoContent', 'ExportDemoNativeFixtures'}

    def test_current_importer_closure(self):
        importer = CIChangeScope.importer_sources(CIChangeScope.ROOT)
        for path in ('Source/Lockdown/Private/Development/LDDemoContentCatalog.cpp',
                     'Source/Lockdown/Private/Development/LDDemoContentValidation.cpp',
                     'Source/Lockdown/Public/Development/LDDemoContentDefinitions.h',
                     'Source/Lockdown/Public/Development/LDDemoContentIds.h',
                     'Source/Lockdown/Private/Region/LDMapDefinition.cpp',
                     'Source/Lockdown/Public/Region/LDMapGeometry.h',
                     'Source/Lockdown/Private/World/LDDemoWorldContract.h',
                     'Source/Lockdown/Private/Tests/LDDemoContentCatalogTests.cpp'):
            self.assertIn(path, importer)
        for path in ('Source/Lockdown/Private/AI/LDOpponentPolicy.cpp', 'Source/Lockdown/Private/Region/LDMapRegistry.cpp',
                     'Source/Lockdown/Private/Tests/LDOpponentPolicyTests.cpp'):
            self.assertNotIn(path, importer)

    def test_follows_includes_and_own_cpp_but_not_its_includes(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            files = {
                'Source/Lockdown/Public/Development/LDDemoContentCatalog.h': '#include "Shared/Types.h"\n',
                'Source/Lockdown/Private/Development/LDDemoContentCatalog.cpp': '#include "LocalHelper.h"\n',
                'Source/Lockdown/Private/Development/LocalHelper.h': '',
                'Source/Lockdown/Public/Shared/Types.h': '#include "CoreMinimal.h"\n',
                'Source/Lockdown/Private/Shared/Types.cpp': '#include "Gameplay/Big.h"\n',
                'Source/Lockdown/Public/Gameplay/Big.h': '',
                'Source/Lockdown/Private/Gameplay/Other.cpp': '#include "Shared/Types.h"\n',
            }
            for relative, text in files.items():
                (root / relative).parent.mkdir(parents=True, exist_ok=True)
                (root / relative).write_text(text, encoding='utf-8')
            self.assertEqual(CIChangeScope.importer_sources(root), {
                'Source/Lockdown/Public/Development/LDDemoContentCatalog.h',
                'Source/Lockdown/Private/Development/LDDemoContentCatalog.cpp',
                'Source/Lockdown/Private/Development/LocalHelper.h',
                'Source/Lockdown/Public/Shared/Types.h',
                'Source/Lockdown/Private/Shared/Types.cpp',
            })
            (root / 'Source/Lockdown/Public/Development/LDDemoContentCatalog.h').unlink()
            (root / 'Source/Lockdown/Private/Development/LDDemoContentCatalog.cpp').unlink()
            with self.assertRaises(RuntimeError):
                CIChangeScope.importer_sources(root)

    def test_import_inputs(self):
        importer = {'Source/Lockdown/Private/Development/LDDemoContentCatalog.cpp'}
        for path in ('SourceData/Demo/content.json', 'SourceData/World/demo_world.json',
                     'Content/Lockdown/Data/Demo/manifest.json', 'Config/DefaultEngine.ini', 'Lockdown.uproject',
                     'Source/Lockdown/Lockdown.Build.cs', 'Scripts/DemoContent.py', 'Scripts/RunTests.ps1',
                     'Scripts/CIChangeScope.py', 'Source\\Lockdown\\Private\\Development\\LDDemoContentCatalog.cpp'):
            self.assertEqual(len(CIChangeScope.import_inputs(['docs/x.md', path], self.native, importer)), 1, path)
        self.assertEqual(CIChangeScope.import_inputs(
            ['Source/Lockdown/Private/AI/LDOpponentPolicy.cpp', 'Scripts/RunDemoOpponentCheck.ps1',
             'Scripts/SummarizePlaytestLog.py', 'Content/Lockdown/Generated/Maps/L_Demo.umap', 'docs/DEVELOPMENT.md'],
            self.native, importer), [])

    def test_deleted_or_renamed_importer_source_runs_the_group(self):
        # The closure is computed on the new tree, where a deleted or renamed-away importer file no longer appears.
        importer = {'Source/Lockdown/Private/Development/LDDemoContentCatalog.cpp'}
        old = 'Source/Lockdown/Private/Development/LDDemoContentOld.h'
        self.assertEqual(CIChangeScope.import_inputs([old], self.native, importer, {old}), [old])
        renamed_to = 'Source/Lockdown/Private/Development/Renamed.h'
        self.assertEqual(CIChangeScope.import_inputs([old, renamed_to], self.native, importer, {old}), [old])
        self.assertEqual(CIChangeScope.import_inputs(['docs/gone.md', 'Scripts/Gone.ps1x'], self.native, importer,
                                                     {'docs/gone.md'}), [])

    def test_changed_paths_reports_deletions_and_renames(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            def run(*args):
                subprocess.run(['git', '-C', str(root), *args], check=True, capture_output=True)
            run('init', '-q')
            run('config', 'user.email', 't@example.com')
            run('config', 'user.name', 't')
            for name in ('Kept.h', 'Gone.h', 'Moved.h'):
                (root / 'Source' / name).parent.mkdir(parents=True, exist_ok=True)
                (root / 'Source' / name).write_text(name, encoding='utf-8')
            run('add', '-A')
            run('commit', '-q', '-m', 'base')
            (root / 'Source' / 'Gone.h').unlink()
            (root / 'Source' / 'Moved.h').rename(root / 'Source' / 'MovedNew.h')
            run('add', '-A')
            run('commit', '-q', '-m', 'head')
            with mock.patch.object(CIChangeScope, 'ROOT', root):
                paths, deleted = CIChangeScope.changed_paths('HEAD~1', 'HEAD')
                self.assertEqual(paths, ['Source/Gone.h', 'Source/Moved.h', 'Source/MovedNew.h'])
                self.assertEqual(deleted, {'Source/Gone.h', 'Source/Moved.h'})
                (root / 'Source' / 'Kept.h').unlink()
                (root / 'Source' / 'Untracked.h').write_text('x', encoding='utf-8')
                paths, deleted = CIChangeScope.changed_paths('HEAD', None)
                self.assertEqual((paths, deleted), (['Source/Kept.h', 'Source/Untracked.h'], {'Source/Kept.h'}))


if __name__ == '__main__':
    unittest.main()
