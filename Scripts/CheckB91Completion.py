"""Validate the current environment run matrix without historical archives."""
import argparse,json
from pathlib import Path
from CheckB91Combat import check as combat
from CheckB91Evidence import check as structural
from CheckB91Checkpoint import check as checkpoint
from CheckB91Saplings import check as saplings
from CheckB91Grass import check as grass
from CheckB91Ignition import check as ignition
from CheckB91Tools import check as tools
from CheckB91Traversal import check as traversal
from CheckB91Objects import check as objects
from CheckB91Fields import check as fields
from CheckB91Network import check as doors
from CheckEconomyEvidence import verify

ROOT = Path(__file__).resolve().parents[1]

def read(path):
    raw=path.read_bytes();return json.loads(raw.decode('utf-16' if raw[:2] in (b'\xff\xfe',b'\xfe\xff') else 'utf-8-sig'))

def validate(runs):
    for name,fn in [('combat',combat),('structural',structural),('doors',doors),('saplings',saplings),('grass',grass),('ignition',ignition),('tools',tools),('traversal',traversal),('objects',objects),('furniture',objects),('fields',fields)]:fn(runs[name])
    checkpoint(runs['checkpoint'],placement=True)
    e=read(runs['encounter']/'Encounter.json');assert e['complete'] and e['phase']==91 and not e['failures'] and len(e['checks'])>=44 and all(c['passed'] for c in e['checks']) and e['walked_cm']>10000 and e['falling_s']==0
    for phase in range(1,28):
        for role in ('A','B'):assert read(runs['encounter']/f'{role}_{phase}.json')['completed']
    e=read(runs['encounter']/'Resume.json');assert e['complete'] and not e['failures'] and len(e['checks'])>=3 and all(c['passed'] for c in e['checks'])
    for phase in (15,22,24):assert (runs['encounter']/f'Reference_{phase}.png').is_file()
    f=read(runs['forest']/'Forest.json');assert f['count']==f['walking_blockers']==f['sight_blockers']==3000 and f['navigation'] and f['not_replicated'] and f['no_tick']
    fitted=read(ROOT/'Content/Lockdown/Data/V09/forest_b91.json')['rows'];assert f['rows']==fitted
    for name in ('fit_a','fit_b'):assert read(runs[name]/'Forest.json')['rows']==fitted
    r=read(runs['routes']/'ForestRoutes.json');assert r['complete'] and not r['failures'] and len(r['routes'])==864 and all(v['passed'] for v in r['routes'])
    for name,n in [('Server',536),('A',40),('B',40)]:
        d=read(runs['legacy']/(name+'.json'));assert not d['failures'] and len(d['checks'])==n
    verify(runs['legacy']/'Ledger.json')
    r=read(runs['rules']/'Report/index.json');assert r['succeeded']>=56 and not any(r[k] for k in ('failed','notRun','inProcess'))
    for name in ('editor','game'):assert 'Result: Succeeded' in (runs[name]/('Build'+name.title()+'.log')).read_text(encoding='utf-8-sig')
    return r

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runs',type=Path)
    args=parser.parse_args()
    runs={name:Path(path) for name,path in read(args.runs).items()}
    validate(runs)
    print('PASS current B91 completion matrix')

if __name__=='__main__':main()
