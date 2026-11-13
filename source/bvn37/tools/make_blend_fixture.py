#!/usr/bin/env python3
"""Generate lossless gradient inputs for the actual effect renderer GPU test."""
import argparse,hashlib,json,struct,tempfile,zlib
from pathlib import Path
from PIL import Image
from pack_fighter import FighterTextures,EMPTY_BOX
from pack_actions import HEADER,CLIP,FRAME,TEXTURE,POSE,FINISH,compress_textures
from pack_stage import aligned
def build(output):
    with tempfile.TemporaryDirectory() as temporary:
        textures=FighterTextures(Path(temporary));frames=[];clips=[]
        for _ in range(9):
            clips.append((len(frames),1,0,1));frames.append((*textures.blank(),*EMPTY_BOX,0,0,0,0))
        clips.append((len(frames),256,FINISH,1))
        for alpha in range(256):
            raw=bytes(v for y in range(2) for x in range(256) for v in (x,x*73&255,255-x,alpha))
            image=Image.frombytes('RGBA',(256,2),raw)
            frames.append((0.,0.,*textures.store(image),*EMPTY_BOX,0,0,0,0))
        packed,tiles,entries,payload=compress_textures(textures,frames,lambda b:zlib.compress(b,1))
        assert not tiles
        start=aligned(52+24*18+len(clips)*CLIP.size+len(packed)*FRAME.size+len(entries)*TEXTURE.size)
        data=bytearray(HEADER.pack(b'BVNFGT12',len(packed),0,0,start,len(clips),0,1.,1.,1.))
        data.extend(struct.pack('<2H2I',0,0,len(entries),0));data.extend(struct.pack('<24H',*([FINISH]*24)));data.extend(bytes(24*16))
        for fmt,rows in [(CLIP,clips),(FRAME,packed),(TEXTURE,entries)]:data.extend(b''.join(fmt.pack(*row) for row in rows))
        data.extend(bytes(start-len(data)));data.extend(payload);output.write_bytes(data)
        output.with_suffix('.json').write_text(json.dumps({'alpha_values':256,'source_texels_per_frame':512,'format':'RGBA8888',
          'sha256':hashlib.sha256(data).hexdigest(),'scope':'Synthetic direct renderer blend inputs, not original Flash art'},indent=2)+'\n')
        print('Generated all 256 alpha values and full RGB gradients for direct renderer checks')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path);a=p.parse_args();build(a.output)
