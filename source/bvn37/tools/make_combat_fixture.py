#!/usr/bin/env python3
"""Build source-derived value/selector and independently decoded art fixtures."""
import argparse,hashlib,json,struct,zlib
from pathlib import Path
from pack_actions import HEADER,CLIP,FRAME,TEXTURE,POSE

def fnv(raw):
    value=14695981039346656037
    for byte in raw:value=((value^byte)*1099511628211)&0xffffffffffffffff
    return value

def build(model,art,output,header):
    catalog=json.loads(model.read_text());meta=json.loads(art.with_suffix('.json').read_text())
    effects=sorted(catalog['effects'],key=lambda e:e['id']);queries=catalog['queries']
    lines=[f'BVNEFFECT1 {len(effects)} {len(queries)} {len(meta["clips"])} {meta["frames"]}']
    for e in effects:
        shine=e['shine'] or {};shake=e['shake'] or {};slow=e['slowDown'] or {}
        flags=sum(int(e[k])*v for k,v in [('randRotate',1),('followDirect',2),('isSpecial',4),('isBuff',8),('isSteelHit',16)])
        flags|=32*int(e['targetColorOffset'] is not None)
        words=[e['id'],e['className'],e['sound'] or '-',e['specialEffectId'] or '-',
               ['normal','add','lighten','hardlight','screen'].index(e['blendMode']),e['freeze'],int(e['shine'] is not None),
               shine.get('color',0xffffff),shine.get('alpha',.2),shake.get('x',0),shake.get('y',0),shake.get('pow',0),
               shake.get('time',0),slow.get('rate',1.5),slow.get('time',0),flags,*(e['targetColorOffset'] or [0,0,0])]
        lines.append(' '.join(map(str,words)))
    for q in queries:lines.append(f'{q["kind"]} {q["type"]} {int(q["enemy"])} {q["defense"]} {q["id"] or "-"}')
    data=art.read_bytes();magic,n,hitCount,eventCount,payload,clipCount,strikeCount,*_=HEADER.unpack_from(data)
    assert magic==b'BVNFGT12' and not(hitCount or eventCount or strikeCount)
    projectiles,attackers,textureCount,tileCount=struct.unpack_from('<2H2I',data,HEADER.size)
    assert not(projectiles or attackers)
    cursor=HEADER.size+12+24*2+24*16
    clips=[CLIP.unpack_from(data,cursor+i*CLIP.size) for i in range(clipCount)];cursor+=clipCount*CLIP.size
    frames=[FRAME.unpack_from(data,cursor+i*FRAME.size) for i in range(n)];cursor+=n*FRAME.size
    textures=[TEXTURE.unpack_from(data,cursor+i*TEXTURE.size) for i in range(textureCount)];cursor+=textureCount*TEXTURE.size
    tiles=[POSE.unpack_from(data,cursor+i*POSE.size) for i in range(tileCount)]
    pixels=[]
    for offset,packed,raw in textures:
        item=zlib.decompress(data[payload+offset:payload+offset+packed]);assert len(item)==raw;pixels.append(item)
    total_bytes=0;art_rows=[];header_rows=[]
    for item in meta['clips']:
        cls,clip,count=item['className'],item['clip'],item['cache_frames']
        assert clips[clip][1]==count
        lines.append(f'{cls} {clip} {count}');header_rows.append(f'    {{"{cls}", {clip}, {count}}},')
        for phase in range(count):
            pose=frames[clips[clip][0]+phase][:7]
            parts=tiles[pose[2]:pose[2]+pose[3]] if pose[5]==0 else [pose]
            raw=b''.join(pixels[p[2]] for p in parts)
            for p in parts:assert len(pixels[p[2]])==p[5]*p[6]*4
            digest=fnv(raw);total_bytes+=len(raw)
            lines.append(f'{phase} {len(parts)} {len(raw)} {digest:016x}')
            art_rows.append({'className':cls,'phase':phase,'tiles':len(parts),'decoded_bytes':len(raw),'fnv64':f'{digest:016x}'})
    output.write_text('\n'.join(lines)+'\n')
    header.write_text('// Generated from the pinned combat effect bank.\n// SPDX-License-Identifier: GPL-3.0-or-later\n'
                      '#pragma once\n#include <string_view>\nnamespace bvn {\n'
                      'struct CombatArt { std::string_view className; unsigned clip,frames; };\n'
                      'inline constexpr CombatArt CombatArtData[]{\n'+'\n'.join(header_rows)+'\n};\n}\n')
    proof={'definitions':len(effects),'queries':len(queries),'clips':len(meta['clips']),'frames':meta['frames'],
           'decoded_bytes_in_frame_order':total_bytes,'art_rows':art_rows,
           'model_sha256':hashlib.sha256(model.read_bytes()).hexdigest(),
           'art_sha256':hashlib.sha256(data).hexdigest(),'fixture_sha256':hashlib.sha256(output.read_bytes()).hexdigest(),
           'scope':'Original AS3 value/selection expectations and independent Python zlib texture decoding; '
                   'not original Flash display/timer integration or native rendering validation'}
    output.with_suffix('.json').write_text(json.dumps(proof,indent=2)+'\n')
    print(f'Prepared {len(effects)} definitions, {len(queries)} queries, {meta["frames"]} decoded frames / {total_bytes} bytes')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['model','art','output','header']:p.add_argument(name,type=Path)
    a=p.parse_args();build(a.model,a.art,a.output,a.header)
