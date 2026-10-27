#!/usr/bin/env python3
"""Compile the finite v0.9 portable-tool overlay without changing the v0.8 catalog."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
from SyncB91Objects import compile_objects

ROOT = Path(__file__).resolve().parents[1]


def compile_tools():
    raw = {n: (ROOT/'docs/v09/data'/(n+'.json')).read_bytes()
           for n in ('map_items', 'object_types', 'props', 'world_objects_rules')}
    data = {n: json.loads(v) for n, v in raw.items()}
    assert all(v['schema_version'] == 9 for v in data.values())
    rules = data['world_objects_rules']
    assert rules['torch']['fuel_ms'] == 180000 and rules['lantern']['fuel_ms'] == 300000
    assert not rules['torch']['refill'] and not rules['lantern']['refill']
    assert rules['torch']['light_cosmetic_radius_cm'] == 600 and rules['lantern']['light_cosmetic_radius_cm'] == 800
    assert rules['torch']['sight_bonus_cm'] == rules['lantern']['sight_bonus_cm'] == 0
    assert rules['torch']['active_blocked_by'] == rules['lantern']['active_blocked_by'] == 'Mute'
    kinds = dict(torch=(94, 7, 2, 180000), lantern=(95, 8, 3, 300000), rope=(96, 9, 3, 0))
    definitions = []
    for row in data['map_items']['rows']:
        assert row['id'] in kinds and row['stage_id'] == 'B91'
        native, world, weight, fuel = kinds[row['id']]
        assert row['weight'] == weight and row['equipment_slot'] == 'accessory'
        assert not row['consumable'] and row['tradable'] and row['quality'] == 'standard_only'
        assert row['stats'] == row['passives'] == {} and row['official_price_gold'] is None
        assert row['source'] == 'finite_world_endowment' and row['death_policy'] == 'drop'
        assert set(row['persistent_instance_fields']) == {'fuel_remaining_ms', 'lit', 'location', 'cooldown_ready_at', 'revision'}
        assert row['active']['op'] == 'portable_world_tool' and row['active']['blocked_by'] == 'Mute'
        assert row['active']['mana_cost'] == 0 and row['active']['range_cm'] == 120
        definitions.append(dict(id=row['id'], name=row['name'], native_kind=native,
                                world_kind=world, weight=weight, fuel_ms=fuel, binding=7,
                                active=row['active']))
    assert len(definitions) == 3 and len({d['id'] for d in definitions}) == 3
    rows = [r for r in data['props']['rows'] if r['type_id'] in kinds]
    assert collections.Counter(r['type_id'] for r in rows) == dict(torch=24, lantern=12, rope=12)
    for row in rows:
        assert row['initial_owner'] == 'world' and not row['respawn'] and row['stage_id'] == 'B91'
    payload = dict(schema=9, stage='B91', revision=1, definitions=definitions,
                   rows=[dict(id=r['id'], kind=kinds[r['type_id']][1], position=r['position_cm']) for r in rows],
                   source_sha256={n: hashlib.sha256(v).hexdigest() for n, v in raw.items()},
                   scope='Finite portable instances and retained cosmetic light fuel; socket fire and traversal are separate operations')
    return (json.dumps(payload, sort_keys=True, separators=(',', ':'))+'\n').encode()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    payload = compile_tools()
    digest = hashlib.sha256(payload).hexdigest()
    base = hashlib.sha256(compile_objects()).hexdigest()
    world = hashlib.sha256((base+digest).encode()).hexdigest()
    outputs = {'Content/Lockdown/Data/V09/tools_b91.json': payload,
               'Source/Lockdown/Private/World/LDB91ToolsContract.h':
                   ('#pragma once\n#define LD_B91_TOOLS_SHA TEXT("'+digest+'")\n'
                    '#define LD_B91_WORLD_SHA TEXT("'+world+'")\n').encode()}
    for name, value in outputs.items():
        path = ROOT/name
        if args.check:
            assert path.read_bytes().replace(b'\r\n', b'\n') == value, name
        else:
            path.write_bytes(value)
    print('PASS B91 portable compiler: 24 torches / 12 lanterns / 12 ropes; '+digest)


if __name__ == '__main__':
    main()
