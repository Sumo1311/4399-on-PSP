#!/usr/bin/env python3
"""Relink release object files against a compatible or modified PSP runtime.

SPDX-License-Identifier: MIT
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('materials', type=Path)
    parser.add_argument('game')
    parser.add_argument('--sdk', required=True, type=Path)
    parser.add_argument('--pthread-lib-dir', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--check-identical', action='store_true')
    args = parser.parse_args()
    root = args.materials.resolve()
    game = root / args.game
    if game.parent != root or not game.is_dir():
        parser.error('Select a game directory in the relinking materials.')
    sdk = args.sdk.resolve()
    config = json.loads((game / 'link.json').read_text())
    if args.output.exists():
        parser.error('The output already exists; choose a new file.')
    if args.pthread_lib_dir and not (args.pthread_lib_dir / 'libpthread.a').is_file():
        parser.error('--pthread-lib-dir must contain libpthread.a')
    for item in config['inputs']:
        path = (game / item['path']).resolve()
        if not path.is_relative_to(game) or hashlib.sha256(path.read_bytes()).hexdigest() != item['sha256']:
            raise SystemExit('Object integrity check failed: ' + item['path'])
    template = (game / 'eboot-template.pbp').read_bytes()
    if hashlib.sha256(template).hexdigest() != config['template_sha256']:
        raise SystemExit('EBOOT template integrity check failed')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='psp-relink-', dir=args.output.parent) as tmp:
        elf = Path(tmp) / config['target']
        command = [str(sdk / 'bin/psp-g++')]
        if args.pthread_lib_dir:
            command.append('-L' + str(args.pthread_lib_dir.resolve()))
        for item in config['args']:
            item = item.replace('{PSPDEV}', str(sdk))
            if item == '{OUTPUT}':
                item = str(elf)
            command.append(item)
        subprocess.run(command, cwd=game, check=True)
        subprocess.run([str(sdk / 'bin/psp-strip'), str(elf)], check=True)
        subprocess.run([str(sdk / 'bin/psp-fixup-imports'), str(elf)], check=True)
        runtime = elf.read_bytes()
        digest = hashlib.sha256(runtime).hexdigest()
        identical = digest == config['runtime_sha256']
        if args.check_identical and not identical:
            raise SystemExit('Relinked DATA.PSP differs from the release: ' + digest)
        offsets = list(struct.unpack_from('<8I', template, 8)) + [len(template)]
        sections = [template[offsets[i]:offsets[i + 1]] for i in range(8)]
        sections[6] = runtime
        new_offsets = []
        size = 40
        for section in sections:
            new_offsets.append(size)
            size += len(section)
        result = template[:8] + struct.pack('<8I', *new_offsets) + b''.join(sections)
        args.output.write_bytes(result)
        print(json.dumps({'game': args.game, 'output': str(args.output),
                          'runtime_sha256': digest, 'matches_release': identical}))


if __name__ == '__main__':
    main()
