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
    bank = data.get('bank_integration', False)
    official = data.get('official_integration', False)
    deposits_enabled = data.get('deposit_integration', False)
    survey_enabled = data.get('survey_integration', False)
    extraction = [r for r in ledger['receipts'] if r['cause'] in ('B92DepositExtraction', 'B92SurveyExtraction')]
    if survey_enabled:
        assert len(data['checks']) >= 67
        survey = [r for r in extraction if r['cause'] == 'B92SurveyExtraction']
        assert len(survey) == 2 and all(r['items'][0]['kind'] == 5 for r in survey)
        for task in (27,28,29,30,31,32):
            assert read(root / f'A2_{task}.json')['completed']
        assert read(root / 'A2_28.json')['accepted'] and read(root / 'A2_30.json')['accepted']
        assert all(not read(root / f'A2_{task}.json')['accepted'] for task in (27,29,31,32))
    if deposits_enabled:
        from collections import Counter
        assert len(data['checks']) >= 59 and len(extraction) == 54
        assert len({r['budget'] for r in extraction}) == 9
        assert set(Counter(r['budget'] for r in extraction).values()) == {6}
        assert Counter(r['items'][0]['kind'] for r in extraction) == {5: 30, 4: 12, 3: 12}
        assert all(r['units'] == 1 and int(r['mint']) == int(r['burn']) == 0 for r in extraction)
        for r in extraction:
            assert not r['gold'] and len(r['items']) == 1
            expected_kind = 5 if ':crystal_' in r['budget'] else 4 if ':ore_' in r['budget'] else 3
            assert r['items'][0]['kind'] == expected_kind
            assert any(i['id'] == r['items'][0]['id'] and i['origin'] == r['id'] for i in ledger['items'])
        assert all(b['initial'] == 6 and b['remaining'] == 0 for b in ledger['budgets'] if b['id'].startswith('B92Deposit:'))
        assert len([b for b in ledger['budgets'] if b['id'].startswith('B92Deposit:')]) == 9
        assert read(root / 'A2_26.json')['completed'] and read(root / 'A2_25.json')['completed']
    purchases = [r for r in ledger['receipts'] if r['cause'] == 'B92OfficialPurchase']
    official_burn = sum(int(r['burn']) for r in purchases)
    assert (int(ledger['burned']) - official_burn) % 5 == (1 if bank else 0)
    if official:
        assert len(data['checks']) >= 52 and len(purchases) == 3 and official_burn == 184
        books = [r for r in purchases if r['budget'].startswith('B92OfficialBook')]
        assert len(books) == 2 and books[0]['budget'] == books[1]['budget']
        assert all(r['units'] == 1 and int(r['burn']) == 90 for r in books)
        assert all(len(r['items']) == 1 and r['items'][0]['kind'] == 0
                   and r['items'][0]['skill'] == int(r['budget'].removeprefix('B92OfficialBook')) for r in books)
        assert len({r['items'][0]['to'] for r in books}) == 2
        for r, report in zip(books, ('A2_19.json', 'B_20.json')):
            item_id = r['items'][0]['id']
            assert any(i['id'] == item_id and i['kind'] == 0 for i in read(root / report)['items'])
            assert any(i['id'] == item_id and i['origin'] == r['id'] for i in ledger['items'])
        budgets = [b for b in ledger['budgets'] if b['id'] == books[0]['budget']]
        assert len(budgets) == 1 and budgets[0]['initial'] == 2 and budgets[0]['remaining'] == 0
        for role, task in [('A2',19),('B',20),('A2',21),('A2',22),('A2',23),('A2',24)]:
            assert read(root / f'{role}_{task}.json')['completed']
        assert not read(root / 'A2_21.json')['accepted'] and not read(root / 'A2_23.json')['accepted']
    if bank:
        assert len(data['checks']) >= 45
        for role, task in [('A', 15), ('A', 17), ('A', 18), ('A2', 16)]:
            assert read(root / f'{role}_{task}.json')['completed']
        deposits = [r for r in ledger['receipts'] if r['cause'] == 'BankDeposit']
        withdrawals = [r for r in ledger['receipts'] if r['cause'] == 'BankWithdrawal']
        assert len(deposits) == len(withdrawals) == 1
        assert int(deposits[0]['burn']) == 1 and int(withdrawals[0]['burn']) == 0
        assert len(deposits[0]['gold']) == len(withdrawals[0]['gold']) == 2
        debit = {g['account']: int(g['delta']) for g in deposits[0]['gold']}
        credit = {g['account']: int(g['delta']) for g in withdrawals[0]['gold']}
        assert debit.keys() == credit.keys()
        assert sorted(debit.values()) == [-3, 2] and sorted(credit.values()) == [-2, 2]
        bank_account = next(a for a in debit if a.startswith('Bank:'))
        assert debit[bank_account] == 2 and credit[bank_account] == -2
        assert not read(root / 'A_17.json')['accepted'] and not read(root / 'A_18.json')['accepted']
        assert read(root / 'A_15.json')['bank'] == '2' and read(root / 'A2_16.json')['bank'] == '0'
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
    assert len(ledger['items']) == 570 + producers + len(purchases) + len(extraction)
    assert sum(i['consumed'] for i in ledger['items']) == 1 + consumes
    assert sum(not i['consumed'] for i in ledger['items']) == 569 + producers - consumes + len(purchases) + len(extraction)
    assert all(r['cause'] != 'DiagnosticReward' for r in events)
    fixtures = [r for r in events if r['cause'] == 'DiagnosticFixtureTransfer']
    assert len(fixtures) == (1 if official else 0)
    if fixtures:
        assert int(fixtures[0]['mint']) == int(fixtures[0]['burn']) == 0
        assert sorted(int(g['delta']) for g in fixtures[0]['gold']) == [-400, 100, 300]
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
