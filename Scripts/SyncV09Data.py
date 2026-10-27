#!/usr/bin/env python3
"""Compile the B90 spatial subset. Never import v09 economy into a v08 match."""
import argparse
import csv
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'docs/v09/data'
INPUTS = ('rules', 'pois', 'routes', 'settlements', 'spawn_pads', 'buildings', 'terrain')


def encode(value):
    return (json.dumps(value, sort_keys=True, separators=(',', ':')) + '\n').encode()


def compile_map():
    raw = {name: (SOURCE / (name + '.json')).read_bytes() for name in INPUTS}
    data = {name: json.loads(value) for name, value in raw.items()}
    assert all(value['schema_version'] == 9 for value in data.values())
    allowed = {
        'pois': 'access functions id interaction_radius_cm kind legacy_alias name position_cm public_map reward_source security stage_id',
        'routes': 'bidirectional dry_for_entire_route from id kind length_cm nominal_travel_s permanent_navigation points_cm river_contacts speed_bonus_percent stage_id to width_cm',
        'spawn_pads': 'contender_slot id position_cm protected_ms respawn_policy settlement_id stage_id',
        'buildings': 'door_ids enterable floor_count id permanent_structural_collision poi_id position_cm roof_access roof_fade_cosmetic_only settlement_id size_cm stage_id wall_vision_proxy',
        'settlements': 'footprint_radius_cm id identity kind name position_cm security',
    }
    for name, fields in allowed.items():
        for row in data[name]['rows']:
            assert not set(row).difference(fields.split()), ('Unknown spatial fields', name, row['id'])
    for name, count in [('pois', 96), ('routes', 117), ('settlements', 11),
                        ('spawn_pads', 24), ('buildings', 144)]:
        rows = data[name]['rows']
        assert len(rows) == count and len({row['id'] for row in rows}) == count, name
    for name in ('pois', 'settlements', 'spawn_pads', 'buildings'):
        for row in data[name]['rows']:
            point = row['position_cm']
            assert len(point) == 3 and all(type(v) is int for v in point), ('Integer centimeters required', row['id'])
            assert max(abs(point[0]), abs(point[1])) <= 118000 and 0 <= point[2] <= 800, ('Point outside spatial contract', row['id'])
    world = data['rules']['world']
    assert world['seed'] == 9001 and world['revision'] == 1
    assert world['bounds_cm'] == [-120000, -120000, 120000, 120000]
    pois = data['pois']['rows']
    ids = {row['id'] for row in pois}
    for row in pois:
        assert row['stage_id'] in {'B90', 'B91', 'B92', 'B93', 'B94'}
        assert row['kind'] in {'settlement', 'service', 'camp', 'ruin', 'boss', 'tower',
                               'heart', 'terminal', 'crossing', 'pass', 'resource', 'shrine', 'landmark'}
        assert len(row['functions']) >= 2
    for route in data['routes']['rows']:
        assert route['from'] in ids and route['to'] in ids
        assert route['permanent_navigation'] and route['bidirectional']
        assert route['kind'] in {'road', 'trail', 'district_lane'}
        assert route['width_cm'] in (400, 800)
        assert len(route['points_cm']) >= 2
        for point in route['points_cm']:
            assert len(point) == 3 and all(type(v) is int for v in point)
            assert max(abs(point[0]), abs(point[1])) <= 118000 and 0 <= point[2] <= 800
    assert {r['contender_slot'] for r in data['spawn_pads']['rows']} == set(range(24))
    assert sum(r['enterable'] for r in data['buildings']['rows']) == 36
    points = lambda rows: [{'id': r['id'], 'position': r['position_cm']} for r in rows]
    terrain = data['terrain']
    travel_path = ROOT / 'docs/v09/atlas/TRAVEL_TABLE.csv'
    travel = list(csv.DictReader(travel_path.read_text().splitlines()))
    payload = dict(schema=9, stage='B90', profile='v09_full', seed=9001, revision=1,
                   bounds=world['bounds_cm'], speed=420, inset=2000,
                   # A fitted mesh is a separate versioned product; source coordinates stay intact.
                   fitter_revision=2,
                   pois=[dict(id=r['id'], name=r['name'], kind=r['kind'], position=r['position_cm']) for r in pois],
                   spawns=points(data['spawn_pads']['rows']),
                   settlements=[dict(id=r['id'], position=r['position_cm'], radius=r['footprint_radius_cm'])
                                for r in data['settlements']['rows']],
                   buildings=[dict(id=r['id'], position=r['position_cm'], size=r['size_cm'],
                                   enterable=r['enterable']) for r in data['buildings']['rows']],
                   routes=[dict(id=r['id'], start=r['from'], end=r['to'], width=r['width_cm'],
                                points=r['points_cm']) for r in data['routes']['rows']],
                   rivers=[dict(id=r['id'], width=r['width_cm'], bank=r['bank_ramp_width_cm'],
                                points=r['points_cm']) for r in terrain['rivers']],
                   plateaus=[dict(id=r['id'], position=r['center_cm'], radius=r['radius_cm']) for r in terrain['plateaus']],
                   bridges=[dict(id=r['id'], river=r['river_id']) for r in terrain['crossings']
                            if r['mode'] == 'permanent_bridge'],
                   wet_inset=dict(position=terrain['heart_water_inset']['center_cm'],
                                  size=terrain['heart_water_inset']['size_cm']),
                   travel=[dict(start=r['from'], end=r['to'], strategic_m=float(r['strategic_length_m']),
                                graph_seconds=float(r['nominal_walk_seconds_420cm_s'])) for r in travel],
                   source_sha256={name: hashlib.sha256(value).hexdigest() for name, value in raw.items()})
    payload['source_sha256']['travel_table'] = hashlib.sha256(travel_path.read_bytes()).hexdigest()
    return encode(payload)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    payload = compile_map()
    digest = hashlib.sha256(payload).hexdigest()
    outputs = {
        'Content/Lockdown/Data/V09/map_b90.json': payload,
        'Source/Lockdown/Private/Region/LDB90Contract.h':
            ('#pragma once\n#define LD_B90_MAP_SHA TEXT("' + digest + '")\n').encode(),
    }
    for name, value in outputs.items():
        path = ROOT / name
        if args.check:
            assert path.read_bytes().replace(b'\r\n', b'\n') == value, 'Stale B90 output: ' + name
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(value)
    print('PASS B90 spatial compilation: 96 POIs / 117 routes / 144 blocks / 24 pads; SHA256 ' + digest)


if __name__ == '__main__':
    main()
