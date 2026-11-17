from pathlib import Path
import hashlib,json,shutil,subprocess
R=Path(__file__).resolve().parents[1];T=R/'work-r24';S=R/'source-response56';D=R/'source-delivery/DadNMeResponseR24'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
assert json.loads((T/'SOURCE_SCOPE_CHECK.json').read_text())['status']=='pass'
assert json.loads((R/'work-r23/HOST_UNIT_CHECKS.json').read_text())['status']=='pass'
assert json.loads((T/'RECORDED_MOTION_CHECK.json').read_text())['pass']
assert json.loads((R/'work-r23/SOURCE_DIFFERENTIAL.json').read_text())['status']=='pass'
assert json.loads((T/'QA_PROGRESS.json').read_text())['status']=='pass'
assert not D.exists();shutil.copytree(S,D,ignore=shutil.ignore_patterns('runs','work','build','dist','qa','__pycache__'))
shutil.copy2(R/'source-delivery/DadNMeResponseR15/build.py',D/'build.py')
identity=json.loads((R/'package/response56/build-identity.json').read_text())
for directory in ['src','vendor','cmake','tests','tools']:
 for p in (D/directory).rglob('*'):
  if p.is_file() and '__pycache__' not in p.parts:identity['source_files'][str(p.relative_to(D))]=sha(p)
identity['scope']='R24: resolve original AS1 NULL variable for two ownership clears; reuse exact previous world endpoint instead of redundant inverse transforms. Original30Hz source cadence and accepted visual/damage/HOME repairs retained. Physical speed and all immobilization causes unverified.'
(D/'TESTED_IDENTITY.json').write_text(json.dumps(identity,indent=2)+'\n')
info=dict(candidate='response56',label='R24',title='Dad n Me Response R24 Test',install_folder='DadNMeResponseR24',tested_eboot_sha256=identity['resources']['EBOOT.PBP'],cmake_options=identity['cmake_options'],status='local-qualification-in-progress',baseline='response52 R20 local+card baseline;response55 R23 isolated control fix',changes=['Fix two f_Grab ownership clear values to match original NULL variable reads','Reuse supplied previous world matrix for unchanged drawing transform;local/singular fallback retained','No media/AI/damage/source cadence changes'],physical_performance_verified=False)
(D/'CANDIDATE.json').write_text(json.dumps(info,indent=2)+'\n')
c=['python3',str(D/'build.py'),'--pspdev','/Users/sumo/.local/share/psp-toolchains/pspdev-v20260701','--cmake','/Users/sumo/.local/share/psp-toolchains/cmake-3.31.10/cmake/data/bin/cmake','--build-dir',str(R/'build-delivery-response56')]
(T/'source-rebuild-command.json').write_text(json.dumps(c,indent=2)+'\n')
with (T/'source-rebuild.log').open('w') as log:subprocess.run(c,cwd=R,stdout=log,stderr=subprocess.STDOUT,check=True)
d=json.loads((D/'REBUILD.json').read_text());assert all(d[k] for k in ['byte_identical_to_tested','native_sources_unchanged','cmake_unchanged','assets_unchanged'])
print(json.dumps(d,indent=2))
