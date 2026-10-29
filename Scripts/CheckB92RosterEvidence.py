"""Portable integrity/receipt validation for frozen B92 roster evidence."""
import hashlib
import sys
from pathlib import Path
from CheckB92Roster import check as roster
from CheckB92Economy import check as economy
from CheckB91Saplings import check as sources
from ExportB92EconomyEvidence import read


def main(root):
    root = Path(root)
    audit = read(root / 'Audit.json')
    assert audit['stage'] == 'B92' and not audit['full_stage_acceptance']
    assert audit['scope'] == 'native_population_admission_and_lifecycle' and len(audit['source_commit']) == 40
    for name, sha in audit['files_sha256_normalized_lf'].items():
        assert hashlib.sha256((root / name).read_bytes().replace(b'\r\n', b'\n')).hexdigest() == sha, name
    roster(root / 'humans4')
    roster(root / 'humans12')
    economy(root / 'economy')
    sources(root / 'sources')
    rules = read(root / 'Rules.json')
    assert rules['succeeded'] >= 58 and not any(rules[k] for k in ('failed', 'notRun', 'inProcess'))
    for name in ('editor', 'game'):
        assert 'Result: Succeeded' in (root / name / f'Build{name.title()}.log').read_text(encoding='utf-8-sig')
    print('PASS frozen B92 roster:', audit['source_commit'])


if __name__ == '__main__':
    main(sys.argv[1])
