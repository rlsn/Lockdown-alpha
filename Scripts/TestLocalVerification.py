import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import CheckRuleReport as rules
import LocalVerification as verify


class LocalVerificationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def write(self, name, text=""):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path

    def test_python_transitive_imports_data_and_unrelated_test(self):
        self.write("Scripts/Reader.py", 'from pathlib import Path\nDATA = Path("SourceData", "Demo")\n')
        self.write("Scripts/Wrapper.py", "import Reader\n")
        self.write("Scripts/TestReader.py", "import Wrapper\n")
        self.write("Scripts/TestOther.py", "import unittest\n")
        graph = verify.Dependencies(self.root)
        for change in ("Scripts/Reader.py", "SourceData/Demo/content.json"):
            self.assertEqual(graph.python_tests([change]), {"TestReader.py": [change]})
        self.assertEqual(graph.python_tests(["SourceData/Audio/cues.json"]), {})

    def test_deleted_module_and_dynamic_script_reference(self):
        self.write("Scripts/TestGone.py", 'import Gone\n')
        self.write("Scripts/TestTool.py", 'COMMAND = "Tool.py"\n')
        graph = verify.Dependencies(self.root)
        self.assertIn("TestGone.py", graph.python_tests(["Scripts/Gone.py"]))
        self.assertIn("TestTool.py", graph.python_tests(["Scripts/Tool.py"]))

    def test_shared_import_analysis_retains_relative_deleted_edges_and_cycles(self):
        self.write("Scripts/TestImports.py", 'import Wrapper as alias\nfrom Wrapper.tools import item\n')
        self.write("Scripts/Wrapper.py", 'import TestImports\nfrom .Gone import item\nimport json\n')
        graph = verify.Dependencies(self.root)
        self.assertIn("TestImports.py", graph.python_tests(["Scripts/Gone.py"]))
        self.assertNotIn("Scripts/json.py", graph.python["Scripts/Wrapper.py"])

    def test_reverse_include_links_implementation_and_cycles(self):
        self.write("Source/Game/Public/Combat.h", '#include "Types.h"\n')
        self.write("Source/Game/Public/Types.h", '#include "Combat.h"\n')
        self.write("Source/Game/Private/Combat.cpp", '#include "Combat.h"\n#include "PrivateOnly.h"\n')
        self.write("Source/Game/Private/PrivateOnly.h")
        self.write("Source/Tests/Private/CombatTests.cpp",
                   '#include "Combat.h"\nIMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest, "Lockdown.Rules.Combat.Damage", Flags)\n')
        self.write("Source/Tests/Private/OtherTests.cpp",
                   '#include "PrivateOnly.h"\nIMPLEMENT_SIMPLE_AUTOMATION_TEST(FOther, "Lockdown.Rules.Other", Flags)\n')
        graph = verify.Dependencies(self.root)
        self.assertEqual(graph.rule_tests(["Source/Game/Private/Combat.cpp"]),
                         {"Lockdown.Rules.Combat.Damage": ["Source/Game/Private/Combat.cpp"]})
        self.assertIn("Lockdown.Rules.Combat.Damage", graph.rule_tests(["Source/Game/Public/Types.h"]))

    def test_broad_selection_defers_instead_of_running_all(self):
        self.write("Scripts/Common.py")
        for n in range(14):
            self.write(f"Scripts/Test{n}.py", "import Common\n")
        plan = verify.make_plan(self.root, ["Scripts/Common.py"], rules="Auto")
        self.assertFalse(any(step["kind"] == "python" for step in plan["steps"]))
        self.assertTrue(any("CI" in reason for reason in plan["deferred"]))
        plan = verify.make_plan(self.root, [], filters=["Lockdown.Rules"])
        self.assertFalse(any(step["kind"] == "rules" for step in plan["steps"]))
        self.assertTrue(any("CI" in reason for reason in plan["deferred"]))

    def test_selected_import_rule_is_not_silently_skipped(self):
        plan = verify.make_plan(self.root, [], filters=["Lockdown.Rules.DemoContent.TypedProjection"])
        command = plan["steps"][0]["command"]
        self.assertEqual(command[command.index("-ImportTests") + 1], "Run")
        self.assertIn("Lockdown.Rules.DemoContent.TypedProjection", command[-1])

    def test_unknown_module_reports_gap_without_full_fallback(self):
        self.write("Scripts/Unknown.py")
        plan = verify.make_plan(self.root, ["Scripts/Unknown.py"])
        self.assertFalse(plan["steps"])
        self.assertTrue(any("No statically reachable" in reason for reason in plan["deferred"]))

    def test_fingerprint_covers_membership_and_missing_inputs(self):
        path = self.write("SourceData/Demo/a.json", "one")
        first = verify.fingerprint(self.root, ["SourceData"])
        self.assertEqual(first, verify.fingerprint(self.root, ["SourceData"]))
        self.write("SourceData/Demo/b.json", "two")
        self.assertNotEqual(first, verify.fingerprint(self.root, ["SourceData"]))
        (self.root / "SourceData/Demo/b.json").unlink()
        self.assertEqual(first, verify.fingerprint(self.root, ["SourceData"]))
        path.unlink()
        self.assertNotEqual(first, verify.fingerprint(self.root, ["SourceData"]))

    def test_local_engine_binding_invalidates_native_check_cache(self):
        plan = verify.make_plan(self.root, ["Source/Game/Private/Combat.cpp"],
                                filters=["Lockdown.Rules.DemoMatchIdentity"])
        for step in plan["steps"]:
            with self.subTest(step=step["id"]):
                first = verify.cache_key(self.root, step)
                self.write(".env", 'LD_UE_ROOT="C:\\Engine One"\n')
                second = verify.cache_key(self.root, step)
                self.assertNotEqual(first, second)
                self.write(".env", 'LD_UE_ROOT="C:\\Engine Two"\n')
                self.assertNotEqual(second, verify.cache_key(self.root, step))
                (self.root / ".env").unlink()

    def test_cache_requires_evidence_outputs_and_identical_inputs(self):
        self.write("input.txt", "a")
        output = self.write("Binaries/tool.dll", "binary")
        step = {"id": "test", "kind": "test", "reason": [], "inputs": ["input.txt"],
                "outputs": ["Binaries"], "command": [sys.executable, "-c", "print('passed')"]}
        with mock.patch.object(verify, "environment_identity", return_value={"python": "fixture"}):
            result = verify.run_step(self.root, step)
            self.assertTrue(verify.cached_result(self.root, step))
            self.assertEqual(verify.run_step(self.root, step)["log"], result["log"])
            self.write("unrelated.txt", "unrelated")
            self.assertTrue(verify.cached_result(self.root, step))
            self.write("input.txt", "changed")
            self.assertIsNone(verify.cached_result(self.root, step))
            self.write("input.txt", "a")
            self.assertTrue(verify.cached_result(self.root, step))
            output.unlink()
            self.assertIsNone(verify.cached_result(self.root, step))
            output.write_text("binary", encoding="utf-8")
            (self.root / result["log"]).unlink()
            self.assertIsNone(verify.cached_result(self.root, step))

    def test_mid_run_input_change_is_not_cached(self):
            self.write("input.txt", "a")
            step = {"id": "test", "kind": "test", "reason": [], "inputs": ["input.txt"], "outputs": [],
                    "command": [sys.executable, "-c", "from pathlib import Path; Path('input.txt').write_text('b')"]}
            with self.assertRaisesRegex(RuntimeError, "Inputs changed"):
                verify.run_step(self.root, step)
            self.assertIsNone(verify.cached_result(self.root, step))

    @unittest.skipUnless(shutil.which("pwsh"), "PowerShell 7 required")
    def test_full_ci_mode_reaches_engine_with_unchanged_gate_options(self):
            runner = Path(__file__).with_name("RunTests.ps1")
            self.write("Scripts/RunTests.ps1", runner.read_text(encoding="utf-8"))
            self.write("Scripts/Common.ps1",
                       'function python { $global:LASTEXITCODE=0; \'{"partial":false}\' }\n'
                       'function Resolve-LDEngine { throw "EXPLICIT_ENGINE_REACHED" }\n')
            for mode in ("Run", "Skip"):
                done = subprocess.run(["pwsh", "-NoProfile", "-File", str(self.root / "Scripts/RunTests.ps1"),
                                       "-Full", "-ImportTests", mode],
                                      capture_output=True, text=True, encoding="utf-8", timeout=30)
                self.assertNotEqual(done.returncode, 0)
                self.assertIn("EXPLICIT_ENGINE_REACHED", done.stderr)

    def test_force_failure_invalidates_prior_success(self):
        self.write("input.txt", "a")
        step = {"id": "test", "kind": "test", "reason": [], "inputs": ["input.txt"], "outputs": [],
                "command": [sys.executable, "-c", "print('passed')"]}
        verify.run_step(self.root, step)
        self.assertTrue(verify.cached_result(self.root, step))
        with mock.patch.object(verify, "execute", side_effect=RuntimeError("failed")):
            with self.assertRaisesRegex(RuntimeError, "failed"):
                verify.run_step(self.root, step, force=True)
        self.assertIsNone(verify.cached_result(self.root, step))

    def run_native_to_engine_boundary(self, *args):
        source = Path(__file__).parent
        self.write("Scripts/RunTests.ps1", (source / "RunTests.ps1").read_text(encoding="utf-8"))
        self.write("Scripts/CheckRuleReport.py",
                   f"import runpy\nrunpy.run_path({str(source / 'CheckRuleReport.py')!r}, run_name='__main__')\n")
        self.write("Scripts/Common.ps1", 'function Resolve-LDEngine { throw "TEST_ENGINE_BOUNDARY_REACHED" }\n')
        return subprocess.run(["pwsh", "-NoProfile", "-File", str(self.root / "Scripts/RunTests.ps1"), *args],
                              capture_output=True, text=True, encoding="utf-8", timeout=30)

    @unittest.skipUnless(shutil.which("pwsh"), "PowerShell 7 required")
    def test_combined_prefixes_cannot_select_complete_run_or_skip_gate(self):
        prefixes = sorted({".".join(name.split(".")[:3]) for name in rules.REQUIRED_TESTS})
        for mode in ("Run", "Skip"):
            with self.subTest(mode=mode):
                selected = rules.select_tests(prefixes, skip_import=mode == "Skip")
                self.assertEqual(selected, rules.REQUIRED_TESTS - (rules.IMPORT_TESTS if mode == "Skip" else set()))
                done = self.run_native_to_engine_boundary("-Filter", ",".join(prefixes), "-ImportTests", mode)
                self.assertNotEqual(done.returncode, 0)
                self.assertIn("choose explicit -Full", done.stderr)
                self.assertNotIn("TEST_ENGINE_BOUNDARY_REACHED", done.stderr)

    @unittest.skipUnless(shutil.which("pwsh"), "PowerShell 7 required")
    def test_specific_partial_prefix_still_reaches_engine_boundary(self):
        for mode in ("Run", "Skip"):
            with self.subTest(mode=mode):
                done = self.run_native_to_engine_boundary("-Filter", "Lockdown.Rules.DemoMatchIdentity",
                                                         "-ImportTests", mode)
                self.assertNotEqual(done.returncode, 0)
                self.assertIn("TEST_ENGINE_BOUNDARY_REACHED", done.stderr)

    @unittest.skipUnless(shutil.which("pwsh"), "PowerShell 7 required")
    def test_unfiltered_native_refuses_before_loading_engine(self):
        runner = Path(__file__).with_name("RunTests.ps1")
        for args in ([], ["-ImportTests", "Run"], ["-Filter", "Lockdown.Rules"],
                     ["-Full", "-Filter", "Lockdown.Rules.DemoGrowth"]):
            done = subprocess.run(["pwsh", "-NoProfile", "-File", str(runner), *args],
                                  capture_output=True, text=True, encoding="utf-8", timeout=30)
            self.assertNotEqual(done.returncode, 0)
            self.assertIn("explicit", done.stderr.lower())
            self.assertNotIn("Resolve-LDEngine", done.stderr)


if __name__ == "__main__":
    unittest.main()
