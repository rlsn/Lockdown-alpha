"""Portable replay and integrity check of the frozen B92 services increment."""
import hashlib
import sys
from pathlib import Path
from ExportB92ServicesEvidence import validate
from ExportB92EconomyEvidence import read


def main(root):
    root = Path(root)
    audit = read(root / 'Audit.json')
    assert audit['stage'] == 'B92' and not audit['full_stage_acceptance']
    assert audit['scope'] == 'remote_takeover_and_five_physical_markets' and len(audit['source_commit']) == 40
    for name, sha in audit['files_sha256_normalized_lf'].items():
        assert hashlib.sha256((root / name).read_bytes().replace(b'\r\n', b'\n')).hexdigest() == sha, name
    runs = {name: root / name for name in ('services', 'humans4', 'humans12', 'economy', 'sources', 'editor', 'game')}
    # The portable bundle retains the exact exported automation index.
    rules = read(root / 'Rules.json')
    runs['rules'] = root / 'rules'
    assert rules == read(runs['rules'] / 'Report/index.json')
    validate(runs)
    print('PASS frozen B92 services:', audit['source_commit'])


if __name__ == '__main__':
    main(sys.argv[1])
