# SPDX-License-Identifier: GPL-3.0-or-later
"""Declared frame-script fixtures, retaining a source bank's valid art and object definitions."""
from pathlib import Path
import json,struct,hashlib,sys
from pack_actions import OPS
base,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True);raw=bytearray(base.read_bytes());assert raw[:8]==b'BVNFGT14'
frames,hits,events,pixels,clips,strikes=struct.unpack_from('<4I2H',raw,8);proj,attackers,textures,tiles=struct.unpack_from('<HHII',raw,40);assert attackers>=2 and clips>=48
bindings=24;header=54;clip_at=header+bindings*18;frame_at=clip_at+clips*8;event_at=frame_at+frames*48+hits*20+strikes*24;attacker_at=event_at+events*12+proj*92
keys=[struct.unpack_from('<I',raw,attacker_at+i*24+20)[0] for i in range(attackers)]
table_end=attacker_at+attackers*24+textures*12+tiles*20;assert (table_end+15)//16*16==pixels
# [name, argument] tapes. Queries conditionally execute their following shine.
tapes=[
 [('summon',0),('dash',1),('summon',1),('dash',0)],
 [('dash',1),('summon',0),('dash',0),('summon',1)],
 [('summon',0),('has',0),('shine',0xffffff)],
 [('summon',0),('remove',0),('has',0),('shine',0xff0000),('dash',1)],
 [('buff',25),('summon',0),('buff',50)],
 [('summon',0),('buff',75),('summon',1)],
 [('shake',2),('summon',0),('shake',5)],
 [('summon',0),('slow',1000),('dash',1)],
 [('bisha',0),('summon',0),('endBisha',0),('dash',1)],
 [('summon',0),('bisha',0),('dash',1),('endBisha',0)],
 [('startShake',0),('summon',0),('endShake',0),('dash',1)],
 [('walk',0),('summon',0),('walk',0),('dash',0)],
 [('summon',0),('remember',0),('controlRemembered',0),('has',0),('shine',0xffffff),('remove',0),('has',0),('shine',0xff0000)],
 [('has',1),('shine',0xffffff),('summon',1),('has',1),('shine',0x123456)],
 [('dash',1),('dash',0),('dash',1),('summon',0)],
 [('summon',0),('dash',1),('remove',0),('summon',0),('has',0),('shine',0xffffff)],
 [('summon',0)]*12+[('dash',1)],
 [('dash',1)]+[('summon',1)]*10+[('dash',0)],
 [('shake',2),('shake',4),('shake',6),('summon',1)],
 [('slow',300),('summon',0),('slow',600),('summon',1)],
 [('buff',25),('dash',1),('buff',50),('summon',1)],
 [('shine',0xffffff),('fire',0),('shine',0x123456),('fire',1)],
 [('walk',0),('summon',0),('remove',0),('has',0),('shine',0xff0000),('walk',0)],
 [('bisha',0),('endBisha',0),('summon',0),('bisha',0),('summon',1),('endBisha',0)],
]
def event(name,value,query_keys=keys):
 op,command,target,x,y=0,0,65535,0,0
 if name in ['summon','fire']:op=OPS.index(name);target=value
 elif name=='dash':op=OPS.index('stageCue');command=3;x=value
 elif name=='shake':op=OPS.index('shake');x=value*2
 elif name=='shine':op=OPS.index('shine');x=value
 elif name=='slow':op=OPS.index('slow');x=value
 elif name=='walk':op=OPS.index('walk')
 elif name=='buff':op=OPS.index('fighterBuff');command=1;x=value;y=300
 elif name in ['bisha','endBisha']:op=OPS.index('cutin' if name=='bisha' else 'endCutin');command=1
 elif name in ['startShake','endShake']:op=OPS.index('shakeLoop');command=int(name=='startShake');x=2 if command else 0;y=1 if command else 0
 elif name=='has':op=OPS.index('ifAttacker');target=1;x=query_keys[value]&65535;y=query_keys[value]>>16
 elif name in ['remove','remember','controlRemembered']:
  op=OPS.index({'remove':'controlAttacker','remember':'rememberAttacker','controlRemembered':'controlRemembered'}[name]);target=value;command=1 if name=='remove' else 0;x=0
 else:raise ValueError(name)
 return struct.pack('<BBHff',op,command,target,x,y)
def write_bank(name,tapes,object_keys=keys):
 data=bytearray(raw);new=b''
 for i,key in enumerate(object_keys):struct.pack_into('<I',data,attacker_at+i*24+20,key)
 for cmd,tape in enumerate(tapes):
  struct.pack_into('<H',data,header+cmd*2,24+cmd);first=struct.unpack_from('<H',data,clip_at+(24+cmd)*8)[0]
  struct.pack_into('<I',data,frame_at+first*48+40,events+len(new)//12);struct.pack_into('<H',data,frame_at+first*48+46,len(tape));new+=b''.join(event(*entry,query_keys=object_keys) for entry in tape)
 prefix=data[:event_at+events*12]+new+data[event_at+events*12:table_end];new_pixels=(len(prefix)+15)//16*16
 prefix+=bytes(new_pixels-len(prefix));struct.pack_into('<I',prefix,16,events+len(new)//12);struct.pack_into('<I',prefix,20,new_pixels)
 (out/name).write_bytes(prefix+data[pixels:])
write_bank('EMITTER.BVF',tapes)
lookup_tapes=[
 [('summon',0)],[('has',0),('shine',0xffffff)],[('remove',0)],
 [('remember',0)],[('controlRemembered',0)],[('summon',1)],
 [('has',1),('shine',0x123456)],[('remember',1)],[('remove',1)],
 [('controlRemembered',1)],
]+[[('dash',0)] for _ in range(14)]
lookup_keys=keys.copy();lookup_keys[1]=0x7b52b1c3;assert lookup_keys[1] not in keys
write_bank('TEAMMATE.BVF',lookup_tapes,lookup_keys)
# Deliberate root-assist/name collision, retaining every original assist script and image.
assist_base=base.parent/'AYLS.BVS';assist=bytearray(assist_base.read_bytes())
af,ah,ae,ap,ac,ast=struct.unpack_from('<4I2H',assist,8);apr=struct.unpack_from('<H',assist,40)[0]
aat=52+22*18+ac*8+af*48+ah*20+ast*24+ae*12+apr*92
struct.pack_into('<I',assist,aat+20,keys[0]);(out/'ASSROOT.BVS').write_bytes(assist)
report={'bank':str(base),'bank_sha256':hashlib.sha256(base.read_bytes()).hexdigest(),'fixture_sha256':hashlib.sha256((out/'EMITTER.BVF').read_bytes()).hexdigest(),'attacker_keys':keys,'templates':tapes,'lookup_templates':lookup_tapes,'lookup_attacker_keys':lookup_keys,'lookup_key_override':'Definition 1 receives a distinct declared fixture name/key; original Hinata definitions 0 and 1 share a name. All other original definitions, art and scripts retained.','root_collision_base':str(assist_base),'root_collision_base_sha256':hashlib.sha256(assist_base.read_bytes()).hexdigest(),'root_collision_fixture_sha256':hashlib.sha256(assist).hexdigest(),'lookup_fixture_sha256':hashlib.sha256((out/'TEAMMATE.BVF').read_bytes()).hexdigest(),'scope':'Declared synthetic scripts in the first frame of 24 existing clips and command bindings. Original art, strikes, projectiles, attackers and all other scripts retained. Component fixture only; not a modified main-game package or full Flash timeline proof.'}
(out/'fixture.json').write_text(json.dumps(report,indent=2)+'\n');print('Emission templates',len(tapes),'lookup templates',len(lookup_tapes))
