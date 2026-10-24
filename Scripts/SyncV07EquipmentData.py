"""Materialize B73 supported equipment and inert gate metadata; no acquisition grants."""
from pathlib import Path
import json,hashlib,argparse
r=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('--check',action='store_true');args=p.parse_args()
selected=['iron_halberd', 'simple_staff', 'runic_staff', 'conduit_staff', 'bastion_plate', 'archivist_robe', 'merchant_harness', 'pathfinder_boots', 'scholars_sigil', 'warding_eye', 'weighted_satchel', 'brokers_seal']
items=json.loads((r/'docs/v07/data/items.json').read_text())['items'];abilities=json.loads((r/'docs/v07/data/abilities.json').read_text())['abilities']
payload={'schema_version':7,'milestone':'B73 equipment subset; sources B74','items':[i for i in items if i['id'] in selected],'ability_gates':[{**{k:a[k] for k in ['id','circle','school','requirements_any','potency_fields']},'skill':10+n} for n,a in enumerate(abilities)]}
raw=(json.dumps(payload,indent=2)+'\n').encode();header=('#pragma once\n#define LD_EQUIPMENT_SHA TEXT("'+hashlib.sha256(raw).hexdigest()+'")\n').encode()
paths=[(r/'Content/Lockdown/Data/V07/equipment.json',raw),(r/'Source/Lockdown/Private/Inventory/LDEquipmentContract.h',header)]
for path,content in paths:
 if args.check: assert path.read_bytes().replace(b'\r\n',b'\n')==content,path
 else:path.write_bytes(content)
print('PASS B73 equipment contract '+hashlib.sha256(raw).hexdigest())
