#!/usr/bin/env python3
"""Rebuild BVN37 1.0.0 hwd1 using the data from its installation ZIP."""
import argparse
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
                    help='BVN37_PSP_1.0.0_Install.zip from the bvn37-v1.0.0 Release')
parser.add_argument('--build-dir', type=Path, default=root / 'build/psp')
parser.add_argument('--output', type=Path, default=root / 'build/install/PSP/GAME/BVN37')
parser.add_argument('--require-identical', action='store_true',
                    help='Require every release file to match, using the recorded PSPDEV toolchain')
args = parser.parse_args()
manifest = json.loads((root / 'CONTENT_MANIFEST.json').read_text())
expected = manifest['files']
data_files = {name: digest for name, digest in expected.items() if name != 'EBOOT.PBP'}
assert len(data_files) == 199

with zipfile.ZipFile(args.install_zip) as archive:
    for name, digest in data_files.items():
        content = archive.read('PSP/GAME/BVN37/' + name)
        assert hashlib.sha256(content).hexdigest() == digest, 'Installation data mismatch: ' + name

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
with zipfile.ZipFile(args.install_zip) as archive:
    for name in data_files:
        assert Path(name).name == name
        (args.output / name).write_bytes(archive.read('PSP/GAME/BVN37/' + name))
shutil.copy2(args.build_dir / 'versus/EBOOT.PBP', args.output / 'EBOOT.PBP')
actual = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
          for p in args.output.iterdir() if p.is_file()}
assert {n: h for n, h in actual.items() if n != 'EBOOT.PBP'} == data_files
identical = actual == expected
if args.require_identical:
    assert identical, 'Use the recorded PSPDEV toolchain for byte-identical EBOOT.PBP'
print('Built 200 install files; release bytes identical:', identical, 'output:', args.output)
