#!/usr/bin/env python3
"""Freeze this settled-world increment after committing all native inputs."""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path
from ExportB91ObjectsEvidence import ROOT, digest
from CheckB91Checkpoint import check, read


def main():
    parser = argparse.ArgumentParser()
    for name in ('checkpoint', 'grass', 'controls', 'objects', 'ignition', 'rules', 'editor', 'game'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--first-ignition', required=True, type=Path)
    parser.add_argument('--second-ignition', required=True, type=Path)
    args = parser.parse_args()
    inputs = ('Source', 'Config', 'Scripts', 'Content/Lockdown/Data/V09')
    subprocess.run(['git', 'diff', '--exit-code', 'HEAD', '--', *inputs], cwd=ROOT, check=True, capture_output=True)
    assert not subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard', *inputs], cwd=ROOT).strip()
    baseline = json.loads(read(ROOT / 'docs/v09/evidence/B91/Audit.json'))
    for key in ('unchanged_generated_assets_sha256', 'unchanged_supplied_files_sha256'):
        for name, expected in baseline[key].items():
            path = ROOT / name if key == 'unchanged_generated_assets_sha256' else ROOT / 'docs/v09' / name
            assert hashlib.sha256(path.read_bytes()).hexdigest() == expected, name
    counts = check(args.checkpoint)
    envelope = json.loads(read(args.checkpoint / 'World.json'))
    first = json.loads(read(args.first_ignition / 'Ignition.json'))
    assert first['failures'] == 1 and first['phase'] == 93
    assert [c['case'] for c in first['checks'] if not c['passed']] == [
        'Torch consumes Oil into bounded3s fire with the igniter attribution']
    second = json.loads(read(args.second_ignition / 'Ignition.json'))
    assert second['failures'] == 1 and second['phase'] == 8
    assert [c['case'] for c in second['checks'] if not c['passed']] == [
        'Accepted ignition fits its reserved cells without exceeding or leaking the pool']
    for name, checker in (('grass', 'CheckB91Grass.py'), ('controls', 'CheckB91Saplings.py'),
                          ('objects', 'CheckB91Objects.py'), ('ignition', 'CheckB91Ignition.py')):
        subprocess.run([sys.executable, '-B', str(ROOT / 'Scripts' / checker), str(getattr(args, name))], check=True)
    rules = json.loads(read(args.rules / 'Report/index.json'))
    assert rules['succeeded'] >= 55 and not any(rules[k] for k in ('failed', 'notRun', 'inProcess'))
    out = ROOT / 'docs/v09/evidence/B91Checkpoint'
    assert not out.exists(), 'Historical evidence cannot be overwritten'
    for name in ('editor', 'game'):
        assert 'Result: Succeeded' in read(getattr(args, name) / ('Build' + name.title() + '.log'))
    out.mkdir()
    for name in ('checkpoint', 'grass', 'controls', 'objects', 'ignition', 'first_ignition', 'second_ignition'):
        target = out / name
        target.mkdir()
        for path in getattr(args, name).iterdir():
            if path.is_file() and path.suffix.lower() in ('.json', '.csv', '.log'):
                shutil.copyfile(path, target / path.name)
    for name in ('editor', 'game'):
        filename = 'Build' + name.title() + '.log'
        shutil.copyfile(getattr(args, name) / filename, out / filename)
    shutil.copyfile(args.rules / 'Automation.log', out / 'Automation.log')
    shutil.copyfile(args.rules / 'Report/index.json', out / 'Rules.json')
    (out / 'README.md').write_text(
        '# B91 settled environment checkpoints\n\n'
        'Audit.json pins source, contracts, immutable inputs and evidence hashes.\n'
        'checkpoint/ contains four sequential dedicated-server processes: diagnostic state write, '
        'startup restore, adversarial rejection before mutation, and explicit fresh reset. '
        'Fixtures stage existing sources and one permitted harvest yield; they do not prove earned player acquisition. '
        'The other folders contain current native gameplay regressions.\n'
        'first_ignition/ and second_ignition/ preserve failed early field/capacity observations. The final fixture waits for admitted '
        'combat transitions before inspecting committed state; final ignition/ must pass all42 checks.\n'
        'This is a trusted server-local, ownerless, settled-environment checkpoint. '
        'It refuses connected owners, channels, cargo, links and temporary effects. '
        'No player/economy/roster resume, anti-rollback, production recovery, packaging, performance, '
        'human acceptance or whole-B91 completion is claimed.\n', encoding='utf-8')
    files = subprocess.check_output(['git', 'ls-files', 'Source', 'Config', 'Scripts'], cwd=ROOT, text=True).splitlines()
    audit = dict(stage='B91', increment='Settled environment checkpoint', full_stage_acceptance=False,
                 source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                 engine=baseline['engine'], platform=baseline['platform'], toolchain=baseline['toolchain'],
                 native_checkpoint_counts=counts, rules=rules['succeeded'],
                 checkpoint_schema=envelope['schema'], world_contract=envelope['objects_contract'],
                 door_contract=envelope['doors_contract'],
                 verification_order=['Checkpoint, grass and source/pointer regressions passed before a test-only ignition observation fix.',
                                     'Ignition now waits for its admitted combat transition to commit before inspecting live fields.',
                                     'Final ignition/storage/rules and both native targets include that fixture fix; gameplay behavior is unchanged.'],
                 limits=['Diagnostic mutations isolate persistence; earned gameplay has separate network regressions.',
                         'Only a settled ownerless environment resumes; contender/account/roster/economy recovery remains open.',
                         'Files are trusted server-local checkpoints, without authentication or anti-rollback.',
                         'Scenery, wider paid reactions, layered fields, reference encounter and human/density acceptance remain open.'],
                 local_artifacts={name: str(path.resolve()) for name, path in vars(args).items()},
                 source_sha256_normalized_lf={name: digest(ROOT / name) for name in files},
                 unchanged_generated_assets_sha256=baseline['unchanged_generated_assets_sha256'],
                 unchanged_supplied_files_sha256=baseline['unchanged_supplied_files_sha256'],
                 files_sha256_normalized_lf={p.relative_to(out).as_posix(): digest(p) for p in sorted(out.rglob('*')) if p.is_file()})
    (out / 'Audit.json').write_text(json.dumps(audit, indent=2) + '\n', encoding='utf-8')
    print('PASS frozen B91 checkpoint evidence at ' + audit['source_commit'])


if __name__ == '__main__':
    main()
