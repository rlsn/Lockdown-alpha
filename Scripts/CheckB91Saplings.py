import json
import sys
from pathlib import Path


def check(root):
    report = json.loads((root/'Saplings.json').read_text(encoding='utf-8-sig'))
    assert report['complete'] and not report['failures'] and report['phase'] == 8
    assert len(report['checks']) == 28 and all(r['passed'] for r in report['checks'])
    assert report['conserved']
    rows = report['manifest']
    assert len(rows) == len({r['id'] for r in rows}) == len({r['finite_source'] for r in rows}) == 64
    fields = {'Handle', 'Kind', 'Position', 'Revision', 'State', 'PhysicalRevision', 'Endpoint'}
    phases = {}
    for phase in range(1, 9):
        phases[phase] = []
        for role in ('A', 'B'):
            data = json.loads((root/f'{role}_{phase}.json').read_text(encoding='utf-8-sig'))
            assert data['fixture'] == 'saplings' and data['completed'] and data['duplicate_same']
            assert set(data['wire_fields']) == fields
            phases[phase].append(data)
    assert sum(r['result'] == 1 for r in phases[1]) == 1
    assert phases[3][0]['result'] == phases[6][0]['result'] == 2
    assert phases[4][0]['result'] == phases[8][0]['result'] == 1
    assert sum(r['result'] == 2 for r in phases[7]) == 1
    print(f"PASS B91 saplings: {len(report['checks'])} native checks, 64 finite sources, two owners, 135 conserved items")


if __name__ == '__main__':
    check(Path(sys.argv[1]))
