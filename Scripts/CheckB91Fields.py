"""Validate bounded full-map paid fields, preserving the diagnostic boundaries."""
import json
import sys
from pathlib import Path


def check(root):
    read = lambda name: json.loads((root/name).read_text(encoding='utf-8-sig'))
    report = read('Fields.json')
    assert report['complete'] and not report['failures'] and report['phase'] == 8
    assert len(report['checks']) == len({row['case'] for row in report['checks']}) == 33 and all(row['passed'] for row in report['checks'])
    assert report['conserved']
    clients = {}
    for phase in range(1, 9):
        for role in ('A', 'B'):
            row = read(f'{role}_{phase}.json')
            assert row['fixture'] == 'fields' and row['phase'] == phase and row['completed']
            assert set(row['wire_fields']) == {'Position', 'Revision', 'States'}
            clients[role, phase] = row
    assert clients['A', 1]['owned_item'] and clients['A', 3]['owned_item']
    assert not clients['A', 4]['owned_item'] and clients['A', 5]['owned_item']
    assert not clients['A', 6]['owned_item'] and not clients['A', 8]['owned_item']
    assert clients['A', 8]['capacity_retained'] and clients['B', 8]['surface_count'] == 0
    assert all(not clients['B', p]['owned_item'] for p in range(1, 9))
    print(f"PASS B91 fields: {len(report['checks'])} native checks; paid Water/Oil, bounded allocator, restricted views, 140 conserved items")


if __name__ == '__main__':
    check(Path(sys.argv[1]))
