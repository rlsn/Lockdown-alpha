"""Replay earned zero-kill Charter progression; travel and alternative builds are fixtures."""
import json
import sys
from collections import Counter
from pathlib import Path
from CheckEconomyEvidence import verify
from CheckB92Courier import distance


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def check(root):
    root = Path(root)
    data = read(root / 'Charter.json')
    assert data['scope'] == 'earned_witness_registered_charter'
    assert data['complete'] and data['staged_travel'] and not data['full_stage_acceptance']
    assert not data['failures'] and len(data['checks']) >= 40
    assert all(c['passed'] for c in data['checks'])
    verify(root / 'Ledger.json')
    ledger = read(root / 'Ledger.json')
    receipts = {r['id']: r for r in ledger['receipts']}
    causes = Counter(r['cause'] for r in receipts.values())
    assert int(ledger['start']) == 3288 and int(ledger['minted']) == 280
    institution_burn = sum(int(r['burn']) for r in receipts.values() if r['cause'] == 'B92InstitutionProduction')
    assert int(ledger['burned']) - institution_burn == 220
    assert all(int(r['mint']) == 0 for r in receipts.values() if r['cause'].startswith('B92Institution'))
    assert causes['B92CharterWitness'] == 4
    assert causes['B92RegisteredCharterPurchase'] == 1
    assert causes['B92CourierDelivery'] == causes['B92CourierIssuance'] == 6
    assert not causes['DiagnosticFixtureTransfer'] and not causes['DeathTax']
    stages = {s['stage']: s for s in data['stages']}
    initial, four, read_book, prepared, witness, end = [stages[s] for s in
        ('initial', 'four_couriers', 'book_read', 'earned_score12', 'earned_witness', 'registered_charter')]
    assert (initial['gold'], initial['bank'], initial['xp'], initial['parley_rank']) == (0, 0, 0, 0)
    assert not initial['items']
    assert (four['gold'], four['xp'], four['level']) == (100, 180, 3)
    assert (read_book['gold'], read_book['xp'], read_book['parley_rank']) == (60, 180, 1)
    assert not read_book['items']
    assert (prepared['cha'], prepared['parley_rank'], prepared['slot0'], prepared['ap'], prepared['sp']) == (10, 2, 6, 0, 1)
    assert (witness['gold'], witness['xp']) == (60, 180)
    assert (end['gold'], end['bank'], end['xp'], end['level'], end['life']) == (60, 0, 360, 4, 1)
    assert not end['items'] and len({s['seat'] for s in stages.values()}) == 1
    assert all(s['life'] == 1 for s in stages.values())
    for field, cause in [('witness', 'B92CharterWitness'), ('industrial', 'B92CoreProcurement'),
                         ('supply', 'B92SupplyProcurement'), ('charter', 'B92RegisteredCharterPurchase')]:
        assert receipts[end[field]]['cause'] == cause
    purchase = receipts[end['charter']]
    assert int(purchase['burn']) == 100 and int(purchase['mint']) == 0
    assert sorted(int(g['delta']) for g in purchase['gold']) == [-50, -50]
    assert len({g['account'] for g in purchase['gold']}) == 2 and not purchase['items']
    for receipt in receipts.values():
        if receipt['cause'] == 'B92CharterWitness':
            assert not receipt['gold'] and not receipt['items']
            assert int(receipt['mint']) == int(receipt['burn']) == 0
    for task in (62, 63, 64, 65, 68):
        assert not read(root / f'A_{task}.json')['accepted']
    for task in (51, 66, 67):
        result = read(root / f'A_{task}.json')
        assert result['accepted'] and result['duplicate_same']
    assert read(root / 'A_58.json')['accepted']
    assert not read(root / 'A_57.json')['accepted']
    contracts = read(root / 'Contracts.json')['contracts']
    assert len(contracts) == 6 and all(not c['active'] for c in contracts)
    assert len({c['owner'] for c in contracts}) == 1
    assert len({c['token'] for c in contracts}) == 6
    items = {i['id']: i for i in ledger['items']}
    for contract in contracts:
        assert contract['distance_cm'] == distance(contract['origin'], contract['destination'])
        assert 25000 <= contract['distance_cm'] <= 80000
        issue, payout = receipts[contract['contract']], receipts[contract['terminal']]
        assert issue['cause'] == 'B92CourierIssuance' and payout['cause'] == 'B92CourierDelivery'
        assert int(payout['mint']) == 25 and issue['items'][0]['id'] == payout['items'][0]['id'] == contract['token']
        assert issue['items'][0]['to'] == payout['items'][0]['from'] == contract['owner']
        assert items[contract['token']]['consumed'] and items[contract['token']]['origin'] == contract['contract']
    official = [r for r in receipts.values() if r['cause'] == 'B92OfficialPurchase']
    assert len(official) == 13 and sum(int(r['burn']) for r in official) == 96
    assert all(len(r['items']) == 1 and items[r['items'][0]['id']]['consumed'] for r in official)
    crafts = [r for r in receipts.values() if r['cause'] == 'B92CraftProduction']
    assert len(crafts) == 5 and sum(int(r['burn']) for r in crafts) == 22
    assert all(len([i for i in r['items'] if i['op'] == 0]) == 1 for r in crafts)
    assert all(len(i['parents']) == 3 for r in crafts for i in r['items'] if i['op'] == 0)
    assert causes['B92DepositExtraction'] == causes['B92CoreProcurement'] == causes['B92SupplyProcurement'] == 1
    print(f'PASS B92 earned Charter: {len(data["checks"])} checks; six courier channels; actual book/build/work/proofs;280 mint/220 burn')


if __name__ == '__main__':
    check(sys.argv[1])
