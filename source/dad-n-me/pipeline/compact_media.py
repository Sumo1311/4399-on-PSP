"""Lossless R3 recoding: exact T4 palettes, duplicate tile payloads, mono PCM.

This does not quantize colors, resample audio, change geometry, or remove sounds.
The input banks are the preserved R2 banks. Bundled LZ4 supplies the encoder.
"""
from pathlib import Path
import argparse,array,ctypes,hashlib,json,struct,subprocess,sys

def sha(data):return hashlib.sha256(data).hexdigest()
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--art-source',type=Path,required=True)
    ap.add_argument('--audio-source',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--expected-art-sha256',default='4f3f5bdfbe2d150715288a4604a8a34ad8b2e36a9a98fffb822bac6dc9cf35d5')
    a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    source=Path(__file__).resolve().parents[1]
    libpath=a.output/('liblz4.dylib' if sys.platform=='darwin' else 'liblz4.so')
    subprocess.run(['clang','-O2']+(['-dynamiclib'] if sys.platform=='darwin' else ['-shared','-fPIC'])+
                   [str(source/'vendor/lz4/lz4.c'),'-o',str(libpath)],check=True)
    lib=ctypes.CDLL(str(libpath.resolve()))
    for name in ['LZ4_compress_default','LZ4_decompress_safe']:
        fn=getattr(lib,name);fn.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int];fn.restype=ctypes.c_int
    lib.LZ4_compressBound.argtypes=[ctypes.c_int];lib.LZ4_compressBound.restype=ctypes.c_int
    bank=a.art_source.read_bytes();header=list(struct.unpack_from('<5I',bank));assert header[1]==4
    assert sha(bank)==a.expected_art_sha256
    entries=bank[20:20+header[2]*16];tiles=[];payloads=[];unique={};aliases=[]
    decoded_before=decoded_after=unique_bytes=converted=0
    for index in range(header[3]):
        t=list(struct.unpack_from('<II4H4fI',bank,20+header[2]*16+index*36));n=t[2]*t[3];fmt=t[10]
        size=((n+63)&~63)+1024 if fmt&1 else n*4
        buf=ctypes.create_string_buffer(size)
        assert lib.LZ4_decompress_safe(bank[t[0]:t[0]+t[1]],buf,t[1],size)==size
        raw=buf.raw;decoded_before+=size
        if fmt&1:
            begin=(n+63)&~63
            pixels=b''.join(raw[begin+i*4:begin+i*4+4] for i in raw[:n])
        else:pixels=raw
        colors={}
        for v, in struct.iter_unpack('<I',pixels):
            if v not in colors:
                if len(colors)==16:colors={};break
                colors[v]=len(colors)
        if colors and t[2]>=32:
            begin=(n//2+63)&~63
            if begin+64<size:
                coded=bytearray(begin+64);values=list(struct.iter_unpack('<I',pixels))
                for i in range(0,n,2):coded[i//2]=colors[values[i][0]]|(colors[values[i+1][0]]<<4)
                for color,at in colors.items():struct.pack_into('<I',coded,begin+at*4,color)
                restored=b''.join(coded[begin+((coded[i//2]>>(4*(i&1)))&15)*4:begin+((coded[i//2]>>(4*(i&1)))&15)*4+4] for i in range(n))
                assert restored==pixels
                raw=bytes(coded);fmt|=9;converted+=1
        decoded_after+=len(raw)
        encoded=ctypes.create_string_buffer(lib.LZ4_compressBound(len(raw)))
        length=lib.LZ4_compress_default(raw,encoded,len(raw),len(encoded));assert length>0
        packed=encoded.raw[:length];key=(t[2],t[3],fmt&~4,raw)
        if key not in unique:
            unique[key]=len(payloads);payloads.append(packed);unique_bytes+=len(raw)
        aliases.append(unique[key]);t[0]=0;t[1]=length;t[10]=fmt;tiles.append(t)
    offset=20+len(entries)+len(tiles)*36;offsets=[]
    for encoded in payloads:offsets.append(offset);offset+=len(encoded)
    for t,alias in zip(tiles,aliases):t[0]=offsets[alias]
    header[1]=5
    art=struct.pack('<5I',*header)+entries+b''.join(struct.pack('<II4H4fI',*t) for t in tiles)+b''.join(payloads)
    (a.output/'art.pak').write_bytes(art)
    old_audio=a.audio_source.read_bytes();magic,count=struct.unpack_from('<II',old_audio)
    assert sha(old_audio)=='50ee0636e24dc9f2764d1b52eb90c981b5cbdaa5a47caaf8c0127dfac3eb4f91'
    records=[];chunks=[];offset=8+count*24;mono=0;audio_samples=0
    for i in range(count):
        record=list(struct.unpack_from('<6I',old_audio,8+i*24));raw=old_audio[record[0]:record[0]+record[1]*4]
        assert len(raw)==record[1]*4
        values=array.array('h',raw);left=values[::2];right=values[1::2];packed=raw
        if raw and left==right:
            packed=left.tobytes();record[4]|=1<<24;mono+=1
            restored=array.array('h');restored.extend(v for sample in left for v in (sample,sample));assert restored.tobytes()==raw
        record[0]=offset;offset+=len(packed);audio_samples+=record[1];records.append(record);chunks.append(packed)
    audio=struct.pack('<II',magic,count)+b''.join(struct.pack('<6I',*r) for r in records)+b''.join(chunks)
    (a.output/'audio.pak').write_bytes(audio)
    report={'status':'recoded-roundtrip-checked','art':{'input_sha256':sha(bank),'sha256':sha(art),'tiles':len(tiles),'T4_tiles':converted,'unique_payloads':len(payloads),'decoded_before_total':decoded_before,'decoded_after_total':decoded_after,'unique_decoded_total':unique_bytes,'bank_before':len(bank),'bank_after':len(art),'pixel_error':0,'geometry_unchanged':True},
            'audio':{'input_sha256':sha(old_audio),'sha256':sha(audio),'records':count,'mono16_records':mono,'frames':audio_samples,'bank_before':len(old_audio),'bank_after':len(audio),'expanded_pcm_error':0,'rates_samples_channels_and_order_unchanged':True},
            'scope':'Cumulative asset sizes, not simultaneous RAM. Decoder and PSP native rendering checks are separate.'}
    (a.output/'MEDIA_RECODE.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
