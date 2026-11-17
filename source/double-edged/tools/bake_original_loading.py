"""Bake the source load_message_570 glyphs for display before VM/art-bank init."""
from pathlib import Path
import argparse,ctypes,hashlib,json,struct
from PIL import Image
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('art',type=Path);parser.add_argument('tree',type=Path);parser.add_argument('header',type=Path);parser.add_argument('--preview',type=Path,required=True)
a=parser.parse_args();bank=a.art.read_bytes();tree=json.loads(a.tree.read_text())
H=struct.Struct('<5I');E=struct.Struct('<IHHIIf');T=struct.Struct('<IIHHHHffffHHHHHH');h=H.unpack_from(bank);assert h[:2]==(0x52414e44,7)
entries=[E.unpack_from(bank,H.size+i*E.size) for i in range(h[2])];tiles=[T.unpack_from(bank,H.size+h[2]*E.size+i*T.size) for i in range(h[3])]
lib=ctypes.CDLL('/opt/homebrew/opt/lz4/lib/liblz4.dylib');lib.LZ4_decompress_safe.argtypes=[ctypes.c_char_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int]
def rgba(t):
    count=t[2]*t[3];fmt,n=t[-2:];size=count*4 if fmt==0 else count if fmt==1 else count//2
    out=ctypes.create_string_buffer(size+n*4);assert lib.LZ4_decompress_safe(bank[t[0]:t[0]+t[1]],out,t[1],len(out))==len(out)
    if fmt==0:return out.raw
    palette=struct.unpack_from('<'+str(n)+'I',out.raw,size);indices=out.raw[:size] if fmt==1 else [(out.raw[i//2]>>((i%2)*4))&15 for i in range(count)]
    return b''.join(struct.pack('<I',palette[i]) for i in indices)
def mul(a,b):return [a[0]*b[0]+a[2]*b[1],a[1]*b[0]+a[3]*b[1],a[0]*b[2]+a[2]*b[3],a[1]*b[2]+a[3]*b[3],a[0]*b[4]+a[2]*b[5]+a[4],a[1]*b[4]+a[3]*b[5]+a[5]]
canvas=Image.new('RGBA',(480,272),(0,0,0,255));glyphs=[]
def walk(n,parent,pc):
    if not n['visible']:return
    mat=mul(parent,n['matrix']);col=[pc[i]*n['color'][i]/256 for i in range(4)]+[pc[i]*n['color'][i+4]/256+pc[i+4] for i in range(4)]
    if n.get('children'):
        for c in n['children']:walk(c,mat,col)
    elif n['symbol']>=0 and n['kind']==2:
        e=max((e for e in entries if e[0]==n['symbol'] and e[1]==0),key=lambda e:e[5]);glyphs.append(dict(symbol=n['symbol'],matrix=mat,color=col,lod=e[2],density=e[5]))
        for i in range(e[3],e[3]+e[4]):
            t=tiles[i];im=Image.frombytes('RGBA',(t[2],t[3]),rgba(t)).crop(tuple(t[10:14]));w,height=im.size
            lut=[]
            for channel in range(4):lut.extend(int(max(0,min(255,v*col[channel]/256+col[channel+4]))) for v in range(256))
            im=im.point(lut);x0,x1,x2,x3,x4,x5=mul(mat,[(t[8]-t[6])/20/w,0,0,(t[9]-t[7])/20/height,t[6]/20,t[7]/20]);det=x0*x3-x1*x2
            inv=(x3/det,-x2/det,(x2*x5-x3*x4)/det,-x1/det,x0/det,(x1*x4-x0*x5)/det)
            canvas.alpha_composite(im.transform(canvas.size,Image.Transform.AFFINE,inv,resample=Image.Resampling.BILINEAR))
assert tree['symbol']==3340
walk(tree,[.68,0,0,.68,53,0],[256,256,256,256,0,0,0,0]);assert len(glyphs)==10
bounds=canvas.convert('RGB').getbbox();left,top,right,bottom=bounds;width,height=right-left,bottom-top;tw=1<<(width-1).bit_length();th=1<<(height-1).bit_length()
texture=Image.new('RGBA',(tw,th),(0,0,0,255));texture.paste(canvas.crop(bounds),(0,0));raw=texture.tobytes();words=struct.unpack('<'+str(tw*th)+'I',raw)
text='#pragma once\n#include <cstdint>\n// Original load_message_570,source glyphs and transforms;see bake manifest.\nnamespace deoriginal {\n'
text+=f'constexpr int Left={left},Top={top},Width={width},Height={height},TextureWidth={tw},TextureHeight={th};\n'
text+=f'alignas(16) inline const std::uint32_t Pixels[{len(words)}]={{\n'
text+=''.join('    '+','.join(f'0x{v:08x}u' for v in words[i:i+16])+',\n' for i in range(0,len(words),16))+'};\n}\n'
a.header.write_text(text);canvas.save(a.preview)
manifest=dict(scope='Source loading symbol3340 at original550x400 screen center,existing bitmap glyphs from art.pak;no new font or animation. Offline resampling to PSP480x272;native scanout verified separately.',art_sha256=hashlib.sha256(bank).hexdigest(),source_tree_sha256=hashlib.sha256(a.tree.read_bytes()).hexdigest(),bounds=list(bounds),texture_bytes=len(raw),texture_rgba_sha256=hashlib.sha256(raw).hexdigest(),preview_sha256=hashlib.sha256(a.preview.read_bytes()).hexdigest(),glyphs=glyphs)
a.header.with_suffix('.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps({k:v for k,v in manifest.items() if k!='glyphs'},indent=2))
