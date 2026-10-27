#!/usr/bin/env python3
import csv
import hashlib
import json
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def point(text):
    p = tuple(map(float, re.findall(r'[XYZ]=(-?[0-9.]+)', text)))
    assert len(p) == 3 and all(math.isfinite(x) for x in p)
    return p


def check(directory):
    raw = (ROOT/'Content/Lockdown/Data/V09/map_b90.json').read_bytes()
    source = json.loads(raw)
    report = json.loads((directory/'Structure.json').read_text(encoding='utf-8-sig'))
    assert report['complete'] and report['failures'] == 0 and report['payload_sha256'] == hashlib.sha256(raw).hexdigest()
    assert report['site_count'] == 96 and report['service_seats'] == 36
    assert {p['id'] for p in report['sites']} == {p['id'] for p in source['pois']}
    assert all(p['floor_valid'] and p['floor_proxy'] != 'None' for p in report['sites'])
    assert report['repeat_registry_identical'] and report['repeat_import_identical']
    assert report['heart_contested_override'] and report['road_paint_instances'] > 0 and report['road_wall_overlaps'] == 0
    buildings = {b['id']: b for b in source['buildings']}
    services = {s['id']: s for s in source['services']}
    settlements = {s['id']: s for s in source['settlements']}
    for r in report['services']:
        s = services[r['id']]
        assert r['poi'] == s['poi'] and r['building'] == s['building'] and r['settlement'] == s['settlement']
        assert r['declared_functions'] == s['functions'] and r['execution_stage'] == 'B92' and not r['transactions_enabled']
        assert r['floor_valid'] and math.dist(point(r['native_cm'])[:2], s['position'][:2]) < 60
        assert len(r['hub_path_cm']) >= 2
    assert len(report['services']) == 36 and len({s['id'] for s in report['services']}) == 36
    assert {s['id'] for s in report['settlements']} == set(settlements)
    for r in report['settlements']:
        s = settlements[r['id']]
        assert r['name'] == s['name'] and r['identity'] == s['identity'] and r['kind'] == s['kind']
        assert r['core_policy'] == 'civic_core' and r['edge_policy'] == 'unwatched_edge'
        assert set(r['district_services']) == {x['id'] for x in services.values() if x['settlement'] == s['id']}
        assert len(r['district_services']) == (1 if s['kind'] == 'town' else 6)
    assert len(report['aliases']) == 16 and all(a['same_address'] and source['aliases'][a['alias']] == a['target'] for a in report['aliases'])
    assert len(report['door_clearance']) == 72 and len({(d['building'], d['side']) for d in report['door_clearance']}) == 72
    assert all(d['passed'] and len(d['three_capsule_lanes_clear']) == 3 and all(d['three_capsule_lanes_clear']) for d in report['door_clearance'])
    assert len(report['closed_shells']) == 108 and all(c['rejected'] for c in report['closed_shells'])
    assert {c['building'] for c in report['closed_shells']} == {b['id'] for b in buildings.values() if not b['enterable']}
    walks = report['walks']
    expected = {s+'__'+k for s in services for k in ('hub_to_service', 'front_to_rear')}
    if report['capture_only']:
        assert not walks and report['planned_walks'] == 0
    else:
        assert len(walks) == report['planned_walks'] == 72 and {w['case'] for w in walks} == expected
        traces = {key: [] for key in expected}
        for r in csv.DictReader((directory/'StructureTrace.csv').read_text(encoding='utf-8-sig').splitlines()):
            traces[r['case']].append(r)
        for w in walks:
            assert w['passed'] and w['falling_seconds'] == w['replans'] == 0 and 0 < w['peak_speed_cm_s'] <= 421
            assert math.dist(point(w['end_cm'])[:2], point(w['goal_cm'])[:2]) < 60
            rows = traces[w['case']]
            assert rows
            previous = None
            b = buildings[w['building']]
            for r in rows:
                t, speed, length = (float(r[k]) for k in ('elapsed_s', 'speed_cm_s', 'walked_cm'))
                p = tuple(float(r[k]) for k in ('x_cm', 'y_cm', 'z_cm'))
                assert all(math.isfinite(v) for v in (*p, t, speed, length)) and 0 <= speed <= 421
                if previous:
                    pt, pp, pl = previous
                    assert t > pt and length >= pl and math.dist(p[:2], pp[:2]) <= 421*(t-pt)+1
                previous = t, p, length
            assert abs(previous[0]-w['walk_seconds']) < .001 and abs(previous[2]-w['walked_cm']) < .01
            if w['kind'] == 'front_to_rear':
                assert w['front_crossed'] and w['rear_crossed'] and w['interior_crossed']
                assert any(math.dist([float(r['x_cm']), float(r['y_cm'])], b['position'][:2]) < 100 for r in rows)
    summary = dict(payload_sha256=report['payload_sha256'], sites=96, services=36, settlements=11, aliases=16,
                   door_sweeps=216, closed_shell_rejections=108, road_wall_overlaps=0, actual_walks=len(walks),
                   aggregate_walk_seconds=sum(w['walk_seconds'] for w in walks),
                   scope='B90 spatial service/district/doorway mapping; service transactions, guards and upper floors remain B91/B92')
    (directory/'StructureSummary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    check(Path(sys.argv[1]))
