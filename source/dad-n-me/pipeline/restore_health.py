"""User-requested ordinary100HP health; preserve all source instructions and damage rules."""
from pathlib import Path
import hashlib,json,struct,argparse
from fix_source import instructions,push_values
from swf import rectangle,text_at
BASE='1ae0ff67bfae8692e139990d338fda987cf3c78ab17bf6289a09762db16715b0'
def patch(raw,health=100):
 assert hashlib.sha256(raw).hexdigest()==BASE and raw[:3]==b'FWS'
 result=bytearray(raw);changes=[];_,offset=rectangle(raw,8);offset+=4
 def walk(start,stop,context):
  pos=start
  while pos<stop:
   word=struct.unpack_from('<H',raw,pos)[0];pos+=2;kind,size=word>>6,word&63
   if size==63:size=struct.unpack_from('<I',raw,pos)[0];pos+=4
   end=pos+size;assert end<=stop
   if kind==39:walk(pos+4,end,struct.unpack_from('<H',raw,pos)[0])
   elif kind==12 and context==1745:
    for op,_,pay,last in instructions(raw[pos:end]):
     if op!=0x9b:continue
     name,at=text_at(raw,pos+pay);count=struct.unpack_from('<H',raw,at)[0];at+=2
     for _ in range(count):_,at=text_at(raw,at)
     length=struct.unpack_from('<H',raw,at)[0];at+=2
     if name!='f_SpawnPlayer':continue
     code=raw[at:at+length]
     matches=[(at+off,t,width) for opcode,_,p,e in instructions(code) if opcode==0x96 for t,off,width,value in push_values(code,p,e) if t in (6,7) and value==1000000]
     assert len(matches)==1
     target,t,width=matches[0];encoded=struct.pack('<i',health) if t==7 else struct.pack('<d',health)
     if t==6:encoded=encoded[4:]+encoded[:4]
     result[target:target+width]=encoded
     changes.append(dict(sprite=context,function=name,field='dude1.health_max',before=1000000,after=health,offset=target,width=width))
   pos=end
   if kind==0:break
 walk(offset,len(raw),0);assert len(changes)==1 and len(raw)==len(result)
 changed=[i for i,(a,b) in enumerate(zip(raw,result)) if a!=b]
 assert all(changes[0]['offset']<=i<changes[0]['offset']+changes[0]['width'] for i in changed)
 report=dict(scope='User requested visible damage and normal finite health.100HP is an explicit candidate balance value, not a claim of an independently recovered official maximum.',base_sha256=BASE,output_sha256=hashlib.sha256(result).hexdigest(),changes=changes,changed_file_offsets=changed,damage_hit_rules_unchanged=True,rage_and_attack_values_unchanged=True)
 return bytes(result),report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--input',type=Path,default=Path(__file__).with_name('source-before-health.swf'));p.add_argument('--output',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 data,report=patch(a.input.read_bytes());a.output.write_bytes(data);a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
