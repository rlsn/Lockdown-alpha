#!/usr/bin/env python3
"""Validate the bounded optional-link native checkpoint; do not close B91."""
import json
import csv
import sys
from pathlib import Path


def check(root):
    report = json.loads((root/'Traversal.json').read_text(encoding='utf-8-sig'))
    assert report['complete'] and report['failures'] == 0 and report['phase'] == 98
    assert len(report['checks']) == 55 and all(r['passed'] for r in report['checks'])
    assert report['conserved'] and report['finite_items'] == 143 and report['ladders'] == 12
    geometry = report['geometry']
    assert len(geometry) == len({r['anchor'] for r in geometry}) == 16
    assert all(r['supported'] and r['capsule_route_clear'] for r in geometry)
    fields = {'Handle', 'Kind', 'Position', 'Revision', 'State', 'PhysicalRevision', 'Endpoint'}
    reports = {}
    for phase in range(1, 9):
        reports[phase] = []
        for role in ('A', 'B'):
            data = json.loads((root/f'{role}_{phase}.json').read_text(encoding='utf-8-sig'))
            assert data['fixture'] == 'traversal' and data['completed']
            assert set(data['wire_fields']) == fields
            assert data['duplicate_same']
            assert not data['active'], (role, phase)
            reports[phase].append(data)
    for phase in (1, 3, 4, 6):
        assert sum(r['receipt_result'] == 1 for r in reports[phase]) == 1, phase
    assert reports[5][0]['receipt_result'] == 2
    assert reports[7][0]['receipt_result'] == reports[8][0]['receipt_result'] == 1
    for phase in (3, 4):
        role = 'A' if reports[phase][0]['receipt_result'] == 1 else 'B'
        with (root/f'{role}_Motion.csv').open(encoding='utf-8-sig') as stream:
            rows = [r for r in csv.DictReader(stream) if int(r['phase']) == phase]
        assert len(rows) >= 30
        extent = {n: max(float(r[n]) for r in rows)-min(float(r[n]) for r in rows)
                  for n in ('x_cm', 'z_cm', 'time_s')}
        # The receipt may share a frame with the last CharacterMovement floor
        # correction. Keep the raw owner samples and allow 10cm at that edge;
        # native endpoint checks separately require a supported body within5cm.
        assert extent['x_cm'] >= 385 and extent['z_cm'] >= 390
        assert extent['time_s'] >= 1.85, 'Owner movement was not a two-second traversal'
    print(f"PASS B91 traversal: {len(report['checks'])} native checks, 16 paired routes, two remote owners, {report['finite_items']} conserved items")


if __name__ == '__main__':
    check(Path(sys.argv[1]))
