from pathlib import Path
import argparse, array, collections, csv, ctypes, hashlib, json, struct, subprocess, sys, zlib

r=Path(__file__).resolve().parent
project=r.parent/'dad_n_me_psp'
p=argparse.ArgumentParser()
p.add_argument('--source',type=Path,default=project/'data/art.pak')
p.add_argument('--output',type=Path,default=r/'tiles25')
p.add_argument('--lz4-source',type=Path,default=project/'vendor/lz4/lz4.c')
a=p.parse_args();out=a.output.resolve()
libpath=out/('liblz4-qa.dylib' if sys.platform=='darwin' else 'liblz4-qa.so')
subprocess.run(['clang','-O2']+(['-dynamiclib'] if sys.platform=='darwin' else ['-shared','-fPIC'])+[a.lz4_source,'-o',libpath],check=True)
lib=ctypes.CDLL(str(libpath))
lib.LZ4_decompress_safe.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int]
lib.LZ4_decompress_safe.restype=ctypes.c_int
original=a.source.read_bytes()
bank=(out/'art.pak').read_bytes()
oh=struct.unpack_from('<5I',original);h=struct.unpack_from('<5I',bank)
assert oh[1]==1 and h[1]==4 and oh[2]==h[2]
ot=[struct.unpack_from('<II4H4f',original,20+oh[2]*16+i*32) for i in range(oh[3])]
nt=[struct.unpack_from('<II4H4fI',bank,20+h[2]*16+i*36) for i in range(h[3])]
mapping=list(csv.DictReader((out/'mapping.csv').open()))
groups=collections.defaultdict(list)
for m in mapping:
    m={k:int(v) for k,v in m.items()};groups[m['parent']].append(m)
assert len(mapping)==h[3] and set(groups)==set(range(oh[3]))
source_pixels=output_pixels=0;max_geometry_error_in_source_pixels=0
for parent,parts in groups.items():
    t=ot[parent]
    raw=zlib.decompress(original[t[0]:t[0]+t[1]])
    covered=bytearray(t[5]*t[4])
    for m in parts:
        n=nt[m['tile']];fmt=n[10]
        assert n[4:6]==(m['width'],m['height']) and bool(fmt&4)==bool(m['gutter'])
        assert n[2]<=128 and n[3]<=128 and fmt in (2,3,6,7)
        size=(((n[2]*n[3]+63)&~63)+1024) if fmt&1 else n[2]*n[3]*4
        encoded=ctypes.create_string_buffer(bank[n[0]:n[0]+n[1]])
        decoded=ctypes.create_string_buffer(size)
        assert lib.LZ4_decompress_safe(encoded,decoded,n[1],size)==size
        if fmt&1:
            indices=decoded.raw[:n[2]*n[3]]
            begin=(n[2]*n[3]+63)&~63
            palette=array.array('I',decoded.raw[begin:begin+1024])
            pixels=array.array('I',(palette[index] for index in indices)).tobytes()
        else:pixels=decoded.raw
        left=m['x']-m['gutter'];right=left+n[2]
        for y in range(n[3]):
            sy=max(0,min(t[3]-1,m['y']+y-m['gutter']))
            row=raw[sy*t[2]*4:(sy+1)*t[2]*4]
            expected=row[:4]*max(0,-left)+row[max(0,left)*4:min(t[2],right)*4]+row[-4:]*max(0,right-t[2])
            assert pixels[y*n[2]*4:(y+1)*n[2]*4]==expected,m
        for y in range(m['y'],m['y']+m['height']):
            begin=y*t[4]+m['x'];end=begin+m['width']
            assert covered[begin:end].count(0)==m['width'];covered[begin:end]=bytes([1])*m['width']
        ideal=[t[6]+t[8]*m['x']/t[4],t[7]+t[9]*m['y']/t[5],t[8]*m['width']/t[4],t[9]*m['height']/t[5]]
        for index,(actual,want) in enumerate(zip(n[6:10],ideal)):
            scale=t[4]/t[8] if index in (0,2) else t[5]/t[9]
            max_geometry_error_in_source_pixels=max(max_geometry_error_in_source_pixels,abs(actual-want)*abs(scale))
        output_pixels+=n[2]*n[3]
    assert covered.count(0)==0,parent
    source_pixels+=len(covered)
for i in range(h[2]):
    a=struct.unpack_from('<IHHII',original,20+i*16);b=struct.unpack_from('<IHHII',bank,20+i*16)
    assert a[:3]==b[:3]
    expected=[]
    for parent in range(a[3],a[3]+a[4]):expected += [m['tile'] for m in groups[parent]]
    assert expected==list(range(b[3],b[3]+b[4]))
report=dict(status='pass',scope='Independent reader checks original-pixel coverage without overlap, original entry identity/order, every new padded RGBA pixel including transparent RGB and filter neighbours, and source geometry mapping. Native rasterization is separate.',source_actual_pixels=source_pixels,output_pixels_including_gutters_and_padding=output_pixels,maximum_rgba_error=0,maximum_geometry_error_in_source_pixels=max_geometry_error_in_source_pixels,source_tiles=oh[3],output_tiles=h[3],art_sha256=hashlib.sha256(bank).hexdigest())
(out/'independent-verification.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
