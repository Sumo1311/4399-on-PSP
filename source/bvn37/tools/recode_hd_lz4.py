#!/usr/bin/env python3
"""Encode the native-density bank payloads as lossless, fast-decoding LZ4 HC."""
import argparse,ctypes,hashlib,json,os,struct,zlib
from pathlib import Path
from promote_native_textures import table,load_lz4,LIMIT,TEXTURE
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);p.add_argument('dimensions',type=Path);p.add_argument('report',type=Path);a=p.parse_args()
    identities={f[1]:int(f[2]) for line in a.dimensions.read_text().splitlines() if (f:=line.split('\t'))[0]=='BANK'}
    lib=load_lz4();lib.LZ4_compress_HC.argtypes=[ctypes.c_char_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_int]
    results={'codec':'LZ4 HC level 10','all_decoded_rgba_bytes_identical':True,'banks':{}}
    for name,identity in identities.items():
        path=a.folder/name;data=path.read_bytes();magic=data[:8]
        hd=magic.startswith(b'BVNFGH') or magic in [b'BVNASSTH',b'BVNASSTL']
        source=data[:-8] if hd else data
        if hd:assert struct.unpack('<Q',data[-8:])[0]==identity
        start,first,entries=table(source);is_lz4=magic==b'BVNASSTL' or magic[6:8] in [b'13',b'15']
        header=bytearray(source[:start]);header[:8]=b'BVNASSTL' if magic.startswith(b'BVNASST') else b'BVNFGH15'
        payload=bytearray();fingerprint=hashlib.sha256()
        for i,(offset,size,rawSize) in enumerate(entries):
            encoded=source[start+offset:start+offset+size]
            if is_lz4:
                decoded=ctypes.create_string_buffer(rawSize)
                assert lib.LZ4_decompress_safe(encoded,decoded,size,rawSize)==rawSize;raw=decoded.raw
            else:raw=zlib.decompress(encoded)
            assert len(raw)==rawSize;fingerprint.update(raw)
            buffer=ctypes.create_string_buffer(lib.LZ4_compressBound(rawSize));n=lib.LZ4_compress_HC(raw,buffer,rawSize,len(buffer),10);assert n>0
            packed=buffer.raw[:n];verified=ctypes.create_string_buffer(rawSize)
            assert lib.LZ4_decompress_safe(packed,verified,n,rawSize)==rawSize and verified.raw==raw
            TEXTURE.pack_into(header,first+i*12,len(payload),n,rawSize);payload.extend(packed)
        assert len(payload)<=LIMIT,(name,len(payload))
        assert header[8:first]==source[8:first] and header[first+len(entries)*12:]==source[first+len(entries)*12:start]
        output=bytes(header)+bytes(payload)+struct.pack('<Q',identity)
        temporary=path.with_suffix(path.suffix+'.NEW');temporary.write_bytes(output);os.replace(temporary,path)
        results['banks'][name]={'bytes':len(output),'payload_bytes':len(payload),'decoded_sha256':fingerprint.hexdigest(),'sha256':hashlib.sha256(output).hexdigest(),'original_metadata':str(identity),'textures':len(entries)}
        print(name,len(data),'->',len(output),flush=True)
    a.report.write_text(json.dumps(results,indent=2)+'\n')
    print('PASS',len(results['banks']),'banks; identical RGBA; LZ4 payloads <=32MiB',flush=True)
if __name__=='__main__':main()
