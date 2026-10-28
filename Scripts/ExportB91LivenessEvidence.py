#!/usr/bin/env python3
"""Freeze issued-command liveness evidence after committing its native inputs."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path
from ExportB91ObjectsEvidence import ROOT, digest
from CheckEconomyEvidence import verify


def read(path):
    raw = path.read_bytes()
    return raw.decode('utf-16' if raw.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig')


def report(path):
    return json.loads(read(path))


def main():
    parser = argparse.ArgumentParser()
    names = ('controls', 'traversal', 'rules', 'input', 'inventory', 'production',
             'legacy', 'world', 'editor', 'game', 'diagnostic', 'poi', 'sequence')
    for name in names:
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT/'docs/v09/evidence/B91Liveness')
    args = parser.parse_args()
    inputs = ('Source', 'Config', 'Scripts', 'Content/Lockdown/Data/V09')
    subprocess.run(['git', 'diff', '--exit-code', 'HEAD', '--', *inputs], cwd=ROOT,
                   check=True, capture_output=True)
    assert not subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard',
                                       *inputs], cwd=ROOT).strip(), 'Commit native inputs first'
    baseline = report(ROOT/'docs/v09/evidence/B91/Audit.json')
    for key in ('unchanged_generated_assets_sha256', 'unchanged_supplied_files_sha256'):
        for name, expected in baseline[key].items():
            path = ROOT/name if key == 'unchanged_generated_assets_sha256' else ROOT/'docs/v09'/name
            assert hashlib.sha256(path.read_bytes()).hexdigest() == expected, name
    for name, checker in (('controls', 'CheckB91Saplings.py'), ('traversal', 'CheckB91Traversal.py')):
        subprocess.run([sys.executable, '-B', str(ROOT/'Scripts'/checker), str(getattr(args, name))], check=True)
    controls = report(args.controls/'Saplings.json')
    assert controls['phase'] == 16 and len(controls['checks']) == 47 and controls['saw_brief_gap']
    contract = re.search(r'LD_B91_CONTROL_WORLD_SHA TEXT\("([0-9a-f]+)"\)',
                         read(ROOT/'Source/Lockdown/Private/World/LDB91ForestryContract.h')).group(1)
    assert controls['contract'] == contract
    assert (args.controls/'SourceChannel.png').is_file(), 'Require the rendered acceptance run'
    assert '-RenderOffScreen' in read(args.controls/'A.log')
    rules = report(args.rules/'Report/index.json')
    assert rules['succeeded'] == 55 and not any(rules[n] for n in ('failed', 'notRun', 'inProcess'))
    assert any(t['fullTestPath'] == 'Lockdown.Rules.IssuedCommandLiveness' and t['state'] == 'Success'
               for t in rules['tests'])
    counts = {}
    for name, expected in (('input', (15, 19, 19)), ('inventory', (59, 26, 26)),
                           ('legacy', (536, 40, 40))):
        counts[name] = []
        for role, count in zip(('Server', 'A', 'B'), expected):
            data = report(getattr(args, name)/(role+'.json'))
            assert not data['failures'] and len(data['checks']) == count, (name, role)
            counts[name].append(count)
    verify(args.legacy/'Ledger.json')
    authority = report(args.production/'Authority.json')
    assert not authority['failures'] and len(authority['checks']) == 50
    assert any('survives a600ms heartbeat gap' in c for c in authority['checks'])
    assert any('expires after two seconds without spending' in c for c in authority['checks'])
    ledgers = list(args.production.glob('*B63Verified*.json'))
    assert len(ledgers) == 1
    verify(ledgers[0])
    from CheckB85Evidence import check as check_world
    world_summary = check_world(args.world)
    for name, filename in (('editor', 'BuildEditor.log'), ('game', 'BuildGame.log')):
        assert 'Result: Succeeded' in read(getattr(args, name)/filename)
    diagnostic = read(args.diagnostic/'Server.log')
    assert 'cause=liveness age=0.351784 movement_cm=0.000000' in diagnostic
    assert report(args.diagnostic/'Saplings.json')['failures'] == 2
    poi = report(args.poi/'Keys/Report.json')
    assert poi['failures'] == 1 and not poi['travel']
    sequence = report(args.sequence/'Keys/Report.json')
    assert sequence['failures'] == 1 and len(sequence['travel']) == 9
    out = args.output
    assert not out.exists(), 'Historical evidence cannot be overwritten'
    out.mkdir(parents=True)
    for name in ('controls', 'traversal', 'input', 'inventory', 'production', 'legacy'):
        target = out/name
        target.mkdir()
        for path in sorted(getattr(args, name).iterdir()):
            if not path.is_file() or path.suffix.lower() not in ('.json', '.csv', '.log', '.png'):
                continue
            if path.suffix.lower() == '.log':
                assert not any(s in read(path) for s in ('appError called:', '=== Critical error: ===',
                    'Fatal error:', 'Unhandled Exception:', 'LD_ECONOMY_RECORD_ERROR', 'LD_ECONOMY_RECORD_FAILED')), path
            shutil.copyfile(path, target/path.name)
    for path in sorted(args.world.rglob('*')):
        if path.is_file() and path.suffix.lower() in ('.json', '.log'):
            if path.suffix.lower() == '.log':
                assert not any(s in read(path) for s in ('appError called:', 'Fatal error:',
                    'Unhandled Exception:', 'LD_ECONOMY_RECORD_ERROR', 'LD_ECONOMY_RECORD_FAILED')), path
            target = out/'world'/path.relative_to(args.world)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
    (out/'diagnostic').mkdir()
    for name in ('Server.log', 'Saplings.json', 'A_4.json'):
        shutil.copyfile(args.diagnostic/name, out/'diagnostic'/name)
    (out/'diagnostic/README.md').write_text(
        'Failed diagnostic reproduction before the fix: two phase4 failures. The log identifies '
        'liveness expiry at351.784ms, zero movement and a16.510ms server frame. '
        'This is root-cause evidence, not a passing scenario.\n', encoding='utf-8')
    (out/'diagnostic/poi').mkdir()
    for name in ('Native.log', 'Report.json'):
        shutil.copyfile(args.poi/'Keys'/name, out/'diagnostic/poi'/name)
    (out/'diagnostic/poi/README.md').write_text(
        'Failed v0.8 Keys regression before the stationary-POI guard. Passing through an '
        'automatic context stopped the first native journey. This failure is retained as '
        'diagnosis, not route acceptance. The passing world/ rerun is separate.\n', encoding='utf-8')
    (out/'diagnostic/sequence').mkdir()
    for name in ('Native.log', 'Report.json'):
        shutil.copyfile(args.sequence/'Keys'/name, out/'diagnostic/sequence'/name)
    (out/'diagnostic/sequence/README.md').write_text(
        'The stationary guard restores nine native journeys. This subsequent failed Keys '
        'fixture still used an independent counter that could collide with new automatic '
        'POI request IDs. The probe now synchronizes its counters with both real controllers. '
        'This failed report is not acceptance.\n', encoding='utf-8')
    for name, filename in (('editor', 'BuildEditor.log'), ('game', 'BuildGame.log')):
        shutil.copyfile(getattr(args, name)/filename, out/filename)
    shutil.copyfile(args.rules/'Automation.log', out/'Automation.log')
    shutil.copyfile(args.rules/'Report/index.json', out/'Rules.json')
    imports = b''
    for name, flags in (('SyncB91Forestry.py', ['--check']), ('TestB91TraversalImport.py', []),
                        ('TestB91ToolsImport.py', []), ('TestB91ObjectsImport.py', []),
                        ('TestB91Import.py', []), ('TestV09Import.py', []), ('TestV08Import.py', [])):
        imports += subprocess.check_output([sys.executable, '-B', str(ROOT/'Scripts'/name), *flags],
                                            cwd=ROOT, stderr=subprocess.STDOUT)
    (out/'ImportTests.log').write_bytes(imports)
    (out/'README.md').write_text(
        '# B91 issued-command liveness\n\nAudit.json pins committed native inputs, protected bytes and raw evidence.\n'
        'controls/ passes47 checks with one720p offscreen rendered owner and one headless peer. '
        'The real command sender deliberately pauses for one second, then a separate order expires '
        'without heartbeats; late refresh cannot revive it. SourceChannel.png was visually inspected.\n'
        'traversal/ passes55 checks including missing-heartbeat safe return. Other folders contain '
        'liveness regressions; production is a controlled-clock local-authority fixture.\n'
        'world/ contains all four earned v0.8 route regressions and independent ledger replay; '
        'these are small-map native fixtures, not full-map or human acceptance.\n'
        'diagnostic/ intentionally retains the pre-fix failed reproduction and is not acceptance.\n'
        'No human, performance, full-session or whole-B91 acceptance is claimed.\n', encoding='utf-8')
    files = subprocess.check_output(['git', 'ls-files', 'Source', 'Config', 'Scripts'],
                                    cwd=ROOT, text=True).splitlines()
    audit = dict(stage='B91', increment='Issued-command liveness and rendered channel regression',
        full_stage_acceptance=False,
        source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        engine=baseline['engine'], platform=baseline['platform'], toolchain=baseline['toolchain'],
        world_contract=controls['contract'], command_liveness_seconds=2,
        forestry_contract_sha256=digest(ROOT/'Content/Lockdown/Data/V09/forestry_b91.json'),
        source_sha256_normalized_lf={name: digest(ROOT/name) for name in files},
        unchanged_generated_assets_sha256=baseline['unchanged_generated_assets_sha256'],
        unchanged_supplied_files_sha256=baseline['unchanged_supplied_files_sha256'],
        local_artifacts={name: str(getattr(args, name).resolve()) for name in names},
        regression_counts=counts, rules=rules['succeeded'], production_checks=len(authority['checks']),
        inherited_world_routes=world_summary,
        verification_order=['Liveness, rendered controls, traversal, inventory, production and fields verified before the final stationary-POI guard.',
                            'Inherited earned routes and shared input rerun after the POI guard and probe sequencing fix; final Editor/Game builds contain all changes.',
                            'The B91 environment controller bypasses the shared UI automatic-POI method changed by that final guard.'],
        limits=['One offscreen rendered client, one headless peer; no physical human input or frame-rate acceptance.',
                'Finite knives and full-bag cases use explicit existing-item fixture staging.',
                'Grass, saved mutations, broader paid actions, layered fields and full-stage fitting remain open.'],
        files_sha256_normalized_lf={p.relative_to(out).as_posix(): digest(p)
                                    for p in sorted(out.rglob('*')) if p.is_file()})
    (out/'Audit.json').write_text(json.dumps(audit, indent=2)+'\n', encoding='utf-8')
    print('PASS frozen B91 liveness checkpoint at '+audit['source_commit'])


if __name__ == '__main__':
    main()
