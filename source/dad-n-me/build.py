"""Rebuild frozen R24,then reproduce the formal Dad n Me PBP and runtime payload."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import sys

R=Path(__file__).resolve().parent
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pspdev',type=Path,default=os.environ.get('PSPDEV'))
    p.add_argument('--cmake',default='cmake');p.add_argument('--build-dir',type=Path);a=p.parse_args()
    if not a.pspdev:p.error('Set PSPDEV or pass --pspdev')
    cmd=[sys.executable,str(R/'candidate_build.py'),'--pspdev',str(a.pspdev),'--cmake',a.cmake]
    if a.build_dir:cmd+=['--build-dir',str(a.build_dir)]
    subprocess.run(cmd,check=True)
    candidate=json.loads((R/'CANDIDATE.json').read_text());native=json.loads((R/'TESTED_IDENTITY.json').read_text())
    source=R/'dist/PSP/GAME'/candidate['install_folder'];assert sha(source/'EBOOT.PBP')==candidate['tested_eboot_sha256']
    target=R/'dist/PSP/GAME/DadNMe';target.mkdir(parents=True,exist_ok=True)
    for n,h in native['resources'].items():
        if n in ['EBOOT.PBP','SHA256SUMS']:continue
        assert sha(source/n)==h,n;(target/n).parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source/n,target/n)
    spec=importlib.util.spec_from_file_location('repack',R/'tools/repack_xmb.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    formal,promotion=module.repack((source/'EBOOT.PBP').read_bytes(),(R/'artwork/ICON0.png').read_bytes(),(R/'artwork/PIC1.png').read_bytes())
    (target/'EBOOT.PBP').write_bytes(formal);shutil.copytree(R/'packaging',target,dirs_exist_ok=True)
    expected=json.loads((R/'OFFICIAL_IDENTITY.json').read_text())
    for n,h in expected['resources'].items():assert sha(target/n)==h,n
    files=sorted(x for x in target.rglob('*') if x.is_file() and x.name!='SHA256SUMS')
    (target/'SHA256SUMS').write_text(''.join(sha(x)+'  '+str(x.relative_to(target))+'\n' for x in files))
    proof=dict(status='passed',native_sources_unchanged=all(sha(R/n)==h for n,h in native['source_files'].items()),
        cmake_unchanged=sha(R/'CMakeLists.txt')==native['cmake_sha256'],
        formal_eboot_byte_identical=sha(target/'EBOOT.PBP')==expected['resources']['EBOOT.PBP'],
        runtime_assets_unchanged=True,formal_resources_exact=True,**promotion)
    assert proof['native_sources_unchanged'] and proof['cmake_unchanged'] and proof['formal_eboot_byte_identical']
    (R/'REBUILD_OFFICIAL.json').write_text(json.dumps(proof,ensure_ascii=False,indent=2)+'\n');print(json.dumps(proof,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
