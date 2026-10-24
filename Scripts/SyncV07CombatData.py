#!/usr/bin/env python3
"""Materialize the implemented B71 subset; runtime never reads docs.

Use --check in validation. Rebuild/import native assets after an authorized sync.
This does not implement operations or certify gameplay.
"""
import argparse
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check', action='store_true')
args = parser.parse_args()
data = root / 'docs/v07/data'
abilities = json.loads((data / 'abilities.json').read_text())['abilities']
items = json.loads((data / 'items.json').read_text())['items']
selected = {'anchor_bash', 'arc_bolt', 'purge_touch', 'surety_shell'}
payload = {'schema_version': 7, 'milestone': 'B71 partial',
           'abilities': [row for row in abilities if row['id'] in selected],
           'null_bell': next(row for row in items if row['id'] == 'null_bell')}
encoded = (json.dumps(payload, indent=2) + '\n').encode('utf-8')
target = root / 'Content/Lockdown/Data/V07/b71_combat.json'
if args.check:
    if target.read_bytes() != encoded:
        raise SystemExit('FAIL: staged B71 subset differs from adopted specification')
else:
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(encoded)
print('PASS: B71 implemented subset SHA-256 ' + hashlib.sha256(encoded).hexdigest())
