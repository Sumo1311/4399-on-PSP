#!/usr/bin/env python3
"""Append the source-defined recovery input highlight without regenerating accepted UI art."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import tempfile
from pack_versus_ui import pack_images, render_label, training_highlight

HEADER = struct.Struct('<8s2I')
RECORD = struct.Struct('<32sI4H')


def read(path):
    data = Path(path).read_bytes()
    magic, count, at = HEADER.unpack_from(data)
    assert magic == b'BVNUI001'
    records = [RECORD.unpack_from(data, HEADER.size + RECORD.size * n) for n in range(count)]
    return records, data[at:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--font', required=True)
    args = parser.parse_args()
    records, payload = read(args.base)
    original = next(row for row in records if row[0].rstrip(b'\0') == b'log-6')
    image = render_label('L', args.font, 29)
    highlighted = training_highlight(image)
    with tempfile.TemporaryDirectory(dir=args.output.parent) as directory:
        normal, high = Path(directory) / 'normal.bvu', Path(directory) / 'high.bvu'
        pack_images({'log-6': image}, normal)
        pack_images({'log-highlight-6': highlighted}, high)
        nr, np = read(normal)
        hr, hp = read(high)
    assert original[2:] == nr[0][2:]
    length = original[4] * original[5] * 2
    assert payload[original[1]:original[1] + length] == np, 'Font rendering differs from accepted glyph'
    record = (hr[0][0], len(payload), *hr[0][2:])
    assert not any(row[0] == record[0] for row in records)
    rows = sorted([*records, record])
    start = (HEADER.size + RECORD.size * len(rows) + 15) // 16 * 16
    tables = b''.join(RECORD.pack(*row) for row in rows)
    args.output.write_bytes(HEADER.pack(b'BVNUI001', len(rows), start) + tables +
                            bytes(start - HEADER.size - len(tables)) + payload + hp)
    actual, pixels = read(args.output)
    assert [row for row in actual if row[0] != record[0]] == records
    assert pixels[:len(payload)] == payload
    report = dict(base_sha256=hashlib.sha256(args.base.read_bytes()).hexdigest(),
                  output_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(),
                  old_records_and_payload_preserved=True, source_rgb_offsets=[50, -30, -30],
                  new_key='log-highlight-6', unchanged_glyph_verified=True)
    args.output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    highlighted.save(args.output.with_suffix('.png'))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
