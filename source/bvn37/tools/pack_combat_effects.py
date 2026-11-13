#!/usr/bin/env python3
"""Bake pinned hit, guard, floor and ending EffectVO bitmap caches into a world bank."""
import argparse,hashlib,json,re,struct,zlib
from functools import lru_cache
from copy import deepcopy
from pathlib import Path
import xml.etree.ElementTree as ET
from PIL import Image
from build_musou_xfl import IDENTITY,NO_COLOR,concatenate,matrix,color_transform,tag,DAT_HEADER,inflate_chunks,rgba_from_cache
from pack_counter_effects import Project as CounterProject,CounterTimeline
from build_musou_xfl import frame_index,end
from pack_fighter import FighterTextures,EMPTY_BOX,SCALE
from pack_actions import HEADER,CLIP,FRAME,TEXTURE,POSE,FINISH,compress_textures
from pack_stage import aligned

CLASSES=('XG_kan2','XG_kanx2','XG_qdj','XG_qdjx','XG_mfdj','XG_mfdjx','XG_fire','XG_ice','XG_leidian',
         'XG_fykan','XG_fykanx','XG_fyq','XG_fyqx','XG_mffy','XG_mffyx','XG_fire_fy','XG_ice_fy','XG_dian_fy',
         'xg_catch_hit','XG_hitfloor','xg_hitfloor_low','xg_hitfllor_heavy','xg_hitfloor_yan','xg_hitover',
         'xg_fire_ing','xg_ice_ing','xg_dian_ing','xg_mc_change',
         'xg_buff_speed','xg_buff_power','xg_buff_defense',
         'xg_buff_effect_speed','xg_buff_effect_power','xg_buff_effect_defense','kobg_effect_mc')

class CombatTimeline(CounterTimeline):
    def entries(self,frame):
        if self.xml.get('name')!='xg_ice_ing':
            yield from super().entries(frame);return
        for layer,frames in reversed(self.layers):
            active=next((key for key in reversed(frames) if frame_index(key)<=frame<end(key)),None)
            if active is None:continue
            instances=active.findall('./{*}elements/*')
            if active.get('tweenType'):
                assert active.get('tweenType')=='motion' and active.get('acceleration','0')=='0'
                assert frame_index(active) in (0,2) and end(active)==frame_index(active)+2
                following=next(key for key in frames if frame_index(key)==end(active))
                targets=following.findall('./{*}elements/*');assert len(instances)==len(targets)==1
                a,b=instances[0],targets[0]
                assert a.get('libraryItemName')==b.get('libraryItemName')=='元件 8' and matrix(a)==matrix(b)
                ca=a.find('./{*}color/{*}Color');cb=b.find('./{*}color/{*}Color')
                assert ca is None or set(ca.attrib)=={'alphaMultiplier'}
                assert cb is None or set(cb.attrib)=={'alphaMultiplier'}
                alpha_a=1 if ca is None else float(ca.get('alphaMultiplier'))
                alpha_b=1 if cb is None else float(cb.get('alphaMultiplier'))
                instance=deepcopy(a);color=instance.find('./{*}color')
                if color is None:color=ET.SubElement(instance,'{http://ns.adobe.com/xfl/2008/}color')
                color.clear();ET.SubElement(color,'{http://ns.adobe.com/xfl/2008/}Color',
                    alphaMultiplier=str(alpha_a+(alpha_b-alpha_a)*(frame-frame_index(active))/2))
                instances=[instance]
            for instance in instances:yield layer.get('name'),instance,frame-frame_index(active)

class Project(CounterProject):
    def __init__(self,document):
        super().__init__(document)
        self.bitmap_rectangles=set()
        self.opaque_palettes=set()
    @lru_cache(maxsize=None)
    def symbol(self,name):
        path=self.root/'LIBRARY'/(name+'.xml');self.sources.add(path)
        return CombatTimeline(ET.parse(path).find('./{*}timeline/{*}DOMTimeline'))
    @lru_cache(maxsize=None)
    def bitmap(self,name):
        path=self.root/'bin'/self.media[name].get('bitmapDataHRef')
        data=path.read_bytes()
        if data[24]!=0:return super().bitmap(name)
        self.sources.add(path)
        magic,stride,width,height,left,right,top,bottom=DAT_HEADER.unpack_from(data)
        assert magic==0x303 and stride>=width and (left,right,top,bottom)==(0,width*20,0,height*20)
        count=struct.unpack_from('<H',data,25)[0];assert 1<=count<=256
        flags=27+4*count;assert data[flags]==1
        palette=rgba_from_cache(data[27:flags],(count,1))
        assert palette.getchannel('A').getextrema()==(255,255)
        raw=inflate_chunks(data,flags+1,path);assert len(raw)==stride*height
        image=Image.frombytes('P',(width,height),raw,'raw','P',stride)
        image.putpalette(palette.tobytes(),'RGBA');self.opaque_palettes.add(name)
        return image.convert('RGBA')
    def images(self,timeline,frame,placement,held=None,color=NO_COLOR):
        for layer,instance,elapsed in timeline.entries(frame):
            transform=concatenate(placement,matrix(instance))
            transformed=color_transform(color,instance)
            assert instance.get('blendMode') in (None,'normal'),instance.attrib
            if tag(instance)=='DOMBitmapInstance':
                yield transform,self.colored_bitmap(instance.get('libraryItemName'),transformed)
            elif tag(instance)=='DOMSymbolInstance':
                child=self.symbol(instance.get('libraryItemName'))
                yield from self.images(child,self.child_frame(instance,child,elapsed,held),transform,color=transformed)
            elif tag(instance)=='DOMShape':
                # These decompiled shapes are full, axis-aligned bitmap rectangles,
                # not a vector rasterizer or a general shape approximation.
                styles=instance.findall('./{*}fills/{*}FillStyle')
                assert len(styles)==1 and not instance.findall('./{*}strokes/*')
                fill=styles[0].find('./{*}BitmapFill')
                assert fill is not None and fill.get('bitmapIsClipped')=='true'
                assert matrix(fill)==(20.,0.,0.,20.,0.,0.),ET.tostring(fill)
                name=fill.get('bitmapPath');image=self.colored_bitmap(name,transformed)
                segments=[]
                for edge in instance.findall('./{*}edges/{*}Edge'):
                    assert edge.get('fillStyle1')==styles[0].get('index') and not edge.get('strokeStyle')
                    encoded=edge.get('edges','')
                    points=re.findall(r'([!|])\s*(-?\d+)\s+(-?\d+)',encoded)
                    assert re.sub(r'[!|]\s*-?\d+\s+-?\d+','',encoded).strip()==''
                    current=None
                    for op,x,y in points:
                        point=(int(x),int(y))
                        if op=='|':
                            assert current is not None
                            segments.append((current,point))
                        current=point
                width,height=image.size;corners={(0,0),(20*width,0),(20*width,20*height),(0,20*height)}
                assert len(segments)==4 and {p for line in segments for p in line}==corners
                assert all(a[0]==b[0] or a[1]==b[1] for a,b in segments)
                assert len({tuple(sorted(line)) for line in segments})==4
                self.bitmap_rectangles.add(name)
                yield transform,image
            else:
                raise ValueError(f'Unsupported visible {tag(instance)} in {timeline.xml.get("name")}')

def build(reference,destination,scratch):
    scratch.mkdir(parents=True,exist_ok=True);(scratch/'poses').mkdir(exist_ok=True)
    project=Project(reference/'flash-source/SRC_XFL/effect/DOMDocument.xml')
    linkages={}
    for item in project.document.findall('./{*}symbols/*'):
        p=project.root/'LIBRARY'/item.get('href');root=ET.parse(p).getroot()
        if root.get('linkageClassName'):
            linkages[root.get('linkageClassName')]=root.get('name')
    textures=FighterTextures(scratch,compact=True);frames=[];clips=[]
    for _ in range(9):
        clips.append((len(frames),1,0,1));frames.append((*textures.blank(),*EMPTY_BOX,0,0,0,0))
    report=[]
    for index,cls in enumerate(CLASSES):
        symbol=linkages[cls];timeline=project.symbol(symbol)
        expected_labels={'xg_fire_ing':{'loop':12,'finish':13},'xg_ice_ing':{'loop':5,'finish':6},
                         'xg_dian_ing':{'loop':6,'finish':7}}.get(cls,{})
        assert timeline.labels==expected_labels and not timeline.scripts,(cls,timeline.labels,timeline.scripts)
        count=timeline.count-1
        assert count>1
        clips.append((len(frames),count,FINISH,1));poses=[]
        for frame in range(count):
            key=f'{index}-{frame}'
            pose=project.cache_pose(timeline,frame,scratch,key)
            baked=textures.bake(key,pose,allow_tiles=True)
            frames.append((*baked,*EMPTY_BOX,0,0,0,0))
            poses.append({'key':key,'source_pose':pose,'packed_pose':baked})
        report.append({'className':cls,'symbol':symbol,'clip':9+index,'cache_frames':count,
                       'labels':timeline.labels,'poses':poses})
    packed,tiles,entries,payload=compress_textures(textures,frames,lambda d:zlib.compress(d,1))
    start=aligned(52+24*18+len(clips)*CLIP.size+len(packed)*FRAME.size+len(entries)*TEXTURE.size+len(tiles)*POSE.size)
    data=bytearray(HEADER.pack(b'BVNFGT12',len(packed),0,0,start,len(clips),0,1.,1.,1.))
    data.extend(struct.pack('<2H2I',0,0,len(entries),len(tiles)))
    data.extend(struct.pack('<24H',*([FINISH]*24)));data.extend(bytes(24*16))
    for fmt,rows in [(CLIP,clips),(FRAME,packed),(TEXTURE,entries),(POSE,tiles)]:
        data.extend(b''.join(fmt.pack(*row) for row in rows))
    data.extend(bytes(start-len(data)));data.extend(payload);destination.write_bytes(data)
    document={'scope':'35 pinned bitmap-backed XFL hit/guard/floor/ending, elemental, team-change and buff caches, including validated full '
                     'bitmap-fill rectangles; not original compiled Flash raster or complete EffectModel playback',
              'frames':sum(c['cache_frames'] for c in report),'clips':report,'format':'BVNFGT12',
              'output_sha256':hashlib.sha256(data).hexdigest(),'bitmap_rectangles':sorted(project.bitmap_rectangles),
              'opaque_flag_zero_palettes':sorted(project.opaque_palettes),
              'sources':{str(p.relative_to(reference)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(project.sources)},
              'sampling':'EffectVO excludes the final authored frame; final cached frame is not shown by auto-removing EffectView',
              'quality':'Lossless RGBA8888 payload after existing PSP view-scale resize; nearest sampling of source bitmap transforms'}
    destination.with_suffix('.json').write_text(json.dumps(document,indent=2)+'\n')
    header=Path(__file__).resolve().parent.parent/'src/combat_effect_art.hpp'
    offsets=[0 if p['source_pose'] is None else p['source_pose'][0]*SCALE for r in report for p in r['poses']]
    start=0
    for r in report:r['offsets_start']=start;start+=r['cache_frames']
    def literal(v):
        s=format(v,'.9g');return s+('.0' if '.' not in s and 'e' not in s else '')+'f'
    header.write_text('// Generated from the pinned combat effect bank.\n// SPDX-License-Identifier: GPL-3.0-or-later\n'
                      '#pragma once\n#include <string_view>\nnamespace bvn {\n'
                      'struct CombatArt { std::string_view className; unsigned clip,frames,loop=0,finish=0,offsetsStart=0; };\n'
                      'inline constexpr float CombatArtOffsetX[]{'+','.join(map(literal,offsets))+'};\n'
                      'inline constexpr CombatArt CombatArtData[]{\n'+
                      ''.join('    {"%s", %d, %d, %d, %d, %d},\n'%(r['className'],r['clip'],r['cache_frames'],
                              r['labels'].get('loop',0),r['labels'].get('finish',0),r['offsets_start']) for r in report)+'};\n}\n')
    print(f'Packed {len(report)} classes, {document["frames"]} source frames, {len(data)} bytes')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('reference',type=Path);p.add_argument('output',type=Path);p.add_argument('scratch',type=Path)
    a=p.parse_args();build(a.reference.resolve(),a.output,a.scratch)
