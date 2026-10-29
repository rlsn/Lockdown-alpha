import json,sys
from pathlib import Path
def check(root):
    r=json.loads((root/'Combat.json').read_text(encoding='utf-8-sig'))
    assert r['complete'] and r['phase']==17 and not r['failures']
    assert len(r['checks'])>=50 and all(c['passed'] for c in r['checks'])
    for phase in (1,2,3,4,6,7,8,9,10,11,12,13,14,15,16,17):
        for role in ('A','B'):
            d=json.loads((root/f'{role}_{phase}.json').read_text(encoding='utf-8-sig'))
            assert d['completed'] and set(d['surface_fields'])=={'Position','Revision','States'}
    print('PASS B91 combat:',len(r['checks']),'checks')
if __name__=='__main__':check(Path(sys.argv[1]))
