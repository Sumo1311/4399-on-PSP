"""Losslessly arrange the R3 bank in PSP16-byte by8-row texture cache blocks."""
from pathlib import Path
import argparse
import copy
import ctypes
import hashlib
import json
import shutil
import struct
import subprocess
import sys


def sha(data):
    return hashlib.sha256(data).hexdigest()


def swizzle(raw, row_bytes, height):
    output = bytearray()
    for block_y in range(0, height, 8):
        for block_x in range(0, row_bytes, 16):
            for y in range(block_y, block_y + 8):
                start = y * row_bytes + block_x
                output.extend(raw[start:start + 16])
    return bytes(output)


def restore_reference(blocked, row_bytes, height):
    """Independent row-major traversal of the hardware block-address formula."""
    output = bytearray(row_bytes * height)
    blocks_per_row = row_bytes // 16
    for y in range(height):
        for x in range(0, row_bytes, 16):
            address = ((y // 8) * blocks_per_row + x // 16) * 128 + (y % 8) * 16
            output[y * row_bytes + x:y * row_bytes + x + 16] = blocked[address:address + 16]
    return bytes(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--r3-media', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    previous = json.loads((args.r3_media / 'MEDIA_RECODE.json').read_text())
    bank = (args.r3_media / 'art.pak').read_bytes()
    assert sha(bank) == previous['art']['sha256']
    header = list(struct.unpack_from('<5I', bank))
    assert header[0] == 0x52414e44 and header[1] == 5
    args.output.mkdir(parents=True, exist_ok=False)
    library = args.output / ('liblz4.dylib' if sys.platform == 'darwin' else 'liblz4.so')
    source = Path(__file__).resolve().parents[1]
    subprocess.run(['clang', '-O2'] + (['-dynamiclib'] if sys.platform == 'darwin' else ['-shared', '-fPIC']) +
                   [str(source / 'vendor/lz4/lz4.c'), '-o', str(library)], check=True)
    lib = ctypes.CDLL(str(library.resolve()))
    for name in ['LZ4_compress_default', 'LZ4_decompress_safe']:
        fn = getattr(lib, name)
        fn.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
        fn.restype = ctypes.c_int
    lib.LZ4_compressBound.argtypes = [ctypes.c_int]
    lib.LZ4_compressBound.restype = ctypes.c_int
    entries = bank[20:20 + header[2] * 16]
    tiles, aliases, payloads, unique = [], [], [], {}
    counts = {'T4': 0, 'T8': 0, 'RGBA8888': 0}
    total_decoded = total_verified = 0
    for index in range(header[3]):
        tile = list(struct.unpack_from('<II4H4fI', bank, 20 + len(entries) + index * 36))
        width, height, fmt = tile[2], tile[3], tile[10]
        assert not fmt & 16 and fmt & 2
        pixels = width * height
        pixel_bytes = pixels // 2 if fmt & 8 else pixels if fmt & 1 else pixels * 4
        size = ((pixel_bytes + 63) & ~63) + (64 if fmt & 8 else 1024) if fmt & 1 else pixel_bytes
        decoded = ctypes.create_string_buffer(size)
        assert lib.LZ4_decompress_safe(bank[tile[0]:tile[0] + tile[1]], decoded, tile[1], size) == size
        original = decoded.raw
        raw = original
        row = pixel_bytes // height
        if row >= 16 and row % 16 == 0 and height % 8 == 0:
            raw = swizzle(original[:pixel_bytes], row, height) + original[pixel_bytes:]
            assert restore_reference(raw[:pixel_bytes], row, height) + raw[pixel_bytes:] == original
            fmt |= 16
            counts['T4' if fmt & 8 else 'T8' if fmt & 1 else 'RGBA8888'] += 1
        else:
            assert raw == original
        total_decoded += size
        total_verified += pixel_bytes
        key = (width, height, fmt & ~4, raw)
        if key not in unique:
            encoded = ctypes.create_string_buffer(lib.LZ4_compressBound(size))
            length = lib.LZ4_compress_default(raw, encoded, size, len(encoded))
            assert length > 0
            check = ctypes.create_string_buffer(size)
            assert lib.LZ4_decompress_safe(encoded, check, length, size) == size and check.raw == raw
            unique[key] = len(payloads)
            payloads.append(encoded.raw[:length])
        alias = unique[key]
        tile[0], tile[1], tile[10] = 0, len(payloads[alias]), fmt
        tiles.append(tile)
        aliases.append(alias)
    offset = 20 + len(entries) + len(tiles) * 36
    offsets = []
    for payload in payloads:
        offsets.append(offset)
        offset += len(payload)
    for tile, alias in zip(tiles, aliases):
        tile[0] = offsets[alias]
    header[1] = 6
    output = struct.pack('<5I', *header) + entries + b''.join(struct.pack('<II4H4fI', *t) for t in tiles) + b''.join(payloads)
    (args.output / 'art.pak').write_bytes(output)
    assert len(output) == offset
    audio = (args.r3_media / 'audio.pak').read_bytes()
    assert sha(audio) == previous['audio']['sha256']
    shutil.copyfile(args.r3_media / 'audio.pak', args.output / 'audio.pak')
    report = copy.deepcopy(previous)
    report['art'].update(sha256=sha(output), bank_after=len(output), format_version=6,
                         swizzle_input_sha256=sha(bank), swizzled_tiles=sum(counts.values()),
                         swizzled_formats=counts, decoded_after_total=total_decoded,
                         unique_payloads=len(payloads), verified_pixel_storage_bytes=total_verified)
    report['scope'] = 'R3 recode plus exact PSP texture-layout permutation. Palette, geometry, density and audio unchanged. Runtime checks separate.'
    (args.output / 'MEDIA_RECODE.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
