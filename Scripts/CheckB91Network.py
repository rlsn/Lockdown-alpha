import csv
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def check(directory):
    read = lambda name: json.loads((directory/name).read_text(encoding='utf-8-sig'))
    report = read('Network.json')
    assert report['complete'] and not report['failures'] and len(report['checks']) >= 16
    assert report['contract'] == hashlib.sha256((ROOT/'Content/Lockdown/Data/V09/environment_b91.json').read_bytes()).hexdigest()
    assert all(c['passed'] for c in report['checks'])
    fields = {'Handle', 'PublicDoor', 'Position', 'Revision', 'State', 'Presentation', 'Facing'}
    for role, phases in [('A', (1, 3, 4, 5, 8)), ('B', (2, 6, 7, 8))]:
        for phase in phases:
            data = read(f'{role}_{phase}.json')
            assert data['completed'] and set(data['wire_fields']) == fields
            assert len(data['received_entries']) <= 128
            if phase not in (6, 7, 8):
                assert data['receipt_result'] == (2 if phase == 4 else 1)
    a = read('A_1.json')
    assert a['duplicate_receipt_same']
    handle = lambda data: next(r['handle'] for r in data['received_entries'] if r['public_door'] == 'building_riverport_00_door_north')
    assert handle(a) != handle(read('B_2.json'))
    assert not read('B_6.json')['door_observed']
    assert read('B_7.json')['door_observed'] and read('B_7.json')['door_state'] == 0
    assert sum(read(f'{role}_8.json')['receipt_result'] == 1 for role in ('A', 'B')) == 1
    trace = [r for r in csv.DictReader((directory/'B_Motion.csv').read_text().splitlines()) if r['phase'] == '2']
    source = json.loads((ROOT/'Content/Lockdown/Data/V09/environment_b91.json').read_bytes())
    door_y = next(d['position'][1] for d in source['doors'] if d['id'] == 'building_riverport_00_door_north')
    assert min(float(r['y_cm']) for r in trace) < door_y-40
    assert max(float(r['y_cm']) for r in trace) > door_y+200
    print('PASS B91 network: two independent owners; native doorway travel, revisions, occupied rejection, hidden/reobserved snapshots and one-winner race')


if __name__ == '__main__':
    check(Path(sys.argv[1]))
