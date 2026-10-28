#!/usr/bin/env python3
"""Freeze bounded full-map field evidence after its native source has been committed."""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path
from ExportB91ObjectsEvidence import ROOT, digest
from CheckEconomyEvidence import verify as verify_economy


def main():
    parser = argparse.ArgumentParser()
    for name in ('fields', 'legacy', 'saplings', 'furniture', 'traversal', 'tools', 'objects', 'structural', 'doors', 'rules', 'editor', 'game'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT/'docs/v09/evidence/B91Fields')
    args = parser.parse_args()
    furniture = json.loads((args.furniture/'Objects.json').read_text(encoding='utf-8-sig'))
    assert furniture['furniture_scenario'] and len(furniture['checks']) == 51
    assert sum(r['kind'] == 12 for r in furniture['fitted_objects']) == 32
    subprocess.run(['git', 'diff', '--exit-code', 'HEAD', '--', 'Source', 'Config', 'Scripts',
                    'Content/Lockdown/Data/V09'], cwd=ROOT, check=True, capture_output=True)
    assert not subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard', 'Source',
                                       'Config', 'Scripts', 'Content/Lockdown/Data/V09'], cwd=ROOT).strip()
    baseline = json.loads((ROOT/'docs/v09/evidence/B91/Audit.json').read_text())
    for key in ('unchanged_generated_assets_sha256', 'unchanged_supplied_files_sha256'):
        for name, expected in baseline[key].items():
            path = ROOT/name if key == 'unchanged_generated_assets_sha256' else ROOT/'docs/v09'/name
            assert hashlib.sha256(path.read_bytes()).hexdigest() == expected, name
    for checker, source in (('CheckB91Fields.py', args.fields), ('CheckB91Saplings.py', args.saplings), ('CheckB91Objects.py', args.furniture), ('CheckB91Traversal.py', args.traversal), ('CheckB91Tools.py', args.tools), ('CheckB91Objects.py', args.objects),
                            ('CheckB91Evidence.py', args.structural), ('CheckB91Network.py', args.doors)):
        subprocess.run([sys.executable, '-B', str(ROOT/'Scripts'/checker), str(source)], check=True)
    for label in ('fields', 'legacy', 'saplings', 'furniture', 'traversal', 'tools', 'objects', 'structural', 'doors'):
        for log in getattr(args, label).glob('*.log'):
            content = log.read_text(encoding='utf-8-sig')
            assert not any(marker in content for marker in
                           ('appError called:', '=== Critical error: ===', 'Fatal error:', 'Unhandled Exception:')), log
    rules = json.loads((args.rules/'Report/index.json').read_text(encoding='utf-8-sig'))
    assert rules['succeeded'] >= 53 and not any(rules[n] for n in ('failed', 'notRun', 'inProcess'))
    for source, name in ((args.editor, 'BuildEditor.log'), (args.game, 'BuildGame.log')):
        assert 'Result: Succeeded' in (source/name).read_text(encoding='utf-8-sig')
    for role in ('Server', 'A', 'B'):
        raw = (args.legacy/f'{role}.json').read_bytes()
        legacy = json.loads(raw.decode('utf-16' if raw.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig'))
        assert not legacy['failures'] and len(legacy['checks']) >= 10
    verify_economy(args.legacy/'Ledger.json')
    imports = b''
    for name, flags in (('SyncB91Traversal.py', ['--check']), ('TestB91TraversalImport.py', []), ('SyncB91Tools.py', ['--check']), ('TestB91ToolsImport.py', []),
                        ('SyncB91Objects.py', ['--check']), ('TestB91ObjectsImport.py', []),
                        ('SyncB91Data.py', ['--check']), ('TestB91Import.py', []),
                        ('TestV09Import.py', []), ('TestV08Import.py', [])):
        result = subprocess.run([sys.executable, '-B', str(ROOT/'Scripts'/name), *flags], cwd=ROOT,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)
        imports += result.stdout
    out = args.output
    assert not out.exists(), 'Historical evidence cannot be overwritten'
    out.mkdir(parents=True)
    for label in ('fields', 'legacy', 'saplings', 'furniture', 'traversal', 'tools', 'objects', 'structural', 'doors'):
        target = out/label
        target.mkdir()
        for path in sorted(getattr(args, label).iterdir()):
            if path.is_file() and path.suffix.lower() in ('.json', '.csv', '.log'):
                shutil.copyfile(path, target/path.name)
    for source, name in ((args.editor, 'BuildEditor.log'), (args.game, 'BuildGame.log')):
        shutil.copyfile(source/name, out/name)
    shutil.copyfile(args.rules/'Automation.log', out/'Automation.log')
    shutil.copyfile(args.rules/'Report/index.json', out/'Rules.json')
    (out/'ImportTests.log').write_bytes(imports)
    (out/'README.md').write_text(
        '# B91 bounded paid-field checkpoint\n\n'
        'Audit.json pins the native source and normalized file hashes. Earlier evidence is unchanged.\n\n'
        'fields/: 33 checks, two remote owners, actual paid Water/Oil; capacity fill and Frost are diagnostic.\n'
        'legacy/: v0.8 B84 field regression plus independently replayed source ledger.\n'
        'saplings/, furniture/, objects/, traversal/, tools/, structural/ and doors/: current-source regressions.\n'
        'Rules.json: 53 native rules; BuildEditor.log and BuildGame.log: full-unity Development targets.\n\n'
        'No paid Frost acquisition, stacked-floor fields, direct-hit reaction, socket ignition, rendered readability, '
        'performance or full-stage acceptance is claimed. See ../../B91_FIELDS.md for boundaries.\n', encoding='utf-8')
    files = subprocess.check_output(['git', 'ls-files', 'Source', 'Config', 'Scripts'], cwd=ROOT, text=True).splitlines()
    audit = dict(stage='B91', increment='Bounded full-map paid Water/Oil and restricted terrain-supported field views',
                 full_stage_acceptance=False,
                 resolved_findings=['Support normals keep grounded ramp contact consistent away from cell centres.',
                                    'Empty book-drop cleanup avoids legacy fallback ground queries on shutdown.',
                                    'Client proof waits for actual paid release rather than windup acceptance.'],
                 source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                 engine=baseline['engine'], platform=baseline['platform'], toolchain=baseline['toolchain'],
                 traversal_contract_sha256=digest(ROOT/'Content/Lockdown/Data/V09/traversal_b91.json'),
                 tools_contract_sha256=digest(ROOT/'Content/Lockdown/Data/V09/tools_b91.json'),
                 base_objects_contract_sha256=digest(ROOT/'Content/Lockdown/Data/V09/objects_b91.json'),
                 world_contract=json.loads((args.objects/'Objects.json').read_text())['contract'],
                 environment_contract_sha256=baseline['environment_contract_sha256'],
                 map_contract_sha256=baseline['map_contract_sha256'],
                 source_sha256_normalized_lf={name: digest(ROOT/name) for name in files},
                 unchanged_generated_assets_sha256=baseline['unchanged_generated_assets_sha256'],
                 unchanged_supplied_files_sha256=baseline['unchanged_supplied_files_sha256'],
                 local_artifacts={name: str(getattr(args, name).resolve()) for name in
                                  ('fields', 'legacy', 'saplings', 'furniture', 'traversal', 'tools', 'objects', 'structural', 'doors', 'rules', 'editor', 'game')},
                 limits=['Editor server with two independent headless owners; no packaged server/video/human acceptance.',
                         'Rope binding uses explicit fixture staging of an existing item; binding RPC remains separately proven.',
                         'Full-map body/object/target sight now uses 18m with inherited Blind and capped bonuses.',
                         'Paid Frost acquisition, stacked-floor fields, direct-hit reactions, socket ignition/reservations, foliage burning and broader spells remain open.'],
                 files_sha256_normalized_lf={p.relative_to(out).as_posix(): digest(p)
                                             for p in sorted(out.rglob('*')) if p.is_file()})
    (out/'Audit.json').write_text(json.dumps(audit, indent=2)+'\n', encoding='utf-8')
    print(f'PASS frozen B91 field checkpoint at {audit["source_commit"]}: {out}')


if __name__ == '__main__':
    main()
