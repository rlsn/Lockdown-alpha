"""Validate and freeze the B92 native roster/lifecycle increment."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path
from CheckB92Roster import check as roster
from CheckB92Economy import check as economy
from CheckB91Saplings import check as sources
from ExportB92EconomyEvidence import read, digest, ROOT


def validate(runs):
    for name in ('humans4', 'humans12'):
        roster(runs[name])
    economy(runs['economy'])
    sources(runs['sources'])
    rules = read(runs['rules'] / 'Report/index.json')
    assert rules['succeeded'] >= 58 and not any(rules[k] for k in ('failed', 'notRun', 'inProcess'))
    for name in ('editor', 'game'):
        assert 'Result: Succeeded' in (runs[name] / f'Build{name.title()}.log').read_text(encoding='utf-8-sig')
    return rules


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('runs', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    runs = {k: Path(v) for k, v in read(args.runs).items()}
    rules = validate(runs)
    baseline = read(ROOT / 'docs/v09/evidence/B91/Audit.json')
    for key in ('unchanged_generated_assets_sha256', 'unchanged_supplied_files_sha256'):
        for name, sha in baseline[key].items():
            path = ROOT / name if key == 'unchanged_generated_assets_sha256' else ROOT / 'docs/v09' / name
            assert hashlib.sha256(path.read_bytes()).hexdigest() == sha, name
    if args.check:
        print('PASS B92 roster evidence and 207 unchanged inputs')
        return
    paths = ['Source', 'Config', 'Scripts', 'Content/Lockdown/Data/V09']
    subprocess.run(['git', 'diff', '--exit-code', 'HEAD', '--', *paths], cwd=ROOT, check=True, capture_output=True)
    assert not subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard', *paths], cwd=ROOT).strip()
    output = ROOT / 'docs/v09/evidence/B92Roster'
    assert not output.exists(), 'Frozen evidence is immutable'
    output.mkdir()
    for name, path in runs.items():
        dest = output / name
        dest.mkdir()
        for file in path.iterdir():
            if file.is_file() and file.suffix.lower() in ('.json', '.log', '.csv'):
                shutil.copyfile(file, dest / file.name)
    shutil.copyfile(runs['rules'] / 'Report/index.json', output / 'Rules.json')
    (output / 'README.md').write_text(
        '# B92 native roster and lifecycle\n\n'
        'See [implementation scope](../../B92_ROSTER.md). The four/twelve-human '
        'configurations use actual authority controller fixtures, not four/twelve '
        'remote humans. All220 character bodies and persistent economy identities '
        'are real native objects. Source/money/progression preparation is explicitly '
        'diagnostic. Normal destruction/takeover, death tax, actual-clock return '
        'and finite Torch drops are exercised. Population routines, autonomous '
        'contender policy, witnessed security, live services and complete B92 '
        'acceptance remain open. Diagnostic folders preserve failed attempts; '
        'passing runs have separate names.\n', encoding='utf-8')
    files = subprocess.check_output(['git', 'ls-files', *paths], cwd=ROOT, text=True).splitlines()
    audit = dict(stage='B92', scope='native_population_admission_and_lifecycle', full_stage_acceptance=False,
                 source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                 source_sha256_normalized_lf={name: digest(ROOT / name) for name in files}, rules=rules['succeeded'],
                 unchanged_generated_assets_sha256=baseline['unchanged_generated_assets_sha256'],
                 unchanged_supplied_files_sha256=baseline['unchanged_supplied_files_sha256'],
                 files_sha256_normalized_lf={f.relative_to(output).as_posix(): digest(f) for f in sorted(output.rglob('*')) if f.is_file()})
    (output / 'Audit.json').write_text(json.dumps(audit, indent=2) + '\n', encoding='utf-8')
    print('PASS frozen B92 native roster evidence')


if __name__ == '__main__':
    main()
