"""Validate and freeze the final environment stage after committing native inputs."""
import argparse,hashlib,json,shutil,subprocess
from pathlib import Path
from ExportB91ObjectsEvidence import ROOT,digest
from CheckB91Combat import check as combat
from CheckB91Evidence import check as structural
from CheckB91Network import check as doors
from CheckB91Saplings import check as saplings
from CheckB91Grass import check as grass
from CheckB91Ignition import check as ignition
from CheckB91Tools import check as tools
from CheckB91Traversal import check as traversal
from CheckB91Objects import check as objects
from CheckB91Fields import check as fields
from CheckB91Checkpoint import check as checkpoint
from CheckEconomyEvidence import verify

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
    p=argparse.ArgumentParser();p.add_argument('runs',type=Path);p.add_argument('--check',action='store_true');a=p.parse_args();runs={k:Path(v) for k,v in read(a.runs).items()};rules=validate(runs)
    baseline=read(ROOT/'docs/v09/evidence/B91/Audit.json')
    for key in ('unchanged_generated_assets_sha256','unchanged_supplied_files_sha256'):
        for name,sha in baseline[key].items():
            path=ROOT/name if key=='unchanged_generated_assets_sha256' else ROOT/'docs/v09'/name
            assert hashlib.sha256(path.read_bytes()).hexdigest()==sha,name
    if a.check:print('PASS final B91 stage evidence and protected inputs');return
    subprocess.run(['git','diff','--exit-code','HEAD','--','Source','Config','Scripts','Content/Lockdown/Data/V09'],cwd=ROOT,check=True,capture_output=True)
    assert not subprocess.check_output(['git','ls-files','--others','--exclude-standard','Source','Config','Scripts','Content/Lockdown/Data/V09'],cwd=ROOT).strip()
    out=ROOT/'docs/v09/evidence/B91Completion';assert not out.exists();out.mkdir()
    for name,path in runs.items():
        dest=out/name;dest.mkdir()
        for f in path.iterdir():
            if f.is_file() and f.suffix.lower() in ('.json','.csv','.log','.png'):shutil.copyfile(f,dest/f.name)
    shutil.copyfile(runs['rules']/'Report/index.json',out/'Rules.json')
    (out/'README.md').write_text('# B91 final environment evidence\n\nSee ../../B91_COMPLETION.md for scopes, user control-policy supersession and per-case evidence. Native source inputs are pinned by Audit.json. Initial tools/builds and diagnostic wall/Guard fixtures are labelled; ordinary movement, requests, item costs, reactions, life return and earned environment reload use native authority. One reference owner is rendered with a headless peer; captures are not performance or human pacing evidence. 3,000 scenery instances and864 native service-front paths are checked. B92 acquisition/population/economy, B93 victory and B94 full-session acceptance remain separate. Historical failed attempts are preserved in diagnostic folders; final scenario folders contain passing runs.\n',encoding='utf-8')
    files=subprocess.check_output(['git','ls-files','Source','Config','Scripts','Content/Lockdown/Data/V09'],cwd=ROOT,text=True).splitlines()
    audit=dict(source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),stage='B91',full_stage_acceptance=True,control_policy='CONTROL_POLICY_OVERRIDE.md supersedes source-free F/Z/R and door lockwork; no breaching source selected',rules=rules['succeeded'],source_sha256_normalized_lf={n:digest(ROOT/n) for n in files},unchanged_generated_assets_sha256=baseline['unchanged_generated_assets_sha256'],unchanged_supplied_files_sha256=baseline['unchanged_supplied_files_sha256'],files_sha256_normalized_lf={f.relative_to(out).as_posix():digest(f) for f in sorted(out.rglob('*')) if f.is_file()})
    (out/'Audit.json').write_text(json.dumps(audit,indent=2)+'\n',encoding='utf-8');print('PASS frozen B91 completion evidence')
if __name__=='__main__':main()
