"""Independently replay B92 native authority receipts and scheduled timelines.

This checks the economy foundation, not service RPCs, native population or
full-stage acceptance. Test funding is an explicit transfer of existing cash.
"""
import hashlib
import json
import sys
from collections import Counter
from pathlib import Path
from CheckEconomyEvidence import verify


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def check(root):
    root = Path(root)
    result = read(root / 'Economy.json')
    assert result['stage'] == 'B92'
    assert result['scope'] == 'isolated_native_economy_authority'
    assert result['completed'] and not result['full_stage_acceptance']
    assert result['failures'] == 0 and len(result['checks']) >= 120
    assert all(row['passed'] for row in result['checks'])
    expected = 'fb876c2f3cd03ff3c3b826c8f949392400b2fe0c81860bb71d7df73ee8b7830a'
    assert result['supplied_economy_sha256'] == expected
    totals = {}
    for name, burn, live in (('Schedule', 145, 420), ('Poor', 5, 424), ('Late', 5, 425)):
        path = root / f'{name}Ledger.json'
        verify(path)
        data = read(path)
        assert len(data['accounts']) == 149
        assert (int(data['start']), int(data['minted']), int(data['burned']), int(data['end'])) == (3288, 0, burn, 3288 - burn)
        assert sum(not i['consumed'] for i in data['items']) == live
        markets = {'capital', 'highfort', 'scholars', 'riverport', 'ironworks'}
        assert {a['id'] for a in data['accounts'] if not a['id'].startswith(('Wallet:', 'Bank:'))} == markets
        assert all(int(a['endowment']) == 600 for a in data['accounts'] if a['id'] in markets)
        assert sum(int(a['endowment']) == 3 for a in data['accounts']) == 96
        budgets = {b['id']: b for b in data['budgets']}
        for prefix, units in (('Courier:', 20), ('CourierIssuance:', 40), ('Core:', 3), ('Supply:', 1)):
            rows = [b for key, b in budgets.items() if key.startswith(prefix)]
            assert len(rows) == 24 and all(b['initial'] == b['remaining'] == units for b in rows)
        assert budgets['CoreOrders']['initial'] == budgets['CoreOrders']['remaining'] == 72
        assert budgets['SupplyOrders']['initial'] == budgets['SupplyOrders']['remaining'] == 24
        timeline = read(root / f'{name}Timeline.json')['events']
        receipts = {r['id']: r for r in data['receipts']}
        assert len({(e['at'], e['row']) for e in timeline}) == len(timeline)
        produced = consumed = 0
        for event in timeline:
            assert 180 <= event['at'] <= 5220 and event['at'] % 180 == 0 and 0 <= event['row'] < 5
            if event['status'] != 'Committed':
                assert event['status'] in {'Missed', 'InsufficientCash', 'StockFull', 'Empty'}
                assert event['receipt'] not in receipts
                continue
            r = receipts[event['receipt']]
            assert len(r['items']) == 1 and r['items'][0]['id'] == event['item']
            if event['row'] < 2:
                produced += 1
                assert r['cause'] == 'B92InstitutionProduction'
                assert int(r['burn']) == (2, 3)[event['row']]
                assert r['items'][0]['op'] == 0
            else:
                consumed += 1
                assert r['cause'] == 'B92InstitutionConsumption' and int(r['burn']) == 0
                assert r['items'][0]['op'] == 2
        if name == 'Schedule':
            assert len(timeline) == 145 and produced == 58 and consumed == 64
            assert Counter(e['status'] for e in timeline) == {'Committed': 122, 'Empty': 23}
            transfers = [r for r in data['receipts'] if r['cause'] == 'DiagnosticFixtureTransfer']
            assert len(transfers) == 5 and all(int(r['mint']) == int(r['burn']) == 0 for r in transfers)
            buys = [r for r in data['receipts'] if r['cause'] == 'B92MarketBuy']
            sells = [r for r in data['receipts'] if r['cause'] == 'B92MarketSell']
            assert len(buys) == 6 and len(sells) == 5
            assert {r['items'][0]['from'] for r in buys} == markets
            assert {r['items'][0]['to'] for r in sells} == markets
        elif name == 'Poor':
            assert len(timeline) == 10 and produced == 2 and consumed == 4
            assert [e['status'] for e in timeline[:5]] == ['InsufficientCash', 'StockFull', 'Committed', 'Committed', 'Empty']
        else:
            assert len(timeline) == 15 and produced == 2 and consumed == 3
            assert sum(e['status'] == 'Missed' for e in timeline) == 10
        totals[name] = {'receipts': len(data['receipts']), 'items': len(data['items']), 'live': live, 'burn': burn}
    print(f'PASS B92 native economy foundation: {len(result["checks"])} checks; {totals}')
    return totals


if __name__ == '__main__':
    check(Path(sys.argv[1]))
