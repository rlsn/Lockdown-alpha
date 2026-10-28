#!/usr/bin/env python3
"""Validate restricted grass snapshots without closing the full B91 gate."""
import json
import math
import re
import sys
from pathlib import Path


def check(root):
    data = json.loads((root/'Grass.json').read_text(encoding='utf-8-sig'))
    assert data['complete'] and data['phase'] == 16 and not data['failures']
    assert len(data['checks']) == 44 and all(c['passed'] for c in data['checks'])
    assert len(data['manifest']) == len({r['id'] for r in data['manifest']}) == 96
    assert all(r['supported'] for r in data['manifest'])
    for row in data['manifest']:
        points = [[float(v) for v in re.findall(r'[XYZ]=(-?\d+(?:\.\d+)?)', row[name])]
                  for name in ('source', 'fitted')]
        assert all(len(p) == 3 for p in points) and math.dist(points[0][:2], points[1][:2]) <= 1000.01
    phases = {}
    fields = {'Handle', 'PublicDoor', 'Position', 'Revision', 'State', 'Facing', 'Presentation', 'Light'}
    for phase in range(1, 17):
        phases[phase] = []
        for role in ('A', 'B'):
            row = json.loads((root/f'{role}_{phase}.json').read_text(encoding='utf-8-sig'))
            assert row['fixture'] == 'grass' and row['completed'] and row['duplicate_same']
            assert set(row['wire_fields']) == fields and row['object_count'] <= 128
            phases[phase].append(row)
    for phase in (1, 3, 5, 8, 11):
        assert not phases[phase][1]['body_seen'], phase
    for phase in (2, 4, 6, 7, 9, 10, 12, 15, 16):
        assert phases[phase][1]['body_seen'], phase
    assert sum(r['result'] == 1 for r in phases[14]) == 1
    print(f"PASS B91 grass: {len(data['checks'])} native checks, 96 finite patches, two restricted owners")


if __name__ == '__main__':
    check(Path(sys.argv[1]))
