#!/usr/bin/env python3
"""Append every EffectModel source sound while preserving prior PCM and bindings."""
import argparse,hashlib,json,struct,tempfile,wave
from pathlib import Path
import xml.etree.ElementTree as ET
from pack_versus_audio import from_audio,HEADER,RECORD
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def build(reference,model,base,output,header):
    data=base.read_bytes();magic,count,start=HEADER.unpack_from(data);assert magic==b'BVNAUD01'
    records=[RECORD.unpack_from(data,HEADER.size+i*RECORD.size) for i in range(count)]
    old=data[start:];payload=bytearray(old)
    bindings={n.rstrip(b'\0').decode():(offset-start,samples) for n,offset,samples in records}
    document=reference/'flash-source/SRC_XFL/effect/DOMDocument.xml';root=ET.parse(document)
    sounds=sorted({e['sound'] for e in json.loads(model.read_text())['effects'] if e['sound']})
    proof={}
    with tempfile.TemporaryDirectory() as temp:
        scratch=Path(temp)
        for name in sounds:
            assert name not in bindings and len(name)<32
            item=next(e for e in root.findall('.//{*}DOMSoundItem') if e.get('linkageClassName')==name)
            source=document.parent/'bin'/item.get('soundDataHRef');raw=source.read_bytes();frames=int(item.get('sampleCount'))
            if item.get('exportNative')=='true':
                # XFL appends its low-rate preview after the native MP3 stream.
                size=int(item.get('dataLength'));assert raw[0]==255 and raw[1]&0xe0==0xe0
                encoded=raw[:size];path=scratch/'sound.mp3';path.write_bytes(encoded)
                pcm=from_audio(path,scratch/'converted.wav');mode='exportNative MP3 prefix of declared dataLength, decoded by afconvert'
            else:
                rate,bits,channels=item.get('format').split();rate={'11kHz':11025,'22kHz':22050,'44kHz':44100}[rate]
                width={'8bit':1,'16bit':2}[bits];channels={'Mono':1,'Stereo':2}[channels]
                assert len(raw)==frames*width*channels,(name,item.attrib,len(raw))
                path=scratch/'sound.wav'
                with wave.open(str(path),'wb') as stream:
                    stream.setnchannels(channels);stream.setsampwidth(width);stream.setframerate(rate);stream.writeframes(raw)
                pcm=from_audio(path,scratch/'converted.wav');encoded=raw;mode='declared raw PCM format normalized to 22050 Hz stereo signed 16 bit'
            assert len(pcm)%4==0 and pcm
            match=next(((off,samples) for off,samples in bindings.values() if samples*2==len(pcm) and payload[off:off+len(pcm)]==pcm),None)
            if match is None:match=(len(payload),len(pcm)//2);payload.extend(pcm)
            bindings[name]=match
            proof[name]={'source':str(source.relative_to(reference)),'source_sha256':sha(source),'source_format':item.get('format'),
                         'encoded_bytes':len(encoded),'encoded_sha256':hashlib.sha256(encoded).hexdigest(),'mode':mode,
                         'sample_words':len(pcm)//2,'pcm_sha256':hashlib.sha256(pcm).hexdigest()}
    assert len(bindings)==count+len(sounds)<=256
    offset=HEADER.size+len(bindings)*RECORD.size
    table=b''.join(RECORD.pack(name.encode(),offset+pos,samples) for name,(pos,samples) in sorted(bindings.items()))
    output.write_bytes(HEADER.pack(magic,len(bindings),offset)+table+payload)
    assert output.read_bytes()[offset:offset+len(old)]==old
    header.write_text('// Generated EffectModel sound names.\n// SPDX-License-Identifier: GPL-3.0-or-later\n#pragma once\n'
                      '#include <array>\n#include <string_view>\nnamespace bvn {\n'
                      f'inline constexpr std::array<std::string_view,{len(sounds)}> SourceEffectSoundNames{{{{\n'+
                      ',\n'.join('    '+json.dumps(name) for name in sounds)+'\n}};\n}\n')
    report={'old_bindings':count,'new_bindings':len(bindings),'appended_bindings':len(sounds),'old_bindings_and_pcm_preserved':True,
            'base_sha256':sha(base),'output_sha256':sha(output),'source_document_sha256':sha(document),'sounds':proof,
            'scope':'Pinned XFL exported native MP3 streams or declared PCM, afconvert normalization; '
                    'original Flash decoder delay and live mixed playback are separate checks'}
    output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'Appended {len(sounds)} source sound bindings; preserved {count} old bindings and PCM')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['reference','model','base','output','header']:p.add_argument(name,type=Path)
    a=p.parse_args();build(a.reference.resolve(),a.model,a.base,a.output,a.header)
