"""Rebuild the tested solo/WLAN binaries from this standalone source tree."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess
R=Path(__file__).resolve().parent;p=argparse.ArgumentParser();p.add_argument('--pspdev',type=Path,default=os.environ.get('PSPDEV'));p.add_argument('--cmake',default='cmake');p.add_argument('--build-dir',type=Path);p.add_argument('--variant',choices=['solo','wlan','both'],default='both');a=p.parse_args()
if not a.pspdev:p.error('Set PSPDEV or pass --pspdev')
sdk=Path(a.pspdev).resolve();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();read=lambda p:json.loads(p.read_text());variants=read(R/'CANDIDATE.json')['variants'];root=(a.build_dir or R/'build').resolve();proof={}
env={**os.environ,'PSPDEV':str(sdk),'PATH':str(sdk/'bin')+os.pathsep+os.environ['PATH'],'SOURCE_DATE_EPOCH':'1794355200','LC_ALL':'C'}
for name in (['solo','wlan']if a.variant=='both'else[a.variant]):
 info=variants[name];identity=read(R/('TESTED_'+name.upper()+'_IDENTITY.json'));build=root/name
 subprocess.run([a.cmake,'-S',str(R),'-B',str(build),'-DCMAKE_TOOLCHAIN_FILE='+str(sdk/'psp/share/pspdev.cmake'),'-DCMAKE_MAKE_PROGRAM=/usr/bin/make','-DCMAKE_BUILD_TYPE=Release',*['-D'+v for v in info['cmake_options']]],env=env,check=True)
 subprocess.run([a.cmake,'--build',str(build),'--parallel','3'],env=env,check=True)
 dest=R/'dist/PSP/GAME'/info['install_folder'];dest.mkdir(parents=True,exist_ok=True)
 for n,h in identity['resources'].items():
  if n=='EBOOT.PBP':continue
  source=R/'assets'/name/n
  if not source.exists():source=R/'assets/common'/n
  target=dest/n;target.parent.mkdir(parents=True,exist_ok=True);assert sha(source)==h,n;shutil.copy2(source,target)
 shutil.copy2(build/'EBOOT.PBP',dest/'EBOOT.PBP');actual=sha(dest/'EBOOT.PBP');identity['status']='source-rebuild';identity['linker_map_sha256']=sha(build/'double_edged.map');(dest/'build-identity.json').write_text(json.dumps(identity,indent=2)+'\n')
 proof[name]=dict(status='built',eboot_sha256=actual,tested_eboot_sha256=info['tested_eboot_sha256'],byte_identical_to_tested=actual==info['tested_eboot_sha256'],native_sources_unchanged=all(sha(R/n)==h for n,h in identity['source_files'].items()),assets_unchanged=all(sha(dest/n)==h for n,h in identity['resources'].items()if n!='EBOOT.PBP'))
 (R/('REBUILD_'+name.upper()+'.json')).write_text(json.dumps(proof[name],indent=2)+'\n')
(R/'REBUILD.json').write_text(json.dumps(dict(status='built',variants=proof),indent=2)+'\n');print(json.dumps(proof,indent=2))
