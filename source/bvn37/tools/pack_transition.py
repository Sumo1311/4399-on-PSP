#!/usr/bin/env python3
"""Pack the pinned XFL transition into lazily decoded PSP 8-bit alpha frames."""
from pathlib import Path
import argparse,hashlib,json,struct,xml.etree.ElementTree as ET,zlib
from PIL import Image
HEADER=struct.Struct('<8sI4H3I');ENTRY=struct.Struct('<4I')
def build(reference,output):
 root=reference/'flash-source/SRC_XFL/ui/common';library=root/'LIBRARY';timeline=library/'#链接/MC_transition.xml'
 labels={};keys={};x=ET.parse(timeline).getroot()
 for layer in x.findall('.//{*}DOMLayer'):
  for f in layer.findall('./{*}frames/{*}DOMFrame'):
   i=int(f.get('index'))
   if f.get('name'):labels[i]=f.get('name')
   elements=f.find('./{*}elements')
   if elements is None or not len(elements):continue
   assert len(elements)==1
   e=elements[0];m=e.find('./{*}matrix/{*}Matrix');matrix={} if m is None else dict(m.attrib)
   assert set(matrix)<= {'tx'}
   keys[i]=(e.get('libraryItemName'),float(matrix.get('tx','0')))
 assert labels=={9:'fadin',25:'stop',44:'fadout',60:'stop'},labels
 assert keys=={9:('元件 2',-3380.),24:('元件 2',-1420.),25:('位图 1.png',0.),44:('元件 2',-1260.),59:('元件 2',580.)},keys
 symbol=ET.parse(library/'元件 2.xml').find('.//{*}DOMBitmapInstance');assert symbol.get('libraryItemName')=='transimg.png'
 src=Image.open(library/'transimg.png').convert('RGBA');solid=Image.open(library/'位图 1.png').convert('RGBA')
 assert src.size==(3474,600) and solid.size==(800,600)
 width,height,stride,rows=363,272,512,512;records=[];packed=bytearray();dedup={};checks=[]
 for frame in [0,*range(9,26),*range(44,61)]:
  tx=None
  if 9<=frame<=24:tx=-3380+(frame-9)*1960/15
  elif 44<=frame<=59:tx=-1260+(frame-44)*1840/15
  if tx is not None:
   image=src.transform((800,600),Image.Transform.AFFINE,(1,0,-tx,0,1,0),Image.Resampling.BILINEAR)
  else:image=solid if frame==25 else Image.new('RGBA',(800,600))
  alpha=image.getchannel('A').resize((width,height),Image.Resampling.LANCZOS)
  pixels=bytearray(stride*rows);raw=alpha.tobytes()
  for y in range(height):pixels[y*stride:y*stride+width]=raw[y*width:(y+1)*width]
  data=bytes(pixels);crc=zlib.crc32(data)
  if data not in dedup:
   part=zlib.compress(data,9);dedup[data]=(len(packed),len(part));packed.extend(part)
  offset,length=dedup[data];records.append((frame,offset,length,crc))
  checks.append({'frame':frame,'crc32':crc,'sha256':hashlib.sha256(data).hexdigest(),'translation_x':tx})
 pixel_start=(HEADER.size+ENTRY.size*len(records)+15)//16*16
 blob=HEADER.pack(b'BVNTRA01',len(records),width,height,stride,rows,pixel_start,pixel_start+len(packed),0)
 blob+=b''.join(ENTRY.pack(*r) for r in records);blob+=bytes(pixel_start-len(blob));blob+=packed
 output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(blob)
 paths=[timeline,library/'元件 2.xml',library/'transimg.png',library/'位图 1.png']
 report={'frames':len(records),'width':width,'height':height,'stride':stride,'rows':rows,'resident_alpha_bytes':stride*rows,'packed_bytes':len(blob),'sha256':hashlib.sha256(blob).hexdigest(),'sourceHashes':{str(p.relative_to(reference)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},'scope':'Pinned XFL translation keys and PNG alpha. Declared bilinear subpixel translation plus Lanczos 800x600-to-363x272 alpha sampling; lossless 8-bit alpha payload, zlib and 256-entry black-alpha PSP palette. No Flash VM/raster equivalence claim.','checks':checks}
 output.with_suffix('.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
 output.with_suffix('.IN').write_text(str(len(records))+'\n'+''.join(f'{r[0]} {r[3]}\n' for r in records))
 print('Transition:',len(records),'frames,',len(blob),'bytes,',stride*rows,'resident alpha bytes')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path);p.add_argument('--reference',type=Path,default=Path('reference'));a=p.parse_args();build(a.reference,a.output)
