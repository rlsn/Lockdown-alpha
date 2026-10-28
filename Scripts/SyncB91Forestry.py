"""Compile the user-authorized finite forestry/control overlay; preserve supplied data."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def compile_forestry():
    map_bytes = (ROOT/'Content/Lockdown/Data/V09/map_b90.json').read_bytes()
    settlements = json.loads(map_bytes)['settlements']
    assert len(settlements) == len({s['id'] for s in settlements}) == 11
    definition = dict(id='forestry_knife', name='Forestry Knife', native_kind=97,
                      world_kind=14, binding=7, weight=2, quality='standard_only',
                      price=None, consumable=False, death='drop',
                      active=dict(op='forestry_tool', verbs=['harvest', 'chop'],
                                  blocked_by='Mute', range_cm=120, channel_ms=4000,
                                  quiet_ms=6000, cooldown_ms=0))
    rows = [dict(id=f'prop_forestry_knife_{i:03d}', settlement=s['id'], kind=14,
                 position=[s['position'][0]+1400, s['position'][1]+1400, s['position'][2]])
            for i, s in enumerate(sorted(settlements, key=lambda s: s['id']))]
    return (json.dumps(dict(schema=9, revision=1, stage='B91', definition=definition,
                            rows=rows, finite_count=11,
                            map_sha256=hashlib.sha256(map_bytes).hexdigest(),
                            authority='User control/finite forestry decision, 28 October 2026'),
                       sort_keys=True, separators=(',', ':'))+'\n').encode()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    payload = compile_forestry()
    sha = hashlib.sha256(payload).hexdigest()
    import re
    link = re.search(r'LD_B91_LINK_WORLD_SHA TEXT\("([0-9a-f]+)"\)',
                     (ROOT/'Source/Lockdown/Private/World/LDB91TraversalContract.h').read_text()).group(1)
    world = hashlib.sha256((link+sha+':source-click-channel-v1').encode()).hexdigest()
    outputs = {'Content/Lockdown/Data/V09/forestry_b91.json': payload,
               'Source/Lockdown/Private/World/LDB91ForestryContract.h':
                   (f'#pragma once\n#define LD_B91_FORESTRY_SHA TEXT("{sha}")\n'
                    f'#define LD_B91_CONTROL_WORLD_SHA TEXT("{world}")\n').encode()}
    for name, data in outputs.items():
        path = ROOT/name
        if args.check:
            assert path.read_bytes().replace(b'\r\n', b'\n') == data, name
        else:
            path.write_bytes(data)
    print(f'PASS forestry overlay: 11 finite Standard knives; {sha}')


if __name__ == '__main__':
    main()
