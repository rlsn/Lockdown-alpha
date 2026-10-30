"""Freeze official supply and affected native regressions and finite deposits as a new checkpoint."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path
from CheckB92Services import check
from CheckB92Economy import check as economy
from CheckB92Roster import check as roster
from CheckB91Saplings import check as sources
from CheckB91Combat import check as combat
from ExportB92EconomyEvidence import ROOT, read, digest


def validate(root):
    check(root / 'services')
    economy(root / 'economy')
    roster(root / 'roster')
    sources(root / 'sources')
    combat(root / 'combat')
    assert read(root / 'services/Services.json')['survey_integration']
    assert (root / 'services/Market.png').is_file() and (root / 'services/Survey.png').is_file()
    rules = read(root / 'rules/Report/index.json')
    assert rules['succeeded'] >= 59 and not any(rules[k] for k in ('failed', 'notRun', 'inProcess'))
    for name in ('editor', 'game'):
        assert 'Result: Succeeded' in (root / name / f'Build{name.title()}.log').read_text(encoding='utf-8-sig')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('path', type=Path)
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    if args.verify:
        audit = read(args.path / 'Audit.json')
        assert audit['scope'] == 'prepared_survey_extraction' and not audit['full_stage_acceptance']
        for name, sha in audit['files'].items():
            assert digest(args.path / name) == sha, name
        validate(args.path)
        print('PASS frozen B92 Survey:', audit['source_commit'])
        return
    runs = {k: Path(v) for k, v in read(args.path).items()}
    check(runs['services'])
    assert read(runs['services'] / 'Services.json')['survey_integration']
    baseline = read(ROOT / 'docs/v09/evidence/B91/Audit.json')
    for key in ('unchanged_generated_assets_sha256', 'unchanged_supplied_files_sha256'):
        for name, sha in baseline[key].items():
            path = ROOT / name if key.startswith('unchanged_generated') else ROOT / 'docs/v09' / name
            assert hashlib.sha256(path.read_bytes()).hexdigest() == sha, name
    paths = ['Source', 'Config', 'Scripts']
    subprocess.run(['git', 'diff', '--exit-code', 'HEAD', '--', *paths], check=True, capture_output=True)
    assert not subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard', *paths]).strip()
    output = ROOT / 'docs/v09/evidence/B92Survey'
    assert not output.exists(), 'Frozen evidence is immutable'
    output.mkdir()
    for name, folder in runs.items():
        dest = output / name
        dest.mkdir()
        for file in folder.iterdir():
            if file.is_file() and file.suffix in ('.json', '.log', '.png'):
                shutil.copyfile(file, dest / file.name)
    (output / 'rules/Report').mkdir()
    shutil.copyfile(runs['rules'] / 'Report/index.json', output / 'rules/Report/index.json')
    validate(output)
    (output / 'README.md').write_text('# B92 finite deposits\n\nSee [scope and limitations](../../B92_SURVEY.md). The connected scenario includes commodity regression, two remote owners and replacement-seat takeover. Travel and later source preparation remain fixtures. Three remote basic POI channels execute normally; an explicit authority fixture drains the remaining charges into real ground items for budget closure. Prepared Survey shares those same charges and uses the inherited one-second/six-second channel/cooldown path. Official goods and global CircleIII stock use ordinary remote service requests. A labelled fixture transfers400 existing reserve gold without mint; this is not an earned-acquisition journey. Full B92 acceptance remains open.\n', encoding='utf-8')
    files = subprocess.check_output(['git', 'ls-files', *paths], text=True).splitlines()
    audit = dict(scope='prepared_survey_extraction', full_stage_acceptance=False,
                 source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
                 source_sha256_normalized_lf={p: digest(ROOT / p) for p in files},
                 protected_assets=baseline['unchanged_generated_assets_sha256'],
                 protected_spec=baseline['unchanged_supplied_files_sha256'],
                 files={p.relative_to(output).as_posix(): digest(p) for p in output.rglob('*') if p.is_file()})
    (output / 'Audit.json').write_text(json.dumps(audit, indent=2)+'\n', encoding='utf-8')
    print('PASS B92 Survey evidence and207 protected inputs')


if __name__ == '__main__':
    main()
