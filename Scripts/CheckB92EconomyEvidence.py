"""Portable verification of the frozen B92 economic foundation."""
import hashlib
import sys
from pathlib import Path
from CheckB92Economy import check
from CheckEconomyEvidence import verify
from ExportB92EconomyEvidence import read


def main(root):
    root = Path(root)
    audit = read(root / 'Audit.json')
    assert audit['stage'] == 'B92' and not audit['full_stage_acceptance']
    assert audit['scope'] == 'isolated_native_economy_authority' and len(audit['source_commit']) == 40
    for name, sha in audit['files_sha256_normalized_lf'].items():
        assert hashlib.sha256((root / name).read_bytes().replace(b'\r\n', b'\n')).hexdigest() == sha, name
    check(root / 'authority')
    rules = read(root / 'Rules.json')
    assert rules['succeeded'] >= 58 and not any(rules[k] for k in ('failed', 'notRun', 'inProcess'))
    for name in ('commerce', 'institutions'):
        verify(root / name / 'Ledger.json')
        report = read(root / name / ('Commerce.json' if name == 'commerce' else 'Institutions.json'))
        assert report['failures'] == 0 and all(c.startswith('PASS ') for c in report['checks'])
    for name in ('editor', 'game'):
        assert 'Result: Succeeded' in (root / name / f'Build{name.title()}.log').read_text(encoding='utf-8-sig')
    print('PASS frozen B92 economy authority foundation:', audit['source_commit'])


if __name__ == '__main__':
    main(sys.argv[1])
