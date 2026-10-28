#!/usr/bin/env python3
"""Freeze fixed-furniture evidence after its native source has been committed."""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path
from ExportB91ObjectsEvidence import ROOT, digest


def main():
    parser = argparse.ArgumentParser()
    for name in ('furniture', 'traversal', 'tools', 'objects', 'structural', 'doors', 'rules', 'editor', 'game'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT/'docs/v09/evidence/B91Furniture')
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
    for checker, source in (('CheckB91Objects.py', args.furniture), ('CheckB91Traversal.py', args.traversal), ('CheckB91Tools.py', args.tools), ('CheckB91Objects.py', args.objects),
                            ('CheckB91Evidence.py', args.structural), ('CheckB91Network.py', args.doors)):
        subprocess.run([sys.executable, '-B', str(ROOT/'Scripts'/checker), str(source)], check=True)
    rules = json.loads((args.rules/'Report/index.json').read_text(encoding='utf-8-sig'))
    assert rules['succeeded'] >= 51 and not any(rules[n] for n in ('failed', 'notRun', 'inProcess'))
    for source, name in ((args.editor, 'BuildEditor.log'), (args.game, 'BuildGame.log')):
        assert 'Result: Succeeded' in (source/name).read_text(encoding='utf-8-sig')
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
    for label in ('furniture', 'traversal', 'tools', 'objects', 'structural', 'doors'):
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
    files = subprocess.check_output(['git', 'ls-files', 'Source', 'Config', 'Scripts'], cwd=ROOT, text=True).splitlines()
    audit = dict(stage='B91', increment='32 fixed low storage furniture sources',
                 full_stage_acceptance=False,
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
                                  ('furniture', 'traversal', 'tools', 'objects', 'structural', 'doors', 'rules', 'editor', 'game')},
                 limits=['Editor server with two independent headless owners; no packaged server/video/human acceptance.',
                         'Rope binding uses explicit fixture staging of an existing item; binding RPC remains separately proven.',
                         'Full-map body/object/target sight now uses 18m with inherited Blind and capped bonuses.',
                         'Foliage, socket fire, scenery density and paid environment integration remain open.'],
                 files_sha256_normalized_lf={p.relative_to(out).as_posix(): digest(p)
                                             for p in sorted(out.rglob('*')) if p.is_file()})
    (out/'Audit.json').write_text(json.dumps(audit, indent=2)+'\n', encoding='utf-8')
    print(f'PASS frozen B91 furniture checkpoint at {audit["source_commit"]}: {out}')


if __name__ == '__main__':
    main()
