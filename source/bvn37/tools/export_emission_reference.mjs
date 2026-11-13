// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
import {methodAdapter} from './source_method_adapter.mjs';
const [reference,fixtureFile,originalFile,output]=process.argv.slice(2);if(!output)throw Error('REFERENCE FIXTURE ORIGINAL_META OUTPUT');
const fixture=JSON.parse(fs.readFileSync(fixtureFile)),original=JSON.parse(fs.readFileSync(originalFile));
const base='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/';
const files={mc:base+'fighter/ctrler/FighterMcCtrler.as',fx:base+'fighter/ctrler/FighterEffectCtrl.as',events:base+'ctrler/game_ctrls/BaseFighterEventCtrl.as',buff:base+'fighter/ctrler/FighterBuffCtrler.as',assistEvents:base+'ctrler/game_ctrls/FighterEventCtrl.as'};
const sources=Object.fromEntries(Object.entries(files).map(([k,p])=>[k,fs.readFileSync(path.join(reference,p),'utf8')]));
const fresh=original.find(r=>r.variant==='hinata');const rawFile=path.join(fresh.scripts,'hinata_fla','雏田全动作_2.as');sources.frame=fs.readFileSync(rawFile,'utf8');
let trace,actor,eventCtrl,fxCtrl,buffCtrl,children,bullets;
const record=(code,value=0,point=false)=>trace.push([code,Math.round(value),point?actor.x:0,point?actor.y:0]);
class MovieClip{constructor(name){this.name=name;}}
class FighterMain{}
class FighterAttacker{
 constructor(mc){this.mc=mc;this.name=mc.name;this.referenced=false;}
 setOwner(owner){this.team=owner.team;}init(){}
}
class Bullet{constructor(mc){this.mc=mc;this.name=mc.name;}setHitVO(){}}
class FighterBuffVO{constructor(param){this.param=param;}setHold(){}}
const EffectCtrl={I:{
 shake(x,y,time){record(1,y);},shine(color){record(3,color);},
 doEffectById(id,x,y,direct,opt,sound=true){if(id==='dash'||id==='dash_air')record(2,Number(sound),true);else if(id==='buff_effect_power')record(7,(actor.attackRate-1)*1000,true);else throw Error(id);},
 doBuffEffect(id,target,buff){if(id!=='buff_power')throw Error(id);record(8,(actor.attackRate-1)*1000,true);},
 slowDown(rate,ms){if(rate!==1.5)throw Error('rate');record(9,ms);},
 startShake(x,y){record(10,x*256+y);},endShake(){record(11);},
 bisha(){record(12);},endBisha(){record(13);},startRenderBlackBack(){}
}};
const FighterEvent={ADD_ATTACKER:1,FIRE_BULLET:2};
const GameCtrl={I:{addGameSprite(team,object){const i=children.indexOf(object.mc);if(i>=0)children.splice(i,1);if(object instanceof Bullet)++bullets;},removeGameSprite(){}}};
const globals={MovieClip,FighterMain,FighterAttacker,Bullet,FighterBuffVO,FighterEvent,GameCtrl,EffectCtrl,
 SoundCtrl:{I:{playAssetSoundRandom(){record(14);}}},GameSpriteUtil:{autoChangeSpColor(){}},
 FighterEventDispatcher:{dispatchEvent(fighter,type,params){const id=Number(params.mc.name.slice(1));record(type===1?4:17,id,true);(type===1?eventCtrl.addAttacker:eventCtrl.fireBullet).call(eventCtrl,{fighter,params});}}
};
const method=methodAdapter(sources,globals);
const mcMethods=Object.fromEntries(['addAttacker','fire','move','damping'].map(n=>[n,method('mc',n)]));
const fxMethods=Object.fromEntries(['shake','dash','shine','slowDown','startShake','endShake','walk','bisha','endBisha'].map(n=>[n,method('fx',n)]));
const eventMethods=Object.fromEntries(['addAttacker','fireBullet','getAttacker','removeAttacker','removeBullet'].map(n=>[n,method('events',n)]));
const buffMethods=Object.fromEntries(['addBuff','attackUp'].map(n=>[n,method('buff',n)]));
function reset(context,tape){
 trace=[];children=tape.filter(([op])=>op==='summon'||op==='fire').map(([op,id])=>new MovieClip((op==='summon'?'a':'p')+id));bullets=0;
 actor=Object.assign(new FighterMain(),{x:350+context*7,y:500+context%5,direct:context%2?-1:1,isInAir:false,attackRate:1,team:{id:1},setVelocity(){},setDamping(){},setAnimateFrameOut(){throw Error('fixture child unavailable');},getCtrler(){return {hitModel:{getHitVOByDisplayName:()=>({})}};}});
 eventCtrl={...eventMethods,_attackers:[]};fxCtrl={...fxMethods,_target:actor,_targetDisplay:actor,_isShakeIng:false,_inGhostStep:false,getFace:()=>null};buffCtrl={...buffMethods,_fighter:actor,_buffObj:{}};
 return {...mcMethods,_rt:{fighter:actor,mc:{getChildByName:name=>children.find(c=>c.name===name)},action:{}}};
}
const rows=[];
for(let context=0;context<32;++context)for(let cmd=0;cmd<fixture.templates.length;++cmd){const tape=fixture.templates[cmd],mc=reset(context,tape);
 for(let i=0;i<tape.length;++i){const [op,value]=tape[i],name='a'+value;
  switch(op){
   case 'summon':mc.addAttacker(name);break;
   case 'fire':mc.fire('p'+value);break;
   case 'dash':fxCtrl.dash(!!value);break;
   case 'shake':fxCtrl.shake(value,value);break;
   case 'shine':fxCtrl.shine(value);break;
   case 'slow':fxCtrl.slowDown(value/1000);break;
   case 'buff':buffCtrl.attackUp(value/100,5);break;
   case 'bisha':fxCtrl.bisha(false,null);break;
   case 'endBisha':fxCtrl.endBisha();break;
   case 'walk':fxCtrl.walk();break;
   case 'startShake':fxCtrl.startShake(1,2);break;
   case 'endShake':fxCtrl.endShake();break;
   case 'has':{const found=eventCtrl.getAttacker(name,1);record(6,Number(!!found));if(!found)++i;break;}
   case 'remove':case 'remember':case 'controlRemembered':{
    record(op==='remove'?5:op==='remember'?15:16,value);let found;
    if(op==='remember'){for(const a of eventCtrl._attackers)a.referenced=false;found=eventCtrl.getAttacker(name,1);if(found)found.referenced=true;}
    else{found=op==='remove'?eventCtrl.getAttacker(name,1):eventCtrl._attackers.find(a=>a.name===name&&a.referenced);if(found&&op==='remove')eventCtrl.removeAttacker(found);}break;
   }
   default:throw Error(op);
  }
 }
 rows.push({context,cmd,hz:context&2?60:30,facing:actor.direct,x:actor.x,y:actor.y,trace,attackers:eventCtrl._attackers.length,bullets});
}
// Execute the freshly decompiled authored frame in addition to the supplied tapes.
const mc=reset(0,[['summon',0]]);children[0].name='zh1tx';
const frameGlobals={...globals,parent:{$mc_ctrler:mc,$effect_ctrler:fxCtrl}};
const actualFrame=methodAdapter(sources,frameGlobals)('frame','frame446');
const savedDispatcher=globals.FighterEventDispatcher.dispatchEvent;
globals.FighterEventDispatcher.dispatchEvent=(fighter,type,params)=>{params.mc.name='a0';savedDispatcher(fighter,type,params);};
actualFrame.call({});const authoredTrace=trace;
if(JSON.stringify(authoredTrace)!==JSON.stringify([[1,4,0,0],[2,1,350,500],[4,0,350,500]]))throw Error('unexpected authored frame '+JSON.stringify(authoredTrace));
// Saved controller variables retain object identity per requesting fighter.
// Root Assister sprites are absent from BaseFighterEventCtrl._attackers.
const lookupRows=[];
for(const hz of [30,60])for(const team of [0,1]){
 eventCtrl={...eventMethods,_attackers:[]};children=[];const picked=[];
 const get=(name,t)=>{const a=eventCtrl.getAttacker(name,t);picked.push(a?a.mc.creator:-1);return a;};
 const add=(name,creator,t)=>eventCtrl.addAttacker({fighter:{team:{id:t}},params:{mc:Object.assign(new MovieClip(name),{creator})}});
 get('a0',team);add('a0',0,team);get('a0',team);
 const savedA=get('a0',team);let savedB=get('a0',team);
 add('a1',1,team);savedB=get('a1',team);picked.push(savedA.mc.creator);
 eventCtrl.removeAttacker(eventCtrl.getAttacker('a0',team));get('a0',team);
 add('a0',2,1-team);get('a0',team);get('a0',1-team);
 lookupRows.push({hz,team,picked,scope:'Supplied independent owners; exact pinned getAttacker/addAttacker/removeAttacker methods. Saved variables use ordinary reference identity. Root assists are outside this registry; no construction timeline callbacks.'});
}
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const report={rows:rows.length,lookupRows,sourceHashes:Object.fromEntries(Object.values(files).map(p=>[p,hash(path.join(reference,p))])),authoredFrame:{swf:fresh.swf,swf_sha256:fresh.sha256,script:rawFile,script_sha256:hash(rawFile),method:'frame446',trace:authoredTrace},fixture_sha256:fixture.fixture_sha256,scope:'Mechanically executes pinned FighterMcCtrler, FighterEffectCtrl, BaseFighterEventCtrl and FighterBuffCtrler methods for 24 declared scripts in 32 supplied contexts. Synchronous event dispatch, supplied fresh child pool sufficient for every planned request, owner/team, construction without MC frame callbacks, add/remove containers, empty velocity/damping and buff hold adapters are explicit. Includes the freshly decompiled authored Hinata frame446. Does not prove complete AVM2 dispatch, absent-child deferred retries, object construction frame scripts, movement trajectories or raster. Native full ordinary/Musou scenes tested separately.',trace:rows};
fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');fs.writeFileSync(output.replace(/\.json$/,'.IN'),rows.length+'\n'+rows.map(r=>[r.context,r.cmd,r.hz,r.facing,r.x,r.y,r.attackers,r.bullets,r.trace.length,...r.trace.flat()].join(' ')).join('\n')+'\n');console.log('Original immediate emissions',rows.length,'authored frame',JSON.stringify(authoredTrace));

fs.writeFileSync(output.replace(/\.json$/,'.TEAM.IN'),lookupRows.length+'\n'+lookupRows.map(r=>[r.hz,r.team,...r.picked].join(' ')).join('\n')+'\n');
