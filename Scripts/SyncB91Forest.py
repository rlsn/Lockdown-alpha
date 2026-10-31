import argparse,hashlib,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--fit',type=Path);p.add_argument('--check',action='store_true');a=p.parse_args()
    raw=(ROOT/'SourceData/World/terrain.json').read_bytes();d=json.loads(raw)
    patches=[dict(id=r['id'],center=r['center_cm'],radius=r['radius_cm'],count=r['scenery_tree_target']) for r in d['forest_patches']]
    assert len(patches)==12 and len({r['id'] for r in patches})==12 and all(r['count']==250 for r in patches)
    config=(json.dumps(dict(schema=9,seed=9001,patches=patches,source_sha256=hashlib.sha256(raw).hexdigest()),sort_keys=True,separators=(',',':'))+'\n').encode()
    patch_sha=hashlib.sha256(config).hexdigest();outputs={'Content/Lockdown/Data/V09/forest_patches_b91.json':config}
    if a.fit:
        data=json.loads(a.fit.read_text(encoding='utf-8-sig'));assert data['schema']==9 and data['patch_contract']==patch_sha
        rows=data['rows'];assert len(rows)==len({r['id'] for r in rows})==3000
        for patch in patches:
            group=[r for r in rows if r['patch']==patch['id']];assert len(group)==250
            for r in group:
                assert set(r)=={'id','patch','floor'} and len(r['floor'])==3
                assert all(type(n) in (int,float) for n in r['floor'])
                assert sum((r['floor'][i]-patch['center'][i])**2 for i in (0,1))<=patch['radius']**2
        data['seed']=9001;data['revision']=1;data['trunk_cm']=[120,120,400]
        fitted=(json.dumps(data,sort_keys=True,separators=(',',':'))+'\n').encode();outputs['Content/Lockdown/Data/V09/forest_b91.json']=fitted
    else:
        path=ROOT/'Content/Lockdown/Data/V09/forest_b91.json';fitted=path.read_bytes().replace(b'\r\n',b'\n') if path.exists() else b''
    sha=hashlib.sha256(fitted).hexdigest() if fitted else 'pending'
    parent=re.search(r'LD_B91_GRASS_WORLD_SHA TEXT\(\"([0-9a-f]+)\"\)',(ROOT/'Source/Lockdown/Private/World/LDB91GrassContract.h').read_text()).group(1)
    world=hashlib.sha256((parent+sha+':scenery-v1:riverport-storage-v1:ledge-v1').encode()).hexdigest()
    outputs['Source/Lockdown/Private/World/LDB91ForestContract.h']=(f'#pragma once\n#define LD_B91_FOREST_WORLD_SHA TEXT("{world}")\n#define LD_B91_FOREST_PATCH_SHA TEXT("{patch_sha}")\n#define LD_B91_FOREST_SHA TEXT("{sha}")\n').encode()
    for name,value in outputs.items():
        path=ROOT/name
        if a.check:assert path.read_bytes().replace(b'\r\n',b'\n')==value,name
        else:path.write_bytes(value)
    print('PASS B91 forest contract:',sha)
if __name__=='__main__':main()
