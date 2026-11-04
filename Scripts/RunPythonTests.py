"""Run the Scripts Python tests (Test*.py) across several worker processes.

Same tests and pass rule as `python -B -m unittest discover -s Scripts -p "Test*.py"`, split into units: a class with
class- or module-level fixtures stays together; every other test is its own unit. Each test already writes only to
its own temporary directory, so units are independent. Any error, failure, unexpected success, or a unit that did
not run fails the whole run.
"""

import argparse
import multiprocessing
import os
import sys
import time
import traceback
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent


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


def plan_units(pattern: str) -> tuple[list[list[str]], int]:
    """Return units of test ids, shared-fixture classes first, and the total test count."""
    suite = unittest.defaultTestLoader.discover(str(SCRIPTS), pattern=pattern, top_level_dir=str(SCRIPTS))
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--jobs', type=int, default=min(4, os.cpu_count() or 1), help='worker processes (default: up to 4)')
    parser.add_argument('--pattern', default='Test*.py')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be at least 1')
    init_worker()
    units, expected = plan_units(args.pattern)
    started = time.perf_counter()
    ran = skipped = 0
    problems = []
    slow = []
    with multiprocessing.Pool(args.jobs, initializer=init_worker) as pool:
        for outcome in pool.imap_unordered(run_unit, units):
            ran += outcome['ran']
            skipped += outcome['skipped']
            problems += outcome['problems']
            slow.append((outcome['seconds'], outcome['ids'][0] + (f' (+{len(outcome["ids"]) - 1})' if len(outcome['ids']) > 1 else '')))
            for test_id, kind, _ in outcome['problems']:
                print(f'{kind}: {test_id}', flush=True)
    for test_id, kind, text in problems:
        print(f'\n{"=" * 70}\n{kind}: {test_id}\n{"-" * 70}\n{text}')
    print('\nSlowest units:')
    for seconds, name in sorted(slow, reverse=True)[:5]:
        print(f'  {seconds:6.1f}s  {name}')
    wall = time.perf_counter() - started
    print(f'\nRan {ran} of {expected} tests in {wall:.1f}s with {args.jobs} workers ({skipped} skipped).')
    if ran != expected:
        print(f'FAILED: {expected - ran} discovered tests did not run.')
        return 1
    if problems:
        print(f'FAILED ({len(problems)} problems)')
        return 1
    print('OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())
