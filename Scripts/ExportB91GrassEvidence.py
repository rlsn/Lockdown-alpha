#!/usr/bin/env python3
"""Freeze the bounded authored-grass increment after its source commit."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path
from ExportB91ObjectsEvidence import ROOT, digest
from ExportB91LivenessEvidence import read, report
from CheckEconomyEvidence import verify


def main():
    parser = argparse.ArgumentParser()
    names = ('grass', 'controls', 'ignition', 'objects', 'legacy', 'rules', 'editor', 'game')
    for name in names:
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT/'docs/v09/evidence/B91Grass')
    args = parser.parse_args()
    inputs = ('Source', 'Config', 'Scripts', 'Content/Lockdown/Data/V09')
    subprocess.run(['git', 'diff', '--exit-code', 'HEAD', '--', *inputs], cwd=ROOT,
                   check=True, capture_output=True)
    assert not subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard',
                                       *inputs], cwd=ROOT).strip()
    baseline = report(ROOT/'docs/v09/evidence/B91/Audit.json')
    for key in ('unchanged_generated_assets_sha256', 'unchanged_supplied_files_sha256'):
        for name, expected in baseline[key].items():
            path = ROOT/name if key == 'unchanged_generated_assets_sha256' else ROOT/'docs/v09'/name
            assert hashlib.sha256(path.read_bytes()).hexdigest() == expected, name
    for name, checker in (('grass', 'CheckB91Grass.py'), ('controls', 'CheckB91Saplings.py'),
                          ('ignition', 'CheckB91Ignition.py'), ('objects', 'CheckB91Objects.py')):
        subprocess.run([sys.executable, '-B', str(ROOT/'Scripts'/checker), str(getattr(args, name))], check=True)
    world = re.search(r'LD_B91_GRASS_WORLD_SHA TEXT\("([0-9a-f]+)"\)',
                      read(ROOT/'Source/Lockdown/Private/World/LDB91GrassContract.h')).group(1)
    assert report(args.grass/'Grass.json')['contract'] == world
    assert (args.grass/'GrassConcealment.png').is_file() and '-RenderOffScreen' in read(args.grass/'A.log')
    assert len(report(args.objects/'Objects.json')['fitted_objects']) == 435
    rules = report(args.rules/'Report/index.json')
    assert rules['succeeded'] >= 55 and not any(rules[n] for n in ('failed', 'notRun', 'inProcess'))
    counts = []
    for role, expected in zip(('Server', 'A', 'B'), (536, 40, 40)):
        data = report(args.legacy/(role+'.json'))
        assert not data['failures'] and len(data['checks']) == expected
        counts.append(expected)
    verify(args.legacy/'Ledger.json')
    for name, filename in (('editor', 'BuildEditor.log'), ('game', 'BuildGame.log')):
        assert 'Result: Succeeded' in read(getattr(args, name)/filename)
    out = args.output
    assert not out.exists(), 'Historical evidence cannot be overwritten'
    out.mkdir(parents=True)
    for name in ('grass', 'controls', 'ignition', 'objects', 'legacy'):
        target = out/name
        target.mkdir()
        for path in sorted(getattr(args, name).iterdir()):
            if not path.is_file() or path.suffix.lower() not in ('.json', '.csv', '.log', '.png'):
                continue
            if path.suffix.lower() == '.log':
                assert not any(s in read(path) for s in ('appError called:', '=== Critical error: ===',
                    'Fatal error:', 'Unhandled Exception:', 'LD_ECONOMY_RECORD_ERROR', 'LD_ECONOMY_RECORD_FAILED')), path
            shutil.copyfile(path, target/path.name)
    for name, filename in (('editor', 'BuildEditor.log'), ('game', 'BuildGame.log')):
        shutil.copyfile(getattr(args, name)/filename, out/filename)
    shutil.copyfile(args.rules/'Automation.log', out/'Automation.log')
    shutil.copyfile(args.rules/'Report/index.json', out/'Rules.json')
    imports = b''
    for name, flags in (('SyncB91Grass.py', ['--check']), ('TestB91GrassImport.py', []),
                        ('TestB91TraversalImport.py', []), ('TestB91ToolsImport.py', []),
                        ('TestB91ObjectsImport.py', []), ('TestB91Import.py', []),
                        ('TestV09Import.py', []), ('TestV08Import.py', [])):
        imports += subprocess.check_output([sys.executable, '-B', str(ROOT/'Scripts'/name), *flags],
                                            cwd=ROOT, stderr=subprocess.STDOUT)
    (out/'ImportTests.log').write_bytes(imports)
    (out/'README.md').write_text(
        '# B91 authored grass\n\nAudit.json pins the source, immutable inputs and evidence hashes.\n'
        'grass/: two remote owners, one720p offscreen renderer, real-clock concealment, '
        'proximity/geometry, attack/rearm, stale handle refusal, actual Torch binding, paid '
        'one-winner ignition, warm initialization and actual life return. Wall, detector '
        'fields, sustained-cast and claim-state setup are diagnostic; Torches and positions use explicit '
        'fixture staging. No paid detector acquisition or full-map Heart admission is claimed.\n'
        'Other folders contain companion regressions. GrassConcealment.png was inspected. '
        'No human, performance, saved-match restoration or whole-B91 acceptance is claimed.\n', encoding='utf-8')
    files = subprocess.check_output(['git', 'ls-files', 'Source', 'Config', 'Scripts'],
                                    cwd=ROOT, text=True).splitlines()
    audit = dict(stage='B91', increment='Finite authored grass concealment and paid Torch clearance',
        full_stage_acceptance=False,
        source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        engine=baseline['engine'], platform=baseline['platform'], toolchain=baseline['toolchain'],
        world_contract=world, grass_contract_sha256=digest(ROOT/'Content/Lockdown/Data/V09/grass_b91.json'),
        initial_objects=435, initial_physical_items=143, authored_grass=96,
        source_sha256_normalized_lf={name: digest(ROOT/name) for name in files},
        unchanged_generated_assets_sha256=baseline['unchanged_generated_assets_sha256'],
        unchanged_supplied_files_sha256=baseline['unchanged_supplied_files_sha256'],
        local_artifacts={name: str(getattr(args, name).resolve()) for name in names},
        inherited_field_assertions=counts,
        verification_order=['Source/pointer, ignition, objects and rules fixtures ran during the increment before the final sustained-cast timer adjustment.',
                            'The final44-check rendered grass scenario and inherited field workload include that adjustment; final Editor/Game builds include all changes.'],
        limits=['Detector/wall/sustained-cast/claim-state setup is diagnostic; normal paid acquisition/full-map claim integration remains separate.',
                'Warm initialization and actual life return are tested; saved-match restoration remains open.',
                'Scenery trees, wider paid fire/reactions, layered fields, complete reference encounter and density/human acceptance remain open.'],
        files_sha256_normalized_lf={p.relative_to(out).as_posix(): digest(p)
                                    for p in sorted(out.rglob('*')) if p.is_file()})
    (out/'Audit.json').write_text(json.dumps(audit, indent=2)+'\n', encoding='utf-8')
    print('PASS frozen B91 grass checkpoint at '+audit['source_commit'])


if __name__ == '__main__':
    main()
