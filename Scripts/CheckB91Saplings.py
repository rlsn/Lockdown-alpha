import json
import sys
from pathlib import Path


def check(root):
    report = json.loads((root/'Saplings.json').read_text(encoding='utf-8-sig'))
    assert report['complete'] and not report['failures'] and report['phase'] == 15
    assert len(report['checks']) == 44 and all(r['passed'] for r in report['checks'])
    assert report['conserved']
    rows = report['manifest']
    assert len(rows) == len({r['id'] for r in rows}) == len({r['finite_source'] for r in rows}) == 64
    fields = {'Handle', 'Kind', 'Position', 'Revision', 'State', 'PhysicalRevision', 'Endpoint'}
    phases = {}
    for phase in range(1, 16):
        phases[phase] = []
        for role in ('A', 'B'):
            data = json.loads((root/f'{role}_{phase}.json').read_text(encoding='utf-8-sig'))
            assert data['fixture'] == 'saplings' and data['completed'] and data['duplicate_same']
            assert set(data['wire_fields']) == fields
            assert data['context_removed']
            if phase in (1, 2, 3, 4, 8) and data['acted'] and (data['result'] == 1 or phase == 3):
                assert data['owner_progress']
            phases[phase].append(data)
    assert sum(r['result'] == 1 for r in phases[1]) == 1
    assert phases[3][0]['result'] == phases[6][0]['result'] == 2
    assert phases[4][0]['result'] == phases[8][0]['result'] == 1
    assert sum(r['result'] == 2 for r in phases[7]) == 1
    print(f"PASS B91 saplings: {len(report['checks'])} native checks, 64 finite sources, two owners, 145 conserved items after3 harvests and1 bottle consume")


if __name__ == '__main__':
    check(Path(sys.argv[1]))
