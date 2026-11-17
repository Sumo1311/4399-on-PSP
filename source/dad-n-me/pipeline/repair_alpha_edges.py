"""Repair transparent RGB before original-parent subdivision; retain alpha and every visible source texel."""
from pathlib import Path
import argparse,subprocess,json,hashlib,struct,zlib,shutil
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--audio-source',type=Path,required=True);a=p.parse_args()
r=Path(__file__).resolve().parent;o=a.output.resolve();o.mkdir(exist_ok=False,parents=True)
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
original=r/'source-original-art.pak';assert sha(original)=='5f8aabd1a9f79a635967ceec0bcea0aae2d1dbd369e7d7b59cde9fcdeeef81ca'
def run(cmd):return subprocess.run([str(v) for v in cmd],text=True,capture_output=True,check=True)
run(['clang++','-std=c++17','-O2',r/'repair_alpha_edges.cpp','-lz','-o',o/'repair-alpha'])
fixed=o/'alpha-bleed-v1.pak';repair=json.loads(run([o/'repair-alpha',original,fixed]).stdout)
# Independent reader: all metadata, alpha and nontransparent RGBA must match.
old,new=original.read_bytes(),fixed.read_bytes();oh=struct.unpack_from('<5I',old);nh=struct.unpack_from('<5I',new);assert oh==nh
assert old[20:20+oh[2]*16]==new[20:20+nh[2]*16];checked=changed=0
for i in range(oh[3]):
 at=20+oh[2]*16+i*32;t=struct.unpack_from('<II4H4f',old,at);u=struct.unpack_from('<II4H4f',new,at);assert t[2:]==u[2:]
 x=zlib.decompress(old[t[0]:t[0]+t[1]]);y=zlib.decompress(new[u[0]:u[0]+u[1]]);assert len(x)==len(y)
 for off in range(0,len(x),4):
  assert x[off+3]==y[off+3]
  if x[off+3]:assert x[off:off+4]==y[off:off+4]
  elif x[off:off+3]!=y[off:off+3]:changed+=1
  checked+=1
assert changed==repair['changed_transparent_rgb_pixels']
run(['clang','-O2','-c',r.parent/'vendor/lz4/lz4.c','-o',o/'lz4.o']);run(['clang++','-std=c++17','-O2',r/'pack_tiles.cpp',o/'lz4.o','-lz','-o',o/'pack-tiles'])
v4=o/'tiles-v4';v4.mkdir();packed=json.loads(run([o/'pack-tiles',fixed,v4/'art.pak',v4/'mapping.csv']).stdout)
run(['python3',r/'compact_media.py','--art-source',v4/'art.pak','--expected-art-sha256',sha(v4/'art.pak'),'--audio-source',a.audio_source,'--output',o/'compact'])
run(['python3',r/'swizzle_media.py','--r3-media',o/'compact','--output',o/'swizzled'])
final=o/'swizzled';report=json.loads((final/'MEDIA_RECODE.json').read_text())
report['art']['intermediate_input_sha256']=report['art']['input_sha256'];report['art']['input_sha256']='4f3f5bdfbe2d150715288a4604a8a34ad8b2e36a9a98fffb822bac6dc9cf35d5'
report['art']['transparent_rgb_repair']=dict(repair,independently_checked_source_pixels=checked,source_v1_sha256=sha(original),repaired_v1_sha256=sha(fixed),scope='Transparent RGB deliberately changed to prevent black bilinear fringes. Alpha, visible source pixels, tile geometry and order preserved; native rendering validation is separate.')
(final/'MEDIA_RECODE.json').write_text(json.dumps(report,indent=2)+'\n');(o/'ALPHA_EDGE_REPAIR.json').write_text(json.dumps(dict(repair=report['art']['transparent_rgb_repair'],packed=packed,art_sha256=sha(final/'art.pak')),indent=2)+'\n')
print(json.dumps(dict(status='offline-verified-native-rendering-pending',media=str(final),repair=repair,art_sha256=sha(final/'art.pak')),indent=2))
