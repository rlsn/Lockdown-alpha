"""Portable integrity and native-result audit of the frozen B91 bundle."""
import hashlib,json,sys
from pathlib import Path
from CheckB91Combat import check as combat
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

def read(p):
    b=p.read_bytes();return json.loads(b.decode('utf-16' if b[:2] in (b'\xff\xfe',b'\xfe\xff') else 'utf-8-sig'))
def check(root):
    a=read(root/'Audit.json');assert a['stage']=='B91' and a['full_stage_acceptance'] and len(a['source_commit'])==40
    for name,sha in a['files_sha256_normalized_lf'].items():assert hashlib.sha256((root/name).read_bytes().replace(b'\r\n',b'\n')).hexdigest()==sha,name
    for name,fn in [('combat',combat),('doors',doors),('saplings',saplings),('grass',grass),('ignition',ignition),('tools',tools),('traversal',traversal),('objects',objects),('furniture',objects),('fields',fields)]:fn(root/name)
    checkpoint(root/'checkpoint',placement=True)
    for name in ('Encounter','Resume'):
        d=read(root/'encounter'/(name+'.json'));assert d['complete'] and not d['failures'] and all(v['passed'] for v in d['checks'])
    d=read(root/'structural/Environment.json');assert d['complete'] and not d['failures'] and len(d['walks'])==52 and all(w['passed'] and not w['falling_s'] for w in d['walks'])
    r=read(root/'routes/ForestRoutes.json');assert len(r['routes'])==864 and all(v['passed'] for v in r['routes'])
    r=read(root/'forest/Forest.json');assert r['count']==r['walking_blockers']==r['sight_blockers']==3000 and r['navigation'] and r['no_tick'] and r['not_replicated']
    for name in ('fit_a','fit_b'):assert read(root/name/'Forest.json')['rows']==r['rows']
    verify(root/'legacy/Ledger.json')
    r=read(root/'Rules.json');assert r['succeeded']>=56 and not any(r[k] for k in ('failed','notRun','inProcess'))
    print('PASS frozen B91 completion:',a['source_commit'])
if __name__=='__main__':check(Path(sys.argv[1]))
