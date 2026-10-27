#!/usr/bin/env python3
"""Replay actual B90 movement traces; distinguish spatial fixtures from gameplay."""
import argparse
import csv
import hashlib
import json
import math
import re
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def point(text):
    match = re.fullmatch(r'X=([-\d.]+) Y=([-\d.]+) Z=([-\d.]+)', text)
    assert match, text
    result = tuple(map(float, match.groups()))
    assert all(math.isfinite(v) for v in result)
    return result


def verify(directory):
    payload = (ROOT / 'Content/Lockdown/Data/V09/map_b90.json').read_bytes()
    source = json.loads(payload)
    report = json.loads((directory / 'Traversal.json').read_text(encoding='utf-8-sig'))
    assert report['complete'] and report['failures'] == 0
    assert report['stage'] == 'B90' and report['payload_sha256'] == hashlib.sha256(payload).hexdigest()
    assert report['speed_cm_s'] == 420 and report['empty_anchor_radius_cm'] == 5000
    assert all(report['navigation_contract'].values()) and len(report['navigation_contract']) == 3
    assert abs(report['min_tick_seconds'] - 1 / 60) < .00001
    assert abs(report['max_tick_seconds'] - 1 / 60) < .00001
    pois = {p['id']: p['position'] for p in source['pois']}
    graph_trips = {(p['start'], p['end']): p for p in source['travel']}
    expected = [(p['start'], p['end']) for p in source['travel']]
    for city in source['settlements']:
        if city['id'] == 'capital' or city['radius'] < 10000:
            continue
        towns = sorted((p for p in source['settlements'] if p['radius'] < 10000),
                       key=lambda p: (math.dist(p['position'][:2], city['position'][:2]), p['id']))
        expected.extend((city['id'], p['id']) for p in towns[:2])
    expected.extend([('capital', 'highfort'), ('capital_ford', 'heart'), ('heart_dry_flank', 'heart')])
    if report['lane'] == 'Approaches':
        expected = [(r['end'] if r['start'] == p['id'] else r['start'], p['id'])
                    for p in source['pois'] if p['kind'] in ('ruin', 'boss', 'heart') or p['id'] in ('heart_forge', 'ironworks_forge')
                    for r in source['routes'] if p['id'] in (r['start'], r['end'])]
    if report['trip_filter']:
        expected = [p for p in expected if '__'.join(p) == report['trip_filter']]
    if report['lane'] == 'Bounds':
        expected = []
    actual = [(r['from'], r['to']) for r in report['trips']]
    assert actual == expected and report['planned_trips'] == len(expected)
    traces = defaultdict(list)
    with (directory / 'TraversalTrace.csv').open(encoding='utf-8-sig', newline='') as f:
        for row in csv.DictReader(f):
            traces[row['trip']].append(row)
    assert set(traces) == {'__'.join(p) for p in expected}
    comparisons = []
    for trip in report['trips']:
        assert trip['passed'] and 0 < trip['walk_seconds'] and 0 < trip['peak_speed_cm_s'] <= 421
        assert trip['replans'] <= 2 and trip['native_path_cm'] > 0
        rows = traces['__'.join((trip['from'], trip['to']))]
        assert len(rows) == trip['trace_samples'] >= 2
        previous = None
        gap_start = None
        last_far = 0
        sampled_gap = 0
        for row in rows:
            t, speed, walked = (float(row[k]) for k in ['elapsed_s', 'speed_cm_s', 'walked_cm'])
            pos = tuple(float(row[k]) for k in ['x_cm', 'y_cm', 'z_cm'])
            assert all(math.isfinite(v) for v in (*pos, t, speed, walked))
            assert max(abs(pos[0]), abs(pos[1])) <= 118000 and -100 <= pos[2] <= 1600
            assert 0 <= speed <= 421
            nearest = min(math.dist(pos[:2], p[:2]) for p in pois.values())
            assert abs(nearest - float(row['distance_to_poi_cm'])) < .01
            assert abs(math.dist(pos[:2], pois[row['nearest_poi']][:2]) - nearest) < .01
            if nearest > 5000:
                if gap_start is None:
                    gap_start = t
                last_far = t
                sampled_gap = max(sampled_gap, t - gap_start)
            else:
                if gap_start is not None:
                    sampled_gap = max(sampled_gap, last_far - gap_start)
                gap_start = None
            if previous:
                pt, pp, length = previous
                assert t >= pt and walked >= length
                chord = math.dist(pos[:2], pp[:2])
                assert chord <= (t - pt) * 421 + 1
                assert walked - length >= chord - .02
            previous = t, pos, walked
        assert abs(previous[0] - trip['walk_seconds']) < .002
        assert abs(previous[2] - trip['walked_cm']) < .01
        assert sampled_gap - .1 <= trip['longest_empty_s'] <= sampled_gap + 2.1
        assert sum(row['static_water_geometry'] == 'true' for row in rows) == trip['static_water_samples']
        assert math.dist(previous[1][:2], pois[trip['to']][:2]) < 60
        end = point(trip['end_cm'])
        assert math.dist(previous[1], end) < .01
        graph = graph_trips.get((trip['from'], trip['to']), {})
        comparisons.append(dict(start=trip['from'], end=trip['to'], kind=trip['kind'],
                                strategic_graph_m=graph.get('strategic_m', ''),
                                graph_estimate_seconds=graph.get('graph_seconds', ''),
                                native_path_m=round(trip['native_path_cm'] / 100, 3),
                                path_estimate_seconds=round(trip['path_estimate_s'], 4),
                                measured_walk_seconds=round(trip['walk_seconds'], 4),
                                measured_walk_m=round(trip['walked_cm'] / 100, 3),
                                longest_poi_anchor_gap_seconds=round(trip['longest_empty_s'], 3),
                                navigation_replans=trip['replans']))
    boundaries = ([f'border_{x}_{y}' for y in [-1, 0, 1] for x in [-1, 0, 1] if x or y]
                  + [p['id'] for p in source['plateaus']]) if report['lane'] not in ('Routes', 'Approaches') else []
    assert [r['id'] for r in report['boundaries']] == boundaries
    assert report['planned_boundaries'] == len(boundaries)
    for row in report['boundaries']:
        assert row['passed'] and row['native_dash_accepted'] and row['dash_cm'] <= 361
        assert 0 <= row['outward_push_cm'] <= 1001 and 0 <= row['inward_push_cm'] <= 201
        p = point(row['end_cm'])
        assert max(abs(p[0]), abs(p[1])) <= 118000 and -100 <= p[2] <= 1600
    observed = set()
    with (directory / 'BoundaryTrace.csv').open(encoding='utf-8-sig', newline='') as f:
        for row in csv.DictReader(f):
            assert row['id'] in boundaries and row['safe_endpoint'] == 'true'
            observed.add(row['id'])
            p = [float(row[k]) for k in ['x_cm', 'y_cm', 'z_cm']]
            assert max(abs(p[0]), abs(p[1])) <= 118000 and -100 <= p[2] <= 1600
    assert observed == set(boundaries)
    with (directory / 'MeasuredTravel.csv').open('w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=list(comparisons[0]) if comparisons else ['start', 'end'])
        writer.writeheader(); writer.writerows(comparisons)
    summary = dict(stage='B90', lane=report['lane'], subset=report['trip_filter'],
                   trips=len(expected), boundaries=len(boundaries),
                   simulated_walk_seconds=sum(t['walk_seconds'] for t in report['trips']),
                   longest_poi_anchor_gap_seconds=max((t['longest_empty_s'] for t in report['trips']), default=0),
                   navigation_replans=sum(t['replans'] for t in report['trips']),
                   scope='Actual native motion with diagnostic starting placements and fixed60Hz accelerated wall execution. POI gap is a geometric proxy; no Wet/frozen gameplay or human pacing claim.')
    (directory / 'TraversalSummary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    verify(parser.parse_args().directory)
