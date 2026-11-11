#!/usr/bin/env python3
"""Verify a downloaded release ZIP against the published manifest.

SPDX-License-Identifier: MIT
"""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct
import zipfile


def verify_pbp(data):
    if len(data) < 40 or data[:4] != b'\0PBP':
        raise ValueError('Invalid PBP header')
    offsets = list(struct.unpack_from('<8I', data, 8)) + [len(data)]
    if offsets[0] != 40 or offsets != sorted(offsets) or offsets[-2] > len(data):
        raise ValueError('Invalid PBP section offsets')
    sfo = data[offsets[0]:offsets[1]]
    if sfo[:4] != b'\0PSF':
        raise ValueError('Invalid PARAM.SFO')
    if data[offsets[6]:offsets[6] + 4] != b'\x7fELF':
        raise ValueError('Missing ELF runtime')
    for index, dimensions in [(1, (144, 80)), (4, (480, 272))]:
        png = data[offsets[index]:offsets[index + 1]]
        if not png.startswith(b'\x89PNG\r\n\x1a\n') or struct.unpack_from('>II', png, 16) != dimensions:
            raise ValueError('Missing or incorrectly sized XMB image')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('files', nargs='+', type=Path)
    parser.add_argument('--manifest', type=Path, default=Path(__file__).resolve().parents[1] / 'manifests/release-2026.11.11.json')
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    expected = {entry['name']: entry for entry in manifest['assets']}
    for path in args.files:
        info = expected.get(path.name)
        if info is None:
            raise SystemExit('Unknown asset: ' + path.name)
        with path.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        if path.stat().st_size != info['bytes'] or digest != info['sha256']:
            raise SystemExit('Asset hash or size mismatch: ' + path.name)
        with zipfile.ZipFile(path) as archive:
            if archive.testzip() is not None:
                raise SystemExit('ZIP CRC error: ' + path.name)
            names = archive.namelist()
            if len(names) != len(set(names)):
                raise SystemExit('Duplicate ZIP paths')
            if any(PurePosixPath(name).is_absolute() or '..' in PurePosixPath(name).parts or '\\' in name for name in names):
                raise SystemExit('Unsafe ZIP path')
            installed = []
            if info['kind'] == 'installation':
                for name in names:
                    if not name.startswith('PSP/GAME/'):
                        raise SystemExit('Unexpected installation path: ' + name)
                    if name.endswith('/EBOOT.PBP'):
                        parts = PurePosixPath(name).parts
                        if len(parts) != 4:
                            raise SystemExit('EBOOT.PBP is nested incorrectly: ' + name)
                        verify_pbp(archive.read(name))
                        installed.append(parts[2])
                if sorted(installed) != sorted(info['games']):
                    raise SystemExit('Installed game list differs from manifest')
                for game_id in installed:
                    expected_files = manifest['games'][game_id]['files']
                    prefix = 'PSP/GAME/' + game_id + '/'
                    actual_files = {name[len(prefix):] for name in names if name.startswith(prefix)}
                    if actual_files != set(expected_files):
                        raise SystemExit('Game file list differs: ' + game_id)
                    for relative, record in expected_files.items():
                        data = archive.read(prefix + relative)
                        if len(data) != record['bytes'] or hashlib.sha256(data).hexdigest() != record['sha256']:
                            raise SystemExit('Game file hash mismatch: ' + game_id + '/' + relative)
        print('PASS ' + path.name)


if __name__ == '__main__':
    main()
