#!/usr/bin/env python3
"""Bake source half-resolution map snapshots for every blur requested by this game."""
import argparse,hashlib,json,math,struct,zlib
from pathlib import Path
import numpy as np
from PIL import Image,__version__ as pillow_version
from pack_fighter import FighterTextures,EMPTY_BOX
from pack_actions import HEADER,CLIP,FRAME,TEXTURE,POSE,FINISH,compress_textures
from pack_stage import aligned,SCALE,LAYERS

STRENGTHS=(3,4,6,8,10)
MULTIPLIERS=(3,1,2,2)
REFERENCE=json.loads((Path(__file__).parent/'blur_filter_reference.json').read_text())
FACTORS=np.array(REFERENCE['unpremultiply_factors'],dtype=np.uint64)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

class MapTextures(FighterTextures):
    def bake_compact(self,cropped,x,y,premultiply):
        first=len(self.tiles)
        for top in range(0,cropped.height,128):
            for left in range(0,cropped.width,128):
                tile=cropped.crop((left,top,min(left+128,cropped.width),min(top+128,cropped.height)))
                if tile.getbbox(alpha_only=True):self.tiles.append((x+left,y+top,*self.store(tile,premultiply)))
        if len(self.tiles)==first:return self.blank()
        return 0,0,first,len(self.tiles)-first,0,0,0

def filter_axis(pixels,width,axis):
    """One premultiplied pass with transparent outer samples and fixed weights.

    Fractional end weights and final truncation follow the pinned Ruffle blur
    shader's documented FP-compatible model. This integer implementation is
    validated separately; it does not assert original Flash filter equivalence.
    """
    n=min(255,int(width))
    if n<=1:return pixels.copy()
    r=math.ceil((n-1)/2);edge=255 if n%2 else 127
    pad=[(0,0)]*3;pad[axis]=(r,r)
    p=np.pad(pixels.astype(np.uint64),pad)
    prefix=np.cumsum(p,axis=axis,dtype=np.uint64)
    prefix=np.concatenate([np.zeros_like(np.take(prefix,[0],axis=axis)),prefix],axis=axis)
    size=pixels.shape[axis];indices=np.arange(size)
    inside=np.take(prefix,indices+2*r,axis=axis)-np.take(prefix,indices+1,axis=axis)
    outer=np.take(p,indices,axis=axis)+np.take(p,indices+2*r,axis=axis)
    return ((inside*255+outer*edge)//(255*n)).astype(np.uint8)

def blur(image,x,y=0):
    rgba=np.array(image.convert('RGBA'),dtype=np.uint32)
    alpha=rgba[:,:,3:4]
    rgba[:,:,:3]=(rgba[:,:,:3]*alpha+127)//255
    rgba=filter_axis(rgba.astype(np.uint8),x,1)
    rgba=filter_axis(rgba,y,0)
    result=rgba.copy();a=rgba[:,:,3]
    result[:,:,:3]=((rgba[:,:,:3].astype(np.uint64)*FACTORS[a,None]+0x8000)>>16).astype(np.uint8)
    return Image.fromarray(result)

def stage_layers(folder):
    layers={}
    for row in (folder/'stage.tsv').read_text().splitlines():
        kind,*fields=row.split('\t')
        if kind=='layer':
            name,x,y,w,h,frames=fields
            image=Image.open(folder/(name+'.png')).convert('RGBA')
            assert image.size==(int(w),int(h))
            layers[LAYERS[name]]={'name':name,'x':float(x),'y':float(y),'image':image,'path':folder/(name+'.png'),'declared_frames':int(frames)}
    return layers

def bound_to_stage(path,layers):
    data=path.read_bytes();magic,nfloor,ntile,start,flags,*_=struct.unpack_from('<8s4I8d',data)
    assert magic in (b'BVNSTG01',b'BVNSTG02')
    at=88+(8 if magic==b'BVNSTG02' else 0)+24*nfloor
    resized={k:v['image'].resize(tuple(max(1,round(n*SCALE)) for n in v['image'].size),Image.Resampling.NEAREST) for k,v in layers.items()}
    seen=set()
    for i in range(ntile):
        x,y,offset,bytes_,layer,w,h,stride,rows,reserved=struct.unpack_from('<ffII6H',data,at+28*i)
        if layer not in layers:return False
        image=resized[layer];meta=layers[layer]
        left=round(x-meta['x']*SCALE);top=round(y-meta['y']*SCALE)
        if left<0 or top<0 or left+w>image.width or top+h>image.height:return False
        tile=Image.new('RGBA',(stride,rows));tile.paste(image.crop((left,top,left+w,top+h)))
        if tile.tobytes()!=data[start+offset:start+offset+bytes_]:return False
        seen.add(layer)
    return seen==set(layers)

def pack_one(stage,layers,destination,scratch):
    scratch.mkdir(parents=True);(scratch/'poses').mkdir()
    textures=MapTextures(scratch,compact=True);frames=[];clips=[];report=[]
    for _ in range(9):clips.append((len(frames),1,0,1));frames.append((*textures.blank(),*EMPTY_BOX,0,0,0,0))
    for index,strength in enumerate(STRENGTHS):
        for layer in range(4):
            clips.append((len(frames),1,FINISH,1));key=f'{index}-{layer}'
            if layer not in layers:
                pose=None;baked=textures.blank();pixel_hash=None;half_size=None
            else:
                meta=layers[layer];image=meta['image']
                half_size=(int(image.width/2),int(image.height/2));assert min(half_size)>0
                snapshot=image.resize(half_size,Image.Resampling.NEAREST)
                filtered=blur(snapshot,strength*MULTIPLIERS[layer])
                pixel_hash=hashlib.sha256(filtered.tobytes()).hexdigest()
                # MapLayer displays the filtered half-size Bitmap at scale two.
                display=filtered.resize((half_size[0]*2,half_size[1]*2),Image.Resampling.NEAREST)
                display.save(scratch/'poses'/(key+'.png'))
                pose=(meta['x'],meta['y'],display.width,display.height)
                baked=textures.bake(key,pose,allow_tiles=True)
            frames.append((*baked,*EMPTY_BOX,0,0,0,0))
            report.append({'global_blur':strength,'layer':layer,'layer_blur':strength*MULTIPLIERS[layer],
                'clip':9+index*4+layer,'source_pose':pose,'half_size':half_size,'filtered_rgba_sha256':pixel_hash,'packed_pose':baked})
    packed,tiles,entries,payload=compress_textures(textures,frames,lambda b:zlib.compress(b,1))
    start=aligned(52+24*18+len(clips)*CLIP.size+len(packed)*FRAME.size+len(entries)*TEXTURE.size+len(tiles)*POSE.size)
    data=bytearray(HEADER.pack(b'BVNFGT12',len(packed),0,0,start,len(clips),0,1.,1.,1.))
    data.extend(struct.pack('<2H2I',0,0,len(entries),len(tiles)));data.extend(struct.pack('<24H',*([FINISH]*24)));data.extend(bytes(24*16))
    for fmt,rows in [(CLIP,clips),(FRAME,packed),(TEXTURE,entries),(POSE,tiles)]:data.extend(b''.join(fmt.pack(*row) for row in rows))
    data.extend(bytes(start-len(data)));data.extend(payload);destination.write_bytes(data)
    proof={'stage':stage.name,'stage_sha256':sha(stage),'output_sha256':sha(destination),'clips':report,
        'sources':{str(v['path']):sha(v['path']) for v in layers.values()},
        'declared_layer_frames':{str(k):v['declared_frames'] for k,v in layers.items()},
        'scope':'Blurred snapshots of the exact currently rendered static layer PNGs, bound byte-for-byte to their existing BVN GPU tiles. '
                'Original half-resolution draw, per-layer multipliers, cache-key parameters and scale-two placement are retained. '
                'Filter uses pinned Ruffle-compatible fixed-weight/alpha model, not original Flash raster proof. '
                'Animated-child map snapshots and complete original event timing remain separate migration work.',
        'filter_reference_sha256':sha(Path(__file__).parent/'blur_filter_reference.json'),
        'runtime':{'numpy':np.__version__,'Pillow':pillow_version}}
    destination.with_suffix('.json').write_text(json.dumps(proof,indent=2)+'\n')
    print(destination.name,len(data),'bytes',flush=True)
    return proof

def build(package,roots,output,scratch):
    output.mkdir(parents=True,exist_ok=True);scratch.mkdir(parents=True,exist_ok=True);reports=[]
    for stage in sorted(package.glob('*.BVN')):
        selected=None
        for root in roots:
            folder=root/stage.stem.lower()
            if not (folder/'stage.tsv').is_file():continue
            layers=stage_layers(folder)
            if layers and bound_to_stage(stage,layers):selected=layers;break
        if selected is None:raise ValueError('No byte-matching original layer set for '+stage.name)
        reports.append(pack_one(stage,selected,output/(stage.stem+'.BVB'),scratch/stage.stem))
    assert len(reports)==19
    (output/'manifest.json').write_text(json.dumps({'stages':reports,'strengths':STRENGTHS,'layer_multipliers':MULTIPLIERS},indent=2)+'\n')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('package',type=Path);p.add_argument('output',type=Path);p.add_argument('scratch',type=Path);p.add_argument('--sources',type=Path,nargs='+',required=True)
    a=p.parse_args();build(a.package,a.sources,a.output,a.scratch)
