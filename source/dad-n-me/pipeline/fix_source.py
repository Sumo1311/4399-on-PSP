"""Apply narrowly audited playability repairs to a separate runtime SWF.

The supplied original and media-stripped faithful runtime are never overwritten.
Each replacement is tied to a named AVM1 function and an exact instruction.
"""
import hashlib
import json
from pathlib import Path
import struct
import argparse
import zlib
from swf import tags,tag,rectangle,text_at

ROOT=Path(__file__).resolve().parents[1]

def instructions(code):
    offset=0
    while offset<len(code):
        op=code[offset];start=offset;offset+=1
        size=0
        if op>=128:size=struct.unpack_from('<H',code,offset)[0];offset+=2
        payload=offset
        offset+=size
        if offset>len(code):raise ValueError('Truncated AVM1 instruction')
        yield op,start,payload,offset
        if op==0:break

def push_values(code,start,end):
    """Return type, byte offset, width and value for every ActionPush operand."""
    pos=start
    while pos<end:
        kind=code[pos];pos+=1;at=pos;size=0;value=None
        if kind==0:value,pos=text_at(code,pos);size=pos-at
        elif kind in (2,3):pass
        elif kind in (4,5,8):size=1;value=code[pos]
        elif kind==9:size=2;value=struct.unpack_from('<H',code,pos)[0]
        elif kind==7:size=4;value=struct.unpack_from('<i',code,pos)[0]
        elif kind==1:size=4;value=struct.unpack_from('<f',code,pos)[0]
        elif kind==6:
            size=8;data=bytes(code[pos:pos+8]);value=struct.unpack('<d',data[4:]+data[:4])[0]
        else:raise ValueError(f'Unknown ActionPush operand {kind}')
        if kind!=0:pos+=size
        if pos>end:raise ValueError('Truncated ActionPush operand')
        yield kind,at,size,value

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--reference',action='store_true')
    args=ap.parse_args()
    source=ROOT/('reference/source.swf' if args.reference else 'data/runtime.swf');raw=source.read_bytes()
    if raw[:3]==b'CWS':raw=b'FWS'+raw[3:8]+zlib.decompress(raw[8:])
    if raw[:3]!=b'FWS':raise ValueError('Expected an uncompressed runtime')
    body=raw[8:];_,offset=rectangle(body);offset+=4
    changes=[]
    def actions(data,context):
        data=bytearray(data)
        for op,start,payload,end in instructions(data):
            if op!=0x9b:continue
            name,pos=text_at(data,payload)
            params=struct.unpack_from('<H',data,pos)[0];pos+=2
            for _ in range(params):_,pos=text_at(data,pos)
            count=struct.unpack_from('<H',data,pos)[0];pos+=2
            if context==1745 and name=='f_KidGroup2':
                code=data[pos:pos+count];candidates=[]
                for kind,at,pay,stop in instructions(code):
                    if kind==0x96 and code[pay:stop]==b'\x07\x04\0\0\0':candidates.append(pos+pay+1)
                if len(candidates)!=1:raise ValueError(f'Expected one literal 4 in {name}, got {len(candidates)}')
                target=candidates[0]
                data[target:target+4]=struct.pack('<I',3)
                changes.append(dict(sprite=context,function=name,instruction='ActionPush int32',before=4,after=3,
                    action_payload_offset=target,reason='Three enemies are spawned; the supplied SWF requires four defeats and stalls in both reference and native runs.',
                    reference_evidence='work/reference-campaign-bot-02/result.json',native_evidence='work/campaign-04/result.json'))
        return bytes(data)
    result=bytearray(raw)
    def transform(start,stop,context):
        pos=start
        while pos<stop:
            word=struct.unpack_from('<H',raw,pos)[0];pos+=2
            kind,size=word>>6,word&63
            if size==63:size=struct.unpack_from('<I',raw,pos)[0];pos+=4
            end=pos+size
            if end>stop:raise ValueError('Truncated SWF tag')
            if kind==39:
                sprite=struct.unpack_from('<H',raw,pos)[0]
                transform(pos+4,end,sprite)
            elif kind==12:result[pos:end]=actions(raw[pos:end],context)
            pos=end
            if kind==0:break
    transform(8+offset,len(raw),0)
    result=bytes(result)
    if len(changes)!=1 or len(result)!=len(raw):raise ValueError('Unexpected repair count or SWF length')
    # This repair changes exactly one byte, in one integer literal.
    offsets=[i for i,(a,b) in enumerate(zip(raw,result)) if a!=b]
    if len(offsets)!=1:raise ValueError(f'Unexpected byte changes: {offsets}')
    target=ROOT/('work/reference-playable.swf' if args.reference else 'data/runtime-playable.swf');target.write_bytes(result)
    report=dict(original_source_sha256=json.loads((ROOT/'data/source.json').read_text())['sha256'],
        faithful_runtime_sha256=hashlib.sha256(raw).hexdigest(),playable_runtime_sha256=hashlib.sha256(result).hexdigest(),
        changed_file_offsets=offsets,changes=changes)
    (ROOT/('work/reference-source-fixes.json' if args.reference else 'data/source-fixes.json')).write_text(json.dumps(report,indent=2)+'\n')
    print(f'Created separate playable runtime: one instruction, {len(offsets)} byte changed')

if __name__=='__main__':main()
