#!/usr/bin/env python3
"""Independently verify B90 native outputs; never turn estimates into walked times."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def check(directory, walk=None, compare=None, bridge_entry=None):
    report = json.loads((directory / 'NativePaths.json').read_text(encoding='utf-8-sig'))
    payload = (ROOT / 'Content/Lockdown/Data/V09/map_b90.json').read_bytes()
    source = json.loads(payload)
    assert report['stage'] == 'B90' and report['payload_sha256'] == hashlib.sha256(payload).hexdigest()
    assert report['seed'] == 9001 and report['layout_revision'] == report['fitter_revision'] == 1
    assert report['terrain_tiles'] == 144 and report['spawn_actors'] == 24 and report['failures'] == 0
    assert report['missing_ground_samples'] == 0
    assert report['max_sampled_grade'] <= .25
    contacts = report['candidate_contacts']
    assert report['candidate_capsule_contacts'] == len(contacts)
    if bridge_entry:
        bridge = json.loads((bridge_entry / 'Walk.json').read_text(encoding='utf-8-sig'))
        assert bridge['case'] == 'B90-BridgeEntry' and bridge['passed']
        assert 0 < bridge['walk_seconds'] < 15 and 900 < bridge['walked_cm'] < 1400 and bridge['speed_cm_s'] == 420
    if contacts:
        # A conservative straight capsule sweep at this edge is resolved by
        # actual CharacterMovement. Require that corroborating native run;
        # every other unresolved contact still rejects the evidence bundle.
        assert bridge_entry and len(contacts) == 1
        contact = contacts[0]
        assert (contact['from'], contact['to'], contact['actor']) == ('camp_07', 'east_bridge', 'bridge_east_bridge_014')
    expected = {p['id'] for p in source['pois'] + source['spawns']}
    pads = report['fitted_pads']
    assert len(pads) == len(expected) == 120 and {p['id'] for p in pads} == expected
    assert all(p['projected'] and p['ground_collision'] and p['ground_normal_z'] > 0 for p in pads)
    paths = list(csv.DictReader((directory / 'NativeTravel.csv').read_text(encoding='utf-8-sig').splitlines()))
    pairs = {(p['from'], p['to']) for p in paths}
    assert all((a['id'], b['id']) in pairs for a in source['spawns'] for b in source['pois'])
    assert all((e['start'], e['end']) in pairs and (e['end'], e['start']) in pairs for e in source['routes'])
    assert all((r['start'], r['end']) in pairs for r in source['travel'])
    for p in paths:
        assert p['complete'] == 'true'
        length, seconds, ms = (float(p[k]) for k in ('native_path_length_cm', 'nominal_seconds_at_420', 'query_ms'))
        assert all(math.isfinite(v) and v >= 0 for v in (length, seconds, ms))
        assert abs(length / 420 - seconds) <= .001
    assert len(paths) == report['native_path_queries']
    rows = []
    for trip in source['travel']:
        native = next(p for p in paths if (p['from'], p['to']) == (trip['start'], trip['end']))
        rows.append(dict(start=trip['start'], end=trip['end'], strategic_m=trip['strategic_m'],
                         graph_estimate_seconds=trip['graph_seconds'], native_path_m=float(native['native_path_length_cm']) / 100,
                         native_estimate_seconds=native['nominal_seconds_at_420'], walked_seconds='',
                         scope='Native Recast path; time estimate only'))
    if walk:
        walked = json.loads((walk / 'Walk.json').read_text(encoding='utf-8-sig'))
        assert walked['case'] == 'B90-T06' and walked['passed'] and 0 < walked['walk_seconds'] <= 15
        assert walked['speed_cm_s'] == 420 and 4400 < walked['walked_cm'] < 4800
        for row in rows:
            if (row['start'], row['end']) == ('antechamber', 'heart'):
                row['walked_seconds'] = walked['walk_seconds']
                row['scope'] = 'Native CharacterMovement; labelled start placement; no abilities'
    if compare:
        old = json.loads((compare / 'NativePaths.json').read_text(encoding='utf-8-sig'))
        assert old['payload_sha256'] == report['payload_sha256'] and old['fitted_pads'] == pads
        assert (compare / 'FittedGeometry.txt').read_bytes() == (directory / 'FittedGeometry.txt').read_bytes()
    with (directory / 'TravelComparison.csv').open('w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
    times = sorted(float(p['query_ms']) for p in paths)
    summary = dict(stage='B90', payload_sha256=report['payload_sha256'], native_paths=len(paths),
                   fitted_pads=len(pads), prescribed_trips=len(rows), query_p95_ms=times[math.ceil(len(times) * .95) - 1],
                   missing_ground_samples=report['missing_ground_samples'],
                   candidate_capsule_contacts=len(contacts), unresolved_capsule_contacts=0,
                   native_bridge_entry=bool(bridge_entry), max_sampled_grade=report['max_sampled_grade'],
                   walked_antechamber=bool(walk), regenerated_semantics_equal=bool(compare),
                   scope='Spatial native evidence only; no full-match, network, economy or human acceptance')
    (directory / 'Summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    parser.add_argument('--walk', type=Path)
    parser.add_argument('--compare', type=Path)
    parser.add_argument('--bridge-entry', type=Path)
    args = parser.parse_args()
    check(args.directory, args.walk, args.compare, args.bridge_entry)
