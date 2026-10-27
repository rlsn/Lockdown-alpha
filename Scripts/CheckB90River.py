#!/usr/bin/env python3
"""Check native contact results and actual per-tick velocity evidence."""
import csv
import hashlib
import json
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def check(directory):
    report = json.loads((directory / 'River.json').read_text(encoding='utf-8-sig'))
    payload = (ROOT / 'Content/Lockdown/Data/V09/map_b90.json').read_bytes()
    data = json.loads(payload)
    assert report['map_sha256'] == hashlib.sha256(payload).hexdigest()
    assert report['complete'] and report['dry_seed_rejected'] and report['failures'] == 0
    expected = {'heart_inset_wet', 'heart_inset_frozen', 'heart_dry_circle'}
    bridges = {b['id'] for b in data['bridges']}
    for p in data['pois']:
        if p['kind'] == 'crossing':
            expected.update(p['id'] + s for s in (('_deck', '_under_wet', '_under_frozen') if p['id'] in bridges else ('_wet', '_frozen')))
    source_routes = json.loads((ROOT / 'docs/v09/data/routes.json').read_text(encoding='utf-8-sig'))['rows']
    contacts = {}
    cross = lambda a, b: a[0]*b[1] - a[1]*b[0]
    for route in data['routes']:
        for river in data['rivers']:
            for i, (a, end) in enumerate(zip(route['points'], route['points'][1:]), 1):
                for j, (b, bend) in enumerate(zip(river['points'], river['points'][1:]), 1):
                    v, w, diff = ([q-p for p, q in zip(a, end)], [q-p for p, q in zip(b, bend)], [q-p for p, q in zip(a, b)])
                    den = cross(v, w)
                    if abs(den) < .01:
                        continue
                    t, u = cross(diff, w)/den, cross(diff, v)/den
                    if not (0 <= t <= 1 and 0 <= u <= 1):
                        continue
                    point = [p+u*q for p, q in zip(b, w)]
                    authored = next(r for r in source_routes if r['id'] == route['id'])['river_contacts']
                    assert any(c['river_id'] == river['id'] and math.dist(c['point_xy_cm'], point[:2]) < 2 for c in authored)
                    base = f"{route['id']}__{river['id']}_{i}_{j}"
                    for suffix in ('_wet', '_frozen'):
                        contacts[base+suffix] = point
    assert len(contacts) == 34
    assert sum(len(r['river_contacts']) for r in source_routes) == len(contacts)//2 == 17
    expected.update(contacts)
    rendered = report.get('rendered_heart_fixture', False)
    if rendered:
        expected = {'heart_inset_frozen'}
        assert (directory / 'RiverContact.png').stat().st_size > 1000
    cases = {c['id']: c for c in report['cases']}
    assert len(cases) == len(report['cases']) == (1 if rendered else 55) and set(cases) == expected
    trace = {key: [] for key in expected}
    for row in csv.DictReader((directory / 'RiverTrace.csv').read_text(encoding='utf-8-sig').splitlines()):
        trace[row['case']].append(row)
    for key, c in cases.items():
        rows = trace[key]
        if key in contacts:
            centre = [float(v) for v in re.findall(r'[XYZ]=(-?[0-9.]+)', c['centre_cm'])]
            assert math.dist(centre, contacts[key]) < .01
        assert c['passed'] and c['arrived'] and c['path_unchanged'] and rows
        assert c['stun_samples'] == c['falling_samples'] == 0
        assert c['wet_samples'] == sum(int(r['water']) for r in rows)
        assert c['ice_samples'] == sum(int(r['frozen']) for r in rows)
        assert c['slow_samples'] == sum(int(r['slow']) != 0 for r in rows)
        if c['residual_expected']:
            assert c['exit_wet'] and c['expiry_clear']
        assert bool(c['wet_samples']) == c['water_expected']
        if c['frozen_fixture'] and c['water_expected']:
            assert c['ice_samples'] > 10 and 293 <= c['ice_steady_peak_cm_s'] <= 295
        else:
            assert c['ice_samples'] == c['slow_samples'] == 0
        previous = None
        for row in rows:
            t, x, y, speed, cap = (float(row[k]) for k in ('elapsed_s', 'x_cm', 'y_cm', 'speed_cm_s', 'max_speed_cm_s'))
            assert all(math.isfinite(v) for v in (t, x, y, speed, cap))
            assert 0 <= speed <= 421 and int(row['stun']) == 0
            assert cap == (294 if int(row['frozen']) else 420)
            assert int(row['slow']) == (30 if int(row['frozen']) else 0)
            if previous:
                dt = t - previous[0]
                assert .0165 < dt < .0168
                assert math.hypot(x-previous[1], y-previous[2]) <= 421 * dt + .05
            previous = t, x, y
    print(f'B90 river: {len(cases)} native cases and {sum(map(len, trace.values()))} movement samples passed')


if __name__ == '__main__':
    check(Path(sys.argv[1]))
