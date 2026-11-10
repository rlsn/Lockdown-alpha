"""Run the Scripts Python tests (Test*.py) across several worker processes.

Same tests and pass rule as `python -B -m unittest discover -s Scripts -p "Test*.py"`, split into units: a class with
class- or module-level fixtures stays together; every other test is its own unit. Each test already writes only to
its own temporary directory, so units are independent. Any error, failure, unexpected success, or a unit that did
not run fails the whole run.

Output is also written to Artifacts/PythonTests/<time>-<pid>.log, and the ids of failed tests to
Artifacts/PythonTests/last-failed.json, so a failure never needs a full rerun just to see its details. `--failed`
reruns only the tests recorded by the previous run; it is a partial run, not a gate.
"""

import argparse
from concurrent.futures import ProcessPoolExecutor, as_completed
from datetime import datetime
import json
import os
import sys
import time
import traceback
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
ROOT = SCRIPTS.parent
LOG_DIR = ROOT / 'Artifacts' / 'PythonTests'
LAST_FAILED = LOG_DIR / 'last-failed.json'
KEEP_LOGS = 10
RERUN_HINT = 'python -B Scripts/RunPythonTests.py --failed'


class Tee:
    """Copy this process's stdout to a log file; a log write error never affects the run."""

    def __init__(self, stream, log):
        self.stream, self.log = stream, log

    def write(self, text):
        if self.log is not None:
            try:
                self.log.write(text)
            except OSError as error:
                self.log = None
                self.stream.write(f'WARNING: log not written: {error}\n')
        return self.stream.write(text)

    def flush(self):
        self.stream.flush()
        if self.log is not None:
            try:
                self.log.flush()
            except OSError:
                pass

    def __getattr__(self, name):
        return getattr(self.stream, name)


def iter_tests(suite):
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            yield from iter_tests(item)
        else:
            yield item


def has_shared_fixture(case_type) -> bool:
    module = sys.modules.get(case_type.__module__)
    return ('setUpClass' in vars(case_type) or 'tearDownClass' in vars(case_type)
            or (module is not None and (hasattr(module, 'setUpModule') or hasattr(module, 'tearDownModule'))))


def plan_units(pattern: str | None = None, ids: list[str] | None = None) -> tuple[list[list[str]], int]:
    """Return units of test ids, shared-fixture classes first, and the total test count."""
    if ids is None:
        suite = unittest.defaultTestLoader.discover(str(SCRIPTS), pattern=pattern, top_level_dir=str(SCRIPTS))
    else:
        suite = unittest.defaultTestLoader.loadTestsFromNames(ids)
    tests = list(iter_tests(suite))
    broken = [t.id() for t in tests if type(t).__name__ in ('_FailedTest', 'ModuleImportFailure')]
    if broken:
        raise RuntimeError(f'Test modules failed to import: {", ".join(broken)}')
    grouped: dict[type, list[str]] = {}
    singles: list[list[str]] = []
    for test in tests:
        if has_shared_fixture(type(test)):
            grouped.setdefault(type(test), []).append(test.id())
        else:
            singles.append([test.id()])
    return list(grouped.values()) + singles, len(tests)


def init_worker() -> None:
    sys.path.insert(0, str(SCRIPTS))
    sys.dont_write_bytecode = True


def run_unit(ids: list[str]) -> dict:
    started = time.perf_counter()
    result = unittest.TestResult()
    try:
        unittest.defaultTestLoader.loadTestsFromNames(ids).run(result)
    except Exception:  # A crash outside unittest's own handling still has to fail the run.
        result.errors.append((None, traceback.format_exc()))
    problems = [(t.id() if t else ids[0], kind, text)
                for kind, items in (('ERROR', result.errors), ('FAIL', result.failures))
                for t, text in items]
    problems += [(t.id(), 'UNEXPECTED SUCCESS', '') for t in result.unexpectedSuccesses]
    return {'ids': ids, 'ran': result.testsRun, 'skipped': len(result.skipped), 'problems': problems,
            'seconds': time.perf_counter() - started}


def failed_ids(outcome: dict) -> list[str]:
    """Ids to rerun for a unit: its failed tests, or the whole unit when a fixture failed or tests did not run."""
    ids = outcome['ids']
    if outcome['ran'] < len(ids) or any(test_id not in ids for test_id, _, _ in outcome['problems']):
        return list(ids)
    return sorted({test_id for test_id, _, _ in outcome['problems']})


def write_last_failed(ids: list[str]) -> None:
    try:
        LOG_DIR.mkdir(parents=True, exist_ok=True)
        temp = LAST_FAILED.with_name(f'{LAST_FAILED.name}.{os.getpid()}.tmp')
        temp.write_text(json.dumps({'failed': ids}, indent=1) + '\n', encoding='utf-8')
        os.replace(temp, LAST_FAILED)
    except OSError as error:
        print(f'WARNING: failed-test record not written: {error}')


def read_last_failed() -> list[str]:
    try:
        ids = json.loads(LAST_FAILED.read_text(encoding='utf-8'))['failed']
    except FileNotFoundError:
        return []
    if not isinstance(ids, list) or not all(isinstance(i, str) for i in ids):
        raise ValueError(f'{LAST_FAILED} is malformed')
    return ids


def open_log():
    try:
        LOG_DIR.mkdir(parents=True, exist_ok=True)
        for old in sorted(LOG_DIR.glob('*.log'))[:-(KEEP_LOGS - 1)]:
            old.unlink(missing_ok=True)
        path = LOG_DIR / f'{datetime.now():%Y%m%d-%H%M%S}-{os.getpid()}.log'
        return path, open(path, 'w', encoding='utf-8', buffering=1)
    except OSError as error:
        print(f'WARNING: log not written: {error}')
        return None, None


def run(args) -> int:
    init_worker()
    if args.failed:
        ids = read_last_failed()
        if not ids:
            print(f'ERROR: no failed tests recorded in {LAST_FAILED}; run the tests without --failed first.')
            return 2
        print(f'PARTIAL RUN (--failed), NOT A GATE: rerunning {len(ids)} previously failed tests.')
        units, expected = plan_units(ids=ids)
    else:
        units, expected = plan_units(args.pattern)
    sys.stdout.flush()
    started = time.perf_counter()
    ran = skipped = 0
    problems = []
    rerun = []
    slow = []
    # ProcessPoolExecutor workers are not daemonic, so a test may start its own worker processes (fixture export).
    with ProcessPoolExecutor(args.jobs, initializer=init_worker) as pool:
        for future in as_completed([pool.submit(run_unit, unit) for unit in units]):
            outcome = future.result()
            ran += outcome['ran']
            skipped += outcome['skipped']
            problems += outcome['problems']
            if outcome['problems'] or outcome['ran'] < len(outcome['ids']):
                rerun += failed_ids(outcome)
            slow.append((outcome['seconds'], outcome['ids'][0] + (f' (+{len(outcome["ids"]) - 1})' if len(outcome['ids']) > 1 else '')))
            for test_id, kind, _ in outcome['problems']:
                print(f'{kind}: {test_id}', flush=True)
    write_last_failed(sorted(set(rerun)))
    for test_id, kind, text in problems:
        print(f'\n{"=" * 70}\n{kind}: {test_id}\n{"-" * 70}\n{text}')
    print('\nSlowest units:')
    for seconds, name in sorted(slow, reverse=True)[:5]:
        print(f'  {seconds:6.1f}s  {name}')
    wall = time.perf_counter() - started
    print(f'\nRan {ran} of {expected} tests in {wall:.1f}s with {args.jobs} workers ({skipped} skipped).')
    if args.failed:
        print('PARTIAL RUN (--failed), NOT A GATE.')
    if ran != expected:
        print(f'FAILED: {expected - ran} discovered tests did not run.')
    elif problems:
        print(f'FAILED ({len(problems)} problems)')
    else:
        print('OK')
        return 0
    print(f'Rerun only the failed tests: {RERUN_HINT}')
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--jobs', type=int, default=min(4, os.cpu_count() or 1), help='worker processes (default: up to 4)')
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument('--pattern', default='Test*.py')
    selection.add_argument('--failed', action='store_true',
                           help='rerun only the tests that failed in the previous run (partial, not a gate)')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be at least 1')
    log_path, log = open_log()
    stdout = sys.stdout
    sys.stdout = Tee(stdout, log)
    try:
        return run(args)
    except Exception:
        traceback.print_exc(file=sys.stdout)
        return 1
    finally:
        sys.stdout = stdout
        if log is not None:
            log.close()
            try:
                shown = log_path.relative_to(ROOT)
            except ValueError:
                shown = log_path
            print(f'Log: {shown}', flush=True)


if __name__ == '__main__':
    sys.exit(main())
