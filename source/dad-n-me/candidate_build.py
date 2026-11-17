"""Rebuild the qualified candidate using its explicit package settings."""
from pathlib import Path
import argparse, os, subprocess, shutil, json, hashlib

r = Path(__file__).resolve().parent
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--pspdev', type=Path, default=os.environ.get('PSPDEV'))
p.add_argument('--cmake', default='cmake')
p.add_argument('--build-dir', type=Path)
a = p.parse_args()
if not a.pspdev:
    p.error('Set PSPDEV or pass --pspdev')
sdk = Path(a.pspdev).resolve()
build = (a.build_dir or r/'build').resolve()
info = json.loads((r/'CANDIDATE.json').read_text())
tested = json.loads((r/'TESTED_IDENTITY.json').read_text())

def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

native_unchanged = all(sha(r/k) == v for k, v in tested['source_files'].items())
cmake_unchanged = sha(r/'CMakeLists.txt') == tested['cmake_sha256']
env = dict(os.environ, PSPDEV=str(sdk), PATH=str(sdk/'bin')+os.pathsep+os.environ['PATH'],
           SOURCE_DATE_EPOCH='1794355200', LC_ALL='C')
subprocess.run([a.cmake, '-S', str(r), '-B', str(build),
                '-DCMAKE_TOOLCHAIN_FILE='+str(sdk/'psp/share/pspdev.cmake'),
                '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_MAKE_PROGRAM=/usr/bin/make']+
               ['-D'+v for v in info['cmake_options']], env=env, check=True)
subprocess.run([a.cmake, '--build', str(build), '--parallel', '4'], env=env, check=True)
dest = r/'dist/PSP/GAME'/info['install_folder']
shutil.copytree(r/'assets', dest, dirs_exist_ok=True)
shutil.copy2(build/'EBOOT.PBP', dest/'EBOOT.PBP')
(dest/'SHA256SUMS').write_text(''.join(sha(f)+'  '+f.name+'\n' for f in sorted(dest.iterdir())
    if f.is_file() and f.name not in ['SHA256SUMS', 'build-identity.json']))
resources = {f.name:sha(f) for f in dest.iterdir() if f.is_file() and f.name!='build-identity.json'}
assets_unchanged = all(resources.get(k)==v for k,v in tested['resources'].items()
                       if k not in ['EBOOT.PBP', 'SHA256SUMS'])
identity = dict(tested, resources=resources, status='source-rebuild',
                linker_map_sha256=sha(build/'dad_n_me.map'), cmake_options=info['cmake_options'])
(dest/'build-identity.json').write_text(json.dumps(identity, indent=2)+'\n')
result = {'status':'built', 'eboot_sha256':resources['EBOOT.PBP'],
          'tested_eboot_sha256':info['tested_eboot_sha256'],
          'byte_identical_to_tested':resources['EBOOT.PBP']==info['tested_eboot_sha256'],
          'native_sources_unchanged':native_unchanged, 'cmake_unchanged':cmake_unchanged,
          'assets_unchanged':assets_unchanged}
(r/'REBUILD.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result, indent=2))
if not all(result[k] for k in ['byte_identical_to_tested', 'native_sources_unchanged', 'cmake_unchanged', 'assets_unchanged']):
    raise SystemExit('Build differs from the tested candidate; inspect REBUILD.json before using it.')
