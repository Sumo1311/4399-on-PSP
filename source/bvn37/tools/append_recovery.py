#!/usr/bin/env python3
"""Append an original recovery clip while preserving every existing combat/texture record."""
from dataclasses import dataclass
from pathlib import Path
import hashlib
import json
import struct
import zlib

from pack_actions import (HEADER, CLIP, FRAME, HIT, STRIKE, EVENT, PROJECTILE, ATTACKER, OPS,
                          TEXTURE, POSE, FINISH, lz4_compressor)
from pack_stage import aligned

TABLES = (('clips', CLIP), ('frames', FRAME), ('hits', HIT), ('strikes', STRIKE),
          ('events', EVENT), ('projectiles', PROJECTILE), ('attackers', ATTACKER),
          ('textures', TEXTURE), ('tiles', POSE))


@dataclass
class Bank:
    version: int
    stats: tuple
    bindings: list
    ranges: list
    escape: int
    tables: dict
    payload: bytes

    @property
    def lz4(self):
        return self.version in (13, 15)

    @classmethod
    def read(cls, path):
        data = Path(path).read_bytes()
        magic, nf, nh, ne, pixels, nc, ns, *stats = HEADER.unpack_from(data)
        assert magic[:6] == b'BVNFGT'
        version = int(magic[6:]); assert 11 <= version <= 15
        np, na, nt, ntiles = struct.unpack_from('<2H2I', data, 40)
        at = 52
        escape = FINISH
        if version >= 14: escape, = struct.unpack_from('<H', data, at); at += 2
        count = 24 if version >= 12 else 22
        bindings = list(struct.unpack_from(f'<{count}H', data, at)); at += count * 2
        ranges = [struct.unpack_from('<4f', data, at + 16 * i) for i in range(count)]; at += count * 16
        bindings.extend([FINISH] * (24 - count)); ranges.extend([(0,0,0,0)] * (24 - count))
        counts = (nc,nf,nh,ns,ne,np,na,nt,ntiles)
        tables = {}
        for (name, fmt), n in zip(TABLES, counts):
            tables[name] = [fmt.unpack_from(data, at + fmt.size * i) for i in range(n)]
            at += fmt.size * n
        assert aligned(at) == pixels
        return cls(version, tuple(stats), bindings, ranges, escape, tables, data[pixels:])

    def write(self, path):
        t = self.tables
        start = aligned(54 + 24 * 18 + sum(fmt.size * len(t[name]) for name, fmt in TABLES))
        assert self.escape != FINISH and len(self.payload) <= 32 * 1024 * 1024
        with Path(path).open('wb') as out:
            magic = b'BVNFGT15' if self.lz4 else b'BVNFGT14'
            out.write(HEADER.pack(magic, len(t['frames']), len(t['hits']), len(t['events']), start,
                                  len(t['clips']), len(t['strikes']), *self.stats))
            out.write(struct.pack('<2H2IH', len(t['projectiles']),len(t['attackers']),len(t['textures']),len(t['tiles']),self.escape))
            out.write(struct.pack('<24H', *self.bindings))
            for row in self.ranges: out.write(struct.pack('<4f', *row))
            for name, fmt in TABLES:
                for row in t[name]: out.write(fmt.pack(*row))
            out.write(bytes(start - out.tell())); out.write(self.payload)


def rows_digest(rows, fmt):
    return hashlib.sha256(b''.join(fmt.pack(*row) for row in rows)).hexdigest()


def append_recovery(base_path, addition_path, output, original_report):
    base, extra = Bank.read(base_path), Bank.read(addition_path)
    t, e = base.tables, extra.tables
    assert base.escape == FINISH and extra.escape == len(t['clips'])
    assert len(e['clips']) >= len(t['clips']) + 1 and base.stats == extra.stats
    assert all(row[1] == 0 for row in e['clips'][:extra.escape])
    assert e['clips'][extra.escape][0] == 0 and e['clips'][extra.escape][1] <= len(e['frames'])
    report = {'base_sha256': hashlib.sha256(Path(base_path).read_bytes()).hexdigest(),
              'addition_sha256': hashlib.sha256(Path(addition_path).read_bytes()).hexdigest(),
              'old_payload_sha256': hashlib.sha256(base.payload).hexdigest(),
              'old_tables': {name: rows_digest(t[name], fmt) for name, fmt in TABLES}}
    counts = {name: len(rows) for name, rows in t.items()}
    metadata = json.loads(Path(original_report).read_text())
    addition_meta = json.loads(Path(addition_path).with_suffix('.json').read_text())
    strike_map = {}
    for name, index in addition_meta['strikes'].items():
        if name not in metadata['strikes']:
            metadata['strikes'][name] = len(t['strikes'])
            t['strikes'].append(e['strikes'][index])
        strike_map[index] = metadata['strikes'][name]
    payload = bytearray(base.payload)
    compressor = lz4_compressor() if base.lz4 else lambda raw: zlib.compress(raw, level=1)
    assert not extra.lz4, 'Unexpected large recovery-only texture stream'
    for offset, packed, raw_size in e['textures']:
        raw = zlib.decompress(extra.payload[offset:offset+packed]); assert len(raw) == raw_size
        data = compressor(raw)
        t['textures'].append((len(payload),len(data),raw_size)); payload.extend(data)
    for row in e['tiles']:
        pose = list(row); pose[2] += counts['textures']; t['tiles'].append(tuple(pose))
    for row in e['frames']:
        frame = list(row)
        frame[2] += counts['textures'] if frame[5] else counts['tiles']
        frame[11] += counts['hits']; frame[12] += counts['events']
        t['frames'].append(tuple(frame))
    for row in e['hits']:
        hit = list(row)
        if hit[5] != 2: hit[4] = strike_map[hit[4]]
        t['hits'].append(tuple(hit))
    for row in e['events']:
        event = list(row); op=OPS[event[0]]
        if op=='summon':event[2]+=counts['attackers']
        elif op=='fire':event[2]+=counts['projectiles']
        elif op in ('velocity','damping','swapForm','goto'):pass  # Original clip/form IDs were retained above.
        else:raise ValueError(f'Unreviewed recovery event remapping: {op}')
        t['events'].append(tuple(event))
    for row in e['clips'][extra.escape:]:
        clip = list(row); clip[0] += counts['frames']; t['clips'].append(tuple(clip))
    t['attackers'].extend(e['attackers'])
    for row in e['projectiles']:
        projectile=list(row);projectile[17]=strike_map[projectile[17]];t['projectiles'].append(tuple(projectile))
    base.escape = counts['clips']; base.payload = bytes(payload)
    base.version = 15 if base.lz4 else 14
    base.write(output)
    merged = Bank.read(output)
    for name, fmt in TABLES:
        assert rows_digest(merged.tables[name][:counts[name]],fmt) == report['old_tables'][name], name
    assert merged.payload[:len(Bank.read(base_path).payload)] == Bank.read(base_path).payload
    metadata['recovery_clip'] = {'index':base.escape, **addition_meta['clips'][-1]}
    Path(output).with_suffix('.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2)+'\n')
    report.update(output_sha256=hashlib.sha256(Path(output).read_bytes()).hexdigest(),
                  recovery=metadata['recovery_clip'], frames_added=len(e['frames']), events_added=len(e['events']),
                  recovery_frames=e['clips'][extra.escape][1], attackers_added=len(e['attackers']),
                  projectiles_added=len(e['projectiles']),
                  textures_added=len(e['textures']), old_tables_preserved=True, old_payload_preserved=True,
                  deferred_bindings=addition_meta['deferred_bindings'], deferred_effects=addition_meta['deferred_effects'])
    return report
