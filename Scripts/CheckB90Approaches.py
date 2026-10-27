#!/usr/bin/env python3
"""Independently check static approach geometry and reconcile actual walks."""
import csv
import hashlib
import itertools
import json
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def point(text):
    p = [float(v) for v in re.findall(r'[XYZ]=(-?[0-9.]+)', text)]
    assert len(p) == 3 and all(math.isfinite(v) for v in p)
    return p


def remaining(points, distance):
    for a, b in zip(reversed(points), reversed(points[:-1])):
        length = math.dist(a, b)
        if length and distance <= length:
            return [x+(y-x)*distance/length for x, y in zip(a, b)]
        distance -= length
    return points[0]


def corridor_distance(p, path):
    best = math.inf
    for a, b in zip(path, path[1:]):
        v = [y-x for x, y in zip(a[:2], b[:2])]
        length2 = sum(x*x for x in v)
        t = max(0, min(1, sum((p[i]-a[i])*v[i] for i in range(2))/length2)) if length2 else 0
        best = min(best, math.dist(p, [a[i]+t*v[i] for i in range(2)]))
    return best


def check(directory):
    raw = (ROOT/'Content/Lockdown/Data/V09/map_b90.json').read_bytes()
    source = json.loads(raw)
    report = json.loads((directory/'Approaches.json').read_text(encoding='utf-8-sig'))
    walked = json.loads((directory/'Traversal.json').read_text(encoding='utf-8-sig'))
    assert report['payload_sha256'] == hashlib.sha256(raw).hexdigest()
    assert report['body_radius_cm'] == 1800 and report['watch_radius_cm'] == 2400
    assert report['eye_height_cm'] == 120 and report['distinct_corridor_separation_cm'] == 600
    expected = {p['id'] for p in source['pois'] if p['kind'] in ('ruin', 'boss', 'heart') or p['id'] in ('heart_forge', 'ironworks_forge')}
    assert len(expected) == len(report['targets']) == 7 and {t['target'] for t in report['targets']} == expected
    assert len(report['static_shell_calibration']) == 144
    buildings = {b['id']: b for b in source['buildings']}
    assert {c['building'] for c in report['static_shell_calibration']} == set(buildings)
    for c in report['static_shell_calibration']:
        assert c['passed'] and c['wall_blocks'] and c['enterable'] == buildings[c['building']]['enterable']
        assert c['door_axis_clear'] == c['enterable']
    assert len(report['geometry_contract']) == 7 and all(report['geometry_contract'].values())
    trips = {(t['from'], t['to']): t for t in walked['trips']}
    traces = {}
    for row in csv.DictReader((directory/'TraversalTrace.csv').read_text(encoding='utf-8-sig').splitlines()):
        traces.setdefault(row['trip'], []).append(row)
    total_branches = total_pairs = blocked = ranged = distinct_pairs = ray_count = 0
    max_walk_deviation = 0
    for target in report['targets']:
        branches = target['branches']
        routes = [r for r in source['routes'] if target['target'] in (r['start'], r['end'])]
        assert len(branches) == len(routes) >= 2
        assert {b['route'] for b in branches} == {r['id'] for r in routes}
        total_branches += len(branches)
        paths = []
        for b in branches:
            route = next(r for r in routes if r['id'] == b['route'])
            assert b['from'] == (route['end'] if route['start'] == target['target'] else route['start'])
            path = list(map(point, b['points_cm']))
            assert len(path) >= 2 and math.dist(path[-1][:2], point(target['goal_cm'])[:2]) < 1
            length = sum(map(lambda ab: math.dist(*ab), zip(path, path[1:])))
            # Floors are fitted locally rather than interpolating the Z chord
            # between remote path corners; allow the <=25% grade length delta.
            assert 500 <= length <= 6200 and b['full_native_path_cm'] >= length-200
            assert abs(b['local_native_path_cm']-length) <= 200
            assert max(math.dist(a, c) for a, c in zip(path, path[1:])) <= 125
            paths.append(path)
            trip = trips.get((b['from'], target['target']))
            if trip:
                assert trip['passed'] and trip['falling_seconds'] == trip['replans'] == 0
                start = traces[b['from']+'__'+target['target']][0]
                assert math.dist([float(start['x_cm']), float(start['y_cm'])], path[0][:2]) < 10
                deviation = max(corridor_distance([float(r['x_cm']), float(r['y_cm'])], path)
                                for r in traces[b['from']+'__'+target['target']])
                assert deviation <= 150, f"Walk departed inspected corridor: {b['from']} -> {target['target']}"
                max_walk_deviation = max(max_walk_deviation, deviation)
                # A newly planned tail may shorten a winding original approach;
                # actual native movement still has to arrive without relocation.
                assert trip['walked_cm'] > 500 and trip['walk_seconds'] < 25
                assert abs(trip['native_path_cm']-b['local_native_path_cm']) < 10
        assert {(p['a'], p['b']) for p in target['pairs']} == set(itertools.combinations(range(len(branches)), 2))
        distinct = []
        for pair in target['pairs']:
            total_pairs += 1
            independent = 0
            for s in pair['samples']:
                a, b = point(s['a_cm']), point(s['b_cm'])
                assert math.dist(a, remaining(paths[pair['a']], s['remaining_cm'])) < .2
                assert math.dist(b, remaining(paths[pair['b']], s['remaining_cm'])) < .2
                distance = math.dist(a[:2], b[:2])
                assert abs(distance-s['separation_cm']) < .02
                if s['remaining_cm'] in (1800, 3000) and s['separation_cm'] >= 600:
                    independent += 1
                for key, radius in (('body', 1800), ('watch', 2400)):
                    ray_count += 1
                    ray = s[key]
                    assert ray['result'] in ('clear', 'geometry_blocked', 'out_of_range')
                    if abs(distance-radius) > .05:
                        assert (ray['result'] == 'out_of_range') == (distance > radius)
                    if ray['result'] == 'geometry_blocked':
                        assert ray['blocker'] != 'None'
                        hit = point(ray['hit_cm'])
                        eye_a, eye_b = [*a[:2], a[2]+120], [*b[:2], b[2]+120]
                        assert abs(math.dist(eye_a, hit)+math.dist(hit, eye_b)-math.dist(eye_a, eye_b)) < .1
                        blocked += 1
                    ranged += ray['result'] == 'out_of_range'
            assert pair['distinct'] == (independent == 2)
            distinct_pairs += pair['distinct']
            distinct.append(pair['distinct'])
        assert target['has_alternative'] == any(distinct)
        assert target['has_alternative'], f"No distinct native branch pair at {target['target']}"
    assert total_branches == 23 and total_pairs == 36
    summary = dict(payload_sha256=report['payload_sha256'], targets=len(expected), incoming_branches=total_branches,
                   branch_pairs=total_pairs, distinct_pairs=distinct_pairs, merged_pairs=total_pairs-distinct_pairs,
                   geometry_blocked_rays=blocked, out_of_range_rays=ranged, clear_rays=ray_count-blocked-ranged,
                   primitive_shells_checked=144, geometry_boundary_checks=7,
                   actual_walks=len(trips), all_branches_walked=len(trips)==23,
                   max_recorded_walk_distance_from_inspected_centerline_cm=max_walk_deviation,
                   aggregate_walk_seconds=sum(t['walk_seconds'] for t in trips.values()),
                   scope='Static native approach inspection; sampled range failure is not cover; no unit authorization, full match or human pacing claim')
    (directory/'ApproachSummary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
    print(f'B90 approaches: {len(expected)} targets / {total_branches} branches / {total_pairs} pairs; {blocked} blocked and {ranged} out-of-range static rays; {len(trips)} actual walks')


if __name__ == '__main__':
    check(Path(sys.argv[1]))
