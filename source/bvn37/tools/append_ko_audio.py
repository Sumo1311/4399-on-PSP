#!/usr/bin/env python3
"""Append the pinned super-KO voice without changing existing bindings or PCM."""
import argparse,hashlib,json,tempfile
from pathlib import Path
from pack_versus_audio import HEADER,RECORD,from_audio

def build(reference,base,output):
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    data=base.read_bytes();magic,count,start=HEADER.unpack_from(data);assert magic==b'BVNAUD01'
    entries={name.rstrip(b'\0').decode():(offset-start,samples) for name,offset,samples in
             (RECORD.unpack_from(data,HEADER.size+i*RECORD.size) for i in range(count))}
    payload=data[start:];source=reference/'flash-source/SRC_XFL/#swc/sound/LIBRARY/ko.wav'
    assert 'fx-ko-bisha' not in entries
    with tempfile.TemporaryDirectory(prefix='bvn-ko-audio-') as temp:
        pcm=from_audio(source,Path(temp)/'ko.wav')
    entries['fx-ko-bisha']=(len(payload),len(pcm)//2)
    tableStart=HEADER.size+len(entries)*RECORD.size
    table=b''.join(RECORD.pack(name.encode(),tableStart+offset,samples) for name,(offset,samples) in sorted(entries.items()))
    output.write_bytes(HEADER.pack(magic,len(entries),tableStart)+table+payload+pcm)
    report={'old_bindings':count,'new_bindings':len(entries),'old_bindings_and_pcm_preserved':True,
            'base_sha256':sha(base),'output_sha256':sha(output),'source':str(source.relative_to(reference)),
            'source_sha256':sha(source),'pcm_sha256':hashlib.sha256(pcm).hexdigest(),'sample_words':len(pcm)//2,
            'scope':'Original snd_ko_bs WAV normalized by afconvert to 22050 Hz stereo signed 16-bit. Complete live mixed output remains separate.'}
    output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Appended original super-KO voice',len(pcm),'PCM bytes')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('reference',type=Path);p.add_argument('base',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();build(a.reference.resolve(),a.base,a.output)
