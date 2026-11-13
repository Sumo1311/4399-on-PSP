// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
import {methodAdapter} from './source_method_adapter.mjs';
const [reference,output]=process.argv.slice(2);if(!output)throw Error('REFERENCE OUTPUT.json');
const shared='upstream/CORE_Shared/src/net/play5d/game/bvn/',kernel='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/';
const files={mc:kernel+'fighter/FighterMC.as',hurt:kernel+'fighter/ctrler/mc/FighterMcHurtCtrl.as',ctrl:kernel+'fighter/ctrler/FighterMcCtrler.as',main:kernel+'fighter/FighterMain.as',action:kernel+'fighter/FighterAction.as',config:kernel+'GameConfig.as',state:shared+'data/fighter/FighterActionState.as',fly:shared+'data/fighter/FighterHurtFlyType.as',pose:shared+'data/fighter/FighterSpecialFrame.as',floor:shared+'data/fighter/FighterHitFloorType.as',hit:shared+'data/HitType.as'};
const source=Object.fromEntries(Object.entries(files).map(([k,p])=>[k,fs.readFileSync(path.join(reference,p),'utf8')]));
const constants=key=>Object.fromEntries([...source[key].matchAll(/public static (?:const|var)\s+(\w+)\s*:\s*(?:int|uint|Number|String)\s*=\s*([^;]+);/g)].filter(x=>/^[\d\s.*+\-]+$|^'[^']*'$|^"[^"]*"$/.test(x[2].trim())).map(x=>[x[1],Function('return ('+x[2]+')')()]));
const GameConfig=constants('config'),FighterActionState=constants('state'),FighterHurtFlyType=constants('fly'),FighterSpecialFrame=constants('pose'),FighterHitFloorType=constants('floor'),HitType=constants('hit');
FighterActionState.isHurting=s=>[21,22,23,24].includes(s);HitType.isHeavy=()=>false;
const FighterEvent={HURT_DOWN:'down',DEAD:'dead',HURT:'hurt',HURT_RESUME:'resume'},FighterEventDispatcher={dispatchEvent(f,e){if(e==='down')++f.downEvents;if(e==='dead')++f.deadEvents;}};
const globals={GameConfig,FighterActionState,FighterHurtFlyType,FighterSpecialFrame,FighterHitFloorType,HitType,FighterEvent,FighterEventDispatcher,FighterVoice:{HURT:1,HURT_FLY:2,DIE:3},int:x=>Math.trunc(x),Math,ThrowError:e=>{throw e;},GetLang:x=>x};
const method=methodAdapter(source,globals),methods=(key,names)=>Object.fromEntries(names.map(n=>[n,method(key,n)]));
const mcMethods=methods('mc',['goFrame','playHurtFly','playHurtDown','playHurtDown2','renderHurtFly','renderAnimate','renderChildren']);
const hurtMethods=methods('hurt',['doHurtAnimate','renderHurtAnimate','hurtResume']);
const clearState=method('action','clearState'),hurtFly=method('ctrl','hurtFly'),die=method('main','die');
const poseKeys=['NORMAL','HURT','HURT_FLY','HURT_FALL','HURT_TAN','HURT_DOWN','HURT_DOWN_RESUME'];
const poseId=name=>poseKeys.findIndex(k=>(k==='NORMAL'?'normal':FighterSpecialFrame[k])===name);
const rows=[];
for(const hz of [30,60])for(let scenario=0;scenario<48;++scenario){
 GameConfig.FPS_GAME=hz;const cycle=2+scenario%11;
 const action={clearState};action.clearState();
 const f={hp:1000,isAlive:true,isInAir:false,y:0,direct:1,actionState:0,vx:0,vy:0,dx:0,dy:0,sx:0,sy:0,downEvents:0,deadEvents:0,
  setVelocity(x=0,y=0){this.vx=x;this.vy=y;},setVecY(y){this.vy=y;},setDamping(x=0,y=0){this.dx=x;this.dy=y;},getVec2(){return {x:this.sx,y:this.sy};},getCtrler(){return {getVoiceCtrl(){return {playVoice(){}};}};},die};
 const child={currentFrame:1,currentFrameLabel:'',nextFrame(){this.currentFrame++;},gotoAndStop(n){this.currentFrame=n;}};
 const m={...mcMethods,_fighter:f,_hurtFlyType:0,_hurtFlyFrame:0,_hurtDownFrame:0,_hurtYMin:0,_hity:0,_isHeavyDownAttack:false,
  _curFrameCount:0,_curMainFrameCount:0,_renderMainAnimate:false,_renderMainAnimateFrame:0,_goFrameDelay:null,_curFrameName:'normal',
  _childAnimList:[child],_childAnimTotals:[cycle],ensureChildAnimCache(){},findBodyArea(){},findHitArea(){}};
 let mainPhase=0,impactCount=0,impactType=-1,impactPower=0,touches=0,resumes=0;
 m._mc={gotoAndStop(name){mainPhase=0;child.currentFrame=1;},nextFrame(){++mainPhase;}};
 const effects={endShadow(){},endShake(){},hitFloor(t,p){++impactCount;impactType=t;impactPower=p;}};
 const h={...hurtMethods,_fighter:f,_mc:m,_action:action,_hurtHoldFrame:0,_isTouchFloor:true};
 const owner={_rt:{mc:m,fighter:f,action},effectCtrler:effects,setSteelBody(){},hurtFly,touchFloor(){++touches;},idle(){++resumes;action.clearState();f.actionState=0;m.goFrame('normal');},_hurtCtrl:{resetHurtDownFrame(){}}};
 m._mcCtrler=owner;h._owner=owner;f._fighterCtrl={getMcCtrl(){return {getFighterMc(){return m;}};}};
 for(let tick=0;tick<200;++tick){
  // Supplied floor/secondary-velocity inputs, independent of MC methods.
  const air=(tick>1&&tick<35+scenario%13)||(scenario%5===0&&tick>55&&tick<77);
  f.isInAir=air;f.y=air?320-(tick%29)*13.5:320+scenario%3*80;f.sx=scenario%2?-1.25:2.5;f.sy=scenario%3===0?-2.5:1.75;
  let op=0,type=scenario%2,hitType=scenario%7===0?11:1,ms=[0,100,333,900][scenario%4],x=[-4,0,6][scenario%3],y=[-12,0,3,5,6,12][scenario%6];
  if(tick===0)op=scenario%8===0?3:1;
  if(tick===11&&scenario%3===0){op=1;type=0;ms=100;}
  if(tick===51&&scenario%4===0){op=1;type=1;y=8;}
  if(tick===8&&scenario%4===1||tick===63&&scenario%8===6)op=3;
  if(tick===88&&scenario%6===2)op=4;
  if(op===1){if(tick===0&&scenario%4===2){f.isAlive=false;f.hp=0;}h.doHurtAnimate({hurtType:type,hitType,hurtTime:ms,hitx:x,hity:y,owner:{direct:scenario%2?-1:1}},null);}
  if(op===3)f.die();if(op===4)owner.idle();
  const animate=(hz===30||tick%2===0)&&!(tick>=18&&tick<23)&&!(scenario%9===0&&tick>=67&&tick<74);
  if(animate){m.renderAnimate();if(action.isHurting)h.renderHurtAnimate();}
  const p=poseId(m._curFrameName),active=p!==0||m._hurtFlyType!==0||action.isHurting||action.isHurtFlying;
  const d=m._goFrameDelay;
  rows.push([hz,scenario,tick,op,type,hitType,ms,x,y,+air,f.y,f.sx,f.sy,+(scenario%2?-1:1),+animate,+f.isAlive,
   f.actionState,p,m._hurtFlyType,active?m._curFrameCount:0,h._hurtHoldFrame,m._hurtFlyFrame,m._hurtDownFrame,(p?+m._renderMainAnimate:0),(p?m._renderMainAnimateFrame:0),d?(d.name?1:2):0,d?d.delay:0,+m._isHeavyDownAttack,m._hurtYMin,m._hity,+action.isHurting,+action.isHurtFlying,
   f.vx,f.vy,f.dx,f.dy,f.direct,f.downEvents,f.deadEvents,impactCount,impactType,impactPower,p?mainPhase:0,p?child.currentFrame-1:0,touches,resumes]);
 }
}
const report={rows:rows.length,columns:46,sourceHashes:Object.fromEntries(Object.values(files).map(p=>[p,crypto.createHash('sha256').update(fs.readFileSync(path.join(reference,p))).digest('hex')])),scope:'Original FighterMC goFrame/renderAnimate/renderChildren/hurt-fly/down methods, FighterMcHurtCtrl doHurtAnimate/renderHurtAnimate/hurtResume, FighterMcCtrler.hurtFly, FighterMain.die and FighterAction.clearState mechanically adapted. Supplied air/floor/secondary velocity and animation gates; declared MovieClip phase/child cache, idle and no-op geometry/audio/voice adapters. Not Flash VM, broad action control, guard lethal alive-vs-HP semantics, nonboss lethal randomization or physical motion proof.',trace:rows};
fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');fs.writeFileSync(output.replace(/\.json$/,'.IN'),rows.length+'\n'+rows.map(r=>r.join(' ')).join('\n')+'\n');console.log('Original reaction states',rows.length,rows[0].length);
