"""Materialize all40 B75 item definitions and ability gates; no acquisition grants."""
from pathlib import Path
import json,hashlib,argparse
r=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('--check',action='store_true');args=p.parse_args()
items=json.loads((r/'SourceData/Catalogs/V07/items.json').read_text(encoding='utf-8-sig'))['items'];abilities=json.loads((r/'SourceData/Catalogs/V07/abilities.json').read_text(encoding='utf-8-sig'))['abilities']
payload={'schema_version':7,'milestone':'B75 complete item semantics','items':items,'ability_gates':[{**{k:a[k] for k in ['id','circle','school','requirements_any','potency_fields']},'skill':10+n} for n,a in enumerate(abilities)]}
raw=(json.dumps(payload,indent=2)+'\n').encode();header=('#pragma once\n#define LD_EQUIPMENT_SHA TEXT("'+hashlib.sha256(raw).hexdigest()+'")\n').encode()
paths=[(r/'Content/Lockdown/Data/V07/equipment.json',raw),(r/'Source/Lockdown/Private/Inventory/LDEquipmentContract.h',header)]
for path,content in paths:
 if args.check: assert path.read_bytes().replace(b'\r\n',b'\n')==content,path
 else:path.write_bytes(content)
print('PASS B75 equipment contract '+hashlib.sha256(raw).hexdigest())
