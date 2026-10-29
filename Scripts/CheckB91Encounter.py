import json,sys
from pathlib import Path
root=Path(sys.argv[1])
r=json.loads((root/'Encounter.json').read_text(encoding='utf-8-sig'))
assert r['complete'] and r['phase']==91 and not r['failures']
assert len(r['checks'])>=40 and all(v['passed'] for v in r['checks'])
assert r['walked_cm']>10000 and r['falling_s']<.1
for phase in range(1,28):
    for role in ('A','B'):
        d=json.loads((root/f'{role}_{phase}.json').read_text(encoding='utf-8-sig'));assert d['completed']
assert (root/'EarnedWorld.json').is_file()
print('PASS B91 connected reference:',len(r['checks']),'checks')
