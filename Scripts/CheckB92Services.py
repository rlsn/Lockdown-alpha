"""Replay the connected B92 economy; distinguish source fixtures from income."""
import json
import sys
from pathlib import Path
from CheckEconomyEvidence import verify


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def check(root):
    root = Path(root)
    data = read(root / 'Services.json')
    assert data['complete'] and not data['full_stage_acceptance'] and not data['failures']
    assert data['scope'] == 'remote_takeover_and_five_physical_markets'
    assert len(data['checks']) >= 38 and all(c['passed'] for c in data['checks'])
    verify(root / 'Ledger.json')
    ledger = read(root / 'Ledger.json')
    assert len(ledger['accounts']) == 149 and int(ledger['start']) == 3288
    assert int(ledger['minted']) == 0 and int(ledger['end']) == 3288 - int(ledger['burned'])
    assert int(ledger['burned']) % 5 == 0
    events = ledger['receipts']
    initial = [r for r in events if r['cause'] == 'B92WorldEndowment']
    assert len(initial) == 143 and sum(len(r['items']) for r in initial) == 143
    assert sum(r['cause'] == 'B92SaplingHarvest' for r in events) == 1
    market = [r for r in events if r['cause'] in ('B92MarketBuy', 'B92MarketSell')]
    assert len(market) == 21
    pools = {'capital', 'highfort', 'scholars', 'riverport', 'ironworks'}
    assert {r['items'][0]['from'] for r in market if r['cause'] == 'B92MarketBuy'} == pools
    assert {r['items'][0]['to'] for r in market if r['cause'] == 'B92MarketSell'} == pools
    assert all(int(r['mint']) == int(r['burn']) == 0 for r in market)
    producers = sum(r['cause'] == 'B92InstitutionProduction' for r in events)
    consumes = sum(r['cause'] == 'B92InstitutionConsumption' for r in events)
    assert len(ledger['items']) == 570 + producers
    assert sum(i['consumed'] for i in ledger['items']) == 1 + consumes
    assert sum(not i['consumed'] for i in ledger['items']) == 569 + producers - consumes
    assert all(r['cause'] not in ('DiagnosticFixtureTransfer', 'DiagnosticReward') for r in events)
    for role, phases in {'A': (1, 2, 10, 11, 3, 4, 5, 6, 8), 'B': (1, 2, 5, 6, 7), 'A2': (9, 2, 3, 4, 12, 13, 5, 14)}.items():
        for phase in phases:
            report = read(root / f'{role}_{phase}.json')
            assert report['completed'] and report['life'] == 1
    assert not read(root / 'B_7.json')['has_quote'] and read(root / 'B_7.json')['offers'] == 0
    assert read(root / 'A2_9.json')['combat_floor'] > 100002
    assert not read(root / 'A2_3.json')['accepted']  # final duplicate Buy with a full real bag
    print(f'PASS B92 connected services: {len(data["checks"])} checks, 21 market receipts, 143 imported sources, one harvest/consume; {len(events)} conserved receipts')


if __name__ == '__main__':
    check(Path(sys.argv[1]))
