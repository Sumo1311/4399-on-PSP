#!/usr/bin/env python3
"""Append source counter input art/audio while preserving accepted UI and PCM payloads."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import wave
import xml.etree.ElementTree as ET
from append_training_highlight import read as ui_read, HEADER as UI_HEADER, RECORD as UI_RECORD
from pack_versus_ui import pack_images, render_label, training_highlight
from pack_versus_audio import raw_pcm, from_audio

AUD_HEADER = struct.Struct('<8s2I')
AUD_RECORD = struct.Struct('<32s2I')


def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()


def append_ui(base, output, font, scratch):
    records, payload = ui_read(base)
    original = next(row for row in records if row[0].rstrip(b'\0') == b'log-assist')
    image = render_label('△', font, 29)
    normal, high = scratch / 'normal.bvu', scratch / 'high.bvu'
    pack_images({'log-assist': image}, normal)
    pack_images({'log-highlight-assist': training_highlight(image)}, high)
    nr, np = ui_read(normal); hr, hp = ui_read(high)
    assert original[2:] == nr[0][2:]
    assert payload[original[1]:original[1] + original[4] * original[5] * 2] == np
    record = (hr[0][0], len(payload), *hr[0][2:])
    assert not any(row[0] == record[0] for row in records)
    rows = sorted([*records, record]); table = b''.join(UI_RECORD.pack(*row) for row in rows)
    start = (UI_HEADER.size + len(table) + 15) // 16 * 16
    output.write_bytes(UI_HEADER.pack(b'BVNUI001', len(rows), start) + table +
                       bytes(start - UI_HEADER.size - len(table)) + payload + hp)
    old, new = ui_read(output)
    assert [row for row in old if row[0] != record[0]] == records and new[:len(payload)] == payload
    return dict(base_sha256=digest(base), output_sha256=digest(output), old_records_and_payload_preserved=True,
                highlight='TrainingInputHistoryUI.HIGHLIGHT_CT RGB offsets 50,-30,-30')


def append_audio(reference, base, output, scratch):
    raw = base.read_bytes(); magic, count, start = AUD_HEADER.unpack_from(raw)
    assert magic == b'BVNAUD01'
    records = [AUD_RECORD.unpack_from(raw, AUD_HEADER.size + i * AUD_RECORD.size) for i in range(count)]
    payload = bytearray(raw[start:]); old_payload = bytes(payload)
    bindings = {name.rstrip(b'\0').decode(): (offset-start, samples) for name, offset, samples in records}
    effect = reference / 'flash-source/SRC_XFL/effect'
    media = ET.parse(effect / 'DOMDocument.xml')
    sources = {}
    for name, cls in [('fx-counter-replace','snd_fz'),('fx-counter-charge','snd_baoqi1'),('fx-counter-burst','snd_baoqi')]:
        item = next(item for item in media.findall('.//{*}DOMSoundItem') if item.get('linkageClassName') == cls)
        source = effect / 'bin' / item.get('soundDataHRef')
        frames = int(item.get('sampleCount'))
        if item.get('format') == '44kHz 16bit Mono':
            data = source.read_bytes(); assert len(data) == frames * 2
            wav = scratch / 'charge.wav'
            with wave.open(str(wav), 'wb') as stream:
                stream.setnchannels(1);stream.setsampwidth(2);stream.setframerate(44100);stream.writeframes(data)
            pcm = from_audio(wav, scratch / 'converted.wav')
        else:
            assert item.get('format') == '22kHz 16bit Mono'
            pcm = raw_pcm(source, 1, frames)
        if name == 'fx-counter-replace':
            offset, samples = bindings['fx-assist-naruto']
            assert payload[offset:offset + samples * 2] == pcm
            bindings[name] = offset, samples
        else:
            bindings[name] = len(payload), len(pcm) // 2
            payload.extend(pcm)
        sources[name] = dict(source=str(source.relative_to(reference)), source_sha256=digest(source),
                             pcm_sha256=hashlib.sha256(pcm).hexdigest(), output_sample_words=len(pcm)//2)
    assert len(bindings) == count + 3 <= 128
    pixel_start = AUD_HEADER.size + len(bindings) * AUD_RECORD.size
    table = b''.join(AUD_RECORD.pack(name.encode(), pixel_start+offset, samples) for name,(offset,samples) in sorted(bindings.items()))
    output.write_bytes(AUD_HEADER.pack(magic,len(bindings),pixel_start)+table+payload)
    assert payload[:len(old_payload)] == old_payload
    return dict(base_sha256=digest(base), output_sha256=digest(output), old_pcm_preserved=True,
                old_bindings=count,new_bindings=len(bindings),sources=sources)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference',type=Path);parser.add_argument('base',type=Path);parser.add_argument('output',type=Path)
    parser.add_argument('--font',required=True)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.output) as directory:
        scratch=Path(directory)
        ui=append_ui(args.base/'UI.BVU',args.output/'UI.BVU',args.font,scratch)
        audio=append_audio(args.reference,args.base/'AUDIO.BVA',args.output/'AUDIO.BVA',scratch)
    report=dict(ui=ui,audio=audio)
    (args.output/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'Preserved old UI/PCM; appended highlighted counter input and {audio["new_bindings"]-audio["old_bindings"]} audio bindings')


if __name__=='__main__':main()
