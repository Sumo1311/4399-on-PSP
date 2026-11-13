#!/usr/bin/env python3
"""Extract pinned Musou graphic-only cue transforms and referenced PNG leaves."""
from pathlib import Path
from PIL import Image
import xml.etree.ElementTree as E
import json,hashlib
FIELDS={'a':1.,'d':1.,'b':0.,'c':0.,'tx':0.,'ty':0.}
def build(reference,header):
 root=reference/'flash-source/SRC_XFL/ui/musou/LIBRARY';cache={};auto={};files={root.parent/'DOMDocument.xml'};images={}
 bitmapNames={e.get('name'):e.get('href') for e in E.parse(root.parent/'DOMDocument.xml').getroot().findall('.//{*}DOMBitmapItem')}
 def xml(name):
  if name not in cache:
   p=root/(name+'.xml');files.add(p);cache[name]=E.parse(p).getroot()
  return cache[name]
 def length(name):return max(int(f.get('index'))+int(f.get('duration','1')) for f in xml(name).findall('.//{*}DOMLayer/{*}frames/{*}DOMFrame'))
 def matrix(e):
  m=e.find('./{*}matrix/{*}Matrix');v={} if m is None else m.attrib
  assert set(v)<=set(FIELDS),v
  out={k:float(v.get(k,d)) for k,d in FIELDS.items()};assert not out['b'] and not out['c'];return out
 def color(e):
  c=e.find('./{*}color/{*}Color')
  if c is None:return 1.,0.
  assert set(c.attrib)<= {'alphaMultiplier','redOffset','greenOffset','blueOffset'},c.attrib
  offsets=[float(c.get(k,'0')) for k in ['redOffset','greenOffset','blueOffset']]
  assert len(set(offsets))==1,c.attrib
  return float(c.get('alphaMultiplier','1')),offsets[0]
 def draw(name,frame,parent=(1.,1.,0.,0.,1.,0.),depth=0):
  assert depth<12
  result=[]
  # XFL layers are front-to-back; native draw calls are back-to-front.
  for layer in reversed(xml(name).findall('./{*}timeline/{*}DOMTimeline/{*}layers/{*}DOMLayer')):
   if layer.get('layerType')=='guide' or layer.get('visible')=='false':continue
   frames=layer.findall('./{*}frames/{*}DOMFrame');current=next((f for f in frames if int(f.get('index'))<=frame<int(f.get('index'))+int(f.get('duration','1'))),None)
   if current is None:continue
   children=current.findall('./{*}elements/*');base=int(current.get('index'));nextframe=next((f for f in frames if int(f.get('index'))>base),None)
   for e in children:
    tag=e.tag.split('}')[-1];assert tag in ['DOMBitmapInstance','DOMSymbolInstance'],(name,tag)
    m=matrix(e);a,offset=color(e)
    if current.get('tweenType')=='motion' and nextframe is not None and int(current.get('duration','1'))>1:
     counterparts=nextframe.findall('./{*}elements/*');assert len(children)==len(counterparts)==1,(name,frame,base,len(children),len(counterparts),current.attrib,nextframe.attrib)
     other=counterparts[0];assert other.get('libraryItemName')==e.get('libraryItemName');to=matrix(other);ta,toffset=color(other)
     t=(frame-base)/(int(nextframe.get('index'))-base);acc=float(current.get('acceleration','0'))/100
     # Declared classic-tween easing (quadratic interpolation, no custom ease).
     t=(1-acc)*t+acc*(1-(1-t)*(1-t)) if acc>=0 else (1+acc)*t-acc*t*t
     m={k:m[k]+(to[k]-m[k])*t for k in m};a+=(ta-a)*t;offset+=(toffset-offset)*t
    pa,pd,px,py,palpha,poffset=parent;pose=(pa*m['a'],pd*m['d'],px+pa*m['tx'],py+pd*m['ty'],palpha*a,poffset+offset)
    item=e.get('libraryItemName')
    if tag=='DOMBitmapInstance':
     item=bitmapNames.get(item,item)
     files.add(root/item);images.setdefault(item,'musou-'+str(len(images)))
     with Image.open(root/item) as image:width,height=image.size
     result.append((images[item],*pose,width,height))
    else:
     assert e.get('symbolType','movie clip') in ['graphic','movie clip'],(name,item,e.attrib)
     if e.get('symbolType','movie clip')=='movie clip':
      assert name=='game_start_mc' and base==90 and int(current.get('duration','1'))==74
      assert not xml(item).findall('.//{*}Actionscript')
      auto.setdefault(item,len(auto));result.append(('@'+str(auto[item]),*pose,0,0));continue
     child=int(e.get('firstFrame','0'));loop=e.get('loop','loop')
     if loop!='single frame':child+=frame-base
     n=length(item);child=min(child,n-1) if loop=='play once' else child%n
     result.extend(draw(item,child,pose,depth+1))
  return result
 stopFrames={int(f.get('index')) for f in xml('game_start_mc').findall('.//{*}DOMLayer/{*}frames/{*}DOMFrame') if f.get('name')=='stop'}
 assert stopFrames=={0,69,175,247,299,348,423},stopFrames
 frames=[draw('game_start_mc',frame) for frame in range(430)];flat=[];ranges=[]
 autoFrames=[[draw(name,i) for i in range(length(name))] for name in auto]
 assert len(auto)==2 and all(len(v)==200 for v in autoFrames)
 for frame in frames:ranges.append((len(flat),len(frame)));flat.extend(frame)
 autoRanges=[]
 for clip in autoFrames:
  values=[]
  for frame in clip:values.append((len(flat),len(frame)));flat.extend(frame)
  autoRanges.append(values)
 header.parent.mkdir(parents=True,exist_ok=True)
 def number(x):return format(float(x),'.17g')
 text='// Generated from pinned Musou XFL; declared graphic tween sampling.\n// SPDX-License-Identifier: GPL-3.0-or-later\n#pragma once\n#include <array>\nnamespace bvn {\nstruct MusouCueRange{unsigned first,count;};\nstruct MusouCueDraw{const char* key;double a,d,x,y,alpha,offset,w,h;};\n'
 text+='inline constexpr std::array<MusouCueRange,430> MusouCueRanges{{\n'+''.join('{%d,%d},\n'%r for r in ranges)+'}};\n'
 for index,values in enumerate(autoRanges):
  text+='inline constexpr std::array<MusouCueRange,200> MusouStripRanges%d{{\n'%index+''.join('{%d,%d},\n'%r for r in values)+'}};\n'
 text+=f'inline constexpr std::array<MusouCueDraw,{len(flat)}> MusouCueDraws{{{{\n'+''.join('{"%s",%s},\n'%(r[0],','.join(number(v) for v in r[1:])) for r in flat)+'}};\n}\n'
 header.write_text(text)
 report={'frames':430,'poses':frames,'autoClips':auto,'autoPoses':autoFrames,'draws':len(flat),'images':images,'sourceHashes':{str(p.relative_to(reference)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(files)},'scope':'Pinned XFL composition, axis-aligned transforms, classic quadratic easing, equal-channel RGB offsets and alpha. Explicit sampling model, not Flash VM/raster or full Flash movie-clip construction/broadcast ordering; the two scriptless 200-frame warning-strip clips use independent root-frame ages from keyframe-90 creation, including UI pause.'}
 header.with_suffix('.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n');print('Musou cue extraction',len(flat),'draws',len(images),'PNGs');return report
if __name__=='__main__':
 import argparse
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('header',type=Path);p.add_argument('--reference',type=Path,default=Path('reference'));a=p.parse_args();build(a.reference,a.header)
