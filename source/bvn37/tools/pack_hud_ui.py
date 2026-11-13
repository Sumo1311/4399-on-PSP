#!/usr/bin/env python3
"""Pack untouched pinned HUD bitmap frames missing from the native interface."""
from pathlib import Path
import argparse,json,hashlib
from PIL import Image
from pack_versus_ui import pack_images,SCALE
from build_musou_presentation import build as build_musou
FILES={'hp-red':'位图 3.png','energy-low':'energy1.png','energy-bleach-low':'spiritual1.png','energy-naruto-low':'chakra1.png','qi-max':'qbar_bar4.png'}
def build(reference,output):
 root=reference/'flash-source/SRC_XFL/ui/fight/LIBRARY';images={};source={}
 for key,name in FILES.items():
  p=root/name;source[str(p.relative_to(reference))]=hashlib.sha256(p.read_bytes()).hexdigest()
  with Image.open(p) as im:images[key]=im.convert('RGBA').resize((round(im.width*SCALE),round(im.height*SCALE)),Image.Resampling.LANCZOS)
 cue=build_musou(reference,output.parent/'musou_cue_data.hpp')
 for name,key in cue['images'].items():
  p=reference/'flash-source/SRC_XFL/ui/musou/LIBRARY'/name
  source[str(p.relative_to(reference))]=hashlib.sha256(p.read_bytes()).hexdigest()
  with Image.open(p) as im:
   scale=min(SCALE,512/max(im.size))
   images[key]=im.convert('RGBA').resize((round(im.width*scale),round(im.height*scale)),Image.Resampling.LANCZOS)
 output.parent.mkdir(parents=True,exist_ok=True);pack_images(images,output)
 output.with_suffix('.json').write_text(json.dumps({'sprites':FILES,'musouSprites':cue['images'],'musouSourceHashes':cue['sourceHashes'],'textureSampling':'Lanczos at 272/600, capped at 512 pixels for the one 1203x820 warning leaf. Authored raw leaf dimensions remain unchanged in draw geometry.','sourceHashes':source,'sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'scope':'Pinned original PNG frames resized and packed with the existing declared native HUD sampling/4444 path; not Flash raster parity.'},ensure_ascii=False,indent=2)+'\n')
 print('HUD sprites',len(images),'bytes',output.stat().st_size)
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path);p.add_argument('--reference',type=Path,default=Path('reference'));a=p.parse_args();build(a.reference,a.output)
