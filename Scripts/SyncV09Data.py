#!/usr/bin/env python3
"""Compile the B90 spatial subset. Never import v09 economy into a v08 match."""
import argparse
import csv
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'SourceData/World'
TRAVEL = SOURCE / 'travel_table.csv'
INPUTS = ('rules', 'pois', 'routes', 'settlements', 'spawn_pads', 'buildings', 'terrain',
          'services', 'security', 'legacy_aliases')


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
        'services': 'functions id poi_id protected_official queue shared_stock_pool stage_id',
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
    poi_functions = set('advanced_forge ambush_and_retreat archive_circle3 bank bounded_combat charter_purchase circle1_books circle2_books civilian_rest civilian_work_and_trade core_procurement courier_board covered_detour craft_interception crime_reporting finite_Crystal_harvest finite_Fiber_harvest finite_Ore_harvest fragment_and_book guards heart_forge heartbreaker_activation keys_activation local_public_watch market official_supply patrol_cover preparation public_attempt_sensor recovery route_choice route_hub safe_route_information shrine siege_gate single_guardian_boss single_physical_claim supply_commission supply_storage tactical_vantage trade_route_interception trainer_circle2 unique_key_catalyst_book_cache vision_or_exposure_control witness workshop'.split())
    for row in pois:
        assert row['stage_id'] in {'B90', 'B91', 'B92', 'B93', 'B94'}
        assert row['kind'] in {'settlement', 'service', 'camp', 'ruin', 'boss', 'tower',
                               'heart', 'terminal', 'crossing', 'pass', 'resource', 'shrine', 'landmark'}
        assert len(row['functions']) >= 2
        assert set(row['functions']) <= poi_functions, ('Unknown POI function', row['id'])
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
    buildings = data['buildings']['rows']
    hosts = {b['poi_id']: b for b in buildings if b['enterable']}
    assert len(hosts) == 36 and all(b['poi_id'] in ids for b in buildings if b['enterable'])
    settlements = {s['id']: s for s in data['settlements']['rows']}
    assert all(b['settlement_id'] in settlements for b in buildings)
    for settlement in settlements:
        local = [b for b in buildings if b['settlement_id'] == settlement]
        city = settlements[settlement]['kind'] in {'capital', 'city'}
        assert len(local) == (24 if city else 4)
        assert sum(b['enterable'] for b in local) == (6 if city else 1)
    services = data['services']['rows']
    functions = set('bank charter_purchase core_procurement official_supply circle1_books circle2_books civilian_rest supply_commission courier_board witness workshop preparation shrine safe_route_information guards crime_reporting trainer_circle2 market archive_circle3 advanced_forge'.split())
    assert len(services) == len({s['id'] for s in services}) == 36
    assert {s['poi_id'] for s in services} == set(hosts)
    for service in services:
        assert service['stage_id'] == 'B92' and service['protected_official']
        assert service['functions'] and set(service['functions']) <= functions, ('Unknown service function', service['id'])
    aliases = data['legacy_aliases']['aliases']
    assert len(aliases) == 16 and set(aliases.values()) <= ids
    security = data['security']
    assert security['stage_id'] == 'B92'
    assert security['civic_zone_radius_cm'] == dict(capital=12000, city=7000, town=3000)
    assert security['heart_contested_override_radius_cm'] == 7000
    points = lambda rows: [{'id': r['id'], 'position': r['position_cm']} for r in rows]
    terrain = data['terrain']
    travel_path = TRAVEL
    travel = list(csv.DictReader(travel_path.read_text(encoding='utf-8-sig').splitlines()))
    payload = dict(schema=9, stage='B90', profile='v09_full', seed=9001, revision=1,
                   bounds=world['bounds_cm'], speed=420, inset=2000,
                   # A fitted mesh is a separate versioned product; source coordinates stay intact.
                   fitter_revision=3,
                   pois=[dict(id=r['id'], name=r['name'], kind=r['kind'], position=r['position_cm'],
                              functions=r['functions'], security=r['security']) for r in pois],
                   spawns=points(data['spawn_pads']['rows']),
                   settlements=[dict(id=r['id'], name=r['name'], kind=r['kind'], identity=r['identity'],
                                     security=r['security'], civic_radius=security['civic_zone_radius_cm'][r['kind']],
                                     position=r['position_cm'], radius=r['footprint_radius_cm'])
                                for r in data['settlements']['rows']],
                   buildings=[dict(id=r['id'], position=r['position_cm'], size=r['size_cm'],
                                   enterable=r['enterable'], settlement=r['settlement_id'],
                                   poi=r.get('poi_id', ''), doors=r['door_ids'],
                                   floors=r['floor_count'], roof=r['roof_access']) for r in buildings],
                   services=[dict(id=s['id'], poi=s['poi_id'], building=hosts[s['poi_id']]['id'],
                                  settlement=hosts[s['poi_id']]['settlement_id'],
                                  position=hosts[s['poi_id']]['position_cm'], functions=s['functions'],
                                  execution_stage='B92') for s in services],
                   aliases=aliases, civic_heart_override=7000,
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
