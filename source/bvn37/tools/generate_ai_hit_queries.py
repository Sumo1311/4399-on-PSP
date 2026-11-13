#!/usr/bin/env python3
"""Preserve FighterHitModel.getHitVOLike's substring queries after ID hashing."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


QUERIES = {1: 'kj1', 2: 'kj2', 7: 'zh1', 8: 'zh2', 9: 'zh3', 13: 'tz',
           14: 'bs', 15: 'sbs', 16: 'cbs', 21: 'kbs'}


def key(name):
    value = 2166136261
    for byte in name.encode('utf-8'):
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def signature(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value


def strike_records(path):
    with path.open('rb') as stream:
        raw_header = stream.read(40)
        magic, frames, hits, events, pixels, clips, strikes, *stats = struct.unpack('<8s4I2H3f', raw_header)
        version = int(magic[6:])
        if magic[:6] != b'BVNFGT' or version < 9:
            raise ValueError(f'Expected named strikes in {path}')
        commands = 24 if version >= 12 else 22 if version >= 10 else 18
        header = 44 + (8 if version >= 10 else 0) + (2 if version >= 14 else 0)
        offset = header + commands * 18 + clips * 8 + frames * 48 + hits * 20
        stream.seek(offset)
        records = stream.read(24 * strikes)
        return [struct.unpack_from('<2f6HI', records, n * 24) for n in range(strikes)], signature(raw_header[28:40] + records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--reports', type=Path, required=True, help='Bank names to candidate pack report paths')
    parser.add_argument('--raw-hits', type=Path, required=True, help='Normal fighter banks to complete source hit definitions')
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    args = parser.parse_args()
    reports = json.loads(args.reports.read_text())
    raw_hits = json.loads(args.raw_hits.read_text())
    names, evidence, extras = {}, [], {}
    for bank in sorted(args.package.glob('*.BVF')):
        records, sig = strike_records(bank)
        actual = [row[-1] for row in records]
        candidates = reports[bank.stem]
        selected = None
        for path in candidates:
            path = Path(path)
            meta = json.loads(path.read_text())
            entries = sorted(meta['strikes'].items(), key=lambda row: row[1])
            if [key(name) for name, index in entries] == actual:
                selected = (path, entries)
                break
        if selected is None:
            raise ValueError(f'No report reproduces every strike ID in {bank}')
        path, entries = selected
        extra_break = extra_down = 0
        raw_path = None
        unused = {}
        if bank.stem in raw_hits:
            for candidate in raw_hits[bank.stem]:
                source_hits = json.loads(Path(candidate).read_text())
                if all(name in source_hits and
                       (int(source_hits[name]['isBreakDef']) * 2 + int(source_hits[name]['hurtType'])) == (records[index][5] & 3)
                       for name, index in entries):
                    raw_path = Path(candidate)
                    unused = {name: hit for name, hit in source_hits.items() if name not in meta['strikes']}
                    for name, hit in unused.items():
                        mask = sum(1 << command for command, sub in QUERIES.items() if sub in name)
                        if hit['isBreakDef']: extra_break |= mask
                        if hit['hurtType'] == 1: extra_down |= mask
                    break
            if raw_path is None:
                raise ValueError(f'No complete raw hit definitions agree with {bank}')
        previous = extras.setdefault(sig, (extra_break, extra_down, bank.name))
        if previous[:2] != (extra_break, extra_down):
            raise ValueError(f'Asset signature collision with different source queries: {previous[2]} / {bank}')
        for name, _ in entries:
            hashed = key(name)
            if hashed in names and names[hashed] != name:
                raise ValueError(f'Cross-bank strike ID collision: {name}')
            names[hashed] = name
        evidence.append({'bank': bank.name, 'strikes': len(actual), 'report': str(path),
                         'report_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                         'strike_keys_sha256': hashlib.sha256(struct.pack(f'<{len(actual)}I', *actual)).hexdigest(),
                         'raw_hits': str(raw_path) if raw_path else None,
                         'raw_hits_sha256': hashlib.sha256(raw_path.read_bytes()).hexdigest() if raw_path else None,
                         'unused_hit_names': list(unused), 'extra_break': extra_break, 'extra_down': extra_down,
                         'signature': f'{sig:016x}'})
    rows = []
    for hashed, name in sorted(names.items()):
        mask = sum(1 << command for command, substring in QUERIES.items() if substring in name)
        rows.append(f'    {{0x{hashed:08x}U, 0x{mask:08x}U}}, // {name}')
    text = ('// Generated from named original strikes; see generate_ai_hit_queries.py.\n'
            '// SPDX-License-Identifier: GPL-3.0-or-later\n'
            'struct AiHitQuery { std::uint32_t key, commands; };\n'
            'inline constexpr AiHitQuery AiHitQueries[]{\n' + '\n'.join(rows) + '\n};\n')
    rows = [f'    {{0x{sig:016x}ULL, 0x{guard:08x}U, 0x{down:08x}U}}, // {bank}'
            for sig, (guard, down, bank) in sorted(extras.items())]
    text += ('struct AiUnusedHits { std::uint64_t signature; std::uint32_t breaksGuard, knocksDown; };\n'
             'inline constexpr AiUnusedHits AiUnusedHitQueries[]{\n' + '\n'.join(rows) + '\n};\n')
    source = args.reference / 'upstream/CORE_KernelLogic/src/net/play5d/game/bvn/fighter/models/FighterHitModel.as'
    source_text = source.read_text()
    if 'i.indexOf(likeId) != -1' not in source_text:
        raise ValueError('The original hit-ID query semantics changed')
    args.output.write_text(text)
    args.manifest.write_text(json.dumps({'banks': evidence, 'unique_hit_names': len(names), 'queries': QUERIES,
        'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'header_sha256': hashlib.sha256(text.encode()).hexdigest(), 'semantics': 'substring, including bs in sbs/cbs/kbs'}, indent=2) + '\n')
    print(f'Verified {len(evidence)} banks and {len(names)} original hit IDs')


if __name__ == '__main__':
    main()
