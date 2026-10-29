"""Freeze the scoped B92 foundation after committing its native inputs."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path
from CheckB92Economy import check
from CheckEconomyEvidence import verify

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes().replace(b'\r\n', b'\n')).hexdigest()


def read(path):
    data = path.read_bytes()
    return json.loads(data.decode('utf-16' if data[:2] in (b'\xff\xfe', b'\xfe\xff') else 'utf-8-sig'))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('runs', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    runs = {k: Path(v) for k, v in read(args.runs).items()}
    check(runs['authority'])
    rules = read(runs['rules'] / 'Report/index.json')
    assert rules['succeeded'] >= 58 and not any(rules[k] for k in ('failed', 'notRun', 'inProcess'))
    for name in ('editor', 'game'):
        assert 'Result: Succeeded' in (runs[name] / f'Build{name.title()}.log').read_text(encoding='utf-8-sig')
    for name, file in (('commerce', 'Commerce.json'), ('institutions', 'Institutions.json')):
        report = read(runs[name] / file)
        assert report['failures'] == 0 and len(report['checks']) >= (20 if name == 'commerce' else 36)
        assert all(c.startswith('PASS ') for c in report['checks'])
        verify(runs[name] / 'Ledger.json')
    baseline = read(ROOT / 'docs/v09/evidence/B91/Audit.json')
    for key in ('unchanged_generated_assets_sha256', 'unchanged_supplied_files_sha256'):
        for name, sha in baseline[key].items():
            path = ROOT / name if key == 'unchanged_generated_assets_sha256' else ROOT / 'docs/v09' / name
            assert hashlib.sha256(path.read_bytes()).hexdigest() == sha, name
    if args.check:
        print('PASS B92 economy foundation evidence and 207 unchanged inputs')
        return
    paths = ['Source', 'Config', 'Scripts', 'Content/Lockdown/Data/V09']
    subprocess.run(['git', 'diff', '--exit-code', 'HEAD', '--', *paths], cwd=ROOT, check=True, capture_output=True)
    assert not subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard', *paths], cwd=ROOT).strip()
    output = ROOT / 'docs/v09/evidence/B92Economy'
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
        '# B92 economy authority foundation\n\n'
        'See [scope and remaining gates](../../B92_ECONOMY_FOUNDATION.md). '
        'This is an isolated native authority test, not full-stage acceptance. '
        'Account-custody transactions use real ALDItem stock; diagnostic funding '
        'transfers existing reserves and is not earned progression. Three independent '
        'fixture domains must not be added together as one match. Scheduled event '
        'times are advanced by the probe; no 90-minute performance or pacing claim. '
        'Live service/RPC/bag integration, roster bodies, work, sources, rare official '
        'stock, security and AI remain open. The failed first compile is retained '
        'under diagnostic_editor; final editor/game builds pass.\n', encoding='utf-8')
    files = subprocess.check_output(['git', 'ls-files', *paths], cwd=ROOT, text=True).splitlines()
    audit = dict(stage='B92', full_stage_acceptance=False, scope='isolated_native_economy_authority',
                 source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                 source_sha256_normalized_lf={name: digest(ROOT / name) for name in files},
                 rules=rules['succeeded'],
                 unchanged_generated_assets_sha256=baseline['unchanged_generated_assets_sha256'],
                 unchanged_supplied_files_sha256=baseline['unchanged_supplied_files_sha256'],
                 files_sha256_normalized_lf={f.relative_to(output).as_posix(): digest(f) for f in sorted(output.rglob('*')) if f.is_file()})
    (output / 'Audit.json').write_text(json.dumps(audit, indent=2) + '\n', encoding='utf-8')
    print('PASS frozen B92 economy authority foundation')


if __name__ == '__main__':
    main()
