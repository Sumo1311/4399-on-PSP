#!/usr/bin/env python3
"""Independently decode blur banks and create raster-placement reference stages."""
import argparse,hashlib,json,struct,zlib
from pathlib import Path
from pack_actions import HEADER,CLIP,FRAME,TEXTURE,POSE
from pack_stage import HEADER as STAGE_HEADER,TILE,aligned

def fnv(data):
    v=14695981039346656037
    for b in data:v=((v^b)*1099511628211)&0xffffffffffffffff
    return v

def build(banks,package,output):
    output.mkdir(parents=True,exist_ok=True);lines=['19'];gpu=[];proof=[]
    for stageIndex,path in enumerate(sorted(banks.glob('*.BVB'))):
        b=path.read_bytes();magic,n,hits,events,start,nclips,nstrikes,*_=HEADER.unpack_from(b)
        assert magic==b'BVNFGT12' and not(hits or events or nstrikes) and nclips==29
        projectile,attacker,nt,ntiles=struct.unpack_from('<2H2I',b,HEADER.size);assert not(projectile or attacker)
        at=HEADER.size+12+24*2+24*16
        clips=[CLIP.unpack_from(b,at+i*CLIP.size) for i in range(nclips)];at+=nclips*CLIP.size
        frames=[FRAME.unpack_from(b,at+i*FRAME.size) for i in range(n)];at+=n*FRAME.size
        textures=[TEXTURE.unpack_from(b,at+i*TEXTURE.size) for i in range(nt)];at+=nt*TEXTURE.size
        tiles=[POSE.unpack_from(b,at+i*POSE.size) for i in range(ntiles)]
        decoded=[zlib.decompress(b[start+off:start+off+size]) for off,size,raw in textures]
        for raw,(_,_,size) in zip(decoded,textures):assert len(raw)==size
        lines.append(path.stem)
        original=(package/(path.stem+'.BVN')).read_bytes();_,nfloor,oldTiles,oldStart,flags,*values=STAGE_HEADER.unpack_from(original)
        assert original[:8]==b'BVNSTG02'
        geometry=original[88:96+nfloor*24]
        for variant in range(5):
            stageTiles=[];stagePixels=bytearray()
            for layer in range(4):
                clip=9+variant*4+layer;root=frames[clips[clip][0]][:7]
                parts=tiles[root[2]:root[2]+root[3]] if root[5]==0 else [root]
                raw=b''.join(decoded[p[2]] for p in parts)
                lines.append(f'{clip} {len(parts)} {len(raw)} {fnv(raw):016x}')
                for x,y,index,w,h,stride,rows in parts:
                    assert stride<=128 and rows<=128
                    raw=decoded[index];assert len(raw)==stride*rows*4
                    stageTiles.append((x,y,len(stagePixels),len(raw),layer,w,h,stride,rows,0));stagePixels.extend(raw)
            name=f'MB{stageIndex:02d}_{variant}.BVN';offset=aligned(96+nfloor*24+len(stageTiles)*TILE.size)
            reference=bytearray(STAGE_HEADER.pack(b'BVNSTG02',nfloor,len(stageTiles),offset,flags,*values));reference.extend(geometry)
            reference.extend(b''.join(TILE.pack(*t) for t in stageTiles));reference.extend(bytes(offset-len(reference)));reference.extend(stagePixels)
            (output/name).write_bytes(reference);gpu.append(f'{path.stem} {variant} {name}')
        proof.append({'bank':path.name,'sha256':hashlib.sha256(b).hexdigest()})
    assert len(proof)==19
    (output/'BLURART.IN').write_text('\n'.join(lines)+'\n');(output/'BLURGPU.IN').write_text(str(len(gpu))+'\n'+'\n'.join(gpu)+'\n')
    (output/'blur-data-proof.json').write_text(json.dumps({'banks':proof,'scope':'Independent Python BVF/zlib decoding. Raster reference BVNs repackage identical decoded poses into the established static-stage draw path, to test positioning, clipping, filtering and shading separately from filter generation.'},indent=2)+'\n')
    print('Prepared 19 decoded blur banks and',len(gpu),'placement reference stages')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('banks',type=Path);p.add_argument('package',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();build(a.banks,a.package,a.output)
