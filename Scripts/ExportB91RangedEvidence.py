"""Freeze the bounded ranged-sapling increment after committing native inputs."""
import argparse,hashlib,json,shutil,subprocess,sys
from pathlib import Path
from ExportB91ObjectsEvidence import ROOT,digest
from CheckB91Saplings import check
from CheckB91Checkpoint import read
from CheckEconomyEvidence import verify

def main():
    parser=argparse.ArgumentParser()
    for name in ('ranged','legacy','rules','editor','game'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--first-ranged',type=Path,required=True)
    parser.add_argument('--second-ranged',type=Path,required=True)
    parser.add_argument('--third-ranged',type=Path,required=True)
    args=parser.parse_args()
    subprocess.run(['git','diff','--exit-code','HEAD','--','Source','Config','Scripts'],cwd=ROOT,check=True,capture_output=True)
    assert not subprocess.check_output(['git','ls-files','--others','--exclude-standard','Source','Config','Scripts'],cwd=ROOT).strip()
    check(args.ranged)
    data=json.loads(read(args.ranged/'Saplings.json'));assert data['phase']==18 and len(data['checks'])==53
    baseline=json.loads(read(ROOT/'docs/v09/evidence/B91/Audit.json'))
    for key in ('unchanged_generated_assets_sha256','unchanged_supplied_files_sha256'):
        for name,sha in baseline[key].items():
            p=ROOT/name if key=='unchanged_generated_assets_sha256' else ROOT/'docs/v09'/name
            assert hashlib.sha256(p.read_bytes()).hexdigest()==sha,name
    for role,n in zip(('Server','A','B'),(536,40,40)):
        d=json.loads(read(args.legacy/(role+'.json')));assert not d['failures'] and len(d['checks'])==n
    verify(args.legacy/'Ledger.json')
    rules=json.loads(read(args.rules/'Report/index.json'));assert rules['succeeded']>=55 and not any(rules[k] for k in ('failed','notRun','inProcess'))
    for name in ('editor','game'):assert 'Result: Succeeded' in read(getattr(args,name)/('Build'+name.title()+'.log'))
    out=ROOT/'docs/v09/evidence/B91Ranged';assert not out.exists();out.mkdir()
    for name in ('ranged','legacy','first_ranged','second_ranged','third_ranged'):
        dest=out/name;dest.mkdir()
        for p in getattr(args,name).iterdir():
            if p.is_file() and p.suffix in ('.json','.csv','.log'):shutil.copyfile(p,dest/p.name)
    for name in ('editor','game'):
        filename='Build'+name.title()+'.log';shutil.copyfile(getattr(args,name)/filename,out/filename)
    shutil.copyfile(args.rules/'Automation.log',out/'Automation.log');shutil.copyfile(args.rules/'Report/index.json',out/'Rules.json')
    (out/'README.md').write_text('# B91 ranged basic attacks against saplings\n\n53 native two-owner checks retain47 source/control checks and add traveling projectile clearance from250cm and wall refusal. Ranged gear and wall placement are diagnostic; actual remote basic RPCs and projectiles are exercised. No paid bow acquisition, spell damage, full-map live recovery, rendering/performance/human or full-B91 acceptance is claimed. first_ranged/ preserves an initial timeout because test RPCs still capped phases at16; the final test-only bound accepts18. second_ranged/ retains a subsequent timeout before verifying a grounded firing pad. third_ranged/ diagnoses the old203cm proxy-center endpoint above the150cm ground-query limit; final projectiles aim and resolve contact90cm above the supporting floor. Inherited fields retain616 assertions and replayed economy receipts.\n',encoding='utf-8')
    files=subprocess.check_output(['git','ls-files','Source','Config','Scripts'],cwd=ROOT,text=True).splitlines()
    audit=dict(source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),full_stage_acceptance=False,native_ranged_checks=53,inherited_field_assertions=616,rules=rules['succeeded'],source_sha256_normalized_lf={n:digest(ROOT/n) for n in files},unchanged_generated_assets_sha256=baseline['unchanged_generated_assets_sha256'],unchanged_supplied_files_sha256=baseline['unchanged_supplied_files_sha256'],files_sha256_normalized_lf={p.relative_to(out).as_posix():digest(p) for p in sorted(out.rglob('*')) if p.is_file()})
    (out/'Audit.json').write_text(json.dumps(audit,indent=2)+'\n',encoding='utf-8');print('PASS frozen ranged evidence')
if __name__=='__main__':main()
