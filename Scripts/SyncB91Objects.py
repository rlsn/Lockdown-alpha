#!/usr/bin/env python3
"""Close the storage/cargo subset over immutable v0.9 sources."""
import argparse
import collections
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def compile_objects():
    names = ('props', 'object_types', 'world_objects_rules')
    raw = {n: (ROOT/'docs/v09/data'/(n+'.json')).read_bytes() for n in names}
    data = {n: json.loads(v) for n, v in raw.items()}
    assert all(v['schema_version'] == 9 for v in data.values())
    kinds = dict(chest=1, crate=2, barrel=3, water_flask=4, oil_flask=5)
    rows = [r for r in data['props']['rows'] if r['type_id'] in kinds]
    counts = collections.Counter(r['type_id'] for r in rows)
    assert counts == dict(chest=48, crate=36, barrel=24, water_flask=24, oil_flask=24)
    assert len({r['id'] for r in rows}) == 156
    contents = collections.Counter()
    for r in rows:
        assert r['stage_id'] == 'B91' and r['initial_owner'] == 'world' and not r['respawn']
        assert len(r['position_cm']) == 3 and not r['blocks_critical_corridor']
        for item in r.get('initial_contents', []):
            assert r['type_id'] == 'chest' and item['definition_id'] in ('Fiber', 'Ore')
            contents[item['definition_id']] += item['count']
    assert contents == dict(Fiber=24, Ore=12)
    assert sum(bool(r.get('initial_contents')) for r in rows) == 12
    payload = dict(schema=9, stage='B91', revision=1, profile='v09_environment',
                   scope='Finite storage, inherited flasks and crate/barrel cargo; remaining B91 systems pending',
                   container_capacity=24, container_weight=60, fitter_search_cm=1000,
                   rows=[dict(id=r['id'], kind=kinds[r['type_id']], position=r['position_cm'],
                              contents=r.get('initial_contents', [])) for r in rows],
                   source_sha256={n: hashlib.sha256(v).hexdigest() for n, v in raw.items()})
    return (json.dumps(payload, sort_keys=True, separators=(',', ':'))+'\n').encode()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    payload = compile_objects()
    digest = hashlib.sha256(payload).hexdigest()
    outputs = {'Content/Lockdown/Data/V09/objects_b91.json': payload,
               'Source/Lockdown/Private/World/LDB91ObjectsContract.h':
                   ('#pragma once\n#define LD_B91_OBJECTS_SHA TEXT("'+digest+'")\n').encode()}
    for name, value in outputs.items():
        path = ROOT/name
        if args.check:
            assert path.read_bytes().replace(b'\r\n', b'\n') == value, name
        else:
            path.write_bytes(value)
    print('PASS B91 objects compiler: 156 objects / 84 finite physical items; '+digest)


if __name__ == '__main__':
    main()
