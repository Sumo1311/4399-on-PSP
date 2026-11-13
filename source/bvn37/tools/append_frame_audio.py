#!/usr/bin/env python3
"""Append original footstep PCM while preserving all existing bindings and payloads."""
import argparse,hashlib,json,struct,tempfile
from pathlib import Path
from pack_versus_audio import from_audio,raw_pcm
HEADER=struct.Struct('<8s2I');RECORD=struct.Struct('<32s2I')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def build(reference,base,output):
 data=base.read_bytes();magic,count,start=HEADER.unpack_from(data);assert magic==b'BVNAUD01'
 rows=[RECORD.unpack_from(data,HEADER.size+i*RECORD.size) for i in range(count)]
 old=data[start:];payload=bytearray(old);clips={name.rstrip(b'\0').decode():(offset-start,samples) for name,offset,samples in rows};proof={}
 with tempfile.TemporaryDirectory() as temporary:
  scratch=Path(temporary)
  for key in ['fx-step1','fx-step2','fx-step3','fx-ghost-foot']:
   assert key not in clips
   path=reference/('flash-source/SRC_XFL/effect/bin/M 306 1496844065.dat' if key=='fx-ghost-foot' else 'tag-assets/shared/assets/assets/sounds/'+key.removeprefix('fx-')+'.mp3')
   pcm=raw_pcm(path,1,15968) if key=='fx-ghost-foot' else from_audio(path,scratch/(key+'.wav'))
   clips[key]=(len(payload),len(pcm)//2);payload.extend(pcm)
   proof[key]={'source':str(path.relative_to(reference)),'source_sha256':sha(path),'pcm_sha256':hashlib.sha256(pcm).hexdigest(),'sample_words':len(pcm)//2}
 assert len(clips)==count+4<=256
 offset=HEADER.size+len(clips)*RECORD.size
 table=b''.join(RECORD.pack(name.encode(),offset+relative,samples) for name,(relative,samples) in sorted(clips.items()))
 output.write_bytes(HEADER.pack(magic,len(clips),offset)+table+payload)
 final=output.read_bytes();assert final[offset:offset+len(old)]==old
 for name,old_offset,samples in rows:
  assert clips[name.rstrip(b'\0').decode()]==(old_offset-start,samples)
 report={'base_sha256':sha(base),'output_sha256':sha(output),'old_bindings_and_pcm_preserved':True,'old_bindings':count,'bindings':len(clips),'sources':proof}
 output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');print('Preserved old PCM; appended four source footstep bindings')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('reference',type=Path);p.add_argument('base',type=Path);p.add_argument('output',type=Path)
 a=p.parse_args();build(a.reference.resolve(),a.base,a.output)
