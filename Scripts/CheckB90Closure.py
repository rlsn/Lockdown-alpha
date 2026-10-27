#!/usr/bin/env python3
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def check(directory):
    data = json.loads((directory/'Closure.json').read_text(encoding='utf-8-sig'))
    assert data['complete'] and data['failures'] == 0 and len(data['worlds']) == 4
    payload = (ROOT/'Content/Lockdown/Data/V09/map_b90.json').read_bytes()
    generations = []
    for i, w in enumerate(data['worlds'][:3]):
        assert w['profile'] == 'v09_full' and w['round'] == i and w['passed']
        assert w['payload_sha256'] == hashlib.sha256(payload).hexdigest()
        assert w['site_count'] == 96 and w['service_seats'] == 36 and len(w['sites']) == 96
        assert w['map_anchors'] == 1 and w['spawn_actors'] == 24
        assert w['economy_directors'] == w['physical_items'] == w['legacy_world_directors'] == w['old_world_actors_alive'] == 0
        assert w['repeat_registry_identical'] and w['world_semantics_identical'] and w['repeat_import_identical']
        assert w['stale_generation_rejected'] and w['fresh_surface_cells'] == 0
        assert w['dirty_wet_frozen_fixture'] and w['dirty_surface_cells'] > 0
        assert all(p['floor_valid'] for p in w['sites'])
        assert not any(s['transactions_enabled'] for s in w['services'])
        generations.append(w['generation'])
    w = data['worlds'][3]
    assert w['profile'] == 'v08_integration' and w['passed'] and w['old_world_actors_alive'] == 0
    assert w['v09_registry_disabled'] and w['v09_contact_disabled'] and w['v09_map_anchor_absent']
    assert w['legacy_markets'] == 3 and w['legacy_receipts'] == 76
    generations.append(w['generation'])
    assert all(a < b for a, b in zip(generations, generations[1:]))
    legacy = json.loads((directory/'legacy/Report.json').read_text(encoding='utf-8-sig'))
    assert legacy['failures'] == 0 and legacy['schema'] == 8 and len(legacy['checks']) > 20
    print(f'B90 reset/profile isolation: 3 fresh full-map worlds -> v08; generations {generations}; native legacy source accounting passed')


if __name__ == '__main__':
    check(Path(sys.argv[1]))
