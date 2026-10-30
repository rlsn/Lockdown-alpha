"""Replay physical courier lifecycle and distinguish quota fixtures from travel."""
import json
import heapq
import sys
from collections import Counter
from pathlib import Path
from CheckEconomyEvidence import verify
ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def distance(start, target):
    graph = {}
    for r in read(ROOT / 'docs/v09/data/routes.json')['rows']:
        graph.setdefault(r['from'], []).append((r['to'], r['length_cm']))
        graph.setdefault(r['to'], []).append((r['from'], r['length_cm']))
    best = {start: 0}
    queue = [(0, start)]
    while queue:
        cost, node = heapq.heappop(queue)
        if cost != best[node]:
            continue
        if node == target:
            return cost
        for to, length in graph[node]:
            new = cost + length
            if new < best.get(to, float('inf')):
                best[to] = new
                heapq.heappush(queue, (new, to))
    raise AssertionError('unreachable courier route')


def check(root):
    root = Path(root)
    data = read(root / 'Courier.json')
    assert data['scope'] == 'physical_courier_contracts' and data['complete']
    assert not data['full_stage_acceptance'] and not data['failures']
    assert len(data['checks']) >= 20 and all(c['passed'] for c in data['checks'])
    verify(root / 'Ledger.json')
    ledger = read(root / 'Ledger.json')
    receipts = {r['id']: r for r in ledger['receipts']}
    items = {i['id']: i for i in ledger['items']}
    contracts = read(root / 'Contracts.json')['contracts']
    assert len(contracts) == 60 and len({j['token'] for j in contracts}) == 60
    assert len({j['contract'] for j in contracts}) == 60
    assert len({j['terminal'] for j in contracts}) == 60
    assert Counter(r['cause'] for r in receipts.values())['B92CourierAbandon'] == 38
    boards = {r['poi_id'] for r in read(ROOT / 'docs/v09/data/services.json')['rows'] if 'courier_board' in r['functions']}
    terminals = Counter()
    owners = Counter()
    for j in contracts:
        assert not j['active'] and j['origin'] in boards and j['destination'] in boards
        assert j['origin'] != j['destination']
        assert 25000 <= j['distance_cm'] <= 80000
        assert j['distance_cm'] == distance(j['origin'], j['destination'])
        issue, end = receipts[j['contract']], receipts[j['terminal']]
        assert issue['cause'] == 'B92CourierIssuance' and issue['units'] == 1
        assert issue['budget'] == 'CourierIssuance:' + j['owner'].split(':')[1]
        assert len(issue['items']) == 1 and issue['items'][0]['id'] == j['token']
        assert issue['items'][0]['op'] == 0 and issue['items'][0]['kind'] == 98
        assert issue['items'][0]['to'] == j['owner'] and not issue['gold']
        assert int(issue['mint']) == int(issue['burn']) == 0
        assert items[j['token']]['origin'] == j['contract']
        terminals[end['cause']] += 1
        owners[j['owner']] += 1
        if end['cause'] == 'B92CourierDelivery':
            assert int(end['mint']) == 25 and int(end['burn']) == 0
            assert end['budget'] == 'Courier:' + j['owner'].split(':')[1] and end['units'] == 1
            assert len(end['items']) == len(end['gold']) == 1
            assert end['items'][0]['id'] == j['token'] and end['items'][0]['op'] == 2
            assert end['items'][0]['from'] == j['owner'] and end['gold'][0]['account'] == j['owner']
            assert items[j['token']]['consumed']
        else:
            assert end['cause'] == 'B92CourierAbandon' and not end['items'] and not end['gold']
            assert int(end['mint']) == int(end['burn']) == end['units'] == 0
            assert not items[j['token']]['consumed'] and items[j['token']]['location'] == 'World'
    assert sorted(owners.values()) == [20, 40]
    assert terminals == {'B92CourierDelivery': 22, 'B92CourierAbandon': 38}
    assert int(ledger['start']) == 3288 and int(ledger['minted']) == 550
    assert len(ledger['accounts']) == 149
    assert not any(r['cause'] == 'DiagnosticFixtureTransfer' for r in receipts.values())
    assert sum(int(r['burn']) for r in receipts.values() if r['cause'] == 'DeathTax') == 4
    assert read(root / 'A_42.json')['accepted'] and read(root / 'A2_46.json')['accepted']
    assert all(not read(root / f'{role}_{task}.json')['accepted'] for role, task in [('A',43),('A',44),('A',45),('B',47)])
    assert read(root / 'B_48.json')['accepted'] and read(root / 'A_48.json')['accepted']
    print(f'PASS B92 courier: {len(data["checks"])} checks;60 issued/22 paid/38 void; two remote deliveries;550 bounded mint')


if __name__ == '__main__':
    check(sys.argv[1])
