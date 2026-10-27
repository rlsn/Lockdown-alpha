#!/usr/bin/env python3
import csv
import hashlib
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def check(directory):
    report = json.loads((directory/'Environment.json').read_text(encoding='utf-8-sig'))
    definition = json.loads((ROOT/'Content/Lockdown/Data/V09/environment_b91.json').read_bytes())
    assert report['complete'] and report['failures'] == 0
    assert report['contract'] == hashlib.sha256((ROOT/'Content/Lockdown/Data/V09/environment_b91.json').read_bytes()).hexdigest()
    assert len(report['walks']) == 52 and all(w['passed'] and w['falling_s'] == 0 for w in report['walks'])
    assert len(report['checks']) >= 65 and all(c['passed'] for c in report['checks'])
    goals = {b['id']+'_interior': [b['position'][0], b['position'][1]+1150, b['position'][2]] for b in definition['buildings']}
    roofs = [b for b in definition['buildings'] if b['id'] in definition['roof_buildings']]
    goals.update({f'permanent_stairs_{i:02d}': [b['position'][0], b['position'][1]+740, b['position'][2]+400] for i, b in enumerate(roofs)})
    assert {w['case'] for w in report['walks']} == set(goals)
    assert 'LD_B91_DOOR_COUNTS closed=72 sight=72 clear=72' in (directory/'Native.log').read_text(encoding='utf-8-sig')
    traces = {}
    for r in csv.DictReader((directory/'EnvironmentTrace.csv').read_text(encoding='utf-8-sig').splitlines()):
        traces.setdefault(r['case'], []).append(r)
    for walk in report['walks']:
        rows = traces[walk['case']]
        previous = None
        for row in rows:
            t, length = float(row['elapsed_s']), float(row['walked_cm'])
            p = [float(row[k]) for k in ('x_cm', 'y_cm', 'z_cm')]
            assert all(math.isfinite(v) for v in (*p, t, length))
            if previous:
                pt, pl, pp = previous
                assert t > pt and length >= pl and math.dist(p[:2], pp[:2]) <= 421*(t-pt)+1
            previous = t, length, p
        assert abs(previous[0]-walk['walk_seconds']) < .001 and abs(previous[1]-walk['walked_cm']) < .01
        goal = goals[walk['case']]
        assert math.dist(previous[2][:2], goal[:2]) < 60 and abs(previous[2][2]-90-goal[2]) < 20
    summary = dict(stage='B91', status='Structural/door increment passed; full stage remains open',
                   contract=report['contract'], native_checks=len(report['checks']), interior_walks=36, stair_walks=16,
                   actual_walk_seconds=sum(w['walk_seconds'] for w in report['walks']),
                   closed_door_capsules=72, closed_door_rays=72, open_door_capsules=72,
                   windows_with_eye_clearance_and_capsule_block=72, falling_seconds=0)
    (directory/'Summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print('PASS B91 structural/door increment: 36 interior walks, 16 stair walks and native transaction checks')


if __name__ == '__main__':
    check(Path(sys.argv[1]))
