"""Check native body admission/lifecycle evidence, without claiming NPC policy."""
import json
import sys
from collections import Counter
from pathlib import Path
from CheckEconomyEvidence import verify


def check(root):
    root = Path(root)
    data = json.loads((root / 'Roster.json').read_text(encoding='utf-8-sig'))
    assert data['stage'] == 'B92' and data['scope'] == 'native_population_admission_and_lifecycle'
    assert data['completed'] and not data['full_stage_acceptance'] and not data['failures']
    humans = data['human_capacity']
    assert humans in (4, 12) and len(data['checks']) >= 29 + humans * 2
    assert all(c['passed'] for c in data['checks'])
    assert len(data['seats']) == 24 and sum(s['human'] for s in data['seats']) == humans
    assert len(data['population']) == 196
    assert Counter(p['role'] for p in data['population']) == {0: 96, 1: 32, 2: 24, 3: 20, 4: 24}
    people = data['seats'] + data['population']
    assert len({p['id'] for p in people}) == 220 and all(p['body'] != 'Missing' for p in people)
    assert len({p['body'] for p in people}) == 220
    assert Counter(s['life'] for s in data['seats']) == {1: 23, 2: 1}
    verify(root / 'Ledger.json')
    ledger = json.loads((root / 'Ledger.json').read_text(encoding='utf-8-sig'))
    assert (int(ledger['start']), int(ledger['minted']), int(ledger['burned']), int(ledger['end'])) == (3288, 0, 3, 3285)
    assert len(ledger['accounts']) == 149 and len(ledger['items']) == 426
    assert len(ledger['receipts']) == 5
    assert [r['cause'] for r in ledger['receipts']] == ['B92MarketStockEndowment', 'DiagnosticFixtureTransfer', 'PeerTransfer', 'PeerTransfer', 'DeathTax']
    assert all(not i['consumed'] for i in ledger['items'])
    assert all(b['initial'] == b['remaining'] for b in ledger['budgets'])
    print(f'PASS B92 native roster: {len(data["checks"])} checks, {humans} human-controller fixtures, {24-humans} AI seats, 196 role bodies; no autonomous routine/policy or remote transport claim')


if __name__ == '__main__':
    check(sys.argv[1])
