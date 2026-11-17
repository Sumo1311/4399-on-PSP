"""Repack XMB metadata/artwork while preserving the exact tested executable."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import zlib

NAMES = ['PARAM.SFO','ICON0.PNG','ICON1.PMF','PIC0.PNG','PIC1.PNG','SND0.AT3','DATA.PSP','DATA.PSAR']

def digest(data):
    return hashlib.sha256(data).hexdigest()

def sections(data):
    assert data[:4] == b'\0PBP' and len(data)>=40
    offsets=list(struct.unpack_from('<8I',data,8))+[len(data)]
    assert offsets[0]==40 and offsets==sorted(offsets)
    return [data[offsets[i]:offsets[i+1]] for i in range(8)]

def png(data, width, height):
    assert data[:8]==b'\x89PNG\r\n\x1a\n'
    w,h,depth,kind,compression,filtering,interlace=struct.unpack('>IIBBBBB',data[16:29])
    assert (w,h,depth,kind,compression,filtering,interlace)==(width,height,8,2,0,0,0)
    offset=8
    while offset<len(data):
        size=struct.unpack_from('>I',data,offset)[0]
        chunk=data[offset+4:offset+8+size]
        assert zlib.crc32(chunk)==struct.unpack_from('>I',data,offset+8+size)[0]
        offset+=12+size
        if chunk[:4]==b'IEND':
            break
    assert offset==len(data)

def sfo_values(data):
    magic,version,keys,values,count=struct.unpack_from('<4s4I',data)
    assert magic==b'\0PSF'
    result={}
    for i in range(count):
        key,fmt,size,maximum,offset=struct.unpack_from('<HHIII',data,20+i*16)
        name=data[keys+key:data.index(0,keys+key)].decode()
        raw=data[values+offset:values+offset+size]
        result[name]=struct.unpack('<I',raw)[0] if fmt==0x404 else raw.rstrip(b'\0').decode()
    return result

def repack(original, icon, background):
    parts=sections(original)
    png(icon,144,80);png(background,480,272)
    before=sfo_values(parts[0])
    sfo=bytearray(parts[0]);_,_,keys,values,count=struct.unpack_from('<4s4I',sfo)
    replacements={'TITLE':'狂扁小朋友','APP_VER':'01.03'}
    found=set()
    for i in range(count):
        entry=20+i*16
        key,fmt,size,maximum,offset=struct.unpack_from('<HHIII',sfo,entry)
        name=sfo[keys+key:sfo.index(0,keys+key)].decode()
        if name in replacements:
            raw=replacements[name].encode()+b'\0'
            assert fmt==0x204 and len(raw)<=maximum
            sfo[values+offset:values+offset+maximum]=raw.ljust(maximum,b'\0')
            struct.pack_into('<I',sfo,entry+4,len(raw));found.add(name)
    assert found==set(replacements)
    parts[0]=bytes(sfo);parts[1]=icon;parts[4]=background
    offsets=[];offset=40
    for part in parts:
        offsets.append(offset);offset+=len(part)
    result=original[:8]+struct.pack('<8I',*offsets)+b''.join(parts)
    after=sfo_values(parts[0])
    assert before['MEMSIZE']==after['MEMSIZE']==1
    assert {k:v for k,v in before.items() if k not in replacements}=={k:v for k,v in after.items() if k not in replacements}
    old=sections(original);new=sections(result)
    assert all(old[i]==new[i] for i in [2,3,5,6,7])
    return result,dict(candidate_eboot_sha256=digest(original),formal_eboot_sha256=digest(result),
        data_psp_sha256=digest(new[6]),sfo_before=before,sfo_after=after,
        sections=[dict(name=NAMES[i],bytes=len(new[i]),sha256=digest(new[i]),same_as_candidate=new[i]==old[i])for i in range(8)],
        change_scope='PARAM.SFO TITLE/APP_VER,ICON0 andPIC1 only;DATA.PSP/DATA.PSAR andotherPBPsections exact')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--artwork',type=Path,required=True);p.add_argument('--receipt',type=Path)
    a=p.parse_args();data,receipt=repack(a.input.read_bytes(),(a.artwork/'ICON0.png').read_bytes(),(a.artwork/'PIC1.png').read_bytes())
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(data)
    if a.receipt:a.receipt.write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(receipt,ensure_ascii=False,indent=2))

if __name__=='__main__':
    main()
