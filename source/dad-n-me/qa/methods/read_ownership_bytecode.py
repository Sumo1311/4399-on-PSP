from pathlib import Path
import sys,struct,json,hashlib
R=Path(__file__).resolve().parents[1];sys.path.insert(0,str(R/'source-measure/pipeline'));from swf import rectangle,tags,text_at
raw=(R/'source-response55/assets/source.swf').read_bytes();assert raw[:3]==b'FWS';_,at=rectangle(raw,8);at+=4
sprite=next(body for code,body in tags(raw,at) if code==39 and struct.unpack_from('<H',body)[0]==1745)
code=next(body for kind,body in tags(sprite,4) if kind==12)
pool=[];pc=0;found=None
while pc<len(code):
 op=code[pc];n=struct.unpack_from('<H',code,pc+1)[0] if op>=128 else 0;begin=pc+(3 if op>=128 else 1);end=begin+n;data=code[begin:end]
 if op==0x88:
  count=struct.unpack_from('<H',data)[0];pool=[];i=2
  for _ in range(count):name,i=text_at(data,i);pool.append(name)
 if op==0x9b:
  name,i=text_at(data,0);count=struct.unpack_from('<H',data,i)[0];i+=2
  for _ in range(count):_,i=text_at(data,i)
  length=struct.unpack_from('<H',data,i)[0]
  if name=='f_Grab':found=(end,length,list(pool));break
  end+=length
 if op==0:break
 pc=end
assert found and found[:2]==(21699,777),found[:2] if found else None
start,length,pool=found;body=code[start:start+length];pc=0;previous_push=[];events=[];literal_nulls=0
while pc<len(body):
 op=body[pc];n=struct.unpack_from('<H',body,pc+1)[0] if op>=128 else 0;begin=pc+(3 if op>=128 else 1);end=begin+n;data=body[begin:end]
 if op==0x96:
  i=0;previous_push=[]
  while i<len(data):
   kind=data[i];i+=1
   if kind==0:value,i=text_at(data,i)
   elif kind==1:value=struct.unpack_from('<f',data,i)[0];i+=4
   elif kind==2:value='literal-null';literal_nulls+=1
   elif kind==3:value='literal-undefined'
   elif kind==4:value='register'+str(data[i]);i+=1
   elif kind==5:value=bool(data[i]);i+=1
   elif kind==6:value='double';i+=8
   elif kind==7:value=struct.unpack_from('<i',data,i)[0];i+=4
   elif kind==8:value=pool[data[i]];i+=1
   elif kind==9:value=pool[struct.unpack_from('<H',data,i)[0]];i+=2
   else:raise ValueError(kind)
   previous_push.append(dict(type=kind,value=value))
 elif op==0x1c and previous_push and previous_push[-1]['value']=='NULL':events.append(dict(body_pc=pc,action_buffer_pc=start+pc,opcode='ActionGetVariable 0x1c',pushed_values=previous_push))
 if op==0:break
 pc=end
assert len(events)==4 and literal_nulls==0,(events,literal_nulls)
report=dict(status='verified-original-bytecode',source_sha256=hashlib.sha256(raw).hexdigest(),sprite=1745,function='f_Grab',body_start=start,body_length=length,NULL_variable_reads=events,literal_null_pushes=literal_nulls,meaning='Both comparisons and both ownership clears resolve the AS1 variable NULL;native literal-null stores do not preserve source type')
(R/'work-r23/OWNERSHIP_BYTECODE_EVIDENCE.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
