"""Decide how much of the PR check a change needs.

native: build the editor and run the native rule tests, then the Python tests.
python: only the Python tests under Scripts.

A change needs the native gate unless every changed file is documentation, a workflow, or a Python script that
RunTests.ps1 does not load. RunTests.ps1 exports native fixtures and checks the rule report with Python, so those
scripts and everything they import from Scripts count as native inputs.
"""

import argparse
import ast
import re
import subprocess
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
ROOT = SCRIPTS.parent


def native_python_modules(scripts_dir: Path) -> set[str]:
    """Scripts modules RunTests.ps1 runs, plus their transitive imports inside Scripts."""
    runner = (scripts_dir / 'RunTests.ps1').read_text(encoding='utf-8')
    pending = sorted(set(re.findall(r'([A-Za-z_][A-Za-z0-9_]*)\.py', runner)))
    if not pending:
        raise RuntimeError('RunTests.ps1 names no Python script; the native input closure cannot be derived.')
    found: set[str] = set()
    while pending:
        name = pending.pop()
        if name in found:
            continue
        path = scripts_dir / f'{name}.py'
        if not path.is_file():
            raise RuntimeError(f'RunTests.ps1 depends on missing Scripts/{name}.py.')
        found.add(name)
        tree = ast.parse(path.read_text(encoding='utf-8'), filename=str(path))
        for node in ast.walk(tree):
            names = []
            if isinstance(node, ast.Import):
                names = [alias.name.split('.')[0] for alias in node.names]
            elif isinstance(node, ast.ImportFrom) and node.module and node.level == 0:
                names = [node.module.split('.')[0]]
            pending.extend(n for n in names if (scripts_dir / f'{n}.py').is_file())
    return found


def classify(paths: list[str], native_modules: set[str]) -> tuple[str, list[str]]:
    """Return the scope and the paths that forced the native gate."""
    native = []
    for raw in paths:
        path = raw.replace('\\', '/')
        if path.startswith('docs/') or path.endswith('.md') or path.startswith('.github/'):
            continue
        parts = path.split('/')
        if len(parts) == 2 and parts[0] == 'Scripts' and parts[1].endswith('.py') and parts[1][:-3] not in native_modules:
            continue
        native.append(path)
    return ('native' if native else 'python'), native


def changed_paths(base: str, head: str) -> list[str]:
    result = subprocess.run(['git', '-C', str(ROOT), 'diff', '--name-only', '--no-renames', base, head],
                            capture_output=True, text=True, encoding='utf-8')
    if result.returncode != 0:
        raise RuntimeError(f'git diff {base} {head} failed: {result.stderr.strip()}')
    return [line for line in result.stdout.splitlines() if line]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--base', required=True)
    parser.add_argument('--head', required=True)
    args = parser.parse_args()
    paths = changed_paths(args.base, args.head)
    if not paths:
        raise RuntimeError(f'No changed files between {args.base} and {args.head}.')
    scope, native = classify(paths, native_python_modules(SCRIPTS))
    print(f'scope={scope}')
    if native:
        shown = ', '.join(native[:10]) + (f' and {len(native) - 10} more' if len(native) > 10 else '')
        print(f'Native gate needed for: {shown}', file=sys.stderr)
    else:
        print(f'Python-only change ({len(paths)} files); skipping the editor build and native rule tests.', file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
