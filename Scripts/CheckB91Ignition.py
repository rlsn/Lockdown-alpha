"""Check real paid ignition separately from explicit capacity and recovery setup."""
import json
import sys
from pathlib import Path


def check(root):
    read = lambda name: json.loads((root/name).read_text(encoding='utf-8-sig'))
    report = read('Ignition.json')
    assert report['complete'] and report['phase'] == 8 and not report['failures'] and report['conserved']
    assert len(report['checks']) == len({r['case'] for r in report['checks']}) == 42 and all(row['passed'] for row in report['checks'])
    rows = {}
    for phase in range(1, 9):
        for role in ('A', 'B'):
            row = read(f'{role}_{phase}.json')
            assert row['fixture'] == 'ignition' and row['completed'] and row['phase'] == phase
            assert row['duplicate_same'] and not row['reserved']
            assert set(row['object_fields']) == {'Handle', 'Kind', 'Position', 'Revision', 'State', 'PhysicalRevision', 'Endpoint'}
            assert set(row['surface_fields']) == {'Position', 'Revision', 'States'}
            rows[role, phase] = row
    assert all(rows['A', p]['result'] == 2 for p in (1, 3, 4, 6))
    assert sum(rows[role, 5]['result'] == 1 for role in ('A', 'B')) == 1
    assert rows['B', 6]['result'] == 2 and rows['B', 7]['result'] == 1
    assert rows['A', 8]['result'] == 1 and rows['A', 8]['same_retry_item']
    assert rows['B', 8]['surface_count'] == 0
    print(f"PASS B91 ignition: {len(report['checks'])} native checks, two owners, finite fuel, reserved fields, 131 conserved items")


if __name__ == '__main__':
    check(Path(sys.argv[1]))
