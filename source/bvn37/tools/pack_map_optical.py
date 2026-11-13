#!/usr/bin/env python3
"""Pack exhaustive source-composited optical masks as shared streaming tiles."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse,hashlib,json,math,struct,zlib
from pathlib import Path
import numpy as np
from PIL import Image
from pack_fighter import FighterTextures,EMPTY_BOX
from pack_actions import HEADER,CLIP,FRAME,TEXTURE,POSE,FINISH,compress_textures
from pack_stage import SCALE,aligned
from pack_map_blur import blur,STRENGTHS,stage_layers,bound_to_stage

MISSING=0xffffffff
INFO=struct.Struct('<8s6I');RECT=struct.Struct('<4d');LAYOUT=struct.Struct('<2I');LOOKUP=struct.Struct('<ff2I')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def metadata(folder):
    children=[];references=[];canvas=None
    for line in (folder/'optical.tsv').read_text().splitlines():
        kind,*data=line.split('\t')
        if kind=='canvas':canvas=tuple(map(float,data[:2]))+tuple(map(int,data[2:]))
        elif kind=='child':
            assert int(data[0])==len(children);children.append(tuple(map(float,data[3:])))
        elif kind=='reference':references.append(int(data[0]))
    assert canvas and children and len(children)<=12 and sorted(references)==list(range(1<<len(children)))
    return canvas,children
def upgrade_stage(source,output,children):
    old=source.read_bytes();assert old[:8]==b'BVNSTG02'
    floor,tiles,pixelStart=struct.unpack_from('<3I',old,8);end=96+floor*24+tiles*28
    start=aligned(end+4);data=bytearray(old[:96]);data[:8]=b'BVNSTG03';struct.pack_into('<I',data,16,start)
    data.extend(struct.pack('<I',children));data.extend(old[96:end]);data.extend(bytes(start-len(data)));data.extend(old[pixelStart:])
    assert data[start:]==old[pixelStart:];output.write_bytes(data)

def build_one(folder,package,roots,output):
    canvas,children=metadata(folder);x,y,w,h=canvas;name=folder.name.upper();source=package/(name+'.BVN')
    selected=None
    for root in roots:
        candidate=root/folder.name
        if (candidate/'stage.tsv').is_file():
            layers=stage_layers(candidate)
            if layers and bound_to_stage(source,layers):selected=layers;break
    assert selected and 2 in selected,name
    base=selected[2];first=Image.open(folder/'references/0.png').convert('RGBA')
    assert (base['x'],base['y'],*base['image'].size)==canvas
    assert first.tobytes()==base['image'].tobytes(),name+' original opaque composite changed'
    normal=(round(w*SCALE),round(h*SCALE));half=(int(w/2),int(h/2));display=(half[0]*2,half[1]*2)
    blurred=tuple(max(1,round(n*SCALE)) for n in display)
    layouts=[];positions=[]
    for kind in range(6):
        size=normal if kind==0 else blurred;firstTile=len(positions)
        for top in range(0,size[1],128):
            for left in range(0,size[0],128):positions.append((kind,left,top,min(left+128,size[0]),min(top+128,size[1])))
        layouts.append((firstTile,len(positions)-firstTile))
    masks=1<<len(children);dense=np.full((masks,len(positions)),MISSING,dtype=np.uint32)
    textures=FighterTextures(folder,compact=True);palette=[];known={};proof=[]
    for mask in range(masks):
        path=folder/'references'/f'{mask}.png';image=Image.open(path).convert('RGBA');assert image.size==(w,h)
        images=[image.resize(normal,Image.Resampling.NEAREST)];snapshot=image.resize(half,Image.Resampling.NEAREST)
        hashes=[hashlib.sha256(images[0].tobytes()).hexdigest()]
        for strength in STRENGTHS:
            filtered=blur(snapshot,strength*2);hashes.append(hashlib.sha256(filtered.tobytes()).hexdigest())
            images.append(filtered.resize(display,Image.Resampling.NEAREST).resize(blurred,Image.Resampling.NEAREST))
        for i,(kind,left,top,right,bottom) in enumerate(positions):
            tile=images[kind].crop((left,top,right,bottom))
            if not tile.getbbox(alpha_only=True):continue
            pose=(0.,0.,*textures.store(tile))
            id=known.get(pose)
            if id is None:
                id=len(palette);palette.append(pose);known[pose]=id
            dense[mask,i]=id
        proof.append({'mask':mask,'source_sha256':sha(path),'native_normal_and_filtered_half_rgba_sha256':hashes})
        if mask%256==0:print(name,'mask',mask,'/',masks,'palette',len(palette),flush=True)
    assert palette and len(palette)+9<=32768
    clips=[];frames=[]
    for _ in range(9):clips.append((len(frames),1,0,1));frames.append((*textures.blank(),*EMPTY_BOX,0,0,0,0))
    clips.append((len(frames),len(palette),FINISH,1));frames.extend((*p,*EMPTY_BOX,0,0,0,0) for p in palette)
    packed,tiles,entries,payload=compress_textures(textures,frames,lambda raw:zlib.compress(raw,1));assert not tiles
    start=aligned(52+24*18+len(clips)*CLIP.size+len(packed)*FRAME.size+len(entries)*TEXTURE.size)
    data=bytearray(HEADER.pack(b'BVNFGT12',len(packed),0,0,start,len(clips),0,1.,1.,1.))
    data.extend(struct.pack('<2H2I',0,0,len(entries),0));data.extend(struct.pack('<24H',*([FINISH]*24)));data.extend(bytes(24*16))
    for fmt,values in [(CLIP,clips),(FRAME,packed),(TEXTURE,entries)]:data.extend(b''.join(fmt.pack(*v) for v in values))
    data.extend(bytes(start-len(data)));data.extend(payload);(output/(name+'.BVO')).write_bytes(data)
    lookups=[];indices=[];allMasks=np.arange(masks,dtype=np.uint32)
    for column,(_,left,top,_,_) in enumerate(positions):
        values=dense[:,column];dependencies=0
        for bit in range(len(children)):
            if np.any(values!=values[allMasks^(1<<bit)]):dependencies|=1<<bit
        bits=[i for i in range(len(children)) if dependencies&(1<<i)]
        expanded=np.array([sum(((m>>i)&1)<<bit for i,bit in enumerate(bits)) for m in range(1<<len(bits))],dtype=np.uint32)
        local=values[expanded];compact=np.zeros(masks,dtype=np.uint32)
        for i,bit in enumerate(bits):compact|=((allMasks>>bit)&1)<<i
        assert np.array_equal(local[compact],values)
        lookups.append((x*SCALE+left,y*SCALE+top,dependencies,len(indices)));indices.extend(map(int,local))
    tableStart=aligned(INFO.size+len(children)*RECT.size+6*LAYOUT.size+len(lookups)*LOOKUP.size)
    index=bytearray(INFO.pack(b'BVNOPT01',len(children),len(palette),len(lookups),len(indices),tableStart,0))
    for fmt,values in [(RECT,children),(LAYOUT,layouts),(LOOKUP,lookups)]:index.extend(b''.join(fmt.pack(*v) for v in values))
    index.extend(bytes(tableStart-len(index)));index.extend(struct.pack('<'+'I'*len(indices),*indices));(output/(name+'.BVI')).write_bytes(index)
    upgrade_stage(source,output/(name+'.BVN'),len(children))
    report={'stage':name,'children':len(children),'masks':masks,'variants':masks*6,'palette_frames':len(palette),
        'unique_textures':len(entries),'packed_texture_bytes':len(payload),'dense_lookup_indices':int(dense.size),
        'local_lookup_indices':len(indices),'metadata_bytes':len(index),'source_stage_sha256':sha(source),
        'source_front_png_sha256':sha(base['path']),'source_optical_tsv_sha256':sha(folder/'optical.tsv'),
        'outputs_sha256':{suffix:sha(output/(name+'.'+suffix)) for suffix in ['BVN','BVI','BVO']},'masks_sha256':proof,
        'scope':'All source-composited static optical masks exported directly by JPEXS from the pinned map SWF, including exact overlapping-child raster composition. Normal sampling and half-resolution per-layer blur use the declared native source-raster/filter pipeline. Tile-local dependency tables are exhaustively equivalent to the dense mask tables; streaming tile palettes preserve every distinct raster. This is not original Flash VM/raster equivalence.'}
    (output/(name+'.json')).write_text(json.dumps(report,indent=2)+'\n')
    print('PACKED',name,'palette',len(palette),'indices',len(indices),'bytes',len(data)+len(index),flush=True)
    return report
def build(art,package,roots,output):
    output.mkdir(parents=True,exist_ok=True);reports=[]
    for folder in sorted(art.iterdir()):
        if folder.is_dir() and (folder/'optical.tsv').is_file():reports.append(build_one(folder,package,roots,output))
    assert len(reports)==5 and sum(r['children'] for r in reports)==24 and sum(r['masks'] for r in reports)==4150
    (output/'manifest.json').write_text(json.dumps({'stages':reports,'children':24,'masks':4150,'variants':24900},indent=2)+'\n')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('art',type=Path);p.add_argument('package',type=Path);p.add_argument('output',type=Path);p.add_argument('--sources',type=Path,nargs='+',required=True)
    a=p.parse_args();build(a.art,a.package,a.sources,a.output)
