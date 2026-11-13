"""Decide how much of the PR check a change needs.

scope=docs: skip both Python tests and the native gate.
scope=native: build the editor and run the native rule tests, then the Python tests.
scope=python: only the Python tests under Scripts.

A change needs the native gate unless every changed file is documentation, a workflow, or a Python script that
RunTests.ps1 does not load. RunTests.ps1 exports native fixtures and checks the rule report with Python, so those
scripts and everything they import from Scripts count as native inputs.

import_tests=run|skip: whether the native rule tests must include the data-import group (CheckRuleReport.IMPORT_TESTS).
Only a change to their inputs needs it: SourceData, the generated data, a Python script RunTests.ps1 loads, the rule-test
runner, build configuration, or the importer's native source (IMPORTER_ROOTS and the project headers they include,
with each such header's own .cpp; any deleted or renamed-away native source file also counts). `RunTests.ps1 -ImportTests Auto` applies the same decision to a local worktree.
"""

import argparse
import ast
import re
import subprocess
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
ROOT = SCRIPTS.parent
# The native files the data-import tests exercise; the include closure of these is computed, not listed.
IMPORTER_ROOTS = re.compile(
    r'^Source/Lockdown/(Public|Private)/(Development/LDDemoContent[^/]*|Region/LDMap(Definition|Geometry)[^/]*'
    r'|World/LDDemoWorldContract\.h|Tests/LDDemoContentCatalogTests\.cpp)$')
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
IMPORT_RUNNERS = {'Scripts/RunTests.ps1', 'Scripts/Common.ps1', 'Scripts/CIChangeScope.py'}


def native_python_modules(scripts_dir: Path) -> set[str]:
    """Scripts modules RunTests.ps1 runs, plus their transitive imports inside Scripts."""
    runner = (scripts_dir / 'RunTests.ps1').read_text(encoding='utf-8')
    pending = sorted(set(re.findall(r'([A-Za-z_][A-Za-z0-9_]*)\.py', runner)))
    if not pending:
        raise RuntimeError('RunTests.ps1 names no Python script; the native input closure cannot be derived.')
    return python_closure(scripts_dir, pending, 'RunTests.ps1')


def python_closure(scripts_dir: Path, roots: list[str], owner: str) -> set[str]:
    """The named Scripts modules plus everything they import from Scripts, transitively."""
    pending = list(roots)
    found: set[str] = set()
    while pending:
        name = pending.pop()
        if name in found:
            continue
        path = scripts_dir / f'{name}.py'
        if not path.is_file():
            raise RuntimeError(f'{owner} depends on missing Scripts/{name}.py.')
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
    docs_only = True
    for raw in paths:
        path = raw.replace('\\', '/')
        if path.startswith('docs/') or path.endswith('.md'):
            continue
        docs_only = False
        if path.startswith('.github/'):
            continue
        parts = path.split('/')
        if len(parts) == 2 and parts[0] == 'Scripts' and parts[1].endswith('.py') and parts[1][:-3] not in native_modules:
            continue
        native.append(path)
    return ('native' if native else 'docs' if docs_only else 'python'), native


def importer_sources(root: Path) -> set[str]:
    """IMPORTER_ROOTS plus every project header they include (transitively) and each such header's own .cpp."""
    source = root / 'Source'
    modules = [p for p in source.iterdir() if p.is_dir()] if source.is_dir() else []
    bases = [m / part for m in modules for part in ('Public', 'Private')]

    def relative(path: Path) -> str:
        return path.resolve().relative_to(root.resolve()).as_posix()

    pending = [(p, True) for p in source.rglob('*') if p.suffix in ('.h', '.cpp') and IMPORTER_ROOTS.match(relative(p))]
    if not pending:
        raise RuntimeError('No importer source matches IMPORTER_ROOTS; the data-import inputs cannot be derived.')
    found: dict[str, Path] = {}
    expanded: set[str] = set()
    while pending:
        path, expand = pending.pop()
        path = path.resolve()
        key = relative(path)
        if key in found and (not expand or key in expanded):
            continue
        found[key] = path
        if not expand:
            continue
        expanded.add(key)
        for name in INCLUDE.findall(path.read_text(encoding='utf-8-sig')):
            for base in [path.parent] + bases:
                candidate = base / name
                if candidate.is_file():
                    pending.append((candidate, True))
                    break
        # A reached header's own .cpp counts, but its includes are that file's business, not the importer's.
        if path.suffix == '.h':
            pending.extend((p, False) for p in source.rglob(path.stem + '.cpp') if p.is_file())
    return set(found)


def import_inputs(paths: list[str], native_modules: set[str], importer: set[str], deleted: set[str] = frozenset()) -> list[str]:
    """The changed paths that can change what the data-import tests see. The importer closure is computed on the new
    tree only, so a deleted (or renamed-away) native source file always counts: it may have been part of the old one."""
    removed = {path.replace('\\', '/') for path in deleted}
    hits = []
    for raw in paths:
        path = raw.replace('\\', '/')
        parts = path.split('/')
        script = len(parts) == 2 and parts[0] == 'Scripts' and parts[1].endswith('.py') and parts[1][:-3] in native_modules
        gone_source = path in removed and path.startswith('Source/') and path.endswith(('.h', '.cpp'))
        if (path.startswith(('SourceData/', 'Content/Lockdown/Data/', 'Config/')) or script or path in IMPORT_RUNNERS
                or path in importer or gone_source or path.endswith(('.uproject', '.Build.cs', '.Target.cs'))):
            hits.append(path)
    return hits


def git(*args: str) -> str:
    result = subprocess.run(['git', '-C', str(ROOT), *args], capture_output=True, text=True, encoding='utf-8')
    if result.returncode != 0:
        raise RuntimeError(f'git {" ".join(args)} failed: {result.stderr.strip()}')
    return result.stdout


def changed_paths(base: str, head: str | None) -> tuple[list[str], set[str]]:
    """Files changed from base to head, or with head None from base to this worktree including untracked files, and
    those of them that were deleted (a rename counts as a deletion of the old path)."""
    if head is None:
        merge_base = git('merge-base', base, 'HEAD').strip()
        lines = git('diff', '--name-status', '--no-renames', merge_base).splitlines()
        lines += [f'A\t{line}' for line in git('ls-files', '--others', '--exclude-standard').splitlines()]
    else:
        lines = git('diff', '--name-status', '--no-renames', base, head).splitlines()
    rows = [line.split('\t', 1) for line in lines if line]
    return sorted({path for _, path in rows}), {path for status, path in rows if status.startswith('D')}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--base', required=True)
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument('--head', help='compare two commits (CI)')
    target.add_argument('--worktree', action='store_true',
                        help='compare this worktree, with uncommitted and untracked files, to its merge base with --base')
    args = parser.parse_args()
    paths, deleted = changed_paths(args.base, None if args.worktree else args.head)
    if not paths and not args.worktree:
        raise RuntimeError(f'No changed files between {args.base} and {args.head}.')
    modules = native_python_modules(SCRIPTS)
    scope, native = classify(paths, modules)
    imports = import_inputs(paths, modules, importer_sources(ROOT), deleted) if scope == 'native' else []
    print(f'scope={scope}')
    print(f'import_tests={"run" if imports else "skip"}')
    if native:
        shown = ', '.join(native[:10]) + (f' and {len(native) - 10} more' if len(native) > 10 else '')
        print(f'Native gate needed for: {shown}', file=sys.stderr)
    elif scope == 'docs':
        print(f'Documentation-only change ({len(paths)} files); skipping Python tests and the native gate.', file=sys.stderr)
    else:
        print(f'Python-only change ({len(paths)} files); skipping the editor build and native rule tests.', file=sys.stderr)
    if imports:
        shown = ', '.join(imports[:10]) + (f' and {len(imports) - 10} more' if len(imports) > 10 else '')
        print(f'Data-import tests needed for: {shown}', file=sys.stderr)
    elif scope == 'native':
        print('No data-import input changed; the data-import tests can be left out.', file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
