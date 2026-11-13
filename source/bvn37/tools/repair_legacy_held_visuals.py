#!/usr/bin/env python3
"""Replace Deidara old's held visual poses without recompiling combat events."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib
from pack_fighter import FighterTextures, read_timeline

def digest(data):
    return hashlib.sha256(data).hexdigest()

def fnv(data):
    value = 14695981039346656037
    for b in data:
        value = ((value ^ b) * 1099511628211) & ((1 << 64) - 1)
    return value

def bind_geometry(header, before, after):
    text = header.read_text()
    match = re.search(r'inline constexpr std::array<ObjectAreaBank,(\d+)> ObjectAreaBanks\{\{\n(.*?)\n\}\};', text, re.S)
    assert match, 'Missing object-area signature table'
    rows = {int(key, 16): (int(first), int(count)) for key, first, count in
            re.findall(r'\{0x([0-9a-f]+)ull,(\d+),(\d+)\}', match[2])}
    assert before in rows and len(rows) == int(match[1])
    if after in rows:
        assert rows[after] == rows[before]
    original_rows = dict(rows)
    rows[after] = rows[before]
    replacement = 'inline constexpr std::array<ObjectAreaBank,' + str(len(rows)) + '> ObjectAreaBanks{{\n'
    replacement += '\n'.join(f'    {{0x{key:016x}ull,{value[0]},{value[1]}}},' for key, value in sorted(rows.items()))
    updated = text[:match.start()] + replacement + '\n}};' + text[match.end():]
    assert updated.endswith(text[match.end():])
    assert all(rows[key] == value for key, value in original_rows.items())
    header.write_text(updated)
    return {'old_signatures_retained': len(rows) - 1, 'new_signature_geometry_identical': True,
            'geometry_first': rows[after][0], 'geometry_count': rows[after][1]}

def repair(original, exported, output, object_area_header=None):
    assert original.resolve() != output.resolve(), 'Keep the original bank intact'
    source = original.read_bytes()
    assert digest(source) == 'c7ebb810d45476d27bdd803fe768cc9d5aa01404105a40deb9aa5916dd80d5c8', 'Expected the verified 1.0.0 Deidara old bank'
    magic, frame_count, hits, events, pixel_start, clip_count, strikes, *_ = struct.unpack_from('<8s4I2H3f', source)
    assert magic == b'BVNFGT14', 'Expected the verified zlib Deidara legacy bank'
    projectiles, attackers, texture_count, tile_count, _ = struct.unpack_from('<HHIIH', source, 40)
    assert (frame_count, clip_count, texture_count, tile_count) == (943, 42, 387, 0)
    clip_start = 54 + 24 * 2 + 24 * 16
    frame_start = clip_start + clip_count * 8
    texture_start = frame_start + frame_count * 48 + hits * 20 + strikes * 24 + events * 12 + projectiles * 92 + attackers * 24
    tile_start = texture_start + texture_count * 12
    assert pixel_start == ((tile_start + tile_count * 20 + 15) & ~15)
    body = bytearray(source[:texture_start])
    textures = bytearray(source[texture_start:tile_start])
    tiles = source[tile_start:tile_start + tile_count * 20]
    payload = bytearray(source[pixel_start:])
    # Compressed texture records are contiguous; only metadata is aligned.
    timeline = read_timeline(exported)
    baker = FighterTextures(exported)
    changes, new_textures = [], {}
    for clip, parent in [(0, 1), (2, 45)]:
        first, count, repeat, authored = struct.unpack_from('<4H', source, clip_start + clip * 8)
        assert authored == parent and count == 12 and repeat == 0
        assert timeline.cycles[parent] == (12, 0)
        for phase in range(count):
            visual = baker.bake(f'{parent}-{phase}', timeline.children[parent, phase], allow_tiles=False)
            x, y, raw_offset, width, height, stride, rows = visual
            assert width > 0 and height > 0
            raw = bytes(baker.pixels[raw_offset:raw_offset + stride * rows * 4])
            assert any(raw[3::4]), f'Unexpected transparent source pose {parent}-{phase}'
            key = (stride, rows, raw)
            if key not in new_textures:
                packed = zlib.compress(raw, level=6)
                index = texture_count + len(new_textures)
                textures.extend(struct.pack('<3I', len(payload), len(packed), len(raw)))
                payload.extend(packed)
                new_textures[key] = index
            pose = struct.pack('<ffI4H', x, y, new_textures[key], width, height, stride, rows)
            at = frame_start + (first + phase) * 48
            body[at:at + 20] = pose
            changes.append({'clip': clip, 'parent_frame': parent, 'phase': phase, 'frame': first + phase,
                            'offset': at, 'pose_sha256': digest(pose)})
    struct.pack_into('<I', body, 44, texture_count + len(new_textures))
    new_start = (len(body) + len(textures) + len(tiles) + 15) & ~15
    struct.pack_into('<I', body, 20, new_start)
    metadata = body + textures + tiles
    metadata.extend(bytes(new_start - len(metadata)))
    result = bytes(metadata + payload)
    allowed = set(range(20, 24)) | set(range(44, 48))
    for row in changes:
        allowed.update(range(row['offset'], row['offset'] + 20))
    assert all(a == b or i in allowed for i, (a, b) in enumerate(zip(source[:texture_start], body)))
    assert bytes(textures[:texture_count * 12]) == source[texture_start:tile_start]
    assert bytes(payload[:len(source) - pixel_start]) == source[pixel_start:]
    output.write_bytes(result)
    proof = {'before_sha256': digest(source), 'after_sha256': digest(result),
             'before_metadata_identity': f'{fnv(source[:pixel_start]):016x}',
             'after_metadata_identity': f'{fnv(result[:new_start]):016x}',
             'replaced_poses': changes, 'new_textures': len(new_textures),
             'all_original_texture_records_and_compressed_bytes_retained': True,
             'all_combat_events_boxes_commands_and_clip_timings_identical': True}
    if object_area_header:
        proof['object_area_binding'] = bind_geometry(object_area_header, fnv(source[:pixel_start]), fnv(result[:new_start]))
    output.with_suffix('.repair.json').write_text(json.dumps(proof, indent=2) + '\n')
    print('Replaced', len(changes), 'source-derived held poses; combat metadata unchanged')

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('original', type=Path)
    p.add_argument('exported', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--object-area-header', type=Path, help='Add the new visual identity to the unchanged geometry binding')
    a = p.parse_args()
    repair(a.original, a.exported, a.output, a.object_area_header)
