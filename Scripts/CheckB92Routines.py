"""Replay real-time schedules and separate cosmetic logistics from paid institutions."""
import json
import re
import sys
from pathlib import Path
from CheckEconomyEvidence import verify
ROOT = Path(__file__).resolve().parents[1]


def read(p):
    return json.loads(Path(p).read_text(encoding='utf-8-sig'))


def point(s):
    return tuple(float(v) for v in re.findall(r'[XYZ]=(-?[0-9.]+)', s))


def check(root, full=False):
    root = Path(root)
    report, final = read(root / 'Routines.json'), read(root / 'Final.json')
    assert report['scope'] == 'native_civilian_traveler_routines' and report['complete']
    assert not report['full_stage_acceptance'] and not report['failures']
    assert len(report['checks']) >= (23 if report['requested_s'] >= 900 else 21) and all(c['passed'] for c in report['checks'])
    if full:
        assert report['requested_s'] >= 900
    assert report['duration_s'] >= report['requested_s'] and report['wall_duration_s'] >= report['requested_s'] - 1
    schedules = {r['id']: r for r in read(ROOT / 'SourceData/World/npc_schedules.json')['rows'] if r['role'] != 'guard'}
    rows = {r['source']: r for r in final['routines']}
    assert rows.keys() == schedules.keys() and len(rows) == 128
    assert sum(r['traveler'] for r in rows.values()) == 32
    assert len({r['id'] for r in rows.values()}) == 128
    assert sum(r['dead'] for r in rows.values()) == 1
    assert all(r['max_speed_cm_s'] <= 500 for r in rows.values())
    assert sum(r['distance_cm'] > 1000 for r in rows.values()) >= (127 if report['requested_s'] >= 900 else 32)
    assert sum(r['flees'] for r in rows.values()) > 0 and sum(r['resumes'] for r in rows.values()) > 0
    assert final['path_queries'] > 0 and final['perception_rays'] > 0
    if report['requested_s'] >= 900:
        assert sum(r['traveler'] and r['trips'] > 0 for r in rows.values()) >= 16
        assert sum(not r['traveler'] and r['transitions'] > 0 for r in rows.values()) >= 80
        assert len(report['samples']) >= 80
        times = [s['elapsed_s'] for s in report['samples']]
        assert times[0] < 1 and times[-1] >= report['duration_s'] - 15
        assert all(9.99 <= b-a <= 15 for a, b in zip(times, times[1:]))
    last = {}
    for sample in report['samples']:
        assert len(sample['bodies']) == 128
        for r in sample['bodies']:
            assert r['id'] == rows[r['source']]['id']
            assert r['distance_cm'] >= last.get(r['source'], 0)
            assert len(point(r['position'])) == 3
            last[r['source']] = r['distance_cm']
    verify(root / 'Ledger.json')
    ledger = read(root / 'Ledger.json')
    assert int(ledger['minted']) == 0 and len(ledger['accounts']) == 149
    allowed = {'B92WorldEndowment', 'B92MarketStockEndowment', 'B92InstitutionProduction', 'B92InstitutionConsumption'}
    assert all(r['cause'] in allowed for r in ledger['receipts'])
    assert all(not r['mint'] or int(r['mint']) == 0 for r in ledger['receipts'])
    for a in ledger['accounts']:
        if a['id'].startswith(('Contender:', 'Bank:')):
            assert int(a['gold']) == 0
    for task in (70, 71):
        assert read(root / f'A_{task}.json')['accepted']
    print(f'PASS B92 routines: {len(report["checks"])} checks; {report["duration_s"]:.1f}s real time;128 sources; moving visual-only logistics; zero mint')


if __name__ == '__main__':
    check(sys.argv[1], '--full' in sys.argv[2:])
