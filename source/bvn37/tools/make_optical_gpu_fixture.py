#!/usr/bin/env python3
"""Build static-stage placement references from original optical composite PNGs."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse,hashlib,json,struct,zlib
from pathlib import Path
from PIL import Image
from pack_actions import HEADER,CLIP,FRAME,TEXTURE,POSE
from pack_stage import HEADER as SH,TILE,SCALE,aligned,power_of_two
from pack_map_blur import blur,STRENGTHS

def normal_stage(path):
    data=path.read_bytes();magic,nfloor,n,start,flags,*values=SH.unpack_from(data)
    assert magic==b'BVNSTG02'
    tiles=[TILE.unpack_from(data,96+nfloor*24+i*TILE.size) for i in range(n)]
    return (nfloor,flags,values,data[88:96+nfloor*24]),[(t,data[start+t[2]:start+t[2]+t[3]]) for t in tiles]

def blur_layers(path):
    b=path.read_bytes();magic,n,hits,events,start,nclips,strikes,*_=HEADER.unpack_from(b)
    assert magic==b'BVNFGT12' and nclips==29 and not(hits or events or strikes)
    p,a,nt,ntiles=struct.unpack_from('<2H2I',b,HEADER.size);assert not(p or a)
    at=52+24*18;clips=[CLIP.unpack_from(b,at+i*CLIP.size) for i in range(nclips)];at+=nclips*CLIP.size
    frames=[FRAME.unpack_from(b,at+i*FRAME.size) for i in range(n)];at+=n*FRAME.size
    entries=[TEXTURE.unpack_from(b,at+i*TEXTURE.size) for i in range(nt)];at+=nt*TEXTURE.size
    poses=[POSE.unpack_from(b,at+i*POSE.size) for i in range(ntiles)]
    raw=[zlib.decompress(b[start+off:start+off+size]) for off,size,_ in entries]
    result=[]
    for variant in range(5):
        parts=[]
        for layer in (0,1,3):
            root=frames[clips[9+variant*4+layer][0]][:7]
            tiles=poses[root[2]:root[2]+root[3]] if root[5]==0 else [root]
            for x,y,index,w,h,stride,rows in tiles:
                parts.append(((x,y,0,len(raw[index]),layer,w,h,stride,rows,0),raw[index]))
        result.append(parts)
    return result

def front_tiles(image,x,y):
    result=[]
    for top in range(0,image.height,128):
        for left in range(0,image.width,128):
            tile=image.crop((left,top,min(left+128,image.width),min(top+128,image.height)))
            if not tile.getbbox(alpha_only=True):continue
            stride,rows=power_of_two(tile.width),power_of_two(tile.height)
            padded=Image.new('RGBA',(stride,rows));padded.paste(tile);raw=padded.tobytes()
            result.append(((x*SCALE+left,y*SCALE+top,0,len(raw),2,tile.width,tile.height,stride,rows,0),raw))
    return result

def write_stage(path,geometry,parts):
    nfloor,flags,values,header=geometry;pixels=bytearray();tiles=[]
    for t,raw in parts:
        t=list(t);t[2]=len(pixels);tiles.append(t);pixels.extend(raw)
    start=aligned(96+nfloor*24+len(tiles)*TILE.size)
    data=bytearray(SH.pack(b'BVNSTG02',nfloor,len(tiles),start,flags,*values));data.extend(header)
    data.extend(b''.join(TILE.pack(*t) for t in tiles));data.extend(bytes(start-len(data)));data.extend(pixels);path.write_bytes(data)

def build(art,package,output):
    output.mkdir(parents=True,exist_ok=True);cases=[];proof=[]
    for folder in sorted(art.iterdir()):
        if not (folder/'optical.tsv').is_file():continue
        rows=[r.split('\t') for r in (folder/'optical.tsv').read_text().splitlines()]
        _,x,y,w,h=next(r for r in rows if r[0]=='canvas');x,y=float(x),float(y);w,h=int(w),int(h)
        n=sum(r[0]=='child' for r in rows);allmask=(1<<n)-1
        masks=sorted({0,allmask,sum(1<<i for i in range(0,n,2)),*(1<<i for i in range(n))})
        name=folder.name.upper();geometry,normal=normal_stage(package/(name+'.BVN'));blurred=blur_layers(package/(name+'.BVB'))
        for mask in masks:
            image=Image.open(folder/'references'/f'{mask}.png').convert('RGBA')
            half=(w//2,h//2);display=(half[0]*2,half[1]*2)
            images=[image.resize((round(w*SCALE),round(h*SCALE)),Image.Resampling.NEAREST)]
            snapshot=image.resize(half,Image.Resampling.NEAREST)
            images.extend(blur(snapshot,s*2).resize(display,Image.Resampling.NEAREST).resize(tuple(max(1,round(v*SCALE)) for v in display),Image.Resampling.NEAREST) for s in STRENGTHS)
            for kind,front in enumerate(images):
                rest=[p for p in normal if p[0][4]!=2] if not kind else blurred[kind-1]
                ref=f'OG{len(cases):04d}.BVN';write_stage(output/ref,geometry,rest+front_tiles(front,x,y))
                cases.append(f'{name} {kind} {mask} {ref}')
            proof.append({'stage':name,'mask':mask,'source_png_sha256':hashlib.sha256((folder/'references'/f'{mask}.png').read_bytes()).hexdigest()})
    (output/'OPTICALGPU.IN').write_text(str(len(cases))+'\n'+'\n'.join(cases)+'\n')
    (output/'optical-gpu-proof.json').write_text(json.dumps({'rows':len(cases),'comparisons':len(cases)*4,'pixels':len(cases)*4*480*272,'source_masks':proof,'scope':'Original composite PNGs independently resized/filtered and repacked into the static StageAsset renderer. Covers normal and five blur strengths, opaque/all-half/individual/mixed masks. GPU probe adds two camera/root/shade cases and two qualities. Does not establish original Flash filter raster or VM clock equivalence.'},indent=2)+'\n')
    print('Prepared',len(cases),'optical placement references',flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('art',type=Path);p.add_argument('package',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();build(a.art,a.package,a.output)
