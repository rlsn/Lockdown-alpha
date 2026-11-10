import contextlib
import io
import json
import runpy
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

RUNNER = Path(__file__).resolve().parent / 'RunPythonTests.py'

FIXTURE = '''import unittest
from pathlib import Path

FLAG = Path(__file__).resolve().parent / 'fail.flag'


class Sample(unittest.TestCase):
    def test_passes(self):
        pass

    def test_flagged(self):
        self.assertFalse(FLAG.exists(), 'flagged failure detail')
'''


class RunPythonTestsTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)
        scripts = self.root / 'Scripts'
        scripts.mkdir()
        shutil.copy(RUNNER, scripts / 'RunPythonTests.py')
        (scripts / 'TestSample.py').write_text(FIXTURE, encoding='utf-8')
        self.flag = scripts / 'fail.flag'
        self.logs = self.root / 'Artifacts' / 'PythonTests'

    def run_runner(self, *args):
        done = subprocess.run([sys.executable, '-B', 'Scripts/RunPythonTests.py', '--jobs', '1', *args],
                              cwd=self.root, capture_output=True, text=True, encoding='utf-8', timeout=120)
        return done.returncode, done.stdout

    def recorded(self):
        return json.loads((self.logs / 'last-failed.json').read_text(encoding='utf-8'))['failed']

    def last_log(self, out):
        last = out.strip().splitlines()[-1]
        self.assertTrue(last.startswith('Log: '), out)
        return (self.root / last[len('Log: '):]).read_text(encoding='utf-8')

    def test_failure_is_logged_and_only_failed_tests_rerun(self):
        self.flag.touch()
        code, out = self.run_runner()
        self.assertEqual(code, 1, out)
        self.assertIn('Rerun only the failed tests', out)
        log = self.last_log(out)
        self.assertIn('flagged failure detail', log)
        self.assertIn('Ran 2 of 2 tests', log)
        self.assertEqual(self.recorded(), ['TestSample.Sample.test_flagged'])

        code, out = self.run_runner('--failed')
        self.assertEqual(code, 1, out)
        self.assertIn('PARTIAL RUN (--failed), NOT A GATE', out)
        self.assertIn('Ran 1 of 1 tests', out)

        self.flag.unlink()
        code, out = self.run_runner('--failed')
        self.assertEqual(code, 0, out)
        self.assertIn('Ran 1 of 1 tests', out)
        self.assertIn('PARTIAL RUN (--failed), NOT A GATE', out)
        self.assertEqual(self.recorded(), [])

        code, out = self.run_runner('--failed')
        self.assertEqual(code, 2, out)
        self.assertIn('no failed tests recorded', out)

    def test_failed_without_record_is_an_error(self):
        code, out = self.run_runner('--failed')
        self.assertEqual(code, 2, out)
        self.assertIn('no failed tests recorded', out)

    def test_failed_and_pattern_are_exclusive(self):
        done = subprocess.run([sys.executable, '-B', 'Scripts/RunPythonTests.py', '--failed', '--pattern', 'Test*.py'],
                              cwd=self.root, capture_output=True, text=True, timeout=60)
        self.assertEqual(done.returncode, 2)
        self.assertIn('not allowed with argument', done.stderr)

    def test_discovery_failure_clears_previous_record(self):
        self.logs.mkdir(parents=True)
        (self.logs / 'last-failed.json').write_text(
            json.dumps({'failed': ['OldTest.Case.test_old']}), encoding='utf-8')
        (self.root / 'Scripts' / 'TestBroken.py').write_text('raise RuntimeError("import failed")\n', encoding='utf-8')
        code, out = self.run_runner()
        self.assertEqual(code, 1, out)
        self.assertEqual(self.recorded(), [])

    def test_worker_crash_keeps_unfinished_test_ids(self):
        (self.root / 'Scripts' / 'TestCrash.py').write_text(
            'import os\nimport unittest\nclass Crash(unittest.TestCase):\n'
            '    def test_worker_crash(self):\n        os._exit(7)\n', encoding='utf-8')
        code, out = self.run_runner('--pattern', 'TestCrash.py')
        self.assertEqual(code, 1, out)
        self.assertEqual(self.recorded(), ['TestCrash.Crash.test_worker_crash'])

    def test_log_close_error_still_prints_log_path(self):
        namespace = runpy.run_path(str(RUNNER), run_name='runner_module')

        class CloseErrorLog:
            def close(self):
                raise OSError('disk full')

        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            namespace['finish_log'](Path('test.log'), CloseErrorLog())
        lines = output.getvalue().splitlines()
        self.assertIn('WARNING: log not written: disk full', lines[0])
        self.assertEqual(lines[-1], 'Log: test.log')

    def test_keeps_latest_logs(self):
        self.logs.mkdir(parents=True)
        for index in range(12):
            (self.logs / f'20000101-0000{index:02d}-1.log').write_text('old', encoding='utf-8')
        code, out = self.run_runner()
        self.assertEqual(code, 0, out)
        kept = sorted(path.name for path in self.logs.glob('*.log'))
        self.assertEqual(len(kept), 10)
        self.assertNotIn('20000101-000000-1.log', kept)
        self.assertIn('OK', self.last_log(out))


if __name__ == '__main__':
    unittest.main()
