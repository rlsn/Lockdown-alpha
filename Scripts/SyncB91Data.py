#!/usr/bin/env python3
"""Compile the selected B91 structural/door slice; keep supplied inputs immutable."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT/'SourceData/World'


def compile_environment():
    inputs = {n: (SOURCE/(n+'.json')).read_bytes() for n in
              ('buildings', 'props', 'traversal_anchors', 'world_objects_rules')}
    data = {n: json.loads(v) for n, v in inputs.items()}
    assert all(v['schema_version'] == 9 for v in data.values())
    buildings = data['buildings']['rows']
    selected = [b for b in buildings if b['enterable']]
    doors = [p for p in data['props']['rows'] if p['type_id'] == 'door']
    assert len(buildings) == 144 and len(selected) == 36 and len(doors) == 72
    assert len({d['id'] for d in doors}) == 72
    anchors = data['traversal_anchors']['rows']
    assert len(anchors) == 16 and len({a['building_id'] for a in anchors}) == 16
    rules = data['world_objects_rules']
    assert rules['transaction_radius_cm'] == 120 and rules['quiet_s'] == 6
    assert rules['door_open_ms'] == 300 and rules['door_force_ms'] == rules['door_lockpick_ms'] == 3000
    by_id = {b['id']: b for b in selected}
    rows = []
    for b in selected:
        assert len(b['door_ids']) == 2 and b['floor_count'] in (1, 2)
        assert len(b['position_cm']) == 3 and all(type(v) is int for v in b['position_cm'])
        for identity in b['door_ids']:
            d = next(d for d in doors if d['id'] == identity)
            assert not set(d).difference('id type_id position_cm initial_owner respawn stage_id building_id hinge_side initial_state critical_path lockable lock_owner'.split())
            assert d['stage_id'] == 'B91'
            assert d['building_id'] == b['id'] and d['initial_owner'] == 'world'
            position = b['position_cm'][:]
            rear = d['hinge_side'] == 'north'
            assert d['hinge_side'] in ('north', 'south') and d['lockable'] == rear
            assert d['initial_state'] == 'closed_unlocked' and not d['respawn']
            assert d['lock_owner'] == ('resident:'+b['settlement_id'] if rear else None)
            assert d['position_cm'] == [position[0], position[1]+(900 if rear else -900), position[2]]
            position[1] += (880 if rear else -880)
            rows.append(dict(id=identity, building=b['id'], resident=d['lock_owner'] or '',
                             rear=rear, position=position, source_position=d['position_cm']))
    for a in anchors:
        assert not set(a).difference('id building_id lower_cm upper_cm kind permanent_alternative climb_ms one_user_at_a_time capacity max_rise_cm link_bidirectional stage_id'.split())
        assert a['building_id'] in by_id and by_id[a['building_id']]['roof_access']
        assert by_id[a['building_id']]['floor_count'] == 2
        assert a['permanent_alternative'] == 'interior_stairs'
        assert a['stage_id'] == 'B91' and a['climb_ms'] == 2000 and a['capacity'] == 1 and a['max_rise_cm'] == 500
    payload = dict(schema=9, stage='B91', profile='v09_environment', revision=1,
                   scope='Structural interiors, permanent stairs, doors and restricted observation; remaining B91 object verbs are not enabled',
                   buildings=[dict(id=b['id'], position=b['position_cm'], roof=b['roof_access']) for b in selected],
                   doors=rows, roof_buildings=[a['building_id'] for a in anchors],
                   source_sha256={n: hashlib.sha256(v).hexdigest() for n, v in inputs.items()})
    return (json.dumps(payload, sort_keys=True, separators=(',', ':'))+'\n').encode()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    payload = compile_environment()
    digest = hashlib.sha256(payload).hexdigest()
    outputs = {'Content/Lockdown/Data/V09/environment_b91.json': payload,
               'Source/Lockdown/Private/World/LDB91Contract.h':
                   ('#pragma once\n#define LD_B91_ENV_SHA TEXT("'+digest+'")\n').encode()}
    for name, value in outputs.items():
        path = ROOT/name
        if args.check:
            assert path.read_bytes().replace(b'\r\n', b'\n') == value, name
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(value)
    print('PASS B91 structural compiler: 36 interiors / 72 doors / 16 permanent stairs; '+digest)


if __name__ == '__main__':
    main()
