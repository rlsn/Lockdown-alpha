#!/usr/bin/env python3
"""Freeze the source/pointer control checkpoint without rewriting prior evidence."""
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
    required = ('controls', 'rules', 'editor', 'game', 'objects', 'structural', 'ignition')
    optional = ('doors', 'tools', 'legacy', 'input', 'inventory')
    parser.add_argument('--visual', type=Path)
    for name in required:
        parser.add_argument('--'+name, type=Path, required=True)
    for name in optional:
        parser.add_argument('--'+name, type=Path)
    parser.add_argument('--output', type=Path, default=ROOT/'docs/v09/evidence/B91Controls')
    args = parser.parse_args()
    subprocess.run(['git', 'diff', '--exit-code', 'HEAD', '--', 'Source', 'Config', 'Scripts', 'Content/Lockdown/Data/V09'], cwd=ROOT, check=True, capture_output=True)
    assert not subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard', 'Source', 'Config', 'Scripts', 'Content/Lockdown/Data/V09'], cwd=ROOT).strip()
    baseline = json.loads((ROOT/'docs/v09/evidence/B91/Audit.json').read_text())
    for key in ('unchanged_generated_assets_sha256', 'unchanged_supplied_files_sha256'):
        for name, expected in baseline[key].items():
            path = ROOT/name if key == 'unchanged_generated_assets_sha256' else ROOT/'docs/v09'/name
            assert hashlib.sha256(path.read_bytes()).hexdigest() == expected, name
    checks = {'controls': 'CheckB91Saplings.py', 'objects': 'CheckB91Objects.py', 'structural': 'CheckB91Evidence.py', 'ignition': 'CheckB91Ignition.py', 'doors': 'CheckB91Network.py', 'tools': 'CheckB91Tools.py'}
    for name, checker in checks.items():
        source = getattr(args, name)
        if source:
            subprocess.run([sys.executable, '-B', str(ROOT/'Scripts'/checker), str(source)], check=True)
    for label, minimums in (('input', (15, 19, 19)), ('inventory', (59, 26, 26))):
        source = getattr(args, label)
        if source:
            for role, minimum in zip(('Server', 'A', 'B'), minimums):
                raw = (source/(role+'.json')).read_bytes()
                data = json.loads(raw.decode('utf-16' if raw.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig'))
                assert not data['failures'] and len(data['checks']) >= minimum
    rules = json.loads((args.rules/'Report/index.json').read_text(encoding='utf-8-sig'))
    assert rules['succeeded'] >= 54 and not any(rules[n] for n in ('failed', 'notRun', 'inProcess'))
    for source, name in ((args.editor, 'BuildEditor.log'), (args.game, 'BuildGame.log')):
        assert 'Result: Succeeded' in (source/name).read_text(encoding='utf-8-sig')
    out = args.output
    assert not out.exists(), 'Historical evidence cannot be overwritten'
    out.mkdir(parents=True)
    labels = ('controls', 'objects', 'structural', 'ignition', *optional)
    for label in labels:
        source = getattr(args, label)
        if not source:
            continue
        target = out/label
        target.mkdir()
        for path in sorted(source.iterdir()):
            if path.is_file() and path.suffix.lower() in ('.json', '.csv', '.log', '.png'):
                if path.suffix.lower() == '.log':
                    text = path.read_text(encoding='utf-8-sig')
                    assert not any(m in text for m in ('appError called:', '=== Critical error: ===', 'Fatal error:', 'Unhandled Exception:', 'LD_ECONOMY_RECORD_ERROR', 'LD_ECONOMY_RECORD_FAILED')), path
                shutil.copyfile(path, target/path.name)
    if args.visual:
        visual = out/'visual'
        visual.mkdir()
        for name in ('SourceChannel.png', 'Saplings.json', 'A_4.json'):
            shutil.copyfile(args.visual/name, visual/name)
        (visual/'README.md').write_text('The720p frame predates the final friendly binding caption. It is visual inspection only. This rendered scenario cancelled its phase4 channel through the existing state/movement/damage/connection guard and failed two assertions. It is not a passed functional fixture or performance acceptance. The separate controls/ headless scenario is the functional check.\n', encoding='utf-8')
    if args.legacy:
        from CheckEconomyEvidence import verify
        verify(args.legacy/'Ledger.json')
        for role in ('Server', 'A', 'B'):
            raw = (args.legacy/f'{role}.json').read_bytes()
            data = json.loads(raw.decode('utf-16' if raw.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig'))
            assert not data['failures'] and len(data['checks']) >= 10
    for source, name in ((args.editor, 'BuildEditor.log'), (args.game, 'BuildGame.log')):
        shutil.copyfile(source/name, out/name)
    shutil.copyfile(args.rules/'Automation.log', out/'Automation.log')
    shutil.copyfile(args.rules/'Report/index.json', out/'Rules.json')
    imports = b''
    for name, flags in (('SyncB91Forestry.py', ['--check']), ('TestB91TraversalImport.py', []), ('TestB91ToolsImport.py', []), ('TestB91ObjectsImport.py', []), ('TestB91Import.py', []), ('TestV09Import.py', []), ('TestV08Import.py', [])):
        imports += subprocess.check_output([sys.executable, '-B', str(ROOT/'Scripts'/name), *flags], cwd=ROOT, stderr=subprocess.STDOUT)
    (out/'ImportTests.log').write_bytes(imports)
    (out/'README.md').write_text('# B91 source and pointer controls\n\nAudit.json pins the committed native source and evidence hashes.\ncontrols/: two remote owners call the same one-shot commands used by keyboard/UI; no physical key is held. Existing knives and capacity are explicit fixture staging.\nThe structural broken-open state is diagnostic setup; source-free Force/Lockpick are refused.\nOther directories are current-source regression fixtures. No whole-stage, human or performance acceptance is claimed.\n', encoding='utf-8')
    files = subprocess.check_output(['git', 'ls-files', 'Source', 'Config', 'Scripts'], cwd=ROOT, text=True).splitlines()
    audit = dict(stage='B91', increment='Item and pointer interactions with single-confirmation channels', full_stage_acceptance=False,
                 source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                 engine=baseline['engine'], platform=baseline['platform'], toolchain=baseline['toolchain'],
                 forestry_contract_sha256=digest(ROOT/'Content/Lockdown/Data/V09/forestry_b91.json'),
                 world_contract=json.loads((args.controls/'Saplings.json').read_text())['contract'],
                 source_sha256_normalized_lf={name: digest(ROOT/name) for name in files},
                 unchanged_generated_assets_sha256=baseline['unchanged_generated_assets_sha256'],
                 unchanged_supplied_files_sha256=baseline['unchanged_supplied_files_sha256'],
                 local_artifacts={name: str(getattr(args, name).resolve()) for name in (*required, *optional) if getattr(args, name)},
                 limits=['The separate rendered run failed two phase4 assertions after channel cancellation; its frame is layout inspection only, not functional or performance acceptance.',
                         'The two-owner fixture invokes actual player command methods without physical key hold; this is not human input or readability acceptance.',
                         'The B91 profile has no live traders/economy; B92 service actors must use the shared left-click service convention.',
                         'Grass, ranged sapling damage, broader spells and saved-match restoration remain open.'],
                 files_sha256_normalized_lf={p.relative_to(out).as_posix(): digest(p) for p in sorted(out.rglob('*')) if p.is_file()})
    (out/'Audit.json').write_text(json.dumps(audit, indent=2)+'\n', encoding='utf-8')
    print('PASS frozen B91 control checkpoint at '+audit['source_commit'])


if __name__ == '__main__':
    main()
