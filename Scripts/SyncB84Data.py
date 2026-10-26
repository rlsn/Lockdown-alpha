#!/usr/bin/env python3
"""Compile the complete B84 catalog into isolated schema8 UFS payloads."""
import argparse,hashlib,json,re
from pathlib import Path
from SyncB83Data import native_ids
from SyncB81Data import encode
from SyncV08Data import verify_provenance,validate_schema
ROOT=Path(__file__).resolve().parents[1]
def main():
    check=argparse.ArgumentParser();check.add_argument('--check',action='store_true');args=check.parse_args()
    verify_provenance(ROOT)
    def read(n):return json.loads((ROOT/f'docs/v08/data/{n}.json').read_text())
    ad=json.loads((ROOT/'docs/v08/B80_ADAPTERS.json').read_text())
    schema=json.loads((ROOT/'docs/v08/schemas/catalog.schema.json').read_text())
    for n in ['abilities','items']:validate_schema(read(n),schema,n)
    skills=native_ids('Source/Lockdown/Public/Development/LDCharacterBuild.h','ELDSkill')
    kinds=native_ids('Source/Lockdown/Public/Inventory/LDInventoryTypes.h','ELDItemKind')
    for n,v in skills.items():
        key='legacy_'+n.lower() if 1<=v<=9 else re.sub(r'(?<!^)(?=[A-Z])','_',n).lower()
        assert ad['ability_ids'][key]==v,(n,v)
    source=(ROOT/'Source/Lockdown/Private/Inventory/LDEquipmentCatalog.cpp').read_text()
    mappings=re.findall(r'(?:\{TEXT\("([a-z_]+)"\),|Kinds.Add\(TEXT\("([a-z_]+)"\),)ELDItemKind::(\w+)',source)
    names={a or b:n for a,b,n in mappings}
    items=read('items')['items'];abilities=read('abilities')['abilities'];acq=read('acquisition')
    assert len(items)==80 and len(abilities)==52 and len(acq['recipes'])==20
    assert all(ad['item_ids'][i['id']]==kinds[names[i['id']]] for i in items)
    gates=[{**{k:a[k] for k in ['id','circle','school','requirements_any','potency_fields']},'skill':ad['ability_ids'][a['id']]} for a in abilities]
    values={'combat':dict(schema_version=8,milestone='B84',abilities=abilities,starter_kits=read('starter_kits')['templates'],environment=read('environment'),modifiers=read('modifier_contracts')),
            'equipment':dict(schema_version=8,milestone='B84',items=items,ability_gates=gates,modifiers=read('modifier_contracts')),
            'acquisition':dict(items=[{**i,'kind':names[i['id']]} for i in items],acquisition=acq)}
    outputs={};hashes={}
    for n,v in values.items():
        raw=encode(v);outputs[f'Content/Lockdown/Data/V08/{n}_b84.json']=raw;hashes[n]=hashlib.sha256(raw).hexdigest()
    outputs['Source/Lockdown/Private/Development/LDB84Contract.h']=('#pragma once\n'+''.join(f'#define LD_B84_{k.upper()}_SHA TEXT("{v}")\n' for k,v in hashes.items())).encode()
    outputs['Content/Lockdown/Data/V08/B84Manifest.json']=encode(dict(milestone='B84',enabled_abilities=[a['id'] for a in abilities],enabled_items=[i['id'] for i in items],enabled_recipes=[r['id'] for r in acq['recipes']],enabled_presets=list(range(9)),payload_sha256=hashes))
    for n,raw in outputs.items():
        p=ROOT/n
        if args.check:assert p.read_bytes().replace(b'\r\n',b'\n')==raw,n
        elif not p.exists() or p.read_bytes()!=raw:p.write_bytes(raw)
    print('PASS B84:52 abilities /80 items /20 recipes /18 modifier contracts; stable native reservations')
if __name__=='__main__':main()
