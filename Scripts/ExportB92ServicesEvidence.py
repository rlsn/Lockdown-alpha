"""Freeze remote B92 service/identity integration and its affected regressions."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path
from CheckB92Services import check as services
from ExportB92RosterEvidence import validate as regressions
from ExportB92EconomyEvidence import read, digest, ROOT


def validate(runs):
    services(runs['services'])
    assert (runs['services'] / 'Market.png').is_file()
    rules = regressions(runs)
    assert rules['succeeded'] >= 59
    assert any(t['fullTestPath'] == 'Lockdown.Rules.B92ServicePayload' and t['state'] == 'Success'
               for t in rules['tests'])
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
        print('PASS B92 service evidence and207 unchanged inputs')
        return
    paths = ['Source', 'Config', 'Scripts', 'Content/Lockdown/Data/V09']
    subprocess.run(['git', 'diff', '--exit-code', 'HEAD', '--', *paths], cwd=ROOT, check=True, capture_output=True)
    assert not subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard', *paths], cwd=ROOT).strip()
    output = ROOT / 'docs/v09/evidence/B92Services'
    assert not output.exists(), 'Frozen evidence is immutable'
    output.mkdir()
    for name, path in runs.items():
        dest = output / name
        dest.mkdir()
        for file in path.iterdir():
            if file.is_file() and file.suffix.lower() in ('.json', '.log', '.csv', '.png'):
                shutil.copyfile(file, dest / file.name)
    shutil.copyfile(runs['rules'] / 'Report/index.json', output / 'Rules.json')
    (output / 'rules/Report').mkdir()
    shutil.copyfile(runs['rules'] / 'Report/index.json', output / 'rules/Report/index.json')
    (output / 'README.md').write_text(
        '# B92 remote takeover and physical markets\n\n'
        'See [scope and limitations](../../B92_SERVICES.md). Two actual remote owners '
        'trade existing goods; a replacement connection takes the available original '
        'seat/body/account. This is not authenticated account reclaim. Five physical '
        'commodity counters use real bags and one canonical world/market ledger. '
        'Counter travel pads, later-city cached goods and prepared source tools are '
        'explicit fixtures, without grants or hidden funding. Market.png records '
        'the full-bag refusal. Historical baselines remain unchanged. Diagnostic '
        'folders retain failed attempts. Routines, security, work, bank/rare services '
        'and complete B92/B94 acceptance remain open.\n', encoding='utf-8')
    files = subprocess.check_output(['git', 'ls-files', *paths], cwd=ROOT, text=True).splitlines()
    audit = dict(stage='B92', scope='remote_takeover_and_five_physical_markets', full_stage_acceptance=False,
                 source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                 source_sha256_normalized_lf={name: digest(ROOT / name) for name in files}, rules=rules['succeeded'],
                 unchanged_generated_assets_sha256=baseline['unchanged_generated_assets_sha256'],
                 unchanged_supplied_files_sha256=baseline['unchanged_supplied_files_sha256'],
                 files_sha256_normalized_lf={f.relative_to(output).as_posix(): digest(f) for f in sorted(output.rglob('*')) if f.is_file()})
    (output / 'Audit.json').write_text(json.dumps(audit, indent=2) + '\n', encoding='utf-8')
    print('PASS frozen B92 remote services evidence')


if __name__ == '__main__':
    main()
