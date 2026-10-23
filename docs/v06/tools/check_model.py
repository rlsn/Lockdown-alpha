"""Specification arithmetic checks only. No Unreal or gameplay implementation tests.

Run with Python 3: python tools/check_model.py
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
R = json.loads((ROOT / 'data/economy_rules.json').read_text(encoding='utf-8'))


def ceildiv(a, b):
    return -(-a // b)


def ask(base, target, stock, num=1, den=1):
    return max(ceildiv(base, 2), min(ceildiv(3 * base, 2),
               ceildiv(base * (3 * target - stock) * num, 2 * target * den)))


def bid(base, target, stock, num=1, den=1):
    return min(7 * ask(base, target, stock, num, den) // 10,
               max(0, ask(base, target, stock + 1, num, den) - 1))


states = 0
for locality, market in R['markets'].items():
    for good, spec in market['goods'].items():
        base, target = spec['base'], spec['stock']
        ratios = [(1, 1), (5, 4)] if (locality, good) == ('EastTown', 'Toolkit') else [(1, 1)]
        for num, den in ratios:
            cap = max(12, 2 * target)
            for stock in range(cap + 1):
                a, b = ask(base, target, stock, num, den), bid(base, target, stock, num, den)
                assert 0 <= b < a
                assert ceildiv(base, 2) <= a <= ceildiv(3 * base, 2)
                if stock > 0:
                    assert bid(base, target, stock - 1, num, den) < a
                if stock < cap:
                    assert ask(base, target, stock + 1, num, den) > b
                states += 1

assert [ask(12, 8, s) for s in [8, 7, 6]] == [12, 13, 14]
supply_cost = 2 * (2 * 4 + 6 + 4)
core_cost = supply_cost + 12 + 6
assert (supply_cost, core_cost) == (36, 54)
wallet = 3 * 25 - supply_cost + 50 - core_cost + 80 - 100
assert wallet == R['feasibility']['remainingGold'] == 15
assert 3 * 45 + 30 + 60 == 225
assert 225 + 3 * 25 == 300
assert 8 * 25 + 2 * 25 + 3 * 80 + 50 == 540
assert 3 * 150 + 8 * 36 == 738

# This trace includes institutional cash; Crystal purchase is a transfer.
start, minted, burned = 738, 3 * 25 + 50 + 80, supply_cost + core_cost - 12 + 100
end_wallets = wallet + (150 + 12) + 2 * 150 + 8 * 36
assert start + minted - burned == end_wallets == 765

deposit = 100
fee = ceildiv(2 * deposit, 100)
bank = deposit - fee
bank -= 30
assert (fee, bank, 30 + bank) == (2, 68, 98)
assert [60 + 2 * (s - 10) for s in [8, 10, 16]] == [56, 60, 72]
assert 20 * R['goods']['Ore']['weight'] == 60
assert 24 - 3 + 1 == 22  # Full bag, Toolkit inputs removed before output.
assert 1500 - 4 - 30 - 60 == 1406  # Latest purchase with immediate activation.
assert len(R['npcTicks']['times']) == 8
assert 8 * (2 + 3) == 40

print(f'PASS: {states} configured quote states, both immediate roundtrip directions, prices, costs, budgets, bank, cargo and timing arithmetic.')
print('PASS: illustrative full-account gold trace: 738 + 205 minted - 178 burned = 765.')
print('Not run: Unreal build, authority implementation, network, normal progression and human playtests.')
