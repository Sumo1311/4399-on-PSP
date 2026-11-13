#!/usr/bin/env python3
"""Restore small fighter rasters from matching original PNGs at twice density.

Only the texture table/payload change. Logical poses, timings, collision tables
and the original metadata identity are retained. BVNFGHxx/BVNASSTH add an
eight-byte little-endian original metadata identity after the texture payload.
"""
import argparse,ctypes,ctypes.util,hashlib,json,os,struct,zlib
from pathlib import Path
from PIL import Image

TEXTURE=struct.Struct('<3I')
LIMIT=32*1024*1024
def load_lz4():
    lib=ctypes.CDLL(os.environ.get('LZ4_LIBRARY') or ctypes.util.find_library('lz4') or '/opt/homebrew/opt/lz4/lib/liblz4.dylib')
    lib.LZ4_decompress_safe.argtypes=[ctypes.c_char_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int]
    lib.LZ4_compress_default.argtypes=[ctypes.c_char_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int]
    lib.LZ4_compressBound.argtypes=[ctypes.c_int]
    return lib
def table(data):
    start=struct.unpack_from('<I',data,20)[0]
    count,tiles=struct.unpack_from('<2I',data,44)
    valid=[]
    for pad in range(16):
        first=start-pad-tiles*20-count*12
        if first<52 or any(data[start-pad:start]):continue
        entries=[TEXTURE.unpack_from(data,first+i*12) for i in range(count)]
        position=0
        for offset,packed,raw in entries:
            if offset!=position or not packed or not raw or raw>512*512*4:break
            position+=packed
        else:
            if position==len(data)-start:valid.append((start,first,entries))
    if len(valid)!=1:raise ValueError('Ambiguous texture-table layout')
    return valid[0]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['root','index','dimensions','input','output','report']:p.add_argument('--'+name,required=True,type=Path)
    p.add_argument('--probe',action='store_true');a=p.parse_args()
    index=json.loads(a.index.read_text());dims={};identities={}
    for line in a.dimensions.read_text().splitlines():
        fields=line.split('\t')
        if fields[0]=='BANK':identities[fields[1]]=int(fields[2]);dims[fields[1]]={}
        else:dims[fields[1]][int(fields[2])]=tuple(map(int,fields[3:5]))
    lib=load_lz4();report={'density':2,'filter':'premultiplied-alpha Lanczos from original PNG; not enlarged low-resolution rasters','logical_metadata_preserved':True,'banks':{},'promoted_textures':0,'original_textures':0}
    for name,dimensions in dims.items():
        data=(a.input/name).read_bytes();start,first,entries=table(data);lz4=data[:8] in [b'BVNFGT13',b'BVNFGT15']
        payload=[];changed=[];total=sum(x[1] for x in entries)
        for i,(offset,packed,rawSize) in enumerate(entries):
            source=data[start+offset:start+offset+packed];replacement=None
            stride,rows=dimensions.get(i,(0,0))
            if stride and stride<=256 and rows<=256 and 64<rawSize<=128*1024:
                if lz4:
                    out=ctypes.create_string_buffer(rawSize)
                    if lib.LZ4_decompress_safe(source,out,len(source),rawSize)!=rawSize:raise ValueError('LZ4 decode')
                    raw=out.raw
                else:raw=zlib.decompress(source)
                assert len(raw)==rawSize==4*stride*rows
                match=index.get(f'{stride}x{rows}:'+hashlib.sha256(raw).hexdigest())
                if match:
                    path,size,bounds,premultiplied,pngHash=match
                    changed.append({'texture':i,'png':path,'png_sha256':pngHash,'stride':stride,'rows':rows})
                    if not a.probe:
                        encoded=(a.root/path).read_bytes();assert hashlib.sha256(encoded).hexdigest()==pngHash
                        with Image.open(a.root/path) as image:
                            high=image.convert('RGBA').convert('RGBa').resize((size[0]*2,size[1]*2),Image.Resampling.LANCZOS).convert('RGBA')
                        crop=high.crop(tuple(x*2 for x in bounds));padded=Image.new('RGBA',(stride*2,rows*2));padded.paste(crop)
                        highBytes=(padded.convert('RGBa') if premultiplied else padded).tobytes()
                        if lz4:
                            target=ctypes.create_string_buffer(lib.LZ4_compressBound(len(highBytes)))
                            n=lib.LZ4_compress_default(highBytes,target,len(highBytes),len(target));assert n>0
                            replacement=target.raw[:n]
                        else:replacement=zlib.compress(highBytes,1)
                        if total-packed+len(replacement)>LIMIT:
                            changed.pop();replacement=None
                        else:total+=len(replacement)-packed;rawSize=len(highBytes)
            payload.append((replacement if replacement is not None else source,rawSize))
        result={'textures':len(entries),'promoted':len(changed),'original_metadata':str(identities[name]),'original_sha256':hashlib.sha256(data).hexdigest(),'sources':changed}
        if not a.probe:
            if changed:
                header=bytearray(data[:start]);header[:8]=b'BVNASSTH' if data[:8]==b'BVNASST1' else b'BVNFGH'+data[6:8]
                offset=0
                for i,(compressed,rawSize) in enumerate(payload):TEXTURE.pack_into(header,first+i*12,offset,len(compressed),rawSize);offset+=len(compressed)
                assert header[8:first]==data[8:first] and header[first+len(entries)*12:]==data[first+len(entries)*12:start]
                output=bytes(header)+b''.join(x[0] for x in payload)+struct.pack('<Q',identities[name])
            else:output=data
            (a.output/name).write_bytes(output);result['sha256']=hashlib.sha256(output).hexdigest();result['bytes']=len(output)
        report['banks'][name]=result;report['promoted_textures']+=len(changed);report['original_textures']+=len(entries)
        print(name,len(changed),'/',len(entries),flush=True)
    a.report.write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n')
    print('TOTAL',report['promoted_textures'],'/',report['original_textures'],flush=True)
if __name__=='__main__':main()
