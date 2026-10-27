#!/usr/bin/env python3
import json
import sys
from pathlib import Path


def check(root):
    report = json.loads((root/'Tools.json').read_text(encoding='utf-8-sig'))
    assert report['complete'] and report['failures'] == 0
    assert report['phase'] == 8 and len(report['checks']) == 32 and all(r['passed'] for r in report['checks'])
    assert report['initial_items'] == report['final_items'] == 132 and report['conserved']
    trace = report['fuel_trace']
    assert len(trace) >= 8 and len({r['item'] for r in trace}) == 1
    assert trace[0]['remaining_ms'] == 180000 and trace[-1]['remaining_ms'] == 0
    assert all(a['remaining_ms'] >= b['remaining_ms'] for a, b in zip(trace, trace[1:]))
    assert any(r['tag'] == 'life_return' for r in trace)
    assert report['base_equipment_count'] == 80 and report['bindings'] == 11 and report['actions'] == 6
    world = {'Handle', 'Kind', 'Position', 'Revision', 'State', 'PhysicalRevision'}
    body = {'Handle', 'PublicDoor', 'Position', 'Revision', 'State', 'Presentation', 'Facing', 'Light'}
    for role in ('A', 'B'):
        for phase in range(1, 9):
            data = json.loads((root/f'{role}_{phase}.json').read_text(encoding='utf-8-sig'))
            assert data['completed'], (role, phase, data.get('reason'))
            assert set(data['object_fields']) in (world, world | {'Endpoint'}) and set(data['body_fields']) == body
            assert not data['world_payload_contains_fuel']
            if role == 'B' and phase == 4:
                assert data['peer_body_count'] == 0
    print(f"PASS B91 tools: {len(report['checks'])} native checks; retained fuel through binding/drop/life; two owners")


if __name__ == '__main__':
    check(Path(sys.argv[1]))
