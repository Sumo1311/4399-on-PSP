#!/usr/bin/env python3
"""Bind palette decodes and exhaustive lookup results to original composite PNGs."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse,hashlib,json,struct,zlib
from pathlib import Path
from PIL import Image
from pack_actions import HEADER,CLIP,FRAME,TEXTURE
from pack_stage import SCALE,power_of_two
from pack_map_blur import blur,STRENGTHS

def fnv(raw,value=14695981039346656037):
    for byte in raw:value=((value^byte)*1099511628211)&0xffffffffffffffff
    return value
def palette(path):
    b=path.read_bytes();magic,n,hits,events,start,nclips,strikes,*_=HEADER.unpack_from(b)
    assert magic==b'BVNFGT12' and nclips==10 and not(hits or events or strikes)
    p,a,nt,tiles=struct.unpack_from('<2H2I',b,HEADER.size);assert not(p or a or tiles)
    at=52+24*2+24*16;clips=[CLIP.unpack_from(b,at+i*CLIP.size) for i in range(nclips)];at+=nclips*CLIP.size
    frames=[FRAME.unpack_from(b,at+i*FRAME.size) for i in range(n)];at+=n*FRAME.size
    entries=[TEXTURE.unpack_from(b,at+i*TEXTURE.size) for i in range(nt)]
    textures=[zlib.decompress(b[start+off:start+off+size]) for off,size,_ in entries]
    poses=[frames[clips[9][0]+i][:7] for i in range(clips[9][1])]
    values=[(pose,textures[pose[2]]) for pose in poses]
    return values
def make(art,banks,destination):
    lines=['5'];proof=[]
    for folder in sorted(art.iterdir()):
        if not (folder/'optical.tsv').exists():continue
        rows=[r.split('\t') for r in (folder/'optical.tsv').read_text().splitlines()]
        canvas=next(r[1:] for r in rows if r[0]=='canvas');x,y=map(float,canvas[:2]);w,h=map(int,canvas[2:])
        children=sum(r[0]=='child' for r in rows);masks=1<<children;name=folder.name.upper()
        values=palette(banks/(name+'.BVO'));reverse={}
        for id,(pose,raw) in enumerate(values):
            _,_,_,width,height,stride,heightRows=pose;reverse[(width,height,stride,heightRows,raw)]=id
        assert len(reverse)==len(values)
        normal=(round(w*SCALE),round(h*SCALE));half=(int(w/2),int(h/2));display=(half[0]*2,half[1]*2)
        blurred=tuple(max(1,round(n*SCALE)) for n in display);positions=[];counts=[]
        for kind in range(6):
            size=normal if not kind else blurred;first=len(positions)
            for top in range(0,size[1],128):
                for left in range(0,size[0],128):positions.append((kind,left,top,min(left+128,size[0]),min(top+128,size[1])))
            counts.append(len(positions)-first)
        lines.append(f'{name} {len(values)} {masks} '+' '.join(map(str,counts)))
        for _,left,top,_,_ in positions:lines.append(f'{x*SCALE+left:.17g} {y*SCALE+top:.17g}')
        for pose,raw in values:lines.append(' '.join(map(str,pose[3:]))+f' {len(raw)} {fnv(raw):016x}')
        digest=14695981039346656037
        for mask in range(masks):
            image=Image.open(folder/'references'/f'{mask}.png').convert('RGBA');assert image.size==(w,h)
            images=[image.resize(normal,Image.Resampling.NEAREST)];snapshot=image.resize(half,Image.Resampling.NEAREST)
            for strength in STRENGTHS:
                images.append(blur(snapshot,strength*2).resize(display,Image.Resampling.NEAREST).resize(blurred,Image.Resampling.NEAREST))
            for kind,left,top,right,bottom in positions:
                tile=images[kind].crop((left,top,right,bottom))
                if tile.getbbox(alpha_only=True):
                    stride,heightRows=power_of_two(tile.width),power_of_two(tile.height)
                    padded=Image.new('RGBA',(stride,heightRows));padded.paste(tile)
                    id=reverse[(tile.width,tile.height,stride,heightRows,padded.tobytes())]
                else:id=0xffffffff
                digest=fnv(struct.pack('<I',id),digest)
        lines.append(f'{digest:016x}');proof.append({'stage':name,'palette_frames':len(values),'mask_count':masks,'lookups':masks*len(positions),'dense_lookup_fnv':f'{digest:016x}'})
        print('Bound source masks',name,masks,flush=True)
    destination.write_text('\n'.join(lines)+'\n')
    destination.with_suffix('.json').write_text(json.dumps({'maps':proof,'fixture_sha256':hashlib.sha256(destination.read_bytes()).hexdigest(),
        'scope':'Independent BVF/zlib palette decoding and source-PNG reconstruction of every normal/blurred optical mask. Expected lookup IDs are found by matching source-generated RGBA tiles to decoded palette pixels without reading the optimized BVI dependency tables. Filter model is the separately checked declared integer model, not original Flash filter raster.'},indent=2)+'\n')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('art',type=Path);p.add_argument('banks',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();make(a.art,a.banks,a.output)
