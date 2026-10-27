#!/usr/bin/env python3
"""Close approved optional links over the immutable anchor/ladder sources."""
import argparse
import hashlib
import json
from pathlib import Path
from SyncB91Objects import compile_objects
from SyncB91Tools import compile_tools

ROOT = Path(__file__).resolve().parents[1]


def compile_traversal():
    raw = {n: (ROOT/'docs/v09/data'/(n+'.json')).read_bytes()
           for n in ('traversal_anchors', 'props', 'object_types', 'buildings', 'world_objects_rules')}
    data = {n: json.loads(v) for n, v in raw.items()}
    assert all(v['schema_version'] == 9 for v in data.values())
    rules = data['world_objects_rules']
    assert rules['transaction_radius_cm'] == 120 and rules['quiet_s'] == 6
    assert rules['heavy_carry_ms'] == 1000 and rules['placement_ms'] == 1500
    assert rules['rope'] == dict(equipment_slot='accessory', consume_on_attach=False,
                                 action_requires_binding=True, detach_quiet_s=6,
                                 max_rise_cm=500, use_placed_link='world_channel_not_free_portable_cast')
    types = {r['id']: r for r in data['object_types']['rows']}
    assert types['ladder']['weight'] == 10 and types['ladder']['size_cm'] == [70, 500, 30]
    assert not types['ladder']['blocks_walk'] and not types['ladder']['blocks_sight']
    ladders = [r for r in data['props']['rows'] if r['type_id'] == 'ladder']
    assert len(ladders) == 12 and all(not r['respawn'] and r['initial_owner'] == 'world' for r in ladders)
    buildings = {r['id']: r for r in data['buildings']['rows']}
    anchors = []
    for row in data['traversal_anchors']['rows']:
        b = buildings[row['building_id']]
        assert b['enterable'] and b['roof_access'] and b['floor_count'] == 2
        assert row['kind'] == 'portable_ladder_or_rope' and row['permanent_alternative'] == 'interior_stairs'
        assert row['climb_ms'] == 2000 and row['capacity'] == 1 and row['one_user_at_a_time']
        assert row['link_bidirectional'] and row['max_rise_cm'] == 500
        x, y, z = b['position_cm']
        assert row['lower_cm'] == [x+950, y, z] and row['upper_cm'] == [x+600, y, z+400]
        # Use the existing north upper landing, leaving the permanent stairs and
        # centre hall intact. Both endpoints stay within the 10m fitting budget.
        anchors.append(dict(id=row['id'], building=row['building_id'],
                            lower=[x+950, y+740, z], upper=[x+560, y+740, z+400],
                            source_lower=row['lower_cm'], source_upper=row['upper_cm']))
    assert len(anchors) == 16 and len({a['id'] for a in anchors}) == 16
    payload = dict(schema=9, stage='B91', revision=1, climb_ms=2000, reservation_ms=3000,
                   capacity=1, max_rise_cm=500, anchors=anchors,
                   ladders=[dict(id=r['id'], position=r['position_cm']) for r in ladders],
                   source_sha256={n: hashlib.sha256(v).hexdigest() for n, v in raw.items()})
    return (json.dumps(payload, sort_keys=True, separators=(',', ':'))+'\n').encode()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    payload = compile_traversal()
    digest = hashlib.sha256(payload).hexdigest()
    parts = [hashlib.sha256(v).hexdigest() for v in (compile_objects(), compile_tools(), payload)]
    world = hashlib.sha256(''.join(parts).encode()).hexdigest()
    outputs = {'Content/Lockdown/Data/V09/traversal_b91.json': payload,
               'Source/Lockdown/Private/World/LDB91TraversalContract.h':
                   ('#pragma once\n#define LD_B91_TRAVERSAL_SHA TEXT("'+digest+'")\n'
                    '#define LD_B91_LINK_WORLD_SHA TEXT("'+world+'")\n').encode()}
    for name, value in outputs.items():
        path = ROOT/name
        if args.check:
            assert path.read_bytes().replace(b'\r\n', b'\n') == value, name
        else:
            path.write_bytes(value)
    print('PASS B91 traversal compiler: 12 ladders / 16 approved paired sockets; '+digest)


if __name__ == '__main__':
    main()
