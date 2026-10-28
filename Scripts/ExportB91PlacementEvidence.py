"""Freeze checkpoint placement validation without rewriting historical evidence."""
import argparse, hashlib, json, shutil, subprocess
from pathlib import Path
from ExportB91ObjectsEvidence import ROOT, digest
from CheckB91Checkpoint import check, read

def main():
    parser=argparse.ArgumentParser()
    for name in ('checkpoint','editor','game'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    inputs=('Source','Config','Scripts')
    subprocess.run(['git','diff','--exit-code','HEAD','--',*inputs],cwd=ROOT,check=True,capture_output=True)
    assert not subprocess.check_output(['git','ls-files','--others','--exclude-standard',*inputs],cwd=ROOT).strip()
    counts=check(args.checkpoint,placement=True)
    assert sum(counts)>=58
    baseline=json.loads(read(ROOT/'docs/v09/evidence/B91/Audit.json'))
    for key in ('unchanged_generated_assets_sha256','unchanged_supplied_files_sha256'):
        for name,sha in baseline[key].items():
            path=ROOT/name if key=='unchanged_generated_assets_sha256' else ROOT/'docs/v09'/name
            assert hashlib.sha256(path.read_bytes()).hexdigest()==sha,name
    for name in ('editor','game'):
        assert 'Result: Succeeded' in read(getattr(args,name)/('Build'+name.title()+'.log'))
    out=ROOT/'docs/v09/evidence/B91Placement'
    assert not out.exists()
    out.mkdir()
    shutil.copytree(args.checkpoint,out/'checkpoint')
    for name in ('editor','game'):
        filename='Build'+name.title()+'.log'
        shutil.copyfile(getattr(args,name)/filename,out/filename)
    (out/'README.md').write_text('# B91 checkpoint placement validation\n\nFour sequential native processes verify a legally relocated crate across restart and reject floating, inconsistent, overlapping and wall-intersecting cargo before mutation. Mutations are diagnostic fixtures; no new earned cargo journey, live-owner recovery, performance or whole-B91 acceptance is claimed. The Editor target excludes optional Fab because this installed engine lacks FabToolset module rules.\n',encoding='utf-8')
    files=subprocess.check_output(['git','ls-files',*inputs],cwd=ROOT,text=True).splitlines()
    audit=dict(source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),full_stage_acceptance=False,native_counts=counts,
        source_sha256_normalized_lf={n:digest(ROOT/n) for n in files},
        unchanged_generated_assets_sha256=baseline['unchanged_generated_assets_sha256'],unchanged_supplied_files_sha256=baseline['unchanged_supplied_files_sha256'],
        files_sha256_normalized_lf={p.relative_to(out).as_posix():digest(p) for p in sorted(out.rglob('*')) if p.is_file()})
    (out/'Audit.json').write_text(json.dumps(audit,indent=2)+'\n',encoding='utf-8')
    print('PASS frozen placement evidence',counts)
if __name__=='__main__': main()
