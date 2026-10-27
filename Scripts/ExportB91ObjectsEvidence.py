#!/usr/bin/env python3
"""Freeze a verified storage/cargo checkpoint after its native source commit."""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes().replace(b'\r\n', b'\n')).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    for name in ('network', 'structural', 'doors', 'rules', 'editor', 'game'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT/'docs/v09/evidence/B91Objects')
    args = parser.parse_args()
    subprocess.run(['git', 'diff', '--exit-code', 'HEAD', '--', 'Source', 'Config', 'Scripts',
                    'Content/Lockdown/Data/V09'], cwd=ROOT, check=True, capture_output=True)
    untracked = subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard',
                                         'Source', 'Config', 'Scripts', 'Content/Lockdown/Data/V09'],
                                        cwd=ROOT, text=True)
    assert not untracked.strip(), 'Commit all native source/helper/contract inputs before freezing evidence'
    baseline = json.loads((ROOT/'docs/v09/evidence/B91/Audit.json').read_text())
    for key in ('unchanged_generated_assets_sha256', 'unchanged_supplied_files_sha256'):
        for name, expected in baseline[key].items():
            path = ROOT/name if key == 'unchanged_generated_assets_sha256' else ROOT/'docs/v09'/name
            assert hashlib.sha256(path.read_bytes()).hexdigest() == expected, name
    for checker, source in (('CheckB91Objects.py', args.network),
                            ('CheckB91Evidence.py', args.structural),
                            ('CheckB91Network.py', args.doors)):
        subprocess.run([sys.executable, '-B', str(ROOT/'Scripts'/checker), str(source)], check=True)
    rules = json.loads((args.rules/'Report/index.json').read_text(encoding='utf-8-sig'))
    assert rules['succeeded'] >= 51 and not any(rules[n] for n in ('failed', 'notRun', 'inProcess'))
    for source, name in ((args.editor, 'BuildEditor.log'), (args.game, 'BuildGame.log')):
        assert 'Result: Succeeded' in (source/name).read_text(encoding='utf-8-sig')
    imports = b''
    for name, flags in (('SyncB91Objects.py', ['--check']), ('TestB91ObjectsImport.py', []),
                        ('SyncB91Data.py', ['--check']), ('TestB91Import.py', []),
                        ('TestV09Import.py', []), ('TestV08Import.py', [])):
        result = subprocess.run([sys.executable, '-B', str(ROOT/'Scripts'/name), *flags], cwd=ROOT,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)
        imports += result.stdout
    out = args.output
    assert not out.exists(), 'Refusing to overwrite historical evidence'
    out.mkdir(parents=True)
    for label in ('network', 'structural', 'doors'):
        source = getattr(args, label)
        target = out/label
        target.mkdir()
        for path in sorted(source.iterdir()):
            if path.is_file() and path.suffix.lower() in ('.json', '.csv', '.log'):
                shutil.copyfile(path, target/path.name)
    for source, name in ((args.editor, 'BuildEditor.log'), (args.game, 'BuildGame.log')):
        shutil.copyfile(source/name, out/name)
    shutil.copyfile(args.rules/'Automation.log', out/'Automation.log')
    shutil.copyfile(args.rules/'Report/index.json', out/'Rules.json')
    (out/'ImportTests.log').write_bytes(imports)
    files = subprocess.check_output(['git', 'ls-files', 'Source', 'Config', 'Scripts'], cwd=ROOT, text=True).splitlines()
    audit = dict(stage='B91', increment='finite storage and crate/barrel cargo', full_stage_acceptance=False,
                 source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                 engine=baseline['engine'], platform=baseline['platform'], toolchain=baseline['toolchain'],
                 objects_contract_sha256=digest(ROOT/'Content/Lockdown/Data/V09/objects_b91.json'),
                 environment_contract_sha256=baseline['environment_contract_sha256'],
                 map_contract_sha256=baseline['map_contract_sha256'],
                 source_sha256_normalized_lf={name: digest(ROOT/name) for name in files},
                 unchanged_generated_assets_sha256=baseline['unchanged_generated_assets_sha256'],
                 unchanged_supplied_files_sha256=baseline['unchanged_supplied_files_sha256'],
                 local_artifacts={name: str(getattr(args, name).resolve()) for name in
                                  ('network', 'structural', 'doors', 'rules', 'editor', 'game')},
                 limits=['Native editor server and two independent headless clients; no packaged server or video.',
                         'Filled-break setup transfers two existing cache items into a crate.',
                         'Positive push setup refits that same cargo instance to a native clear lane.',
                         'Remaining B91 portable, foliage, field/contact and full density cases stay open.'],
                 files_sha256_normalized_lf={p.relative_to(out).as_posix(): digest(p)
                                             for p in sorted(out.rglob('*')) if p.is_file()})
    (out/'Audit.json').write_text(json.dumps(audit, indent=2)+'\n', encoding='utf-8')
    print(f'PASS frozen B91 object checkpoint at {audit["source_commit"]}: {out}')


if __name__ == '__main__':
    main()
