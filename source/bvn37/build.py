#!/usr/bin/env python3
"""Rebuild BVN37 1.0.3 using the matching verified high-density installation data."""
import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import zipfile

root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--install-zip', type=Path, required=True,
                    help='Complete BVN37_PSP_1.0.3_Install.zip')
parser.add_argument('--build-dir', type=Path, default=root / 'build/psp')
parser.add_argument('--output', type=Path, default=root / 'build/install/PSP/GAME/BVN37')
parser.add_argument('--require-identical', action='store_true',
                    help='Require every release file to match, using the recorded PSPDEV toolchain')
args = parser.parse_args()
manifest = json.loads((root / 'CONTENT_MANIFEST.json').read_text())
expected = manifest['files']
data_files = {name: digest for name, digest in expected.items() if name != 'EBOOT.PBP'}
assert len(data_files) == manifest.get('install_files', 200) - 1

def asset_contents():
    with ExitStack() as stack:
        base = stack.enter_context(zipfile.ZipFile(args.install_zip))
        for name in data_files:
            assert Path(name).name == name
            key = 'PSP/GAME/BVN37/' + name
            if name == 'BVN_SYNC.IO' and key not in base.namelist() and manifest.get('default_reader') == 'stdio-only':
                yield name, b''
            else:
                yield name, base.read(key)

for name, content in asset_contents():
    assert hashlib.sha256(content).hexdigest() == data_files[name], 'Installation data mismatch: ' + name

sdk = Path(os.environ['PSPDEV']).expanduser().resolve()
cmake = os.environ.get('CMAKE', 'cmake')
env = {**os.environ, 'PSPDEV': str(sdk),
       'PATH': str(sdk / 'bin') + os.pathsep + os.environ['PATH'],
       'SOURCE_DATE_EPOCH': os.environ.get('SOURCE_DATE_EPOCH', '1793232000')}
subprocess.run([cmake, '-S', str(root), '-B', str(args.build_dir.resolve()),
                '-DCMAKE_TOOLCHAIN_FILE=' + str(sdk / 'psp/share/pspdev.cmake'),
                '-DCMAKE_BUILD_TYPE=Release',
                '-DBVN_RESOURCE_DIAGNOSTICS=' + ('ON' if manifest.get('resource_diagnostics', False) else 'OFF')], env=env, check=True)
subprocess.run([cmake, '--build', str(args.build_dir.resolve()), '--target', 'bvn_versus',
                '--parallel', os.environ.get('BUILD_JOBS', '4')], env=env, check=True)

args.output.mkdir(parents=True, exist_ok=True)
for name, content in asset_contents():
    (args.output / name).write_bytes(content)
shutil.copy2(args.build_dir / 'versus/EBOOT.PBP', args.output / 'EBOOT.PBP')
actual = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
          for p in args.output.iterdir() if p.is_file()}
assert {n: h for n, h in actual.items() if n != 'EBOOT.PBP'} == data_files
identical = actual == expected
if args.require_identical:
    assert identical, 'Use the recorded PSPDEV toolchain for byte-identical EBOOT.PBP'
print(f'Built {len(actual)} install files; release bytes identical:', identical, 'output:', args.output)
