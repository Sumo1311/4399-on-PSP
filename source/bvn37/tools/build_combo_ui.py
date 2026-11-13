#!/usr/bin/env python3
"""Pack pinned HitsUI PNGs and sample its two authored axis-aligned timelines."""
from pathlib import Path
import argparse,hashlib,json,xml.etree.ElementTree as E
from PIL import Image
from pack_versus_ui import pack_images,SCALE
FIELDS={'a':1.,'d':1.,'b':0.,'c':0.,'tx':0.,'ty':0.}
def build(reference,output,header):
    files=set();images={};profiles=[];digits=[]
    for profile in ['fight','musou']:
        root=reference/'flash-source/SRC_XFL/ui'/profile/'LIBRARY';cache={}
        def xml(name):
            if name not in cache:
                p=root/(name+'.xml');files.add(p);cache[name]=E.parse(p).getroot()
            return cache[name]
        def matrix(e):
            m=e.find('./{*}matrix/{*}Matrix');v={} if m is None else m.attrib
            assert set(v)<=set(FIELDS) and not float(v.get('b',0)) and not float(v.get('c',0))
            assert e.find('./{*}color') is None
            return {k:float(v.get(k,d)) for k,d in FIELDS.items()}
        def draw(name,frame,parent=(1.,1.,0.,0.),depth=0):
            assert depth<10;result=[]
            for layer in reversed(xml(name).findall('./{*}timeline/{*}DOMTimeline/{*}layers/{*}DOMLayer')):
                if layer.get('layerType')=='guide' or layer.get('visible')=='false':continue
                frames=layer.findall('./{*}frames/{*}DOMFrame');current=next((f for f in frames if int(f.get('index'))<=frame<int(f.get('index'))+int(f.get('duration','1'))),None)
                if current is None:continue
                children=current.findall('./{*}elements/*');base=int(current.get('index'));nxt=next((f for f in frames if int(f.get('index'))>base),None)
                for e in children:
                    m=matrix(e)
                    if current.get('tweenType')=='motion' and nxt is not None:
                        counterparts=nxt.findall('./{*}elements/*');assert len(children)==len(counterparts)==1
                        other=counterparts[0];assert other.get('libraryItemName')==e.get('libraryItemName');to=matrix(other)
                        t=(frame-base)/(int(nxt.get('index'))-base);acc=float(current.get('acceleration','0'))/100
                        t=(1-acc)*t+acc*(1-(1-t)*(1-t)) if acc>=0 else (1+acc)*t-acc*t*t
                        m={k:m[k]+(to[k]-m[k])*t for k in m}
                    a,d,x,y=parent;pose=(a*m['a'],d*m['d'],x+a*m['tx'],y+d*m['ty']);item=e.get('libraryItemName');tag=e.tag.split('}')[-1]
                    if name=='#链接/MC_hits' and e.get('name')=='ct':
                        # Its authored digit is a guide; the runtime MCNumber is
                        # inserted here by HitsUI and rendered separately.
                        assert all(l.get('layerType')=='guide' for l in xml(item).findall('./{*}timeline/{*}DOMTimeline/{*}layers/{*}DOMLayer'))
                        result.append(('@number',*pose,0,0));continue
                    if tag=='DOMBitmapInstance':
                        p=root/item;files.add(p)
                        with Image.open(p) as im:width,height=im.size
                        assert item=='hits.png' or item.startswith('hits_num')
                        key='combo-label' if item=='hits.png' else 'combo-'+p.stem
                        if key not in images:
                            with Image.open(p) as im:images[key]=im.convert('RGBA').resize((round(width*SCALE),round(height*SCALE)),Image.Resampling.LANCZOS)
                        result.append((key,*pose,width,height))
                    else:
                        assert tag=='DOMSymbolInstance'
                        child=int(e.get('firstFrame','0'))
                        if e.get('symbolType','movie clip')=='movie clip':
                            total=max(int(f.get('index'))+int(f.get('duration','1')) for f in xml(item).findall('.//{*}DOMLayer/{*}frames/{*}DOMFrame'))
                            scripts=[f for f in xml(item).findall('.//{*}DOMFrame') if f.find('./{*}Actionscript') is not None]
                            assert total==1 or any(int(f.get('index'))==0 and 'stop();' in ''.join(f.itertext()) for f in scripts),(item,total)
                        result.extend(draw(item,child,pose,depth+1))
            return result
        mc=xml('#链接/MC_hits');labels={f.get('name'):int(f.get('index'))+1 for f in mc.findall('.//{*}DOMFrame') if f.get('name')}
        assert labels=={'fadin':10,'update':30,'fadout':51}
        frames=[draw('#链接/MC_hits',i) for i in range(56)]
        assert all(len(f)==2 and [r[0] for r in f]==['combo-label','@number'] for f in frames)
        profiles.append(frames)
        if profile=='fight':
            for digit in range(10):
                rows=draw('#链接/MC_hitsNumer',digit);assert len(rows)==1 and rows[0][1:3]==(1.,1.)
                digits.append(rows[0])
    output.parent.mkdir(parents=True,exist_ok=True);pack_images(images,output)
    header.parent.mkdir(parents=True,exist_ok=True)
    num=lambda v:format(float(v),'.17g')
    text='// Generated from pinned HitsUI XFL and PNG geometry.\n// SPDX-License-Identifier: GPL-3.0-or-later\n#pragma once\n#include <array>\nnamespace bvn {\nstruct ComboArtDraw{const char* key;double a,d,x,y,w,h;};\n'
    text+='inline constexpr std::array<ComboArtDraw,10> ComboDigitArt{{\n'+''.join('{"%s",%s},\n'%(r[0],','.join(num(v) for v in r[1:])) for r in digits)+'}};\n'
    for name,frames in zip(['Versus','Musou'],profiles):
        text+=f'inline constexpr std::array<std::array<ComboArtDraw,2>,56> Combo{name}Art{{{{\n'
        text+=''.join('{{'+''.join('{"%s",%s},'%(r[0],','.join(num(v) for v in r[1:])) for r in frame)+'}},\n' for frame in frames)+'}};\n'
    text+='}\n';header.write_text(text)
    source_files=[reference/'upstream/CORE_KernelLogic/src/net/play5d/game/bvn/ui/fight/HitsUI.as',reference/'libraries/LIB_KyoLib/src/net/play5d/kyo/display/MCNumber.as']
    files.update(source_files)
    report={'frames_per_profile':56,'profiles':profiles,'digits':digits,'sprites':len(images),'asset_sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'header_sha256':hashlib.sha256(header.read_bytes()).hexdigest(),'sourceHashes':{str(p.relative_to(reference)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(files)},'scope':'Pinned original HitsUI labels/stops, axis-aligned classic tween sampling with declared quadratic easing, initial stopped nested movie clips, raw PNG digit bounds and runtime MCNumber insertion. PNGs use existing Lanczos/4444 native sampling. Not Flash VM, arbitrary MovieClip construction/broadcast or Flash raster equivalence.'}
    output.with_suffix('.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n');print('Combo UI',len(images),'sprites',output.stat().st_size,'bytes')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path);p.add_argument('header',type=Path);p.add_argument('--reference',type=Path,default=Path('reference'));a=p.parse_args();build(a.reference,a.output,a.header)
