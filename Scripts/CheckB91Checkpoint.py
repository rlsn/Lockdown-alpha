#!/usr/bin/env python3
"""Independently inspect the native cross-process checkpoint evidence."""
import hashlib
import json
import sys
from pathlib import Path


def read(path):
    raw = path.read_bytes()
    return raw.decode('utf-16' if raw[:2] in (b'\xff\xfe', b'\xfe\xff') else 'utf-8-sig')


def check(root):
    counts = []
    for mode, minimum in (('Write', 16), ('Read', 10), ('Reject', 13), ('Fresh', 4)):
        data = json.loads(read(root / (mode + '.json')))
        assert data['mode'] == mode and data['completed'] and data['failures'] == 0
        assert len(data['checks']) >= minimum and all(c['passed'] for c in data['checks'])
        log = read(root / (mode + '.log'))
        assert not any(s in log for s in ('Fatal error:', 'Unhandled Exception:', 'appError called:', '=== Critical error: ==='))
        counts.append(len(data['checks']))
    envelope = json.loads(read(root / 'World.json'))
    assert envelope['schema'] == 1 and envelope['scope'] == 'settled_environment'
    assert hashlib.sha256(envelope['payload'].encode()).hexdigest() == envelope['sha256']
    saved = json.loads(envelope['payload'])
    restored = json.loads(json.loads(read(root / 'Restored.json'))['payload'])
    assert len(saved['objects']) == 436 and len(saved['items']) == 143 and len(saved['consumed']) == 1
    assert saved['objects'] == restored['objects'] and saved['doors'] == restored['doors']
    assert saved['consumed'] == restored['consumed']
    live = {i['id']: i for i in saved['items']}
    assert len(live) == 143 and not set(live).intersection(i['id'] for i in saved['consumed'])
    assert len({i['receipt'] for i in saved['items'] + saved['consumed']}) == 144
    for item in restored['items']:
        old = live[item['id']]
        for key in old:
            if key not in ('fuel', 'ready_ms', 'proc_ms'):
                assert item[key] == old[key], (key, item['id'])
        assert all(0 <= old[key] - item[key] <= 4000 for key in ('fuel', 'ready_ms', 'proc_ms'))
    assert sum(o['kind'] == 13 and bool(o['harvest']) and bool(o['yield']) for o in saved['objects']) == 1
    assert sum(o['kind'] == 15 and bool(o['burn']) and o['broken'] for o in saved['objects']) == 1
    assert sum(i['lit'] and i['location'] and i['fuel'] < 180000 for i in saved['items']) == 1
    print(f'PASS B91 settled checkpoint: {sum(counts)} native checks across four processes; exact finite identities and provenance')
    return counts


if __name__ == '__main__':
    check(Path(sys.argv[1]))
