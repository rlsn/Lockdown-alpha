#!/usr/bin/env python3
"""Compile the finite authored grass extension without changing supplied inputs."""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def compile_grass():
    names = ('props', 'object_types', 'world_objects_rules')
    raw = {n: (ROOT/'SourceData/World'/(n+'.json')).read_bytes() for n in names}
    data = {n: json.loads(v) for n, v in raw.items()}
    assert all(v['schema_version'] == 9 for v in data.values())
    rows = [r for r in data['props']['rows'] if r['type_id'] == 'tall_grass']
    kind = next(r for r in data['object_types']['rows'] if r['id'] == 'tall_grass')
    assert len(rows) == len({r['id'] for r in rows}) == kind['count'] == 96
    assert kind['size_cm'] == [500, 500, 150] and kind['concealment_shape'] == 'disk_radius_250cm_feet_point'
    assert not kind['portable'] and not kind['blocks_walk'] and not kind['blocks_sight']
    assert kind['burnable'] and kind['verbs'] == ['enter', 'ignite']
    assert kind['interaction_radius_cm'] == 120
    rules = data['world_objects_rules']['grass']
    assert rules == dict(hide_requires_inside_and_no_detected_authorized_observer=True,
                         reveal_on_attack_or_cast=True, burning_disables_concealment_ms=3000,
                         burnt_for_match=True, concealment_rearm_ms=1000,
                         reveal_after_offense_ms=2000, proximity_reveal_cm=200)
    assert all(r['stage_id'] == 'B91' and r['initial_owner'] == 'world' and not r['respawn']
               and len(r['position_cm']) == 3 and not r['blocks_critical_corridor'] for r in rows)
    keys = {'id', 'type_id', 'position_cm', 'initial_owner', 'respawn', 'stage_id',
            'anchor_poi_id', 'placement_pad_cm', 'blocks_critical_corridor'}
    assert all(set(r) == keys and all(type(v) is int for v in r['position_cm'])
               and abs(r['position_cm'][0]) <= 118000 and abs(r['position_cm'][1]) <= 118000
               and r['placement_pad_cm'] == 250 for r in rows)
    payload = dict(schema=9, stage='B91', revision=1, kind=15, radius_cm=250,
                   size_cm=kind['size_cm'], rules=rules,
                   rows=[dict(id=r['id'], position=r['position_cm']) for r in rows],
                   source_sha256={n: hashlib.sha256(v).hexdigest() for n, v in raw.items()})
    return (json.dumps(payload, sort_keys=True, separators=(',', ':'))+'\n').encode()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    payload = compile_grass()
    sha = hashlib.sha256(payload).hexdigest()
    parent = re.search(r'LD_B91_CONTROL_WORLD_SHA TEXT\("([0-9a-f]+)"\)',
                       (ROOT/'Source/Lockdown/Private/World/LDB91ForestryContract.h').read_text()).group(1)
    world = hashlib.sha256((parent+sha+':authored-grass-v1').encode()).hexdigest()
    outputs = {'Content/Lockdown/Data/V09/grass_b91.json': payload,
               'Source/Lockdown/Private/World/LDB91GrassContract.h':
                   (f'#pragma once\n#define LD_B91_GRASS_SHA TEXT("{sha}")\n'
                    f'#define LD_B91_GRASS_WORLD_SHA TEXT("{world}")\n').encode()}
    for name, value in outputs.items():
        path = ROOT/name
        if args.check:
            assert path.read_bytes().replace(b'\r\n', b'\n') == value, name
        else:
            path.write_bytes(value)
    print('PASS grass compiler: 96 finite patches; '+sha)


if __name__ == '__main__':
    main()
